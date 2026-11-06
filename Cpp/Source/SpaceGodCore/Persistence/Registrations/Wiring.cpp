// Not a C# file (US-T36): the delegate wiring a load has to redo (docs/PERSISTENCE.md, "Delegates").
//
// C#'s ObjectGraph saves every delegate as its invocation list and rebuilds it on load. A std::function cannot be
// looked into, so the C++ build writes delegates as null and, once the whole graph is read, Galaxy's SG_LOADED hook
// (Persistence/Registrations/Systems.cpp) calls RewireGalaxy, which wires everything again as construction wired it:
//   - each world: World::Construct's adventure hooks, MarketShocks' constructor hook, War's constructor hooks (escape
//     pods), then World::Wire's own (the law, the outlaws, contracts, trade insurance, mining, docking, colonisation,
//     the fleet's and the economy's callbacks), then War::Hook for every battle still running;
//   - then the galaxy's: InterstellarTraffic (each system's gate arrivals and voyage orders, the pirate clans' watch
//     on each war, the deep-space cruises), the polities' ships in transit, InterstellarColonisation (each system).
// For an event several objects subscribe to, the order is construction's (a world's own handlers first, the galaxy's
// after them), so a loaded game calls its handlers in the order the running one did.
//
// World::RewireAfterLoad repeats World::Construct/Wire/WireMining/WireColonisation's delegate statements verbatim
// (extracted from World.cpp and World.Mining.cpp, creation left out): a change to that wiring must be made here too, and
// in CSharp/Core/SaveCompat/SaveWiring.cs, which does the same for a C++ save loaded by the C# track (US-T41).
// Subscribers outside Core (the views' toasts) subscribe to the loaded world again themselves, as in C#.

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
#include "Combat/Pursuit.h"
#include "Combat/War.h"
#include "Economy/BeltMining.h"
#include "Economy/Colonisation.h"
#include "Economy/Companies.h"
#include "Economy/Contracts.h"
#include "Economy/Economy.h"
#include "Economy/Holdings.h"
#include "Economy/Industry.h"
#include "Economy/MarketShocks.h"
#include "Economy/People.h"
#include "Economy/Propulsion.h"
#include "Economy/Settlement.h"
#include "Economy/TradeDesk.h"
#include "Economy/TradeInsurance.h"
#include "Events/EventDirector.h"
#include "Events/Opportunity.h"
#include "Law/Law.h"
#include "Law/Outlaws.h"
#include "Systems/DeepSpace.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarColonisation.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/LaneUpkeep.h"
#include "Systems/PirateClans.h"
#include "Systems/Polities.h"
#include "Systems/StrategicCommand.h"
#include "Persistence/Registrations/Wiring.h"

namespace SpaceGod::Core
{
	namespace
	{
		// World.cpp's names for the classes World has members named like.
		using EconomyT = ::SpaceGod::Core::Economy;
		using CompaniesT = ::SpaceGod::Core::Companies;
		using ContractsT = ::SpaceGod::Core::Contracts;
	}

	/// <summary>The private parts of the wiring (a friend of every class with SG_SERIALIZABLE).</summary>
	template <> struct Graph::Access<1>
	{
		// MarketShocks' constructor: it hears of every sale, first of the economy's Sold subscribers.
		static void Shocks(MarketShocks& m)
		{
			auto* self = &m;
			m.world->Economy->Sold += [self](cs::Ref<Ship> ship, cs::Ref<Settlement> port, Good good, double tonnes) { self->OnSold(ship, port, good, tonnes); };
		}

		// War's constructor: the escape pods' hooks (War.cpp, War::War).
		static void WarCrews(War& w)
		{
			auto* self = &w;
			w.Crews->HavenOf = [self](cs::Ref<Ship> s) { return self->HomeHaven(s); };
			w.Crews->RaidersTaken = [self](cs::Ref<EscapePod> pod, cs::Ref<Faction> by, double when)
			{
				if (by->IsMilitary && pod->Haven != nullptr) self->Discover(pod->Haven, by, when, cs::Cat("raiders picked up from the ", pod->Name, "'s escape pods give it away"));
			};
			w.Crews->Launched += [self](cs::Ref<EscapePod> pod)
			{
				self->world->Log(pod->LaunchedJd,
					cs::Cat("The ", pod->Name, " launches ", pod->Pods, " escape pod", (pod->Pods == 1 ? "" : "s"), ": ", cs::Fmt(pod->People, "0"), " ", pod->Owners->Name, " crew adrift and broadcasting"), WorldEventKind::Loss, War::PodBody(pod));
			};
			w.Crews->Resolved += [self](cs::Ref<EscapePod> pod, cs::Ref<Ship> ship)
			{
				cs::String by = ship != nullptr ? cs::Cat("the ", ship->Name) : pod->TakenBy != nullptr ? cs::Cat("the ", pod->TakenBy->Name) : cs::String("nobody");
				cs::String people = cs::Fmt(pod->People, "0");
				cs::String text;
				if (pod->Breacher)
				{
					switch (pod->State)
					{
					// A drifting Breacher round (U.SPC-T519).
					case PodState::Rescued: text = cs::Cat(War::Cap(by), " tows in the ", pod->Name, ": ", people, " ", pod->Owners->Name, " marines home safe"); break;
					case PodState::Hostage: text = cs::Cat(War::Cap(by), " salvages the ", pod->Name, " and holds its ", people, " marines for ransom or sale"); break;
					case PodState::Prisoner: text = cs::Cat(War::Cap(by), " tows in the ", pod->Name, " and takes its ", people, " marines prisoner"); break;
					default: text = cs::Cat("The ", pod->Name, " falls silent: its air ran out with ", people, " marines inside"); break;
					}
				}
				else
				{
					switch (pod->State)
					{
					case PodState::Rescued: text = cs::Cat(War::Cap(by), " picks up the ", pod->Name, "'s escape pods: ", people, " crew safe"); break;
					case PodState::Hostage: text = cs::Cat(War::Cap(by), " hauls in the ", pod->Name, "'s escape pods and holds ", people, " crew for ransom"); break;
					case PodState::Prisoner: text = cs::Cat(War::Cap(by), " picks up the ", pod->Name, "'s escape pods and takes ", people, " prisoners"); break;
					default: text = cs::Cat("The ", pod->Name, "'s escape pods fall silent: their air ran out with ", people, " aboard"); break;
					}
				}
				self->world->Log(pod->ResolvedJd, text, pod->State == PodState::Lost ? WorldEventKind::Loss : WorldEventKind::Info, War::PodBody(pod));
			};
			w.Crews->CaptivesReleased += [self](cs::Ref<Captives> c, cs::String how, double when)
			{
				self->world->Log(when, how, WorldEventKind::Info, c->Haven != nullptr ? c->Haven->Body : -1);
			};
		}

		// War::StartBattle hooks each battle it opens: again for every battle the save holds.
		static void Battles(War& w)
		{
			auto battles = w.Battles;
			for (const auto& b : battles) w.Hook(b);
		}

		// TradeDesk::Construct: the insurance hears of the house's losses.
		static void Trade(TradeDesk& d)
		{
			auto insurance = d.Insurance;
			d.world->Holdings->ShipLost += [insurance](cs::Ref<OwnedShip> o, double value, double jd) { insurance->OnShipLost(o, value, jd); };
			d.world->Holdings->PropertyLost += [insurance](cs::Ref<Property> prop, double value, double jd) { insurance->OnPropertyLost(prop, value, jd); };
		}

		static void RewireWorld(::SpaceGod::Core::World& w) { w.RewireAfterLoad(); }

		// InterstellarTraffic's constructor and Register; Galaxy::Create's polities hook; InterstellarColonisation's constructor.
		static void Galaxy(::SpaceGod::Core::Galaxy& g)
		{
			if (auto traffic = g.Traffic)
			{
				auto* self = traffic.get();
				for (const auto& system : g.Systems) self->Register(system);
				g.DeepSpace->Arrived += [self](cs::Ref<Cruise> c, double when) { self->OnCruiseArrived(c, when); };
				g.DeepSpace->ReachedStar += [self](cs::Ref<Cruise> c, double when) { self->OnReachedStar(c, when); };
				g.Polities->InTransit = [traffic]() { return traffic->InTransit(); };
			}
			if (auto colonisation = g.Colonisation)
			{
				auto* self = colonisation.get();
				for (const auto& s : g.Systems) self->Wire(s);
				g.SystemAdded += [self](cs::Ref<StarSystem> s, double jd) { (void)jd; self->Wire(s); };
			}
		}
	};

	void World::RewireAfterLoad()
	{
		// ---- World::Construct
		Adventures->Spawned += [this](cs::Ref<Opportunity> op)
		{
			Log(op->SpawnJd, cs::Cat(op->MarkerLabel(), ": ", op->Hook), WorldEventKind::Opportunity,
				op->Ship && op->Ship->Orbit ? op->Ship->Orbit->Body : op->Body);
		};
		Adventures->Resolved += [this](cs::Ref<Opportunity> op)
		{
			Log(op->ResolvedJd, cs::Cat(op->MarkerLabel(), op->State == OpportunityState::Completed ? " done: " : " lost: ", op->Outcome),
				WorldEventKind::Opportunity, op->Ship && op->Ship->Orbit ? op->Ship->Orbit->Body : op->Body);
			CreditAdventure(op); // US-T148
		};
		if (Shocks) Graph::Access<1>::Shocks(*Shocks);
		// A world World::Wire never ran on (no war) has nothing more.
		if (War == nullptr) return;
		auto economy = Economy;
		auto fleet = Fleet;
		auto war = War;
		Graph::Access<1>::WarCrews(*war);
		// ---- World::Wire
		{
			auto outlaws = Outlaws;
			auto law = Law;
			war->BattleStarted += [outlaws](cs::Ref<Battle> b) { outlaws->OnBattleStarted(b); };
			war->BattleEnded += [law](cs::Ref<Battle> b, double jd) { law->OnBattleEnded(b, jd); };
			war->BattleEnded += [outlaws](cs::Ref<Battle> b, double jd) { outlaws->OnBattleEnded(b, jd); };
			Law->Committed += [outlaws](cs::Ref<Crime> c) { outlaws->OnCrime(c); };
			war->Crews->Resolved += [outlaws](cs::Ref<EscapePod> pod, cs::Ref<Ship> ship) { outlaws->OnPodResolved(pod, ship); };
		}
		{
			auto contracts = Contracts;
			war->BattleEnded += [contracts](cs::Ref<Battle> b, double jd) { contracts->OnBattleEnded(b, jd); };
		}
		if (Trade) Graph::Access<1>::Trade(*Trade);
		// ---- World::WireMining
		if (auto m = Mining)
		{
			m->Position = [this](int body, double jd) { return Ephemeris->HelioAt(body, jd); };
			m->PiratePressure = [this]() { return MiningPiratePressure(); };
			m->Raiders = [this]() { return Pirates; };
			m->Guarded = [this](cs::Ref<MaterialWarehouse> w)
			{
				for (const auto& s : Economy->Settlements)
					if (s->Body == w->Body && s->Owner == w->Owner && !s->Fallen && s->Platforms.Count() > 0) return true;
				return false;
			};
			m->Log = [this](double jd, cs::String text, int body) { Log(jd, text, WorldEventKind::Economy, body); };
			m->HeavyLane = [this](int from, int to, double jd) { return MiningHeavyLane(from, to, jd); };
			// The ore tugs are real hulls in the fleet (U.SPC-T498): the works sends them, the fleet flies them.
			m->TugFree = [this](cs::Ref<Ship> tug) { return TugFree(tug); };
			m->HaulSent = [this](cs::Ref<TugHaul> haul, double jd) { TugSent(haul, jd); };
			m->TugThreat = [this](cs::Ref<TugHaul> haul, double jd) { return TugThreat(haul, jd); };
			m->TugSeized = [this](cs::Ref<TugHaul> haul, double jd) { HandOverTug(haul, jd); };
			// So are the freighters its raw-material convoys sail in (U.SPC-T523): the works loads them, the fleet flies them.
			m->FreighterFree = [this](cs::Ref<Ship> freighter) { return FreighterFree(freighter); };
			m->ConvoySailed = [this](cs::Ref<MaterialConvoy> convoy, double jd) { FreighterSailed(convoy, jd); };
			m->ConvoyDelivered = [this](cs::Ref<MaterialConvoy> convoy, double jd) { FreighterDelivered(convoy, jd); };
			m->YardBuilt = [this](cs::Ref<MegastructureProject> p, double /*jd*/)
			{
				for (const auto& s : Economy->Settlements)
				{
					if (s->Body != p->SiteBody || s->Owner != p->Builder || s->Fallen) continue;
					for (const auto& f : s->Facilities)
						if (f->Recipe->Kind == IndustryKind::Shipyard)
						{
							f->Level += 1;
							f->BaseLevel += 1;
							return;
						}
					s->Facilities.Add(cs::New<Facility>(Recipe::Of(IndustryKind::Shipyard), 1));
					return;
				}
			};
		}
		{
			auto law = Law;
			economy->DockAllowed = [law](cs::Ref<Ship> ship, cs::Ref<Settlement> s) { return law->AllowsDocking(ship, s); };
			economy->PriceFactor = [law](cs::Ref<Ship> ship, cs::Ref<Settlement> s) { return law->PriceFactorFor(ship, s); };
		}
		// ---- World::WireColonisation
		if (auto c = Colonisation)
		{
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
		}
		// Merchants and couriers go down to the surface through the rings; warships and pirates hold orbit, and so does a
		// bulk freighter, built in space and never landing: it trades from orbit (U.SPC-T482).
		fleet->Lands = [this](cs::Ref<Ship> s) { return IsTrader(s) && !BulkFreighters::NeverLands(s); };
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
		fleet->Leaked = [this](cs::Ref<Ship> ship, double tonnes, double when)
		{
			Log(when, cs::Cat(ship->Name, " vents ", cs::Fmt(tonnes, "N0"), " t of reaction mass from a holed tank"), WorldEventKind::Info, ship->Orbit ? ship->Orbit->Body : -1);
		};
		economy->Danger = [war](int body) { return war->Danger(body) * 0.08; };
		{
			auto companies = Companies;
			// economy.Loaded / economy.Sold are multicast delegate fields in C# (cs::Delegate): `+=` keeps whoever
			// subscribed first (MarketShocks, in the constructor, subscribes to Sold) and calls them in order.
			economy->Loaded += [companies](cs::Ref<Ship> ship, cs::Ref<Settlement> from) { companies->OnLoaded(ship, from); };
			economy->Sold += [companies](cs::Ref<Ship> ship, cs::Ref<Settlement> to, Good good, double tonnes) { companies->OnSold(ship, to, good, tonnes); };
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
		// ---- War::StartBattle's hooks, for the battles under way
		Graph::Access<1>::Battles(*war);
	}

	void Registrations::RewireGalaxy(::SpaceGod::Core::Galaxy& g)
	{
		for (const auto& s : g.Systems)
			if (s->World) Graph::Access<1>::RewireWorld(*s->World);
		Graph::Access<1>::Galaxy(g);
	}
}
