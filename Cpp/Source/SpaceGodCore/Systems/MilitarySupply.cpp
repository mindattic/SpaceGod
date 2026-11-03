// Port of Assets/UnitySpace/Core/Systems/MilitarySupply.cs (see docs/MIGRATION.md).

#include "Systems/MilitarySupply.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "DVec3.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Combat/War.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Holdings.h"
#include "Economy/Propulsion.h"
#include "Economy/Settlement.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/InterstellarWars.h"
#include "Systems/JumpGate.h"
#include "Systems/Polities.h"

namespace SpaceGod::Core
{
	// ================================================================ FleetSupply

	double FleetSupply::ReadinessOf(double stock)
	{
		return stock >= LowSupply ? 1 : ReadinessFloor + (1 - ReadinessFloor) * cs::Math::Max(0.0, stock) / LowSupply;
	}

	double FleetSupply::Readiness() const { return ReadinessOf(Lowest()); }

	double FleetSupply::Lowest() const { return cs::Math::Min(Propellant, cs::Math::Min(Munitions, Spares)); }

	bool FleetSupply::MustWithdraw() const { return Readiness() < WithdrawReadiness && Convoy == nullptr; }

	void FleetSupply::Drain(double days, bool moving, bool fighting)
	{
		Propellant = cs::Math::Max(0.0, Propellant - days / (moving ? MarchDays : IdleDays));
		if (fighting) Munitions = cs::Math::Max(0.0, Munitions - days / BattleDays);
		Spares = cs::Math::Max(0.0, Spares - days / WearDays);
	}

	double FleetSupply::FieldRepair(double damage, int hulls, double days)
	{
		if (damage <= 0.001 || Spares <= 0) return 0;
		double perShare = SparesPerRepair / cs::Math::Max(1, hulls);
		double share = cs::Math::Min(damage, FieldRepairPerDay * days * Readiness());
		share = cs::Math::Min(share, Spares / perShare);
		Spares = cs::Math::Max(0.0, Spares - share * perShare);
		return share;
	}

	void FleetSupply::Deliver(cs::Ref<SupplyConvoy> c)
	{
		Propellant = cs::Math::Min(1.0, Propellant + c->Propellant);
		Munitions = cs::Math::Min(1.0, Munitions + c->Munitions);
		Spares = cs::Math::Min(1.0, Spares + c->Spares);
	}

	// ================================================================ SupplyConvoy

	cs::String SupplyConvoy::ToString() const
	{
		return cs::Cat(Tankers, " tanker", Tankers == 1 ? "" : "s", " and ", Colliers, " collier", Colliers == 1 ? "" : "s", " from ", Base ? Base->Name : cs::String());
	}

	// ================================================================ MilitaryLogistics

	const cs::List<cs::Ref<SupplyConvoy>>& MilitaryLogistics::Convoys() const { return convoys; }

	MilitaryLogistics::MilitaryLogistics(Galaxy* galaxy)
		: galaxy(galaxy)
	{
	}

	cs::Ref<Core::Diplomacy> MilitaryLogistics::Diplomacy() const { return galaxy->Polities->Diplomacy; }

	bool MilitaryLogistics::BaseOpen(cs::Ref<Settlement> s, cs::Ref<World> world)
	{
		if (s == nullptr || s->Fallen || !s->HasShipyard() || s->Kind == SettlementKind::Haven) return false;
		auto by = Diplomacy()->BlockadedBy(s);
		if (by != nullptr && by != s->Owner) return false;
		auto besieger = world->War->Besieger(s);
		if (besieger != nullptr && besieger != s->Owner) return false;
		return world->War->BattleAt(s->Body) == nullptr;
	}

	MilitaryLogistics::LineToResult MilitaryLogistics::LineTo(cs::Ref<Faction> f, cs::Ref<StarSystem> here, double jd)
	{
		cs::Ref<Settlement> best = nullptr;
		GateRoute bestRoute = nullptr;
		double bestDays = cs::Double::MaxValue;
		if (f == nullptr || here == nullptr) return LineToResult{ nullptr, nullptr };
		for (const auto& s : galaxy->Systems)
		{
			GateRoute route = nullptr;
			bool tried = false;
			for (const auto& st : s->World->Economy->Settlements)
			{
				if (st->Owner != f || !BaseOpen(st, s->World)) continue;
				if (!tried)
				{
					tried = true;
					route = s == here ? cs::New<cs::List<cs::Ref<JumpGate>>>() : (galaxy->Network ? galaxy->Network->Route(s, here, f, jd) : nullptr);
				}
				if (route == nullptr) break;
				double days = GateNetwork::RouteDays(route);
				if (days < bestDays)
				{
					bestDays = days;
					best = st;
					bestRoute = route;
				}
			}
		}
		return LineToResult{ best, bestRoute };
	}

	void MilitaryLogistics::InGate(cs::Ref<Campaign> c, double jd) { c->Supply->Drain(1, /*moving:*/ true, /*fighting:*/ false); }

	void MilitaryLogistics::Day(cs::Ref<Campaign> c, cs::Ref<StarSystem> system, cs::Ref<Ship> leader, double jd)
	{
		auto supply = c->Supply;
		auto world = system->World;
		auto group = Group(world, leader);
		bool fighting = false;
		for (const auto& s : group) fighting |= s->Battle != nullptr;
		bool moving = leader->Phase != ShipPhase::Parked || leader->GateKm.has_value();
		supply->Drain(1, moving, fighting);
		for (const auto& s : group)
			if (s->Battle == nullptr)
			{
				double share = supply->FieldRepair(s->Combat->Damage(), group.Count());
				if (share > 0) s->Combat->Repair(share);
			}

		auto [station, route] = LineTo(c->Faction, system, jd);
		supply->Base = station;
		if (station == nullptr && !supply->OutOfSupply)
		{
			supply->OutOfSupply = true;
			supply->OutOfSupplySinceJd = jd;
			world->Log(jd, cs::Cat("The ", c->Faction->Name, " war fleet under the ", leader->Name, " at ", system->Name, " is cut off from its bases: out of supply"), WorldEventKind::Info,
				system->Catalog->Root()->Index);
		}
		else if (station != nullptr && supply->OutOfSupply)
		{
			supply->OutOfSupply = false;
			supply->OutOfSupplySinceJd = cs::Double::NaN;
			world->Log(jd, cs::Cat("The ", c->Faction->Name, " war fleet under the ", leader->Name, " is in supply again from ", station->Name), WorldEventKind::Info, system->Catalog->Root()->Index);
		}

		auto convoy = supply->Convoy;
		if (convoy != nullptr) Track(c, convoy, system, leader, group, jd);
		if (supply->Convoy == nullptr && station != nullptr && supply->Lowest() < FleetSupply::ResupplyBelow) Dispatch(c, station, route, system, leader, group, jd);

		double r = supply->Readiness();
		for (const auto& s : group) s->Combat->Readiness = r;
	}

	void MilitaryLogistics::Track(cs::Ref<Campaign> c, cs::Ref<SupplyConvoy> convoy, cs::Ref<StarSystem> system, cs::Ref<Ship> leader, const cs::List<cs::Ref<Ship>>& group, double jd)
	{
		if (Alive(convoy, 0, convoy->Hulls.Count()) == 0)
		{
			Lost(c, convoy, system, jd, cs::Cat("The ", c->Faction->Name, " supply ships (", convoy, ") are lost on their way to the war fleet at ", system->Name));
			return;
		}
		auto [at, flag] = LeaderOf(convoy);
		if (flag == nullptr)
		{
			// Inside a gate: it is on its way.
			if (InGate(convoy)) return;
			// Its voyage given up (a gate shut against it for good) or its leading hull taken with nobody to carry on.
			Lost(c, convoy, system, jd, cs::Cat("The ", c->Faction->Name, " supply ships (", convoy, ") cannot get through to the war fleet at ", system->Name, ": the way is shut to them"));
			return;
		}
		auto v = flag->Voyage;
		auto traffic = galaxy->Traffic;
		if (at == system)
		{
			if (Met(system->World, flag, leader, jd))
			{
				Arrive(c, convoy, system, leader, group, jd);
				return;
			}
			// Here, but bound on through a gate for where the fleet was: it stops here, and makes for the fleet.
			if (v->Destination != system && traffic) traffic->Hold(system, flag, jd);
			return;
		}
		// In another system, or bound for one the fleet has left: the way from where it is to where the fleet is now
		// must be open to it, or it cannot get through.
		GateRoute way = galaxy->Network ? galaxy->Network->Route(at, system, c->Faction, jd) : nullptr;
		if (way == nullptr || way->Count() == 0)
		{
			Lost(c, convoy, system, jd, cs::Cat("The ", c->Faction->Name, " supply ships (", convoy, ") cannot get through to the war fleet at ", system->Name, ": the way behind it is cut"));
			return;
		}
		if (v->Destination != system && !v->AtGate && flag->Battle == nullptr && traffic) traffic->Redirect(at, flag, system, jd, /*force:*/ false);
	}

	int MilitaryLogistics::ConvoyOrders(cs::Ref<StarSystem> system, cs::Ref<Ship> ship, int here, double jd)
	{
		auto convoy = ship->Voyage ? ship->Voyage->Convoy : nullptr;
		if ((convoy ? convoy->Campaign : nullptr) == nullptr) return -1;
		ship->StakeoutKm = std::nullopt;
		auto leader = FlagshipIn(system->World, convoy->Campaign);
		if (leader == nullptr) return here;
		bool parked = leader->Phase == ShipPhase::Parked || leader->Phase == ShipPhase::Landed;
		int port = parked ? InterstellarTraffic::PortOf(leader) : leader->DestinationBody;
		if (port < 0) return here;
		int root = system->Catalog->Root()->Index;
		if (port != root) return port;
		// The fleet is out off a gate (holding it, or bound for it): the supply ships fly out to the same spot off the
		// star, as raiders do to lie in wait off one, never to an orbit round the star itself.
		cs::Nullable<DVec3> spot = leader->GateKm;
		if (!spot.has_value()) spot = leader->StakeoutKm;
		if (!spot.has_value())
			spot = (parked && leader->Orbit != nullptr && leader->Orbit->Body == root ? cs::Nullable<DVec3>(leader->Orbit->OffsetAt(jd)) : cs::Nullable<DVec3>());
		if (!spot.has_value()) return here;
		ship->StakeoutKm = spot;
		// Already off the star, but away from the spot: a real hop out to it, though it starts and ends at one body.
		if (here == root && ship->Orbit != nullptr && (ship->Orbit->OffsetAt(jd) - *spot).Length() > RendezvousKm) ship->Homing = true;
		return root;
	}

	bool MilitaryLogistics::Met(cs::Ref<World> world, cs::Ref<Ship> flag, cs::Ref<Ship> leader, double jd)
	{
		if (flag->Battle != nullptr || (flag->Phase != ShipPhase::Parked && flag->Phase != ShipPhase::Landed)) return false;
		if ((leader->Phase == ShipPhase::Parked || leader->Phase == ShipPhase::Landed) &&
			InterstellarTraffic::PortOf(flag) == InterstellarTraffic::PortOf(leader)) return true;
		return (world->Fleet->StateOf(flag, jd).HelioKm - world->Fleet->StateOf(leader, jd).HelioKm).Length() <= RendezvousKm;
	}

	cs::Ref<Ship> MilitaryLogistics::FlagshipIn(cs::Ref<World> world, cs::Ref<Campaign> c)
	{
		for (const auto& s : world->Fleet->Ships())
			if (!s->Destroyed && !s->IsEscort() && !s->Derelict && s->Faction == c->Faction && s->Voyage != nullptr && s->Voyage->Campaign == c)
				return s;
		return nullptr;
	}

	std::pair<cs::Ref<StarSystem>, cs::Ref<Ship>> MilitaryLogistics::LeaderOf(cs::Ref<SupplyConvoy> convoy)
	{
		for (const auto& s : galaxy->Systems)
			for (const auto& ship : s->World->Fleet->Ships())
				if (!ship->Destroyed && !ship->IsEscort() && !ship->Derelict && ship->Faction == convoy->Faction && (ship->Voyage ? ship->Voyage->Convoy : nullptr) == convoy)
					return { s, ship };
		return { nullptr, nullptr };
	}

	bool MilitaryLogistics::InGate(cs::Ref<SupplyConvoy> convoy)
	{
		if (galaxy->Traffic == nullptr) return false;
		for (const auto& p : galaxy->Traffic->InGates())
			if ((p->Leader()->Voyage ? p->Leader()->Voyage->Convoy : nullptr) == convoy) return true;
		return false;
	}

	int MilitaryLogistics::Alive(cs::Ref<SupplyConvoy> convoy, int from, int count)
	{
		int n = 0;
		for (int i = from; i < from + count && i < convoy->Hulls.Count(); i++)
		{
			auto s = convoy->Hulls[i];
			if (s != nullptr && !s->Destroyed && !s->Derelict && s->Faction == convoy->Faction) n++;
		}
		return n;
	}

	void MilitaryLogistics::Lost(cs::Ref<Campaign> c, cs::Ref<SupplyConvoy> convoy, cs::Ref<StarSystem> system, double jd, const cs::String& why)
	{
		c->Supply->Convoy = nullptr;
		convoys.Remove(convoy);
		c->Supply->ConvoysLost++;
		system->World->Log(jd, why, WorldEventKind::Loss, system->Catalog->Root()->Index);
		Release(convoy, jd);
	}

	void MilitaryLogistics::Release(cs::Ref<SupplyConvoy> convoy, double jd)
	{
		auto traffic = galaxy->Traffic;
		if (traffic != nullptr)
			for (const auto& p : traffic->InGates())
				if ((p->Leader()->Voyage ? p->Leader()->Voyage->Convoy : nullptr) == convoy) p->Leader()->Voyage->Convoy = nullptr;
		cs::List<std::pair<cs::Ref<StarSystem>, cs::Ref<Ship>>> bound;
		for (const auto& s : galaxy->Systems)
			for (const auto& ship : s->World->Fleet->Ships())
				if ((ship->Voyage ? ship->Voyage->Convoy : nullptr) == convoy) bound.Add({ s, ship });
		for (const auto& [s, ship] : bound)
		{
			auto v = ship->Voyage;
			v->Convoy = nullptr;
			ship->StakeoutKm = std::nullopt;
			ship->Homing = false;
			if (ship->Destroyed || ship->Derelict || ship->IsEscort() || ship->Faction != v->Faction || traffic == nullptr) continue;
			// Home now, or the way home; with no way open, it lies where it is and the traffic tries again later.
			if (v->AtGate) traffic->Hold(s, ship, jd);
			if (traffic->Redirect(s, ship, v->Home, jd, /*force:*/ false)) continue;
			v->Destination = s;
			v->Route = cs::List<cs::Ref<JumpGate>>();
			v->Leg = 0;
			v->StayUntilJd = jd;
		}
	}

	void MilitaryLogistics::Arrive(cs::Ref<Campaign> c, cs::Ref<SupplyConvoy> convoy, cs::Ref<StarSystem> system, cs::Ref<Ship> leader, const cs::List<cs::Ref<Ship>>& group, double jd)
	{
		auto supply = c->Supply;
		supply->Convoy = nullptr;
		convoys.Remove(convoy);
		auto world = system->World;
		// Each tanker and collier brings its own share: those lost on the way bring nothing.
		int tankers = Alive(convoy, 0, convoy->Tankers), colliers = Alive(convoy, convoy->Tankers, convoy->Colliers);
		double tankShare = convoy->Tankers > 0 ? static_cast<double>(tankers) / convoy->Tankers : 0;
		double collierShare = convoy->Colliers > 0 ? static_cast<double>(colliers) / convoy->Colliers : 0;
		convoy->Propellant *= tankShare;
		convoy->Munitions *= collierShare;
		convoy->Spares *= collierShare;
		supply->Deliver(convoy);
		supply->ConvoysArrived++;
		world->Log(jd, cs::Cat("The ", c->Faction->Name, " supply ships from ", convoy->Base ? convoy->Base->Name : cs::String(), " come up with the war fleet under the ", leader->Name, " at ", system->Name, ": ",
				tankers, " tanker", tankers == 1 ? "" : "s", " and ", colliers, " collier", colliers == 1 ? "" : "s", " hand over their loads"),
			WorldEventKind::Info, InterstellarTraffic::PortOf(leader));
		for (const auto& s : convoy->Hulls)
			if (s != nullptr && !s->Destroyed && s->Faction == convoy->Faction) s->Cargo = cs::Cat("Empty, back to ", convoy->Base ? convoy->Base->Name : cs::String("base"));
		Release(convoy, jd);
		// Tanks topped up and magazines filled from what the fleet now has aboard.
		for (const auto& s : group)
		{
			s->SetPropellant(cs::Math::Max(s->Propellant(), s->PropellantCapacity() * supply->Propellant));
			for (const auto& sub : s->Combat->Subsystems)
				if (sub->Def->Weapon != nullptr && sub->Def->Weapon->Missile && !sub->Destroyed())
					sub->Ammo = cs::Math::Max(sub->Ammo, cs::ToInt(cs::Math::Round(sub->Def->Weapon->Ammo * supply->Munitions)));
		}
	}

	void MilitaryLogistics::Dispatch(cs::Ref<Campaign> c, cs::Ref<Settlement> station, GateRoute route, cs::Ref<StarSystem> system, cs::Ref<Ship> leader, const cs::List<cs::Ref<Ship>>& group, double jd)
	{
		auto f = c->Faction;
		if (f->Treasury <= 0) return;
		auto supply = c->Supply;
		int hulls = group.Count();
		double capacity = 0;
		for (const auto& s : group) capacity += s->PropellantCapacity();

		double propNeed = (1 - supply->Propellant) * capacity;
		double munNeed = (1 - supply->Munitions) * MunitionsPerHull * hulls;
		double alloysNeed = (1 - supply->Spares) * AlloysPerHull * hulls;
		double elecNeed = (1 - supply->Spares) * ElectronicsPerHull * hulls;
		auto& stock = station->Stock;
		auto& price = station->Price;
		(void)price;
		// The economy feels what the war asks of it, whether or not it can give it.
		station->ExtraDemand[static_cast<int>(Good::Water)] += propNeed;
		station->ExtraDemand[static_cast<int>(Good::Munitions)] += munNeed;
		station->ExtraDemand[static_cast<int>(Good::Alloys)] += alloysNeed;
		station->ExtraDemand[static_cast<int>(Good::Electronics)] += elecNeed;

		double massHave = stock[static_cast<int>(Good::Water)] + stock[static_cast<int>(Good::Hydrocarbons)];
		double propShare = propNeed > 0 ? cs::Math::Min(1.0, massHave / propNeed) : 0;
		double munShare = munNeed > 0 ? cs::Math::Min(1.0, stock[static_cast<int>(Good::Munitions)] / munNeed) : 0;
		double sparesShare = alloysNeed > 0 ? cs::Math::Min(1.0, cs::Math::Min(stock[static_cast<int>(Good::Alloys)] / alloysNeed, stock[static_cast<int>(Good::Electronics)] / cs::Math::Max(elecNeed, 1e-9))) : 0;

		// Worked out, and the trip judged worth sending, before anything is actually taken off the market: Take()
		// mutates the station's stock as it goes, so finding out only afterward that there is nothing worth a
		// convoy over would already have thrown away stock the fleet never got and nobody ever paid for.
		double prop = propNeed * propShare;
		double mun = munNeed * munShare;
		double spares = (alloysNeed + elecNeed) * sparesShare;
		if (prop + mun + spares < 1) return;
		// The supply ships sail from the base's own system (U.SPC-T525): no traffic to fly them, no convoy.
		auto from = SystemOf(station);
		if (!from) from = system;
		if (galaxy->Traffic == nullptr || (from != system && (route == nullptr || route->Count() == 0))) return;
		int tankers = prop > 0 ? cs::ToInt(cs::Math::Ceiling(prop / TankerTonnes)) : 0;
		int colliers = mun + spares > 0 ? cs::ToInt(cs::Math::Ceiling((mun + spares) / CollierTonnes)) : 0;
		if (tankers + colliers == 0) return;

		double cost = 0;
		double fromWater = cs::Math::Min(prop, stock[static_cast<int>(Good::Water)]);
		cost += Take(station, Good::Water, fromWater) + Take(station, Good::Hydrocarbons, prop - fromWater);
		cost += Take(station, Good::HeliumThree, prop * Propulsion::HeliumThreePerTonne);
		cost += Take(station, Good::Munitions, mun);
		cost += Take(station, Good::Alloys, alloysNeed * sparesShare) + Take(station, Good::Electronics, elecNeed * sparesShare);
		f->Treasury -= cost;

		auto convoy = cs::New<SupplyConvoy>();
		convoy->Campaign = c; convoy->Faction = f; convoy->Base = station; convoy->From = from; convoy->SetOutJd = jd;
		convoy->DueJd = jd + LoadingDays + GateNetwork::RouteDays(route) + CrossingDays;
		convoy->Tankers = tankers;
		convoy->Colliers = colliers;
		convoy->Propellant = (1 - supply->Propellant) * propShare;
		convoy->Munitions = (1 - supply->Munitions) * munShare;
		convoy->Spares = (1 - supply->Spares) * sparesShare;
		convoy->Tonnes = prop + mun + spares; convoy->Cost = cost;
		Muster(convoy, station, from, jd);
		// Fresh from port, with its way checked above, the leading hull always sails.
		galaxy->Traffic->SendSupply(convoy->Hulls[0], from, system, route, convoy, jd);
		supply->Convoy = convoy;
		convoys.Add(convoy);
		auto baseWorld = from->World;
		baseWorld->Log(jd, cs::Cat(station->Name, " sends ", convoy->Tankers, " tanker", convoy->Tankers == 1 ? "" : "s", " and ", convoy->Colliers, " ammunition collier", convoy->Colliers == 1 ? "" : "s", " after the ", f->Name, " war fleet at ", system->Name),
			WorldEventKind::Launch, station->Body);
	}

	// ---------------------------------------------------------------- the supply ships' hulls (U.SPC-T525)

	const cs::List<cs::String>& MilitaryLogistics::TankerNames()
	{
		static const cs::List<cs::String> names{ "Wellspring", "Deep Well", "Cistern", "Freshet", "Reservoir", "Brimming", "Spring Tide", "Aquifer" };
		return names;
	}

	const cs::List<cs::String>& MilitaryLogistics::CollierNames()
	{
		static const cs::List<cs::String> names{ "Powder Hoy", "Magazine", "Quartermaster", "Sutler", "Armourer", "Stores", "Bandolier", "Caisson" };
		return names;
	}

	void MilitaryLogistics::Muster(cs::Ref<SupplyConvoy> convoy, cs::Ref<Settlement> station, cs::Ref<StarSystem> from, double jd)
	{
		auto world = from->World;
		auto fleet = world->Fleet;
		auto f = convoy->Faction;
		int need = convoy->Tankers + convoy->Colliers;
		// The navy's supply ships in port here with nothing to do: each its own master again (a convoy home from a run
		// still lies in formation on the hull that led it).
		cs::List<cs::Ref<Ship>> idle;
		for (const auto& s : fleet->Ships())
		{
			if (s->Destroyed || s->Derelict || s->Faction != f || s->Battle != nullptr || s->Voyage != nullptr || s->GateKm.has_value() || s == world->PilotedShip) continue;
			if (world->Holdings != nullptr && world->Holdings->Owns(s)) continue;
			auto head = s->Controller();
			if (head->Role != ShipRole::Supply || head->Voyage != nullptr || head->Battle != nullptr || head->GateKm.has_value()) continue;
			if ((head->Phase != ShipPhase::Parked && head->Phase != ShipPhase::Landed) || head->Orbit == nullptr) continue;
			// Lying at this base: one at another world of the system is not here to load.
			if (InterstellarTraffic::PortOf(head) != station->Body) continue;
			idle.Add(s);
		}
		// Every one its own master first, so none left out of this convoy goes along on a leader that is in it.
		for (const auto& s : idle)
			if (s->IsEscort()) Stand(s, station, world, jd);
		for (const auto& s : idle)
			if (convoy->Hulls.Count() < need) convoy->Hulls.Add(s);
		while (convoy->Hulls.Count() < need)
		{
			bool tanker = convoy->Hulls.Count() < convoy->Tankers;
			cs::String name = tanker ? Named(TankerNames(), tankersNamed) : Named(CollierNames(), colliersNamed);
			auto s = fleet->AddShip(name, ShipClass::Freighter, f, ShipRole::Supply);
			s->CapacityTonnes = Economy::CapacityOf(ShipClass::Freighter);
			Stand(s, station, world, jd);
			world->NotifyAdded(s);
			convoy->Hulls.Add(s);
		}
		auto flag = convoy->Hulls[0];
		for (int i = 0; i < convoy->Hulls.Count(); i++)
		{
			auto s = convoy->Hulls[i];
			s->Cargo = i < convoy->Tankers
				? cs::Cat("Reaction mass for the ", f->Name, " war fleet")
				: cs::Cat("Munitions and spares for the ", f->Name, " war fleet");
			if (i > 0) fleet->AddEscort(flag, s, Fleet::SlotFor(i - 1));
		}
	}

	cs::String MilitaryLogistics::Named(const cs::List<cs::String>& names, int& named)
	{
		cs::String name = cs::Cat(names[named % names.Count()], named >= names.Count() ? cs::Cat(" ", named / names.Count() + 1) : cs::String());
		named++;
		return name;
	}

	void MilitaryLogistics::Stand(cs::Ref<Ship> s, cs::Ref<Settlement> station, cs::Ref<World> world, double jd)
	{
		s->Leader = nullptr;
		s->Role = ShipRole::Supply;
		s->Route = cs::List<int>();
		s->Ports = cs::List<int>{ station->Body };
		world->Fleet->Station(s, station->Body, jd);
	}

	double MilitaryLogistics::Take(cs::Ref<Settlement> s, Good g, double tonnes)
	{
		tonnes = cs::Math::Min(cs::Math::Max(0.0, tonnes), s->Stock[static_cast<int>(g)]);
		s->Stock[static_cast<int>(g)] -= tonnes;
		return tonnes * s->Price[static_cast<int>(g)];
	}

	void MilitaryLogistics::Ended(cs::Ref<Campaign> c, double jd)
	{
		auto convoy = c->Supply->Convoy;
		c->Supply->Convoy = nullptr;
		if (convoy == nullptr) return;
		convoys.Remove(convoy);
		// Its supply ships, wherever they are, turn for home with what they carry (U.SPC-T525).
		if (cs::Double::IsNaN(jd)) jd = convoy->From != nullptr && !cs::Double::IsNaN(convoy->From->World->Jd) ? convoy->From->World->Jd : convoy->SetOutJd;
		Release(convoy, jd);
	}

	cs::Ref<StarSystem> MilitaryLogistics::SystemOf(cs::Ref<Settlement> st)
	{
		auto world = galaxy->Polities->WorldOf(st);
		for (const auto& s : galaxy->Systems)
			if (s->World == world) return s;
		return nullptr;
	}

	cs::List<cs::Ref<Ship>> MilitaryLogistics::Group(cs::Ref<World> world, cs::Ref<Ship> leader)
	{
		cs::List<cs::Ref<Ship>> list{ leader };
		for (const auto& s : world->Fleet->Ships())
			if (s != leader && s->Leader == leader && !s->Destroyed) list.Add(s);
		return list;
	}
}
