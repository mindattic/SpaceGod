#pragma once
// Port of Assets/UnitySpace/Core/Systems/FtlRules.cs (US-T17).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "GalacticPosition.h"
#include "Systems/ProceduralSystems.h"

namespace SpaceGod::Core
{
	class Faction;
	class StarSystem;
	class BodyDefinition;
	class CruiseDriveSpec;
	class Ship;
	class RangeSetting;
	struct NearbyStar;

	/// <summary>What made a gravity field: a body's own mass, or a machine built to throw one.</summary>
	enum class GravityFieldKind : int
	{
		/// <summary>A star's or a planet's mass: the field is where its pull is above <see cref="FtlRules.WellGravity"/>.</summary>
		Mass,

		/// <summary>A gravity projector: a station (or anything else) throwing a field to pull hulls out of FTL.</summary>
		Projector,
	};
	const char* ToString(GravityFieldKind v);

	/// <summary>A sphere an FTL drive will not hold in (U.SPC-T418): a body's well, or a projector's field.</summary>
	struct GravityField
	{
		cs::String Name;
		GravityFieldKind Kind = GravityFieldKind::Mass;
		GalacticPosition Center;
		double RadiusKm = 0;

		/// <summary>Whose it is (a projector's); null for a body's mass, which pulls on everyone.</summary>
		cs::Ref<Faction> Owner;

		GravityField() = default;
		GravityField(const cs::String& name, GravityFieldKind kind, GalacticPosition center, double radiusKm, const cs::Ref<Faction>& owner = nullptr);

		/// <summary>Whether <paramref name="place"/> is inside it.</summary>
		bool Contains(GalacticPosition place, double slack = 1) const { return place.KmFrom(Center).Length() < RadiusKm * slack; }

		cs::String ToString() const { return Name; }
	};

	/// <summary>
	/// A gravity projector out in open space (U.SPC-T418): a field of <see cref="RadiusKm"/> about <see cref="Position"/>
	/// that pulls any hull not its owner's out of FTL at its edge, to cross it under its ordinary drive. Placed with
	/// <see cref="DeepSpace.AddProjector"/>.
	/// </summary>
	class GravityProjector final
	{
	public:
		cs::String Name;
		cs::Ref<Faction> Owner;
		GalacticPosition Position;
		double RadiusKm = 0;

		/// <summary>Switched off, it pulls nobody out.</summary>
		bool Active = true;

		GravityField Field() const { return GravityField(Name, GravityFieldKind::Projector, Position, RadiusKm, Owner); }

		cs::String ToString() const { return Name; }
	};

	/// <summary>
	/// The physics and rules of FTL (U.SPC-T418), the same for the helm's own drive and every group crossing between the
	/// stars, every limit data-tunable here and in the settings menu's FTL group:
	///
	///  - Gravity wells. A drive cannot engage, and drops out, inside a well: where a body's pull is above
	///    <see cref="WellGravity"/> (a well's radius is sqrt(GM / g), so a star's is a couple of AU, Jupiter's ten million
	///    km, Earth's half a million). Arrivals end at the edge of a system (its outer rim, <see cref="CruiseDrives.WellLimitKm"/>)
	///    and clear of any planet's well, and finish under the ordinary drive.
	///  - Interdiction. A group under way that runs into a star's well, or a gravity projector's field, on its way is pulled
	///    out at the edge, crosses it under its ordinary drive at <see cref="CrossingSpeedKmS"/>, and spools again on the far side.
	///  - Spool-up and cooldown. Each drive charges for its <see cref="CruiseDriveSpec.SpoolDays"/> and, after it drops out,
	///    cannot charge again for its <see cref="CruiseDriveSpec.CooldownDays"/> (both scaled by the menu).
	///  - Power and fuel. A charge burns a share of the tanks, and every light-year a little more
	///    (<see cref="PropellantFor"/>); the helm will not spool without it.
	///  - Momentum. The hull's own velocity going in is the velocity it comes out with (<see cref="Cruise.CarriedVelocityKmS"/>),
	///    and the drive flies a straight line: it does not turn, only drops out.
	///  - Top speed. Each drive's rated speed, times <see cref="CruiseDrives.SpeedScale"/>: at the default a capital
	///    drive crosses the galaxy in centuries; at the menu's top, in a few years (<see cref="GalaxyCrossingDays"/>).
	/// </summary>
	// [SettingsSource]: the settings registry finds the RangeSettings by touching their accessors.
	class FtlRules
	{
	public:
		/// <summary>The Sun's gravitational parameter, km³/s².</summary>
		static constexpr double SunGm = 1.32712440018e11;

		/// <summary>The galaxy's width the top speed is measured against, light-years (the disc's diameter).</summary>
		static constexpr double GalaxyWidthLy = 2 * GalacticFrame::DiscRadiusLy;

		static const cs::Ref<RangeSetting>& WellGravity();
		static const cs::Ref<RangeSetting>& SpoolScale();
		static const cs::Ref<RangeSetting>& CooldownScale();
		static const cs::Ref<RangeSetting>& FuelScale();
		static const cs::Ref<RangeSetting>& CrossingSpeedKmS();

		// ---------------------------------------------------------------- wells

		/// <summary>The threshold pull, km/s².</summary>
		static double ThresholdKmS2();

		/// <summary>How far out a body of <paramref name="gm"/> (km³/s²) pulls harder than the threshold, km: sqrt(GM / g).</summary>
		static double WellRadiusKm(double gm);

		/// <summary>As <see cref="WellRadiusKm(double)"/>, for a threshold of <paramref name="mmS2"/> mm/s².</summary>
		static double WellRadiusKm(double gm, double mmS2) { return gm > 0 && mmS2 > 0 ? cs::Math::Sqrt(gm / (mmS2 * 1e-6)) : 0; }

		/// <summary>A star's rough mass from its spectral class, in Suns (a plain star on the charts carries no mass of its own).</summary>
		static double SolarMasses(const cs::String& spectralType);

		/// <summary>The mass (GM, km³/s²) a running system's stars hold together.</summary>
		static double StarsGm(const cs::Ref<StarSystem>& system);

		/// <summary>The well of a running system's stars, about its root.</summary>
		static GravityField StarWell(const cs::Ref<StarSystem>& system);

		/// <summary>The well of a plain star on the charts.</summary>
		static GravityField StarWell(const NearbyStar& star);

		/// <summary>
		/// The body (other than the system's root, whose well is the outer rim's business) whose well <paramref name="localKm"/>
		/// (km from <paramref name="here"/>'s root, the ephemeris as it stands) is inside, or null: a planet, a moon, or one
		/// of a multiple's stars.
		/// </summary>
		static cs::Ref<BodyDefinition> BodyWellAt(const cs::Ref<StarSystem>& here, DVec3 localKm);

		/// <summary>As <see cref="BodyWellAt"/>, the body's index in the catalogue, or -1.</summary>
		static int BodyWellIndex(const cs::Ref<StarSystem>& here, DVec3 localKm);

		/// <summary>
		/// <paramref name="localKm"/> moved out of any planet's or moon's well it is in, straight away from that body to just
		/// past its edge (where an arrival drops out, to finish under the ordinary drive).
		/// </summary>
		static DVec3 ClearOfBodyWells(const cs::Ref<StarSystem>& here, DVec3 localKm);

		// ---------------------------------------------------------------- drive costs

		/// <summary>
		/// The reaction mass (tonnes) <paramref name="ship"/> burns for a jump of <paramref name="ly"/> light-years on
		/// <paramref name="drive"/>: the charge's share of its tanks and the per-light-year share, times <see cref="FuelScale"/>.
		/// </summary>
		static double PropellantFor(const cs::Ref<CruiseDriveSpec>& drive, const cs::Ref<Ship>& ship, double ly);

		/// <summary>How far (ly) <paramref name="ship"/>'s tanks as they stand take it on <paramref name="drive"/> in one jump.</summary>
		static double RangeLy(const cs::Ref<CruiseDriveSpec>& drive, const cs::Ref<Ship>& ship);

		/// <summary>When <paramref name="ship"/>'s drive is cool enough to charge again.</summary>
		static double CoolUntil(const cs::Ref<Ship>& ship);

		/// <summary>Starts <paramref name="ship"/>'s drive cooling at <paramref name="jd"/> (it has just dropped out).</summary>
		static void StartCooldown(const cs::Ref<Ship>& ship, const cs::Ref<CruiseDriveSpec>& drive, double jd);

		/// <summary>Days for <paramref name="drive"/>, at the menu's top speed now, to cross the whole galaxy (no stops).</summary>
		static double GalaxyCrossingDays(const cs::Ref<CruiseDriveSpec>& drive);

		/// <summary>
		/// The first field among <paramref name="fields"/> the straight run from <paramref name="from"/> to <paramref name="to"/>
		/// enters (not one the run starts inside; one it ends inside is entered on the way), and how far along the run (0..1) it is entered. False if none.
		/// </summary>
		static bool FirstEntry(GalacticPosition from, GalacticPosition to, const cs::List<GravityField>& fields, GravityField& hit, double& along);

		/// <summary>Where a straight run through <paramref name="field"/> entered at <paramref name="entry"/>, heading
		/// <paramref name="dir"/> (unit), comes out the far side.</summary>
		static GalacticPosition ExitPoint(const GravityField& field, GalacticPosition entry, DVec3 dir);
	};
}
