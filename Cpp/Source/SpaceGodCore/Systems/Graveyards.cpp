// US-T131: ship graveyards and derelict stations (see Graveyards.h).

#include "Systems/Graveyards.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/War.h"
#include "Combat/Wreck.h"
#include "Economy/Goods.h"
#include "Systems/VoidSites.h"
#include "Systems/VoidStations.h"

#include <map>
#include <mutex>

namespace SpaceGod::Core
{
	namespace
	{
		/// <summary>The place's own stream: SplitMix64 from the FNV-1a of its name and a tag.</summary>
		class Stream
		{
			uint64_t state;

		public:
			explicit Stream(const cs::String& name)
			{
				uint64_t h = 0xCBF29CE484222325ull;
				for (unsigned char c : name) { h ^= c; h *= 0x100000001B3ull; }
				state = h ^ 0x632BE59BD9B4E019ull;
			}

			uint64_t Next()
			{
				uint64_t z = (state += 0x9E3779B97F4A7C15ull);
				z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
				z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
				return z ^ (z >> 31);
			}

			double U() { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }
			double Range(double a, double b) { return a + (b - a) * U(); }
			int Int(int n) { return static_cast<int>(Next() % static_cast<uint64_t>(n)); }
			DVec3 InBall()
			{
				for (;;)
				{
					DVec3 v(Range(-1, 1), Range(-1, 1), Range(-1, 1));
					if (v.LengthSquared() <= 1) return v;
				}
			}
		};

		const cs::List<cs::String>& HulkNames()
		{
			static const cs::List<cs::String> n = { "Perseverance", "Last Light", "Iron Psalm", "Gallant", "Steadfast", "Old Tom", "Calloway",
				"Harrier", "Mercy", "Ninth Hour", "Vigil", "Tidewater", "Brannigan", "Sorrow", "Albatross", "Corinth", "Hesperus",
				"Long Haul", "Kestrel", "Unconquered", "Penitent", "Bulwark", "Wayfarer", "Graywater" };
			return n;
		}

		const cs::List<cs::String>& YardNames()
		{
			static const cs::List<cs::String> n = { "Boneyard", "Graveyard", "Drowned Fleet", "Breakers' Field", "Silent Armada", "Hulk Reach",
				"Wrack", "Last Muster", "Rust Tide", "Cenotaph" };
			return n;
		}

		Graveyard Make(Stream& rng, const cs::String& place, DVec3 centre, double radius, bool warm, int hulks, int stations)
		{
			Graveyard g;
			g.Name = cs::Cat(place, " ", YardNames()[rng.Int(YardNames().Count())]);
			g.CentreKm = centre;
			g.RadiusKm = radius;
			g.Warm = warm;
			g.Seed = static_cast<uint32_t>(rng.Next());
			// Capital wrecks and the bulk freighters of a lost convoy, at many depths through the field.
			static const cs::List<ShipClass> classes = { ShipClass::Battleship, ShipClass::Carrier, ShipClass::Cruiser, ShipClass::Freighter,
				ShipClass::Freighter, ShipClass::Destroyer, ShipClass::CommandCruiser, ShipClass::Miner };
			static const cs::List<Good> holds = { Good::Alloys, Good::Machinery, Good::Electronics, Good::Munitions, Good::NickelIron, Good::Food };
			auto names = HulkNames();
			for (int i = 0; i < hulks; i++)
			{
				GraveHulk h;
				int pick = rng.Int(names.Count());
				h.Name = cs::Cat("the ", names[pick], " (", g.Name, ")");
				names.RemoveAt(pick);
				h.Class = classes[rng.Int(classes.Count())];
				auto p = rng.InBall();
				h.OffsetKm = centre + DVec3(p.X, p.Y, p.Z * 0.5) * (radius * 0.8);
				h.Cargo = holds[rng.Int(holds.Count())];
				h.CargoTonnes = rng.Range(40, 900);
				g.Hulks.Add(h);
			}
			for (int i = 0; i < stations; i++)
			{
				DerelictStation st;
				static const cs::List<cs::String> sn = { "Waystation", "Relay", "Depot", "Hab", "Outpost", "Drydock", "Anchorage" };
				static const cs::List<cs::String> sw = { "Kepler", "Meridian", "Tolland", "Vashti", "Orison", "Halcyon", "Ruskin", "Amaranth" };
				st.Name = cs::Cat(sw[rng.Int(sw.Count())], " ", sn[rng.Int(sn.Count())], " (derelict)");
				auto p = rng.InBall();
				st.OffsetKm = centre + DVec3(p.X, p.Y, p.Z * 0.5) * (radius * 0.5);
				st.SizeKm = rng.Range(2, 6);
				st.Seed = static_cast<uint32_t>(rng.Next());
				g.Stations.Add(st);
			}
			// The drawn wreckage: very dense, chunks to container-sized pieces, thicker toward the heart.
			int chunks = hulks > 0 ? 900 : 260;
			for (int i = 0; i < chunks; i++)
			{
				auto p = rng.InBall();
				double r = cs::Math::Pow(p.Length(), 0.7);
				auto dir = p.Length() > 1e-9 ? p / p.Length() : DVec3::UnitX;
				g.ChunkOffsetsKm.Add(centre + DVec3(dir.X, dir.Y, dir.Z * 0.6) * (r * radius));
				g.ChunkRadiiKm.Add(0.01 * cs::Math::Pow(30.0, rng.U() * rng.U()));
			}
			return g;
		}

		/// <summary>
		/// US-T85: Wolf 359's graveyard, the same in every game. A picket fleet met something here long ago and none of it
		/// left: their hulks lie in a cold field round what they met, a cube kilometres on a side, still and faintly lit.
		/// </summary>
		Graveyard WolfMuster(Stream& rng)
		{
			const cs::String place = "Wolf 359";
			double au = 5.5;
			double radius = 0.014 * Astro::AuKm;
			DVec3 centre = DVec3(0.6, -0.8, 0.05).Normalized() * (au * Astro::AuKm);
			// The picket line, warships all, broken where they stood.
			static const cs::List<cs::String> names = { "Resolute Dawn", "Halberd", "Ardent", "Watchfire", "Lodestar", "Morrow",
				"Thessaly", "Covenant Light", "Ninefold", "Tenebrae", "Cassiopeia", "Hold Fast" };
			// Laid as any hulked graveyard (its haze, its dense wreckage), then given its own hulks and name.
			Graveyard g = Make(rng, place, centre, radius, false, names.Count(), 0);
			g.Hulks.Clear();
			g.Name = "Wolf 359 Muster";
			static const cs::List<ShipClass> classes = { ShipClass::Cruiser, ShipClass::Destroyer, ShipClass::Battleship, ShipClass::Destroyer,
				ShipClass::CommandCruiser, ShipClass::Cruiser, ShipClass::Carrier, ShipClass::Destroyer };
			static const cs::List<Good> holds = { Good::Munitions, Good::Alloys, Good::Electronics, Good::Machinery };
			for (int i = 0; i < names.Count(); i++)
			{
				GraveHulk h;
				h.Name = cs::Cat("the ", names[i], " (", g.Name, ")");
				h.Class = classes[i % classes.Count()];
				// A ring round the heart, as a line drawn up to bar the way would fall.
				double a = cs::Math::PI * 2 * (i + rng.Range(-0.3, 0.3)) / names.Count();
				double r = radius * rng.Range(0.35, 0.8);
				h.OffsetKm = centre + DVec3(cs::Math::Cos(a) * r, cs::Math::Sin(a) * r, rng.Range(-0.15, 0.15) * radius);
				h.Cargo = holds[rng.Int(holds.Count())];
				h.CargoTonnes = rng.Range(30, 260);
				g.Hulks.Add(h);
			}
			DerelictStation cube;
			cube.Name = "Unison Cube (silent)";
			cube.OffsetKm = centre;
			cube.SizeKm = 3;
			cube.Seed = static_cast<uint32_t>(rng.Next());
			cube.Cube = true;
			cube.Summary = "At the Muster's heart hangs a cube three kilometres on a side, its every face a grown lattice of conduit "
				"and alcove: a vessel of the Unison, the many-in-one that thought with one mind. A picket fleet stood against it here "
				"and lies round it still. It has not moved since. Its lights have never quite gone out.";
			g.Stations.Add(cube);
			return g;
		}

		cs::List<Graveyard> Build(const cs::String& root, double teffK, double luminosity)
		{
			cs::List<Graveyard> list;
			if (root.empty() || root == "Sun" || root == "Sol" || root == "Sol System") return list;
			if (root == "Wolf 359")
			{
				Stream wolf(cs::Cat(root, "/muster"));
				list.Add(WolfMuster(wolf));
				return list;
			}
			if (cs::Strings::StartsWith(root, "Void Base")) return list;
			Stream rng(cs::Cat(root, "/graveyards"));
			auto Away = [&](double loKm, double hiKm)
			{
				double a = rng.Range(0, cs::Math::PI * 2), z = rng.Range(-0.2, 0.2);
				return DVec3(cs::Math::Cos(a), cs::Math::Sin(a), z).Normalized() * rng.Range(loKm, hiKm);
			};
			if (cs::Strings::StartsWith(root, VoidStations::Prefix))
			{
				auto plan = VoidSites::Plan(root);
				if (plan.Mix.Graveyard)
				{
					// Out past the station's sites, in the cold light of its tiny suns.
					// (Wide enough that its hulks, drawn at ship scale, stand apart in the game scale too.)
					double radius = rng.Range(80000, 150000);
					double out = plan.OverviewSpanKm * 1.05 + radius;
					list.Add(Make(rng, root, Away(out, out * 1.3), radius, rng.U() < 0.25, 3 + rng.Int(4), rng.U() < 0.5 ? 1 : 0));
				}
				return list;
			}
			// A star system: a graveyard close in to a hot star burns orange in its light; by a cool star, or far out, it is cold.
			if (rng.U() < 0.3)
			{
				bool hot = teffK >= 5000 && luminosity >= 0.3;
				bool close = hot && rng.U() < 0.7;
				double au = close ? rng.Range(0.25, 0.9) * cs::Math::Sqrt(cs::Math::Max(0.3, luminosity)) : rng.Range(4, 18);
				list.Add(Make(rng, root, Away(au * Astro::AuKm, au * Astro::AuKm * 1.05), rng.Range(0.006, 0.02) * Astro::AuKm,
					close, 4 + rng.Int(5), rng.U() < 0.6 ? 1 : 0));
			}
			// And now and then a derelict station on its own.
			if (rng.U() < 0.2)
				list.Add(Make(rng, root, Away(2 * Astro::AuKm, 12 * Astro::AuKm), rng.Range(300, 900), rng.U() < 0.3, 0, 1));
			return list;
		}

		std::mutex& Gate() { static std::mutex m; return m; }
		// Derived, never saved: rebuilt from the place's name whenever it is needed.
		std::map<cs::String, cs::List<Graveyard>>& Cache() { static std::map<cs::String, cs::List<Graveyard>> m; return m; }
	}

	cs::Func<cs::Ref<Interior>, cs::Ref<Wreck>, DerelictStation>& Graveyards::StationInterior()
	{
		static cs::Func<cs::Ref<Interior>, cs::Ref<Wreck>, DerelictStation> hook = nullptr;
		return hook;
	}

	const cs::List<Graveyard>& Graveyards::Of(const cs::Ref<BodyDefinition>& root)
	{
		std::lock_guard<std::mutex> lock(Gate());
		auto& cache = Cache();
		auto it = cache.find(root->Name);
		if (it != cache.end()) return it->second;
		return cache[root->Name] = Build(root->Name, root->TeffK, root->LuminositySolar);
	}

	const cs::List<Graveyard>& Graveyards::Of(const cs::String& rootName)
	{
		std::lock_guard<std::mutex> lock(Gate());
		auto& cache = Cache();
		auto it = cache.find(rootName);
		if (it != cache.end()) return it->second;
		// Not yet planned from its root (a void station's needs no star): planned as a cold place.
		return cache[rootName] = Build(rootName, 3000, 0.01);
	}

	void Graveyards::Populate(const cs::Ref<World>& world, double jd)
	{
		if (world == nullptr || world->War == nullptr) return;
		auto root = world->Catalog()->Root();
		const auto& yards = Of(root);
		if (yards.Count() == 0) return;
		// Laid already (this game, or a saved one): its wrecks carry the graveyard's names.
		for (const auto& w : world->War->Wrecks->Items)
			for (const auto& g : yards)
			{
				for (const auto& h : g.Hulks) if (w->Name == h.Name) return;
				for (const auto& st : g.Stations) if (w->Name == st.Name) return;
			}
		auto owner = world->Independents != nullptr ? world->Independents : world->Sol;
		if (owner == nullptr) return;
		auto Hull = [&](const cs::String& name, ShipClass cls, DVec3 at, Good cargo, double tonnes, bool inFleet)
		{
			auto ship = cs::New<Ship>();
			ship->Name = name;
			ship->Spec = ShipClassSpec::Of(cls);
			ship->Faction = owner;
			ship->Combat = cs::New<ShipCombat>(CombatSpec::Of(cls), cs::Nullable<ShipClass>(cls));
			ship->HasCargo = tonnes > 0;
			ship->CargoGood = cargo;
			ship->CargoTonnes = tonnes;
			if (inFleet)
			{
				// In the fleet, destroyed, so it is drawn as the broken wreck any battle leaves.
				world->Fleet->Receive(ship);
				Fleet::PinAt(ship, root->Index, at, DVec3::UnitX, jd);
				ship->Destroyed = true;
				world->NotifyAdded(ship);
			}
			return world->War->Wrecks->Add(ship, root->Index, at, jd, LifetimeDays);
		};
		for (const auto& g : yards)
		{
			for (const auto& h : g.Hulks) Hull(h.Name, h.Class, h.OffsetKm, h.Cargo, h.CargoTonnes, true);
			for (const auto& st : g.Stations)
			{
				// A station's wreck is no ship's: drawn as a station (StationOf), salvaged for its stores.
				auto w = Hull(st.Name, ShipClass::Freighter, st.OffsetKm, Good::Machinery, 300 + 200 * st.SizeKm, false);
				w->Ship = nullptr;
				w->Pieces.Clear();
				w->LengthKm = st.SizeKm;
				if (st.Cube)
				{
					// No stores, no sick bay: what is stripped off the cube is its own strange machinery.
					w->Salvage.Add(SalvageStack(Good::Electronics, 200 * st.SizeKm));
					w->Salvage.Add(SalvageStack(Good::Alloys, 150 * st.SizeKm));
				}
				else
				{
					w->Salvage.Add(SalvageStack(Good::Electronics, 60 * st.SizeKm));
					w->Salvage.Add(SalvageStack(Good::MedicalSupplies, 20 * st.SizeKm));
				}
			}
		}
		world->Log(jd, cs::Cat("Wrecks lie in the dark here: ", yards[0].Name), WorldEventKind::Info, -1);
		for (const auto& g : yards)
			for (const auto& st : g.Stations)
				if (!st.Summary.empty()) world->Log(jd, st.Summary, WorldEventKind::Info, -1);
	}

	cs::List<LurkSite> Graveyards::LurkSites(const cs::Ref<World>& world)
	{
		cs::List<LurkSite> list;
		if (world == nullptr) return list;
		for (const auto& g : Of(world->Catalog()->Root()))
		{
			if (g.Hulks.Count() > 0) list.Add(LurkSite { g.Name, g.CentreKm, g.RadiusKm * 0.5, false });
			for (const auto& st : g.Stations) list.Add(LurkSite { st.Name, st.OffsetKm, st.SizeKm * 3, true });
		}
		return list;
	}

	const DerelictStation* Graveyards::StationOf(const cs::Ref<World>& world, const cs::Ref<Wreck>& wreck)
	{
		if (world == nullptr || wreck == nullptr || wreck->Ship != nullptr) return nullptr;
		for (const auto& g : Of(world->Catalog()->Root()))
			for (const auto& st : g.Stations)
				if (st.Name == wreck->Name) return &st;
		return nullptr;
	}
}
