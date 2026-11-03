// Port of Assets/UnitySpace/Core/Combat/BreacherBays.cs (see docs/MIGRATION.md).

#include "Combat/BreacherBays.h"

#include "Combat/Boarding.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Facilities.h"
#include "Combat/FleetCoordination.h"
#include "Combat/MarineHabitats.h"
#include "Combat/Outfitting.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	cs::String BreacherBays::Seconds(double v) { return cs::Fmt(v, "0") + " s"; }

	const cs::Ref<RangeSetting>& BreacherBays::TeamSize()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.breacherTeam", SettingGroups::Broadsides,
			"Breacher team", 70, 10, 200, 5,
			"How many marines ride in one Breacher round fired from a Breacher bay. The ship's marine habitats must have a whole "
			"team ready before the bay can fire.", cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0") + " marines"; }),
			"breacher bay assault boarding round marines team");
		return s;
	}

	const cs::Ref<RangeSetting>& BreacherBays::ReadySeconds()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.breacherReady", SettingGroups::Broadsides,
			"Breacher team readying", 120, 10, 900, 10,
			"How long (seconds) one marine habitat takes to suit up the next team and load it into a Breacher round. A ship with "
			"more habitats readies teams faster; the Breacher bay reloads no faster than they do.", cs::Func<cs::String, double>(&Seconds),
			"breacher bay assault reload marines habitat ready team");
		return s;
	}

	const cs::Ref<RangeSetting>& BreacherBays::CycleSeconds()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.breacherCycle", SettingGroups::Broadsides,
			"Breacher bay cycle", 20, 2, 120, 1,
			"How long (seconds) a Breacher bay takes to fire again with a team ready and waiting. More bays share the work.", cs::Func<cs::String, double>(&Seconds),
			"breacher bay assault reload cycle");
		return s;
	}

	const cs::Ref<RangeSetting>& BreacherBays::RoundSpeedKmS()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.breacherSpeed", SettingGroups::Broadsides,
			"Breacher round speed", 2.5, 0.5, 8, 0.1,
			"How fast (km/s, relative to the ship that fired it) a Breacher round leaves the Breacher bay.",
			cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0.0") + " km/s"; }), "breacher round speed velocity bay assault");
		return s;
	}

	const cs::Ref<RangeSetting>& BreacherBays::SteerKmS2()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.breacherSteer", SettingGroups::Broadsides,
			"Breacher course correction", 0.02, 0, 0.2, 0.005,
			"How hard (km/s\xC2\xB2) a Breacher round's thrusters can push it off its line toward its target: a slight correction, "
			"not a missile's homing.", cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0.000") + " km/s\xC2\xB2"; }),
			"breacher round steering correction ballistic");
		return s;
	}

	const cs::Ref<RangeSetting>& BreacherBays::RangeKm()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.breacherRange", SettingGroups::Broadsides,
			"Breacher range", 15, 2, 40, 1,
			"How far (km) a Breacher bay fires a Breacher round at a hull.", cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0") + " km"; }),
			"breacher bay assault range");
		return s;
	}

	const cs::Ref<RangeSetting>& BreacherBays::ImpactDamage()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.breacherImpact", SettingGroups::Broadsides,
			"Breacher impact", 300, 0, 2000, 25,
			"How much damage a Breacher round's drill does to the hull it strikes as it bores in.",
			cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0"); }), "breacher round drill impact damage");
		return s;
	}

	cs::Ref<FacilityKind> BreacherBays::Kind() { return FacilityKinds::Of(FacilityKinds::BreacherBay); }

	int BreacherBays::Count(const cs::Ref<Ship>& s) { return FleetCoordination::Count(s, FacilityKinds::BreacherBay); }

	int BreacherBays::Habitats(const cs::Ref<Ship>& s) { return FleetCoordination::Count(s, FacilityKinds::MarineHabitat); }

	double BreacherBays::IntervalSeconds(const cs::Ref<Ship>& s)
	{
		int bays = Count(s), habitats = Habitats(s);
		if (bays <= 0 || habitats <= 0) return cs::Double::PositiveInfinity;
		return cs::Math::Max(CycleSeconds()->Value / bays, ReadySeconds()->Value / habitats);
	}

	bool BreacherBays::TeamReady(const cs::Ref<Interior>& interior) { return MarineHabitats::Ready(interior) >= TeamSize()->Value - 1e-9; }

	bool BreacherBays::Opens(const cs::Ref<ShipCombat>& c) { return c != nullptr && (!c->HasShieldGenerator() || c->Shield() <= c->Spec->Shield * ShieldShare); }

	bool BreacherBays::CanCarry(const cs::Ref<Loadout>& l)
	{
		auto k = Kind();
		return l != nullptr && k != nullptr && k->AllowedOn(l->Category, l->Class)
			&& l->FreePowerMw() >= k->PowerMw - 1e-9 && l->FreeCrew() >= k->Crew - 1e-9;
	}

	bool BreacherBays::CanMan(const cs::Ref<Loadout>& l) { return l != nullptr && MarineHabitats::Complement(l) >= TeamSize()->Value - 1e-9; }

	bool BreacherBays::Equip(const cs::Ref<Ship>& s)
	{
		if (s == nullptr || Kind() == nullptr) return false;
		auto habitat = MarineHabitats::Kind();
		auto l = Outfitting::LoadoutOf(s);
		if (l == nullptr || !CanCarry(l)) return false;
		while (MarineHabitats::Complement(l) < TeamSize()->Value && l->FreeSpace() >= habitat->Size + Kind()->Size - 1e-9
			&& Outfitting::Install(s, habitat)) { }
		return Outfitting::Install(s, Kind());
	}

	DVec3 BreacherBays::Correct(DVec3 vel, DVec3 toward, double accelKmS2, double dt)
	{
		double speed = vel.Length();
		if (speed < 1e-12 || toward.LengthSquared() < 1e-18 || accelKmS2 <= 0 || dt <= 0) return vel;
		auto dv = toward.Normalized() * speed - vel;
		double most = accelKmS2 * dt;
		auto turned = vel + (dv.Length() > most ? dv.Normalized() * most : dv);
		return turned.LengthSquared() < 1e-18 ? vel : turned.Normalized() * speed;
	}

	DVec3 BreacherBays::Lead(DVec3 from, DVec3 fromVel, DVec3 at, DVec3 atVel, double speedKmS)
	{
		auto rel = atVel - fromVel;
		auto aim = at;
		for (int i = 0; i < 4; i++) aim = at + rel * ((aim - from).Length() / cs::Math::Max(speedKmS, 1e-6));
		return aim;
	}
}
