#pragma once
// US-T170: the flight feel pass. How flying each kind of hull feels on screen, apart from how it flies: the sense of speed
// (speed streaks and dust, the view widening with pace and kicking wide on the afterburner), and the hull's heft (the
// camera's shake and rumble under thrust, scaled by its mass), per role -- a fighter twitchy and thrilling, a freighter
// lumbering, a capital ship immense, a repair drone nimble. View only: nothing here moves a hull or touches the sim.

#include "Cs/Cs.h"
#include "Combat/PilotTuning.h" // PilotControl (by value)

namespace SpaceGod::Core
{
	/// <summary>The feel a hull is flown with (US-T170), from how it is steered.</summary>
	enum class FeelRole : int
	{
		/// <summary>A strike craft: snappy, a fast high buzz under thrust, a big kick on the afterburner.</summary>
		Fighter,

		/// <summary>A freighter, hauler or miner: lumbering, a slow throb that builds as the drive takes the load.</summary>
		Hauler,

		/// <summary>A capital ship: immense, a deep slow rumble, and a broadside that shakes the whole view.</summary>
		Capital,

		/// <summary>A repair drone or EVA worker: nimble and light, a sharp tactile tick at every good weld.</summary>
		Drone,
	};
	SPACEGODCORE_API const char* ToString(FeelRole v);

	/// <summary>
	/// The numbers one role's feel is tuned by (US-T170): what the view adds to its field of view (degrees) with pace, the
	/// afterburner and the cruise drive, how fast it gets there, and the camera's shake (degrees, Hz) and heft (seconds the
	/// shake takes to build and die away). <see cref="Of"/> scales the role's own by the hull's mass: the heavier the hull,
	/// the slower and deeper its rumble and the longer it takes to come on.
	/// </summary>
	struct SPACEGODCORE_API FlightFeelProfile
	{
		FeelRole Role = FeelRole::Fighter;
		double MassTonnes = 0;

		/// <summary>Degrees the view widens with the afterburner lit, under the cruise drive, and at full pace.</summary>
		double BoostFovDeg = 0, CruiseFovDeg = 0, SpeedFovDeg = 0;

		/// <summary>The extra kick the view takes for a moment as the afterburner lights, degrees.</summary>
		double IgnitionFovDeg = 0;

		/// <summary>How quickly the field of view follows, per second.</summary>
		double FovResponseHz = 0;

		/// <summary>Peak shake at full load (degrees) and its pitch (Hz).</summary>
		double ShakeDeg = 0, ShakeHz = 0;

		/// <summary>Seconds the shake takes to build and fade: the hull's heft.</summary>
		double HeftS = 0;

		/// <summary>A one-off jolt (<see cref="FlightFeel.Kick"/>): a broadside fired, flares thrown, a weld struck; degrees.</summary>
		double KickDeg = 0;

		/// <summary>The hull's pace, as a share of its flight-assist top speed, at which the speed streaks run full.</summary>
		double StreakFullPace = 0;

		/// <summary>The role's feel, scaled by a hull of <paramref name="massTonnes"/>.</summary>
		static FlightFeelProfile Of(FeelRole role, double massTonnes);

		/// <summary>The feel of a hull steered with <paramref name="stick"/>.</summary>
		static FeelRole RoleOf(PilotControl stick);

		/// <summary>The mass's share of the heft, 0 (a light craft) to about 1.2 (the biggest hulls): log10 of tonnes over 5.</summary>
		static double MassScale(double massTonnes);
	};

	/// <summary>One frame's flight, as the feel reads it (US-T170).</summary>
	struct SPACEGODCORE_API FlightFeelInput
	{
		/// <summary>The hull's speed as a share of its flight-assist top speed.</summary>
		double Pace = 0;

		/// <summary>How hard the drive is pushing, as a share of the main drive's full thrust (0..1, more with the afterburner).</summary>
		double Thrust = 0;

		bool Boosting = false;
		bool Cruising = false;
	};

	/// <summary>
	/// The feel of the hull being flown (US-T170), stepped once a frame: the speed streaks' strength, the field of view
	/// the camera eases toward, and the camera's shake as small turns (degrees) to lay over the view. Deterministic: its
	/// shake is a sum of sines on its own clock, so the same frames give the same view (a capture, a co-op peer, a replay).
	/// </summary>
	class SPACEGODCORE_API FlightFeel final
	{
	public:
		FlightFeelProfile Profile;

		/// <summary>The speed streaks' and dust's strength, 0..1.</summary>
		double Streaks = 0;

		/// <summary>Degrees the view is held widened over its base by pace, afterburner and cruise drive, eased.</summary>
		double FovDeg = 0;

		/// <summary>What the view is widened by this frame: <see cref="FovDeg"/> plus the afterburner's ignition punch.</summary>
		double ViewFovDeg() const { return FovDeg + ignition * Profile.IgnitionFovDeg; }

		/// <summary>The load the shake is running at (0..1+), eased by the hull's heft.</summary>
		double Load = 0;

		/// <summary>The shake this frame, as turns of the view: yaw, pitch and roll, degrees.</summary>
		double ShakeYawDeg = 0, ShakePitchDeg = 0, ShakeRollDeg = 0;

		/// <summary>US-T242: the last frame flown as <see cref="Step"/> was given it (all zero once <see cref="Settle"/> runs: off
		/// the controls), for the engine's sound to follow.</summary>
		FlightFeelInput Last;

		/// <summary>Starts over on a new hull with its own feel.</summary>
		void Reset(const FlightFeelProfile& profile);

		/// <summary>A jolt: <paramref name="strength"/> of the role's <see cref="FlightFeelProfile.KickDeg"/>, dying away in a
		/// moment (a broadside, flares thrown, a weld struck). Kicks stack up to twice the role's.</summary>
		void Kick(double strength = 1);

		/// <summary>One frame of <paramref name="dt"/> seconds.</summary>
		void Step(double dt, const FlightFeelInput& in);

		/// <summary>Everything still: no shake, no streaks, the view at its base (off the helm).</summary>
		void Settle(double dt);

	private:
		double t = 0, kick = 0, ignition = 0;
		bool boostWas = false;

		void Shake(double amplitude);
	};

	/// <summary>
	/// A hauler's flare-and-chaff dispenser at the helm (US-T170): a freighter has no fighter's dispenser hardpoint, so
	/// its helm carries a magazine of its own to spoof the heat-seekers pirates throw at it. Ctrl in a fight throws one
	/// (<see cref="Battle.ThrowFlares"/>); <see cref="CooldownS"/> between throws. Not saved: the magazine is full each
	/// time a hull's helm is taken.
	/// </summary>
	struct SPACEGODCORE_API HelmFlares
	{
		static constexpr int Magazine = 6;
		static constexpr double CooldownS = 2.0;

		int Left = Magazine;
		double Cooldown = 0;

		/// <summary>Whether one can be thrown now.</summary>
		bool Ready() const { return Left > 0 && Cooldown <= 0; }

		/// <summary>One thrown: the magazine down by one and the dispenser cycling.</summary>
		void Spend()
		{
			Left--;
			Cooldown = CooldownS;
		}

		void Step(double dt) { Cooldown = Cooldown > dt ? Cooldown - dt : 0; }

		void Refill() { *this = HelmFlares(); }
	};
}
