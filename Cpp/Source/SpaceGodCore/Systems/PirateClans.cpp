// Port of Assets/UnitySpace/Core/Systems/PirateClans.cs (see docs/MIGRATION.md).

#include "Systems/PirateClans.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/Crews.h"
#include "Combat/Diplomacy.h"
#include "Combat/War.h"
#include "Economy/BlackMarket.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Law/Outlaws.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/JumpGate.h"
#include "Systems/Polities.h"

namespace SpaceGod::Core
{
	double PirateClans::RunnerPrice() { return Economy::HullPrice(ShipClass::Freighter) * 0.5; }

	const cs::List<cs::String>& PirateClans::RunnerNames()
	{
		static const cs::List<cs::String> names
		{
			"Quiet Money", "Back Door", "Blind Eye", "Hush", "Fence Post", "Bad Penny", "Dark Horse", "Night Freight", "Small Hours", "Loose Change",
		};
		return names;
	}

	const cs::List<cs::String>& PirateClans::BreakawayNames()
	{
		static const cs::List<cs::String> names{ "Brethren", "Free Company", "Reavers", "Corsairs", "Black Hand" };
		return names;
	}

	PirateClans::PirateClans(Galaxy* galaxy, InterstellarTraffic* traffic, int seed)
		: galaxy(galaxy), traffic(traffic), rng(seed)
	{
	}

	void PirateClans::Watch(cs::Ref<StarSystem> system)
	{
		auto w = system->World;
		if (w->War == nullptr) return;
		w->War->Plundered += [this, w](cs::Ref<Faction> clan, double value, double /*jd*/) { OnPlundered(w, clan, value); };
		// A hull destroyed rather than robbed brings the state down on the clan far harder (U.SPC-T374).
		w->War->Heat += [this, w](cs::Ref<Faction> clan, double heat, double /*jd*/) { Pledge(Polities::HolderOf(w), clan, heat); };
		w->War->BattleEnded += [this, w](cs::Ref<Battle> b, double jd) { OnBattleEnded(w, b, jd); };
	}

	// ---------------------------------------------------------------- who and where

	cs::List<cs::Ref<Faction>> PirateClans::Clans()
	{
		cs::List<cs::Ref<Faction>> list;
		for (const auto& f : galaxy->Polities->Factions())
			if (f->IsPirate) list.Add(f);
		return list;
	}

	cs::List<PirateClans::HavenEntry> PirateClans::Havens(cs::Ref<Faction> clan)
	{
		cs::List<HavenEntry> list;
		for (const auto& s : galaxy->Systems)
			for (const auto& st : s->World->Economy->Settlements)
				if (st->Kind == SettlementKind::Haven && !st->Fallen && st->Owner == clan) list.Add(HavenEntry{ s, st });
		return list;
	}

	cs::HashSet<cs::Ref<StarSystem>> PirateClans::SystemsOf(const cs::List<HavenEntry>& havens)
	{
		cs::HashSet<cs::Ref<StarSystem>> set;
		for (const auto& [system, _] : havens) set.Add(system);
		return set;
	}

	cs::List<cs::Ref<Ship>> PirateClans::Bands(cs::Ref<Faction> clan, cs::Ref<StarSystem> system)
	{
		cs::List<cs::Ref<Ship>> list;
		for (const auto& s : system->World->Fleet->Ships())
			if (!s->Destroyed && !s->IsEscort() && !s->Runner && !s->Derelict && s->Faction == clan && s->Voyage == nullptr) list.Add(s);
		return list;
	}

	double PirateClans::Bounty(cs::Ref<Faction> clan)
	{
		double sum = 0;
		cs::Ref<cs::Dictionary<cs::Ref<Faction>, double>> by;
		if (clan != nullptr && pledges.TryGetValue(clan, by))
			for (auto [key, value] : *by) sum += value;
		return sum;
	}

	double PirateClans::Pledged(cs::Ref<Faction> state, cs::Ref<Faction> clan)
	{
		cs::Ref<cs::Dictionary<cs::Ref<Faction>, double>> by;
		double v = 0;
		return clan != nullptr && pledges.TryGetValue(clan, by) && by->TryGetValue(state, v) ? v : 0;
	}

	void PirateClans::Pledge(cs::Ref<Faction> state, cs::Ref<Faction> clan, double credits)
	{
		if (state == nullptr || clan == nullptr || state->IsPirate || !clan->IsPirate || credits <= 0) return;
		cs::Ref<cs::Dictionary<cs::Ref<Faction>, double>> by;
		if (!pledges.TryGetValue(clan, by)) pledges[clan] = by = cs::New<cs::Dictionary<cs::Ref<Faction>, double>>();
		double v = 0;
		by->TryGetValue(state, v);
		(*by)[state] = cs::Math::Min(MaxPledge, v + credits);
	}

	// ---------------------------------------------------------------- the day

	void PirateClans::Daily(double jd)
	{
		double fade = cs::Math::Exp(-1 / PledgeMemoryDays);
		for (const auto& by : pledges.Values())
			for (const auto& state : by->Keys()) (*by)[state] *= fade;
		for (const auto& clan : Clans())
		{
			auto havens = Havens(clan);
			if (havens.Count() == 0) continue;
			Smuggle(clan, havens, jd);
			int systems = SystemsOf(havens).Count();
			if (systems < MaxSystems && clan->Treasury >= ExpandTreasury && !resolved.ContainsKey(clan) && rng.NextDouble() < ExpandChancePerDay) resolved[clan] = jd;
			double resolvedJd = 0;
			if (resolved.TryGetValue(clan, resolvedJd))
			{
				// Until a band is free to go (a month at most).
				if (Expand(clan, jd) != nullptr || jd - resolvedJd > 30) resolved.Remove(clan);
			}
			else if (systems > 1 && rng.NextDouble() < MoveChancePerDay) MoveBand(clan, havens, jd);
			if (systems > 1 && rng.NextDouble() < SplitChancePerDay)
				for (const auto& [system, haven] : havens)
				{
					double since = 0;
					if (founded.TryGetValue(haven, since) && jd - since >= SplitMinDays && system->World != galaxy->Polities->CapitalSystemOf(clan))
					{
						Split(haven, jd);
						break;
					}
				}
			if (rng.NextDouble() < HuntChancePerDay) Hunt(clan, havens, jd);
		}
	}

	// ---------------------------------------------------------------- smuggling

	void PirateClans::Smuggle(cs::Ref<Faction> clan, const cs::List<HavenEntry>& havens, double jd)
	{
		for (const auto& [from, haven] : havens)
		{
			if (traffic->Out(from, VoyagePurpose::Smuggle) > 0 || BlackMarket::LootValue(haven) < MinSmuggleValue) continue;
			cs::Ref<Settlement> best = nullptr;
			cs::Ref<StarSystem> bestSystem = nullptr;
			GateRoute bestRoute = nullptr;
			double bestScore = 0;
			for (const auto& to : galaxy->Systems)
			{
				if (to == from) continue;
				for (const auto& market : to->World->Economy->Settlements)
				{
					if (!market->Lawless || market->Fallen) continue;
					auto route = traffic->Network()->Route(from, to, clan, jd);
					if (route == nullptr || route->Count() == 0) continue;
					double gain = Plan(haven, market, Economy::CapacityOf(ShipClass::Freighter), nullptr);
					double score = gain / (GateNetwork::RouteDays(route) + 10);
					if (gain >= MinSmuggleGain && score > bestScore)
					{
						bestScore = score;
						best = market;
						bestSystem = to;
						bestRoute = route;
					}
				}
			}
			if (best != nullptr) SendRunner(from, haven, bestSystem, best, bestRoute, jd);
		}
	}

	double PirateClans::Plan(cs::Ref<Settlement> haven, cs::Ref<Settlement> market, double capacity, cs::List<double>* load)
	{
		struct GoodMargin { Good g; double margin; };
		cs::List<GoodMargin> goods;
		for (int i = 0; i < GoodInfo::Count; i++)
		{
			auto g = static_cast<Good>(i);
			double margin = BlackMarket::Price(market, g) - GoodInfo::Of(g)->BasePrice * BlackMarket::HavenShare;
			if (margin > 0 && BlackMarket::Held(haven, g) > 1) goods.Add(GoodMargin{ g, margin });
		}
		goods.Sort([](const GoodMargin& a, const GoodMargin& b) { return cs::CompareTo(b.margin, a.margin); });
		double room = capacity, gain = 0;
		for (const auto& [g, _] : goods)
		{
			if (room <= 1) break;
			double t = cs::Math::Min(cs::Math::Min(BlackMarket::Held(haven, g), room), BlackMarket::Appetite(market, g));
			double clear = BlackMarket::SaleValue(market, g, t) - t * GoodInfo::Of(g)->BasePrice * BlackMarket::HavenShare;
			if (clear <= 0) continue;
			gain += clear;
			room -= t;
			if (load != nullptr) (*load)[static_cast<int>(g)] += t;
		}
		return gain;
	}

	cs::Ref<Ship> PirateClans::SendRunner(cs::Ref<StarSystem> from, cs::Ref<Settlement> haven, cs::Ref<StarSystem> to, cs::Ref<Settlement> market, GateRoute route, double jd)
	{
		auto clan = haven->Owner;
		if (!route) route = traffic->Network()->Route(from, to, clan, jd);
		if (route == nullptr || route->Count() == 0) return nullptr;
		auto runner = IdleRunner(from, haven);
		// One back through the gates but not yet in at the haven (still crossing from the gate it came out of) is
		// waited for, not replaced with a new hull (U.SPC-T304).
		if (runner == nullptr && !Returning(from, haven)) runner = BuildRunner(from, haven, jd);
		if (runner == nullptr) return nullptr;
		auto load = cs::NewArray<double>(GoodInfo::Count);
		Plan(haven, market, runner->CapacityTonnes, &load);
		double tonnes = 0;
		for (int i = 0; i < GoodInfo::Count; i++)
		{
			if (load[i] <= 0) continue;
			haven->Stock[i] = cs::Math::Max(0.0, haven->Stock[i] - load[i]);
			tonnes += load[i];
		}
		if (tonnes <= 0) return nullptr;
		runner->Plunder = load;
		runner->Cargo = cs::Cat("Stolen goods, bound for the black market at ", market->Name);
		auto v = traffic->SetOut(from, to, runner, VoyagePurpose::Smuggle, route, jd);
		v->TargetBody = market->Body;
		Runs++;
		from->World->Log(jd, cs::Cat(runner->Name, ", a ", clan->Name, " smuggler, slips out with ", cs::Fmt(tonnes, "N0"), " t of stolen goods for ", to->Name),
			WorldEventKind::Info, -1);
		return runner;
	}

	cs::Ref<Ship> PirateClans::IdleRunner(cs::Ref<StarSystem> system, cs::Ref<Settlement> haven)
	{
		for (const auto& s : system->World->Fleet->Ships())
			if (s->Runner && s->Faction == haven->Owner && InterstellarTraffic::Idle(system->World, s) && InterstellarTraffic::PortOf(s) == haven->Body) return s;
		return nullptr;
	}

	bool PirateClans::Returning(cs::Ref<StarSystem> system, cs::Ref<Settlement> haven)
	{
		for (const auto& s : system->World->Fleet->Ships())
			// (s.Ports != null: a null int[] is an empty List here, whose IndexOf is -1 all the same.)
			if (s->Runner && !s->Destroyed && !s->Derelict && s->Faction == haven->Owner && s->Voyage == nullptr && s->Ports.IndexOf(haven->Body) >= 0)
				return true;
		return false;
	}

	cs::Ref<Ship> PirateClans::BuildRunner(cs::Ref<StarSystem> system, cs::Ref<Settlement> haven, double jd)
	{
		auto clan = haven->Owner;
		auto world = system->World;
		double hands = Crews::Complement(ShipClass::Freighter);
		if (clan->Treasury < RunnerPrice() + 2e7 || clan->Manpower < hands) return nullptr;
		clan->Treasury -= RunnerPrice();
		Crews::Draft(clan, hands);
		int n = runnersBuilt++;
		const auto& names = RunnerNames();
		cs::String name = cs::Cat(names[n % names.Count()], n >= names.Count() ? cs::Cat(" ", n / names.Count() + 1) : cs::String());
		auto ship = world->Fleet->AddShip(name, ShipClass::Freighter, clan, ShipRole::Free);
		ship->Runner = true;
		ship->CapacityTonnes = Economy::CapacityOf(ShipClass::Freighter);
		ship->Route = cs::List<int>{ haven->Body };
		ship->Ports = cs::List<int>{ haven->Body };
		world->Fleet->Station(ship, haven->Body, jd);
		world->NotifyAdded(ship);
		return ship;
	}

	int PirateClans::SmugglerOrders(cs::Ref<StarSystem> system, cs::Ref<Ship> ship, int here, double jd)
	{
		auto v = ship->Voyage;
		cs::Ref<Settlement> market = nullptr;
		for (const auto& st : system->World->Economy->At(v->TargetBody))
			if (BlackMarket::Takes(st) && !st->Owner->IsPirate) market = st;
		if (market == nullptr)
		{
			// Its market is gone (taken, or never was): home with the goods.
			v->StayUntilJd = jd;
			return here;
		}
		if (here != market->Body) return market->Body;
		if (ship->Plunder.Count() > 0) // ship.Plunder != null
		{
			double value = 0, tonnes = 0;
			for (int i = 0; i < GoodInfo::Count; i++)
			{
				double t = ship->Plunder[i];
				if (t <= 0) continue;
				value += BlackMarket::Sell(market, static_cast<Good>(i), t);
				tonnes += t;
			}
			ship->Plunder = cs::List<double>();
			ship->Cargo = cs::String();
			ship->Faction->Treasury += value;
			ship->Credits += value;
			Sales++;
			SmuggledValue += value;
			system->World->Log(jd, cs::Cat(ship->Name, " (", ship->Faction->Name, ") sells ", cs::Fmt(tonnes, "N0"), " t of stolen goods on the black market at ", market->Name, " for ", cs::Fmt(value / 1e6, "0.0"), " M"),
				WorldEventKind::Info, market->Body);
		}
		v->StayUntilJd = cs::Math::Min(v->StayUntilJd, jd + 1);
		return here;
	}

	// ---------------------------------------------------------------- growing and moving

	cs::Ref<Ship> PirateClans::Expand(cs::Ref<Faction> clan, double jd)
	{
		auto havens = Havens(clan);
		if (havens.Count() == 0 || SystemsOf(havens).Count() >= MaxSystems || clan->Treasury < FoundingCost) return nullptr;
		for (const auto& s : galaxy->Systems)
			for (const auto& ship : s->World->Fleet->Ships())
				if (!ship->Destroyed && ship->Faction == clan && ship->Voyage != nullptr && ship->Voyage->Purpose == VoyagePurpose::Relocate) return nullptr;
		// A band already under way is retired out of every system's fleet while it rides inside the gate itself
		// (see InterstellarTraffic.InTransit): the loop above alone would miss it there and send a second one.
		for (const auto& ship : traffic->InTransit())
			if (ship->Faction == clan && ship->Voyage != nullptr && ship->Voyage->Purpose == VoyagePurpose::Relocate) return nullptr;
		auto held = SystemsOf(havens);
		cs::Ref<Ship> band = nullptr;
		cs::Ref<StarSystem> from = nullptr, to = nullptr;
		GateRoute bestRoute = nullptr;
		int site = -1;
		double bestDays = cs::Double::MaxValue;
		for (const auto& system : held)
		{
			auto ready = ReadyBand(clan, system);
			if (ready == nullptr) continue;
			for (const auto& target : galaxy->Systems)
			{
				if (held.Contains(target) || target->World->HavenSites.Count() == 0) continue;
				auto route = traffic->Network()->Route(system, target, clan, jd);
				if (route == nullptr || route->Count() == 0) continue;
				double days = GateNetwork::RouteDays(route);
				if (days < bestDays)
				{
					bestDays = days;
					band = ready;
					from = system;
					to = target;
					bestRoute = route;
					site = target->World->HavenSites[0];
				}
			}
		}
		if (band == nullptr) return nullptr;
		auto v = traffic->SetOut(from, to, band, VoyagePurpose::Relocate, bestRoute, jd);
		v->TargetBody = site;
		v->SiteName = (*to->Catalog)[site]->Name;
		clan->Treasury -= FoundingCost;
		Expansions++;
		from->World->Log(jd, cs::Cat(clan->Name, " raiders under ", band->Name, " slip away through the ", (*bestRoute)[0]->Name, " to found a hold beyond the gates"), WorldEventKind::Info, -1);
		return band;
	}

	cs::Ref<Ship> PirateClans::MoveBand(cs::Ref<Faction> clan, const cs::List<HavenEntry>& havens, double jd)
	{
		HavenEntry empty{}, crowded{};
		int most = 1;
		for (const auto& h : havens)
		{
			int n = Bands(clan, h.system).Count();
			if (n > most)
			{
				most = n;
				crowded = h;
			}
		}
		// An empty haven already being refilled (a band of the clan's on its way to it from another haven) is left
		// to that band: it is the moves to it that count, not those from it (U.SPC-T302).
		for (const auto& h : havens)
		{
			if (empty.haven != nullptr || Bands(clan, h.system).Count() != 0) continue;
			bool incoming = false;
			for (const auto& o : havens)
				if (o.system != h.system && traffic->Out(o.system, VoyagePurpose::Relocate, h.system) > 0) { incoming = true; break; }
			if (!incoming) empty = h;
		}
		if (empty.haven == nullptr || crowded.haven == nullptr || empty.system == crowded.system) return nullptr;
		auto band = ReadyBand(clan, crowded.system);
		if (band == nullptr) return nullptr;
		auto route = traffic->Network()->Route(crowded.system, empty.system, clan, jd);
		if (route == nullptr || route->Count() == 0) return nullptr;
		auto v = traffic->SetOut(crowded.system, empty.system, band, VoyagePurpose::Relocate, route, jd);
		v->TargetBody = empty.haven->Body;
		Moves++;
		crowded.system->World->Log(jd, cs::Cat(clan->Name, " raiders under ", band->Name, " slip away through the ", (*route)[0]->Name, " for the clan's hold at ", empty.system->Name), WorldEventKind::Info, -1);
		return band;
	}

	cs::Ref<Ship> PirateClans::ReadyBand(cs::Ref<Faction> clan, cs::Ref<StarSystem> system)
	{
		for (const auto& s : Bands(clan, system))
			if (InterstellarTraffic::Idle(system->World, s) && s->Phase == ShipPhase::Parked && s->Combat->Damage() <= 0.2 && s->Combat->Armed()) return s;
		return nullptr;
	}

	void PirateClans::Settle(cs::Ref<StarSystem> system, cs::Ref<Ship> band, double jd)
	{
		auto v = band->Voyage;
		auto world = system->World;
		auto clan = band->Faction;
		auto haven = world->War->LiveHavenOf(clan);
		if (haven == nullptr && v->TargetBody >= 0 && world->HavenSites.Contains(v->TargetBody))
		{
			haven = world->FoundHaven(!v->SiteName.empty() ? v->SiteName : (*system->Catalog)[v->TargetBody]->Name, v->TargetBody, clan, jd);
			founded[haven] = jd;
			Foundings++;
		}
		band->GateKm = std::nullopt;
		band->Role = v->HomeRole;
		if (haven == nullptr)
		{
			v->Purpose = VoyagePurpose::Raid;
			v->StayUntilJd = jd + 15;
			band->Route = cs::List<int>();
			band->Ports = world->FreeShipPorts();
			world->Fleet->ScheduleNext(band, jd);
			return;
		}
		band->Voyage = nullptr;
		band->Route = cs::List<int>{ haven->Body };
		band->Ports = world->FreeShipPorts();
		world->Fleet->ScheduleNext(band, jd);
		world->Log(jd, cs::Cat(clan->Name, " raiders under ", band->Name, " come through from ", v->Home->Name, " and make their hold in the dark"), WorldEventKind::Info, -1);
	}

	cs::Ref<Faction> PirateClans::Split(cs::Ref<Settlement> haven, double jd)
	{
		auto old = haven->Owner;
		auto world = galaxy->Polities->WorldOf(haven);
		if (old == nullptr || !old->IsPirate || world == nullptr) return nullptr;
		const auto& breakaway = BreakawayNames();
		cs::String name = cs::Cat(haven->Name, " ", breakaway[rng.Next(breakaway.Count())]);
		if (galaxy->Polities->Find(name) != nullptr) name = cs::Cat(haven->Name, " ", breakaway[Splits % breakaway.Count()], " ", Splits + 2);
		auto c = old->Color;
		auto clan = cs::New<Faction>(name, old->Prefix, Rgb(static_cast<float>(cs::Math::Min(1.0f, c.R * 0.8f + 0.15f)), c.G * 0.7f, static_cast<float>(cs::Math::Min(1.0f, c.B * 0.8f + 0.2f))));
		clan->Kind = PolityKind::PirateClan; clan->IsPirate = true; clan->MarineQuality = old->MarineQuality;
		galaxy->Polities->Found(clan, world);
		world->Fleet->AddFaction(clan);
		double purse = cs::Math::Max(0.0, old->Treasury) * 0.3;
		old->Treasury -= purse;
		clan->Treasury = purse;
		if (!cs::Double::IsPositiveInfinity(old->Manpower))
		{
			double hands = cs::Math::Max(0.0, old->Manpower) * 0.3;
			old->Manpower -= hands;
			clan->Manpower = hands;
		}
		clan->CrewMorale = old->CrewMorale;
		haven->Owner = clan;
		clan->Capital = haven;
		for (const auto& s : world->Fleet->Ships())
		{
			if (s->Destroyed || s->Faction != old) continue;
			auto lead = s->Controller();
			const auto& route = lead->Route;
			if (route.Count() > 0 && route[0] == haven->Body) s->Faction = clan; // route != null && route.Length > 0
		}
		if (world->Pirates == old) world->Pirates = clan;
		Splits++;
		world->Log(jd, cs::Cat(haven->Name, " breaks away from ", old->Name, ": its raiders sail as ", clan->Name, " now"), WorldEventKind::Diplomacy, haven->Discovered() ? haven->Body : -1);
		return clan;
	}

	// ---------------------------------------------------------------- the hunt

	void PirateClans::OnPlundered(cs::Ref<World> world, cs::Ref<Faction> clan, double value)
	{
		auto holder = Polities::HolderOf(world);
		if (holder != nullptr) Pledge(holder, clan, value * PledgeShare);
	}

	void PirateClans::OnBattleEnded(cs::Ref<World> world, cs::Ref<Battle> b, double jd)
	{
		if (b->Winner < 0) return;
		auto payee = b->SideLeaders[b->Winner];
		if (payee == nullptr || payee->IsPirate) return;
		double paid = 0;
		cs::Ref<Faction> clan = nullptr;
		for (const auto& u : b->Units)
		{
			// (A hull taken is counted on its captors' side by then: a clan's hull lost when someone else won is theirs.)
			if (!(u->Dead || u->Captured) || u->Ship == nullptr || u->Kind == UnitKind::Fighter) continue;
			auto f = u->Ship->Faction;
			if (f == nullptr || !f->IsPirate) continue;
			paid += Collect(f, Economy::HullPrice(u->Ship->Spec->Class) * HullBountyShare);
			clan = f;
		}
		if (paid <= 0) return;
		payee->Treasury += paid;
		BountiesPaid += paid;
		world->Log(jd, cs::Cat(payee->Name, " collects ", cs::Fmt(paid / 1e6, "0.0"), " M of the bounty on ", clan->Name), WorldEventKind::Info, -1);
	}

	double PirateClans::Collect(cs::Ref<Faction> clan, double amount)
	{
		double pool = Bounty(clan);
		if (pool <= 0 || amount <= 0) return 0;
		double take = cs::Math::Min(pool, amount);
		auto by = pledges.At(clan);
		for (const auto& state : by->Keys())
		{
			double share = take * by->At(state) / pool;
			by->At(state) -= share;
			state->Treasury -= share;
		}
		return take;
	}

	void PirateClans::Pursue(cs::Ref<Faction> clan, double jd) { Hunt(clan, Havens(clan), jd); }

	void PirateClans::Hunt(cs::Ref<Faction> clan, const cs::List<HavenEntry>& havens, double jd)
	{
		cs::Ref<cs::Dictionary<cs::Ref<Faction>, double>> by;
		if (!pledges.TryGetValue(clan, by)) return;
		// Navies: against a hidden haven somebody has found (an open one, like Blackwater, they leave be).
		for (const auto& [system, haven] : havens)
		{
			if (!haven->Hidden || !haven->Discovered()) continue;
			for (auto [state, pledged] : *by)
			{
				if (pledged < MinExpeditionPledge || !state->IsMilitary) continue;
				for (const auto& from : galaxy->Systems)
				{
					if (from == system || from->World->Sol != state || traffic->Out(from, VoyagePurpose::Expedition) > 0) continue;
					if (traffic->Network()->Route(from, system, state, jd) == nullptr) continue;
					// Word of where it is has reached them.
					haven->KnownTo.Add(state);
					if (traffic->DispatchExpedition(from, system, clan, jd))
					{
						Expeditions++;
						return;
					}
				}
			}
		}
		// Hunters: where the clan's bands are, from a system with a hunter free of contracts.
		if (Bounty(clan) < MinHuntBounty) return;
		cs::Ref<StarSystem> where = nullptr;
		int most = 0;
		for (const auto& s : galaxy->Systems)
		{
			int n = Bands(clan, s).Count();
			if (n > most)
			{
				most = n;
				where = s;
			}
		}
		if (where == nullptr) return;
		for (const auto& from : galaxy->Systems)
		{
			auto outlaws = from->World->Outlaws;
			if (from == where || outlaws == nullptr || traffic->Out(from, VoyagePurpose::Hunt) > 0) continue;
			for (const auto& h : outlaws->Hunters)
			{
				auto ship = h->Ship;
				if (!h->Alive() || h->Contract != nullptr || !InterstellarTraffic::Idle(from->World, ship) || ship->Phase != ShipPhase::Parked || ship->Combat->Damage() > 0.2) continue;
				auto route = traffic->Network()->Route(from, where, ship->Faction, jd);
				if (route == nullptr || route->Count() == 0) continue;
				auto v = traffic->SetOut(from, where, ship, VoyagePurpose::Hunt, route, jd);
				v->Quarry = clan;
				v->Origin = ship;
				Hunts++;
				from->World->Log(jd, cs::Cat("The bounty hunter ", ship->Name, " goes through the ", (*route)[0]->Name, " after ", clan->Name, " at ", where->Name, ": there is ", cs::Fmt(Bounty(clan) / 1e6, "0"), " M on their heads"),
					WorldEventKind::Info, InterstellarTraffic::PortOf(ship));
				return;
			}
		}
	}
}
