#pragma once
#include "Cs/Cs.h"
#include "DVec3.h"
#include "Combat/FlightModels.h"

namespace SpaceGod::Core
{
	class Ship;
	class ShipCombat;
	struct PilotState;
	struct PilotInput;
	struct PilotTuning;
	struct HullDynamics;

	/// <summary>
	/// A hauler's handling numbers (U.SPC-T443, docs/FLIGHT_MODELS.md, Haulers): data, not code, like
	/// <see cref="LightFighterHandling"/>, so a tuning pass or the settings registry (U.SPC-T431) changes them without
	/// touching <see cref="HaulerFlightModel"/>.
	/// </summary>
	struct SPACEGODCORE_API HaulerHandling
	{
		/// <summary>The mass-per-tonne weighting: how much of each tonne aboard beyond the hull's ready mass (dry, tanks
		/// full, hold empty: what its thrust and turn figures were set for) counts against its drive, thrusters and
		/// attitude thrusters. At 1 the hull is honest Newtonian mass (double the mass, half the push); below 1 a full hold
		/// weighs on the hull less than physics would, for feel. Tanks burned down make it lighter than ready, and a little
		/// handier, by the same rule.</summary>
		double MassWeight = 0;

		/// <summary>Docking mode's set point (Ctrl held inside a port's approach), as a fraction of
		/// <see cref="PilotTuning.MaxSpeed"/>: the throttle floored asks only for docking speed.</summary>
		double DockingSpeedFraction = 0;

		/// <summary>Docking mode's top turn rate, as a fraction of the hull's own (laden) one, for precise placement.</summary>
		double DockingTurnFraction = 0;

		/// <summary>Flight assist's strafe set point as a fraction of the forward one: a hauler barely sidesteps.</summary>
		double StrafeSpeedFraction = 0;

		/// <summary>The boost's charge as a fraction of the class's <see cref="PilotTuning.BoostDurationS"/>: a short
		/// emergency burn to get clear, not a dodge.</summary>
		double BoostDurationFraction = 0;

		constexpr HaulerHandling() = default;

		constexpr HaulerHandling(double massWeight, double dockingSpeedFraction, double dockingTurnFraction, double strafeSpeedFraction,
			double boostDurationFraction)
			: MassWeight(massWeight), DockingSpeedFraction(dockingSpeedFraction), DockingTurnFraction(dockingTurnFraction),
			  StrafeSpeedFraction(strafeSpeedFraction), BoostDurationFraction(boostDurationFraction)
		{
		}

		/// <summary>The first-pass numbers, for the owner's review (docs/FLIGHT_MODELS.md): a full truss freighter (20,000 t in
		/// its pods on a 15,000 t ready hull) pushes and turns at a little over half what it does empty.</summary>
		static const HaulerHandling Default;
	};
	inline constexpr HaulerHandling HaulerHandling::Default = HaulerHandling(
		/*massWeight:*/ 0.6, /*dockingSpeedFraction:*/ 0.1, /*dockingTurnFraction:*/ 0.35, /*strafeSpeedFraction:*/ 0.25, /*boostDurationFraction:*/ 0.25);

	/// <summary>
	/// Where a hull comes to rest under full retro (U.SPC-T443): every thruster set braking its own share of the way at
	/// its full push, the retros against the way along the nose (the main drive for a hull backing), the manoeuvre
	/// thrusters across it. What the HUD shows a hauler's pilot so the braking can be planned.
	/// </summary>
	struct SPACEGODCORE_API HaulerStop
	{
		/// <summary>From the hull to where it stops, km (its own frame's axes).</summary>
		DVec3 OffsetKm;

		/// <summary>How long it takes to stop, s (the slowest axis); infinite when a set of thrusters that is needed has no push.</summary>
		double Seconds = 0;

		HaulerStop() = default;

		HaulerStop(DVec3 offsetKm, double seconds) : OffsetKm(offsetKm), Seconds(seconds) {}

		/// <summary>How far off the stop point is, km.</summary>
		double DistanceKm() const { return OffsetKm.Length(); }

		/// <summary>Whether the hull can stop at all.</summary>
		bool Reachable() const { return !cs::Double::IsInfinity(Seconds) && !cs::Double::IsNaN(Seconds); }
	};

	/// <summary>
	/// Haulers (U.SPC-T443, docs/FLIGHT_MODELS.md): freighters, miners and the other work hulls, satisfying to fly because
	/// they are heavy. (1) Laden mass: what is aboard (the hold, the personal grid hold and the tanks, given as
	/// <see cref="PilotInput.MassRatio"/>) weighs on the drive, retros, manoeuvre and attitude thrusters and the top turn,
	/// by <see cref="HaulerHandling.MassWeight"/>, so a full hull gathers way, brakes and turns slower than an empty one.
	/// (2) The retros stay weak, and <see cref="StopPoint(in PilotState, in HullDynamics, in PilotInput)"/> is where the
	/// hull comes to rest at full retro, for the HUD. (3) Docking mode: holding Ctrl (<see cref="PilotInput.Fine"/>) inside
	/// a port's approach (<see cref="PilotInput.DockingApproach"/>) drops the set point to docking speed and gentles the
	/// turn, with no boost or cruise. (4) No combat agility: the strafe is weak and the boost a short emergency burn.
	/// With flight assist off the hull is the Newtonian one on its laden mass: mass is physics, not the flight computer.
	/// </summary>
	class SPACEGODCORE_API HaulerFlightModel final : public IFlightModel
	{
	public:
		/// <summary>The least a light hull's mass factor goes to, however far its tanks are burned down.</summary>
		static constexpr double MinMassFactor = 0.5;

		HaulerHandling Handling;

		HaulerFlightModel() : HaulerFlightModel(HaulerHandling::Default) {}

		HaulerFlightModel(HaulerHandling handling) : Handling(handling) {}

		cs::String Name() const override { return "Hauler"; }

		/// <summary>
		/// A ship's all-up mass (hull, tanks and holds, <see cref="Ship.MassTonnes"/>) over its ready mass (dry with full
		/// tanks and an empty hold): what the helm passes as <see cref="PilotInput.MassRatio"/>. 1 for a hull fresh from its
		/// yard, more for a laden one.
		/// </summary>
		static double LadenRatio(const cs::Ref<Ship>& ship);

		/// <summary>What a mass ratio does to the hull: the divisor on every push and on the turn (1 for a ready hull, or
		/// for no ratio given), by <see cref="HaulerHandling.MassWeight"/>.</summary>
		double MassFactor(double massRatio) const;

		/// <summary>The hull with its load aboard: every thruster set's push and the attitude thrusters' spin cut by the mass factor.</summary>
		HullDynamics Laden(const HullDynamics& hull, double massRatio) const;

		/// <summary>Whether this frame flies in docking mode: Ctrl held inside a port's approach, flight assist on.</summary>
		static bool Docking(const PilotInput& input);

		/// <summary>The flight computer's figures for a hauler this frame: the top turn cut by the load (and by docking mode),
		/// the boost a short burn, and in docking mode the set point at docking speed.</summary>
		PilotTuning HelmTuning(const PilotTuning& tuning, const PilotInput& input) const;

		/// <summary>Where the hull comes to rest under full retro with this frame's load aboard (see <see cref="HaulerStop"/>).</summary>
		HaulerStop StopPoint(const PilotState& s, const HullDynamics& hull, const PilotInput& input) const;

		/// <summary>Where a hull of <paramref name="hull"/>'s push comes to rest under full retro (see <see cref="HaulerStop"/>):
		/// each axis of its way braked by its own thrusters at full push, v²/2a along each.</summary>
		static HaulerStop StopPoint(const PilotState& s, const HullDynamics& hull);

	private:
		static double Brake(double v, double a, double& seconds);

	public:
		void Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt) override;

	private:
		static double Clamp01(double x);
	};
}
