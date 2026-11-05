#pragma once
#include "Cs/Cs.h"
#include "DVec3.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class ShipCombat;
	class CombatSpec;
	struct PilotTuning;
	enum class EngineTelegraph : int;

	/// <summary>
	/// One frame of player flight input, already translated into pure flight terms by the Runtime
	/// input layer, so the integration math below stays engine-free and unit-testable.
	/// </summary>
	struct SPACEGODCORE_API PilotInput
	{
		/// <summary>The direction the player wants the nose turned toward (unit vector, ecliptic axes).
		/// Zero means no stick input: flight assist then stops the nose swinging, while with assist off
		/// the hull keeps whatever spin it has.</summary>
		DVec3 DesiredForward;

		/// <summary>Forward/reverse thrust, -1..1.</summary>
		double Throttle = 0;

		/// <summary>Lateral and vertical strafe thrust in the hull's own frame, each -1..1.</summary>
		double StrafeRight = 0, StrafeUp = 0;

		/// <summary>Roll, -1..1: positive rolls clockwise as seen from behind (right wing down).</summary>
		double Roll = 0;

		bool Boost = false;

		/// <summary>Holding the cruise key: see <see cref="PilotCruiseState"/>.</summary>
		bool Cruise = false;

		/// <summary>True whenever the hull is in an active battle (set by the Runtime layer from
		/// <c>ship.Battle</c>/<c>BattleUnit.PlayerControlled</c>) -- cruise is a pure open-space travel
		/// tier and must drop the instant combat is live, win or lose.</summary>
		bool InCombat = false;

		/// <summary>Flight assist switched off (it is on by default): throttle and strafe become raw
		/// thrust, nothing counter-thrusts, and the hull drifts on its momentum -- for slides past a
		/// target and turn-and-burns. See <see cref="PilotFlight.Step"/>.</summary>
		bool FlightAssistOff = false;

		/// <summary>Rings the engine telegraph this frame (U.SPC-T499, <see cref="CapitalFlightModel"/>): +1 one step
		/// ahead, -1 one step astern, 0 leaves it where it stands. Only the capital model reads it.</summary>
		int TelegraphStep = 0;

		/// <summary>The hull's all-up mass over its ready mass (U.SPC-T443, <see cref="HaulerFlightModel.LadenRatio"/>): above 1
		/// with a load aboard. 0 (unset) reads as 1. Only the hauler model reads it.</summary>
		double MassRatio = 0;

		/// <summary>Ctrl held (U.SPC-T443): fine control. Inside a port's approach (<see cref="DockingApproach"/>) it puts a
		/// hauler in docking mode (<see cref="HaulerFlightModel"/>).</summary>
		bool Fine = false;

		/// <summary>The hull is inside a port's approach (set by the Runtime layer), where <see cref="Fine"/> is docking mode.</summary>
		bool DockingApproach = false;

		/// <summary>A player's hands are on this stick (US-T87): set only by the player's helm (<see cref="ShipHelm"/>) and
		/// the player's fighter seat (<see cref="FighterAI.FlyManned"/>). The models' player-facing handling (the light
		/// fighter's snap and lateral dash, US-T79; the heavy fighter's weight and surge, US-T80; the capital's broadside
		/// translation, US-T81) reads it. Crews never set it, so AI flight, and with it the Bench, flies exactly as before.</summary>
		bool Piloted = false;
	};

	/// <summary>
	/// What stopping costs a hull right now (US-T87), for the HUD with flight assist off: a turn-and-burn (swing the nose
	/// onto the retrograde, then fire the main drive) against braking on the retro thrusters without turning, which is
	/// slower but needs no flip. Seconds and km, so it reads the same at true scale and game scale.
	/// </summary>
	struct SPACEGODCORE_API BrakePlan
	{
		/// <summary>Seconds to swing the nose onto the retrograde, then to burn the speed off on the main drive, and the
		/// distance covered meanwhile (km).</summary>
		double FlipS = 0, BurnS = 0, BurnKm = 0;

		/// <summary>Seconds and km to stop on the retro thrusters alone, nose kept where it is; infinite with none fitted.</summary>
		double RetroS = 0, RetroKm = 0;

		/// <summary>The hull is moving at all.</summary>
		bool Moving = false;

		bool HasRetros() const { return RetroS < cs::Double::PositiveInfinity; }

		/// <summary>One HUD line: "BRAKE flip+burn 6 s / 12 km  retros 9 s / 18 km".</summary>
		cs::String Text() const;
	};

	/// <summary>
	/// Ramp state for the cruise drive (see <see cref="PilotFlight.Step"/>): a second, much stronger
	/// drive tier for open-space travel, distinct from tactical boost. Persists across frames inside
	/// <see cref="PilotState"/>.
	/// </summary>
	struct SPACEGODCORE_API PilotCruiseState
	{
		/// <summary>0 (tactical flight) .. 1 (full cruise thrust) -- eases up over
		/// <see cref="PilotTuning.CruiseRampS"/> while engaged, but drops to 0 instantly the moment any
		/// disqualifying condition fires (damage, combat, throttling down, or turning hard), rather
		/// than easing back down -- cruise is meant to feel like it snaps off, not coast down.</summary>
		double RampT = 0;

	private:
		double lastHull = 0;
		bool hasLastHull = false;

	public:
		/// <summary>Whether cruise is contributing any extra thrust at all right now.</summary>
		bool Active() const { return RampT > 0; }

		/// <summary>Tracks the hull's own health between frames purely to notice a hit; has nothing to
		/// do with repairs or destruction, which <see cref="ShipCombat"/> already owns.</summary>
		bool NoteHullAndCheckDamage(double hull)
		{
			bool tookDamage = hasLastHull && hull < lastHull - 1e-9;
			lastHull = hull;
			hasLastHull = true;
			return tookDamage;
		}
		SG_SERIALIZABLE(PilotCruiseState); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>
	/// A hull's design turned into what flight needs (U.SPC-T59): how hard each set of thrusters can
	/// push it (km/s²) and how hard its attitude thrusters can spin it (rad/s²), from the mass, thrust,
	/// torque and moments of inertia on its <see cref="CombatSpec"/>. A design with no mass (a defence
	/// platform) cannot move at all.
	/// </summary>
	struct SPACEGODCORE_API HullDynamics
	{
		double MassKt = 0;

		/// <summary>Linear acceleration each thruster set gives, km/s²: the main drive along the nose,
		/// the retro thrusters against it, and the manoeuvre thrusters sideways and up/down.</summary>
		double MainAccel = 0, RetroAccel = 0, LateralAccel = 0, VerticalAccel = 0;

		/// <summary>Angular acceleration the attitude thrusters give, rad/s²: torque over moment of inertia.</summary>
		double PitchYawAngularAccel = 0, RollAngularAccel = 0;

		HullDynamics() = default;

		HullDynamics(double massKt, double mainAccel, double retroAccel, double lateralAccel, double verticalAccel, double pitchYawAngularAccel, double rollAngularAccel)
			: MassKt(massKt), MainAccel(mainAccel), RetroAccel(retroAccel), LateralAccel(lateralAccel), VerticalAccel(verticalAccel),
			  PitchYawAngularAccel(pitchYawAngularAccel), RollAngularAccel(rollAngularAccel)
		{
		}

	private:
		/// <summary>MN over kt gives m/s²; the flight model runs in km and seconds.</summary>
		static constexpr double MetresPerKm = 1000;

	public:
		static HullDynamics Of(const cs::Ref<CombatSpec>& spec);
	};

	/// <summary>
	/// Everything about a piloted hull's motion that carries from one frame to the next: where it is
	/// and how fast it is going (km, km/s, in its body's frame, ecliptic axes), its full attitude
	/// (nose and roof, so roll is a real axis) and how fast it is spinning, and the cruise drive's ramp.
	/// </summary>
	struct SPACEGODCORE_API PilotState
	{
		DVec3 Pos, Vel;

		/// <summary>Unit nose and roof directions, kept perpendicular.</summary>
		DVec3 Forward, Up;

		/// <summary>Spin, rad/s, about a world-space axis (right-hand rule).</summary>
		DVec3 AngularVelocity;

		PilotCruiseState Cruise;

		/// <summary>Where a capital ship's engine telegraph stands (U.SPC-T499, <see cref="CapitalFlightModel"/>); null until
		/// the capital model first flies the hull, which then rings the step nearest the way it has on.</summary>
		cs::Nullable<EngineTelegraph> Telegraph;

		/// <summary>The hull's starboard side: with ecliptic +Z up and the nose on +X, this is -Y.</summary>
		DVec3 Right() const;

		/// <summary>How fast the nose is swinging (pitch and yaw together), deg/s.</summary>
		double TurnRateDegS() const;

		/// <summary>How fast the hull is rolling about its nose, deg/s (positive = clockwise from behind).</summary>
		double RollRateDegS() const;

		/// <summary>A hull at rest in attitude, nose on <paramref name="forward"/> and roof as near the
		/// ecliptic's north as the nose allows (the same level the renderer drew hulls at before roll).</summary>
		static PilotState At(DVec3 pos, DVec3 vel, DVec3 forward);

		/// <summary>The roof direction for a level hull flying along <paramref name="forward"/>.</summary>
		static DVec3 LevelUp(DVec3 forward);
	};

	/// <summary>
	/// Flies a player-piloted hull with Newtonian physics (U.SPC-T59): its thrusters and attitude
	/// thrusters apply forces and torques to a body with mass and moments of inertia, and the hull
	/// carries its momentum, linear and angular, from frame to frame.
	///
	/// Flight assist (on by default) is the flight computer: it reads the throttle and strafe as a
	/// commanded velocity (the hull's set point, up to <see cref="PilotTuning.MaxSpeed"/>) and fires
	/// whichever thrusters bring the real velocity to it -- cancelling drift and gravity as it goes,
	/// within what the thrusters can push -- and holds the roll rate to the roll input. Off, the
	/// throttle and strafe are raw thrust, nothing cancels anything, and the hull slides on whatever
	/// heading it was going, so it can turn its nose and fire across its own track or flip and burn
	/// to brake. Either way the nose follows the stick, as fast as the hull's torque allows.
	/// </summary>
	class SPACEGODCORE_API PilotFlight
	{
	public:
		/// <summary>Flight assist's strafe set point, as a fraction of its forward one.</summary>
		static constexpr double StrafeSpeedFraction = 0.35;

	private:
		/// <summary>Boost drains twice as fast as it recharges.</summary>
		static constexpr double RechargeFraction = 0.5;

		/// <summary>Heading drift from the desired forward still read as "flying roughly straight";
		/// anything sharper is a deliberate turn and drops cruise.</summary>
		static constexpr double CruiseMaxHeadingDriftDeg = 5;

		/// <summary>Throttle below this reads as throttling down out of cruise, not holding it floored.</summary>
		static constexpr double CruiseMinThrottle = 0.9;

	public:
		/// <summary>A body's gravity reaches a piloted hull only this close (in body radii): near enough
		/// to matter against a hull's thrust (a quarter of surface gravity at the edge of it, three
		/// radii being where ships park), out of the way of ordinary flying elsewhere.</summary>
		static constexpr double GravityReachRadii = 4;

	private:
		/// <summary>The outer part of that reach (as a fraction of it) over which gravity eases in, so
		/// crossing the edge is not a jolt.</summary>
		static constexpr double GravityFadeFraction = 0.25;

	public:
		/// <summary>
		/// Advances the hull by <paramref name="dt"/> seconds: turns it with its attitude thrusters,
		/// works out this frame's thrust (flight assist's counter-thrust toward the commanded velocity,
		/// or the raw stick with assist off), adds <paramref name="gravityKmS2"/> (see
		/// <see cref="Gravity"/>), and integrates velocity then position (semi-implicit Euler, which
		/// with no force carries momentum exactly).
		/// </summary>
		static void Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt);

		// ------------------------------------------------------------ the stages of a step
		// Step above is the Newtonian model (NewtonianFlightModel); other flight models (see
		// IFlightModel) compose these same stages with their own rules in between.

		/// <summary>
		/// Drains or recharges the boost charge and advances the cruise drive's ramp, returning this
		/// frame's main-drive multiplier: boost (an afterburner) and cruise (the drive spooled up) are
		/// more main-drive thrust, and flight assist's set point rises with the tier.
		/// <paramref name="forwardBeforeTurn"/> is the nose before this frame's turn (cruise drops on a hard turn).
		/// </summary>
		static double DriveMultiplier(PilotState& s, const cs::Ref<ShipCombat>& combat, const PilotTuning& tuning, const PilotInput& input, DVec3 forwardBeforeTurn, double dt);

		/// <summary>Flight assist off: the throttle and strafe are raw thrust on each set of thrusters, km/s².</summary>
		static DVec3 RawThrust(const PilotState& s, const HullDynamics& hull, const PilotInput& input, double driveMult);

		/// <summary>
		/// Flight assist on: the throttle and strafe are a commanded velocity (forward up to the set
		/// point, strafe up to <paramref name="strafeSpeedFraction"/> of it), and this is exactly the push
		/// that lands on it this frame with gravity cancelled, cut to what the thrusters can give: the
		/// main drive and retro thrusters along the nose, and at most <paramref name="lateralCap"/> and
		/// <paramref name="verticalCap"/> km/s² across it (the Newtonian model passes the manoeuvre
		/// thrusters' own; a model with more grip passes more).
		/// </summary>
		static DVec3 AssistThrust(const PilotState& s, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double driveMult, double dt, double strafeSpeedFraction, double lateralCap, double verticalCap);

		/// <summary>Applies this frame's thrust and gravity: velocity then position (semi-implicit Euler,
		/// which with no force carries momentum exactly).</summary>
		static void Integrate(PilotState& s, DVec3 thrustKmS2, DVec3 gravityKmS2, double dt);

		/// <summary>
		/// One body's pull on a hull <paramref name="fromBodyKm"/> from its centre, km/s²: inverse-square
		/// within <see cref="GravityReachRadii"/> of it (easing in over the outer part of that reach),
		/// nothing beyond. Inside the body it holds at its surface value. The caller sums this over the
		/// bodies near the hull.
		/// </summary>
		static DVec3 Gravity(DVec3 fromBodyKm, double gmKm3S2, double radiusKm);

		/// <summary>
		/// Holds the hull at least <paramref name="floorKm"/> from a body's centre
		/// (<paramref name="bodyKm"/>, in the hull's own frame): a hull that sinks onto the surface comes
		/// to rest on it, its downward speed lost and its sideways speed kept. True if it touched.
		/// </summary>
		static bool KeepAboveSurface(PilotState& s, DVec3 bodyKm, double floorKm);

		/// <summary>As <see cref="KeepAboveSurface(ref PilotState, DVec3, double)"/>, also giving the downward
		/// speed the hull lost on the surface, km/s: how hard it came down (see
		/// <see cref="Collisions.SurfaceImpactShareTJ"/>, U.SPC-T60).</summary>
		static bool KeepAboveSurface(PilotState& s, DVec3 bodyKm, double floorKm, double& sinkKmS);

	private:
		/// <summary>
		/// Advances the cruise drive's ramp and returns this frame's main-drive multiplier (1 when not
		/// cruising). Engaging requires holding <see cref="PilotInput.Cruise"/> at near-full throttle
		/// while flying close to straight, with no boost and no active battle; once engaged it eases
		/// from 1x to <see cref="PilotTuning.CruiseSpeedMultiplier"/> over
		/// <see cref="PilotTuning.CruiseRampS"/> seconds. Any disqualifying condition -- including this
		/// frame's hit, checked here rather than left to the caller since <see cref="ShipCombat.Hull"/>
		/// is already in hand -- snaps the ramp back to zero immediately rather than easing it down.
		/// </summary>
		static double StepCruise(PilotCruiseState& cruise, const PilotInput& input, DVec3 forwardBeforeTurn, const cs::Ref<ShipCombat>& combat, const PilotTuning& tuning, double dt);

	public:
		/// <summary>
		/// The attitude thrusters, then the spin they leave applied to the hull. Pitch and yaw follow
		/// the stick: the computer swings the nose toward <see cref="PilotInput.DesiredForward"/> no faster
		/// than <see cref="PilotTuning.TurnRateDegS"/>, spinning up and braking at the hull's own angular
		/// acceleration so the nose settles on the heading instead of overshooting -- heavy hulls are
		/// slow to start swinging and slow to stop. Roll is held to the roll input's rate with flight
		/// assist on (so letting go stops the roll); off, roll input is raw torque and a roll, once
		/// started, keeps going until countered. With assist off and no stick input at all, nothing
		/// touches the spin: angular momentum is conserved.
		/// </summary>
		static void Rotate(PilotState& s, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, double dt);

		/// <summary>As <see cref="Rotate(ref PilotState, in HullDynamics, in PilotTuning, in PilotInput, double)"/>, with the
		/// nose's top swing and flight assist's roll rate given outright, deg/s, for a model that sets them by rules of its own
		/// (the capital model's steerage, U.SPC-T499).</summary>
		static void Rotate(PilotState& s, const HullDynamics& hull, double turnRateDegS, double rollRateDegS, const PilotInput& input, double dt);

		/// <summary>The least the stick's aim may lead the nose, deg: room for fine aim even on a hull that
		/// stops its swing almost at once.</summary>
		static constexpr double MinAimLeadDeg = 4;

		/// <summary>
		/// How far the stick's aim may run ahead of the nose, radians: as far as the hull needs to brake
		/// its swing from full turn rate (ω²/2α), and no further. That much lets the nose reach its full
		/// rate while the mouse keeps moving; any more and the hull goes on turning after the hand has
		/// stopped, chasing an aim point the player cannot see.
		/// </summary>
		static double AimLeadRad(const HullDynamics& hull, const PilotTuning& tuning);

		/// <summary>Pulls <paramref name="aim"/> back to within <paramref name="maxLeadRad"/> of
		/// <paramref name="forward"/> (both unit), along the great circle between them.</summary>
		static DVec3 ClampAim(DVec3 aim, DVec3 forward, double maxLeadRad);

		/// <summary>Rodrigues' rotation of <paramref name="v"/> by <paramref name="angle"/> radians about unit axis <paramref name="k"/>.</summary>
		static DVec3 RotateAbout(DVec3 v, DVec3 k, double angle);

		/// <summary>Below this speed, km/s, a hull counts as at rest: nothing to flip onto or brake.</summary>
		static constexpr double RestKmS = 1e-6;

		/// <summary>
		/// The turn-and-burn's heading (US-T87): the retrograde, so the main drive fires against the way the hull is
		/// going. The hull's own nose when it is at rest. The helm and the fighter seat steer onto it while the flip key
		/// is held; the hull swings round on its attitude thrusters and keeps sliding along its track meanwhile.
		/// </summary>
		static DVec3 FlipAim(const PilotState& s);

		/// <summary>
		/// What stopping costs right now (US-T87, see <see cref="BrakePlan"/>): the flip at <paramref name="turnRateDegS"/>
		/// with the hull's own angular acceleration (spin up, swing, brake), then the burn at the main drive's push; or
		/// the retro thrusters' push with no flip at all.
		/// </summary>
		static BrakePlan Brake(const PilotState& s, const HullDynamics& hull, double turnRateDegS);

	private:
		static double Clamp(double x);

		static double Limit(double x, double max);
	};
}
