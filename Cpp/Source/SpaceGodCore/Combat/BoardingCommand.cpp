// Port of Assets/UnitySpace/Core/Combat/BoardingCommand.cs (see docs/MIGRATION.md).

#include "Combat/BoardingCommand.h"

#include "Combat/Battle.h"
#include "Combat/Boarding.h"
#include "Combat/CombatState.h"

namespace SpaceGod::Core
{
	const char* ToString(BoardingRole v)
	{
		switch (v)
		{
			case BoardingRole::Boarders: return "Boarders";
			case BoardingRole::Defenders: return "Defenders";
		}
		return "";
	}

	const char* ToString(BoardingOrder v)
	{
		switch (v)
		{
			case BoardingOrder::Auto: return "Auto";
			case BoardingOrder::Push: return "Push";
			case BoardingOrder::Hold: return "Hold";
		}
		return "";
	}

	cs::Nullable<std::tuple<cs::Ref<BattleUnit>, BoardingRole>> BoardingCommand::FightFor(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& unit)
	{
		if (battle == nullptr || unit == nullptr || !unit->Present() || unit->Captured) return std::nullopt;
		if (unit->Combat->Interior->UnderBoarding()) return std::make_tuple(unit, BoardingRole::Defenders);
		auto latched = unit->Latched;
		// Its faction's boarding, alone or (U.SPC-T392) with allies: one party, under one set of orders.
		if (latched != nullptr && latched->Present() && latched->Combat->Interior->UnderBoarding() && latched->Combat->Interior->IsParty(unit->Faction))
			return std::make_tuple(latched, BoardingRole::Boarders);
		for (const auto& o : battle->Units)
			if (o != unit && o->Present() && o->Combat->Interior->UnderBoarding() && o->Combat->Interior->IsParty(unit->Faction))
				return std::make_tuple(o, BoardingRole::Boarders);
		return std::nullopt;
	}

	std::tuple<BoardingOrder, int> BoardingCommand::OrdersOf(const cs::Ref<Interior>& interior, BoardingRole role)
	{
		return role == BoardingRole::Boarders ? std::make_tuple(interior->BoarderOrder, interior->BoarderObjective) : std::make_tuple(interior->DefenderOrder, interior->DefenderObjective);
	}

	int BoardingCommand::NextObjective(const cs::Ref<Interior>& interior, int current)
	{
		return interior->Compartments.Count() == 0 ? -1 : (cs::Math::Max(-1, current) + 1) % interior->Compartments.Count();
	}

	cs::String BoardingCommand::Label(const cs::Ref<Interior>& interior, BoardingRole role)
	{
		auto [order, objective] = OrdersOf(interior, role);
		switch (order)
		{
			case BoardingOrder::Push:
			{
				cs::String where = objective >= 0 && objective < interior->Compartments.Count() ? interior->Compartments[objective]->Name : cs::String("?");
				return cs::String(role == BoardingRole::Boarders ? "push for " : "muster on ") + where;
			}
			case BoardingOrder::Hold:
				return role == BoardingRole::Boarders ? "dig in where they stand" : "hold their posts";
			default:
				return role == BoardingRole::Boarders ? "auto (for the bridge and engineering)" : "auto (muster on the fighting)";
		}
	}
}
