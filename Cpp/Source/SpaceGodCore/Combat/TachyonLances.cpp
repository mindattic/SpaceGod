// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/TachyonLances.cs (see docs/MIGRATION.md).

#include "Combat/TachyonLances.h"

#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Economy/Industry.h"
#include "Economy/Settlement.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	void TachyonLances::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		Range();
		Damage();
		Recharge();
		PowerDraw();
		YardLevel();
		CapitalClasses();
	}

	// ------------------------------------------------------------ settings

	const cs::Ref<RangeSetting>& TachyonLances::Range()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = []
		{
			auto s = cs::New<RangeSetting>("lance.range", SettingGroups::Broadsides,
				"Tachyon lance range", 60, 30, 120, 5,
				"How far a tachyon lance reaches, km. Its shot lands the instant it is fired, anywhere in that reach.",
				[](double v) { return cs::Cat(cs::Fmt(v, "0"), " km"); }, "tachyon lance spinal capital range weapon");
			s->Applying([](double v) { WeaponSpec::TachyonLance()->RangeKm = v; });
			return s;
		}();
		return value;
	}

	const cs::Ref<RangeSetting>& TachyonLances::Damage()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = []
		{
			auto s = cs::New<RangeSetting>("lance.damage", SettingGroups::Broadsides,
				"Tachyon lance damage", 650, 200, 1500, 50,
				"What one tachyon lance shot does to what it strikes, hit points (a railgun slug does 260, a capital torpedo 900).",
				[](double v) { return cs::Fmt(v, "0"); }, "tachyon lance spinal capital damage weapon");
			s->Applying([](double v) { WeaponSpec::TachyonLance()->Damage = v; });
			return s;
		}();
		return value;
	}

	const cs::Ref<RangeSetting>& TachyonLances::Recharge()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = []
		{
			auto s = cs::New<RangeSetting>("lance.recharge", SettingGroups::Broadsides,
				"Tachyon lance recharge", 60, 15, 180, 5,
				"Seconds a tachyon lance takes to charge again after a shot.",
				[](double v) { return cs::Cat(cs::Fmt(v, "0"), " s"); }, "tachyon lance spinal capital recharge cooldown power");
			s->Applying([](double v) { WeaponSpec::TachyonLance()->Cooldown = v; });
			return s;
		}();
		return value;
	}

	const cs::Ref<RangeSetting>& TachyonLances::PowerDraw()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("lance.powerDraw", SettingGroups::Broadsides,
			"Tachyon lance power draw", 0.5, 0, 1, 0.05,
			"How much of the hull's shield recovery a charging tachyon lance takes from the reactor (0 lets it charge for nothing).",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "tachyon lance power reactor shield regen capital");
		return value;
	}

	const cs::Ref<RangeSetting>& TachyonLances::YardLevel()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("lance.yardLevel", SettingGroups::Broadsides,
			"Tachyon lance works", 2, 0, 4, 0.25,
			"How great a shipyard must be to build a tachyon lance: only yards at least this size (Earth's, Ganymede's and Titan's are 2) can.",
			[](double v) { return cs::Fmt(v, "0.##"); }, "tachyon lance yard shipyard rare build");
		return value;
	}

	// ------------------------------------------------------------ rules

	cs::Ref<WeaponSpec> TachyonLances::Spec() { return WeaponSpec::TachyonLance(); }

	cs::HashSet<ShipClass>& TachyonLances::CapitalClasses()
	{
		RunClassConstructor();
		static cs::HashSet<ShipClass> value { ShipClass::Battleship, ShipClass::Carrier };
		return value;
	}

	bool TachyonLances::CanCarry(cs::Nullable<ShipClass> cls) { return cls.has_value() && CapitalClasses().Contains(*cls); }

	bool TachyonLances::YardBuilds(const cs::Ref<Settlement>& yard)
	{
		if (yard == nullptr || yard->Fallen || yard->Kind == SettlementKind::Haven) return false;
		if (yard->Owner == nullptr || yard->Owner->IsPirate) return false;
		auto works = yard->Shipyard();
		return works != nullptr && works->Level + 1e-9 >= YardLevel()->Value;
	}

	double TachyonLances::ShieldRegenFactor(const cs::Ref<ShipCombat>& c)
	{
		if (c == nullptr) return 1;
		for (const auto& s : c->Subsystems)
			if (s->Kind() == SubsystemKind::TachyonLance && !s->Destroyed() && s->Cooldown > 0) return 1 - PowerDraw()->Value;
		return 1;
	}
}
