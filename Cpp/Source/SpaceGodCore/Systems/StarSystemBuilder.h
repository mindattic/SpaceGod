#pragma once
// Port of Assets/UnitySpace/Core/Systems/StarSystemBuilder.cs (US-T17).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "BodyDefinition.h"

namespace SpaceGod::Core
{
	class SystemSpec;
	class StarSpec;
	class StellarNode;
	class CompanionSpec;
	class PlanetSpec;
	class StationSpec;
	class OrbitalElements;

	/// <summary>A debris belt or disc about one star, in the system's own (J2000 ecliptic) axes.</summary>
	class DebrisDisc final
	{
	public:
		cs::String Name;
		cs::String HostName;
		int HostIndex = -1;
		double InnerKm = 0, OuterKm = 0;

		/// <summary>Unit normal of the disc's plane.</summary>
		DVec3 Normal;

		cs::String Summary;
	};

	/// <summary>
	/// Turns a <see cref="SystemSpec"/> (catalogue data) into the bodies of a <see cref="BodyCatalog"/>
	/// and its debris discs, the same way for every system:
	///  - a single star is the root; a pair at the root circles an invisible barycentre (the root),
	///    both members sharing their measured visual orbit, scaled by each other's share of the mass;
	///  - a wide companion (star or pair) orbits the root through the place it is seen today;
	///  - planets keep their catalogued period, semi-major axis and eccentricity exactly; the plane,
	///    node and phase are measured where the catalogue has them, assumed (and said so) where not;
	///  - each planet is flagged against its star's habitable zone (<see cref="HabitableZones"/>).
	/// </summary>
	class StarSystemBuilder
	{
	public:
		static constexpr double SunGm = 1.32712440018e11;
		static constexpr double SunRadiusKm = 695700;
		static constexpr double EarthGm = 398600.435;
		static constexpr double EarthRadiusKm = 6371.0;

		/// <summary>Above this mass (Earth masses) a planet is drawn as a gas giant.</summary>
		static constexpr double GasGiantMassEarth = 50;

		class Result final
		{
		public:
			cs::List<cs::Ref<BodyDefinition>> Bodies;
			cs::List<cs::Ref<DebrisDisc>> Discs;

			/// <summary>The plane assumed for unmeasured orientations: the root pair's orbit, or the system's reference plane.</summary>
			DVec3 ReferenceNormal;

			/// <summary>A direction in that plane: the root pair's periastron, or the plane's node.</summary>
			DVec3 ReferencePeriapsis;
		};

		static cs::Ref<Result> Build(const cs::Ref<SystemSpec>& spec);

		/// <summary>A companion's offset from the primary today (km, ecliptic J2000).</summary>
		static DVec3 CompanionOffsetKm(const cs::Ref<SystemSpec>& spec, const cs::Ref<CompanionSpec>& c);

	private:
		static cs::Ref<StarSpec> StarOf(const cs::Dictionary<cs::String, cs::Ref<StarSpec>, cs::OrdinalIgnoreCaseComparer>& stars, const cs::String& host, const cs::String& what);

		// ---------------------------------------------------------------- stars

		static void AddNode(const cs::Ref<Result>& r, const cs::Ref<SystemSpec>& spec, const cs::Ref<StellarNode>& node, const cs::String& parent, const cs::Ref<OrbitalElements>& orbit);

		static cs::Ref<BodyDefinition> Star(const cs::Ref<StarSpec>& s, const cs::String& parent, const cs::Ref<OrbitalElements>& orbit, double orbitScale, DVec3 pole);

		static void AddCompanion(const cs::Ref<Result>& r, const cs::Ref<SystemSpec>& spec, const cs::Ref<CompanionSpec>& c, const cs::String& root);

		// ---------------------------------------------------------------- planets

		static DVec3 SkyNormal(const cs::Ref<SystemSpec>& spec, double iDeg, double nodeDeg);

		static cs::Ref<BodyDefinition> Planet(const cs::Ref<Result>& r, const cs::Ref<SystemSpec>& spec, const cs::Ref<PlanetSpec>& p, const cs::Ref<StarSpec>& host);

		/// <summary>A fixed phase (degrees) from the name, for planets whose phase is not catalogued.</summary>
		static double NamePhase(const cs::String& name);

		/// <summary>
		/// A plausible colour (assumed: none of these worlds has been seen): gas giants tan, Neptune-like
		/// worlds pale blue, rocky worlds from scorched rust to temperate grey to icy white by the light they get.
		/// </summary>
		static Rgb PlanetColor(double massEarth, double flux);

		// ---------------------------------------------------------------- game layer

		static cs::Ref<BodyDefinition> Station(const cs::Ref<Result>& r, const cs::Ref<StationSpec>& s, const cs::Ref<StarSpec>& host);
	};
}
