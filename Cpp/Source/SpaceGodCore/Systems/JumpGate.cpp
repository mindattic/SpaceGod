// Port of Assets/UnitySpace/Core/Systems/JumpGate.cs (US-T17).

#include "Systems/JumpGate.h"
#include "Systems/Galaxy.h"
#include "Ships.h"
#include "World.h"
#include "Combat/Diplomacy.h"
#include "Combat/CombatState.h"

namespace SpaceGod::Core
{
	bool JumpGate::HostileTo(const cs::Ref<Faction>& faction) const
	{
		// C#: Home?.World.Diplomacy.Get(Owner, faction) == Relation.War (a null Home compares false).
		return Owner != nullptr && faction != nullptr && faction != Owner && !faction->IsPirate && Home != nullptr && Home->World->Diplomacy()->Get(Owner, faction) == Relation::War;
	}

	JumpGate::JumpGate(const cs::String& name, const cs::Ref<StarSystem>& home, DVec3 positionKm)
		: Name(name), Home(home), PositionKm(positionKm)
	{
	}

	bool JumpGate::Damage(double amount)
	{
		bool was = Working();
		Integrity = cs::Math::Max(0.0, Integrity - cs::Math::Max(0.0, amount));
		return was && !Working();
	}

	bool JumpGate::Admits(const cs::Ref<Faction>& faction, double jd) const
	{
		if (Owner == nullptr || faction == nullptr || faction == Owner) return true;
		if (Blockaded(jd)) return false;
		// C#: Home?.World.Diplomacy.Get(Owner, faction) != Relation.War (a null Home compares true).
		return Home == nullptr || Home->World->Diplomacy()->Get(Owner, faction) != Relation::War;
	}
}
