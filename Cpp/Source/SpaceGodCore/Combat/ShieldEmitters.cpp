// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/ShieldEmitters.cs (see docs/MIGRATION.md).

#include "Combat/ShieldEmitters.h"

#include "Combat/Collisions.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	void ShieldEmitters::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		RegenDelay();
		RegenRate();
		FocusFire();
		EmergencyRepairRate();
		EmergencyRepairWork();
		RestoredHealth();
	}

	// ------------------------------------------------------------ settings

	const cs::Ref<RangeSetting>& ShieldEmitters::RegenDelay()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("shield.regenDelay", SettingGroups::Broadsides,
			"Shield field regrowth delay", 8, 0, 30, 1,
			"Seconds a shield emitter's field must go unhit before it starts to regrow: each patch of hull keeps its own clock, so the patch under fire stays down while the rest recover.",
			[](double v) { return cs::Cat(cs::Fmt(v, "0"), " s"); }, "shield emitter field regen regrow delay patch");
		return value;
	}

	const cs::Ref<RangeSetting>& ShieldEmitters::RegenRate()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("shield.regenRate", SettingGroups::Broadsides,
			"Shield field regrowth rate", 1, 0, 3, 0.1,
			"How fast shield emitters regrow their fields, against each hull's own rating: 0 means a field shot away stays down for the battle, 1 is as designed.",
			[](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }, "shield emitter field regen rate patch");
		return value;
	}

	const cs::Ref<RangeSetting>& ShieldEmitters::FocusFire()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("shield.focusFire", SettingGroups::Broadsides,
			"Gunners focus a weakened patch", 2, 0, 5, 0.25,
			"How strongly gunners favour a hardpoint in a patch of hull whose shield field is weakened: a mount in a patch holed outright is this many times again as likely to be picked (0 ignores the fields).",
			[](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }, "shield emitter patch focus fire gunners ai holed");
		return value;
	}

	const cs::Ref<RangeSetting>& ShieldEmitters::EmergencyRepairRate()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("broadside.emergencyRepair", SettingGroups::Broadsides,
			"Emergency repair rate", 1, 0, 3, 0.25,
			"How fast a capital ship's emergency repair crews bring back a shield emitter or gun mount shot away in battle: 0 leaves it lost until a yard refits it, 1 is as designed.",
			[](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }, "emergency repair crew damage control shield emitter turret restore capital");
		return value;
	}

	const cs::Ref<RangeSetting>& ShieldEmitters::EmergencyRepairWork()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("broadside.emergencyRepairWork", SettingGroups::Broadsides,
			"Emergency repair work", 0.6, 0.1, 2, 0.1,
			"The work, as a share of a mount's hit points, the crews put in to bring a shot-away shield emitter or gun mount back.",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "emergency repair crew damage control shield emitter turret restore capital");
		return value;
	}

	const cs::Ref<RangeSetting>& ShieldEmitters::RestoredHealth()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("broadside.restoredHealth", SettingGroups::Broadsides,
			"Emergency repair restores to", 0.3, 0.05, 0.85, 0.05,
			"How whole a shot-away mount is when the crews bring it back (an emitter's field then regrows from nothing).",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "emergency repair crew damage control shield emitter turret restore capital");
		return value;
	}

	// ------------------------------------------------------------ patches

	DVec3 ShieldEmitters::StrikePoint(const cs::Ref<CombatSpec>& spec, DVec3 local, cs::Nullable<DVec3> incomingLocal)
	{
		if (local.LengthSquared() < 0.02 * 0.02)
		{
			if (incomingLocal.has_value() && (*incomingLocal).LengthSquared() > 1e-18) return Collisions::SurfacePoint(spec, *incomingLocal * -1);
			return local;
		}
		auto surface = Collisions::SurfacePoint(spec, local);
		return local.LengthSquared() > surface.LengthSquared() ? surface : local;
	}

	double ShieldEmitters::PatchStrength(const cs::Ref<ShipCombat>& c, DVec3 local)
	{
		auto patch = c->PatchAt(local);
		if (patch == nullptr) return 1;
		return patch->ShieldFraction();
	}

	double ShieldEmitters::FocusWeight(const cs::Ref<ShipCombat>& c, const cs::Ref<Subsystem>& s)
	{
		double k = FocusFire()->Value;
		if (k <= 0 || s->Def->Buried) return 1;
		return 1 + k * (1 - PatchStrength(c, DVec3(s->Def->X, s->Def->Y, s->Def->Z)));
	}

	bool ShieldEmitters::Restorable(const cs::Ref<Subsystem>& s)
	{
		// A nanite repair system on a shield mount (US-T102) is brought back as the emitter it stands in for would be.
		return s->Destroyed() && (s->Kind() == SubsystemKind::ShieldGenerator || s->Kind() == SubsystemKind::NaniteRepair || s->Def->IsWeapon());
	}
}
