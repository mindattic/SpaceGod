// Weapon systems (US-T189): see WeaponSystems.h.

#include "Combat/WeaponSystems.h"

#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/EnergyWeapons.h"
#include "Combat/Outfitting.h"
#include "Combat/ShipCatalog.h"

namespace SpaceGod::Core
{
	const char* ToString(WeaponFamily v)
	{
		switch (v)
		{
			case WeaponFamily::None: return "None";
			case WeaponFamily::Laser: return "Laser";
			case WeaponFamily::Plasma: return "Plasma";
			case WeaponFamily::Missile: return "Missile";
			case WeaponFamily::Torpedo: return "Torpedo";
			case WeaponFamily::Railgun: return "Railgun";
			case WeaponFamily::Flak: return "Flak";
			case WeaponFamily::Kinetic: return "Kinetic";
			case WeaponFamily::Lance: return "Lance";
		}
		return "?";
	}

	const char* ToString(ProjectileLook v)
	{
		switch (v)
		{
			case ProjectileLook::None: return "None";
			case ProjectileLook::Beam: return "Beam";
			case ProjectileLook::Bolt: return "Bolt";
			case ProjectileLook::Trail: return "Trail";
			case ProjectileLook::Tracer: return "Tracer";
			case ProjectileLook::Slug: return "Slug";
			case ProjectileLook::Burst: return "Burst";
			case ProjectileLook::Lance: return "Lance";
		}
		return "?";
	}

	const char* ToString(WeaponCueEvent v)
	{
		switch (v)
		{
			case WeaponCueEvent::Fire: return "Fire";
			case WeaponCueEvent::Impact: return "Impact";
			case WeaponCueEvent::ShieldImpact: return "ShieldImpact";
			case WeaponCueEvent::Intercepted: return "Intercepted";
		}
		return "?";
	}

	// ---------------------------------------------------------------- families

	WeaponFamily WeaponSystems::FamilyOf(SubsystemKind k)
	{
		switch (k)
		{
			case SubsystemKind::LaserTurret:
			case SubsystemKind::HeavyLaser:
			case SubsystemKind::PointDefense:
			case SubsystemKind::PhaserArray: return WeaponFamily::Laser;
			case SubsystemKind::PlasmaCannon: return WeaponFamily::Plasma;
			case SubsystemKind::MissileLauncher:
			case SubsystemKind::InterceptorMissile:
			case SubsystemKind::HiveLauncher:
			case SubsystemKind::RocketPod: return WeaponFamily::Missile;
			case SubsystemKind::Torpedo:
			case SubsystemKind::TorpedoTube:
			case SubsystemKind::AntimatterTorpedo: return WeaponFamily::Torpedo;
			case SubsystemKind::Railgun:
			case SubsystemKind::LightRailgun:
			case SubsystemKind::Coilgun: return WeaponFamily::Railgun;
			case SubsystemKind::FlakCannon: return WeaponFamily::Flak;
			case SubsystemKind::FighterGuns:
			case SubsystemKind::Autocannon:
			case SubsystemKind::AutoTurret:
			case SubsystemKind::VulcanCannon: return WeaponFamily::Kinetic;
			case SubsystemKind::TachyonLance: return WeaponFamily::Lance;
			default: return WeaponFamily::None;
		}
	}

	WeaponFamily WeaponSystems::FamilyOfModule(ModuleKind m)
	{
		const auto& spec = ModuleSpec::Of(m);
		// A facility (plate, racks, a scanner) or a shield or drive mount is no weapon.
		if (spec == nullptr || !spec->Mount.has_value()) return WeaponFamily::None;
		auto mount = *spec->Mount;
		if (mount == MountType::Shield || mount == MountType::Engine) return WeaponFamily::None;
		return FamilyOf(ModuleSpec::SubsystemOf(m));
	}

	WeaponFamily WeaponSystems::FamilyOfRound(MissileKind k)
	{
		switch (k)
		{
			case MissileKind::Torpedo:
			case MissileKind::Antimatter: return WeaponFamily::Torpedo;
			case MissileKind::Missile:
			case MissileKind::Interceptor:
			case MissileKind::Rocket:
			case MissileKind::Hive:
			case MissileKind::HiveMicro: return WeaponFamily::Missile;
			default: return WeaponFamily::None; // a pod, a Breacher round, an EMP round
		}
	}

	WeaponFamily WeaponSystems::FamilyOfRound(const KineticRound& r) { return r.Railgun ? WeaponFamily::Railgun : WeaponFamily::Kinetic; }

	const WeaponSystemInfo& WeaponSystems::Info(WeaponFamily f)
	{
		// The profiles: a laser strips a shield a third again faster than it burns bare hull; plasma splashes off a field
		// but melts what is under it; a torpedo's heavy warhead and a railgun's slug go through plate. Missiles hit about
		// evenly, a touch harder on hull; flak is shrapnel, poor against any field.
		static const WeaponSystemInfo table[] =
		{
			{ WeaponFamily::None, "Weapon", { 1, 1, 0 }, ProjectileLook::None, false, false, false, "" },
			{ WeaponFamily::Laser, "Laser", { 1.35, 0.8, 0 }, ProjectileLook::Beam, true, false, false,
				"Hitscan beams: strip shields fast, burn bare hull slowly." },
			{ WeaponFamily::Plasma, "Plasma", { 0.65, 1.4, 0.1 }, ProjectileLook::Bolt, false, false, false,
				"Slow glowing bolts that bleed off with range: weak on shields, devastating on hull up close." },
			{ WeaponFamily::Missile, "Missile", { 1.0, 1.1, 0 }, ProjectileLook::Trail, false, true, true,
				"Guided warheads with a long reach; flak, interceptors and point defence can shoot them down." },
			{ WeaponFamily::Torpedo, "Torpedo", { 0.75, 1.35, 0.3 }, ProjectileLook::Trail, false, true, true,
				"Slow, heavy guided warheads: the hardest hit on a hull, if the screen lets them through." },
			{ WeaponFamily::Railgun, "Railgun", { 0.7, 1.15, 0.6 }, ProjectileLook::Slug, false, false, false,
				"Fast kinetic slugs at long range that punch through armour; a shield blunts them." },
			{ WeaponFamily::Flak, "Flak", { 0.6, 1.0, 0 }, ProjectileLook::Burst, false, false, false,
				"Area bursts that shred every fighter and missile caught in them." },
			{ WeaponFamily::Kinetic, "Kinetic", { 0.85, 1.0, 0.15 }, ProjectileLook::Tracer, false, false, false,
				"Rapid tracer fire: autocannons, vulcans, fighter guns and auto turrets." },
			{ WeaponFamily::Lance, "Tachyon lance", { 1.15, 1.0, 0 }, ProjectileLook::Lance, true, false, false,
				"Lands the instant it fires at the longest reach of any gun, on a long recharge." },
		};
		int i = static_cast<int>(f);
		return i >= 0 && i < static_cast<int>(sizeof(table) / sizeof(table[0])) ? table[i] : table[0];
	}

	const cs::List<WeaponFamily>& WeaponSystems::Families()
	{
		static const cs::List<WeaponFamily> value = cs::List<WeaponFamily> { WeaponFamily::Laser, WeaponFamily::Plasma, WeaponFamily::Missile,
			WeaponFamily::Torpedo, WeaponFamily::Railgun, WeaponFamily::Flak, WeaponFamily::Kinetic, WeaponFamily::Lance };
		return value;
	}

	// ---------------------------------------------------------------- damage

	double WeaponSystems::ThroughShield(WeaponFamily f, double damage, double shield, double& soaked)
	{
		double vs = ProfileOf(f).VsShield;
		double against = damage * vs;
		soaked = cs::Math::Min(shield, against);
		// The neutral profile soaks point for point, exactly as the battle did before weapon systems.
		if (vs == 1 || against <= 0) return damage - soaked;
		return damage * (1 - soaked / against);
	}

	double WeaponSystems::VsHull(WeaponFamily f, double damage) { return damage * ProfileOf(f).VsHull; }

	double WeaponSystems::ArmourFaced(WeaponFamily f, double armour) { return armour * (1 - ProfileOf(f).ArmourPierce); }

	// ---------------------------------------------------------------- sound

	std::function<void(const WeaponSoundCue&)>& WeaponSystems::SoundHook()
	{
		static std::function<void(const WeaponSoundCue&)> hook;
		return hook;
	}

	const char* WeaponSystems::CueName(WeaponFamily f, WeaponCueEvent e)
	{
		static const char* const names[][4] =
		{
			{ "weapon.fire", "weapon.impact", "weapon.shield", "weapon.intercepted" },
			{ "laser.fire", "laser.impact", "laser.shield", "laser.intercepted" },
			{ "plasma.fire", "plasma.impact", "plasma.shield", "plasma.intercepted" },
			{ "missile.fire", "missile.impact", "missile.shield", "missile.intercepted" },
			{ "torpedo.fire", "torpedo.impact", "torpedo.shield", "torpedo.intercepted" },
			{ "railgun.fire", "railgun.impact", "railgun.shield", "railgun.intercepted" },
			{ "flak.fire", "flak.impact", "flak.shield", "flak.intercepted" },
			{ "kinetic.fire", "kinetic.impact", "kinetic.shield", "kinetic.intercepted" },
			{ "lance.fire", "lance.impact", "lance.shield", "lance.intercepted" },
		};
		int i = static_cast<int>(f), j = static_cast<int>(e);
		if (i < 0 || i >= static_cast<int>(sizeof(names) / sizeof(names[0]))) i = 0;
		if (j < 0 || j > 3) j = 0;
		return names[i][j];
	}

	void WeaponSystems::Cue(WeaponFamily f, WeaponCueEvent e, DVec3 at, double size, int side, int battleId)
	{
		const auto& hook = SoundHook();
		if (!hook) return;
		WeaponSoundCue cue;
		cue.Family = f;
		cue.Event = e;
		cue.Name = CueName(f, e);
		cue.At = at;
		cue.Size = size;
		cue.Side = side;
		cue.BattleId = battleId;
		hook(cue);
	}

	WeaponFamily WeaponSystems::FamilyOfFx(FxKind k)
	{
		switch (k)
		{
			case FxKind::Beam:
			case FxKind::BeamMiss:
			case FxKind::PointDefense:
			case FxKind::Phaser:
			case FxKind::PhaserMiss: return WeaponFamily::Laser;
			case FxKind::Plasma:
			case FxKind::PlasmaMiss: return WeaponFamily::Plasma;
			case FxKind::Railgun:
			case FxKind::RailgunMiss: return WeaponFamily::Railgun;
			case FxKind::Guns:
			case FxKind::Vulcan: return WeaponFamily::Kinetic;
			case FxKind::FlakBurst: return WeaponFamily::Flak;
			case FxKind::MissileLaunch: return WeaponFamily::Missile;
			case FxKind::TachyonLance:
			case FxKind::TachyonLanceMiss: return WeaponFamily::Lance;
			default: return WeaponFamily::None;
		}
	}

	void WeaponSystems::CueFx(const CombatFx& fx, int battleId)
	{
		if (!SoundHook()) return;
		// A round shot down (a screen's kill: the small pops are rounds fizzling out at the end of their reach).
		if (fx.Kind == FxKind::MissileKill)
		{
			if (fx.Size >= 0.06) Cue(WeaponFamily::None, WeaponCueEvent::Intercepted, fx.A, fx.Size, fx.Side, battleId);
			return;
		}
		auto family = FamilyOfFx(fx.Kind);
		if (family == WeaponFamily::None) return;
		// A launch names its launcher, so a torpedo tube sounds as one (a beam's Sub is the hardpoint it struck, not its own).
		if (fx.Kind == FxKind::MissileLaunch && fx.Unit != nullptr && fx.Unit->Combat != nullptr && fx.Sub >= 0 && fx.Sub < fx.Unit->Combat->Subsystems.Count())
		{
			auto launcher = FamilyOf(fx.Unit->Combat->Subsystems[fx.Sub]->Kind());
			if (launcher != WeaponFamily::None) family = launcher;
		}
		Cue(family, WeaponCueEvent::Fire, fx.A, 0, fx.Side, battleId);
	}

	// ---------------------------------------------------------------- fitting through the slot model

	cs::List<ModuleKind> WeaponSystems::Options(const EquipmentSlot& slot, cs::Nullable<ShipClass> cls)
	{
		cs::List<ModuleKind> options;
		if (slot.Category != SlotCategory::Weapon) return options;
		for (const auto& m : ModuleSpec::Catalogue())
		{
			if (FamilyOfModule(m->Kind) == WeaponFamily::None || !slot.Accepts(m->Kind) || !EnergyWeapons::RoleAllows(m->Kind, cls)) continue;
			options.Add(m->Kind);
		}
		return options;
	}

	bool WeaponSystems::CanField(const cs::Ref<ShipCatalogEntry>& e, WeaponFamily f)
	{
		if (e == nullptr || f == WeaponFamily::None) return false;
		for (const auto& slot : e->Slots)
			for (auto m : Options(slot, e->Class))
				if (FamilyOfModule(m) == f) return true;
		return false;
	}

	cs::Dictionary<WeaponFamily, int> WeaponSystems::Armament(const cs::Ref<ShipCombat>& c)
	{
		cs::Dictionary<WeaponFamily, int> by;
		if (c == nullptr) return by;
		for (const auto& s : c->Subsystems)
		{
			if (s->Destroyed() || s->Def->Weapon == nullptr) continue;
			auto f = FamilyOf(s->Kind());
			if (f == WeaponFamily::None) continue;
			int n = 0;
			by.TryGetValue(f, n);
			by[f] = n + 1;
		}
		return by;
	}
}
