// Port of Assets/UnitySpace/Core/Systems/DeepSpace.cs (see docs/MIGRATION.md).

#include "Systems/DeepSpace.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Combat/GatePickets.h"
#include "Economy/Escorts.h"
#include "Economy/Goods.h"
#include "Information/News.h"
#include "Systems/CruiseDrive.h"
#include "Systems/DeepSpaceLife.h"
#include "Systems/FtlRules.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/Polities.h"
#include "Systems/VoidRaiders.h"
#include "Systems/VoidWatcher.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	// ================================================================ Cruise

	cs::List<cs::Ref<Ship>> Cruise::CarriedBy(cs::Ref<Ship> carrier)
	{
		cs::List<cs::Ref<Ship>> list;
		for (auto [key, value] : Aboard)
			if (value == carrier) list.Add(key);
		return list;
	}

	cs::Ref<Ship> Cruise::Leader() const { return Ships[0]; }

	double Cruise::ArriveJd() const { return Holding ? cs::Double::PositiveInfinity : LegJd + Profile.TotalDays(); }

	DVec3 Cruise::Direction() const { return End.KmFrom(Start).Normalized(); }

	bool Cruise::Engaged(double jd) const { return !Holding && jd >= LegJd + Profile.SpoolDays && jd < ArriveJd(); }

	GalacticPosition Cruise::PositionAt(double jd) const
	{
		if (Holding || jd <= LegJd + Profile.SpoolDays) return Start;
		if (jd >= ArriveJd()) return End;
		return Start.Offset(Direction() * (Profile.LyAt(jd - LegJd) * Astro::LightYearKm));
	}

	double Cruise::SpeedKmS(double jd) const
	{
		return Holding ? 0 : Profile.SpeedAt(jd - LegJd) * Astro::LightYearKm / Astro::SecondsPerDay;
	}

	double Cruise::LyToGo(double jd) const { return Target.DistanceLy(PositionAt(jd)); }

	void Cruise::SetLeg(GalacticPosition from, double jd, bool charged)
	{
		Holding = false;
		Crossing = false;
		HoldUntilJd = cs::Double::NaN;
		Start = from;
		End = Target;
		LegJd = jd;
		StopJd = cs::Double::NaN;
		RaidStrikeJd = cs::Double::NaN;
		RaidSeen = RaidStruck = RunDark = false;
		double ly = Target.DistanceLy(from);
		// No jump longer than the drive reaches (US-T86): a longer way is flown as even jumps, this the first of them,
		// ending at a stop in the void (DeepSpace.Due takes it on from there).
		int jumps = Drive->JumpsFor(ly);
		Stopover = jumps > 1;
		if (Stopover)
		{
			End = GalacticPosition::Lerp(from, Target, 1.0 / jumps);
			ly = End.DistanceLy(from);
		}
		double cool = 0;
		for (const auto& s : Ships)
		{
			cool = cs::Math::Max(cool, FtlRules::CoolUntil(s) - jd);
			s->SetPropellant(s->Propellant() - FtlRules::PropellantFor(Drive, s, ly));
		}
		ChargeJd = charged ? jd : jd + cs::Math::Max(0.0, cool);
		Profile = charged ? CruiseProfile(ly, Drive->LyPerDay(), Drive->RampDays, 0)
			: CruiseProfile(ly, Drive->LyPerDay(), Drive->RampDays, Drive->SpoolDays() + cs::Math::Max(0.0, cool));
	}

	bool Cruise::AtVoidStop(double jd) const
	{
		return !Holding && !Crossing && !cs::Double::IsNaN(StopJd) && jd < EngageJd();
	}

	double Cruise::ChargeFraction(double jd) const
	{
		double from = cs::Double::IsNaN(ChargeJd) ? LegJd : ChargeJd, to = EngageJd();
		if (jd <= from) return 0;
		return to <= from ? 1 : cs::Math::Min(1.0, (jd - from) / (to - from));
	}

	void Cruise::DelayCharge(double days)
	{
		if (!(days > 0)) return;
		if (!cs::Double::IsNaN(ChargeJd)) ChargeJd += days;
		Profile.SpoolDays += days;
	}

	int Cruise::JumpsLeft(double jd) const
	{
		if (Holding || Crossing || Drive == nullptr) return 1;
		auto from = jd >= EngageJd() ? End : Start;
		return (jd >= EngageJd() ? 1 : 0) + (from.DistanceKm(Target) > 1 ? Drive->JumpsFor(Target.DistanceLy(from)) : 0);
	}

	void Cruise::SetCrossing(GalacticPosition entry, GalacticPosition exit, double jd)
	{
		Holding = false;
		Crossing = true;
		HoldUntilJd = cs::Double::NaN;
		Start = entry;
		End = exit;
		LegJd = jd;
		double lyPerDay = FtlRules::CrossingSpeedKmS()->Value * Astro::SecondsPerDay / Astro::LightYearKm;
		Profile = CruiseProfile(exit.DistanceLy(entry), lyPerDay, 1e-6, 0);
	}

	cs::String Cruise::ToString() const { return cs::Cat(Leader(), " bound for ", ToName); }

	// ================================================================ DeepSpaceObject

	const char* ToString(DeepSpaceObjectKind v)
	{
		switch (v)
		{
		case DeepSpaceObjectKind::Wreck: return "Wreck";
		case DeepSpaceObjectKind::Derelict: return "Derelict";
		case DeepSpaceObjectKind::RoguePlanet: return "RoguePlanet";
		case DeepSpaceObjectKind::Nebula: return "Nebula";
		case DeepSpaceObjectKind::IceBody: return "IceBody";
		case DeepSpaceObjectKind::Relay: return "Relay";
		case DeepSpaceObjectKind::ListeningPost: return "ListeningPost";
		case DeepSpaceObjectKind::Distress: return "Distress";
		}
		return "";
	}

	cs::String DeepSpaceObject::ToString() const { return Name; }

	// ================================================================ DeepSpace

	const cs::List<cs::Ref<Cruise>>& DeepSpace::Cruises() const { return cruises; }

	const cs::List<cs::Ref<DeepSpaceObject>>& DeepSpace::Objects() const { return objects; }

	const cs::List<cs::Ref<DeepSpaceEvent>>& DeepSpace::Events() const { return events; }

	cs::List<cs::Ref<Ship>> DeepSpace::InFlight() const
	{
		cs::List<cs::Ref<Ship>> all;
		for (const auto& c : cruises)
			for (const auto& s : c->Ships)
				all.Add(s);
		return all;
	}

	cs::List<cs::Func<cs::Ref<IDeepSpaceEncounter>>>& DeepSpace::EncounterKinds()
	{
		static cs::List<cs::Func<cs::Ref<IDeepSpaceEncounter>>> kinds
		{
			[]() -> cs::Ref<IDeepSpaceEncounter> { return cs::New<FtlInterdiction>(); },
			[]() -> cs::Ref<IDeepSpaceEncounter> { return cs::New<CruiseMeeting>(); },
			[]() -> cs::Ref<IDeepSpaceEncounter> { return cs::New<WreckSalvage>(); },
			// Life in deep space (U.SPC-T421): what lies out here, and who is out here with it.
			[]() -> cs::Ref<IDeepSpaceEncounter> { return cs::New<SurveyFinds>(); },
			[]() -> cs::Ref<IDeepSpaceEncounter> { return cs::New<IceRefuelling>(); },
			[]() -> cs::Ref<IDeepSpaceEncounter> { return cs::New<DistressCalls>(); },
			[]() -> cs::Ref<IDeepSpaceEncounter> { return cs::New<DeepSpaceOutposts>(); },
			[]() -> cs::Ref<IDeepSpaceEncounter> { return cs::New<RouteAmbushes>(); },
		};
		return kinds;
	}

	DeepSpace::DeepSpace(Core::Galaxy* galaxy, int seed)
		: galaxy(galaxy), rng(seed)
	{
		for (const auto& make : EncounterKinds()) Encounters.Add(make());
	}

	// ---------------------------------------------------------------- setting out

	DVec3 DeepSpace::DeparturePointKm(cs::Ref<StarSystem> from, GalacticPosition target)
	{
		auto way = target.KmFrom(from->Origin);
		auto dir = way.LengthSquared() > 0 ? way.Normalized() : DVec3::UnitX;
		return dir * CruiseDrives::WellLimitKm(from);
	}

	GalacticPosition DeepSpace::ArrivalPoint(GalacticPosition star, GalacticPosition from, double wellKm)
	{
		auto back = from.KmFrom(star);
		auto dir = back.LengthSquared() > 0 ? back.Normalized() : DVec3::UnitX;
		return star.Offset(dir * wellKm);
	}

	cs::Ref<Cruise> DeepSpace::Launch(const cs::List<cs::Ref<Ship>>& ships, cs::Ref<StarSystem> from, DVec3 fromKm, cs::Ref<StarSystem> to, cs::Ref<CruiseDriveSpec> drive, double jd,
		NewsList news, bool charged, cs::Nullable<DVec3> dropDir)
	{
		auto start = from->At(fromKm);
		double wellKm = CruiseDrives::WellLimitKm(to);
		auto target = dropDir.has_value() && dropDir->LengthSquared() > 0 ? to->Origin.Offset(dropDir->Normalized() * wellKm) : ArrivalPoint(to->Origin, start, wellKm);
		return Launch(ships, from, start, to, to->Name, target, drive, jd, news, charged);
	}

	cs::Ref<Cruise> DeepSpace::LaunchToStar(const cs::List<cs::Ref<Ship>>& ships, cs::Ref<StarSystem> from, DVec3 fromKm, NearbyStar star, cs::Ref<CruiseDriveSpec> drive, double jd,
		NewsList news)
	{
		auto start = from->At(fromKm);
		return Launch(ships, from, start, nullptr, star.Name, ArrivalPoint(star.Origin(), start, CruiseDrives::WellLimitKm(nullptr)), drive, jd, news, false);
	}

	cs::Ref<Cruise> DeepSpace::Launch(const cs::List<cs::Ref<Ship>>& ships, cs::Ref<StarSystem> from, GalacticPosition start, cs::Ref<StarSystem> to, const cs::String& toName, GalacticPosition target,
		cs::Ref<CruiseDriveSpec> drive, double jd, NewsList news, bool charged)
	{
		if (drive == nullptr) throw cs::ArgumentNullException("drive: A cruise needs a drive");
		auto c = cs::New<Cruise>();
		c->From = from; c->To = to; c->ToName = toName; c->Drive = drive; c->Target = target; c->SetOutJd = jd; c->News = news;
		// What lies along the way is there before anyone flies it (U.SPC-T421): the same for every flight of this route.
		GalacticPosition star;
		if (TryStar(toName, star)) SeedRoute(from->Name, from->Origin, toName, star);
		c->Ships.AddRange(ships);
		if (c->Ships.Count() == 0) throw cs::ArgumentException("A cruise needs a ship (ships)");
		for (const auto& s : c->Ships)
		{
			s->Destroyed = false;
			s->GateDeparted = false;
		}
		c->SetLeg(start, jd, charged);
		cruises.Add(c);
		Log(jd, cs::Cat(c->Leader()->Name, More(c), " engage", c->Ships.Count() > 1 ? "" : "s", " ", cs::Strings::ToLowerInvariant(drive->Name), "s out of ", from->Name, " for ", toName, ", ",
			cs::Fmt(c->Profile.Ly, "0.00"), " ly, due in ", cs::Fmt(c->Profile.TotalDays(), "0.0"), " days"), start);
		return c;
	}

	cs::String DeepSpace::More(const cs::Ref<Cruise>& c) { return c->Ships.Count() > 1 ? cs::Cat(" and ", c->Ships.Count() - 1, " more") : cs::String(); }

	void DeepSpace::Retarget(cs::Ref<Cruise> c, cs::Ref<StarSystem> to, double jd)
	{
		auto here = c->PositionAt(jd);
		GalacticPosition was;
		if (TryStar(c->ToName, was)) SeedRoute(c->ToName, was, to->Name, to->Origin);
		c->To = to;
		c->ToName = to->Name;
		c->Target = ArrivalPoint(to->Origin, here, CruiseDrives::WellLimitKm(to));
		c->SetLeg(here, jd);
		Log(jd, cs::Cat(c->Leader()->Name, More(c), " set", c->Ships.Count() > 1 ? "" : "s", " out from open space for ", to->Name, ", ",
			cs::Fmt(c->Profile.Ly, "0.00"), " ly"), here);
	}

	void DeepSpace::DropOut(cs::Ref<Cruise> c, double jd, double days)
	{
		auto here = c->PositionAt(jd);
		c->Start = c->End = here;
		c->Holding = true;
		c->Crossing = false;
		c->HoldUntilJd = jd + days;
		for (const auto& s : c->Ships) FtlRules::StartCooldown(s, c->Drive, jd);
	}

	// ---------------------------------------------------------------- gravity fields (U.SPC-T418)

	const cs::List<cs::Ref<GravityProjector>>& DeepSpace::Projectors() const { return projectors; }

	cs::Ref<GravityProjector> DeepSpace::AddProjector(cs::Ref<GravityProjector> p)
	{
		if (p != nullptr && !projectors.Contains(p)) projectors.Add(p);
		return p;
	}

	void DeepSpace::RemoveProjector(cs::Ref<GravityProjector> p) { projectors.Remove(p); }

	cs::List<DeepSpace::FieldSource>& DeepSpace::FieldSources()
	{
		static cs::List<FieldSource> sources{ FieldSource(StarWells), FieldSource(ProjectorFields), FieldSource(RogueWells) };
		return sources;
	}

	cs::List<GravityField> DeepSpace::StarWells(DeepSpace& space, cs::Ref<Cruise> c, GalacticPosition near, double km)
	{
		// (A C# iterator: built eagerly here, as nothing it does has a side effect.)
		cs::List<GravityField> found;
		auto g = space.galaxy;
		if (g == nullptr) return found;
		for (const auto& s : g->Systems)
		{
			if (s->Origin.DistanceKm(near) >= km + 200 * Astro::AuKm) continue;
			auto f = FtlRules::StarWell(s);
			if (f.RadiusKm > 0 && f.Center.DistanceKm(near) < km + f.RadiusKm) found.Add(f);
		}
		for (const auto& n : g->NearbyStars)
		{
			if (n.Origin().DistanceKm(near) >= km + 200 * Astro::AuKm) continue;
			auto f = FtlRules::StarWell(n);
			if (f.Center.DistanceKm(near) < km + f.RadiusKm) found.Add(f);
		}
		// A star nobody has discovered pulls a group out all the same (U.SPC-T456): it is there, charted or not. Only a
		// course being planned cannot reckon with it.
		if (c != nullptr && c->Plan) return found;
		for (const auto& n : g->Undiscovered())
		{
			if (n.Origin().DistanceKm(near) >= km + 200 * Astro::AuKm) continue;
			auto f = FtlRules::StarWell(n);
			if (f.Center.DistanceKm(near) < km + f.RadiusKm) found.Add(f);
		}
		return found;
	}

	cs::List<GravityField> DeepSpace::ProjectorFields(DeepSpace& space, cs::Ref<Cruise> c, GalacticPosition near, double km)
	{
		cs::List<GravityField> found;
		for (const auto& p : space.projectors)
		{
			if (!p->Active || p->RadiusKm <= 0) continue;
			if (p->Owner != nullptr && c->Leader()->Faction == p->Owner) continue;
			if (p->Position.DistanceKm(near) < km + p->RadiusKm) found.Add(p->Field());
		}
		return found;
	}

	cs::List<GravityField> DeepSpace::FieldsAlong(cs::Ref<Cruise> c, GalacticPosition from, GalacticPosition to)
	{
		cs::List<GravityField> found;
		auto mid = GalacticPosition::Lerp(from, to, 0.5);
		double half = to.DistanceKm(from) / 2;
		for (const auto& source : FieldSources())
			for (const auto& f : source(*this, c, mid, half))
				found.Add(f);
		return found;
	}

	void DeepSpace::Interdict(cs::Ref<Cruise> c, GravityField field, GalacticPosition entry, double jd)
	{
		auto way = c->Target.KmFrom(entry);
		auto dir = way.LengthSquared() > 0 ? way.Normalized() : c->Direction();
		auto exit = FtlRules::ExitPoint(field, entry, dir);
		// Never past where it was going.
		if (exit.DistanceKm(entry) > c->Target.DistanceKm(entry)) exit = c->Target;
		for (const auto& s : c->Ships) FtlRules::StartCooldown(s, c->Drive, jd);
		c->InterdictedBy = field.Name;
		c->SetCrossing(entry, exit, jd);
		cs::String what = field.Kind == GravityFieldKind::Projector ? cs::Cat("the gravity projector ", field.Name) : cs::Cat(field.Name, "'s gravity well");
		bool many = c->Ships.Count() > 1;
		Log(jd, cs::Cat(c->Leader()->Name, More(c), " ", many ? "are" : "is", " pulled out of FTL by ", what, ", crossing it under ", many ? "their" : "its", " ordinary drive"), entry);
		// A star nobody knew was there is found by falling into its well (U.SPC-T456).
		if (field.Kind != GravityFieldKind::Projector && galaxy != nullptr) galaxy->Discover(field.Name, jd, cs::Cat("the ", c->Leader()->Name));
	}

	// ---------------------------------------------------------------- the step

	void DeepSpace::Update(double jd)
	{
		if (cs::Double::IsNaN(jd)) return;
		if (cs::Double::IsNaN(nextMarkJd) || cruises.Count() == 0) nextMarkJd = Mark(jd) + StepDays;
		while (nextMarkJd <= jd)
		{
			double mark = nextMarkJd;
			Due(mark);
			for (const auto& e : Encounters) e->Step(*this, mark - StepDays, mark, rng);
			// The ghosts at the void stops (US-T124), on their own numbers (not an encounter kind, so a save from before
			// them runs them too).
			VoidRaiders::Step(*this, mark - StepDays, mark, raidRng);
			// The Watcher in the void (US-T178): dread gathered, and its signs, on its own numbers too.
			VoidWatcher::Step(*this, mark - StepDays, mark, watcherRng);
			nextMarkJd += StepDays;
		}
		Due(jd);
	}

	double DeepSpace::Mark(double jd) { return cs::Math::Floor(jd / StepDays + 1e-9) * StepDays; }

	void DeepSpace::Due(double jd)
	{
		while (true)
		{
			cs::Ref<Cruise> next = nullptr;
			double when = cs::Double::PositiveInfinity;
			for (const auto& c : cruises)
			{
				double t = c->Holding ? c->HoldUntilJd : c->ArriveJd();
				if (t <= jd && t < when)
				{
					when = t;
					next = c;
				}
			}
			if (next == nullptr) return;
			if (next->Holding) next->SetLeg(next->Start, when);
			else if (next->Stopover && !next->Crossing)
			{
				// One jump done of several (US-T86): it drops out here in the void, every drive cooling from the jump, and
				// spools again for the next once they have cooled.
				for (const auto& s : next->Ships) FtlRules::StartCooldown(s, next->Drive, when);
				int stops = next->Stops + 1;
				next->SetLeg(next->End, when);
				next->StopJd = when;
				next->Stops = stops;
				cs::String many = next->Ships.Count() > 1 ? "" : "s";
				Log(when, cs::Cat(next->Leader()->Name, More(next), " drop", many, " out in the void between jumps (stop ", stops, ", ",
					cs::Fmt(next->LyToGo(when), "0.0"), " ly short of ", next->ToName, "), the drive", next->Ships.Count() > 1 ? "s" : "", " cooling to spool again"), next->Start);
			}
			else if (next->Crossing && next->End.DistanceKm(next->Target) > 1)
			{
				// Clear of the field on the far side: it spools again for where it was going.
				next->SetLeg(next->End, when);
				cs::String many = next->Ships.Count() > 1 ? "" : "s";
				Log(when, cs::Cat(next->Leader()->Name, More(next), " clear", many, " ", next->InterdictedBy, "'s pull and spool", many, " again for ", next->ToName), next->Start);
			}
			else Drop(next, when);
		}
	}

	void DeepSpace::Drop(cs::Ref<Cruise> c, double jd)
	{
		if (c->To == nullptr)
		{
			c->Start = c->End = c->Target;
			c->Holding = true;
			c->Crossing = false;
			c->HoldUntilJd = cs::Double::PositiveInfinity;
			for (const auto& s : c->Ships) FtlRules::StartCooldown(s, c->Drive, jd);
			Log(jd, cs::Cat(c->Leader()->Name, More(c), " drop", c->Ships.Count() > 1 ? "" : "s", " out at ", c->ToName), c->Target);
			ReachedStar.Invoke(c, jd);
			return;
		}
		cruises.Remove(c);
		auto system = c->To;
		auto world = system->World;
		auto fleet = world->Fleet;
		// At the edge of the system, clear of any planet's well too (U.SPC-T418).
		auto at = FtlRules::ClearOfBodyWells(system, system->LocalKm(c->Target));
		// Facing in, toward the star, as it comes out of its cruise.
		auto forward = at.LengthSquared() > 0 ? (-at).Normalized() : DVec3::UnitX;
		auto up = cs::Math::Abs(forward.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX;
		auto right = DVec3::Cross(forward, up).Normalized();
		for (const auto& s : c->Ships)
		{
			// Still exactly as it was engaging its drive (U.SPC-T453: the same object, never reset mid-flight)
			// until now, dropping out at the edge of this system's well.
			s->ResetForArrival();
			FtlRules::StartCooldown(s, c->Drive, jd);
			auto orbit = cs::New<ParkingOrbit>();
			orbit->Body = system->Catalog->Root()->Index; orbit->RadiusKm = 0; orbit->Center = at; orbit->U = right; orbit->V = forward;
			orbit->EpochJd = jd; orbit->PhaseAtEpoch = 0; orbit->RateRadPerDay = 0; orbit->Fixed = true;
			s->Orbit = orbit;
			s->Phase = ShipPhase::Parked;
			s->PhaseStartJd = jd;
			s->PhaseEndJd = jd;
			s->DestinationBody = s->Orbit->Body;
			s->Ports = world->FreeShipPorts();
			s->GateKm = std::nullopt;
			fleet->Receive(s);
			// A fresh id here: the group keeps its own identity from ids this fleet handed out (as at a gate, U.SPC-T363).
			s->BandId = s->Leader == nullptr ? s->Id : s->Leader->BandId;
			if (c->Hired.Contains(s) && world->Escorts != nullptr) world->Escorts->MarkHired(s);
		}
		world->Log(jd, cs::Cat(c->Leader()->Name, More(c), " of ", c->Leader()->Faction->Name, " drop", c->Ships.Count() > 1 ? "" : "s", " out of cruise at the edge of ", system->Name, ", ",
				"in from ", c->From != nullptr ? c->From->Name : cs::String("open space")), WorldEventKind::Info, system->Catalog->Root()->Index);
		// A gate picket near where it drops out sees it and warns its navy if it is hostile (U.SPC-T480).
		GatePickets::OnDropOut(system, c->Ships, at, jd);
		Arrived.Invoke(c, jd);
		// Nobody sent it here on business: a free ship, it finds its own way in (the player's own, jumped from the helm,
		// is the helm's to fly on from here, U.SPC-T417).
		auto leader = c->Leader();
		if (leader->Voyage == nullptr && !leader->Destroyed && !c->Piloted)
		{
			leader->Role = leader->Role == ShipRole::Escort ? ShipRole::Free : leader->Role;
			fleet->ScheduleNext(leader, jd);
		}
		for (const auto& s : c->Ships) world->NotifyAdded(s);
	}

	// ---------------------------------------------------------------- what happens out here

	void DeepSpace::Log(double jd, const cs::String& text, GalacticPosition where, int kind)
	{
		auto e = cs::New<DeepSpaceEvent>();
		e->Jd = jd; e->Text = text; e->Where = where; e->Kind = kind;
		events.Add(e);
		if (events.Count() > MaxEvents) events.RemoveRange(0, events.Count() - MaxEvents);
	}

	void DeepSpace::Add(cs::Ref<DeepSpaceObject> o)
	{
		objects.Add(o);
		if (!DeepSpaceFeatures::Transient(o->Kind)) return;
		int transient = 0;
		for (const auto& x : objects)
			if (DeepSpaceFeatures::Transient(x->Kind)) transient++;
		for (int i = 0; i < objects.Count() && transient > MaxObjects; i++)
			if (DeepSpaceFeatures::Transient(objects[i]->Kind))
			{
				objects.RemoveAt(i--);
				transient--;
			}
	}

	void DeepSpace::Remove(cs::Ref<DeepSpaceObject> o) { objects.Remove(o); }

	bool DeepSpace::Hostile(cs::Ref<Faction> a, cs::Ref<Faction> b) { return galaxy->Polities->Diplomacy->Hostile(a, b); }

	void DeepSpace::Destroy(cs::Ref<Cruise> c, cs::Ref<Ship> ship, double jd, const cs::String& how)
	{
		if (!c->Ships.Contains(ship)) return;
		// Craft docked aboard go down with the hull carrying them (U.SPC-T419).
		for (const auto& craft : c->CarriedBy(ship)) Destroy(c, craft, jd, cs::Cat("docked aboard the ", ship->Name));
		int i = c->Ships.IndexOf(ship);
		auto where = c->PositionAt(jd);
		c->Ships.RemoveAt(i);
		c->Hired.Remove(ship);
		c->Aboard.Remove(ship);
		ship->Destroyed = true;
		if (ship->Combat != nullptr) ship->Combat->Hull = 0;
		{
			auto o = cs::New<DeepSpaceObject>();
			o->Kind = DeepSpaceObjectKind::Wreck; o->Name = cs::Cat("the wreck of the ", ship->Name); o->Position = where.Offset(DVec3(i * 3, 0, 0));
			o->SalvageCredits = WreckValue(ship); o->LostBy = ship->Faction; o->SinceJd = jd;
			Add(o);
		}
		Log(jd, cs::Cat(ship->Name, " of ", ship->Faction->Name, " is destroyed in open space ", how), where);
		if (c->Ships.Count() == 0)
		{
			if (ship->Voyage != nullptr) ship->Voyage->LostWithAllHands();
			cruises.Remove(c);
			return;
		}
		if (i == 0)
		{
			// The next hull flying on its own drive takes command (not a craft docked aboard it, U.SPC-T472), and
			// what the group was out here to do; it leads the group from the front of the line.
			auto heir = c->Ships.Find([&c](const cs::Ref<Ship>& s) { return !c->Aboard.ContainsKey(s); });
			if (!heir) heir = c->Ships[0];
			if (c->Ships[0] != heir)
			{
				c->Ships.Remove(heir);
				c->Ships.Insert(0, heir);
			}
			heir->Leader = nullptr;
			heir->Voyage = ship->Voyage;
			heir->Role = ship->Role;
			heir->Ports = ship->Ports;
			for (const auto& s : c->Ships)
				if (s != heir) s->Leader = heir;
			ship->Voyage = nullptr;
		}
	}

	double DeepSpace::WreckValue(cs::Ref<Ship> ship)
	{
		return WreckPartsCredits * (ship->Spec != nullptr && ship->Spec->IsMilitary ? 3 : 1) + (ship->HasCargo ? cs::Math::Max(0.0, ship->CargoCost) * 0.4 : 0);
	}

	double DeepSpace::ClosestKm(cs::Ref<Cruise> a, cs::Ref<Cruise> b, double fromJd, double toJd)
	{
		return Closest(a->PositionAt(fromJd).KmFrom(b->PositionAt(fromJd)), a->PositionAt(toJd).KmFrom(b->PositionAt(toJd)));
	}

	double DeepSpace::ClosestKm(cs::Ref<Cruise> c, GalacticPosition place, double fromJd, double toJd)
	{
		return Closest(c->PositionAt(fromJd).KmFrom(place), c->PositionAt(toJd).KmFrom(place));
	}

	double DeepSpace::Closest(DVec3 p0, DVec3 p1)
	{
		auto d = p1 - p0;
		double len2 = d.LengthSquared();
		double t = len2 > 0 ? cs::Math::Max(0.0, cs::Math::Min(1.0, -DVec3::Dot(p0, d) / len2)) : 0;
		return (p0 + d * t).Length();
	}

	double DeepSpace::Strength(cs::Ref<Cruise> c)
	{
		double s = 0;
		for (const auto& ship : c->Ships)
			if (!ship->Destroyed && ship->Combat != nullptr) s += ship->Combat->Strength();
		return s;
	}

	// ================================================================ FtlInterdiction

	void FtlInterdiction::Step(DeepSpace& space, double fromJd, double toJd, cs::Random& /*rng*/)
	{
		if (space.Cruises().Count() == 0) return;
		for (const auto& c : space.Cruises().ToList())
		{
			if (c->Ships.Count() == 0 || c->Holding || c->Crossing) continue;
			double t0 = cs::Math::Max(fromJd, c->LegJd + c->Profile.SpoolDays), t1 = cs::Math::Min(toJd, c->ArriveJd());
			if (!(t1 > t0)) continue;
			auto p0 = c->PositionAt(t0);
			auto p1 = c->PositionAt(t1);
			// (C#'s list stays null until a field is found: an empty one here.)
			cs::List<GravityField> fields;
			// Not the fields it set out from or is bound into: those are the ends of its way.
			for (const auto& f : space.FieldsAlong(c, p0, p1))
				if (!f.Contains(c->Start, 1.001) && !f.Contains(c->Target, 1.001)) fields.Add(f);
			GravityField hit;
			double along = 0;
			if (fields.Count() == 0 || !FtlRules::FirstEntry(p0, p1, fields, hit, along)) continue;
			auto entry = GalacticPosition::Lerp(p0, p1, along);
			space.Interdict(c, hit, entry, When(c, entry, t0, t1));
		}
	}

	double FtlInterdiction::When(const cs::Ref<Cruise>& c, GalacticPosition at, double t0, double t1)
	{
		double ly = at.DistanceLy(c->Start);
		for (int i = 0; i < 48; i++)
		{
			double mid = (t0 + t1) / 2;
			if (c->Profile.LyAt(mid - c->LegJd) < ly) t0 = mid;
			else t1 = mid;
		}
		return (t0 + t1) / 2;
	}

	// ================================================================ CruiseMeeting

	void CruiseMeeting::Step(DeepSpace& space, double fromJd, double toJd, cs::Random& rng)
	{
		if (space.Cruises().Count() < 2) return;
		// Over the groups as they stood at the start of the step: a fight may take one out of the list.
		auto list = space.Cruises().ToList();
		cs::HashSet<cs::Ref<Cruise>> done;
		for (int i = 0; i < list.Count(); i++)
			for (int j = i + 1; j < list.Count(); j++)
			{
				auto a = list[i];
				auto b = list[j];
				// One fight a step for any group.
				if (done.Contains(a) || done.Contains(b) || a->Ships.Count() == 0 || b->Ships.Count() == 0) continue;
				if (!(a->Engaged(toJd) || a->Engaged(fromJd)) && !(b->Engaged(toJd) || b->Engaged(fromJd))) continue;
				if (!space.Hostile(a->Leader()->Faction, b->Leader()->Faction)) continue;
				double closest = DeepSpace::ClosestKm(a, b, fromJd, toJd);
				if (closest > RangeKm) continue;
				// Inside a nebula sensors see only so far (U.SPC-T421).
				if (closest > cs::Math::Min(space.SensorRangeKm(a->PositionAt(toJd), RangeKm), space.SensorRangeKm(b->PositionAt(toJd), RangeKm))) continue;
				if (!Resolve(space, a, b, toJd, rng)) continue;
				done.Add(a);
				done.Add(b);
			}
	}

	bool CruiseMeeting::Aggressive(const cs::Ref<Ship>& leader) { return leader->Faction->IsPirate || leader->Spec->IsMilitary; }

	bool CruiseMeeting::Loot(const cs::Ref<Cruise>& c)
	{
		for (const auto& s : c->Ships)
			if (s->HasCargo || s->PlunderTonnes() > 0) return true;
		return false;
	}

	bool CruiseMeeting::Resolve(DeepSpace& space, cs::Ref<Cruise> a, cs::Ref<Cruise> b, double jd, cs::Random& rng)
	{
		double sa = DeepSpace::Strength(a), sb = DeepSpace::Strength(b);
		// Who would start it: one with guns and the will; of two, the stronger.
		bool aWill = Aggressive(a->Leader()) && sa > 0 && sa >= sb * NerveRatio && (!a->Leader()->Faction->IsPirate || Loot(b));
		bool bWill = Aggressive(b->Leader()) && sb > 0 && sb >= sa * NerveRatio && (!b->Leader()->Faction->IsPirate || Loot(a));
		if (!aWill && !bWill) return false;
		auto attacker = aWill && (!bWill || sa >= sb) ? a : b;
		auto defender = attacker == a ? b : a;
		double sAtt = attacker == a ? sa : sb, sDef = attacker == a ? sb : sa;
		double win = sDef <= 0 ? 1 : sAtt / (sAtt + sDef);
		bool attackerWins = rng.NextDouble() < win;
		auto where = attacker->PositionAt(jd);
		space.Log(jd, cs::Cat(attacker->Leader()->Name, " of ", attacker->Leader()->Faction->Name, " pulls ", defender->Leader()->Name, " of ", defender->Leader()->Faction->Name, " out of cruise"), where);
		auto winner = attackerWins ? attacker : defender;
		auto loser = attackerWins ? defender : attacker;
		double sLoser = attackerWins ? sDef : sAtt, sWinner = attackerWins ? sAtt : sDef;
		// The winners pay for it.
		double hurt = sLoser + sWinner > 0 ? 0.35 * sLoser / (sLoser + sWinner) : 0;
		for (const auto& s : winner->Ships)
			if (s->Combat != nullptr)
				s->Combat->Hull = cs::Math::Max(cs::Math::Min(s->Combat->Hull, s->Combat->Spec->Hull * 0.05), s->Combat->Hull - s->Combat->Spec->Hull * hurt);
		if (attackerWins && attacker->Leader()->Faction->IsPirate && defender->Leader()->Faction != nullptr && !defender->Leader()->Spec->IsMilitary)
		{
			// Raiders rob a hull they have run down rather than destroy it.
			double tonnes = 0;
			auto robber = attacker->Leader();
			if (robber->Plunder.Count() == 0) robber->Plunder = cs::NewArray<double>(GoodInfo::Count); // robber.Plunder ??= new double[GoodInfo.Count]
			for (const auto& s : defender->Ships)
			{
				if (s->HasCargo)
				{
					robber->Plunder[static_cast<int>(s->CargoGood)] += s->CargoTonnes;
					tonnes += s->CargoTonnes;
					s->HasCargo = false;
					s->CargoTonnes = 0;
					s->CargoCost = 0;
					s->Cargo = cs::String();
				}
				if (s->Plunder.Count() > 0) // s.Plunder != null
					for (int g = 0; g < s->Plunder.Count() && g < robber->Plunder.Count(); g++)
					{
						robber->Plunder[g] += s->Plunder[g];
						tonnes += s->Plunder[g];
						s->Plunder[g] = 0;
					}
			}
			space.Log(jd, cs::Cat(robber->Name, "'s raiders rob ", defender->Leader()->Name, " of ", cs::Fmt(tonnes, "N0"), " t and let it go"), where);
			space.DropOut(defender, jd, FightDays);
		}
		else
		{
			for (const auto& s : loser->Ships.ToList()) space.Destroy(loser, s, jd, cs::Cat("by ", winner->Leader()->Faction->Name));
		}
		if (winner->Ships.Count() > 0) space.DropOut(winner, jd, FightDays);
		return true;
	}

	// ================================================================ WreckSalvage

	void WreckSalvage::Step(DeepSpace& space, double fromJd, double toJd, cs::Random& /*rng*/)
	{
		if (space.Objects().Count() == 0) return;
		for (const auto& c : space.Cruises().ToList())
		{
			if (c->Ships.Count() == 0 || !(c->Engaged(toJd) || c->Engaged(fromJd))) continue;
			auto leader = c->Leader();
			if (leader->Voyage != nullptr && leader->Voyage->Purpose == VoyagePurpose::Campaign) continue;
			for (auto o : space.Objects()) // (each a copy of its Ref: the one stripped is taken out of the list before the break)
			{
				// Only what is left of a hull is stripped (U.SPC-T421: not a rogue planet or a power's relay), and only one seen.
				if (o->Kind != DeepSpaceObjectKind::Wreck && o->Kind != DeepSpaceObjectKind::Derelict) continue;
				if (o->SinceJd >= toJd) continue;
				double closest = DeepSpace::ClosestKm(c, o->Position, fromJd, toJd);
				if (closest > RangeKm || closest > space.SensorRangeKm(o->Position, RangeKm)) continue;
				leader->Credits += o->SalvageCredits;
				leader->Faction->Treasury += o->SalvageCredits;
				space.Remove(o);
				space.Log(toJd, cs::Cat(leader->Name, " of ", leader->Faction->Name, " drops out to strip ", o->Name, ": ",
					cs::Fmt(o->SalvageCredits, "N0"), " cr of salvage"), o->Position);
				space.DropOut(c, toJd, SalvageDays);
				break;
			}
		}
	}
}
