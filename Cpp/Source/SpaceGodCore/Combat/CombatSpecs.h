#pragma once
// Port of Assets/UnitySpace/Core/Combat/CombatSpecs.cs (see docs/MIGRATION.md).

#include <memory>

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	enum class ShipClass : int;

	enum class SubsystemKind : int
	{
		LaserTurret,
		HeavyLaser,
		MissileLauncher,
		PointDefense,
		FighterGuns,
		Torpedo,

		/// <summary>A fixed spinal kinetic cannon: no turret traverse, so the hull itself must aim it
		/// (see <see cref="Battle.BroadsideFacing"/>) -- the tradeoff for the single hardest-hitting,
		/// longest-ranged non-missile shot in the game.</summary>
		Railgun,

		/// <summary>Launches drill-tipped boarding pods that punch into a hull and put marines aboard.</summary>
		BoardingPods,

		/// <summary>A Breacher's drill head: it bores into the hull the round strikes and its team goes in through the hole.</summary>
		Drill,
		Engine,
		ShieldGenerator,
		Bridge,
		Hangar,
		Reactor,

		/// <summary>A fittable rapid-fire kinetic gun (U.SPC-T271): a fighter's main gun, or on a small ballistic mount of a bigger hull.</summary>
		Autocannon,

		/// <summary>A fighter-sized railgun: a slow, hard, fast-flying slug for a gun mount (U.SPC-T271).</summary>
		LightRailgun,

		/// <summary>Fires a homing electromagnetic burst that knocks out the drives and shields of what it bursts by (U.SPC-T271).</summary>
		EmpLauncher,

		/// <summary>Lays proximity mines astern (U.SPC-T271).</summary>
		MineLayer,

		/// <summary>An automated gun on its own turret mount (U.SPC-T278): it traverses, picks its targets and fires by
		/// itself, whoever is flying the hull and whether or not the trigger is held.</summary>
		AutoTurret,

		/// <summary>A capital torpedo tube (U.SPC-T377): a fixed launcher that fires one heavy, slow, hard-to-stop
		/// torpedo down the bow at a time. A carrier bomber's slung <see cref="Torpedo"/> is a different, smaller thing.</summary>
		TorpedoTube,

		/// <summary>Throws flare-and-chaff clouds astern that draw off missiles homing on the craft (U.SPC-T424, see <see cref="Decoy"/>).</summary>
		Countermeasures,

		/// <summary>A capital ship's spinal tachyon lance (U.SPC-T487, see <see cref="TachyonLances"/>): its shot lands the
		/// instant it is fired, at the longest reach of any gun, but it is fixed down the bow, charges slowly and starves
		/// the shields while it does.</summary>
		TachyonLance,

		/// <summary>A phaser-style beam array (U.SPC-T488): a versatile medium-range energy weapon its captain tunes in battle,
		/// a narrow lance for hulls or a wide sweep for fighters and missiles (see <see cref="EnergyWeapons"/>).</summary>
		PhaserArray,

		/// <summary>A plasma cannon (U.SPC-T488): a devastating short-range bolt whose damage bleeds off over distance.</summary>
		PlasmaCannon,

		/// <summary>An antimatter (photon) torpedo launcher (U.SPC-T489): a heavy guided warhead at medium range for frigates
		/// and capitals, the hardest single hit in the game, tough but not too tough for a screen to shoot down.</summary>
		AntimatterTorpedo,

		/// <summary>A rotary flak cannon (U.SPC-T489): a destroyer's short-ranged screen gun, whose shells burst among
		/// missiles and fighters and hurt everything hostile in the burst.</summary>
		FlakCannon,

		/// <summary>An interceptor missile launcher (U.SPC-T489): fast guided rounds that run down incoming missiles and
		/// fighters before they reach the hulls the screen is guarding.</summary>
		InterceptorMissile,

		/// <summary>A spinal coilgun (U.SPC-T490, see <see cref="SpinalMounts"/>): magnetic coils the length of a battleship's
		/// keel that throw a lighter, slower slug than a spinal railgun's, a little less far, but recharge in half the time.</summary>
		Coilgun,

		/// <summary>A radiator panel on a big ship's hull (U.SPC-T491, see <see cref="Radiators"/>): it sheds the heat the
		/// engines and guns build. Thin and spread out to the sky, so it is a weak point: a hull that loses its radiators
		/// must throttle down or cook.</summary>
		Radiator,

		// The full arsenal (US-T101). Appended, so every saved value above keeps its number. The homing missile is the
		// MissileLauncher above (its rounds steer onto a locked target); the three below are the kinds it lacked.

		/// <summary>A vulcan rotary cannon (US-T101, see <see cref="Arsenal"/>): a gatling for a fighter's nose or a small
		/// turret that spins up from a quarter of its rate to a torrent of tracers, short-ranged and hot.</summary>
		VulcanCannon,

		/// <summary>A hive missile launcher (US-T101): one round that flies a moment and splits into a swarm of homing
		/// micro-missiles, each light enough for point defence to pick off.</summary>
		HiveLauncher,

		/// <summary>A dumb-fire rocket pod (US-T101): an unguided salvo along the nose, fast and cheap, that hits whatever
		/// hostile hull it flies into and nothing it does not.</summary>
		RocketPod,

		/// <summary>A nanite repair system (US-T102, see <see cref="NaniteRepairs"/>): a shield mount's module that rebuilds
		/// the hull, its damaged sections and its standing hardpoints over time instead of projecting a field. Appended.</summary>
		NaniteRepair,

		/// <summary>A hull's broadcast array (US-T96, see <see cref="BroadcastArrays"/>): the comms mast it reports an attack
		/// with. Never one of a design's hardpoints (it lives on the battle unit), so it is numbered apart from them.</summary>
		BroadcastArray = 100,
	};
	SPACEGODCORE_API const char* ToString(SubsystemKind v);

	/// <summary>What a non-gun weapon puts out into the battle instead of firing at a target.</summary>
	enum class DeployKind : int { None, Mine, Decoy };
	SPACEGODCORE_API const char* ToString(DeployKind v);

	class CombatSpec;

	/// <summary>What a weapon does. Ranges are km, damage is hit points, cooldowns are seconds.</summary>
	class SPACEGODCORE_API WeaponSpec final
	{
	public:
		double RangeKm = 0;
		double Damage = 0;
		double Cooldown = 0;
		double Accuracy = 0;
		bool Missile = false;
		double MissileSpeedKmS = 0;
		int Salvo = 1;
		int Ammo = 0;

		/// <summary>Point defence: shoots missiles and fighters first.</summary>
		bool AntiSmall = false;

		/// <summary>Muzzle velocity (km/s) of a direct-fire kinetic round -- a railgun slug or an
		/// autocannon burst. Zero for an energy weapon: a laser beam crosses its range effectively
		/// instantly, so <see cref="Battle.Fire"/> resolves it the instant it's aimed rather than
		/// spawning a travelling <see cref="KineticRound"/>.</summary>
		double MuzzleSpeedKmS = 0;

		/// <summary>A tachyon lance (U.SPC-T487): no travel time and no lead, and a hit as sure at the end of its reach
		/// as close in.</summary>
		bool Lance = false;

		/// <summary>The tachyon lance: the longest reach of any gun and a hit second only to a capital torpedo, landing
		/// the instant it is fired, on a long recharge. Its figures are the <see cref="TachyonLances"/> settings'.</summary>
		static const cs::Ref<WeaponSpec>& TachyonLance();
		/// <summary>The share of <see cref="Damage"/> a shot still does at the very edge of <see cref="RangeKm"/>, falling off
		/// evenly from full at the muzzle (a plasma bolt bleeding off over distance, U.SPC-T488); 1, the default, for a weapon
		/// that hits as hard at any range.</summary>
		double FalloffEdgeShare = 1;

		/// <summary>What a shot does landing <paramref name="km"/> from the muzzle (see <see cref="FalloffEdgeShare"/>).</summary>
		double DamageAt(double km) const;

		static const cs::Ref<WeaponSpec>& Laser();
		static const cs::Ref<WeaponSpec>& Heavy();
		static const cs::Ref<WeaponSpec>& Missiles();
		static const cs::Ref<WeaponSpec>& PointDefense();

		/// <summary>A fighter's rapid-firing forward autocannon: a dumb kinetic round with a real,
		/// if short, flight time across its short range -- not a hitscan.</summary>
		static const cs::Ref<WeaponSpec>& FighterGuns();
		static const cs::Ref<WeaponSpec>& Torpedo();

		/// <summary>A kinetic spinal gun: the longest non-missile range and the hardest single hit
		/// outside a torpedo, at the slowest cooldown -- a precise, patient weapon built around one
		/// shot landing hard rather than sustained fire. Hypervelocity, but still slow enough next to
		/// its own long range to take a real second or two in flight.</summary>
		static const cs::Ref<WeaponSpec>& Railgun();

		/// <summary>An electromagnetic burst (<see cref="EmpLauncher"/>): seconds of dead drives and shields it leaves a
		/// small hull it bursts beside (a big one shrugs it off sooner), and how near the burst it reaches, km.</summary>
		double EmpSeconds = 0, EmpRadiusKm = 0;

		/// <summary>What it puts out into the battle (a mine, a sentry turret) instead of firing at a target.</summary>
		DeployKind Deploy = DeployKind::None;

		/// <summary>Mines, sentry turrets and EMP rounds: a hull's means of getting away or holding ground, not of
		/// fighting, so they make no hull armed, count for nothing in its strength and turn no broadside.</summary>
		bool Supporting() const { return Deploy != DeployKind::None || EmpSeconds > 0; }

		/// <summary>A fittable autocannon: the fighter's rapid kinetic gun, as a module (U.SPC-T271).</summary>
		static const cs::Ref<WeaponSpec>& Autocannon();

		/// <summary>A fighter-sized railgun: four times the reach of an autocannon and a slug that hits five times as
		/// hard and flies far faster, one shot every two seconds -- the sniper's gun of a dogfight.</summary>
		static const cs::Ref<WeaponSpec>& LightRailgun();

		/// <summary>A homing EMP round: no damage, but a fighter it bursts beside drifts dead for eight seconds.</summary>
		static const cs::Ref<WeaponSpec>& Emp();

		/// <summary>Proximity mines: <see cref="RangeKm"/> is the blast's reach, <see cref="Damage"/> its force at the centre.</summary>
		static const cs::Ref<WeaponSpec>& Mines();

		/// <summary>An auto turret: a light, fast-traversing kinetic gun its own fire control lays on whatever hostile
		/// comes near, a fighter as readily as a hull (U.SPC-T278).</summary>
		static const cs::Ref<WeaponSpec>& AutoTurret();

		/// <summary>A capital torpedo (U.SPC-T377): the hardest single hit in the game, slow enough that point defence
		/// has time to work on it, and only six in the tube's magazine.</summary>
		static const cs::Ref<WeaponSpec>& HeavyTorpedo();

		/// <summary>A small craft's dogfight missile (U.SPC-T424): short-ranged, quick and agile enough to run down another
		/// fighter once the lock is made, and light enough for point defence or a flare to stop.</summary>
		static const cs::Ref<WeaponSpec>& DogfightMissile();

		/// <summary>A small craft's flare-and-chaff dispenser (U.SPC-T424): <see cref="Ammo"/> clouds, one every <see cref="Cooldown"/> seconds.</summary>
		static const cs::Ref<WeaponSpec>& Flares();

		/// <summary>A guided round whose seeker locks onto small craft as readily as onto hulls (U.SPC-T424).</summary>
		bool SeeksSmallCraft = false;

		/// <summary>A flak shell's burst, km (U.SPC-T489): every hostile missile and fighter within it is hurt, not only the one
		/// it was laid on. Zero for every weapon that hits only what it hits.</summary>
		double BurstKm = 0;

		/// <summary>An antimatter torpedo (U.SPC-T489, see <see cref="ScreenWeapons"/> for its settings): medium-ranged, quicker
		/// than a tube's torpedo and nearly twice the warhead, four to a launcher.</summary>
		static const cs::Ref<WeaponSpec>& AntimatterTorpedo();

		/// <summary>A rotary flak cannon (U.SPC-T489): a shell every 0.8 s out to 5 km, each bursting over
		/// <see cref="BurstKm"/> (times <see cref="ScreenWeapons.FlakBurst"/>) among the missiles and fighters it was laid on.</summary>
		static const cs::Ref<WeaponSpec>& Flak();

		/// <summary>An interceptor missile (U.SPC-T489): a fast, agile round that hunts an incoming missile (or a fighter) out
		/// to 14 km, its warhead enough to kill any one of them.</summary>
		static const cs::Ref<WeaponSpec>& Interceptor();

		/// <summary>Boarding pods: slow, few, and stopped by shields; each carries a squad of marines.</summary>
		static const cs::Ref<WeaponSpec>& Pods();

		/// <summary>A rotary gun's spin-up (US-T101), seconds from rest to its full rate: a cold gun fires at a fraction of
		/// it (<see cref="Arsenal.SpinStartFactor"/>) and gains speed with every round. Zero for every other gun.</summary>
		double SpinUpS = 0;

		/// <summary>A hive round (US-T101): the micro-missiles it splits into, and after how many seconds of flight. Zero for
		/// a round that stays whole. <see cref="Damage"/> is then each micro-missile's warhead.</summary>
		int SplitInto = 0;
		double SplitAfterS = 0;

		/// <summary>A dumb-fire rocket (US-T101): no seeker and no lock, it flies straight down the line it was fired on.</summary>
		bool Unguided = false;

		/// <summary>The vulcan rotary cannon (US-T101): under an autocannon's reach, a lighter round, and at full spin over
		/// three times its rate, so it shreds what it reaches and overheats fast.</summary>
		static const cs::Ref<WeaponSpec>& Vulcan();

		/// <summary>A hive missile (US-T101): four rounds, each splitting after a second into eight micro-missiles.</summary>
		static const cs::Ref<WeaponSpec>& HiveMissile();

		/// <summary>Dumb-fire rockets (US-T101): salvos of four, quick and hard-hitting at short range, twenty-four to a pod.</summary>
		static const cs::Ref<WeaponSpec>& Rockets();

		/// <summary>Marines per boarding pod.</summary>
		static constexpr double PodMarines = 12;
	};

	/// <summary>One hardpoint on a hull: its kind, where it sits (ship lengths, nose +Z, up +Y) and its toughness.</summary>
	// enable_shared_from_this: the bodied members hand `this` to FireCones, which takes a cs::Ref<SubsystemDef>.
	class SPACEGODCORE_API SubsystemDef final : public std::enable_shared_from_this<SubsystemDef>
	{
	public:
		SubsystemKind Kind{};
		double X = 0, Y = 0, Z = 0;
		double MaxHp = 0;
		cs::Ref<WeaponSpec> Weapon;

		/// <summary>Turrets facing aft rest pointing backwards.</summary>
		bool FacesAft = false;

		/// <summary>The hull frame's own aiming direction for a weapon on this hardpoint (U.SPC-T533, see <see cref="FireCones"/>),
		/// unit, in the hull frame (right, up, forward), or null for the mount's own normal (along the keel for a fixed
		/// mount). A shield emitter aims nothing: its field covers the patch of hull around where it sits (U.SPC-T501, see
		/// <see cref="ShipCombat.PatchAt"/>).</summary>
		cs::Nullable<DVec3> ConeAxis;

		/// <summary>The hull frame's own half-angle for a weapon on this hardpoint, deg, or null for the module's
		/// (<see cref="ModuleSpec.ConeHalfDeg"/>). 180 bears everywhere.</summary>
		cs::Nullable<double> ConeHalfDeg;

		/// <summary>The grade of what is fitted here (U.SPC-T65), 1 for the design's own part: a hardened
		/// shield generator's share of the hull's shield and regen against a stock one's, or a torch drive's
		/// thrust against a stock engine's (see <see cref="Loadout"/>). Ignored for other kinds.</summary>
		double Rating = 1;

		/// <summary>Buried at the centre of the hull (U.SPC-T492, a warship's bridge): nothing of it shows on the outside, and
		/// a shot or a collision reaches it only through the hull's full thickness (<see cref="BuriedBridges"/>).</summary>
		bool Buried = false;

		/// <summary>A spinal mount (U.SPC-T490, see <see cref="SpinalMounts"/>): a gun as long as the hull, laid down the keel
		/// with its muzzle at the bow, that the hull itself lays on the target and a spinal power plant feeds.</summary>
		bool Spinal = false;

		/// <param name="coneAxis">The hull frame's aiming direction for the hardpoint (see <see cref="ConeAxis"/>), or null for the mount's normal.</param>
		/// <param name="coneHalfDeg">The hull frame's half-angle for it (see <see cref="ConeHalfDeg"/>), or null for its module's.</param>
		SubsystemDef(SubsystemKind kind, double x, double y, double z, bool facesAft = false, cs::Nullable<DVec3> coneAxis = std::nullopt,
			cs::Nullable<double> coneHalfDeg = std::nullopt);

		bool IsWeapon() const { return Weapon != nullptr; }

		/// <summary>How far this mount can bear off its axis, deg, at the settings' scale (see <see cref="FireCones.HalfDeg"/>).</summary>
		double HalfDeg() const;

		/// <summary>Whether this mount bears everywhere (a cone as wide as the sky).</summary>
		bool BearsEverywhere() const { return HalfDeg() >= 180; }

		/// <summary>The direction this mount aims about on a hull of <paramref name="spec"/>'s beam, hull frame (see <see cref="FireCones.Axis(SubsystemDef, CombatSpec)"/>).</summary>
		DVec3 AxisOn(const cs::Ref<CombatSpec>& spec) const;

		/// <summary>Whether a direction in the hull frame (right, up, forward) lies inside this mount's cone on a hull of
		/// <paramref name="spec"/>'s beam (U.SPC-T533).</summary>
		bool Covers(const cs::Ref<CombatSpec>& spec, DVec3 dirLocal) const;

	private:
		/// <summary>Hardpoints are built to take punishment; a capital ship loses guns one at a time.</summary>
		/// <summary>A radiator panel is the exception (U.SPC-T491): a thin sheet out in the open, not built to take a hit.</summary>
		static double DefaultHp(SubsystemKind k);

		static double BaseHp(SubsystemKind k);

		static cs::Ref<WeaponSpec> DefaultWeapon(SubsystemKind k);

		cs::Ref<SubsystemDef> Self() const { return std::const_pointer_cast<SubsystemDef>(shared_from_this()); }

	public:
		static cs::String Name(SubsystemKind k);
	private:
		SubsystemDef() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(SubsystemDef); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>A hull design's fighting qualities and hardpoint layout (positions match its mesh). Several
	/// designs can share a <see cref="ShipClass"/> (its broad category); each ship picks one at construction
	/// and keeps it for its lifetime, so two ships of the same class can fly and fight differently.</summary>
	class SPACEGODCORE_API CombatSpec final
	{
	public:
		/// <summary>A display name for this specific design, distinct from its class.</summary>
		cs::String DesignName;

		/// <summary>This design's own length — ships of the same class can be different sizes.</summary>
		double LengthKm = 0;

		double Hull = 0, Shield = 0, ShieldRegen = 0, Armor = 0;

		/// <summary>Tactical speed and acceleration/deceleration in battle, km/s and km/s².</summary>
		double MaxSpeed = 0, Accel = 0, Decel = 0;

		/// <summary>Boost multiplies MaxSpeed while held, draining over BoostDurationS and recharging otherwise.</summary>
		double BoostSpeedMultiplier = 1, BoostDurationS = 0;

		double TurnRateDegS = 0;

		/// <summary>The hull's real drive (U.SPC-T59), what a piloted ship flies on in
		/// <see cref="PilotFlight"/>: its mass in kilotonnes, the thrust of its main drive (along the nose),
		/// its retro thrusters (against it) and its manoeuvre thrusters per axis (sideways and up/down),
		/// all in meganewtons, so an acceleration in km/s² is thrust / (mass × 1000). The AI's battle
		/// kinematics stay on <see cref="MaxSpeed"/>/<see cref="Accel"/>/<see cref="Decel"/>; these were
		/// set so each class's main drive pushes it as hard as its pilot tuning did before the hull had
		/// mass, with retro thrust a quarter weaker and manoeuvre thrust well under half.</summary>
		double MassKt = 0, MainThrustMN = 0, RetroThrustMN = 0, LateralThrustMN = 0, VerticalThrustMN = 0;

		/// <summary>Attitude torque, MN·km: about the pitch and yaw axes (one figure, the hull being
		/// long and roughly round in section) and about the roll axis along its length.</summary>
		double PitchYawTorqueMNKm = 0, RollTorqueMNKm = 0;

		/// <summary>Beam as a fraction of <see cref="LengthKm"/>, for the roll moment of inertia.</summary>
		double BeamFraction = 0.2;

		/// <summary>Moment of inertia about the pitch and yaw axes, kt·km²: a uniform rod of the hull's
		/// length (m L² / 12).</summary>
		double PitchYawInertia() const { return MassKt * LengthKm * LengthKm / 12; }

		/// <summary>Moment of inertia about the roll axis, kt·km²: a uniform cylinder of the hull's beam
		/// (m r² / 2), far smaller than pitch/yaw, so a hull rolls much more readily than it turns.</summary>
		double RollInertia() const
		{
			double r = LengthKm * BeamFraction * 0.5;
			return MassKt * r * r / 2;
		}

		cs::List<cs::Ref<SubsystemDef>> Layout;

		/// <summary>Fighter squadrons a carrier can launch (6 craft each).</summary>
		int Squadrons = 0;

		/// <summary>Recon fighters a carrier flies ahead of its squadrons (U.SPC-T485, see <see cref="ElectronicWarfare"/>),
		/// berthed in its hangars beside them.</summary>
		int ReconCraft = 0;

		/// <summary>Every craft its hangars hold: six to a squadron, and its recon flight.</summary>
		int HangarCraft() const { return Squadrons * 6 + ReconCraft; }

		/// <summary>Boarding shuttles a carrier keeps in its hangars beside its squadrons (U.SPC-T486, <see cref="BoardingShuttles"/>).</summary>
		int Shuttles = 0;

		/// <summary>People aboard: the ship's company, and the marines who board and repel boarders. The marines are the
		/// complement its marine habitats quarter (U.SPC-T465, <see cref="FacilityLayouts.Complement(ShipClass, CombatSpec)"/>).</summary>
		double Crew = 0, Marines = 0;

		/// <summary>Spinal power plants inside the hull (U.SPC-T490, <see cref="FacilityKinds.SpinalPlant"/>): what feeds its
		/// spinal mounts (<see cref="SpinalMounts.ChargeRate"/>).</summary>
		int SpinalPlants = 0;

		/// <summary>
		/// Propulsion (see <see cref="Propulsion"/>): the hull's mass with empty tanks and an empty hold, its
		/// tanks' capacity, both in tonnes, and the speed its fusion drive throws reaction mass out at, km/s.
		/// Torch-drive fiction on real rocketry: the rocket equation sets what every manoeuvre costs.
		/// </summary>
		double DryMassTonnes = 0, PropellantTonnes = 0, ExhaustVelocityKmS = 0;

		/// <summary>
		/// US-T269: the utility parts the hull was built with (<see cref="ShipComponents"/>), one entry per socket: what is
		/// fitted (a <see cref="UtilityKind"/>'s value, -1 for none), what of that still works (a part shot out with its
		/// component reads -1), and the component it sits in (<see cref="ShipComponents.ModuleKey"/>). With them the spec
		/// as it was before any utility part's effect (<see cref="UtilityBase"/>) and the complement an oxygen plant's share
		/// is of, so a part shot out can be taken off again. All empty (and null) for a hull with no utility parts, every
		/// stock design among them. Saved, as optional fields older saves lack.
		/// </summary>
		cs::List<int> UtilityFitted, Utility, UtilityModule;
		cs::Ref<CombatSpec> UtilityBase;
		double UtilityCrewBase = 0;

		/// <summary>US-T269: how much more heat the hull sheds for its heat sinks, as a share of what it sheds without
		/// (<see cref="Radiators.Shed"/>; a player's guns cool that much faster too). 0 with none.</summary>
		double HeatShedBonus = 0;

	private:
		/// The designs by class: the C# static field initialiser, then its static constructor (see Specs()).
		static const cs::Dictionary<ShipClass, cs::List<cs::Ref<CombatSpec>>>& Specs();
		static cs::Dictionary<ShipClass, cs::List<cs::Ref<CombatSpec>>>& SpecsField();

		/// <summary>Every class's marines are what its design's habitats quarter (U.SPC-T465, <see cref="MarineHabitats"/>), not
		/// a flat figure per class: a hull with no habitat carries none and repels boarders with its crew alone.</summary>
		static void StaticConstructor(cs::Dictionary<ShipClass, cs::List<cs::Ref<CombatSpec>>>& specs);

	public:
		/// <summary>A copy to refit (U.SPC-T65): every figure the same, the hardpoint list its own array
		/// (sharing the unchanged <see cref="SubsystemDef"/>s, which are never mutated).</summary>
		cs::Ref<CombatSpec> Clone() const;

		static const cs::Ref<CombatSpec>& Of(ShipClass c); // US-T42: a const& into the static table (C# returns the reference)

		/// <summary>Every design of a class (the construction catalogue lists each as its own hull frame, U.SPC-T377).</summary>
		static const cs::List<cs::Ref<CombatSpec>>& DesignsOf(ShipClass c);

		/// <summary>Picks one of this class's designs at random, for a ship that will keep it for its lifetime.</summary>
		static cs::Ref<CombatSpec> RandomOf(ShipClass c, cs::Random& rng);

		/// <summary>A settlement's orbital defence platform: a static gun station.</summary>
		static const cs::Ref<CombatSpec>& Platform();

		static constexpr double PlatformLengthKm = 1.4;
		SG_SERIALIZABLE(CombatSpec); // ObjectGraph registration (Persistence/Registrations)
	};
}
