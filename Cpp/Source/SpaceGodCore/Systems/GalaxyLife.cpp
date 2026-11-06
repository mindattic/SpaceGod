// US-T100: the galaxy goes on living with nobody at the controls (see Systems/GalaxyLife.h).

#include "Systems/GalaxyLife.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Economy/Colonisation.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Fleet.h"
#include "Infrastructure/Infrastructure.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
#include "Systems/Polities.h"
#include "Systems/Statecraft.h"
#include "Systems/Territory.h"
#include "World.h"

#include <cctype>
#include <cstdint>
#include <unordered_map>

namespace SpaceGod::Core
{
	namespace
	{
		/// The most reviews one update catches up on: a longer jump is reviewed once at its end.
		constexpr int MaxCatchUp = 13;

		bool Stands(const cs::Ref<Settlement>& s) { return s != nullptr && !s->Fallen && s->Stage != ColonyStage::Ruin && s->Kind != SettlementKind::Haven; }

		/// Owners that build and govern: states and companies (free traders own hulls, clans keep havens).
		bool Builds(const cs::Ref<Faction>& f) { return f != nullptr && !f->IsPirate && (f->Kind == PolityKind::State || f->Kind == PolityKind::Company); }

		/// A fixed hash of a name (FNV-1a), for the choices that must come out the same on every peer.
		std::uint32_t Hash(const cs::String& s, std::uint32_t salt = 0)
		{
			std::uint32_t h = 2166136261u ^ salt;
			for (unsigned char c : s) { h ^= c; h *= 16777619u; }
			return h;
		}

		/// A share of 0..1 from a hash.
		double Unit(std::uint32_t h) { return (h % 10007u) / 10007.0; }

		bool OrbitsHere(StructureKind k) { return k == StructureKind::Depot || k == StructureKind::Relay || k == StructureKind::Habitat || k == StructureKind::Gateway; }

		const char* StationName(StructureKind k)
		{
			switch (k)
			{
				case StructureKind::Depot: return "Depot";
				case StructureKind::Relay: return "Relay";
				case StructureKind::Habitat: return "Habitat";
				default: return "Gateway";
			}
		}

		double StationKm(StructureKind k)
		{
			switch (k)
			{
				case StructureKind::Depot: return 1.4;
				case StructureKind::Relay: return 0.6;
				case StructureKind::Habitat: return 2.2;
				default: return 1.8;
			}
		}

		cs::Ref<StarSystem> SystemOf(Galaxy& galaxy, const cs::Ref<World>& w)
		{
			for (const auto& s : galaxy.Systems)
				if (s->World == w) return s;
			return nullptr;
		}

		double At(const cs::Ref<World>& w, double jd) { return cs::Double::IsNaN(w->Jd) ? jd : cs::Math::Max(jd, w->Jd); }
	}

	cs::Ref<GalaxyLife> GalaxyLife::Of(Galaxy& galaxy)
	{
		auto territory = Territory::Of(galaxy);
		if (territory->Life == nullptr) territory->Life = cs::New<GalaxyLife>();
		return territory->Life;
	}

	cs::Ref<SettlementLife> GalaxyLife::LifeOf(const cs::Ref<Settlement>& s) const
	{
		for (const auto& l : lives)
			if (l->Settlement == s) return l;
		return nullptr;
	}

	// ---------------------------------------------------------------- the review

	void GalaxyLife::Update(Galaxy& galaxy, double jd)
	{
		if (cs::Double::IsNaN(jd)) return;
		if (cs::Double::IsNaN(nextReviewJd))
		{
			// The first look only takes stock: every settlement standing is seen, and its first works scheduled.
			Review(galaxy, jd, true);
			nextReviewJd = cs::Math::Floor(jd) + ReviewDays;
			return;
		}
		if (jd < nextReviewJd) return;
		// Each review runs on its own day mark, whatever the frame or the warp, so the galaxy never depends on the frame rate.
		if (jd - nextReviewJd > MaxCatchUp * ReviewDays) nextReviewJd = cs::Math::Floor(jd);
		while (nextReviewJd <= jd)
		{
			double day = nextReviewJd;
			Review(galaxy, day, false);
			Drift(galaxy, day);
			nextReviewJd = day + ReviewDays;
		}
	}

	void GalaxyLife::Review(Galaxy& galaxy, double jd, bool first)
	{
		cs::List<cs::Ref<SettlementLife>> seen;
		for (const auto& sys : galaxy.Systems)
		{
			auto w = sys->World;
			if (w == nullptr || w->Economy == nullptr) continue;
			// Settlements are founded as the works go on: walk a copy.
			for (const auto& s : w->Economy->Settlements.ToArray())
			{
				if (!Stands(s)) continue;
				auto life = LifeOf(s);
				if (life == nullptr)
				{
					life = cs::New<SettlementLife>();
					life->Settlement = s;
					life->NextWorksJd = cs::Math::Floor(jd) + FirstWorksDays + ReviewDays * (s->Id % 12);
					lives.Add(life);
				}
				seen.Add(life);
				if (!first && jd >= life->NextWorksJd && Builds(s->Owner)) Works(w, life, jd);
			}
		}
		// Forget the settlements that have fallen, failed, or gone with a system the galaxy let go.
		lives = seen;
	}

	void GalaxyLife::Works(const cs::Ref<World>& w, const cs::Ref<SettlementLife>& life, double jd)
	{
		auto s = life->Settlement;
		// One work a year at most, the most wanted first: a moon base for a big world, then the satellites, then a station.
		bool built = MoonBase(w, s, jd) || Satellites(w, s) || Station(w, s);
		// Nothing wanted yet, or no money for it: it looks again in a season.
		life->NextWorksJd = built ? jd + WorksEveryDays : jd + ReviewDays * 3;
	}

	bool GalaxyLife::Satellites(const cs::Ref<World>& w, const cs::Ref<Settlement>& s)
	{
		auto infra = w->Infrastructure;
		auto owner = s->Owner;
		if (infra == nullptr || s->Population < SatellitesAt || owner->Treasury < 2 * SatelliteCost) return false;
		auto b = (*w->Catalog())[s->Body];
		if (b->Kind == BodyKind::Station || b->Kind == BodyKind::Star || b->Kind == BodyKind::Barycentre || b->HabitatSite || b->RadiusKm <= 0) return false;
		cs::Ref<Constellation> net = nullptr;
		for (const auto& c : infra->Constellations)
			if (c->Body == s->Body) { net = c; break; }
		double r = b->RadiusKm;
		std::uint32_t h = Hash(s->Name, 0x5A7u);
		if (net == nullptr)
		{
			net = cs::New<Constellation>();
			net->Name = cs::Cat(s->Name, " Net");
			net->Body = s->Body;
			net->Color = Rgb(cs::Math::Min(1.0f, owner->Color.R * 0.6f + 0.4f), cs::Math::Min(1.0f, owner->Color.G * 0.6f + 0.4f), cs::Math::Min(1.0f, owner->Color.B * 0.6f + 0.4f));
			// A navigation and relay net in low orbit: four planes of six, inclined so they see the poles.
			net->Shells.Add(Constellation::Shell(r * 1.6 + 300, 48 + 10 * Unit(h), 4, 6));
			infra->Constellations.Add(net);
			owner->Treasury -= SatelliteCost;
			SatelliteNetworks++;
			w->Log(At(w, w->Economy->Jd), cs::Cat("The ", owner->Name, " put up ", net->Name, ", ", net->Count(), " satellites over ", b->Name), WorldEventKind::Economy, s->Body);
			return true;
		}
		// A world of a million people gets a second, wider shell over its own (once).
		if (s->Population < SecondShellAt || net->Shells.Count() >= 2) return false;
		net->Shells.Add(Constellation::Shell(r * 3.2 + 600, 70 + 15 * Unit(h >> 7), 6, 8));
		owner->Treasury -= SatelliteCost;
		SatelliteNetworks++;
		w->Log(At(w, w->Economy->Jd), cs::Cat(net->Name, " over ", b->Name, " grows to ", net->Count(), " satellites"), WorldEventKind::Economy, s->Body);
		return true;
	}

	bool GalaxyLife::Station(const cs::Ref<World>& w, const cs::Ref<Settlement>& s)
	{
		auto infra = w->Infrastructure;
		auto owner = s->Owner;
		if (infra == nullptr || s->Population < StationsFrom || owner->Treasury < 2 * StationCost) return false;
		int wanted = cs::Math::Min(MaxStations, static_cast<int>(cs::Math::Floor(cs::Math::Log10(s->Population / StationsFrom))) + 1);
		cs::List<StructureKind> have;
		int onBody = 0;
		for (const auto& st : infra->Structures)
		{
			if (st->Body == s->Body && !st->AtLagrange()) onBody++;
			if (st->Settlement == s && OrbitsHere(st->Kind)) have.Add(st->Kind);
		}
		if (have.Count() >= wanted) return false;
		// The next kind it lacks, in the order a growing port wants them.
		StructureKind kind = StructureKind::Gateway;
		for (StructureKind k : { StructureKind::Depot, StructureKind::Relay, StructureKind::Habitat, StructureKind::Gateway })
			if (!have.Contains(k)) { kind = k; break; }
		auto b = (*w->Catalog())[s->Body];
		if (b->RadiusKm <= 0) return false;
		// Low, near-equatorial orbits a layer above what is there already (a free station's works in a loose cluster about it),
		// as the settlements' first works were laid out (Infrastructure.AddSettlementStructures).
		double radii = b->Kind == BodyKind::Station ? 3.0 + onBody * 0.9 : 1.32 + onBody * 0.11;
		std::uint32_t h = Hash(s->Name, static_cast<std::uint32_t>(kind) * 7919u + 1u);
		auto st = cs::New<OrbitalStructure>();
		st->Name = cs::Cat(s->Name, " ", StationName(kind));
		st->Kind = kind;
		st->Body = s->Body;
		st->Orbit = Infrastructure::EquatorialOrbit(b, b->RadiusKm * radii, Unit(h) * 12 - 6, Unit(h >> 5) * 360, Unit(h >> 11) * 360, w->Economy->Jd);
		st->SetOwner(owner);
		st->Settlement = s;
		st->Industry = IndustryKind::Farm;
		st->LengthKm = StationKm(kind);
		st->Note = kind == StructureKind::Depot ? "Propellant depot, built as the port grew (game layer)"
			: kind == StructureKind::Relay ? "Communications relay, built as the port grew (game layer)"
			: kind == StructureKind::Habitat ? "Orbital habitat, built as the port grew (game layer)"
			: "Transfer station, built as the port grew (game layer)";
		infra->Structures.Add(st);
		owner->Treasury -= StationCost;
		StationsBuilt++;
		w->Log(At(w, w->Economy->Jd), cs::Cat("The ", owner->Name, " open ", st->Name, " in orbit of ", b->Name), WorldEventKind::Economy, s->Body);
		return true;
	}

	bool GalaxyLife::MoonBase(const cs::Ref<World>& w, const cs::Ref<Settlement>& s, double jd)
	{
		auto c = w->Colonisation;
		auto economy = w->Economy;
		auto owner = s->Owner;
		if (c == nullptr || s->Population < MoonBaseAt || owner->Treasury < 2 * MoonBaseCost) return false;
		auto catalog = w->Catalog();
		auto b = (*catalog)[s->Body];
		if (b->Kind != BodyKind::Planet && b->Kind != BodyKind::DwarfPlanet) return false;
		// The best free moon of its world that people can live on (water, or ice to melt), the lowest body first on a tie.
		cs::Ref<ColonySite> best = nullptr;
		double at = At(w, cs::Math::Max(jd, economy->Jd));
		for (const auto& site : c->Sites)
		{
			if (site == nullptr || site->Body < 0 || site->Body >= catalog->Count()) continue;
			auto m = (*catalog)[site->Body];
			if (m->ParentIndex != s->Body || m->Kind != BodyKind::Moon || !site->Viable()) continue;
			if (Colonisation::Occupied(economy, site->Body, at) || c->Bound(site->Body)) continue;
			if (best == nullptr || site->Score() > best->Score()) best = site;
		}
		if (best == nullptr) return false;
		double people = cs::Math::Min(MoonBaseParty, s->Population * MoonBaseShare);
		s->Population -= people;
		auto base = Colonisation::Found(economy, owner, best, people, at);
		if (base->Name == cs::Cat(best->Name, " Landing") && !best->Habitat) base->Name = cs::Cat(best->Name, " Base");
		// Its starter goods come up from the world below: as much of what it needs as the port can spare.
		for (int i = 0; i < GoodInfo::Count; i++)
		{
			double give = cs::Math::Min(cs::Math::Max(0.0, s->Stock[i]) * 0.1, base->Target(static_cast<Good>(i)));
			if (give <= 0) continue;
			s->Stock[i] -= give;
			base->Stock[i] += give;
		}
		economy->Open(base);
		owner->Treasury -= MoonBaseCost;
		MoonBases++;
		w->Log(at, cs::Cat(s->Name, " sends ", cs::Fmt(people, "N0"), " people up to found ", base->Name, " on its moon ", (*catalog)[best->Body]->Name), WorldEventKind::Economy, best->Body);
		return true;
	}

	// ---------------------------------------------------------------- secession

	void GalaxyLife::Drift(Galaxy& galaxy, double jd)
	{
		auto polities = galaxy.Polities;
		if (polities == nullptr) return;
		auto player = polities->Statecraft != nullptr ? polities->Statecraft->Player() : nullptr;
		cs::List<std::pair<cs::Ref<StarSystem>, cs::Ref<Settlement>>> ready;
		for (const auto& sys : galaxy.Systems)
		{
			auto w = sys->World;
			if (w == nullptr || w->Economy == nullptr) continue;
			// Each owner's biggest settlement in the system speaks for its holdings there.
			std::unordered_map<const Faction*, cs::Ref<Settlement>> biggest;
			for (const auto& s : w->Economy->Settlements)
			{
				if (!Stands(s) || s->Owner == nullptr) continue;
				auto& b = biggest[s->Owner.get()];
				if (b == nullptr || s->Population > b->Population) b = s;
			}
			for (const auto& s : w->Economy->Settlements)
			{
				auto life = LifeOf(s);
				if (life == nullptr) continue;
				auto f = s->Owner;
				double rate = 0;
				// The player's own polity keeps its colonies (it answers to the player); companies and clans have no state to leave.
				if (f != nullptr && f->Kind == PolityKind::State && !f->IsPirate && f != player && biggest[f.get()] == s && s->OccupiedFrom == nullptr
					&& s->Population >= SecedePopulation)
				{
					auto capital = polities->CapitalOf(f);
					auto home = capital != nullptr ? polities->WorldOf(capital) : nullptr;
					auto homeSystem = home != nullptr && home != w ? SystemOf(galaxy, home) : nullptr;
					double ly = homeSystem != nullptr ? Galaxy::DistanceLy(homeSystem, sys) : 0;
					if (ly >= SecedeLy)
					{
						// The farther and the more restless, the faster; a settlement there from the start remembers old loyalties.
						rate = (ly / SecedeLy) * (1 + 3 * cs::Math::Max(0.0, s->Unrest)) / (SecedeYears * 365.25);
						if (!Colonisation::IsFounded(s)) rate *= 0.5;
					}
				}
				// A colony brought close to home again (its owner's seat moved, or it was retaken) slowly settles back.
				life->Yearning = rate > 0 ? life->Yearning + rate * ReviewDays : cs::Math::Max(0.0, life->Yearning - ReviewDays / 365.25 * 0.1);
				if (life->Yearning >= 1) ready.Add(std::make_pair(sys, s));
			}
		}
		for (const auto& [sys, s] : ready)
			if (Secede(galaxy, sys, s, jd) != nullptr)
				if (auto life = LifeOf(s)) life->Yearning = 0;
	}

	cs::Ref<Faction> GalaxyLife::Secede(Galaxy& galaxy, const cs::Ref<StarSystem>& system, const cs::Ref<Settlement>& s, double jd)
	{
		auto polities = galaxy.Polities;
		auto old = s != nullptr ? s->Owner : nullptr;
		auto w = system != nullptr ? system->World : nullptr;
		if (old == nullptr || old->IsPirate || w == nullptr || w->Economy == nullptr || polities == nullptr) return nullptr;
		// What goes: every standing settlement the old owner holds in this system; it must keep something elsewhere.
		cs::List<cs::Ref<Settlement>> going;
		double here = 0;
		for (const auto& x : w->Economy->Settlements)
			if (Stands(x) && x->Owner == old)
			{
				going.Add(x);
				here += x->Population;
			}
		double total = 0;
		for (const auto& x : polities->SettlementsOf(old))
			if (Stands(x)) total += x->Population;
		if (going.Count() == 0 || total <= here) return nullptr;

		// A name of its own, after its system: "Ross 128 Free State", numbered if it is taken.
		static const char* const Kinds[] = { "Free State", "Republic", "Commonwealth", "Free Colonies", "Compact" };
		std::uint32_t h = Hash(s->Name, 0xF4EEu);
		cs::String kind = Kinds[h % 5];
		cs::String name = cs::Cat(system->Name, " ", kind);
		for (int n = 2; polities->Find(name) != nullptr; n++) name = cs::Cat(system->Name, " ", kind, " ", n);
		cs::String prefix;
		bool start = true;
		for (unsigned char ch : system->Name)
		{
			if (start && std::isalpha(ch) && prefix.size() < 2) prefix += static_cast<char>(std::toupper(ch));
			start = ch == ' ' || ch == '-';
		}
		if (prefix.empty()) prefix = "X";
		prefix += kind[0];
		// Its colours turned from the old owner's, so the map tells them apart.
		auto c = old->Color;
		Rgb color(cs::Math::Min(1.0f, c.G * 0.75f + 0.2f), cs::Math::Min(1.0f, c.B * 0.75f + 0.2f), cs::Math::Min(1.0f, c.R * 0.75f + 0.2f));

		double share = here / total;
		double purse = cs::Math::Max(0.0, old->Treasury) * share * 0.5;
		old->Treasury -= purse;
		auto nf = w->Polity(name, prefix, color, PolityKind::State, purse, old->MarineQuality);
		nf->IsMilitary = true;
		nf->CrewMorale = 1;
		if (!cs::Double::IsPositiveInfinity(old->Manpower))
		{
			double hands = cs::Math::Max(0.0, old->Manpower) * share;
			old->Manpower -= hands;
			nf->Manpower = hands;
		}
		nf->Capital = s;
		cs::HashSet<int> bodies;
		for (const auto& x : going)
		{
			x->Owner = nf;
			x->OccupiedFrom = nullptr;
			x->Garrison = cs::Double::NaN;
			// Free at last: the restless calm down.
			x->Unrest = cs::Math::Max(0.0, x->Unrest - 0.2);
			bodies.Add(x->Body);
		}
		// The civilian hulls homed in its ports go with it; the navy stays loyal.
		if (w->Fleet != nullptr)
			for (const auto& ship : w->Fleet->Ships())
			{
				if (ship->Destroyed || ship->Faction != old || (ship->Spec != nullptr && ship->Spec->IsMilitary)) continue;
				const auto& route = ship->Controller()->Route;
				if (route.Count() > 0 && bodies.Contains(route[0])) ship->Faction = nf;
			}
		if (auto statecraft = polities->Statecraft)
		{
			statecraft->Sway(old, nf, -40);
			statecraft->Sway(nf, old, -25);
		}
		auto record = cs::New<Secession>();
		record->System = system;
		record->Settlement = s;
		record->From = old;
		record->To = nf;
		record->Jd = jd;
		Secessions.Add(record);
		// Diplomacy is notable news: the relays carry it out through the gates (SystemReport::Notable).
		w->Log(At(w, jd), cs::Cat(s->Name, " declares its independence from the ", old->Name, ": its people in ", system->Name, " are the ", nf->Name, " now"),
			WorldEventKind::Diplomacy, -1);
		return nf;
	}

	// ================================================================ ObjectGraph registrations (docs/PERSISTENCE.md)

	SG_SERIALIZE(SettlementLife, "SpaceGod.Core.SettlementLife",
		SG_F(Settlement), SG_F(NextWorksJd), SG_F(Yearning))

	SG_SERIALIZE(Secession, "SpaceGod.Core.Secession",
		SG_F(System), SG_F(Settlement), SG_F(From), SG_F(To), SG_F(Jd))

	SG_SERIALIZE(GalaxyLife, "SpaceGod.Core.GalaxyLife",
		SG_F(Secessions), SG_F(StationsBuilt), SG_F(SatelliteNetworks), SG_F(MoonBases), SG_F(lives), SG_F(nextReviewJd))
}
