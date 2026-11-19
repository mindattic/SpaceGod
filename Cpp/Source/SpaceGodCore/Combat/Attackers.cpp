// US-T307: the player's "lock attacker" key (see Attackers.h).

#include "Combat/Attackers.h"

#include "Combat/Battle.h"

namespace SpaceGod::Core
{
	namespace
	{
		bool Fighting(const Battle& battle, const cs::Ref<BattleUnit>& me, const cs::Ref<BattleUnit>& o)
		{
			return o != nullptr && o != me && o->Present() && !o->Surrendering && !o->Captured && battle.Hostile(me, o);
		}

		struct Ranked
		{
			cs::Ref<BattleUnit> unit;
			double distSq = 0;
		};
	}

	cs::List<cs::Ref<BattleUnit>> Attackers::Of(const Battle& battle, const cs::Ref<BattleUnit>& me)
	{
		cs::List<Ranked> ranked;
		if (me != nullptr)
			for (const auto& o : battle.Units)
				if (Fighting(battle, me, o) && o->Target == me) ranked.Add(Ranked{ o, (o->Pos - me->Pos).LengthSquared() });
		ranked.Sort([](const Ranked& a, const Ranked& b) { return cs::Compare(a.distSq, b.distSq); });
		cs::List<cs::Ref<BattleUnit>> out;
		for (const auto& r : ranked) out.Add(r.unit);
		return out;
	}

	cs::Ref<BattleUnit> Attackers::Next(const Battle& battle, const cs::Ref<BattleUnit>& me, const cs::Ref<BattleUnit>& current, bool& engaging)
	{
		engaging = false;
		if (me == nullptr) return nullptr;
		auto engaged = Of(battle, me);
		if (engaged.Count() > 0)
		{
			engaging = true;
			int i = engaged.FindIndex([&](const cs::Ref<BattleUnit>& u) { return u == current; });
			return engaged[i < 0 ? 0 : (i + 1) % engaged.Count()];
		}
		cs::Ref<BattleUnit> nearest;
		double best = 0;
		for (const auto& o : battle.Units)
		{
			if (!Fighting(battle, me, o)) continue;
			double d = (o->Pos - me->Pos).LengthSquared();
			if (nearest == nullptr || d < best)
			{
				nearest = o;
				best = d;
			}
		}
		return nearest;
	}
}
