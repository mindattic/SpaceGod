// Not a C# file (US-T36): the ObjectGraph registrations (docs/PERSISTENCE.md) of the Information/ types a
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
	SG_ENUM(NewsCarrier, "Unity.SpaceGod.Core.NewsCarrier")

	SG_STRUCT(FactionPair, "Unity.SpaceGod.Core.FactionPair",
		SG_F(A), SG_F(B))

	SG_STRUCT(GateState, "Unity.SpaceGod.Core.GateState",
		SG_F(BlockadeUntilJd), SG_F(Damage), SG_F(Gate), SG_F(Owner))

	SG_STRUCT(PortThreat, "Unity.SpaceGod.Core.PortThreat",
		SG_F(BesiegedBy), SG_F(BlockadedBy), SG_F(Owner), SG_F(Port))

	SG_SERIALIZE(Newswire, "Unity.SpaceGod.Core.Newswire",
		SG_P(Delivered), SG_F(Received), SG_F(RelayIntervalDays), SG_F(galaxy), SG_F(inFlight), SG_F(known),
		SG_F(nextRelayJd))

	SG_SERIALIZE(SystemReport, "Unity.SpaceGod.Core.SystemReport",
		SG_F(AsOfJd), SG_F(CarriedBy), SG_F(Carrier), SG_F(Gates), SG_F(Havens), SG_F(Headlines), SG_F(Holder),
		SG_F(Markets), SG_F(PirateShips), SG_F(PortThreats), SG_F(ReceivedJd), SG_F(Source), SG_F(Wars))
}
