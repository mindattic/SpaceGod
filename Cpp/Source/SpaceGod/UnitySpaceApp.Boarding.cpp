// Port of Runtime/UnitySpaceApp.Boarding.cs (declarations in UnitySpaceApp.Boarding.inl).

#include "UnitySpaceApp.h"

#include "Combat/Battle.h"
#include "Combat/Boarding.h"
#include "Combat/BoardingCommand.h"
#include "Combat/CombatState.h"

#include <tuple>

using namespace SpaceGod::Core;

namespace SpaceGod
{
	bool UnitySpaceApp::BoardingPanelOpen()
	{
		return ShowBoarding && IsPiloting() && BoardingFight.has_value() && std::get<0>(*BoardingFight)->Combat->Interior->UnderBoarding();
	}

	void UnitySpaceApp::UpdateBoardingCommand(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& unit)
	{
		auto fight = BoardingCommand::FightFor(battle, unit);
		cs::Ref<BattleUnit> fightHull = fight.has_value() ? std::get<0>(*fight) : nullptr;
		cs::Ref<BattleUnit> heldHull = BoardingFight.has_value() ? std::get<0>(*BoardingFight) : nullptr;
		if (fightHull != heldHull) BoardingPick = -1;
		BoardingFight = fight;
		if (Input.ToggleBoarding)
		{
			if (!fight.has_value()) ShowToast("No fight aboard to command");
			ShowBoarding = fight.has_value() && !ShowBoarding;
		}
		if (!fight.has_value())
		{
			ShowBoarding = false;
			return;
		}
		if (!ShowBoarding) return;
		// A hail is answered first.
		if (unit->CargoDemandedBy != nullptr || battle->AwaitingTermsFrom(unit->Side) != nullptr) return;
		int key = Input.NumberKey;
		if (key < 0) return;
		auto [hull, role] = *fight;
		auto inside = hull->Combat->Interior;
		switch (key)
		{
			case 1:
				BoardingPick = BoardingCommand::NextObjective(inside, BoardingPick);
				if (BoardingPick >= 0) ShowToast(cs::Cat("Picked: ", inside->Compartments[BoardingPick]->Name));
				break;
			case 2:
				if (BoardingPick < 0) BoardingPick = BoardingCommand::NextObjective(inside, BoardingPick);
				if (inside->Command(role, BoardingOrder::Push, BoardingPick)) ShowToast(cs::Cat(Side(role), BoardingCommand::Label(inside, role)));
				break;
			case 3:
				if (inside->Command(role, BoardingOrder::Hold)) ShowToast(cs::Cat(Side(role), BoardingCommand::Label(inside, role)));
				break;
			case 4:
				// US-T82: over the hull for the picked space, out through the hatches of what they hold.
				if (role != BoardingRole::Boarders)
				{
					ShowToast("Only boarders go over the hull");
					break;
				}
				if (BoardingPick < 0) BoardingPick = BoardingCommand::NextObjective(inside, BoardingPick);
				if (inside->Command(role, BoardingOrder::OverHull, BoardingPick)) ShowToast(cs::Cat(Side(role), BoardingCommand::Label(inside, role)));
				else if (BoardingPick >= 0) ShowToast(cs::Cat(inside->Compartments[BoardingPick]->Name, " has no hatch to the hull"));
				break;
			case 0:
				inside->AutoResolve(role);
				ShowToast(cs::Cat(Side(role), BoardingCommand::Label(inside, role)));
				break;
		}
	}

	cs::String UnitySpaceApp::Side(BoardingRole role) { return role == BoardingRole::Boarders ? "Our boarders: " : "Our marines: "; }
}
