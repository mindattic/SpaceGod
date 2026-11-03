// Port of Assets/UnitySpace/Core/Systems/Galaxy.cs (US-T17).

#include "Systems/Galaxy.h"
#include "Systems/AlphaCentauri.h"
#include "Systems/StarSystemBuilder.h"
#include "Systems/StarSystemSpec.h"
#include "Systems/NearbySystems.h"
#include "Systems/NearbySocieties.h"
#include "Systems/SocietySpec.h"
#include "Systems/JumpGate.h"
#include "Systems/GateNetwork.h"
#include "Systems/Polities.h"
#include "Systems/DeepSpace.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/GateConstruction.h"
#include "Systems/InterstellarColonisation.h"
#include "Systems/InterstellarWars.h"
#include "Systems/StrategicCommand.h"
#include "Systems/Statecraft.h"
#include "Systems/VoidBaseBuilder.h"
#include "Combat/GatePickets.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Economy/Colonisation.h"
#include "Astro.h"
#include "BodyCatalog.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "OrbitalElements.h"
#include "World.h"

namespace SpaceGod::Core
{
	// ================================================================ StarSystem

	cs::Ref<Hyperlanes> StarSystem::Lanes() const { return World->Fleet->Lanes; }

	void StarSystem::AddGate(const cs::Ref<JumpGate>& gate)
	{
		gates.Add(gate);
		// Its world's raiders can lie in wait off it (U.SPC-T94).
		World->Gates.Add(gate);
		// And its hyperlane network leads on to it, by a spur a pilot can ride (U.SPC-T379).
		if (World->Fleet && World->Fleet->Lanes) World->Fleet->Lanes->AddGateSpur(gate, Ephemeris->Jd);
	}

	cs::Ref<JumpGate> StarSystem::GateTo(const cs::Ref<StarSystem>& other) const
	{
		for (const auto& g : gates)
			if (g->Destination() == other) return g;
		return nullptr;
	}

	cs::Ref<JumpGate> StarSystem::GateInRange(DVec3 helioKm) const
	{
		cs::Ref<JumpGate> best = nullptr;
		double bestKm = cs::Double::MaxValue;
		for (const auto& g : gates)
		{
			double km = (helioKm - g->PositionKm).Length();
			if (g->CanActivate(helioKm) && km < bestKm)
			{
				bestKm = km;
				best = g;
			}
		}
		return best;
	}

	bool StarSystem::OpenToSettlement() const
	{
		if (IsVoidBase || Settled()) return false;
		for (const auto& g : gates)
			if (g->Link != nullptr && g->Link->Working()) return true;
		return false;
	}

	bool StarSystem::Settled() const
	{
		// A ruin is nobody's to trade with (U.SPC-T298).
		for (const auto& s : World->Economy->Settlements)
			if (s->Kind != SettlementKind::Haven && !s->Fallen && s->Stage != ColonyStage::Ruin) return true;
		return false;
	}

	StarSystem::StarSystem(const cs::String& name, DVec3 positionLy, const cs::Ref<SpaceGod::Core::Ephemeris>& ephemeris, const cs::Ref<SpaceGod::Core::World>& world, const cs::Ref<SystemSpec>& spec, const cs::List<cs::Ref<DebrisDisc>>& discs, double overviewSpanKm,
		bool isVoidBase)
	{
		Name = name;
		PositionLy = positionLy;
		Origin = GalacticPosition::FromLy(positionLy);
		Catalog = ephemeris->Catalog;
		Ephemeris = ephemeris;
		World = world;
		Spec = spec;
		Discs = discs;
		OverviewSpanKm = overviewSpanKm;
		IsVoidBase = isVoidBase;
		double extent = 0;
		for (int i = 0; i < Catalog->Count(); i++) extent = cs::Math::Max(extent, ephemeris->Helio(i).Length());
		for (const auto& d : discs) extent = cs::Math::Max(extent, ephemeris->Helio(d->HostIndex).Length() + d->OuterKm);
		ExtentKm = extent;
	}

	void StarSystem::Update(double jd)
	{
		Ephemeris->Update(jd);
		World->Update(jd);
	}

	cs::Ref<Faction> StarSystem::Holder() const { return World->Holder(); }

	cs::Ref<StarSystem> StarSystem::CreateSol(double jd, const cs::Ref<Polities>& polities)
	{
		auto eph = cs::New<SpaceGod::Core::Ephemeris>(BodyCatalog::CreateSolSystem());
		eph->Update(jd);
		return cs::New<StarSystem>("Sol System", DVec3::Zero, eph, SpaceGod::Core::World::CreateDefault(eph, jd, 1977, polities), nullptr, cs::List<cs::Ref<DebrisDisc>>(), 2.2 * Astro::AuKm);
	}

	cs::Ref<StarSystem> StarSystem::CreateVoidBase(const cs::String& name, DVec3 positionLy, double jd, const cs::Ref<Polities>& polities)
	{
		auto catalog = VoidBaseBuilder::Build(name);
		auto eph = cs::New<SpaceGod::Core::Ephemeris>(catalog);
		eph->Update(jd);
		auto world = SpaceGod::Core::World::CreateUnsettled(eph, jd, SeedOf(name), polities);
		return cs::New<StarSystem>(name, positionLy, eph, world, nullptr, cs::List<cs::Ref<DebrisDisc>>(), VoidBaseBuilder::OverviewSpanKm, true);
	}

	cs::Ref<StarSystem> StarSystem::CreateAlphaCentauri(double jd, const cs::Ref<Polities>& polities)
	{
		return Create(AlphaCentauri::Spec(), jd, [polities](cs::Ref<SpaceGod::Core::Ephemeris> eph, double at, int seed) { return SpaceGod::Core::World::CreateAlphaCentauri(eph, at, seed, polities); }, polities);
	}

	cs::Ref<StarSystem> StarSystem::CreateSettled(const cs::Ref<SystemSpec>& spec, double jd, const cs::Ref<Polities>& polities)
	{
		auto society = NearbySocieties::For(spec);
		if (society == nullptr) return Create(spec, jd, nullptr, polities);
		society->AddStations(spec);
		return Create(spec, jd, [society, polities](cs::Ref<SpaceGod::Core::Ephemeris> eph, double at, int seed) { return SpaceGod::Core::World::CreateSettled(eph, at, seed, society, polities); }, polities);
	}

	cs::Ref<StarSystem> StarSystem::Create(const cs::Ref<SystemSpec>& spec, double jd, cs::Func<cs::Ref<SpaceGod::Core::World>, cs::Ref<SpaceGod::Core::Ephemeris>, double, int> world, const cs::Ref<Polities>& polities)
	{
		auto built = StarSystemBuilder::Build(spec);
		auto catalog = cs::New<BodyCatalog>(built->Bodies);
		for (const auto& d : built->Discs) d->HostIndex = (*catalog)[d->HostName]->Index;
		auto eph = cs::New<SpaceGod::Core::Ephemeris>(catalog);
		eph->Update(jd);
		auto w = world != nullptr ? world(eph, jd, SeedOf(spec->Name)) : SpaceGod::Core::World::CreateUnsettled(eph, jd, SeedOf(spec->Name), polities);
		double span = cs::Double::IsNaN(spec->OverviewSpanAu) ? OverviewSpan(catalog) : spec->OverviewSpanAu * Astro::AuKm;
		return cs::New<StarSystem>(spec->Name, spec->PositionLy(), eph, w, spec, built->Discs, span);
	}

	int StarSystem::SeedOf(const cs::String& name)
	{
		if (name == "Alpha Centauri") return 4243;
		int h = 17;
		// unchecked int arithmetic over the name's UTF-16 chars, as C#.
		cs::ForEachUtf16(name, [&](uint32_t c) { h = cs::WrapAdd(cs::WrapMul(h, 31), static_cast<int>(c)); });
		return h & 0x7fffffff;
	}

	double StarSystem::OverviewSpan(const cs::Ref<BodyCatalog>& catalog)
	{
		double span = 0;
		for (const auto& b : catalog->Bodies())
		{
			if (b->ParentIndex < 0 || b->Kind == BodyKind::Station || !(*catalog)[b->ParentIndex]->UsesStellarMapping()) continue;
			double apo = b->Elements->AKm * cs::Math::Abs(b->OrbitScale) * (1 + b->Elements->E);
			if (apo < 200 * Astro::AuKm) span = cs::Math::Max(span, apo);
		}
		return cs::Math::Max(span * 1.15, 0.12 * Astro::AuKm);
	}

	// ================================================================ NearbyStar

	NearbyStar::NearbyStar(const cs::String& name, double raDeg, double decDeg, double distanceLy, const cs::String& spectralType, double magnitude, double teffK)
		: NearbyStar(name, Astro::DirectionFromRaDec(raDeg, decDeg) * distanceLy, spectralType, magnitude, teffK)
	{
	}

	NearbyStar::NearbyStar(const cs::String& name, DVec3 positionLy, const cs::String& spectralType, double magnitude, double teffK)
	{
		Name = name;
		PositionLy = positionLy;
		SpectralType = spectralType;
		ApparentMagnitude = magnitude;
		TeffK = teffK;
		Color = AlphaCentauri::StarColor(teffK);
	}

	// ================================================================ Galaxy

	Galaxy::Galaxy()
		: Polities(cs::New<SpaceGod::Core::Polities>())
	{
		DeepSpace = cs::New<SpaceGod::Core::DeepSpace>(Self());
		Polities->Command->Galaxy = Self();
		Polities->Statecraft->Galaxy = Self();
	}

	void Galaxy::Update(double jd)
	{
		// The galaxy charted and built about the pilot as it moves (U.SPC-T413).
		ExploreIfMoved(jd);
		for (const auto& s : Systems)
		{
			if (AtFullDetail(s))
			{
				s->Update(jd);
				s->World->SetCoarse(false);
			}
			else
			{
				s->World->SetCoarse(true);
				double mark = CoarseMark(jd);
				if (!(mark <= s->World->Jd)) s->Update(mark);
			}
		}
		if (Traffic) Traffic->Update(jd);
		// Between the stars: groups under their own drives fly on, meet, find wrecks and drop out at their stars.
		DeepSpace->Update(jd);
		// The pilot and the groups out there find the undiscovered stars they come near (U.SPC-T456).
		LookForStars(jd);
		if (Construction) Construction->Update(jd);
		if (Wars) Wars->Update(jd);
		// The polities' standing directives: research accrues, and groups given objectives elsewhere are sent (U.SPC-T423).
		Polities->Command->Update(jd);
		// The diplomacy game: opinions, pacts kept, allies called to arms, and the AI's proposals (U.SPC-T427).
		Polities->Statecraft->Update(jd);
	}

	void Galaxy::AddSystem(const cs::Ref<StarSystem>& system, double jd)
	{
		if (system == nullptr || Systems.Contains(system)) return;
		Systems.Add(system);
		cs::String shown = system->Spec != nullptr && !system->Spec->NeighbourName.empty() ? system->Spec->NeighbourName : system->Name;
		NearbyStars.RemoveAll([&](const NearbyStar& n) { return cs::Strings::EqualsOrdinalIgnoreCase(n.Name, shown); });
		// A system that runs is known (U.SPC-T456): one still undiscovered is found by being reached.
		Unhide(shown);
		system->World->SetCoarse(!AtFullDetail(system));
		if (Traffic) Traffic->Register(system);
		SystemAdded.Invoke(system, jd);
	}

	void Galaxy::Promote(const cs::Ref<StarSystem>& system, double jd)
	{
		Focus = system;
		if (system == nullptr) return;
		if (!(jd <= system->World->Jd)) system->Update(jd);
		system->World->SetCoarse(false);
	}

	cs::Ref<StarSystem> Galaxy::Find(const cs::String& name) const
	{
		for (const auto& s : Systems)
			if (cs::Strings::EqualsOrdinalIgnoreCase(s->Name, name)) return s;
		return nullptr;
	}

	double Galaxy::GateDistanceKm(const cs::Ref<StarSystem>& s) { return s->Catalog->OuterEdgeKm() + JumpGate::RadiusKm; }

	cs::Ref<Galaxy> Galaxy::CreateDefault(double jd, int seed)
	{
		auto g = cs::New<Galaxy>();
		g->Seed = seed;
		// Sol first: the Federation is founded there, with its capital on Earth, before its frontier joins it.
		auto sol = StarSystem::CreateSol(jd, g->Polities);
		auto alphaCentauri = StarSystem::CreateAlphaCentauri(jd, g->Polities);
		g->Systems.Add(sol);
		g->Systems.Add(alphaCentauri);
		for (const auto& spec : NearbySystems::All()) g->Systems.Add(StarSystem::CreateSettled(spec, jd, g->Polities));
		cs::HashSet<cs::NoCase> simulated;
		for (const auto& s : g->Systems)
			if (s->Spec != nullptr && !s->Spec->NeighbourName.empty()) simulated.Add(s->Spec->NeighbourName);
		for (const auto& n : Neighbours())
			if (!simulated.Contains(n.Name)) g->NearbyStars.Add(n);
		// The rest of the real neighbourhood out to 100 ly, and the void stations between: charted within 21 ly of Sol,
		// undiscovered past it until someone comes near (U.SPC-T456).
		g->SetUpNeighbourhood();
		g->Connect(jd);
		// The space between is not empty (U.SPC-T421): every route flown is seeded with what lies along it, and the
		// routes between the running systems, where the powers' relays and listening posts stand, from the start.
		g->DeepSpace->RouteLife = true;
		g->DeepSpace->SeedSystemRoutes();
		// The powers treat with each other (and with the player) of their own accord (U.SPC-T427).
		g->Polities->Statecraft->AutoDiplomacy = true;
		return g;
	}

	void Galaxy::Connect(double jd, const cs::Ref<GateNetwork>& network)
	{
		Network = network ? network : GateNetwork::Build(Systems, jd, Polities);
		Network->Fortify();
		// And watches them: a picket at each held gate's exit (U.SPC-T480).
		GatePickets::PostAll(Network, jd);
		Traffic = cs::New<InterstellarTraffic>(Self(), jd);
		auto traffic = Traffic;
		Polities->InTransit = [traffic]() { return traffic->InTransit(); };
		Construction = cs::New<GateConstruction>(Self(), jd);
		Wars = cs::New<InterstellarWars>(Self(), jd);
		Colonisation = cs::New<InterstellarColonisation>(Self());
	}

	cs::List<NearbyStar> Galaxy::Neighbours()
	{
		auto S = [](const char* name, double rh, double rm, double rs, double dd, double dm, double ds, bool south, double ly, const char* type, double mag, double teff)
		{
			return NearbyStar(name, Astro::Hms(rh, rm, rs), Astro::Dms(dd, dm, ds, south), ly, type, mag, teff);
		};

		cs::List<NearbyStar> r;
		r.Add(S("Barnard's Star", 17, 57, 48.5, 4, 41, 36, false, 5.9629, "M4.0Ve", 9.53, 3195));
		r.Add(S("Luhman 16", 10, 49, 18.9, 53, 19, 10, true, 6.5102, "L8 + T1", cs::Double::NaN, 1300));
		r.Add(S("WISE 0855−0714", 8, 55, 10.8, 7, 14, 43, true, 7.430, "Y4", cs::Double::NaN, 285));
		r.Add(S("Wolf 359", 10, 56, 29.2, 7, 0, 53, false, 7.856, "M6.0V", 13.44, 2800));
		r.Add(S("Lalande 21185", 11, 3, 20.2, 35, 58, 12, false, 8.3044, "M2.0V", 7.47, 3550));
		r.Add(S("Sirius", 6, 45, 8.9, 16, 42, 58, true, 8.599, "A1V + DA2", -1.46, 9940));
		r.Add(S("Gliese 65", 1, 39, 1.3, 17, 57, 1, true, 8.770, "M5.5Ve", 12.54, 2840));
		r.Add(S("Ross 154", 18, 49, 49.4, 23, 50, 10, true, 9.7063, "M3.5Ve", 10.43, 3250));
		r.Add(S("Ross 248", 23, 41, 54.7, 44, 10, 30, false, 10.3057, "M5.5Ve", 12.29, 2800));
		r.Add(S("Epsilon Eridani", 3, 32, 55.8, 9, 27, 30, true, 10.4749, "K2V", 3.73, 5084));
		r.Add(S("Gliese 887", 23, 5, 52.0, 35, 51, 11, true, 10.7241, "M0.5V", 7.34, 3690));
		r.Add(S("Ross 128", 11, 47, 44.4, 0, 48, 16, false, 11.0074, "M4.0Vn", 11.13, 3190));
		r.Add(S("EZ Aquarii", 22, 38, 33.4, 15, 17, 57, true, 11.109, "M5.0Ve", 13.33, 2900));
		r.Add(S("61 Cygni", 21, 6, 53.9, 38, 44, 58, false, 11.4039, "K5.0V + K7.0V", 5.21, 4526));
		r.Add(S("Procyon", 7, 39, 18.1, 5, 13, 30, false, 11.463, "F5IV–V + DQZ", 0.38, 6530));
		r.Add(S("Struve 2398", 18, 42, 46.7, 59, 37, 49, false, 11.4908, "M3.0V + M3.5V", 8.90, 3400));
		r.Add(S("Groombridge 34", 0, 18, 22.9, 44, 1, 23, false, 11.6191, "M1.5V + M3.5V", 8.08, 3700));
		r.Add(S("DX Cancri", 8, 29, 49.5, 26, 46, 37, false, 11.6797, "M6.5Ve", 14.78, 2700));
		r.Add(S("Epsilon Indi", 22, 3, 21.7, 56, 47, 10, true, 11.8670, "K5Ve + T1 + T6", 4.69, 4630));
		r.Add(S("Tau Ceti", 1, 44, 4.1, 15, 56, 15, true, 11.9118, "G8.5Vp", 3.49, 5344));
		r.Add(S("GJ 1061", 3, 35, 59.7, 44, 30, 45, true, 11.9839, "M5.5V", 13.09, 2950));
		return r;
	}
}
