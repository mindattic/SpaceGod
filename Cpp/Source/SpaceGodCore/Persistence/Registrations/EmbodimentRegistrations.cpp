// Not a C# file (US-T36): the ObjectGraph registrations (docs/PERSISTENCE.md) of the Embodiment/ types a
// save can reach (from SaveData through every field, every implementer of a field's interface). C#'s ObjectGraph finds
// them by reflection; here each lists its C# instance fields by .NET name (the registry orders them as FieldsOf
// does). Generated from Unity.SpaceGod.Core.dll's metadata; regenerate rather than hand-edit when a C# class changes.

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/AlphaStrike.h"
#include "Combat/Battle.h"
#include "Combat/Boarding.h"
#include "Combat/BoardingCommand.h"
#include "Combat/Broadsides.h"
#include "Combat/CarrierWings.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Construction.h"
#include "Combat/Crews.h"
#include "Combat/Diplomacy.h"
#include "Combat/Dogfighting.h"
#include "Combat/EnergyWeapons.h"
#include "Combat/Facilities.h"
#include "Combat/Fighters.h"
#include "Combat/FlightDecks.h"
#include "Combat/FlightModels.h"
#include "Combat/GatePickets.h"
#include "Combat/ModularHull.h"
#include "Combat/Morale.h"
#include "Combat/Outfitting.h"
#include "Combat/PilotFlight.h"
#include "Combat/Pursuit.h"
#include "Combat/War.h"
#include "Combat/Wreck.h"
#include "DVec3.h"
#include "Economy/BeltMining.h"
#include "Economy/CargoHold.h"
#include "Economy/Charters.h"
#include "Economy/Colonisation.h"
#include "Economy/Companies.h"
#include "Economy/Contracts.h"
#include "Economy/Economy.h"
#include "Economy/Escorts.h"
#include "Economy/Goods.h"
#include "Economy/Holdings.h"
#include "Economy/Industry.h"
#include "Economy/LocalTraffic.h"
#include "Economy/MarketIntel.h"
#include "Economy/MarketShocks.h"
#include "Economy/Propulsion.h"
#include "Economy/Settlement.h"
#include "Economy/Smuggling.h"
#include "Economy/TradeDesk.h"
#include "Economy/TradeInsurance.h"
#include "Embodiment/MountedTurret.h"
#include "Embodiment/MechWalker.h" // US-T270
#include "Ephemeris.h"
#include "Events/EventDirector.h"
#include "Events/Opportunity.h"
#include "Fleet.h"
#include "GalacticPosition.h"
#include "Hyperlanes.h"
#include "Information/News.h"
#include "Infrastructure/Infrastructure.h"
#include "Infrastructure/Satellite.h"
#include "Lagrange.h"
#include "Law/Law.h"
#include "Law/Outlaws.h"
#include "OrbitalElements.h"
#include "Persistence/SaveGame.h"
#include "Sensors/Sensors.h"
#include "Ships.h"
#include "SimClock.h"
#include "Systems/CruiseDrive.h"
#include "Systems/DeepSpace.h"
#include "Systems/DeepSpaceLife.h"
#include "Systems/FleetDoctrine.h"
#include "Systems/FtlRules.h"
#include "Systems/Galaxy.h"
#include "Systems/GateConstruction.h"
#include "Systems/GateNetwork.h"
#include "Systems/InterstellarColonisation.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/InterstellarWars.h"
#include "Systems/JumpGate.h"
#include "Systems/LaneUpkeep.h"
#include "Systems/MilitarySupply.h"
#include "Systems/PirateClans.h"
#include "Systems/Polities.h"
#include "Systems/StarDiscovery.h"
#include "Systems/StarSystemBuilder.h"
#include "Systems/StarSystemSpec.h"
#include "Systems/Statecraft.h"
#include "Systems/StrategicCommand.h"
#include "Trajectories.h"
#include "World.h"
#include "Persistence/Registrations/Registrations.h"

namespace SpaceGod::Core
{
	SG_SERIALIZE(MountControl, "Unity.SpaceGod.Core.MountControl",
		SG_F(Aim), SG_F(ByStation), SG_F(Trigger))

	// US-T270: a mech out of a bay (Battle.Mechs), saved with its fight; not a C# type (the mech is new to the port), so every
	// field is its own. Whether a player holds it is not saved: a load drops the player back in command, beside it.
	SG_SERIALIZE(MechWalker, "Unity.SpaceGod.Core.MechWalker",
		SG_F(AimDir), SG_F(AimPitchDeg), SG_F(Armor), SG_F(Battle), SG_F(Callsign), SG_F(CannonAmmo), SG_F(Carrier),
		SG_F(Event), SG_F(EventS), SG_F(Facing), SG_F(Footfalls), SG_F(GaitPhase), SG_F(Heat), SG_F(HitsTaken), SG_F(Hull),
		SG_F(JetFuel), SG_F(JetsLit), SG_F(Landings), SG_F(Legs), SG_F(Local), SG_F(Lost), SG_F(Missiles), SG_F(MissilesFired),
		SG_F(Overheated), SG_F(Pos), SG_F(ShellsFired), SG_F(SinceHit), SG_F(SinceLanding), SG_F(Side), SG_F(Speed),
		SG_F(TorsoYawDeg), SG_F(Up), SG_F(Vel), SG_F(cannonCooldown), SG_F(liftGrace), SG_F(missileCooldown),
		SG_F(nextArm), SG_F(salvoLeft), SG_F(salvoTarget), SG_F(salvoTimer), SG_F(secondaryWas))
}
