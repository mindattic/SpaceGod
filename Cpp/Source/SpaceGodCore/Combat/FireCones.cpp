// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/FireCones.cs (see docs/MIGRATION.md).

#include "Combat/FireCones.h"

#include "Astro.h"
#include "Combat/CombatSpecs.h"
#include "Combat/Outfitting.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	void FireCones::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		TurretTraverse();
		FixedCone();
		AiBringsGunsToBear();
		// Bow, Stern, Starboard, Port, Dorsal and Ventral are plain values (static inline const).
	}

	// ------------------------------------------------------------ settings

	const cs::Ref<RangeSetting>& FireCones::TurretTraverse()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("cone.turretTraverse", SettingGroups::Weapons,
			"Turret traverse", 1, 0.25, 1.75, 0.05,
			"How far every turret can traverse off its mount's normal, against its design: below 1 narrows every turret's cone, above 1 widens it (no cone ever wider than the whole sky).",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "fire cone turret traverse arc mount bear 3d");
		return value;
	}

	const cs::Ref<RangeSetting>& FireCones::FixedCone()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("cone.fixed", SettingGroups::Weapons,
			"Fixed mount cone", 1, 0.25, 3, 0.05,
			"How far a spinal mount or fixed tube can be laid off the keel, against its design: the hull itself has to lay the rest.",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "fire cone spinal fixed tube keel gimbal bear");
		return value;
	}

	const cs::Ref<ToggleSetting>& FireCones::AiBringsGunsToBear()
	{
		RunClassConstructor();
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("cone.aiBearing", SettingGroups::Broadsides,
			"AI captains bring their guns to bear", true,
			"An AI hull turns so that the guns whose cones can take its target carry the most firepower onto it; off, it simply points its bow at the target.",
			"fire cone bear turn broadside ai doctrine");
		return value;
	}

	// ------------------------------------------------------------ what a mount is

	bool FireCones::IsFixed(const cs::Ref<SubsystemDef>& d)
	{
		if (d->Spinal) return true;
		switch (d->Kind)
		{
			case SubsystemKind::FighterGuns:
			case SubsystemKind::Torpedo:
			case SubsystemKind::Drill:
				return true;
			default:
				break;
		}
		auto m = ModuleOf(d->Kind);
		return m.has_value() && ModuleSpec::Of(*m)->Spinal;
	}

	cs::Nullable<ModuleKind> FireCones::ModuleOf(SubsystemKind k)
	{
		switch (k)
		{
			case SubsystemKind::LaserTurret: return ModuleKind::LaserTurret;
			case SubsystemKind::HeavyLaser: return ModuleKind::HeavyLaser;
			case SubsystemKind::Railgun: return ModuleKind::Railgun;
			case SubsystemKind::MissileLauncher: return ModuleKind::MissileLauncher;
			case SubsystemKind::PointDefense: return ModuleKind::PointDefense;
			case SubsystemKind::Autocannon: return ModuleKind::Autocannon;
			case SubsystemKind::LightRailgun: return ModuleKind::LightRailgun;
			case SubsystemKind::EmpLauncher: return ModuleKind::EmpLauncher;
			case SubsystemKind::MineLayer: return ModuleKind::MineLayer;
			case SubsystemKind::AutoTurret: return ModuleKind::AutoTurret;
			case SubsystemKind::TorpedoTube: return ModuleKind::TorpedoTube;
			case SubsystemKind::TachyonLance: return ModuleKind::TachyonLance;
			case SubsystemKind::PhaserArray: return ModuleKind::PhaserArray;
			case SubsystemKind::PlasmaCannon: return ModuleKind::PlasmaCannon;
			case SubsystemKind::AntimatterTorpedo: return ModuleKind::AntimatterTorpedo;
			case SubsystemKind::FlakCannon: return ModuleKind::FlakCannon;
			case SubsystemKind::InterceptorMissile: return ModuleKind::InterceptorMissile;
			case SubsystemKind::Coilgun: return ModuleKind::Coilgun;
			case SubsystemKind::ShieldGenerator: return ModuleKind::ShieldGenerator;
			case SubsystemKind::Engine: return ModuleKind::Engine;
			default: return std::nullopt;
		}
	}

	double FireCones::DesignHalfDeg(SubsystemKind k)
	{
		switch (k)
		{
			case SubsystemKind::FighterGuns: return 12;
			case SubsystemKind::Torpedo: return 20;
			case SubsystemKind::Drill: return 10;
			case SubsystemKind::BoardingPods:
			case SubsystemKind::Countermeasures: return 180;
			default:
				break;
		}
		auto m = ModuleOf(k);
		return m.has_value() ? ModuleSpec::Of(*m)->ConeHalfDeg : 180;
	}

	double FireCones::HalfDeg(const cs::Ref<SubsystemDef>& d)
	{
		double design = d->ConeHalfDeg.has_value() ? *d->ConeHalfDeg : DesignHalfDeg(d->Kind);
		if (design >= 180) return 180;
		double scale = IsFixed(d) ? FixedCone()->Value : TurretTraverse()->Value;
		return cs::Math::Max(0.0, cs::Math::Min(180.0, design * scale));
	}

	DVec3 FireCones::Axis(const cs::Ref<SubsystemDef>& d, double beamFraction)
	{
		if (d->ConeAxis.has_value() && (*d->ConeAxis).LengthSquared() > 1e-18) return (*d->ConeAxis).Normalized();
		if (IsFixed(d)) return d->FacesAft ? Stern : Bow;
		return MountNormal(d->X, d->Y, d->Z, beamFraction, d->FacesAft);
	}

	DVec3 FireCones::Axis(const cs::Ref<SubsystemDef>& d, const cs::Ref<CombatSpec>& spec)
	{
		return Axis(d, spec ? spec->BeamFraction : DefaultBeamFraction);
	}

	DVec3 FireCones::MountNormal(double x, double y, double z, double beamFraction, bool facesAft)
	{
		double r = cs::Math::Max(0.005, cs::Math::Min(0.5, beamFraction * 0.5));
		double core = cs::Math::Max(0.0, 0.5 - r);
		double along = cs::Math::Max(-core, cs::Math::Min(core, z));
		auto n = DVec3(x, y, z - along);
		if (n.LengthSquared() > 1e-10) return n.Normalized();
		return cs::Math::Abs(z) >= core && core > 0 ? (z > 0 ? Bow : Stern) : facesAft ? Stern : Dorsal;
	}

	// ------------------------------------------------------------ bearing

	bool FireCones::Covers(const cs::Ref<SubsystemDef>& d, const cs::Ref<CombatSpec>& spec, DVec3 dirLocal)
	{
		double half = HalfDeg(d);
		if (half >= 180) return true;
		if (dirLocal.LengthSquared() < 1e-24) return false;
		return OffAxisDeg(d, spec, dirLocal) <= half + 1e-9;
	}

	double FireCones::OffAxisDeg(const cs::Ref<SubsystemDef>& d, const cs::Ref<CombatSpec>& spec, DVec3 dirLocal)
	{
		if (dirLocal.LengthSquared() < 1e-24) return 0;
		double cos = DVec3::Dot(Axis(d, spec), dirLocal.Normalized());
		return cs::Math::Acos(cs::Math::Max(-1.0, cs::Math::Min(1.0, cos))) * Astro::Rad2Deg;
	}

	double FireCones::OutsideDeg(const cs::Ref<SubsystemDef>& d, const cs::Ref<CombatSpec>& spec, DVec3 dirLocal)
	{
		return cs::Math::Max(0.0, OffAxisDeg(d, spec, dirLocal) - HalfDeg(d));
	}

	bool FireCones::Overlap(const cs::Ref<SubsystemDef>& a, const cs::Ref<SubsystemDef>& b, const cs::Ref<CombatSpec>& spec)
	{
		double cos = DVec3::Dot(Axis(a, spec), Axis(b, spec));
		double between = cs::Math::Acos(cs::Math::Max(-1.0, cs::Math::Min(1.0, cos))) * Astro::Rad2Deg;
		return between <= HalfDeg(a) + HalfDeg(b) + 1e-9;
	}

	DVec3 FireCones::Clamp(DVec3 dir, DVec3 axis, double halfDeg)
	{
		if (halfDeg >= 180 || dir.LengthSquared() < 1e-24) return dir;
		auto a = axis.Normalized();
		auto v = dir.Normalized();
		double off = cs::Math::Acos(cs::Math::Max(-1.0, cs::Math::Min(1.0, DVec3::Dot(a, v))));
		double half = halfDeg * Astro::Deg2Rad;
		if (off <= half + 1e-12) return v;
		return DVec3::TurnStep(a, v, half / off);
	}
}
