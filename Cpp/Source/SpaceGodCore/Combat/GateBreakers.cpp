// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/GateBreakers.cs (see docs/MIGRATION.md).

#include "Combat/GateBreakers.h"

#include "Ships.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/Facilities.h"
#include "Combat/Outfitting.h"
#include "Settings/GameSetting.h"
#include "Systems/JumpGate.h"

namespace SpaceGod::Core
{
	cs::String GateBreakers::Count(double v) { return cs::Fmt(v, "0"); }

	cs::String GateBreakers::Percent(double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }

	const cs::Ref<RangeSetting>& GateBreakers::TeamMarines()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.breakerTeam", SettingGroups::Broadsides,
			"Gate breaker boarding teams", 80, 10, 300, 10,
			cs::String("How many marines a gate breaker must have aboard for its boarding teams to storm a jump gate whose forts are silenced ") +
			"and take it intact on the first day; with fewer the fleet holds the gate the usual days before it is theirs.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(Count(v), " marines"); }), "gate breaker siege boarding team marines seize gate");
		return s;
	}

	const cs::Ref<RangeSetting>& GateBreakers::FieldRepair()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.breakerRepair", SettingGroups::Broadsides,
			"Gate breaker field repairs", 1, 0, 1, 0.05,
			cs::String("The share of the damage a jump gate's ring has taken (sabotage, a fight) that a gate breaker's engineering teams make ") +
			"good on the spot when they seize it, so a sabotaged ring is back in service at once.", cs::Func<cs::String, double>(Percent),
			"gate breaker engineers repair ring seize intact sabotage");
		return s;
	}

	const cs::Ref<RangeSetting>& GateBreakers::FortFocus()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.breakerFortFocus", SettingGroups::Broadsides,
			"Gate breaker fort preference", 15, 0, 40, 1,
			"How strongly a gate breaker picks a gate's fort over anything else in a gate battle (as if it were that many km nearer).",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(Count(v), " km"); }), "gate breaker siege fort target preference");
		return s;
	}

	const cs::Ref<RangeSetting>& GateBreakers::PerNavy()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("war.breakersPerNavy", SettingGroups::Ai,
			"Gate breakers per navy at war", 1, 0, 4, 1,
			cs::String("How many gate breakers a navy at war keeps afloat: one short, the next yard that can afford one lays it down before ") +
			"its build plan's next hull. 0 leaves them to a player's orders.", cs::Func<cs::String, double>(Count),
			"gate breaker siege capital build navy war yard");
		return s;
	}

	// ---------------------------------------------------------------- who breaks gates

	cs::HashSet<ShipClass>& GateBreakers::classes()
	{
		static cs::HashSet<ShipClass> s = cs::HashSet<ShipClass>{ ShipClass::GateBreaker };
		return s;
	}

	const cs::HashSet<ShipClass>& GateBreakers::Classes() { return classes(); }

	void GateBreakers::Register(ShipClass cls, bool breaks)
	{
		if (breaks) classes().Add(cls);
		else classes().Remove(cls);
	}

	bool GateBreakers::IsClass(ShipClass cls) { return classes().Contains(cls); }

	bool GateBreakers::Works(const cs::Ref<Ship>& s)
	{
		if ((s == nullptr || s->Spec == nullptr) || !IsClass(s->Spec->Class) || s->Destroyed || s->Derelict || s->Combat == nullptr || s->Combat->Disabled()) return false;
		return s->Loadout != nullptr
			? s->Loadout->CountFacility(FacilityKinds::SiegeEngineering) > 0
			: FacilityLayouts::CountOf(FacilityLayouts::For(s->Spec->Class), FacilityKinds::SiegeEngineering) > 0;
	}

	cs::Ref<Ship> GateBreakers::Stormer(const cs::List<cs::Ref<Ship>>& group)
	{
		// (C#'s `group == null` cannot arise: the list is passed by reference.)
		for (const auto& s : group)
			if (Works(s) && s->Combat->MarinesAvailable() >= TeamMarines()->Value) return s;
		return nullptr;
	}

	double GateBreakers::GateHoldDays(double holdDays) { return cs::Math::Min(holdDays, 1.0); }

	double GateBreakers::MakeGood(const cs::Ref<JumpGate>& gate)
	{
		if (gate == nullptr || gate->Integrity >= 1) return 0;
		double repaired = (1 - gate->Integrity) * cs::Math::Max(0.0, cs::Math::Min(1.0, FieldRepair()->Value));
		gate->Integrity = cs::Math::Min(1.0, gate->Integrity + repaired);
		return repaired;
	}

	// ---------------------------------------------------------------- in battle

	double GateBreakers::TargetBias(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& o)
	{
		if (u == nullptr || o == nullptr || o->Fort == nullptr || u->PlayerControlled() || (u->Ship == nullptr || u->Ship->Spec == nullptr) || !IsClass(u->Ship->Spec->Class)) return 0;
		return FortFocus()->Value;
	}

	// ---------------------------------------------------------------- building them

	bool GateBreakers::Wanted(const cs::Ref<Faction>& f, bool atWar, int have)
	{
		return f != nullptr && atWar && f->IsMilitary && !f->IsPirate && have < cs::ToInt(cs::Math::Round(PerNavy()->Value));
	}
}
