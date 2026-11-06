#include "Combat/Drift.h"
#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Combat/FlightModels.h"
#include "Astro.h"

namespace SpaceGod::Core
{
	void Drift::Step(PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt)
	{
		if (dt <= 0) return;
		// The attitude thrusters alone: assist's roll hold (so a roll stops when let go), no drive, no cruise.
		PilotInput in = input;
		in.FlightAssistOff = false;
		in.Boost = in.Cruise = false;
		in.Throttle = in.StrafeRight = in.StrafeUp = 0;
		double rate = TurnRateDegS(tuning.TurnRateDegS);
		double spin = cs::Math::Max(hull.PitchYawAngularAccel, rate * Astro::Deg2Rad / SpinUpS);
		double roll = cs::Math::Max(hull.RollAngularAccel, tuning.RollRateDegS() * Astro::Deg2Rad / SpinUpS);
		auto rcs = HullDynamics(hull.MassKt, hull.MainAccel, hull.RetroAccel, hull.LateralAccel, hull.VerticalAccel, spin, roll);
		auto forwardBeforeTurn = s.Forward;
		PilotFlight::Rotate(s, rcs, rate, tuning.RollRateDegS(), in, dt);
		// The boost charge refills while the drive is cold, and a cruise ramp drops.
		if (combat != nullptr) PilotFlight::DriveMultiplier(s, combat, tuning, in, forwardBeforeTurn, dt);
		// Barrelling on along the way it had: nothing but gravity changes it.
		PilotFlight::Integrate(s, DVec3::Zero, gravityKmS2, dt);
	}

	void Drift::Fly(const cs::Ref<IFlightModel>& model, PilotState& s, const cs::Ref<ShipCombat>& combat, const HullDynamics& hull, const PilotTuning& tuning, const PilotInput& input, DVec3 gravityKmS2, double dt)
	{
		if (input.Drift || model == nullptr) Step(s, combat, hull, tuning, input, gravityKmS2, dt);
		else model->Step(s, combat, hull, tuning, input, gravityKmS2, dt);
	}

	bool DriftControl::Update(bool held, double dt)
	{
		if (held)
		{
			Active = true;
			BoostOutLeft = 0;
			return false;
		}
		if (Active)
		{
			Active = false;
			BoostOutLeft = Drift::BoostOutS;
			return true;
		}
		BoostOutLeft = cs::Math::Max(0.0, BoostOutLeft - cs::Math::Max(0.0, dt));
		return false;
	}

	void DriftControl::Apply(PilotInput& input) const
	{
		if (Active)
		{
			input.Drift = true;
			return;
		}
		if (BoostOutLeft <= 0) return;
		// The boost-out: snapped back to assisted flight in the new facing, the afterburner lit along it.
		input.Boost = true;
		input.FlightAssistOff = false;
		input.Throttle = cs::Math::Max(input.Throttle, 1.0);
	}
}
