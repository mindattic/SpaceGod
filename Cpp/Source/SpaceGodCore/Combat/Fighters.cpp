#include "Combat/Fighters.h"
#include "Combat/Battle.h"
#include "Combat/BoardingShuttles.h"
#include "Combat/Boarding.h"
#include "Combat/CarrierWings.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/ElectronicWarfare.h"
#include "Combat/FlightDecks.h"
#include "Combat/FlightModels.h"
#include "Combat/Formations.h"
#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Combat/StrandedWings.h"
#include "Combat/WeakPatches.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "Astro.h"
#include <map>

namespace SpaceGod::Core
{
	using cs::Math::Max;
	using cs::Math::Min;

	const char* ToString(FighterPhase v)
	{
		switch (v)
		{
			case FighterPhase::Launch: return "Launch";
			case FighterPhase::Approach: return "Approach";
			case FighterPhase::AttackRun: return "AttackRun";
			case FighterPhase::BreakOff: return "BreakOff";
			case FighterPhase::Dogfight: return "Dogfight";
			case FighterPhase::Return: return "Return";
		}
		return "";
	}

	const cs::Ref<CombatSpec>& FighterAI::Interceptor()
	{
		static const cs::Ref<CombatSpec> spec = FighterCraft::Interceptor()->Spec;
		return spec;
	}

	const cs::Ref<CombatSpec>& FighterAI::BomberSpec()
	{
		static const cs::Ref<CombatSpec> spec = FighterCraft::Bomber()->Spec;
		return spec;
	}

	void FighterAI::Launch(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& carrier)
	{
		if (carrier == nullptr || carrier->Combat->Fighters() <= 0) return;
		int hangars = 0;
		for (const auto& s : carrier->Combat->Subsystems)
			if (s->Kind() == SubsystemKind::Hangar && !s->Destroyed()) hangars++;
		if (hangars == 0) return;
		auto spec = carrier->Combat->Spec;
		// Squadron by squadron in berth order; the recon flight, out ahead of the strike (U.SPC-T485, see
		// ElectronicWarfare), is launched last under the squadron number after the carrier's own, so it keeps to itself.
		// C# SortedDictionary<int, List<Ship>>: enumerates in ascending key order, as std::map does.
		std::map<int, cs::List<cs::Ref<Ship>>> squadrons;
		cs::List<cs::Ref<Ship>> recon;
		for (const auto& craft : *carrier->Combat->Hangar)
		{
			if (!CarrierWings::Flyable(craft)) continue;
			int q = 0, discardSlot = 0;
			CarrierWings::Berth(spec, craft->Craft->Berth, q, discardSlot);
			if (craft->Craft->Role == FighterRole::Recon) recon.Add(craft);
			else
			{
				q = cs::Math::Max(0, q);
				squadrons[q].Add(craft);
			}
		}
		DVec3 right, up;
		carrier->Basis(right, up);
		int next = 0;
		for (auto& kv : squadrons)
		{
			cs::Ref<BattleUnit> lead = nullptr;
			for (int k = 0; k < kv.second.Count(); k++)
				lead = LaunchCraft(b, carrier, kv.second[k], kv.first, k, lead, right, up, FighterPhase::Launch);
			next = kv.first + 1;
		}
		cs::Ref<BattleUnit> reconLead = nullptr;
		for (int k = 0; k < recon.Count(); k++)
			reconLead = LaunchCraft(b, carrier, recon[k], next, k, reconLead, right, up, FighterPhase::Approach);
	}

	cs::Ref<BattleUnit> FighterAI::LaunchCraft(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& carrier, const cs::Ref<Ship>& hull, int q, int k, const cs::Ref<BattleUnit>& lead, DVec3 right, DVec3 up, FighterPhase phase)
	{
		auto craft = FighterCraft::Of(hull->Craft->Role);
		// Out of the bow and side bays, fanning ahead of the carrier.
		auto bay = carrier->Combat->Subsystems.Find([](const cs::Ref<Subsystem>& s) { return s->Kind() == SubsystemKind::Hangar && !s->Destroyed(); });
		auto pos = carrier->PointOf(bay->Def)
			+ carrier->Forward * (0.3 + 0.15 * k) + right * ((q - 1) * 0.6 + (k % 2 == 0 ? 0.1 : -0.1) * k) + up * (0.05 * k);
		CarrierWings::Undock(carrier->Combat, hull);
		hull->Faction = carrier->Faction;
		auto u = b->NewUnit(UnitKind::Fighter, carrier->Side, carrier->Faction, hull->Combat, craft->LengthKm, pos, carrier->Forward);
		u->CraftHull = hull;
		hull->Craft->Carrier = carrier->Ship;
		u->Vel = carrier->Vel + carrier->Forward * 0.25;
		u->Power = cs::New<PowerDistribution>();
		auto pilot = CarrierWings::PilotOf(hull, b->Rng());
		pilot->Sorties++;
		auto f = cs::New<FighterState>();
		f->Carrier = carrier;
		f->Lead = lead;
		f->Squadron = q;
		f->Slot = k;
		f->Bomber = craft->Role == FighterRole::Bomber;
		f->Role = craft->Role;
		f->Phase = phase;
		f->Skill = pilot->Skill;
		f->Reaction = pilot->Reaction;
		f->Discipline = pilot->Discipline;
		f->Seed = pilot->Seed;
		u->Fighter = f;
		// Out through a flight deck and its bow mouth, in column with the rest of the launch (U.SPC-T518).
		FlightDecks::Catapult(b, carrier, u);
		return lead ? lead : u;
	}

	void FighterAI::Step(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt)
	{
		auto f = u->Fighter;
		// On the glide path into a flight deck, or being launched out of one, the deck flies it (U.SPC-T518).
		if (FlightDecks::Fly(b, u, dt)) return;
		// A boarding shuttle flies to its prize, not into the fight (U.SPC-T486).
		if (f->Role == FighterRole::AssaultShuttle)
		{
			Shuttle(b, u, dt);
			return;
		}
		// Its drives knocked out (an EMP): it drifts on the way it was going until they come back.
		if (u->Stunned > 0)
		{
			u->Pos = u->Pos + u->Vel * dt;
			return;
		}
		f->PhaseTime += dt;
		// Its carrier lost or gone (U.SPC-T473): it looks for another friendly carrier or a station to land on.
		StrandedWings::Step(b, u, dt);
		auto carrier = f->LandsOn();
		// Recalled: the shooting is over, the carrier is pulling out, or its helm has called the wing in (US-T106).
		if (carrier->Present() && !carrier->Captured && (b->Mopping || carrier->Fleeing || Recalled(u)))
		{
			if (f->Phase != FighterPhase::Return) Switch(f, FighterPhase::Return);
			Land(b, u, dt);
			return;
		}
		// Hurt or out of ordnance: home to the flight deck to rearm and repair, then out again (U.SPC-T518).
		if (FlightDecks::WantsService(b, u))
		{
			if (f->Phase != FighterPhase::Return) Switch(f, FighterPhase::Return);
			Land(b, u, dt);
			return;
		}
		// Recalled and then sent back in (the shooting started again): it turns back to the fight.
		if (f->Phase == FighterPhase::Return)
		{
			FlightDecks::Abort(u);
			Switch(f, FighterPhase::Approach);
		}
		// A lead gone home to its deck leaves the squadron to the next in line until it is back.
		if (f->Lead != nullptr && (!f->Lead->Present() || FlightDecks::Away(f->Lead))) f->Lead = NewLead(b, u);
		SharePower(b, u);
		if (f->Lead == nullptr)
		{
			// The carrier's helm may have called the wing onto its locked hull, or to guard it (US-T106); with no call
			// (every AI carrier's wing) the lead picks its own fight as before.
			bool guard = false;
			auto call = f->Carrier != nullptr && f->Carrier->WingCall != 0 ? HelmCall(b, u, guard) : nullptr;
			if (guard && call == nullptr)
			{
				// Nothing near the carrier: the squadron flies a guard circuit round it.
				auto c = f->Carrier;
				auto ring = c->Pos + DVec3(cs::Math::Cos(b->Seconds * 0.15 + f->Seed), cs::Math::Sin(b->Seconds * 0.15 + f->Seed), 0.15) * (c->Radius() + 2.5);
				u->Target = nullptr;
				Steer(b, u, ring, u->Combat->Spec->MaxSpeed * 0.7, dt);
				return;
			}
			Lead(b, u, dt, call);
		}
		else Wingman(b, u, dt);
	}

	bool FighterAI::Recalled(const cs::Ref<BattleUnit>& u)
	{
		auto f = u != nullptr ? u->Fighter : nullptr;
		return f != nullptr && f->Carrier != nullptr && f->Carrier->WingRecalled && f->LandsOn() == f->Carrier && f->Role != FighterRole::AssaultShuttle;
	}

	cs::Ref<BattleUnit> FighterAI::HelmCall(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, bool& guard)
	{
		auto f = u->Fighter;
		auto c = f->Carrier;
		guard = c->WingCall == 2;
		if (c->WingCall == 1)
		{
			auto t = c->PlayerLockTarget;
			if (!Fair(b, u, t) || !t->Present() || (f->Bomber && t->Kind == UnitKind::Fighter)) return nullptr;
			return t;
		}
		if (!guard || !c->Present()) return nullptr;
		cs::Ref<BattleUnit> best = nullptr;
		double bestD = c->Radius() + GuardKm;
		for (const auto& o : b->Units)
		{
			if (!o->Present() || !Fair(b, u, o) || !o->Combat->Armed() || (f->Bomber && o->Kind == UnitKind::Fighter)) continue;
			double d = (o->Pos - c->Pos).Length();
			if (d < bestD)
			{
				bestD = d;
				best = o;
			}
		}
		return best;
	}

	cs::List<cs::Ref<BattleUnit>> FighterAI::LaunchOrdered(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& carrier, FighterRole role)
	{
		cs::List<cs::Ref<BattleUnit>> out;
		if (b == nullptr || carrier == nullptr || carrier->Combat == nullptr || carrier->Combat->Hangar == nullptr || !carrier->Present()) return out;
		bool bay = false;
		for (const auto& s : carrier->Combat->Subsystems)
			if (s->Kind() == SubsystemKind::Hangar && !s->Destroyed()) bay = true;
		if (!bay) return out;
		auto spec = carrier->Combat->Spec;
		// The squadron that goes: the first, in berth order, with a craft of this class aboard fit to fly.
		int pick = 0;
		bool any = false;
		for (const auto& craft : *carrier->Combat->Hangar)
		{
			if (craft->Craft->Role != role || !CarrierWings::Flyable(craft)) continue;
			int q = 0, discardSlot = 0;
			CarrierWings::Berth(spec, craft->Craft->Berth, q, discardSlot);
			if (!any || q < pick) pick = q;
			any = true;
		}
		if (!any) return out;
		cs::List<cs::Ref<Ship>> group;
		for (const auto& craft : *carrier->Combat->Hangar)
		{
			if (craft->Craft->Role != role || !CarrierWings::Flyable(craft)) continue;
			int q = 0, discardSlot = 0;
			CarrierWings::Berth(spec, craft->Craft->Berth, q, discardSlot);
			if (q == pick) group.Add(craft);
		}
		// As Launch numbers them: the recon flight flies under the squadron number after the carrier's own.
		int squadron = role == FighterRole::Recon
			? (Max(0, spec->HangarCraft() - Max(0, spec->ReconCraft)) + SquadronSize - 1) / SquadronSize
			: Max(0, pick);
		auto phase = role == FighterRole::Recon ? FighterPhase::Approach : FighterPhase::Launch;
		DVec3 right, up;
		carrier->Basis(right, up);
		cs::Ref<BattleUnit> lead = nullptr;
		for (int k = 0; k < group.Count(); k++)
		{
			auto hull = group[k];
			// Landed in this fight and struck down into the hangar: the same unit goes out again.
			cs::Ref<BattleUnit> u = nullptr;
			for (const auto& o : b->Units)
				if (o->CraftHull == hull && !o->Dead && o->Fighter != nullptr) u = o;
			if (u != nullptr)
			{
				auto f = u->Fighter;
				if (auto v = f->Deck)
				{
					if (v->Deck != nullptr)
					{
						v->Deck->Aboard.Remove(u);
						if (v->Deck->Final == u) v->Deck->Final = nullptr;
					}
					f->Deck = nullptr;
				}
				CarrierWings::Undock(carrier->Combat, hull);
				hull->Craft->Carrier = carrier->Ship;
				u->Docked = false;
				u->Target = nullptr;
				f->Carrier = carrier;
				f->Home = nullptr;
				f->Stranded = false;
				f->Lead = lead;
				f->Squadron = squadron;
				f->Slot = k;
				f->Order = WingOrder::FormUp;
				f->Phase = phase;
				f->PhaseTime = 0;
				f->RunClosest = cs::Double::MaxValue;
				f->RunEntry = DVec3::Zero;
				u->Pos = carrier->Pos + carrier->Forward * (carrier->Radius() + 0.3 + 0.15 * k);
				u->Vel = carrier->Vel + carrier->Forward * 0.25;
				u->Forward = carrier->Forward;
				FlightDecks::Catapult(b, carrier, u);
				if (lead == nullptr) lead = u;
			}
			else
			{
				lead = LaunchCraft(b, carrier, hull, squadron, k, lead, right, up, phase);
				u = b->Units[b->Units.Count() - 1];
			}
			out.Add(u);
		}
		return out;
	}

	void FighterAI::SharePower(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		auto p = u->Power;
		if (p == nullptr) return;
		auto f = u->Fighter;
		auto lead = f->Lead != nullptr && f->Order == WingOrder::FormUp ? f->Lead->Fighter : f;
		double shields = u->Combat->Spec->Shield > 0 ? u->Combat->Shield() / u->Combat->Spec->Shield : 1;
		if (shields < 0.35 || b->MissileInbound(u, Countermeasures::ReachKm) != nullptr) p->Set(1, 4, 1);
		else if (lead->Phase == FighterPhase::Return || lead->Phase == FighterPhase::BreakOff) p->Set(1, 1, 4);
		else if (lead->Phase == FighterPhase::AttackRun || lead->Phase == FighterPhase::Dogfight) p->Set(4, 1, 1);
		else p->Balance();
	}

	cs::Ref<BattleUnit> FighterAI::NewLead(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		cs::Ref<BattleUnit> best = nullptr;
		for (const auto& o : b->Units)
		{
			if (!o->Present() || o->Kind != UnitKind::Fighter || o->Fighter->Squadron != u->Fighter->Squadron || o->Fighter->Carrier != u->Fighter->Carrier
				|| BoardingShuttles::Is(o) || (o != u && FlightDecks::Away(o))) continue;
			if (best == nullptr || o->Fighter->Slot < best->Fighter->Slot) best = o;
		}
		if (best == nullptr || best == u) return nullptr;
		return best;
	}

	// ---------------------------------------------------------------- the lead

	bool FighterAI::Fair(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t)
	{
		return t != nullptr && b->Hostile(u, t) && !t->Surrendering && !t->Captured;
	}

	void FighterAI::Lead(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt, const cs::Ref<BattleUnit>& pinned)
	{
		auto f = u->Fighter;
		auto& rng = b->Rng();
		// A recon fighter holds off its mark, jamming and painting, instead of flying runs (U.SPC-T485).
		if (f->Role == FighterRole::Recon)
		{
			Recon(b, u, dt, pinned);
			return;
		}
		if (pinned != nullptr)
		{
			if (u->Target != pinned)
			{
				u->Target = pinned;
				if (f->Phase == FighterPhase::Dogfight || f->Phase == FighterPhase::AttackRun) Switch(f, FighterPhase::Approach);
			}
		}
		else
		{
			// A target that has struck its colours or changed hands is no longer one.
			bool lost = !Fair(b, u, u->Target);
			if (lost || (f->Phase != FighterPhase::AttackRun && rng.NextDouble() < dt / 20))
			{
				u->Target = ChooseTarget(b, u);
				if (f->Phase == FighterPhase::Dogfight || f->Phase == FighterPhase::AttackRun) Switch(f, FighterPhase::Approach);
			}
		}
		auto t = u->Target;
		DVec3 goal;
		double speed = u->Combat->Spec->MaxSpeed;
		u->ManeuverLeft -= dt;
		// US-T164: a run or a trick belongs to the phase it was flown in; anywhere else it starts afresh.
		if (f->Phase != FighterPhase::AttackRun && Choreography::Strafing(u)) Choreography::EndRun(u);

		switch (f->Phase)
		{
			case FighterPhase::Launch:
				goal = f->Carrier->Pos + f->Carrier->Forward * 4 + DVec3::UnitZ * 0.6;
				if (f->PhaseTime > 7) Switch(f, FighterPhase::Approach);
				break;

			case FighterPhase::Approach:
				if (t == nullptr)
				{
					goal = (f->LandsOn()->Present() ? f->LandsOn()->Pos : DVec3::Zero) + DVec3(cs::Math::Cos(b->Seconds * 0.1 + f->Seed), cs::Math::Sin(b->Seconds * 0.1 + f->Seed), 0.2) * 3;
					speed *= 0.6;
					break;
				}
				if (t->Kind == UnitKind::Fighter)
				{
					Switch(f, FighterPhase::Dogfight);
					goal = t->Pos;
					break;
				}
				// US-T164: a fighter's strafing run lines itself up (Choreography::StrafeRun's burn), straight from here.
				if (!f->Bomber && Choreography::Strafeable(t))
				{
					Switch(f, FighterPhase::AttackRun);
					goal = t->Pos;
					break;
				}
				if (f->RunEntry.LengthSquared() == 0 || f->PhaseTime < dt * 1.5) f->RunEntry = PickEntry(b, u, t);
				goal = t->Pos + f->RunEntry;
				if ((u->Pos - goal).Length() < 1.0 || f->PhaseTime > 60) Switch(f, FighterPhase::AttackRun);
				break;

			case FighterPhase::AttackRun:
			{
				if (t == nullptr)
				{
					Switch(f, FighterPhase::Approach);
					goal = u->Pos + u->Forward;
					break;
				}
				// US-T164: a fighter on a capital flies the strafing run: burn in, coast down the hull's length with the
				// drive cut and the guns on it, flip and burn out; then round again from a fresh entry.
				if (!f->Bomber && Choreography::Strafeable(t))
				{
					auto run = Choreography::StrafeRun(b, u, t, u->Combat->Spec->TurnRateDegS, dt);
					FlyRun(b, u, run, speed, dt);
					if (!Choreography::Strafing(u) || f->PhaseTime > 90)
					{
						Choreography::EndRun(u);
						f->RunEntry = DVec3::Zero;
						Switch(f, FighterPhase::Approach);
					}
					Torpedo(b, u);
					return;
				}
				// Dive at the target, aiming to pass a few hundred metres off its hull.
				auto to = t->Pos - u->Pos;
				double dist = to.Length();
				auto side = DVec3::Cross(to, DVec3::UnitZ);
				if (side.LengthSquared() < 1e-9) side = DVec3::UnitX;
				goal = t->Pos + side.Normalized() * (t->Radius() + 0.35) * cs::Math::Sign(cs::Math::Sin(f->Seed)) + t->Vel * (dist / speed);
				if (dist < f->RunClosest) f->RunClosest = dist;
				// Flying away only means 'past' once the run has closed; at its entry 5-7 km out it is also moving outward.
				bool past = f->RunClosest < t->Radius() + 0.8 && DVec3::Dot(u->Vel, to) < 0;
				if ((past && dist > t->Radius() + 0.8) || f->PhaseTime > 45)
				{
					auto away = (u->Pos - t->Pos).Normalized();
					// C# evaluates the two draws left to right; C++ leaves operand order unspecified, so they are sequenced here.
					double r1 = rng.NextDouble();
					double r2 = rng.NextDouble();
					f->BreakPoint = t->Pos + (away + DVec3::UnitZ * (r1 - 0.3)).Normalized() * (7 + r2 * 3);
					Switch(f, FighterPhase::BreakOff);
				}
				break;
			}

			case FighterPhase::BreakOff:
				goal = f->BreakPoint;
				if ((u->Pos - goal).Length() < 1.2 || f->PhaseTime > 35)
				{
					f->RunEntry = DVec3::Zero;
					Switch(f, FighterPhase::Approach);
				}
				break;

			default:
			{
				// Dogfight: pursue with lead, jinking a little.
				if (t == nullptr || t->Kind != UnitKind::Fighter)
				{
					Switch(f, FighterPhase::Approach);
					goal = u->Pos + u->Forward;
					break;
				}
				// US-T164: through the merge a loop back onto it, and now and then a rolling break.
				Choreography::Flight trick;
				if (Choreography::FighterTurn(b, u, t, u->Combat->Spec->TurnRateDegS, dt, trick))
				{
					FlyRun(b, u, trick, speed, dt);
					Torpedo(b, u);
					return;
				}
				double dist = (t->Pos - u->Pos).Length();
				auto jink = DVec3(cs::Math::Sin(b->Seconds * 1.7 + f->Seed), cs::Math::Sin(b->Seconds * 1.3 + f->Seed * 2), cs::Math::Sin(b->Seconds * 0.9 + f->Seed * 3)) * 0.15;
				goal = t->Pos + t->Vel * Min(dist / speed, 3.0) + jink;
				break;
			}
		}
		Steer(b, u, goal, speed, dt);
		Choreography::Bank(u, 0, dt);
		Torpedo(b, u);
	}

	void FighterAI::FlyRun(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const Choreography::Flight& run, double speed, double dt)
	{
		if (run.Coast)
		{
			// Drive cut: the nose swings onto the hull while the craft falls on down its length on the way it had.
			Battle::TurnToward(u, run.Goal, u->Combat->Spec->TurnRateDegS, dt);
			u->Pos = u->Pos + u->Vel * dt;
		}
		else
		{
			auto dir = run.Goal.LengthSquared() > 1e-12 ? run.Goal.Normalized() : u->Forward;
			Steer(b, u, u->Pos + dir * 10, speed * run.Throttle * (run.Boost ? BurnBoost : 1), dt);
		}
		Choreography::Bank(u, run.Roll * RollDegS, dt);
	}

	void FighterAI::Recon(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt, const cs::Ref<BattleUnit>& pinned)
	{
		auto f = u->Fighter;
		if (f->Phase != FighterPhase::Approach) Switch(f, FighterPhase::Approach);
		if (pinned != nullptr && pinned->Kind != UnitKind::Fighter) u->Target = pinned;
		else if (!Fair(b, u, u->Target) || b->Rng().NextDouble() < dt / 20) u->Target = ElectronicWarfare::ChooseMark(b, u);
		auto t = u->Target;
		double speed = u->Combat->Spec->MaxSpeed;
		if (t == nullptr)
		{
			auto home = f->LandsOn()->Present() ? f->LandsOn()->Pos : DVec3::Zero;
			Steer(b, u, home + DVec3(cs::Math::Cos(b->Seconds * 0.1 + f->Seed), cs::Math::Sin(b->Seconds * 0.1 + f->Seed), 0.2) * 3, speed * 0.6, dt);
			return;
		}
		auto from = u->Pos - t->Pos;
		if (from.LengthSquared() < 1e-12) from = -t->Forward;
		auto dir = from.Normalized();
		auto round = DVec3::Cross(dir, DVec3::UnitZ);
		if (round.LengthSquared() < 1e-9) round = DVec3::UnitX;
		// Edging round the mark, so its gunners never have a steady line on it.
		dir = (dir + round.Normalized() * (0.15 * (cs::Math::Sin(f->Seed) >= 0 ? 1 : -1))).Normalized();
		auto station = t->Pos + dir * (ElectronicWarfare::StandoffKm + t->Radius()) + t->Vel * 2;
		bool there = (station - u->Pos).Length() < 3;
		Steer(b, u, station, speed, dt, there ? cs::Nullable<DVec3>(t->Vel) : cs::Nullable<DVec3>());
	}

	cs::Ref<BattleUnit> FighterAI::ChooseTarget(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		if (u->Fighter->Role == FighterRole::Recon) return ElectronicWarfare::ChooseMark(b, u);
		bool bomber = u->Fighter->Bomber;
		auto craft = FighterCraft::Of(u->Fighter->Role);
		cs::Ref<BattleUnit> best = nullptr;
		double bestScore = cs::Double::MaxValue;
		for (const auto& o : b->Units)
		{
			if (!b->Hostile(u, o) || o->Surrendering || o->Captured) continue;
			if (o->Kind == UnitKind::Fighter && bomber) continue;
			double d = (o->Pos - u->Pos).Length();
			double score = d;
			if (o->Kind == UnitKind::Fighter)
			{
				double hunt = craft->HuntsFightersKm;
				score = d < 9 + hunt ? d - hunt : d + 10;
				if (craft->ScreensBombers && OnABomber(b, u, o)) score -= ScreenBonusKm;
			}
			else if (o->Ship != nullptr && !o->Ship->Spec->IsMilitary) score += 8;
			else
			{
				score -= o->Combat->Spec->Hull / 3000.0;
				if (bomber && WeakPoints::IsCapital(o) && StrikeTorpedoes::ShieldDownToward(o, u->Pos)) score -= BareHullBonusKm;
				// A capital its own recon fighters are painting is where its torpedoes run truest (U.SPC-T485).
				if (bomber && ElectronicWarfare::Painted(b, o, u->Side)) score -= ElectronicWarfare::PaintBonusKm;
			}
			// US-T180: the fighters, heavy fighters and bombers go for the hulls, so the capitals are swarmed.
			score -= Choreography::TargetBias(u, o);
			score *= 0.8 + b->Rng().NextDouble() * 0.4;
			if (score < bestScore)
			{
				bestScore = score;
				best = o;
			}
		}
		return best;
	}

	bool FighterAI::OnABomber(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& o)
	{
		for (const auto& f : b->Units)
			if (f->Present() && f->Kind == UnitKind::Fighter && f->Side == u->Side && f->Fighter->Bomber && (f->Pos - o->Pos).Length() < ScreenKm) return true;
		return false;
	}

	DVec3 FighterAI::PickEntry(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t)
	{
		// A bomber lines its run up on the side a weak point lies open from (U.SPC-T424): under the reactor's vent,
		// astern of the engines, over the shield emitters.
		if (u->Fighter->Bomber && WeakPoints::AiGoesForThem()->Value)
		{
			if (auto weak = WeakPoints::Preferred(t))
			{
				auto axis = WeakPoints::Axis(t, WeakPoints::SpecOf(weak->Kind()));
				// C#'s constructor arguments are drawn left to right; sequenced here.
				double jx = b->Rng().NextDouble() - 0.5;
				double jy = b->Rng().NextDouble() - 0.5;
				double jz = b->Rng().NextDouble() - 0.5;
				auto jitter = DVec3(jx, jy, jz) * 0.5;
				return (axis + jitter).Normalized() * (5 + b->Rng().NextDouble() * 2);
			}
		}
		auto from = (u->Pos - t->Pos).Normalized();
		auto side = DVec3::Cross(from, DVec3::UnitZ);
		if (side.LengthSquared() < 1e-9) side = DVec3::UnitX;
		double swing = (b->Rng().NextDouble() - 0.5) * 2.2;
		auto dir = (from * cs::Math::Cos(swing) + side.Normalized() * cs::Math::Sin(swing) + DVec3::UnitZ * (b->Rng().NextDouble() - 0.4) * 0.6).Normalized();
		return dir * (5 + b->Rng().NextDouble() * 2);
	}

	void FighterAI::Switch(const cs::Ref<FighterState>& f, FighterPhase p)
	{
		f->Phase = p;
		f->PhaseTime = 0;
		f->RunClosest = cs::Double::MaxValue;
	}

	void FighterAI::Torpedo(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		auto f = u->Fighter;
		// Only a lead changes phase; a wingman in the vee flies (and fires on) its lead's run, one fighting on its own its own run.
		auto phase = f->Lead != nullptr && FlysInFormation(b, u) ? f->Lead->Fighter->Phase : f->Phase;
		// The lead's own retarget check runs once a tick and can lag a target's death or
		// surrender by a tick (both are resolved later in Battle.Step, after fighters fly);
		// re-check the target's own state here so neither the lead nor a wingman copying its
		// target gets a last torpedo off at a hull that is already struck or already gone.
		if (!f->Bomber || phase != FighterPhase::AttackRun || u->Target == nullptr || u->Target->Kind == UnitKind::Fighter
			|| !b->Hostile(u, u->Target) || u->Target->Surrendering || u->Target->Captured) return;
		auto to = u->Target->Pos - u->Pos;
		double dist = to.Length();
		if (dist > WeaponSpec::Torpedo()->RangeKm || DVec3::Dot(u->Forward, to / dist) < TorpedoBoresightCos) return;
		LaunchTorpedoes(b, u, u->Target);
	}

	cs::Ref<Subsystem> FighterAI::TorpedoAimPoint(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target)
	{
		if (u->PlayerControlled())
		{
			auto picked = u->PlayerSubTarget;
			if (target == u->PlayerLockTarget && picked != nullptr && !picked->Destroyed() && target->Combat->Subsystems.Contains(picked)) return picked;
		}
		else if (WeakPoints::AiGoesForThem()->Value)
		{
			if (auto open = WeakPoints::BestFrom(target, u->Pos)) return open;
		}
		return b->PickSubsystem(target, u);
	}

	void FighterAI::LaunchTorpedoes(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& target)
	{
		for (const auto& s : u->Combat->Subsystems)
		{
			if (s->Kind() != SubsystemKind::Torpedo || s->Destroyed() || s->Ammo <= 0 || s->Cooldown > 0) continue;
			s->Ammo--;
			s->Cooldown = s->Def->Weapon->Cooldown;
			auto sub = TorpedoAimPoint(b, u, target);
			auto m = cs::New<Missile>();
			m->Pos = u->Pos;
			m->Vel = u->Vel;
			m->Target = target;
			m->TargetSub = sub;
			m->Damage = s->Def->Weapon->Damage;
			m->Speed = s->Def->Weapon->MissileSpeedKmS + u->Vel.Length();
			m->Hp = 25;
			m->Life = 30;
			m->Side = u->Side;
			m->Torpedo = true;
			m->Kind = MissileKind::Torpedo;
			m->SmallCraft = true;
			b->Missiles.Add(m);
			b->Emit(FxKind::MissileLaunch, u->Pos, u->Pos + u->Forward, 0, u->Side, u, s->Index);
		}
	}

	// ---------------------------------------------------------------- flown by the player (U.SPC-T422)

	void FighterAI::FlyManned(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<FighterControl>& stick, double dt)
	{
		// The lock is its target: wingmen in the vee fire on it, and told to, go after it.
		if (u->PlayerLockTarget != nullptr && !u->PlayerLockTarget->Present())
		{
			u->PlayerLockTarget = nullptr;
			u->PlayerSubTarget = nullptr;
		}
		u->Target = u->PlayerLockTarget;
		// The second trigger fires the group picked (Battle::SecondaryFires): homing or hive missiles on the lock, rockets
		// straight off; a bomber's torpedoes go below (US-T101).
		u->PlayerSecondaryHeld = stick->Secondary && stick->SecondaryGroup != WeaponGroup::Torpedoes;
		// Lined up on a friendly flight deck's glide path, the deck takes it in, turns it round and launches it (U.SPC-T518).
		if (FlightDecks::FlyManned(b, u, stick, dt)) return;
		if (u->Stunned > 0)
		{
			u->Pos = u->Pos + u->Vel * dt;
			return;
		}
		auto craft = u->Fighter != nullptr ? FighterCraft::Of(u->Fighter->Role) : FighterCraft::Interceptor();
		double engines = PowerDistribution::FactorOf(u, PowerSystem::Engines);
		auto hull = HullDynamics::Of(u->Combat->Spec);
		hull = HullDynamics(hull.MassKt, hull.MainAccel * engines, hull.RetroAccel * engines, hull.LateralAccel * engines, hull.VerticalAccel * engines,
			hull.PitchYawAngularAccel, hull.RollAngularAccel);
		PilotState state;
		state.Pos = u->Pos;
		state.Vel = u->Vel;
		state.Forward = u->Forward;
		state.Up = stick->Up.LengthSquared() > 1e-12 ? stick->Up : PilotState::LevelUp(u->Forward);
		state.AngularVelocity = stick->AngularVelocity;
		PilotInput input;
		input.DesiredForward = stick->Aim.LengthSquared() > 1e-12 ? stick->Aim.Normalized() : DVec3::Zero;
		input.Throttle = Max(0.0, Min(1.0, stick->Throttle));
		input.StrafeRight = stick->StrafeRight;
		input.StrafeUp = stick->StrafeUp;
		input.Roll = stick->Roll;
		input.Boost = stick->Boost;
		input.InCombat = true;
		input.FlightAssistOff = stick->FlightAssistOff;
		// The player's own hands (US-T87): the player-facing handling, never a crew's. With assist off W and S are raw
		// thrust on the main drive and the retros, and the flip key swings the nose onto the retrograde.
		input.Piloted = true;
		// Power to the engines refills the boost faster too (US-T94); speed and thrust already scale with it above.
		input.BoostRecharge = engines;
		if (stick->FlightAssistOff) input.Throttle = Max(-1.0, Min(1.0, stick->Burn));
		// Drift (US-T165): held, the craft coasts and turns free onto the aim; let go, a boost-out along the new facing.
		stick->Drift.Update(stick->DriftHeld, dt);
		stick->Drift.Apply(input);
		if (stick->Flip && !input.Drift) input.DesiredForward = PilotFlight::FlipAim(state);
		auto tuning = craft->Tuning(engines);
		Drift::Fly(craft->Model(), state, u->Combat, hull, tuning, input, DVec3::Zero, dt);
		stick->Note = stick->FlightAssistOff ? PilotFlight::Brake(state, hull, tuning.TurnRateDegS).Text() : cs::String();
		if (input.Drift) stick->Note = "DRIFT: coasting on your way, the craft turns free on its thrusters; let go of R to boost out along the nose";
		u->Pos = state.Pos;
		u->Vel = state.Vel;
		u->Forward = state.Forward;
		stick->Up = state.Up;
		u->Up = state.Up;
		stick->AngularVelocity = state.AngularVelocity;
		if (stick->Secondary && stick->SecondaryGroup == WeaponGroup::Torpedoes)
		{
			if (auto target = TorpedoTarget(b, u)) LaunchTorpedoes(b, u, target);
		}
	}

	cs::Ref<BattleUnit> FighterAI::TorpedoTarget(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		auto locked = u->PlayerLockTarget;
		if (locked != nullptr && locked->Present() && locked->Kind != UnitKind::Fighter && Fair(b, u, locked))
		{
			auto toLocked = locked->Pos - u->Pos;
			double d = toLocked.Length();
			if (d > 1e-9 && d - locked->Radius() <= WeaponSpec::Torpedo()->RangeKm && DVec3::Dot(u->Forward, toLocked / d) >= TorpedoBoresightCos) return locked;
		}
		cs::Ref<BattleUnit> best = nullptr;
		double bestCos = TorpedoBoresightCos;
		for (const auto& o : b->Units)
		{
			if (!o->Present() || o->Kind == UnitKind::Fighter || !b->Hostile(u, o) || o->Surrendering || o->Captured) continue;
			auto to = o->Pos - u->Pos;
			double dist = to.Length();
			if (dist < 1e-9 || dist - o->Radius() > WeaponSpec::Torpedo()->RangeKm) continue;
			double cos = DVec3::Dot(u->Forward, to / dist);
			if (cos >= bestCos)
			{
				bestCos = cos;
				best = o;
			}
		}
		return best;
	}

	void FighterAI::Land(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt)
	{
		auto carrier = u->Fighter->LandsOn();
		// A hull with a flight deck open has it fly the approach (U.SPC-T518): to the gate astern of the deck, holding
		// off while the deck is busy, then cleared onto the glide path and in through the stern mouth.
		auto flightDeck = FlightDecks::Assign(u);
		if (flightDeck != nullptr)
		{
			if (FlightDecks::TryFinal(b, u, flightDeck, dt))
			{
				FlightDecks::Fly(b, u, dt);
				return;
			}
			auto gate = FlightDecks::InboundGoal(b, u, flightDeck);
			double lead = Min(5.0, (gate - u->Pos).Length() / Max(1e-6, u->Combat->Spec->MaxSpeed));
			Steer(b, u, gate + carrier->Vel * lead, u->Combat->Spec->MaxSpeed, dt, carrier->Vel, /*homing:*/ true);
			return;
		}
		cs::Ref<Subsystem> bay = nullptr;
		for (const auto& s : carrier->Combat->Subsystems)
			if (s->Kind() == SubsystemKind::Hangar && !s->Destroyed()) bay = s;
		auto deck = bay != nullptr ? carrier->PointOf(bay->Def) : carrier->Pos;
		// Line up astern of the bay and come in matched to the carrier.
		auto approach = deck - carrier->Forward * 0.4;
		double toDeck = (deck - u->Pos).Length();
		auto goal = toDeck < 0.6 ? deck : approach;
		Steer(b, u, goal + carrier->Vel * 2, u->Combat->Spec->MaxSpeed, dt, carrier->Vel, /*homing:*/ true);
		if (toDeck < Max(0.12, u->Vel.Length() * dt * 1.5))
		{
			u->Docked = true;
			TakeAboard(carrier, u);
		}
	}

	void FighterAI::TakeAboard(const cs::Ref<BattleUnit>& home, const cs::Ref<BattleUnit>& craft)
	{
		if ((home ? home->Combat : nullptr) == nullptr) return;
		if (!BoardingShuttles::Is(craft))
		{
			if (craft->CraftHull != nullptr) CarrierWings::Dock(home->Combat, craft->CraftHull);
			else home->Combat->RestoreFighters(1);
			return;
		}
		if (craft->CraftHull != nullptr) CarrierWings::Dock(home->Combat, craft->CraftHull);
		else home->Combat->RestoreShuttles(1);
		double team = craft->Combat->Interior->TakeMarines(craft->Combat->Interior->Marines());
		if (team > 0) home->Combat->Interior->Embark(0, team);
	}

	// ---------------------------------------------------------------- boarding shuttles (U.SPC-T486)

	cs::Ref<BattleUnit> FighterAI::LaunchShuttle(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& carrier, const cs::Ref<BattleUnit>& target, double team, cs::Ref<Ship> hull)
	{
		auto craft = FighterCraft::AssaultShuttle();
		if (!hull)
		{
			hull = CarrierWings::ReadyShuttle(carrier->Combat);
			if (!hull) hull = CarrierWings::NewCraft(FighterRole::AssaultShuttle, -1);
		}
		CarrierWings::Undock(carrier->Combat, hull);
		hull->Faction = carrier->Faction;
		hull->Craft->Carrier = carrier->Ship;
		cs::Ref<Subsystem> bay = nullptr;
		for (const auto& s : carrier->Combat->Subsystems)
			if (s->Kind() == SubsystemKind::Hangar && !s->Destroyed()) bay = s;
		auto pos = (bay != nullptr ? carrier->PointOf(bay->Def) : carrier->Pos) + carrier->Forward * 0.3;
		auto u = b->NewUnit(UnitKind::Fighter, carrier->Side, carrier->Faction, hull->Combat, craft->LengthKm, pos, carrier->Forward);
		u->CraftHull = hull;
		u->Vel = carrier->Vel + carrier->Forward * 0.1;
		u->Power = cs::New<PowerDistribution>();
		auto pilot = CarrierWings::PilotOf(hull, b->Rng());
		pilot->Sorties++;
		auto f = cs::New<FighterState>();
		f->Carrier = carrier;
		f->Squadron = -1;
		f->Role = craft->Role;
		f->Phase = FighterPhase::Launch;
		f->Drill = carrier->Combat->Interior->DrillFactor();
		f->Skill = pilot->Skill;
		f->Reaction = pilot->Reaction;
		f->Discipline = pilot->Discipline;
		f->Seed = pilot->Seed;
		u->Fighter = f;
		u->BoardTarget = target;
		if (team > 0) u->Combat->Interior->Embark(0, team);
		FlightDecks::Catapult(b, carrier, u);
		return u;
	}

	void FighterAI::Shuttle(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt)
	{
		auto f = u->Fighter;
		if (u->Latched != nullptr)
		{
			u->Pos = u->Latched->PointLocal(u->LatchLocal);
			u->Vel = u->Latched->Vel;
			return;
		}
		if (u->Stunned > 0)
		{
			u->Pos = u->Pos + u->Vel * dt;
			return;
		}
		f->PhaseTime += dt;
		StrandedWings::Step(b, u, dt);
		auto home = f->LandsOn();
		bool recalled = home->Present() && home->Fleeing;
		if (u->BoardTarget != nullptr && (recalled || !b->ShuttleMayBoard(u, u->BoardTarget))) u->BoardTarget = nullptr;
		if (u->BoardTarget == nullptr && !recalled && u->Combat->Interior->Marines() >= 0.5) u->BoardTarget = b->ShuttleTarget(u);
		if (u->BoardTarget == nullptr)
		{
			if (f->Phase != FighterPhase::Return) Switch(f, FighterPhase::Return);
			Land(b, u, dt);
			return;
		}
		if (f->Phase != FighterPhase::Approach) Switch(f, FighterPhase::Approach);
		Dock(b, u, u->BoardTarget, dt);
	}

	void FighterAI::Dock(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& t, double dt)
	{
		auto spec = u->Combat->Spec;
		double engines = PowerDistribution::FactorOf(u, PowerSystem::Engines);
		auto dock = u->DockPoint(t, BoardingShuttles::StandOffKm);
		auto to = dock - u->Pos;
		double dist = to.Length();
		// Arrive matched: the closer, the slower, on top of the hull's own drift.
		double want = Min(spec->MaxSpeed * engines, dist * 0.05 + 0.002);
		auto desired = t->Vel + (dist > 1e-6 ? to / dist : DVec3::Zero) * want;
		auto dv = desired - u->Vel;
		double step = spec->Accel * engines * dt;
		u->Vel = u->Vel + (dv.Length() > step ? dv.Normalized() * step : dv);
		u->Pos = u->Pos + u->Vel * dt;
		auto face = dist > 0.5 ? to / dist : t->Forward;
		double turn = spec->TurnRateDegS * Astro::Deg2Rad * dt;
		double angle = cs::Math::Acos(Max(-1.0, Min(1.0, DVec3::Dot(u->Forward, face))));
		u->Forward = angle <= turn ? face : DVec3::TurnStep(u->Forward, face, turn / angle);

		if (dist < BoardingShuttles::DockKm && (u->Vel - t->Vel).Length() < BoardingShuttles::MatchKmS)
		{
			u->LatchTimer += dt;
			if (u->LatchTimer >= BoardingShuttles::LatchSeconds()->Value) b->ShuttleLatch(u, t);
		}
		else u->LatchTimer = 0;
	}

	// ---------------------------------------------------------------- wingmen

	DVec3 FighterAI::SlotOffset(int slot)
	{
		return Formations::Slot(FormationShape::Wedge, slot, false); // US-T198: the wedge, one shape of several
	}

	bool FighterAI::Independent(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, cs::Ref<BattleUnit>& pinned)
	{
		auto f = u->Fighter;
		pinned = nullptr;
		switch (f->Order)
		{
			case WingOrder::AttackMyTarget:
				pinned = Fair(b, u, f->Lead->Target) ? f->Lead->Target : nullptr;
				return pinned != nullptr;
			case WingOrder::CoverMe:
				pinned = Wing::Threat(b, f->Lead);
				return pinned != nullptr;
			case WingOrder::EngageAtWill:
				return true;
			default:
				return false;
		}
	}

	bool FighterAI::FlysInFormation(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		cs::Ref<BattleUnit> discard;
		return u->Fighter->Lead != nullptr && !Independent(b, u, discard);
	}

	void FighterAI::Wingman(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt)
	{
		auto f = u->Fighter;
		auto lead = f->Lead;
		auto lf = lead->Fighter;
		cs::Ref<BattleUnit> pinned;
		if (Independent(b, u, pinned))
		{
			Lead(b, u, dt, pinned);
			return;
		}
		// US-T198: the squadron has engaged enemy fighters (Formations::BreakTarget): the wingman breaks out of formation
		// into a dogfight of its own, and forms up on its lead again once the furball is over. Not while the lead strafes a
		// capital: then every wingman flies its own run (US-T180, below).
		bool strafe = lf->Phase == FighterPhase::AttackRun && !f->Bomber && lead->Target != nullptr && Choreography::Strafeable(lead->Target);
		if (!strafe)
		{
			if (auto foe = Formations::BreakTarget(b, u))
			{
				Formations::Break(u, foe);
				Lead(b, u, dt, foe);
				return;
			}
		}
		Formations::Rejoin(u);
		u->Target = lead->Target;
		// US-T180: the lead is on a capital: the flight breaks up and each fighter flies its own strafing run down the hull
		// (Choreography::StrafeRun, from whichever end it is nearer), so the hull is swarmed rather than passed by one vee
		// (US-T164 had the wingmen hold their slots and only coast when the lead did). A bomber keeps station for its run.
		if (strafe)
		{
			u->ManeuverLeft -= dt;
			auto run = Choreography::StrafeRun(b, u, u->Target, u->Combat->Spec->TurnRateDegS, dt);
			FlyRun(b, u, run, u->Combat->Spec->MaxSpeed, dt);
			Torpedo(b, u);
			return;
		}
		if (Choreography::Strafing(u)) Choreography::EndRun(u);
		// Station: the slot in the lead's frame, stretched on the run, plus this pilot's own wander. The frame is held level
		// (US-T164): the lead banking and rolling (Choreography::Bank) must not swing its wingmen's slots round it.
		auto up = WeakPatches::LevelRoof(lead->Forward);
		auto right = DVec3::Cross(lead->Forward, up).Normalized();
		// US-T198: closing on enemy fighters the squadron only loosens a little; it breaks once they are near (above).
		double spread = lf->Phase == FighterPhase::AttackRun ? 2.6 : lf->Phase == FighterPhase::Dogfight ? 1.5 : 1.0;
		spread *= 1.6 - f->Discipline;
		auto slot = Formations::SlotOf(u) * spread; // US-T198: the squadron's own shape (wedge, line, echelon)
		double t = b->Seconds;
		auto wander = DVec3(
			cs::Math::Sin(t * 0.37 + f->Seed) + 0.5 * cs::Math::Sin(t * 1.13 + f->Seed * 1.7),
			cs::Math::Sin(t * 0.29 + f->Seed * 2.3) + 0.5 * cs::Math::Sin(t * 0.91 + f->Seed * 0.4),
			cs::Math::Sin(t * 0.21 + f->Seed * 3.1)) * (0.05 * (1.4 - f->Discipline));
		auto station = lead->Pos + right * (slot.X + wander.X) + up * (slot.Y + wander.Y) + lead->Forward * (slot.Z + wander.Z);
		// Reaction lag: they fly to where the lead was heading a moment ago, not where it is now.
		station = station - lead->Vel * f->Reaction * 0.08;
		// Close on the station itself, the lead's velocity as feed-forward; inside a small dead-band hold the lead's heading
		// rather than pointing the nose at a near-zero gap vector (U.SPC-T559).
		auto gap = station - u->Pos;
		auto want = lead->Vel + gap * 1.2;
		double maxSpeed = u->Combat->Spec->MaxSpeed * 1.3;
		// Steer scales the requested speed by engine power; undo that here so the feed-forward stays the lead's own speed.
		double engines = Max(1e-6, PowerDistribution::FactorOf(u, PowerSystem::Engines));
		double speed = Min(maxSpeed, want.Length() / engines);
		if (gap.Length() < StationDeadBandKm || want.Length() < 1e-6)
			Steer(b, u, u->Pos + lead->Forward, speed, dt);
		else
			Steer(b, u, u->Pos + want, speed, dt);
		Choreography::Bank(u, 0, dt); // US-T164: banked into its turns
		Torpedo(b, u);
	}

	// ---------------------------------------------------------------- flying

	void FighterAI::Steer(const cs::Ref<Battle>& /*b*/, const cs::Ref<BattleUnit>& u, DVec3 goal, double speed, double dt, cs::Nullable<DVec3> match, bool homing)
	{
		auto spec = u->Combat->Spec;
		// Power to the engines (U.SPC-T424) is speed and thrust.
		double engines = PowerDistribution::FactorOf(u, PowerSystem::Engines);
		speed *= engines;
		auto to = goal - u->Pos;
		double dist = to.Length();
		// Stay inside the battle -- except homing on its carrier, wherever that has drifted to.
		if (!homing && u->Pos.Length() > Battle::EscapeKm * 0.8) to = -u->Pos;
		double toLength = to.Length();
		auto desiredDir = toLength > 1e-6 ? to / toLength : u->Forward;
		Battle::TurnToward(u, desiredDir, spec->TurnRateDegS, dt);
		// Throttle back when closing on a slot so wingmen do not overshoot.
		double want = speed;
		// Given a velocity to match (a carrier's, a lead's), even a standing one: an unmoving carrier is still closed on gently.
		if (match.has_value()) want = Min(speed, match->Length() + dist * 1.2);
		auto desiredVel = u->Forward * want;
		auto dv = desiredVel - u->Vel;
		double step = spec->Accel * engines * dt;
		u->Vel = u->Vel + (dv.Length() > step ? dv.Normalized() * step : dv);
		u->Pos = u->Pos + u->Vel * dt;
	}
}
