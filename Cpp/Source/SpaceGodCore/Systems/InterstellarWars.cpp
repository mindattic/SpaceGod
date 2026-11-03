// Port of Unity.SpaceGod Core/Systems/InterstellarWars.cs (US-T15, see docs/MIGRATION.md).

#include "Systems/InterstellarWars.h"

#include "BodyCatalog.h"
#include "Combat/Boarding.h"
#include "Combat/BoardingShuttles.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Combat/GateBreakers.h"
#include "Combat/GatePickets.h"
#include "Combat/War.h"
#include "DVec3.h"
#include "Economy/Economy.h"
#include "Economy/Holdings.h"
#include "Economy/Settlement.h"
#include "Fleet.h"
#include "GalacticPosition.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "Systems/DeepSpace.h"
#include "Systems/FleetDoctrine.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/JumpGate.h"
#include "Systems/MilitarySupply.h"
#include "Systems/Polities.h"
#include "Systems/Statecraft.h"
#include "Systems/StrategicCommand.h"
#include "World.h"

namespace SpaceGod::Core
{
	const char* ToString(CampaignStage v)
	{
		switch (v)
		{
			case CampaignStage::Marching: return "Marching";
			case CampaignStage::Invading: return "Invading";
			case CampaignStage::Holding: return "Holding";
			case CampaignStage::Returning: return "Returning";
			case CampaignStage::Over: return "Over";
		}
		return "";
	}

	// ==================================================================== Campaign

	Campaign::Campaign()
	{
		Supply = cs::New<FleetSupply>();
	}

	cs::String Campaign::ToString() const
	{
		return cs::Cat((Faction ? Faction->Name : cs::String()), " ", (Force == ForceKind::JumpStrikeGroup ? "strike group" : "campaign"), " (", Mission, ") for ", (Goal ? Goal->Name : cs::String()), " (", Stage, ")");
	}

	// ==================================================================== Conflict

	cs::String Conflict::ToString() const
	{
		return cs::Cat((Aggressor ? Aggressor->Name : cs::String()), " against ", (Defender ? Defender->Name : cs::String()));
	}

	// ==================================================================== InterstellarWars

	InterstellarWars::InterstellarWars(cs::Ref<::SpaceGod::Core::Galaxy> galaxy, double jd, int seed)
		: rng(seed)
	{
		this->galaxy = galaxy;
		Logistics = cs::New<MilitaryLogistics>(galaxy.get());
		nextDailyJd = jd + 0.75;
		nextDeclareJd = jd + 60;
	}

	cs::Ref<::SpaceGod::Core::Polities> InterstellarWars::Polities() const { return galaxy->Polities; }
	cs::Ref<::SpaceGod::Core::Diplomacy> InterstellarWars::Diplomacy() const { return galaxy->Polities->Diplomacy; }
	cs::Ref<InterstellarTraffic> InterstellarWars::Traffic() const { return galaxy->Traffic; }

	cs::Ref<Conflict> InterstellarWars::Between(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) const
	{
		for (const auto& w : wars)
			if (w->Involves(a) && w->Involves(b)) return w;
		return nullptr;
	}

	cs::Ref<Conflict> InterstellarWars::Of(const cs::Ref<Faction>& f) const
	{
		for (const auto& w : wars)
			if (w->Involves(f)) return w;
		return nullptr;
	}

	// ---------------------------------------------------------------- going to war

	cs::Ref<Conflict> InterstellarWars::Declare(const cs::Ref<Faction>& aggressor, const cs::Ref<Faction>& defender, double jd, cs::List<cs::Ref<StarSystem>> goals)
	{
		if (aggressor == nullptr || defender == nullptr || aggressor == defender || aggressor->IsPirate || defender->IsPirate) return nullptr;
		if (Of(aggressor) != nullptr || Of(defender) != nullptr || Diplomacy()->InTruce(aggressor, defender, jd)) return nullptr;
		// A pact that keeps the peace must be broken first (Statecraft.DeclareWar), and a vassal makes no war of its own (U.SPC-T427).
		auto statecraft = Polities()->Statecraft;
		if (statecraft->BoundToPeace(aggressor, defender)) return nullptr;
		auto liege = statecraft->OverlordOf(aggressor);
		if (liege != nullptr && liege != defender) return nullptr;
		auto w = cs::New<Conflict>();
		w->Aggressor = aggressor;
		w->Defender = defender;
		w->StartJd = jd;
		w->LastActiveJd = jd;
		if (goals.Count() > 0) w->Goals.AddRange(goals);
		else
			for (const auto& s : galaxy->Systems)
				if (::SpaceGod::Core::Polities::HolderOf(s->World) == defender) w->Goals.Add(s);
		w->Goals.RemoveAll([&](const cs::Ref<StarSystem>& s) { return s == nullptr || !EnemyHolds(defender, s); });
		if (w->Goals.Count() == 0) return nullptr;
		Diplomacy()->Set(aggressor, defender, Relation::War);
		wars.Add(w);
		cs::String goalNames = Names(w->Goals);
		for (const auto& world : Involved(w))
		{
			world->War->RevokeBookings();
			world->Log(jd, cs::Cat("The ", aggressor->Name, " declares war on the ", defender->Name, ", its goal ", goalNames), WorldEventKind::Diplomacy);
		}
		return w;
	}

	cs::String InterstellarWars::Names(const cs::List<cs::Ref<StarSystem>>& systems)
	{
		auto names = cs::List<cs::String>();
		for (const auto& s : systems) names.Add(s->Name);
		return names.Count() <= 1 ? cs::Strings::Join("", names)
			: cs::Cat(cs::Strings::Join(", ", cs::List<cs::String>(names.begin(), names.begin() + (names.Count() - 1))), " and ", names[names.Count() - 1]);
	}

	cs::List<cs::Ref<World>> InterstellarWars::Involved(const cs::Ref<Conflict>& w) const
	{
		auto list = cs::List<cs::Ref<World>>();
		for (const auto& world : Polities()->Worlds())
			for (const auto& s : world->Economy->Settlements)
				if (!s->Fallen && w->Involves(s->Owner))
				{
					list.Add(world);
					break;
				}
		return list;
	}

	bool InterstellarWars::EnemyHolds(const cs::Ref<Faction>& f, const cs::Ref<StarSystem>& s)
	{
		for (const auto& st : s->World->Economy->Settlements)
			if (st->Owner == f && !st->Fallen && st->Kind != SettlementKind::Haven) return true;
		return false;
	}

	// ---------------------------------------------------------------- the day

	void InterstellarWars::Update(double jd)
	{
		while (jd >= nextDailyJd)
		{
			Daily(nextDailyJd);
			nextDailyJd += 1;
		}
	}

	void InterstellarWars::Daily(double jd)
	{
		for (const auto& w : wars)
		{
			Plan(w, w->Aggressor, jd);
			Plan(w, w->Defender, jd);
		}
		auto campaignsNow = cs::List<cs::Ref<Campaign>>(campaigns);
		for (const auto& c : campaignsNow)
			if (c->Active()) Step(c, jd);
		GroundWar(jd);
		Garrisons(jd);
		auto warsNow = cs::List<cs::Ref<Conflict>>(wars);
		for (const auto& w : warsNow) Judge(w, jd);
		RebuildForts(jd);
		// The gates' pickets: handed on with their gates, and replaced when lost (U.SPC-T480).
		if (galaxy->Network != nullptr)
			for (const auto& link : galaxy->Network->Links())
			{
				GatePickets::Upkeep(link->AtA, jd);
				GatePickets::Upkeep(link->AtB, jd);
			}
		if (AutoDeclare && jd >= nextDeclareJd)
		{
			nextDeclareJd = jd + 30;
			Ambitions(jd);
		}
	}

	// ---------------------------------------------------------------- campaigns

	cs::List<cs::Ref<StarSystem>> InterstellarWars::GoalsOf(const cs::Ref<Conflict>& w, const cs::Ref<Faction>& f) const
	{
		auto list = cs::List<cs::Ref<StarSystem>>();
		auto enemy = w->Enemy(f);
		if (f == w->Aggressor)
		{
			for (const auto& s : w->Goals)
				if (EnemyHolds(enemy, s)) list.Add(s);
			return list;
		}
		for (const auto& s : galaxy->Systems)
			for (const auto& st : s->World->Economy->Settlements)
				if (st->Owner == enemy && st->OccupiedFrom == f && !st->Fallen)
				{
					list.Add(s);
					break;
				}
		return list;
	}

	void InterstellarWars::Plan(const cs::Ref<Conflict>& w, const cs::Ref<Faction>& f, double jd)
	{
		cs::Ref<Campaign> main = nullptr, strike = nullptr;
		for (const auto& c : w->Campaigns)
			if (c->Active() && c->Faction == f)
			{
				w->LastActiveJd = jd;
				if (c->Force == ForceKind::JumpStrikeGroup) { if (!strike) strike = c; }
				else if (!main) main = c;
			}
		bool doctrine = FleetDoctrine::StrikeGroups()->Value;
		// Before the doctrine, a navy kept one fleet in the field whatever it was.
		if (!doctrine && (main != nullptr || strike != nullptr)) return;
		double threat = FleetDoctrine::StrikeThreat(galaxy, f);
		if (main == nullptr) PlanGateFleet(w, f, jd, doctrine, threat);
		if (doctrine && strike == nullptr) PlanStrike(w, f, jd, threat);
	}

	void InterstellarWars::PlanGateFleet(const cs::Ref<Conflict>& w, const cs::Ref<Faction>& f, double jd, bool doctrine, double threat)
	{
		auto goals = GoalsOf(w, f);
		if (goals.Count() == 0) return;

		cs::Ref<Ship> best = nullptr;
		cs::Ref<StarSystem> from = nullptr;
		double bestStrength = 0;
		for (const auto& s : galaxy->Systems)
		{
			auto world = s->World;
			for (const auto& ship : world->Fleet->Ships())
			{
				if (!Fit(world, ship, f)) continue;
				// A jump strike group is kept for the missions only it can fly (U.SPC-T474).
				if (doctrine && FleetDoctrine::IsStrikeGroup(world, ship)) continue;
				if (!FleetDoctrine::LeavesReserve(world, ship, f, threat)) continue;
				double strength = world->War->GroupStrength(ship);
				if (strength > bestStrength)
				{
					bestStrength = strength;
					best = ship;
					from = s;
				}
			}
		}
		if (best == nullptr) return;

		// The nearest goal (by the way it would go) that this group is strong enough for.
		cs::Ref<StarSystem> goal = nullptr;
		cs::Ref<cs::List<cs::Ref<JumpGate>>> route = nullptr;
		double bestDays = cs::Double::MaxValue;
		for (const auto& g : goals)
		{
			// Taking back what was lost at home is its own navy's work there, not a campaign's.
			if (g == from) continue;
			auto r = galaxy->Network->Route(from, g, f, jd, true);
			if (r == nullptr) continue;
			if (bestStrength < LaunchRatio * Expected(f, w->Enemy(f), g, *r)) continue;
			double days = GateNetwork::RouteDays(r);
			if (days < bestDays)
			{
				bestDays = days;
				goal = g;
				route = r;
			}
		}
		if (goal == nullptr) return;

		auto c2 = cs::New<Campaign>();
		c2->War = w;
		c2->Faction = f;
		c2->Home = from;
		c2->Goal = goal;
		c2->Stage = CampaignStage::Marching;
		c2->SetOutJd = jd;
		c2->Flagship = best->Name;
		if (Traffic()->SendCampaign(best, from, goal, c2, jd) == nullptr) return;
		w->Campaigns.Add(c2);
		campaigns.Add(c2);
		w->LastActiveJd = jd;
		from->World->Log(jd, cs::Cat("The ", best->Name, " leads the ", f->Name, " war fleet out for ", goal->Name, ", ", route->Count(), " gate", (route->Count() == 1 ? "" : "s"), " away by the ", (*route)[0]->Name),
			WorldEventKind::Diplomacy, from->Catalog->Root()->Index);
	}

	void InterstellarWars::PlanStrike(const cs::Ref<Conflict>& w, const cs::Ref<Faction>& f, double jd, double threat)
	{
		cs::Ref<Ship> best = nullptr;
		cs::Ref<StarSystem> from = nullptr;
		double strength = 0;
		for (const auto& s : galaxy->Systems)
		{
			auto world = s->World;
			for (const auto& ship : world->Fleet->Ships())
			{
				if (!FitStrike(world, ship, f) || !FleetDoctrine::IsStrikeGroup(world, ship) || !FleetDoctrine::LeavesReserve(world, ship, f, threat)) continue;
				double g = world->War->GroupStrength(ship);
				if (g > strength)
				{
					strength = g;
					best = ship;
					from = s;
				}
			}
		}
		if (best == nullptr) return;
		auto enemy = w->Enemy(f);
		double range = FleetDoctrine::StrikeRangeLy()->Value;
		auto InReach = [&](const cs::Ref<StarSystem>& t) { return t != from && t->Origin.DistanceLy(from->Origin) <= range; };

		// Relieve: one of its own systems the enemy is attacking.
		cs::Ref<StarSystem> goal = nullptr;
		auto mission = CampaignMission::Relieve;
		double nearest = cs::Double::MaxValue;
		for (const auto& t : galaxy->Systems)
		{
			if (!InReach(t)) continue;
			double attackers = Attacking(t->World, f, enemy);
			if (attackers <= 0 || strength < LaunchRatio * attackers) continue;
			double ly = t->Origin.DistanceLy(from->Origin);
			if (ly < nearest)
			{
				nearest = ly;
				goal = t;
			}
		}

		// A second front: a goal the gate fleet is not marching on (with none out, the nearest goal itself), if it has
		// the marines to take it.
		if (goal == nullptr && GroupMarines(from->World, best) > 0)
		{
			cs::Ref<Campaign> main = nullptr;
			for (const auto& c : w->Campaigns)
				if (c->Active() && c->Faction == f && c->Force == ForceKind::GateFleet) main = c;
			double bestScore = cs::Double::MaxValue;
			for (const auto& g : GoalsOf(w, f))
			{
				if (!InReach(g) || strength < LaunchRatio * DefenceOf(enemy, g)) continue;
				// The gate fleet's own goal only when there is no other.
				double score = g->Origin.DistanceLy(from->Origin) + (main != nullptr && main->Goal == g ? 1e6 : 0);
				if (score < bestScore)
				{
					bestScore = score;
					goal = g;
				}
			}
			if (goal != nullptr) mission = main != nullptr ? CampaignMission::SecondFront : CampaignMission::Invade;
		}

		// A raid: the nearest enemy system with a port it outguns.
		if (goal == nullptr)
		{
			nearest = cs::Double::MaxValue;
			for (const auto& t : galaxy->Systems)
			{
				if (!InReach(t) || !EnemyHolds(enemy, t)) continue;
				if (RaidTarget(t->World, f, enemy, strength, nullptr) == nullptr) continue;
				double ly = t->Origin.DistanceLy(from->Origin);
				if (ly < nearest)
				{
					nearest = ly;
					goal = t;
				}
			}
			mission = CampaignMission::Raid;
		}
		if (goal == nullptr) return;
		LaunchStrike(w, best, from, goal, mission, jd);
	}

	cs::Ref<Campaign> InterstellarWars::LaunchStrike(const cs::Ref<Conflict>& w, const cs::Ref<Ship>& leader, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& goal, CampaignMission mission, double jd)
	{
		auto f = leader->Faction;
		auto c = cs::New<Campaign>();
		c->War = w;
		c->Faction = f;
		c->Home = from;
		c->Goal = goal;
		c->Stage = CampaignStage::Marching;
		c->SetOutJd = jd;
		c->Flagship = leader->Name;
		c->Force = ForceKind::JumpStrikeGroup;
		c->Mission = mission;
		if (Traffic()->SendStrike(leader, from, goal, c, jd) == nullptr) return nullptr;
		w->Campaigns.Add(c);
		campaigns.Add(c);
		w->LastActiveJd = jd;
		const char* why;
		switch (mission)
		{
			case CampaignMission::Relieve: why = "to relieve"; break;
			case CampaignMission::Raid: why = "to raid"; break;
			case CampaignMission::SecondFront: why = "to open a second front at"; break;
			default: why = "against"; break;
		}
		from->World->Log(jd, cs::Cat("The ", leader->Name, " leads a ", f->Name, " jump strike group out under its own drives ", why, " ", goal->Name, ", ",
			cs::Fmt(goal->Origin.DistanceLy(from->Origin), "0.0"), " ly away"),
			WorldEventKind::Diplomacy, from->Catalog->Root()->Index);
		return c;
	}

	double InterstellarWars::Attacking(const cs::Ref<World>& world, const cs::Ref<Faction>& f, const cs::Ref<Faction>& enemy) const
	{
		bool holds = false;
		for (const auto& s : world->Economy->Settlements)
			if (s->Owner == f && !s->Fallen && s->Kind != SettlementKind::Haven) holds = true;
		if (!holds) return 0;
		double total = 0;
		for (const auto& s : world->Fleet->Ships())
			if (EnemyWarship(s, enemy)) total += s->Combat->Strength();
		if (total > 0) return total;
		for (const auto& l : landings)
			if (l->By == enemy && l->Troops > 0 && l->Target->Owner == f && Polities()->WorldOf(l->Target) == world) return 1;
		return 0;
	}

	bool InterstellarWars::EnemyWarship(const cs::Ref<Ship>& s, const cs::Ref<Faction>& enemy)
	{
		return !s->Destroyed && !s->Derelict && s->Faction == enemy && s->Spec->IsMilitary && s->PicketGate == nullptr && s->Combat != nullptr;
	}

	double InterstellarWars::DefenceOf(const cs::Ref<Faction>& enemy, const cs::Ref<StarSystem>& goal)
	{
		double there = 0;
		for (const auto& st : goal->World->Economy->Settlements)
			if (st->Owner == enemy)
				for (const auto& p : st->Platforms) there += p->Strength();
		for (const auto& s : goal->World->Fleet->Ships())
			if (!s->Destroyed && s->Faction == enemy && s->Spec->IsMilitary) there += s->Combat->Strength();
		return there;
	}

	cs::Ref<Settlement> InterstellarWars::RaidTarget(const cs::Ref<World>& world, const cs::Ref<Faction>& f, const cs::Ref<Faction>& enemy, double strength, const cs::List<cs::Ref<Settlement>>* done)
	{
		cs::Ref<Settlement> best = nullptr;
		double bestValue = 0;
		for (const auto& s : world->Economy->Settlements)
		{
			if (s->Owner != enemy || s->Fallen || s->Kind == SettlementKind::Haven || (done != nullptr && done->Contains(s))) continue;
			double defence = world->War->DefenceAt(s->Body, f);
			if (strength < defence * 1.25) continue;
			double value = (s->Population / 1e5 + 2) * (s->HasShipyard() ? 2 : 1) / (defence + 200);
			if (value > bestValue)
			{
				bestValue = value;
				best = s;
			}
		}
		return best;
	}

	cs::Ref<Campaign> InterstellarWars::Launch(const cs::Ref<Ship>& leader, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& goal, double jd)
	{
		auto f = leader ? leader->Faction : nullptr;
		if (f == nullptr || from == nullptr || goal == nullptr || from == goal) return nullptr;
		cs::Ref<Conflict> w = nullptr;
		for (const auto& c : wars)
			if (c->Involves(f) && EnemyHolds(c->Enemy(f), goal))
			{
				w = c;
				break;
			}
		// A defender's campaigns take back only what was taken from it; anything else is the group's own work there.
		if (w == nullptr || (f == w->Defender && !GoalsOf(w, f).Contains(goal))) return nullptr;
		// A jump strike group jumps in on the flank under its own drives (U.SPC-T474), a second front if a gate fleet of its
		// side is already out.
		if (FleetDoctrine::StrikeGroups()->Value && InterstellarTraffic::CanCruise(leader) && FleetDoctrine::IsStrikeGroup(from->World, leader))
		{
			bool marching = false;
			for (const auto& c : w->Campaigns)
				if (c->Active() && c->Faction == f && c->Force == ForceKind::GateFleet) marching = true;
			auto strike = LaunchStrike(w, leader, from, goal, marching ? CampaignMission::SecondFront : CampaignMission::Invade, jd);
			if (strike != nullptr && f == w->Aggressor && !w->Goals.Contains(goal)) w->Goals.Add(goal);
			return strike;
		}
		auto route = galaxy->Network->Route(from, goal, f, jd, true);
		if (route == nullptr || route->Count() == 0) return nullptr;
		auto campaign = cs::New<Campaign>();
		campaign->War = w;
		campaign->Faction = f;
		campaign->Home = from;
		campaign->Goal = goal;
		campaign->Stage = CampaignStage::Marching;
		campaign->SetOutJd = jd;
		campaign->Flagship = leader->Name;
		if (Traffic()->SendCampaign(leader, from, goal, campaign, jd) == nullptr) return nullptr;
		if (f == w->Aggressor && !w->Goals.Contains(goal)) w->Goals.Add(goal);
		w->Campaigns.Add(campaign);
		campaigns.Add(campaign);
		w->LastActiveJd = jd;
		from->World->Log(jd, cs::Cat("The ", leader->Name, " leads a ", f->Name, " war fleet out for ", goal->Name, " on its command's orders, ", route->Count(), " gate", (route->Count() == 1 ? "" : "s"), " away"),
			WorldEventKind::Diplomacy, from->Catalog->Root()->Index);
		return campaign;
	}

	bool InterstellarWars::Fit(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, const cs::Ref<Faction>& f)
	{
		return FitStrike(world, ship, f) && GroupMarines(world, ship) > 0;
	}

	bool InterstellarWars::FitStrike(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, const cs::Ref<Faction>& f)
	{
		// Nor a patrol wing: it keeps to its beat at home (U.SPC-T371).
		if (ship->Destroyed || ship->IsEscort() || ship->Faction != f || !ship->Spec->IsMilitary || ship->Derelict || ship->Battle != nullptr || ship->PatrolWing) return false;
		// Nor one dry in port waiting on a tanker (InterstellarTraffic.Idle refuses to send it): it would be picked
		// every day and every day fail to sail, holding back every other group.
		if (ship == world->PilotedShip || ship->Voyage != nullptr || ship->GateKm.has_value() || ship->Phase != ShipPhase::Parked || ship->OutOfPropellant) return false;
		if (world->Holdings != nullptr && world->Holdings->Owns(ship)) return false;
		// Nor one its polity's command has given an objective of its own (U.SPC-T423).
		if (world->Polities->Command->Holds(ship)) return false;
		return ship->Combat->HullDamage() <= 0.2 && ship->Combat->Armed() && !world->War->IsHomeGuard(ship);
	}

	double InterstellarWars::GroupMarines(const cs::Ref<World>& world, const cs::Ref<Ship>& leader)
	{
		double m = leader->Combat->MarinesAvailable();
		for (const auto& s : world->Fleet->Ships())
			if (s->Leader == leader && !s->Destroyed) m += s->Combat->MarinesAvailable();
		return m;
	}

	double InterstellarWars::Expected(const cs::Ref<Faction>& f, const cs::Ref<Faction>& enemy, const cs::Ref<StarSystem>& goal, const cs::List<cs::Ref<JumpGate>>& route) const
	{
		double worst = 0;
		for (const auto& g : route)
			for (const auto& end : cs::List<cs::Ref<JumpGate>>{ g, g->Partner })
				if (end->HostileTo(f))
				{
					double forts = 0;
					for (const auto& fort : end->Forts) forts += fort->Strength();
					worst = cs::Math::Max(worst, forts);
				}
		double there = 0;
		for (const auto& st : goal->World->Economy->Settlements)
			if (st->Owner == enemy)
				for (const auto& p : st->Platforms) there += p->Strength();
		for (const auto& s : goal->World->Fleet->Ships())
			if (!s->Destroyed && s->Faction == enemy && s->Spec->IsMilitary) there += s->Combat->Strength();
		return cs::Math::Max(worst, there);
	}

	InterstellarWars::Located InterstellarWars::Locate(const cs::Ref<Campaign>& c) const
	{
		for (const auto& s : galaxy->Systems)
			for (const auto& ship : s->World->Fleet->Ships())
				// Still its own side's and under way: a flagship taken (no heir to pass the voyage to) or left a hulk
				// carries the voyage on, but it is no longer the campaign.
				if (!ship->Destroyed && !ship->IsEscort() && !ship->Derelict && ship->Faction == c->Faction && ship->Voyage != nullptr && ship->Voyage->Campaign == c)
					return { s, ship };
		return { nullptr, nullptr };
	}

	bool InterstellarWars::InGate(const cs::Ref<Campaign>& c) const
	{
		for (const auto& p : Traffic()->InGates())
			if (p->Leader()->Voyage != nullptr && p->Leader()->Voyage->Campaign == c) return true;
		return false;
	}

	bool InterstellarWars::InCruise(const cs::Ref<Campaign>& c) const
	{
		for (const auto& cr : galaxy->DeepSpace->Cruises())
			if (cr->Ships.Count() > 0 && !cr->Leader()->Destroyed && cr->Leader()->Voyage != nullptr && cr->Leader()->Voyage->Campaign == c) return true;
		return false;
	}

	void InterstellarWars::Step(const cs::Ref<Campaign>& c, double jd)
	{
		auto [system, ship] = Locate(c);
		if (ship == nullptr)
		{
			if (InGate(c) || InCruise(c))
			{
				Logistics->InGate(c, jd);
				return;
			}
			// Gone from every system and no gate holds it: home (its voyage over), or lost with every hull.
			if (c->Stage != CampaignStage::Returning)
			{
				c->Lost = true;
				auto where = c->Goal->World;
				where->Log(jd, cs::Cat("The ", c->Faction->Name, " war fleet sent against ", c->Goal->Name, " is lost"), WorldEventKind::Loss, where->Catalog()->Root()->Index);
			}
			Finish(c, jd);
			return;
		}
		c->Flagship = ship->Name;
		auto v = ship->Voyage;
		auto world = system->World;
		Logistics->Day(c, system, ship, jd);
		if (ship->Battle != nullptr) return;
		if (c->Stage == CampaignStage::Returning)
		{
			// Called home but not yet on the way (no way through when it was called): try again.
			if (v->Destination != v->Home && v->HoldGate == nullptr && !v->AtGate) Traffic()->Redirect(system, ship, v->Home, jd);
		}
		else if (GroupDamage(world, ship) > 0.4 || ship->Combat->HullDamage() > 0.45 || !ship->Combat->Armed())
		{
			Recall(c, system, ship, jd, "battered");
			return;
		}
		else if (c->Supply->MustWithdraw())
		{
			// Its stores run down and nothing on its way to it: it cannot carry the campaign on (U.SPC-T93).
			Recall(c, system, ship, jd, c->Supply->OutOfSupply ? "out of supply" : "its stores spent");
			return;
		}
		else if (v->Destination != c->Goal && v->HoldGate == nullptr && !v->AtGate)
		{
			// Its way on was shut for good and it gave up where it was (U.SPC-T296): it goes home.
			Recall(c, system, ship, jd, "its way shut");
			return;
		}

		// At an enemy gate: one it came out of, or one it has to go through.
		auto gate = v->HoldGate ? v->HoldGate : (v->AtGate && v->NextGate() != nullptr && v->NextGate()->Home == system ? v->NextGate() : nullptr);
		if (gate != nullptr)
		{
			Siege(c, system, ship, gate, jd);
			return;
		}
		if (c->Stage == CampaignStage::Returning || system != c->Goal || ship->GateKm.has_value()) return;

		// A jump strike group on a relief or a raid (U.SPC-T474).
		if (c->Mission == CampaignMission::Relieve || c->Mission == CampaignMission::Raid)
		{
			StrikeStep(c, system, ship, jd);
			return;
		}

		// In the goal system.
		auto target = Objective(c, system);
		if (target == nullptr)
		{
			Won(c, system, ship, jd);
			return;
		}
		c->Stage = CampaignStage::Invading;
		if (c->Objective != target)
		{
			c->Objective = target;
			world->Log(jd, cs::Cat("The ", c->Faction->Name, " war fleet under the ", ship->Name, " moves on ", target->Name, " (", (*world->Catalog())[target->Body]->Name, ")"), WorldEventKind::Battle, target->Body);
		}
		if (!Fleet::IsAt(ship, target->Body))
		{
			// Idling somewhere else: on to the objective now rather than at the end of its stay.
			if (ship->Phase == ShipPhase::Parked && ship->DestinationBody != target->Body) world->Fleet->ScheduleNext(ship, jd);
			return;
		}
		if (world->War->BattleAt(target->Body) != nullptr) return;
		// The platforms must be down first; a defending squadron the fleet far outweighs cannot stop the landing, though
		// the fleet still fights it off.
		double defence = world->War->DefenceAt(target->Body, c->Faction);
		if (target->Platforms.Count() > 0 || defence > LandingCover * world->War->GroupStrength(ship))
		{
			world->War->TryStartBattle(target->Body, c->Faction, jd);
			return;
		}
		Land(c, world, ship, target, jd);
		if (defence > 0) world->War->TryStartBattle(target->Body, c->Faction, jd);
	}

	void InterstellarWars::StrikeStep(const cs::Ref<Campaign>& c, const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd)
	{
		auto world = system->World;
		c->Stage = CampaignStage::Invading;
		bool relief = c->Mission == CampaignMission::Relieve;
		cs::String done = relief ? cs::String("the system relieved") : cs::Cat("its raid done (", c->PortsRaided, " port", (c->PortsRaided == 1 ? "" : "s"), " struck)");
		// Its time runs from when it first reaches its work, not from the edge of the well.
		if (!cs::Double::IsNaN(c->ArrivedJd) && jd - c->ArrivedJd > FleetDoctrine::StrikeDays()->Value)
		{
			Recall(c, system, ship, jd, done);
			return;
		}
		auto [body, port] = StrikeObjective(c, system, ship, jd);
		if (body < 0)
		{
			Recall(c, system, ship, jd, done);
			return;
		}
		if (port != nullptr && c->Objective != port)
		{
			c->Objective = port;
			world->Log(jd, relief
					? cs::Cat("The ", c->Faction->Name, " strike group under the ", ship->Name, " jumps in to relieve ", port->Name, " (", (*world->Catalog())[body]->Name, ")")
					: cs::Cat("The ", c->Faction->Name, " strike group under the ", ship->Name, " falls on ", port->Name, " (", (*world->Catalog())[body]->Name, ")"),
				WorldEventKind::Battle, body);
		}
		if (!Fleet::IsAt(ship, body))
		{
			if (ship->Phase == ShipPhase::Parked && ship->DestinationBody != body) world->Fleet->ScheduleNext(ship, jd);
			return;
		}
		if (cs::Double::IsNaN(c->ArrivedJd)) c->ArrivedJd = jd;
		if (world->War->BattleAt(body) != nullptr) return;
		if (relief)
		{
			if (world->War->EnemiesAt(body, c->Faction))
			{
				world->War->TryStartBattle(body, c->Faction, jd);
				return;
			}
			if (port == nullptr || c->Raided.Contains(port)) return;
			// The orbit clear over its own port: its marines go down to the garrison, to fight beside it.
			double marines = 0;
			auto ships = cs::List<cs::Ref<Ship>>(world->Fleet->Ships());
			for (const auto& s : ships)
				if (!s->Destroyed && (s == ship || s->Leader == ship) && s->Battle == nullptr)
					marines += s->Combat->Interior->TakeMarines(s->Combat->MarinesAvailable());
			if (marines >= 1)
			{
				port->Garrison = GarrisonOf(port) + marines;
				c->Relieved++;
				world->Log(jd, cs::Cat(cs::Fmt(marines, "0"), " ", c->Faction->Name, " marines from the ", ship->Name, " go down to stiffen ", port->Name, "'s garrison"), WorldEventKind::Battle, body);
			}
			c->Raided.Add(port);
			return;
		}
		if (world->War->DefenceAt(body, c->Faction) > 0)
		{
			world->War->TryStartBattle(body, c->Faction, jd);
			return;
		}
		// Its defences broken: on to the next.
		c->Raided.Add(port);
		c->PortsRaided++;
		world->Log(jd, cs::Cat("The ", c->Faction->Name, " strike group under the ", ship->Name, " leaves ", port->Name, "'s defences broken"), WorldEventKind::Battle, body);
	}

	InterstellarWars::StrikeMark InterstellarWars::StrikeObjective(const cs::Ref<Campaign>& c, const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd) const
	{
		auto world = system->World;
		auto enemy = c->War->Enemy(c->Faction);
		if (c->Mission == CampaignMission::Raid)
		{
			auto mark = RaidTarget(world, c->Faction, enemy, world->War->GroupStrength(ship), &c->Raided);
			return mark != nullptr ? StrikeMark{ mark->Body, mark } : StrikeMark{ -1, nullptr };
		}
		// A relief: its own port with the enemy at it; else one with the enemy's marines down on it; else, while the
		// enemy is loose in the system, the nearest of its ports it has not yet stiffened; else the enemy's warships
		// wherever they are.
		bool loose = false;
		for (const auto& s : world->Fleet->Ships())
			if (EnemyWarship(s, enemy)) loose = true;
		cs::Ref<Settlement> landed = nullptr, unseen = nullptr;
		double unseenDays = cs::Double::MaxValue;
		int here = ship->Orbit ? ship->Orbit->Body : -1;
		for (const auto& s : world->Economy->Settlements)
		{
			if (s->Owner != c->Faction || s->Fallen || s->Kind == SettlementKind::Haven) continue;
			if (world->War->EnemiesAt(s->Body, c->Faction)) return { s->Body, s };
			if (c->Raided.Contains(s)) continue;
			for (const auto& l : landings)
				if (l->Target == s && l->By == enemy && l->Troops > 0) { if (!landed) landed = s; }
			if (!loose || here < 0) continue;
			double days = s->Body == here ? 0 : world->Fleet->EstimateTravelDays(ship, here, s->Body, jd);
			if (days < unseenDays)
			{
				unseenDays = days;
				unseen = s;
			}
		}
		if (landed != nullptr) return { landed->Body, landed };
		if (unseen != nullptr) return { unseen->Body, unseen };
		for (const auto& s : world->Fleet->Ships())
		{
			if (!EnemyWarship(s, enemy)) continue;
			auto e = s->Controller();
			int at = e->Phase == ShipPhase::Parked && e->Orbit != nullptr ? e->Orbit->Body : e->DestinationBody;
			if (at < 0) continue;
			cs::Ref<Settlement> own = nullptr;
			for (const auto& st : world->Economy->At(at))
				if (st->Owner == c->Faction && !st->Fallen && st->Kind != SettlementKind::Haven) own = st;
			return { at, own };
		}
		return { -1, nullptr };
	}

	double InterstellarWars::GroupDamage(const cs::Ref<World>& world, const cs::Ref<Ship>& leader)
	{
		double sum = leader->Combat->HullDamage();
		int n = 1;
		for (const auto& s : world->Fleet->Ships())
			if (s->Leader == leader && !s->Destroyed)
			{
				sum += s->Combat->HullDamage();
				n++;
			}
		return sum / n;
	}

	void InterstellarWars::Siege(const cs::Ref<Campaign>& c, const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, const cs::Ref<JumpGate>& gate, double jd)
	{
		auto v = ship->Voyage;
		if (!gate->HostileTo(c->Faction))
		{
			// Taken (by this fleet or another), or the war is over: on its way.
			if (v->HoldGate != nullptr) Traffic()->Release(system, ship, jd);
			return;
		}
		if (gate->Forts.Count() > 0 || DefendersAt(system->World, gate, c->Faction))
		{
			if (system->World->War->StartGateBattle(gate, c->Faction, jd) != nullptr) c->GateBattles++;
			c->Besieging = gate;
			c->BesiegedDays = 0;
			return;
		}
		if (c->Stage == CampaignStage::Returning)
		{
			// Its guns silenced: a fleet going home slips through without stopping to take it.
			if (v->HoldGate != nullptr) Traffic()->Release(system, ship, jd);
			return;
		}
		if (c->Besieging != gate)
		{
			c->Besieging = gate;
			c->BesiegedDays = 0;
		}
		c->BesiegedDays += 1;
		// Boarding shuttles in the group put marines aboard the gate and take it sooner (U.SPC-T486); a gate breaker's
		// boarding and engineering teams storm it the first day and take it intact (U.SPC-T478).
		auto group = cs::List<cs::Ref<Ship>>(Group(system->World, ship));
		bool shuttles = BoardingShuttles::CanSeizeGate(group);
		auto breaker = GateBreakers::Stormer(group);
		double hold = BoardingShuttles::GateHoldDays(GateHoldDays, shuttles);
		if (breaker != nullptr) hold = GateBreakers::GateHoldDays(hold);
		if (c->BesiegedDays < hold) return;
		SeizeGate(c, gate, jd, shuttles, breaker);
		if (v->HoldGate != nullptr) Traffic()->Release(system, ship, jd);
	}

	bool InterstellarWars::DefendersAt(const cs::Ref<World>& world, const cs::Ref<JumpGate>& gate, const cs::Ref<Faction>& f)
	{
		int root = world->Catalog()->Root()->Index;
		for (const auto& s : world->Fleet->Ships())
		{
			if (s->Destroyed || !s->Spec->IsMilitary || s->Faction->IsPirate || !world->Diplomacy()->Hostile(f, s->Faction)) continue;
			auto o = s->Controller()->Orbit;
			if (Fleet::IsAt(s->Controller(), root) && o != nullptr && (o->OffsetAt(world->Jd) - gate->PositionKm).Length() <= War::GateReachKm) return true;
		}
		return false;
	}

	cs::List<cs::Ref<Ship>> InterstellarWars::Group(const cs::Ref<World>& world, const cs::Ref<Ship>& leader)
	{
		// C# yields these lazily; the caller copies them into a list at once, so building it eagerly is the same.
		auto list = cs::List<cs::Ref<Ship>>();
		list.Add(leader);
		for (const auto& s : world->Fleet->Ships())
			if (s->Leader == leader && !s->Destroyed) list.Add(s);
		return list;
	}

	void InterstellarWars::SeizeGate(const cs::Ref<Campaign>& c, const cs::Ref<JumpGate>& gate, double jd, bool boarded, const cs::Ref<Ship>& breaker)
	{
		double patched = breaker != nullptr ? GateBreakers::MakeGood(gate) : 0;
		auto old = gate->Owner;
		TakenGate taken;
		if (takenGates.TryGetValue(gate, taken) && taken.War == c->War)
		{
			// Back to how this war found it: nothing left for its own peace to give back.
			if (taken.Before == c->Faction) takenGates.Remove(gate);
		}
		else
			// Not recorded for this war: either its first time taking the gate, or a different, still-unsettled
			// war's claim on it that this capture supersedes (U.SPC-T323) -- either way, this war now owns its fate.
			takenGates[gate] = TakenGate{ c->War, old };
		gate->Owner = c->Faction;
		gate->BlockadeUntilJd = cs::Double::NegativeInfinity;
		c->GatesTaken++;
		c->Besieging = nullptr;
		c->BesiegedDays = 0;
		auto world = gate->Home->World;
		cs::String how = breaker != nullptr
			? cs::Cat("its forts silenced, the ", breaker->Name, "'s boarding and engineering teams storm it and take it intact",
				(patched > 0 ? cs::Cat(", patching its ring to ", cs::Fmt(gate->Integrity * 100, "0"), "% whole") : cs::String()))
			: boarded ? cs::String("its forts silenced, their boarding shuttles put marines aboard and take it intact") : cs::String("its forts silenced, the gate is theirs");
		world->Log(jd, cs::Cat("The ", c->Faction->Name, " take the ", gate->Name, " at ", gate->Home->Name, " from the ", (old ? old->Name : cs::String("nobody")), ": ", how), WorldEventKind::Capture,
			world->Catalog()->Root()->Index);
	}

	bool InterstellarWars::OnEmerged(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& leader, const cs::Ref<JumpGate>& gate, double jd)
	{
		auto v = leader->Voyage;
		auto c = v ? v->Campaign : nullptr;
		if (c == nullptr || !c->Active() || !gate->HostileTo(c->Faction)) return false;
		if (c->Stage == CampaignStage::Returning && gate->Forts.Count() == 0) return false;
		v->HoldGate = gate;
		leader->GateKm = std::nullopt;
		system->World->Fleet->ScheduleNext(leader, jd);
		if (system->World->War->StartGateBattle(gate, c->Faction, jd) != nullptr) c->GateBattles++;
		c->Besieging = gate;
		c->BesiegedDays = 0;
		return true;
	}

	bool InterstellarWars::MustTake(const cs::Ref<JumpGate>& gate, const cs::Ref<Voyage>& v) const
	{
		auto c = v ? v->Campaign : nullptr;
		return c != nullptr && c->Active() && gate->HostileTo(c->Faction) && (gate->Forts.Count() > 0 || c->Stage != CampaignStage::Returning);
	}

	int InterstellarWars::Orders(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, int here, double jd)
	{
		auto v = ship->Voyage;
		auto c = v ? v->Campaign : nullptr;
		if (c == nullptr || !c->Active()) return -1;
		if (v->HoldGate != nullptr) return here;
		if (c->Stage == CampaignStage::Returning || system != c->Goal) return -1;
		if (c->Mission == CampaignMission::Relieve || c->Mission == CampaignMission::Raid)
		{
			auto mark = StrikeObjective(c, system, ship, jd).Body;
			return mark >= 0 ? mark : here;
		}
		auto target = Objective(c, system);
		return target != nullptr ? target->Body : here;
	}

	cs::Ref<Settlement> InterstellarWars::Objective(const cs::Ref<Campaign>& c, const cs::Ref<StarSystem>& system)
	{
		auto enemy = c->War->Enemy(c->Faction);
		cs::Ref<Settlement> best = nullptr;
		for (const auto& s : system->World->Economy->Settlements)
		{
			if (s->Owner != enemy || s->Fallen || s->Kind == SettlementKind::Haven) continue;
			// A defender takes back only what was taken from it.
			if (c->Faction == c->War->Defender && s->OccupiedFrom != c->Faction) continue;
			if (best == nullptr || s->Population > best->Population) best = s;
		}
		return best;
	}

	void InterstellarWars::Won(const cs::Ref<Campaign>& c, const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd)
	{
		if (c->Stage != CampaignStage::Holding)
			system->World->Log(jd, cs::Cat("The ", c->Faction->Name, " hold ", system->Name, ": every ", c->War->Enemy(c->Faction)->Name, " settlement there taken"), WorldEventKind::Capture,
				system->Catalog->Root()->Index);
		c->Objective = nullptr;
		for (const auto& g : GoalsOf(c->War, c->Faction))
		{
			if (g == system) continue;
			auto r = galaxy->Network->Route(system, g, c->Faction, jd, true);
			if (r == nullptr) continue;
			if (Traffic()->Redirect(system, ship, g, jd))
			{
				c->Goal = g;
				c->Stage = CampaignStage::Marching;
				return;
			}
		}
		c->Stage = CampaignStage::Holding;
	}

	void InterstellarWars::Recall(const cs::Ref<Campaign>& c, const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd, const cs::String& why)
	{
		if (c->Stage == CampaignStage::Returning) return;
		c->Stage = CampaignStage::Returning;
		c->Objective = nullptr;
		c->Besieging = nullptr;
		system->World->Log(jd, cs::Cat("The ", c->Faction->Name, " war fleet under the ", ship->Name, " turns for home, ", why), WorldEventKind::Info, system->Catalog->Root()->Index);
		Traffic()->Redirect(system, ship, ship->Voyage->Home, jd);
	}

	void InterstellarWars::Finish(const cs::Ref<Campaign>& c, double jd)
	{
		c->Stage = CampaignStage::Over;
		c->EndedJd = jd;
		Logistics->Ended(c, jd);
		landings.RemoveAll([&](const cs::Ref<Landing>& l) { return l->Campaign == c && l->Troops <= 0; });
	}

	// ---------------------------------------------------------------- marines and garrisons

	double InterstellarWars::GarrisonOf(const cs::Ref<Settlement>& s)
	{
		if (cs::Double::IsNaN(s->Garrison)) s->Garrison = FullGarrison(s);
		return s->Garrison;
	}

	double InterstellarWars::FullGarrison(const cs::Ref<Settlement>& s) { return cs::Math::Max(MinGarrison, s->Population * GarrisonPerPerson); }

	void InterstellarWars::Land(const cs::Ref<Campaign>& c, const cs::Ref<World>& world, const cs::Ref<Ship>& leader, const cs::Ref<Settlement>& target, double jd)
	{
		double marines = 0;
		auto ships = cs::List<cs::Ref<Ship>>(world->Fleet->Ships());
		for (const auto& s : ships)
			if (!s->Destroyed && (s == leader || s->Leader == leader) && s->Battle == nullptr)
				marines += s->Combat->Interior->TakeMarines(s->Combat->MarinesAvailable());
		cs::Ref<Landing> landing = nullptr;
		for (const auto& l : landings)
			if (l->Target == target && l->By == c->Faction) landing = l;
		if (marines < 1)
		{
			if (landing == nullptr) Recall(c, c->Goal, leader, jd, "its marines spent");
			return;
		}
		double garrison = GarrisonOf(target);
		if (landing == nullptr)
		{
			landing = cs::New<Landing>();
			landing->Target = target;
			landing->By = c->Faction;
			landing->Campaign = c;
			landing->SinceJd = jd;
			landings.Add(landing);
			world->Log(jd, cs::Cat(cs::Fmt(marines, "0"), " ", c->Faction->Name, " marines land on ", target->Name, " (", (*world->Catalog())[target->Body]->Name, ") against ", cs::Fmt(garrison, "0"), " of the ", target->Owner->Name, "'s garrison"),
				WorldEventKind::Battle, target->Body);
		}
		else world->Log(jd, cs::Cat(cs::Fmt(marines, "0"), " more ", c->Faction->Name, " marines go down to ", target->Name), WorldEventKind::Battle, target->Body);
		landing->Troops += marines;
	}

	void InterstellarWars::GroundWar(double jd)
	{
		auto landingsNow = cs::List<cs::Ref<Landing>>(landings);
		for (const auto& l : landingsNow)
		{
			auto s = l->Target;
			auto world = Polities()->WorldOf(s);
			if (world == nullptr || s->Fallen || s->Owner == l->By || !Diplomacy()->Hostile(l->By, s->Owner) || l->Troops <= 0)
			{
				landings.Remove(l);
				continue;
			}
			double d = GarrisonOf(s), a = l->Troops;
			double killed = cs::Math::Min(d, a * l->By->MarineQuality * GroundRate);
			double lost = cs::Math::Min(a, d * s->Owner->MarineQuality * DugIn * GroundRate);
			s->Garrison = d - killed;
			l->Troops = a - lost;
			if (s->Garrison < 0.5)
			{
				landings.Remove(l);
				Occupy(l, world, jd);
			}
			else if (l->Troops < 0.5)
			{
				landings.Remove(l);
				world->Log(jd, cs::Cat("The ", l->By->Name, " marines on ", s->Name, " are wiped out: its garrison holds (", cs::Fmt(s->Garrison, "0"), " left)"), WorldEventKind::Battle, s->Body);
			}
		}
	}

	void InterstellarWars::Occupy(const cs::Ref<Landing>& l, const cs::Ref<World>& world, double jd)
	{
		auto s = l->Target;
		auto old = s->Owner;
		world->Economy->Capture(s, l->By);
		s->Garrison = l->Troops;
		if (s->OccupiedFrom == l->By)
		{
			// Taken back: liberated rather than occupied.
			s->OccupiedFrom = nullptr;
			s->Unrest = cs::Math::Max(0.1, s->Unrest * 0.3);
		}
		else
		{
			if (!s->OccupiedFrom) s->OccupiedFrom = old;
			s->Unrest = cs::Math::Max(s->Unrest, OccupationUnrest);
		}
		if (l->Campaign != nullptr) l->Campaign->SettlementsTaken++;
		world->War->RevokeBookings();
		world->Log(jd, cs::Cat(s->Name, " (", (*world->Catalog())[s->Body]->Name, ") falls to the ", l->By->Name, ": the ", old->Name, " garrison overrun, ", cs::Fmt(s->Garrison, "0"), " marines hold it"), WorldEventKind::Capture, s->Body);
	}

	void InterstellarWars::Garrisons(double jd)
	{
		for (const auto& world : Polities()->Worlds())
			for (const auto& s : world->Economy->Settlements)
			{
				if (s->Fallen) continue;
				double full = FullGarrison(s);
				if (!cs::Double::IsNaN(s->Garrison) && s->OccupiedFrom == nullptr && s->Garrison < full && !UnderAssault(s))
					s->Garrison = cs::Math::Min(full, s->Garrison + full * 0.02);
				if (s->Unrest <= 0) continue;
				double hold = cs::Double::IsNaN(s->Garrison) ? 1 : cs::Math::Min(1.0, s->Garrison / full);
				s->Unrest = cs::Math::Max(0.0, s->Unrest - 0.006 * (0.5 + hold));
				if (s->OccupiedFrom != nullptr && s->Unrest > 0.5 && hold < 0.25 && Between(s->Owner, s->OccupiedFrom) != nullptr && rng.NextDouble() < 0.03)
				{
					auto occupier = s->Owner;
					auto old = s->OccupiedFrom;
					world->Economy->Capture(s, old);
					s->OccupiedFrom = nullptr;
					s->Garrison = full * 0.3;
					s->Unrest = 0.3;
					world->War->RevokeBookings();
					world->Log(jd, cs::Cat(s->Name, " rises against the ", occupier->Name, ": its thin garrison overrun, it declares for the ", old->Name, " again"), WorldEventKind::Capture, s->Body);
				}
			}
	}

	bool InterstellarWars::UnderAssault(const cs::Ref<Settlement>& s) const
	{
		for (const auto& l : landings)
			if (l->Target == s) return true;
		return false;
	}

	void InterstellarWars::RebuildForts(double /*jd*/)
	{
		if (galaxy->Network == nullptr) return;
		for (const auto& link : galaxy->Network->Links())
			for (const auto& g : cs::List<cs::Ref<JumpGate>>{ link->AtA, link->AtB })
			{
				if (g->Forts.Count() >= g->MaxForts || g->Owner == nullptr || g->Owner->Treasury < War::PlatformPrice) continue;
				auto war = g->Home->World->War;
				if (war->BattleAt(g->Home->Catalog->Root()->Index) != nullptr || rng.NextDouble() > 0.04) continue;
				g->Owner->Treasury -= War::PlatformPrice;
				g->Forts.Add(cs::New<ShipCombat>(CombatSpec::Platform()));
			}
	}

	// ---------------------------------------------------------------- peace

	void InterstellarWars::Judge(const cs::Ref<Conflict>& w, double jd)
	{
		auto a = w->Aggressor;
		auto d = w->Defender;
		if (Diplomacy()->Get(a, d) != Relation::War)
		{
			Settle(w, jd, "the two make peace");
			return;
		}
		bool goalsTaken = true;
		for (const auto& g : w->Goals)
			if (EnemyHolds(d, g)) goalsTaken = false;
		if (goalsTaken)
		{
			Settle(w, jd, cs::Cat("the ", d->Name, ", its systems taken, sues for peace"));
			return;
		}
		if (Polities()->Navy(d).Count() == 0 && Occupied(a, d) > 0)
		{
			Settle(w, jd, cs::Cat("the ", d->Name, ", its navy gone, sues for peace"));
			return;
		}
		if (Polities()->Navy(a).Count() == 0)
		{
			Settle(w, jd, cs::Cat("the ", a->Name, ", its navy gone, sues for peace"));
			return;
		}
		if (jd - w->StartJd > MaxWarDays)
		{
			Settle(w, jd, "both sides war-weary after a year");
			return;
		}
		if (jd - w->LastActiveJd > StalemateDays)
			Settle(w, jd, "neither side able to carry the war to the other");
	}

	int InterstellarWars::Occupied(const cs::Ref<Faction>& by, const cs::Ref<Faction>& from) const
	{
		int n = 0;
		for (const auto& s : Polities()->Settlements())
			if (s->Owner == by && s->OccupiedFrom == from && !s->Fallen) n++;
		return n;
	}

	void InterstellarWars::Settle(const cs::Ref<Conflict>& w, double jd, const cs::String& why)
	{
		auto a = w->Aggressor;
		auto d = w->Defender;
		auto t = cs::New<::SpaceGod::Core::Treaty>();
		t->A = a;
		t->B = d;
		t->Jd = jd;
		t->TruceUntilJd = jd + TruceDays;
		auto ceded = cs::Dictionary<cs::Ref<StarSystem>, cs::Ref<Faction>>();
		for (const auto& s : galaxy->Systems)
		{
			auto holder = ::SpaceGod::Core::Polities::HolderOf(s->World);
			if (!w->Involves(holder)) continue;
			auto other = w->Enemy(holder);
			for (const auto& st : s->World->Economy->Settlements)
				if (st->Owner == holder && st->OccupiedFrom == other && !st->Fallen)
				{
					ceded[s] = holder;
					break;
				}
		}
		int toA = 0, toD = 0;
		for (auto [key, value] : ceded)
			if (value == a) toA++;
			else toD++;
		t->Winner = toA > toD ? a : toD > toA ? d : nullptr;

		for (const auto& s : galaxy->Systems)
		{
			auto world = s->World;
			cs::Ref<Faction> to;
			bool isCeded = ceded.TryGetValue(s, to);
			for (const auto& st : world->Economy->Settlements)
			{
				if (st->Fallen) continue;
				if (isCeded && st->Owner == w->Enemy(to) && st->Kind != SettlementKind::Haven)
				{
					// Ceded with its system.
					world->Economy->Capture(st, to);
					st->OccupiedFrom = nullptr;
					st->Garrison = FullGarrison(st) * 0.5;
					st->Unrest = cs::Math::Max(st->Unrest, 0.3);
				}
				else if (isCeded && st->Owner == to && w->Involves(st->OccupiedFrom)) st->OccupiedFrom = nullptr;
				else if (w->Involves(st->Owner) && st->OccupiedFrom == w->Enemy(st->Owner))
				{
					// Given back.
					world->Economy->Capture(st, st->OccupiedFrom);
					st->OccupiedFrom = nullptr;
					st->Garrison = cs::Double::NaN;
					st->Unrest = cs::Math::Min(st->Unrest, 0.1);
				}
			}
			for (const auto& g : s->Gates())
			{
				TakenGate taken;
				if (isCeded && g->Owner == w->Enemy(to))
				{
					g->Owner = to;
					if (takenGates.TryGetValue(g, taken) && taken.War == w) takenGates.Remove(g);
				}
				else if (takenGates.TryGetValue(g, taken) && taken.War == w)
				{
					// Only this war's own capture: a different, still-unsettled war's claim on this same gate
					// (U.SPC-T323) is that war's to answer for when it makes its own peace, not this one's.
					if (!isCeded) g->Owner = taken.Before;
					takenGates.Remove(g);
				}
			}
			if (isCeded) t->Ceded.Add(s->Name);
		}
		landings.RemoveAll([&](const cs::Ref<Landing>& l) { return w->Involves(l->By); });

		auto names = cs::List<cs::Ref<StarSystem>>();
		for (auto [key, value] : ceded) names.Add(key);
		// Named for the loser's seat of government (the defender's in a white peace), as founded: lost in the war, still its name.
		auto loserOrD = t->Loser() ? t->Loser() : d;
		auto seat = loserOrD->Capital ? loserOrD->Capital : Polities()->CapitalOf(loserOrD);
		cs::String place = seat ? seat->Name : loserOrD->Name;
		t->Terms = t->Winner == nullptr
			? cs::Cat("The Treaty of ", place, ": ", why, "; the ", a->Name, " and the ", d->Name, " make peace, all occupied ground given back")
			: cs::Cat("The Treaty of ", place, ": ", why, "; the ", t->Loser()->Name, " cede", (names.Count() == 1 ? "s" : ""), " ", Names(names), " to the ", t->Winner->Name);
		Diplomacy()->Sign(t);
		w->Treaty = t;
		w->EndedJd = jd;
		wars.Remove(w);
		ended.Add(w);

		for (const auto& c : w->Campaigns)
		{
			if (!c->Active() || c->Stage == CampaignStage::Returning) continue;
			auto [system, ship] = Locate(c);
			if (ship == nullptr)
			{
				c->Stage = CampaignStage::Returning;
				continue;
			}
			Recall(c, system, ship, jd, "the war over");
		}
		auto told = cs::HashSet<cs::Ref<World>>();
		for (const auto& world : Involved(w))
			if (told.Add(world))
			{
				world->War->RevokeBookings();
				world->Log(jd, t->Terms, WorldEventKind::Diplomacy);
			}
		for (auto [key, value] : ceded)
			if (told.Add(key->World)) key->World->Log(jd, t->Terms, WorldEventKind::Diplomacy);
	}

	// ---------------------------------------------------------------- ambition

	void InterstellarWars::Ambitions(double jd)
	{
		if (wars.Count() > 0 || galaxy->Network == nullptr || rng.NextDouble() > DeclareChancePerMonth) return;
		struct Option
		{
			cs::Ref<Faction> f, g;
			cs::Ref<StarSystem> goal;
		};
		auto options = cs::List<Option>();
		for (const auto& f : Polities()->Factions())
		{
			if (!Warlike(f) || Polities()->AtWar(f) || Polities()->Statecraft->OverlordOf(f) != nullptr) continue;
			double mine = NavyStrength(f);
			if (mine <= 0) continue;
			for (const auto& home : Polities()->SystemsWithHoldings(f))
			{
				auto homeSystem = SystemOf(home);
				if (homeSystem == nullptr) continue;
				for (const auto& link : galaxy->Network->LinksOf(homeSystem))
				{
					auto next = link->Other(homeSystem);
					auto g = ::SpaceGod::Core::Polities::HolderOf(next->World);
					if (g == nullptr || g == f || !Warlike(g) || Polities()->AtWar(g) || Diplomacy()->InTruce(f, g, jd)) continue;
					// Bound by a pact, or too friendly to want it (U.SPC-T427).
					if (Polities()->Statecraft->BoundToPeace(f, g) || Polities()->Statecraft->Opinion(f, g) > 40) continue;
					if (mine < DeclareRatio * cs::Math::Max(NavyStrength(g), 1.0)) continue;
					options.Add(Option{ f, g, next });
				}
			}
		}
		if (options.Count() == 0) return;
		auto pick = options[rng.Next(options.Count())];
		Declare(pick.f, pick.g, jd, { pick.goal });
	}

	bool InterstellarWars::Warlike(const cs::Ref<Faction>& f)
	{
		return f != nullptr && !f->IsPirate && f->IsMilitary && f->Kind == PolityKind::State;
	}

	double InterstellarWars::NavyStrength(const cs::Ref<Faction>& f) const
	{
		double total = 0;
		for (const auto& s : Polities()->Navy(f)) total += s->Combat->Strength();
		return total;
	}

	cs::Ref<StarSystem> InterstellarWars::SystemOf(const cs::Ref<World>& w) const
	{
		for (const auto& s : galaxy->Systems)
			if (s->World == w) return s;
		return nullptr;
	}
}
