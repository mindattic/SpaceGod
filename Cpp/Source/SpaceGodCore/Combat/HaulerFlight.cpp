#include "Combat/HaulerFlight.h"
#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	using cs::Math::Max;
	using cs::Math::Min;

	double HaulerFlightModel::LadenRatio(const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr) return 1;
		double ready = ship->DryMassTonnes() + ship->PropellantCapacity();
		return ready > 0 ? ship->MassTonnes() / ready : 1;
	}

	double HaulerFlightModel::MassFactor(double massRatio) const
	{
		if (massRatio <= 0 || cs::Double::IsNaN(massRatio) || cs::Double::IsInfinity(massRatio)) return 1;
		return Max(MinMassFactor, 1 + Max(0.0, Handling.MassWeight) * (massRatio - 1));
	}

	HullDynamics HaulerFlightModel::Laden(const HullDynamics& hull, double massRatio) const
	{
		double k = MassFactor(massRatio);
		if (k == 1) return hull;
		return HullDynamics(hull.MassKt * k, hull.MainAccel / k, hull.RetroAccel / k, hull.LateralAccel / k, hull.VerticalAccel / k,
			hull.PitchYawAngularAccel / k, hull.RollAngularAccel / k);
	}

	bool HaulerFlightModel::Docking(const PilotInput& input) { return input.Fine && input.DockingApproach && !input.FlightAssistOff; }

	PilotTuning HaulerFlightModel::HelmTuning(const PilotTuning& tuning, const PilotInput& input) const
	{
		double k = MassFactor(input.MassRatio);
		bool docking = Docking(input);
		double turn = tuning.TurnRateDegS / k * (docking ? Clamp01(Handling.DockingTurnFraction) : 1);
		double speed = docking ? tuning.MaxSpeed * Clamp01(Handling.DockingSpeedFraction) : tuning.MaxSpeed;
		double boost = tuning.BoostDurationS * Max(0.0, Handling.BoostDurationFraction);
		return PilotTuning(speed, turn, tuning.BoostSpeedMultiplier, boost, tuning.CruiseSpeedMultiplier, tuning.CruiseRampS);
	}

	HaulerStop HaulerFlightModel::StopPoint(const PilotState& s, const HullDynamics& hull, const PilotInput& input) const
	{
		return StopPoint(s, Laden(hull, input.MassRatio));
	}

	HaulerStop HaulerFlightModel::StopPoint(const PilotState& s, const HullDynamics& hull)
	{
		auto f = s.Forward;
		auto r = s.Right();
		auto u = s.Up;
		double vf = DVec3::Dot(s.Vel, f), vr = DVec3::Dot(s.Vel, r), vu = DVec3::Dot(s.Vel, u);
		double seconds = 0;
		// C# evaluates the three Brake calls left to right (each updates seconds); keep that order explicit.
		double bf = Brake(vf, vf > 0 ? hull.RetroAccel : hull.MainAccel, seconds);
		double br = Brake(vr, hull.LateralAccel, seconds);
		double bu = Brake(vu, hull.VerticalAccel, seconds);
		auto offset = f * bf
			+ r * br
			+ u * bu;
		return HaulerStop(offset, seconds);
	}

	double HaulerFlightModel::Brake(double v, double a, double& seconds)
	{
		if (cs::Math::Abs(v) < 1e-12) return 0;
		if (a <= 0)
		{
			seconds = cs::Double::PositiveInfinity;
			return 0;
		}
		seconds = Max(seconds, cs::Math::Abs(v) / a);
		return v * cs::Math::Abs(v) / (2 * a);
	}

	void HaulerFlightModel::Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt)
	{
		auto laden = Laden(hull, input.MassRatio);
		auto helm = HelmTuning(tuning, input);
		if (input.FlightAssistOff)
		{
			PilotFlight::Step(s, combat, laden, helm, input, gravityKmS2, dt);
			return;
		}
		if (dt <= 0) return;

		// Docking mode is a slow, careful approach: no boost and no cruise.
		auto drive = input;
		if (Docking(input))
		{
			drive.Boost = false;
			drive.Cruise = false;
		}
		auto forwardBeforeTurn = s.Forward;
		PilotFlight::Rotate(s, laden, helm, drive, dt);
		double driveMult = PilotFlight::DriveMultiplier(s, combat, helm, drive, forwardBeforeTurn, dt);
		auto thrust = PilotFlight::AssistThrust(s, laden, helm, drive, gravityKmS2, driveMult, dt, Handling.StrafeSpeedFraction,
			laden.LateralAccel, laden.VerticalAccel);
		PilotFlight::Integrate(s, thrust, gravityKmS2, dt);
	}

	double HaulerFlightModel::Clamp01(double x) { return Max(0.0, Min(1.0, x)); }
}
