// US-T126: the dev menu's built-in entries and the cheats they run (declarations in UnitySpaceApp.Cheats.inl).

#include "UnitySpaceApp.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Combat/PilotFlight.h"
#include "Combat/Pursuit.h"
#include "Combat/War.h"
#include "Coop/CoopPeer.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/ShipHelm.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Infrastructure/Infrastructure.h"
#include "PracticeSpawn.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "SimClock.h"
#include "Systems/Galaxy.h"
#include "World.h"

#include <algorithm>

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		/// Every hull class there is, as the spawn grid lists them.
		const cs::List<ShipClass>& AllClasses()
		{
			static const cs::List<ShipClass> classes
			{
				ShipClass::Fighter, ShipClass::Interceptor, ShipClass::Gunship, ShipClass::Bomber, ShipClass::Recon, ShipClass::AssaultShuttle,
				ShipClass::Corvette, ShipClass::Frigate, ShipClass::Destroyer, ShipClass::Cruiser, ShipClass::CommandCruiser, ShipClass::Battleship,
				ShipClass::Carrier, ShipClass::GateBreaker, ShipClass::GatePicket, ShipClass::Courier, ShipClass::Freighter, ShipClass::Miner,
				ShipClass::Explorer, ShipClass::OreTug, ShipClass::CaptureTug, ShipClass::LaneTender,
			};
			return classes;
		}

		/// The warships a navy sends (the military classes, small to large).
		const cs::List<ShipClass>& WarClasses()
		{
			static const cs::List<ShipClass> classes
			{
				ShipClass::Fighter, ShipClass::Interceptor, ShipClass::Gunship, ShipClass::Bomber, ShipClass::Corvette, ShipClass::Frigate,
				ShipClass::Destroyer, ShipClass::Cruiser, ShipClass::CommandCruiser, ShipClass::Battleship, ShipClass::Carrier, ShipClass::GateBreaker,
			};
			return classes;
		}

		cs::String ClassName(ShipClass c) { return ShipClassSpec::Of(c)->DisplayName; }

		int spawned = 0;

		cs::String Plural(int n, const char* one) { return cs::Cat(n, " ", one, n == 1 ? "" : "s"); }
	}

	// ---------------------------------------------------------------- the menu's frame

	bool UnitySpaceApp::UpdateCheatMenu()
	{
		bool was = cheatMenu->IsOpen;
		if (Input.ToggleCheatMenu)
		{
			if (was) cheatMenu->Close();
			else cheatMenu->Open();
		}
		else if (Input.ToggleSpawnMenu) // US-T186: Shift+F2, the spawn menu: the same menu, opened on its Spawn tab
		{
			if (was) cheatMenu->Close();
			else
			{
				cheatMenu->Open();
				cheatMenu->ShowCategory("Spawn");
			}
		}
		cheatMenu->Update(*this, Time::unscaledDeltaTime);
		// Keys and buttons held as it closes are not the craft's or the camera's until let go (US-T90).
		if (was && !cheatMenu->IsOpen) inputHandover = true;
		return was || cheatMenu->IsOpen;
	}

	cs::String UnitySpaceApp::CheatBlocker() const
	{
		if (Coop::CoopPeer::Current() != nullptr) return "single-player only";
		if (Travel->Active || InFtlCruise()) return "between the stars: arrive first";
		return cs::String();
	}

	// ---------------------------------------------------------------- spawning beside the player

	void UnitySpaceApp::CheatPlayerFrame(int& frame, DVec3& offsetKm, DVec3& forward, DVec3& up)
	{
		if (IsPiloting())
		{
			frame = pilotFrameBody;
			offsetKm = pilotOffsetKm();
			forward = pilotForward();
			up = pilot().Up;
			return;
		}
		// The camera's own frame (its free-flight frame goes stale while it is focused on something far off).
		frame = SpaceCamera->FrameAt(SpaceCamera->ScenePosition);
		offsetKm = Scale()->InverseLocalOffset((*Catalog())[frame], SpaceCamera->ScenePosition - BodyScene(frame));
		auto f = SpaceCamera->Rotation * Vector3::forward();
		auto u = SpaceCamera->Rotation * Vector3::up();
		forward = DVec3(f.x, f.y, f.z).SwapYZ().Normalized();
		up = DVec3(u.x, u.y, u.z).SwapYZ().Normalized();
	}

	cs::List<cs::Ref<Ship>> UnitySpaceApp::CheatSpawn(const cs::List<CheatSlot>& slots, const cs::Ref<Faction>& faction)
	{
		if (faction == nullptr || !CheatBlocker().empty()) return cs::List<cs::Ref<Ship>>();
		int frame;
		DVec3 base, forward, up;
		CheatPlayerFrame(frame, base, forward, up);
		return CheatSpawnAt(slots, faction, frame, base, forward, up, false);
	}

	cs::List<cs::Ref<Ship>> UnitySpaceApp::CheatSpawnAt(const cs::List<CheatSlot>& slots, const cs::Ref<Faction>& faction, int frame,
		DVec3 base, DVec3 forward, DVec3 up, bool pilot)
	{
		cs::List<cs::Ref<Ship>> made;
		if (faction == nullptr || !CheatBlocker().empty()) return made;
		auto right = DVec3::Cross(forward, up);
		right = right.LengthSquared() > 1e-12 ? right.Normalized() : DVec3::UnitY;
		double jd = Clock->Jd;
		auto frameHelio = Ephemeris()->Helio(frame);
		auto player = IsPiloting() ? Piloted.Ship : nullptr;
		// US-T186: hostile to the hull flown (whoever's it is), else to the player's own.
		bool hostile = World()->Diplomacy()->Hostile(faction, player != nullptr ? player->Faction : World()->Sol);
		CollectPracticeObstacles(nullptr); // the player's own hull is one of them
		cs::List<DVec3> locals;
		for (const auto& slot : slots)
		{
			double lengthKm = CombatSpec::Of(slot.Class)->LengthKm;
			// The layout's kilometre grows with the hulls, so a carrier is not put a kilometre off a fighter.
			double unit = cs::Math::Max(1.0, lengthKm * 2.5 + (player != nullptr ? player->LengthKm() * 1.5 : 0.0));
			auto local = forward * (slot.Ahead * unit) + right * (slot.Right * unit) + up * (slot.Up * unit);
			auto facing = hostile && !pilot ? -forward : forward;
			auto at = PracticeSpawn::Clear(frameHelio + base + local, facing, lengthKm, practiceObstacles) - frameHelio;
			auto ship = Fleet()->SpawnAt(slot.Name, slot.Class, faction, frame, at, facing, jd);
			ship->Ports = World()->FreeShipPorts(); // not a hidden haven's rock, as for every free ship
			practiceObstacles.Add(SpawnObstacle(frameHelio + at, ship->LengthKm() * 0.5));
			made.Add(ship);
			locals.Add(at - base);
		}

		// At a helm: enemies open a fight with the hull flown, laid out where they were put (as the dogfight demo's), and
		// friends join the fight it is in.
		if (player != nullptr && !player->Destroyed)
		{
			auto battle = player->Battle;
			if (battle == nullptr && hostile) battle = World()->War->TryStartPlayerBattle(player, pilotFrameBody, pilotOffsetKm(), pilotForward(), jd);
			else if (battle != nullptr)
				for (const auto& s : made)
					if (s->Battle == nullptr && s->Orbit != nullptr) World()->War->OnArrived(s, jd);
			if (battle != nullptr)
			{
				auto me = battle->UnitOf(player);
				auto origin = me != nullptr ? me->Pos : DVec3::Zero;
				for (int i = 0; i < made.Count(); i++)
				{
					auto u = made[i]->Battle == battle ? battle->UnitOf(made[i]) : nullptr;
					if (u == nullptr) continue;
					u->Pos = origin + locals[i];
					u->Vel = DVec3::Zero;
					if (hostile) u->Forward = (origin - u->Pos).Normalized();
				}
			}
		}

		// US-T186: anything spawned hostile to a ship near it (not the hull flown, settled above) engages it at once.
		for (const auto& s : made)
			if (s->Battle == nullptr && !s->Destroyed) CheatEngage(s);
		return made;
	}

	cs::Ref<Ship> UnitySpaceApp::CheatNearestHostile(const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr || ship->Destroyed || ship->Orbit == nullptr) return nullptr;
		double jd = Clock->Jd;
		int body = ship->Orbit->Body;
		auto here = Fleet()->StateOf(ship, jd).HelioKm;
		// The reach a fight at this world gathers from: its neighbourhood, or about a gate's approach off the system's root.
		double reachKm = body == Catalog()->Root()->Index ? War::GateFightRangeKm : cs::Math::Max(ScaleModel::NeighbourhoodKm((*Catalog())[body]), 2000.0);
		cs::Ref<Ship> best;
		double bestKm = reachKm;
		for (const auto& s : Fleet()->Ships())
		{
			if (s == ship || s->Destroyed || s->Derelict || s->Planetside() || s->Phase == ShipPhase::Lane || !Fleet()->IsPresent(s)) continue;
			if (s->Faction == nullptr || !World()->Diplomacy()->Hostile(s->Faction, ship->Faction)) continue;
			double km = (Fleet()->StateOf(s, jd).HelioKm - here).Length();
			if (km < bestKm)
			{
				bestKm = km;
				best = s;
			}
		}
		return best;
	}

	cs::Ref<Battle> UnitySpaceApp::CheatEngage(const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr || ship->Destroyed) return nullptr;
		if (ship->Battle != nullptr) return ship->Battle;
		auto foe = CheatNearestHostile(ship);
		if (foe == nullptr) return nullptr;
		double jd = Clock->Jd;
		auto war = World()->War;
		// The hull flown is the player's to fight (TryStartPlayerBattle), never swept into a fight the war starts.
		if (IsPiloting() && Piloted.Ship == foe)
		{
			if (foe->Battle == nullptr) war->TryStartPlayerBattle(foe, pilotFrameBody, pilotOffsetKm(), pilotForward(), jd);
			else if (ship->Orbit != nullptr) war->OnArrived(ship, jd);
			return ship->Battle;
		}
		// The war's own path: it joins a fight on there or starts one; else the other side, fit to, falls on it.
		war->OnArrived(ship, jd);
		if (ship->Battle == nullptr && ship->Orbit != nullptr)
			war->TryStartBattle(ship->Orbit->Body, foe->Faction, jd, ship->Orbit->Body == Catalog()->Root()->Index ? cs::Nullable<DVec3>(ship->Orbit->OffsetAt(jd)) : cs::Nullable<DVec3>());
		return ship->Battle;
	}

	cs::Ref<Faction> UnitySpaceApp::CheatHostileNavy()
	{
		auto home = World()->Sol;
		for (const auto& f : { World()->RivalA, World()->RivalB, World()->Reach, World()->Belt, World()->Independents })
			if (f != nullptr && f != home && !f->IsPirate && World()->Diplomacy()->Hostile(f, home)) return f;
		return nullptr;
	}

	// ---------------------------------------------------------------- teleports

	double UnitySpaceApp::BodyStandoffKm(int body, DVec3& outward)
	{
		auto def = (*Catalog())[body];
		auto helio = Ephemeris()->Helio(body);
		auto sun = helio.LengthSquared() > 1e-6 ? (-helio).Normalized() : DVec3::UnitX;
		auto pole = def->Pole.LengthSquared() > 1e-12 ? def->Pole.Normalized() : DVec3::UnitZ;
		auto across = DVec3::Cross(pole, sun);
		across = across.LengthSquared() > 1e-12 ? across.Normalized() : DVec3::UnitY;
		outward = (sun * 0.75 + pole * 0.55 + across * 0.35).Normalized();
		double rangeKm = cs::Math::Max(cs::Math::Max(def->RadiusKm * 2.6, def->RingOuterKm * 1.3), Battle::AnchorDistanceKm(def));
		return cs::Math::Max(cs::Math::Min(rangeKm, ScaleModel::NeighbourhoodKm(def) * 0.9), def->RadiusKm + 50);
	}

	void UnitySpaceApp::CheatTeleportToBody(int body)
	{
		auto def = (*Catalog())[body];
		auto target = Target::OfBody(body);
		if (!IsPiloting())
		{
			if (AtControls()) ReturnToCommand();
			Select(target);
			SpaceCamera->FocusOn(target);
			ShowToast(cs::Cat("Camera to ", def->Name));
			return;
		}
		auto ship = Piloted.Ship;
		if (ship->Battle != nullptr || ship->Phase == ShipPhase::Lane)
		{
			ShowToast("Not mid-fight or mid-lane: finish it first");
			return;
		}
		DVec3 outward;
		double km = BodyStandoffKm(body, outward);
		DropSystemCruise(cs::String()); // US-T127: a teleport ends any cruise
		pilotFrameBody = body;
		SetpilotOffsetKm(outward * km);
		SetpilotVelKmS(DVec3::Zero);
		pilot().Forward = -outward;
		pilot().Up = PilotState::LevelUp(-outward);
		pilot().AngularVelocity = DVec3::Zero;
		ResetPilotAim();
		PinWherePiloted(ship);
		if (World()->Pursuit != nullptr && World()->Pursuit->Active()) World()->Pursuit->ReleaseAll(Clock->Jd);
		ShowToast(cs::Cat("Teleported to ", def->Name, ", ", Num(km, "N0"), " km off its sunlit side"));
	}

	void UnitySpaceApp::CheatTeleportToStructure(const cs::Ref<OrbitalStructure>& st)
	{
		auto target = Target::OfStructure(st);
		if (!IsPiloting())
		{
			if (AtControls()) ReturnToCommand();
			Select(target);
			SpaceCamera->FocusOn(target);
			ShowToast(cs::Cat("Camera to ", st->Name));
			return;
		}
		auto ship = Piloted.Ship;
		if (ship->Battle != nullptr || ship->Phase == ShipPhase::Lane)
		{
			ShowToast("Not mid-fight or mid-lane: finish it first");
			return;
		}
		auto rel = st->HelioAt(Ephemeris(), Clock->Jd) - Ephemeris()->Helio(st->Body);
		auto outward = rel.LengthSquared() > 1e-6 ? rel.Normalized() : DVec3::UnitX;
		double km = cs::Math::Max(4.0, st->LengthKm * 6 + ship->LengthKm() * 4);
		DropSystemCruise(cs::String()); // US-T127: a teleport ends any cruise
		pilotFrameBody = st->Body;
		SetpilotOffsetKm(rel + outward * km);
		SetpilotVelKmS(DVec3::Zero);
		pilot().Forward = -outward;
		pilot().Up = PilotState::LevelUp(-outward);
		pilot().AngularVelocity = DVec3::Zero;
		ResetPilotAim();
		PinWherePiloted(ship);
		if (World()->Pursuit != nullptr && World()->Pursuit->Active()) World()->Pursuit->ReleaseAll(Clock->Jd);
		ShowToast(cs::Cat("Teleported to ", st->Name, ", ", Num(km, "N1"), " km off"));
	}

	void UnitySpaceApp::CheatTravelTo(const cs::Ref<StarSystem>& system)
	{
		if (system == nullptr || system == ActiveSystem) return;
		if (AtControls()) ReturnToCommand();
		TravelTo(system);
	}

	void UnitySpaceApp::CheatMendHull()
	{
		auto ship = IsPiloting() ? Piloted.Ship : nullptr;
		if (ship == nullptr || ship->Combat == nullptr) return;
		ship->Combat->Repair(1);
		ship->Combat->FreeResupply();
		ship->SetPropellant(ship->PropellantCapacity());
		ship->OutOfPropellant = false;
		ship->StrandedInFlight = false;
		ShowToast(cs::Cat(ship->Name, ": whole, rearmed and refuelled"));
	}

	// ---------------------------------------------------------------- the built-in entries

	void UnitySpaceApp::RegisterBuiltInCheats()
	{
		using UI::CheatEntry;
		using UI::CheatRegistry;
		using Entries = std::vector<CheatEntry>;
		auto blocker = [](UnitySpaceApp& a) { return a.CheatBlocker(); };
		auto atHelm = [](UnitySpaceApp& a) { return !a.CheatBlocker().empty() ? a.CheatBlocker() : a.IsPiloting() ? cs::String() : cs::String("take a helm first"); };

		CheatRegistry::DeclareCategory("Spawn", 10, "every hull, station, pirate group and Bio Mass growth, for any faction: spawn, or spawn and pilot (Shift+F2)");
		CheatRegistry::DeclareCategory("Enemies", 20, "hostiles beside you; at a helm they open a fight with you");
		CheatRegistry::DeclareCategory("Travel", 30, "the cruise drive, teleports and the other stars");
		CheatRegistry::DeclareCategory("Helm", 40, "take a helm or a seat, switch craft");
		CheatRegistry::DeclareCategory("Combat", 50, "the dogfight demo and the hull flown");
		CheatRegistry::DeclareCategory("Time & scale", 60, "time warp and the realistic / game scale");

		// ---- Spawn: the spawn menu's own entries (US-T186, UnitySpaceApp.Spawn.cpp; Shift+F2 opens the menu on them).
		RegisterSpawnMenu();

		// ---- Enemies: pirates by size, any pirate hull, a hostile navy's warships, and whatever else hunts the player.
		struct Group { const char* Label; const char* Detail; std::vector<std::pair<ShipClass, double>> Hulls; };
		static const Group groups[] = {
			{ "Pirate raiders, small", "an interceptor and two fighters, a few km ahead",
				{ { ShipClass::Interceptor, 0 }, { ShipClass::Fighter, 1 }, { ShipClass::Fighter, -1 } } },
			{ "Pirate raiders, medium", "a corvette, a frigate and a heavy fighter",
				{ { ShipClass::Corvette, 0 }, { ShipClass::Frigate, 1 }, { ShipClass::Gunship, -1 } } },
			{ "Pirate raiders, large", "a destroyer, a cruiser and two heavy fighters",
				{ { ShipClass::Destroyer, 0 }, { ShipClass::Cruiser, 1 }, { ShipClass::Gunship, -1 }, { ShipClass::Gunship, 2 } } },
			{ "Pirate capital group", "a battleship with a destroyer and a corvette",
				{ { ShipClass::Battleship, 0 }, { ShipClass::Destroyer, 1 }, { ShipClass::Corvette, -1 } } },
		};
		for (const auto& g : groups)
		{
			const Group* group = &g;
			CheatRegistry::Add("Enemies", CheatEntry::Action(g.Label, g.Detail, [group](UnitySpaceApp& a)
			{
				cs::List<CheatSlot> slots;
				for (const auto& [cls, side] : group->Hulls)
					slots.Add(CheatSlot{ cls, cs::Cat("Raider ", ClassName(cls), " ", ++spawned), 4.5 + cs::Math::Abs(side) * 1.5, side * 2.2, side * 0.6 });
				auto made = a.CheatSpawn(slots, a.World()->Pirates);
				if (made.Count() == 0) return;
				bool fighting = a.IsPiloting() && a.Piloted.Ship->Battle != nullptr;
				a.ShowToast(cs::Cat(group->Label, ": ", Plural(made.Count(), "hull"), " ahead", fighting ? cs::String(", in the fight with you") : a.IsPiloting() ? cs::String(" (no fight opened: close in)") : cs::String()));
			}).WithBlocker(blocker));
		}
		CheatRegistry::Add("Enemies", CheatEntry::Menu("One pirate hull", "any class, under the black flag", [](UnitySpaceApp&)
		{
			Entries out;
			for (auto cls : AllClasses())
				out.push_back(CheatEntry::Action(ClassName(cls), "a pirate, a few km ahead", [cls](UnitySpaceApp& a)
				{
					auto made = a.CheatSpawn({ CheatSlot{ cls, cs::Cat("Raider ", ClassName(cls), " ", ++spawned), 5, 0.5, 0.3 } }, a.World()->Pirates);
					if (made.Count() > 0) a.ShowToast(cs::Cat("Spawned the ", made[0]->Name, " ahead"));
				}).WithBlocker([](UnitySpaceApp& a) { return a.CheatBlocker(); }).StaysOpen());
			return out;
		}).WithBlocker(blocker));
		CheatRegistry::Add("Enemies", CheatEntry::Menu("Hostile navy warship", "a polity at war with you sends one", [](UnitySpaceApp&)
		{
			Entries out;
			for (auto cls : WarClasses())
				out.push_back(CheatEntry::Action(ClassName(cls), "a hostile navy's, a few km ahead", [cls](UnitySpaceApp& a)
				{
					auto navy = a.CheatHostileNavy();
					auto made = a.CheatSpawn({ CheatSlot{ cls, cs::Cat(navy != nullptr ? navy->Name : cs::String("Navy"), " ", ClassName(cls), " ", ++spawned), 5.5, -0.8, 0.4 } }, navy);
					if (made.Count() > 0) a.ShowToast(cs::Cat("Spawned the ", made[0]->Name, " ahead"));
				}).WithBlocker([](UnitySpaceApp& a) { return a.CheatBlocker(); }).StaysOpen());
			return out;
		}).WithBlocker([](UnitySpaceApp& a)
		{
			if (!a.CheatBlocker().empty()) return a.CheatBlocker();
			return a.CheatHostileNavy() != nullptr ? cs::String() : cs::String("no polity is at war with you here");
		}));
		// US-T125: the Bio Mass, through its own console command; it lights up the moment that command is in the build.
		CheatRegistry::Add("Enemies", CheatEntry::Console("Bio Mass globule", "a living globule beside your hull (US-T125)", "sg.BioMass here"));
		// US-T192: its blooms and tumors.
		CheatRegistry::Add("Enemies", CheatEntry::Console("Bio Mass blooms", "a cluster of blooms just off your hull: they latch on and bite (US-T192)", "sg.BioMass here bloom"));
		CheatRegistry::Add("Enemies", CheatEntry::Console("Bio Mass tumor", "a tumor rooted on your hull, draining it as it grows (US-T192)", "sg.BioMass here tumor"));

		// ---- Travel: the other stars, the worlds and stations of this one (the cruise drive adds its own, US-T127).
		CheatRegistry::Add("Travel", CheatEntry::Menu("Teleport to a world", "the hull flown (or the camera) to a star, planet or moon here", [](UnitySpaceApp& a)
		{
			Entries out;
			for (const auto& b : a.Catalog()->Bodies())
			{
				if (!b->IsVisible() || b->RadiusKm <= 0) continue;
				int index = b->Index;
				cs::String kind = cs::Strings::ToLowerInvariant(Core::ToString(b->Kind));
				if (b->ParentIndex >= 0 && !(*a.Catalog())[b->ParentIndex]->IsStar() && (*a.Catalog())[b->ParentIndex]->IsVisible())
					kind = cs::Cat(kind, " of ", (*a.Catalog())[b->ParentIndex]->Name);
				out.push_back(CheatEntry::Action(b->Name, kind, [index](UnitySpaceApp& app) { app.CheatTeleportToBody(index); })
					.WithBlocker([](UnitySpaceApp& app) { return app.CheatBlocker(); }));
			}
			return out;
		}).WithBlocker(blocker));
		CheatRegistry::Add("Travel", CheatEntry::Menu("Teleport to a station", "a station, yard or platform in orbit here", [](UnitySpaceApp& a)
		{
			Entries out;
			for (const auto& st : a.World()->Infrastructure->Structures)
			{
				auto s = st;
				out.push_back(CheatEntry::Action(st->Name, cs::Cat("over ", (*a.Catalog())[st->Body]->Name), [s](UnitySpaceApp& app) { app.CheatTeleportToStructure(s); })
					.WithBlocker([](UnitySpaceApp& app) { return app.CheatBlocker(); }));
			}
			return out;
		}).WithBlocker(blocker));
		CheatRegistry::Add("Travel", CheatEntry::Menu("Fly to another star", "the camera's trip to another system, nearest first (Esc skips it)", [](UnitySpaceApp& a)
		{
			Entries out;
			auto here = a.ActiveSystem != nullptr ? a.ActiveSystem->PositionLy : DVec3::Zero;
			std::vector<std::pair<double, cs::Ref<StarSystem>>> near;
			for (const auto& s : a.Galaxy->Systems)
				if (s != a.ActiveSystem) near.emplace_back((s->PositionLy - here).Length(), s);
			std::sort(near.begin(), near.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
			for (size_t i = 0; i < near.size() && i < 60; i++)
			{
				auto s = near[i].second;
				out.push_back(CheatEntry::Action(s->Name, cs::Cat(Num(near[i].first, "0.0"), " ly", s->IsVoidBase ? "  ·  void station" : ""), [s](UnitySpaceApp& app) { app.CheatTravelTo(s); })
					.WithBlocker([](UnitySpaceApp& app) { return Coop::CoopPeer::Current() != nullptr ? cs::String("single-player only") : cs::String(); }));
			}
			return out;
		}).WithBlocker([](UnitySpaceApp& a) { return Coop::CoopPeer::Current() != nullptr ? cs::String("single-player only") : a.Travel->Active ? cs::String("already on the way") : cs::String(); }));
		CheatRegistry::Add("Travel", CheatEntry::Console("Void Station 1", "the nearest void station, the generator in view (US-T84)", "sg.VoidStation 1"));
		CheatRegistry::Add("Travel", CheatEntry::Action("System view", "the camera out over the whole system", [](UnitySpaceApp& a) { a.FocusSystemOverview(); })
			.WithKeys("Home").WithBlocker([](UnitySpaceApp& a) { return a.AtControls() ? cs::String("at the controls: P to release first") : cs::String(); }));

		// ---- Helm: embody, switch craft.
		CheatRegistry::Add("Helm", CheatEntry::Action("Take the helm of the selected ship", "you fly it; P again hands it back", [](UnitySpaceApp& a) { a.TogglePiloting(); })
			.WithKeys("P").WithBlocker([](UnitySpaceApp& a)
			{
				if (a.AtControls()) return cs::String("already at the controls (release first)");
				return a.Selected.IsShip() ? cs::String() : cs::String("select a ship first (click it, or Tab)");
			}));
		CheatRegistry::Add("Helm", CheatEntry::Action("Pilot the nearest ship", "the nearest hull you can take, wherever you are", [](UnitySpaceApp& a)
		{
			auto me = a.PlayerHelioKm();
			cs::Ref<Ship> best;
			double bestKm = cs::Double::PositiveInfinity;
			for (const auto& s : a.Fleet()->Ships())
			{
				if (s->Destroyed || s->IsEscort() || s == a.Piloted.Ship || !a.Fleet()->IsPresent(s) || s->Phase == ShipPhase::Landed) continue;
				if (!Embodiables::HelmOf(a.World(), s)->CanEmbody()) continue;
				double km = (a.Fleet()->StateOf(s, a.Clock->Jd).HelioKm - me).Length();
				if (km < bestKm)
				{
					bestKm = km;
					best = s;
				}
			}
			if (best == nullptr)
			{
				a.ShowToast("No hull here to take");
				return;
			}
			a.RememberCommand();
			a.ReleaseSeat();
			if (a.IsPiloting()) a.ExitPiloting();
			a.EnterPiloting(Target::OfShip(best));
		}).WithBlocker(blocker));
		CheatRegistry::Add("Helm", CheatEntry::Action("Next seat in the fight", "a ship's helm, a fighter's cockpit or a station in the fight in view", [](UnitySpaceApp& a) { a.CycleSeat(1); })
			.WithKeys("'"));
		CheatRegistry::Add("Helm", CheatEntry::Action("Seat picker", "every seat on the hull in hand and around the camera", [](UnitySpaceApp& a) { a.TogglePicker(); })
			.WithKeys("F9"));
		CheatRegistry::Add("Helm", CheatEntry::Action("Man the next gun", "down the barrel of the next gun on the hull held", [](UnitySpaceApp& a) { a.ManNextGun(); })
			.WithKeys("/"));
		CheatRegistry::Add("Helm", CheatEntry::Action("Into the craft just launched", "from a carrier's helm: the fighter or shuttle it launched last", [](UnitySpaceApp& a) { a.JumpToCraft(); })
			.WithKeys("Shift+/"));
		CheatRegistry::Add("Helm", CheatEntry::Action("Practice fighter", "one of your fighters at the camera, you at its helm", [](UnitySpaceApp& a) { a.SpawnPracticeFighter(); })
			.WithKeys("Shift+V").WithBlocker(blocker));
		CheatRegistry::Add("Helm", CheatEntry::Action("Release to command", "hand the seat back to its crew", [](UnitySpaceApp& a) { a.ReturnToCommand(); })
			.WithKeys("P").WithBlocker([](UnitySpaceApp& a) { return a.AtControls() ? cs::String() : cs::String("not at any controls"); }));

		// ---- Combat: the dogfight demo (US-T88, US-T101) and the hull flown.
		struct Demo { const char* Label; const char* Detail; bool Heavy; const char* Kit; };
		static const Demo demos[] = {
			{ "Dogfight demo", "a light fighter against four pirates off a quiet giant", false, "" },
			{ "Dogfight demo, heavy fighter", "the same at a heavy fighter's helm", true, "" },
			{ "Dogfight demo, full arsenal", "a heavy fighter with vulcans and hive missiles", true, "arsenal" },
			{ "Dogfight demo, vulcans", "a light fighter with rotary cannons", false, "vulcan" },
			{ "Dogfight demo, hive missiles", "a light fighter with hive missiles", false, "hive" },
			{ "Dogfight demo, rockets", "a light fighter with dumb-fire rockets", false, "rockets" },
		};
		for (const auto& d : demos)
		{
			const Demo* demo = &d;
			CheatRegistry::Add("Combat", CheatEntry::Action(d.Label, d.Detail, [demo](UnitySpaceApp& a) { a.StartDogfightDemo(demo->Heavy, demo->Kit); })
				.WithKeys(cs::Cat("sg.DogfightDemo", demo->Heavy ? " heavy" : "", *demo->Kit ? " " : "", demo->Kit)).WithBlocker(blocker));
		}
		CheatRegistry::Add("Combat", CheatEntry::Action("Mend the hull flown", "whole again, rearmed, tanks full", [](UnitySpaceApp& a) { a.CheatMendHull(); })
			.WithBlocker(atHelm).StaysOpen());
		CheatRegistry::Add("Combat", CheatEntry::Action("Watch the biggest battle", "the camera onto the biggest fight under way", [](UnitySpaceApp& a)
		{
			auto b = a.BiggestBattle();
			if (b != nullptr) a.WatchBattle(b);
			else a.ShowToast("No battles under way");
		}).WithKeys("B").WithBlocker([](UnitySpaceApp& a) { return a.AtControls() ? cs::String("at the controls: P to release first") : cs::String(); }));

		// ---- Time and scale.
		CheatRegistry::Add("Time & scale", CheatEntry::Action("Pause / resume", "eases to a stop and back", [](UnitySpaceApp& a) { a.TogglePause(); })
			.WithKeys("Space").WithState([](UnitySpaceApp& a) { return cs::String(a.Clock->Paused() ? "PAUSED" : "RUNNING"); }).StaysOpen());
		CheatRegistry::Add("Time & scale", CheatEntry::Menu("Time warp", "pick a pace", [](UnitySpaceApp&)
		{
			Entries out;
			const auto& levels = SimClock::Levels();
			for (int i = 0; i < levels.Count(); i++)
				out.push_back(CheatEntry::Action(levels[i].Label, i == 0 ? "the helm flies in real time" : "", [i](UnitySpaceApp& a)
				{
					if (i > 0 && a.IsPiloting() && a.Piloted.Ship->Battle != nullptr)
					{
						a.ShowToast("Time warp is held at real time while the helm is in a battle");
						return;
					}
					a.Clock->SetWarp(i);
					a.ShowToast(cs::Cat("Time: ", a.Clock->Warp().Label));
				}).WithState([i](UnitySpaceApp& a) { return cs::String(a.Clock->WarpIndex == i ? "NOW" : ""); }).StaysOpen());
			return out;
		}).WithState([](UnitySpaceApp& a) { return cs::Strings::ToUpperInvariant(a.Clock->Warp().Label); }));
		CheatRegistry::Add("Time & scale", CheatEntry::Action("Faster", "one step up the warp", [](UnitySpaceApp& a) { a.TimeFaster(); })
			.WithKeys(".").WithState([](UnitySpaceApp& a) { return cs::Strings::ToUpperInvariant(a.Clock->Warp().Label); }).StaysOpen());
		CheatRegistry::Add("Time & scale", CheatEntry::Action("Slower", "one step down the warp", [](UnitySpaceApp& a) { a.Clock->Slower(); })
			.WithKeys(",").WithState([](UnitySpaceApp& a) { return cs::Strings::ToUpperInvariant(a.Clock->Warp().Label); }).StaysOpen());
		CheatRegistry::Add("Time & scale", CheatEntry::Action("Back to now", "the clock to today's date", [](UnitySpaceApp& a) { a.ResetToNow(); })
			.WithKeys("T").WithBlocker([](UnitySpaceApp& a) { return a.AtControls() ? cs::String("at the controls: P to release first") : cs::String(); }));
		CheatRegistry::Add("Time & scale", CheatEntry::Action("Realistic / game scale", "true distances and sizes, or the navigable game scale", [](UnitySpaceApp& a) { a.ToggleScale(); })
			.WithKeys("G").WithState([](UnitySpaceApp& a) { return cs::String(a.ScaleGoal > 0.5 ? "GAME SCALE" : "REALISTIC"); }).StaysOpen());
	}

	namespace
	{
		UI::CheatRegistration GBuiltInCheats(&UnitySpaceApp::RegisterBuiltInCheats);
	}
}
