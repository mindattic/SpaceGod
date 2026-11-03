// Port of Unity.SpaceGod Core/Systems/InterstellarTraffic.cs (US-T15, see docs/MIGRATION.md).

#include "Systems/InterstellarTraffic.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Combat/GatePickets.h"
#include "Combat/War.h"
#include "Economy/Economy.h"
#include "Economy/Escorts.h"
#include "Economy/Goods.h"
#include "Economy/Holdings.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Information/LightSpeed.h"
#include "Information/News.h"
#include "Infrastructure/Infrastructure.h"
#include "Law/Outlaws.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "Systems/CruiseDrive.h"
#include "Systems/DeepSpace.h"
#include "Systems/FleetDoctrine.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/InterstellarWars.h"
#include "Systems/JumpGate.h"
#include "Systems/MilitarySupply.h"
#include "Systems/PirateClans.h"
#include "Systems/Polities.h"
#include "Systems/StrategicCommand.h"
#include "World.h"

namespace SpaceGod::Core
{
	namespace
	{
		// .NET's double.CompareTo: NaN sorts before everything (and equals itself), else the usual order.
		int CompareTo(double a, double b)
		{
			if (a < b) return -1;
			if (a > b) return 1;
			if (a == b) return 0;
			if (cs::Double::IsNaN(a)) return cs::Double::IsNaN(b) ? 0 : -1;
			return 1;
		}
	}

	const char* ToString(VoyagePurpose v)
	{
		switch (v)
		{
			case VoyagePurpose::Trade: return "Trade";
			case VoyagePurpose::Courier: return "Courier";
			case VoyagePurpose::Survey: return "Survey";
			case VoyagePurpose::Raid: return "Raid";
			case VoyagePurpose::Expedition: return "Expedition";
			case VoyagePurpose::Campaign: return "Campaign";
			case VoyagePurpose::Smuggle: return "Smuggle";
			case VoyagePurpose::Relocate: return "Relocate";
			case VoyagePurpose::Hunt: return "Hunt";
			case VoyagePurpose::Ambush: return "Ambush";
			case VoyagePurpose::Directed: return "Directed";
			case VoyagePurpose::Plotted: return "Plotted";
			case VoyagePurpose::Supply: return "Supply";
			case VoyagePurpose::Colonise: return "Colonise";
		}
		return "";
	}

	// ==================================================================== Voyage

	void Voyage::LostWithAllHands()
	{
		if (Purpose == VoyagePurpose::Hunt && Origin != nullptr && Home && Home->World->Outlaws) Home->World->Outlaws->SetAway(Origin, false);
	}

	// ==================================================================== InterstellarTraffic

	cs::List<cs::Ref<Ship>> InterstellarTraffic::InTransit() const
	{
		auto list = cs::List<cs::Ref<Ship>>();
		for (const auto& p : inGates)
			for (const auto& s : p->Ships)
				list.Add(s);
		for (const auto& s : galaxy->DeepSpace->InFlight())
			list.Add(s);
		return list;
	}

	cs::List<cs::Ref<Voyage>> InterstellarTraffic::VoyagesInTransit() const
	{
		auto list = cs::List<cs::Ref<Voyage>>();
		for (const auto& p : inGates)
			if (p->Leader()->Voyage != nullptr) list.Add(p->Leader()->Voyage);
		for (const auto& c : galaxy->DeepSpace->Cruises())
			if (c->Ships.Count() > 0 && c->Leader()->Voyage != nullptr) list.Add(c->Leader()->Voyage);
		return list;
	}

	void InterstellarTraffic::LogCrossing(const cs::Ref<GateCrossing>& c)
	{
		int i = crossingLog.Count();
		while (i > 0 && crossingLog[i - 1]->Jd > c->Jd) i--;
		crossingLog.Insert(i, c);
	}

	int InterstellarTraffic::MaxTradersPerRoute() { return TradersPerRoute()->IntValue(); }

	const cs::Ref<RangeSetting>& InterstellarTraffic::TradersPerRoute()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("economy.tradersPerRoute", SettingGroups::Economy,
			"Freighters per interstellar route", 4, 1, 12, 1,
			"The most of a system's freighters out on its runs to one other system at once: more makes busier lanes and tighter margins.",
			[](double v) { return cs::Fmt(v, "0"); }, "trade traffic interstellar freight");
		return s;
	}

	InterstellarTraffic::InterstellarTraffic(cs::Ref<::SpaceGod::Core::Galaxy> galaxy, double jd, int seed)
		: rng(seed), cruiseRng(seed + 13), ambushRng(seed + 29)
	{
		this->galaxy = galaxy;
		nextDailyJd = jd + 0.5;
		News = cs::New<Newswire>(galaxy, jd);
		// C# hands PirateClans `this`; no Ref to this traffic exists while it is being constructed.
		Clans = cs::New<PirateClans>(galaxy, this, seed + 7);
		for (const auto& system : galaxy->Systems) Register(system);
		galaxy->DeepSpace->Arrived += [this](cs::Ref<Cruise> c, double when) { OnCruiseArrived(c, when); };
		galaxy->DeepSpace->ReachedStar += [this](cs::Ref<Cruise> c, double when) { OnReachedStar(c, when); };
	}

	void InterstellarTraffic::Register(const cs::Ref<StarSystem>& s)
	{
		s->World->ReachedGate += [this, s](cs::Ref<Ship> ship, double when) { OnReachedGate(s, ship, when); };
		s->World->VoyageOrders = [this, s](cs::Ref<Ship> ship, int here, double when) { return Orders(s, ship, here, when); };
		Clans->Watch(s);
	}

	cs::Ref<GateNetwork> InterstellarTraffic::Network() const { return galaxy->Network; }

	void InterstellarTraffic::Update(double jd)
	{
		// Ships that have reached a gate go in (or wait, or turn for another), in date order.
		atGates.Sort([](const AtGateEntry& a, const AtGateEntry& b)
		{
			return CompareTo(a.ship->Voyage ? a.ship->Voyage->AtGateJd : 0, b.ship->Voyage ? b.ship->Voyage->AtGateJd : 0);
		});
		auto snapshot = cs::List<AtGateEntry>(atGates);
		for (const auto& [system, ship] : snapshot)
		{
			auto v = ship->Voyage;
			// The first try is when it got there; a ship already waiting tries again now.
			double when = v != nullptr && cs::Double::IsNaN(v->WaitingSinceJd) ? cs::Math::Min(v->AtGateJd, jd) : jd;
			TryEnter(system, ship, when, jd);
		}

		// In the order they come out, not the order they went in: each draws on shared random numbers, so two
		// due in one step must take them as they would on separate frames (U.SPC-T384).
		cs::Ref<cs::List<cs::Ref<Passage>>> due = nullptr;
		for (int i = inGates.Count() - 1; i >= 0; i--)
			if (inGates[i]->ExitJd <= jd)
			{
				if (!due) due = cs::New<cs::List<cs::Ref<Passage>>>();
				due->Add(inGates[i]);
				inGates.RemoveAt(i);
			}
		if (due != nullptr)
		{
			due->Sort([](const cs::Ref<Passage>& a, const cs::Ref<Passage>& b) { return CompareTo(a->ExitJd, b->ExitJd); });
			for (const auto& p : *due) Emerge(p);
		}

		// The news: relays sent and reports arrived, before anyone plans on it.
		News->Update(jd);

		while (jd >= nextDailyJd)
		{
			Daily(nextDailyJd);
			nextDailyJd += 1;
		}
	}

	// ---------------------------------------------------------------- at the gate

	void InterstellarTraffic::OnReachedGate(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd)
	{
		auto v = ship->Voyage;
		if (v == nullptr)
		{
			// Bound for a gate with nowhere to go: back to work.
			ship->GateKm = std::nullopt;
			return;
		}
		v->AtGate = true;
		v->AtGateJd = jd;
		atGates.Add(AtGateEntry{ system, ship });
	}

	void InterstellarTraffic::TryEnter(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double when, double now)
	{
		auto v = ship->Voyage;
		auto isShip = [&](const AtGateEntry& e) { return e.ship == ship; };
		// Fighting at the gate: it goes through (or not) once the fight is over.
		if (!ship->Destroyed && ship->Battle != nullptr) return;
		// A hulk cast adrift there is going nowhere.
		if (ship->Destroyed || v == nullptr || ship == system->World->PilotedShip || ship->Faction != v->Faction || ship->Derelict)
		{
			atGates.RemoveAll(isShip);
			if (!ship->Destroyed) CancelVoyage(system, ship, now, v != nullptr && ship->Faction == v->Faction);
			return;
		}
		// At the edge of the well, bound across open space under its own drive (U.SPC-T412): no gate to open.
		if (v->ByCruise)
		{
			atGates.RemoveAll(isShip);
			EngageCruise(system, ship, when);
			return;
		}
		auto gate = v->NextGate();
		bool here = gate != nullptr && gate->Home == system && gate->InRange(ship->Orbit->OffsetAt(when));
		bool campaign = v->Purpose == VoyagePurpose::Campaign && v->Campaign != nullptr;
		if (campaign && gate != nullptr && gate->Home == system && here)
		{
			// A war fleet at an enemy's gate takes it before it goes through (U.SPC-T92): it holds here while the
			// campaign fights its forts and holds the gate (see InterstellarWars), however long that takes.
			if (galaxy->Wars != nullptr && galaxy->Wars->MustTake(gate, v))
			{
				if (cs::Double::IsNaN(v->WaitingSinceJd)) v->WaitingSinceJd = now;
				return;
			}
			// The far end held against it is forced: it goes through, to fight there.
			// (Through a ring that works: a sabotaged or fought-down one is shut to a war fleet too, U.SPC-T383.)
			if (gate->Link->Working() && GateNetwork::Passable(gate, ship->Faction, when))
			{
				atGates.RemoveAll(isShip);
				Enter(system, ship, gate, when);
				return;
			}
		}
		if (gate == nullptr || gate->Home != system || !gate->Link->Open(system, ship->Faction, when) || !here)
		{
			auto route = Network()->Route(system, v->Destination, ship->Faction, now, campaign);
			if (route.Count() > 0 && route[0] != gate)
			{
				// Round by another gate.
				atGates.RemoveAll(isShip);
				v->Route = route;
				v->Leg = 0;
				v->AtGate = false;
				v->WaitingSinceJd = cs::Double::NaN;
				ship->GateKm = route[0]->PositionKm;
				system->World->Fleet->ScheduleNext(ship, now);
				if (gate != nullptr)
					system->World->Log(now, cs::Cat(ship->Name, " turns for the ", route[0]->Name, ": the ", gate->Name, " is shut to it"), WorldEventKind::Info, system->Catalog->Root()->Index);
				return;
			}
			if (route.Count() > 0 && !here)
			{
				// The right gate, but not at it (it was knocked off course): fly the last of the way.
				atGates.RemoveAll(isShip);
				v->AtGate = false;
				ship->GateKm = route[0]->PositionKm;
				system->World->Fleet->ScheduleNext(ship, now);
				return;
			}
			if (cs::Double::IsNaN(v->WaitingSinceJd))
			{
				v->WaitingSinceJd = now;
				if (gate != nullptr) system->World->Log(now, cs::Cat(ship->Name, " waits at the ", gate->Name, ": it is shut to ", ship->Faction->Name), WorldEventKind::Info, system->Catalog->Root()->Index);
			}
			if (now - v->WaitingSinceJd > MaxWaitDays)
			{
				atGates.RemoveAll(isShip);
				GiveUp(system, ship, now);
			}
			return;
		}

		atGates.RemoveAll(isShip);
		Enter(system, ship, gate, when);
	}

	void InterstellarTraffic::Enter(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& leader, const cs::Ref<JumpGate>& gate, double jd)
	{
		auto v = leader->Voyage;
		auto world = system->World;
		auto link = gate->Link;
		v->AtGate = false;
		v->WaitingSinceJd = cs::Double::NaN;
		v->Leg++;

		// The gate's toll on a cargo, split between its two owners: the distance-scaled markup on interstellar freight.
		if (leader->HasCargo && leader->CargoCost > 0)
		{
			double toll = leader->CargoCost * MarkupPerLy * link->Ly;
			leader->CargoCost += toll;
			leader->Faction->Treasury -= toll;
			v->Tolls += toll;
			TollsPaid += toll;
			if (gate->Owner != nullptr) gate->Owner->Treasury += toll * 0.5;
			if (gate->Partner->Owner != nullptr) gate->Partner->Owner->Treasury += toll * 0.5;
		}
		world->Economy->Release(leader);

		auto p = cs::New<Passage>();
		p->From = gate;
		p->To = gate->Partner;
		p->EnterJd = jd;
		p->ExitJd = jd + link->TransitDays();
		p->News = News->Bundle(system, jd);
		p->Ships.Add(leader);
		auto going = cs::List<cs::Ref<Ship>>();
		for (const auto& e : world->Fleet->Ships())
		{
			// An escort still fighting, or left a hulk, stays behind; so does one that never leaves its home system
			// (a corvette, U.SPC-T472). Every other hull crosses by the gate, drive or none.
			if (e->Leader != leader || e->Destroyed || e->Battle != nullptr || e->Derelict || !FtlRoster::LeavesHome(e)) continue;
			going.Add(e);
		}
		for (const auto& e : going)
		{
			p->Ships.Add(e);
			if (world->Escorts != nullptr && world->Escorts->IsHired(e))
			{
				p->Hired.Add(e);
				world->Escorts->UnmarkHired(e);
			}
			if (world->War) world->War->Forget(e);
			world->Fleet->LetGo(e);
			// The same leader it was flying with, still the one going along with it, orbit and all until it
			// too is reset at the far gate (Ship.ResetForArrival, in Emerge).
			e->Leader = leader;
		}
		DepartLeader(world, leader, v);
		inGates.Add(p);
		Crossings++;
		{
			auto crossing = cs::New<GateCrossing>();
			crossing->Link = link;
			crossing->From = system;
			crossing->Jd = jd;
			crossing->Purpose = v->Purpose;
			crossing->Faction = leader->Faction;
			crossing->Ships = p->Ships.Count();
			crossing->Tonnes = leader->HasCargo ? leader->CargoTonnes : 0;
			LogCrossing(crossing);
		}
		gate->Traffic += 1;
		cs::String group = p->Ships.Count() > 1 ? cs::Cat(" and ", p->Ships.Count() - 1, " more") : cs::String();
		world->Log(jd, cs::Cat(leader->Name, group, " enter", (p->Ships.Count() > 1 ? "" : "s"), " the ", gate->Name, " for ", gate->Destination()->Name), WorldEventKind::Info, system->Catalog->Root()->Index);
	}

	void InterstellarTraffic::DepartLeader(const cs::Ref<World>& world, const cs::Ref<Ship>& leader, const cs::Ref<Voyage>& v)
	{
		if (world->War) world->War->Forget(leader);
		leader->GateKm = std::nullopt;
		leader->Voyage = nullptr;
		// The old flagship's hold goes with the hull that is actually leaving, not a stay-behind heir.
		leader->Destroyed = true;
		if (world->War) world->War->HandOverCommand(leader, v->Purpose == VoyagePurpose::Hunt);
		leader->Destroyed = false;
		if (v->Purpose == VoyagePurpose::Hunt && world->Outlaws) world->Outlaws->SetAway(leader, true);
		world->Fleet->LetGo(leader);
		leader->Voyage = v;
	}

	void InterstellarTraffic::Emerge(const cs::Ref<Passage>& p)
	{
		auto system = p->To->Home;
		auto world = system->World;
		auto fleet = world->Fleet;
		auto leader = p->Leader();
		auto v = leader->Voyage;
		double jd = p->ExitJd;
		auto forward = p->From->ArrivalForward();
		auto up = cs::Math::Abs(forward.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX;
		auto right = DVec3::Cross(forward, up).Normalized();
		auto AtGate = [&]()
		{
			auto o = cs::New<ParkingOrbit>();
			o->Body = system->Catalog->Root()->Index;
			o->RadiusKm = 0;
			o->Center = p->From->ArrivalOffsetKm();
			o->U = right;
			o->V = forward;
			o->EpochJd = jd;
			o->PhaseAtEpoch = 0;
			o->RateRadPerDay = 0;
			o->Fixed = true;
			return o;
		};
		for (const auto& s : p->Ships)
		{
			// Still exactly as it was leaving the last system (U.SPC-T453: the same object, never reset in
			// transit) until now, arriving at this one.
			s->ResetForArrival();
			s->Orbit = AtGate();
			s->Phase = ShipPhase::Parked;
			s->PhaseStartJd = jd;
			s->PhaseEndJd = jd;
			s->DestinationBody = s->Orbit->Body;
			s->Ports = world->FreeShipPorts();
			fleet->Receive(s);
			// A fresh id in this system's fleet, so a leftover id from the system it came from never
			// happens to land on some other local group here: the arriving band keeps its own identity
			// (see Ship.BandId), rebuilt from ids this fleet just handed out, the same way Fleet.AddEscort
			// would (U.SPC-T363).
			s->BandId = s->Leader == nullptr ? s->Id : s->Leader->BandId;
			// Picks a hired fighter's tracking back up here, so it is still paid off when its merchant is gone,
			// instead of flying on untracked once the gate it crossed left the system that hired it behind (U.SPC-T301).
			if (p->Hired.Contains(s) && world->Escorts) world->Escorts->MarkHired(s);
		}

		if (v->Purpose == VoyagePurpose::Campaign && galaxy->Wars != nullptr && galaxy->Wars->OnEmerged(system, leader, p->To, jd))
		{
			// A war fleet come out beside an enemy's gate holds there to take it first (U.SPC-T92).
		}
		else if (v->NextGate() != nullptr)
		{
			// Passing through: across to the next gate.
			leader->GateKm = v->NextGate()->PositionKm;
			fleet->ScheduleNext(leader, jd);
		}
		else if (v->Homeward()) Home(system, leader, jd);
		else Arrive(system, leader, p->To, jd);
		for (const auto& s : p->Ships) world->NotifyAdded(s);
		// The gate's pickets see it come out and warn their navy if it is hostile (U.SPC-T480).
		GatePickets::OnArrival(p->To, p->Ships, jd);
		News->Carried(system, p->News, leader->Name, jd);
		p->To->Traffic += 1;
		// Raiders lying off the gate see it come out (U.SPC-T94), however quickly it moves on.
		world->War->GateAmbushes(cs::Double::IsNaN(world->Jd) ? jd : cs::Math::Max(jd, world->Jd));
	}

	// ---------------------------------------------------------------- under its own drive (U.SPC-T412)

	bool InterstellarTraffic::CanCruise(const cs::Ref<Ship>& ship) { return CruiseDrives::Of(ship) != nullptr; }

	cs::Ref<Voyage> InterstellarTraffic::SendByCruise(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, VoyagePurpose purpose, double jd)
	{
		if (to == nullptr || from == to || !Idle(from->World, ship) || !CanCruise(ship)) return nullptr;
		return SetOutByCruise(from, to, cs::String(), ship, purpose, jd);
	}

	cs::Ref<Voyage> InterstellarTraffic::SendToStar(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const NearbyStar& star, VoyagePurpose purpose, double jd)
	{
		if (star.Name.empty() || !Idle(from->World, ship) || !CanCruise(ship)) return nullptr;
		return SetOutByCruise(from, nullptr, star.Name, ship, purpose, jd);
	}

	cs::Ref<Voyage> InterstellarTraffic::SendToAmbush(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const GalacticPosition& station, const cs::String& where, double jd)
	{
		if (!Idle(from->World, ship) || !CanCruise(ship)) return nullptr;
		return SetOutByCruise(from, nullptr, cs::String(), ship, VoyagePurpose::Ambush, jd, station, where);
	}

	cs::Ref<Voyage> InterstellarTraffic::SetOutByCruise(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::String& star, const cs::Ref<Ship>& ship, VoyagePurpose purpose, double jd,
		cs::Nullable<GalacticPosition> station, const cs::String& stationName)
	{
		cs::Nullable<GalacticPosition> target = station;
		if (!target.has_value())
		{
			if (to != nullptr) target = to->Origin;
			else
			{
				auto found = FindStar(star);
				if (found.has_value()) target = found->Origin();
			}
		}
		if (!target.has_value()) return nullptr;
		ship->StakeoutKm = std::nullopt;
		auto v = cs::New<Voyage>();
		v->Purpose = purpose;
		v->Home = from;
		v->Destination = to;
		v->Route = cs::List<cs::Ref<JumpGate>>();
		v->SetOutJd = jd;
		v->Faction = ship->Faction;
		v->Ly = target->DistanceLy(from->Origin);
		v->ByCruise = true;
		v->StarName = star;
		v->Station = station;
		v->StationName = stationName;
		v->HomeRole = ship->Role;
		v->HomeRoute = ship->Route;
		v->HomePorts = ship->Ports;
		ship->Voyage = v;
		if (outCache) outCache->Add(v);
		ship->GateKm = DeepSpace::DeparturePointKm(from, *target);
		from->World->Fleet->ScheduleNext(ship, jd);
		return v;
	}

	void InterstellarTraffic::CruiseFrom(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& to, double jd)
	{
		auto v = ship->Voyage;
		v->Destination = to;
		v->ByCruise = true;
		v->StarName = cs::String();
		v->Station = std::nullopt;
		v->Route = cs::List<cs::Ref<JumpGate>>();
		v->Leg = 0;
		v->AtGate = false;
		v->WaitingSinceJd = cs::Double::NaN;
		ship->GateKm = DeepSpace::DeparturePointKm(system, to->Origin);
		system->World->Fleet->ScheduleNext(ship, jd);
	}

	cs::Nullable<NearbyStar> InterstellarTraffic::FindStar(const cs::String& name) const
	{
		if (name.empty()) return std::nullopt;
		for (const auto& n : galaxy->NearbyStars)
			if (cs::Strings::EqualsOrdinalIgnoreCase(n.Name, name)) return n;
		return std::nullopt;
	}

	void InterstellarTraffic::EngageCruise(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& leader, double jd)
	{
		auto v = leader->Voyage;
		auto world = system->World;
		auto drive = CruiseDrives::Of(leader);
		auto star = !v->StarName.empty() ? FindStar(v->StarName) : std::nullopt;
		if (drive == nullptr || (v->Destination == nullptr && !star.has_value() && !v->Station.has_value()) || v->Destination == system)
		{
			CancelVoyage(system, leader, jd, system == v->Home);
			return;
		}
		v->AtGate = false;
		v->WaitingSinceJd = cs::Double::NaN;
		world->Economy->Release(leader);
		auto at = leader->Orbit != nullptr ? leader->Orbit->OffsetAt(jd) : leader->GateKm.value_or(DVec3::Zero);

		auto ships = cs::List<cs::Ref<Ship>>{ leader };
		auto hired = cs::HashSet<cs::Ref<Ship>>();
		auto going = cs::List<cs::Ref<Ship>>();
		auto craft = cs::List<cs::Ref<Ship>>();
		for (const auto& e : world->Fleet->Ships())
		{
			// An escort still fighting or left a hulk stays behind; one with no drive of its own goes only docked aboard,
			// or clamped to a carrier's hull (U.SPC-T473).
			if (e->Leader != leader || e->Destroyed || e->Battle != nullptr || e->Derelict) continue;
			if (CanCruise(e)) going.Add(e);
			else craft.Add(e);
		}
		auto carriers = cs::List<cs::Ref<Ship>>{ leader };
		carriers.AddRange(going);
		auto aboard = FtlCarriage::Stow(carriers, craft);
		for (const auto& e : craft)
			if (aboard.ContainsKey(e)) going.Add(e);
		for (const auto& e : going)
		{
			ships.Add(e);
			if (world->Escorts != nullptr && world->Escorts->IsHired(e))
			{
				hired.Add(e);
				world->Escorts->UnmarkHired(e);
			}
			if (world->War) world->War->Forget(e);
			world->Fleet->LetGo(e);
			// The same leader it was flying with, still the one going along with it, orbit and all until it
			// too is reset dropping out of cruise (Ship.ResetForArrival, in DeepSpace.Drop).
			e->Leader = leader;
		}
		DepartLeader(world, leader, v);

		auto news = News->Bundle(system, jd);
		auto c = v->Station.has_value() && v->Destination == nullptr
			? galaxy->DeepSpace->LaunchToPoint(ships, system, at, *v->Station, !v->StationName.empty() ? v->StationName : cs::String("open space"), drive, jd, news)
			: star.has_value()
			? galaxy->DeepSpace->LaunchToStar(ships, system, at, *star, drive, jd, news)
			: galaxy->DeepSpace->Launch(ships, system, at, v->Destination, drive, jd, news, false, v->DropDir);
		for (const auto& h : hired) c->Hired.Add(h);
		for (auto [key, value] : aboard) c->Aboard[key] = value;
		cs::String group = ships.Count() > 1 ? cs::Cat(" and ", ships.Count() - 1, " more") : cs::String();
		// A jump strike group: its escorts clamped to the carrier's hull go with it (U.SPC-T473).
		int clamped = FtlCarriage::CountClamped(c->Aboard.Keys());
		cs::String onHull = clamped > 0 ? cs::Cat(", ", clamped, " escort", (clamped == 1 ? "" : "s"), " clamped to the hull") : cs::String();
		world->Log(jd, cs::Cat(leader->Name, group, " engage", (ships.Count() > 1 ? "" : "s"), " ", cs::Strings::ToLowerInvariant(drive->Name), " for ", c->ToName, ", ",
			cs::Fmt(c->Profile.Ly, "0.00"), " ly through open space", onHull), WorldEventKind::Info, system->Catalog->Root()->Index);
	}

	void InterstellarTraffic::OnCruiseArrived(const cs::Ref<Cruise>& c, double jd)
	{
		auto system = c->To;
		auto leader = c->Leader();
		auto v = leader->Voyage;
		if (v != nullptr)
		{
			v->ByCruise = false;
			v->DropDir = std::nullopt;
			if (v->Homeward()) Home(system, leader, jd);
			else if (v->Destination == system) Arrive(system, leader, nullptr, jd);
			else
			{
				// Sent somewhere else on the way: on from here, by the gates if they go there, else by its drive.
				auto route = v->Destination == nullptr ? cs::List<cs::Ref<JumpGate>>() : Network()->Route(system, v->Destination, leader->Faction, jd);
				if (route.Count() > 0)
				{
					v->Route = route;
					v->Leg = 0;
					leader->GateKm = route[0]->PositionKm;
					system->World->Fleet->ScheduleNext(leader, jd);
				}
				else if (v->Destination != nullptr && CanCruise(leader)) CruiseFrom(system, leader, v->Destination, jd);
				else Arrive(system, leader, nullptr, jd);
			}
		}
		News->Carried(system, c->News, leader->Name, jd);
	}

	void InterstellarTraffic::OnReachedStar(const cs::Ref<Cruise>& c, double jd)
	{
		auto leader = c->Leader();
		auto v = leader->Voyage;
		if (v == nullptr || v->Home == nullptr) return;
		{
			int n = 0;
			int next = (Arrivals.TryGetValue(v->Purpose, n) ? n : 0) + 1;
			Arrivals[v->Purpose] = next;
		}
		if (v->Purpose == VoyagePurpose::Ambush && v->Station.has_value())
		{
			// At its station on the route: it lies in wait there a while, then goes home (U.SPC-T421).
			double wait = AmbushDays + ambushRng.NextDouble() * AmbushDays;
			cs::String where = !v->StationName.empty() ? v->StationName : cs::String("in open space");
			v->Station = std::nullopt;
			v->Destination = v->Home;
			v->StayUntilJd = jd + wait;
			galaxy->DeepSpace->Retarget(c, v->Home, jd + wait);
			galaxy->DeepSpace->LieInWait(c, jd, wait, where);
			return;
		}
		double stay = 8 + cruiseRng.NextDouble() * 12;
		v->Home->World->Log(jd, cs::Cat(leader->Name, " of ", leader->Faction->Name, " reports from ", c->ToName, ", ", cs::Fmt(v->Ly, "0.0"), " ly out under its own drive"),
			WorldEventKind::Info, v->Home->Catalog->Root()->Index);
		// The first survey of a star off the gate network pays for its charts (U.SPC-T421).
		if (v->Purpose == VoyagePurpose::Survey && galaxy->DeepSpace->SurveyStar(c->ToName))
		{
			double pay = StarSurveyCredits * (1 + v->Ly / 10);
			leader->Credits += pay;
			leader->Faction->Treasury += pay;
			galaxy->DeepSpace->Log(jd, cs::Cat("Survey find: ", leader->Name, " of ", leader->Faction->Name, " is first to chart ", c->ToName, ", ",
				"for ", cs::Fmt(pay, "N0"), " cr"), c->Target);
		}
		v->StarName = cs::String();
		v->Destination = v->Home;
		v->StayUntilJd = jd + stay;
		galaxy->DeepSpace->Retarget(c, v->Home, jd + stay);
	}

	void InterstellarTraffic::DispatchAmbush(const cs::Ref<StarSystem>& from, const cs::List<cs::Ref<StarSystem>>& settled, double jd)
	{
		auto world = from->World;
		if (Out(from, VoyagePurpose::Ambush) > 0 || ambushRng.NextDouble() > AmbushChancePerDay) return;
		auto pirates = world->Pirates;
		auto navy = world->Sol != nullptr && world->Sol->IsMilitary && galaxy->Polities->AtWar(world->Sol) ? world->Sol : nullptr;
		for (const auto& side : cs::List<cs::Ref<Faction>>{ pirates, navy })
		{
			if (side == nullptr) continue;
			cs::Ref<StarSystem> other = nullptr;
			for (const auto& s : settled)
			{
				if (s == from) continue;
				// A navy pickets the approach from an enemy's system; a clan works any run between settled systems.
				if (side == navy && (s->World->Sol == nullptr || galaxy->Polities->Diplomacy->Get(side, s->World->Sol) != Relation::War)) continue;
				if (s->Origin.DistanceLy(from->Origin) > DeepSpaceFeatures::MaxRouteLy) continue;
				if (other == nullptr || ambushRng.NextDouble() < 0.5) other = s;
			}
			if (other == nullptr) continue;
			for (const auto& ship : world->Fleet->Ships())
			{
				if (!Idle(world, ship) || ship->Faction != side || !CanCruise(ship) || ship->Phase != ShipPhase::Parked) continue;
				if (ship->Combat == nullptr || ship->Combat->HullDamage() > 0.2 || !ship->Combat->Armed() || (side == navy && (ship->PatrolWing || world->War->IsHomeGuard(ship)))) continue;
				double t = side == navy ? 0.15 + 0.15 * ambushRng.NextDouble() : 0.3 + 0.4 * ambushRng.NextDouble();
				auto station = GalacticPosition::Lerp(from->Origin, other->Origin, t);
				cs::String where = side == navy ? cs::Cat("on the approach from ", other->Name) : cs::Cat("on the ", from->Name, "\xE2\x80\x93", other->Name, " run");
				auto v = SendToAmbush(ship, from, station, where, jd);
				if (v == nullptr) continue;
				world->Log(jd, side == navy
						? cs::Cat(ship->Name, " goes out under its own drive to picket the FTL approach from ", other->Name)
						: cs::Cat("The ", side->Name, " raiders under ", ship->Name, " go out under their own drive to lie in wait ", where),
					WorldEventKind::Info, PortOf(ship));
				return;
			}
		}
	}

	void InterstellarTraffic::Home(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd)
	{
		auto v = ship->Voyage;
		ship->Voyage = nullptr;
		ship->GateKm = std::nullopt;
		v->ByCruise = false;
		ship->Role = v->HomeRole;
		ship->Route = v->HomeRoute;
		ship->RouteIndex = 0;
		// `v.HomePorts ?? ship.Ports` (a null array is an empty List here).
		ship->Ports = v->HomePorts.Count() > 0 ? v->HomePorts : ship->Ports;
		// A bounty hunter takes up its old place on the home boards (U.SPC-T94). The hull that set out is the one
		// coming home now (U.SPC-T453: the same object the whole trip) unless it was lost abroad and an escort
		// took the hunt on as heir, so this only rebinds the hunter's row when that actually happened.
		if (v->Purpose == VoyagePurpose::Hunt && v->Origin != nullptr)
		{
			if (v->Origin != ship) { if (system->World->Outlaws) system->World->Outlaws->OnSuccession(v->Origin, ship); }
			else if (system->World->Outlaws) system->World->Outlaws->SetAway(ship, false);
		}
		// A smuggler back with goods it could not sell puts them back in the haven's stores.
		if (ship->Runner && ship->Plunder.Count() > 0)
			if (auto haven = system->World->War->LiveHavenOf(ship->Faction))
			{
				for (int i = 0; i < GoodInfo::Count; i++) haven->Stock[i] += ship->Plunder[i];
				ship->Plunder = cs::List<double>();
			}
		system->World->Fleet->ScheduleNext(ship, jd);
		system->World->Log(jd, cs::Cat(ship->Name, " is home from ", Describe(v)), WorldEventKind::Info, system->Catalog->Root()->Index);
	}

	cs::String InterstellarTraffic::Describe(const cs::Ref<Voyage>& v)
	{
		switch (v->Purpose)
		{
			case VoyagePurpose::Raid: return "a raid beyond the gates";
			case VoyagePurpose::Expedition: return "an expedition beyond the gates";
			case VoyagePurpose::Campaign: return "a campaign beyond the gates";
			case VoyagePurpose::Survey: return "a survey beyond the gates";
			case VoyagePurpose::Courier: return "a mail run beyond the gates";
			case VoyagePurpose::Smuggle: return "a smuggling run beyond the gates";
			case VoyagePurpose::Hunt: return "a hunt beyond the gates";
			case VoyagePurpose::Relocate: return "a move beyond the gates";
			case VoyagePurpose::Ambush: return "lying in wait between the stars";
			case VoyagePurpose::Directed: return "its orders beyond the gates";
			case VoyagePurpose::Plotted: return "a plotted course beyond the gates";
			case VoyagePurpose::Colonise: return "a colony voyage beyond the gates";
			case VoyagePurpose::Supply: return "a supply run to the war fleet";
			default: return "a run beyond the gates";
		}
	}

	void InterstellarTraffic::Arrive(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, const cs::Ref<JumpGate>& cameIn, double jd)
	{
		auto v = ship->Voyage;
		auto world = system->World;
		ship->GateKm = std::nullopt;
		{
			int n = 0;
			int next = (Arrivals.TryGetValue(v->Purpose, n) ? n : 0) + 1;
			Arrivals[v->Purpose] = next;
		}
		switch (v->Purpose)
		{
			case VoyagePurpose::Trade:
				ship->Ports = world->PortBodies();
				v->StayUntilJd = jd + 25 + rng.NextDouble() * 20;
				break;
			case VoyagePurpose::Courier:
			{
				double fee = MailFeePerLy * v->Ly;
				ship->Faction->Treasury += fee;
				ship->Credits += fee;
				if (world->Sol != nullptr) world->Sol->Treasury -= fee;
				ship->Cargo = cs::String();
				ship->Ports = world->PortBodies();
				v->StayUntilJd = jd + 5 + rng.NextDouble() * 7;
				break;
			}
			case VoyagePurpose::Survey:
				ship->Ports = world->FreeShipPorts();
				v->StayUntilJd = jd + 15 + rng.NextDouble() * 15;
				break;
			case VoyagePurpose::Raid:
				ship->Route = cs::List<int>();
				ship->Ports = cs::List<int>();
				v->StayUntilJd = jd + 20 + rng.NextDouble() * 15;
				// The gate's owner shuts it behind them, and sends a navy after them to where they came from. (Raiders
				// who came in through open space under their own drive came by no one's gate.)
				if (cameIn != nullptr && cameIn->Owner != nullptr && cameIn->Owner->IsMilitary)
				{
					Blockade(cameIn, jd, BlockadeDays, cs::Cat(ship->Faction->Name, " raiders come through"));
					expeditionsWanted.Add(ExpeditionWanted{ system, v->Home, ship->Faction, jd });
				}
				break;
			case VoyagePurpose::Expedition:
			case VoyagePurpose::Hunt:
				ship->Route = cs::List<int>();
				ship->Ports = cs::List<int>();
				v->StayUntilJd = jd + 20 + rng.NextDouble() * 10;
				break;
			case VoyagePurpose::Directed:
				// Here until its objective is done or called off (see StrategicCommand); the war here gives its orders.
				ship->Route = cs::List<int>();
				ship->Ports = cs::List<int>();
				v->StayUntilJd = cs::Double::PositiveInfinity;
				break;
			case VoyagePurpose::Plotted:
				// The course is run: the voyage ends and the hull is its own again (see the end of this method).
				ship->Route = cs::List<int>();
				ship->Ports = cs::List<int>();
				break;
			case VoyagePurpose::Campaign:
				// Here until the campaign is won, lost or called off (see InterstellarWars), not for a set stay.
				ship->Route = cs::List<int>();
				ship->Ports = cs::List<int>();
				v->StayUntilJd = cs::Double::PositiveInfinity;
				break;
			case VoyagePurpose::Smuggle:
				// Until it has sold (see Orders), and no longer than this.
				ship->Ports = world->FreeShipPorts();
				v->StayUntilJd = jd + 30;
				break;
			case VoyagePurpose::Colonise:
				// In to the site to land its colonists (U.SPC-T524, see World.ColonyHullArrived); here until it has.
				ship->Route = cs::List<int>();
				ship->Ports = v->TargetBody >= 0 ? cs::List<int>{ v->TargetBody } : world->FreeShipPorts();
				v->StayUntilJd = cs::Double::PositiveInfinity;
				break;
			case VoyagePurpose::Relocate:
				Clans->Settle(system, ship, jd);
				return;
			case VoyagePurpose::Supply:
				// Here to come up with its war fleet (U.SPC-T525): its navy's logistics says where it goes next and when
				// it turns for home. Turned back on the way (its fleet's campaign over), it goes home at once.
				ship->Ports = world->FreeShipPorts();
				v->StayUntilJd = v->Convoy != nullptr ? cs::Double::PositiveInfinity : jd;
				break;
		}
		ship->Role = v->Purpose == VoyagePurpose::Survey ? ShipRole::Free : v->HomeRole;
		if (ship->Role == ShipRole::Free || v->Purpose == VoyagePurpose::Raid || v->Purpose == VoyagePurpose::Expedition || v->Purpose == VoyagePurpose::Campaign || v->Purpose == VoyagePurpose::Hunt ||
			v->Purpose == VoyagePurpose::Directed || v->Purpose == VoyagePurpose::Plotted)
			// `ship.Ports ??= world.FreeShipPorts()` (a null array is an empty List here).
			if (ship->Ports.Count() == 0) ship->Ports = world->FreeShipPorts();
		world->Log(jd, cameIn != nullptr
				? cs::Cat(ship->Name, " of ", ship->Faction->Name, " comes through the ", cameIn->Partner->Name, " from ", v->Home->Name)
				: cs::Cat(ship->Name, " of ", ship->Faction->Name, " comes in from ", v->Home->Name, " under its own drive"), WorldEventKind::Info, system->Catalog->Root()->Index);
		if (v->Purpose == VoyagePurpose::Plotted) ship->Voyage = nullptr;
		world->Fleet->ScheduleNext(ship, jd);
	}

	void InterstellarTraffic::Blockade(const cs::Ref<JumpGate>& gate, double jd, double days, const cs::String& why)
	{
		bool already = gate->Blockaded(jd);
		gate->BlockadeUntilJd = cs::Math::Max(gate->BlockadeUntilJd, jd + days);
		if (!already && gate->Owner != nullptr)
			gate->Home->World->Log(jd, cs::Cat(gate->Owner->Name, " closes the ", gate->Name, (!why.empty() ? cs::Cat(" after ", why) : cs::String())), WorldEventKind::Diplomacy, gate->Home->Catalog->Root()->Index);
	}

	void InterstellarTraffic::Lift(const cs::Ref<JumpGate>& gate, double jd)
	{
		if (!gate->Blockaded(jd)) return;
		gate->BlockadeUntilJd = jd;
		if (gate->Owner != nullptr) gate->Home->World->Log(jd, cs::Cat(gate->Owner->Name, " opens the ", gate->Name, " again"), WorldEventKind::Diplomacy, gate->Home->Catalog->Root()->Index);
	}

	void InterstellarTraffic::GiveUp(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd)
	{
		auto v = ship->Voyage;
		v->AtGate = false;
		v->WaitingSinceJd = cs::Double::NaN;
		ship->GateKm = std::nullopt;
		// The gates are shut to it, but it has a drive of its own (U.SPC-T412): it goes the long way, through open space.
		if (v->Campaign == nullptr && v->Destination != nullptr && v->Destination != system && CanCruise(ship))
		{
			CruiseFrom(system, ship, v->Destination, jd);
			system->World->Log(jd, cs::Cat(ship->Name, " gives up on the gates and sets out for ", v->Destination->Name, " under its own drive"), WorldEventKind::Info, system->Catalog->Root()->Index);
			return;
		}
		// A war fleet's voyage is its campaign's: it is not called off here but ended by the war (U.SPC-T296).
		if (system == v->Home && v->Campaign == nullptr)
		{
			CancelVoyage(system, ship, jd);
			return;
		}
		// Stranded abroad (or between): it stays a while and tries again for home.
		v->Destination = system;
		v->Route = cs::List<cs::Ref<JumpGate>>();
		v->Leg = 0;
		v->StayUntilJd = jd + 3;
		ship->Ports = ship->Role == ShipRole::Free || ship->Spec->IsMilitary || ship->Faction->IsPirate ? system->World->FreeShipPorts() : system->World->PortBodies();
		system->World->Fleet->ScheduleNext(ship, jd);
	}

	void InterstellarTraffic::CancelVoyage(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd, bool restore)
	{
		auto v = ship->Voyage;
		ship->GateKm = std::nullopt;
		if (v == nullptr) return;
		v->AtGate = false;
		ship->Voyage = nullptr;
		if (restore && system == v->Home)
		{
			ship->Role = v->HomeRole;
			ship->Route = v->HomeRoute;
			ship->Ports = v->HomePorts.Count() > 0 ? v->HomePorts : ship->Ports;
		}
		else if (restore || ship->Ports.Count() == 0) ship->Ports = system->World->FreeShipPorts();
		if (ship != system->World->PilotedShip && !ship->Destroyed && !ship->Derelict) system->World->Fleet->ScheduleNext(ship, jd);
	}

	// ---------------------------------------------------------------- orders abroad

	int InterstellarTraffic::Orders(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, int here, double jd)
	{
		auto v = ship->Voyage;
		if (v == nullptr || ship->GateKm.has_value()) return -1;
		if (v->Purpose == VoyagePurpose::Campaign) return galaxy->Wars ? galaxy->Wars->Orders(system, ship, here, jd) : -1;
		// A navy's supply ships make for their war fleet wherever it is in this system, home or abroad (U.SPC-T525).
		if (v->Purpose == VoyagePurpose::Supply && v->Convoy != nullptr) return galaxy->Wars ? galaxy->Wars->Logistics->ConvoyOrders(system, ship, here, jd) : -1;
		if (system == v->Home) return -1;
		auto economy = system->World->Economy;
		auto war = system->World->War;
		if (v->Purpose == VoyagePurpose::Raid)
		{
			// Lying in wait off a gate till the watch is up; or, now and then, off the busiest one (U.SPC-T94).
			int root = system->Catalog->Root()->Index;
			auto watch = ship->StakeoutKm;
			ship->StakeoutKm = std::nullopt;
			if (watch.has_value() && jd < ship->StakeoutUntilJd)
			{
				DVec3 spot = *watch;
				ship->StakeoutKm = spot;
				return root;
			}
			if (here != root && rng.NextDouble() < War::GateWatchChance)
				if (auto gate = war->BusiestGate(ship->Faction, jd))
				{
					double travel = system->World->Fleet->EstimateTravelDays(ship, here, root, jd);
					double r = rng.NextDouble();
					war->StakeOut(ship, gate, jd + travel + 2 + r * 2);
					return root;
				}
			// The busiest port with guns it can face; it hangs about a port that is paying off.
			int best = -1;
			double bestScore = 0;
			for (const auto& s : economy->Settlements)
			{
				if (s->Kind == SettlementKind::Haven || !s->Discovered()) continue;
				double traffic = 1;
				for (const auto& t : system->World->Fleet->Ships())
					if (!t->Destroyed && system->World->IsTrader(t) && (t->DestinationBody == s->Body || (t->Orbit != nullptr && t->Orbit->Body == s->Body))) traffic++;
				double defence = war->DefenceAt(s->Body, ship->Faction);
				double score = traffic / (defence + 100) * (0.7 + rng.NextDouble() * 0.6);
				if (score > bestScore)
				{
					bestScore = score;
					best = s->Body;
				}
			}
			return best >= 0 ? best : here;
		}
		if (v->Purpose == VoyagePurpose::Smuggle) return Clans->SmugglerOrders(system, ship, here, jd);
		if (v->Purpose == VoyagePurpose::Expedition || v->Purpose == VoyagePurpose::Hunt)
		{
			// The quarry's haven, if it is known (to it); else wherever its raiders are; else the busiest port.
			// Sent after one clan, it goes after that clan only, not any other pirate's hold or band (U.SPC-T309).
			for (const auto& s : economy->Settlements)
				if (s->Kind == SettlementKind::Haven && !s->Fallen && s->KnownBy(ship->Faction) && (v->Quarry == nullptr || s->Owner == v->Quarry))
				{
					// A hidden hold found, its guns silenced: a navy storms it, boarding it if it has the marines (U.SPC-T94).
					// (An open one, like Blackwater, the navies leave be.)
					if (s->Body == here && s->Hidden && v->Purpose == VoyagePurpose::Expedition && war->CanAssault(s, ship))
					{
						war->AssaultHaven(s, ship, war->HasMarines(ship), jd);
						continue;
					}
					return s->Body;
				}
			for (const auto& t : system->World->Fleet->Ships())
				if (!t->Destroyed && !t->IsEscort() && t->Faction->IsPirate && !t->Runner && (v->Quarry == nullptr || t->Faction == v->Quarry) && t->Orbit != nullptr && t->Phase == ShipPhase::Parked)
					return t->Orbit->Body;
			auto ports = system->World->PortBodies();
			return ports.Count() > 0 ? ports[rng.Next(ports.Count())] : here;
		}
		return -1;
	}

	// ---------------------------------------------------------------- sending ships out

	void InterstellarTraffic::Daily(double jd)
	{
		int stale = 0;
		while (stale < crossingLog.Count() && crossingLog[stale]->Jd < jd - CrossingLogDays) stale++;
		if (stale > 0) crossingLog.RemoveRange(0, stale);

		auto settled = cs::List<cs::Ref<StarSystem>>();
		for (const auto& s : galaxy->Systems)
			if (s->Settled) settled.Add(s);

		for (const auto& s : galaxy->Systems) SendHomeWhenDone(s, jd);
		// Every voyage under way, gathered once: the dispatchers below count them for each of many routes.
		outCache = cs::New<cs::List<cs::Ref<Voyage>>>(VoyagesInTransit());
		for (const auto& s : galaxy->Systems)
			for (const auto& ship : s->World->Fleet->Ships())
				if (!ship->Destroyed && ship->Voyage != nullptr) outCache->Add(ship->Voyage);
		for (const auto& from : settled)
			for (const auto& to : settled)
			{
				if (from == to) continue;
				DispatchTrade(from, to, jd);
				DispatchCourier(from, to, jd);
			}
		for (const auto& from : settled)
		{
			DispatchRaid(from, settled, jd);
			DispatchSurvey(from, jd);
			DispatchAmbush(from, settled, jd);
		}
		for (int i = expeditionsWanted.Count() - 1; i >= 0; i--)
		{
			auto [from, to, quarry, asked] = expeditionsWanted[i];
			if (DispatchExpedition(from, to, quarry, jd) || jd - asked > 20) expeditionsWanted.RemoveAt(i);
		}
		// The clans: their smugglers, their moves, their splits; and the navies and hunters sent after them.
		Clans->Daily(jd);
		outCache = nullptr;
		double fade = cs::Math::Exp(-1 / GateTrafficDays);
		for (const auto& link : Network()->Links())
		{
			link->AtA->Traffic *= fade;
			link->AtB->Traffic *= fade;
		}
	}

	bool InterstellarTraffic::Idle(const cs::Ref<World>& world, const cs::Ref<Ship>& s)
	{
		return !s->Destroyed && !s->IsEscort() && !s->Derelict && s->Battle == nullptr && s != world->PilotedShip && s->Voyage == nullptr && !s->GateKm.has_value() &&
			(s->Phase == ShipPhase::Parked || s->Phase == ShipPhase::Landed) && s->Orbit != nullptr
			// Nor one of the player's own fleet: it sails where its captain's standing orders take it.
			&& (world->Holdings == nullptr || !world->Holdings->Owns(s))
			// Nor one dry in port waiting on a tanker: it would go back for its old trip first.
			&& !s->OutOfPropellant
			// Nor a raider band lying in wait off a gate: it is on its own job until the watch is up.
			&& !s->StakeoutKm.has_value()
			// Nor a hull that never leaves its home system (a corvette, U.SPC-T472).
			&& FtlRoster::LeavesHome(s);
	}

	int InterstellarTraffic::PortOf(const cs::Ref<Ship>& s) { return s->Phase == ShipPhase::Landed && s->Ring != nullptr ? s->Ring->Body : s->Orbit->Body; }

	int InterstellarTraffic::Out(const cs::Ref<StarSystem>& home, VoyagePurpose purpose, const cs::Ref<StarSystem>& bound) const
	{
		int n = 0;
		auto Counts = [&](const cs::Ref<Voyage>& v)
		{
			return v != nullptr && v->Home == home && v->Purpose == purpose && (bound == nullptr || v->Destination == bound || (v->Homeward() && v->Route.Count() > 0 && v->Route[0]->Home == bound) || v->Destination == bound);
		};
		if (outCache != nullptr)
		{
			for (const auto& v : *outCache)
				if (Counts(v)) n++;
			return n;
		}
		for (const auto& v : VoyagesInTransit())
			if (Counts(v)) n++;
		for (const auto& s : galaxy->Systems)
			for (const auto& ship : s->World->Fleet->Ships())
				if (!ship->Destroyed && Counts(ship->Voyage)) n++;
		return n;
	}

	cs::Ref<Voyage> InterstellarTraffic::Send(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, VoyagePurpose purpose, double jd)
	{
		if (from == to || !Idle(from->World, ship)) return nullptr;
		auto route = Network()->Route(from, to, ship->Faction, jd);
		// No way through the gates: a hull with a drive of its own goes through open space instead (U.SPC-T412).
		if (route.Count() == 0) return CanCruise(ship) ? SetOutByCruise(from, to, cs::String(), ship, purpose, jd) : nullptr;
		return SetOut(from, to, ship, purpose, route, jd);
	}

	cs::Ref<Voyage> InterstellarTraffic::SendCampaign(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<::SpaceGod::Core::Campaign>& campaign, double jd)
	{
		if (from == to || !Idle(from->World, ship)) return nullptr;
		auto route = Network()->Route(from, to, ship->Faction, jd, true);
		if (route.Count() == 0) return nullptr;
		auto v = SetOut(from, to, ship, VoyagePurpose::Campaign, route, jd);
		v->Campaign = campaign;
		v->StayUntilJd = cs::Double::PositiveInfinity;
		return v;
	}

	cs::Ref<Voyage> InterstellarTraffic::SendPlotted(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, double jd)
	{
		if (ship == nullptr || from == nullptr || to == nullptr || from == to) return nullptr;
		auto world = from->World;
		// Its own master, not fighting, with nothing else on and standing still (a hull just let go of at the helm is):
		// the player's own hull is not "idle in port" the way a fleet's trader is, so the rest of the usual test is waived.
		if (ship->Destroyed || ship->IsEscort() || ship->Derelict || ship->Battle != nullptr || ship == world->PilotedShip || ship->Voyage != nullptr ||
			ship->GateKm.has_value() || ship->Orbit == nullptr || (ship->Phase != ShipPhase::Parked && ship->Phase != ShipPhase::Landed) ||
			ship->Id < 0 || ship->Id >= world->Fleet->Ships().Count() || world->Fleet->Ships()[ship->Id] != ship) return nullptr;
		auto route = Network()->Route(from, to, ship->Faction, jd);
		if (route.Count() == 0) return nullptr;
		return SetOut(from, to, ship, VoyagePurpose::Plotted, route, jd);
	}

	cs::Ref<Voyage> InterstellarTraffic::SendSupply(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::List<cs::Ref<JumpGate>>& route, const cs::Ref<SupplyConvoy>& convoy, double jd)
	{
		if (ship == nullptr || from == nullptr || to == nullptr || ship->Voyage != nullptr) return nullptr;
		cs::Ref<Voyage> v;
		if (from == to || route.Count() == 0)
		{
			if (from != to) return nullptr;
			// No gate to take: a voyage that never leaves home, so the hull is flown to the fleet by its orders here.
			ship->StakeoutKm = std::nullopt;
			v = cs::New<Voyage>();
			v->Purpose = VoyagePurpose::Supply;
			v->Home = from;
			v->Destination = from;
			v->SetOutJd = jd;
			v->Faction = ship->Faction;
			v->HomeRole = ship->Role;
			v->HomeRoute = ship->Route;
			v->HomePorts = ship->Ports;
			v->StayUntilJd = cs::Double::PositiveInfinity;
			ship->Voyage = v;
			if (outCache) outCache->Add(v);
			ship->GateKm = std::nullopt;
			from->World->Fleet->ScheduleNext(ship, jd);
		}
		else v = SetOut(from, to, ship, VoyagePurpose::Supply, route, jd);
		v->Convoy = convoy;
		return v;
	}

	void InterstellarTraffic::Hold(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd)
	{
		auto v = ship->Voyage;
		if (v == nullptr) return;
		atGates.RemoveAll([&](const AtGateEntry& e) { return e.ship == ship; });
		v->AtGate = false;
		v->HoldGate = nullptr;
		v->WaitingSinceJd = cs::Double::NaN;
		v->Destination = system;
		v->Route = cs::List<cs::Ref<JumpGate>>();
		v->Leg = 0;
		v->ByCruise = false;
		ship->GateKm = std::nullopt;
		if (ship->Battle == nullptr) system->World->Fleet->ScheduleNext(ship, jd);
	}

	cs::Ref<Voyage> InterstellarTraffic::SendStrike(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<::SpaceGod::Core::Campaign>& campaign, double jd)
	{
		if (to == nullptr || from == to || !Idle(from->World, ship) || !CanCruise(ship)) return nullptr;
		auto v = SetOutByCruise(from, to, cs::String(), ship, VoyagePurpose::Campaign, jd);
		if (v == nullptr) return nullptr;
		v->Campaign = campaign;
		v->StayUntilJd = cs::Double::PositiveInfinity;
		v->DropDir = FleetDoctrine::DropDirection(to, from->Origin, ship->Faction);
		return v;
	}

	cs::Ref<Voyage> InterstellarTraffic::SendDirected(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, double jd)
	{
		if (from == to || !Idle(from->World, ship)) return nullptr;
		cs::Ref<Voyage> v;
		if (CanCruise(ship) && FleetDoctrine::IsStrikeGroup(from->World, ship))
		{
			// A jump strike group jumps in on the flank, gates or none (U.SPC-T474).
			v = SetOutByCruise(from, to, cs::String(), ship, VoyagePurpose::Directed, jd);
			if (v != nullptr) v->DropDir = FleetDoctrine::DropDirection(to, from->Origin, ship->Faction);
		}
		else
		{
			auto route = Network()->Route(from, to, ship->Faction, jd);
			if (route.Count() == 0) v = CanCruise(ship) ? SetOutByCruise(from, to, cs::String(), ship, VoyagePurpose::Directed, jd) : nullptr;
			else v = SetOut(from, to, ship, VoyagePurpose::Directed, route, jd);
		}
		if (v != nullptr) v->StayUntilJd = cs::Double::PositiveInfinity;
		return v;
	}

	bool InterstellarTraffic::Redirect(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& to, double jd, bool force)
	{
		auto v = ship->Voyage;
		if (v == nullptr) return false;
		auto isShip = [&](const AtGateEntry& e) { return e.ship == ship; };
		// A jump strike group goes on, or home, under its own drives (U.SPC-T474): no gates to fight through.
		if (system != to && v->Campaign != nullptr && v->Campaign->Force == ForceKind::JumpStrikeGroup && CanCruise(ship))
		{
			atGates.RemoveAll(isShip);
			v->AtGate = false;
			v->HoldGate = nullptr;
			v->WaitingSinceJd = cs::Double::NaN;
			v->Destination = to;
			v->ByCruise = true;
			v->StarName = cs::String();
			v->Station = std::nullopt;
			v->Route = cs::List<cs::Ref<JumpGate>>();
			v->Leg = 0;
			v->DropDir = to == v->Home ? cs::Nullable<DVec3>() : cs::Nullable<DVec3>(FleetDoctrine::DropDirection(to, system->Origin, ship->Faction));
			ship->GateKm = DeepSpace::DeparturePointKm(system, to->Origin);
			if (ship->Battle == nullptr) system->World->Fleet->ScheduleNext(ship, jd);
			return true;
		}
		cs::List<cs::Ref<JumpGate>> route;
		if (system != to)
		{
			route = Network()->Route(system, to, ship->Faction, jd, force);
			// (GateNetwork.Route's null, no way through, is an empty list here: system != to, so a real route is never empty.)
			if (route.Count() == 0 || (!force && route.Count() == 0)) return false;
		}
		atGates.RemoveAll(isShip);
		v->AtGate = false;
		v->HoldGate = nullptr;
		v->WaitingSinceJd = cs::Double::NaN;
		if (system == to && to == v->Home)
		{
			Home(system, ship, jd);
			return true;
		}
		v->Destination = to;
		v->Route = route;
		v->Leg = 0;
		v->ByCruise = false;
		v->StarName = cs::String();
		v->Station = std::nullopt;
		ship->GateKm = v->Route.Count() > 0 ? cs::Nullable<DVec3>(v->Route[0]->PositionKm) : cs::Nullable<DVec3>();
		if (ship->Battle == nullptr) system->World->Fleet->ScheduleNext(ship, jd);
		return true;
	}

	void InterstellarTraffic::Release(const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& ship, double jd)
	{
		auto v = ship->Voyage;
		if (v == nullptr) return;
		auto held = v->HoldGate;
		v->HoldGate = nullptr;
		auto next = v->NextGate();
		ship->GateKm = next ? cs::Nullable<DVec3>(next->PositionKm) : cs::Nullable<DVec3>();
		if (!ship->GateKm.has_value() && system == v->Destination && held != nullptr && ship->Battle == nullptr)
		{
			// The way's end: it has arrived (or is home), as a fleet that met no gate in its way would have.
			if (v->Homeward()) Home(system, ship, jd);
			else Arrive(system, ship, held, jd);
			return;
		}
		if (ship->Battle == nullptr) system->World->Fleet->ScheduleNext(ship, jd);
	}

	cs::Ref<Voyage> InterstellarTraffic::SetOut(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<Ship>& ship, VoyagePurpose purpose, const cs::List<cs::Ref<JumpGate>>& route, double jd)
	{
		ship->StakeoutKm = std::nullopt;
		auto v = cs::New<Voyage>();
		v->Purpose = purpose;
		v->Home = from;
		v->Destination = to;
		v->Route = route;
		v->SetOutJd = jd;
		v->Ly = GateNetwork::RouteLy(route);
		v->Faction = ship->Faction;
		v->HomeRole = ship->Role;
		v->HomeRoute = ship->Route;
		v->HomePorts = ship->Ports;
		ship->Voyage = v;
		if (outCache) outCache->Add(v);
		ship->GateKm = route[0]->PositionKm;
		from->World->Fleet->ScheduleNext(ship, jd);
		return v;
	}

	void InterstellarTraffic::DispatchTrade(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, double jd)
	{
		if (Out(from, VoyagePurpose::Trade, to) >= MaxTradersPerRoute()) return;
		if (Out(from, VoyagePurpose::Trade) >= cs::Math::Max(1, cs::ToInt(Freighters(from) * MaxAbroadShare))) return;
		// Traders here know the far markets only by the last news from them (U.SPC-T100): none, no run.
		auto news = News->Known(from, to);
		if (news == nullptr) return;
		auto world = from->World;
		cs::Ref<Ship> bestShip = nullptr;
		cs::Ref<Settlement> bestFrom = nullptr;
		Good bestGood{};
		double bestTonnes = 0, bestProfit = 0;
		cs::List<cs::Ref<JumpGate>> bestRoute;
		cs::Ref<Settlement> bestTo = nullptr;
		double bestSale = 0;
		for (const auto& ship : world->Fleet->Ships())
		{
			if (!Idle(world, ship) || ship->Role != ShipRole::Trader || ship->Spec->Class != ShipClass::Freighter || ship->HasCargo) continue;
			auto route = Network()->Route(from, to, ship->Faction, jd);
			if (route.Count() == 0) continue;
			cs::Ref<Settlement> src, dst;
			Good good{};
			double tonnes = 0, profit = 0, sale = 0;
			if (BestCargo(from, to, ship, PortOf(ship), route, jd, news->Markets, src, good, tonnes, profit, dst, sale)
				&& profit > bestProfit)
			{
				bestTo = dst;
				bestSale = sale;
				bestProfit = profit;
				bestShip = ship;
				bestFrom = src;
				bestGood = good;
				bestTonnes = tonnes;
				bestRoute = route;
			}
		}
		if (bestShip == nullptr) return;
		world->Economy->LoadUnbooked(bestShip, bestFrom, bestGood, bestTonnes, to->Name);
		auto voyage = SetOut(from, to, bestShip, VoyagePurpose::Trade, bestRoute, jd);
		if (voyage != nullptr)
		{
			voyage->PlannedMarket = bestTo->Name;
			voyage->ExpectedSale = bestSale;
			voyage->PlannedOnNewsJd = news->AsOfJd;
		}
		world->Log(jd, cs::Cat(bestShip->Name, " loads ", Tonnes(bestTonnes), " ", GoodInfo::Of(bestGood)->Name, " at ", bestFrom->Name, " for ", bestTo->Name, " at ", to->Name, ", by the ", bestRoute[0]->Name, ", on news ", LightSpeed::Describe(news->AgeDays(jd)), " old"), WorldEventKind::Info, bestFrom->Body);
	}

	cs::String InterstellarTraffic::Tonnes(double t) { return cs::Cat(cs::Fmt(t, "N0"), " t"); }

	bool InterstellarTraffic::BestCargo(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<Ship>& ship, int port, const cs::List<cs::Ref<JumpGate>>& route, double jd,
		cs::Ref<Settlement>& source, Good& good, double& tonnes, double& profit)
	{
		cs::Ref<Settlement> market;
		double sale = 0;
		return BestCargo(from, to, ship, port, route, jd, to->World->Economy->Settlements, source, good, tonnes, profit, market, sale);
	}

	bool InterstellarTraffic::BestCargo(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<Ship>& ship, int port, const cs::List<cs::Ref<JumpGate>>& route, double jd, const cs::List<cs::Ref<Settlement>>& markets,
		cs::Ref<Settlement>& source, Good& good, double& tonnes, double& profit, cs::Ref<Settlement>& market, double& sale)
	{
		market = nullptr;
		sale = 0;
		source = nullptr;
		good = Good{};
		tonnes = 0;
		profit = 0;
		double ly = GateNetwork::RouteLy(route);
		double days = VoyageDays(from, to, ship, port, route);
		double markup = 1 + MarkupPerLy * ly;
		for (const auto& src : from->World->Economy->At(port))
		{
			if (!from->World->Economy->CanTrade(ship, src)) continue;
			for (int g = 0; g < GoodInfo::Count; g++)
			{
				auto gd = static_cast<Good>(g);
				if (!Economy::Carries(ship->Spec->Class, gd)) continue;
				double available = src->Available(gd);
				double minimum = cs::Math::Min(ship->CapacityTonnes * 0.1, 200.0);
				if (available < minimum) continue;
				for (const auto& dst : markets)
				{
					if (dst->Kind == SettlementKind::Haven || !to->World->Diplomacy()->CanDockAt(ship->Faction, dst)) continue;
					double t = cs::Math::Min(cs::Math::Min(ship->CapacityTonnes, available), dst->Absorb(gd));
					if (t < minimum) continue;
					double landed = src->PurchaseCost(gd, t) * (1 + Economy::PortTax) * markup;
					double value = dst->SaleValue(gd, t) * (1 - Economy::PortTax);
					double p = value - landed - days * RunningCostPerDay;
					if (p > landed * MinMargin && p > profit)
					{
						profit = p;
						market = dst;
						sale = value;
						source = src;
						good = gd;
						tonnes = t;
					}
				}
			}
		}
		return source != nullptr;
	}

	double InterstellarTraffic::VoyageDays(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<Ship>& ship, int port, const cs::List<cs::Ref<JumpGate>>& route)
	{
		double accel = ship->Spec->AccelG * Astro::GravityKmPerDay2;
		auto Burn = [&](double km) { return 2 * cs::Math::Sqrt(cs::Math::Max(km, 1.0) / accel); };
		double outbound = Burn(from->Ephemeris->Helio(port).Length());
		// In from the far gate: to a typical port, about as far out as the one it left.
		double inbound = Burn(cs::Math::Max(from->Ephemeris->Helio(port).Length() * 0.5, 0.05 * Astro::AuKm));
		return outbound + GateNetwork::RouteDays(route) + inbound + 1;
	}

	int InterstellarTraffic::Freighters(const cs::Ref<StarSystem>& home) const
	{
		int n = 0;
		for (const auto& s : home->World->Fleet->Ships())
			if (!s->Destroyed && !s->IsEscort() && s->Role == ShipRole::Trader && s->Spec->Class == ShipClass::Freighter && s->Voyage == nullptr) n++;
		return n + Out(home, VoyagePurpose::Trade);
	}

	void InterstellarTraffic::DispatchCourier(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, double jd)
	{
		if (Out(from, VoyagePurpose::Courier, to) > 0 || rng.NextDouble() > 0.2) return;
		auto world = from->World;
		for (const auto& ship : world->Fleet->Ships())
		{
			if (!Idle(world, ship) || ship->Role != ShipRole::Courier || ship->HasCargo) continue;
			auto route = Network()->Route(from, to, ship->Faction, jd);
			if (route.Count() == 0) continue;
			SetOut(from, to, ship, VoyagePurpose::Courier, route, jd);
			ship->Cargo = cs::Cat("Interstellar mail and data cores for ", to->Name);
			world->Log(jd, cs::Cat(ship->Name, " takes the mail for ", to->Name, " by the ", route[0]->Name), WorldEventKind::Info, PortOf(ship));
			return;
		}
	}

	void InterstellarTraffic::DispatchRaid(const cs::Ref<StarSystem>& from, const cs::List<cs::Ref<StarSystem>>& settled, double jd)
	{
		auto world = from->World;
		if (world->Pirates == nullptr || Out(from, VoyagePurpose::Raid) > 0 || rng.NextDouble() > RaidChancePerDay) return;
		auto targets = cs::List<cs::Ref<StarSystem>>();
		for (const auto& s : settled)
			if (s != from) targets.Add(s);
		if (targets.Count() == 0) return;
		auto to = targets[rng.Next(targets.Count())];
		for (const auto& ship : world->Fleet->Ships())
		{
			if (!Idle(world, ship) || ship->Faction != world->Pirates || ship->Phase != ShipPhase::Parked || ship->Combat->HullDamage() > 0.2 || !ship->Combat->Armed()) continue;
			auto route = Network()->Route(from, to, ship->Faction, jd);
			if (route.Count() == 0) continue;
			SetOut(from, to, ship, VoyagePurpose::Raid, route, jd);
			world->Log(jd, cs::Cat("The ", world->Pirates->Name, " raiders under ", ship->Name, " slip away for ", to->Name), WorldEventKind::Info, -1);
			return;
		}
	}

	void InterstellarTraffic::DispatchSurvey(const cs::Ref<StarSystem>& from, double jd)
	{
		if (Out(from, VoyagePurpose::Survey) >= 2 || rng.NextDouble() > SurveyChancePerDay) return;
		// Now and then a survey hull with a drive of its own goes to look at a star off the gate network (U.SPC-T412).
		if (galaxy->NearbyStars.Count() > 0 && cruiseRng.NextDouble() < StarSurveyShare && DispatchStarSurvey(from, jd)) return;
		auto world = from->World;
		auto wild = cs::List<cs::Ref<StarSystem>>();
		for (const auto& s : galaxy->Systems)
			if (s != from && !s->Settled) wild.Add(s);
		if (wild.Count() == 0) return;
		auto to = wild[rng.Next(wild.Count())];
		for (const auto& ship : world->Fleet->Ships())
		{
			if (!Idle(world, ship) || !IsSurveyHull(ship)) continue;
			auto route = Network()->Route(from, to, ship->Faction, jd);
			if (route.Count() == 0) continue;
			SetOut(from, to, ship, VoyagePurpose::Survey, route, jd);
			ship->Cargo = cs::Cat("Survey instruments, bound for ", to->Name);
			world->Log(jd, cs::Cat(ship->Name, " sets out to survey ", to->Name, ", ", route.Count(), " gate", (route.Count() == 1 ? "" : "s"), " out"), WorldEventKind::Info, PortOf(ship));
			return;
		}
	}

	bool InterstellarTraffic::IsSurveyHull(const cs::Ref<Ship>& ship)
	{
		return ship->Role == ShipRole::Free && (ship->Spec->Class == ShipClass::Courier || ship->Spec->Class == ShipClass::Explorer);
	}

	bool InterstellarTraffic::DispatchStarSurvey(const cs::Ref<StarSystem>& from, double jd)
	{
		// `near` in C#: renamed, as <windows.h> defines it as a macro.
		auto nearStars = cs::List<NearbyStar>();
		for (const auto& n : galaxy->NearbyStars)
			if (n.Origin().DistanceLy(from->Origin) <= MaxStarSurveyLy) nearStars.Add(n);
		if (nearStars.Count() == 0) return false;
		auto star = nearStars[cruiseRng.Next(nearStars.Count())];
		auto world = from->World;
		for (const auto& ship : world->Fleet->Ships())
		{
			if (!Idle(world, ship) || !IsSurveyHull(ship) || !CanCruise(ship)) continue;
			if (SendToStar(ship, from, star, VoyagePurpose::Survey, jd) == nullptr) continue;
			ship->Cargo = cs::Cat("Survey instruments, bound for ", star.Name);
			world->Log(jd, cs::Cat(ship->Name, " sets out under its own drive to survey ", star.Name, ", ",
				cs::Fmt(star.Origin().DistanceLy(from->Origin), "0.0"), " ly through open space"), WorldEventKind::Info, PortOf(ship));
			return true;
		}
		return false;
	}

	bool InterstellarTraffic::DispatchExpedition(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<::SpaceGod::Core::Faction>& quarry, double jd)
	{
		auto world = from->World;
		auto navy = world->Sol;
		if (navy == nullptr || !navy->IsMilitary || Out(from, VoyagePurpose::Expedition) > 0) return Out(from, VoyagePurpose::Expedition) > 0;
		for (const auto& ship : world->Fleet->Ships())
		{
			if (!Idle(world, ship) || ship->Faction != navy || !ship->Spec->IsMilitary || ship->PatrolWing || ship->Phase != ShipPhase::Parked) continue;
			if (world->War->IsHomeGuard(ship) || ship->Combat->HullDamage() > 0.2 || !ship->Combat->Armed()) continue;
			auto route = Network()->Route(from, to, ship->Faction, jd);
			if (route.Count() == 0) continue;
			auto v = SetOut(from, to, ship, VoyagePurpose::Expedition, route, jd);
			v->Quarry = quarry;
			world->Log(jd, cs::Cat(ship->Name, " leads an expedition through the ", route[0]->Name, " after the ", quarry->Name, " at ", to->Name), WorldEventKind::Info, PortOf(ship));
			return true;
		}
		return false;
	}

	void InterstellarTraffic::SendHomeWhenDone(const cs::Ref<StarSystem>& system, double jd)
	{
		auto world = system->World;
		for (const auto& ship : world->Fleet->Ships())
		{
			auto v = ship->Voyage;
			if (v == nullptr || ship->Destroyed || ship->IsEscort() || v->Homeward() || v->AtGate || ship->GateKm.has_value() || system != v->Destination) continue;
			if (ship == world->PilotedShip || ship->Faction != v->Faction)
			{
				CancelVoyage(system, ship, jd, ship->Faction == v->Faction);
				continue;
			}
			// A war fleet comes home when its campaign says so (see InterstellarWars).
			if (v->Purpose == VoyagePurpose::Campaign) continue;
			// So do supply ships still bound for their war fleet, when their navy's logistics says so (U.SPC-T525).
			if (v->Purpose == VoyagePurpose::Supply && v->Convoy != nullptr) continue;
			bool hurt = (ship->Spec->IsMilitary || ship->Faction->IsPirate) && ship->Combat->HullDamage() > 0.45;
			// A group its polity sent here stays while its objective here stands (U.SPC-T423).
			if (v->Purpose == VoyagePurpose::Directed && !hurt && galaxy->Polities->Command->Holds(ship, world)) continue;
			if (jd < v->StayUntilJd && !hurt && v->Purpose != VoyagePurpose::Directed) continue;
			if (ship->Battle != nullptr || (ship->Phase != ShipPhase::Parked && ship->Phase != ShipPhase::Landed)) continue;
			// A freighter sells what it carries before it goes (unless it has been at it far too long).
			if (ship->HasCargo && v->Purpose == VoyagePurpose::Trade && jd < v->StayUntilJd + 30) continue;
			auto route = Network()->Route(system, v->Home, ship->Faction, jd);
			ship->StakeoutKm = std::nullopt;
			if (route.Count() == 0)
			{
				// No way home through the gates, but a drive of its own (U.SPC-T412): home through open space.
				if (CanCruise(ship) && system != v->Home) CruiseFrom(system, ship, v->Home, jd);
				continue;
			}
			// It knows home's markets only by the news that has reached it here (U.SPC-T100).
			auto homeNews = News->Known(system, v->Home);
			cs::Ref<Settlement> src, market;
			Good good{};
			double tonnes = 0, profit = 0, sale = 0;
			if (v->Purpose == VoyagePurpose::Trade && !ship->HasCargo && homeNews != nullptr &&
				BestCargo(system, v->Home, ship, PortOf(ship), route, jd, homeNews->Markets, src, good, tonnes, profit, market, sale))
			{
				world->Economy->LoadUnbooked(ship, src, good, tonnes, v->Home->Name);
				world->Log(jd, cs::Cat(ship->Name, " loads ", Tonnes(tonnes), " ", GoodInfo::Of(good)->Name, " at ", src->Name, " to take home to ", v->Home->Name), WorldEventKind::Info, src->Body);
			}
			v->Destination = v->Home;
			v->Route = route;
			v->Leg = 0;
			v->ByCruise = false;
			ship->GateKm = route[0]->PositionKm;
			world->Fleet->ScheduleNext(ship, jd);
		}
	}
}
