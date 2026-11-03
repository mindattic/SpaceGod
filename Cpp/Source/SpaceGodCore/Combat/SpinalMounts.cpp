// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/SpinalMounts.cs (see docs/MIGRATION.md).

#include "Combat/SpinalMounts.h"

#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Facilities.h"
#include "Combat/FireCones.h"
#include "Combat/Outfitting.h"
#include "Combat/TachyonLances.h"
#include "Economy/Goods.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	/// <summary>A module built to a spinal mount's length: its name, and its bill and mass that many times over.</summary>
	class SpinalMounts::SpinalPart final : public IPartSpec
	{
		cs::Ref<ModuleSpec> module;
		double times;

	public:
		SpinalPart(ModuleKind m, const cs::String& name, double times)
		{
			module = ModuleSpec::Of(m);
			Name = name;
			this->times = times;
		}

		cs::String Name;
		cs::String PartName() const override { return Name; }
		const cs::List<std::pair<Good, double>>& PartBill() const override { return module->Bill; }
		double Tonnes(int i, double scale) override { return module->Tonnes(i, scale) * times; }
		double Mass(double scale) override { return module->Mass(scale) * times; }
	};

	void SpinalMounts::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		// The static fields, in textual order...
		Railgun();
		Coilgun();
		Plasma();
		RailgunRange();
		RailgunDamage();
		RailgunRecharge();
		CoilgunRange();
		CoilgunDamage();
		CoilgunRecharge();
		PlasmaRange();
		PlasmaDamage();
		PlasmaRecharge();
		Unpowered();
		kinds();
		Classes();
		// ...then the static constructor's body.
		// A railgun's rails and a plasma cannon's bottle are four times the turret's; the coilgun is only ever spinal, and a
		// tachyon lance is a spinal weapon already, so their own modules are what is built.
		Register(cs::New<Kind>(ModuleKind::Railgun, SubsystemKind::Railgun, [] { return Railgun(); }, cs::New<SpinalPart>(ModuleKind::Railgun, "Spinal railgun", 4)));
		Register(cs::New<Kind>(ModuleKind::Coilgun, SubsystemKind::Coilgun, [] { return Coilgun(); }, nullptr));
		Register(cs::New<Kind>(ModuleKind::TachyonLance, SubsystemKind::TachyonLance, [] { return TachyonLances::Spec(); }, nullptr));
		Register(cs::New<Kind>(ModuleKind::PlasmaCannon, SubsystemKind::PlasmaCannon, [] { return Plasma(); }, cs::New<SpinalPart>(ModuleKind::PlasmaCannon, "Plasma spinal cannon", 4)));
	}

	// ------------------------------------------------------------ the weapons (before the settings that tune them)

	const cs::Ref<WeaponSpec>& SpinalMounts::Railgun()
	{
		RunClassConstructor();
		static const cs::Ref<WeaponSpec> value = []
		{
			auto w = cs::New<WeaponSpec>();
			w->RangeKm = 50; w->Damage = 900; w->Cooldown = 30; w->Accuracy = 0.9; w->MuzzleSpeedKmS = 22;
			return w;
		}();
		return value;
	}

	const cs::Ref<WeaponSpec>& SpinalMounts::Coilgun()
	{
		RunClassConstructor();
		static const cs::Ref<WeaponSpec> value = []
		{
			auto w = cs::New<WeaponSpec>();
			w->RangeKm = 44; w->Damage = 520; w->Cooldown = 15; w->Accuracy = 0.85; w->MuzzleSpeedKmS = 14;
			return w;
		}();
		return value;
	}

	const cs::Ref<WeaponSpec>& SpinalMounts::Plasma()
	{
		RunClassConstructor();
		static const cs::Ref<WeaponSpec> value = []
		{
			auto w = cs::New<WeaponSpec>();
			w->RangeKm = 22; w->Damage = 1800; w->Cooldown = 40; w->Accuracy = 0.8; w->FalloffEdgeShare = 0.35;
			return w;
		}();
		return value;
	}

	// ------------------------------------------------------------ settings

	cs::String SpinalMounts::Km(double v) { return cs::Cat(cs::Fmt(v, "0"), " km"); }
	cs::String SpinalMounts::Hp(double v) { return cs::Cat(cs::Fmt(v, "0"), " hp"); }
	cs::String SpinalMounts::Secs(double v) { return cs::Cat(cs::Fmt(v, "0"), " s"); }
	cs::String SpinalMounts::Percent(double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }

	const cs::Ref<RangeSetting>& SpinalMounts::RailgunRange()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = []
		{
			auto s = cs::New<RangeSetting>("spinal.railgunRange", SettingGroups::Broadsides,
				"Spinal railgun range", 50, 25, 54, 1,
				"How far a battleship's spinal railgun reaches (a destroyer's turret railgun reaches 30 km).", &Km,
				"spinal railgun dreadnought battleship range sniper");
			s->Applying([](double v) { Railgun()->RangeKm = v; });
			return s;
		}();
		return value;
	}

	const cs::Ref<RangeSetting>& SpinalMounts::RailgunDamage()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = []
		{
			auto s = cs::New<RangeSetting>("spinal.railgunDamage", SettingGroups::Broadsides,
				"Spinal railgun damage", 900, 200, 2000, 25,
				"What one spinal railgun slug does to what it strikes.", &Hp,
				"spinal railgun dreadnought battleship damage slug");
			s->Applying([](double v) { Railgun()->Damage = v; });
			return s;
		}();
		return value;
	}

	const cs::Ref<RangeSetting>& SpinalMounts::RailgunRecharge()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = []
		{
			auto s = cs::New<RangeSetting>("spinal.railgunRecharge", SettingGroups::Broadsides,
				"Spinal railgun recharge", 30, 8, 120, 1,
				"Seconds a spinal railgun takes to charge its rails again after a shot.", &Secs,
				"spinal railgun dreadnought battleship recharge cooldown");
			s->Applying([](double v) { Railgun()->Cooldown = v; });
			return s;
		}();
		return value;
	}

	const cs::Ref<RangeSetting>& SpinalMounts::CoilgunRange()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = []
		{
			auto s = cs::New<RangeSetting>("spinal.coilgunRange", SettingGroups::Broadsides,
				"Spinal coilgun range", 44, 20, 54, 1,
				"How far a battleship's spinal coilgun reaches.", &Km,
				"spinal coilgun dreadnought battleship range sniper");
			s->Applying([](double v) { Coilgun()->RangeKm = v; });
			return s;
		}();
		return value;
	}

	const cs::Ref<RangeSetting>& SpinalMounts::CoilgunDamage()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = []
		{
			auto s = cs::New<RangeSetting>("spinal.coilgunDamage", SettingGroups::Broadsides,
				"Spinal coilgun damage", 520, 100, 1500, 20,
				"What one spinal coilgun slug does to what it strikes.", &Hp,
				"spinal coilgun dreadnought battleship damage slug");
			s->Applying([](double v) { Coilgun()->Damage = v; });
			return s;
		}();
		return value;
	}

	const cs::Ref<RangeSetting>& SpinalMounts::CoilgunRecharge()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = []
		{
			auto s = cs::New<RangeSetting>("spinal.coilgunRecharge", SettingGroups::Broadsides,
				"Spinal coilgun recharge", 15, 4, 60, 1,
				"Seconds a spinal coilgun takes to charge its coils again after a shot.", &Secs,
				"spinal coilgun dreadnought battleship recharge cooldown");
			s->Applying([](double v) { Coilgun()->Cooldown = v; });
			return s;
		}();
		return value;
	}

	const cs::Ref<RangeSetting>& SpinalMounts::PlasmaRange()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = []
		{
			auto s = cs::New<RangeSetting>("spinal.plasmaRange", SettingGroups::Broadsides,
				"Plasma spinal cannon range", 22, 8, 40, 1,
				"How far a plasma spinal cannon's bolt holds together.", &Km,
				"plasma spinal cannon dreadnought battleship range");
			s->Applying([](double v) { Plasma()->RangeKm = v; });
			return s;
		}();
		return value;
	}

	const cs::Ref<RangeSetting>& SpinalMounts::PlasmaDamage()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = []
		{
			auto s = cs::New<RangeSetting>("spinal.plasmaDamage", SettingGroups::Broadsides,
				"Plasma spinal cannon damage", 1800, 400, 4000, 50,
				"What a plasma spinal cannon's bolt does at point-blank range, before it bleeds off over distance.", &Hp,
				"plasma spinal cannon dreadnought battleship damage bolt");
			s->Applying([](double v) { Plasma()->Damage = v; });
			return s;
		}();
		return value;
	}

	const cs::Ref<RangeSetting>& SpinalMounts::PlasmaRecharge()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = []
		{
			auto s = cs::New<RangeSetting>("spinal.plasmaRecharge", SettingGroups::Broadsides,
				"Plasma spinal cannon recharge", 40, 10, 120, 1,
				"Seconds a plasma spinal cannon takes to fill its magnetic bottle again after a shot.", &Secs,
				"plasma spinal cannon dreadnought battleship recharge cooldown");
			s->Applying([](double v) { Plasma()->Cooldown = v; });
			return s;
		}();
		return value;
	}

	const cs::Ref<RangeSetting>& SpinalMounts::Unpowered()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("spinal.unpowered", SettingGroups::Broadsides,
			"Spinal mount without its plant", 0.25, 0, 1, 0.05,
			cs::Cat("How fast a spinal mount recharges, against its full rate, when the hull has fewer spinal power plants than spinal ",
				"mounts (100% lets it charge as fast without one)."), &Percent,
			"spinal mount power plant reactor recharge dreadnought battleship");
		return value;
	}

	// ------------------------------------------------------------ the mount

	SpinalMounts::Kind::Kind(ModuleKind module, SubsystemKind subsystem, cs::Func<cs::Ref<WeaponSpec>> weapon, cs::Ref<IPartSpec> part)
	{
		Module = module;
		Subsystem = subsystem;
		if (!weapon) throw cs::ArgumentNullException("weapon");
		Weapon = weapon;
		Part = part ? part : cs::Ref<IPartSpec>(ModuleSpec::Of(module));
	}

	cs::Dictionary<ModuleKind, cs::Ref<SpinalMounts::Kind>>& SpinalMounts::kinds()
	{
		RunClassConstructor();
		static cs::Dictionary<ModuleKind, cs::Ref<Kind>> value;
		return value;
	}

	cs::HashSet<ShipClass>& SpinalMounts::Classes()
	{
		RunClassConstructor();
		static cs::HashSet<ShipClass> value { ShipClass::Battleship };
		return value;
	}

	void SpinalMounts::Register(const cs::Ref<Kind>& kind)
	{
		if (kind == nullptr) throw cs::ArgumentNullException("kind");
		kinds()[kind->Module] = kind;
	}

	cs::Ref<SpinalMounts::Kind> SpinalMounts::Of(ModuleKind m)
	{
		cs::Ref<Kind> k;
		return kinds().TryGetValue(m, k) ? k : nullptr;
	}

	bool SpinalMounts::Takes(ModuleKind m) { return kinds().ContainsKey(m); }

	bool SpinalMounts::CanCarry(cs::Nullable<ShipClass> cls) { return cls.has_value() && Classes().Contains(*cls); }

	cs::Ref<IPartSpec> SpinalMounts::Part(ModuleKind m)
	{
		auto k = Of(m);
		auto part = k ? k->Part : cs::Ref<IPartSpec>();
		return part ? part : cs::Ref<IPartSpec>(ModuleSpec::Of(m));
	}

	cs::Ref<SubsystemDef> SpinalMounts::Mount(ModuleKind m, double x, double y, double z)
	{
		auto k = Of(m);
		if (k == nullptr) throw cs::ArgumentException(cs::Cat(m, " does not go in a spinal mount"));
		auto def = cs::New<SubsystemDef>(k->Subsystem, x, y, z, false, cs::Nullable<DVec3>(FireCones::Bow), cs::Nullable<double>(ConeHalfDeg));
		def->Spinal = true;
		def->Weapon = k->Weapon();
		def->MaxHp *= HpFactor;
		return def;
	}

	cs::Ref<SubsystemDef> SpinalMounts::Mount(ModuleKind m, const cs::Ref<SubsystemDef>& was) { return Mount(m, was->X, was->Y, was->Z); }

	// ------------------------------------------------------------ in battle

	bool SpinalMounts::IsSlug(SubsystemKind k) { return k == SubsystemKind::Railgun || k == SubsystemKind::LightRailgun || k == SubsystemKind::Coilgun; }

	bool SpinalMounts::HullLaid(const cs::Ref<SubsystemDef>& d)
	{
		return d != nullptr && (d->Spinal || d->Kind == SubsystemKind::Railgun || d->Kind == SubsystemKind::Coilgun || d->Kind == SubsystemKind::TachyonLance || (d->Weapon ? d->Weapon->Lance : false));
	}

	int SpinalMounts::Standing(const cs::Ref<ShipCombat>& c)
	{
		if (c == nullptr) return 0;
		int n = 0;
		for (const auto& s : c->Subsystems)
			if (s->Def->Spinal && !s->Destroyed()) n++;
		return n;
	}

	double SpinalMounts::ChargeRate(const cs::Ref<ShipCombat>& c, const cs::Ref<Subsystem>& s)
	{
		if (c == nullptr || s == nullptr || !s->Def->Spinal) return 1;
		int mounts = Standing(c);
		double plants = cs::Math::Max(0, c->Spec->SpinalPlants);
		if (mounts <= 0 || plants >= mounts) return 1;
		return cs::Math::Max(Unpowered()->Value, plants / mounts);
	}

	double SpinalMounts::ReachKm(const cs::Ref<ShipCombat>& c)
	{
		if (c == nullptr) return 0;
		double reach = cs::Double::MaxValue;
		for (const auto& s : c->Subsystems)
			if (s->Def->Spinal && !s->Destroyed() && s->Def->Weapon != nullptr) reach = cs::Math::Min(reach, s->Def->Weapon->RangeKm);
		return reach == cs::Double::MaxValue ? 0 : reach;
	}
}
