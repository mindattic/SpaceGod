// US-T186: the spawn menu, the dev menu's Spawn tab (Shift+F2 opens the menu on it; declarations in UnitySpaceApp.Cheats.inl).
// Every hull in the ship catalog, every kind of station, the pirate groups and the Bio Mass growths, at any time, for the
// faction chosen: Spawn puts it beside the player (the hull flown, else the camera) on its own AI, Spawn and Pilot puts the
// player at its helm. Whatever is spawned hostile to a ship near it engages it at once (UnitySpaceApp::CheatEngage).

#include "UnitySpaceApp.h"

#include "BodyCatalog.h"
#include "Combat/BioMass.h"
#include "Combat/CombatSpecs.h"
#include "Combat/Construction.h"
#include "Combat/Diplomacy.h"
#include "Combat/ShipCatalog.h"
#include "Combat/War.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Infrastructure/Infrastructure.h"
#include "PracticeSpawn.h"
#include "Ships.h"
#include "SimClock.h"
#include "Target.h"
#include "Views/StructureViews.h"
#include "World.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		using UI::CheatEntry;
		using Entries = std::vector<CheatEntry>;

		/// Which of <see cref="SpawnFactions"/> the menu spawns for (wrapped onto the list as it is now).
		int spawnFaction = 0;

		int spawnedCount = 0;

		/// Every faction a hull can be spawned for, the player's own first.
		cs::List<cs::Ref<Faction>> SpawnFactions(UnitySpaceApp& a)
		{
			cs::List<cs::Ref<Faction>> out;
			auto w = a.World();
			for (const auto& f : { w->Sol, w->RivalA, w->RivalB, w->Reach, w->Belt, w->Independents, w->Pirates })
				if (f != nullptr && !out.Contains(f)) out.Add(f);
			return out;
		}

		cs::Ref<Faction> SpawnFaction(UnitySpaceApp& a)
		{
			auto all = SpawnFactions(a);
			if (all.Count() == 0) return nullptr;
			int n = all.Count();
			return all[((spawnFaction % n) + n) % n];
		}

		/// How <paramref name="f"/> stands with the player: theirs, at war with them, or neither.
		cs::String Standing(UnitySpaceApp& a, const cs::Ref<Faction>& f)
		{
			auto home = a.World()->Sol;
			if (f == home) return "yours";
			return a.World()->Diplomacy()->Hostile(f, home) ? "hostile to you" : "neutral to you";
		}

		cs::String Blocker(UnitySpaceApp& a) { return a.CheatBlocker(); }

		/// The line that steps through the factions, at the top of the tab and of each hull's own menu.
		CheatEntry FactionEntry()
		{
			return CheatEntry::Action("Faction", "Enter steps to the next: who the hulls are spawned for", [](UnitySpaceApp&) { spawnFaction++; })
				.WithState([](UnitySpaceApp& a)
				{
					auto f = SpawnFaction(a);
					return f != nullptr ? cs::Cat(cs::Strings::ToUpperInvariant(f->Name), " (", Standing(a, f), ")") : cs::String("NONE");
				})
				.StaysOpen();
		}

		/// What a hull of kind <paramref name="c"/> is, as the menu's lines say it.
		cs::String KindOf(HullCategory c)
		{
			switch (c)
			{
			case HullCategory::Fighter: return "strike craft";
			case HullCategory::SmallCraft: return "small craft";
			case HullCategory::Freighter: return "freighter";
			case HullCategory::Warship: return "warship";
			case HullCategory::Battleship: return "capital ship";
			case HullCategory::Carrier: return "carrier";
			case HullCategory::Station: return "station";
			case HullCategory::Satellite: return "satellite";
			case HullCategory::Support: return "service hull";
			}
			return "hull";
		}

		/// After a spawn: who it is fighting, or why not, for the toast.
		cs::String FightNote(UnitySpaceApp& a, const cs::Ref<Ship>& s)
		{
			if (s == nullptr) return cs::String();
			auto foe = a.CheatNearestHostile(s);
			if (s->Battle != nullptr) return foe != nullptr ? cs::Cat(", engaging the ", foe->Name) : cs::String(", in the fight here");
			if (foe != nullptr) return cs::Cat(" (the hostile ", foe->Name, " is near, but neither side will open a fight)");
			return cs::String();
		}

		cs::String FactionTag(const cs::Ref<Faction>& f) { return f->Prefix.empty() ? f->Name : f->Prefix; }

		void SpawnHull(UnitySpaceApp& a, ShipClass cls, bool pilot)
		{
			auto f = SpawnFaction(a);
			if (f == nullptr) return;
			auto name = cs::Cat(FactionTag(f), " ", ShipClassSpec::Of(cls)->DisplayName, " ", ++spawnedCount);
			if (pilot)
			{
				auto s = a.CheatSpawnAndPilot({ UnitySpaceApp::CheatSlot{ cls, name, 1.2, 0, 0 } }, f);
				if (s != nullptr) a.ShowToast(cs::Cat("At the helm of the ", s->Name, " (", f->Name, ")", FightNote(a, s), "  ·  P hands it back"));
				return;
			}
			auto made = a.CheatSpawn({ UnitySpaceApp::CheatSlot{ cls, name, 0.6, 1.8, 0.4 } }, f);
			if (made.Count() > 0) a.ShowToast(cs::Cat("Spawned the ", made[0]->Name, " beside you (", f->Name, ")", FightNote(a, made[0])));
		}

		/// One hull's own menu: the faction, then Spawn and Spawn and Pilot.
		CheatEntry HullMenu(const cs::Ref<ShipCatalogEntry>& e)
		{
			auto cls = e->Class;
			auto detail = e->Role.empty() ? KindOf(e->Category) : cs::Cat(KindOf(e->Category), "  ·  ", e->Role);
			return CheatEntry::Menu(e->Name, detail, [cls](UnitySpaceApp&)
			{
				Entries out;
				out.push_back(FactionEntry());
				out.push_back(CheatEntry::Action("Spawn", "beside you, flying its own AI; hostile to a ship near, it engages it at once", [cls](UnitySpaceApp& a) { SpawnHull(a, cls, false); })
					.WithBlocker(Blocker).StaysOpen());
				out.push_back(CheatEntry::Action("Spawn and pilot", "just ahead, you at its helm (P hands it back); hostile to a ship near, you are in the fight", [cls](UnitySpaceApp& a) { SpawnHull(a, cls, true); })
					.WithBlocker(Blocker));
				return out;
			}).WithBlocker(Blocker);
		}

		// ---------------------------------------------------------------- stations

		struct StationKind { StructureKind Kind; const char* Label; const char* Detail; double LengthKm; };
		const StationKind stationKinds[] = {
			{ StructureKind::Shipyard, "Shipyard", "an orbital yard: an open frame with a cradle", 3.5 },
			{ StructureKind::Factory, "Factory station", "a works with its radiators", 1.6 },
			{ StructureKind::Habitat, "Habitat ring", "a spinning ring for a world's orbital population", 2.2 },
			{ StructureKind::Depot, "Propellant depot", "tank farms where ships leaving a world pass", 1.2 },
			{ StructureKind::Gateway, "Transfer gateway", "a crewed station where traffic changes ships", 2.0 },
			{ StructureKind::Relay, "Signal relay", "a communications relay", 0.4 },
			{ StructureKind::CrewedStation, "Crewed station", "a small crewed outpost", 0.3 },
		};

		CheatEntry StationMenu(const StationKind* k)
		{
			return CheatEntry::Menu(k->Label, k->Detail, [k](UnitySpaceApp&)
			{
				Entries out;
				out.push_back(FactionEntry());
				out.push_back(CheatEntry::Action("Spawn", "a few of its lengths ahead of you, holding there", [k](UnitySpaceApp& a)
				{
					auto f = SpawnFaction(a);
					auto st = a.CheatSpawnStation(k->Kind, f);
					if (st != nullptr) a.ShowToast(cs::Cat("Spawned ", st->Name, " ahead of you (", f->Name, ")"));
				}).WithBlocker(Blocker).StaysOpen());
				out.push_back(CheatEntry::Action("Spawn and pilot", "", [](UnitySpaceApp&) {})
					.WithBlocker([](UnitySpaceApp&) { return cs::String("a station has no helm to take: Spawn sets it here"); }));
				return out;
			}).WithBlocker(Blocker);
		}

		// ---------------------------------------------------------------- pirate groups

		struct Group { const char* Label; const char* Detail; std::vector<std::pair<ShipClass, double>> Hulls; };
		const Group& PirateGroup(int i)
		{
			static const Group groups[] = {
				{ "Raiders, small", "an interceptor and two fighters", { { ShipClass::Interceptor, 0 }, { ShipClass::Fighter, 1 }, { ShipClass::Fighter, -1 } } },
				{ "Raiders, medium", "a corvette, a frigate and a heavy fighter", { { ShipClass::Corvette, 0 }, { ShipClass::Frigate, 1 }, { ShipClass::Gunship, -1 } } },
				{ "Raiders, large", "a destroyer, a cruiser and two heavy fighters",
					{ { ShipClass::Destroyer, 0 }, { ShipClass::Cruiser, 1 }, { ShipClass::Gunship, -1 }, { ShipClass::Gunship, 2 } } },
				{ "Capital group", "a battleship with a destroyer and a corvette", { { ShipClass::Battleship, 0 }, { ShipClass::Destroyer, 1 }, { ShipClass::Corvette, -1 } } },
			};
			return groups[i];
		}
		constexpr int PirateGroups = 4;

		void SpawnGroup(UnitySpaceApp& a, const Group& g, bool pilot)
		{
			auto pirates = a.World()->Pirates;
			if (pirates == nullptr) return;
			cs::List<UnitySpaceApp::CheatSlot> slots;
			for (const auto& [cls, side] : g.Hulls)
				slots.Add(UnitySpaceApp::CheatSlot{ cls, cs::Cat("Raider ", ShipClassSpec::Of(cls)->DisplayName, " ", ++spawnedCount),
					(pilot && side == 0 ? 1.2 : 4.5) + cs::Math::Abs(side) * 1.5, side * 2.2, side * 0.6 });
			if (pilot)
			{
				auto s = a.CheatSpawnAndPilot(slots, pirates);
				if (s != nullptr) a.ShowToast(cs::Cat("At the helm of the ", s->Name, ", leading the pirates' ", cs::Strings::ToLowerInvariant(g.Label), FightNote(a, s)));
				return;
			}
			auto made = a.CheatSpawn(slots, pirates);
			if (made.Count() > 0) a.ShowToast(cs::Cat("Pirates' ", cs::Strings::ToLowerInvariant(g.Label), ": ", made.Count(), " hulls ahead", FightNote(a, made[0])));
		}

		CheatEntry GroupMenu(int i)
		{
			const auto& g = PirateGroup(i);
			return CheatEntry::Menu(g.Label, g.Detail, [i](UnitySpaceApp&)
			{
				Entries out;
				out.push_back(CheatEntry::Action("Spawn", "a few km ahead under the black flag: they fall on whatever is near", [i](UnitySpaceApp& a) { SpawnGroup(a, PirateGroup(i), false); })
					.WithBlocker(Blocker).StaysOpen());
				out.push_back(CheatEntry::Action("Spawn and pilot", "the same, you at the lead hull's helm", [i](UnitySpaceApp& a) { SpawnGroup(a, PirateGroup(i), true); })
					.WithBlocker(Blocker));
				return out;
			}).WithBlocker(Blocker);
		}

		// ---------------------------------------------------------------- Bio Mass (US-T192's simulation, through its own entry points)

		/// The hull a tumor roots on: the one flown, else the one selected.
		cs::Ref<Ship> TumorHost(UnitySpaceApp& a)
		{
			if (a.IsPiloting() && a.Piloted.Ship != nullptr && !a.Piloted.Ship->Destroyed) return a.Piloted.Ship;
			if (a.Selected.IsShip() && a.Selected.Ship != nullptr && !a.Selected.Ship->Destroyed) return a.Selected.Ship;
			return nullptr;
		}

		/// Where a growth comes, ahead of the player: the frame and the heliocentric point <paramref name="km"/> along its nose.
		DVec3 AheadHelio(UnitySpaceApp& a, double km, int& frame, DVec3& local)
		{
			DVec3 base, forward, up;
			a.CheatPlayerFrame(frame, base, forward, up);
			local = base + forward * km;
			return a.Ephemeris()->Helio(frame) + local;
		}

		enum class Growth { Globule, Blooms, Tumor };

		void SpawnGrowth(UnitySpaceApp& a, Growth g)
		{
			if (!a.CheatBlocker().empty()) return;
			double jd = a.Clock->Jd;
			auto bio = a.World()->EnsureBioMass(jd);
			if (bio == nullptr) return;
			int frame;
			DVec3 local;
			auto player = a.IsPiloting() ? a.Piloted.Ship : nullptr;
			double off = player != nullptr ? cs::Math::Max(8.0, player->LengthKm() * 6) : 8.0;
			switch (g)
			{
			case Growth::Globule:
			{
				AheadHelio(a, off, frame, local);
				auto made = bio->Spawn(frame, local, DVec3::Zero, 1.5e6, jd);
				a.ShowToast(made != nullptr ? cs::String("A Bio Mass globule wells up ahead of you") : cs::String("No room for another globule here"));
				return;
			}
			case Growth::Blooms:
			{
				auto at = AheadHelio(a, cs::Math::Max(off, 400.0), frame, local);
				int n = bio->EruptBlooms(at, BioMass::BloomCluster, jd);
				a.ShowToast(n > 0 ? cs::Cat(n, " Bio Mass blooms erupt ahead: they close and latch on") : cs::String("No room for blooms in this system (at the cap)"));
				return;
			}
			case Growth::Tumor:
			{
				auto host = TumorHost(a);
				if (host == nullptr) return;
				auto helio = a.Fleet()->StateOf(host, jd).HelioKm;
				auto t = bio->SpawnTumor(BioHost::Ship, host->Id, 0, helio - a.Ephemeris()->HelioAt(0, jd), BioMass::TumorSeedMass, jd);
				a.ShowToast(t != nullptr ? cs::Cat("A Bio Mass tumor roots on the ", host->Name) : cs::String("The tumor could not take"));
				return;
			}
			}
		}

		CheatEntry GrowthMenu(Growth g, const char* label, const char* detail)
		{
			return CheatEntry::Menu(label, detail, [g](UnitySpaceApp&)
			{
				Entries out;
				auto spawn = CheatEntry::Action("Spawn", g == Growth::Tumor ? "rooted on the hull flown, else the ship selected" : "ahead of you, left to the simulation (and the guns)",
					[g](UnitySpaceApp& a) { SpawnGrowth(a, g); }).StaysOpen();
				if (g == Growth::Tumor)
					spawn.WithBlocker([](UnitySpaceApp& a)
					{
						if (!a.CheatBlocker().empty()) return a.CheatBlocker();
						return TumorHost(a) != nullptr ? cs::String() : cs::String("fly or select a hull for it to root on");
					});
				else spawn.WithBlocker(Blocker);
				out.push_back(spawn);
				out.push_back(CheatEntry::Action("Spawn and pilot", "", [](UnitySpaceApp&) {})
					.WithBlocker([](UnitySpaceApp&) { return cs::String("a growth has no helm to take: Spawn sets it loose"); }));
				return out;
			}).WithBlocker(Blocker);
		}
	}

	// ---------------------------------------------------------------- spawning

	cs::Ref<Ship> UnitySpaceApp::CheatSpawnAndPilot(const cs::List<CheatSlot>& slots, const cs::Ref<Faction>& faction)
	{
		if (faction == nullptr || slots.Count() == 0 || !CheatBlocker().empty()) return nullptr;
		// Where the player is now, taken before any helm is let go.
		int frame;
		DVec3 base, forward, up;
		CheatPlayerFrame(frame, base, forward, up);
		RememberCommand();
		ReleaseSeat();
		if (IsPiloting()) ExitPiloting();
		ShowSpawn = ShowGalaxyMap = ShowPicker = false;
		// Spawned on its own AI first, so a hostile near engages it as any spawn (CheatEngage); the helm takes over the
		// fight's unit then (EnterPiloting).
		auto made = CheatSpawnAt(slots, faction, frame, base, forward, up, true);
		if (made.Count() == 0) return nullptr;
		auto ship = made[0];
		EnterPiloting(Target::OfShip(ship));
		// The one hostile near is the player's own former hull, or none would fight the AI: the helm's own fight.
		if (IsPiloting() && Piloted.Ship == ship && ship->Battle == nullptr && CheatNearestHostile(ship) != nullptr)
			World()->War->TryStartPlayerBattle(ship, pilotFrameBody, pilotOffsetKm(), pilotForward(), Clock->Jd);
		return ship;
	}

	cs::Ref<OrbitalStructure> UnitySpaceApp::CheatSpawnStation(StructureKind kind, const cs::Ref<Faction>& faction)
	{
		if (faction == nullptr || !CheatBlocker().empty() || World()->Infrastructure == nullptr) return nullptr;
		double lengthKm = 1;
		const char* label = "Station";
		for (const auto& k : stationKinds)
			if (k.Kind == kind)
			{
				lengthKm = k.LengthKm;
				label = k.Label;
			}
		int frame;
		DVec3 base, forward, up;
		CheatPlayerFrame(frame, base, forward, up);
		auto player = IsPiloting() ? Piloted.Ship : nullptr;
		double jd = Clock->Jd;
		auto frameHelio = Ephemeris()->Helio(frame);
		CollectPracticeObstacles(nullptr);
		double ahead = cs::Math::Max(2.0, lengthKm * 4 + (player != nullptr ? player->LengthKm() * 3 : 0.0));
		auto at = PracticeSpawn::Clear(frameHelio + base + forward * ahead, forward, lengthKm, practiceObstacles) - frameHelio;

		// Held where it is put (a fixed point in its world's frame), as a spawned hull is (Fleet::SpawnAt).
		DVec3 v = forward.LengthSquared() > 1e-12 ? forward.Normalized() : DVec3::UnitX;
		DVec3 u = DVec3::Cross(cs::Math::Abs(v.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX, v).Normalized();
		auto orbit = cs::New<ParkingOrbit>();
		orbit->Body = frame;
		orbit->RadiusKm = 0;
		orbit->Center = at;
		orbit->U = u;
		orbit->V = v;
		orbit->EpochJd = jd;
		orbit->Fixed = true;

		auto st = cs::New<OrbitalStructure>();
		st->Name = cs::Cat(FactionTag(faction), " ", label, " ", ++spawnedCount);
		st->Kind = kind;
		st->Body = frame;
		st->Orbit = orbit;
		st->SetOwner(faction);
		st->LengthKm = lengthKm;
		st->Note = "Spawned from the spawn menu (US-T186)";
		World()->Infrastructure->Structures.Add(st);
		if (Structures != nullptr) Structures->Add(st);
		return st;
	}

	// ---------------------------------------------------------------- the menu's entries

	void UnitySpaceApp::RegisterSpawnMenu()
	{
		using UI::CheatRegistry;
		CheatRegistry::Add("Spawn", FactionEntry().At(-100));
		// Every hull in the catalog, in its order (strike craft, then working hulls, then warships).
		for (const auto& e : ShipCatalog::Entries())
			CheatRegistry::Add("Spawn", HullMenu(e).At(-90));
		CheatRegistry::Add("Spawn", CheatEntry::Menu("Stations", "a yard, works, habitat, depot, gateway or relay ahead of you, for the faction chosen", [](UnitySpaceApp&)
		{
			Entries out;
			for (const auto& k : stationKinds) out.push_back(StationMenu(&k));
			return out;
		}).WithBlocker(Blocker).At(-80));
		CheatRegistry::Add("Spawn", CheatEntry::Menu("Pirate groups", "raiders by size, under the black flag whatever the faction chosen", [](UnitySpaceApp&)
		{
			Entries out;
			for (int i = 0; i < PirateGroups; i++) out.push_back(GroupMenu(i));
			return out;
		}).WithBlocker(Blocker).At(-70));
		CheatRegistry::Add("Spawn", CheatEntry::Menu("Bio Mass", "a globule, a cluster of blooms or a tumor (US-T192)", [](UnitySpaceApp&)
		{
			Entries out;
			out.push_back(GrowthMenu(Growth::Globule, "Globule", "a living mass that creeps and feeds"));
			out.push_back(GrowthMenu(Growth::Blooms, "Blooms", "small, fast growths that latch onto hulls and bite"));
			out.push_back(GrowthMenu(Growth::Tumor, "Tumor", "rooted on a hull, draining it as it grows"));
			return out;
		}).WithBlocker(Blocker).At(-60));
		CheatRegistry::Add("Spawn", CheatEntry::Action("Spawn & pilot grid", "pick a hull from the grid and take its helm at the camera", [](UnitySpaceApp& a) { a.ShowSpawn = true; })
			.WithKeys("V").WithBlocker(Blocker).At(50));
		CheatRegistry::Add("Spawn", CheatEntry::Action("Practice fighter", "one of your fighters at the camera, you at its helm", [](UnitySpaceApp& a) { a.SpawnPracticeFighter(); })
			.WithKeys("Shift+V").WithBlocker(Blocker).At(50));
	}
}
