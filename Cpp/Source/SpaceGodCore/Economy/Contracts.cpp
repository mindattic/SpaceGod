// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Contracts.cs (US-T18).

#include "Economy/Contracts.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Combat/War.h"
#include "Economy/CargoHold.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/People.h"
#include "Economy/Settlement.h"
#include "Embodiment/Influence.h" // US-T140
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Law/Law.h"
#include "Law/Outlaws.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "World.h"

#include <tuple>

namespace SpaceGod::Core
{
	const char* ToString(ContractKind v)
	{
		switch (v)
		{
			case ContractKind::Freight: return "Freight";
			case ContractKind::Passengers: return "Passengers";
			case ContractKind::Colonists: return "Colonists";
			case ContractKind::Escort: return "Escort";
			case ContractKind::Patrol: return "Patrol";
			case ContractKind::Survey: return "Survey";
			case ContractKind::Bounty: return "Bounty";
		}
		return "";
	}

	const char* ToString(ContractState v)
	{
		switch (v)
		{
			case ContractState::Open: return "Open";
			case ContractState::Taken: return "Taken";
			case ContractState::Completed: return "Completed";
			case ContractState::Failed: return "Failed";
			case ContractState::Abandoned: return "Abandoned";
			case ContractState::Expired: return "Expired";
		}
		return "";
	}

	const char* ToString(Contracts::TakeOutcome v)
	{
		switch (v)
		{
			case Contracts::TakeOutcome::Ok: return "Ok";
			case Contracts::TakeOutcome::NotOpen: return "NotOpen";
			case Contracts::TakeOutcome::NotOffered: return "NotOffered";
			case Contracts::TakeOutcome::NotHere: return "NotHere";
			case Contracts::TakeOutcome::NoRoom: return "NoRoom";
			case Contracts::TakeOutcome::Unarmed: return "Unarmed";
			case Contracts::TakeOutcome::NoGoods: return "NoGoods";
		}
		return "";
	}

	// ---------------------------------------------------------------- Contract

	cs::String Contract::Title() const
	{
		cs::String dest = Destination != nullptr ? Destination->Name : cs::String();
		switch (Kind)
		{
			case ContractKind::Freight: return cs::Cat(cs::Fmt(Tonnes, "N0"), " t ", GoodInfo::Of(Good)->Name, " to ", dest);
			case ContractKind::Passengers: return cs::Cat(People, (People == 1 ? " passenger" : " passengers"), " to ", dest);
			case ContractKind::Colonists: return cs::Cat(cs::Fmt(People, "N0"), " colonists to ", dest);
			case ContractKind::Escort: return cs::Cat("Escort the ", (Client != nullptr ? Client->Name : cs::String("merchant")), " in to ", BodyLabel);
			case ContractKind::Patrol: return cs::Cat("Patrol ", BodyLabel, " for ", cs::Fmt(PatrolDays, "0.#"), " d");
			case ContractKind::Survey: return cs::Cat("Survey ", BodyLabel);
			default: return cs::Cat("Bounty on ", (Target != nullptr ? (Target->IsPlayer() ? cs::String("you") : Target->Name) : cs::String("a wanted captain")));
		}
	}

	// ---------------------------------------------------------------- Contracts

	Contracts::Contracts(cs::Ref<World> world, int seed)
		: rng(seed)
	{
		this->world = world;
		for (const auto& s : world->Economy->Settlements) Visited[s->Body] = cs::Double::NegativeInfinity;
	}

	cs::Ref<::SpaceGod::Core::Economy> Contracts::Economy() const { return world->Economy; }
	cs::Ref<::SpaceGod::Core::Law> Contracts::Law() const { return world->Law; }

	// ---------------------------------------------------------------- the clock

	void Contracts::Update(double jd)
	{
		if (cs::Double::IsNaN(nextDailyJd))
		{
			Post(jd);
			nextDailyJd = jd + 1;
			return;
		}
		while (jd >= nextDailyJd)
		{
			Review(nextDailyJd);
			Post(nextDailyJd);
			nextDailyJd += 1;
		}
	}

	// ---------------------------------------------------------------- the boards

	cs::List<cs::Ref<Contract>> Contracts::BoardAt(const cs::Ref<Settlement>& port) const
	{
		auto list = cs::List<cs::Ref<Contract>>();
		if (!Lawful(port)) return list;
		for (const auto& c : All)
			if (c->IsOpen() && c->Board == port) list.Add(c);
		list.Sort([](const cs::Ref<Contract>& a, const cs::Ref<Contract>& b) { return cs::CompareTo(b->Pay, a->Pay); });
		return list;
	}

	cs::List<cs::Ref<Contract>> Contracts::PlayerActive() const
	{
		auto list = cs::List<cs::Ref<Contract>>();
		for (const auto& c : All)
			if (c->IsActive() && c->ByPlayer) list.Add(c);
		return list;
	}

	cs::Ref<Contract> Contracts::ActiveFor(const cs::Ref<Ship>& ship) const
	{
		if (ship == nullptr) return nullptr;
		auto lead = ship->Controller();
		for (const auto& c : All)
			if (c->IsActive() && !c->ByPlayer && (c->Taker == lead || c->Taker == ship)) return c;
		return nullptr;
	}

	bool Contracts::Lawful(const cs::Ref<Settlement>& s)
	{
		return s != nullptr && s->Owner != nullptr && !s->Owner->IsPirate && !s->Fallen && s->Kind != SettlementKind::Haven;
	}

	int Contracts::OpenAt(const cs::Ref<Settlement>& board) const
	{
		int n = 0;
		for (const auto& c : All)
			if (c->IsOpen() && c->Board == board) n++;
		return n;
	}

	cs::Ref<Contract> Contracts::Add(const cs::Ref<Contract>& c, double jd)
	{
		c->Id = nextId++;
		c->PostedJd = jd;
		if (c->ExpiresJd <= jd) c->ExpiresJd = jd + OfferDays;
		if (c->Body >= 0 && c->Body < world->Catalog()->Count()) c->BodyLabel = (*world->Catalog())[c->Body]->Name;
		All.Add(c);
		return c;
	}

	// ---------------------------------------------------------------- pricing

	double Contracts::TravelDays(double km) { return 2 * cs::Math::Sqrt(cs::Math::Max(0.0, km) / 0.003) / Astro::SecondsPerDay + 1; }

	double Contracts::Price(double basePay, double perAu, double km, double danger, double urgency)
	{
		return cs::Math::Round((basePay + perAu * km / Astro::AuKm) * (1 + 2 * Clamp01(danger)) * (1 + 0.5 * Clamp01(urgency)) * 0.01) * 100;
	}

	double Contracts::Clamp01(double v) { return cs::Math::Max(0.0, cs::Math::Min(1.0, v)); }

	double Contracts::DangerAt(int body) const
	{
		return world->War == nullptr || body < 0 ? 0 : 1 - cs::Math::Exp(-world->War->Danger(body) / 3);
	}

	double Contracts::Km(int a, int b, double jd) const
	{
		return a == b || a < 0 || b < 0 ? 0 : (world->Ephemeris->HelioAt(a, jd) - world->Ephemeris->HelioAt(b, jd)).Length();
	}

	// ---------------------------------------------------------------- posting

	void Contracts::Post(double jd)
	{
		PostFreight(jd);
		PostPeople(jd);
		PostEscorts(jd);
		PostPatrols(jd);
		PostSurveys(jd);
		PostBounties(jd);
	}

	double Contracts::OnContract(const cs::Ref<Settlement>& to, Good g) const
	{
		double t = 0;
		for (const auto& c : All)
			if (c->Kind == ContractKind::Freight && c->Destination == to && c->Good == g && (c->IsOpen() || (c->IsActive() && c->ByPlayer)))
				t += cs::Math::Max(0.0, c->Tonnes - c->Delivered);
		return t;
	}

	bool Contracts::OpenFreight(const cs::Ref<Settlement>& to, Good g) const
	{
		for (const auto& c : All)
			if (c->Kind == ContractKind::Freight && c->IsOpen() && c->Destination == to && c->Good == g) return true;
		return false;
	}

	void Contracts::PostFreight(double jd)
	{
		for (const auto& to : Economy()->Settlements)
		{
			if (!Lawful(to)) continue;
			for (int i = 0; i < GoodInfo::Count; i++)
			{
				auto g = static_cast<Good>(i);
				double target = to->Target(g);
				if (to->Demand[i] <= 0 || target <= 0) continue;
				double expected = to->Stock[i] + to->Inbound[i];
				double cover = expected / target;
				if (cover >= ShortCover || OpenFreight(to, g)) continue;
				double want = target * RestoreCover - expected - OnContract(to, g);
				double crate = CargoFootprint::Of(g).Tonnes;
				if (want < crate * ParcelCrates) continue;
				auto from = Supplier(to, g, cs::Math::Min(want, crate * ParcelCrates), jd);
				if (from == nullptr || OpenAt(from) >= BoardCap) continue;
				double urgency = 1 - cover / ShortCover;
				double bulk = cs::Math::Min(BulkLotTonnes, want - crate * ParcelCrates);
				if (bulk >= crate * ParcelCrates * 2 && from->Available(g) >= bulk)
					PostFreightLot(from, to, g, cs::Math::Floor(bulk / crate) * crate, cover, urgency, jd);
				PostFreightLot(from, to, g, crate * ParcelCrates, cover, urgency, jd);
			}
		}
	}

	void Contracts::PostFreightLot(const cs::Ref<Settlement>& from, const cs::Ref<Settlement>& to, Good g, double tonnes, double cover, double urgency, double jd)
	{
		double km = Km(from->Body, to->Body, jd);
		double danger = DangerAt(to->Body);
		// The goods at a fair price, and the carriage: a fee a run and so much a tonne for every AU.
		double goods = tonnes * GoodInfo::Of(g)->BasePrice;
		double pay = Price(goods + 20000 + goods * 0.15, 30000 + goods * 0.1, km, danger, urgency);
		if (to->Owner->Treasury < pay * 2) return;
		auto c = cs::New<Contract>();
		c->Kind = ContractKind::Freight;
		c->Board = from;
		c->Patron = to->Owner;
		c->Destination = to;
		c->Body = to->Body;
		c->Good = g;
		c->Tonnes = tonnes;
		c->CoverAtPost = cover;
		c->Pay = pay;
		c->DistanceKm = km;
		c->Danger = danger;
		c->Urgency = urgency;
		c->DeadlineDays = cs::Math::Round(cs::Math::Max(5.0, TravelDays(km) * 3 * (1 - 0.4 * urgency)));
		Add(c, jd);
	}

	cs::Ref<Settlement> Contracts::Supplier(const cs::Ref<Settlement>& to, Good g, double tonnes, double jd) const
	{
		cs::Ref<Settlement> best = nullptr;
		double bestKm = cs::Double::MaxValue;
		for (const auto& s : Economy()->Settlements)
		{
			if (s == to || s->Body == to->Body || !Lawful(s) || s->Available(g) < tonnes) continue;
			if (world->Diplomacy()->Hostile(s->Owner, to->Owner)) continue;
			double km = Km(s->Body, to->Body, jd);
			if (km < bestKm)
			{
				bestKm = km;
				best = s;
			}
		}
		return best;
	}

	void Contracts::PostPeople(double jd)
	{
		for (const auto& from : Economy()->Settlements)
		{
			if (!Lawful(from) || from->NetMigration >= -0.5 || OpenAt(from) >= BoardCap) continue;
			double outflow = -from->NetMigration;
			cs::Ref<Settlement> best = nullptr;
			for (const auto& to : Economy()->Settlements)
			{
				if (to == from || to->Body == from->Body || !Lawful(to) || to->NetMigration <= 0 || !People::CanMigrate(world->Diplomacy(), from, to)) continue;
				if (best == nullptr || to->NetMigration > best->NetMigration) best = to;
			}
			if (best == nullptr) continue;
			double urgency = Clamp01(outflow / (ColonistOutflow * 5));
			if (!HasOpen(ContractKind::Passengers, from, best))
			{
				int people = cs::ToInt(cs::Math::Max(4.0, cs::Math::Min(60.0, cs::Math::Round(outflow * 3))));
				PostPeopleLot(ContractKind::Passengers, from, best, people, from->Owner, urgency, jd);
			}
			if (outflow >= ColonistOutflow && !HasOpen(ContractKind::Colonists, from, best))
			{
				int people = cs::ToInt(cs::Math::Max(100.0, cs::Math::Min(4000.0, cs::Math::Round(outflow * 20 / 10) * 10)));
				PostPeopleLot(ContractKind::Colonists, from, best, people, best->Owner, urgency, jd);
			}
		}
	}

	bool Contracts::HasOpen(ContractKind kind, const cs::Ref<Settlement>& board, const cs::Ref<Settlement>& to) const
	{
		for (const auto& c : All)
			if (c->Kind == kind && c->Board == board && c->Destination == to && (c->IsOpen() || c->IsActive())) return true;
		return false;
	}

	void Contracts::PostPeopleLot(ContractKind kind, const cs::Ref<Settlement>& from, const cs::Ref<Settlement>& to, int people, const cs::Ref<Faction>& patron, double urgency, double jd)
	{
		// Passage is how migrants travel, not more of them: never more than a sliver, nor below six tenths of the founding people.
		if (people > from->Population * 0.05 || from->Population - people < from->InitialPopulation * MinPeopleShare) return;
		double km = Km(from->Body, to->Body, jd);
		double danger = DangerAt(to->Body);
		double fare = kind == ContractKind::Passengers ? 2500 : 150;
		double perAu = kind == ContractKind::Passengers ? 3000 : 200;
		double pay = Price(people * fare, people * perAu, km, danger, urgency);
		if (patron->Treasury < pay * 2) return;
		auto c = cs::New<Contract>();
		c->Kind = kind;
		c->Board = from;
		c->Patron = patron;
		c->Destination = to;
		c->Body = to->Body;
		c->People = people;
		c->Pay = pay;
		c->DistanceKm = km;
		c->Danger = danger;
		c->Urgency = urgency;
		c->DeadlineDays = cs::Math::Round(cs::Math::Max(6.0, TravelDays(km) * 3));
		Add(c, jd);
	}

	void Contracts::PostEscorts(double jd)
	{
		for (const auto& s : world->Fleet->Ships())
		{
			if (s->Destroyed || s->IsEscort() || !world->IsTrader(s) || s == world->PilotedShip || !s->HasCargo || s->Phase != ShipPhase::Transit) continue;
			if (s->Faction == nullptr || s->Faction->IsPirate || s->DestinationBody < 0 || s->PhaseEndJd - jd < 0.5) continue;
			double value = s->CargoTonnes * GoodInfo::Of(s->CargoGood)->BasePrice;
			double danger = DangerAt(s->DestinationBody);
			if (value < EscortCargoValue || danger < EscortDanger || HasEscort(s)) continue;
			cs::Ref<Settlement> board = nullptr;
			for (const auto& st : Economy()->At(s->OriginBody))
				if (Lawful(st) && (board == nullptr || st->Population > board->Population)) board = st;
			if (board == nullptr || OpenAt(board) >= BoardCap) continue;
			double km = Km(s->OriginBody, s->DestinationBody, jd);
			double pay = Price(60000 + value * 0.02, 40000, km, danger, Clamp01(1 - (s->PhaseEndJd - jd) / 10));
			if (s->Faction->Treasury < pay * 2) continue;
			auto c = cs::New<Contract>();
			c->Kind = ContractKind::Escort;
			c->Board = board;
			c->Patron = s->Faction;
			c->Client = s;
			c->Body = s->DestinationBody;
			c->Pay = pay;
			c->DistanceKm = km;
			c->Danger = danger;
			c->Urgency = Clamp01(1 - (s->PhaseEndJd - jd) / 10);
			c->ExpiresJd = s->PhaseEndJd;
			c->DeadlineDays = s->PhaseEndJd - jd + EscortWindowDays;
			Add(c, jd);
		}
	}

	bool Contracts::HasEscort(const cs::Ref<Ship>& client) const
	{
		for (const auto& c : All)
			if (c->Kind == ContractKind::Escort && c->Client == client && (c->IsOpen() || c->IsActive())) return true;
		return false;
	}

	void Contracts::PostPatrols(double jd)
	{
		for (const auto& s : Economy()->Settlements)
		{
			if (!Lawful(s) || OpenAt(s) >= BoardCap) continue;
			double danger = DangerAt(s->Body);
			if (danger < PatrolDanger) continue;
			bool posted = false;
			for (const auto& c : All)
				if (c->Kind == ContractKind::Patrol && c->Body == s->Body && (c->IsOpen() || c->IsActive())) posted = true;
			if (posted) continue;
			double pay = Price(120000, 0, 0, danger, danger);
			if (s->Owner->Treasury < pay * 2) continue;
			auto c = cs::New<Contract>();
			c->Kind = ContractKind::Patrol;
			c->Board = s;
			c->Patron = s->Owner;
			c->Body = s->Body;
			c->PatrolDays = PatrolStationDays;
			c->Pay = pay;
			c->Danger = danger;
			c->Urgency = danger;
			c->DeadlineDays = 12;
			Add(c, jd);
		}
	}

	void Contracts::PostSurveys(double jd)
	{
		auto capitals = cs::Dictionary<cs::Ref<Faction>, cs::Ref<Settlement>>();
		for (const auto& s : Economy()->Settlements)
		{
			cs::Ref<Settlement> c;
			if (Lawful(s) && (!capitals.TryGetValue(s->Owner, c) || s->Population > c->Population)) capitals[s->Owner] = s;
		}
		auto hidden = cs::HashSet<int>();
		for (const auto& s : Economy()->Settlements)
			if (s->Hidden) hidden.Add(s->Body);
		for (const auto& board : capitals.Values())
		{
			int open = 0;
			for (const auto& c : All)
				if (c->Kind == ContractKind::Survey && c->Board == board && c->IsOpen()) open++;
			if (open >= 2 || OpenAt(board) >= BoardCap) continue;
			auto candidates = cs::List<std::tuple<int, double>>();
			for (const auto& b : world->Catalog()->Bodies())
			{
				if (!b->IsVisible() || b->IsStar() || b->Index == world->Catalog()->Root()->Index || hidden.Contains(b->Index) || Visited.ContainsKey(b->Index)) continue;
				if (SurveyPosted(b->Index)) continue;
				if (world->Fleet->Lanes != nullptr && world->Fleet->Lanes->SystemOf(b->Index) < 0) continue;
				candidates.Add(std::tuple<int, double>(b->Index, Km(board->Body, b->Index, jd)));
			}
			candidates.Sort([](const std::tuple<int, double>& a, const std::tuple<int, double>& b) { return cs::CompareTo(std::get<1>(a), std::get<1>(b)); });
			for (int i = 0; i < candidates.Count() && open < 2; i++, open++)
			{
				auto [body, km] = candidates[i];
				double danger = DangerAt(body);
				double pay = Price(50000, 40000, km, danger, 0);
				if (board->Owner->Treasury < pay * 2) break;
				auto c = cs::New<Contract>();
				c->Kind = ContractKind::Survey;
				c->Board = board;
				c->Patron = board->Owner;
				c->Body = body;
				c->Pay = pay;
				c->DistanceKm = km;
				c->Danger = danger;
				c->ExpiresJd = jd + SurveyOfferDays;
				c->DeadlineDays = cs::Math::Round(cs::Math::Max(10.0, TravelDays(km) * 3));
				Add(c, jd);
			}
		}
	}

	bool Contracts::SurveyPosted(int body) const
	{
		for (const auto& c : All)
			if (c->Kind == ContractKind::Survey && c->Body == body && (c->IsOpen() || c->IsActive())) return true;
		return false;
	}

	void Contracts::PostBounties(double jd)
	{
		if (world->Outlaws == nullptr) return;
		for (const auto& p : world->Outlaws->Postings())
		{
			if (p->IsPlayer() || p->TotalBounty < Outlaws::MinContract) continue;
			cs::Ref<Contract> existing = nullptr;
			for (const auto& c : All)
				if (c->Kind == ContractKind::Bounty && c->Target == p->Target && (c->IsOpen() || c->IsActive())) existing = c;
			if (existing != nullptr)
			{
				existing->Pay = p->TotalBounty;
				continue;
			}
			cs::Ref<Faction> patron = nullptr;
			for (auto [key, value] : p->Target->Bounty)
				if (patron == nullptr || value > p->Target->BountyBy(patron)) patron = key;
			cs::Ref<Settlement> board = nullptr;
			for (const auto& s : Economy()->Settlements)
				if (Lawful(s) && s->Owner == patron && (board == nullptr || s->Population > board->Population)) board = s;
			if (board == nullptr) continue;
			int body = p->LastSeen ? p->LastSeen->Body : -1;
			auto c = cs::New<Contract>();
			c->Kind = ContractKind::Bounty;
			c->Board = board;
			c->Patron = patron;
			c->Target = p->Target;
			c->Body = body;
			c->Pay = p->TotalBounty;
			c->Danger = 1;
			c->Urgency = p->Outlaw != nullptr && p->Outlaw->Feared ? 1 : 0.5;
			c->DistanceKm = Km(board->Body, body, jd);
			c->ExpiresJd = jd + Outlaws::ContractDays;
			c->DeadlineDays = Outlaws::ContractDays;
			Add(c, jd);
		}
	}

	// ---------------------------------------------------------------- taking

	int Contracts::Berths(const cs::Ref<Ship>& ship, bool player)
	{
		if (ship == nullptr) return 0;
		if (player) return ship->PersonalHold()->Width * ship->PersonalHold()->Height * 10;
		switch (ship->Spec->Class)
		{
			case ShipClass::Courier: return 60;
			case ShipClass::Freighter: return 5000;
			default: return 0;
		}
	}

	cs::Ref<LawRecord> Contracts::RecordFor(const cs::Ref<Ship>& ship, bool player) const
	{
		auto law = Law();
		return player ? (law ? law->Player : nullptr) : (law ? law->RecordOf(ship->Controller()) : nullptr);
	}

	Contracts::TakeOutcome Contracts::CanTake(const cs::Ref<Contract>& c, const cs::Ref<Ship>& ship, bool player, double jd) const
	{
		if (c == nullptr || !c->IsOpen()) return TakeOutcome::NotOpen;
		if (ship == nullptr || ship->Destroyed) return TakeOutcome::NotHere;
		auto law = Law();
		auto rec = player ? (law ? law->Player : nullptr) : (law ? law->FindRecord(ship->Controller()) : nullptr);
		if (rec != nullptr && !Law::OffersContracts(rec, c->Patron)) return TakeOutcome::NotOffered;
		if (player && !PlayerNear(ship, c->Board->Body, jd)) return TakeOutcome::NotHere;
		switch (c->Kind)
		{
			case ContractKind::Passengers:
			case ContractKind::Colonists:
				if (!player && !At(ship, c->Board->Body)) return TakeOutcome::NotHere;
				if (Berths(ship, player) < c->People) return TakeOutcome::NoRoom;
				break;
			case ContractKind::Freight:
				if (player) { if (ship->PersonalHold()->Width * ship->PersonalHold()->Height <= 0) return TakeOutcome::NoRoom; }
				else
				{
					if (!At(ship, c->Board->Body)) return TakeOutcome::NotHere;
					if (ship->HasCargo || !Economy::Carries(ship->Spec->Class, c->Good) || ship->CapacityTonnes < c->Tonnes) return TakeOutcome::NoRoom;
					// A lot booked for a port its flag may not use (an embargo, a war) is none for this captain to take.
					if (c->Destination != nullptr && !Economy()->CanTrade(ship, c->Destination)) return TakeOutcome::NotOffered;
					if (c->Board->Available(c->Good) < c->Tonnes || ship->Faction->Treasury < c->Board->PurchaseCost(c->Good, c->Tonnes) * (1 + Economy::PortTax)) return TakeOutcome::NoGoods;
				}
				break;
			case ContractKind::Escort:
			case ContractKind::Patrol:
			case ContractKind::Bounty:
				if (ship->Combat == nullptr || !ship->Combat->Armed()) return TakeOutcome::Unarmed;
				break;
			default:
				break;
		}
		return TakeOutcome::Ok;
	}

	Contracts::TakeOutcome Contracts::Take(const cs::Ref<Contract>& c, const cs::Ref<Ship>& ship, bool player, double jd)
	{
		auto outcome = CanTake(c, ship, player, jd);
		if (outcome != TakeOutcome::Ok) return outcome;
		auto taker = player ? ship : ship->Controller();
		c->State = ContractState::Taken;
		c->Taker = taker;
		c->ByPlayer = player;
		c->TakerRecord = RecordFor(ship, player);
		c->TakerFaction = taker->Faction;
		c->TakenJd = jd;
		c->DueJd = jd + c->DeadlineDays;
		switch (c->Kind)
		{
			case ContractKind::Passengers:
			case ContractKind::Colonists:
				c->Board->Population -= c->People;
				c->Board->Emigrated += c->People;
				c->Aboard = true;
				if (!player) taker->Cargo = c->Title();
				break;
			case ContractKind::Freight:
				if (!player)
				{
					Economy()->LoadFor(taker, c->Board, c->Good, c->Tonnes, c->Destination);
					taker->Cargo += " (contract)";
				}
				break;
			default:
				break;
		}
		cs::String who = player ? cs::String("You take") : cs::Cat("The ", taker->Name, " takes");
		world->Log(jd, cs::Cat(who, " a contract at ", c->Board->Name, ": ", c->Title(), " for ", Cr(c->Pay), " cr, due in ", cs::Fmt(c->DeadlineDays, "0"), " d"), WorldEventKind::Info, c->Board->Body);
		return TakeOutcome::Ok;
	}

	bool Contracts::Abandon(const cs::Ref<Contract>& c, double jd)
	{
		if (c == nullptr || !c->IsActive()) return false;
		Close(c, ContractState::Abandoned, jd, "given up");
		return true;
	}

	// ---------------------------------------------------------------- NPC captains

	int Contracts::ChooseNext(const cs::Ref<Ship>& ship, int here, double jd)
	{
		if (ship == nullptr || ship == world->PilotedShip || ship->IsEscort()) return -1;
		auto active = ActiveFor(ship);
		if (active != nullptr) return active->Body >= 0 && active->Body != here ? active->Body : -1;
		if (ship->HasCargo || ship->Faction == nullptr || ship->Faction->IsPirate || rng.NextDouble() > NpcTakeChance) return -1;
		cs::Ref<Contract> best = nullptr;
		double bestScore = 0;
		for (const auto& st : Economy()->At(here))
		{
			if (!Lawful(st)) continue;
			for (const auto& c : All)
			{
				if (!c->IsOpen() || c->Board != st || !Suits(ship, c) || CanTake(c, ship, false, jd) != TakeOutcome::Ok) continue;
				double days = c->Body == here ? 1 : world->Fleet->EstimateTravelDays(ship, here, c->Body, jd) + 1;
				if (days > c->DeadlineDays) continue;
				double cost = c->Kind == ContractKind::Freight ? c->Board->PurchaseCost(c->Good, c->Tonnes) : 0;
				double score = (c->Pay - cost) / days;
				if (score > bestScore)
				{
					bestScore = score;
					best = c;
				}
			}
		}
		if (best == nullptr || Take(best, ship, false, jd) != TakeOutcome::Ok) return -1;
		return best->Body;
	}

	bool Contracts::Suits(const cs::Ref<Ship>& ship, const cs::Ref<Contract>& c)
	{
		switch (c->Kind)
		{
			case ContractKind::Freight: return ship->Role == ShipRole::Trader || ship->Role == ShipRole::Miner || ship->Role == ShipRole::Courier;
			case ContractKind::Passengers: return ship->Role == ShipRole::Courier || ship->Role == ShipRole::Trader;
			case ContractKind::Colonists: return ship->Role == ShipRole::Trader;
			case ContractKind::Survey: return ship->Role == ShipRole::Free || ship->Role == ShipRole::Courier;
			default: return false;
		}
	}

	int Contracts::ChooseForHunter(const cs::Ref<Ship>& leader, int here, double jd)
	{
		auto active = ActiveFor(leader);
		if (active != nullptr) return active->Kind == ContractKind::Bounty ? -1 : active->Body;
		cs::Ref<Contract> best = nullptr;
		double bestScore = 0;
		for (const auto& c : All)
		{
			if (!c->IsOpen() || (c->Kind != ContractKind::Patrol && c->Kind != ContractKind::Escort) || CanTake(c, leader, false, jd) != TakeOutcome::Ok) continue;
			double days = c->Body == here ? 0.5 : world->Fleet->EstimateTravelDays(leader, here, c->Body, jd);
			double window = c->Kind == ContractKind::Escort ? c->ExpiresJd - jd + EscortWindowDays : c->DeadlineDays - c->PatrolDays;
			if (days > window) continue;
			if (world->War != nullptr && world->War->DefenceAt(c->Body, leader->Faction) > world->War->GroupStrength(leader) * 1.2) continue;
			double score = c->Pay / (days + 1);
			if (score > bestScore)
			{
				bestScore = score;
				best = c;
			}
		}
		if (best == nullptr || Take(best, leader, false, jd) != TakeOutcome::Ok) return -1;
		return best->Body;
	}

	bool Contracts::OnArrived(const cs::Ref<Ship>& ship, int body, double jd)
	{
		if (ship == nullptr) return false;
		Visited[body] = jd;
		bool holding = false;
		auto snapshot = cs::List<cs::Ref<Contract>>(All);
		for (const auto& c : snapshot)
		{
			if (c->Kind == ContractKind::Escort && c->Client == ship && cs::Double::IsNaN(c->ClientArrivedJd) && body == c->Body)
			{
				c->ClientArrivedJd = jd;
				if (c->IsActive() && TakerAt(c, body, jd)) Close(c, ContractState::Completed, jd, cs::Cat("the ", ship->Name, " is in safe"));
				continue;
			}
			// Bound for a place since abandoned: nothing to deliver to (the day's review calls it off).
			if (!c->IsActive() || c->ByPlayer || (c->Taker != ship && c->Taker != ship->Controller()) || DestinationGone(c)) continue;
			switch (c->Kind)
			{
				case ContractKind::Freight:
					if (!CarriesLot(c)) break;
					if (body == c->Body) DeliverNpcFreight(c, jd);
					holding = true;
					break;
				case ContractKind::Passengers:
				case ContractKind::Colonists:
					if (body == c->Body) Close(c, ContractState::Completed, jd, "delivered");
					break;
				case ContractKind::Survey:
					if (body == c->Body) Close(c, ContractState::Completed, jd, "surveyed");
					break;
				case ContractKind::Escort:
					if (body == c->Body && !cs::Double::IsNaN(c->ClientArrivedJd) && jd <= c->ClientArrivedJd + EscortWindowDays)
						Close(c, ContractState::Completed, jd, cs::Cat("the ", c->Client->Name, " seen in"));
					break;
				default:
					break;
			}
		}
		return holding;
	}

	bool Contracts::CarriesLot(const cs::Ref<Contract>& c)
	{
		auto s = c->Taker;
		return s != nullptr && !s->Destroyed && s->HasCargo && s->CargoGood == c->Good && s->CargoDestination == c->Destination;
	}

	void Contracts::DeliverNpcFreight(const cs::Ref<Contract>& c, double jd)
	{
		auto ship = c->Taker;
		Economy()->Release(ship);
		c->Destination->Stock[static_cast<int>(c->Good)] += ship->CargoTonnes;
		c->Delivered = ship->CargoTonnes;
		c->Destination->RefreshPrices();
		ship->HasCargo = false;
		ship->CargoTonnes = 0;
		// What the lot cost comes off its earnings (as a sale's would), the pay going on below: its profit, not its takings.
		ship->Credits -= ship->CargoCost;
		ship->CargoCost = 0;
		ship->CargoBonus = 0;
		ship->CargoDestination = nullptr;
		ship->Cargo = cs::String();
		Close(c, ContractState::Completed, jd, "delivered");
	}

	// ---------------------------------------------------------------- the player at the helm

	void Contracts::UpdatePilot(const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, double jd)
	{
		if (ship == nullptr || ship->Destroyed || body < 0) return;
		double last = pilotJd;
		pilotBody = body;
		pilotOffset = offsetKm;
		pilotJd = jd;
		if (!PlayerNear(ship, body, jd)) return;
		Visited[body] = jd;
		auto snapshot = cs::List<cs::Ref<Contract>>(All);
		for (const auto& c : snapshot)
		{
			if (!c->IsActive() || !c->ByPlayer || c->Body != body || DestinationGone(c)) continue;
			switch (c->Kind)
			{
				case ContractKind::Survey:
					Close(c, ContractState::Completed, jd, "surveyed");
					break;
				case ContractKind::Passengers:
				case ContractKind::Colonists:
					if (c->Taker == ship) Close(c, ContractState::Completed, jd, "put ashore");
					break;
				case ContractKind::Freight:
					HandOver(c, ship, jd);
					break;
				case ContractKind::Patrol:
					if (!cs::Double::IsNaN(last) && jd > last) c->OnStation += cs::Math::Min(jd - last, 0.1);
					if (c->OnStation >= c->PatrolDays) Close(c, ContractState::Completed, jd, "patrol flown");
					break;
				case ContractKind::Escort:
					if (!cs::Double::IsNaN(c->ClientArrivedJd) && jd <= c->ClientArrivedJd + EscortWindowDays)
						Close(c, ContractState::Completed, jd, cs::Cat("the ", c->Client->Name, " seen in"));
					break;
				default:
					break;
			}
		}
	}

	double Contracts::HandOver(const cs::Ref<Contract>& c, const cs::Ref<Ship>& ship, double jd)
	{
		if (c == nullptr || !c->IsActive() || c->Kind != ContractKind::Freight || ship == nullptr || DestinationGone(c)) return 0;
		double handed = 0;
		auto hold = ship->PersonalHold();
		auto items = cs::List<cs::Ref<CargoItem>>(hold->Items());
		for (const auto& item : items)
		{
			if (c->Delivered >= c->Tonnes - 1e-6) break;
			if (item->Good != c->Good || item->Stolen) continue;
			hold->Remove(item);
			c->Delivered += item->Tonnes();
			handed += item->Tonnes();
			c->Destination->Stock[static_cast<int>(c->Good)] += item->Tonnes();
		}
		if (handed <= 0) return 0;
		c->Destination->RefreshPrices();
		if (c->Delivered >= c->Tonnes - 1e-6) Close(c, ContractState::Completed, jd, "delivered");
		else if (auto law = Law()) law->Notice(cs::Cat("Handed over ", cs::Fmt(handed, "N0"), " t ", GoodInfo::Of(c->Good)->Name, " at ", c->Destination->Name, ": ", cs::Fmt(c->Delivered, "N0"), " of ", cs::Fmt(c->Tonnes, "N0"), " t"));
		return handed;
	}

	bool Contracts::PlayerNear(const cs::Ref<Ship>& ship, int body, double jd) const
	{
		return ship != nullptr && ship == world->PilotedShip && pilotBody == body && !cs::Double::IsNaN(pilotJd) && cs::Math::Abs(jd - pilotJd) < 0.25 &&
			body >= 0 && body < world->Catalog()->Count() && pilotOffset.Length() <= ScaleModel::NeighbourhoodKm((*world->Catalog())[body]);
	}

	bool Contracts::At(const cs::Ref<Ship>& ship, int body)
	{
		auto c = ship->Controller();
		if (c->Destroyed || c->Orbit == nullptr || c->Orbit->Body != body) return false;
		return c->Phase == ShipPhase::Parked || c->Phase == ShipPhase::Queued || c->Planetside();
	}

	bool Contracts::TakerAt(const cs::Ref<Contract>& c, int body, double jd) const
	{
		return c->ByPlayer ? PlayerNear(c->Taker, body, jd) || (c->Taker != world->PilotedShip && At(c->Taker, body)) : At(c->Taker, body);
	}

	// ---------------------------------------------------------------- review

	void Contracts::Review(double jd)
	{
		auto snapshot = cs::List<cs::Ref<Contract>>(All);
		for (const auto& c : snapshot)
		{
			if (c->IsOpen())
			{
				if (jd >= c->ExpiresJd || !StillNeeded(c)) Close(c, ContractState::Expired, jd, cs::String());
				else if (c->Kind == ContractKind::Bounty) MirrorHunter(c, jd);
				continue;
			}
			if (!c->IsActive()) continue;
			auto t = c->Taker;
			// Taken as a prize (by pirates, or a navy at war with its flag), the hull is its captor's, not the taker's.
			bool lost = t == nullptr || t->Destroyed || t->Derelict || t->GateDeparted || (c->TakerRecord != nullptr && !c->ByPlayer && t->Faction != nullptr && t->Faction->IsPirate)
				|| (!c->ByPlayer && c->TakerFaction != nullptr && t->Faction != c->TakerFaction);
			if (c->ByPlayer && lost && t != nullptr && !t->Destroyed && !t->Derelict) lost = false;
			if (lost)
			{
				Close(c, ContractState::Failed, jd, t != nullptr && t->Destroyed ? cs::Cat("the ", t->Name, " was lost") : cs::String("the taker is gone"));
				continue;
			}
			// Its destination was abandoned to ruin after it was taken: nobody is left there to take the goods or
			// the people. Called off, the taker not to blame; a merchant's lot goes back to the open market.
			if (DestinationGone(c))
			{
				if (!c->ByPlayer && c->Kind == ContractKind::Freight && CarriesLot(c))
				{
					Economy()->Release(t);
					t->CargoDestination = nullptr;
				}
				Close(c, ContractState::Failed, jd, cs::Cat(c->Destination->Name, " is no more"), true, /*blame:*/ false);
				continue;
			}
			bool next = false;
			switch (c->Kind)
			{
				case ContractKind::Freight:
					if (!c->ByPlayer && !CarriesLot(c)) { Close(c, ContractState::Failed, jd, "the cargo went elsewhere"); next = true; }
					break;
				case ContractKind::Escort:
					if (c->Client == nullptr || c->Client->Destroyed || c->Client->Faction != c->Patron) { Close(c, ContractState::Failed, jd, cs::Cat("the ", (c->Client ? c->Client->Name : cs::String()), " was lost")); next = true; break; }
					if (!cs::Double::IsNaN(c->ClientArrivedJd))
					{
						if (TakerAt(c, c->Body, jd)) { Close(c, ContractState::Completed, jd, cs::Cat("the ", c->Client->Name, " seen in")); next = true; break; }
						if (jd > c->ClientArrivedJd + EscortWindowDays) { Close(c, ContractState::Failed, jd, cs::Cat("not there when the ", c->Client->Name, " came in")); next = true; break; }
					}
					break;
				case ContractKind::Patrol:
					if (!c->ByPlayer && At(c->Taker, c->Body)) c->OnStation += 1;
					if (c->OnStation >= c->PatrolDays) { Close(c, ContractState::Completed, jd, "patrol flown"); next = true; }
					break;
				case ContractKind::Bounty:
					if (ReviewBounty(c, jd)) next = true;
					break;
				default:
					break;
			}
			// (C#'s `continue` from inside the switch.)
			if (next) continue;
			if (jd > c->DueJd) Close(c, ContractState::Failed, jd, "overdue");
		}
		if (All.Count() > 0) Trim();
	}

	bool Contracts::StillNeeded(const cs::Ref<Contract>& c) const
	{
		if (!Lawful(c->Board)) return false;
		switch (c->Kind)
		{
			case ContractKind::Freight:
			{
				auto to = c->Destination;
				if (!Lawful(to)) return false;
				double target = to->Target(c->Good);
				return target > 0 && (to->Stock[static_cast<int>(c->Good)] + to->Inbound[static_cast<int>(c->Good)]) / target < RestoreCover;
			}
			case ContractKind::Passengers:
			case ContractKind::Colonists:
				return Lawful(c->Destination) && c->Board->Population - c->People >= c->Board->InitialPopulation * MinPeopleShare;
			case ContractKind::Escort:
				return c->Client != nullptr && !c->Client->Destroyed && cs::Double::IsNaN(c->ClientArrivedJd) && c->Client->Faction == c->Patron;
			case ContractKind::Patrol:
				return DangerAt(c->Body) >= PatrolDanger * 0.5;
			case ContractKind::Survey:
				return !Visited.ContainsKey(c->Body);
			case ContractKind::Bounty:
			{
				auto o = world->Outlaws ? world->Outlaws->Of(c->Target) : nullptr;
				return c->Target->Wanted() && (o == nullptr || o->AtLarge());
			}
			default: return true;
		}
	}

	void Contracts::MirrorHunter(const cs::Ref<Contract>& c, double jd)
	{
		auto h = world->Outlaws ? world->Outlaws->HunterOn(c->Target) : nullptr;
		if (h == nullptr) return;
		c->State = ContractState::Taken;
		c->Taker = h->Ship;
		auto law = Law();
		c->TakerRecord = law ? law->RecordOf(h->Ship) : nullptr;
		c->TakenJd = jd;
		c->DueJd = h->Contract != nullptr ? h->Contract->UntilJd : jd + Outlaws::ContractDays;
	}

	bool Contracts::ReviewBounty(const cs::Ref<Contract>& c, double jd)
	{
		auto o = world->Outlaws ? world->Outlaws->Of(c->Target) : nullptr;
		bool ended = o != nullptr ? !o->AtLarge() : !c->Target->Wanted();
		if (!c->ByPlayer)
		{
			auto h = world->Outlaws ? world->Outlaws->HunterOf(c->Taker) : nullptr;
			if (ended)
			{
				// C#'s `FateBy != null`: a null string is the empty string here.
				bool theirs = o != nullptr && h != nullptr && !o->FateBy.empty() && cs::Strings::Contains(o->FateBy, h->Name());
				Close(c, theirs ? ContractState::Completed : ContractState::Expired, jd, theirs ? cs::String("collected") : cs::String(), /*pay:*/ false);
				return true;
			}
			if (h == nullptr || h->Contract == nullptr || h->Contract->Target != c->Target)
			{
				Close(c, ContractState::Abandoned, jd, "dropped");
				return true;
			}
			return false;
		}
		if (ended)
		{
			// The player's own kill is closed by OnBattleEnded; here only someone else's.
			Close(c, ContractState::Expired, jd, cs::String());
			return true;
		}
		return false;
	}

	void Contracts::OnBattleEnded(const cs::Ref<Battle>& b, double jd)
	{
		if (b == nullptr) return;
		auto snapshot = cs::List<cs::Ref<Contract>>(All);
		for (const auto& c : snapshot)
		{
			if (!c->IsActive() || !c->ByPlayer || c->Kind != ContractKind::Bounty) continue;
			for (const auto& u : b->Units)
			{
				if (u->Ship == nullptr || !(u->Dead || u->Captured) || u->Ship->IsEscort()) continue;
				auto law = Law();
				if ((law ? law->FindRecord(u->Ship) : nullptr) != c->Target) continue;
				bool playerOpposite = false;
				for (const auto& v : b->Units)
					if (v->Side == 1 - u->FoughtOn() && v->PlayerFlown) playerOpposite = true;
				// A captain who got away in the pods is still at large: the contract stands.
				auto outlaw = world->Outlaws ? world->Outlaws->Of(c->Target) : nullptr;
				if (playerOpposite && (outlaw == nullptr || !outlaw->AtLarge()))
					// The bounty itself is the law's to pay (a captain in the brig handed over, a kill on a scanned
					// wreck): the patron does not pay it a second time.
					Close(c, ContractState::Completed, jd, u->Dead ? cs::String("killed") : cs::String("taken"), /*pay:*/ false);
				break;
			}
		}
	}

	// ---------------------------------------------------------------- closing

	bool Contracts::DestinationGone(const cs::Ref<Contract>& c)
	{
		return (c->Kind == ContractKind::Freight || c->Kind == ContractKind::Passengers || c->Kind == ContractKind::Colonists) &&
			c->Destination != nullptr && c->Destination->Fallen;
	}

	void Contracts::Close(const cs::Ref<Contract>& c, ContractState state, double jd, const cs::String& why, bool pay, bool blame)
	{
		auto was = c->State;
		c->State = state;
		c->ClosedJd = jd;
		if (was == ContractState::Open) return;
		auto rec = c->TakerRecord;
		cs::String who = c->ByPlayer ? cs::String("you") : cs::Cat("the ", (c->Taker ? c->Taker->Name : cs::String("taker")));
		auto law = Law();
		if (state == ContractState::Completed)
		{
			if (c->Aboard)
			{
				c->Destination->Population += c->People;
				c->Aboard = false;
			}
			if (pay && c->Pay > 0)
			{
				c->Patron->Treasury -= c->Pay;
				if (c->Taker != nullptr)
				{
					c->Taker->Faction->Treasury += c->Pay;
					if (!c->ByPlayer) c->Taker->Credits += c->Pay;
				}
			}
			Standing(rec, c->Patron, CompleteStanding + UrgentStanding * c->Urgency);
			if (!c->ByPlayer && c->Taker != nullptr && (c->Kind == ContractKind::Passengers || c->Kind == ContractKind::Colonists)) c->Taker->Cargo = cs::String();
			world->Log(jd, cs::Cat("Contract done: ", c->Title(), " (", why, ") by ", who, (pay ? cs::Cat(", ", Cr(c->Pay), " cr from ", c->Patron->Name) : cs::String())), WorldEventKind::Info, c->Body);
			if (c->ByPlayer && law) law->Notice(cs::Cat("Contract complete: ", c->Title(), (pay ? cs::Cat(": ", c->Patron->Name, " pays ", Cr(c->Pay), " cr") : cs::String())));
			// US-T140: a mission carried through at a helm is the SpaceGod's Influence (the pay and standing are the pilot's).
			if (c->ByPlayer) SpaceGodInfluence::Grant(world, InfluenceSource::Mission, 40 + cs::Math::Min(60.0, c->Pay / 20000.0), cs::Cat("mission: ", c->Title()));
			return;
		}
		if (state == ContractState::Expired) return;
		if (c->Aboard)
		{
			// A hull gone through a gate or sent to the breakers is marked destroyed too, but nobody died with it:
			// its people go back ashore where they boarded rather than vanishing.
			bool lostWithHull = c->Taker == nullptr || (c->Taker->Destroyed && !c->Taker->GateDeparted && !c->Taker->Scrapped);
			if (!lostWithHull)
			{
				c->Board->Population += c->People;
				c->Board->Emigrated = cs::Math::Max(0.0, c->Board->Emigrated - c->People);
			}
			c->Aboard = false;
			if (lostWithHull) world->Log(jd, cs::Cat(cs::Fmt(c->People, "N0"), " ", (c->Kind == ContractKind::Colonists ? "colonists" : "passengers"), " bound for ", c->Destination->Name, " are lost with the ", (c->Taker ? c->Taker->Name : cs::String())), WorldEventKind::Loss, -1);
		}
		if (!c->ByPlayer && c->Taker != nullptr && (c->Kind == ContractKind::Passengers || c->Kind == ContractKind::Colonists)) c->Taker->Cargo = cs::String();
		cs::String what = state == ContractState::Abandoned ? "abandoned" : "failed";
		if (!blame)
		{
			world->Log(jd, cs::Cat("Contract called off: ", c->Title(), " by ", who, " (", why, ")"), WorldEventKind::Info, c->Body);
			if (c->ByPlayer && law) law->Notice(cs::Cat("Contract called off: ", c->Title(), " (", why, ")"));
			return;
		}
		Standing(rec, c->Patron, -(state == ContractState::Abandoned ? AbandonStanding : FailStanding));
		world->Log(jd, cs::Cat("Contract ", what, ": ", c->Title(), " by ", who, " (", why, "); ", c->Patron->Name, " will remember it"), WorldEventKind::Info, c->Body);
		if (c->ByPlayer && law) law->Notice(cs::Cat("Contract ", what, ": ", c->Title(), " (", why, "). Standing with ", c->Patron->Name, " falls"));
	}

	void Contracts::Standing(const cs::Ref<LawRecord>& r, const cs::Ref<Faction>& f, double delta)
	{
		if (r == nullptr || f == nullptr || f->IsPirate || delta == 0) return;
		(*r->Standing)[f] = r->StandingWith(f) + delta;
	}

	void Contracts::Trim()
	{
		int closed = 0;
		for (const auto& c : All) if (c->IsClosed()) closed++;
		for (int i = 0; i < All.Count() && closed > HistoryCap; )
		{
			if (All[i]->IsClosed())
			{
				All.RemoveAt(i);
				closed--;
			}
			else i++;
		}
	}

	cs::String Contracts::Describe(const cs::Ref<Contract>& c, double jd)
	{
		cs::String text = cs::Cat(c->Title(), "  \xC2\xB7  ", Cr(c->Pay), " cr  \xC2\xB7  ", c->Patron->Name);
		if (c->IsOpen()) text += cs::Cat("  \xC2\xB7  ", cs::Fmt(c->DeadlineDays, "0"), " d to do it, offer ends in ", cs::Fmt(cs::Math::Max(0.0, c->ExpiresJd - jd), "0"), " d");
		else if (c->IsActive()) text += cs::Cat("  \xC2\xB7  due in ", cs::Fmt(cs::Math::Max(0.0, c->DueJd - jd), "0.#"), " d");
		if (c->Kind == ContractKind::Freight && c->IsActive()) text += cs::Cat("  \xC2\xB7  ", cs::Fmt(c->Delivered, "N0"), " t handed over");
		if (c->Kind == ContractKind::Patrol && c->IsActive()) text += cs::Cat("  \xC2\xB7  ", cs::Fmt(c->OnStation, "0.0"), " d on station");
		if (c->Danger >= 0.3) text += "  \xC2\xB7  dangerous";
		if (c->Urgency >= 0.6) text += "  \xC2\xB7  urgent";
		return text;
	}

	cs::String Contracts::Cr(double v) { return cs::Fmt(v, "N0"); }
}
