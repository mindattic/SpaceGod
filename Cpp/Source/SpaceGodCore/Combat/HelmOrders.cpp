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
#include "Combat/FleetCoordination.h" // US-T332
#include "Combat/ScreenWeapons.h" // US-T333
#include "Combat/ElectronicWarfare.h" // US-T338
#include "Combat/Broadsides.h" // US-T347
#include "Combat/Morale.h" // US-T363
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

		/// <summary>US-T379: whether the helm's own hull can still give an order to its side (present, neither taken nor struck).</summary>
		bool InTheFight(const cs::Ref<BattleUnit>& u) { return u->Present() && !u->Captured && !u->Surrendering; }

		/// <summary>US-T379: whether <paramref name="o"/> is a hull that answers a call to fire or cripple by picking its target
		/// again: an AI hull (not a craft, not another player's) still fighting.</summary>
		bool AnswersTargetCall(const cs::Ref<BattleUnit>& o)
		{
			return o->Kind != UnitKind::Fighter && o->Combat != nullptr && !o->PlayerControlled() && o->Present() && !o->Captured
				&& !o->Surrendering && !o->Combat->Disabled();
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
		if (u->Combat->Disabled()) return "the hull is disabled"; // US-T379: as the Breacher and shuttle orders
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
		if (!ShuttleMayBoard(u, target))
		{
			// US-T379: say which of ShuttleMayBoard's tests failed, not always the last.
			if (!MayBoard(u, target)) return cs::Cat("the ", target->Name(), " is not ours to board");
			if (target->Ship != nullptr && Spares(u, target) && target->Ship->HasCargo && !target->SurrenderOpenToBoarding)
				return cs::Cat("the ", target->Name(), " is spared for its hold: ask for its cargo first");
			return cs::Cat("the ", target->Name(), " is still fighting: disable it, or have it strike, first");
		}
		double d = (target->Pos - u->Pos).Length();
		if (d > BoardingShuttles::RangeKm()->Value) return cs::Cat("out of reach: ", Km(d), " (", Km(BoardingShuttles::RangeKm()->Value), " at most)");
		sent = FighterAI::LaunchShuttle(shared_from_this(), u, target, c->Interior->TakeMarines(team), hull);
		u->ShuttleLaunchIn = BoardingShuttles::LaunchSeconds;
		return cs::String();
	}

	cs::String Battle::OrderFireCall(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target, int& answering)
	{
		answering = 0;
		if (u == nullptr || u->Combat == nullptr) return "no hull at the helm";
		if (!InTheFight(u)) return "the hull is out of the fight";
		if (target == nullptr) return "lock a hull first (T)";
		if (!target->Present()) return cs::Cat("the ", target->Name(), " is gone");
		if (!Hostile(u, target)) return cs::Cat("the ", target->Name(), " is not hostile");
		if (target->Captured || target->Surrendering) return cs::Cat("the ", target->Name(), " has struck");
		double reach = FleetCoordination::ReachKm()->Value;
		for (const auto& o : Units)
		{
			if (o == u || o->Side != u->Side || !AnswersTargetCall(o)) continue;
			if ((o->Pos - u->Pos).Length() > reach) continue;
			o->RetargetIn = 0; // they pick again on their next thought, the call weighing in (PickTarget)
			answering++;
		}
		if (answering == 0) return cs::Cat("no ship of ours within ", Km(reach), " to answer");
		u->FireCallTarget = target;
		u->FireCallUntil = Seconds + FireCallSeconds;
		return cs::String();
	}

	cs::Ref<BattleUnit> Battle::CalledFireFor(const cs::Ref<BattleUnit>& u)
	{
		if (u == nullptr) return nullptr;
		cs::Ref<BattleUnit> best = nullptr;
		double bestKm = FleetCoordination::ReachKm()->Value;
		for (const auto& c : Units)
		{
			if (c == u || c->FireCallTarget == nullptr || Seconds >= c->FireCallUntil || c->Side != u->Side || !c->Present()) continue;
			if (c->Captured || c->Surrendering) continue; // US-T379: a struck caller's call no longer steers its side
			if (!c->FireCallTarget->Present() || c->FireCallTarget->Captured || c->FireCallTarget->Surrendering) continue;
			double d = (c->Pos - u->Pos).Length();
			if (d <= bestKm)
			{
				bestKm = d;
				best = c->FireCallTarget;
			}
		}
		return best;
	}

	int Battle::ScreensInReach(const cs::Ref<BattleUnit>& u)
	{
		if (u == nullptr) return 0;
		double reach = FleetCoordination::ReachKm()->Value;
		int n = 0;
		for (const auto& o : Units)
		{
			if (o == u || o->Side != u->Side || !o->Present() || o->Captured || o->Kind == UnitKind::Fighter || o->Combat == nullptr) continue;
			if ((o->Pos - u->Pos).Length() > reach || !ScreenWeapons::Screens(o->Combat)) continue;
			n++;
		}
		return n;
	}

	cs::String Battle::OrderScreenCall(const cs::Ref<BattleUnit>& u, int& answering)
	{
		answering = 0;
		if (u == nullptr || u->Combat == nullptr) return "no hull at the helm";
		if (!InTheFight(u)) return "the hull is out of the fight";
		if (ScreenWeapons::ScreenStation()->Value <= 0) return "screens roam free (Screen station-keeping is 0)";
		answering = ScreensInReach(u);
		if (answering == 0) return cs::Cat("no hull of ours with flak or interceptors within ", Km(FleetCoordination::ReachKm()->Value));
		u->ScreenCallUntil = Seconds + ScreenCallSeconds;
		return cs::String();
	}

	cs::Ref<BattleUnit> Battle::CalledScreenFor(const cs::Ref<BattleUnit>& u)
	{
		if (u == nullptr) return nullptr;
		cs::Ref<BattleUnit> best = nullptr;
		double bestKm = FleetCoordination::ReachKm()->Value;
		for (const auto& c : Units)
		{
			if (c == u || Seconds >= c->ScreenCallUntil || c->Side != u->Side || !c->Present() || c->Captured || c->Surrendering) continue;
			double d = (c->Pos - u->Pos).Length();
			if (d <= bestKm)
			{
				bestKm = d;
				best = c;
			}
		}
		return best;
	}

	int Battle::ReconAloftFor(const cs::Ref<BattleUnit>& u)
	{
		if (u == nullptr) return 0;
		int n = 0;
		for (const auto& r : ReconAloft)
			if (r->Side == u->Side && r->Kind == UnitKind::Fighter && !r->PlayerControlled() && ElectronicWarfare::Working(r)) n++;
		return n;
	}

	cs::String Battle::OrderPaintCall(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target, int& answering)
	{
		answering = 0;
		if (u == nullptr || u->Combat == nullptr) return "no hull at the helm";
		if (!InTheFight(u)) return "the hull is out of the fight";
		if (target == nullptr) return "lock a hull first (T)";
		if (!target->Present()) return cs::Cat("the ", target->Name(), " is gone");
		if (!Hostile(u, target)) return cs::Cat("the ", target->Name(), " is not hostile");
		if (target->Kind == UnitKind::Fighter) return "recon paints ships, not craft";
		if (target->Captured || target->Surrendering) return cs::Cat("the ", target->Name(), " has struck");
		if (ElectronicWarfare::PaintRangeKm()->Value <= 0) return "painting is off (Recon painting range is 0)";
		for (const auto& r : ReconAloft)
		{
			if (r->Side != u->Side || r->Kind != UnitKind::Fighter || r->PlayerControlled() || !ElectronicWarfare::Working(r)) continue;
			r->Target = target; // onto the mark now; ChooseMark keeps it there while the call stands
			answering++;
		}
		if (answering == 0) return "no recon craft of ours aloft (launch recon first)";
		u->PaintCallTarget = target;
		u->PaintCallUntil = Seconds + PaintCallSeconds;
		return cs::String();
	}

	cs::Ref<BattleUnit> Battle::CalledPaintFor(const cs::Ref<BattleUnit>& r)
	{
		if (r == nullptr) return nullptr;
		cs::Ref<BattleUnit> best = nullptr;
		double bestKm = cs::Double::MaxValue;
		for (const auto& c : Units)
		{
			if (c == r || c->PaintCallTarget == nullptr || Seconds >= c->PaintCallUntil || c->Side != r->Side || !c->Present()) continue;
			auto t = c->PaintCallTarget;
			if (!t->Present() || t->Captured || t->Surrendering || !Hostile(r, t)) continue;
			double d = (c->Pos - r->Pos).Length();
			if (d < bestKm)
			{
				bestKm = d;
				best = t;
			}
		}
		return best;
	}

	namespace
	{
		/// <summary>US-T342: whether <paramref name="o"/> is a badly damaged ship still fighting that a call to fall back moves.</summary>
		bool FallsBack(const cs::Ref<BattleUnit>& o)
		{
			return o->Kind == UnitKind::Ship && o->Combat != nullptr && !o->PlayerControlled() && o->Present() && !o->Captured
				&& !o->Surrendering && !o->Fleeing && !o->Combat->Disabled() && o->Combat->HullFraction() < Battle::FallBackHullFraction;
		}
	}

	int Battle::DamagedInReach(const cs::Ref<BattleUnit>& u)
	{
		if (u == nullptr) return 0;
		double reach = FleetCoordination::ReachKm()->Value;
		int n = 0;
		for (const auto& o : Units)
		{
			if (o == u || o->Side != u->Side || !FallsBack(o)) continue;
			if ((o->Pos - u->Pos).Length() > reach) continue;
			n++;
		}
		return n;
	}

	cs::String Battle::OrderFallBackCall(const cs::Ref<BattleUnit>& u, int& answering)
	{
		answering = 0;
		if (u == nullptr || u->Combat == nullptr) return "no hull at the helm";
		if (!u->Present() || u->Captured || u->Surrendering) return "the hull is out of the fight";
		answering = DamagedInReach(u);
		if (answering == 0) return cs::Cat("no badly damaged ship of ours within ", Km(FleetCoordination::ReachKm()->Value));
		u->FallBackCallUntil = Seconds + FallBackCallSeconds;
		return cs::String();
	}

	cs::Ref<BattleUnit> Battle::CalledFallBackFor(const cs::Ref<BattleUnit>& u)
	{
		if (u == nullptr || !FallsBack(u)) return nullptr;
		cs::Ref<BattleUnit> best = nullptr;
		double bestKm = FleetCoordination::ReachKm()->Value;
		for (const auto& c : Units)
		{
			if (c == u || Seconds >= c->FallBackCallUntil || c->Side != u->Side || !c->Present() || c->Captured || c->Surrendering) continue;
			double d = (c->Pos - u->Pos).Length();
			if (d <= bestKm)
			{
				bestKm = d;
				best = c;
			}
		}
		return best;
	}

	int Battle::MendableSystems(const cs::Ref<BattleUnit>& u, cs::String& worst) const
	{
		worst = cs::String();
		if (u == nullptr || u->Combat == nullptr) return 0;
		int n = 0;
		double worstHealth = cs::Double::MaxValue;
		for (const auto& s : u->Combat->Subsystems)
		{
			if (s->Destroyed() || s->Hp >= s->Def->MaxHp * Broadsides::FieldRepairCap) continue;
			n++;
			double health = s->Health();
			if (health < worstHealth)
			{
				worstHealth = health;
				worst = SubsystemDef::Name(s->Kind());
			}
		}
		return n;
	}

	cs::String Battle::OrderRushRepair(const cs::Ref<BattleUnit>& u, cs::String& worst)
	{
		worst = cs::String();
		if (u == nullptr || u->Combat == nullptr) return "no hull at the helm";
		if (!u->Present() || u->Captured || u->Surrendering) return "the hull is out of the fight";
		if (u->Broadside == nullptr) return "no damage control teams aboard";
		if (Broadsides::DamageControlRate()->Value <= 0) return "damage control is off (Damage control rate is 0)";
		if (u->Stunned > 0) return "the systems are knocked out";
		int teams = Broadsides::Teams(u);
		for (int bits = u->EvaHeldTeams; bits != 0; bits &= bits - 1) teams--;
		if (teams <= 0) return "no damage control team free (too few crew, or all out on the plating)";
		if (RushingRepairs(u)) return cs::Cat("the teams are already at it, ", cs::Fmt(u->RushRepairUntil - Seconds, "0"), " s left");
		if (MendableSystems(u, worst) == 0) return "nothing the teams can mend in the field";
		u->RushRepairUntil = Seconds + RushRepairSeconds;
		return cs::String();
	}

	namespace
	{
		/// <summary>US-T350: whether <paramref name="o"/> is a hull a call to hold fire holds (an AI hull still fighting; not a craft).</summary>
		bool HoldsOnCall(const cs::Ref<BattleUnit>& o)
		{
			return o->Kind != UnitKind::Fighter && o->Combat != nullptr && !o->PlayerControlled() && o->Present() && !o->Captured
				&& !o->Surrendering;
		}
	}

	int Battle::HoldersInReach(const cs::Ref<BattleUnit>& u)
	{
		if (u == nullptr) return 0;
		double reach = FleetCoordination::ReachKm()->Value;
		int n = 0;
		for (const auto& o : Units)
		{
			if (o == u || o->Side != u->Side || !HoldsOnCall(o)) continue;
			if ((o->Pos - u->Pos).Length() > reach) continue;
			n++;
		}
		return n;
	}

	cs::String Battle::OrderHoldFireCall(const cs::Ref<BattleUnit>& u, int& answering, bool& lifted)
	{
		answering = 0;
		lifted = false;
		if (u == nullptr || u->Combat == nullptr) return "no hull at the helm";
		if (Seconds < u->HoldFireCallUntil)
		{
			// Weapons free: the call is lifted now, whoever was holding on it.
			u->HoldFireCallUntil = 0;
			lifted = true;
			answering = HoldersInReach(u);
			return cs::String();
		}
		if (!u->Present() || u->Captured || u->Surrendering) return "the hull is out of the fight";
		answering = HoldersInReach(u);
		if (answering == 0) return cs::Cat("no hull of ours within ", Km(FleetCoordination::ReachKm()->Value));
		u->HoldFireCallUntil = Seconds + HoldFireCallSeconds;
		HoldFireStanding = true;
		return cs::String();
	}

	bool Battle::HeldFireByCall(const cs::Ref<BattleUnit>& u)
	{
		if (!HoldFireStanding || u == nullptr || !HoldsOnCall(u)) return false;
		double reach = FleetCoordination::ReachKm()->Value;
		for (const auto& c : Units)
		{
			if (c == u || Seconds >= c->HoldFireCallUntil || c->Side != u->Side || !c->Present() || c->Captured || c->Surrendering) continue;
			if ((c->Pos - u->Pos).Length() <= reach) return true;
		}
		return false;
	}

	bool Battle::HasDrives(const cs::Ref<BattleUnit>& target)
	{
		if (target == nullptr || target->Combat == nullptr) return false;
		for (const auto& s : target->Combat->Subsystems)
			if (s->Kind() == SubsystemKind::Engine && !s->Destroyed()) return true;
		return false;
	}

	cs::String Battle::OrderCrippleCall(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target, int& answering)
	{
		answering = 0;
		if (u == nullptr || u->Combat == nullptr) return "no hull at the helm";
		if (!InTheFight(u)) return "the hull is out of the fight";
		if (target == nullptr) return "lock a hull first (T)";
		if (!target->Present()) return cs::Cat("the ", target->Name(), " is gone");
		if (!Hostile(u, target)) return cs::Cat("the ", target->Name(), " is not hostile");
		if (target->Kind == UnitKind::Fighter) return "craft have no drives to aim at";
		if (target->Captured || target->Surrendering) return cs::Cat("the ", target->Name(), " has struck");
		if (!HasDrives(target)) return cs::Cat("the ", target->Name(), "'s drives are already shot away");
		double reach = FleetCoordination::ReachKm()->Value;
		for (const auto& o : Units)
		{
			if (o == u || o->Side != u->Side || !AnswersTargetCall(o)) continue;
			if ((o->Pos - u->Pos).Length() > reach) continue;
			o->RetargetIn = 0; // they pick again on their next thought, the call weighing in (PickTarget)
			answering++;
		}
		if (answering == 0) return cs::Cat("no ship of ours within ", Km(reach), " to answer");
		u->CrippleCallTarget = target;
		u->CrippleCallUntil = Seconds + CrippleCallSeconds;
		CrippleStanding = true;
		return cs::String();
	}

	cs::Ref<BattleUnit> Battle::CalledCrippleFor(const cs::Ref<BattleUnit>& u)
	{
		if (!CrippleStanding || u == nullptr) return nullptr;
		cs::Ref<BattleUnit> best = nullptr;
		double bestKm = FleetCoordination::ReachKm()->Value;
		for (const auto& c : Units)
		{
			if (c->CrippleCallTarget == nullptr || Seconds >= c->CrippleCallUntil || c->Side != u->Side || !c->Present()) continue;
			if (c->Captured || c->Surrendering) continue; // US-T379: a struck caller's call no longer steers its side
			auto t = c->CrippleCallTarget;
			if (!t->Present() || t->Captured || t->Surrendering) continue;
			double d = (c->Pos - u->Pos).Length();
			if (d <= bestKm)
			{
				bestKm = d;
				best = t;
			}
		}
		return best;
	}

	// US-T356: whether o, of u's side and within reach, would answer a call to withdraw (an AI ship still fighting).
	static bool AnswersWithdraw(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& o, double reach)
	{
		if (o == u || o->Side != u->Side || o->Kind != UnitKind::Ship || !o->Present() || o->PlayerControlled()) return false;
		if (o->Captured || o->Surrendering || o->Fleeing || o->Combat == nullptr || o->Combat->Disabled()) return false;
		return (o->Pos - u->Pos).Length() <= reach;
	}

	int Battle::WithdrawersInReach(const cs::Ref<BattleUnit>& u)
	{
		if (u == nullptr) return 0;
		double reach = FleetCoordination::ReachKm()->Value;
		int n = 0;
		for (const auto& o : Units)
			if (AnswersWithdraw(u, o, reach)) n++;
		return n;
	}

	int Battle::WithdrawingFor(const cs::Ref<BattleUnit>& u)
	{
		if (u == nullptr) return 0;
		int n = 0;
		for (const auto& o : Units)
			if (o->WithdrawCalledBy == u && o->Present() && o->Fleeing) n++;
		return n;
	}

	cs::String Battle::DemandSurrenderBlocked(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target)
	{
		if (u == nullptr || u->Combat == nullptr) return "no hull at the helm";
		if (!InTheFight(u)) return "the hull is out of the fight";
		if (target == nullptr) return "lock a hull first (T)";
		if (!target->Present()) return cs::Cat("the ", target->Name(), " is gone");
		if (!Hostile(u, target)) return cs::Cat("the ", target->Name(), " is not hostile");
		if (target->Kind != UnitKind::Ship || target->Combat == nullptr) return "only a ship's crew can strike";
		if (target->Captured || target->Surrendering) return cs::Cat("the ", target->Name(), " has already struck");
		if (target->PlayerControlled()) return cs::Cat("the ", target->Name(), " is a player's to strike");
		if (Seconds < u->DemandSurrenderNext) return cs::Cat("hail again in ", cs::Fmt(u->DemandSurrenderNext - Seconds, "0"), " s");
		return cs::String();
	}

	cs::String Battle::OrderDemandSurrender(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target, DemandAnswer& answer)
	{
		answer = DemandAnswer::Refused;
		auto why = DemandSurrenderBlocked(u, target);
		if (!why.empty()) return why;
		u->DemandSurrenderNext = Seconds + DemandSurrenderCooldownSeconds;
		auto outcome = DemandOutcome(target); // US-T382: the same weighing the target card previews
		if (outcome == DemandAnswer::Struck)
		{
			Surrender(target);
			answer = DemandAnswer::Struck;
			return cs::String();
		}
		if (outcome == DemandAnswer::Ran)
		{
			// Not beaten enough to strike, but enough to see the way the fight is going: they break off as a broken crew does.
			target->Fleeing = true;
			target->BoardTarget = nullptr;
			answer = DemandAnswer::Ran;
			return cs::String();
		}
		answer = DemandAnswer::Refused;
		return cs::String();
	}

	Battle::DemandAnswer Battle::DemandOutcome(const cs::Ref<BattleUnit>& target) const
	{
		// The crew weigh it as they weigh striking on their own (StrikesColours), the hail pressing a little harder.
		double toll = DamageTollOf(target);
		double outgunned = OutgunnedOf(target);
		double resolve = Morale::Resolve(target->Faction, target->Ship != nullptr && target->Ship->Spec->IsMilitary)
			* (target->Side >= 0 && target->Side < 2 ? Dread[target->Side] : 1) * Morale::SupplyResolve(target->Combat->Readiness);
		double pressure = Morale::Pressure(toll, outgunned, resolve) + DemandSurrenderPressure;
		if (toll >= Morale::MinDamageToStrike && Morale::ShouldSurrender(pressure, target->MoraleThreshold)) return DemandAnswer::Struck;
		if (!target->Fleeing && !target->Combat->Disabled() && (toll >= DemandRunToll || outgunned >= DemandRunOutgunned)) return DemandAnswer::Ran;
		return DemandAnswer::Refused;
	}

	bool Battle::OpenPrize(const cs::Ref<BattleUnit>& target)
	{
		return target != nullptr && target->Combat != nullptr && !target->Captured &&
			(target->Combat->Disabled() || target->SurrenderOpenToBoarding || target->Stunned > 10);
	}

	int Battle::BoardCallAnswer(const cs::Ref<BattleUnit>& o, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target)
	{
		if (o == u || o->Side != u->Side || o->Kind != UnitKind::Ship || o->Combat == nullptr || !o->Present()) return 0;
		if (o->PlayerControlled() || o->Captured || o->Surrendering || o->Fleeing || o->Combat->Disabled()) return 0;
		if ((o->Pos - u->Pos).Length() > FleetCoordination::ReachKm()->Value) return 0;
		if (!target->Combat->Interior->Admits(o->Faction, o->Side)) return 0;
		if (BoardsByShuttle(o))
		{
			// As the helm's own shuttle order (OrderShuttle) would have it: a working deck clear to launch, a shuttle fit to
			// fly, a team to spare and the prize within the shuttles' reach.
			if (!HangarWorks(o) || o->ShuttleLaunchIn > 0 || CarrierWings::ReadyShuttle(o->Combat) == nullptr) return 0;
			if (o->Combat->MarinesAvailable() < BoardingShuttles::TeamSize()->Value + BoardingShuttles::Reserve) return 0;
			if (!ShuttleMayBoard(o, target)) return 0;
			if ((target->Pos - o->Pos).Length() > BoardingShuttles::RangeKm()->Value) return 0;
			return 2;
		}
		// A hull that boards with its own (ChooseBoarding): a boarder with marines to send, not held in a flagship's formation
		// (which drops a boarding target), already on its way to this prize or free to take it.
		bool boarder = o->Faction->IsPirate || o->Faction->IsMilitary || (Boards != nullptr && Boards(o));
		if (!boarder || o->Combat->MarinesAvailable() < 15 || Broadsides::HoldsFormation(o)) return 0;
		if (o->Latched != nullptr || !MayBoard(o, target)) return 0;
		return 1;
	}

	cs::String Battle::BoardCallBlocked(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target, int& able)
	{
		able = 0;
		if (u == nullptr || u->Combat == nullptr) return "no hull at the helm";
		cs::String why = LockedPrize(*this, u, target);
		if (!why.empty()) return why;
		if (target->Kind != UnitKind::Ship || target->Combat == nullptr) return "only a ship can be boarded";
		if (!OpenPrize(target)) return cs::Cat("the ", target->Name(), " is still fighting: disable it, or have it strike, first");
		for (const auto& o : Units)
			if (BoardCallAnswer(o, u, target) != 0) able++;
		if (able == 0) return "no boarders of ours in reach (marines, a shuttle deck or boarding craft)";
		return cs::String();
	}

	cs::String Battle::OrderBoardCall(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target, int& answering)
	{
		answering = 0;
		int able = 0;
		auto why = BoardCallBlocked(u, target, able);
		if (!why.empty()) return why;
		auto all = Units.ToArray(); // a shuttle launched below joins Units
		for (const auto& o : all)
		{
			int how = BoardCallAnswer(o, u, target);
			if (how == 2)
			{
				auto hull = CarrierWings::ReadyShuttle(o->Combat);
				FighterAI::LaunchShuttle(shared_from_this(), o, target, o->Combat->Interior->TakeMarines(BoardingShuttles::TeamSize()->Value), hull);
				o->ShuttleLaunchIn = BoardingShuttles::LaunchSeconds;
				answering++;
			}
			else if (how == 1)
			{
				o->BoardTarget = target; // ChooseBoarding keeps it while the prize stays open; Move flies the approach
				answering++;
			}
		}
		return answering > 0 ? cs::String() : cs::String("no boarders of ours in reach");
	}

	cs::String Battle::OrderWithdrawCall(const cs::Ref<BattleUnit>& u, int& answering, bool& recalled)
	{
		answering = 0;
		recalled = false;
		if (u == nullptr || u->Combat == nullptr) return "no hull at the helm";
		if (WithdrawingFor(u) > 0)
		{
			// Called back: those still running turn to the fight again (their own crews may still choose to run, as ever).
			for (const auto& o : Units)
			{
				if (o->WithdrawCalledBy != u) continue;
				if (o->Present() && o->Fleeing)
				{
					o->Fleeing = false;
					o->RetargetIn = 0;
					answering++;
				}
				o->WithdrawCalledBy = nullptr;
			}
			recalled = true;
			return cs::String();
		}
		if (!u->Present() || u->Captured || u->Surrendering) return "the hull is out of the fight";
		double reach = FleetCoordination::ReachKm()->Value;
		for (const auto& o : Units)
		{
			if (!AnswersWithdraw(u, o, reach)) continue;
			o->Fleeing = true;
			o->BoardTarget = nullptr;
			o->WithdrawCalledBy = u;
			answering++;
		}
		if (answering == 0) return cs::Cat("no ship of ours still fighting within ", Km(reach));
		WithdrawStanding = true; // US-T360: they go together from the next move
		return cs::String();
	}

	bool Battle::WithdrawSteer(const cs::Ref<BattleUnit>& u, double maxSpeed, DVec3& desired)
	{
		if (!WithdrawStanding || u == nullptr || u->WithdrawCalledBy == nullptr || u->Combat == nullptr) return false;
		auto caller = u->WithdrawCalledBy;
		auto middle = DVec3::Zero;
		double harm = 0;
		double slowest = maxSpeed;
		int n = 0;
		for (const auto& o : Units)
		{
			if (o->WithdrawCalledBy != caller || !o->Present() || !o->Fleeing || o->Combat == nullptr) continue;
			middle = middle + o->Pos;
			harm += 1 - o->Combat->HullFraction();
			if (!o->Combat->Disabled())
				slowest = cs::Math::Min(slowest, o->Combat->Spec->MaxSpeed * (0.25 + 0.75 * o->Combat->EngineFactor()));
			n++;
		}
		if (n < 2) return false;
		middle = middle / n;
		harm /= n;
		auto away = middle - HostileCentroid(u);
		if (away.LengthSquared() < 1e-6) away = middle.LengthSquared() > 0 ? middle : DVec3::UnitX;
		auto way = away.Normalized();
		// The worse hurt than the group's middle lead toward the exit; the healthier hang back, between them and the enemy.
		double lead = ((1 - u->Combat->HullFraction()) - harm) * WithdrawCoverKm;
		auto slot = middle + way * lead;
		desired = way * (slowest * WithdrawPace) + (slot - u->Pos) * 0.05;
		return true;
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
