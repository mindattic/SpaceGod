// Port of Unity.SpaceGod Core/World.cs (US-T8). The partial class's other files are World.Mining.cpp,
// World.Colonies.cpp and World.MiningConvoys.cpp; all four share World.h.

#include "World.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Combat/Battle.h"
#include "Combat/BulkFreighters.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Crews.h"
#include "Combat/Diplomacy.h"
#include "Combat/PatrolWings.h"
#include "Combat/Pursuit.h"
#include "Combat/Salvage.h"
#include "Combat/War.h"
#include "Combat/Wreck.h"
#include "Economy/BeltMining.h"
#include "Economy/Colonisation.h"
#include "Economy/Companies.h"
#include "Economy/Contracts.h"
#include "Economy/Economy.h"
#include "Economy/Escorts.h"
#include "Economy/Goods.h"
#include "Economy/Holdings.h"
#include "Economy/Industry.h"
#include "Economy/LocalTraffic.h"
#include "Economy/MarketShocks.h"
#include "Economy/People.h"
#include "Economy/Propulsion.h"
#include "Economy/Settlement.h"
#include "Economy/TradeDesk.h"
#include "Events/EventDirector.h"
#include "Events/EventSources.h"
#include "Events/Opportunity.h"
#include "Infrastructure/Infrastructure.h"
#include "Law/Law.h"
#include "Law/Outlaws.h"
#include "Sensors/Sensors.h"
#include "Systems/LaneUpkeep.h"
#include "Systems/Polities.h"
#include "Systems/StrategicCommand.h"

namespace SpaceGod::Core
{
	namespace
	{
		// World's members hide these type names inside its member functions (World::Fleet is a field), so its code
		// names the types through these aliases.
		using FleetT = ::SpaceGod::Core::Fleet;
		using EconomyT = ::SpaceGod::Core::Economy;
		using PolitiesT = ::SpaceGod::Core::Polities;
		using WarT = ::SpaceGod::Core::War;
		using InfrastructureT = ::SpaceGod::Core::Infrastructure;
		using LawT = ::SpaceGod::Core::Law;
		using SensorsT = ::SpaceGod::Core::Sensors;
		using OutlawsT = ::SpaceGod::Core::Outlaws;
		using PursuitT = ::SpaceGod::Core::Pursuit;
		using CompaniesT = ::SpaceGod::Core::Companies;
		using ContractsT = ::SpaceGod::Core::Contracts;
		using HoldingsT = ::SpaceGod::Core::Holdings;
		using ColonisationT = ::SpaceGod::Core::Colonisation;
		using LaneUpkeepT = ::SpaceGod::Core::LaneUpkeep;
		using EphemerisT = ::SpaceGod::Core::Ephemeris;
	}

	const char* ToString(WorldEventKind v)
	{
		switch (v)
		{
		case WorldEventKind::Info: return "Info";
		case WorldEventKind::Launch: return "Launch";
		case WorldEventKind::Battle: return "Battle";
		case WorldEventKind::Capture: return "Capture";
		case WorldEventKind::Diplomacy: return "Diplomacy";
		case WorldEventKind::Loss: return "Loss";
		case WorldEventKind::Opportunity: return "Opportunity";
		case WorldEventKind::Economy: return "Economy";
		case WorldEventKind::Market: return "Market";
		case WorldEventKind::Law: return "Law";
		}
		return "";
	}

	cs::Ref<BodyCatalog> World::Catalog() const { return Ephemeris->Catalog; }

	cs::Ref<::SpaceGod::Core::Diplomacy> World::Diplomacy() const { return Polities->Diplomacy; }

	cs::Ref<World> World::Make(cs::Ref<EphemerisT> ephemeris, double startJd, int seed, cs::Ref<PolitiesT> polities)
	{
		// The constructor is private, so the Ref is made here rather than through cs::New.
		cs::Ref<World> w(new World());
		w->Construct(ephemeris, startJd, seed, polities);
		return w;
	}

	void World::Construct(cs::Ref<EphemerisT> ephemeris, double startJd, int seed, cs::Ref<PolitiesT> polities)
	{
		Ephemeris = ephemeris;
		Polities = polities ? polities : cs::New<PolitiesT>();
		Polities->Register(shared_from_this());
		Fleet = cs::New<FleetT>(ephemeris, seed);
		Economy = cs::New<EconomyT>(Diplomacy(), startJd, seed + 1);
		Adventures = cs::New<EventDirector>(seed + 5);
		Shocks = cs::New<MarketShocks>(shared_from_this(), seed + 7);
		Adventures->Spawned += [this](cs::Ref<Opportunity> op)
		{
			Log(op->SpawnJd, cs::Cat(op->MarkerLabel(), ": ", op->Hook), WorldEventKind::Opportunity,
				op->Ship && op->Ship->Orbit ? op->Ship->Orbit->Body : op->Body);
		};
		Adventures->Resolved += [this](cs::Ref<Opportunity> op)
		{
			Log(op->ResolvedJd, cs::Cat(op->MarkerLabel(), op->State == OpportunityState::Completed ? " done: " : " lost: ", op->Outcome),
				WorldEventKind::Opportunity, op->Ship && op->Ship->Orbit ? op->Ship->Orbit->Body : op->Body);
		};
	}

	bool World::Coarse() const { return Fleet->Coarse(); }
	void World::SetCoarse(bool value) { Fleet->SetCoarse(value); }

	void World::Update(double jd)
	{
		Jd = jd;
		Fleet->Update(jd);
		Economy->AdvanceTo(jd);
		Companies->Update(jd);
		War->Update(jd);
		if (LaneUpkeep) LaneUpkeep->Update(jd);
		if (Sensors) Sensors->Update(jd);
		if (Infrastructure) Infrastructure->UpdateSatellites(jd);
		Shocks->Update(jd);
		Law->Update(jd);
		Outlaws->Update(jd);
		Contracts->Update(jd);
		if (Escorts) Escorts->Update(jd);
		Holdings->Update(jd);
		if (Trade) Trade->Update(jd);
		if (Mining) Mining->Update(jd);
		ReplaceWorksEscorts(jd);
		if (jd >= nextRecountJd)
		{
			Economy->RecountInbound(Fleet->Ships());
			nextRecountJd = jd + 1;
		}
	}

	void World::UpdateAdventures(double jd, DVec3 playerHelioKm)
	{
		if (War == nullptr) return;
		Adventures->Update(jd, EventSources::Of(shared_from_this(), jd), playerHelioKm, PilotedShip);
	}

	DVec3 World::WreckHelio(cs::Ref<Wreck> w, double jd)
	{
		auto lanes = Fleet->Lanes;
		if (w->Site == nullptr || lanes == nullptr) return Ephemeris->HelioAt(w->Body, jd) + w->Pos;
		auto hop = w->Site->Hop;
		auto a = Ephemeris->HelioAt(hop.From, jd) + w->Site->FromOffset;
		auto b = Ephemeris->HelioAt(hop.To, jd) + w->Site->ToOffset;
		return lanes->CurveKm(hop.Lane, a, b, w->Site->Progress, jd) + w->Site->Side + w->Local;
	}

	DVec3 World::PodHelio(cs::Ref<EscapePod> pod, double jd)
	{
		auto lanes = Fleet->Lanes;
		if (pod->Site == nullptr || lanes == nullptr) return Ephemeris->HelioAt(pod->Body, jd) + pod->PosAt(jd);
		auto hop = pod->Site->Hop;
		auto a = Ephemeris->HelioAt(hop.From, jd) + pod->Site->FromOffset;
		auto b = Ephemeris->HelioAt(hop.To, jd) + pod->Site->ToOffset;
		return lanes->CurveKm(hop.Lane, a, b, pod->Site->Progress, jd) + pod->Site->Side + pod->LocalAt(jd);
	}

	PodState World::PickUpPods(cs::Ref<EscapePod> pod, cs::Ref<Ship> ship, double jd)
	{
		if (pod == nullptr || ship == nullptr || War == nullptr) return pod ? pod->State : PodState::Lost;
		return War->Crews->Collect(pod, ship->Faction, ship, Diplomacy(), jd, /*payRescuer:*/ false);
	}

	cs::Ref<Opportunity> World::FinishSalvage(cs::Ref<SalvageSession> session, double jd)
	{
		if (session == nullptr) return nullptr;
		session->CastOff();
		if (!session->Worked()) return nullptr;
		int body = session->Derelict && session->Derelict->Orbit ? session->Derelict->Orbit->Body : session->Wreck ? session->Wreck->Body : -1;
		Log(jd, cs::Cat("The ", session->Player->Name, " ", session->Summary(), " aboard ", session->Name()), session->Claimed ? WorldEventKind::Capture : WorldEventKind::Info, body);
		return Adventures ? Adventures->HulkWorked(session, jd) : nullptr;
	}

	void World::NotifyAdded(cs::Ref<Ship> ship) { ShipAdded.Invoke(ship); }

	void World::Log(double jd, cs::String text, WorldEventKind kind, int body)
	{
		auto e = WorldEvent(jd, text, kind, body);
		Events.Add(e);
		if (Events.Count() > 400) Events.RemoveRange(0, Events.Count() - 400);
		Logged.Invoke(e);
	}

	bool World::IsTrader(cs::Ref<Ship> s) const { return !s->IsEscort() && (s->Role == ShipRole::Trader || s->Role == ShipRole::Miner || s->Role == ShipRole::Courier); }

	bool World::BurnsPropellant(cs::Ref<Ship> s) const
	{
		return !s->IsEscort() && !s->Spec->IsMilitary && s->Voyage == nullptr && !s->PatrolWing && s->Role != ShipRole::Commuter &&
			s->Role != ShipRole::Tender && s->Role != ShipRole::Tug && s->Role != ShipRole::Hauler;
	}

	void World::PayToll(cs::Ref<Ship> ship, LaneHop hop, double jd)
	{
		// An ore tug on its works' heavy lanes rides at its works' charge, like a tender at its upkeep (U.SPC-T498).
		if (ship->Spec->IsMilitary || ship->PatrolWing || ship->Faction->IsPirate || ship == PilotedShip || ship->Role == ShipRole::Tender || ship->Role == ShipRole::Tug ||
			ship->Role == ShipRole::Hauler) return;
		double toll = EconomyT::LaneToll(ship->Spec->Class, ship->MassTonnes());
		ship->Faction->Treasury -= toll;
		ship->Credits -= toll;
		auto keeper = LaneKeeper(hop.From);
		if (keeper != nullptr) keeper->Treasury += toll;
	}

	cs::Ref<Faction> World::LaneKeeper(int system)
	{
		auto lanes = Fleet->Lanes;
		cs::Ref<Settlement> best = nullptr;
		for (const auto& s : Economy->Settlements)
		{
			if (s->Fallen || s->Owner == nullptr || s->Owner->IsPirate || lanes == nullptr || lanes->SystemOf(s->Body) != system) continue;
			if (best == nullptr || s->Population > best->Population) best = s;
		}
		return best ? best->Owner : nullptr;
	}

	// ---------------------------------------------------------------- polities

	cs::Ref<Faction> World::Polity(cs::String name, cs::String prefix, Rgb color, PolityKind kind, double treasury, double marines)
	{
		auto f = Polities->Find(name);
		if (f == nullptr)
		{
			f = cs::New<Faction>(name, prefix, color);
			f->Kind = kind;
			f->MarineQuality = marines;
			f->IsPirate = kind == PolityKind::PirateClan;
			Polities->Found(f, shared_from_this());
		}
		f->Treasury += treasury;
		return Fleet->AddFaction(f);
	}

	cs::Ref<Settlement> World::FoundHaven(cs::String name, int body, cs::Ref<Faction> clan, double jd)
	{
		if (clan == nullptr || !clan->IsPirate) return nullptr;
		Fleet->AddFaction(clan);
		auto s = Economy->Add(name, body, SettlementKind::Haven, clan, FoundedHavenPopulation);
		s->Hidden = true;
		Subsist(s, [](cs::Ref<Settlement> st, Good g, double perDay) { st->Facilities.Add(cs::New<Facility>(Recipe::Mine(g, perDay), 1)); },
			[](cs::Ref<Settlement> st, IndustryKind k, double level) { st->Facilities.Add(cs::New<Facility>(Recipe::Of(k), level)); });
		Economy->Open(s);
		// Stocked for a start, as havens are at the beginning.
		for (int i = 0; i < GoodInfo::Count; i++) s->Stock[i] = s->Target(static_cast<Good>(i));
		s->RefreshPrices();
		s->MaxPlatforms = 1;
		s->Platforms.Add(cs::New<ShipCombat>(CombatSpec::Platform()));
		HavenSites.Remove(body);
		bool ownClan = false;
		for (const auto& h : Economy->Settlements)
			if (h != s && h->Kind == SettlementKind::Haven && !h->Fallen && h->Owner == Pirates) ownClan = true;
		if (!ownClan) Pirates = clan;
		PirateGroupCap = cs::Math::Max(PirateGroupCap, 1);
		PirateIncomePerDay = cs::Math::Max(PirateIncomePerDay, 2e5);
		HunterCap = cs::Math::Max(HunterCap, 1);
		Log(jd, cs::Cat("A new raider hold is whispered of somewhere in the dark: ", clan->Name, " have come to ", Catalog()->Root()->Name), WorldEventKind::Info, -1);
		return s;
	}

	cs::Ref<Faction> World::UnknownRaiders() { return Polity("Unknown", "", Rgb(0.8f, 0.3f, 0.3f), PolityKind::PirateClan, 0); }

	void World::SeatCapitals()
	{
		for (const auto& f : Fleet->Factions())
			if (f->Capital == nullptr) Polities->CapitalOf(f);
	}

	void World::MusterReserves() { Crews::MusterReserves(Fleet->Factions(), Polities->Settlements()); }

	cs::Ref<Faction> World::Holder() { return PolitiesT::HolderOf(shared_from_this()); }

	// ---------------------------------------------------------------- the default system

	cs::Ref<World> World::CreateDefault(cs::Ref<EphemerisT> ephemeris, double jd, int seed, cs::Ref<PolitiesT> polities)
	{
		double startJd = jd - 45;
		auto w = Make(ephemeris, startJd, seed, polities);
		auto fleet = w->Fleet;
		auto economy = w->Economy;

		// The core: the Federation governs Earth, the Moon and Mars, and from Earth the frontier it holds beyond the gates.
		w->Sol = w->Polity(SolFederation, "SFS", SolFederationColor, PolityKind::State, 4e9, 1.25);
		w->Belt = w->Polity("Belt Consortium", "BCS", Rgb(1.0f, 0.6f, 0.25f), PolityKind::State, 2.5e9, 1.0);
		w->Reach = w->Polity("Outer Reach Compact", "ORS", Rgb(0.72f, 0.5f, 1.0f), PolityKind::State, 3.5e9, 1.15);
		w->Independents = w->Polity("Independent", "MV", Rgb(0.85f, 0.85f, 0.78f), PolityKind::FreeTraders, 6e8, 0.6);
		w->Pirates = w->Polity("Black Banner", "", Rgb(0.95f, 0.22f, 0.2f), PolityKind::PirateClan, 2e8, 1.1);
		w->Sol->IsMilitary = w->Belt->IsMilitary = w->Reach->IsMilitary = true;
		w->Diplomacy()->Set(w->Sol, w->Reach, Relation::War);
		w->RivalA = w->Sol;
		w->RivalB = w->Reach;

		w->BuildSettlements();
		w->Wire(startJd, seed, Hyperlanes::CreateDefault(ephemeris));
		auto war = w->War;
		w->Infrastructure->AddSolSpacecraft();
		w->Infrastructure->AddSolLagrangeStations(w->Sol, w->Belt);
		w->Infrastructure->AddSolRings(w->Economy);
		w->Fleet->Rings.AddRange(w->Infrastructure->Rings);

		w->BuildMilitary(startJd);
		World* wp = w.get();
		w->BuildMerchants(startJd, FreighterNames(), MinerNames(), CourierNames(), cs::List<cs::Ref<Faction>>{ w->Independents, w->Independents, w->Sol, w->Belt, w->Reach, w->Independents },
			[wp](int i) { return i % 3 == 2 ? wp->Reach : wp->Belt; });
		using Co = std::tuple<cs::String, cs::String, cs::String, cs::Ref<Faction>, bool>;
		w->FoundCompanies(startJd, cs::List<Co>{
			Co("Kepler Freight Lines", "KFL", "Earth Orbital", w->Sol, false),
			Co("Ares Haulage", "AH", "Ares Prime", w->Sol, true),
			Co("Jovian Bulk Carriers", "JBC", "Ganymede Union", w->Belt, false),
			Co("Vesta Ore and Haulage", "VOH", "Ceres Hub", w->Belt, true),
			Co("Kraken Hydrocarbon Company", "KHC", "Kraken Port", w->Reach, false),
			Co("Meridian Free Traders", "MFT", "Tycho City", w->Independents, false),
			Co("Long Haul Carrying Company", "LHC", "Ares Prime", w->Independents, false) });
		w->AddFreeShips(startJd, cs::List<cs::String>{ "Kestrel's Folly", "Wanderlust", "Solitaire", "Driftwood", "Errant", "Idle Hours" },
			cs::List<cs::String>{ "Prospector Nine", "Lucky Strike" }, cs::List<cs::String>{ "Far Horizon", "Stargazer" });
		int haven = (*w->Catalog())["Deimos"]->Index;
		war->RaisePirateGroup(startJd, haven);
		// The Banner's second band sails from its biggest belt hideout; recruiting fills the other
		// havens first (it raises each new band at the standing haven with the fewest).
		war->RaisePirateGroup(startJd, (*w->Catalog())[std::get<1>(BeltHavens()[0])]->Index);
		// Each navy's standing patrol wings fly a beat of its ports (U.SPC-T371).
		PatrolWings::Raise(w, startJd);
		// And its ports' ordinary traffic: buses, taxis and people's own craft (U.SPC-T375).
		LocalTraffic::Raise(w, startJd);
		// Its belt works and the builds they feed (U.SPC-T330).
		w->SeedSolMining(jd);
		w->SeatCapitals();
		w->MusterReserves();

		w->Update(jd);
		return w;
	}

	void World::Wire(double startJd, int seed, cs::Ref<Hyperlanes> lanes)
	{
		auto economy = Economy;
		auto fleet = Fleet;
		economy->Initialise();
		War = cs::New<WarT>(shared_from_this(), startJd, seed + 2);
		auto war = War;
		Sensors = cs::New<SensorsT>(shared_from_this());
		// The player's standing is one table: adventures seen through raise it, crimes lower it.
		Law = cs::New<LawT>(shared_from_this(), Adventures->Reputation);
		// The law judges a battle first; then the outlaws' records take in what their bands did in it.
		Outlaws = cs::New<OutlawsT>(shared_from_this(), seed + 11);
		Pursuit = cs::New<PursuitT>(shared_from_this(), seed + 13);
		// (Each `x += Obj.Method` binds the object it names now, as a C# method-group delegate does.)
		{
			auto outlaws = Outlaws;
			auto law = Law;
			war->BattleStarted += [outlaws](cs::Ref<Battle> b) { outlaws->OnBattleStarted(b); };
			war->BattleEnded += [law](cs::Ref<Battle> b, double jd) { law->OnBattleEnded(b, jd); };
			war->BattleEnded += [outlaws](cs::Ref<Battle> b, double jd) { outlaws->OnBattleEnded(b, jd); };
			Law->Committed += [outlaws](cs::Ref<Crime> c) { outlaws->OnCrime(c); };
			war->Crews->Resolved += [outlaws](cs::Ref<EscapePod> pod, cs::Ref<Ship> ship) { outlaws->OnPodResolved(pod, ship); };
		}
		// The boards post from the markets, migration, the war's danger and the law's books (U.SPC-T101).
		Contracts = cs::New<ContractsT>(shared_from_this(), seed + 17);
		Escorts = cs::New<MerchantEscorts>(shared_from_this(), seed + 19);
		{
			auto contracts = Contracts;
			war->BattleEnded += [contracts](cs::Ref<Battle> b, double jd) { contracts->OnBattleEnded(b, jd); };
		}
		// Each subsystem its own offset: two on one seed would draw the very same "random" numbers in step.
		Holdings = cs::New<HoldingsT>(shared_from_this(), seed + 23);
		Trade = cs::New<TradeDesk>(shared_from_this(), seed + 41);
		WireMining(seed + 31);
		{
			auto law = Law;
			economy->DockAllowed = [law](cs::Ref<Ship> ship, cs::Ref<Settlement> s) { return law->AllowsDocking(ship, s); };
			economy->PriceFactor = [law](cs::Ref<Ship> ship, cs::Ref<Settlement> s) { return law->PriceFactorFor(ship, s); };
		}
		Infrastructure = cs::New<InfrastructureT>(Catalog(), Ephemeris);
		Infrastructure->AddSettlementStructures(economy, seed + 3);
		WireColonisation();
		// Merchants and couriers go down to the surface through the rings; warships and pirates hold orbit, and so does a
		// bulk freighter, built in space and never landing: it trades from orbit (U.SPC-T482).
		fleet->Lands = [this](cs::Ref<Ship> s) { return IsTrader(s) && !BulkFreighters::NeverLands(s); };

		fleet->Lanes = lanes;
		// The lanes wear, and the tenders their keepers raise keep them up (U.SPC-T481).
		// (LaneUpkeep's C++ constructor takes a raw World*.)
		LaneUpkeep = cs::New<LaneUpkeepT>(this, seed + 43);
		// Drive-appropriate trajectories: torches flip and burn, slow boats fall along transfer orbits.
		fleet->OrbitalTransfers = true;
		economy->EstimateTravelDays = [fleet](cs::Ref<Ship> ship, int from, int to, double jd) { return fleet->EstimateTravelDays(ship, from, to, jd); };
		// Civilian hulls fly on real propellant and buy it in port; traders price every run by it.
		// (Navies' own supply -- tankers and depots -- is a war-logistics matter, left to their stores for now.)
		fleet->BurnsPropellant = [this](cs::Ref<Ship> s) { return BurnsPropellant(s); };
		fleet->CanRefuelAt = [economy](cs::Ref<Ship> ship, int body) { return economy->HasPropellantAt(ship, body); };
		fleet->ForceTopUp = [economy](cs::Ref<Ship> ship, int body, double jd) { economy->Refuel(ship, body, jd, /*force:*/ true); };
		economy->EstimateTrip = [fleet](cs::Ref<Ship> ship, int from, int to, double jd) { return fleet->EstimateTrip(ship, from, to, jd); };
		fleet->RanDry = [this](cs::Ref<Ship> ship, double when)
		{
			Log(when, cs::Cat(ship->Name, " has run its tanks dry off ", (*Catalog())[ship->Orbit->Body]->Name, " and calls for a tanker"), WorldEventKind::Info, ship->Orbit->Body);
		};
		fleet->TankerArrived = [this](cs::Ref<Ship> ship, double tonnes, double when)
		{
			if (tonnes <= 0) return;
			double fee = Propulsion::TankerFee(tonnes);
			ship->Faction->Treasury -= fee;
			ship->Credits -= fee;
			Log(when, cs::Cat("A tanker reaches ", ship->Name, " with ", cs::Fmt(tonnes, "N0"), " t of reaction mass"), WorldEventKind::Info, ship->Orbit->Body);
		};
		fleet->LaneJoined = [this](cs::Ref<Ship> ship, LaneHop hop, double when)
		{
			PayToll(ship, hop, when);
			// A worn lane's rings may slip under the rider (U.SPC-T481).
			LaneUpkeep->OnLaneJoined(ship, hop, when);
		};
		// Now and then a tank is holed on the way: the ship runs dry and calls for help.
		fleet->LeakChance = PropellantLeakChance;
		fleet->Leaked = [this](cs::Ref<Ship> ship, double tonnes, double when)
		{
			Log(when, cs::Cat(ship->Name, " vents ", cs::Fmt(tonnes, "N0"), " t of reaction mass from a holed tank"), WorldEventKind::Info, ship->Orbit ? ship->Orbit->Body : -1);
		};
		economy->Danger = [war](int body) { return war->Danger(body) * 0.08; };
		// The merchant fleet belongs to trading companies, which buy hulls from their profits: a yard lays down a
		// company's order first, then the navies take their slips. A system without companies keeps the old way:
		// the yards' owners keep the merchant fleet up to strength before the navies get theirs.
		Companies = cs::New<CompaniesT>(shared_from_this(), seed + 29);
		{
			auto companies = Companies;
			// economy.Loaded / economy.Sold are multicast delegate fields in C#: `+=` keeps whoever subscribed first
			// (MarketShocks, in the constructor, subscribes to Sold) and calls them in order. Plain cs::Action fields, so
			// chained by hand (Cs.h gap: no multicast Action).
			auto loadedBefore = economy->Loaded;
			economy->Loaded = [loadedBefore, companies](cs::Ref<Ship> ship, cs::Ref<Settlement> from)
			{
				if (loadedBefore) loadedBefore(ship, from);
				companies->OnLoaded(ship, from);
			};
			auto soldBefore = economy->Sold;
			economy->Sold = [soldBefore, companies](cs::Ref<Ship> ship, cs::Ref<Settlement> to, Good good, double tonnes)
			{
				if (soldBefore) soldBefore(ship, to, good, tonnes);
				companies->OnSold(ship, to, good, tonnes);
			};
			economy->RouteFactor = [](cs::Ref<Ship> ship, cs::Ref<Settlement> from, cs::Ref<Settlement> to) { return CompaniesT::RouteFactor(ship, from, to); };
			economy->PaidFor = [companies](cs::Ref<Settlement> yard, ShipClass cls, double price) { return companies->PaidFor(yard, cls, price); };
			ShipAdded += [companies](cs::Ref<Ship> ship) { companies->OnShipAdded(ship); };
		}
		// Each polity's production focus steers which of its industries grow (U.SPC-T423).
		economy->Priority = [this](cs::Ref<Settlement> s, IndustryKind kind)
		{
			auto r = Polities->Command->Priority(s->Owner, kind);
			return std::tuple<bool, double>(r.Favoured, r.Bonus);
		};
		// A lane short of its tenders comes first: everything else rides the lanes (U.SPC-T481).
		economy->ChooseBuild = [this, war](cs::Ref<Settlement> yard) -> cs::Nullable<ShipClass>
		{
			auto tender = LaneUpkeep->OrderAt(yard);
			if (tender) return tender;
			if (Companies->Enabled())
			{
				auto r = Requisition(yard);
				if (r) return r;
				auto o = Companies->OrderAt(yard);
				if (o) return o;
				return war->ChooseWarship(yard);
			}
			if (TraderCount() < MerchantTarget) return ChooseBuild(yard);
			auto warship = war->ChooseWarship(yard);
			return warship ? warship : ChooseBuild(yard);
		};
		economy->ShipBuilt = [this](cs::Ref<Settlement> yard, ShipClass cls, double jd) { OnShipBuilt(yard, cls, jd); };
		economy->CanCrew = [](cs::Ref<Settlement> yard, ShipClass cls) { return Crews::CanCrew(yard->Owner, cls); };
		fleet->BeforeEvent = [economy](double jd) { economy->AdvanceTo(jd); };
		// A group running the player down is flown by the chase, not its schedule (U.SPC-T273).
		fleet->IsHeld = [this](cs::Ref<Ship> ship)
		{
			return ship->Battle != nullptr || ship->Derelict || ship == PilotedShip || (ship->Voyage != nullptr && ship->Voyage->AtGate) ||
				(Pursuit != nullptr && Pursuit->Active() && Pursuit->Of(ship) != nullptr);
		};
		// The player's captains keep their own time in port (mining a claim, waiting on a charge).
		fleet->Dwell = [this, war](cs::Ref<Ship> ship, double normal)
		{
			return Holdings->Owns(ship) ? Holdings->Dwell(ship, normal)
				: ship->Role == ShipRole::Tender ? LaneUpkeep->Dwell(ship)
				: ship->Role == ShipRole::Tug || ship->Role == ShipRole::Hauler ? normal : war->Dwell(ship, normal);
		};
		fleet->Arrived = [this, war, economy](cs::Ref<Ship> ship, double when)
		{
			// At a jump gate, not a port: the interstellar traffic takes it from here.
			if (ship->GateKm.has_value() && ship->Orbit->Body == Catalog()->Root()->Index)
			{
				ReachedGate.Invoke(ship, when);
				return;
			}
			// A freighter bound for a jump gate carries goods for another system's markets: it keeps them through
			// any port it passes on the way to the gate (selling there skipped the gate's toll and the far market).
			bool bound = ship->Voyage != nullptr && ship->GateKm.has_value();
			// A colony ship's hull lands its colonists where they are bound, and is broken up there for a new settlement (U.SPC-T524).
			if (ship->Role == ShipRole::Colonist && ColonyHullArrived(ship, when)) return;
			// Contract cargo and people go to the contract, not the market (U.SPC-T101).
			// An ore tug's works latches its load on out in the field, or takes it in where it is for (U.SPC-T498).
			if (ship->Role == ShipRole::Tug) TugArrived(ship, when);
			// A works' freighter empties its hold into the warehouse its convoy is for (U.SPC-T523).
			if (ship->Role == ShipRole::Hauler) FreighterArrived(ship, when);
			bool contracted = Contracts->OnArrived(ship, ship->Orbit->Body, when);
			if (IsTrader(ship) && !bound && !contracted) economy->OnArrive(ship, ship->Orbit->Body, when);
			war->OnArrived(ship, when);
		};
		fleet->ReachedRing = [war](cs::Ref<Ship> ship, double jd) { war->OnArrived(ship, jd); };
		// Merchants heading into a fight that has just broken out turn aside for somewhere safe.
		war->BattleStarted += [this](cs::Ref<Battle> b) { DivertAround(b, b->StartJd); };
		// A free ship does not wander to a known haven, or an armed port, of someone hostile to it: their platforms
		// would fire on it every time it came back (U.SPC-T291).
		fleet->Shuns = [this, economy](cs::Ref<Ship> ship, int body)
		{
			// Local traffic keeps clear of a fight (U.SPC-T375).
			if (ship->Role == ShipRole::Commuter && War->BattleAt(body) != nullptr) return true;
			for (const auto& s : economy->At(body))
				if ((s->Kind == SettlementKind::Haven || s->Platforms.Count() > 0) && Diplomacy()->Hostile(ship->Faction, s->Owner)) return true;
			return false;
		};
		fleet->ChooseNext = [this, war, economy](cs::Ref<Ship> ship, int here, double when)
		{
			// In port, a ship on propellant tops up its tanks before it plans the next run.
			if (BurnsPropellant(ship)) economy->Refuel(ship, here, when);
			if (ship->Voyage != nullptr && VoyageOrders)
			{
				int ordered = VoyageOrders(ship, here, when);
				if (ordered >= 0) return ordered;
			}
			// A colony ship's hull flies its voyage: to the site, or home with colonists it could not land (U.SPC-T524).
			if (ship->Role == ShipRole::Colonist)
			{
				int colonyNext = ColonyHullNext(ship, here, when);
				if (colonyNext >= 0) return colonyNext;
			}
			// A lane tender works its lane (U.SPC-T481).
			if (ship->Role == ShipRole::Tender && !Holdings->Owns(ship)) return LaneUpkeep->ChooseNext(ship, here, when);
			// An ore tug goes where its belt works sends it (U.SPC-T498).
			if (ship->Role == ShipRole::Tug && !Holdings->Owns(ship))
			{
				int tugNext = Mining ? Mining->TugNext(ship, here) : -1;
				return tugNext >= 0 ? tugNext : here;
			}
			// A works' freighter goes where its belt works sends it (U.SPC-T523).
			if (ship->Role == ShipRole::Hauler && !Holdings->Owns(ship))
			{
				int haulerNext = Mining ? Mining->FreighterNext(ship, here) : -1;
				return haulerNext >= 0 ? haulerNext : here;
			}
			// A clan's smuggler lies at its haven between runs (U.SPC-T94).
			// (C#'s `Route != null && Route.Length > 0`: a null route is the empty list here.)
			if (ship->Runner) return ship->Route.Count() > 0 ? ship->Route[0] : here;
			// The player's own ships go where their captains' standing orders take them.
			if (Holdings->Owns(ship)) return Holdings->ChooseNext(ship, here, when);
			// Local traffic (U.SPC-T375) keeps to its line or its whims, the fleet's own choice.
			if (ship->Role == ShipRole::Commuter) return -1;
			// A merchant, courier or free ship on a contract goes where it takes it; an idle one may take one here.
			if (IsTrader(ship) || ship->Role == ShipRole::Free)
			{
				int job = Contracts->ChooseNext(ship, here, when);
				if (job >= 0) return job;
			}
			// Free ships follow their own whims (the fleet's built-in choice), not orders or markets -- unless
			// their tanks are getting low, when even an idle wanderer heads for somewhere to fill them.
			if (ship->Role == ShipRole::Free)
				return ship->PropellantFraction() < 0.4 && !economy->HasPropellantAt(ship, here) ? economy->NearestPropellant(ship, here, when) : -1;
			if (!IsTrader(ship)) return war->ChooseNext(ship, here, when);
			int next = economy->PlanTrade(ship, here, when);
			if (next < 0 && !ship->HasCargo) ship->Cargo = "Empty, looking for work";
			return next;
		};
	}

	void World::WireColonisation()
	{
		auto c = Colonisation = cs::New<ColonisationT>();
		c->SurveyAll(Catalog());
		c->TravelDays = [this](cs::Ref<Settlement> from, int body, double when)
		{
			return ContractsT::TravelDays(from->Body == body ? 0 : (Ephemeris->HelioAt(from->Body, when) - Ephemeris->HelioAt(body, when)).Length());
		};
		c->Launched = [this](cs::Ref<ColonyShip> v)
		{
			Log(v->DepartJd, v->Reinforcing != nullptr
				? cs::Cat("The colony ship ", v->Name, " sails from ", v->From->Name, " with ", cs::Fmt(v->Colonists, "N0"), " settlers for ", v->Reinforcing->Name)
				: cs::Cat("The colony ship ", v->Name, " sails from ", v->From->Name, " with ", cs::Fmt(v->Colonists, "N0"), " colonists to settle ", v->Site->Name) +
				  (v->Abroad() ? cs::Cat(" in ", v->Where, ", through the gates") : cs::String()), WorldEventKind::Launch, v->From->Body);
		};
		c->Landed = [this](cs::Ref<ColonyShip> v, cs::Ref<Settlement> s)
		{
			Log(Economy->Jd, s != nullptr
				? cs::Cat("The colony ship ", v->Name, " lands ", cs::Fmt(v->Colonists, "N0"), " settlers at ", s->Name)
				: cs::Cat("The colony ship ", v->Name, " finds ", v->Site->Name, v->Abroad() ? cs::Cat(" in ", v->Where) : cs::String(), " taken and carries its colonists home"),
				WorldEventKind::Info, v->Abroad() ? v->From->Body : v->Body());
		};
		c->Founded = [this](cs::Ref<Settlement> s)
		{
			// Settlers from another system bring their polity here with them (U.SPC-T293).
			Fleet->AddFaction(s->Owner);
			Log(Economy->Jd, cs::Cat(s->Owner->Name, " colonists found ", s->Name, ", an outpost of ", cs::Fmt(s->Population, "N0"), " ") +
				((*Catalog())[s->Body]->HabitatSite ? cs::String("in a new habitat") : cs::Cat("on ", (*Catalog())[s->Body]->Name)), WorldEventKind::Economy, s->Body);
		};
		c->Expanded = [this](cs::Ref<Settlement> s) { Log(Economy->Jd, cs::Cat(s->Name, " builds out its habitat for ", cs::Fmt(s->InitialPopulation * People::MaxGrowth, "N0"), " people"), WorldEventKind::Economy, s->Body); };
		c->Promoted = [this](cs::Ref<Settlement> s) { Log(Economy->Jd, cs::Cat(s->Name, " has grown from an outpost into a colony of ", cs::Fmt(s->Population, "N0")), WorldEventKind::Economy, s->Body); };
		c->Ruined = [this](cs::Ref<Settlement> s) { Log(Economy->Jd, cs::Cat(s->Name, " has failed: its last settlers leave and it stands empty, a ruin"), WorldEventKind::Loss, s->Body); };
		WireColonyHulls(c);
		Economy->Colonisation = c;
	}

	// ---------------------------------------------------------------- Alpha Centauri

	cs::Ref<World> World::CreateAlphaCentauri(cs::Ref<EphemerisT> ephemeris, double jd, int seed, cs::Ref<PolitiesT> polities)
	{
		double startJd = jd - 45;
		auto w = Make(ephemeris, startJd, seed, polities);
		auto fleet = w->Fleet;

		// The Centauri government, its own state beyond the Federation.
		auto accord = w->Sol = w->Polity("Centauri Accord", "CAS", Rgb(0.3f, 0.88f, 0.75f), PolityKind::State, 9e8, 1.15);
		auto combine = w->Belt = w->Polity("Toliman Combine", "TCV", Rgb(1.0f, 0.78f, 0.3f), PolityKind::Company, 7e8, 0.8);
		w->Independents = w->Polity("Kentaurus Free Traders", "KFT", Rgb(0.78f, 0.84f, 0.92f), PolityKind::FreeTraders, 1.2e8, 0.6);
		w->Pirates = w->Polity("Long Dark", "", Rgb(0.85f, 0.25f, 0.6f), PolityKind::PirateClan, 8e7, 1.2);
		accord->IsMilitary = true;
		// Rivals, not enemies: the hook for a war later.
		w->RivalA = accord;
		w->RivalB = combine;
		w->MerchantTarget = 50;
		w->MerchantCap = 80;
		w->NavyCap = 10;
		w->NavyCapAtWar = 16;
		// A thin system feeds few raiders.
		w->PirateGroupCap = 2;
		w->PirateIncomePerDay = 4e5;
		w->HunterCap = 1;
		w->RaiderNames = cs::List<cs::String>
		{
			"Lightless", "Event Horizon", "Cold Sleep", "Red Shift", "Penumbra", "Dead Air", "Null Return", "Last Light",
			"Occultation", "Silent Running", "Umbral", "Long Fall",
		};

		w->BuildCentauriSettlements();
		w->Wire(startJd, seed, Hyperlanes::CreateAlphaCentauri(ephemeris));
		auto war = w->War;
		w->Infrastructure->AddCentauriRings(w->Economy);
		w->Fleet->Rings.AddRange(w->Infrastructure->Rings);

		using Esc = std::pair<ShipClass, cs::String>;
		fleet->Group(accord, startJd, ShipClass::Battleship, "Terminator", cs::List<cs::String>{ "Proxima b", "Proxima d", "Proxima c" },
			cs::List<Esc>{ Esc(ShipClass::Cruiser, "Long Noon"), Esc(ShipClass::Destroyer, "Flare"), Esc(ShipClass::Destroyer, "Ember") });
		fleet->Group(accord, startJd, ShipClass::Cruiser, "Centaur", cs::List<cs::String>{ "Proxima b", "Rigil Gate", "Toliman Forge" },
			cs::List<Esc>{ Esc(ShipClass::Destroyer, "Hadar"), Esc(ShipClass::Destroyer, "Agena") });

		cs::List<cs::String> freighters =
		{
			"Second Sun", "Tidal Lock", "Far Side", "Kentaur Pride", "Twilight Band", "Nine Lights", "Slow Fuse",
			"Toliman's Due", "Red Noon", "Barycentre",
		};
		cs::List<cs::String> haulers = { "Dross", "Cinder", "Ore Moth", "Spoil" };
		cs::List<cs::String> couriers = { "Long Shot", "Lightspeed Promise" };
		w->BuildMerchants(startJd, freighters, haulers, couriers,
			cs::List<cs::Ref<Faction>>{ w->Independents, w->Independents, combine, w->Independents, accord }, [combine](int i) { return combine; });

		w->Companies->MaxCompanies = 5;
		using Co = std::tuple<cs::String, cs::String, cs::String, cs::Ref<Faction>, bool>;
		w->FoundCompanies(startJd, cs::List<Co>{
			Co("Kentaurus Carrying Company", "KCC", "New Kentaurus", w->Independents, false),
			Co("Toliman Bulk", "TB", "Toliman Forge", combine, true),
			Co("Proxima Line", "PL", "New Kentaurus", accord, false) });
		w->AddFreeShips(startJd, cs::List<cs::String>{ "First Light", "Terminator Dawn" }, cs::List<cs::String>{ "Red Dust" }, cs::List<cs::String>{ "Long Look" });
		int haven = (*w->Catalog())["Blackhollow"]->Index;
		war->RaisePirateGroup(startJd, haven);
		PatrolWings::Raise(w, startJd);
		// And its ports' ordinary traffic: buses, taxis and people's own craft (U.SPC-T375).
		LocalTraffic::Raise(w, startJd);
		w->SeatCapitals();
		w->MusterReserves();

		w->Update(jd);
		return w;
	}

	cs::Ref<World> World::CreateUnsettled(cs::Ref<EphemerisT> ephemeris, double jd, int seed, cs::Ref<PolitiesT> polities)
	{
		double startJd = jd - 45;
		auto w = Make(ephemeris, startJd, seed, polities);
		// One survey service and one unknown for every empty system: they are the same polities everywhere.
		w->Sol = w->Independents = w->Polity("Survey Service", "SSV", Rgb(0.7f, 0.85f, 0.95f), PolityKind::FreeTraders, 0);
		w->Pirates = w->UnknownRaiders();
		w->MerchantTarget = w->MerchantCap = 0;
		w->NavyCap = w->NavyCapAtWar = 0;
		w->PirateGroupCap = 0;
		w->PirateIncomePerDay = 0;
		w->HunterCap = 0;
		w->Wire(startJd, seed, cs::New<Hyperlanes>(ephemeris));
		w->Update(jd);
		return w;
	}

	void World::BuildCentauriSettlements()
	{
		auto e = Economy;
		auto B = [this](const cs::String& name) { return (*Catalog())[name]->Index; };
		auto Mine = [](cs::Ref<Settlement> s, Good g, double perDay) { s->Facilities.Add(cs::New<Facility>(Recipe::Mine(g, perDay), 1)); };
		auto Make = [](cs::Ref<Settlement> s, IndustryKind k, double level) { s->Facilities.Add(cs::New<Facility>(Recipe::Of(k), level)); };

		// The Accord: Proxima's worlds.
		auto kentaurus = e->Add("New Kentaurus", B("Proxima b"), SettlementKind::Colony, Sol, 400e3);
		Mine(kentaurus, Good::Water, 2500);
		Mine(kentaurus, Good::Nitrogen, 700);
		Mine(kentaurus, Good::Hydrocarbons, 300);
		Make(kentaurus, IndustryKind::PharmaLab, 2);
		Make(kentaurus, IndustryKind::MunitionsPlant, 1);
		Make(kentaurus, IndustryKind::Shipyard, 0.8);

		auto ember = e->Add("Ember Station", B("Proxima d"), SettlementKind::MiningOutpost, Sol, 30e3);
		Mine(ember, Good::NickelIron, 4500);
		Mine(ember, Good::Titanium, 220);
		Mine(ember, Good::Copper, 450);
		Mine(ember, Good::Zinc, 120);
		Make(ember, IndustryKind::Smelter, 1);

		auto farReach = e->Add("Far Reach", B("Proxima c"), SettlementKind::Station, Sol, 20e3);
		Mine(farReach, Good::Water, 4000);
		Mine(farReach, Good::Nitrogen, 800);
		Mine(farReach, Good::HeliumThree, 8);

		// The Combine: stations around A and B.
		auto rigil = e->Add("Rigil Gate", B("Rigil Gate"), SettlementKind::Station, Belt, 120e3);
		Make(rigil, IndustryKind::ElectronicsFab, 1.5);
		Make(rigil, IndustryKind::MachineWorks, 1);
		Make(rigil, IndustryKind::PharmaLab, 1);

		auto aurora = e->Add("Aurora Skimmer", B("Alpha Centauri Ab"), SettlementKind::Station, Belt, 15e3);
		Mine(aurora, Good::HeliumThree, 30);
		Mine(aurora, Good::Hydrocarbons, 800);

		auto forge = e->Add("Toliman Forge", B("Toliman Forge"), SettlementKind::Station, Belt, 80e3);
		// Ore from B's debris (fiction: no asteroid belt is known there).
		Mine(forge, Good::NickelIron, 1500);
		Mine(forge, Good::Titanium, 120);
		Mine(forge, Good::RareEarths, 40);
		Mine(forge, Good::Platinum, 4);
		Make(forge, IndustryKind::Smelter, 1.5);
		Make(forge, IndustryKind::MachineWorks, 1);
		Make(forge, IndustryKind::Shipyard, 1);

		e->Add("Blackhollow", B("Blackhollow"), SettlementKind::Haven, Pirates, 8e3);

		for (const auto& s : e->Settlements) Subsist(s, Mine, Make);
		// The Combine has no navy, so its stations are well gunned.
		for (const auto& s : e->Settlements)
		{
			s->MaxPlatforms = s->Population >= 400e3 ? 3 : s->Owner == Belt && s->Population >= 50e3 ? 3 : s->Kind == SettlementKind::Haven ? 1 : 2;
			for (int i = 0; i < s->MaxPlatforms; i++) s->Platforms.Add(cs::New<ShipCombat>(CombatSpec::Platform()));
		}
	}

	void World::BuildSettlements()
	{
		auto e = Economy;
		auto B = [this](const cs::String& name) { return (*Catalog())[name]->Index; };
		auto Mine = [](cs::Ref<Settlement> s, Good g, double perDay) { s->Facilities.Add(cs::New<Facility>(Recipe::Mine(g, perDay), 1)); };
		auto Make = [](cs::Ref<Settlement> s, IndustryKind k, double level) { s->Facilities.Add(cs::New<Facility>(Recipe::Of(k), level)); };

		// Sol Federation: Earth's orbital industry and the inner worlds.
		auto earth = e->Add("Earth Orbital", B("Earth"), SettlementKind::Colony, Sol, 12e6);
		Mine(earth, Good::Water, 82000);
		Mine(earth, Good::Nitrogen, 3500);
		Mine(earth, Good::Hydrocarbons, 1500);
		Make(earth, IndustryKind::Farm, 48);
		Make(earth, IndustryKind::PharmaLab, 12);
		Make(earth, IndustryKind::ElectronicsFab, 7);
		Make(earth, IndustryKind::MachineWorks, 3);
		Make(earth, IndustryKind::MunitionsPlant, 3);
		Make(earth, IndustryKind::Shipyard, 2);

		auto luna = e->Add("Tycho City", B("Moon"), SettlementKind::Colony, Sol, 500e3);
		Mine(luna, Good::HeliumThree, 45);
		Mine(luna, Good::Titanium, 400);
		Mine(luna, Good::RareEarths, 70);
		Mine(luna, Good::Platinum, 6);
		Mine(luna, Good::Water, 1500);
		Make(luna, IndustryKind::Farm, 1.5);

		auto mercury = e->Add("Caloris Works", B("Mercury"), SettlementKind::MiningOutpost, Sol, 100e3);
		Mine(mercury, Good::NickelIron, 6500);
		Mine(mercury, Good::Copper, 700);
		Mine(mercury, Good::Titanium, 300);
		Make(mercury, IndustryKind::Smelter, 2);

		auto venus = e->Add("Aphrodite Aerostat", B("Venus"), SettlementKind::Station, Sol, 150e3);
		Mine(venus, Good::Nitrogen, 1500);
		Mine(venus, Good::Hydrocarbons, 1200);
		Make(venus, IndustryKind::PharmaLab, 4);

		auto mars = e->Add("Ares Prime", B("Mars"), SettlementKind::Colony, Sol, 1.5e6);
		Mine(mars, Good::Water, 10500);
		Mine(mars, Good::NickelIron, 4200);
		Mine(mars, Good::Copper, 500);
		Mine(mars, Good::Zinc, 200);
		Make(mars, IndustryKind::Farm, 6);
		Make(mars, IndustryKind::Smelter, 1.5);
		Make(mars, IndustryKind::MunitionsPlant, 2);
		Make(mars, IndustryKind::Shipyard, 1.5);

		auto phobos = e->Add("Stickney Depot", B("Phobos"), SettlementKind::Station, Sol, 25e3);
		Mine(phobos, Good::Water, 900);
		Mine(phobos, Good::Hydrocarbons, 400);

		e->Add("Blackwater", B("Deimos"), SettlementKind::Haven, Pirates, 15e3);

		// The Banner's hidden belt bases, off the lanes on real asteroids, unknown until someone finds them.
		for (const auto& [name, body, people] : BeltHavens())
			e->Add(name, B(body), SettlementKind::Haven, Pirates, people)->Hidden = true;
		// And one out in Jupiter's trailing Trojan camp, on the twin Patroclus-Menoetius.
		for (const auto& [name, body, people] : TrojanHavens())
			e->Add(name, B(body), SettlementKind::Haven, Pirates, people)->Hidden = true;

		// Belt Consortium: Ceres and the Jovian moons.
		auto ceres = e->Add("Ceres Hub", B("Ceres"), SettlementKind::Colony, Belt, 700e3);
		Mine(ceres, Good::Water, 9000);
		Mine(ceres, Good::NickelIron, 9000);
		Mine(ceres, Good::Platinum, 15);
		Mine(ceres, Good::RareEarths, 50);
		Make(ceres, IndustryKind::Smelter, 3);
		Make(ceres, IndustryKind::Farm, 2.5);
		Make(ceres, IndustryKind::MunitionsPlant, 1.5);
		Make(ceres, IndustryKind::Shipyard, 1.5);

		auto europa = e->Add("Conamara Station", B("Europa"), SettlementKind::Station, Belt, 120e3);
		Mine(europa, Good::Water, 14000);

		auto ganymede = e->Add("Ganymede Union", B("Ganymede"), SettlementKind::Colony, Belt, 800e3);
		Mine(ganymede, Good::Water, 6500);
		Mine(ganymede, Good::Titanium, 250);
		Make(ganymede, IndustryKind::Farm, 5);
		Make(ganymede, IndustryKind::ElectronicsFab, 4);
		Make(ganymede, IndustryKind::MachineWorks, 3);
		Make(ganymede, IndustryKind::PharmaLab, 3);
		Make(ganymede, IndustryKind::Shipyard, 2);

		auto callisto = e->Add("Valhalla Mines", B("Callisto"), SettlementKind::MiningOutpost, Belt, 150e3);
		Mine(callisto, Good::NickelIron, 7000);
		Mine(callisto, Good::Water, 2000);
		Make(callisto, IndustryKind::Smelter, 2);

		auto io = e->Add("Loki Sulphide Works", B("Io"), SettlementKind::MiningOutpost, Belt, 40e3);
		Mine(io, Good::Copper, 2000);
		Mine(io, Good::Zinc, 600);

		// The Greek camp at Jupiter's L4: dark, primitive Trojans rich in ices and organics, mined
		// for volatiles and a little metal (game layer, on real rocks; see Core/Systems/SOURCES.md).
		auto hektor = e->Add("Skamandrios Diggings", B("Hektor"), SettlementKind::MiningOutpost, Belt, 30e3);
		Mine(hektor, Good::Hydrocarbons, 900);
		Mine(hektor, Good::Water, 1800);
		Mine(hektor, Good::Nitrogen, 300);
		Mine(hektor, Good::NickelIron, 1200);

		auto eurybates = e->Add("Queta Camp", B("Eurybates"), SettlementKind::MiningOutpost, Belt, 12e3);
		Mine(eurybates, Good::NickelIron, 900);
		Mine(eurybates, Good::RareEarths, 25);
		Mine(eurybates, Good::Water, 600);

		// Outer Reach Compact: Saturn and beyond.
		auto titan = e->Add("Kraken Port", B("Titan"), SettlementKind::Colony, Reach, 500e3);
		Mine(titan, Good::Hydrocarbons, 2500);
		Mine(titan, Good::Nitrogen, 2000);
		Mine(titan, Good::Water, 3500);
		Make(titan, IndustryKind::Farm, 3);
		Make(titan, IndustryKind::PharmaLab, 4);
		Make(titan, IndustryKind::MunitionsPlant, 3);
		Make(titan, IndustryKind::Shipyard, 2);

		auto enceladus = e->Add("Tiger Stripe", B("Enceladus"), SettlementKind::Station, Reach, 40e3);
		Mine(enceladus, Good::Water, 6000);
		Mine(enceladus, Good::Hydrocarbons, 500);

		auto titania = e->Add("Gertrude Base", B("Titania"), SettlementKind::Station, Reach, 60e3);
		Mine(titania, Good::HeliumThree, 50);
		Mine(titania, Good::Water, 1500);

		auto oberon = e->Add("Oberon Deep", B("Oberon"), SettlementKind::MiningOutpost, Reach, 40e3);
		Mine(oberon, Good::RareEarths, 150);
		Mine(oberon, Good::Titanium, 250);
		Mine(oberon, Good::Platinum, 6);

		auto triton = e->Add("Triton Station", B("Triton"), SettlementKind::Station, Reach, 70e3);
		Mine(triton, Good::Nitrogen, 1200);
		Mine(triton, Good::HeliumThree, 35);
		Mine(triton, Good::Water, 1000);

		auto charon = e->Add("Charon Outpost", B("Charon"), SettlementKind::MiningOutpost, Reach, 30e3);
		Mine(charon, Good::Water, 1500);
		Mine(charon, Good::Nitrogen, 600);

		for (const auto& s : e->Settlements) Subsist(s, Mine, Make);

		// Orbital defence platforms: the big colonies are fortresses, outposts have one or none.
		for (const auto& s : e->Settlements)
		{
			// A haven's garrison: two gun platforms over the rock.
			s->MaxPlatforms = s->Kind == SettlementKind::Haven ? 2 : s->Population >= 5e6 ? 4 : s->Population >= 1e6 ? 3 : s->Population >= 400e3 ? 2 : 1;
			for (int i = 0; i < s->MaxPlatforms; i++) s->Platforms.Add(cs::New<ShipCombat>(CombatSpec::Platform()));
		}
	}

	const cs::List<std::tuple<cs::String, cs::String, double>>& World::BeltHavens()
	{
		static const cs::List<std::tuple<cs::String, cs::String, double>> havens =
		{
			{ "Hollow Iron", "Psyche", 6e3 },
			{ "Lantern Rock", "Lutetia", 4e3 },
			{ "Dactyl's Rest", "Ida", 3e3 },
		};
		return havens;
	}

	const cs::List<std::tuple<cs::String, cs::String, double>>& World::TrojanHavens()
	{
		static const cs::List<std::tuple<cs::String, cs::String, double>> havens =
		{
			{ "Menoetius Hold", "Patroclus", 5e3 },
		};
		return havens;
	}

	void World::Subsist(cs::Ref<Settlement> s, cs::Action<cs::Ref<Settlement>, Good, double> mine, cs::Action<cs::Ref<Settlement>, IndustryKind, double> make)
	{
		double share = s->Kind == SettlementKind::Haven ? 1.1 : 0.92;
		auto farm = Recipe::Of(IndustryKind::Farm);
		double food = 0, water = 0, nitrogen = 0;
		for (const auto& f : s->Facilities)
		{
			for (const auto& [g, rate] : f->Recipe->Outputs)
			{
				if (g == Good::Food) food += rate * f->Level;
				if (g == Good::Water) water += rate * f->Level;
				if (g == Good::Nitrogen) nitrogen += rate * f->Level;
			}
			for (const auto& [g, rate] : f->Recipe->Inputs)
			{
				if (g == Good::Water) water -= rate * f->Level;
				if (g == Good::Nitrogen) nitrogen -= rate * f->Level;
			}
		}
		double foodNeed = s->Population * People::NeedOf(s->Kind, Good::Food) * share;
		if (food < foodNeed)
		{
			double level = (foodNeed - food) / std::get<1>(farm->Outputs[0]); // .perDay
			make(s, IndustryKind::Farm, level);
			water -= std::get<1>(farm->Inputs[0]) * level;
			nitrogen -= std::get<1>(farm->Inputs[1]) * level;
		}
		double waterNeed = s->Population * People::NeedOf(s->Kind, Good::Water) * share;
		if (water < waterNeed) mine(s, Good::Water, (waterNeed - water) * 1.05);
		// Air leaks away: the nitrogen to top it up is a need like the farms' own.
		nitrogen -= s->Population * People::NeedOf(s->Kind, Good::Nitrogen) * share;
		// The people's share is already in the need above, and the farms want all the nitrogen they draw.
		if (nitrogen < 0) mine(s, Good::Nitrogen, -nitrogen);
	}

	void World::BuildMilitary(double jd)
	{
		auto fleet = Fleet;
		using Esc = std::pair<ShipClass, cs::String>;
		using Route = cs::List<cs::String>;
		using Escs = cs::List<Esc>;
		fleet->Group(Sol, jd, ShipClass::Carrier, "Resolute", Route{ "Earth", "Moon", "Mars" },
			Escs{ Esc(ShipClass::Battleship, "Dauntless"), Esc(ShipClass::Cruiser, "Aegis"), Esc(ShipClass::Cruiser, "Halberd"),
			Esc(ShipClass::Destroyer, "Kestrel"), Esc(ShipClass::Destroyer, "Merlin"),
			Esc(ShipClass::Fighter, "Mayfly"), Esc(ShipClass::Fighter, "Wasp") });
		fleet->Group(Sol, jd, ShipClass::Battleship, "Vanguard", Route{ "Mars", "Ceres", "Earth" },
			Escs{ Esc(ShipClass::Cruiser, "Lance"), Esc(ShipClass::Destroyer, "Swift"), Esc(ShipClass::Destroyer, "Talon"),
			Esc(ShipClass::Fighter, "Gadfly"), Esc(ShipClass::Fighter, "Hornet") });
		fleet->Group(Belt, jd, ShipClass::Carrier, "Tycho Ascendant", Route{ "Ceres", "Ganymede", "Callisto", "Europa" },
			Escs{ Esc(ShipClass::Battleship, "Iron Ledger"), Esc(ShipClass::Cruiser, "Ore Hawk"), Esc(ShipClass::Cruiser, "Pallas"),
			Esc(ShipClass::Destroyer, "Flint"), Esc(ShipClass::Fighter, "Firefly"), Esc(ShipClass::Fighter, "Cinder") });
		fleet->Group(Belt, jd, ShipClass::Cruiser, "Hygiea", Route{ "Ceres", "Mars" },
			Escs{ Esc(ShipClass::Destroyer, "Shard"), Esc(ShipClass::Destroyer, "Spall"),
			Esc(ShipClass::Fighter, "Grit"), Esc(ShipClass::Fighter, "Pebble") });
		fleet->Group(Reach, jd, ShipClass::Battleship, "Long Night", Route{ "Titan", "Titania", "Triton" },
			Escs{ Esc(ShipClass::Cruiser, "Umbra"), Esc(ShipClass::Cruiser, "Perihelion"), Esc(ShipClass::Destroyer, "Wisp"),
			Esc(ShipClass::Fighter, "Nightjar"), Esc(ShipClass::Fighter, "Moth") });
		fleet->Group(Reach, jd, ShipClass::Carrier, "Endurance", Route{ "Triton", "Charon", "Titan", "Ganymede" },
			Escs{ Esc(ShipClass::Cruiser, "Aphelion"), Esc(ShipClass::Destroyer, "Rime"), Esc(ShipClass::Destroyer, "Hoarfrost"),
			Esc(ShipClass::Fighter, "Sleet"), Esc(ShipClass::Fighter, "Driftglass") });
	}

	const cs::List<cs::String>& World::FreighterNames()
	{
		static const cs::List<cs::String> names =
		{
			"Bountiful Harvest", "Kepler's Due", "Slow Boat", "Red Ledger", "Ice Mule", "Long Haul", "Patience",
			"Margin of Error", "Cold Comfort", "Ballast", "Heavy Lifting", "Second Wind", "Tin Can", "Far Shore",
			"Due Diligence", "Fair Winds", "Hard Bargain", "Payload", "Quiet Profit", "Steady State", "Tramp Royal",
			"Volume Discount", "Wanderer", "Yield Curve", "Small Mercies", "Cargo Cult", "Dead Reckoning", "Night Market",
		};
		return names;
	}

	const cs::List<cs::String>& World::MinerNames()
	{
		static const cs::List<cs::String> names = { "Pickaxe", "Rockhound", "Prospect", "Tailings", "Motherlode", "Seam", "Slag Heap", "Assay", "Lode Star", "Gangue" };
		return names;
	}

	const cs::List<cs::String>& World::CourierNames()
	{
		static const cs::List<cs::String> names = { "Quicksilver", "Hermes", "Dispatch", "Lightfoot", "Express", "Swiftsure" };
		return names;
	}

	void World::BuildMerchants(double jd, cs::List<cs::String> freighters, cs::List<cs::String> miners, cs::List<cs::String> couriers, cs::List<cs::Ref<Faction>> owners, cs::Func<cs::Ref<Faction>, int> minerOwner)
	{
		auto ports = PortBodies();
		merchantNames = freighters;
		minerNames = miners;
		for (int i = 0; i < freighters.Count(); i++)
			AddMerchant(freighters[i], ShipClass::Freighter, owners[i % owners.Count()], ShipRole::Trader, ports, jd);
		for (int i = 0; i < miners.Count(); i++)
			AddMerchant(miners[i], ShipClass::Miner, minerOwner(i), ShipRole::Miner, ports, jd);
		for (const auto& name : couriers)
			AddMerchant(name, ShipClass::Courier, Independents, ShipRole::Courier, ports, jd);
	}

	cs::List<int> World::PortBodies()
	{
		cs::List<int> list;
		for (const auto& s : Economy->Settlements)
			if (s->Kind != SettlementKind::Haven && !list.Contains(s->Body)) list.Add(s->Body);
		return list.ToArray();
	}

	void World::Launch(cs::Ref<Ship> ship, cs::Ref<Settlement> yard, double jd)
	{
		auto slip = Infrastructure ? Infrastructure->YardOf(yard) : nullptr;
		Fleet->Station(ship, yard->Body, jd, slip ? slip->Orbit : nullptr);
	}

	void World::DivertAround(cs::Ref<Battle> b, double jd)
	{
		if (b->Site != nullptr) return;
		auto lanes = Fleet->Lanes;
		bool offRoot = b->Body == Catalog()->Root()->Index;
		for (const auto& s : Fleet->Ships())
		{
			if ((!IsTrader(s) && s->Role != ShipRole::Commuter) || s->Destroyed || s->Phase != ShipPhase::Transit || s->Orbit == nullptr || s->Orbit->Body != b->Body) continue;
			// (A null Journey is the empty list: JourneyIndex < 0 is false, as the null test was.)
			if (s->JourneyIndex < s->Journey.Count()) continue;
			// (A null Ports is the empty list: the search below then finds nothing and skips it, as the null test did.)
			if (s->PhaseEndJd - jd < 0.05 || s->Ports.Count() == 0) continue;
			if (offRoot)
			{
				auto bound = s->GateKm.has_value() ? *s->GateKm : s->Orbit->OffsetAt(s->PhaseEndJd);
				if ((bound - b->Anchor).Length() > WarT::GateFightRangeKm) continue;
			}
			auto here = Fleet->StateOf(s, jd).HelioKm;
			int best = -1;
			double bestKm = cs::Double::MaxValue;
			for (int p : s->Ports)
			{
				if (p == b->Body || War->BattleAt(p) != nullptr) continue;
				bool reachable = lanes == nullptr || lanes->SystemOf(p) == lanes->SystemOf(b->Body) || lanes->SystemOf(p) == lanes->SystemOf(s->OriginBody);
				if (!reachable) continue;
				double km = (Ephemeris->HelioAt(p, jd) - here).Length();
				if (km < bestKm)
				{
					bestKm = km;
					best = p;
				}
			}
			if (best < 0 || !Fleet->Divert(s, best, jd)) continue;
			s->Cargo = s->HasCargo ? s->Cargo : "Empty, diverted";
			Log(jd, cs::Cat(s->Name, " turns aside for ", (*Catalog())[best]->Name, ": fighting at ", (*Catalog())[b->Body]->Name), WorldEventKind::Info, best);
		}
	}

	cs::List<int> World::FreeShipPorts()
	{
		cs::HashSet<int> hidden;
		for (const auto& s : Economy->Settlements)
			if (s->Hidden) hidden.Add(s->Body);
		cs::List<int> anywhere;
		for (const auto& b : Catalog()->Bodies())
			if (b->IsVisible() && !b->IsStar() && !hidden.Contains(b->Index)) anywhere.Add(b->Index);
		return anywhere.ToArray();
	}

	void World::AddFreeShips(double jd, cs::List<cs::String> yachts, cs::List<cs::String> prospectors, cs::List<cs::String> explorers)
	{
		auto places = FreeShipPorts();
		auto Add = [&](const cs::String& name, ShipClass cls)
		{
			auto ship = Fleet->AddShip(name, cls, Independents, ShipRole::Free);
			ship->Ports = places;
			Fleet->Station(ship, places[Fleet->Rng().Next(places.Count())], jd);
		};
		for (const auto& n : yachts) Add(n, ShipClass::Courier);
		// (`explorers ?? new string[0]`: a null list is the empty one here.)
		for (const auto& n : explorers) Add(n, ShipClass::Explorer);
		for (const auto& n : prospectors) Add(n, ShipClass::Miner);
	}

	cs::Ref<Ship> World::AddMerchant(cs::String name, ShipClass cls, cs::Ref<Faction> owner, ShipRole role, cs::List<int> ports, double jd, int at, cs::Ref<Settlement> yard)
	{
		auto ship = Fleet->AddShip(name, cls, owner, role);
		ship->CapacityTonnes = EconomyT::CapacityOf(cls);
		ship->Ports = ports;
		if (at < 0)
		{
			// Start at a port the owner may trade at.
			for (int tries = 0; tries < 20; tries++)
			{
				at = ports[Fleet->Rng().Next(ports.Count())];
				bool ok = false;
				for (const auto& s : Economy->At(at)) ok |= Diplomacy()->CanDock(owner, s->Owner);
				if (ok) break;
			}
		}
		if (yard != nullptr) Launch(ship, yard, jd);
		else Fleet->Station(ship, at, jd);
		return ship;
	}

	void World::FoundCompanies(double jd, cs::List<std::tuple<cs::String, cs::String, cs::String, cs::Ref<Faction>, bool>> table)
	{
		cs::List<std::pair<cs::Ref<Company>, bool>> made;
		for (const auto& [name, tag, home, flag, mining] : table)
		{
			cs::Ref<Settlement> port = nullptr;
			for (const auto& s : Economy->Settlements)
				if (s->Name == home) port = s;
			made.Add(std::pair<cs::Ref<Company>, bool>(Companies->Found(name, tag, CompaniesT::LiveryFor(made.Count()), port, flag, Companies->FoundingCapital, jd), mining));
		}
		for (const auto& ship : Fleet->Ships())
		{
			if (ship->Destroyed || !IsTrader(ship)) continue;
			bool ore = ship->Spec->Class == ShipClass::Miner;
			cs::Ref<Company> best = nullptr;
			int bestRank = cs::Int::MaxValue;
			for (const auto& [c, mining] : made)
			{
				if (c->Flag != ship->Faction) continue;
				int rank = (mining == ore ? 0 : 1000) + c->Ships.Count();
				if (rank < bestRank)
				{
					bestRank = rank;
					best = c;
				}
			}
			if (best != nullptr) Companies->Enlist(best, ship);
		}
	}

	// ---------------------------------------------------------------- shipbuilding

	int World::TraderCount()
	{
		int n = 0;
		for (const auto& s : Fleet->Ships())
			if (!s->Destroyed && IsTrader(s)) n++;
		return n;
	}

	cs::Nullable<ShipClass> World::Requisition(cs::Ref<Settlement> yard)
	{
		auto f = yard->Owner;
		if (f == nullptr || !f->IsMilitary || !War->AtWar(f)) return std::nullopt;
		int navy = 0;
		for (const auto& s : Fleet->Ships())
			if (!s->Destroyed && s->Faction == f && s->Spec->IsMilitary && !s->PatrolWing) navy++;
		return navy < NavyCap ? War->ChooseWarship(yard) : std::nullopt;
	}

	cs::Nullable<ShipClass> World::ChooseBuild(cs::Ref<Settlement> yard)
	{
		if (TraderCount() >= MerchantCap) return std::nullopt;
		return builtCount % 3 == 2 ? ShipClass::Miner : ShipClass::Freighter;
	}

	void World::OnShipBuilt(cs::Ref<Settlement> yard, ShipClass cls, double jd)
	{
		// Its crew and marines come out of the owner's trained reserve.
		Crews::Draft(yard->Owner, Crews::Complement(cls));
		cs::Ref<Ship> ship;
		if (ShipClassSpec::Of(cls)->IsMilitary)
		{
			ship = War->LaunchWarship(yard, cls, jd);
			ShipAdded.Invoke(ship);
			return;
		}
		if (cls == ShipClass::LaneTender)
		{
			// A tender for one of its owner's lanes (U.SPC-T481).
			ship = LaneUpkeep->Launch(yard, jd);
			if (ship != nullptr) ShipAdded.Invoke(ship);
			return;
		}
		builtCount++;
		cs::Ref<Company> company = nullptr;
		if (cls == ShipClass::Freighter || cls == ShipClass::Miner || cls == ShipClass::Courier)
		{
			auto role = cls == ShipClass::Miner ? ShipRole::Miner : cls == ShipClass::Courier ? ShipRole::Courier : ShipRole::Trader;
			const auto& names = cls == ShipClass::Miner ? minerNames : merchantNames;
			// The company that ordered it pays the yard and takes it; else it is the yard owner's own.
			company = Companies->TakeDelivery(yard, cls, jd);
			cs::String name = company != nullptr ? Companies->HullName(company, names) : cs::Cat(names[builtCount % names.Count()], " ", Roman(2 + builtCount / names.Count()));
			ship = AddMerchant(name, cls, company && company->Flag ? company->Flag : yard->Owner, role, PortBodies(), jd, yard->Body, yard);
			if (company != nullptr) Companies->Enlist(company, ship);
		}
		else return;
		Log(jd, cs::Cat(yard->Name, " launches the ", cs::Strings::ToLowerInvariant(ship->Spec->DisplayName), " ", ship->Name) + (company != nullptr ? cs::Cat(" for ", company->Name) : cs::String()), WorldEventKind::Launch, yard->Body);
		ShipAdded.Invoke(ship);
	}

	cs::String World::Roman(int n)
	{
		static const cs::List<cs::String> r = { "", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X" };
		return n < r.Count() ? r[n] : cs::Str(n);
	}
}
