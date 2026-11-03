// Port of Unity.SpaceGod Core/Embodiment/MountedTurret.cs (US-T21, see docs/MIGRATION.md).

#include "Embodiment/MountedTurret.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/FireCones.h"
#include "Combat/Gunnery.h"
#include "Combat/PilotFlight.h"
#include "Combat/SpinalMounts.h"

namespace SpaceGod::Core
{
	MountedTurret::MountedTurret(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& unit, const cs::Ref<Subsystem>& mount, const cs::Ref<IEmbodiable>& host)
	{
		Battle = battle;
		Unit = unit;
		Mount = mount;
		this->host = host;
	}

	cs::String MountedTurret::Name() const
	{
		return cs::Cat(Unit->Name(), " ", cs::Strings::ToLowerInvariant(SubsystemDef::Name(Mount->Kind())), " ", Mount->Index + 1);
	}

	cs::Ref<Core::Faction> MountedTurret::Faction() const { return Unit->Faction; }

	bool MountedTurret::Embodied() const
	{
		cs::Ref<MountControl> m;
		return Battle->Manned.TryGetValue(Mount, m) && !m->ByStation;
	}

	cs::String MountedTurret::Blocker() const
	{
		if (!IsMannable(Mount->Def)) return "not a gun mount";
		if (Battle->Over) return "the fight is over";
		if (!Unit->Present()) return "gone";
		if (Unit->Captured) return "taken as a prize";
		if (Mount->Destroyed()) return "shot away";
		return cs::String();
	}

	bool MountedTurret::IsMannable(const cs::Ref<SubsystemDef>& d)
	{
		return d->Weapon != nullptr && !d->Weapon->Missile && d->Weapon->Deploy == DeployKind::None && d->Weapon->EmpSeconds <= 0 && d->Kind != SubsystemKind::BoardingPods;
	}

	double MountedTurret::TraverseDegS(SubsystemKind k)
	{
		switch (k)
		{
		case SubsystemKind::PointDefense:
		case SubsystemKind::AutoTurret: return 120;
		case SubsystemKind::FlakCannon: return 100;
		case SubsystemKind::Autocannon:
		case SubsystemKind::FighterGuns: return 90;
		case SubsystemKind::LaserTurret:
		case SubsystemKind::PhaserArray: return 45;
		case SubsystemKind::PlasmaCannon: return 20;
		case SubsystemKind::LightRailgun: return 30;
		case SubsystemKind::HeavyLaser: return 25;
		case SubsystemKind::Railgun:
		case SubsystemKind::Coilgun: return 10;
		case SubsystemKind::TachyonLance: return 5;
		default: return 40;
		}
	}

	DVec3 MountedTurret::RestAim(const cs::Ref<BattleUnit>& u, const cs::Ref<SubsystemDef>& d)
	{
		auto axis = d->AxisOn(u->Combat ? u->Combat->Spec : nullptr);
		if (!FireCones::IsFixed(d) && cs::Math::Abs(axis.Y) > 0.7)
		{
			auto along = d->FacesAft ? FireCones::Stern : FireCones::Bow;
			axis = FireCones::Clamp(along, axis, d->HalfDeg());
		}
		return u->LocalDirToWorld(axis).Normalized();
	}

	DVec3 MountedTurret::Bound(const cs::Ref<BattleUnit>& u, const cs::Ref<SubsystemDef>& d, DVec3 aim)
	{
		// A spinal mount, whatever gun is in it, swings only within the same gimbal (U.SPC-T490).
		if (SpinalMounts::HullLaid(d)) return Gunnery::Gimbal(aim, RestAim(u, d), Gunnery::RailgunGimbalDeg);
		if (d->BearsEverywhere()) return aim;
		auto axis = u->LocalDirToWorld(d->AxisOn(u->Combat ? u->Combat->Spec : nullptr));
		return FireCones::Clamp(aim, axis, d->HalfDeg());
	}

	EmbodimentPose MountedTurret::PoseAt(double jd)
	{
		auto aim = Mount->Aim.LengthSquared() > 1e-12 ? Mount->Aim.Normalized() : RestAim(Unit, Mount->Def);
		DVec3 unusedRight, up;
		Unit->Basis(unusedRight, up);
		auto roof = up - aim * DVec3::Dot(up, aim);
		return EmbodimentPose(Battle->Body, Unit->PointOf(Mount->Def) + Battle->Anchor, aim, roof.LengthSquared() > 1e-12 ? roof.Normalized() : PilotState::LevelUp(aim));
	}

	void MountedTurret::TakeCharge()
	{
		auto aim = Mount->Aim.LengthSquared() > 1e-12 ? Mount->Aim.Normalized() : RestAim(Unit, Mount->Def);
		auto control = cs::New<MountControl>();
		control->Aim = aim;
		Battle->Manned[Mount] = control;
	}

	void MountedTurret::Control(const EmbodimentInput& input, double dt)
	{
		cs::Ref<MountControl> m;
		if (!Battle->Manned.TryGetValue(Mount, m)) return;
		if (input.Aim.LengthSquared() > 1e-12) m->Aim = input.Aim.Normalized();
		m->Trigger = input.Primary;
	}

	void MountedTurret::HandBack() { Battle->Manned.Remove(Mount); }

	bool MountedTurret::Equals(const cs::Ref<IEmbodiable>& obj) const
	{
		auto t = cs::As<MountedTurret>(obj);
		return t && t->Mount == Mount && t->Battle == Battle;
	}

	size_t MountedTurret::GetHashCode() const { return cs::Hash<cs::Ref<Subsystem>>{}(Mount); }
}
