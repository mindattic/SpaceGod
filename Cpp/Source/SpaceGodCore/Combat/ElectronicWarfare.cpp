// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/ElectronicWarfare.cs (see docs/MIGRATION.md).

#include "Combat/ElectronicWarfare.h"

#include "Combat/Battle.h"
#include "Combat/CarrierWings.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/Fighters.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	cs::String ElectronicWarfare::Km(double v) { return cs::Cat(cs::Fmt(v, "0.#"), " km"); }

	cs::String ElectronicWarfare::Percent(double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }

	void ElectronicWarfare::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		JamRangeKm();
		JamAccuracy();
		PaintRangeKm();
		PaintGuidance();
		PaintDecoyResist();
	}

	const cs::Ref<RangeSetting>& ElectronicWarfare::JamRangeKm()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("dogfight.jamRange", SettingGroups::Dogfighting,
			"Recon jamming range", 8, 0, 20, 0.5,
			"How far a recon fighter's jammers reach: every enemy ship, platform and craft this near it fires less accurately. 0 switches jamming off.",
			&Km, "recon ew electronic warfare jamming jammer accuracy dogfight");
		return value;
	}

	const cs::Ref<RangeSetting>& ElectronicWarfare::JamAccuracy()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("dogfight.jamAccuracy", SettingGroups::Dogfighting,
			"Accuracy lost to jamming", 0.3, 0, 0.8, 0.05,
			"How much of its gunners' and point defence's accuracy a jammed ship loses (several jammers on it do not add up).",
			&Percent, "recon ew electronic warfare jamming jammer accuracy gunnery dogfight");
		return value;
	}

	const cs::Ref<RangeSetting>& ElectronicWarfare::PaintRangeKm()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("dogfight.paintRange", SettingGroups::Dogfighting,
			"Recon painting range", 12, 0, 25, 0.5,
			"How near its mark a recon fighter must be to paint it for its bombers. 0 switches painting off.",
			&Km, "recon ew electronic warfare paint designate target bomber torpedo dogfight");
		return value;
	}

	const cs::Ref<RangeSetting>& ElectronicWarfare::PaintGuidance()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("dogfight.paintGuidance", SettingGroups::Dogfighting,
			"Torpedo guidance on a painted target", 2.5, 1, 5, 0.25,
			"How much harder a bomber's torpedo steers onto a target a recon fighter is painting. 1 takes the bonus away.",
			[](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }, "recon paint bomber torpedo guidance homing dogfight");
		return value;
	}

	const cs::Ref<RangeSetting>& ElectronicWarfare::PaintDecoyResist()
	{
		RunClassConstructor();
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("dogfight.paintDecoyResist", SettingGroups::Dogfighting,
			"Painted torpedoes ignore decoys", 0.8, 0, 1, 0.05,
			"How much less often a flare-and-chaff cloud draws off a bomber's torpedo homing on a painted target: it rides the paint, not the heat.",
			&Percent, "recon paint bomber torpedo decoy flare chaff countermeasures dogfight");
		return value;
	}

	bool ElectronicWarfare::IsRecon(const cs::Ref<BattleUnit>& u)
	{
		return u != nullptr && (u->Fighter != nullptr ? u->Fighter->Role == FighterRole::Recon : u->Ship != nullptr && u->Ship->Spec->Class == ShipClass::Recon);
	}

	bool ElectronicWarfare::Working(const cs::Ref<BattleUnit>& r) { return r->Present() && r->Stunned <= 0 && !r->Surrendering && !r->Captured; }

	void ElectronicWarfare::Sweep(const cs::Ref<Battle>& b)
	{
		b->ReconAloft.Clear();
		for (const auto& u : b->Units)
			if (u->Present() && IsRecon(u)) b->ReconAloft.Add(u);
	}

	double ElectronicWarfare::JamFactor(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		if (b == nullptr || u == nullptr || b->ReconAloft.Count() == 0) return 1;
		double range = JamRangeKm()->Value;
		if (range <= 0) return 1;
		for (const auto& r : b->ReconAloft)
			if (r->Side != u->Side && Working(r) && (r->Pos - u->Pos).Length() - u->Radius() <= range)
				return 1 - cs::Math::Max(0.0, cs::Math::Min(0.95, JamAccuracy()->Value));
		return 1;
	}

	bool ElectronicWarfare::Jammed(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u) { return JamFactor(b, u) < 1; }

	bool ElectronicWarfare::Painted(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& target, int side)
	{
		if (b == nullptr || target == nullptr || b->ReconAloft.Count() == 0 || target->Side == side) return false;
		double range = PaintRangeKm()->Value;
		if (range <= 0) return false;
		for (const auto& r : b->ReconAloft)
			if (r->Side == side && r->Target == target && Working(r) && (r->Pos - target->Pos).Length() - target->Radius() <= range) return true;
		return false;
	}

	double ElectronicWarfare::Guidance(const cs::Ref<Battle>& b, const cs::Ref<Missile>& m)
	{
		return StrikeTorpedoes::IsStrike(m) && Painted(b, m->Target, m->Side) ? cs::Math::Max(1.0, PaintGuidance()->Value) : 1;
	}

	double ElectronicWarfare::SeduceChance(const cs::Ref<Battle>& b, const cs::Ref<Missile>& m)
	{
		double chance = Countermeasures::SeduceChance(m->Kind);
		if (StrikeTorpedoes::IsStrike(m) && Painted(b, m->Target, m->Side)) chance *= 1 - cs::Math::Max(0.0, cs::Math::Min(1.0, PaintDecoyResist()->Value));
		return chance;
	}

	cs::Ref<BattleUnit> ElectronicWarfare::ChooseMark(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		cs::Ref<BattleUnit> bomberMark = nullptr;
		double nearestBomber = cs::Double::MaxValue;
		for (const auto& f : b->Units)
		{
			if (!f->Present() || f->Kind != UnitKind::Fighter || f->Side != u->Side || f->Fighter == nullptr || !f->Fighter->Bomber) continue;
			auto t = f->Target;
			if (t == nullptr || t->Kind == UnitKind::Fighter || !b->Hostile(u, t) || t->Surrendering || t->Captured || !WeakPoints::IsCapital(t)) continue;
			double d = (f->Pos - u->Pos).Length();
			if (d < nearestBomber)
			{
				nearestBomber = d;
				bomberMark = t;
			}
		}
		if (bomberMark != nullptr) return bomberMark;
		cs::Ref<BattleUnit> best = nullptr;
		double bestScore = cs::Double::MaxValue;
		for (const auto& o : b->Units)
		{
			if (o->Kind == UnitKind::Fighter || !b->Hostile(u, o) || o->Surrendering || o->Captured) continue;
			double score = (o->Pos - u->Pos).Length() - o->Combat->Spec->Hull / 1000.0 - (WeakPoints::IsCapital(o) ? 30 : 0);
			if (score < bestScore)
			{
				bestScore = score;
				best = o;
			}
		}
		return best;
	}

	int ElectronicWarfare::ReconAboard(const cs::Ref<Ship>& s)
	{
		return (s ? s->Combat : cs::Ref<ShipCombat>()) == nullptr ? 0 : CarrierWings::Aboard(s->Combat, FighterRole::Recon);
	}
}
