#pragma once
#include "Cs/Cs.h"
#include "DVec3.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	// The dogfighting game (U.SPC-T424): a carrier's squadrons as craft of their own classes, each flown by its own
	// flight model, with weapon groups, lock-on missiles, countermeasures, power to share between weapons, shields and
	// engines, wingmen who take orders, and capital-ship weak points a lone small craft can bring a battle down on.
	// Every part is data a new craft, weak point or order joins by a table entry, not by an edit to the battle loop.

	class Battle;
	class BattleUnit;
	class CombatSpec;
	class Subsystem;
	class Missile;
	class IFlightModel;
	class RangeSetting;
	class ToggleSetting;
	struct PilotTuning;
	enum class ShipClass : int;
	enum class SubsystemKind : int;
	enum class MissileKind : int;
	enum class FlightFamily : int;

	/// <summary>What a launched small craft is for (U.SPC-T424): each is a <see cref="ShipClass"/> of its own (U.SPC-T484)
	/// with its own handling.</summary>
	enum class FighterRole : int
	{
		/// <summary>An interceptor (<see cref="ShipClass.Interceptor"/>): the fastest, the tightest turn, light guns and a
		/// missile, for hunting other craft and screening its own bombers.</summary>
		Interceptor,

		/// <summary>A heavy fighter: tougher and slower to turn, guns and a full missile load, for fighting anything. Flown on
		/// the gunship's hull (U.SPC-T467).</summary>
		HeavyFighter,

		/// <summary>A bomber (<see cref="ShipClass.Bomber"/>): slow and armoured, slung torpedoes for capital ships.</summary>
		Bomber,

		/// <summary>A space superiority fighter (<see cref="ShipClass.Fighter"/>, U.SPC-T484): the balanced workhorse, guns
		/// and a pair of missile racks, as ready to fight craft as to strafe a hull.</summary>
		SpaceSuperiority,

		/// <summary>A recon / electronic warfare fighter (<see cref="ShipClass.Recon"/>, U.SPC-T485): flown ahead of the
		/// squadrons in a carrier's recon flight, it jams the enemy's targeting and paints a capital for the bombers (see
		/// <see cref="ElectronicWarfare"/>). Never in <see cref="FighterCraft.SquadronMix"/>.</summary>
		Recon,

		/// <summary>A boarding shuttle (<see cref="ShipClass.AssaultShuttle"/>, U.SPC-T486): unarmed and armoured, it carries a
		/// marine team to a disabled hull or a silenced station, docks and latches on (<see cref="BoardingShuttles"/>). Not
		/// one of a carrier's squadrons: a carrier sends one out when there is a prize to take.</summary>
		AssaultShuttle,
	};
	SPACEGODCORE_API const char* ToString(FighterRole v);

	/// <summary>
	/// One class of launched small craft (U.SPC-T424): the <see cref="ShipClass"/> it is a hull of (U.SPC-T484), its
	/// fighting qualities and hull (<see cref="Spec"/>, with a real mass, thrust and torque so it flies through
	/// <see cref="HullDynamics"/>), the <see cref="IFlightModel"/> its class flies on in a player's hands (U.SPC-T17), the
	/// handling numbers flight assist works to, and how its pilots pick their fights. A new class is a new entry here
	/// (<see cref="Register"/>) and a place in <see cref="SquadronMix"/>.
	/// </summary>
	class SPACEGODCORE_API FighterCraft final
	{
	public:
		FighterRole Role;
		cs::String Name;

		/// <summary>The hull class it is (U.SPC-T484): what the carrier launches, what it is drawn as, and whose flight
		/// model flies it (<see cref="FlightModelRegistry.For"/>).</summary>
		ShipClass Class;

		cs::Ref<CombatSpec> Spec;
		double LengthKm = 0;

		/// <summary>The afterburner: top speed times this while the charge lasts, over this many seconds.</summary>
		double BoostMultiplier = 0, BoostSeconds = 0;

		/// <summary>How much further its pilots go after an enemy fighter than after a hull, km (an interceptor hunts craft;
		/// a bomber never picks a fighter at all, see <see cref="FighterState.Bomber"/>).</summary>
		double HuntsFightersKm = 6;

		/// <summary>Whether its pilots go first for an enemy fighter closing on one of their own side's bombers (U.SPC-T484):
		/// the interceptor's screen.</summary>
		bool ScreensBombers = false;

		FighterCraft(FighterRole role, cs::String name, ShipClass cls, cs::Ref<CombatSpec> spec, double lengthKm, double boostMultiplier, double boostSeconds);

		/// <summary>The flight family its class flies in.</summary>
		FlightFamily Family() const;

		/// <summary>How flight assist flies it at <paramref name="engines"/> times its own power (see <see cref="PowerDistribution"/>).</summary>
		PilotTuning Tuning(double engines = 1) const;

		/// <summary>The flight model a player flies it on: its class's.</summary>
		cs::Ref<IFlightModel> Model() const;

		/// <summary>A carrier's squadrons in launch order, one role each, repeating: interceptors, space superiority fighters,
		/// bombers, then (on a carrier with a fourth squadron) heavy fighters.</summary>
		// A mutable static array in C#: a function returning the one list, so callers may still reassign its contents.
		static cs::List<FighterRole>& SquadronMix();

		static FighterRole RoleOfSquadron(int squadron);

		static void Register(const cs::Ref<FighterCraft>& craft);

		static cs::Ref<FighterCraft> Of(FighterRole role);

		static const cs::Ref<FighterCraft>& Interceptor();

		/// <summary>The balanced workhorse (U.SPC-T484): a little slower and wider in the turn than the interceptor, tougher,
		/// and with twice its missiles, as ready to strafe a hull as to fight a craft.</summary>
		static const cs::Ref<FighterCraft>& SpaceSuperiority();

		static const cs::Ref<FighterCraft>& HeavyFighter();

		static const cs::Ref<FighterCraft>& Bomber();

		/// <summary>The recon / EW fighter (U.SPC-T485): an interceptor's pace, a light gun for its own defence, flares, and
		/// the rest of it sensors and jammers (<see cref="ElectronicWarfare"/>). It never hunts fighters: it holds off its
		/// mark, jamming and painting.</summary>
		static const cs::Ref<FighterCraft>& Recon();

		/// <summary>The boarding shuttle (U.SPC-T486): slow, plated and shielded to get its team across, no guns, flares to
		/// shake a missile off. Its pilots fly it to a prize, never into a fight (<see cref="BoardingShuttles"/>).</summary>
		static const cs::Ref<FighterCraft>& AssaultShuttle();

		/// <summary>The craft a carrier launches as hulls of <paramref name="cls"/>, or null if none is.</summary>
		static cs::Ref<FighterCraft> OfClass(ShipClass cls);

	private:
		/// <summary>
		/// Gives <paramref name="spec"/> a real hull for the flight models: <paramref name="massKt"/>, a main drive that
		/// pushes it at its <see cref="CombatSpec.Accel"/>, retro thrusters three quarters as strong and manoeuvre
		/// thrusters half, and attitude torque that spins it up at <paramref name="angularAccel"/> rad/s² (rolling twice as
		/// readily), and registers it.
		/// </summary>
		static cs::Ref<FighterCraft> Add(FighterRole role, cs::String name, ShipClass cls, double lengthKm, double massKt, double angularAccel, double boost, double boostSeconds, cs::Ref<CombatSpec> spec,
			double huntsFightersKm = 6, bool screensBombers = false);

		// C# runs every static initialiser of the type, in textual order, the first time any static member is touched
		// (Crafts, SquadronMix, then each craft's Add, which registers it): TypeInit reproduces that.
		static void TypeInit();
		friend struct FighterCraftStatics;
	};

	// ---------------------------------------------------------------- weapon groups

	/// <summary>The groups a small craft's weapons are fired in (U.SPC-T424): the guns on the main trigger, and one
	/// selectable group of guided weapons on the second.</summary>
	enum class WeaponGroup : int
	{
		Guns,

		/// <summary>The homing missiles: they go once the lock is made and steer onto it.</summary>
		Missiles,
		Torpedoes,

		/// <summary>Dumb-fire rockets (US-T101): no lock, straight down the nose.</summary>
		Rockets,

		/// <summary>Hive missiles (US-T101): on the lock, each round splitting into a swarm.</summary>
		Hive,
	};
	SPACEGODCORE_API const char* ToString(WeaponGroup v);

	/// <summary>Which group each kind of mount fires in: a table, so a new mount joins a group by an entry here.</summary>
	class SPACEGODCORE_API WeaponGroups
	{
		static cs::Dictionary<SubsystemKind, WeaponGroup>& ByKind();

	public:
		static void Assign(SubsystemKind kind, WeaponGroup group);

		/// <summary>The group a mount fires in, or null for one that is not fired from the cockpit (a shield, a dispenser).</summary>
		static cs::Nullable<WeaponGroup> Of(SubsystemKind kind);

		/// <summary>The guided groups (everything but the guns) <paramref name="u"/> carries a live mount of, in order.</summary>
		static cs::List<WeaponGroup> Secondaries(const cs::Ref<BattleUnit>& u);

		/// <summary>Rounds left across <paramref name="u"/>'s live mounts of <paramref name="g"/> (-1 for guns, which never run dry).</summary>
		static int Ammo(const cs::Ref<BattleUnit>& u, WeaponGroup g);

		static cs::String Label(WeaponGroup g);
	};

	// ---------------------------------------------------------------- energy

	/// <summary>The three systems a small craft's reactor feeds (U.SPC-T424).</summary>
	enum class PowerSystem : int
	{
		Weapons,
		Shields,
		Engines,
	};
	SPACEGODCORE_API const char* ToString(PowerSystem v);

	/// <summary>
	/// How a small craft shares its reactor (U.SPC-T424): <see cref="TotalPips"/> pips across weapons, shields and
	/// engines, at most <see cref="MaxPips"/> to any one. At an even share each runs as designed; every pip more is a
	/// quarter faster (guns reloading and cooling, shields recharging, top speed and thrust), every pip less a quarter
	/// slower. A unit with no distribution runs everything as designed.
	/// </summary>
	class SPACEGODCORE_API PowerDistribution final
	{
	public:
		static constexpr int TotalPips = 6; static constexpr int MaxPips = 4; static constexpr int EvenPips = 2;
		static constexpr double PerPip = 0.25;

	private:
		cs::List<int> pips = { EvenPips, EvenPips, EvenPips };

	public:
		int operator[](PowerSystem s) const { return pips[static_cast<int>(s)]; }

		/// <summary>How hard <paramref name="s"/> runs: 1 at an even share, 0.5 with none, 1.5 at the most.</summary>
		double Factor(PowerSystem s) const { return 1 + (pips[static_cast<int>(s)] - EvenPips) * PerPip; }

		/// <summary>Moves a pip to <paramref name="s"/> from whichever other system has the most; false at the most already.</summary>
		bool Shift(PowerSystem s);

		/// <summary>Back to an even share.</summary>
		void Balance();

		/// <summary>Sets the share outright (each 0..<see cref="MaxPips"/>, summing to <see cref="TotalPips"/>); false if it cannot be.</summary>
		bool Set(int weapons, int shields, int engines);

		bool IsEven() const { return pips[0] == EvenPips && pips[1] == EvenPips && pips[2] == EvenPips; }

		/// <summary>All the reactor can spare to <paramref name="s"/> (US-T94): <see cref="MaxPips"/> to it, one each to the others.</summary>
		void AllTo(PowerSystem s);

		/// <summary>How much field the shield emitters hold on the player's split (US-T94): 1 at an even share, an eighth
		/// more or less per pip (0.75 with none, 1.25 at the most).</summary>
		double ShieldCapacity() const { return 1 + (pips[static_cast<int>(PowerSystem::Shields)] - EvenPips) * PerPip * 0.5; }

		/// <summary>The split as one number for a command line (US-T94): weapons*25 + shields*5 + engines.</summary>
		int Code() const { return pips[0] * 25 + pips[1] * 5 + pips[2]; }

		/// <summary>Sets the split from <see cref="Code"/>'s number; false (and unchanged) if it is not a legal split.</summary>
		bool SetCode(int code);

		/// <summary>"BALANCED", "ALL WEAPONS" and so on for a preset split, else empty.</summary>
		cs::String PresetName() const;

		/// <summary><paramref name="u"/>'s factor for <paramref name="s"/>: 1 when it shares no power.</summary>
		static double FactorOf(const cs::Ref<BattleUnit>& u, PowerSystem s);

		cs::String ToString() const;
		SG_SERIALIZABLE(PowerDistribution); // ObjectGraph registration (Persistence/Registrations)
	};

	// ---------------------------------------------------------------- countermeasures

	/// <summary>
	/// A flare-and-chaff cloud a small craft throws out astern (U.SPC-T424): each guided round homing on the craft
	/// within <see cref="Countermeasures.ReachKm"/> may be seduced onto it (<see cref="Countermeasures.Effectiveness"/>,
	/// less for a torpedo), and goes off harmlessly when it reaches it or the cloud burns out.
	/// </summary>
	class Decoy final
	{
	public:
		DVec3 Pos, Vel;
		int Side = 0;
		double Life = 0;
		bool Gone = false;
	};

	// [SettingsSource]
	class SPACEGODCORE_API Countermeasures
	{
	public:
		/// <summary>How long a cloud burns, and how far off a homing round must be to be drawn onto it, km.</summary>
		static constexpr double LifeSeconds = 4; static constexpr double ReachKm = 3;

		/// <summary>The share of an incoming torpedo's chance of being seduced to a missile's: a torpedo's seeker is harder to fool.</summary>
		static constexpr double TorpedoShare = 0.4;

		/// <summary>How near an inbound round an AI pilot waits to throw a cloud, km.</summary>
		static constexpr double AiReactKm = 2.2;

		static const cs::Ref<RangeSetting>& Effectiveness();

		/// <summary>The chance a cloud seduces a round of <paramref name="kind"/>; never an EMP round or a boarding pod.</summary>
		static double SeduceChance(MissileKind kind);
	};

	// ---------------------------------------------------------------- capital-ship weak points

	/// <summary>
	/// A part of a capital ship that is exposed from one side (U.SPC-T424): its reactor's vent, its engine bells, its
	/// shield emitters. A small craft's shot that strikes it coming in from that side (within <see cref="ConeDeg"/> of
	/// <see cref="LocalAxis"/>, in ship-length axes: right, up, forward) does <see cref="Multiplier"/> times the damage
	/// that gets through the shields.
	/// </summary>
	class SPACEGODCORE_API WeakPointSpec final
	{
	public:
		SubsystemKind Kind;
		cs::String Label;
		DVec3 LocalAxis;
		double ConeDeg = 0, Multiplier = 0;

		/// <summary>Which a pilot goes for first when several are open: higher first.</summary>
		int Priority = 0;

		WeakPointSpec(SubsystemKind kind, cs::String label, DVec3 localAxis, double coneDeg, double multiplier, int priority);
	};

	// [SettingsSource]
	class SPACEGODCORE_API WeakPoints
	{
		static cs::Dictionary<SubsystemKind, cs::Ref<WeakPointSpec>>& Specs();

	public:
		/// <summary>The smallest hull with weak points to go for, km (a destroyer and up), and the largest that counts as a small craft.</summary>
		static constexpr double MinCapitalKm = 0.4; static constexpr double MaxSmallCraftKm = 0.2;

		static const cs::Ref<RangeSetting>& Damage();

		static const cs::Ref<ToggleSetting>& AiGoesForThem();

		static void Register(const cs::Ref<WeakPointSpec>& spec);

		static cs::Ref<WeakPointSpec> SpecOf(SubsystemKind kind);

		/// <summary>A hull big enough to have weak points worth an attack run.</summary>
		static bool IsCapital(const cs::Ref<BattleUnit>& u);

		/// <summary>A craft small enough to get in close to a weak point: any launched craft, or a fighter-sized hull.</summary>
		static bool IsSmallCraft(const cs::Ref<BattleUnit>& u);

		/// <summary>The world direction <paramref name="spec"/> is open toward on <paramref name="target"/>.</summary>
		static DVec3 Axis(const cs::Ref<BattleUnit>& target, const cs::Ref<WeakPointSpec>& spec);

		/// <summary>Whether a shot travelling along <paramref name="incoming"/> strikes <paramref name="sub"/> from its open side.</summary>
		static bool Exposed(const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& sub, DVec3 incoming);

		/// <summary>Whether <paramref name="sub"/> lies open to a craft at <paramref name="from"/>.</summary>
		static bool ExposedFrom(const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& sub, DVec3 from);

		/// <summary>
		/// What a small craft's hit on <paramref name="sub"/> travelling along <paramref name="incoming"/> does to what
		/// gets through the shields: the weak point's multiplier (scaled by <see cref="Damage"/>) from its open side, else 1.
		/// </summary>
		static double Factor(const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& sub, DVec3 incoming);

		/// <summary><paramref name="target"/>'s live weak points, the most telling first.</summary>
		static cs::List<cs::Ref<Subsystem>> On(const cs::Ref<BattleUnit>& target);

		/// <summary>The weak point a run should go for: the most telling one still standing (a shield emitter whose field is
		/// already down is passed over while anything else is left), or null.</summary>
		static cs::Ref<Subsystem> Preferred(const cs::Ref<BattleUnit>& target);

		/// <summary>The most telling weak point open to a craft at <paramref name="from"/>, or null if none is.</summary>
		static cs::Ref<Subsystem> BestFrom(const cs::Ref<BattleUnit>& target, DVec3 from);

		/// <summary>The next (or previous) weak point on <paramref name="target"/> after <paramref name="current"/>, wrapping.</summary>
		static cs::Ref<Subsystem> Cycle(const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& current, int step);

		// C# returns null for a null subsystem; a cs::String has no null, so that is "".
		static cs::String Label(const cs::Ref<Subsystem>& s);
	};

	// ---------------------------------------------------------------- strike torpedoes

	/// <summary>
	/// A bomber's torpedoes against a capital ship (U.SPC-T484, docs/SHIP_ROSTER.md: "bombers knock the shields down first
	/// and then torpedo"). A shield soaks a torpedo like any other hit, but one that arrives where the capital's shield
	/// on that side is already down meets bare hull with a shaped warhead and does <see cref="BareHullDamage"/> times its
	/// damage. Small craft only (a capital's own torpedo tubes are not strike torpedoes), and only on a capital.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API StrikeTorpedoes
	{
	public:
		static const cs::Ref<RangeSetting>& BareHullDamage();

		/// <summary>Whether <paramref name="m"/> is a strike craft's torpedo.</summary>
		static bool IsStrike(const cs::Ref<Missile>& m);

		/// <summary>The warhead's factor on <paramref name="target"/> when the shield it came in through is
		/// <paramref name="shieldDown"/>: <see cref="BareHullDamage"/> on a capital's bare hull, else 1.</summary>
		static double Factor(const cs::Ref<BattleUnit>& target, bool shieldDown);

		/// <summary>Whether the field over the patch of <paramref name="target"/>'s hull facing a craft at <paramref name="from"/>
		/// is down (or its emitter is shot away, or it has none, U.SPC-T501): where a bomber's torpedo does its work.</summary>
		static bool ShieldDownToward(const cs::Ref<BattleUnit>& target, DVec3 from);
	};

	// ---------------------------------------------------------------- wingmen

	/// <summary>What a squadron's lead has told its wingmen to do (U.SPC-T424).</summary>
	enum class WingOrder : int
	{
		/// <summary>Hold the vee on the lead and fire on its target (what a squadron does on its own).</summary>
		FormUp,

		/// <summary>Break off and fly runs of their own on the lead's target.</summary>
		AttackMyTarget,

		/// <summary>Go after whatever hostile is on the lead or closing on it, then form up again.</summary>
		CoverMe,

		/// <summary>Each picks and fights its own target.</summary>
		EngageAtWill,
	};
	SPACEGODCORE_API const char* ToString(WingOrder v);

	/// <summary>A squadron's wingmen and the orders its lead gives them (U.SPC-T424).</summary>
	class SPACEGODCORE_API Wing
	{
	public:
		/// <summary>How near the lead a hostile must be for a wingman covering it to go after it, km.</summary>
		static constexpr double CoverKm = 4;

		/// <summary>The craft of <paramref name="lead"/>'s squadron still flying, but for the lead itself.</summary>
		static cs::List<cs::Ref<BattleUnit>> Members(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& lead);

		/// <summary>
		/// <paramref name="lead"/> takes its squadron and gives it <paramref name="order"/>: every wingman flies on it from
		/// now on, under that order. Returns how many heard it.
		/// </summary>
		static int Command(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& lead, WingOrder order);

		/// <summary>The hostile a wingman covering <paramref name="lead"/> goes after: the nearest armed one within
		/// <see cref="CoverKm"/> of it, one behind it first; null if the lead is clear.</summary>
		static cs::Ref<BattleUnit> Threat(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& lead);

		static cs::String Label(WingOrder o);
	};
}
