// Port of Runtime/UnitySpaceApp.Practice.cs (declarations in UnitySpaceApp.Practice.inl).

#include "UnitySpaceApp.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Combat/Arsenal.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/Outfitting.h"
#include "Combat/CombatState.h"
#include "Combat/War.h"
#include "Combat/Wreck.h"
#include "Economy/Holdings.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/ShipHelm.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Infrastructure/Infrastructure.h"
#include "PracticeSpawn.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"

#include "Coop/CoopPeer.h"
#include "GameLaunch.h"
#include "SpaceGodLog.h"
#include "Tour/ShowcaseTour.h"
#include "UnitySpaceAppActor.h"
#include "HAL/IConsoleManager.h"

using namespace SpaceGod::Core;

namespace
{
	/// US-T88: <c>sg.DogfightDemo [heavy] [vulcan|rockets|hive|arsenal]</c> in the console (~): the dogfight demo, from
	/// wherever the game is, the craft rearmed with the kit named (US-T101).
	FAutoConsoleCommandWithWorldAndArgs GDogfightDemoCommand(
		TEXT("sg.DogfightDemo"),
		TEXT("US-T88: drop into the dogfight demo, at a light fighter's helm against four pirates (sg.DogfightDemo heavy: a heavy fighter's). "
			"US-T101: add vulcan, rockets, hive or arsenal to rearm the craft (e.g. sg.DogfightDemo heavy arsenal)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& args, UWorld* world)
		{
			auto* actor = AUnitySpaceAppActor::Find(world);
			auto* app = actor != nullptr ? actor->GetApp() : nullptr;
			if (app == nullptr)
			{
				UE_LOG(LogSpaceGod, Warning, TEXT("sg.DogfightDemo: no game running (start one first, or launch with -dogfight)"));
				return;
			}
			bool heavy = false;
			cs::String kit;
			for (const auto& a : args)
			{
				if (a.Equals(TEXT("heavy"), ESearchCase::IgnoreCase)) heavy = true;
				else kit = TCHAR_TO_UTF8(*a.ToLower());
			}
			app->StartDogfightDemo(heavy, kit);
		}));

	/// US-T173: the craft the dogfight demo last put the player at the helm of, and how it was asked for, so the tour's stops
	/// can fly it, keep it alive for the shot and find it again at the other scale.
	struct DogfightTourCraft
	{
		cs::Ref<SpaceGod::Core::Ship> Ship;
		bool Heavy = false;
		cs::String Kit;
		int Respawns = 0;
	};
	DogfightTourCraft& TourCraft()
	{
		static DogfightTourCraft c;
		return c;
	}

	/// US-T147: the dogfight demo's showcase tour stops, a light fighter's and a heavy fighter's.
	SpaceGod::Tour::TourRegistration GDogfightTourStops([]
	{
		using SpaceGod::Tour::TourRegistry;
		using SpaceGod::Tour::TourStop;
		auto atHelm = [](SpaceGod::UnitySpaceApp& app, double t) { return app.IsPiloting() && t > 4; };
		TourRegistry::Add(TourStop::Make("Flight and combat", "dogfight-cockpit", "Dogfight from the helm",
			"A light fighter against four pirates: the lead reticle on the target ahead, and arrows at the screen's edge pointing to the rest")
			.At(10).WithSetup([](SpaceGod::UnitySpaceApp& app) { TourCraft().Respawns = 0; app.StartDogfightDemo(false); return cs::String(); }).Until(atHelm, 30).WithHelm([](SpaceGod::UnitySpaceApp& app, double) { app.FlyDogfightTourCraft(); }).For(10).AtBothScales());
		TourRegistry::Add(TourStop::Make("Flight and combat", "dogfight-heavy", "Heavy fighter",
			"The same fight at a heavy fighter's helm with the full arsenal: slower to turn, harder hitting, vulcans and hive missiles")
			.At(20).WithSetup([](SpaceGod::UnitySpaceApp& app) { TourCraft().Respawns = 0; app.StartDogfightDemo(true, "arsenal"); return cs::String(); }).Until(atHelm, 30).WithHelm([](SpaceGod::UnitySpaceApp& app, double) { app.FlyDogfightTourCraft(); }).For(10));
	});
}

namespace SpaceGod
{
	void UnitySpaceApp::SpawnPracticeFighter()
	{
		// Between the stars there is no system to fly in (HandleCommands does not even get here then).
		if (Travel->Active || InFtlCruise()) return;

		// Where the camera is now, taken before any helm is let go: its free-flight frame goes stale while it is
		// focused on something far off.
		int frame = SpaceCamera->FrameAt(SpaceCamera->ScenePosition);
		auto offsetKm = Scale()->InverseLocalOffset((*Catalog())[frame], SpaceCamera->ScenePosition - BodyScene(frame));
		auto forwardUnity = SpaceCamera->Rotation * Vector3::forward();
		auto forward = DVec3(forwardUnity.x, forwardUnity.y, forwardUnity.z).SwapYZ();

		RememberCommand();
		ReleaseSeat();
		if (IsPiloting()) ExitPiloting();
		// Out of any screen the spawn was asked from (the spawn grid, the galaxy map): straight to the helm.
		ShowSpawn = ShowGalaxyMap = ShowPicker = false;
		if (CoopSpawns("practice", cs::String(), frame, offsetKm, forward)) return; // US-T44 (US-T43): the rest on every peer (CoopSpawnPractice)

		double jd = Clock->Jd;
		auto frameHelio = Ephemeris()->Helio(frame);
		auto ship = ReusablePracticeFighter();
		CollectPracticeObstacles(ship);
		double lengthKm = ship ? ship->LengthKm() : CombatSpec::Of(ShipClass::Fighter)->LengthKm;
		auto at = PracticeSpawn::Clear(frameHelio + offsetKm, forward, lengthKm, practiceObstacles) - frameHelio;

		if (ship != nullptr)
		{
			// The same hull, brought over: whole again, tanks full, parked here.
			ship->Combat->Repair(1);
			ship->Combat->FreeResupply();
			ship->SetPropellant(ship->PropellantCapacity());
			ship->OutOfPropellant = false;
			ship->StrandedInFlight = false;
			Fleet()->PinAt(ship, frame, at, forward, jd);
		}
		else
		{
			ship = Fleet()->SpawnAt(PracticeSpawn::Name, ShipClass::Fighter, World()->Sol, frame, at, forward, jd);
			ship->Ports = World()->FreeShipPorts(); // not a hidden haven's rock, as for every free ship
		}

		EnterPiloting(Target::OfShip(ship));
		ShowToast(cs::Cat("Practice fighter: ", ship->Name, " at the camera  (Shift+V again to bring it to the camera, P to hand it back)"));
	}

	cs::Ref<Ship> UnitySpaceApp::ReusablePracticeFighter()
	{
		for (const auto& s : Fleet()->Ships())
		{
			if (!PracticeSpawn::Is(s)) continue;
			if (!Fleet()->IsPresent(s) || s->Destroyed || s->Derelict || s->Battle != nullptr || s->IsEscort()) continue;
			if (s->Faction != World()->Sol || s->Combat == nullptr || !s->Combat->Alive()) continue;
			if (s->Phase == ShipPhase::Lane || s->Planetside()) continue;
			if (World()->Holdings != nullptr && World()->Holdings->Owns(s)) continue; // enlisted in the player's fleet: theirs now
			if (!Embodiables::HelmOf(World(), s)->CanEmbody()) continue;
			return s;
		}
		return nullptr;
	}

	void UnitySpaceApp::CollectPracticeObstacles(const cs::Ref<Ship>& self)
	{
		practiceObstacles.Clear();
		double jd = Clock->Jd;
		auto battles = cs::HashSet<cs::Ref<Battle>>();
		for (const auto& s : Fleet()->Ships())
		{
			if (s == self || s->Destroyed || s->Combat == nullptr || s->Planetside() || s->IsEscort()) continue;
			if (s->Battle != nullptr)
			{
				battles.Add(s->Battle);
				continue;
			}
			auto helio = s->Derelict && s->Orbit != nullptr ? DerelictHelio(Ephemeris(), s, jd) : Fleet()->StateOf(s, jd).HelioKm;
			practiceObstacles.Add(SpawnObstacle(helio, s->LengthKm() * 0.5));
		}
		for (const auto& b : battles)
		{
			auto origin = Ephemeris()->Helio(b->Body) + b->Anchor;
			for (const auto& u : b->Units)
				if (u->Present() && (self == nullptr || u->Ship != self))
					practiceObstacles.Add(SpawnObstacle(origin + u->Pos, u->LengthKm * 0.5));
		}
		for (const auto& st : World()->Infrastructure->Structures)
			practiceObstacles.Add(SpawnObstacle(st->HelioAt(Ephemeris(), jd), st->LengthKm * 0.5));
		for (const auto& w : World()->War->Wrecks->Items)
			practiceObstacles.Add(SpawnObstacle(World()->WreckHelio(w, jd), w->DebrisRadiusKm(jd) + w->LengthKm));
		for (const auto& body : Catalog()->Bodies())
			if (body->RadiusKm > 0)
				practiceObstacles.Add(SpawnObstacle(Ephemeris()->Helio(body->Index), body->RadiusKm * 1.02));
	}

	// ---------------------------------------------------------------- the dogfight demo (US-T88)

	void UnitySpaceApp::UpdateDogfightDemo()
	{
		if (!dogfightDemoAsked)
		{
			dogfightDemoAsked = true;
			int asked = GameLaunch::TakeDogfightDemo();
			if (asked != 0)
			{
				dogfightDemoHeavy = asked == 2;
				dogfightDemoDueFrames = 3; // a few frames for the scene to lay itself out first
			}
		}
		if (dogfightDemoDueFrames < 0) return;
		if (--dogfightDemoDueFrames > 0) return;
		dogfightDemoDueFrames = -1;
		StartDogfightDemo(dogfightDemoHeavy);
	}

	int UnitySpaceApp::DogfightDemoBody()
	{
		// A giant fills the view the same at both scales (its own size dwarfs the compression), and none is settled.
		static const char* const giants[] = { "Saturn", "Uranus", "Neptune", "Jupiter" };
		int fallback = -1;
		for (const char* name : giants)
		{
			cs::Ref<BodyDefinition> b;
			if (!Catalog()->TryGet(name, b) || b == nullptr) continue;
			if (fallback < 0) fallback = b->Index;
			if (World()->War->BattleAt(b->Index) != nullptr) continue;
			bool busy = false;
			for (const auto& s : Fleet()->Ships())
			{
				if (s->Destroyed || s->Derelict || s == World()->PilotedShip) continue;
				auto c = s->Controller();
				if (c == nullptr || c->Destroyed || c->Role == ShipRole::Commuter || c->Orbit == nullptr) continue;
				if (Core::Fleet::IsAt(c, b->Index)) { busy = true; break; }
			}
			if (!busy) return b->Index;
		}
		if (fallback >= 0) return fallback;
		// Another star's system: its largest world.
		int best = -1;
		double radius = 0;
		for (const auto& b : Catalog()->Bodies())
			if (!b->IsStar() && b->RadiusKm > radius)
			{
				radius = b->RadiusKm;
				best = b->Index;
			}
		return best;
	}

	void UnitySpaceApp::StartDogfightDemo(bool heavy, const cs::String& kit)
	{
		if (Coop::CoopPeer::Current() != nullptr)
		{
			ShowToast("The dogfight demo is single-player only");
			return;
		}
		if (Travel->Active || InFtlCruise())
		{
			ShowToast("No dogfight demo between the stars: arrive first");
			return;
		}
		int body = DogfightDemoBody();
		if (body < 0) return;

		RememberCommand();
		ReleaseSeat();
		if (IsPiloting()) ExitPiloting();
		ShowSpawn = ShowGalaxyMap = ShowPicker = false;
		Clock->SetPaused(false);

		double jd = Clock->Jd;
		auto def = (*Catalog())[body];
		// Out on the sunlit side and well above the ring plane, facing the world: its lit face (and rings) behind the
		// enemy. The pole's tilt keeps the rings an open ellipse even near a ring-plane crossing.
		auto helio = Ephemeris()->Helio(body);
		auto sun = helio.LengthSquared() > 1e-6 ? (-helio).Normalized() : DVec3::UnitX;
		auto pole = def->Pole.LengthSquared() > 1e-12 ? def->Pole.Normalized() : DVec3::UnitZ;
		auto across = DVec3::Cross(pole, sun);
		across = across.LengthSquared() > 1e-12 ? across.Normalized() : DVec3::UnitY;
		auto out = (sun * 0.75 + pole * 0.55 + across * 0.35).Normalized();
		// Clear of the rings, and at least as far as any fight there is laid out (Battle::AnchorDistanceKm, which keeps the
		// battle's km scale clear of the drawn world at the gameplay scale); within the world's neighbourhood, where a
		// player's fight may start (War::TryStartPlayerBattle).
		double rangeKm = cs::Math::Max(cs::Math::Max(def->RadiusKm * 2.6, def->RingOuterKm * 1.3), Battle::AnchorDistanceKm(def));
		rangeKm = cs::Math::Min(rangeKm, ScaleModel::NeighbourhoodKm(def) * 0.9);
		auto at = out * rangeKm;
		auto forward = -out;
		auto up = (pole - forward * DVec3::Dot(pole, forward));
		up = up.LengthSquared() > 1e-12 ? up.Normalized() : DVec3::UnitZ;
		auto right = DVec3::Cross(forward, up).Normalized();

		auto cls = heavy ? ShipClass::Gunship : ShipClass::Fighter;
		auto ship = Fleet()->SpawnAt(heavy ? "Demo Heavy Fighter" : "Demo Fighter", cls, World()->Sol, body, at, forward, jd);
		ship->Ports = World()->FreeShipPorts(); // not a hidden haven's rock, as for every free ship
		// The kit asked for (US-T101): the craft's guns and missile mounts rearmed, as a yard would fit them.
		cs::String armed;
		if (kit == "vulcan" || kit == "arsenal")
			if (Arsenal::FitAll(ship, ModuleKind::VulcanCannon) > 0) armed = "vulcan cannons";
		if (kit == "hive" || kit == "arsenal")
			if (Arsenal::FitAll(ship, ModuleKind::HiveLauncher) > 0) armed = armed.empty() ? cs::String("hive missiles") : armed + " and hive missiles";
		if (kit == "rockets" && Arsenal::FitAll(ship, ModuleKind::RocketPod) > 0) armed = "dumb-fire rockets";
		if (!kit.empty() && armed.empty())
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.DogfightDemo: no kit '%s' (vulcan, rockets, hive or arsenal), the craft flies as built"), UTF8_TO_TCHAR(kit.c_str()));

		// Small, medium and large raiders, km ahead (along the nose), right and up of the player, all closing.
		struct Slot { ShipClass Class; const char* Name; double Ahead, Right, Up; };
		static const Slot slots[] = {
			{ ShipClass::Interceptor, "Raider Interceptor", 3.5, 1.4, 0.5 },
			{ ShipClass::Fighter, "Raider Fighter", 4.2, -1.6, -0.3 },
			{ ShipClass::Corvette, "Raider Corvette", 7.5, 3.0, 1.0 },
			{ ShipClass::Destroyer, "Raider Destroyer", 11.0, -3.2, -1.2 },
		};
		cs::List<cs::Ref<Ship>> pirates;
		cs::List<DVec3> pirateSpots;
		for (const auto& slot : slots)
		{
			auto local = forward * slot.Ahead + right * slot.Right + up * slot.Up;
			auto p = Fleet()->SpawnAt(slot.Name, slot.Class, World()->Pirates, body, at + local, -forward, jd);
			p->Ports = World()->FreeShipPorts();
			pirates.Add(p);
			pirateSpots.Add(local);
		}

		EnterPiloting(Target::OfShip(ship));
		{
			auto& tc = TourCraft();
			tc.Ship = ship;
			tc.Heavy = heavy;
			tc.Kit = kit;
		}
		auto battle = World()->War->TryStartPlayerBattle(ship, pilotFrameBody, pilotOffsetKm(), pilotForward(), jd);
		int seated = 0;
		if (battle != nullptr)
		{
			// The fight opens with the raiders where they were spawned, a few km off the nose, not at its usual stations.
			auto me = battle->UnitOf(ship);
			auto origin = me != nullptr ? me->Pos : DVec3::Zero;
			for (int i = 0; i < pirates.Count(); i++)
			{
				auto u = battle->UnitOf(pirates[i]);
				if (u == nullptr) continue;
				u->Pos = origin + pirateSpots[i];
				u->Vel = DVec3::Zero;
				u->Forward = (origin - u->Pos).Normalized();
				seated++;
			}
		}
		UE_LOG(LogSpaceGod, Log, TEXT("Dogfight demo (US-T88): %s at the helm of the %s off %s (%.0f km out), fight %s, %d of %d pirates in it"),
			IsPiloting() ? TEXT("piloting") : TEXT("NOT piloting"), UTF8_TO_TCHAR(ship->Name.c_str()), UTF8_TO_TCHAR(def->Name.c_str()), rangeKm,
			battle != nullptr ? UTF8_TO_TCHAR(battle->Name.c_str()) : TEXT("not started"), seated, pirates.Count());
		ShowToast(cs::Cat("DOGFIGHT DEMO off ", def->Name, ": ", heavy ? "heavy" : "light", " fighter", armed.empty() ? cs::String() : cs::Cat(" with ", armed),
			" vs a pirate interceptor, fighter, corvette and destroyer",
			"  (Left-click fires, right-click the missiles or rockets, T locks, F flight assist, Alt flip, Shift+strafe dash, P to release)"));
	}

	/// US-T173: the tour idles the player's input, so a dogfight stop's helm is flown here: the nose laid on the nearest
	/// raider, the guns held, a slow approach that stops short of ramming; the hull and shields held up so the pirates'
	/// opening fire cannot scuttle it before the shot; and, if the helm was lost (the other scale's pass, a respawn), taken
	/// again or the demo started over rather than showing an empty free camera.
	void UnitySpaceApp::FlyDogfightTourCraft()
	{
		auto& c = TourCraft();
		auto ship = c.Ship;
		if (ship == nullptr || ship->Destroyed || ship->Combat == nullptr)
		{
			if (c.Respawns++ < 3) StartDogfightDemo(c.Heavy, c.Kit);
			return;
		}
		if (!IsPiloting() && World()->PilotedShip == nullptr) EnterPiloting(SpaceGod::Target::OfShip(ship));
		auto combat = ship->Combat;
		if (combat->Spec != nullptr)
		{
			double floor = combat->Spec->Hull * 0.7;
			if (combat->Hull < floor) combat->Hull = floor;
		}
		for (const auto& sub : combat->Subsystems)
			if (sub->MaxShield > 0 && sub->Shield < sub->MaxShield * 0.7) sub->Shield = sub->MaxShield * 0.7;
		auto battle = ship->Battle;
		if (battle == nullptr) return;
		auto me = battle->UnitOf(ship);
		if (me == nullptr) return;
		cs::Ref<SpaceGod::Core::BattleUnit> nearest;
		double best = 1e30;
		for (const auto& u : battle->Units)
		{
			if (u == me || !u->Present() || u->Kind != SpaceGod::Core::UnitKind::Ship || !battle->Hostile(me, u)) continue;
			double d = (u->Pos - me->Pos).Length();
			if (d < best) { best = d; nearest = u; }
		}
		if (nearest == nullptr) return;
		pilotAim = (nearest->Pos - me->Pos).Normalized();
		Input.Fire = true;
		Input.Move.z = best > 2.5 ? 0.7f : best < 1.0 ? -0.6f : 0.0f;
	}
}
