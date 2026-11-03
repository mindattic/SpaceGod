// Not a C# file (US-T36): the ObjectGraph registrations (docs/PERSISTENCE.md) of the Events/ types a
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
	SG_ENUM(OpportunityKind, "Unity.SpaceGod.Core.OpportunityKind")

	SG_ENUM(OpportunityState, "Unity.SpaceGod.Core.OpportunityState")

	SG_STRUCT(OpportunityReward, "Unity.SpaceGod.Core.OpportunityReward",
		SG_F(CargoCrates), SG_F(CargoGood), SG_F(Credits), SG_F(PatronShare), SG_F(Reputation), SG_F(ReputationWith))

	SG_SERIALIZE(EventDirector, "Unity.SpaceGod.Core.EventDirector",
		SG_F(Active), SG_F(AmbushDanger), SG_F(AnomalyRatePerDay), SG_F(ArriveKm), SG_F(BountyRadiusKm),
		SG_F(DryTanksTransfer), SG_F(EscortDanger), SG_F(History), SG_F(HistoryLength), SG_F(MaxActive),
		SG_F(MinEscortDays), SG_F(PodPickupKm), SG_F(PodRescueCredits), SG_F(PodRescueReputation), SG_F(Reputation),
		SG_F(Resolved), SG_F(ScanIntervalDays), SG_F(SpawnRadiusKm), SG_F(Spawned), SG_F(SurvivorRescueCredits),
		SG_F(SurvivorRescueReputation), SG_F(lastAnomalyRollJd), SG_F(nextId), SG_F(nextScanJd), SG_F(restUntil),
		SG_F(rng))

	SG_SERIALIZE(Opportunity, "Unity.SpaceGod.Core.Opportunity",
		SG_F(Battle), SG_F(Body), SG_F(Captain), SG_F(DryTanks), SG_F(Engaged), SG_F(EngagedBy),
		SG_F(EngagedFaction), SG_F(ExpiresJd), SG_F(HelioKm), SG_F(Hook), SG_F(Id), SG_F(Kind), SG_F(Lane),
		SG_F(OffsetKm), SG_F(Outcome), SG_F(Patron), SG_F(Pod), SG_F(ResolvedJd), SG_F(Reward), SG_F(Ship),
		SG_F(SourceKey), SG_F(SpawnJd), SG_F(State), SG_F(Title), SG_F(UnderAttack), SG_F(Wreck))
}
