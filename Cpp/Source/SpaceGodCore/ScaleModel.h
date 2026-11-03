#pragma once
// Port of Unity.SpaceGod Core/ScaleModel.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class BodyCatalog;
	class BodyDefinition;
	class Ephemeris;
	class Hyperlanes;
	class Lane;
	class Ship;

	/// <summary>
	/// Maps true (km) geometry into scene units, blending between two presentations:
	///
	///  - Realistic (Blend = 0): one linear scale for everything, 1 unit = 1000 km. The planets are
	///    specks; Neptune is 4.5 million units out.
	///  - Gameplay (Blend = 1): radii and distances are each compressed by power laws so the system
	///    is navigable and the planets read as worlds rather than dots.
	///
	/// Every mapping is radial about a parent (directions are never changed, only distances), so
	/// orbit shapes, phases and alignments stay true in both modes. Each mapping also sends a
	/// parent's surface to its displayed surface (x = 1 → 1 display radius), so things in low orbit
	/// stay outside the body at every blend. Intermediate blends interpolate in log space, which
	/// reads as a smooth zoom across the factor-of-40 differences.
	/// </summary>
	class SPACEGODCORE_API ScaleModel final
	{
	public:
		static constexpr double KmPerUnit = 1000.0;

		// Gameplay radii: Earth = 255 units, others by (R / R_earth)^0.45.
		static constexpr double GameEarthRadius = 255.0;
		static constexpr double GameRadiusExponent = 0.45;

		// Gameplay heliocentric distance: 20000 units per AU^0.55 from the solar surface.
		static constexpr double GameAuScale = 20000.0;
		static constexpr double GameAuExponent = 0.55;

		// Gameplay distance around a planet: R_display · (1 + c·(x - 1)^p), x in body radii.
		static constexpr double GameLocalC = 1.17;
		static constexpr double GameLocalP = 0.44;

		// Gameplay ship length: 12 units per km^0.5 (a 3 km carrier is ~21 units against Earth's 255 radius).
		static constexpr double GameShipScale = 12.0;
		static constexpr double GameShipExponent = 0.5;

	private:
		static constexpr double EarthRadiusKm = 6371.0;

		double blend = 1.0;

	public:
		/// <summary>0 = realistic, 1 = gameplay.</summary>
		double Blend() const { return blend; }
		void SetBlend(double value) { blend = cs::Math::Max(0.0, cs::Math::Min(1.0, value)); }

		static double LogLerp(double a, double b, double t);

		static double RealisticRadius(double radiusKm) { return radiusKm / KmPerUnit; }

		static double GameplayRadius(double radiusKm);

		double DisplayRadius(double radiusKm) const;

		double DisplayRadius(const cs::Ref<BodyDefinition>& body) const;

		double ShipLength(double lengthKm) const;

		/// <summary>Scene distance from the centre of <paramref name="body"/> for a true distance in km.</summary>
		double LocalDistance(const cs::Ref<BodyDefinition>& body, double distanceKm) const;

		static double GameplayLocalDistance(const cs::Ref<BodyDefinition>& body, double distanceKm);

		/// <summary>Inverse of <see cref="LocalDistance"/> (monotonic, so a bisection in log space is exact enough).</summary>
		double InverseLocalDistance(const cs::Ref<BodyDefinition>& body, double sceneDistance) const;

		/// <summary>d(scene distance)/d(true km) at a true distance; used to convert camera motion.</summary>
		double LocalDistanceDerivative(const cs::Ref<BodyDefinition>& body, double distanceKm) const;

		/// <summary>Scene offset (Unity axes) from a body for a true ecliptic offset from it.</summary>
		DVec3 LocalOffset(const cs::Ref<BodyDefinition>& body, DVec3 trueOffsetKm) const;

		/// <summary>Inverse of <see cref="LocalOffset"/>: a scene offset (Unity axes) to a true ecliptic offset (km).</summary>
		DVec3 InverseLocalOffset(const cs::Ref<BodyDefinition>& body, DVec3 sceneOffset) const;

		/// <summary>
		/// Lays every body out in scene space (Unity axes, Sun at the origin) from the ephemeris.
		/// Each body is placed relative to its parent's already-placed position.
		/// </summary>
		void Layout(const cs::Ref<Ephemeris>& ephemeris, cs::List<DVec3>& scenePositions) const;

		/// <summary>
		/// Out to this many body radii, a point is placed with the body's own local mapping, exactly
		/// as parked ships are. Parking orbits go out to 7 radii.
		/// </summary>
		static constexpr double NeighbourhoodRadii = 8.0;

		/// <summary>A planet's neighbourhood also reaches this far past its outermost moon, which is laid out locally.</summary>
		static constexpr double NeighbourhoodMoonMargin = 1.25;

		/// <summary>How far (km) from a body points are placed with its own local mapping.</summary>
		static double NeighbourhoodKm(const cs::Ref<BodyDefinition>& body);

		/// <summary>
		/// Scene position (Unity axes) of a true heliocentric point (km), seen from the neighbourhood
		/// of <paramref name="body"/>: the Sun's mapping, corrected by each body from the top of
		/// <paramref name="body"/>'s chain down (see <see cref="MapThrough"/>).
		/// </summary>
		/// <param name="scene">Body scene positions from <see cref="Layout"/>, for the ephemeris' current date.</param>
		DVec3 MapNear(const cs::Ref<Ephemeris>& ephemeris, const cs::List<DVec3>& scene, int body, DVec3 helioKm) const;

	private:
		/// <summary>
		/// The Sun's mapping of a true heliocentric point, then each body's neighbourhood correction in
		/// order. Within a body's neighbourhood (<see cref="NeighbourhoodKm"/>) its correction is its
		/// own local mapping, exactly as parked ships and its moons are placed; beyond, it eases back
		/// into the mapping so far, staying at least as far out as the neighbourhood edge so a point
		/// never dips into the drawn body. The last body in the list wins near it. At realistic scale
		/// every mapping is the same linear one, so this is just km / KmPerUnit.
		/// </summary>
		DVec3 MapThrough(const cs::Ref<Ephemeris>& ephemeris, const cs::List<DVec3>& scene, const cs::List<int>& bodies, int count, DVec3 helioKm) const;

		/// <summary>Appends <paramref name="body"/> and its ancestors below the Sun, outermost first, skipping any already listed.</summary>
		static void AddChain(const cs::Ref<BodyCatalog>& catalog, int body, cs::List<int>& into);

	public:
		/// <summary>
		/// Scene position of a transiting ship at a fraction of its path: its true position, mapped
		/// with the neighbourhood corrections of both ends and the planets they orbit. The origin's
		/// correction is applied last at departure and the destination's by arrival, crossfading
		/// between, so the ship leaves and joins its parking slots exactly and stays out of both
		/// drawn bodies. In open space every correction has eased out, so between worlds the ship
		/// flies its true path as the scale model draws it.
		/// </summary>
		DVec3 TransitScene(const cs::Ref<Ephemeris>& ephemeris, const cs::List<DVec3>& scene, const cs::Ref<Ship>& ship, double progress, DVec3& travelDirection) const;

	private:
		/// <summary>The end body's correction must be applied last, even when it is an ancestor of the other end's.</summary>
		static void MoveLast(cs::List<int>& chain, int body);

	public:
		/// <summary>
		/// Scene position of a ship riding a hyperlane: the lane's curve drawn between its two ramps as
		/// the scale model places them (each in its planet's neighbourhood), so the ship meets the
		/// burns at either end exactly. The Sun keep-out and bow are scaled into scene units.
		/// </summary>
		DVec3 LaneScene(const cs::Ref<Ephemeris>& ephemeris, const cs::List<DVec3>& scene, const cs::Ref<Hyperlanes>& lanes, const cs::Ref<Ship>& ship, double progress, DVec3& travelDirection) const;

		/// <summary>
		/// A point on a lane drawn in scene space between ramp points given in true km (each mapped in
		/// its planet's neighbourhood), bowed from the lane's hub and swung round the same star as the
		/// true-km curve, with the keep-out scaled into scene units by that star's own mapping.
		/// </summary>
		DVec3 LaneCurveScene(const cs::Ref<Ephemeris>& ephemeris, const cs::List<DVec3>& scene, const cs::Ref<Hyperlanes>& lanes, const cs::Ref<Lane>& lane, int from, DVec3 aKm, int to, DVec3 bKm,
			double progress, DVec3& travelDirection, int star = -1, int shape = -1) const;

		/// <summary>Scene radius of a body's sphere of influence, used for camera reference frames.</summary>
		double SceneSoi(const cs::Ref<BodyDefinition>& body) const;
	};
}
