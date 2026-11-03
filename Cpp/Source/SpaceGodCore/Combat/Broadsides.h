#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/Broadsides.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class RangeSetting;
	class ShipCombat;
	class Subsystem;
	class ToggleSetting;

	// The capital-ship broadside game (U.SPC-T425): commanding a capital ship as a naval battle, Black Flag / Rebel Galaxy
	// style. Every gun aims within its own fire cone (U.SPC-T533, see FireCones), so the captain's orders go to the guns that
	// bear where the captain looks, a volley is called and telegraphed, the gun crews go for a weakened patch of the enemy's
	// shields or spread their fire (shields are emitters on the hull, not arcs: U.SPC-T501, see ShieldEmitters), the crew
	// braces for a broadside and damage control teams patch mounts under fire and bring back shield emitters and gun mounts
	// shot away, and the flagship orders its line into formation. AI captains use the same commands by doctrine. There are no
	// bow, starboard, stern and port arcs: everything fights in full 3D, and armour angling is measured against the hull's
	// own surface where each shot strikes it (U.SPC-T534, see ArmourAngling). The handling itself
	// (engine telegraph, steerage, keel, full 3D) is the capital flight model's (U.SPC-T499, docs/FLIGHT_MODELS.md); this
	// file is everything a captain does on top of it.

	/// <summary>What a gun's crew does with a target in its fire cone (U.SPC-T425, U.SPC-T533).</summary>
	enum class BatteryOrder : int
	{
		/// <summary>Every gun fires as it bears and is loaded (what a crew does on its own).</summary>
		FireAtWill,

		/// <summary>The gun does not fire.</summary>
		HoldFire,

		/// <summary>The gun holds until every gun of the volley bearing on the same hull is loaded, then they fire together: a
		/// broadside, telegraphed to the target while it builds, surer and harder than fire at will.</summary>
		Volley,
	};

	const char* ToString(BatteryOrder v);

	/// <summary>Where a capital ship's damage control teams work first (U.SPC-T425).</summary>
	enum class DamageControlFocus : int
	{
		/// <summary>The worst-hurt mount first, whatever it is.</summary>
		Balanced,

		Weapons,

		/// <summary>Shield emitters: bringing back those shot away first, and keeping the fields of those under fire charging.</summary>
		Shields,

		Engines,
	};

	const char* ToString(DamageControlFocus v);

	/// <summary>The formations a flagship orders its line into (U.SPC-T425).</summary>
	enum class FleetFormation : int
	{
		/// <summary>No order given: escorts keep loose station as they always have.</summary>
		None,

		/// <summary>A column astern of the flagship, every broadside clear to bear on the beam.</summary>
		LineAhead,

		/// <summary>Abreast of the flagship, alternately to starboard and to port, every bow bearing ahead.</summary>
		LineAbreast,

		/// <summary>Stepped back to starboard: bows and starboard batteries both clear.</summary>
		Echelon,

		/// <summary>A ring around the flagship, to take fire and fighters off it.</summary>
		Screen,

		/// <summary>The line breaks up and each ship fights its own fight.</summary>
		EngageAtWill,
	};

	const char* ToString(FleetFormation v);

	/// <summary>
	/// A capital ship's standing orders and state in the broadside game (U.SPC-T425), on its <see cref="BattleUnit"/>: the
	/// order each of its guns fires under (given to the guns that bear where the captain looks, U.SPC-T533), whether its gun
	/// crews go for a weakened patch of the enemy's shields, its crew's brace, where damage control works, the formation it
	/// has ordered its line into, and the broadside it is building (its telegraph).
	/// </summary>
	class BroadsideCommand final
	{
		cs::List<BatteryOrder> orders;

	public:
		/// <summary>The order the mount at <paramref name="index"/> in the hull's layout fires under (fire at will unless given another).</summary>
		/// <remarks>C#'s indexer get; its set is <see cref="Set"/>.</remarks>
		BatteryOrder operator[](int index) const;

		/// <summary>C#'s indexer set (<c>cmd[index] = value</c>): gives the mount at <paramref name="index"/> an order.</summary>
		void Set(int index, BatteryOrder value);

		/// <summary>The order <paramref name="s"/> fires under.</summary>
		BatteryOrder OrderOf(const cs::Ref<Subsystem>& s) const;

		/// <summary>The gun crews spread their fire over the enemy's hull instead of going for the patch of it whose shield field
		/// is weakest (U.SPC-T501, see <see cref="ShieldEmitters.FocusWeight"/>): false (focus) unless the captain orders it.</summary>
		bool SpreadFire = false;

		/// <summary>Seconds the crew stays braced, and until it can brace again.</summary>
		double BraceLeft = 0, BraceCooldown = 0;

		bool Braced() const { return BraceLeft > 0; }

		DamageControlFocus Focus = DamageControlFocus::Balanced;

		/// <summary>The formation this hull has ordered its line into (it leads it).</summary>
		FleetFormation Formation = FleetFormation::None;

		/// <summary>The telegraph: the hull a volley is building on and the share of the guns of the volley bearing on it that
		/// are loaded (1 fires it). <see cref="ReadyingOn"/> is null while no volley is building.</summary>
		cs::Ref<BattleUnit> ReadyingOn;

		double ReadyingLoaded = 0;

		/// <summary>Whether a volley is building (the telegraph is up).</summary>
		bool Readying() const { return ReadyingOn != nullptr; }

		/// <summary>Keep the guns and drive at full however hot the hull gets (U.SPC-T491, see <see cref="Radiators"/>): past
		/// the heat sinks' limit the crew and systems take the heat instead of the crew throttling down.</summary>
		bool RunHot = false;

		/// <summary>Hit points damage control has put back this battle (for the HUD and the tests).</summary>
		double Repaired = 0;

		/// <summary>Mounts shot away that the emergency repair crews have brought back this battle (U.SPC-T501).</summary>
		int Restored = 0;

		/// <summary>The player's order at the helm (U.SPC-T502, see <see cref="WeakPatches"/>): the helm assist rolls the hull
		/// to keep its weak patches out of the heaviest fire (an AI captain does so by doctrine, <see cref="WeakPatches.AiRolls"/>).</summary>
		bool RollWeakPatches = false;

		/// <summary>The attitude the captain last settled on to shield its weak patches (U.SPC-T502): the bearing off its bow
		/// it turns the enemy onto, so the guns whose fire cones hold it there bear (U.SPC-T533,
		/// <see cref="WeakPatches.NoBearing"/> for a roll about the bow alone), and how far it is rolled from level about the
		/// line it turns on, deg; and seconds until it weighs the fire again.</summary>
		int RollBearing = cs::Int::MinValue; // WeakPatches::NoBearing (int.MinValue), spelled out to keep WeakPatches.h out of this header

		double RollDeg = 0, RollReconsider = 0;

		/// <summary>Whether <see cref="RollBearing"/> and <see cref="RollDeg"/> hold a choice yet.</summary>
		bool RollChosen = false;

		/// <summary>This step: whether the volley on each hull goes now, worked out the first time one of its guns asks.</summary>
		cs::Dictionary<cs::Ref<BattleUnit>, bool> Releasing; // internal in C#
	};

	/// <summary>The broadside game's rules (U.SPC-T425): batteries (the guns whose fire cones bear together, U.SPC-T533), focus
	/// fire on a weakened shield patch, the brace, damage control, formations and turning circles. Tunables are declared settings.</summary>
	// [SettingsSource]
	class Broadsides
	{
	public:
		/// <summary>C# RuntimeHelpers.RunClassConstructor (what SettingsRegistry does for a [SettingsSource] class): creates
		/// every static field in textual order. Every static field accessor runs it first.</summary>
		static void RunClassConstructor();

		// ------------------------------------------------------------ settings

		static const cs::Ref<RangeSetting>& BraceReduction();

		static const cs::Ref<RangeSetting>& DamageControlRate();

		static const cs::Ref<ToggleSetting>& AiVolleys();

		static const cs::Ref<ToggleSetting>& AiFocusFire();

		static const cs::Ref<ToggleSetting>& AiBraces();

		// ------------------------------------------------------------ constants

		/// <summary>The smallest hull commanded as a capital ship, km (a destroyer and up).</summary>
		static double MinCapitalKm();

		/// <summary>How long a brace lasts, and how long before the crew can brace again, s.</summary>
		static constexpr double BraceSeconds = 3; static constexpr double BraceCooldownSeconds = 12;

		/// <summary>How much harder, and surer, a crew's volley is than the same guns at will.</summary>
		static constexpr double CrewVolleyDamageBonus = 0.2; static constexpr double CrewVolleyAccuracyBonus = 0.2;

		/// <summary>How loaded the guns of a volley building on a hull must be before that hull's AI captain braces.</summary>
		static constexpr double BraceAtLoaded = 0.75;

		/// <summary>People per damage control team, the most teams any hull musters, the hit points a team puts back a
		/// second, and how whole it can make a mount in the field (a refit at a yard does the rest).</summary>
		static constexpr double CrewPerTeam = 250; static constexpr double RepairHpPerTeamS = 1.5; static constexpr double FieldRepairCap = 0.85;

		static constexpr int MaxTeams = 8;

		// ------------------------------------------------------------ who commands

		/// <summary>A hull fought as a capital ship: a ship of a destroyer's size and up.</summary>
		static bool Commands(const cs::Ref<BattleUnit>& u);

		/// <summary><paramref name="u"/>'s orders (made on first asking), or null for anything smaller than a capital ship.</summary>
		static cs::Ref<BroadsideCommand> Of(const cs::Ref<BattleUnit>& u);

		// ------------------------------------------------------------ batteries

		/// <summary>A gun that fires in broadsides: a live direct-fire mount (not a missile, point defence or a support mount).</summary>
		static bool IsBatteryGun(const cs::Ref<Subsystem>& s);

		/// <summary>A mount that takes the captain's orders: a live weapon that is not point defence, a support mount or a pod
		/// launcher (missile launchers and spinal mounts hold or fire under them like any gun).</summary>
		static bool IsOrdered(const cs::Ref<Subsystem>& s);

		/// <summary>The battery bearing along a battle-local direction off <paramref name="u"/> (where the captain looks): its
		/// guns whose fire cones take that line in (U.SPC-T533), and how many are loaded.</summary>
		/// <returns>C#'s (int loaded, int guns): first is loaded, second is guns.</returns>
		static std::pair<int, int> Battery(const cs::Ref<BattleUnit>& u, DVec3 dir);

		/// <summary>The volley bearing on <paramref name="target"/>: the guns under a volley order whose cones hold it, and how
		/// many of them are loaded.</summary>
		/// <returns>C#'s (int loaded, int guns): first is loaded, second is guns.</returns>
		static std::pair<int, int> VolleyOn(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target);

		/// <summary>
		/// The order the mounts bearing along a battle-local direction off <paramref name="u"/> fire under: theirs if they share
		/// one, null if they differ; and how many there are (0 if none bears there).
		/// </summary>
		/// <returns>C#'s (BatteryOrder? order, int mounts): first is order, second is mounts.</returns>
		static std::pair<cs::Nullable<BatteryOrder>, int> OrderAlong(const cs::Ref<BattleUnit>& u, DVec3 dir);

		/// <summary>Puts every mount bearing along a battle-local direction off <paramref name="u"/> under
		/// <paramref name="order"/>; returns how many heard it.</summary>
		static int OrderAlong(const cs::Ref<BattleUnit>& u, DVec3 dir, BatteryOrder order);

		/// <summary>
		/// Steps the battery bearing along a battle-local direction off <paramref name="u"/> (where the captain looks) to its
		/// next order (at will, hold, volley), every mount whose cone takes that line in together; returns the new order and
		/// how many mounts took it (none: no gun bears there, and nothing changes). Mounts under different orders all go to
		/// fire at will first.
		/// </summary>
		/// <returns>C#'s (BatteryOrder order, int mounts): first is order, second is mounts.</returns>
		static std::pair<BatteryOrder, int> CycleOrder(const cs::Ref<BattleUnit>& u, DVec3 dir);

		/// <summary>Puts every mount under <paramref name="order"/>.</summary>
		static void SetAll(const cs::Ref<BattleUnit>& u, BatteryOrder order);

		/// <summary>Before a hull's guns fire this step: the telegraph and last step's volleys are cleared, to be worked out
		/// again by the guns that hold or release this step (<see cref="Releases"/>, <see cref="Holding"/>).</summary>
		static void BeginVolleys(const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// Whether the volley on <paramref name="target"/> goes this step: once every gun under a volley order whose cone holds
		/// the target is loaded (with none, at once), and every loaded one goes with it. Worked out once a step, before any of
		/// them fires.
		/// </summary>
		static bool Releases(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target);

		/// <summary>A gun holds its fire for the volley building on <paramref name="target"/>: the telegraph shows it, with how
		/// loaded the guns of the volley bearing on it are.</summary>
		static void Holding(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target);

		// ------------------------------------------------------------ focus fire

		/// <summary>Toggles whether <paramref name="u"/>'s gun crews spread their fire (true) or go for a weakened shield patch
		/// (false, the default); returns whether they now spread it.</summary>
		static bool ToggleSpreadFire(const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// Whether <paramref name="shooter"/>'s gunners favour the hardpoints in a weakened patch of their target's shields
		/// (U.SPC-T501, see <see cref="ShieldEmitters.FocusWeight"/>): an AI's crews by doctrine (<see cref="AiFocusFire"/>),
		/// a player's unless the captain has ordered the fire spread. With no shooter named (a stray hit), they do not.
		/// </summary>
		static bool FocusesFire(const cs::Ref<BattleUnit>& shooter);

		// ------------------------------------------------------------ the brace

		/// <summary>The crew braces for a broadside: <see cref="BraceSeconds"/> of every hit shrugged off by
		/// <see cref="BraceReduction"/>, its guns holding their fire meanwhile. False while the last brace is still wearing off.</summary>
		static bool Brace(const cs::Ref<BattleUnit>& u);

		/// <summary>The share of a hit that gets past <paramref name="u"/>'s crew: less while braced.</summary>
		static double BraceFactor(const cs::Ref<BattleUnit>& u);

		/// <summary>Whether <paramref name="u"/>'s crews work its guns this step: not while braced (they load, and fire when
		/// the brace is over; point defence fires on its own throughout).</summary>
		static bool Fires(const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// The broadside about to come in on <paramref name="u"/>: a hostile's volley building on it with most of its guns
		/// loaded, or a hostile helmsman building one with the trigger held while looking its way. Null if none is.
		/// </summary>
		static cs::Ref<BattleUnit> IncomingBroadside(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		// ------------------------------------------------------------ damage control

		/// <summary>The damage control teams <paramref name="u"/> musters: one per <see cref="CrewPerTeam"/> of its crew
		/// (half while boarders are aboard), at most <see cref="MaxTeams"/>.</summary>
		static int Teams(const cs::Ref<BattleUnit>& u);

		/// <summary>Steps where damage control works first; returns the new focus.</summary>
		static DamageControlFocus CycleFocus(const cs::Ref<BattleUnit>& u);

	private:
		static bool InFocus(const cs::Ref<Subsystem>& s, DamageControlFocus focus);

	public:
		/// <summary>
		/// The teams work: each puts back <see cref="RepairHpPerTeamS"/> a second (times <see cref="DamageControlRate"/> and
		/// the fleet's supply readiness) into damaged mounts, those in the focus first and the worst-hurt first among them, up
		/// to <see cref="FieldRepairCap"/> of whole. Its emergency repair crews bring back a shield emitter or gun mount shot
		/// away (U.SPC-T501): <see cref="ShieldEmitters.EmergencyRepairWork"/> of its hit points' worth of work, at
		/// <see cref="ShieldEmitters.EmergencyRepairRate"/>, restores it to <see cref="ShieldEmitters.RestoredHealth"/> (an
		/// emitter's field then regrows from nothing); anything else shot away, and the hull itself, wait for a yard.
		/// With the focus on shields, emitters under fire keep their fields charging at half their rate.
		/// </summary>
		static void StepDamageControl(const cs::Ref<BattleUnit>& u, double dt);

	private:
		/// <summary>
		/// The emergency repair crews (U.SPC-T501): <paramref name="teams"/> teams' work, at
		/// <see cref="ShieldEmitters.EmergencyRepairRate"/>, goes into the shield emitters and gun mounts shot away, those in
		/// the damage control focus first and among them the one nearest done, each coming back at
		/// <see cref="ShieldEmitters.RestoredHealth"/> once its <see cref="ShieldEmitters.EmergencyRepairWork"/> is in.
		/// </summary>
		static void StepEmergencyRepair(const cs::Ref<ShipCombat>& c, const cs::Ref<BroadsideCommand>& cmd, int teams, double dt);

	public:
		// ------------------------------------------------------------ the step

		/// <summary>
		/// A capital ship's broadside step, run by the battle each step before its shields regenerate: the brace wears off,
		/// an AI captain follows doctrine (beam batteries in volleys, a brace when a broadside is about to come in) and damage
		/// control works, its emergency repair crews bringing back what has been shot away.
		/// </summary>
		static void Step(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt);

	private:
		/// <summary>
		/// What an AI captain orders on its own (each part a setting): the guns whose fire cones overlap another gun's, those
		/// that can bear on a hull together, fire in volleys (<see cref="AiVolleys"/>), and a gun that bears alone fires at
		/// will (U.SPC-T533); and it braces for a broadside about to come in.
		/// </summary>
		static void Doctrine(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BroadsideCommand>& cmd);

	public:
		// ------------------------------------------------------------ formations

		/// <summary>A formation that holds its ships on station (every order but none and engage at will).</summary>
		static bool IsHeld(FleetFormation f);

		/// <summary>Whether <paramref name="u"/> keeps a held formation on its leader.</summary>
		static bool HoldsFormation(const cs::Ref<BattleUnit>& u);

		/// <summary>The ships <paramref name="flag"/> can order into formation: its own faction's warships of a destroyer's
		/// size and up on its side, flown by their crews and still fighting, biggest first.</summary>
		static cs::List<cs::Ref<BattleUnit>> FormationMembers(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& flag);

		/// <summary>
		/// The <paramref name="i"/>th of <paramref name="n"/> ships' station in <paramref name="f"/>, km in the flagship's
		/// frame (right, up, forward), <paramref name="spacing"/> km apart.
		/// </summary>
		static DVec3 SlotFor(FleetFormation f, int i, int n, double spacing);

		/// <summary>The distance between ships in a line led by <paramref name="flag"/>, km: half again the longest hull, plus a km.</summary>
		static double Spacing(const cs::Ref<BattleUnit>& flag, const cs::List<cs::Ref<BattleUnit>>& members);

		/// <summary>
		/// <paramref name="flag"/> orders its line into <paramref name="f"/>: every ship it can order (see
		/// <see cref="FormationMembers"/>) takes station on it, or, to engage at will, is cut loose to fight its own fight.
		/// Returns how many ships heard it.
		/// </summary>
		static int OrderFormation(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& flag, FleetFormation f);

		static cs::String Label(FleetFormation f);

		static cs::String Label(BatteryOrder o);

		static cs::String Label(DamageControlFocus f);

		// ------------------------------------------------------------ turning circles

		/// <summary>The radius of the circle a hull turning at <paramref name="turnDegS"/> traces at <paramref name="speedKmS"/>, km.</summary>
		static double TurningRadiusKm(double speedKmS, double turnDegS);

		/// <summary>How long a hull turning at <paramref name="turnDegS"/> takes to come about (turn through 180 deg), s.</summary>
		static double ComeAboutSeconds(double turnDegS);

		/// <summary>How long <paramref name="u"/>, turning at <paramref name="turnDegS"/>, takes to swing
		/// <paramref name="target"/> into <paramref name="gun"/>'s fire cone, s (0 if it already bears).</summary>
		static double SecondsToBear(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& gun, double turnDegS);
	};
}
