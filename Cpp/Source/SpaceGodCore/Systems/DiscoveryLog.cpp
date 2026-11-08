// US-T259: the discovery log (see DiscoveryLog.h).

#include "Systems/DiscoveryLog.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/BioMass.h"
#include "Comms/Pilots.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Law/Law.h"
#include "Lore/LoreKeys.h"
#include "Lore/ScanTabs.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "Systems/DeepSpace.h"
#include "Systems/Galaxy.h"
#include "Systems/Graveyards.h"
#include "Systems/VoidSites.h"
#include "World.h"

namespace SpaceGod::Core
{
	const char* ToString(DiscoveryKind v)
	{
		switch (v)
		{
			case DiscoveryKind::Star: return "Star";
			case DiscoveryKind::Body: return "Body";
			case DiscoveryKind::Settlement: return "Settlement";
			case DiscoveryKind::Haven: return "Haven";
			case DiscoveryKind::Graveyard: return "Graveyard";
			case DiscoveryKind::Derelict: return "Derelict";
			case DiscoveryKind::VoidSight: return "VoidSight";
			case DiscoveryKind::Sighting: return "Sighting";
			case DiscoveryKind::Pilot: return "Pilot";
		}
		return "Star";
	}

	const char* DiscoveryLog::Heading(DiscoveryKind kind)
	{
		switch (kind)
		{
			case DiscoveryKind::Star: return "Stars charted";
			case DiscoveryKind::Body: return "Bodies surveyed";
			case DiscoveryKind::Settlement: return "Settlements";
			case DiscoveryKind::Haven: return "Pirate havens";
			case DiscoveryKind::Graveyard: return "Graveyards";
			case DiscoveryKind::Derelict: return "Derelicts";
			case DiscoveryKind::VoidSight: return "Void sights";
			case DiscoveryKind::Sighting: return "Sightings";
			case DiscoveryKind::Pilot: return "Pilots met";
		}
		return "";
	}

	void DiscoveryLog::Index() const
	{
		if (indexed == Entries.Count()) return;
		ids.clear();
		for (const auto& e : Entries)
			if (e != nullptr) ids.insert(e->Id);
		indexed = Entries.Count();
	}

	bool DiscoveryLog::Has(const cs::String& id) const
	{
		Index();
		return ids.count(id) > 0;
	}

	cs::Ref<DiscoveryEntry> DiscoveryLog::Find(const cs::String& id) const
	{
		if (!Has(id)) return nullptr;
		for (const auto& e : Entries)
			if (e != nullptr && e->Id == id) return e;
		return nullptr;
	}

	cs::Ref<DiscoveryEntry> DiscoveryLog::Record(DiscoveryKind kind, const cs::String& id, const cs::String& name, const cs::String& system,
		int body, double jd, const cs::String& by, const cs::String& note, const cs::String& loreKey)
	{
		if (id.empty() || Has(id)) return nullptr;
		auto e = cs::New<DiscoveryEntry>();
		e->Kind = kind;
		e->Id = id;
		e->Name = name;
		e->System = system;
		e->Body = body;
		e->Jd = jd;
		e->By = by;
		e->Note = note;
		e->LoreKey = loreKey;
		Entries.Add(e);
		ids.insert(id);
		indexed = Entries.Count();
		Version++;
		return e;
	}

	int DiscoveryLog::CountOf(DiscoveryKind kind) const
	{
		int n = 0;
		for (const auto& e : Entries)
			if (e != nullptr && e->Kind == kind) n++;
		return n;
	}

	cs::List<cs::Ref<DiscoveryEntry>> DiscoveryLog::Of(DiscoveryKind kind) const
	{
		cs::List<cs::Ref<DiscoveryEntry>> list;
		for (int i = Entries.Count() - 1; i >= 0; i--) // newest written first, so equal days keep the latest on top
			if (Entries[i] != nullptr && Entries[i]->Kind == kind) list.Add(Entries[i]);
		// Stable by date, newest first (an insertion sort: the list is short and nearly in order already).
		for (int i = 1; i < list.Count(); i++)
		{
			auto e = list[i];
			int j = i - 1;
			while (j >= 0 && list[j]->Jd < e->Jd)
			{
				list[j + 1] = list[j];
				j--;
			}
			list[j + 1] = e;
		}
		return list;
	}

	namespace
	{
		cs::String Id(const char* kind, const cs::String& system, const cs::String& name)
		{
			return cs::Cat(kind, "|", system, "|", name);
		}

		cs::String Cap(const cs::String& s)
		{
			if (s.empty()) return s;
			cs::String r = s;
			if (r[0] >= 'a' && r[0] <= 'z') r[0] = static_cast<char>(r[0] - 'a' + 'A');
			return r;
		}

		cs::String Article(const cs::String& word)
		{
			if (word.empty()) return word;
			char c = static_cast<char>(word[0] | 0x20);
			return cs::Cat(c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ? "an " : "a ", word);
		}

		const char* SettlementWord(SettlementKind k)
		{
			switch (k)
			{
				case SettlementKind::Colony: return "colony";
				case SettlementKind::Station: return "station port";
				case SettlementKind::MiningOutpost: return "mining outpost";
				case SettlementKind::Haven: return "pirate haven";
			}
			return "settlement";
		}

		const char* BodyWord(BodyKind k)
		{
			switch (k)
			{
				case BodyKind::Star: return "star";
				case BodyKind::Planet: return "planet";
				case BodyKind::DwarfPlanet: return "dwarf planet";
				case BodyKind::Moon: return "moon";
				case BodyKind::Barycentre: return "barycentre";
				case BodyKind::Station: return "station";
				case BodyKind::Asteroid: return "asteroid";
			}
			return "body";
		}

		const char* MetWord(MeetKind k)
		{
			switch (k)
			{
				case MeetKind::Hailed: return "Hailed";
				case MeetKind::Embodied: return "Embodied";
				case MeetKind::Fought: return "Fought";
				case MeetKind::Rescued: return "Rescued";
				case MeetKind::Mentioned: return "Heard of";
			}
			return "Met";
		}

		cs::String People(double n)
		{
			return n >= 1e9 ? cs::Cat(cs::Fmt(n / 1e9, "0.0"), " bn")
				: n >= 1e6 ? cs::Cat(cs::Fmt(n / 1e6, "0.0"), " m")
				: cs::Fmt(n, "N0");
		}

		/// The finder of a found haven: the first by name of the factions that know it (a set's order is no peer's to share).
		cs::String FinderOf(const Settlement& s)
		{
			cs::String best;
			for (const auto& f : s.KnownTo)
				if (f != nullptr && (best.empty() || f->Name < best)) best = f->Name;
			return best;
		}

		void SweepHere(DiscoveryLog& log, const StarSystem& sys, double jd)
		{
			const auto& name = sys.Name;
			auto cat = sys.Catalog;
			auto root = cat != nullptr && cat->Count() > 0 ? cat->Root() : nullptr;
			cs::String kindNote = sys.IsVoidBase ? cs::String("A void station: visited")
				: root != nullptr && root->Kind == BodyKind::Barycentre ? cs::String("A multiple star: visited")
				: cs::String("Visited");
			auto star = log.Find(Id("star", name, name));
			if (star == nullptr) log.Record(DiscoveryKind::Star, Id("star", name, name), name, name, root != nullptr ? root->Index : 0, jd, "", kindNote, LoreKeys::System(name));
			else if (star->Note.find("isited") == cs::String::npos) { star->Note = star->Note.empty() ? cs::String("Visited") : cs::Cat(star->Note, "; visited"); log.Version++; }

			const auto& world = sys.World;
			if (world != nullptr && world->Economy != nullptr)
				for (const auto& s : world->Economy->Settlements)
				{
					if (s == nullptr || !s->Discovered()) continue;
					bool haven = s->Kind == SettlementKind::Haven;
					cs::String owner = s->Owner != nullptr ? s->Owner->Name : cs::String();
					cs::String on = cat != nullptr && s->Body >= 0 && s->Body < cat->Count() ? (*cat)[s->Body]->Name : cs::String();
					log.Record(haven ? DiscoveryKind::Haven : DiscoveryKind::Settlement, Id(haven ? "haven" : "settlement", name, s->Name), s->Name, name, s->Body, jd,
						haven ? FinderOf(*s) : cs::String(),
						cs::Cat(Cap(Article(SettlementWord(s->Kind))),
							owner.empty() ? cs::String() : cs::Cat(" of ", owner), on.empty() ? cs::String() : cs::Cat(" on ", on)),
						LoreKeys::Settlement(name, s->Name));
				}

			if (root != nullptr)
				for (const auto& g : Graveyards::Of(root))
				{
					log.Record(DiscoveryKind::Graveyard, Id("graveyard", name, g.Name), g.Name, name, root->Index, jd, "",
						cs::Cat(g.Hulks.Count(), g.Hulks.Count() == 1 ? " great hulk" : " great hulks", g.Stations.Count() > 0 ? cs::Cat(" and ", g.Stations.Count(), g.Stations.Count() == 1 ? " derelict station" : " derelict stations") : cs::String(),
							", ", g.Warm ? "backlit in an orange haze" : "in an icy blue-white haze"),
						LoreKeys::Named(LoreKeys::System(name), LoreKeys::Slug(g.Name)));
					for (const auto& st : g.Stations)
						log.Record(DiscoveryKind::Derelict, Id("derelict", name, st.Name), st.Name, name, root->Index, jd, "",
							st.Summary.empty() ? cs::Cat("A derelict station in ", g.Name) : st.Summary,
							LoreKeys::Named(LoreKeys::System(name), LoreKeys::Slug(st.Name)));
				}

			if (cat != nullptr)
				for (int i = 0; i < cat->Count(); i++)
				{
					const auto& b = (*cat)[i];
					auto site = VoidSites::Find(b->Name, b->ParentName);
					if (site == nullptr || (site->Kind != VoidSiteKind::Sight && site->Kind != VoidSiteKind::Megastructure)) continue;
					log.Record(DiscoveryKind::VoidSight, Id("sight", name, b->Name), b->Name, name, i, jd, "", site->Summary.empty() ? b->Summary : site->Summary,
						LoreKeys::Body(b->Kind, name, b->Name));
				}

			if (world != nullptr && world->BioMass != nullptr)
			{
				bool alive = false;
				for (const auto& g : world->BioMass->Globules)
					if (g != nullptr && g->Alive()) { alive = true; break; }
				if (alive)
					log.Record(DiscoveryKind::Sighting, Id("biomass", name, "Bio Mass"), cs::Cat("Bio Mass in ", name), name, -1, jd, "",
						cs::Cat("The Bio Mass grows here: globules and tumors on the move among the bodies of ", name),
						LoreKeys::Named(LoreKeys::Universe, "bio-mass"));
			}
		}

		void SweepBody(DiscoveryLog& log, const StarSystem& sys, int index, double jd, const char* how)
		{
			auto cat = sys.Catalog;
			if (cat == nullptr || index <= 0 || index >= cat->Count()) return; // the root is the star itself
			const auto& b = (*cat)[index];
			if (b->Kind == BodyKind::Star || b->Kind == BodyKind::Barycentre) return;
			cs::String parent = b->ParentName.empty() ? cs::String() : cs::Cat(" of ", b->ParentName);
			log.Record(DiscoveryKind::Body, Id("body", sys.Name, b->Name), b->Name, sys.Name, index, jd, "",
				cs::Cat(Cap(Article(BodyWord(b->Kind))), parent, ", ", how),
				LoreKeys::Body(b->Kind, sys.Name, b->Name));
		}
	}

	int DiscoveryLog::Sweep(const Galaxy& galaxy, const Place& place, double jd)
	{
		int before = Entries.Count();

		// The stars the lookouts found, each on its own day and by its own finder.
		for (const auto& d : galaxy.Discoveries)
			if (d != nullptr)
				Record(DiscoveryKind::Star, Id("star", d->Name, d->Name), d->Name, d->Name, 0, d->Jd, d->By,
					cs::Cat("Charted at ", cs::Fmt(d->PositionLy.Length(), "0.0"), " ly from Sol"), LoreKeys::System(d->Name));

		if (place.Here != nullptr)
		{
			SweepHere(*this, *place.Here, jd);
			if (place.NearBody >= 0) SweepBody(*this, *place.Here, place.NearBody, jd, "come close to at the helm");
			if (place.ScannedBody >= 0) SweepBody(*this, *place.Here, place.ScannedBody, jd, "scanned");
		}

		// The Void Raiders' strikes and the Watcher's signs, out between the stars, as the deep-space log tells them.
		if (galaxy.DeepSpace != nullptr)
			for (const auto& ev : galaxy.DeepSpace->Events())
			{
				if (ev == nullptr || (ev->Kind != DeepSpaceEvent::VoidRaid && ev->Kind != DeepSpaceEvent::WatcherSign)) continue;
				bool raid = ev->Kind == DeepSpaceEvent::VoidRaid;
				Record(DiscoveryKind::Sighting, Id(raid ? "voidraid" : "watcher", "", cs::Fmt(ev->Jd, "0.000000")),
					raid ? cs::String("Void Raider strike") : cs::String("Sign of the Watcher"), "", -1, ev->Jd, "", ev->Text,
					LoreKeys::Named(LoreKeys::Universe, raid ? "void-raiders" : "the-watcher"));
			}

		// Every named pilot met, wherever, on the day they were met (one only named in someone else's story is not met).
		for (const auto& sys : galaxy.Systems)
		{
			if (sys == nullptr || sys->World == nullptr || sys->World->Law == nullptr || sys->World->Law->Pilots == nullptr) continue;
			for (const auto& p : sys->World->Law->Pilots->All)
			{
				if (p == nullptr || p->FirstMet == MeetKind::Mentioned) continue;
				auto e = Record(DiscoveryKind::Pilot, Id("pilot", sys->Name, cs::Cat(p->Id, " ", p->Name)), p->Styled(), sys->Name, -1, p->MetJd,
					"", cs::Cat(MetWord(p->FirstMet), p->ShipName.empty() ? cs::String() : cs::Cat(" aboard the ", p->ShipName), " in ", sys->Name),
					LoreKeys::Pilot(p->Name));
				if (e != nullptr) e->PilotId = p->Id;
			}
		}
		return Entries.Count() - before;
	}

	cs::List<cs::String> DiscoveryLog::Readings(const Galaxy& galaxy, const DiscoveryEntry& e, double jd)
	{
		cs::List<cs::String> lines;
		auto sys = e.System.empty() ? nullptr : galaxy.Find(e.System);
		auto cat = sys != nullptr ? sys->Catalog : nullptr;
		switch (e.Kind)
		{
			case DiscoveryKind::Star:
			{
				if (sys != nullptr)
				{
					lines.Add(cs::Cat(cs::Fmt(sys->PositionLy.Length(), "0.00"), " ly from Sol"));
					auto root = cat != nullptr && cat->Count() > 0 ? cat->Root() : nullptr;
					if (sys->IsVoidBase) lines.Add("No star: a relay's gravity generator and its tiny suns");
					else if (root != nullptr && root->Kind == BodyKind::Star)
						lines.Add(cs::Cat(root->SpectralType.empty() ? cs::String("A star") : cs::Cat(root->SpectralType, " star"), ", ",
							cs::Fmt(root->TeffK, "N0"), " K, ", cs::Fmt(root->LuminositySolar, "0.####"), " Sun"));
					else if (root != nullptr) lines.Add("A multiple star about a common centre");
					if (cat != nullptr)
					{
						int planets = 0, moons = 0, stations = 0;
						for (int i = 0; i < cat->Count(); i++)
						{
							auto k = (*cat)[i]->Kind;
							if (k == BodyKind::Planet || k == BodyKind::DwarfPlanet) planets++;
							else if (k == BodyKind::Moon) moons++;
							else if (k == BodyKind::Station) stations++;
						}
						lines.Add(cs::Cat(planets, planets == 1 ? " planet, " : " planets, ", moons, moons == 1 ? " moon, " : " moons, ", stations,
							stations == 1 ? " station" : " stations"));
					}
					if (sys->World != nullptr && sys->World->Economy != nullptr)
					{
						int n = 0;
						double people = 0;
						for (const auto& s : sys->World->Economy->Settlements)
							if (s != nullptr && s->Discovered() && !s->Fallen) { n++; people += s->Population; }
						lines.Add(n == 0 ? cs::String("Unsettled") : cs::Cat(n, n == 1 ? " settlement, " : " settlements, ", People(people), " people"));
					}
					auto holder = sys->Holder();
					if (holder != nullptr) lines.Add(cs::Cat("Held by ", holder->Name));
				}
				else
				{
					for (const auto& s : galaxy.NearbyStars)
						if (s.Name == e.Name)
						{
							lines.Add(cs::Cat(cs::Fmt(s.PositionLy.Length(), "0.00"), " ly from Sol"));
							lines.Add(cs::Cat(s.SpectralType.empty() ? cs::String("Type unknown") : cs::Cat(s.SpectralType, " star"),
								s.TeffK > 0 ? cs::Cat(", ", cs::Fmt(s.TeffK, "N0"), " K") : cs::String()));
							lines.Add("On the charts: nobody simulates it yet and no gate goes there");
							break;
						}
				}
				break;
			}
			case DiscoveryKind::Body:
			case DiscoveryKind::VoidSight:
			{
				if (cat == nullptr || e.Body < 0 || e.Body >= cat->Count()) break;
				const auto& b = (*cat)[e.Body];
				if (b->Name != e.Name) break; // the catalogue has changed under it
				lines.Add(cs::Cat(Cap(BodyWord(b->Kind)),
					b->ParentName.empty() ? cs::String() : cs::Cat(" of ", b->ParentName), ", radius ", cs::Fmt(b->RadiusKm, "N0"), " km"));
				double g = ScanFacts::SurfaceGravityG(*b);
				if (g > 0) lines.Add(cs::Cat("Surface gravity ", cs::Fmt(g, "0.00"), " g"));
				double k = ScanFacts::EquilibriumK(*cat, e.Body);
				if (!cs::Double::IsNaN(k)) lines.Add(cs::Cat("Equilibrium temperature ", cs::Fmt(k, "N0"), " K (", cs::Fmt(k - 273.15, "N0"), " C)"));
				lines.Add(cs::Cat("Atmosphere: ", ScanFacts::Atmosphere(*b)));
				if (b->RingOuterKm > 0) lines.Add("Ringed");
				if (sys->World != nullptr && sys->World->Economy != nullptr)
					for (const auto& s : sys->World->Economy->At(e.Body))
						if (s != nullptr && s->Discovered())
							lines.Add(cs::Cat("Home to ", s->Name, " (", SettlementWord(s->Kind), ", ", People(s->Population), " people)"));
				break;
			}
			case DiscoveryKind::Settlement:
			case DiscoveryKind::Haven:
			{
				if (sys == nullptr || sys->World == nullptr || sys->World->Economy == nullptr) break;
				for (const auto& s : sys->World->Economy->Settlements)
				{
					if (s == nullptr || s->Name != e.Name) continue;
					cs::String on = cat != nullptr && s->Body >= 0 && s->Body < cat->Count() ? (*cat)[s->Body]->Name : cs::String();
					lines.Add(cs::Cat(Cap(SettlementWord(s->Kind)),
						on.empty() ? cs::String() : cs::Cat(" on ", on), " in ", sys->Name));
					if (s->Owner != nullptr) lines.Add(cs::Cat("Flag: ", s->Owner->Name));
					lines.Add(s->Fallen ? cs::String("Fallen: in ruins") : cs::Cat("Population ", People(s->Population)));
					if (s->HasShipyard()) lines.Add("Shipyard");
					if (s->Kind == SettlementKind::Haven) lines.Add(cs::Cat("Known to ", s->KnownTo.Count(), s->KnownTo.Count() == 1 ? " faction" : " factions"));
					break;
				}
				break;
			}
			case DiscoveryKind::Graveyard:
			case DiscoveryKind::Derelict:
			{
				if (cat == nullptr || cat->Count() == 0) break;
				for (const auto& g : Graveyards::Of(cat->Root()))
				{
					if (e.Kind == DiscoveryKind::Graveyard && g.Name == e.Name)
					{
						lines.Add(cs::Cat("Some ", cs::Fmt(g.RadiusKm, "N0"), " km across"));
						lines.Add(cs::Cat(g.Hulks.Count(), g.Hulks.Count() == 1 ? " great hulk, " : " great hulks, ", g.Stations.Count(),
							g.Stations.Count() == 1 ? " derelict station" : " derelict stations"));
						lines.Add(g.Warm ? "Backlit by a hot star close by: an orange haze of dust" : "By a cool or far star: an icy blue-white haze");
					}
					else if (e.Kind == DiscoveryKind::Derelict)
						for (const auto& st : g.Stations)
							if (st.Name == e.Name)
							{
								lines.Add(cs::Cat(st.Cube ? "A cube " : "A station ", cs::Fmt(st.SizeKm, "0.#"), " km long, in ", g.Name));
								lines.Add("Dark, holed, salvageable");
							}
				}
				break;
			}
			case DiscoveryKind::Pilot:
			{
				if (sys == nullptr || sys->World == nullptr || sys->World->Law == nullptr || sys->World->Law->Pilots == nullptr) break;
				for (const auto& p : sys->World->Law->Pilots->All)
				{
					if (p == nullptr || p->Id != e.PilotId) continue;
					if (!p->Callsign.empty()) lines.Add(cs::Cat("Called \"", p->Callsign, "\" on the lanes"));
					lines.Add(cs::Cat(Careers::Title(p->Track, p->Rank), p->Faction != nullptr ? cs::Cat(", ", p->Faction->Name) : cs::String()));
					lines.Add(cs::Cat("Age ", p->AgeAt(jd), p->Birthplace.empty() ? cs::String() : cs::Cat(", born on ", p->Birthplace)));
					if (!p->ShipName.empty()) lines.Add(cs::Cat("Flies the ", p->ShipName));
					lines.Add(cs::Cat("Renown ", cs::Fmt(p->Renown, "N0"), ", ", p->BattlesSurvived, p->BattlesSurvived == 1 ? " battle lived through" : " battles lived through"));
					lines.Add(cs::Cat("Hailed ", p->TimesHailed, p->TimesHailed == 1 ? " time" : " times"));
					break;
				}
				break;
			}
			case DiscoveryKind::Sighting:
				break;
		}
		return lines;
	}

	// ================================================================ ObjectGraph registrations (docs/PERSISTENCE.md)

	SG_ENUM(DiscoveryKind, "SpaceGod.Core.DiscoveryKind")

	SG_SERIALIZE(DiscoveryEntry, "SpaceGod.Core.DiscoveryEntry",
		SG_F(Kind), SG_F(Id), SG_F(Name), SG_F(System), SG_F(Body), SG_F(Jd), SG_F(By), SG_F(Note), SG_F(LoreKey), SG_F(PilotId))

	SG_SERIALIZE(DiscoveryLog, "SpaceGod.Core.DiscoveryLog",
		SG_F(Entries))
}
