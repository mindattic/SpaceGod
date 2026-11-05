// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/ScreenWeapons.cs (see docs/MIGRATION.md).

#include "Combat/ScreenWeapons.h"

#include "Combat/Arsenal.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	void ScreenWeapons::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		AntimatterYield();
		AntimatterToughness();
		FlakBurst();
		FlakDamage();
		InterceptorReliability();
		ScreenStation();
	}

	// ------------------------------------------------------------ settings

	const cs::Ref<RangeSetting>& ScreenWeapons::AntimatterYield()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("weapons.antimatterYield", SettingGroups::Weapons,
			"Antimatter torpedo yield", 1, 0.25, 3, 0.05,
			"How hard an antimatter torpedo hits, against its design (1): its warhead is the hardest single hit in the game.",
			[](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }, "antimatter photon torpedo warhead damage frigate capital");
		return value;
	}

	const cs::Ref<RangeSetting>& ScreenWeapons::AntimatterToughness()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("weapons.antimatterToughness", SettingGroups::Weapons,
			"Antimatter torpedo toughness", 60, 10, 200, 5,
			"The damage a screen must do to an antimatter torpedo to stop it (a missile takes 12, a tube's torpedo 40).",
			[](double v) { return cs::Cat(cs::Fmt(v, "0"), " hp"); }, "antimatter photon torpedo intercept point defence flak armour");
		return value;
	}

	const cs::Ref<RangeSetting>& ScreenWeapons::FlakBurst()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("weapons.flakBurst", SettingGroups::Weapons,
			"Flak burst size", 1, 0.25, 2.5, 0.05,
			"How wide a flak shell's burst is, against its design (1, 0.4 km): every hostile missile and fighter inside it is hurt, not only the one it was laid on.",
			[](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }, "flak burst radius area screen destroyer missile swarm fighter");
		return value;
	}

	const cs::Ref<RangeSetting>& ScreenWeapons::FlakDamage()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("weapons.flakDamage", SettingGroups::Weapons,
			"Flak damage", 1, 0, 3, 0.1,
			"How hard a flak burst hits what is inside it, against its design (1); 0 makes flak harmless.",
			[](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }, "flak damage screen destroyer");
		return value;
	}

	const cs::Ref<RangeSetting>& ScreenWeapons::InterceptorReliability()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("weapons.interceptorReliability", SettingGroups::Weapons,
			"Interceptor reliability", 0.85, 0, 1, 0.05,
			"The chance an interceptor missile that reaches the round it is hunting kills it.",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "interceptor missile screen destroyer kill chance");
		return value;
	}

	const cs::Ref<RangeSetting>& ScreenWeapons::ScreenStation()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("weapons.screenStation", SettingGroups::Weapons,
			"Screen station-keeping", 0.6, 0, 1, 0.05,
			"How closely a destroyer carrying flak or interceptors keeps between the capital it screens and the enemy, rather than fighting its own fight (0 lets it roam).",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "screen escort destroyer umbrella station capital");
		return value;
	}

	// ------------------------------------------------------------ rules

	bool ScreenWeapons::IsScreenMount(SubsystemKind k) { return k == SubsystemKind::FlakCannon || k == SubsystemKind::InterceptorMissile; }

	bool ScreenWeapons::Screens(const cs::Ref<ShipCombat>& c)
	{
		for (const auto& s : c->Subsystems)
			if (IsScreenMount(s->Kind()) && !s->Destroyed() && (s->Kind() != SubsystemKind::InterceptorMissile || s->Ammo > 0)) return true;
		return false;
	}

	MissileKind ScreenWeapons::KindOf(SubsystemKind launcher, const cs::Ref<WeaponSpec>& w)
	{
		if (w->EmpSeconds > 0) return MissileKind::Emp;
		switch (launcher)
		{
			case SubsystemKind::AntimatterTorpedo: return MissileKind::Antimatter;
			case SubsystemKind::InterceptorMissile: return MissileKind::Interceptor;
			// The full arsenal (US-T101).
			case SubsystemKind::HiveLauncher: return MissileKind::Hive;
			case SubsystemKind::RocketPod: return MissileKind::Rocket;
			case SubsystemKind::Torpedo:
			case SubsystemKind::TorpedoTube: return MissileKind::Torpedo;
			default: return MissileKind::Missile;
		}
	}

	double ScreenWeapons::RoundHp(SubsystemKind launcher)
	{
		switch (launcher)
		{
			case SubsystemKind::AntimatterTorpedo: return AntimatterToughness()->Value;
			case SubsystemKind::TorpedoTube: return 40;
			case SubsystemKind::Torpedo: return 25;
			case SubsystemKind::InterceptorMissile: return InterceptorHp;
			// The full arsenal (US-T101).
			case SubsystemKind::HiveLauncher: return Arsenal::HiveHp;
			case SubsystemKind::RocketPod: return Arsenal::RocketHp;
			default: return 12;
		}
	}

	double ScreenWeapons::Warhead(SubsystemKind launcher, const cs::Ref<WeaponSpec>& w)
	{
		return launcher == SubsystemKind::AntimatterTorpedo ? w->Damage * AntimatterYield()->Value : w->Damage;
	}

	bool ScreenWeapons::IsThreat(const cs::Ref<Missile>& m) { return !m->Gone && m->Kind != MissileKind::Interceptor; }

	double ScreenWeapons::Threat(const cs::Ref<Missile>& m)
	{
		double damage = cs::Math::Max(m->Damage, 1.0);
		// A Breacher round tumbling adrift threatens nobody (U.SPC-T519): the last thing a screen spends itself on.
		if (m->Adrift) return 1e-3;
		auto t = m->Target;
		if (t == nullptr) return damage;
		double capital = t->Kind != UnitKind::Fighter && t->LengthKm >= 1 ? 1.5 : 1;
		double toGo = cs::Math::Max(0.0, (t->Pos - m->Pos).Length() - t->Radius());
		return damage * capital / (1 + toGo);
	}

	cs::Ref<BattleUnit> ScreenWeapons::WardOf(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		auto leader = u->Leader;
		if (leader != nullptr && leader->Present() && !leader->Fleeing && leader->Combat->Spec->Hull > u->Combat->Spec->Hull) return leader;
		cs::Ref<BattleUnit> best = nullptr;
		for (const auto& o : b->Units)
		{
			if (o == u || o->Side != u->Side || !o->Present() || o->Kind == UnitKind::Fighter || o->Captured || o->Fleeing || o->Surrendering) continue;
			if (o->Combat->Spec->Hull < u->Combat->Spec->Hull * 1.5 || (o->Pos - u->Pos).Length() > WardSearchKm) continue;
			if (best == nullptr || o->Combat->Spec->Hull > best->Combat->Spec->Hull) best = o;
		}
		return best;
	}

	DVec3 ScreenWeapons::StationOn(const cs::Ref<BattleUnit>& ward, DVec3 threat)
	{
		auto dir = threat - ward->Pos;
		dir = dir.LengthSquared() > 1e-9 ? dir.Normalized() : ward->Forward;
		return ward->Pos + dir * (ward->Radius() + StandoffKm);
	}
}
