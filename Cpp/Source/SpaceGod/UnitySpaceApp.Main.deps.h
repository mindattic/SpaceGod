#pragma once

// What UnitySpaceApp.Main.inl's members need (port of Runtime/UnitySpaceApp.cs lines 1-1470).
// Complete types for the by-value members and the default member initialisers that make objects
// (helmFlight, pauseMenu); everything held by cs::Ref is forward-declared.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "UnityCompat/UnityTypes.h"
#include "Target.h"
#include "Ships.h"                     // ShipState (by value in shipStates), ShipClass
#include "Combat/PilotFlight.h"        // PilotState, PilotCruiseState, EngineTelegraph
#include "Combat/PilotTuning.h"        // PilotControl (by value)
#include "Embodiment/HelmFlight.h"     // helmFlight's initialiser (cs::New<HelmFlight>)
#include "Embodiment/FlightFeel.h"     // FlightFeel, HelmFlares (by value), US-T170
#include "UI/PauseMenu.h"              // pauseMenu's initialiser (cs::New<PauseMenu>)

#include <tuple>

namespace SpaceGod::Core
{
	class Galaxy;
	class StarSystem;
	class BodyCatalog;
	class BodyDefinition;
	class Ephemeris;
	class World;
	class SimClock;
	class ScaleModel;
	class Fleet;
	class Economy;
	class Faction;
	class Battle;
	class BattleUnit;
	class JumpGate;
	class SalvageSession;
	class ShipHelm;
	class Ship;
	class ShipCombat;
	class Wreck;
	class EscapePod;
	class LaneSite;
	class Opportunity;
	class Settlement;
}

namespace SpaceGod::Cameras
{
	class SpaceCamera;
	class StarTravel;
}

namespace SpaceGod::Views
{
	class InterstellarView;
	class StructureViews;
	class AsteroidBelt;
	class SkyView;
	class BodyView;
	class LineStrip;
	class ShipView;
	class BattleFx;
	class DebrisFields;
	class PodBeacons;
	class BioMassView;
	class DiscView;
	class GraveyardView;
	class HarvesterView;
	class SpaceFeaturesView;
	class FighterViews;
	class LaneView;
	class GateView;
	class EmbeddedPods;
	class SatelliteSwarm;
}

namespace SpaceGod::UI
{
	class Hud;
}
