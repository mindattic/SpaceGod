// US-T102: nanite repair systems, a hardpoint module that rebuilds a damaged hull over time.

#include "Combat/NaniteRepairs.h"

#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/ModularHull.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	void NaniteRepairs::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		Rate();
		HullCap();
	}

	const cs::Ref<RangeSetting>& NaniteRepairs::Rate()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("nanite.rate", SettingGroups::Broadsides,
			"Nanite repair rate", 1, 0, 3, 0.1,
			"How fast a nanite repair system rebuilds the hull it is fitted to in a fight (its points, its damaged sections and its standing hardpoints): 0 leaves the swarm inert, 1 is as designed.",
			[](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }, "nanite repair self repair regenerate hull section structure hardpoint module");
		return value;
	}

	const cs::Ref<RangeSetting>& NaniteRepairs::HullCap()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("nanite.hullCap", SettingGroups::Broadsides,
			"Nanites rebuild the hull to", 0.75, 0.2, 1, 0.05,
			"How whole a nanite repair system can rebuild a hull's points; the rest takes a yard.",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "nanite repair self repair regenerate hull cap");
		return value;
	}

	double NaniteRepairs::Capacity(const cs::Ref<ShipCombat>& c)
	{
		if (c == nullptr) return 0;
		double n = 0;
		for (const auto& s : c->Subsystems)
			if (s->Kind() == SubsystemKind::NaniteRepair && !s->Destroyed()) n += s->Health();
		return n;
	}

	bool NaniteRepairs::Carries(const cs::Ref<ShipCombat>& c)
	{
		if (c == nullptr) return false;
		for (const auto& s : c->Subsystems) if (s->Kind() == SubsystemKind::NaniteRepair) return true;
		return false;
	}

	double NaniteRepairs::Step(const cs::Ref<BattleUnit>& u, double dt)
	{
		if (u == nullptr || dt <= 0 || u->Dead || u->Captured || u->Stunned > 0) return 0;
		auto c = u->Combat;
		if (c == nullptr || !c->Alive()) return 0;
		double capacity = Capacity(c);
		if (capacity <= 0) return 0;
		return Rebuild(c, capacity, dt);
	}

	double NaniteRepairs::Rebuild(const cs::Ref<ShipCombat>& c, double capacity, double dt)
	{
		double rate = Rate()->Value;
		if (c == nullptr || !c->Alive() || capacity <= 0 || rate <= 0 || dt <= 0) return 0;
		double share = SharePerSecond * rate * capacity * dt;

		// The hull's points, up to the cap.
		double rebuilt = 0;
		double cap = c->Spec->Hull * HullCap()->Value;
		if (c->Hull < cap)
		{
			rebuilt = cs::Math::Min(cap - c->Hull, c->Spec->Hull * share);
			c->Hull += rebuilt;
		}

		// Every damaged section still on the hull, and every joint still holding. A hull nothing has struck yet is whole,
		// so its structure is not laid out just to be looked at.
		if (c->HasStructure())
		{
			auto h = c->Structure();
			for (const auto& m : h->Modules)
				if (!m->Detached && !m->Shattered() && m->Structure < m->MaxStructure)
					m->Structure = cs::Math::Min(m->MaxStructure, m->Structure + m->MaxStructure * share);
			for (const auto& j : h->Joints)
				if (!j->Broken() && j->Strength < j->MaxStrength)
					j->Strength = cs::Math::Min(j->MaxStrength, j->Strength + j->MaxStrength * share);
		}

		// The hardpoints still standing, the worst first: the swarm's budget is the share of one mount's points for every
		// whole system, spent in hull order on ties so every peer mends the same mount.
		double budget = share * cs::Math::Max(1.0, capacity);
		for (int pass = 0; pass < 8 && budget > 1e-9; pass++)
		{
			cs::Ref<Subsystem> worst = nullptr;
			double worstHealth = cs::Double::MaxValue;
			for (const auto& s : c->Subsystems)
			{
				if (s->Destroyed() || s->Hp >= s->Def->MaxHp * MountCap) continue;
				double health = s->Health();
				if (health < worstHealth)
				{
					worstHealth = health;
					worst = s;
				}
			}
			if (worst == nullptr) break;
			double room = worst->Def->MaxHp * MountCap - worst->Hp;
			double fix = cs::Math::Min(room, worst->Def->MaxHp * budget);
			worst->Hp += fix;
			budget -= fix / worst->Def->MaxHp;
		}
		return rebuilt;
	}
}
