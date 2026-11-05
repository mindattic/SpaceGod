// US-T106: the battle's side of the orders a player gives from a ship's helm (Battle members, as BoardingShuttles.cpp holds
// the shuttles'): a Breacher round, the boarding pods or a boarding shuttle at the hull locked, and the lapse of a carrier's
// wing orders once nobody holds its helm. The AI never calls these; its own firing (FireBreacherBay, FirePods, SendShuttles)
// is unchanged.

#include "Combat/Battle.h"

#include "Combat/Boarding.h"
#include "Combat/BoardingShuttles.h"
#include "Combat/BreacherBays.h"
#include "Combat/CarrierWings.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/Fighters.h"
#include "Settings/GameSetting.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	namespace
	{
		cs::String Km(double km) { return cs::Fmt(km, "0.0") + " km"; }

		/// <summary>Why the hull locked is no prize for a boarding order, or empty: nothing locked, not hostile, gone or taken.</summary>
		cs::String LockedPrize(Battle& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target)
		{
			if (target == nullptr) return "lock a hull first (T)";
			if (!target->Present()) return cs::Cat("the ", target->Name(), " is gone");
			if (target->Kind == UnitKind::Fighter) return "a small craft cannot be boarded: lock a hull";
			if (!b.Hostile(u, target)) return cs::Cat("the ", target->Name(), " is not hostile");
			if (target->Captured) return cs::Cat("the ", target->Name(), " is already taken");
			if (target->Surrendering && !target->SurrenderOpenToBoarding) return cs::Cat("the ", target->Name(), " has struck: decide its terms first");
			if (!target->Combat->Interior->Admits(u->Faction, u->Side)) return cs::Cat("another side is boarding the ", target->Name());
			return cs::String();
		}
	}

	cs::String Battle::OrderBreacher(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target)
	{
		if (u == nullptr || u->Ship == nullptr || BreacherBays::Count(u->Ship) <= 0) return "no Breacher bay aboard";
		if (u->Combat->Disabled()) return "the hull is disabled";
		cs::String why = LockedPrize(*this, u, target);
		if (!why.empty()) return why;
		if (!MayBreach(u, target)) return cs::Cat("the ", target->Name(), " is not ours to board");
		if (u->BreacherBayReload > 0) return cs::Cat("the bays are cycling: ", cs::Fmt(u->BreacherBayReload / cs::Math::Max(0.05, u->Combat->Readiness), "0"), " s");
		if (!BreacherBays::TeamReady(u->Combat->Interior)) return "the habitats have no whole team ready";
		if (!BreacherBays::Opens(target->Combat)) return cs::Cat("the ", target->Name(), "'s shields would stop the round: knock them below a quarter");
		double d = (target->Pos - u->Pos).Length();
		if (d > BreacherBays::RangeKm()->Value) return cs::Cat("out of reach: ", Km(d), " (", Km(BreacherBays::RangeKm()->Value), " at most)");
		FireBreacherAt(u, target);
		return cs::String();
	}

	cs::String Battle::OrderPods(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target)
	{
		if (u == nullptr || u->Combat == nullptr) return "no boarding pods aboard";
		cs::Ref<Subsystem> ready = nullptr, any = nullptr;
		for (const auto& s : u->Combat->Subsystems)
		{
			if (s->Kind() != SubsystemKind::BoardingPods || s->Destroyed() || s->Def->Weapon == nullptr) continue;
			if (any == nullptr) any = s;
			if (ready == nullptr && s->Ammo > 0 && s->Cooldown <= 0) ready = s;
		}
		if (any == nullptr) return "no boarding pod launcher left standing";
		cs::String why = LockedPrize(*this, u, target);
		if (!why.empty()) return why;
		if (ready == nullptr) return any->Ammo <= 0 ? cs::String("the pod racks are empty") : cs::String("the launchers are reloading");
		if (u->Combat->MarinesAvailable() < WeaponSpec::PodMarines * 2) return "not marines enough aboard to send a squad and hold the ship";
		if (target->Combat->HasShieldGenerator() && target->Combat->Shield() > target->Combat->Spec->Shield * 0.05)
			return cs::Cat("the ", target->Name(), "'s shields are up: pods cannot get through them");
		double d = (target->Pos - u->Pos).Length();
		if (d > ready->Def->Weapon->RangeKm) return cs::Cat("out of reach: ", Km(d), " (", Km(ready->Def->Weapon->RangeKm), " at most)");
		FirePodsAt(u, ready, target);
		return cs::String();
	}

	cs::String Battle::OrderShuttle(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target, cs::Ref<BattleUnit>& sent)
	{
		sent = nullptr;
		if (u == nullptr || u->Combat == nullptr || u->Combat->Spec->Shuttles <= 0) return "no shuttle bay aboard";
		auto c = u->Combat;
		if (c->Disabled()) return "the hull is disabled";
		if (!HangarWorks(u)) return "the hangars are shot away";
		cs::String why = LockedPrize(*this, u, target);
		if (!why.empty()) return why;
		if (u->ShuttleLaunchIn > 0) return cs::Cat("the shuttle deck is clearing: ", cs::Fmt(u->ShuttleLaunchIn, "0"), " s");
		auto hull = CarrierWings::ReadyShuttle(c);
		if (hull == nullptr) return c->Shuttles() > 0 ? cs::String("no shuttle aboard fit to fly") : cs::String("every shuttle is out");
		double team = BoardingShuttles::TeamSize()->Value;
		if (c->MarinesAvailable() < team + BoardingShuttles::Reserve) return "not marines enough aboard for a team and the ship's own reserve";
		if (!ShuttleMayBoard(u, target)) return cs::Cat("the ", target->Name(), " is still fighting: disable it, or have it strike, first");
		double d = (target->Pos - u->Pos).Length();
		if (d > BoardingShuttles::RangeKm()->Value) return cs::Cat("out of reach: ", Km(d), " (", Km(BoardingShuttles::RangeKm()->Value), " at most)");
		sent = FighterAI::LaunchShuttle(shared_from_this(), u, target, c->Interior->TakeMarines(team), hull);
		u->ShuttleLaunchIn = BoardingShuttles::LaunchSeconds;
		return cs::String();
	}

	void Battle::HelmOrdersLapse(const cs::Ref<BattleUnit>& u)
	{
		if (!(u->WingHeld || u->WingRecalled || u->WingCall != 0) || u->PlayerControlled()) return;
		// The player flying one of its own craft keeps the carrier's orders standing (launch one, jump in, come back).
		for (const auto& o : Units)
			if (o->Fighter != nullptr && o->Fighter->Carrier == u && Flown.ContainsKey(o)) return;
		bool launch = u->WingHeld || u->WingRecalled;
		u->WingHeld = u->WingRecalled = false;
		u->WingCall = 0;
		if (!launch || !u->Present() || u->Fleeing || Mopping) return;
		// Its captain puts the wing up, as any carrier's does at the start of a fight: every class, squadron by squadron.
		for (auto role : { FighterRole::Interceptor, FighterRole::SpaceSuperiority, FighterRole::Bomber, FighterRole::HeavyFighter, FighterRole::Recon })
			while (FighterAI::LaunchOrdered(shared_from_this(), u, role).Count() > 0) {}
	}
}
