// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/EnergyWeapons.cs (see docs/MIGRATION.md).

#include "Combat/EnergyWeapons.h"

#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Outfitting.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	const char* ToString(PhaserTuning v)
	{
		switch (v)
		{
			case PhaserTuning::Lance: return "Lance";
			case PhaserTuning::Sweep: return "Sweep";
		}
		return "";
	}

	// ------------------------------------------------------------ settings

	cs::String EnergyWeapons::Km(double v) { return cs::Cat(cs::Fmt(v, "0.#"), " km"); }
	cs::String EnergyWeapons::Hp(double v) { return cs::Cat(cs::Fmt(v, "0"), " hp"); }

	void EnergyWeapons::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		// The static fields, in textual order...
		LanceDamage();
		LanceRange();
		SweepDamage();
		SweepRange();
		AiTunesPhasers();
		PlasmaDamage();
		PlasmaRange();
		PlasmaEdgeShare();
		PhaserLance();
		PhaserSweep();
		PlasmaCannon();
		Roles();
		// ...then the static constructor's body.
		Apply();
		for (const auto& s : cs::List<cs::Ref<GameSetting>> { LanceDamage(), LanceRange(), SweepDamage(), SweepRange(), PlasmaDamage(), PlasmaRange(), PlasmaEdgeShare() })
			s->Changed += [](cs::Ref<GameSetting>) { Apply(); };
	}

	const cs::Ref<RangeSetting>& EnergyWeapons::LanceDamage()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("phaser.lanceDamage", SettingGroups::Weapons,
			"Phaser lance damage", 100, 20, 300, 5,
			"What one shot of a phaser array tuned to a narrow lance does to a hull.", &Hp, "phaser beam lance energy weapon damage");
		return value;
	}

	const cs::Ref<RangeSetting>& EnergyWeapons::LanceRange()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("phaser.lanceRange", SettingGroups::Weapons,
			"Phaser lance range", 20, 8, 40, 1,
			"How far a phaser array tuned to a narrow lance reaches.", &Km, "phaser beam lance energy weapon range");
		return value;
	}

	const cs::Ref<RangeSetting>& EnergyWeapons::SweepDamage()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("phaser.sweepDamage", SettingGroups::Weapons,
			"Phaser sweep damage", 16, 2, 60, 1,
			"What each pulse of a phaser array tuned to a wide sweep does: low, but fast enough to cut down fighters and missiles.", &Hp,
			"phaser beam sweep point defence fighter missile damage");
		return value;
	}

	const cs::Ref<RangeSetting>& EnergyWeapons::SweepRange()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("phaser.sweepRange", SettingGroups::Weapons,
			"Phaser sweep range", 6, 2, 15, 0.5,
			"How far a phaser array tuned to a wide sweep reaches.", &Km, "phaser beam sweep point defence range");
		return value;
	}

	const cs::Ref<ToggleSetting>& EnergyWeapons::AiTunesPhasers()
	{
		RunClassConstructor();
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("phaser.aiTunes", SettingGroups::Weapons,
			"AI captains tune phasers", true,
			"AI captains sweep their phaser arrays wide when fighters or missiles close on them and lance hulls otherwise; off, they keep the lance.",
			"phaser beam tune sweep lance ai doctrine");
		return value;
	}

	const cs::Ref<RangeSetting>& EnergyWeapons::PlasmaDamage()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("plasma.damage", SettingGroups::Weapons,
			"Plasma cannon damage", 480, 100, 1500, 10,
			"What a plasma bolt does at point-blank range, before it bleeds off over distance.", &Hp, "plasma cannon bolt damage brawler");
		return value;
	}

	const cs::Ref<RangeSetting>& EnergyWeapons::PlasmaRange()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("plasma.range", SettingGroups::Weapons,
			"Plasma cannon range", 9, 3, 20, 0.5,
			"How far a plasma bolt holds together.", &Km, "plasma cannon bolt range brawler");
		return value;
	}

	const cs::Ref<RangeSetting>& EnergyWeapons::PlasmaEdgeShare()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("plasma.edgeShare", SettingGroups::Weapons,
			"Plasma damage at full range", 0.2, 0, 1, 0.05,
			"The share of a plasma bolt's damage left when it lands at the very edge of its range; it falls off evenly in between (100% turns the bleed off).",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "plasma cannon falloff bleed distance");
		return value;
	}

	// ------------------------------------------------------------ the weapons

	const cs::Ref<WeaponSpec>& EnergyWeapons::PhaserLance()
	{
		RunClassConstructor();
		static const cs::Ref<WeaponSpec> value = []
		{
			auto w = cs::New<WeaponSpec>();
			w->Cooldown = 5;
			w->Accuracy = 0.78;
			return w;
		}();
		return value;
	}

	const cs::Ref<WeaponSpec>& EnergyWeapons::PhaserSweep()
	{
		RunClassConstructor();
		static const cs::Ref<WeaponSpec> value = []
		{
			auto w = cs::New<WeaponSpec>();
			w->Cooldown = 0.5;
			w->Accuracy = 0.6;
			w->AntiSmall = true;
			return w;
		}();
		return value;
	}

	const cs::Ref<WeaponSpec>& EnergyWeapons::PlasmaCannon()
	{
		RunClassConstructor();
		static const cs::Ref<WeaponSpec> value = []
		{
			auto w = cs::New<WeaponSpec>();
			w->Cooldown = 10;
			w->Accuracy = 0.7;
			return w;
		}();
		return value;
	}

	void EnergyWeapons::Apply()
	{
		PhaserLance()->Damage = LanceDamage()->Value;
		PhaserLance()->RangeKm = LanceRange()->Value;
		PhaserSweep()->Damage = SweepDamage()->Value;
		PhaserSweep()->RangeKm = SweepRange()->Value;
		PlasmaCannon()->Damage = PlasmaDamage()->Value;
		PlasmaCannon()->RangeKm = PlasmaRange()->Value;
		PlasmaCannon()->FalloffEdgeShare = PlasmaEdgeShare()->Value;
	}

	// ------------------------------------------------------------ tuning

	cs::Ref<WeaponSpec> EnergyWeapons::Effective(const cs::Ref<Subsystem>& s, const cs::Ref<ShipCombat>& c)
	{
		return s->Kind() == SubsystemKind::PhaserArray && c != nullptr && c->PhaserTuning == PhaserTuning::Sweep ? PhaserSweep() : s->Def->Weapon;
	}

	bool EnergyWeapons::HasPhasers(const cs::Ref<ShipCombat>& c)
	{
		if (c == nullptr) return false;
		for (const auto& s : c->Subsystems)
			if (s->Kind() == SubsystemKind::PhaserArray && !s->Destroyed()) return true;
		return false;
	}

	PhaserTuning EnergyWeapons::Retune(const cs::Ref<ShipCombat>& c)
	{
		if (c == nullptr) return PhaserTuning::Lance;
		c->PhaserTuning = c->PhaserTuning == PhaserTuning::Lance ? PhaserTuning::Sweep : PhaserTuning::Lance;
		return c->PhaserTuning;
	}

	cs::String EnergyWeapons::Label(PhaserTuning t) { return t == PhaserTuning::Sweep ? "wide sweep" : "narrow lance"; }

	PhaserTuning EnergyWeapons::Choose(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		double reach = PhaserSweep()->RangeKm * 1.5, reach2 = reach * reach;
		double lance = PhaserLance()->RangeKm;
		int small = 0;
		bool hullInReach = false;
		for (const auto& o : b->Units)
		{
			if (o == u || !b->Hostile(u, o) || o->Surrendering || o->Captured) continue;
			double d = (o->Pos - u->Pos).Length();
			if (o->Kind == UnitKind::Fighter) { if (d * d <= reach2) small++; }
			else if (d - o->Radius() <= lance) hullInReach = true;
		}
		for (const auto& m : b->Missiles)
			if (!m->Gone && m->Side != u->Side && (m->Pos - u->Pos).LengthSquared() <= reach2) small++;
		return small >= SweepThreats || (small > 0 && !hullInReach) ? PhaserTuning::Sweep : PhaserTuning::Lance;
	}

	void EnergyWeapons::Step(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		if (u->PlayerControlled() || !AiTunesPhasers()->Value || !HasPhasers(u->Combat)) return;
		u->Combat->PhaserTuning = Choose(b, u);
	}

	cs::Nullable<FxKind> EnergyWeapons::FxOf(SubsystemKind k, bool hit)
	{
		switch (k)
		{
			case SubsystemKind::PhaserArray: return hit ? FxKind::Phaser : FxKind::PhaserMiss;
			case SubsystemKind::PlasmaCannon: return hit ? FxKind::Plasma : FxKind::PlasmaMiss;
			default: return std::nullopt;
		}
	}

	// ------------------------------------------------------------ fitting by role

	cs::Dictionary<ModuleKind, cs::HashSet<ShipClass>>& EnergyWeapons::Roles()
	{
		RunClassConstructor();
		static cs::Dictionary<ModuleKind, cs::HashSet<ShipClass>> value = []
		{
			cs::Dictionary<ModuleKind, cs::HashSet<ShipClass>> d;
			d[ModuleKind::PhaserArray] = cs::HashSet<ShipClass> { ShipClass::Destroyer, ShipClass::Cruiser };
			d[ModuleKind::PlasmaCannon] = cs::HashSet<ShipClass> { ShipClass::Battleship, ShipClass::GateBreaker };
			return d;
		}();
		return value;
	}

	void EnergyWeapons::Allow(ModuleKind module, ShipClass cls)
	{
		auto& roles = Roles();
		if (!roles.ContainsKey(module)) return;
		auto& set = roles.At(module);
		set.Add(cls);
	}

	bool EnergyWeapons::RoleAllows(ModuleKind module, cs::Nullable<ShipClass> cls)
	{
		auto& roles = Roles();
		return !roles.ContainsKey(module) || (cls.has_value() && roles.At(module).Contains(*cls));
	}
}
