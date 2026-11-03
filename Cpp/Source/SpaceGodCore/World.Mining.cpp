// Port of Unity.SpaceGod Core/World.Mining.cs (US-T8): part of the partial class World (World.h).

#include "World.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Economy/BeltMining.h"
#include "Economy/Economy.h"
#include "Economy/Industry.h"
#include "Economy/Settlement.h"

namespace SpaceGod::Core
{
	void World::WireMining(int seed)
	{
		auto m = Mining = cs::New<BeltMining>(seed);
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

	double World::MiningPiratePressure()
	{
		double raiders = 0;
		for (const auto& s : Fleet->Ships())
			if (!s->Destroyed && !s->IsEscort() && s->Faction != nullptr && s->Faction->IsPirate) raiders++;
		for (const auto& h : Economy->Settlements)
			if (h->Kind == SettlementKind::Haven && !h->Fallen) raiders += 0.5;
		return cs::Math::Min(1.0, raiders / MiningPressureRaiders);
	}

	cs::Nullable<HeavyLanePath> World::MiningHeavyLane(int from, int to, double jd)
	{
		auto lanes = Fleet ? Fleet->Lanes : nullptr;
		if (lanes == nullptr) return std::nullopt;
		// (Hyperlanes' C++ routes are cs::Ref<cs::List<LaneHop>>, null where C# returns null.)
		auto route = lanes->HeavyRoute(lanes->SystemOf(from), lanes->SystemOf(to), jd);
		if (route == nullptr || route->Count() == 0) return std::nullopt;
		double laneKm = 0;
		cs::StringBuilder names;
		for (const auto& hop : *route)
		{
			laneKm += lanes->LaneLengthKm(hop.Lane, jd);
			if (names.Length() > 0) names.Append(", ");
			names.Append(hop.Lane->Name);
		}
		auto first = (*route)[0];
		auto last = (*route)[route->Count() - 1];
		double legKm = (Ephemeris->HelioAt(from, jd) - lanes->RampHelio(first.Lane, first.From, jd)).Length()
			+ (Ephemeris->HelioAt(to, jd) - lanes->RampHelio(last.Lane, last.To, jd)).Length();
		return HeavyLanePath(laneKm, legKm, names.ToString());
	}

	void World::SeedSolMining(double jd)
	{
		auto depot = Mining->AddWarehouse("Valhalla Depot", Belt, (*Catalog())["Callisto"]->Index);
		auto works = Mining->AddOperation("Ceres Belt Works", Belt, (*Catalog())["Ceres"]->Index, depot);
		works->OreTugs = 2;
		works->TugIceShare = 1;
		works->IceDepot = Mining->AddWarehouse("Valhalla Ice Depot", Belt, (*Catalog())["Callisto"]->Index);
		RaiseTugs(works, jd);
		// Its convoys sail in two freighters of its own, each with a gunship riding escort (U.SPC-T523).
		works->ConvoyFreighters = 2;
		works->EscortShare = 0.5;
		RaiseFreighters(works, jd);
		// Its capture tugs (U.SPC-T522) are real hulls too, bringing belt rock in to the grinders at Ceres.
		RaiseCaptureTugs(works, jd);
		Mining->Begin(MegastructureSpec::SpaceElevator(), Belt, (*Catalog())["Ganymede"]->Index, "Ganymede Elevator", jd);
		Mining->Begin(MegastructureSpec::DysonRing(), Belt, 0, "Helios Swarm", jd);
	}

	// ---------------------------------------------------------------- the ore tugs' hulls (U.SPC-T498)

	const cs::List<cs::String>& World::TugNames()
	{
		static const cs::List<cs::String> names = { "Bulldog", "Sisyphus", "Atlas", "Yoke", "Plough Horse", "Long Shove", "Hauler's Oath", "Mule" };
		return names;
	}

	cs::List<cs::Ref<Ship>> World::RaiseTugs(cs::Ref<MiningOperation> op, double jd)
	{
		if (!op->Tugs) op->Tugs = cs::New<cs::List<cs::Ref<Ship>>>();
		cs::List<cs::Ref<Ship>> made;
		int home = op->TugHome(), to = op->IceDepot ? op->IceDepot->Body : op->RefineryBody;
		while (op->Tugs->Count() < op->OreTugs)
		{
			cs::String name = TugNames()[tugsNamed % TugNames().Count()] + (tugsNamed >= TugNames().Count() ? cs::Cat(" ", tugsNamed / TugNames().Count() + 1) : cs::String());
			tugsNamed++;
			auto ship = Fleet->AddShip(name, ShipClass::OreTug, op->Owner, ShipRole::Tug);
			ship->Ports = home == to ? cs::List<int>{ home } : cs::List<int>{ home, to };
			ship->Cargo = "Claws open, waiting for a load";
			Fleet->Station(ship, home, jd);
			op->Tugs->Add(ship);
			made.Add(ship);
		}
		op->OreTugs = op->Tugs->Count();
		return made;
	}

	// ---------------------------------------------------------------- the capture tugs' hulls (U.SPC-T522)

	const cs::List<cs::String>& World::CaptureTugNames()
	{
		static const cs::List<cs::String> names = { "Lasso", "Grapple", "Gaff", "Tongs", "Snare", "Bola", "Butterfly Net", "Catch-As-Catch-Can" };
		return names;
	}

	cs::List<cs::Ref<Ship>> World::RaiseCaptureTugs(cs::Ref<MiningOperation> op, double jd)
	{
		if (!op->CaptureHulls) op->CaptureHulls = cs::New<cs::List<cs::Ref<Ship>>>();
		cs::List<cs::Ref<Ship>> made;
		int home = op->RefineryBody;
		while (op->CaptureHulls->Count() < op->CaptureTugs)
		{
			cs::String name = CaptureTugNames()[captureTugsNamed % CaptureTugNames().Count()]
				+ (captureTugsNamed >= CaptureTugNames().Count() ? cs::Cat(" ", captureTugsNamed / CaptureTugNames().Count() + 1) : cs::String());
			captureTugsNamed++;
			auto ship = Fleet->AddShip(name, ShipClass::CaptureTug, op->Owner, ShipRole::Tug);
			ship->Ports = cs::List<int>{ home };
			ship->Cargo = "Claws open, waiting to go out for a rock";
			Fleet->Station(ship, home, jd);
			op->CaptureHulls->Add(ship);
			made.Add(ship);
		}
		op->CaptureTugs = op->CaptureHulls->Count();
		return made;
	}

	bool World::TugFree(cs::Ref<Ship> tug) { return tug != PilotedShip && tug->Battle == nullptr && (!Fleet->IsHeld || !Fleet->IsHeld(tug)); }

	void World::TugSent(cs::Ref<TugHaul> haul, double jd)
	{
		auto tug = haul->Tug;
		double now = cs::Double::IsNaN(Jd) ? jd : cs::Math::Max(jd, Jd);
		if (haul->Latched)
		{
			tug->Cargo = cs::Cat("Pushing ", haul->Load->Designation, ", ", cs::Fmt(haul->Load->Tonnes, "N0"), " t, to ", (*Catalog())[haul->ToBody]->Name);
			Fleet->ScheduleNext(tug, now);
			return;
		}
		tug->Cargo = cs::Cat("Out to the field for ", haul->Load->Designation);
		tug->Wander = true;
		tug->DestinationBody = tug->Orbit->Body;
		tug->PhaseStartJd = now;
		tug->PhaseEndJd = now + 0.05;
	}

	void World::TugArrived(cs::Ref<Ship> tug, double jd)
	{
		auto h = Mining ? Mining->HaulOf(tug) : nullptr;
		if (h == nullptr || !Mining->TugArrived(tug, jd)) return;
		if (tug->Tow != nullptr) tug->Cargo = cs::Cat("Pushing ", h->Load->Designation, ", ", cs::Fmt(h->Load->Tonnes, "N0"), " t, in to ", (*Catalog())[h->ToBody]->Name);
		else
		{
			tug->Cargo = h->Capture ? "Claws open, back at the refinery" : "Running empty, back to the field";
			Log(jd, cs::Cat("The ", h->TugKind(), " ", tug->Name, " brings ", h->Load->Designation, " in to ", (h->ToDepot != nullptr ? cs::Cat("the ", h->ToDepot->Name) : (*Catalog())[h->ToBody]->Name),
				": ", cs::Fmt(h->Load->Tonnes, "N0"), " t of ", cs::Strings::ToLowerInvariant(h->Load->Class->Name)),
				WorldEventKind::Economy, h->ToBody);
		}
	}

	TugDanger World::TugThreat(cs::Ref<TugHaul> haul, double jd)
	{
		auto tug = haul->Tug;
		double at = cs::Double::IsNaN(Jd) ? jd : Jd;
		auto here = Fleet->StateOf(tug, at).HelioKm;
		// (C#'s local `near` is `nearby` here: <windows.h> defines a `near` macro a UE unity build can pull in.)
		int nearby = 0;
		cs::Ref<Faction> nearest = nullptr;
		double best = cs::Double::MaxValue;
		for (const auto& s : Fleet->Ships())
		{
			if (s->Destroyed || s->Derelict || s->Faction == nullptr || !s->Faction->IsPirate || s->Controller()->Planetside()) continue;
			double d = (Fleet->StateOf(s, at).HelioKm - here).Length();
			if (d > TugRaidReachKm) continue;
			nearby++;
			if (d < best)
			{
				best = d;
				nearest = s->Faction;
			}
		}
		if (nearby == 0) return TugDanger(nullptr, 0, -1);
		return TugDanger(nearest, cs::Math::Min(1.0, static_cast<double>(nearby) / TugPressureRaiders), NearestBody(here, at));
	}

	int World::NearestBody(DVec3 helioKm, double jd)
	{
		int best = -1;
		double bestKm = cs::Double::MaxValue;
		for (const auto& b : Catalog()->Bodies())
		{
			if (!b->IsVisible() || b->IsStar()) continue;
			double d = (Ephemeris->HelioAt(b->Index, jd) - helioKm).Length();
			if (d < bestKm)
			{
				bestKm = d;
				best = b->Index;
			}
		}
		return best;
	}

	void World::HandOverTug(cs::Ref<TugHaul> haul, double jd)
	{
		auto tug = haul->Tug;
		auto by = haul->TakenBy;
		if (tug == nullptr || by == nullptr || tug->Destroyed) return;
		auto old = tug->Faction;
		tug->Faction = by;
		// (C#'s culture-sensitive string.StartsWith(string) is ordinal here: the names are ASCII.)
		if (static_cast<int>(by->Prefix.size()) > 0 && old != nullptr && cs::Strings::StartsWith(tug->Name, old->Prefix + " "))
			tug->Name = by->Prefix + cs::Strings::Substring(tug->Name, static_cast<int>(old->Prefix.size()));
		Fleet->AddFaction(by);
		tug->Role = ShipRole::Free;
		tug->Wander = false;
		tug->Homing = false;
		tug->Cargo = cs::Cat("Taken by the ", by->Name);
		cs::List<int> havens;
		for (const auto& s : Economy->Settlements)
			if (s->Owner == by && s->Kind == SettlementKind::Haven && !s->Fallen && !havens.Contains(s->Body)) havens.Add(s->Body);
		bool hold = havens.Count() > 0;
		if (!hold)
			for (const auto& b : Catalog()->Bodies())
				if (b->IsVisible() && !b->IsStar()) havens.Add(b->Index);
		tug->Ports = havens.ToArray();
		// Turned for the raiders' hold at once where it is on a burn; on a lane, or in orbit, when it next plans.
		double now = cs::Double::IsNaN(Jd) ? jd : cs::Math::Max(jd, Jd);
		if (hold && Fleet->Divert(tug, havens[0], now)) return;
		if (tug->Phase == ShipPhase::Parked && (!Fleet->IsHeld || !Fleet->IsHeld(tug))) Fleet->ScheduleNext(tug, now);
	}
}
