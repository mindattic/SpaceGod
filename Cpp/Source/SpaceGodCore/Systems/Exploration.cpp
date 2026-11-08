// US-T260: exploration rewards (see Systems/Exploration.h).

#include "Systems/Exploration.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"
#include "Comms/Pilots.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Embodiment/Influence.h"
#include "Events/EventDirector.h"
#include "Events/Opportunity.h"
#include "Persistence/ObjectGraph.h"
#include "Systems/DeepSpace.h"
#include "Systems/Galaxy.h"
#include "Systems/Graveyards.h"
#include "Systems/Polities.h"
#include "Systems/StarDiscovery.h"
#include "Systems/Territory.h"
#include "Systems/VoidSites.h"

namespace SpaceGod::Core
{
	const char* ToString(SurveyKind v)
	{
		switch (v)
		{
		case SurveyKind::Star: return "Star";
		case SurveyKind::Body: return "Body";
		case SurveyKind::Sight: return "Sight";
		case SurveyKind::Anomaly: return "Anomaly";
		case SurveyKind::Graveyard: return "Graveyard";
		}
		return "?";
	}

	namespace
	{
		cs::String ChartKey(const cs::Ref<Faction>& f, const cs::String& key) { return cs::Cat(f != nullptr ? f->Name : cs::String(), "|", key); }

		/// What a void station's sight is worth on top of a body's: the rarer the oddity, the more.
		double SightValue(VoidSight s)
		{
			switch (s)
			{
			case VoidSight::DerelictArk: return 60000;
			case VoidSight::StoneArch: return 45000;
			case VoidSight::Lantern: return 35000;
			case VoidSight::GlassMoonlet: return 30000;
			case VoidSight::FrozenFountain: return 25000;
			default: return 20000;
			}
		}

		const char* SightName(VoidSight s)
		{
			switch (s)
			{
			case VoidSight::DerelictArk: return "derelict ark";
			case VoidSight::StoneArch: return "stone arch";
			case VoidSight::Lantern: return "lantern rock";
			case VoidSight::GlassMoonlet: return "glass moonlet";
			case VoidSight::FrozenFountain: return "frozen fountain";
			default: return "sight";
			}
		}

		/// What a body is worth to survey, by what it is (0: not something a survey is sold for).
		double BodyValue(const cs::Ref<BodyDefinition>& b)
		{
			switch (b->Kind)
			{
			case BodyKind::Planet: return 20000;
			case BodyKind::DwarfPlanet: return 12000;
			case BodyKind::Moon: return 8000;
			case BodyKind::Asteroid: return 4000;
			default: return 0;
			}
		}
	}

	// ================================================================ Exploration

	cs::Ref<Exploration> Exploration::Of(Galaxy& galaxy)
	{
		auto t = Territory::Of(galaxy);
		if (t->Exploration == nullptr) t->Exploration = cs::New<Exploration>();
		return t->Exploration;
	}

	cs::Ref<Exploration> Exploration::Find(const Galaxy& galaxy)
	{
		return galaxy.Territory != nullptr ? galaxy.Territory->Exploration : nullptr;
	}

	bool Exploration::ChartedFor(const cs::Ref<Faction>& f, const cs::String& key) const
	{
		return f != nullptr && Charted.ContainsKey(ChartKey(f, key));
	}

	int Exploration::ChartedCount(const cs::Ref<Faction>& f) const
	{
		if (f == nullptr) return 0;
		cs::String prefix = cs::Cat(f->Name, "|");
		int n = 0;
		for (const auto& kv : Charted)
			if (cs::Strings::StartsWith(kv.first, prefix)) n++;
		return n;
	}

	double Exploration::BaseValue(SurveyKind kind)
	{
		switch (kind)
		{
		case SurveyKind::Star: return 50000;
		case SurveyKind::Body: return 10000;
		case SurveyKind::Sight: return 30000;
		case SurveyKind::Anomaly: return 25000;
		case SurveyKind::Graveyard: return 30000;
		}
		return 10000;
	}

	double Exploration::DistanceFactor(double distanceLy)
	{
		double beyond = cs::Math::Max(0.0, distanceLy - StarDiscovery::KnownToLy);
		return 1 + beyond / 7.0;
	}

	cs::Ref<Ship> Exploration::EmbodiedHull(const Galaxy& galaxy, cs::Ref<World>& world, cs::Ref<StarSystem>& system)
	{
		world = nullptr;
		system = nullptr;
		// Flown in a system: the focused one first (where the player is), then any other.
		if (galaxy.Focus != nullptr && galaxy.Focus->World != nullptr && galaxy.Focus->World->PilotedShip != nullptr
			&& !galaxy.Focus->World->PilotedShip->Destroyed)
		{
			system = galaxy.Focus;
			world = system->World;
			return world->PilotedShip;
		}
		for (const auto& s : galaxy.Systems)
			if (s->World != nullptr && s->World->PilotedShip != nullptr && !s->World->PilotedShip->Destroyed)
			{
				system = s;
				world = s->World;
				return world->PilotedShip;
			}
		// Jumped from the helm and crossing open space: the player's own group.
		if (galaxy.DeepSpace != nullptr)
			for (const auto& c : galaxy.DeepSpace->Cruises())
				if (c->Piloted && !c->Plan && c->Ships.Count() > 0 && c->Ships[0] != nullptr && !c->Ships[0]->Destroyed)
				{
					world = c->From != nullptr ? c->From->World : nullptr;
					return c->Ships[0];
				}
		return nullptr;
	}

	cs::List<cs::Ref<SurveyRecord>> Exploration::HeldBy(const cs::Ref<Ship>& hull) const
	{
		cs::List<cs::Ref<SurveyRecord>> r;
		if (hull == nullptr || hull->Destroyed) return r;
		for (const auto& h : Held)
			if (h->Hull == hull) r.Add(h);
		return r;
	}

	double Exploration::ValueHeldBy(const cs::Ref<Ship>& hull) const
	{
		double v = 0;
		for (const auto& h : HeldBy(hull)) v += h->Value;
		return v;
	}

	bool Exploration::IsCartographer(const cs::Ref<Settlement>& s)
	{
		return s != nullptr && s->Owner != nullptr && !s->Owner->IsPirate && s->Kind != SettlementKind::Haven
			&& s->Kind != SettlementKind::MiningOutpost && s->Population >= CartographerPopulation;
	}

	double Exploration::OfferAt(const cs::Ref<Settlement>& port, const cs::Ref<Ship>& hull, int& reports) const
	{
		reports = 0;
		if (!IsCartographer(port)) return 0;
		double v = 0;
		for (const auto& h : HeldBy(hull))
			if (!ChartedFor(port->Owner, h->Key))
			{
				v += h->Value;
				reports++;
			}
		return v;
	}

	double Exploration::SellAt(const cs::Ref<World>& world, const cs::Ref<Settlement>& port, const cs::Ref<Ship>& hull, double jd, int& reports)
	{
		reports = 0;
		if (!IsCartographer(port) || hull == nullptr || hull->Destroyed) return 0;
		double paid = 0;
		for (int i = 0; i < Held.Count();)
		{
			auto h = Held[i];
			if (h->Hull != hull || ChartedFor(port->Owner, h->Key)) { i++; continue; }
			Charted[ChartKey(port->Owner, h->Key)] = jd;
			paid += h->Value;
			reports++;
			Held.RemoveAt(i);
		}
		if (reports == 0) return 0;
		paid = cs::Math::Round(paid);
		if (hull->Faction != nullptr) hull->Faction->Treasury += paid;
		Sold += reports;
		Earned += paid;
		if (world != nullptr)
			world->Log(jd, cs::Cat(port->Name, "'s cartographers buy ", reports, " survey report", reports == 1 ? "" : "s", " from the ", hull->Name,
				" for ", cs::Fmt(paid, "N0"), " cr: ", port->Owner->Name, "'s maps gain them"), WorldEventKind::Info, port->Body);
		return paid;
	}

	bool Exploration::Record(Galaxy& galaxy, const cs::Ref<World>& world, const cs::Ref<StarSystem>& system, const cs::Ref<Ship>& hull, SurveyKind kind,
		const cs::String& key, const cs::String& subject, const cs::String& place, double baseValue, double distanceLy, double jd, int body)
	{
		if (hull == nullptr || hull->Destroyed || key.empty() || IsSurveyed(key)) return false;
		Surveyed[key] = jd;
		auto r = cs::New<SurveyRecord>();
		r->Subject = subject;
		r->Key = key;
		r->Place = place;
		r->Kind = kind;
		r->BeyondLy = cs::Math::Max(0.0, distanceLy - StarDiscovery::KnownToLy);
		r->Value = cs::Math::Round(baseValue * DistanceFactor(distanceLy) / 100) * 100;
		r->Jd = jd;
		r->Hull = hull;

		// The pilot it is credited to: the one flying the hull, met (and held) if need be.
		cs::Ref<NamedPilot> pilot;
		cs::Ref<Characters> chars = world != nullptr ? Characters::Of(world) : nullptr;
		if (chars != nullptr)
		{
			pilot = chars->Find(hull);
			if (pilot == nullptr && world->PilotedShip == hull) pilot = chars->Embodied(world, hull, jd);
		}
		r->Pilot = pilot != nullptr ? pilot->Name : cs::Cat("the crew of the ", hull->Name);
		Held.Add(r);
		Firsts++;

		// In the news: this system's log, or the deep-space log out between the stars.
		cs::String news = cs::Cat("First discovery: ", subject, " charted by ", r->Pilot, pilot != nullptr ? cs::Cat(" of the ", hull->Name) : cs::String(),
			" (survey data worth ", cs::Fmt(r->Value, "N0"), " cr to a cartographer)");
		if (system != nullptr && world != nullptr) world->Log(jd, news, WorldEventKind::Info, body);
		else if (galaxy.DeepSpace != nullptr && galaxy.Viewpoint.has_value()) galaxy.DeepSpace->Log(jd, news, *galaxy.Viewpoint);

		// In the pilot's story, as their own deed.
		if (pilot != nullptr) chars->Act(pilot, LifeEventKind::Discovered, jd, kind == SurveyKind::Star ? 0.5 : 0.2, { "place", subject, "ship", hull->Name });

		// And a little reach for the SpaceGod.
		auto w = world;
		if (w == nullptr && galaxy.Polities != nullptr && galaxy.Polities->Worlds().Count() > 0) w = galaxy.Polities->Worlds()[0];
		if (w != nullptr) SpaceGodInfluence::Grant(w, InfluenceSource::Discovery, kind == SurveyKind::Star ? StarXp : FindXp, cs::Cat("first to chart ", subject));

		Notices.Add(cs::Cat("Survey data: ", subject, "  (", ToString(kind), ", ", cs::Fmt(r->Value, "N0"), " cr to a cartographer)"));
		return true;
	}

	void Exploration::StarFound(Galaxy& galaxy, const cs::String& star, double distanceLy, double jd)
	{
		cs::Ref<World> world;
		cs::Ref<StarSystem> system;
		auto hull = EmbodiedHull(galaxy, world, system);
		if (hull == nullptr) return;
		Record(galaxy, world, system, hull, SurveyKind::Star, cs::Cat("star|", star), star, star, BaseValue(SurveyKind::Star), distanceLy, jd);
	}

	void Exploration::Update(Galaxy& galaxy, double jd)
	{
		// Data dies with the hull that carried it.
		Held.RemoveAll([](const cs::Ref<SurveyRecord>& h) { return h == nullptr || h->Hull == nullptr || h->Hull->Destroyed; });

		cs::Ref<World> world;
		cs::Ref<StarSystem> system;
		auto hull = EmbodiedHull(galaxy, world, system);
		if (hull == nullptr || system == nullptr || world == nullptr || world->Fleet == nullptr || world->Ephemeris == nullptr) return;
		double ly = system->PositionLy.Length();

		// Anomalies the hull has seen through (the director pays its own reward; the readings are survey data too).
		if (world->Adventures != nullptr)
			for (const auto& op : world->Adventures->History)
				if (op != nullptr && op->Kind == OpportunityKind::Anomaly && op->State == OpportunityState::Completed && op->EngagedBy == hull
					&& !op->SourceKey.empty())
					Record(galaxy, world, system, hull, SurveyKind::Anomaly, cs::Cat("anomaly|", system->Name, "|", op->SourceKey), cs::Cat(op->Title, " (", system->Name, ")"),
						system->Name, cs::Math::Max(BaseValue(SurveyKind::Anomaly), op->Reward.Credits * 0.25), ly, op->ResolvedJd > 0 ? op->ResolvedJd : jd, op->Body);

		// Where the helm has the hull now (none reported: nobody is flying it this moment).
		DVec3 at;
		if (!world->Fleet->HeldPoseOf(hull, jd, at)) return;
		auto cat = world->Catalog();
		if (cat == nullptr || cat->Count() == 0) return;
		bool beyond = ly > StarDiscovery::KnownToLy;
		for (int i = 0; i < cat->Count(); i++)
		{
			auto b = (*cat)[i];
			if (b == nullptr || b->IsStar() || !b->IsVisible()) continue;
			auto site = VoidSites::Find(b->Name, b->ParentName);
			SurveyKind kind = SurveyKind::Body;
			double value = 0;
			cs::String subject = b->Name;
			if (site != nullptr)
			{
				if (site->Kind == VoidSiteKind::Sight) { kind = SurveyKind::Sight; value = SightValue(site->Sight); subject = cs::Cat(b->Name, ", a ", SightName(site->Sight)); }
				else if (site->Kind == VoidSiteKind::Planetoid) { kind = SurveyKind::Body; value = 15000; }
				else continue; // drift rocks and chunks are prospected, not surveyed
			}
			else
			{
				// Past the known neighbourhood only: everything nearer has been on the charts for generations.
				if (!beyond) continue;
				value = BodyValue(b);
				if (value <= 0) continue;
				kind = SurveyKind::Body;
				// A settled body is somebody's home, not a find.
				if (world->Economy != nullptr && world->Economy->At(i).Count() > 0) continue;
			}
			cs::String key = cs::Cat(kind == SurveyKind::Sight ? "sight|" : "body|", system->Name, "|", b->Name);
			if (IsSurveyed(key)) continue;
			double reach = cs::Math::Max(SurveyKm, SurveyRadii * b->RadiusKm);
			if ((world->Ephemeris->HelioAt(i, jd) - at).Length() > reach) continue;
			Record(galaxy, world, system, hull, kind, key, subject, system->Name, value, ly, jd, i);
		}

		// Graveyards, measured from the system's root.
		const auto& yards = Graveyards::Of(cat->Root());
		if (yards.Count() > 0)
		{
			auto root = world->Ephemeris->HelioAt(0, jd);
			for (const auto& g : yards)
			{
				cs::String key = cs::Cat("graveyard|", system->Name, "|", g.Name);
				if (IsSurveyed(key)) continue;
				if ((root + g.CentreKm - at).Length() > g.RadiusKm + SurveyKm) continue;
				bool cube = false;
				for (const auto& st : g.Stations) if (st.Cube) cube = true;
				Record(galaxy, world, system, hull, SurveyKind::Graveyard, key, cs::Cat(g.Name, " (", system->Name, ")"), system->Name,
					BaseValue(SurveyKind::Graveyard) + 10000.0 * g.Stations.Count() + (cube ? 80000 : 0), ly, jd, 0);
			}
		}
	}

	void Exploration::Run(Galaxy& galaxy, double jd)
	{
		auto ledger = Find(galaxy);
		if (ledger == nullptr)
		{
			cs::Ref<World> world;
			cs::Ref<StarSystem> system;
			if (EmbodiedHull(galaxy, world, system) == nullptr) return;
			ledger = Of(galaxy);
		}
		ledger->Update(galaxy, jd);
	}

	cs::List<cs::String> Exploration::TakeNotices()
	{
		auto r = Notices;
		Notices.Clear();
		return r;
	}

	// ================================================================ ObjectGraph registrations (docs/PERSISTENCE.md)

	SG_ENUM(SurveyKind, "SpaceGod.Core.SurveyKind")

	SG_SERIALIZE(SurveyRecord, "SpaceGod.Core.SurveyRecord",
		SG_F(Subject), SG_F(Key), SG_F(Place), SG_F(Kind), SG_F(Value), SG_F(BeyondLy), SG_F(Jd), SG_F(Hull), SG_F(Pilot))

	SG_SERIALIZE(Exploration, "SpaceGod.Core.Exploration",
		SG_F(Surveyed), SG_F(Held), SG_F(Charted), SG_F(Firsts), SG_F(Sold), SG_F(Earned))
}
