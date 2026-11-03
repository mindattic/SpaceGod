// Port of Unity.SpaceGod Core/Combat/RaidDoctrine.cs (US-T10).

#include "Combat/RaidDoctrine.h"

#include "Combat/CombatSpecs.h"
#include "Combat/Crews.h"
#include "Economy/Contracts.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "Systems/LaneUpkeep.h"

namespace SpaceGod::Core
{
	bool RaidDoctrine::IsCivilian(const cs::Ref<Ship>& ship, const cs::Ref<Contracts>& contracts)
	{
		if (ship == nullptr || ship->Spec->IsMilitary || ship->Faction == nullptr || ship->Faction->IsPirate) return false;
		if (ship->Role == ShipRole::Free) return true;
		if (contracts != nullptr)
			for (const auto& c : contracts->All)
				if (c->Taker == ship && c->IsActive() && c->Aboard && c->People > 0 && (c->Kind == ContractKind::Passengers || c->Kind == ContractKind::Colonists))
					return true;
		return false;
	}

	double RaidDoctrine::Heat(const cs::Ref<Ship>& ship, const cs::Ref<Contracts>& contracts) { return IsCivilian(ship, contracts) ? CivilianHeat : 1; }

	double RaidDoctrine::PreyValue(const cs::Ref<Ship>& ship, const cs::Ref<Contracts>& contracts)
	{
		if (ship == nullptr) return 0;
		double value = ship->HasCargo ? ship->CargoTonnes * GoodInfo::Of(ship->CargoGood)->BasePrice : 0;
		// A lane tender (U.SPC-T481): whoever wants a system's lanes cut pays well for one.
		if (LaneUpkeep::IsTender(ship)) value += LaneUpkeep::TenderValue()->Value;
		if (contracts != nullptr)
			for (const auto& c : contracts->All)
			{
				if (c->Taker != ship || !c->IsActive()) continue;
				if (c->Kind != ContractKind::Freight && c->Kind != ContractKind::Passengers && c->Kind != ContractKind::Colonists) continue;
				value += c->Pay;
				if (!c->Aboard || c->People <= 0) continue;
				value += c->People * Crews::RansomPerHead;
				if (c->Kind == ContractKind::Passengers && c->Pay >= VipFare) value += c->Pay * VipRansomMultiple;
			}
		return value;
	}

	double RaidDoctrine::Nerve(double value)
	{
		return cs::Math::Min(MaxNerve, BaseNerve + Greed * cs::Math::Log10(1 + cs::Math::Max(0.0, value) / ReferenceHaul));
	}

	bool RaidDoctrine::WorthTaking(double value, double heat, double guns, double bandStrength)
	{
		if (heat > 1 && value < CivilianMinPrize) return false;
		return guns <= bandStrength * Nerve(value);
	}

	double RaidDoctrine::Score(double value, double heat, double guns, double bandStrength)
	{
		return (value + 1) / cs::Math::Max(1.0, heat) / (1 + cs::Math::Max(0.0, guns) / cs::Math::Max(1e-6, bandStrength));
	}

	double RaidDoctrine::DestroyHeat(const cs::Ref<Ship>& ship, const cs::Ref<Contracts>& contracts)
	{
		if (ship == nullptr || ship->Spec->IsMilitary || ship->Faction == nullptr || ship->Faction->IsPirate) return 0;
		return DestroyHeatShare * (Economy::HullPrice(ship->Spec->Class) + PreyValue(ship, contracts)) * Heat(ship, contracts);
	}

	bool RaidDoctrine::Robs(const cs::Ref<Faction>& raider, const cs::Ref<Ship>& target)
	{
		return raider != nullptr && raider->IsPirate && target != nullptr && !target->Spec->IsMilitary && target->Faction != nullptr && !target->Faction->IsPirate &&
			!LaneUpkeep::IsTender(target);
	}

	bool RaidDoctrine::IsPicket(const cs::Ref<Ship>& ship) { return ship != nullptr && ship->Spec->Class == ShipClass::GatePicket; }

	double RaidDoctrine::TargetBias(const cs::Ref<Faction>& raider, const cs::Ref<Ship>& target)
	{
		return raider != nullptr && raider->IsPirate && IsPicket(target) && target->Faction != nullptr && !target->Faction->IsPirate ? PicketFirstKm : 0;
	}

	double RaidDoctrine::SubsystemWeight(SubsystemKind kind, bool shieldUp)
	{
		switch (kind)
		{
			case SubsystemKind::Engine: return 4;
			case SubsystemKind::ShieldGenerator: return shieldUp ? 4 : 0.3;
			case SubsystemKind::Reactor: return 0;
			case SubsystemKind::Bridge: return 0.2;
			case SubsystemKind::Hangar: return 0.3;
			default: return 1;
		}
	}
}
