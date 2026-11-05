// Port of Runtime/UnitySpaceApp.Broadsides.cs (declarations in UnitySpaceApp.Broadsides.inl).

#include "UnitySpaceApp.h"

#include "Combat/Battle.h"
#include "Combat/Broadsides.h"
#include "Combat/PilotTuning.h" // PilotControl

using namespace SpaceGod::Core;

namespace SpaceGod
{
	void UnitySpaceApp::UpdateHelmCommand(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& unit)
	{
		if (PilotControlMode != PilotControl::Helm || !Core::Broadsides::Commands(unit)) return;
		if (unit->CargoDemandedBy != nullptr || battle->AwaitingTermsFrom(unit->Side) != nullptr) return;
		// The boarding panel, while open, has the number keys (U.SPC-T468).
		if (BoardingPanelOpen()) return;
		int key = Input.NumberKey;
		if (key < 0) return;
		auto look = PilotLookDirection();
		switch (key)
		{
			case 1:
			{
				auto [order, mounts] = Core::Broadsides::CycleOrder(unit, look);
				ShowToast(mounts == 0 ? cs::String("No gun bears there") : cs::Cat("The battery bearing there (", mounts, "): ", Core::Broadsides::Label(order)));
				break;
			}
			case 2:
				ShowToast(Core::Broadsides::ToggleSpreadFire(unit) ? "Gunners: spread your fire" : "Gunners: go for the weakened shields");
				break;
			case 3:
				ShowToast(Core::Broadsides::Brace(unit) ? "Brace, brace, brace!" : "The crew is still getting back to their stations");
				break;
			case 4:
				ShowToast(cs::Cat("Damage control: ", Core::Broadsides::Label(Core::Broadsides::CycleFocus(unit)), " first"));
				break;
			case 5: OrderLine(battle, unit, FleetFormation::LineAhead); break;
			case 6: OrderLine(battle, unit, FleetFormation::LineAbreast); break;
			case 7: OrderLine(battle, unit, FleetFormation::Echelon); break;
			case 8: OrderLine(battle, unit, FleetFormation::Screen); break;
			case 9: OrderLine(battle, unit, FleetFormation::EngageAtWill); break;
			case 0:
				Core::Broadsides::SetAll(unit, BatteryOrder::FireAtWill);
				ShowToast("All batteries: fire at will");
				break;
		}
	}

	void UnitySpaceApp::OrderLine(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& flag, FleetFormation formation)
	{
		int heard = Core::Broadsides::OrderFormation(battle, flag, formation);
		ShowToast(heard == 0 ? cs::String("No ships of the line to order") : cs::Cat("The line (", heard, "): ", Core::Broadsides::Label(formation)));
	}
}
