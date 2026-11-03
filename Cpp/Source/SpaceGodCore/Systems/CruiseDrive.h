#pragma once
// Port of Assets/UnitySpace/Core/Systems/CruiseDrive.cs (US-T17).

#include "Cs/Cs.h"
#include "Ships.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class StarSystem;
	class RangeSetting;

	/// <summary>
	/// A long-range cruise drive (U.SPC-T412): what lets a hull leave its system by its own power and cross the open
	/// space between the stars, rather than only by a jump gate. It charges (spools) at the edge of its system's
	/// gravity well, comes up to its cruise speed, holds it across the light-years, and comes down again to drop out
	/// at the edge of the far star's well, where the hull finishes the way in under its ordinary drive. Its speed is
	/// many times light's, but slow next to a gate: a gate is a shortcut, the drive is the way anywhere.
	///
	/// A drive is data, not code: a new one is a new spec registered for the hull classes that carry it
	/// (<see cref="CruiseDrives.Register"/>).
	/// </summary>
	class SPACEGODCORE_API CruiseDriveSpec final
	{
	public:
		cs::String Name;

		/// <summary>Top cruise speed, as a multiple of the speed of light (before the menu's <see cref="CruiseDrives.SpeedScale"/>).</summary>
		double CruiseC = 0;

		/// <summary>How long it is rated to charge at the well's edge before it engages, days.</summary>
		double RatedSpoolDays = 0;

		/// <summary>How long it charges before it engages, days: its rating times the menu's <see cref="FtlRules.SpoolScale"/>.</summary>
		double SpoolDays() const;

		/// <summary>How long it takes to come up to cruise speed (and to come down from it), days.</summary>
		double RampDays = 0;

		/// <summary>How long it is rated to cool after it drops out before it can charge again, days (U.SPC-T418).</summary>
		double RatedCooldownDays = 0;

		/// <summary>How long it cools after it drops out, days: its rating times the menu's <see cref="FtlRules.CooldownScale"/>.</summary>
		double CooldownDays() const;

		/// <summary>The share of a hull's tanks a charge burns, and the share each light-year flown burns (U.SPC-T418,
		/// before the menu's <see cref="FtlRules.FuelScale"/>; see <see cref="FtlRules.PropellantFor"/>).</summary>
		double ChargePropellantShare = 0;
		double PropellantSharePerLy = 0;

		CruiseDriveSpec(const cs::String& name, double cruiseC, double spoolDays, double rampDays, double cooldownDays = 0,
			double chargePropellantShare = 0, double propellantSharePerLy = 0);

		/// <summary>Light-years a day at cruise speed, at the menu's speed scale now.</summary>
		double LyPerDay() const;

		cs::String ToString() const { return Name; }
	private:
		CruiseDriveSpec() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(CruiseDriveSpec); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>
	/// A cruise's speed through one leg (U.SPC-T412): charging, coming up to speed, holding it, coming down. Worked out
	/// once when the leg is set and closed-form after that, so where a group is between the stars never depends on how
	/// often it is looked at.
	/// </summary>
	struct SPACEGODCORE_API CruiseProfile
	{
		/// <summary>The leg's length, light-years; its speed at cruise, light-years a day; the ramp up and the charge, days.</summary>
		double Ly = 0, LyPerDay = 0, RampDays = 0, SpoolDays = 0;

		CruiseProfile() = default;
		CruiseProfile(double ly, double lyPerDay, double rampDays, double spoolDays);

		static CruiseProfile For(const cs::Ref<CruiseDriveSpec>& drive, double ly);

	private:
		double Accel() const { return LyPerDay / RampDays; }

		/// <summary>Whether the leg is too short to reach cruise speed (it comes straight down again from its peak).</summary>
		bool Short() const { return Ly < LyPerDay * RampDays; }

	public:
		/// <summary>Days under way once engaged (not counting the charge).</summary>
		double FlightDays() const { return Short() ? 2 * cs::Math::Sqrt(Ly / Accel()) : RampDays + Ly / LyPerDay; }

		/// <summary>Days from the start of the charge to dropping out.</summary>
		double TotalDays() const { return SpoolDays + FlightDays(); }

		/// <summary>Light-years covered <paramref name="days"/> after the charge began.</summary>
		double LyAt(double days) const;

		/// <summary>Speed <paramref name="days"/> after the charge began, light-years a day.</summary>
		double SpeedAt(double days) const;
	};

	/// <summary>How a class of hull crosses between the stars (U.SPC-T472; docs/SHIP_ROSTER.md, "Who has their own FTL").</summary>
	enum class FtlAccess : int
	{
		/// <summary>Always carries a self-contained FTL drive: carriers, gate breakers, deep-space explorers.</summary>
		Own,

		/// <summary>Carries one only when its design fits the FTL drive facility (<see cref="FacilityKinds.FtlDrive"/>), and
		/// otherwise goes by the gates: battleships, command cruisers.</summary>
		Fitted,

		/// <summary>No FTL of its own: it crosses by the jump gates, or docked on a carrier's hull: cruisers, destroyers,
		/// frigates, couriers, freighters and the other small and civil hulls.</summary>
		Gates,

		/// <summary>Crosses between the stars inside a carrier's hangar (or through a gate with its group): fighters, bombers.</summary>
		Hangar,

		/// <summary>Never leaves its home system, by gate, drive or carriage: corvettes.</summary>
		HomeSystem,
	};
	SPACEGODCORE_API const char* ToString(FtlAccess v);

	/// <summary>
	/// The owner's roster of who has their own FTL (U.SPC-T472), as data: carriers and gate breakers always have a
	/// self-contained drive; battleships and command cruisers only when the design fits one (a drive facility taking
	/// hull space, <see cref="FacilityKinds.FtlDrive"/>); cruisers, destroyers and frigates have none and travel by gate,
	/// or docked on a carrier; corvettes never leave their home system; fighters and bombers travel inside a carrier's
	/// hangar. Explorers (U.SPC-T419) are long-legged survey ships and keep their own. A new class slots in with one
	/// <see cref="Register"/> call (and, if it has a drive, <see cref="CruiseDrives.Register"/>), never an edit to the
	/// systems that move fleets between the stars, which all ask <see cref="CruiseDrives.Of(Ship)"/> and
	/// <see cref="LeavesHome"/>.
	/// </summary>
	class SPACEGODCORE_API FtlRoster
	{
		static cs::Dictionary<ShipClass, FtlAccess>& byClass();

	public:
		/// <summary>Sets how hulls of <paramref name="cls"/> cross between the stars: how a new class slots in.</summary>
		static void Register(ShipClass cls, FtlAccess access) { byClass()[cls] = access; }

		/// <summary>How hulls of <paramref name="cls"/> cross between the stars. A class not on the roster has its own drive
		/// if one is registered for it (<see cref="CruiseDrives.Register"/>), and otherwise goes by the gates.</summary>
		static FtlAccess Of(ShipClass cls);

		/// <summary>Whether a hull of <paramref name="cls"/> ever leaves its home system (a corvette never does).</summary>
		static bool LeavesHome(ShipClass cls) { return Of(cls) != FtlAccess::HomeSystem; }

		/// <summary>Whether <paramref name="ship"/> ever leaves its home system.</summary>
		static bool LeavesHome(const cs::Ref<Ship>& ship) { return ship == nullptr || ship->Spec == nullptr || LeavesHome(ship->Spec->Class); }

		/// <summary>Whether <paramref name="ship"/>'s hull has the FTL drive facility in it now: its refitted loadout's, or
		/// its design's as laid down (<see cref="FacilityLayouts.For"/>) if it was never refitted.</summary>
		static bool DriveFitted(const cs::Ref<Ship>& ship);

		/// <summary>Why <paramref name="ship"/> cannot cross between the stars under its own power, by its roster entry, or
		/// null if it has a drive.</summary>
		static cs::String NoDriveReason(const cs::Ref<Ship>& ship);
	};

	/// <summary>
	/// Which hulls carry a cruise (FTL) drive, and the menu's FTL tuning (U.SPC-T412, U.SPC-T419, U.SPC-T472). Who has a
	/// drive is the owner's roster (<see cref="FtlRoster"/>): carriers always carry the heavy capital drive, battleships
	/// and command cruisers only when their design fits the FTL drive facility, and the deep-space explorer, the hull the survey services send
	/// out past the gates, the lighter, faster survey drive. Everything else (cruisers, destroyers, fighters, couriers,
	/// breachers, freighters, haulers) cannot leave a system except by a gate, or docked aboard a hull that can
	/// (see <see cref="FtlCarriage"/>).
	/// </summary>
	// [SettingsSource]: the settings registry finds SpeedScale and WellLimit by touching their accessors.
	class SPACEGODCORE_API CruiseDrives
	{
	public:
		/// <summary>A capital ship's drive: a light-year a day, charging six hours and cooling twelve after, a charge burning
		/// 2% of the tanks and each light-year 0.2% (some 490 ly on full tanks).</summary>
		static const cs::Ref<CruiseDriveSpec>& Capital();

		/// <summary>A survey hull's drive: two light-years a day, charging under three hours and cooling six after, a charge
		/// burning 1% of the tanks and each light-year 0.1% (some 990 ly on full tanks).</summary>
		static const cs::Ref<CruiseDriveSpec>& Survey();

	private:
		static cs::Dictionary<ShipClass, cs::Ref<CruiseDriveSpec>>& byClass();

	public:
		/// <summary>Gives hulls of <paramref name="cls"/> <paramref name="drive"/> (null takes it away): how a new drive or a
		/// new hull plugs in. Whether a given hull carries it is its class's <see cref="FtlRoster"/> entry.</summary>
		static void Register(ShipClass cls, const cs::Ref<CruiseDriveSpec>& drive);

		/// <summary>The drive hulls of <paramref name="cls"/> carry (for a class that carries one only when fitted, the one
		/// it carries then), or null.</summary>
		static cs::Ref<CruiseDriveSpec> Of(ShipClass cls);

		/// <summary>The drive <paramref name="ship"/> carries, or null if it cannot cross between the stars on its own: by its
		/// class's roster entry (<see cref="FtlRoster"/>), and for a battleship whether its design has the drive fitted.</summary>
		static cs::Ref<CruiseDriveSpec> Of(const cs::Ref<Ship>& ship);

		/// <summary>The settings menu's FTL tuning: every cruise drive's speed, as a multiple of its rated speed.</summary>
		static const cs::Ref<RangeSetting>& SpeedScale();

		/// <summary>The settings menu's FTL tuning: how far out from a star a cruise drive can engage, and where it drops out.</summary>
		static const cs::Ref<RangeSetting>& WellLimit();

		/// <summary>How far from <paramref name="system"/>'s root a cruise engages and drops out, km.</summary>
		static double WellLimitKm(const cs::Ref<StarSystem>& system);
	};

	/// <summary>
	/// What a hull with an FTL drive can take with it docked aboard (U.SPC-T419): carriers, and battleships and command cruisers
	/// with a drive fitted, have hangar berths, and fighters and small craft that cannot cross between the stars on their own ride in
	/// them, so a fleet can strike or explore off the gate network with its wings. Cruisers and destroyers have no drive
	/// and no berths (U.SPC-T472): they cross by the gates, or clamped to a carrier's hull. A carrier, besides its hangars,
	/// has docking clamps on the outside of its hull (U.SPC-T473) that take a few escorts too big for a berth (destroyers
	/// and frigates; never a corvette, which does not leave its home system, U.SPC-T476) through its own jump, so a jump strike group is a carrier, the
	/// capitals with drives of their own, and the escorts clamped to it. Data, not code: a hull's berths and clamps and a
	/// craft's size are registered (<see cref="RegisterBerths"/>, <see cref="RegisterCraft"/>, <see cref="RegisterClamps"/>,
	/// <see cref="RegisterClampable"/>), never threaded through the cruise.
	/// </summary>
	class SPACEGODCORE_API FtlCarriage
	{
		/// <summary>Hangar berths each class of drive-carrying hull has for small craft.</summary>
		static cs::Dictionary<ShipClass, int>& berths();

		/// <summary>How many berths a craft of each class takes up; a class not listed cannot be carried.</summary>
		static cs::Dictionary<ShipClass, int>& sizes();

		/// <summary>Docking clamps on the outside of each class of drive-carrying hull (U.SPC-T473): a carrier's four take
		/// two destroyers or four frigates.</summary>
		static cs::Dictionary<ShipClass, int>& clamps();

		/// <summary>How many clamps an escort of each class takes up; a class not listed is never clamped to a hull.</summary>
		static cs::Dictionary<ShipClass, int>& clampSizes();

	public:
		/// <summary>Gives hulls of <paramref name="cls"/> <paramref name="count"/> berths (0 takes them away).</summary>
		static void RegisterBerths(ShipClass cls, int count);

		/// <summary>Lets craft of <paramref name="cls"/> be carried, taking <paramref name="size"/> berths (0 says it cannot be).</summary>
		static void RegisterCraft(ShipClass cls, int size);

		/// <summary>Gives hulls of <paramref name="cls"/> <paramref name="count"/> docking clamps on the outside of the hull
		/// (0 takes them away, U.SPC-T473).</summary>
		static void RegisterClamps(ShipClass cls, int count);

		/// <summary>Lets escorts of <paramref name="cls"/> ride clamped to a hull, taking <paramref name="size"/> clamps (0 says
		/// they cannot be): how a frigate slotted in (U.SPC-T473, U.SPC-T475).</summary>
		static void RegisterClampable(ShipClass cls, int size);

		/// <summary>Berths hulls of <paramref name="cls"/> have.</summary>
		static int BerthsOf(ShipClass cls);

		/// <summary>Berths <paramref name="ship"/> has to carry craft through FTL: none without a drive of its own to carry them on.</summary>
		static int BerthsOf(const cs::Ref<Ship>& ship);

		/// <summary>Docking clamps hulls of <paramref name="cls"/> have on the outside of the hull.</summary>
		static int ClampsOf(ShipClass cls);

		/// <summary>Clamps <paramref name="ship"/> has to take escorts through FTL: none without a drive of its own.</summary>
		static int ClampsOf(const cs::Ref<Ship>& ship);

		/// <summary>Berths a craft of <paramref name="cls"/> takes up, or 0 if it cannot be carried in a hangar.</summary>
		static int SizeOf(ShipClass cls);

		/// <summary>Clamps an escort of <paramref name="cls"/> takes up, or 0 if it is never clamped to a hull.</summary>
		static int ClampSizeOf(ShipClass cls);

		/// <summary>Whether a hull of <paramref name="cls"/> rides clamped to the outside of its carrier rather than in a hangar.</summary>
		static bool RidesClamped(ShipClass cls) { return SizeOf(cls) == 0 && ClampSizeOf(cls) > 0; }

		/// <summary>Whether <paramref name="craft"/> rides clamped to the outside of its carrier rather than in a hangar.</summary>
		static bool RidesClamped(const cs::Ref<Ship>& craft) { return craft != nullptr && craft->Spec != nullptr && RidesClamped(craft->Spec->Class); }

		/// <summary>Whether <paramref name="craft"/> needs carrying (no drive of its own) and can be (it fits a berth, or a clamp).</summary>
		static bool Carriable(const cs::Ref<Ship>& craft);

		/// <summary>
		/// Docks as many of <paramref name="craft"/> as fit aboard <paramref name="carriers"/> (the hulls going under
		/// their own drives), each in the first with berths (or, for an escort too big for a hangar, clamps) to spare, in
		/// the order given: what goes aboard which. Craft that need no carrying, cannot be carried or find no room are
		/// left out (they stay behind).
		/// </summary>
		static cs::Dictionary<cs::Ref<Ship>, cs::Ref<Ship>> Stow(const cs::List<cs::Ref<Ship>>& carriers, const cs::List<cs::Ref<Ship>>& craft);

		/// <summary>How many of <paramref name="aboard"/> (craft to the hull carrying them) ride clamped to the hull rather than
		/// docked in a hangar.</summary>
		static int CountClamped(const cs::List<cs::Ref<Ship>>& aboard);

		/// <summary>How a group going through FTL is made up, for the log: ", 2 escorts alongside, 6 craft docked aboard and 1
		/// escort clamped to the hull", or empty for a hull going alone.</summary>
		static cs::String Describe(int alongside, int docked, int clamped);
	};
}
