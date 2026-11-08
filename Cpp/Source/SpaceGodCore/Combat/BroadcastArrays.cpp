// US-T96: a hull's broadcast array (see Combat/BroadcastArrays.h).

#include "Combat/BroadcastArrays.h"

#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"

namespace SpaceGod::Core
{
	bool BroadcastArrays::Carries(const cs::Ref<BattleUnit>& u)
	{
		return u != nullptr && u->Kind == UnitKind::Ship && u->Ship != nullptr && u->Combat != nullptr;
	}

	cs::Ref<Subsystem> BroadcastArrays::Of(const cs::Ref<BattleUnit>& u)
	{
		if (!Carries(u)) return nullptr;
		if (u->Broadcast == nullptr)
		{
			auto def = cs::New<SubsystemDef>(SubsystemKind::BroadcastArray, LocalX, LocalY, LocalZ);
			def->MaxHp = Hp;
			// Not one of the hull's hardpoints: no index into ShipCombat.Subsystems (an effect naming it names the hull).
			u->Broadcast = cs::New<Subsystem>(def, -1);
		}
		return u->Broadcast;
	}

	bool BroadcastArrays::Is(const cs::Ref<BattleUnit>& u, const cs::Ref<Subsystem>& s)
	{
		return u != nullptr && s != nullptr && u->Broadcast == s;
	}

	bool BroadcastArrays::Silenced(const cs::Ref<BattleUnit>& u)
	{
		return u != nullptr && u->Broadcast != nullptr && u->Broadcast->Destroyed();
	}

	double BroadcastArrays::Health(const cs::Ref<BattleUnit>& u)
	{
		return u == nullptr || u->Broadcast == nullptr ? 1 : u->Broadcast->Health();
	}

	cs::Ref<Subsystem> BroadcastArrays::Struck(const cs::Ref<BattleUnit>& shooter, const cs::Ref<BattleUnit>& hit, DVec3 at, const cs::Ref<Subsystem>& nearest)
	{
		if (shooter == nullptr || hit == nullptr || !shooter->PlayerControlled() || hit != shooter->PlayerLockTarget) return nearest;
		auto array = shooter->PlayerSubTarget;
		if (!Is(hit, array) || array->Destroyed()) return nearest;
		double d = (hit->PointOf(array->Def) - at).Length();
		if (d > hit->LengthKm * ReachShare) return nearest;
		if (nearest != nullptr && (hit->PointOf(nearest->Def) - at).Length() < d) return nearest;
		return array;
	}

	cs::Ref<Subsystem> BroadcastArrays::Cycle(const cs::Ref<BattleUnit>& target, const cs::Ref<Subsystem>& current, int step)
	{
		if (target == nullptr) return nullptr;
		auto list = WeakPoints::On(target);
		auto array = Of(target);
		if (array != nullptr && !array->Destroyed()) list.Add(array);
		if (list.Count() == 0) return nullptr;
		int i = current == nullptr ? -1 : list.IndexOf(current);
		if (i < 0) return step >= 0 ? list[0] : list[list.Count() - 1];
		return list[((i + step) % list.Count() + list.Count()) % list.Count()];
	}
}
