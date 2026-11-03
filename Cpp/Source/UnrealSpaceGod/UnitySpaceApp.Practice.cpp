// Port of Runtime/UnitySpaceApp.Practice.cs (declarations in UnitySpaceApp.Practice.inl).

#include "UnitySpaceApp.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
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

using namespace SpaceGod::Core;

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
}
