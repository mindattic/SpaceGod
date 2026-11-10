// US-T171: the headless Universe Interchange exporter (Prose RFC 0007, docs/PROSE.md).
//
// Builds the galaxy exactly as the game and the Bench do (Galaxy::CreateDefault at a fixed start date and seed), runs it
// on for --days at coarse detail, then writes everything in it as one Universe Interchange file
// (Prose's docs/schemas/universe.schema.json; it is the Prose consumer kit's mapper, US-T236): systems, stars, planets, moons, stations, settlements, pirate bases, void stations
// and their sites, nebulas, ice fields, graveyards and derelicts, factions, every ship, a named pilot for each ship with
// their life story, technologies, weapons, equipment, ship classes and designs, goods, missing ships with their
// last-known coordinates, gas harvesters, and the edges between them (orbits, located-in, owns, crew-of, grudges,
// rivalries, wars, gate links).
//
// Nothing here needs Unreal: it links the standalone SpaceGodCore.lib (tools/build-core.ps1). Same seed + same build =
// the same file, byte for byte (the ids are slugs of names, never pointers or counters that move between runs). It only
// reads the simulation, except that it meets each ship's pilot (Characters::Meet), which is what the game does the moment
// the SpaceGod hails anyone; that happens after the run, so the run itself is the Bench's.
//
// Usage: UniverseExport.exe --out <file> [--seed 1977] [--start-jd 2461324] [--days 30] [--build <git hash>]
//                           [--pilots all|none]

#include "Cs/Cs.h"
#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "OrbitalElements.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"
#include "GalacticPosition.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Construction.h"
#include "Combat/Diplomacy.h"
#include "Combat/Outfitting.h"
#include "Comms/Pilots.h"
#include "Lore/LoreKeys.h"
#include "Lore/ScanTabs.h" // US-T239: the scanner readings the scan card shows
#include "Economy/Industry.h"
#include "Combat/Boarding.h" // Interior: a hull's crew and marines
#include "Economy/BeltMining.h"
#include "Economy/Companies.h"
#include "Law/Law.h"
#include "Economy/Economy.h"
#if __has_include("Economy/GasHarvest.h")
#include "Economy/GasHarvest.h" // US-T146's gas harvesters, once they land
#define SG_EXPORT_HARVEST 1
#endif
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Systems/Biomes.h"
#include "Systems/DeepSpace.h"
#include "Systems/Galaxy.h"
#include "Systems/Graveyards.h"
#include "Systems/InterstellarWars.h"
#include "Systems/JumpGate.h"
#include "Systems/Polities.h"
#include "Systems/SpaceFeatures.h"
#include "Systems/StarDiscovery.h"
#include "Systems/StarSystemSpec.h"
#include "Systems/Statecraft.h"
#include "Systems/StrategicCommand.h"
#include "Systems/Society.h" // US-T274: the people and their organisations, for Prose to write about
#include "Systems/Civilization/Civilization.h"
#include "Systems/Civilization/Narrator.h"
#include "Systems/SystemSources.h"
#include "Systems/VoidSites.h"
#include "Systems/VoidStations.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace SpaceGod::Core;

namespace
{
	// ------------------------------------------------------------------ JSON

	std::string Esc(const std::string& s)
	{
		std::string o;
		o.reserve(s.size() + 2);
		o += '"';
		for (unsigned char c : s)
		{
			switch (c)
			{
			case '"': o += "\\\""; break;
			case '\\': o += "\\\\"; break;
			case '\n': o += "\\n"; break;
			case '\r': o += "\\r"; break;
			case '\t': o += "\\t"; break;
			default:
				if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
				else o += static_cast<char>(c);
			}
		}
		o += '"';
		return o;
	}

	/// <summary>A number as JSON: 10 significant digits (stable to read and diff), integers plain; NaN/inf are not JSON.</summary>
	std::string Num(double v)
	{
		if (!std::isfinite(v)) return "null";
		if (v == std::floor(v) && std::fabs(v) < 1e15) { char b[32]; std::snprintf(b, sizeof b, "%.0f", v); return std::string(b) == "-0" ? "0" : b; }
		char b[40];
		std::snprintf(b, sizeof b, "%.10g", v);
		return b;
	}

	/// <summary>An ordered JSON object built field by field (the order written is the order added).</summary>
	class Obj
	{
	public:
		Obj& S(const std::string& k, const std::string& v) { if (!v.empty()) Add(k, Esc(v)); return *this; }
		Obj& N(const std::string& k, double v) { if (std::isfinite(v)) Add(k, Num(v)); return *this; }
		Obj& I(const std::string& k, long long v) { Add(k, std::to_string(v)); return *this; }
		Obj& B(const std::string& k, bool v) { Add(k, v ? "true" : "false"); return *this; }
		Obj& O(const std::string& k, const Obj& v) { if (!v.Empty()) Add(k, v.Str()); return *this; }
		Obj& A(const std::string& k, const std::vector<std::string>& strings)
		{
			if (strings.empty()) return *this;
			std::string a = "[";
			for (size_t i = 0; i < strings.size(); i++) { if (i) a += ", "; a += Esc(strings[i]); }
			Add(k, a + "]");
			return *this;
		}
		Obj& Raw(const std::string& k, const std::string& json) { Add(k, json); return *this; }
		Obj& Vec(const std::string& k, DVec3 v) { return Raw(k, "[" + Num(v.X) + ", " + Num(v.Y) + ", " + Num(v.Z) + "]"); }
		bool Empty() const { return fields.empty(); }
		std::string Str() const
		{
			std::string o = "{";
			for (size_t i = 0; i < fields.size(); i++) { if (i) o += ", "; o += Esc(fields[i].first) + ": " + fields[i].second; }
			return o + "}";
		}

	private:
		void Add(const std::string& k, const std::string& v)
		{
			for (auto& f : fields) if (f.first == k) { f.second = v; return; }
			fields.emplace_back(k, v);
		}
		std::vector<std::pair<std::string, std::string>> fields;
	};

	std::string ArrOfObj(const std::vector<Obj>& items)
	{
		std::string a = "[";
		for (size_t i = 0; i < items.size(); i++) { if (i) a += ", "; a += items[i].Str(); }
		return a + "]";
	}

	// ------------------------------------------------------------------ ids

	/// <summary>
	/// A Prose-stable slug: lowercase ASCII letters and digits joined by single hyphens, nothing else (Prose re-slugifies every
	/// id with exactly this rule, so an id already in this form comes back unchanged -- the EVE post-mortem's re-slugify bug
	/// cannot bite it).
	/// </summary>
	/// US-T237: Core's LoreKeys makes it, so the game's panels look texts up by exactly the ids written here.
	std::string Slug(const std::string& s) { return LoreKeys::Slug(s); }

	std::string Lower(std::string s)
	{
		for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
		return s;
	}

	/// <summary>An enum's name as a slug ("MiningOutpost" -> "mining-outpost").</summary>
	std::string EnumSlug(const char* name)
	{
		std::string s;
		for (const char* p = name; *p; ++p)
		{
			if (*p >= 'A' && *p <= 'Z' && !s.empty() && s.back() != '-' && !(p[-1] >= 'A' && p[-1] <= 'Z')) s += '-';
			s += *p;
		}
		return Slug(s);
	}

	/// <summary>A styled name without its rank and callsign: "Captain Mara "Lanebreaker" Seddon" -> "Mara Seddon".</summary>
	std::string PlainName(const std::string& styled)
	{
		std::string s;
		bool quoted = false;
		for (char c : styled) { if (c == '"') { quoted = !quoted; continue; } if (!quoted) s += c; }
		for (int t = 0; t <= static_cast<int>(CareerTrack::Reform); t++)
			for (int r = 0; r <= Careers::TopRank(static_cast<CareerTrack>(t)); r++)
			{
				std::string title = Careers::Title(static_cast<CareerTrack>(t), r) + " ";
				if (s.size() > title.size() && _strnicmp(s.c_str(), title.c_str(), title.size()) == 0) { s = s.substr(title.size()); t = 99; break; }
			}
		std::string o;
		for (char c : s) if (!(c == ' ' && (o.empty() || o.back() == ' '))) o += c;
		while (!o.empty() && o.back() == ' ') o.pop_back();
		return o.empty() ? styled : o;
	}

	/// <summary>A system's short slug ("Sol System" -> "sol").</summary>
	std::string SysSlug(const std::string& name) { return LoreKeys::SystemSlug(name); }

	// ------------------------------------------------------------------ the file

	struct Entity
	{
		std::string Id, Type, Name, Summary;
		Obj Details;
		std::vector<std::pair<std::string, std::string>> Relations; // (to, kind)
		std::vector<std::string> Tags;
	};

	class Universe
	{
	public:
		std::vector<Entity> Entities;
		std::unordered_map<std::string, size_t> ById;
		int Duplicates = 0;

		/// <summary>Adds an entity; the id is made unique by a counter suffix when two different things slug alike (in the
		/// order they are found, which is fixed by the seed, so the suffixes are stable too). Returns its id.</summary>
		std::string Add(Entity e, bool uniquify = true)
		{
			if (ById.count(e.Id))
			{
				if (!uniquify) { Duplicates++; return e.Id; }
				for (int n = 2;; n++)
				{
					std::string id = e.Id + "-" + std::to_string(n);
					if (!ById.count(id)) { e.Id = id; break; }
				}
			}
			ById[e.Id] = Entities.size();
			Entities.push_back(std::move(e));
			return Entities.back().Id;
		}

		bool Has(const std::string& id) const { return ById.count(id) > 0; }
		Entity* Find(const std::string& id) { auto it = ById.find(id); return it == ById.end() ? nullptr : &Entities[it->second]; }

		void Rel(const std::string& from, const std::string& to, const std::string& kind)
		{
			if (from.empty() || to.empty() || from == to) return;
			auto* e = Find(from);
			if (e == nullptr) return;
			for (const auto& r : e->Relations) if (r.first == to && r.second == kind) return;
			e->Relations.emplace_back(to, kind);
		}
	};

	// ------------------------------------------------------------------ helpers

	std::string FactionId(const cs::Ref<Faction>& f) { return f ? LoreKeys::Faction(f->Name) : std::string(); }

	const char* KindType(BodyKind k) { return LoreKeys::BodyType(k); }

	Obj Coords(const GalacticPosition& p)
	{
		Obj o;
		o.Vec("ly", p.Ly());
		o.Raw("sector", "[" + std::to_string(p.X) + ", " + std::to_string(p.Y) + ", " + std::to_string(p.Z) + "]");
		o.Vec("sectorOffsetKm", p.LocalKm);
		return o;
	}

	std::string Rgb3(const Rgb& c) { return "[" + Num(c.R) + ", " + Num(c.G) + ", " + Num(c.B) + "]"; }

	struct Args
	{
		std::string Out = "spacegod.universe.json";
		int Seed = Galaxy::DefaultSeed;
		double StartJd = 2461324.0; // Bench/scenarios.json's startJd: a fixed date, so the export never depends on the clock
		double Days = 30;
		std::string Build = "unknown";
		bool Pilots = true;
		/// <summary>US-T274: run the game's Society and Civilization layers too (houses, firms, parties, faiths, teams...); "--society none" leaves them out.</summary>
		bool Society = true;
	};

	Args Parse(int argc, char** argv)
	{
		Args a;
		for (int i = 1; i < argc; i++)
		{
			std::string k = argv[i];
			auto next = [&]() -> std::string { if (i + 1 >= argc) throw std::runtime_error("missing value for " + k); return argv[++i]; };
			if (k == "--out") a.Out = next();
			else if (k == "--seed") a.Seed = std::stoi(next());
			else if (k == "--start-jd") a.StartJd = std::stod(next());
			else if (k == "--days") a.Days = std::stod(next());
			else if (k == "--build") a.Build = next();
			else if (k == "--pilots") a.Pilots = next() != "none";
			else if (k == "--society") a.Society = next() != "none";
			else throw std::runtime_error("unknown argument " + k);
		}
		return a;
	}

	// ------------------------------------------------------------------ the export

	class Exporter
	{
	public:
		Exporter(const Args& args) : args(args) {}

		Universe U;
		std::map<std::string, int> TypeCounts;

		void Run()
		{
			std::cout << "building galaxy (seed " << args.Seed << ", JD " << Num(args.StartJd) << ")\n";
			galaxy = Galaxy::CreateDefault(args.StartJd, args.Seed);
			auto sol = galaxy->Systems[0];
			// As the game opens (the Bench's Simulation::Build): Sol in focus, the pilot's viewpoint at Sol.
			galaxy->Promote(sol, args.StartJd);
			galaxy->Viewpoint = sol->Origin;
			jd = args.StartJd;
			// US-T274: as the game makes them (UnitySpaceApp::UpdateInfluence), so the export carries the galaxy's people.
			if (args.Society)
			{
				SpaceGod::Core::Society::Of(*galaxy);
				SpaceGod::Core::Civilization::Of(*galaxy);
			}
			if (args.Days > 0)
			{
				std::cout << "running " << Num(args.Days) << " days\n";
				double end = args.StartJd + args.Days;
				while (jd < end - 1e-9)
				{
					jd = std::min(end, jd + 0.25);
					galaxy->Update(jd);
				}
			}

			Catalogues();
			Factions();
			for (const auto& s : galaxy->Systems) System(s);
			VoidStationsAll();
			ChartedStars();
			DeepSpaceObjects();
			InTransit();
			if (args.Pilots) PilotsAll();
			if (args.Society) People();
			Politics();
			Prune();
			Manifest();
		}

	private:
		const Args& args;
		cs::Ref<Galaxy> galaxy;
		double jd = 0;

		/// <summary>Place name -> its entity id (systems, void stations, charted stars), for edges by name.</summary>
		std::unordered_map<std::string, std::string> placeByName;
		std::unordered_map<const Ship*, std::string> shipIds;
		std::unordered_map<const Ship*, cs::Ref<World>> shipWorld;
		std::unordered_map<const Settlement*, std::string> settlementIds;
		std::vector<std::pair<std::string, std::string>> grudgesByName; // (pilot id, target name)

		std::string Add(Entity e, bool uniquify = true)
		{
			TypeCounts[e.Type]++;
			return U.Add(std::move(e), uniquify);
		}

		// -------------------------------------------------------------- static catalogues

		void Catalogues()
		{
			// Goods.
			for (int g = 0; g < GoodInfo::Count; g++)
			{
				const auto& info = GoodInfo::Of(static_cast<Good>(g));
				Entity e{ "good-" + Slug(ToString(info->Good)), "good", info->Name,
					cs::Cat(info->Name, ": a traded commodity (", ToString(info->Category), "), base price ", Num(info->BasePrice), " credits a tonne.") };
				e.Details.S("enum", ToString(info->Good)).S("category", ToString(info->Category)).N("basePricePerTonne", info->BasePrice);
				e.Tags = { "good", EnumSlug(ToString(info->Category)) };
				Add(std::move(e));
			}

			// Research (the technologies a polity puts its people to).
			for (const auto& t : ResearchTrack::All())
			{
				Entity e{ "tech-" + Slug(t->Name), "technology", t->Name, t->Description.empty() ? t->Name + " research." : t->Description };
				e.Details.S("kind", "research-track").N("baseCost", t->BaseCost).I("maxLevel", t->MaxLevel);
				e.Tags = { "technology", "research" };
				Add(std::move(e));
			}
			// Megastructure designs (what belt works can build).
			for (const auto& m : MegastructureSpec::All())
			{
				Entity e{ "tech-megastructure-" + Slug(m->Name), "technology", m->Name,
					cs::Cat(m->Name, ": a megastructure design, built from its bill of materials in no fewer than ", Num(m->MinDays), " days.") };
				Obj bill;
				for (int g = 0; g < m->Bill.Count() && g < GoodInfo::Count; g++)
					if (m->Bill[g] > 0) bill.N(ToString(static_cast<Good>(g)), m->Bill[g]);
				e.Details.S("kind", "megastructure-design").N("minDays", m->MinDays).O("billTonnes", bill);
				e.Tags = { "technology", "megastructure" };
				auto id = Add(std::move(e));
				for (int g = 0; g < m->Bill.Count() && g < GoodInfo::Count; g++)
					if (m->Bill[g] > 0) U.Rel(id, "good-" + Slug(ToString(static_cast<Good>(g))), "made-from");
			}

			// Weapons and equipment: the outfitting catalogue (every module a yard fits).
			for (const auto& m : ModuleSpec::Catalogue())
			{
				bool weapon = m->Mount.has_value() && m->Mount.value() != MountType::Shield && m->Mount.value() != MountType::Engine;
				Entity e{ "module-" + Slug(ToString(m->Kind)), weapon ? "weapon" : "equipment", m->Name,
					cs::Cat(m->Name, weapon ? ": a ship weapon" : ": ship equipment",
						m->Mount.has_value() ? cs::Cat(" for a ", ToString(m->Size), " ", ToString(m->Mount.value()), " mount") : std::string(" fitted inside the hull"),
						m->Spinal ? " (spinal)" : "", ", ", Num(m->MassTonnes), m->Scaled ? " t per unit of hull scale." : " t installed.") };
				e.Details.S("module", ToString(m->Kind)).N("massTonnes", m->MassTonnes).B("scaledWithHull", m->Scaled).B("spinal", m->Spinal)
					.N("fireConeHalfDeg", m->ConeHalfDeg);
				if (m->Mount.has_value()) e.Details.S("mount", ToString(m->Mount.value())).S("hardpointSize", ToString(m->Size));
				Obj bill;
				for (const auto& [g, t] : m->Bill) bill.N(ToString(g), t);
				e.Details.O("billTonnes", bill);
				e.Tags = { weapon ? "weapon" : "equipment" };
				if (m->Mount.has_value()) e.Tags.push_back(EnumSlug(ToString(m->Mount.value())));
				auto id = Add(std::move(e));
				for (const auto& [g, t] : m->Bill) U.Rel(id, "good-" + Slug(ToString(g)), "made-from");
			}

			// The battle profiles behind the weapons (what a hit does, how far, how often).
			struct Profile { const char* Id; const char* Name; const cs::Ref<WeaponSpec>* Spec; };
			const Profile profiles[] = {
				{ "laser", "Laser", &WeaponSpec::Laser() }, { "heavy", "Heavy gun", &WeaponSpec::Heavy() },
				{ "missiles", "Missiles", &WeaponSpec::Missiles() }, { "point-defense", "Point defence", &WeaponSpec::PointDefense() },
				{ "fighter-guns", "Fighter guns", &WeaponSpec::FighterGuns() }, { "torpedo", "Torpedo", &WeaponSpec::Torpedo() },
				{ "railgun", "Railgun", &WeaponSpec::Railgun() }, { "tachyon-lance", "Tachyon lance", &WeaponSpec::TachyonLance() },
			};
			for (const auto& p : profiles)
			{
				const auto& w = *p.Spec;
				Entity e{ std::string("weapon-profile-") + p.Id, "weapon", p.Name,
					cs::Cat(p.Name, ": ", Num(w->Damage), " damage at up to ", Num(w->RangeKm), " km, every ", Num(w->Cooldown), " s.") };
				e.Details.S("kind", "battle-profile").N("rangeKm", w->RangeKm).N("damage", w->Damage).N("cooldownS", w->Cooldown).N("accuracy", w->Accuracy)
					.B("missile", w->Missile).N("missileSpeedKmS", w->MissileSpeedKmS).I("salvo", w->Salvo).I("ammo", w->Ammo).B("antiSmall", w->AntiSmall)
					.N("muzzleSpeedKmS", w->MuzzleSpeedKmS).B("lance", w->Lance).N("falloffEdgeShare", w->FalloffEdgeShare);
				e.Tags = { "weapon", "battle-profile" };
				Add(std::move(e));
			}

			// Ship classes and their designs.
			for (int c = 0; c <= static_cast<int>(ShipClass::CaptureTug); c++)
			{
				if (c == 7) continue; // the retired Breacher hull number (Ships.h)
				auto cls = static_cast<ShipClass>(c);
				auto spec = ShipClassSpec::Of(cls);
				if (spec == nullptr) continue;
				std::string cid = "ship-class-" + Slug(ToString(cls));
				Entity e{ cid, "ship-class", spec->DisplayName.empty() ? std::string(ToString(cls)) : spec->DisplayName,
					cs::Cat(spec->DisplayName, ": a ", spec->IsMilitary ? "military" : "civilian", " hull class, ", Num(spec->AccelG), " g sustained drive.") };
				e.Details.S("enum", ToString(cls)).B("military", spec->IsMilitary).N("accelG", spec->AccelG);
				e.Tags = { "ship-class", spec->IsMilitary ? "military" : "civilian" };
				Add(std::move(e));
				const auto& designs = CombatSpec::DesignsOf(cls);
				for (const auto& d : designs)
				{
					std::string name = d->DesignName.empty() ? std::string(ToString(cls)) : d->DesignName;
					Entity de{ "ship-design-" + Slug(ToString(cls)) + "-" + Slug(name), "ship-design", name,
						cs::Cat(name, ": a ", spec->DisplayName, " design, ", Num(d->LengthKm * 1000), " m long, hull ", Num(d->Hull), ", shield ", Num(d->Shield),
							", armour ", Num(d->Armor), ", crew ", Num(d->Crew), ".") };
					std::vector<std::string> hardpoints;
					for (const auto& h : d->Layout) hardpoints.push_back(ToString(h->Kind));
					de.Details.S("class", ToString(cls)).N("lengthKm", d->LengthKm).N("hull", d->Hull).N("shield", d->Shield).N("shieldRegen", d->ShieldRegen)
						.N("armor", d->Armor).N("maxSpeedKmS", d->MaxSpeed).N("turnRateDegS", d->TurnRateDegS).N("massKt", d->MassKt).N("crew", d->Crew)
						.N("marines", d->Marines).I("squadrons", d->Squadrons).N("dryMassTonnes", d->DryMassTonnes).N("propellantTonnes", d->PropellantTonnes)
						.N("exhaustVelocityKmS", d->ExhaustVelocityKmS).A("hardpoints", hardpoints);
					de.Tags = { "ship-design" };
					auto did = Add(std::move(de));
					U.Rel(did, cid, "design-of");
				}
			}

			// Biomes: the nine painted kinds a place beyond Sol can be (and Sol's own).
			for (int k = 0; k < static_cast<int>(BiomeKind::Count); k++)
			{
				auto b = Biomes::Of(static_cast<BiomeKind>(k), 0);
				Entity e{ std::string("biome-") + b.Id, "biome", b.Name, cs::Cat(b.Name, ": ", b.Mood, b.Hazards[0] ? cs::Cat(" Hazards: ", b.Hazards) : std::string()) };
				Obj fields;
				fields.N("nebula", b.Fields.Nebula).N("iceField", b.Fields.IceField).N("graveyard", b.Fields.Graveyard).N("asteroids", b.Fields.Asteroids)
					.N("dust", b.Fields.Dust).N("storms", b.Fields.Storms).N("heat", b.Fields.Heat);
				e.Details.S("kind", b.Id).S("mood", b.Mood).S("hazards", b.Hazards).O("fields", fields);
				e.Tags = { "biome" };
				Add(std::move(e));
			}
		}

		std::string BiomeOf(const std::string& placeName) { return std::string("biome-") + Biomes::For(placeName).Id; }

		// -------------------------------------------------------------- factions

		std::vector<cs::Ref<Faction>> AllFactions()
		{
			std::vector<cs::Ref<Faction>> all;
			std::unordered_set<const Faction*> seen;
			auto add = [&](const cs::Ref<Faction>& f) { if (f && seen.insert(f.get()).second) all.push_back(f); };
			for (const auto& f : galaxy->Polities->Factions()) add(f);
			for (const auto& s : galaxy->Systems) for (const auto& f : s->World->Fleet->Factions()) add(f);
			return all;
		}

		void Factions()
		{
			for (const auto& f : AllFactions())
			{
				const char* kind = ToString(f->Kind);
				Entity e{ FactionId(f), "faction", f->Name,
					cs::Cat(f->Name, ": ", f->IsPirate ? "a pirate clan" : f->Kind == PolityKind::State ? "a state with a navy" : f->Kind == PolityKind::Company ? "a company"
						: f->Kind == PolityKind::FreeTraders ? "independent shipping" : "a polity", ", flying the prefix ", f->Prefix.empty() ? "(none)" : f->Prefix, ".") };
				e.Details.S("polityKind", kind).S("prefix", f->Prefix).B("pirate", f->IsPirate).B("military", f->IsMilitary).B("house", f->IsHouse)
					.N("treasuryCredits", f->Treasury).N("marineQuality", f->MarineQuality).N("crewMorale", f->CrewMorale).Raw("color", Rgb3(f->Color));
				if (std::isfinite(f->Manpower)) e.Details.N("manpower", f->Manpower);
				e.Tags = { "faction", EnumSlug(kind) };
				if (f->IsPirate) e.Tags.push_back("pirate");
				Add(std::move(e), false);
			}
		}

		// -------------------------------------------------------------- systems

		void System(const cs::Ref<StarSystem>& s)
		{
			if (s->IsVoidBase && VoidStationName(s->Name)) { VoidStation(s->Name, s); return; }
			std::string sys = SysSlug(s->Name);
			std::string id = "system-" + sys;
			placeByName[Lower(s->Name)] = id;
			auto holder = s->Holder();
			Entity e{ id, "star-system", s->Name,
				s->Spec && !s->Spec->Summary.empty() ? s->Spec->Summary
					: cs::Cat(s->Name, ": a simulated star system ", Num(s->PositionLy.Length()), " ly from Sol.") };
			e.Details.B("simulated", true).Vec("positionLy", s->PositionLy).N("distanceFromSolLy", s->PositionLy.Length()).O("galactic", Coords(s->Origin))
				.B("voidBase", s->IsVoidBase).B("settled", s->Settled()).B("openToSettlement", s->OpenToSettlement()).N("extentKm", s->ExtentKm)
				.S("holder", holder ? holder->Name : "").S("biome", Biomes::For(s->Name).Id).S("discovery", galaxy->IsUndiscovered(s->Name) ? "undiscovered" : "discovered");
			if (!std::isnan(s->OpenedJd)) e.Details.N("openedJd", s->OpenedJd);
			e.Tags = { "star-system", "simulated" };
			Add(std::move(e), false);
			U.Rel(id, BiomeOf(s->Name), "has-biome");
			if (holder) U.Rel(FactionId(holder), id, "holds");

			Bodies(s, sys, id);
			Settlements(s, sys);
			Features(s->Catalog->Root()->Name, sys, id, s->Name);
			Gates(s, sys, id);
			Ships(s, sys, id);
			Harvest(s, sys);
			Megastructures(s, sys);
		}

		std::unordered_map<int, std::string> bodyIds; // per system, rebuilt in Bodies

		void Bodies(const cs::Ref<StarSystem>& s, const std::string& sys, const std::string& systemId)
		{
			bodyIds.clear();
			const auto& cat = s->Catalog;
			for (int i = 0; i < cat->Count(); i++)
			{
				auto b = (*cat)[i];
				std::string type = KindType(b->Kind);
				std::string id = type + "-" + sys + "-" + Slug(b->Name);
				std::string summary = b->Summary;
				if (summary.empty())
				{
					std::string parent = b->ParentIndex >= 0 ? (*cat)[b->ParentIndex]->Name : std::string();
					summary = cs::Cat(b->Name, ": a ", type, parent.empty() ? std::string() : cs::Cat(" orbiting ", parent), " in the ", s->Name, ".");
				}
				Entity e{ id, type, b->Name, summary };
				e.Details.S("kind", ToString(b->Kind)).S("system", s->Name).N("radiusKm", b->RadiusKm).N("gmKm3S2", b->Gm).S("status", ToString(b->Status))
					.N("rotationHours", b->RotationHours).B("gasGiant", b->GasGiant).B("rings", b->HasRings()).B("habitatSite", b->HabitatSite).Raw("color", Rgb3(b->Color));
				if (b->Elements)
				{
					Obj orbit;
					orbit.N("semiMajorAxisKm", b->Elements->AKm).N("semiMajorAxisAu", b->Elements->AKm / Astro::AuKm).N("eccentricity", b->Elements->E)
						.N("inclinationDeg", b->Elements->IDeg).N("epochJd", b->Elements->EpochJd);
					e.Details.O("orbit", orbit);
				}
				if (b->IsStar()) e.Details.S("spectralType", b->SpectralType).N("teffK", b->TeffK).N("luminositySolar", b->LuminositySolar).B("singularity", b->Singularity);
				// US-T239: what the scan card reads off a world, so a writer sees the numbers a player sees.
				if (b->IsVisible() && !b->IsStar())
				{
					Obj scan;
					if (b->Kind != BodyKind::Station) scan.N("surfaceGravityG", ScanFacts::SurfaceGravityG(*b)).S("atmosphere", ScanFacts::Atmosphere(*b));
					double teq = ScanFacts::EquilibriumK(*cat, i);
					if (std::isfinite(teq)) scan.N("equilibriumTempK", teq);
					if (b->DiscoveryYear > 0) scan.S("habitableZone", ToString(b->HabitableZone));
					e.Details.O("scan", scan);
				}
				if (b->DiscoveryYear) e.Details.I("discoveryYear", b->DiscoveryYear);
				if (b->MassIsMinimum) e.Details.B("massIsMinimum", true);
				if (!b->CoOrbitalWith.empty()) e.Details.S("coOrbitalWith", b->CoOrbitalWith);
				const VoidSite* site = b->ParentIndex >= 0 ? VoidSites::Find(b->Name, (*cat)[b->ParentIndex]->Name) : nullptr;
				if (site) e.Details.S("voidSite", VoidSites::KindLabel(b));
				e.Tags = { type, b->IsVisible() ? "body" : "barycentre" };
				if (b->Status == BodyStatus::Fictional) e.Tags.push_back("fictional");
				id = Add(std::move(e));
				bodyIds[i] = id;
				U.Rel(id, systemId, "located-in");
				if (b->ParentIndex >= 0) U.Rel(id, bodyIds[b->ParentIndex], "orbits");
			}
		}

		void Settlements(const cs::Ref<StarSystem>& s, const std::string& sys)
		{
			for (const auto& st : s->World->Economy->Settlements)
			{
				bool haven = st->Kind == SettlementKind::Haven;
				std::string type = haven ? "pirate-base" : "settlement";
				std::string where = bodyIds.count(st->Body) ? (*s->Catalog)[st->Body]->Name : std::string();
				Entity e{ "settlement-" + sys + "-" + Slug(st->Name), type, st->Name,
					cs::Cat(st->Name, ": ", haven ? "a pirate haven" : cs::Cat("a ", Lower(EnumSlug(ToString(st->Kind)))), where.empty() ? std::string() : cs::Cat(" at ", where),
						" in the ", s->Name, ", population ", Num(std::round(st->Population)), st->Owner ? cs::Cat(", held by ", st->Owner->Name) : std::string(), st->Fallen ? " (fallen)" : "", ".") };
				Obj stock, price;
				for (int g = 0; g < GoodInfo::Count; g++)
				{
					if (st->Stock[g] > 0.5) stock.N(ToString(static_cast<Good>(g)), std::round(st->Stock[g]));
					if (st->Price[g] > 0) price.N(ToString(static_cast<Good>(g)), st->Price[g]);
				}
				e.Details.S("kind", ToString(st->Kind)).S("system", s->Name).S("body", where).S("owner", st->Owner ? st->Owner->Name : "")
					.N("population", st->Population).B("shipyard", st->HasShipyard()).B("hidden", st->Hidden).B("lawless", st->Lawless).B("fallen", st->Fallen)
					.N("unrest", st->Unrest).S("occupiedFrom", st->OccupiedFrom ? st->OccupiedFrom->Name : "").I("defencePlatforms", st->Platforms.Count())
					.O("stockTonnes", stock).O("pricePerTonne", price);
				{
					// US-T239: the scan card's industry and people lines.
					std::vector<Obj> industry;
					for (const auto& f : st->Facilities)
					{
						if (f == nullptr || f->Recipe == nullptr) continue;
						Obj o;
						o.S("recipe", f->Recipe->Name).N("level", f->Level).N("utilisation", f->Utilisation);
						industry.push_back(o);
					}
					e.Details.Raw("industry", ArrOfObj(industry)).N("staffing", st->Staffing).N("netMigrationPerDay", st->NetMigration)
						.B("capital", st->Owner != nullptr && st->Owner->Capital == st);
				}
				e.Tags = { type, EnumSlug(ToString(st->Kind)) };
				if (st->HasShipyard()) e.Tags.push_back("shipyard");
				auto id = Add(std::move(e));
				settlementIds[st.get()] = id;
				if (bodyIds.count(st->Body)) U.Rel(id, bodyIds[st->Body], "located-in");
				if (st->Owner) U.Rel(FactionId(st->Owner), id, "owns");
				if (st->Owner && st->Owner->Capital == st) U.Rel(id, FactionId(st->Owner), "capital-of");
				if (st->OccupiedFrom) U.Rel(id, FactionId(st->OccupiedFrom), "occupied-from");
				for (int g = 0; g < GoodInfo::Count; g++)
					if (st->Stock[g] > 0.5) U.Rel(id, "good-" + Slug(ToString(static_cast<Good>(g))), "stocks");
			}
		}

		/// <summary>The nebulas, ice fields and graveyards of the place whose root body is <paramref name="root"/>.</summary>
		void Features(const std::string& root, const std::string& sys, const std::string& placeId, const std::string& placeName)
		{
			const auto& f = SpaceFeatures::Of(root);
			for (const auto& n : f.Nebulae)
			{
				Entity e{ "nebula-" + sys + "-" + Slug(n.Name), "nebula", n.Name,
					cs::Cat(n.Name, ": a nebula of ", std::to_string(n.Puffs.Count()), " glowing knots, ", Num(n.RadiusKm / Astro::AuKm), " AU across its reach, in ", placeName,
						". It swallows sensors: a haven inside is found only by flying in close or pinging.") };
				e.Details.S("place", placeName).Vec("centreKm", n.CentreKm).N("radiusKm", n.RadiusKm).I("puffs", n.Puffs.Count());
				e.Tags = { "nebula", "hazard" };
				auto id = Add(std::move(e));
				U.Rel(id, placeId, "located-in");
			}
			for (const auto& c : f.IceFields)
			{
				Entity e{ "ice-field-" + sys + "-" + Slug(c.Name), "ice-field", c.Name,
					cs::Cat(c.Name, ": a cloud of ", std::to_string(c.Chunks.Count()), " glittering ice chunks in ", placeName, ".") };
				e.Details.S("place", placeName).Vec("centreKm", c.CentreKm).N("radiusKm", c.RadiusKm).I("chunks", c.Chunks.Count());
				e.Tags = { "ice-field", "hazard" };
				auto id = Add(std::move(e));
				U.Rel(id, placeId, "located-in");
			}
			for (const auto& g : Graveyards::Of(root))
			{
				std::string gid = "graveyard-" + sys + "-" + Slug(g.Name);
				Entity e{ gid, "graveyard", g.Name,
					cs::Cat(g.Name, ": a ship graveyard in ", placeName, ", ", std::to_string(g.Hulks.Count()), " hulks and ", std::to_string(g.Stations.Count()),
						" derelict stations in a ", g.Warm ? "warm orange" : "cold blue-white", " haze.") };
				e.Details.S("place", placeName).Vec("centreKm", g.CentreKm).N("radiusKm", g.RadiusKm).B("warm", g.Warm).I("hulks", g.Hulks.Count())
					.I("derelictStations", g.Stations.Count()).I("wreckageChunks", g.ChunkOffsetsKm.Count());
				e.Tags = { "graveyard", "salvage" };
				gid = Add(std::move(e));
				U.Rel(gid, placeId, "located-in");
				for (const auto& h : g.Hulks)
				{
					Entity he{ "derelict-" + sys + "-" + Slug(h.Name), "derelict", h.Name,
						cs::Cat(h.Name, ": the hulk of a ", ToString(h.Class), " lying in ", g.Name, h.CargoTonnes > 0 ? cs::Cat(", ", Num(std::round(h.CargoTonnes)), " t of ", ToString(h.Cargo), " still aboard") : std::string(), ".") };
					he.Details.S("class", ToString(h.Class)).Vec("offsetKm", h.OffsetKm).S("cargo", h.CargoTonnes > 0 ? ToString(h.Cargo) : "").N("cargoTonnes", h.CargoTonnes);
					he.Tags = { "derelict", "hulk" };
					auto hid = Add(std::move(he));
					U.Rel(hid, gid, "located-in");
					U.Rel(hid, "ship-class-" + Slug(ToString(h.Class)), "is-class");
					if (h.CargoTonnes > 0) U.Rel(hid, "good-" + Slug(ToString(h.Cargo)), "carries");
				}
				for (const auto& d : g.Stations)
				{
					Entity de{ "derelict-station-" + sys + "-" + Slug(d.Name), "derelict-station", d.Name,
						cs::Cat(d.Name, ": a dark, holed station ", Num(d.SizeKm), " km long in ", g.Name, ", salvageable.") };
					de.Details.Vec("offsetKm", d.OffsetKm).N("sizeKm", d.SizeKm);
					de.Tags = { "derelict-station", "derelict" };
					auto did = Add(std::move(de));
					U.Rel(did, gid, "located-in");
				}
			}
		}

		void Gates(const cs::Ref<StarSystem>& s, const std::string& sys, const std::string& systemId)
		{
			for (const auto& g : s->Gates())
			{
				auto to = g->Destination();
				std::string name = g->Name.empty() ? cs::Cat(s->Name, " gate to ", to ? to->Name : std::string("nowhere")) : g->Name;
				Entity e{ "gate-" + sys + "-" + (to ? SysSlug(to->Name) : Slug(name)), "jump-gate", name,
					cs::Cat(name, ": a jump gate of the ", s->Name, to ? cs::Cat(" leading to ", to->Name) : std::string(), g->Owner ? cs::Cat(", held by ", g->Owner->Name) : std::string(),
						g->Working() ? "." : " (not working).") };
				e.Details.S("system", s->Name).S("destination", to ? to->Name : "").S("owner", g->Owner ? g->Owner->Name : "").N("integrity", g->Integrity)
					.B("working", g->Working()).Vec("positionKm", g->PositionKm).N("traffic", g->Traffic).I("ambushes", g->Ambushes);
				e.Tags = { "jump-gate" };
				auto id = Add(std::move(e));
				U.Rel(id, systemId, "located-in");
				if (g->Owner) U.Rel(FactionId(g->Owner), id, "owns");
				if (to) U.Rel(systemId, "system-" + SysSlug(to->Name), "gate-link");
			}
		}

		void Ships(const cs::Ref<StarSystem>& s, const std::string& sys, const std::string& systemId)
		{
			const auto& fleet = s->World->Fleet;
			for (const auto& ship : fleet->Ships())
			{
				if (ship->Destroyed || ship->GateDeparted || ship->Scrapped) continue;
				if (shipIds.count(ship.get())) continue;
				std::string where;
				int body = ship->Orbit ? ship->Orbit->Body : ship->FrameBody;
				if (bodyIds.count(body)) where = bodyIds[body];
				if (ship->Derelict) { MissingFleetShip(s, ship, sys, systemId, where); continue; }
				auto id = ShipEntity(ship, "ship-" + sys + "-" + Slug(ship->Name), s->Name, systemId, where);
				shipWorld[ship.get()] = s->World;
				(void)id;
			}
		}

		std::string ShipEntity(const cs::Ref<Ship>& ship, const std::string& wantId, const std::string& placeName, const std::string& placeId, const std::string& bodyId)
		{
			auto cls = ship->Spec ? ship->Spec->Class : ShipClass::Freighter;
			std::string design = ship->Combat && ship->Combat->Spec ? ship->Combat->Spec->DesignName : std::string();
			bool military = ship->Spec && ship->Spec->IsMilitary;
			Entity e{ wantId, "ship", ship->Name,
				cs::Cat(ship->Name, ": a ", ship->Spec ? ship->Spec->DisplayName : std::string(ToString(cls)),
					design.empty() || (ship->Spec && design == ship->Spec->DisplayName) ? std::string() : cs::Cat(" (", design, ")"),
					ship->Faction ? cs::Cat(" of the ", ship->Faction->Name) : std::string(), ", ", Lower(ToString(ship->Role)), " role, in ", placeName,
					!ship->Cargo.empty() && ship->HasCargo ? cs::Cat(", carrying ", ship->Cargo) : std::string(), ".") };
			e.Details.S("class", ToString(cls)).S("design", design).S("role", ToString(ship->Role)).S("faction", ship->Faction ? ship->Faction->Name : "")
				.S("place", placeName).S("phase", ToString(ship->Phase)).B("military", military).N("cargoTonnes", ship->CargoTonnes).S("cargo", ship->HasCargo ? ship->Cargo : "")
				.N("capacityTonnes", ship->CapacityTonnes).I("tripsCompleted", ship->TripsCompleted).N("credits", ship->Credits);
			if (ship->Leader) e.Details.S("leader", ship->Leader->Name);
			if (ship->Company) e.Details.S("company", ship->Company->Name);
			if (ship->Combat != nullptr && ship->Combat->Interior != nullptr)
			{
				// US-T239: the scan card's crew and hull lines.
				Obj scan;
				scan.S("hullClass", ship->Spec ? ship->Spec->DisplayName : std::string()).N("lengthKm", ship->LengthKm())
					.N("accelG", ship->Spec ? ship->Spec->AccelG : 0.0).N("crew", ship->Combat->Interior->Crew()).N("marines", ship->Combat->Interior->Marines());
				e.Details.O("scan", scan);
			}
			e.Tags = { "ship", military ? "military" : "civilian", EnumSlug(ToString(cls)) };
			auto id = Add(std::move(e));
			shipIds[ship.get()] = id;
			U.Rel(id, placeId, "located-in");
			if (!bodyId.empty()) U.Rel(id, bodyId, "near");
			if (ship->Faction) U.Rel(FactionId(ship->Faction), id, "owns");
			U.Rel(id, "ship-class-" + Slug(ToString(cls)), "is-class");
			if (!design.empty())
			{
				// A refit or one-off design the class catalogue does not list (CombatSpec::Clone keeps the name): catalogued here.
				std::string did = "ship-design-" + Slug(ToString(cls)) + "-" + Slug(design);
				if (!U.Has(did))
				{
					const auto& d = ship->Combat->Spec;
					Entity de{ did, "ship-design", design, cs::Cat(design, ": a ", ToString(cls), " design flown in the galaxy but not in the yards' catalogue, ",
						Num(d->LengthKm * 1000), " m long, hull ", Num(d->Hull), ", shield ", Num(d->Shield), ".") };
					de.Details.S("class", ToString(cls)).N("lengthKm", d->LengthKm).N("hull", d->Hull).N("shield", d->Shield).N("armor", d->Armor).N("crew", d->Crew).B("catalogued", false);
					de.Tags = { "ship-design", "uncatalogued" };
					Add(std::move(de));
					U.Rel(did, "ship-class-" + Slug(ToString(cls)), "design-of");
				}
				U.Rel(id, did, "is-design");
			}
			if (ship->HasCargo) for (int g = 0; g < GoodInfo::Count; g++)
				if (Lower(ship->Cargo) == Lower(GoodInfo::Of(static_cast<Good>(g))->Name)) U.Rel(id, "good-" + Slug(ToString(static_cast<Good>(g))), "carries");
			return id;
		}

		/// <summary>A hull left drifting with its crew gone (Ship.Derelict): missing, with where it was last known.</summary>
		void MissingFleetShip(const cs::Ref<StarSystem>& s, const cs::Ref<Ship>& ship, const std::string& sys, const std::string& systemId, const std::string& bodyId)
		{
			DVec3 helio;
			bool known = false;
			try { helio = s->World->Fleet->StateOf(ship, s->World->Ephemeris->Jd).HelioKm; known = true; }
			catch (...) {}
			Entity e{ "missing-ship-" + sys + "-" + Slug(ship->Name), "missing-ship", ship->Name,
				cs::Cat(ship->Name, ": a ", ship->Spec ? ship->Spec->DisplayName : std::string("hull"), ship->Faction ? cs::Cat(" of the ", ship->Faction->Name) : std::string(),
					" found adrift in the ", s->Name, " with nobody aboard, derelict since JD ", Num(ship->DerelictSinceJd), ".") };
			e.Details.S("class", ship->Spec ? ToString(ship->Spec->Class) : "").S("faction", ship->Faction ? ship->Faction->Name : "").S("system", s->Name)
				.N("missingSinceJd", ship->DerelictSinceJd).S("lostTo", ship->DerelictBy ? ship->DerelictBy->Name : "");
			Obj last;
			last.S("frame", s->Name).N("asOfJd", s->World->Ephemeris->Jd);
			if (known) { last.Vec("systemKm", helio); last.O("galactic", Coords(s->At(helio))); }
			e.Details.O("lastKnown", last);
			e.Tags = { "missing-ship", "derelict" };
			auto id = Add(std::move(e));
			shipIds[ship.get()] = id;
			U.Rel(id, systemId, "last-seen-in");
			if (!bodyId.empty()) U.Rel(id, bodyId, "near");
			if (ship->Faction) U.Rel(FactionId(ship->Faction), id, "owns");
			if (ship->DerelictBy) U.Rel(id, FactionId(ship->DerelictBy), "lost-to");
		}

		void Harvest([[maybe_unused]] const cs::Ref<StarSystem>& s, [[maybe_unused]] const std::string& sys)
		{
#ifdef SG_EXPORT_HARVEST
			const auto& h = s->World->Harvest;
			if (h == nullptr) return; // made the first time a pilot comes near (World::EnsureHarvest): not at galaxy build
			for (const auto& op : h->Operations)
			{
				std::string where = bodyIds.count(op->Body) ? bodyIds[op->Body] : std::string();
				Entity e{ "gas-harvester-" + sys + "-" + Slug(op->Name), "gas-harvester", op->Name,
					cs::Cat(op->Name, ": gas harvesting works, ", std::to_string(op->Platforms.Count()), " platforms and ", std::to_string(op->Tankers.Count()), " tankers",
						op->Owner ? cs::Cat(", run by the ", op->Owner->Name) : std::string(), ".") };
				std::vector<std::string> platforms;
				for (const auto& p : op->Platforms) platforms.push_back(p->Name);
				e.Details.S("owner", op->Owner ? op->Owner->Name : "").S("port", op->Port ? op->Port->Name : "").A("platforms", platforms).I("raids", op->Raids)
					.I("tankersLost", op->TankersLost).I("deliveries", op->Deliveries);
				e.Tags = { "gas-harvester" };
				auto id = Add(std::move(e));
				if (!where.empty()) U.Rel(id, where, "orbits");
				if (op->Owner) U.Rel(FactionId(op->Owner), id, "owns");
				if (op->Port && settlementIds.count(op->Port.get())) U.Rel(id, settlementIds[op->Port.get()], "supplies");
				for (const auto& t : op->Tankers)
				{
					if (!t->Lost) continue;
					Entity me{ "missing-ship-" + sys + "-" + Slug(t->Name), "missing-ship", t->Name,
						cs::Cat(t->Name, ": a gas tanker of ", op->Name, " taken under way and never seen again.") };
					Obj last;
					last.S("frame", s->Name).S("leg", "tanker run").N("departJd", t->DepartJd).N("arriveJd", t->ArriveJd).N("progress", t->Progress(jd));
					me.Details.S("works", op->Name).O("lastKnown", last);
					me.Tags = { "missing-ship", "tanker" };
					auto mid = Add(std::move(me));
					U.Rel(mid, id, "last-seen-near");
				}
			}
#endif
		}

		void Megastructures(const cs::Ref<StarSystem>& s, const std::string& sys)
		{
			const auto& mining = s->World->Mining;
			if (mining == nullptr) return;
			for (const auto& p : mining->Projects)
			{
				Entity e{ "megastructure-" + sys + "-" + Slug(p->Name), "megastructure", p->Name,
					cs::Cat(p->Name, ": a ", p->Spec ? p->Spec->Name : std::string("megastructure"), p->Done() ? " standing" : " being built", p->Builder ? cs::Cat(" by the ", p->Builder->Name) : std::string(),
						" in the ", s->Name, ".") };
				e.Details.S("design", p->Spec ? p->Spec->Name : "").S("builder", p->Builder ? p->Builder->Name : "").I("stage", p->Stage).N("progress", p->Progress())
					.N("startJd", p->StartJd).B("done", p->Done());
				e.Tags = { "megastructure" };
				auto id = Add(std::move(e));
				if (bodyIds.count(p->SiteBody)) U.Rel(id, bodyIds[p->SiteBody], "orbits");
				if (p->Builder) U.Rel(FactionId(p->Builder), id, "owns");
				if (p->Spec) U.Rel(id, "tech-megastructure-" + Slug(p->Spec->Name), "built-to");
			}
		}

		// -------------------------------------------------------------- void stations

		static bool VoidStationName(const std::string& name)
		{
			for (const auto& v : VoidStations::Stations()) if (v->Name == name) return true;
			return false;
		}

		std::unordered_set<std::string> voidDone;

		void VoidStationsAll()
		{
			for (const auto& v : VoidStations::Stations()) VoidStation(v->Name, nullptr, v);
		}

		/// <summary>A void station: from its plan (its name alone, the same in every game), plus what runs there if it is simulated.</summary>
		void VoidStation(const std::string& name, const cs::Ref<StarSystem>& simulated, cs::Ref<StarEntry> entry = nullptr)
		{
			if (!voidDone.insert(name).second) return;
			if (entry == nullptr) for (const auto& v : VoidStations::Stations()) if (v->Name == name) { entry = v; break; }
			std::string sys = Slug(name);
			std::string id = "void-station-" + sys.substr(std::string("void-station-").size() <= sys.size() && sys.rfind("void-station-", 0) == 0 ? std::string("void-station-").size() : 0);
			placeByName[Lower(name)] = id;
			auto plan = VoidSites::Plan(name);
			auto bridges = VoidStations::Bridges(name);
			DVec3 ly = simulated ? simulated->PositionLy : entry ? entry->PositionLy : DVec3();
			Entity e{ id, "void-station", name,
				cs::Cat(name, ": a relay standing in open space ", Num(ly.Length()), " ly from Sol, where a capital ship's drive must cool between jumps; no star, only its artificial gravity generator",
					bridges.first.empty() ? std::string() : cs::Cat(", on the link between ", bridges.first, " and ", bridges.second), ". Lawless: no state ever holds it.") };
			e.Details.Vec("positionLy", ly).N("distanceFromSolLy", ly.Length()).B("simulated", simulated != nullptr).S("discovery", galaxy->IsUndiscovered(name) ? "undiscovered" : "discovered")
				.S("biome", Biomes::For(name).Id).S("field", plan.FieldName).S("fieldStyle", FieldStyle(plan.Mix.Field)).S("fieldOre", ToString(plan.FieldOre))
				.B("nebula", plan.Mix.Nebula).B("megastructure", plan.Mix.Megastructure).B("graveyard", plan.Mix.Graveyard);
			if (!bridges.first.empty()) e.Details.A("bridges", { bridges.first, bridges.second });
			e.Tags = { "void-station", "lawless" };
			Add(std::move(e), false);
			U.Rel(id, BiomeOf(name), "has-biome");

			// Its sites.
			auto site = [&](const std::string& type, const std::string& siteName, const std::string& summary, Obj details, std::vector<std::string> tags) {
				Entity se{ type + "-" + sys + "-" + Slug(siteName), type, siteName, summary };
				se.Details = std::move(details);
				se.Tags = std::move(tags);
				auto sid = Add(std::move(se));
				U.Rel(sid, id, "located-in");
				return sid;
			};
			std::string fieldId;
			if (!plan.FieldName.empty())
			{
				int rocks = 0;
				for (const auto& s : plan.Sites) if (s.Kind == VoidSiteKind::FieldRock) rocks++;
				Obj d; d.S("style", FieldStyle(plan.Mix.Field)).S("ore", ToString(plan.FieldOre)).N("innerKm", plan.FieldInnerKm).N("outerKm", plan.FieldOuterKm).I("rocks", rocks);
				std::string type = plan.Mix.Field == VoidFieldStyle::IceField ? "ice-field" : "asteroid-field";
				fieldId = site(type, plan.FieldName, cs::Cat(plan.FieldName, ": ", type == "ice-field" ? "glittering chunks of ice" : "an asteroid drift", " circling ", name,
					", worked for nickel-iron, ice and ", ToString(plan.FieldOre), "."), d, { type, "mining" });
				U.Rel(fieldId, "good-" + Slug(ToString(plan.FieldOre)), "yields");
			}
			if (!plan.OutpostName.empty())
			{
				Obj d; d.S("kind", "MiningOutpost").N("population", plan.OutpostPopulation).S("place", name);
				auto oid = site("settlement", plan.OutpostName, cs::Cat(plan.OutpostName, ": the assay camp on ", plan.FieldName, "'s biggest rock, buying the drift's ore."), d, { "settlement", "mining-outpost" });
				if (!fieldId.empty()) U.Rel(oid, fieldId, "located-in");
			}
			if (!plan.PlanetoidName.empty())
			{
				const VoidSite* p = plan.Find(plan.PlanetoidName);
				Obj d; if (p) d.B("exploded", p->Exploded).N("radiusKm", p->RadiusKm).N("orbitKm", p->AKm);
				site("planetoid", plan.PlanetoidName, cs::Cat(plan.PlanetoidName, ": what is left of a planetoid that ", p && p->Exploded ? "blew apart" : "cracked", ", wrapped in its own debris, near ", name, "."),
					d, { "planetoid", "debris" });
			}
			for (const auto& s : plan.Sites)
			{
				if (s.Kind != VoidSiteKind::Sight) continue;
				Obj d; d.S("sight", SightName(s.Sight)).N("radiusKm", s.RadiusKm).N("orbitKm", s.AKm);
				site("sight", s.Name, s.Summary.empty() ? cs::Cat(s.Name, ": an oddity crews detour to see while the drive cools.") : s.Summary, d, { "sight", "landmark" });
			}
			if (plan.Mix.Nebula && !plan.NebulaName.empty())
			{
				Obj d; d.N("radiusKm", plan.NebulaRadiusKm).S("place", name);
				site("nebula", plan.NebulaName, cs::Cat(plan.NebulaName, ": the nebula ", name, " sits in, hiding a pirate haven in its depths."), d, { "nebula", "hazard" });
			}
			if (!plan.HavenName.empty())
			{
				Obj d; d.S("denRock", plan.DenName).S("place", name);
				auto hid = site("pirate-base", plan.HavenName, cs::Cat(plan.HavenName, ": a pirate haven dug into ", plan.DenName, ", deep in ", plan.NebulaName, "."), d, { "pirate-base", "haven", "hidden" });
				(void)hid;
			}
			if (plan.Mix.Megastructure && !plan.MegastructureName.empty())
			{
				Obj d; d.N("population", plan.MegastructurePopulation).S("place", name);
				site("megastructure", plan.MegastructureName, cs::Cat(plan.MegastructureName, ": a colossal inhabited megastructure, stacked tower blocks lit by thousands of windows, docking spars reaching far out; ",
					Num(std::round(plan.MegastructurePopulation)), " live in it."), d, { "megastructure", "inhabited" });
			}
			// Bridges (the systems whose link it stands on), resolved once every place is known (Politics()).
			if (!bridges.first.empty()) { pendingBridges.emplace_back(id, bridges.first); pendingBridges.emplace_back(id, bridges.second); }

			Features(name, sys, id, name);
			if (simulated)
			{
				Bodies(simulated, sys, id);
				Settlements(simulated, sys);
				Gates(simulated, sys, id);
				Ships(simulated, sys, id);
			}
		}

		std::vector<std::pair<std::string, std::string>> pendingBridges;

		static const char* FieldStyle(VoidFieldStyle f)
		{
			switch (f) { case VoidFieldStyle::Drift: return "drift"; case VoidFieldStyle::Rubble: return "rubble"; case VoidFieldStyle::MetalSwarm: return "metal-swarm"; case VoidFieldStyle::IceField: return "ice-field"; }
			return "drift";
		}

		static const char* SightName(VoidSight s)
		{
			switch (s)
			{
			case VoidSight::GlassMoonlet: return "glass-moonlet"; case VoidSight::FrozenFountain: return "frozen-fountain"; case VoidSight::DerelictArk: return "derelict-ark";
			case VoidSight::StoneArch: return "stone-arch"; case VoidSight::Lantern: return "lantern"; default: return "none";
			}
		}

		// -------------------------------------------------------------- charted stars (not simulated)

		void ChartedStars()
		{
			auto one = [&](const NearbyStar& n, bool discovered) {
				if (VoidStationName(n.Name) || placeByName.count(Lower(n.Name))) return;
				std::string id = "charted-star-" + Slug(n.Name);
				placeByName[Lower(n.Name)] = id;
				Entity e{ id, "star", n.Name,
					cs::Cat(n.Name, ": a ", n.SpectralType.empty() ? std::string("star") : cs::Cat(n.SpectralType, " star"), " ", Num(n.PositionLy.Length()), " ly from Sol, charted but not yet reached by any gate",
						discovered ? "." : " (undiscovered).") };
				e.Details.B("simulated", false).S("discovery", discovered ? "discovered" : "undiscovered").Vec("positionLy", n.PositionLy).N("distanceFromSolLy", n.PositionLy.Length())
					.S("spectralType", n.SpectralType).N("apparentMagnitude", n.ApparentMagnitude).N("teffK", n.TeffK).S("biome", Biomes::For(n.Name).Id);
				e.Tags = { "star", "charted", discovered ? "discovered" : "undiscovered" };
				Add(std::move(e), false);
				U.Rel(id, BiomeOf(n.Name), "has-biome");
				Features(n.Name, Slug(n.Name), id, n.Name);
			};
			for (const auto& n : galaxy->NearbyStars) one(n, true);
			for (const auto& n : galaxy->Undiscovered()) one(n, false);
		}

		// -------------------------------------------------------------- deep space

		void DeepSpaceObjects()
		{
			if (galaxy->DeepSpace == nullptr) return;
			for (const auto& o : galaxy->DeepSpace->Objects())
			{
				std::string kind = EnumSlug(ToString(o->Kind));
				bool missing = o->Kind == DeepSpaceObjectKind::Derelict || o->Kind == DeepSpaceObjectKind::Distress;
				std::string type = missing ? "missing-ship" : o->Kind == DeepSpaceObjectKind::Wreck ? "wreck" : kind;
				Entity e{ type + "-deep-" + Slug(o->Name), type, o->Name, "" };
				DVec3 ly = o->Position.Ly();
				switch (o->Kind)
				{
				case DeepSpaceObjectKind::Derelict: e.Summary = cs::Cat(o->Name, ": a hull left drifting whole between the stars, its crew gone; last known ", Num(ly.Length()), " ly from Sol."); break;
				case DeepSpaceObjectKind::Distress: e.Summary = cs::Cat(o->Name, ": a hull adrift calling for help between the stars, ", std::to_string(o->Survivors), " aboard (or a pirate lure dressed up as one)."); break;
				case DeepSpaceObjectKind::Wreck: e.Summary = cs::Cat(o->Name, ": what is left of a hull destroyed between the stars."); break;
				case DeepSpaceObjectKind::RoguePlanet: e.Summary = cs::Cat(o->Name, ": a planet with no star, wandering between them; its well pulls passing groups out of FTL."); break;
				case DeepSpaceObjectKind::Nebula: e.Summary = cs::Cat(o->Name, ": a cloud of gas and dust between the stars where sensors see only a fraction as far."); break;
				case DeepSpaceObjectKind::IceBody: e.Summary = cs::Cat(o->Name, ": a comet adrift between the stars, water for a group short of reaction mass."); break;
				case DeepSpaceObjectKind::Relay: e.Summary = cs::Cat(o->Name, ": a deep-space relay passing news between systems when the gates cannot."); break;
				case DeepSpaceObjectKind::ListeningPost: e.Summary = cs::Cat(o->Name, ": a listening post on an approach, hearing enemies pass."); break;
				}
				e.Details.S("deepSpaceKind", ToString(o->Kind)).O(missing ? "lastKnown" : "position", Coords(o->Position)).N("sinceJd", o->SinceJd).N("radiusKm", o->RadiusKm)
					.N("salvageCredits", o->SalvageCredits).S("lostBy", o->LostBy ? o->LostBy->Name : "").S("owner", o->Owner ? o->Owner->Name : "")
					.S("routeA", o->RouteA).S("routeB", o->RouteB).N("iceTonnes", o->IceTonnes).I("survivors", o->Survivors).B("offNetwork", o->OffNetwork);
				if (std::isfinite(o->ExpiresJd)) e.Details.N("expiresJd", o->ExpiresJd);
				e.Tags = { type, "deep-space" };
				auto id = Add(std::move(e));
				if (o->LostBy) U.Rel(FactionId(o->LostBy), id, missing ? "lost" : "lost");
				if (o->Owner) U.Rel(FactionId(o->Owner), id, "owns");
				if (!o->RouteA.empty()) pendingRoutes.emplace_back(id, o->RouteA);
				if (!o->RouteB.empty()) pendingRoutes.emplace_back(id, o->RouteB);
			}
			// The groups crossing open space under their own drives.
			for (const auto& c : galaxy->DeepSpace->Cruises())
			{
				auto pos = c->PositionAt(jd);
				for (const auto& ship : c->Ships)
				{
					if (shipIds.count(ship.get()) || ship->Destroyed) continue;
					auto id = ShipEntity(ship, "ship-deep-" + Slug(ship->Name), "deep space", "", "");
					if (auto* e = U.Find(id))
					{
						Obj cruise; cruise.S("from", c->From ? c->From->Name : "").S("to", c->To ? c->To->Name : c->ToName).O("position", Coords(pos));
						e->Details.O("cruise", cruise);
						e->Tags.push_back("in-deep-space");
					}
					if (c->From) pendingRoutes.emplace_back(id, c->From->Name);
					if (!c->ToName.empty() || c->To) pendingDest.emplace_back(id, c->To ? c->To->Name : c->ToName);
				}
			}
		}

		std::vector<std::pair<std::string, std::string>> pendingRoutes, pendingDest;

		void InTransit()
		{
			if (!galaxy->Polities->InTransit) return;
			for (const auto& ship : galaxy->Polities->InTransit())
			{
				if (!ship || shipIds.count(ship.get()) || ship->Destroyed) continue;
				auto id = ShipEntity(ship, "ship-transit-" + Slug(ship->Name), "a jump gate", "", "");
				if (auto* e = U.Find(id)) e->Tags.push_back("in-transit");
			}
		}

		// -------------------------------------------------------------- pilots

		void PilotsAll()
		{
			std::cout << "meeting pilots\n";
			for (const auto& s : galaxy->Systems)
			{
				const auto& world = s->World;
				auto chars = Characters::Of(world);
				for (const auto& ship : world->Fleet->Ships())
				{
					auto it = shipIds.find(ship.get());
					if (it == shipIds.end() || ship->Derelict) continue;
					auto pilot = chars->Meet(world, ship, MeetKind::Mentioned, jd);
					if (pilot == nullptr) continue;
					Pilot(pilot, it->second);
				}
			}
		}

		void Pilot(const cs::Ref<NamedPilot>& p, const std::string& shipId)
		{
			auto story = Characters::LifeStory(p, jd, false, 64);
			std::string life;
			for (const auto& line : story) { if (!life.empty()) life += " "; life += line; }
			Entity e{ "pilot-" + Slug(p->Name), "character", p->Name,
				cs::Cat(p->Styled(), p->Callsign.empty() ? std::string() : cs::Cat(" (\"", p->Callsign, "\")"), ", ", std::to_string(p->AgeAt(jd)), ", ",
					p->Faction ? cs::Cat("of the ", p->Faction->Name) : std::string("unaffiliated"), ", flying the ", p->ShipName, ".") };
			Obj traits;
			traits.N("courage", p->Courage).N("greed", p->Greed).N("honour", p->Honour).N("warmth", p->Warmth).N("spite", p->Spite).N("chatter", p->Chatter);
			std::vector<std::string> flown(p->ShipsFlown.begin(), p->ShipsFlown.end());
			std::vector<std::string> grudges(p->Grudges.begin(), p->Grudges.end());
			std::vector<std::string> lines(story.begin(), story.end());
			std::vector<Obj> beats;
			for (const auto& b : p->Story)
			{
				Obj o; o.N("jd", b->Jd).S("kind", ToString(b->Kind));
				Obj slots;
				for (int i = 0; i + 1 < b->Slots.Count(); i += 2) slots.S(b->Slots[i], b->Slots[i + 1]);
				o.O("slots", slots);
				beats.push_back(o);
			}
			e.Details.S("role", "pilot").S("styled", p->Styled()).S("callsign", p->Callsign).S("pronoun", p->Pronoun ? "he" : "she").N("bornJd", p->BornJd)
				.I("ageYears", p->AgeAt(jd)).S("birthplace", p->Birthplace).S("faction", p->Faction ? p->Faction->Name : "").S("ship", p->ShipName)
				.S("career", ToString(p->Track)).S("rank", Careers::Title(p->Track, p->Rank)).I("rankIndex", p->Rank).N("renown", p->Renown).O("traits", traits)
				.A("shipsFlown", flown).I("battlesSurvived", p->BattlesSurvived).I("robberies", p->Robberies).A("grudges", grudges)
				.S("lifeStory", life).A("lifeStoryLines", lines).Raw("lifeEvents", ArrOfObj(beats)).S("ashore", p->Ashore);
			e.Tags = { "character", "pilot", EnumSlug(ToString(p->Track)) };
			auto id = Add(std::move(e));
			U.Rel(id, shipId, "crew-of");
			if (p->Faction) U.Rel(id, FactionId(p->Faction), "member-of");
			pilotByName[Lower(p->Name)] = id;
			for (const auto& g : p->Grudges) grudgesByName.emplace_back(id, g);
		}

		std::unordered_map<std::string, std::string> pilotByName;

		// -------------------------------------------------------------- US-T274: the galaxy's people and organisations

		void People()
		{
			auto society = SpaceGod::Core::Society::Find(*galaxy);
			auto civ = SpaceGod::Core::Civilization::Find(*galaxy);
			if (society == nullptr) return;
			std::unordered_map<const Household*, std::string> houseIds;
			for (const auto& h : society->HousesOf(nullptr))
			{
				auto head = h->Head();
				Entity e{ "house-" + Slug(h->Surname) + "-" + std::to_string(h->Id), "faction", cs::Cat("House ", h->Surname),
					cs::Cat("House ", h->Surname, ", a family of ", h->Home != nullptr ? h->Home->Name : cs::String("nowhere"), h->Business != nullptr && h->Business->Trading() ? cs::Cat(", running ", h->Business->Name) : cs::String(""), ".") };
				e.Details.S("kind", "house").S("home", h->Home != nullptr ? h->Home->Name : "").N("wealth", h->Wealth).I("living", h->Living())
					.S("business", h->Business != nullptr ? h->Business->Name : "").S("businessKind", h->Business != nullptr ? ToString(h->Business->Kind) : "")
					.S("head", head != nullptr ? head->FullName() : "");
				e.Tags = { "faction", "house" };
				auto id = Add(std::move(e));
				houseIds[h.get()] = id;
				if (h->Home != nullptr && h->Home->Owner != nullptr) U.Rel(id, FactionId(h->Home->Owner), "member-of");
				// Its notable people: the head, and anyone famous.
				for (const auto& c : h->Members)
				{
					if (!c->Alive() || (c->Role != CitizenRole::Head && c->Reputation < 0.5)) continue;
					std::string traits;
					for (int bit = 1; bit <= (1 << 11); bit <<= 1)
						if (c->Traits & bit) { if (!traits.empty()) traits += ", "; traits += ToString(static_cast<Trait>(bit)); }
					Entity p{ "citizen-" + Slug(c->FullName()) + "-" + std::to_string(c->Id), "character", c->FullName(),
						cs::Cat(c->FullName(), ", ", std::to_string(static_cast<int>(c->AgeYears(jd))), ", ", ToString(c->Role), " of House ", h->Surname, ".") };
					p.Details.S("role", "citizen").S("houseRole", ToString(c->Role)).I("ageYears", static_cast<int>(c->AgeYears(jd))).S("traits", traits)
						.S("ambition", ToString(c->Goal)).N("reputation", c->Reputation).N("skill", c->Skill).S("faith", c->Faith != nullptr ? c->Faith->Name : "");
					p.Tags = { "character", "citizen" };
					auto pid = Add(std::move(p));
					U.Rel(pid, id, "member-of");
				}
			}
			if (civ == nullptr) return;
			for (const auto& o : civ->Orgs)
			{
				if (!o->Active()) continue;
				std::string kind = LoreKeys::Slug(ToString(o->Kind));
				Entity e{ "org-" + kind + "-" + Slug(o->Name) + "-" + std::to_string(o->Id), "faction", o->Name,
					cs::Cat(o->Name, ", a ", ToString(o->Kind), o->Seat != nullptr ? cs::Cat(" of ", o->Seat->Name) : cs::String(""), o->Leader != nullptr ? cs::Cat(", led by ", o->Leader->FullName()) : cs::String(""), ".") };
				e.Details.S("kind", ToString(o->Kind)).S("seat", o->Seat != nullptr ? o->Seat->Name : "").N("strength", o->Strength).N("stance", o->Stance)
					.N("funds", o->Funds).I("members", o->Members.Count()).S("leader", o->Leader != nullptr ? o->Leader->FullName() : "")
					.S("nameKey", SpaceGod::Core::Narrator::NamesKey(o->Kind));
				e.Tags = { "faction", "org", kind };
				auto id = Add(std::move(e));
				if (o->Faction != nullptr) U.Rel(id, FactionId(o->Faction), o->Kind == OrgKind::Agency || o->Kind == OrgKind::Party ? "member-of" : "allied-with");
				if (o->Owner != nullptr)
					if (auto it = houseIds.find(o->Owner.get()); it != houseIds.end()) U.Rel(id, it->second, "owned-by");
			}
			// The kinds of event the Narrator words, each a Prose bark pool (docs/PROSE.md, "Social narration").
			for (int i = 1; i < static_cast<int>(SocialKind::Count_); i++)
			{
				auto k = static_cast<SocialKind>(i);
				std::string lines;
				for (const auto& l : SpaceGod::Core::Narrator::Templates(k)) { if (!lines.empty()) lines += " | "; lines += l; }
				Entity e{ "social-" + SpaceGod::Core::Narrator::Slug(k), "concept", ToString(k), cs::Cat("A kind of happening among the galaxy's people, worded from the bark pool ", SpaceGod::Core::Narrator::Key(k), ".") };
				e.Details.S("kind", "social-event").S("key", SpaceGod::Core::Narrator::Key(k)).S("fallbackLines", lines).I("count", civ->Count(k));
				e.Tags = { "concept", "social-event" };
				Add(std::move(e));
			}
		}

		// -------------------------------------------------------------- politics and the edges by name

		std::string ResolveName(const std::string& name)
		{
			std::string l = Lower(name);
			if (auto it = pilotByName.find(l); it != pilotByName.end()) return it->second;
			if (auto it = placeByName.find(l); it != placeByName.end()) return it->second;
			std::string f = LoreKeys::Faction(name);
			if (U.Has(f)) return f;
			for (const auto& [ship, id] : shipIds) if (Lower(ship->Name) == l) return id;
			return std::string();
		}

		void Politics()
		{
			const auto& pol = galaxy->Polities;
			auto factions = AllFactions();
			// Wars between the systems' powers.
			if (galaxy->Wars)
				for (const auto& w : galaxy->Wars->Active())
					if (w->Aggressor && w->Defender && !w->Over()) U.Rel(FactionId(w->Aggressor), FactionId(w->Defender), "at-war-with");
			// Hostility, opinion and pacts between every pair of states.
			for (const auto& a : factions)
				for (const auto& b : factions)
				{
					if (a == b) continue;
					if (pol->Diplomacy && pol->Diplomacy->Hostile(a, b) && !a->IsPirate && !b->IsPirate) U.Rel(FactionId(a), FactionId(b), "hostile-to");
					if (pol->Statecraft && Statecraft::Party(a) && Statecraft::Party(b))
					{
						double o = pol->Statecraft->Opinion(a, b);
						if (o <= -40) U.Rel(FactionId(a), FactionId(b), "rival-of");
						else if (o >= 60) U.Rel(FactionId(a), FactionId(b), "friendly-to");
					}
				}
			if (pol->Statecraft)
				for (const auto& p : pol->Statecraft->Pacts())
					if (p->A && p->B && p->Kind) U.Rel(FactionId(p->A), FactionId(p->B), "pact-" + Slug(p->Kind->Name));
			// Every world's rival pair, and the grievances crews hold over what was done to them by radio.
			for (const auto& s : galaxy->Systems)
			{
				const auto& w = s->World;
				if (w->RivalA && w->RivalB) U.Rel(FactionId(w->RivalA), FactionId(w->RivalB), "rival-of");
				auto chars = w->Law ? w->Law->Pilots : nullptr;
				if (chars)
					for (const auto& [key, harm] : chars->Grievance)
					{
						auto bar = key.find('|');
						if (bar == std::string::npos || harm <= 0) continue;
						U.Rel("faction-" + Slug(key.substr(0, bar)), "faction-" + Slug(key.substr(bar + 1)), "grudge-against");
					}
			}
			// Pilots' grudges, by name, against whoever they hold them against (a pilot, a ship, a faction, a place). A
			// captain known only from someone's story ("Captain Mara "Lanebreaker" Seddon", met by nobody yet) becomes a
			// character of their own, mentioned only, so the grudge has someone to point at.
			for (const auto& [pilot, target] : grudgesByName)
			{
				auto to = ResolveName(target);
				std::string plain = PlainName(target);
				if (to.empty() && plain != target) to = ResolveName(plain);
				if (to.empty() && plain != target)
				{
					Entity e{ "pilot-" + Slug(plain), "character", plain, cs::Cat(target, ": known in the lanes only from the stories of those who hold a grudge against them.") };
					e.Details.S("role", "pilot").S("styled", target).B("mentionedOnly", true);
					e.Tags = { "character", "pilot", "mentioned" };
					to = Add(std::move(e));
					pilotByName[Lower(plain)] = to;
				}
				if (!to.empty()) U.Rel(pilot, to, "grudge-against");
			}
			for (const auto& [from, name] : pendingBridges) { auto to = ResolveName(name); if (!to.empty()) U.Rel(from, to, "bridges"); }
			for (const auto& [from, name] : pendingRoutes) { auto to = ResolveName(name); if (!to.empty()) U.Rel(from, to, "on-route-from"); }
			for (const auto& [from, name] : pendingDest) { auto to = ResolveName(name); if (!to.empty()) U.Rel(from, to, "bound-for"); }
			// Capitals and research.
			for (const auto& f : factions)
			{
				auto cap = pol->CapitalOf(f);
				if (cap && settlementIds.count(cap.get())) U.Rel(settlementIds[cap.get()], FactionId(f), "capital-of");
				if (pol->Command)
					for (const auto& t : ResearchTrack::All())
					{
						int level = pol->Command->Level(f, t);
						if (level > 0) U.Rel(FactionId(f), "tech-" + Slug(t->Name), "researched");
					}
			}
		}

	public:
		/// <summary>Relations dropped because their target never got an entity, by kind (printed, and kept out of the file:
		/// a dangling edge would make Prose invent a stub entity, which the round-trip check would report as a leak).</summary>
		std::map<std::string, int> Dropped;

	private:
		void Prune()
		{
			for (auto& e : U.Entities)
				e.Relations.erase(std::remove_if(e.Relations.begin(), e.Relations.end(), [&](const auto& r) {
					if (U.Has(r.first)) return false;
					Dropped[r.second + " -> " + r.first.substr(0, r.first.find('-', r.first.find('-') + 1))]++;
					return true;
				}), e.Relations.end());
		}

		void Manifest()
		{
			// The export's own record (round-trips through Prose as an ordinary entity, so a pull shows which build and seed
			// the canon came from): the build hash, the inputs, and the counts the round-trip check holds Prose to.
			Entity e{ "spacegod-export-manifest", "manifest", "SpaceGod export manifest",
				cs::Cat("The SpaceGod universe as generated by build ", args.Build, " from seed ", std::to_string(args.Seed), ", run ", Num(args.Days), " days from JD ", Num(args.StartJd), ".") };
			size_t edges = 0;
			for (const auto& x : U.Entities) edges += x.Relations.size();
			Obj counts;
			for (const auto& [t, n] : TypeCounts) counts.I(t, n);
			counts.I("manifest", 1);
			e.Details.S("buildHash", args.Build).I("seed", args.Seed).N("startJd", args.StartJd).N("simDays", args.Days).N("endJd", jd)
				.S("generator", "tools/universe-export (US-T171)").I("schemaVersion", 1).I("entityCount", static_cast<long long>(U.Entities.size() + 1))
				.I("edgeCount", static_cast<long long>(edges)).O("entitiesByType", counts)
				.S("ownership", "Simulation facts in this file belong to the game and are read-only canon in Prose; narrative fields belong to Prose.");
			e.Tags = { "manifest" };
			Add(std::move(e), false);
		}
	};

	void Write(const Universe& u, const Args& args, const std::string& path)
	{
		std::ostringstream o;
		// US-T236: the file's provenance, in the interchange's own top-level source block (Prose keeps it with the universe
		// and returns it on export); the manifest entity keeps the counts. No timestamp: same build and seed, same bytes.
		o << "{\n  \"source\": {\"consumer\": \"spacegod\", \"generator\": \"tools/universe-export\", \"build\": " << Esc(args.Build)
		  << ", \"seed\": " << args.Seed << ", \"startJd\": " << Num(args.StartJd) << ", \"simDays\": " << Num(args.Days) << "},\n";
		o << "  \"universe\": {\n";
		o << "    \"id\": \"spacegod\",\n";
		o << "    \"name\": \"SpaceGod\",\n";
		o << "    \"tagline\": " << Esc("Become any unit in a living galaxy that runs whether you watch it or not.") << ",\n";
		o << "    \"era\": " << Esc("The near future: the Sol Federation, the Centauri Accord and the frontier colonies, joined by jump gates and void stations.") << ",\n";
		o << "    \"setting\": " << Esc("The real neighbourhood of the Sun out to 100 light-years at its real positions, every system simulated on one clock: economies, trade, mining, wars, pirates and the space between the stars.") << ",\n";
		o << "    \"logline\": " << Esc("The SpaceGod can embody any ship, station, turret, worker or pilot; whatever it does in a body, that pilot did, and owns it afterwards.") << ",\n";
		o << "    \"rules\": [\n";
		const char* rules[] = {
			"Simulation facts (orbits, positions, stats, cargo, histories) belong to the game and are read-only canon here; narrative fields (lore, voice, motives, rumours, technology descriptions) belong to Prose.",
			"Whatever the SpaceGod did in a body, its pilot did: every act becomes the pilot's own choice, with a motive from their personality, never a hint of being steered.",
			"Sol is kept as it really is; beyond Sol every system and void station is its own wild, painted place with a biome.",
			"Every feature reads right at real scale and at game scale.",
			"Void stations are lawless: no state ever holds one.",
		};
		for (size_t i = 0; i < std::size(rules); i++) o << "      " << Esc(rules[i]) << (i + 1 < std::size(rules) ? ",\n" : "\n");
		o << "    ]\n  },\n  \"entities\": [\n";
		for (size_t i = 0; i < u.Entities.size(); i++)
		{
			const auto& e = u.Entities[i];
			o << "    {\"id\": " << Esc(e.Id) << ", \"type\": " << Esc(e.Type) << ", \"name\": " << Esc(e.Name) << ", \"summary\": " << Esc(e.Summary)
			  << ", \"details\": " << e.Details.Str() << ", \"relations\": [";
			for (size_t r = 0; r < e.Relations.size(); r++)
				o << (r ? ", " : "") << "{\"to\": " << Esc(e.Relations[r].first) << ", \"kind\": " << Esc(e.Relations[r].second) << "}";
			o << "], \"tags\": [";
			for (size_t t = 0; t < e.Tags.size(); t++) o << (t ? ", " : "") << Esc(e.Tags[t]);
			o << "]}" << (i + 1 < u.Entities.size() ? ",\n" : "\n");
		}
		o << "  ]\n}\n";
		std::ofstream f(path, std::ios::binary);
		if (!f) throw std::runtime_error("cannot write " + path);
		f << o.str();
	}
}

int main(int argc, char** argv)
{
	try
	{
		Args args = Parse(argc, argv);
		Exporter x(args);
		x.Run();
		Write(x.U, args, args.Out);
		size_t edges = 0;
		for (const auto& e : x.U.Entities) edges += e.Relations.size();
		int dropped = 0;
		for (const auto& [k, n] : x.Dropped) dropped += n;
		std::cout << "wrote " << args.Out << ": " << x.U.Entities.size() << " entities, " << edges << " edges (" << dropped << " dangling dropped, "
				  << x.U.Duplicates << " duplicate ids skipped)\n";
		for (const auto& [t, n] : x.TypeCounts) std::cout << "  " << t << " " << n << "\n";
		for (const auto& [k, n] : x.Dropped) std::cout << "  dropped " << k << " " << n << "\n";
		return 0;
	}
	catch (const std::exception& ex)
	{
		std::cerr << "UniverseExport: " << ex.what() << "\n";
		return 1;
	}
}
