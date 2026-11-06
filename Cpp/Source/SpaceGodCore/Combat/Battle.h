#pragma once
// Port of Unity.SpaceGod Core/Combat/Battle.cs (US-T10). Battle is a partial class: its other part,
// Core/Combat/BoardingShuttles.cs, is declared here too and defined in Combat/BoardingShuttles.cpp.

#include <array>

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Hyperlanes.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Faction;
	class Ship;
	class Settlement;
	class JumpGate;
	class ShipCombat;
	class Subsystem;
	class SubsystemDef;
	class WeaponSpec;
	class CombatSpec;
	class FighterState;
	class PowerDistribution;
	class BroadsideCommand;
	class Decoy;
	class MountControl;
	class FighterControl;
	class Battle;
	struct PilotTuning;
	struct HullDynamics;
	enum class SurrenderTerms : int;
	enum class StrikePhase : int;
	enum class ShipClass : int;
	enum class SubsystemKind : int;
	enum class PowerSystem : int;

	enum class UnitKind : int
	{
		Ship,
		Platform,
		Fighter,
	};
	SPACEGODCORE_API const char* ToString(UnitKind v);

	/// <summary>What a dogfighting small craft is doing (U.SPC-T274).</summary>
	enum class DogfightManeuver : int
	{
		/// <summary>Nose on the lead point, guns working, jinking on the way in.</summary>
		Attack,

		/// <summary>Through the merge and out the other side under boost, to come round for another pass.</summary>
		Extend,

		/// <summary>A hard turn away from a hostile on its tail.</summary>
		Break,

		/// <summary>US-T164 (Choreography): a strafing run on a capital, burning hard for a point off one end of the hull.</summary>
		StrafeBurn,

		/// <summary>US-T164: the run's drive cut (flight assist off), coasting down the hull's length with the guns on it.</summary>
		StrafeCoast,

		/// <summary>US-T164: past the far end, flipped onto the retrograde and burning back for the next pass.</summary>
		StrafeFlip,

		/// <summary>US-T164: through the merge, the nose pulled up and over the top to come round on the target's tail.</summary>
		Loop,

		/// <summary>US-T164: a rolling break, hard over one way.</summary>
		Roll,

		/// <summary>US-T164: a capital steaming across the enemy's line on one broadside until it comes about.</summary>
		Crossing,
	};
	SPACEGODCORE_API const char* ToString(DogfightManeuver v);

	/// <summary>One combatant in a battle. Positions are km from the battle's anchor, ecliptic axes.</summary>
	class SPACEGODCORE_API BattleUnit final
	{
	public:
		int Id = 0;
		UnitKind Kind = UnitKind::Ship;
		int Side = 0;
		cs::Ref<Core::Faction> Faction;
		cs::Ref<Core::Ship> Ship;
		cs::Ref<Settlement> Platform;

		/// <summary>The gate a fort unit guards (U.SPC-T92); its <see cref="Kind"/> is <see cref="UnitKind.Platform"/>, with no settlement.</summary>
		cs::Ref<JumpGate> Fort;
		cs::Ref<ShipCombat> Combat;
		double LengthKm = 0;

		DVec3 Pos, Vel;
		DVec3 Forward = DVec3::UnitX;

		/// <summary>How fast the AI is swinging the nose, rad/s about a world axis across it (U.SPC-T557, <see cref="Battle.TurnToward"/>);
		/// for a crew in a dogfight, its flight model's spin (U.SPC-T440).</summary>
		DVec3 TurnSpin;

		/// <summary>True while a human is flying this unit's <see cref="Ship"/> directly: <see cref="Battle.Move"/>
		/// skips its own AI steering/position-integration for it (the Runtime pilot loop drives <see cref="Pos"/>,
		/// <see cref="Vel"/> and <see cref="Forward"/> instead every frame) and <see cref="Battle.Fire"/> routes its
		/// forward-facing weapons through <see cref="PlayerFireHeld"/>/<see cref="PlayerLockTarget"/> rather than
		/// AI target selection. Side/aft turrets and point defence are unaffected and keep firing autonomously.</summary>
		bool PlayerControlled() const { return playerControlled; }
		void SetPlayerControlled(bool value)
		{
			playerControlled = value;
			if (value) PlayerFlown = true;
		}

	private:
		bool playerControlled = false;

	public:
		/// <summary>A human flew this unit at some point in the battle (kept after <see cref="PlayerControlled"/> is
		/// cleared, as when the hull is captured), so the law knows whose deeds its were (see <see cref="Law.OnBattleEnded"/>).</summary>
		bool PlayerFlown = false;

		/// <summary>While <see cref="PlayerControlled"/>: whether the player is holding the fire trigger this frame.</summary>
		bool PlayerFireHeld = false;

		/// <summary>While <see cref="PlayerControlled"/>: the target a forward missile launcher fires at, once
		/// <see cref="LockProgress"/> has made the lock. Direct-fire weapons ignore this and fire down the
		/// crosshair (<see cref="PlayerAim"/>) instead.</summary>
		cs::Ref<BattleUnit> PlayerLockTarget;

		/// <summary>While <see cref="PlayerControlled"/>: where the crosshair points (unit, ecliptic axes), the
		/// line the player's guns swing to meet (<see cref="Gunnery"/>, U.SPC-T270). Zero means down the nose.</summary>
		DVec3 PlayerAim;

		/// <summary>While <see cref="PlayerControlled"/>: the missile trigger held this frame.</summary>
		bool PlayerSecondaryHeld = false;

		/// <summary>The player's guns' heat, 0-1: firing builds it, and once it reaches 1 they are
		/// <see cref="Overheated"/> until it has fallen below <see cref="Gunnery.HeatRecoverBelow"/>.</summary>
		double Heat = 0;
		bool Overheated = false;

		/// <summary>A big ship's heat (U.SPC-T491, see <see cref="Radiators"/>): 0 cold, 1 with its heat sinks at their
		/// limit, past which its crew and systems cook. Zero always for a hull with no radiators to manage.</summary>
		double HullHeat = 0;

		/// <summary>The missile lock on <see cref="PlayerLockTarget"/>, 0-1: made at 1 (see <see cref="Gunnery.LockSeconds"/>).</summary>
		double LockProgress = 0;

		/// <summary>A dogfighting small craft's manoeuvre (see <see cref="Battle.Dogfight"/>, U.SPC-T274), the seconds
		/// it holds it for, and the way it breaks.</summary>
		DogfightManeuver Maneuver = DogfightManeuver::Attack;
		double ManeuverLeft = 0;
		int BreakSide = 1;

		/// <summary>The velocity gunners read this hull as having: its real one, eased in over
		/// <see cref="Gunnery.TrackingLagS"/>, so a steady course is tracked and a sudden jink is not (U.SPC-T270).</summary>
		DVec3 SeenVel;

		cs::Ref<BattleUnit> Target;
		double RetargetIn = 0;
		bool Fled = false, Dead = false;
		bool Fleeing = false;

		/// <summary>True once this hull has struck its colours: holds fire and waits on its captor's
		/// terms (<see cref="SurrenderOutcome"/>) rather than fighting to the last. See <see cref="Morale"/>.</summary>
		bool Surrendering = false;

		/// <summary>Seconds spent hailing since <see cref="Surrendering"/> began; past
		/// <see cref="Battle.SurrenderDecisionSeconds"/> the battle decides terms itself if nobody chooses first.</summary>
		double SurrenderTimer = 0;

		/// <summary>What its captor decided once it struck its colours, or null while still awaiting terms.</summary>
		cs::Nullable<SurrenderTerms> SurrenderOutcome;

		/// <summary>Set once its captor chooses to take the ship: open to boarding exactly like a disabled hull.</summary>
		bool SurrenderOpenToBoarding = false;

		/// <summary>This unit's own breaking point, 0-1, rolled once at creation so otherwise-identical
		/// hulls do not all strike their colours on the same tick under the same pressure. See <see cref="Morale.Pressure"/>.</summary>
		double MoraleThreshold = 0;

		/// <summary>Pirates hailing this hull with "your cargo or your life": who is asking, or null. Answered through
		/// <see cref="Battle.AnswerCargoDemand"/> (at once by an AI hull); unanswered, it lapses into a fight.</summary>
		cs::Ref<Core::Faction> CargoDemandedBy;

		/// <summary>Seconds the current <see cref="CargoDemandedBy"/> has gone unanswered.</summary>
		double CargoDemandTimer = 0;

		/// <summary>Set once a cargo demand has been made of this hull, so pirates ask only once a battle.</summary>
		bool CargoDemandMade = false;

		/// <summary>Set when this hull turned down (or let lapse) a pirate cargo demand: the raiders no longer spare it once
		/// it lies dead in space (U.SPC-T374, see <see cref="RaidDoctrine"/>).</summary>
		bool CargoRefused = false;

		/// <summary>Formation: the unit this one keeps station on, and where (km, in the leader's frame).</summary>
		cs::Ref<BattleUnit> Leader;
		DVec3 Slot;

		/// <summary>Per-unit randomness: circling direction and a phase for station-keeping wander.</summary>
		double Wander = 0, Circle = 0;

		/// <summary>Fighter-only state (see the fighter AI).</summary>
		cs::Ref<FighterState> Fighter;

		/// <summary>The carrier's craft this launched unit is (U.SPC-T526, see <see cref="CarrierWings"/>): the hull it flies
		/// on (its <see cref="Combat"/> is that hull's own) and docks again as when it lands. Null for any other unit (it is set for a
		/// carrier's boarding shuttles, U.SPC-T590). Not <see cref="Ship"/>, which is only ever a hull in a fleet's list.</summary>
		cs::Ref<Core::Ship> CraftHull;

		/// <summary>Boarding: the disabled enemy this ship is closing on to board, and the one it is latched to.</summary>
		cs::Ref<BattleUnit> BoardTarget, Latched;

		/// <summary>Where it is fixed to the latched hull (ship lengths in that hull's frame), and the countdown to opening the airlock.</summary>
		DVec3 LatchLocal;
		double LatchTimer = 0;
		/// <summary>Which flank of its boarding target this unit docks on (+1 starboard, -1 port), fixed when the approach starts (U.SPC-T567).</summary>
		double DockSide = 0;

	private:
		cs::Ref<BattleUnit> dockSideFor;

	public:
		/// <summary>The dock point off <paramref name="t"/>'s flank facing this unit when it began closing, kept thereafter so it never flies through the hull.</summary>
		DVec3 DockPoint(const cs::Ref<BattleUnit>& t, double standOffKm);

		/// <summary>Seconds until its Breacher bays can fire the next Breacher round (U.SPC-T466, <see cref="BreacherBays.IntervalSeconds"/>).</summary>
		double BreacherBayReload = 0;

		/// <summary>Seconds until a carrier can send its next boarding shuttle out of its hangar (U.SPC-T486, <see cref="BoardingShuttles"/>).</summary>
		double ShuttleLaunchIn = 0;

		/// <summary>A carrier's wing as the player ordered it from the helm (US-T106, <see cref="HelmAbilities"/>): held aboard
		/// when the hull came into the fight with the player at its helm (launched squadron by squadron on order), recalled
		/// to land and stay aboard, and the standing call to its craft (<see cref="HelmWingCall"/>: 0 their own fights, 1 the
		/// helm's locked hull, 2 guard the carrier). They lapse, and its captain launches what is held, once nobody holds the
		/// helm or flies one of its craft (<see cref="Battle.HelmOrdersLapse"/>). Never set by the AI.</summary>
		bool WingHeld = false, WingRecalled = false;
		int WingCall = 0;

		/// <summary>Taken by boarders: it has changed sides and carries a prize crew, so it no longer fights.</summary>
		bool Captured = false;

		/// <summary>The side a captured hull fought on before it was taken (its <see cref="Side"/> is now its captor's); -1 until then.</summary>
		int TakenFromSide = -1;

		/// <summary>The side it fought on: the one it was taken from, if it was captured.</summary>
		int FoughtOn() const { return Captured && TakenFromSide >= 0 ? TakenFromSide : Side; }

		/// <summary>A fighter that has landed back aboard its carrier.</summary>
		bool Docked = false;

		/// <summary>Seconds left with its systems knocked out by an interdiction pulse or an EMP: drives, guns and
		/// shield regeneration all down until it comes back.</summary>
		double Stunned = 0;

		/// <summary>The command ship on its side it fights coordinated under this step, or null (U.SPC-T477, see
		/// <see cref="FleetCoordination"/>).</summary>
		cs::Ref<BattleUnit> CommandedBy;

		/// <summary>Seconds left in the confusion after the command ship coordinating it was lost (U.SPC-T477).</summary>
		double Disrupted = 0;

		/// <summary>Where an alpha-striking hull (a frigate, U.SPC-T475) is in its run: burning in, or burning back out after its
		/// volley (see <see cref="AlphaStrike"/>).</summary>
		StrikePhase Strike{};

		bool Present() const { return !Dead && !Fled && !Docked; }
		double Radius() const { return LengthKm * 0.5; }
		cs::String Name() const;

		/// <summary>While <see cref="PlayerControlled"/>: the EMP trigger held, and a mine asked for this frame (the
		/// battle clears it once it has tried, U.SPC-T271).</summary>
		bool PlayerEmpHeld = false, PlayerMineRequested = false;

		/// <summary>While <see cref="PlayerControlled"/>: a capital ship at the helm (U.SPC-T272). Its crews work every
		/// mount, favouring the locked hull, and the player calls broadsides: holding the trigger has the battery
		/// bearing on <see cref="PlayerVolleyAim"/> hold its fire and build a volley, and letting go fires it together.</summary>
		bool PlayerHelm = false;

		/// <summary>At the helm: where the player is looking (unit, ecliptic axes), the side a volley is called on.</summary>
		DVec3 PlayerVolleyAim;

		/// <summary>At the helm: the trigger let go this frame, firing the volley (cleared once the battle has fired it).</summary>
		bool PlayerVolleyRelease = false;

		/// <summary>At the helm: seconds the volley has been building, up to <see cref="Battle.VolleyBuildSeconds"/>.</summary>
		double VolleyCharge = 0;

		/// <summary>How a small craft shares its reactor between weapons, shields and engines (U.SPC-T424); null runs
		/// everything as designed.</summary>
		cs::Ref<PowerDistribution> Power;

		/// <summary>While <see cref="PlayerControlled"/>: the weak point on <see cref="PlayerLockTarget"/> its missiles and
		/// torpedoes are aimed at (U.SPC-T424), or null to leave it to the seeker.</summary>
		cs::Ref<Subsystem> PlayerSubTarget;

		/// <summary>While <see cref="PlayerControlled"/>: a flare-and-chaff cloud asked for this frame (the battle clears it
		/// once it has tried, U.SPC-T424).</summary>
		bool PlayerDecoyRequested = false;

		/// <summary>A capital ship's orders in the broadside game (U.SPC-T425): orders to its guns, focus fire on a weakened shield patch, the
		/// brace, damage control and the formation it leads, given by the player at the helm or by its captain's doctrine.
		/// Null for anything smaller (see <see cref="Broadsides.Of"/>).</summary>
		cs::Ref<BroadsideCommand> Broadside;

		/// <summary>US-T135: the damage control teams out on the plating as EVA workers the player holds (a bit per team,
		/// <see cref="RepairWorker"/>): out of the pool damage control works with while held. 0 whenever no player holds one
		/// (every crew, the AI and the Bench); not saved, as no seat is held across a load.</summary>
		int EvaHeldTeams = 0;

		/// <summary>The roof of a hull flown at the helm, rolled however its captain has rolled it (U.SPC-T499: capitals fight
		/// in full 3D, rolling a broadside onto a ship above or below); read only while <see cref="PlayerControlled"/>.
		/// Zero is held level, roof toward ecliptic north; a hull flown by the battle takes its captain's <see cref="Roof"/> instead.</summary>
		DVec3 Up;

		/// <summary>The roof of a hull the battle flies, rolled however its captain has rolled it to keep its weak patches
		/// out of the heaviest fire (U.SPC-T502, see <see cref="WeakPatches"/>); read only while not
		/// <see cref="PlayerControlled"/>. Zero holds it level, roof toward ecliptic north.</summary>
		DVec3 Roof;

		/// <summary>Right and up axes of the hull (nose along <see cref="Forward"/>, up toward ecliptic north, or the helm's
		/// own roof, <see cref="Up"/>, or its captain's, <see cref="Roof"/>): the frame its fire cones, bearings and hardpoints are reckoned in.</summary>
		void Basis(DVec3& right, DVec3& up) const;

		/// <summary>Battle-local position of a hardpoint.</summary>
		DVec3 PointOf(const cs::Ref<SubsystemDef>& d) const;

		/// <summary>Battle-local position of a point given in ship lengths (right, up, forward).</summary>
		DVec3 PointLocal(DVec3 local) const;

		/// <summary>A battle-local point expressed in ship lengths in this hull's frame.</summary>
		DVec3 ToLocal(DVec3 point) const;

		/// <summary>A battle-local direction expressed in this hull's frame (right, up, forward), unscaled.</summary>
		DVec3 DirToLocal(DVec3 dir) const;

		/// <summary>
		/// Where on this hull, in ship lengths in its frame, a strike at battle-local <paramref name="point"/> travelling
		/// along <paramref name="incoming"/> meets the shield (U.SPC-T501, see <see cref="ShieldEmitters.StrikePoint"/>):
		/// the patch whose emitter's field takes it.
		/// </summary>
		DVec3 ShieldStrike(DVec3 point, cs::Nullable<DVec3> incoming = std::nullopt) const;

		/// <summary>A direction in this hull's frame (right, up, forward) as a battle-local direction, unscaled.</summary>
		DVec3 LocalDirToWorld(DVec3 local) const;

		/// <summary>Whether the mount <paramref name="d"/> on this hull bears on a battle-local point: the point lies inside its
		/// fire cone seen from where the mount sits, above or below the hull as well as to the side (U.SPC-T533).</summary>
		bool Bears(const cs::Ref<SubsystemDef>& d, DVec3 point) const;

		/// <summary>Whether the mount <paramref name="d"/> on this hull bears along a battle-local direction (a line of sight
		/// off the hull, with no point at its end).</summary>
		bool BearsAlong(const cs::Ref<SubsystemDef>& d, DVec3 dir) const;

		/// <summary>Yaw angle (degrees) of a battle-local point relative to this hull's own facing:
		/// 0 = dead ahead, 90 = starboard, 180 = astern, 270 = port.</summary>
		double YawDegOf(DVec3 point) const;
		SG_SERIALIZABLE(BattleUnit); // ObjectGraph registration (Persistence/Registrations)
	};

	enum class MissileKind : int
	{
		Missile,
		Torpedo,
		Pod,

		/// <summary>An EMP round: it bursts beside its target and stuns rather than damages (U.SPC-T271).</summary>
		Emp,

		/// <summary>An antimatter (photon) torpedo (U.SPC-T489): a heavy guided warhead, tougher than a torpedo to stop.</summary>
		Antimatter,

		/// <summary>A screen's interceptor (U.SPC-T489): it hunts a hostile round (<see cref="Missile.Prey"/>) or a fighter.</summary>
		Interceptor,

		/// <summary>A Breacher round (U.SPC-T466): a marine team fired from a Breacher bay, flying like a bullet with only a
		/// slight course correction until it strikes a hull and drills in (<see cref="BreacherBays"/>).</summary>
		Breacher,

		// The full arsenal (US-T101), appended. Missile above is the homing missile: it steers onto its target.

		/// <summary>A dumb-fire rocket: no target and no seeker, it flies straight and hits the first hostile hull it meets.</summary>
		Rocket,

		/// <summary>A hive round in its first moment of flight, before it splits into its swarm (<see cref="Missile.SplitIn"/>).</summary>
		Hive,

		/// <summary>One of a hive round's micro-missiles: a small, frail homing seeker.</summary>
		HiveMicro,
	};
	SPACEGODCORE_API const char* ToString(MissileKind v);

	/// <summary>
	/// A proximity mine (U.SPC-T271): it sits where it was laid, arms after a moment, and blows when a hull hostile to
	/// its layer comes within its trigger, hurting everything hostile within its blast.
	/// </summary>
	class Mine final
	{
	public:
		DVec3 Pos, Vel;
		int Side = 0;
		double Damage = 0, BlastKm = 0;

		/// <summary>Seconds until it is armed, and until it fizzles out unexploded.</summary>
		double ArmIn = 0, Life = 0;
		bool Gone = false;

		bool Armed() const { return ArmIn <= 0; }
	};

	/// <summary>A boarding pod or Breacher round stuck in a hull: where (ship lengths in that hull's frame), whose, and pointing which way.</summary>
	class Embedded final
	{
	public:
		cs::Ref<BattleUnit> Unit;
		DVec3 Local;
		DVec3 Inward;
		int Side = 0;
		bool Breacher = false;
	};

	/// <summary>A battle fought in open space beside a hyperlane, after an interdiction pulse.</summary>
	class LaneSite final
	{
	public:
		LaneHop Hop;
		DVec3 FromOffset, ToOffset;
		double Progress = 0;

		/// <summary>Offset of the fight from the lane line, km (the ambushers wait just outside it).</summary>
		DVec3 Side;
	};

	class Missile final
	{
	public:
		DVec3 Pos, Vel;
		cs::Ref<BattleUnit> Target;
		cs::Ref<Subsystem> TargetSub;
		double Damage = 0, Speed = 0, Hp = 0, Life = 0;
		int Side = 0;
		bool Torpedo = false;
		bool Gone = false;
		MissileKind Kind = MissileKind::Missile;

		/// <summary>A pod's squad: who they fight for and how good they are.</summary>
		cs::Ref<Core::Faction> Owner;
		double Marines = 0;

		/// <summary>The ship that fired a pod, so a squad that cannot land can go home.</summary>
		cs::Ref<BattleUnit> Source;

		/// <summary>An EMP round's stun and reach (see <see cref="WeaponSpec.EmpSeconds"/>).</summary>
		double EmpSeconds = 0, EmpRadiusKm = 0;

		/// <summary>Fired by a small craft, so it hits a capital ship's weak points as hard as one (U.SPC-T424, see <see cref="WeakPoints"/>).</summary>
		bool SmallCraft = false;

		/// <summary>The flare-and-chaff cloud it has been drawn off onto, or null (U.SPC-T424).</summary>
		cs::Ref<Core::Decoy> Decoy;

		/// <summary>The hostile round an interceptor is hunting (U.SPC-T489), or null for one sent after a fighter.</summary>
		cs::Ref<Missile> Prey;

		/// <summary>A Breacher round that missed or glanced off raised shields (U.SPC-T519): tumbling on through space, a
		/// derelict with its team still alive inside, drilling into nothing now. Point defence can still shoot it out of the
		/// sky; once the fight is over it is left drifting for a tow to recover or pirates to salvage (<see cref="DriftingRounds"/>).</summary>
		bool Adrift = false;

		/// <summary>A hive round (US-T101): seconds of flight left before it splits, and the micro-missiles it splits into
		/// (0 for a round that stays whole).</summary>
		double SplitIn = 0;
		int SplitInto = 0;
	};

	/// <summary>
	/// A railgun slug or autocannon burst in flight: a dumb, non-homing round that was aimed, with a
	/// lead, at where its target was expected to be, and takes real time crossing the range instead of
	/// landing the instant the trigger was pulled. <see cref="Hit"/> is decided at the muzzle, same as a
	/// beam's accuracy roll; a round that "hit" only actually calls <see cref="Battle.ApplyDamage"/> once
	/// it arrives, so a target that dies or peels off first takes nothing. A round that missed keeps
	/// flying past its aim point and fades, rather than never having existed.
	/// </summary>
	class KineticRound final
	{
	public:
		DVec3 Pos, Vel;
		cs::Ref<BattleUnit> Target;
		cs::Ref<Subsystem> TargetSub;
		double Damage = 0;

		/// <summary>Seconds left before the round reaches its aim point (where it resolves, if it hit).</summary>
		double ArriveIn = 0;

		/// <summary>Seconds left before the round is removed -- longer than <see cref="ArriveIn"/> for a
		/// miss, so it keeps travelling past the target before it fades.</summary>
		double Life = 0;

		int Side = 0;
		bool Hit = false;

		/// <summary>True for a railgun slug, false for fighter autocannon fire -- cosmetic only (Runtime
		/// draws the two differently).</summary>
		bool Railgun = false;
		bool Gone = false;

		/// <summary>An aimed round (U.SPC-T270): no roll at the muzzle and no <see cref="Target"/>; it hits
		/// whatever hostile hull it actually flies through, or nothing (see <see cref="Gunnery"/>).</summary>
		bool Aimed = false;

		/// <summary>Fired by a small craft (U.SPC-T424, see <see cref="WeakPoints"/>).</summary>
		bool SmallCraft = false;

		/// <summary>A vulcan's round (US-T101): drawn as a hot tracer rather than a plain gun round -- cosmetic only.</summary>
		bool Tracer = false;
	};

	enum class FxKind : int
	{
		Beam,
		BeamMiss,

		/// <summary>A railgun's hypervelocity kinetic round -- a harder, thinner, whiter streak than a
		/// laser <see cref="Beam"/>.</summary>
		Railgun,
		RailgunMiss,
		PointDefense,
		Guns,
		ShieldHit,
		Impact,
		SubsystemDestroyed,
		MissileKill,
		UnitDestroyed,
		MissileLaunch,

		/// <summary>A pod or Breacher round holes a hull: a burst of venting atmosphere.</summary>
		Breach,
		PodBounce,
		Captured,
		Latch,

		/// <summary>An interdiction pulse: an expanding electromagnetic shell.</summary>
		Pulse,

		/// <summary>A hull breaking off, weapons powered down, hailing to ask terms.</summary>
		Hail,

		/// <summary>A mine going off (Size: its blast, km).</summary>
		MineBlast,

		/// <summary>An EMP round bursting (Size: its reach, km).</summary>
		EmpBurst,

		/// <summary>A mine laid.</summary>
		Deployed,

		/// <summary>A tachyon lance's shot (U.SPC-T487): there and gone in the same instant along its whole length, a
		/// wide, violet-white line from the bow to what it struck.</summary>
		TachyonLance,
		TachyonLanceMiss,

		/// <summary>A phaser array's beam (U.SPC-T488): Size is its damage, so a lance draws as one hard line and a
		/// sweep as a thin flicker.</summary>
		Phaser,
		PhaserMiss,

		/// <summary>A plasma cannon's bolt (U.SPC-T488): Size is the damage it landed with, after its bleed over distance.</summary>
		Plasma,
		PlasmaMiss,

		/// <summary>A flak shell (U.SPC-T489): its tracer from the cannon (A) to its burst (B), Size the burst, km.</summary>
		FlakBurst,

		// The full arsenal (US-T101), appended.

		/// <summary>A vulcan's burst of tracers, from the muzzle (A) down the line it was fired on (B).</summary>
		Vulcan,

		/// <summary>A hive round splitting (A, heading towards B): a flash and its swarm fanning out, Size the micro-missiles.</summary>
		HiveSplit,
	};
	SPACEGODCORE_API const char* ToString(FxKind v);

	/// <summary>Something to draw: a beam, a hit, an explosion. A and B are battle-local km.</summary>
	struct CombatFx
	{
		int Seq = 0;
		FxKind Kind = FxKind::Beam;
		DVec3 A, B;
		double Size = 0;
		int Side = 0;
		cs::Ref<BattleUnit> Unit;

		/// <summary>The subsystem index concerned, or -1: the hardpoint hit or lost, or for a
		/// <see cref="FxKind.ShieldHit"/> the shield emitter whose patch took the hit.</summary>
		int Sub = 0;
	};

	class Interior;
	class BodyDefinition;

	/// <summary>
	/// A fight at one body. It runs a tactical simulation in a small local space (tens of km) anchored
	/// in orbit: each hull manoeuvres to its weapons' range, every hardpoint picks targets and fires,
	/// lasers and missiles hit particular subsystems on the far hull, point defence shoots missiles and
	/// fighters down, and damaged ships break off and run. Outcomes are real: what dies is gone, what
	/// runs carries its damage home. Seconds are simulated seconds; at high time warp the battle takes
	/// coarser steps rather than holding the clock back.
	/// </summary>
	/// <remarks>C# passes the battle itself (`this`) to hooks and helpers; here that is shared_from_this(), so a
	/// Battle must be made with cs::New.</remarks>
	class SPACEGODCORE_API Battle final : public std::enable_shared_from_this<Battle>
	{
	public:
		int Id = 0;
		int Body = 0;
		cs::String Name;

		/// <summary>Where the fight is: an offset (km, ecliptic) from the body's centre, fixed in its frame.</summary>
		DVec3 Anchor;

		/// <summary>This battle's line-of-approach axis; fixed at creation so late joiners always use the right axis.</summary>
		DVec3 ApproachAxis = DVec3::UnitX;

		double StartJd = 0;
		double Seconds = 0;
		cs::List<cs::Ref<BattleUnit>> Units;
		cs::List<cs::Ref<Missile>> Missiles;
		cs::List<cs::Ref<KineticRound>> Rounds;
		cs::List<cs::Ref<Mine>> Mines;

		/// <summary>Flare-and-chaff clouds burning in the battle (U.SPC-T424).</summary>
		cs::List<cs::Ref<Decoy>> Decoys;

		/// <summary>The recon fighters in the fight this step (U.SPC-T485, see <see cref="ElectronicWarfare.Sweep"/>): who jams and paints.</summary>
		cs::List<cs::Ref<BattleUnit>> ReconAloft;
		cs::List<cs::Ref<Core::Embedded>> Embedded;

		/// <summary>Gun mounts the player has taken (U.SPC-T18, see <see cref="MountedTurret"/>): each is laid and fired by
		/// <see cref="FireManned"/> instead of its crew. Kept on the battle, so nothing stays manned past the fight.</summary>
		cs::Dictionary<cs::Ref<Subsystem>, cs::Ref<MountControl>> Manned;

		/// <summary>Launched small craft the player is flying (U.SPC-T422, see <see cref="FighterSeat"/>): each is flown by
		/// <see cref="FighterAI.FlyManned"/> on its stick instead of by its pilot. Kept on the battle, like <see cref="Manned"/>.</summary>
		cs::Dictionary<cs::Ref<BattleUnit>, cs::Ref<FighterControl>> Flown;

		/// <summary>Set for an ambush beside a hyperlane; null for a battle at a body.</summary>
		cs::Ref<LaneSite> Site;

		/// <summary>The side that started the fight (every battle the war starts puts its aggressor on side 0); a
		/// lawful ship on the other side was defending itself (see <see cref="Law.OnBattleEnded"/>).</summary>
		int AggressorSide = 0;

		/// <summary>The side enforcing the law (a patrol interdicting a wanted ship, see <see cref="War.TryStartInterdiction"/>), or -1.</summary>
		int EnforcingSide = -1;

		/// <summary>Raised when boarders take a unit (the world decides what they do with it).</summary>
		cs::Action<cs::Ref<Battle>, cs::Ref<BattleUnit>, cs::Ref<Faction>> UnitCaptured;
		cs::List<CombatFx> Fx;
		cs::List<cs::Ref<Faction>> SideLeaders = cs::NewArray<cs::Ref<Faction>>(2);

		/// <summary>How much of their usual resolve each side's crews keep (1 normally): facing a feared outlaw
		/// they strike their colours sooner (see <see cref="Outlaws.FearedResolve"/>).</summary>
		cs::List<double> Dread = { 1, 1 };

		bool Over = false;

		/// <summary>True until the battle's first advance: it does not run in the frame it starts, so an observer can slow the clock first.</summary>
		bool Fresh = true;

		/// <summary>The side that held the field (0 or 1), or -1 if both broke off.</summary>
		int Winner = -1;

		int FxSeq = 0;

		/// <summary>Raised when a unit is destroyed (the world books the loss).</summary>
		cs::Action<cs::Ref<Battle>, cs::Ref<BattleUnit>> UnitDestroyed;

		/// <summary>Raised the moment a hull strikes its colours: breaks off, powers down weapons and hails.</summary>
		cs::Action<cs::Ref<Battle>, cs::Ref<BattleUnit>> UnitSurrendered;

		/// <summary>Raised once a captor's terms for a surrendered hull are settled -- auto-decided or
		/// chosen externally via <see cref="OfferSurrenderTerms"/> -- so the world can apply the
		/// consequences (cargo ransomed, a clean getaway; a taken prize follows the normal capture hook).</summary>
		cs::Action<cs::Ref<Battle>, cs::Ref<BattleUnit>, cs::Ref<Faction>, SurrenderTerms> SurrenderDecided;

		/// <summary>Raised when pirates demand a player-flown hull's cargo (the hull, the pirates).</summary>
		cs::Action<cs::Ref<Battle>, cs::Ref<BattleUnit>, cs::Ref<Faction>> CargoDemanded;

		/// <summary>Fighter AI hook, run each step for every fighter (see FighterAI).</summary>
		cs::Action<cs::Ref<Battle>, cs::Ref<BattleUnit>, double> FighterStep;

		/// <summary>Carriers launch their squadrons through this (see FighterAI).</summary>
		cs::Action<cs::Ref<Battle>, cs::Ref<BattleUnit>> Launch;

		static constexpr double EscapeKm = 55;
		static constexpr double MaxStep = 1.0;

		/// <summary>How long a hailing hull waits before the battle decides terms itself if nobody has.</summary>
		static constexpr double SurrenderDecisionSeconds = 15;

		/// <summary>How long an AI captor takes to answer a hail when no player-flown hull is there to choose.</summary>
		static constexpr double SurrenderAiDecisionSeconds = 4;

	private:
		cs::List<double> sideStrength = cs::NewArray<double>(2);
		static constexpr int MaxStepsPerAdvance = 500;
		static constexpr double MaxSeconds = 4 * 3600;

		cs::Random rng;
		int nextUnitId = 0;
		double accumulator = 0;

	public:
		Battle(int id, int body, cs::String name, DVec3 anchor, double startJd, int seed);

		cs::Random& Rng() { return rng; }

		// ---------------------------------------------------------------- membership

		cs::Ref<BattleUnit> AddShip(const cs::Ref<Ship>& ship, int side, DVec3 pos, DVec3 forward);

		cs::Ref<BattleUnit> AddPlatform(const cs::Ref<Settlement>& s, const cs::Ref<ShipCombat>& platform, int side, DVec3 pos);

		/// <summary>A gate fort joins the fight for its gate's owner (U.SPC-T92).</summary>
		cs::Ref<BattleUnit> AddFort(const cs::Ref<JumpGate>& gate, const cs::Ref<ShipCombat>& fort, int side, DVec3 pos);

		cs::Ref<BattleUnit> NewUnit(UnitKind kind, int side, const cs::Ref<Faction>& faction, const cs::Ref<ShipCombat>& combat, double lengthKm, DVec3 pos, DVec3 forward);

		cs::Ref<BattleUnit> UnitOf(const cs::Ref<Ship>& ship);

		bool Hostile(const cs::Ref<BattleUnit>& a, const cs::Ref<BattleUnit>& b) const;

		// ---------------------------------------------------------------- stepping

		/// <summary>A step (seconds) of a battle fought at coarse detail, in a system nobody is looking at (U.SPC-T104).</summary>
		static constexpr double CoarseStep = 15;

	private:
		/// <summary>The most coarse steps one advance takes: enough for six hours, the coarse worlds' own step.</summary>
		static constexpr int MaxCoarseStepsPerAdvance = 1600;

	public:
		/// <summary>
		/// Fights <paramref name="seconds"/> more of the battle: in steps of a second (<see cref="MaxStep"/>), or
		/// at <paramref name="coarse"/> detail (a system nobody is looking at, see <see cref="World.Coarse"/>) in
		/// steps of <see cref="CoarseStep"/>, the same fight resolved fifteen times more cheaply. A battle can go
		/// from one to the other at any step and carries on as it stands.
		/// </summary>
		void Advance(double seconds, bool coarse = false);

	private:
		void Step(double dt);

		/// <summary>
		/// Over when one side has nobody left on the field (dead or got away), when neither side has a
		/// gun it is willing to use, or after four hours. Those running are still fair game until they
		/// get clear, so a rout is a pursuit, not an armistice.
		/// </summary>
		void CheckOver();

		/// <summary>A ship our side is boarding, or about to board: worth more taken than destroyed.</summary>
		bool IsPrize(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target);

		/// <summary>A raider or warship with marines to spare is a combatant even with no guns.</summary>
		bool CanBoard(const cs::Ref<BattleUnit>& u);

	public:
		/// <summary>Whether a unit holds its fire on a disabled foe it wants taken alive (a bounty hunter after its
		/// quarry's captain, U.SPC-T63) while its side can still board it, or null.</summary>
		cs::Func<bool, cs::Ref<BattleUnit>, cs::Ref<BattleUnit>> WantsAlive;

		/// <summary>Whether a unit sends boarders across though its faction is neither a navy nor raiders (bounty hunters), or null.</summary>
		cs::Func<bool, cs::Ref<BattleUnit>> Boards;

	private:
		/// <summary>Whether <paramref name="u"/> would board <paramref name="target"/>: a navy or raiders board anything,
		/// a hunter only the quarry it wants alive (never a lawful hull: that would be piracy).</summary>
		bool MayBoard(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target);

		/// <summary>Whether anyone on <paramref name="side"/> can still send a boarding party.</summary>
		bool SideCanBoard(int side);

	public:
		/// <summary>The shooting is over; boarding parties and landing fighters are finishing up.</summary>
		bool Mopping = false;

	private:
		/// <summary>A boarding fight or a boarding approach is under way somewhere in the battle.</summary>
		bool Boarding() const;

		/// <summary>Fighters still flying that have a carrier to land on. One the player is flying never lands on its own, so
		/// it does not hold the fight open: it is taken back aboard with the rest when the fight ends (see War).</summary>
		bool FightersAloft() const;

		double NearestHostileDistance(const cs::Ref<BattleUnit>& u);

		// ---------------------------------------------------------------- decisions

		void Think(const cs::Ref<BattleUnit>& u, double dt);

		bool Routed(int side);

	public:
		/// <summary>Score multiplier for the hull already being fought; beats the +/-15% jitter so a tie doesn't flip-flop.</summary>
		static constexpr double TargetStickiness = 0.7;

		cs::Ref<BattleUnit> PickTarget(const cs::Ref<BattleUnit>& u);

	private:
		/// <summary>The side's flagship: its working command ship if it has one, where the fleet's senior officers are
		/// (U.SPC-T477), else its biggest hull.</summary>
		cs::Ref<BattleUnit> SideFlagship(int side);

		void Move(const cs::Ref<BattleUnit>& u, double dt);

	public:
		// ---------------------------------------------------------------- dogfighting (U.SPC-T274)

		/// <summary>The share of a hull's own turn rate at the helm (<see cref="PilotTuning.TurnRateDegS"/>) its crew
		/// flies it at in a dogfight: a human's hands are quicker than a crew's reactions. US-T180: 0.8 (was 0.6), so the
		/// crewed fighters carve round the capitals with the carriers' squadrons, still a shade under the player's hands.</summary>
		static constexpr double DogfightTurnShare = 0.8;

		/// <summary>How close the merge is, km (inside it the attacker extends), how far it extends before coming
		/// round, and how near a hostile behind must be to count as on its tail.</summary>
		static constexpr double MergeKm = 0.3; static constexpr double ExtendKm = 1.6; static constexpr double TailKm = 2.5;

		/// <summary>How far off a dogfighter goes for the player in preference to whatever else it was after, km.</summary>
		static constexpr double HuntPlayerKm = 8;

	private:
		/// <summary>The small craft that dogfight: the light fighters and interceptors. A heavy fighter (the gunship) brawls
		/// on the ordinary circling approach, its crewed turrets firing all round, and a bomber flies at the hulls.</summary>
		static bool Dogfights(const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// A light fighter flown by its crew in a dogfight: on the same airframe the player flies (its helm turn rate,
		/// cut to <see cref="DogfightTurnShare"/>, and its own thrust) and through the same flight model, its class's
		/// (<see cref="FlightModelRegistry.For"/>, U.SPC-T440), so it carves, corners and boosts as the player's would. It lays
		/// its nose on the lead point for its guns and jinks on the way in; through the merge it extends under boost and comes
		/// round again; with a hostile on its tail and pointed at it, it breaks hard to one side under boost. It goes for the
		/// player first.
		/// </summary>
		void Dogfight(const cs::Ref<BattleUnit>& u, double dt);

		/// <summary>
		/// One frame of a crew at the stick of <paramref name="u"/> (U.SPC-T440): its class's flight model (the one the player
		/// flies it on) with flight assist on, the nose steered toward <paramref name="goal"/> at the crew's share of the helm
		/// turn rate, the throttle at <paramref name="throttle"/> of top speed and the boost held on <paramref name="boost"/>
		/// (drawing on the hull's own charge), all scaled by its engines.
		/// </summary>
		/// US-T164: <paramref name="coast"/> cuts the drive with flight assist off (the strafing run's free fall), and
		/// <paramref name="roll"/> is the crew's roll input; the roof the model leaves it with is the one it is drawn with.
		static void FlyCrewed(const cs::Ref<BattleUnit>& u, ShipClass cls, const PilotTuning& tune, const HullDynamics& hull, double engines, DVec3 goal, double throttle, bool boost, double dt,
			bool coast = false, double roll = 0);

		/// <summary>A hostile close behind <paramref name="u"/> with its nose on it, or null.</summary>
		cs::Ref<BattleUnit> OnItsTail(const cs::Ref<BattleUnit>& u);

		/// <summary>The fastest round a hull's forward guns fire, km/s; 0 if they are beams.</summary>
		static double RoundSpeedOf(const cs::Ref<ShipCombat>& c);

		/// <summary>
		/// Which way to point the bow so the guns whose fire cones can take the target carry the most firepower onto it
		/// (U.SPC-T533, see <see cref="ConeFirepower"/>). So a broadside warship turns a flank in, not its nose, while a hull
		/// whose guns bear best ahead, or bear as well any way (or any hull with <see cref="FireCones.AiBringsGunsToBear"/>
		/// off), simply points its nose at the target. An AI captain also weighs laying its hull slanted to the target's fire
		/// so the hits glance off its armour (<see cref="ArmourAngling.AiWeight"/>, U.SPC-T534): a broadside hull keeps its
		/// flank in but angled off the line of fire, as long as its guns still bear.
		/// </summary>
		DVec3 BroadsideFacing(const cs::Ref<BattleUnit>& u, DVec3 toTarget);

	public:
		/// <summary>
		/// The headings a captain could turn its hull to so its guns bear on a target along <paramref name="toTarget"/>
		/// (U.SPC-T533): by the bearing off the bow the target would then lie on, whole degrees (0 = the bow on it, 90 =
		/// starboard; see <see cref="BowFor"/>), the firepower (damage a second) of every ready gun (not on cooldown, with
		/// ammo, its mount not destroyed) whose fire cone would hold the target from that heading, in 3D. The candidates are
		/// the bow and each ready gun's cone axis swung round onto the target; a cone centred straight up or down off the hull
		/// is brought to bear by no turn of the bow. With <paramref name="slant"/> the strongest candidates are also swung
		/// <see cref="ArmourAngling.SlantDeg"/> either way, headings that lay the hull slanted to the target's fire
		/// (U.SPC-T534). Null for a hull with no ready guns; just the bow with <see cref="FireCones.AiBringsGunsToBear"/> off.
		/// </summary>
		/// <remarks>C# returns a nullable Dictionary reference, so this returns a cs::Ref (null for none).</remarks>
		static cs::Ref<cs::Dictionary<int, double>> ConeFirepower(const cs::Ref<BattleUnit>& u, DVec3 toTarget, bool slant = false);

		/// <summary>The way to point the bow, held level, so a target along <paramref name="toTarget"/> lies
		/// <paramref name="bearingDeg"/> off it (0 = dead ahead, 90 = starboard): the heading that turns the guns whose cones
		/// are centred there onto the target.</summary>
		static DVec3 BowFor(DVec3 toTarget, int bearingDeg);

	private:
		/// <summary>Rotates a direction by an angle (degrees) around the battle's vertical (ecliptic) axis.</summary>
		static DVec3 RotateYaw(DVec3 v, double angleDeg);

	public:
		/// <summary>A unit's station on its leader: slot in the leader's frame plus a slow human wander.</summary>
		DVec3 StationOf(const cs::Ref<BattleUnit>& u);

		/// <summary>Swings an AI hull's nose toward <paramref name="dir"/> the way the helm does (U.SPC-T557,
		/// <see cref="PilotFlight.Rotate(ref PilotState, in HullDynamics, double, double, in PilotInput, double)"/>): the nose spins up
		/// and brakes at the hull's angular acceleration, to a top swing of <paramref name="degPerSec"/>, so a wanted facing that flips
		/// cannot reverse the swing in one frame. A hull with no attitude thrusters of its own spins up in about a second and a half.</summary>
		static void TurnToward(const cs::Ref<BattleUnit>& u, DVec3 dir, double degPerSec, double dt);

	private:
		static double PreferredRange(const cs::Ref<BattleUnit>& u);

		DVec3 HostileCentroid(const cs::Ref<BattleUnit>& u);

		// ---------------------------------------------------------------- weapons

		void Fire(const cs::Ref<BattleUnit>& u, double dt);

		/// <summary>
		/// Launches a railgun slug or autocannon round: leads the target's current velocity across the
		/// round's own time of flight (same lead construction a homing missile uses in <see cref="StepMissiles"/>,
		/// just without the homing -- a kinetic round is dumb and flies the straight line it was aimed
		/// down). The hit/miss roll already happened in <see cref="Fire"/>; a hit flies true to the lead
		/// point, a miss gets the same random offset a beam's miss does and keeps going past it. Emits the
		/// muzzle-flash Fx immediately (so it still reads as instant feedback at the gun) but defers the
		/// actual <see cref="ApplyDamage"/> to <see cref="StepRounds"/>, once the round has actually crossed
		/// the intervening space.
		/// </summary>
		void FireKineticRound(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& mount, DVec3 origin, DVec3 aimPoint, const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& targetSub, const cs::Ref<WeaponSpec>& w, bool hit, double dist, FxKind kind, double damageScale = 1);

		/// <summary>Advances every kinetic round in flight; a round that hit calls <see cref="ApplyDamage"/>
		/// the instant it arrives (skipped if the target died or got clear first), a round that missed just
		/// keeps flying until its own fuse runs out.</summary>
		void StepRounds(double dt);

		/// <summary>A mount the player can personally aim by pointing the hull's own nose at something --
		/// a forward-centred hardpoint, as opposed to a side or aft battery a crew works autonomously.</summary>
		static bool IsPlayerAimable(const cs::Ref<BattleUnit>& u, const cs::Ref<SubsystemDef>& d);

		/// <summary>
		/// One of the player's guns fires down the crosshair (U.SPC-T270): swung off the nose as far as its
		/// gimbal lets it (<see cref="Gunnery.Gimbal"/>), converged on whatever hostile the line of sight meets
		/// (else at the gun's range) so guns spread along the hull all land where the crosshair is, and
		/// building heat. What it hits is <see cref="ResolveShot"/>'s to decide.
		/// </summary>
		void FirePlayerGun(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, DVec3 origin, const cs::Ref<WeaponSpec>& w);

		/// <summary>
		/// A gun mount the player has taken (U.SPC-T18, <see cref="MountedTurret"/>): it traverses toward their aim
		/// at its own rate, never past its fire cone (a spinal railgun never past its gimbal), and on the trigger fires
		/// down its barrel at its own rhythm, an aimed shot that hits only what it meets (<see cref="ResolveShot"/>).
		/// </summary>
		void FireManned(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, const cs::Ref<MountControl>& m, const cs::Ref<WeaponSpec>& w, double dt, double accuracyScale);

		/// <summary>
		/// Where a gunner lays his gun on the player's hull: led for the round's flight (none for a beam), but
		/// trailing a target that crosses his sights by a reaction lag, and scattered, both less the better the
		/// gun (<see cref="WeaponSpec.Accuracy"/>, cut by a lost bridge). A hull flying straight is hit; one
		/// turning, jinking or sliding across the line is missed.
		/// </summary>
		DVec3 AimAt(const cs::Ref<BattleUnit>& u, DVec3 origin, const cs::Ref<BattleUnit>& target, DVec3 aimPoint, const cs::Ref<WeaponSpec>& w, double accuracyScale);

		/// <summary>
		/// An aimed shot from <paramref name="origin"/> down <paramref name="dir"/>: a beam lands at once on the
		/// first hostile hull it meets within range, a round flies off to hit whatever it meets on the way
		/// (<see cref="StepAimedRound"/>). Either hits the hardpoint nearest where it struck.
		/// </summary>
		void ResolveShot(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, DVec3 origin, DVec3 dir, const cs::Ref<WeaponSpec>& w, double damageScale = 1);

		/// <summary>An aimed round flies on, and hits the first hostile hull its path crosses this step.</summary>
		void StepAimedRound(const cs::Ref<KineticRound>& r, double dt);

		/// <summary>How far down a line of fire the first hull hostile to <paramref name="side"/> lies, within
		/// <paramref name="maxKm"/>; -1 (and no <paramref name="hit"/>) if the line is clear. A hull that has
		/// struck its colours is never what a shot resolves onto: it is no longer shot at while it waits on
		/// its captor's terms (see <see cref="Surrender"/>), the same as target selection already honours.</summary>
		double FirstHostileAlong(int side, DVec3 origin, DVec3 dir, double maxKm, cs::Ref<BattleUnit>& hit);

		/// <summary>The hardpoint nearest where a shot struck, if one is close enough to have taken it (within a
		/// sixth of the hull's length); otherwise the shot is to the hull. A bridge buried at the centre of the hull
		/// (U.SPC-T492) is never near where a shot strikes the outside.</summary>
		static cs::Ref<Subsystem> SubsystemNear(const cs::Ref<BattleUnit>& target, DVec3 at);

	public:
		// ---------------------------------------------------------------- broadsides at the helm (U.SPC-T272)

		/// <summary>Seconds a volley takes to build to full, the extra damage a full one does (as a share of each
		/// gun's own), and how much surer its gunners' aim is than their aim at will.</summary>
		static constexpr double VolleyBuildSeconds = 1.5; static constexpr double VolleyDamageBonus = 0.5; static constexpr double VolleyAccuracyBonus = 0.25;

		/// <summary>How far off the line the player is looking a volley will take a target, deg.</summary>
		static constexpr double VolleyConeDeg = 25;

	private:
		/// <summary>
		/// A mount at a capital ship's helm that bears on the side the player is looking: held, its gun waits on the
		/// volley; let go, every such gun that is loaded fires together at the hostile nearest that line (the locked one
		/// if it is near it), harder and surer the longer the volley was built. True if the mount was the volley's.
		/// </summary>
		bool HelmVolley(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, DVec3 origin, const cs::Ref<WeaponSpec>& w, double accuracyScale);

		/// <summary>What a volley called along <paramref name="look"/> is fired at: the locked hull if it lies near that
		/// line, else the hostile nearest it within <see cref="VolleyConeDeg"/> and <paramref name="rangeKm"/>.</summary>
		cs::Ref<BattleUnit> VolleyTarget(const cs::Ref<BattleUnit>& u, DVec3 look, double rangeKm);

		/// <summary>The player's locked hull, if this mount can reach it and it lies in the mount's fire cone.</summary>
		cs::Ref<BattleUnit> LockedFor(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, DVec3 origin, const cs::Ref<WeaponSpec>& w);

	public:
		// ---------------------------------------------------------------- the dogfight arsenal (U.SPC-T271)

		/// <summary>How long a mine takes to arm once laid, and how long it lies waiting, seconds.</summary>
		static constexpr double MineArmSeconds = 1.5; static constexpr double MineLifeSeconds = 240;

		/// <summary>How near a mine a hostile hull must come to set it off, km beyond the hull's own size.</summary>
		static constexpr double MineTriggerKm = 0.12;

	private:
		/// <summary>Lays a mine astern when the mount's owner wants one: the player when asked, a crew when something
		/// is on its tail.</summary>
		void TryDeploy(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, const cs::Ref<WeaponSpec>& w);

		/// <summary>An armed hostile close astern: what a crew lays a mine for.</summary>
		bool SomethingOnItsTail(const cs::Ref<BattleUnit>& u);

		// ---------------------------------------------------------------- countermeasures (U.SPC-T424)

		/// <summary>
		/// Throws a flare-and-chaff cloud astern when the dispenser's owner wants one (the player when asked, a pilot when
		/// a guided round is closing on it), and gives every round homing on the craft within
		/// <see cref="Countermeasures.ReachKm"/> its chance of being drawn off onto it.
		/// </summary>
		void ThrowDecoy(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, const cs::Ref<WeaponSpec>& w);

	public:
		/// <summary>The nearest guided round homing on <paramref name="u"/> within <paramref name="km"/> and not already drawn off, or null.</summary>
		cs::Ref<Missile> MissileInbound(const cs::Ref<BattleUnit>& u, double km);

	private:
		/// <summary>Clouds drift and burn out.</summary>
		void StepDecoys(double dt);

		/// <summary>What the player's EMP launcher fires at: the locked hull if it is in reach, else the hostile nearest
		/// the crosshair (within 25 degrees of it) that is not already drifting dead.</summary>
		cs::Ref<BattleUnit> PlayerEmpTarget(const cs::Ref<BattleUnit>& u, DVec3 origin, const cs::Ref<WeaponSpec>& w);

	public:
		/// <summary>The stun an EMP burst leaves a hull of <paramref name="lengthKm"/>: the full
		/// <paramref name="seconds"/> for a small craft, less for a bigger hull with more to knock out.</summary>
		static double EmpStunSeconds(double seconds, double lengthKm);

		/// <summary>An EMP round bursts: every hull hostile to <paramref name="side"/> within its reach has its drives and
		/// guns knocked out (<see cref="BattleUnit.Stunned"/>) and its shields halved.</summary>
		void EmpBurst(DVec3 at, int side, double seconds, double radiusKm);

	private:
		/// <summary>Mines arm, wait and run out; an armed one with a hostile hull inside its trigger blows, hurting
		/// everything hostile in its blast, harder the nearer the centre.</summary>
		void StepMines(double dt);

		cs::List<cs::Ref<Subsystem>> burnedOut;

		/// <summary>A big ship's heat this step (U.SPC-T491, <see cref="Radiators.Step"/>), with the guns reloading as fast as
		/// <see cref="Fire"/> lets them, and every system the heat burns out shown going.</summary>
		void StepHeat(const cs::Ref<BattleUnit>& u, double dt);

	public:
		/// <summary>How hard <paramref name="u"/> runs <paramref name="s"/> on its reactor's split: a small craft's own
		/// (<see cref="PowerDistribution.FactorOf"/>, its pilot's or the player's), a hull's or station's the player set
		/// (US-T94, <see cref="ShipCombat.Reactor"/>) while the player holds it, else 1.</summary>
		double PowerFactor(const cs::Ref<BattleUnit>& u, PowerSystem s) const;

		/// <summary>How much field <paramref name="u"/>'s emitters hold on the player's split (US-T94,
		/// <see cref="PowerDistribution.ShieldCapacity"/>): 1 for anything the player does not hold.</summary>
		double ShieldCapacityOf(const cs::Ref<BattleUnit>& u) const;

		/// <summary>The player holds <paramref name="u"/> (US-T94): at its helm, at one of its guns, or at its station's controls.</summary>
		bool PlayerAboard(const cs::Ref<BattleUnit>& u) const;

	private:

		/// <summary>
		/// The player's guns cool and the missile lock builds or slips (U.SPC-T270): the lock grows while the
		/// crosshair sits on the locked target and, once made, holds while the target stays ahead of the nose.
		/// </summary>
		void StepPlayerWeapons(const cs::Ref<BattleUnit>& u, double dt);

		/// <summary>A player's locked-on missile target, valid only while it is still hostile, present
		/// and in range -- cleared (by the Runtime pilot loop) the moment it stops being any of those.</summary>
		cs::Ref<BattleUnit> PlayerLockedTarget(const cs::Ref<BattleUnit>& u, const cs::Ref<WeaponSpec>& w, DVec3 origin);

		cs::Ref<BattleUnit> ChooseWeaponTarget(const cs::Ref<BattleUnit>& u, DVec3 origin, const cs::Ref<WeaponSpec>& w);

		// ---------------------------------------------------------------- the screen (U.SPC-T489)

		/// <summary>
		/// The hostile round a screen mount on <paramref name="u"/> shoots at: the worst threat (<see cref="ScreenWeapons.Threat"/>)
		/// within its reach, not merely the nearest. An interceptor launcher skips a round one of its side's interceptors is
		/// already hunting, so a salvo spreads over a swarm instead of piling onto its leader.
		/// </summary>
		cs::Ref<Missile> ScreenTarget(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& mount, DVec3 origin, const cs::Ref<WeaponSpec>& w);

		/// <summary>Where the danger to <paramref name="ward"/> comes from, for its screen <paramref name="u"/> to stand in the way
		/// of: the centre of the hostile rounds homing on it, weighted by their warheads, or the enemy's centre when none are.</summary>
		DVec3 ThreatTo(const cs::Ref<BattleUnit>& ward, const cs::Ref<BattleUnit>& u);

		/// <summary>Whether an interceptor of <paramref name="side"/> is already after <paramref name="m"/>.</summary>
		bool Hunted(const cs::Ref<Missile>& m, int side);

		/// <summary>
		/// A screen mount fires on a hostile round (<paramref name="round"/>) or, with none in reach, a fighter
		/// (<paramref name="craft"/>): a flak cannon lays a shell that bursts on it (scattered by a miss, but a burst among a
		/// swarm still finds something), an interceptor launcher sends a round after it.
		/// </summary>
		void FireScreen(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, DVec3 origin, const cs::Ref<WeaponSpec>& w, const cs::Ref<Missile>& round, const cs::Ref<BattleUnit>& craft, double accuracyScale);

	public:
		/// <summary>A flak burst at <paramref name="at"/>: every hostile round (an interceptor too) and every hostile small craft
		/// within <paramref name="radiusKm"/> takes its <paramref name="damage"/>, less toward the burst's edge.</summary>
		void FlakBurstAt(DVec3 at, int side, double damage, double radiusKm);

	private:
		/// <summary>
		/// An interceptor hunting a round: it leads its prey hard (it is far quicker and more agile than what it hunts), and
		/// its proximity fuze goes off when it passes near enough, killing the round at <see cref="ScreenWeapons.InterceptorReliability"/>.
		/// A prey gone already (shot down, gone off, drawn away) and it looks for another within <see cref="ScreenWeapons.ReacquireKm"/>
		/// nobody else is hunting, or gives up.
		/// </summary>
		void StepInterceptor(const cs::Ref<Missile>& m, double dt);

		cs::Ref<Missile> NearestMissile(int side, DVec3 origin, double range);

	public:
		/// <summary>Gunners go for weapons first, then shields, engines and the bridge; the reactor is a lucky shot; and any of
		/// them the more in a patch of hull whose shield field is weakened (U.SPC-T501, <see cref="ShieldEmitters.FocusWeight"/>). Raiders
		/// working a merchant or civilian hull (<paramref name="shooter"/>, see <see cref="RaidDoctrine"/>, U.SPC-T374) go for
		/// its shields and engines instead, to leave it dead in space, and never its reactor.</summary>
		cs::Ref<Subsystem> PickSubsystem(const cs::Ref<BattleUnit>& target, const cs::Ref<BattleUnit>& shooter = nullptr);

		/// <summary>
		/// Shields soak damage first; armour blunts each hit (small guns most); what gets through is
		/// split between the struck hardpoint and the hull. A lost reactor takes the ship with it. What gets through the
		/// shields is multiplied by <paramref name="exposed"/>: a small craft's hit on an open weak point (U.SPC-T424, see
		/// <see cref="WeakPoints.Factor"/>). A <paramref name="strikeTorpedo"/> (a bomber's) that arrives where the shield
		/// is already down hits a capital's bare hull harder (U.SPC-T484, see <see cref="StrikeTorpedoes"/>).
		/// </summary>
		void ApplyDamage(const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& sub, double damage, DVec3 at, cs::Nullable<DVec3> shieldAt = std::nullopt, double exposed = 1, cs::Nullable<DVec3> incoming = std::nullopt,
			bool strikeTorpedo = false);

		/// <summary>
		/// A collision's damage to a unit (U.SPC-T60): <paramref name="hp"/> (see <see cref="Collisions.DamageHp"/>)
		/// done at battle-local point <paramref name="at"/> to the hardpoints on that side, through its shields there
		/// (<see cref="Collisions.ApplyDamage"/>), with the same effects and the same death as a shot.
		/// </summary>
		void ApplyImpact(const cs::Ref<BattleUnit>& target, DVec3 at, double hp);

	private:
		void StepMissiles(double dt);

		// ---------------------------------------------------------------- the full arsenal (US-T101)

		/// <summary>A salvo of dumb-fire rockets away from mount <paramref name="s"/> down <paramref name="dir"/> (unit), each
		/// scattered a little off it. The caller has set the mount's reload.</summary>
		void FireRockets(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, DVec3 origin, DVec3 dir, const cs::Ref<WeaponSpec>& w);

		/// <summary>A rocket's step: straight on, swept against every hostile hull, burst on the first it meets.</summary>
		void StepRocket(const cs::Ref<Missile>& m, double dt);

		/// <summary>A hive round splits: it is gone and its micro-missiles, fanned out round its heading, go on after its
		/// target (into <paramref name="swarm"/>, added once the step is done).</summary>
		void SplitHive(const cs::Ref<Missile>& m, cs::List<cs::Ref<Missile>>& swarm);

		/// <summary>Whether the second trigger fires mount <paramref name="s"/>: on a craft the player flies from a carrier
		/// only the weapon group picked for it (<see cref="FighterControl.SecondaryGroup"/>), at any other helm every guided
		/// mount and rocket pod at once.</summary>
		bool SecondaryFires(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s);

		// ---------------------------------------------------------------- boarding

		/// <summary>
		/// Pods go at hulls whose shields are down (they cannot get through a shield); each carries a
		/// squad. A ship keeps a reserve of marines aboard to defend itself.
		/// </summary>
		void FirePods(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s);

		void PodHit(const cs::Ref<Missile>& m, const cs::Ref<BattleUnit>& t, DVec3 at, cs::Nullable<DVec3> shieldAt = std::nullopt);

		void Embed(const cs::Ref<BattleUnit>& t, DVec3 at, DVec3 travel, int side, bool breacher);

		static void RemoveMarines(const cs::Ref<Interior>& interior, double count);

		/// <summary>
		/// A ship's Breacher bays (U.SPC-T466, U.SPC-T521): once the bays have cycled and the marine habitats have a whole
		/// team ready, the captain, of whatever side (a navy, raiders, a bounty hunter or a merchant's escort), fires a
		/// Breacher round at the weakened hostile hull in range that the team could take as a prize and the round could get
		/// into (its shields down to a quarter, or none). The team is drawn from the habitats' barracks, and the bays wait
		/// for the next one to be readied (<see cref="BreacherBays.IntervalSeconds"/>).
		/// </summary>
		void FireBreacherBay(const cs::Ref<BattleUnit>& u, double dt);

		/// <summary>The hull a ship's Breacher bay would fire a round at: the nearest hostile in range that its team may take
		/// as a prize (<see cref="MayBreach"/>) and the round would get into, the most weakened favoured (a disabled one, then
		/// the one with the least of its hull left, U.SPC-T521); or null.</summary>
		cs::Ref<BattleUnit> BreacherTarget(const cs::Ref<BattleUnit>& u);

		/// <summary>Whether <paramref name="u"/>'s Breacher bay may fire its team into <paramref name="target"/> to take it as a
		/// prize (U.SPC-T521): whatever its side would board (<see cref="MayBoard"/>), and, for a lawful hull of any side (a
		/// merchant's escort), a raider attacking it, which is no piracy to take.</summary>
		bool MayBreach(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target);

		/// <summary>A Breacher round away from <paramref name="u"/>'s bays at <paramref name="target"/>, its team drawn from the
		/// habitats and the bays set cycling (the firing half of <see cref="FireBreacherBay"/>).</summary>
		void FireBreacherAt(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target);

		/// <summary>A salvo of boarding pods away from mount <paramref name="s"/> at <paramref name="target"/> (the firing half
		/// of <see cref="FirePods"/>).</summary>
		void FirePodsAt(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s, const cs::Ref<BattleUnit>& target);

	public:
		// ---------------------------------------------------------------- orders from the player's helm (US-T106)

		/// <summary>The helm orders a Breacher round at <paramref name="target"/> (the hull locked): fired now if the bays have
		/// cycled, a team is ready and the round would get in. Empty when it went, else why not.</summary>
		cs::String OrderBreacher(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target);

		/// <summary>The helm orders the boarding pods at <paramref name="target"/>: the first loaded launcher fires its salvo if
		/// the target's shields are down and it is in reach. Empty when they went, else why not.</summary>
		cs::String OrderPods(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target);

		/// <summary>The helm sends a boarding shuttle at <paramref name="target"/> with a team (the carrier keeping its reserve);
		/// <paramref name="sent"/> is the shuttle. Empty when it went, else why not.</summary>
		cs::String OrderShuttle(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target, cs::Ref<BattleUnit>& sent);

		/// <summary>A carrier's helm orders (<see cref="BattleUnit.WingHeld"/>) lapse once nobody holds its helm or flies one of
		/// its craft: the recall and the call are lifted and its captain launches the wing held aboard, as at the start of a fight.</summary>
		void HelmOrdersLapse(const cs::Ref<BattleUnit>& u);

	private:

		/// <summary>
		/// A Breacher round in flight (U.SPC-T466): it coasts like a bullet, its thrusters bending its track only slightly
		/// toward where its target will be, and strikes the first hostile hull it flies through. One that strikes nothing
		/// is not lost with its team (U.SPC-T519): once it has flown its course it goes adrift, tumbling on with the
		/// marines alive inside, and coasts until point defence shoots it down or the fight ends and it is left drifting.
		/// </summary>
		void StepBreacher(const cs::Ref<Missile>& m, double dt);

		/// <summary>
		/// A Breacher round goes adrift (U.SPC-T519): its thrusters give up the chase and it tumbles on along its line, a
		/// derelict with its team alive inside. It no longer homes on anything or drills into anything.
		/// </summary>
		static void GoAdrift(const cs::Ref<Missile>& m);

		/// <summary>
		/// A Breacher round is shot out of space (U.SPC-T519): the team inside dies with it.
		/// </summary>
		static void RoundLost(const cs::Ref<Missile>& m);

		/// <summary>
		/// A Breacher round strikes <paramref name="t"/> from direction <paramref name="from"/> (U.SPC-T466); true if it is
		/// spent (drilled in, or the hull it struck died). A shield holding more than a quarter of its strength turns it aside:
		/// it glances off and tumbles away adrift, its team still alive inside (U.SPC-T519, <see cref="GoAdrift"/>). Otherwise its drill
		/// bores in, it stays stuck in the plating, and its team storms out into the compartment it struck, where the
		/// boarding fight takes over and the hull's own marines try to stop them. If the hull is no longer theirs to take
		/// (struck its colours on terms that do not open it, already held, or boarded by another faction), the team stays
		/// in the round and is taken off by the ship that fired it.
		/// </summary>
		bool BreacherHit(const cs::Ref<Missile>& m, const cs::Ref<BattleUnit>& t, DVec3 from);

		/// <summary>
		/// A ship with marines to spare picks a disabled enemy to board, once no healthy enemy warship
		/// is close enough to make it suicide.
		/// </summary>
		void ChooseBoarding(const cs::Ref<BattleUnit>& u);

		bool ThreatNear(const cs::Ref<BattleUnit>& u, DVec3 at, double km);

		/// <summary>
		/// Closing to board. A boarding ship lines up alongside, matches the target's drift, latches
		/// and after a while opens its airlock. (A Breacher round is fired, not flown: <see cref="FireBreacherBay"/>.)
		/// </summary>
		void Approach(const cs::Ref<BattleUnit>& u, double dt);

		/// <summary>Runs every fight aboard; releases boarding ships when it is decided.</summary>
		void StepBoarding(double dt);

		/// <summary>
		/// A player's boarding orders (U.SPC-T468) stand only while a player is there to command that side: the hull's
		/// own helm for its defenders, a hull of the boarding faction for the boarders. Once the player has gone (handed
		/// the helm back, left the fight) that side is the resolver's again: the auto-resolve.
		/// </summary>
		void KeepOrdersCommanded(const cs::Ref<BattleUnit>& u, const cs::Ref<Interior>& interior);

		void Capture(const cs::Ref<BattleUnit>& u, const cs::Ref<Faction>& captor);

		// ---------------------------------------------------------------- surrender

		/// <summary>A hull has struck its colours and nobody has settled its terms yet.</summary>
		bool AwaitingSurrenderTerms() const;

		void TallySideStrength();

		double OutgunnedOf(const cs::Ref<BattleUnit>& u);

		static double DamageTollOf(const cs::Ref<BattleUnit>& u);

		bool StrikesColours(const cs::Ref<BattleUnit>& u);

	public:
		/// <summary>
		/// Strikes <paramref name="u"/>'s colours: it breaks off, powers down its weapons and hails,
		/// and is no longer shot at while it waits on its captor's terms.
		/// </summary>
		void Surrender(const cs::Ref<BattleUnit>& u);

		/// <summary>The faction a hull on <paramref name="side"/> surrenders to: the other side's leader.</summary>
		cs::Ref<Faction> CaptorOf(int side) const;

	private:
		/// <summary>True when someone on the side facing <paramref name="u"/> could board it.</summary>
		bool CaptorCanBoard(const cs::Ref<BattleUnit>& u);

		bool CaptorPlayerPresent(const cs::Ref<BattleUnit>& u);

	public:
		/// <summary>The first hull on the side facing <paramref name="captorSide"/> still waiting for terms, or null.</summary>
		cs::Ref<BattleUnit> AwaitingTermsFrom(int captorSide);

		/// <summary>
		/// The captor's answer to a hull that has struck its colours: hand over the cargo and go, be
		/// boarded and taken, or simply go. Taking it needs someone able to board; with nobody, it is
		/// refused. False when <paramref name="u"/> isn't waiting on terms.
		/// </summary>
		bool OfferSurrenderTerms(const cs::Ref<BattleUnit>& u, SurrenderTerms terms);

	private:
		void Settle(const cs::Ref<BattleUnit>& u, const cs::Ref<Faction>& captor, SurrenderTerms terms);

		void StepSurrenders(double dt);

		/// <summary>
		/// Pirates with the upper hand hail a hull that carries cargo: jettison it and go, or fight. Asked once a battle.
		/// A player-flown hull answers through <see cref="AnswerCargoDemand"/>; an AI merchant or civilian hull is asked
		/// once the raiders have done what their doctrine says to first (U.SPC-T374, <see cref="RaidDoctrine"/>): left it
		/// dead in space or knocked out by a pulse, or stripped its shields while they have it outgunned. Its crew are paid
		/// to deliver, not to die, so it complies, and the raiders' drones collect what it jettisons.
		/// </summary>
		void MaybeDemandCargo(const cs::Ref<BattleUnit>& pirate);

		/// <summary>Whether raiders have done to <paramref name="v"/> what their doctrine says to before they demand its
		/// cargo: left it dead in space or stunned by a pulse, or stripped its shields while they have it outgunned.</summary>
		bool Stripped(const cs::Ref<BattleUnit>& v);

	public:
		/// <summary>
		/// The answer to a pirate cargo demand: comply and the cargo is jettisoned for the raiders' drones to collect and the
		/// hull goes free (it leaves the field); refuse and the fight goes on, and the raiders no longer spare it. False when
		/// nothing was asked.
		/// </summary>
		bool AnswerCargoDemand(const cs::Ref<BattleUnit>& victim, bool comply);

		/// <summary>Whether each side has robbed a hull of its cargo in this fight (U.SPC-T374): raiders who have, with nothing
		/// left worth taking, withdraw (see <see cref="RaidDone"/>).</summary>
		/// <remarks>A bool[2]; cs::List&lt;bool&gt; would be std::vector&lt;bool&gt;, whose operator[] can't hand out a bool&amp;.</remarks>
		std::array<bool, 2> Robbed{};

	private:
		/// <summary>Whether <paramref name="u"/>, an AI raider, means to rob <paramref name="o"/> rather than destroy it: a
		/// merchant or civilian hull that hasn't turned its demand down (see <see cref="RaidDoctrine"/>).</summary>
		bool Spares(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& o);

		/// <summary>A raider holds its fire on a hull it is robbing once that lies dead in space.</summary>
		bool HoldsFireOn(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& o);

		/// <summary>
		/// Whether raider <paramref name="u"/>'s work here is done (U.SPC-T374): no merchant or civilian hull is left that it
		/// could still rob (one with cargo it hasn't handed over, one that turned the demand down, one waiting on terms, one
		/// its side is boarding or has marines to board), and either its side has robbed someone or there is nobody armed left to fight. Then it
		/// withdraws, rather than trade fire with escorts for nothing.
		/// </summary>
		bool RaidDone(const cs::Ref<BattleUnit>& u);

		static double SegmentPointDistance(DVec3 a, DVec3 b, DVec3 p);

	public:
		void Emit(FxKind kind, DVec3 a, DVec3 b, double size, int side, const cs::Ref<BattleUnit>& unit, int sub);

		// ---------------------------------------------------------------- placement

		/// <summary>
		/// How far out from <paramref name="body"/> a battle is fought: high enough that its tens of km,
		/// drawn at ship scale in the gameplay view, stay well clear of the inflated world.
		/// </summary>
		static double AnchorDistanceKm(const cs::Ref<BodyDefinition>& body);

		// ================================================================ the partial in Combat/BoardingShuttles.cs
		// (defined in Combat/BoardingShuttles.cpp)

	private:
		/// <summary>
		/// A carrier sends a boarding shuttle (U.SPC-T486): with one docked in its shuttle berths fit to fly (a crippled one
		/// stays aboard, U.SPC-T590) and a working hangar, marines enough for a team and its own reserve, and a prize in reach
		/// (<see cref="ShuttleTarget"/>), one shuttle every <see cref="BoardingShuttles.LaunchSeconds"/>, each to a prize no
		/// other shuttle is already flying to.
		/// </summary>
		void SendShuttles(const cs::Ref<BattleUnit>& u, double dt);

		static bool HangarWorks(const cs::Ref<BattleUnit>& u);

		/// <summary>Whether <paramref name="carrier"/> boards by shuttle: it has one in its hangars or out, so it sends them
		/// rather than closing to board with its own hull.</summary>
		bool BoardsByShuttle(const cs::Ref<BattleUnit>& carrier);

	public:
		bool ShuttleMayBoard(const cs::Ref<BattleUnit>& seeker, const cs::Ref<BattleUnit>& o);

		cs::Ref<BattleUnit> ShuttleTarget(const cs::Ref<BattleUnit>& seeker);

	private:
		/// <summary>Whether another shuttle of <paramref name="seeker"/>'s side is already flying to <paramref name="o"/>.</summary>
		bool ShuttleBound(const cs::Ref<BattleUnit>& seeker, const cs::Ref<BattleUnit>& o);

	public:
		/// <summary>
		/// A shuttle docked alongside <paramref name="t"/> latches on and opens its airlock (U.SPC-T486): its team goes in
		/// at the compartment it docked against, fighting as well as it drilled in its carrier's habitats, with no breach
		/// and no damage to the hull. If the hull is no longer theirs to take (boarded by another faction, or already
		/// held), the team stays aboard and the shuttle casts off.
		/// </summary>
		void ShuttleLatch(const cs::Ref<BattleUnit>& shuttle, const cs::Ref<BattleUnit>& t);
	private:
		Battle() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(Battle); // ObjectGraph registration (Persistence/Registrations)
	};
}
