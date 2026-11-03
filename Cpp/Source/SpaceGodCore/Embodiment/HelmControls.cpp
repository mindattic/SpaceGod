// Port of Unity.SpaceGod Core/Embodiment/HelmControls.cs (US-T21, see docs/MIGRATION.md).

#include "Embodiment/HelmControls.h"
#include "Ships.h"
#include "Combat/CombatState.h"
#include "Combat/FlightModels.h"
#include "Combat/HaulerFlight.h"

namespace SpaceGod::Core
{
	const char* ToString(HelmRole v)
	{
		switch (v)
		{
		case HelmRole::Fighter: return "Fighter";
		case HelmRole::Escort: return "Escort";
		case HelmRole::Courier: return "Courier";
		case HelmRole::Freighter: return "Freighter";
		case HelmRole::Miner: return "Miner";
		case HelmRole::Capital: return "Capital";
		}
		return "";
	}

	HelmControls HelmControls::Of(const cs::Ref<Ship>& ship)
	{
		auto c = ship->Spec->Class;
		auto stick = PilotTuning::ControlOf(c);
		auto model = FlightModelRegistry::Default()->For(c);
		bool guns = ship->Combat != nullptr && ship->Combat->Armed();
		return HelmControls(RoleOf(ship), stick, stick == PilotControl::Hauler ? HaulerLookFactor : 1.0,
			cs::Is<HaulerFlightModel>(model), cs::Is<CapitalFlightModel>(model), guns);
	}

	HelmRole HelmControls::RoleOf(const cs::Ref<Ship>& ship)
	{
		auto c = ship->Spec->Class;
		auto family = FlightModelRegistry::Default()->FamilyOf(c);
		if (family == FlightFamily::Capital) return HelmRole::Capital;
		if (ship->IsEscort() || ship->Role == ShipRole::Escort) return HelmRole::Escort;
		if (c == ShipClass::Courier || ship->Role == ShipRole::Courier) return HelmRole::Courier;
		if (c == ShipClass::Miner || c == ShipClass::OreTug || c == ShipClass::CaptureTug || ship->Role == ShipRole::Miner || ship->Role == ShipRole::Tug)
			return HelmRole::Miner;
		return PilotTuning::ControlOf(c) == PilotControl::Hauler ? HelmRole::Freighter : HelmRole::Fighter;
	}

	cs::String HelmControls::Hint() const
	{
		cs::String fire = Guns ? "" : ", unarmed";
		switch (Role)
		{
		case HelmRole::Capital:
			return "capital: W/S ring the engine telegraph, A/D Q/E turn the hull, mouse looks round it, hold left mouse for a broadside";
		case HelmRole::Escort:
			return cs::Cat("escort, flying free of its formation: ", StickHint(), fire);
		case HelmRole::Courier:
			return cs::Cat("courier: ", StickHint(), ", Shift for the long boost", fire);
		case HelmRole::Freighter:
			return cs::Cat("freighter: ", StickHint(), Heavy(), fire);
		case HelmRole::Miner:
			return cs::Cat("miner: ", StickHint(), Heavy(), fire);
		default:
			return cs::Cat("fighter: ", StickHint(), ", Shift boost", fire);
		}
	}

	cs::String HelmControls::Heavy() const { return DockingMode ? ", flies heavy on its load, Ctrl in a port's approach to dock" : ""; }

	cs::String HelmControls::StickHint() const { return Stick == PilotControl::FreeAim ? "mouse aims, the hull turns after it" : "mouse steers the nose"; }
}
