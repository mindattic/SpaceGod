// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/RearLine.cs (see docs/MIGRATION.md).

#include "Combat/RearLine.h"

#include "DVec3.h"
#include "Ships.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/CombatSpecs.h"
#include "Combat/SpinalMounts.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	cs::String RearLine::Km(double v) { return cs::Cat(cs::Fmt(v, "0"), " km"); }

	cs::String RearLine::Percent(double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }

	const cs::Ref<RangeSetting>& RearLine::Standoff()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("dreadnought.standoff", SettingGroups::Broadsides,
			"Dreadnought standoff", 0.85, 0.4, 1, 0.05,
			cs::String("How far into the reach of its shortest-ranged spinal mount a battleship holds off its mark: every spinal gun then ") +
			"bears, and lower brings it in closer to be surer of its shots.", cs::Func<cs::String, double>(Percent),
			"dreadnought battleship spinal standoff range snipe rear line");
		return s;
	}

	const cs::Ref<RangeSetting>& RearLine::GuardKm()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("dreadnought.guardKm", SettingGroups::Broadsides,
			"Dreadnought guard distance", 12, 0, 30, 1,
			"How near (km) a hostile hull may close on a battleship holding the rear before it backs away from it.", cs::Func<cs::String, double>(Km),
			"dreadnought battleship rear line guard distance back off withdraw");
		return s;
	}

	const cs::Ref<RangeSetting>& RearLine::RearBias()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("dreadnought.rearBias", SettingGroups::Broadsides,
			"Dreadnought holds the rear", 0.6, 0, 2, 0.05,
			cs::String("How strongly a battleship keeps its own side's line between itself and the enemy as it takes its firing station ") +
			"(0 lets it stand off on whatever side it happens to be).", cs::Func<cs::String, double>(Percent),
			"dreadnought battleship rear line station behind escorts");
		return s;
	}

	const cs::Ref<RangeSetting>& RearLine::CapitalBias()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("dreadnought.capitalBias", SettingGroups::Broadsides,
			"Dreadnought capital preference", 10, 0, 40, 1,
			"How strongly a battleship picks a capital ship over a smaller hull to snipe (as if it were that many km nearer).", cs::Func<cs::String, double>(Km),
			"dreadnought battleship spinal target capital preference");
		return s;
	}

	// ---------------------------------------------------------------- who flies it

	cs::HashSet<ShipClass>& RearLine::classes()
	{
		static cs::HashSet<ShipClass> s = cs::HashSet<ShipClass>{ ShipClass::Battleship };
		return s;
	}

	void RearLine::Register(ShipClass cls, bool flies)
	{
		if (flies) classes().Add(cls);
		else classes().Remove(cls);
	}

	bool RearLine::Doctrine(ShipClass cls) { return classes().Contains(cls); }

	bool RearLine::Flies(const cs::Ref<BattleUnit>& u)
	{
		return u != nullptr && u->Kind == UnitKind::Ship && (u->Ship != nullptr && u->Ship->Spec != nullptr) && !u->PlayerControlled() && Doctrine(u->Ship->Spec->Class)
			&& u->Combat != nullptr && SpinalMounts::Standing(u->Combat) > 0;
	}

	double RearLine::StandoffKm(const cs::Ref<BattleUnit>& u) { return SpinalMounts::ReachKm(u->Combat) * Standoff()->Value; }

	void RearLine::Steer(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double speed, DVec3& desired, DVec3& face)
	{
		auto t = u->Target;
		auto from = u->Pos - t->Pos;
		double dist = cs::Math::Max(from.Length(), 1e-6);
		auto outward = from / dist;

		// The rear of its own side: from the enemy's centre toward its own line's (or toward itself, alone).
		DVec3 friends = DVec3::Zero, foes = DVec3::Zero;
		int nf = 0, nh = 0;
		for (const auto& o : b->Units)
		{
			if (o == u || !o->Present() || o->Kind == UnitKind::Fighter) continue;
			if (o->Side == u->Side) { friends = friends + o->Pos; nf++; }
			else if (b->Hostile(u, o)) { foes = foes + o->Pos; nh++; }
		}
		auto dir = outward;
		if (nh > 0 && RearBias()->Value > 0)
		{
			auto rear = (nf > 0 ? friends / nf : u->Pos) - foes / nh;
			if (rear.LengthSquared() > 1e-9)
			{
				auto bent = outward + rear.Normalized() * RearBias()->Value;
				if (bent.LengthSquared() > 1e-9) dir = bent.Normalized();
			}
		}
		auto spot = t->Pos + dir * (StandoffKm(u) + t->Radius());
		desired = (spot - u->Pos) * 0.02;

		// Anything closing on it is backed away from, harder the nearer it is.
		double guard = GuardKm()->Value;
		if (guard > 0)
			for (const auto& o : b->Units)
			{
				if (!o->Present() || o->Kind == UnitKind::Fighter || !b->Hostile(u, o)) continue;
				auto away = u->Pos - o->Pos;
				double gap = away.Length() - o->Radius() - u->Radius();
				if (gap >= guard || away.LengthSquared() < 1e-12) continue;
				desired = desired + away.Normalized() * speed * (1 - cs::Math::Max(0.0, gap) / guard);
			}

		// Never so far back that it leaves the fight.
		if (u->Pos.Length() > Battle::EscapeKm * 0.7) desired = desired + -u->Pos.Normalized() * speed;

		face = t->Pos - u->Pos;
	}

	double RearLine::TargetBias(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& o)
	{
		if (!Flies(u) || o == nullptr || o->Kind == UnitKind::Fighter) return 0;
		return o->LengthKm >= 1 ? CapitalBias()->Value : 0;
	}
}
