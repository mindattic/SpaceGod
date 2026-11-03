#include "Combat/StrandedWings.h"
#include "Combat/Battle.h"
#include "Combat/BoardingShuttles.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Combat/Fighters.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"

namespace SpaceGod::Core
{
	bool StrandedWings::CanLandOn(const cs::Ref<BattleUnit>& home) { return home != nullptr && home->Present() && !home->Captured; }

	bool StrandedWings::IsStation(const cs::Ref<BattleUnit>& haven) { return haven != nullptr && haven->Kind == UnitKind::Platform; }

	bool StrandedWings::TakesStrays(const cs::Ref<BattleUnit>& haven)
	{
		if (!CanLandOn(haven) || haven->Kind == UnitKind::Fighter || haven->Surrendering || haven->Fleeing) return false;
		if (IsStation(haven)) return true;
		if (haven->Combat == nullptr || haven->Combat->Spec->Squadrons <= 0) return false;
		for (const auto& s : haven->Combat->Subsystems)
			if (s->Kind() == SubsystemKind::Hangar && !s->Destroyed()) return true;
		return false;
	}

	int StrandedWings::Room(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& haven)
	{
		if (IsStation(haven)) return cs::Int::MaxValue;
		int room = haven->Combat->Spec->HangarCraft() - haven->Combat->Fighters();
		for (const auto& o : b->Units)
			// A boarding shuttle lands in its own berths, not a squadron's (U.SPC-T486).
			if (o->Kind == UnitKind::Fighter && o->Present() && o->Fighter != nullptr && o->Fighter->LandsOn() == haven && !BoardingShuttles::Is(o)) room--;
		return room;
	}

	cs::Ref<BattleUnit> StrandedWings::Haven(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& craft)
	{
		cs::Ref<BattleUnit> best = nullptr;
		double bestD = cs::Double::MaxValue;
		for (const auto& o : b->Units)
		{
			if (o == craft || o->Side != craft->Side || o == (craft->Fighter ? craft->Fighter->LandsOn() : nullptr) || !TakesStrays(o)) continue;
			double d = (o->Pos - craft->Pos).LengthSquared();
			if (d >= bestD || Room(b, o) <= 0) continue;
			best = o;
			bestD = d;
		}
		return best;
	}

	void StrandedWings::Step(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt)
	{
		auto f = u->Fighter;
		if (f == nullptr || CanLandOn(f->LandsOn())) return;
		f->HavenCheckIn -= dt;
		if (f->HavenCheckIn > 0) return;
		f->HavenCheckIn = HavenCheckSeconds;
		auto haven = Haven(b, u);
		if (haven == nullptr)
		{
			f->Stranded = true;
			return;
		}
		f->Home = haven;
		f->Stranded = false;
	}

	cs::Ref<Settlement> StrandedWings::Port(const cs::Ref<Battle>& b, const cs::Ref<Faction>& owners, const cs::Ref<Economy>& economy, const cs::Ref<Diplomacy>& diplomacy)
	{
		if (b == nullptr || b->Site != nullptr || economy == nullptr || owners == nullptr) return nullptr;
		cs::Ref<Settlement> friendly = nullptr;
		for (const auto& s : economy->At(b->Body))
		{
			if (s->Owner == nullptr || s->Kind == SettlementKind::MiningOutpost) continue;
			if (s->Owner == owners) return s;
			if (friendly == nullptr && diplomacy != nullptr && !diplomacy->Hostile(owners, s->Owner) && diplomacy->CanDockAt(owners, s)) friendly = s;
		}
		return friendly;
	}
}
