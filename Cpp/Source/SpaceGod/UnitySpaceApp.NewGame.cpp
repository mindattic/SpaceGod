// US-T247: the New Game setup page's starting situations (declarations in UnitySpaceApp.NewGame.inl). Not a port of a C# file.

#include "UnitySpaceApp.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Camera/StarTravel.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/War.h"
#include "Economy/Holdings.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/ShipHelm.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "PracticeSpawn.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"

#include "SpaceGodLog.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		bool IsLightCraft(ShipClass c) { return c == ShipClass::Fighter || c == ShipClass::Interceptor || c == ShipClass::Gunship; }

		bool IsCapital(ShipClass c) { return c == ShipClass::Battleship || c == ShipClass::Carrier || c == ShipClass::Cruiser || c == ShipClass::Destroyer; }
	}

	void UnitySpaceApp::UpdateNewGameStart()
	{
		if (newGameStartDueFrames < 0) return;
		if (--newGameStartDueFrames > 0) return;
		newGameStartDueFrames = -1;
		StartSituation situation = newGameStart;
		newGameStart = StartSituation::GodView;
		StartSituationNow(situation);
	}

	bool UnitySpaceApp::StartSituationNow(StartSituation situation)
	{
		if (situation == StartSituation::GodView) return true;
		if (CoopIsClient()) return false; // the host's game is everyone's: a client takes it as it comes
		if (Travel->Active || InFtlCruise()) return false;

		RememberCommand();
		ReleaseSeat();
		if (IsPiloting()) ExitPiloting();
		ShowSpawn = ShowGalaxyMap = ShowPicker = false;
		Clock->SetPaused(false);

		bool started = false;
		switch (situation)
		{
		case StartSituation::FighterPatrol: started = StartFighterPatrol(); break;
		case StartSituation::FreighterCaptain: started = StartFreighterCaptain(); break;
		case StartSituation::CapitalWarZone: started = StartCapitalHelm(); break;
		default: break;
		}
		UE_LOG(LogSpaceGod, Log, TEXT("New game setup (US-T247): %s %s"), UTF8_TO_TCHAR(NewGameSetup::SituationName(situation).c_str()),
			started ? TEXT("started") : TEXT("could not be started (the god view instead)"));
		if (!started) ShowToast(cs::Cat("Could not start as ", NewGameSetup::SituationShort(situation), " here: you begin in the god view"));
		return started;
	}

	void UnitySpaceApp::NewGameSpot(int body, double lengthKm, DVec3& offsetKm, DVec3& forward)
	{
		auto def = (*Catalog())[body];
		// Out on the sunlit side, a little above the orbit plane, facing the world, as the dogfight demo places its fight:
		// clear of the world and its rings, and inside its neighbourhood (where a fight there may start).
		auto helio = Ephemeris()->Helio(body);
		auto sun = helio.LengthSquared() > 1e-6 ? (-helio).Normalized() : DVec3::UnitX;
		auto pole = def->Pole.LengthSquared() > 1e-12 ? def->Pole.Normalized() : DVec3::UnitZ;
		auto across = DVec3::Cross(pole, sun);
		across = across.LengthSquared() > 1e-12 ? across.Normalized() : DVec3::UnitY;
		auto out = (sun * 0.75 + pole * 0.45 + across * 0.45).Normalized();
		double rangeKm = cs::Math::Max(cs::Math::Max(def->RadiusKm * 2.6, def->RingOuterKm * 1.3), Battle::AnchorDistanceKm(def));
		rangeKm = cs::Math::Min(rangeKm, ScaleModel::NeighbourhoodKm(def) * 0.9);
		forward = -out;
		CollectPracticeObstacles(nullptr);
		offsetKm = PracticeSpawn::Clear(helio + out * rangeKm, forward, lengthKm, practiceObstacles) - helio;
	}

	bool UnitySpaceApp::StartFighterPatrol()
	{
		// A home-side fighter already flying patrol (or in a patrol's wing) here, whole, free of any fight and ready to fly.
		cs::Ref<Ship> best;
		int bestScore = -1;
		for (const auto& s : Fleet()->Ships())
		{
			if (s == nullptr || s->Destroyed || s->Derelict || s->Faction != World()->Sol || s->Spec == nullptr || !IsLightCraft(s->Spec->Class)) continue;
			if (!Fleet()->IsPresent(s) || s->Battle != nullptr || s->Planetside() || s->Phase == ShipPhase::Lane) continue;
			if (s->Combat == nullptr || !s->Combat->Alive()) continue;
			if (World()->Holdings != nullptr && World()->Holdings->Owns(s)) continue;
			if (!Embodiables::HelmOf(World(), s)->CanEmbody()) continue;
			int score = s->Role == ShipRole::Patrol ? 2 : s->Leader != nullptr && s->Leader->Role == ShipRole::Patrol ? 1 : 0;
			if (score > bestScore)
			{
				best = s;
				bestScore = score;
			}
		}
		bool spawned = false;
		if (best == nullptr || bestScore == 0)
		{
			// None on patrol: one of the home fleet's fighters put out on patrol near Earth.
			cs::Ref<BodyDefinition> earth;
			if (!Catalog()->TryGet("Earth", earth) || earth == nullptr)
			{
				if (best == nullptr) return false;
			}
			else
			{
				DVec3 at, forward;
				NewGameSpot(earth->Index, CombatSpec::Of(ShipClass::Fighter)->LengthKm, at, forward);
				best = Fleet()->SpawnAt("Patrol Fighter", ShipClass::Fighter, World()->Sol, earth->Index, at, forward, Clock->Jd);
				best->Ports = World()->FreeShipPorts(); // not a hidden haven's rock, as for every free ship
				spawned = true;
			}
		}
		EnterPiloting(Target::OfShip(best));
		if (!IsPiloting() && World()->PilotedShip != best) return false;
		ShowToast(cs::Cat("FIGHTER PILOT ON PATROL: at the helm of the ", best->Name, spawned ? cs::String(" off Earth") : cs::String(),
			"  (W/S thrust, mouse to steer, left-click fires, T locks, P to let go and watch)"));
		return true;
	}

	bool UnitySpaceApp::StartFreighterCaptain()
	{
		auto holdings = World()->Holdings;
		cs::Ref<BodyDefinition> earth;
		if (holdings == nullptr || !Catalog()->TryGet("Earth", earth) || earth == nullptr) return false;
		double jd = Clock->Jd;
		DVec3 at, forward;
		NewGameSpot(earth->Index, CombatSpec::Of(ShipClass::Freighter)->LengthKm, at, forward);
		auto ship = Fleet()->SpawnAt("Wayfarer", ShipClass::Freighter, World()->Sol, earth->Index, at, forward, jd);
		ship->Ports = World()->FreeShipPorts();
		// The player's own: under the house flag, its purse the house account, which starts small.
		if (holdings->Enlist(ship, jd) == nullptr) return false;
		holdings->Transact(NewGameSetup::FreighterPurse - holdings->Balance(), "Starting purse", jd, /*overdraw:*/ true);
		EnterPiloting(Target::OfShip(ship));
		if (!IsPiloting() && World()->PilotedShip != ship) return false;
		ShowToast(cs::Cat("FREIGHTER CAPTAIN: the ", ship->Name, " is yours, in orbit at Earth, with ", cs::Fmt(NewGameSetup::FreighterPurse, "N0"),
			" cr in the house account  (M for the market, F12 the trade desk, P to let go)"));
		return true;
	}

	bool UnitySpaceApp::StartCapitalHelm()
	{
		double jd = Clock->Jd;
		// A fight already on in this system: the largest warship of a lawful side in it.
		cs::Ref<Ship> best;
		double bestLength = 0;
		for (const auto& b : World()->War->Battles)
		{
			if (b == nullptr || b->Over) continue;
			for (const auto& u : b->Units)
			{
				if (u == nullptr || u->Kind != UnitKind::Ship || !u->Present() || u->Ship == nullptr || u->Faction == nullptr || u->Faction->IsPirate) continue;
				auto s = u->Ship;
				if (s->Destroyed || s->Spec == nullptr || !IsCapital(s->Spec->Class) || s->Combat == nullptr || !s->Combat->Alive()) continue;
				if (World()->Holdings != nullptr && World()->Holdings->Owns(s)) continue;
				if (!Embodiables::HelmOf(World(), s)->CanEmbody()) continue;
				if (s->LengthKm() > bestLength)
				{
					best = s;
					bestLength = s->LengthKm();
				}
			}
		}
		if (best != nullptr)
		{
			EnterPiloting(Target::OfShip(best));
			if (IsPiloting() || World()->PilotedShip == best)
			{
				ShowToast(cs::Cat("CAPITAL HELM: the ", best->Name, " of the ", best->Faction->Name, ", in ", best->Battle != nullptr ? best->Battle->Name : cs::String("the fight"),
					"  (left-click fires, right-click the missiles, T locks, P to let go)"));
				return true;
			}
		}

		// None on: a home-side cruiser off a quiet giant world, raiders closing (as the dogfight demo, at a warship's scale).
		int body = DogfightDemoBody();
		if (body < 0) return false;
		DVec3 at, forward;
		NewGameSpot(body, CombatSpec::Of(ShipClass::Cruiser)->LengthKm, at, forward);
		auto ship = Fleet()->SpawnAt("Home Guard Cruiser", ShipClass::Cruiser, World()->Sol, body, at, forward, jd);
		ship->Ports = World()->FreeShipPorts();
		auto pole = (*Catalog())[body]->Pole;
		auto up = pole - forward * DVec3::Dot(pole, forward);
		up = up.LengthSquared() > 1e-12 ? up.Normalized() : DVec3::UnitZ;
		auto right = DVec3::Cross(forward, up).Normalized();
		struct Slot { ShipClass Class; const char* Name; double Ahead, Right, Up; };
		static const Slot slots[] = {
			{ ShipClass::Destroyer, "Raider Destroyer", 24.0, 6.0, 2.0 },
			{ ShipClass::Corvette, "Raider Corvette", 18.0, -7.0, -1.5 },
			{ ShipClass::Corvette, "Raider Corvette II", 20.0, 9.0, -2.5 },
			{ ShipClass::Fighter, "Raider Fighter", 12.0, -3.0, 1.0 },
			{ ShipClass::Interceptor, "Raider Interceptor", 11.0, 4.0, 0.5 },
		};
		cs::List<cs::Ref<Ship>> raiders;
		cs::List<DVec3> spots;
		for (const auto& slot : slots)
		{
			auto local = forward * slot.Ahead + right * slot.Right + up * slot.Up;
			auto p = Fleet()->SpawnAt(slot.Name, slot.Class, World()->Pirates, body, at + local, -forward, jd);
			p->Ports = World()->FreeShipPorts();
			raiders.Add(p);
			spots.Add(local);
		}
		EnterPiloting(Target::OfShip(ship));
		if (!IsPiloting() && World()->PilotedShip != ship) return false;
		auto battle = World()->War->TryStartPlayerBattle(ship, pilotFrameBody, pilotOffsetKm(), pilotForward(), jd);
		if (battle != nullptr)
		{
			// The fight opens with the raiders where they were put, closing from ahead, not at their usual stations.
			auto me = battle->UnitOf(ship);
			auto origin = me != nullptr ? me->Pos : DVec3::Zero;
			for (int i = 0; i < raiders.Count(); i++)
			{
				auto u = battle->UnitOf(raiders[i]);
				if (u == nullptr) continue;
				u->Pos = origin + spots[i];
				u->Vel = DVec3::Zero;
				u->Forward = (origin - u->Pos).Normalized();
			}
		}
		ShowToast(cs::Cat("CAPITAL HELM: the ", ship->Name, " off ", (*Catalog())[body]->Name, ", a pirate destroyer, two corvettes and their fighters closing",
			"  (left-click fires, right-click the missiles, T locks, P to let go)"));
		return true;
	}
}
