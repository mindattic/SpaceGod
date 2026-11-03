// Not a C# file (US-T36): the ObjectGraph registrations (docs/PERSISTENCE.md) of the Infrastructure/ types a
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
	SG_ENUM(StructureKind, "Unity.SpaceGod.Core.StructureKind")

	SG_STRUCT(Constellation::Shell, "Unity.SpaceGod.Core.Constellation+Shell",
		SG_F(IncDeg), SG_F(PerPlane), SG_F(Planes), SG_F(RadiusKm))

	SG_STRUCT(SatelliteCommand, "Unity.SpaceGod.Core.SatelliteCommand",
		SG_F(Aim), SG_F(Roll), SG_F(StrafeRight), SG_F(StrafeUp), SG_F(Throttle))

	SG_SERIALIZE(Constellation, "Unity.SpaceGod.Core.Constellation",
		SG_F(Body), SG_F(Color), SG_F(Flying), SG_F(Name), SG_F(Shells))

	SG_SERIALIZE(DockingRing, "Unity.SpaceGod.Core.DockingRing",
		SG_F(Body), SG_F(City), SG_F(DescentDays), SG_F(EntryInterfaceKm), SG_F(EntryIntervalDays), SG_F(Ephemeris),
		SG_F(Id), SG_F(LatDeg), SG_F(LonDeg), SG_F(Name), SG_F(NextEntryJd), SG_F(RadiusKm), SG_F(Settlement),
		SG_F(SurfaceKm), SG_F(body), SG_F(lockedEpoch), SG_F(lockedRate), SG_F(lockedW0), SG_F(owner))

	SG_SERIALIZE(Infrastructure, "Unity.SpaceGod.Core.Infrastructure",
		SG_F(Constellations), SG_F(Rings), SG_F(Structures), SG_F(catalog), SG_F(ephemeris))

	SG_SERIALIZE(OrbitalStructure, "Unity.SpaceGod.Core.OrbitalStructure",
		SG_F(Body), SG_F(Industry), SG_F(Kind), SG_F(Lagrange), SG_F(LengthKm), SG_F(Name), SG_F(Note), SG_F(Orbit),
		SG_F(Real), SG_F(Settlement), SG_F(owner))

	SG_SERIALIZE(Satellite, "Unity.SpaceGod.Core.Satellite",
		SG_P(Body), SG_P(Constellation), SG_P(Index), SG_P(Jd), SG_P(LastThrustKmS2), SG_P(OnStation), SG_F(Command),
		SG_F(Forward), SG_F(OffsetKm), SG_F(Up), SG_F(VelocityKmS), SG_F(held))
}
