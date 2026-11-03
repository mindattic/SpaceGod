// Not a C# file (US-T36): the static fields of Core types that hold objects a save can share (docs/PERSISTENCE.md,
// "Statics"). C#'s ObjectGraph walks every static field of every Core type and writes an object it reaches there by
// name (an External entry: "Type::field[3].Spec"), so a loaded hull shares its design with every other of its class;
// here each such static is declared once. Generated from Unity.SpaceGod.Core.dll's metadata (static fields whose
// type holds a class a save reaches). C#'s walk reads each field with FieldInfo.GetValue, which runs the type's static
// constructor first; HullFrames emulates its own (StaticConstructor), so its tables are read after it, filled, as in C#
// (US-T41: before, HullFrames::All was walked empty and the station designs got other names than C#'s). The statics the
// C# walk names anything else through are in StaticTables.cpp.

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
#include "Combat/Radiators.h"
#include "Combat/SpinalMounts.h"
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
#include "Systems/AlphaCentauri.h"
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
	// Many of these tables are private: Graph::Access<0> is a friend of every class with SG_SERIALIZABLE.
	template <> struct Graph::Access<0>
	{
		static decltype(auto) S0() { return (AlphaCentauri::Orbit()); } // Unity.SpaceGod.Core.AlphaCentauri::Orbit
		static decltype(auto) S1() { return (AlphaStrike::AntiShipMissile()); } // Unity.SpaceGod.Core.AlphaStrike::AntiShipMissile
		static decltype(auto) S2() { return (AlphaStrike::MagazineTorpedo()); } // Unity.SpaceGod.Core.AlphaStrike::MagazineTorpedo
		static decltype(auto) S3() { return (AsteroidClass::Carbonaceous()); } // Unity.SpaceGod.Core.AsteroidClass::Carbonaceous
		static decltype(auto) S4() { return (AsteroidClass::CometIce()); } // Unity.SpaceGod.Core.AsteroidClass::CometIce
		static decltype(auto) S5() { return (AsteroidClass::Metallic()); } // Unity.SpaceGod.Core.AsteroidClass::Metallic
		static decltype(auto) S6() { return (AsteroidClass::Stony()); } // Unity.SpaceGod.Core.AsteroidClass::Stony
		static decltype(auto) S7() { return (AsteroidClass::registered()); } // Unity.SpaceGod.Core.AsteroidClass::registered
		static decltype(auto) S8() { return (BuildPlan::Balanced()); } // Unity.SpaceGod.Core.BuildPlan::Balanced
		static decltype(auto) S9() { return (BuildPlan::BattleLine()); } // Unity.SpaceGod.Core.BuildPlan::BattleLine
		static decltype(auto) S10() { return (BuildPlan::Carriers()); } // Unity.SpaceGod.Core.BuildPlan::Carriers
		static decltype(auto) S11() { return (BuildPlan::LaneSecurity()); } // Unity.SpaceGod.Core.BuildPlan::LaneSecurity
		static decltype(auto) S12() { return (BuildPlan::Merchant()); } // Unity.SpaceGod.Core.BuildPlan::Merchant
		static decltype(auto) S13() { return (BuildPlan::Mobilise()); } // Unity.SpaceGod.Core.BuildPlan::Mobilise
		static decltype(auto) S14() { return (BuildPlan::Screens()); } // Unity.SpaceGod.Core.BuildPlan::Screens
		static decltype(auto) S15() { return (BuildPlan::Strike()); } // Unity.SpaceGod.Core.BuildPlan::Strike
		static decltype(auto) S16() { return (BuildPlan::all()); } // Unity.SpaceGod.Core.BuildPlan::all
		static decltype(auto) S17() { return (Charters::Classes()); } // Unity.SpaceGod.Core.Charters::Classes
		static decltype(auto) S18() { return (Colonisation::ShipNames()); } // Unity.SpaceGod.Core.Colonisation::ShipNames
		static decltype(auto) S19() { return (CombatSpec::Platform()); } // Unity.SpaceGod.Core.CombatSpec::Platform
		static decltype(auto) S20() { return (CombatSpec::Specs()); } // Unity.SpaceGod.Core.CombatSpec::Specs
		static decltype(auto) S21() { return (Companies::Houses()); } // Unity.SpaceGod.Core.Companies::Houses
		static decltype(auto) S22() { return (CruiseDrives::Capital()); } // Unity.SpaceGod.Core.CruiseDrives::Capital
		static decltype(auto) S23() { return (CruiseDrives::Survey()); } // Unity.SpaceGod.Core.CruiseDrives::Survey
		static decltype(auto) S24() { return (CruiseDrives::byClass()); } // Unity.SpaceGod.Core.CruiseDrives::byClass
		static decltype(auto) S25() { return (Deal::Alliance()); } // Unity.SpaceGod.Core.Deal::Alliance
		static decltype(auto) S26() { return (Deal::DemandFealty()); } // Unity.SpaceGod.Core.Deal::DemandFealty
		static decltype(auto) S27() { return (Deal::DemandTribute()); } // Unity.SpaceGod.Core.Deal::DemandTribute
		static decltype(auto) S28() { return (Deal::NonAggression()); } // Unity.SpaceGod.Core.Deal::NonAggression
		static decltype(auto) S29() { return (Deal::OfferFealty()); } // Unity.SpaceGod.Core.Deal::OfferFealty
		static decltype(auto) S30() { return (Deal::OfferTribute()); } // Unity.SpaceGod.Core.Deal::OfferTribute
		static decltype(auto) S31() { return (Deal::Peace()); } // Unity.SpaceGod.Core.Deal::Peace
		static decltype(auto) S32() { return (Deal::TradePact()); } // Unity.SpaceGod.Core.Deal::TradePact
		static decltype(auto) S33() { return (Deal::all()); } // Unity.SpaceGod.Core.Deal::all
		static decltype(auto) S34() { return (DeepSpace::FieldSources()); } // Unity.SpaceGod.Core.DeepSpace::FieldSources
		static decltype(auto) S35() { return (EnergyWeapons::PhaserLance()); } // Unity.SpaceGod.Core.EnergyWeapons::PhaserLance
		static decltype(auto) S36() { return (EnergyWeapons::PhaserSweep()); } // Unity.SpaceGod.Core.EnergyWeapons::PhaserSweep
		static decltype(auto) S37() { return (EnergyWeapons::PlasmaCannon()); } // Unity.SpaceGod.Core.EnergyWeapons::PlasmaCannon
		static decltype(auto) S38() { return (EventDirector::Anomalies()); } // Unity.SpaceGod.Core.EventDirector::Anomalies
		static decltype(auto) S39() { return (FacilityKinds::All()); } // Unity.SpaceGod.Core.FacilityKinds::All
		static decltype(auto) S40() { return (FacilityKinds::ById()); } // Unity.SpaceGod.Core.FacilityKinds::ById
		static decltype(auto) S41() { return (FacilityLayouts::ByClass()); } // Unity.SpaceGod.Core.FacilityLayouts::ByClass
		static decltype(auto) S42() { return (FacilityLayouts::Platform()); } // Unity.SpaceGod.Core.FacilityLayouts::Platform
		static decltype(auto) S43() { return (FacilityLayouts::Satellite()); } // Unity.SpaceGod.Core.FacilityLayouts::Satellite
		static decltype(auto) S44() { return (FacilityLayouts::Small()); } // Unity.SpaceGod.Core.FacilityLayouts::Small
		static decltype(auto) S45() { return (FacilityLayouts::Station()); } // Unity.SpaceGod.Core.FacilityLayouts::Station
		static decltype(auto) S46() { return (FighterAI::BomberSpec()); } // Unity.SpaceGod.Core.FighterAI::BomberSpec
		static decltype(auto) S47() { return (FighterAI::Interceptor()); } // Unity.SpaceGod.Core.FighterAI::Interceptor
		static decltype(auto) S48() { return (FighterCraft::AssaultShuttle()); } // Unity.SpaceGod.Core.FighterCraft::AssaultShuttle
		static decltype(auto) S49() { return (FighterCraft::Bomber()); } // Unity.SpaceGod.Core.FighterCraft::Bomber
		static decltype(auto) S50() { return (FighterCraft::HeavyFighter()); } // Unity.SpaceGod.Core.FighterCraft::HeavyFighter
		static decltype(auto) S51() { return (FighterCraft::Interceptor()); } // Unity.SpaceGod.Core.FighterCraft::Interceptor
		static decltype(auto) S52() { return (FighterCraft::Recon()); } // Unity.SpaceGod.Core.FighterCraft::Recon
		static decltype(auto) S53() { return (FighterCraft::SpaceSuperiority()); } // Unity.SpaceGod.Core.FighterCraft::SpaceSuperiority
		static decltype(auto) S54() { return (FighterCraft::SquadronMix()); } // Unity.SpaceGod.Core.FighterCraft::SquadronMix
		static decltype(auto) S55() { return (Fleet::CoarseTimeFactors()); } // Unity.SpaceGod.Core.Fleet::CoarseTimeFactors
		static decltype(auto) S56() { return (Fleet::EscortSlots()); } // Unity.SpaceGod.Core.Fleet::EscortSlots
		static decltype(auto) S57() { return (Fleet::TransferTimeFactors()); } // Unity.SpaceGod.Core.Fleet::TransferTimeFactors
		static decltype(auto) S58() { return (FlightDecks::None()); } // Unity.SpaceGod.Core.FlightDecks::None
		static decltype(auto) S59() { return (GateConstruction::Materials()); } // Unity.SpaceGod.Core.GateConstruction::Materials
		static decltype(auto) S60() { return (Holdings::CaptainNames()); } // Unity.SpaceGod.Core.Holdings::CaptainNames
		static decltype(auto) S61() { HullFrames::StaticConstructor(); return (HullFrames::All()); } // Unity.SpaceGod.Core.HullFrames::All
		static decltype(auto) S62() { HullFrames::StaticConstructor(); return (HullFrames::ById()); } // Unity.SpaceGod.Core.HullFrames::ById
		static decltype(auto) S63() { HullFrames::StaticConstructor(); return (HullFrames::OrbitalStation()); } // Unity.SpaceGod.Core.HullFrames::OrbitalStation
		static decltype(auto) S64() { HullFrames::StaticConstructor(); return (HullFrames::SensorSatellite()); } // Unity.SpaceGod.Core.HullFrames::SensorSatellite
		static decltype(auto) S65() { HullFrames::StaticConstructor(); return (HullFrames::WeaponsSatellite()); } // Unity.SpaceGod.Core.HullFrames::WeaponsSatellite
		static decltype(auto) S66() { return (LaneUpkeep::Names()); } // Unity.SpaceGod.Core.LaneUpkeep::Names
		static decltype(auto) S67() { return (LocalTraffic::BusDesign()); } // Unity.SpaceGod.Core.LocalTraffic::BusDesign
		static decltype(auto) S68() { return (LocalTraffic::RunaboutDesign()); } // Unity.SpaceGod.Core.LocalTraffic::RunaboutDesign
		static decltype(auto) S69() { return (LocalTraffic::SkiffDesign()); } // Unity.SpaceGod.Core.LocalTraffic::SkiffDesign
		static decltype(auto) S70() { return (LocalTraffic::TaxiDesign()); } // Unity.SpaceGod.Core.LocalTraffic::TaxiDesign
		static decltype(auto) S71() { return (LocalTraffic::TourerDesign()); } // Unity.SpaceGod.Core.LocalTraffic::TourerDesign
		static decltype(auto) S72() { return (MegastructureSpec::DysonRing()); } // Unity.SpaceGod.Core.MegastructureSpec::DysonRing
		static decltype(auto) S73() { return (MegastructureSpec::Shipyard()); } // Unity.SpaceGod.Core.MegastructureSpec::Shipyard
		static decltype(auto) S74() { return (MegastructureSpec::SpaceElevator()); } // Unity.SpaceGod.Core.MegastructureSpec::SpaceElevator
		static decltype(auto) S75() { return (MegastructureSpec::registered()); } // Unity.SpaceGod.Core.MegastructureSpec::registered
		static decltype(auto) S76() { return (MilitaryLogistics::CollierNames()); } // Unity.SpaceGod.Core.MilitaryLogistics::CollierNames
		static decltype(auto) S77() { return (MilitaryLogistics::TankerNames()); } // Unity.SpaceGod.Core.MilitaryLogistics::TankerNames
		static decltype(auto) S78() { return (Newswire::RelayTraffic()); } // Unity.SpaceGod.Core.Newswire::RelayTraffic
		static decltype(auto) S79() { return (Outlaws::HunterNames()); } // Unity.SpaceGod.Core.Outlaws::HunterNames
		static decltype(auto) S80() { return (PactKind::Alliance()); } // Unity.SpaceGod.Core.PactKind::Alliance
		static decltype(auto) S81() { return (PactKind::NonAggression()); } // Unity.SpaceGod.Core.PactKind::NonAggression
		static decltype(auto) S82() { return (PactKind::Trade()); } // Unity.SpaceGod.Core.PactKind::Trade
		static decltype(auto) S83() { return (PactKind::Tribute()); } // Unity.SpaceGod.Core.PactKind::Tribute
		static decltype(auto) S84() { return (PactKind::Vassalage()); } // Unity.SpaceGod.Core.PactKind::Vassalage
		static decltype(auto) S85() { return (PactKind::all()); } // Unity.SpaceGod.Core.PactKind::all
		static decltype(auto) S86() { return (PirateClans::BreakawayNames()); } // Unity.SpaceGod.Core.PirateClans::BreakawayNames
		static decltype(auto) S87() { return (PirateClans::RunnerNames()); } // Unity.SpaceGod.Core.PirateClans::RunnerNames
		static decltype(auto) S88() { return (ProductionFocus::Balanced()); } // Unity.SpaceGod.Core.ProductionFocus::Balanced
		static decltype(auto) S89() { return (ProductionFocus::Civil()); } // Unity.SpaceGod.Core.ProductionFocus::Civil
		static decltype(auto) S90() { return (ProductionFocus::Extraction()); } // Unity.SpaceGod.Core.ProductionFocus::Extraction
		static decltype(auto) S91() { return (ProductionFocus::War()); } // Unity.SpaceGod.Core.ProductionFocus::War
		static decltype(auto) S92() { return (ProductionFocus::all()); } // Unity.SpaceGod.Core.ProductionFocus::all
		static decltype(auto) S93() { return (Radiators::Panels()); } // Unity.SpaceGod.Core.Radiators::Panels
		static decltype(auto) S94() { return (Recipe::Fixed()); } // Unity.SpaceGod.Core.Recipe::Fixed
		static decltype(auto) S95() { return (ResearchTrack::Industry()); } // Unity.SpaceGod.Core.ResearchTrack::Industry
		static decltype(auto) S96() { return (ResearchTrack::Marines()); } // Unity.SpaceGod.Core.ResearchTrack::Marines
		static decltype(auto) S97() { return (ResearchTrack::Shipyards()); } // Unity.SpaceGod.Core.ResearchTrack::Shipyards
		static decltype(auto) S98() { return (ResearchTrack::all()); } // Unity.SpaceGod.Core.ResearchTrack::all
		static decltype(auto) S99() { return (ShipClassSpec::Specs()); } // Unity.SpaceGod.Core.ShipClassSpec::Specs
		static decltype(auto) S100() { return (SimClock::Levels()); } // Unity.SpaceGod.Core.SimClock::Levels
		static decltype(auto) S101() { return (SpinalMounts::Coilgun()); } // Unity.SpaceGod.Core.SpinalMounts::Coilgun
		static decltype(auto) S102() { return (SpinalMounts::Plasma()); } // Unity.SpaceGod.Core.SpinalMounts::Plasma
		static decltype(auto) S103() { return (SpinalMounts::Railgun()); } // Unity.SpaceGod.Core.SpinalMounts::Railgun
		static decltype(auto) S104() { return (TradeDesk::RankWorth()); } // Unity.SpaceGod.Core.TradeDesk::RankWorth
		static decltype(auto) S105() { return (War::HullNames()); } // Unity.SpaceGod.Core.War::HullNames
		static decltype(auto) S106() { return (War::RaiderNames()); } // Unity.SpaceGod.Core.War::RaiderNames
		static decltype(auto) S107() { return (WeaponSpec::AntimatterTorpedo()); } // Unity.SpaceGod.Core.WeaponSpec::AntimatterTorpedo
		static decltype(auto) S108() { return (WeaponSpec::AutoTurret()); } // Unity.SpaceGod.Core.WeaponSpec::AutoTurret
		static decltype(auto) S109() { return (WeaponSpec::Autocannon()); } // Unity.SpaceGod.Core.WeaponSpec::Autocannon
		static decltype(auto) S110() { return (WeaponSpec::DogfightMissile()); } // Unity.SpaceGod.Core.WeaponSpec::DogfightMissile
		static decltype(auto) S111() { return (WeaponSpec::Emp()); } // Unity.SpaceGod.Core.WeaponSpec::Emp
		static decltype(auto) S112() { return (WeaponSpec::FighterGuns()); } // Unity.SpaceGod.Core.WeaponSpec::FighterGuns
		static decltype(auto) S113() { return (WeaponSpec::Flak()); } // Unity.SpaceGod.Core.WeaponSpec::Flak
		static decltype(auto) S114() { return (WeaponSpec::Flares()); } // Unity.SpaceGod.Core.WeaponSpec::Flares
		static decltype(auto) S115() { return (WeaponSpec::Heavy()); } // Unity.SpaceGod.Core.WeaponSpec::Heavy
		static decltype(auto) S116() { return (WeaponSpec::HeavyTorpedo()); } // Unity.SpaceGod.Core.WeaponSpec::HeavyTorpedo
		static decltype(auto) S117() { return (WeaponSpec::Interceptor()); } // Unity.SpaceGod.Core.WeaponSpec::Interceptor
		static decltype(auto) S118() { return (WeaponSpec::Laser()); } // Unity.SpaceGod.Core.WeaponSpec::Laser
		static decltype(auto) S119() { return (WeaponSpec::LightRailgun()); } // Unity.SpaceGod.Core.WeaponSpec::LightRailgun
		static decltype(auto) S120() { return (WeaponSpec::Mines()); } // Unity.SpaceGod.Core.WeaponSpec::Mines
		static decltype(auto) S121() { return (WeaponSpec::Missiles()); } // Unity.SpaceGod.Core.WeaponSpec::Missiles
		static decltype(auto) S122() { return (WeaponSpec::Pods()); } // Unity.SpaceGod.Core.WeaponSpec::Pods
		static decltype(auto) S123() { return (WeaponSpec::PointDefense()); } // Unity.SpaceGod.Core.WeaponSpec::PointDefense
		static decltype(auto) S124() { return (WeaponSpec::Railgun()); } // Unity.SpaceGod.Core.WeaponSpec::Railgun
		static decltype(auto) S125() { return (WeaponSpec::TachyonLance()); } // Unity.SpaceGod.Core.WeaponSpec::TachyonLance
		static decltype(auto) S126() { return (WeaponSpec::Torpedo()); } // Unity.SpaceGod.Core.WeaponSpec::Torpedo
		static decltype(auto) S127() { return (World::BeltHavens()); } // Unity.SpaceGod.Core.World::BeltHavens
		static decltype(auto) S128() { return (World::CaptureTugNames()); } // Unity.SpaceGod.Core.World::CaptureTugNames
		static decltype(auto) S129() { return (World::CourierNames()); } // Unity.SpaceGod.Core.World::CourierNames
		static decltype(auto) S130() { return (World::FreighterNames()); } // Unity.SpaceGod.Core.World::FreighterNames
		static decltype(auto) S131() { return (World::MinerNames()); } // Unity.SpaceGod.Core.World::MinerNames
		static decltype(auto) S132() { return (World::TrojanHavens()); } // Unity.SpaceGod.Core.World::TrojanHavens
		static decltype(auto) S133() { return (World::TugNames()); } // Unity.SpaceGod.Core.World::TugNames
		static decltype(auto) S134() { return (World::WorksEscortClasses()); } // Unity.SpaceGod.Core.World::WorksEscortClasses
		static decltype(auto) S135() { return (World::WorksFreighterNames()); } // Unity.SpaceGod.Core.World::WorksFreighterNames
	};

	SG_STATIC_OF("Unity.SpaceGod.Core.AlphaCentauri", "Orbit", Graph::Access<0>::S0())
	SG_STATIC_OF("Unity.SpaceGod.Core.AlphaStrike", "AntiShipMissile", Graph::Access<0>::S1())
	SG_STATIC_OF("Unity.SpaceGod.Core.AlphaStrike", "MagazineTorpedo", Graph::Access<0>::S2())
	SG_STATIC_OF("Unity.SpaceGod.Core.AsteroidClass", "Carbonaceous", Graph::Access<0>::S3())
	SG_STATIC_OF("Unity.SpaceGod.Core.AsteroidClass", "CometIce", Graph::Access<0>::S4())
	SG_STATIC_OF("Unity.SpaceGod.Core.AsteroidClass", "Metallic", Graph::Access<0>::S5())
	SG_STATIC_OF("Unity.SpaceGod.Core.AsteroidClass", "Stony", Graph::Access<0>::S6())
	SG_STATIC_OF("Unity.SpaceGod.Core.AsteroidClass", "registered", Graph::Access<0>::S7())
	SG_STATIC_OF("Unity.SpaceGod.Core.BuildPlan", "Balanced", Graph::Access<0>::S8())
	SG_STATIC_OF("Unity.SpaceGod.Core.BuildPlan", "BattleLine", Graph::Access<0>::S9())
	SG_STATIC_OF("Unity.SpaceGod.Core.BuildPlan", "Carriers", Graph::Access<0>::S10())
	SG_STATIC_OF("Unity.SpaceGod.Core.BuildPlan", "LaneSecurity", Graph::Access<0>::S11())
	SG_STATIC_OF("Unity.SpaceGod.Core.BuildPlan", "Merchant", Graph::Access<0>::S12())
	SG_STATIC_OF("Unity.SpaceGod.Core.BuildPlan", "Mobilise", Graph::Access<0>::S13())
	SG_STATIC_OF("Unity.SpaceGod.Core.BuildPlan", "Screens", Graph::Access<0>::S14())
	SG_STATIC_OF("Unity.SpaceGod.Core.BuildPlan", "Strike", Graph::Access<0>::S15())
	SG_STATIC_OF("Unity.SpaceGod.Core.BuildPlan", "all", Graph::Access<0>::S16())
	SG_STATIC_OF("Unity.SpaceGod.Core.Charters", "Classes", Graph::Access<0>::S17())
	SG_STATIC_OF("Unity.SpaceGod.Core.Colonisation", "ShipNames", Graph::Access<0>::S18())
	SG_STATIC_OF("Unity.SpaceGod.Core.CombatSpec", "Platform", Graph::Access<0>::S19())
	SG_STATIC_OF("Unity.SpaceGod.Core.CombatSpec", "Specs", Graph::Access<0>::S20())
	SG_STATIC_OF("Unity.SpaceGod.Core.Companies", "Houses", Graph::Access<0>::S21())
	SG_STATIC_OF("Unity.SpaceGod.Core.CruiseDrives", "Capital", Graph::Access<0>::S22())
	SG_STATIC_OF("Unity.SpaceGod.Core.CruiseDrives", "Survey", Graph::Access<0>::S23())
	SG_STATIC_OF("Unity.SpaceGod.Core.CruiseDrives", "byClass", Graph::Access<0>::S24())
	SG_STATIC_OF("Unity.SpaceGod.Core.Deal", "Alliance", Graph::Access<0>::S25())
	SG_STATIC_OF("Unity.SpaceGod.Core.Deal", "DemandFealty", Graph::Access<0>::S26())
	SG_STATIC_OF("Unity.SpaceGod.Core.Deal", "DemandTribute", Graph::Access<0>::S27())
	SG_STATIC_OF("Unity.SpaceGod.Core.Deal", "NonAggression", Graph::Access<0>::S28())
	SG_STATIC_OF("Unity.SpaceGod.Core.Deal", "OfferFealty", Graph::Access<0>::S29())
	SG_STATIC_OF("Unity.SpaceGod.Core.Deal", "OfferTribute", Graph::Access<0>::S30())
	SG_STATIC_OF("Unity.SpaceGod.Core.Deal", "Peace", Graph::Access<0>::S31())
	SG_STATIC_OF("Unity.SpaceGod.Core.Deal", "TradePact", Graph::Access<0>::S32())
	SG_STATIC_OF("Unity.SpaceGod.Core.Deal", "all", Graph::Access<0>::S33())
	SG_STATIC_OF("Unity.SpaceGod.Core.DeepSpace", "FieldSources", Graph::Access<0>::S34())
	SG_STATIC_OF("Unity.SpaceGod.Core.EnergyWeapons", "PhaserLance", Graph::Access<0>::S35())
	SG_STATIC_OF("Unity.SpaceGod.Core.EnergyWeapons", "PhaserSweep", Graph::Access<0>::S36())
	SG_STATIC_OF("Unity.SpaceGod.Core.EnergyWeapons", "PlasmaCannon", Graph::Access<0>::S37())
	SG_STATIC_OF("Unity.SpaceGod.Core.EventDirector", "Anomalies", Graph::Access<0>::S38())
	SG_STATIC_OF("Unity.SpaceGod.Core.FacilityKinds", "All", Graph::Access<0>::S39())
	SG_STATIC_OF("Unity.SpaceGod.Core.FacilityKinds", "ById", Graph::Access<0>::S40())
	SG_STATIC_OF("Unity.SpaceGod.Core.FacilityLayouts", "ByClass", Graph::Access<0>::S41())
	SG_STATIC_OF("Unity.SpaceGod.Core.FacilityLayouts", "Platform", Graph::Access<0>::S42())
	SG_STATIC_OF("Unity.SpaceGod.Core.FacilityLayouts", "Satellite", Graph::Access<0>::S43())
	SG_STATIC_OF("Unity.SpaceGod.Core.FacilityLayouts", "Small", Graph::Access<0>::S44())
	SG_STATIC_OF("Unity.SpaceGod.Core.FacilityLayouts", "Station", Graph::Access<0>::S45())
	SG_STATIC_OF("Unity.SpaceGod.Core.FighterAI", "BomberSpec", Graph::Access<0>::S46())
	SG_STATIC_OF("Unity.SpaceGod.Core.FighterAI", "Interceptor", Graph::Access<0>::S47())
	SG_STATIC_OF("Unity.SpaceGod.Core.FighterCraft", "AssaultShuttle", Graph::Access<0>::S48())
	SG_STATIC_OF("Unity.SpaceGod.Core.FighterCraft", "Bomber", Graph::Access<0>::S49())
	SG_STATIC_OF("Unity.SpaceGod.Core.FighterCraft", "HeavyFighter", Graph::Access<0>::S50())
	SG_STATIC_OF("Unity.SpaceGod.Core.FighterCraft", "Interceptor", Graph::Access<0>::S51())
	SG_STATIC_OF("Unity.SpaceGod.Core.FighterCraft", "Recon", Graph::Access<0>::S52())
	SG_STATIC_OF("Unity.SpaceGod.Core.FighterCraft", "SpaceSuperiority", Graph::Access<0>::S53())
	SG_STATIC_OF("Unity.SpaceGod.Core.FighterCraft", "SquadronMix", Graph::Access<0>::S54())
	SG_STATIC_OF("Unity.SpaceGod.Core.Fleet", "CoarseTimeFactors", Graph::Access<0>::S55())
	SG_STATIC_OF("Unity.SpaceGod.Core.Fleet", "EscortSlots", Graph::Access<0>::S56())
	SG_STATIC_OF("Unity.SpaceGod.Core.Fleet", "TransferTimeFactors", Graph::Access<0>::S57())
	SG_STATIC_OF("Unity.SpaceGod.Core.FlightDecks", "None", Graph::Access<0>::S58())
	SG_STATIC_OF("Unity.SpaceGod.Core.GateConstruction", "Materials", Graph::Access<0>::S59())
	SG_STATIC_OF("Unity.SpaceGod.Core.Holdings", "CaptainNames", Graph::Access<0>::S60())
	SG_STATIC_OF("Unity.SpaceGod.Core.HullFrames", "All", Graph::Access<0>::S61())
	SG_STATIC_OF("Unity.SpaceGod.Core.HullFrames", "ById", Graph::Access<0>::S62())
	SG_STATIC_OF("Unity.SpaceGod.Core.HullFrames", "OrbitalStation", Graph::Access<0>::S63())
	SG_STATIC_OF("Unity.SpaceGod.Core.HullFrames", "SensorSatellite", Graph::Access<0>::S64())
	SG_STATIC_OF("Unity.SpaceGod.Core.HullFrames", "WeaponsSatellite", Graph::Access<0>::S65())
	SG_STATIC_OF("Unity.SpaceGod.Core.LaneUpkeep", "Names", Graph::Access<0>::S66())
	SG_STATIC_OF("Unity.SpaceGod.Core.LocalTraffic", "BusDesign", Graph::Access<0>::S67())
	SG_STATIC_OF("Unity.SpaceGod.Core.LocalTraffic", "RunaboutDesign", Graph::Access<0>::S68())
	SG_STATIC_OF("Unity.SpaceGod.Core.LocalTraffic", "SkiffDesign", Graph::Access<0>::S69())
	SG_STATIC_OF("Unity.SpaceGod.Core.LocalTraffic", "TaxiDesign", Graph::Access<0>::S70())
	SG_STATIC_OF("Unity.SpaceGod.Core.LocalTraffic", "TourerDesign", Graph::Access<0>::S71())
	SG_STATIC_OF("Unity.SpaceGod.Core.MegastructureSpec", "DysonRing", Graph::Access<0>::S72())
	SG_STATIC_OF("Unity.SpaceGod.Core.MegastructureSpec", "Shipyard", Graph::Access<0>::S73())
	SG_STATIC_OF("Unity.SpaceGod.Core.MegastructureSpec", "SpaceElevator", Graph::Access<0>::S74())
	SG_STATIC_OF("Unity.SpaceGod.Core.MegastructureSpec", "registered", Graph::Access<0>::S75())
	SG_STATIC_OF("Unity.SpaceGod.Core.MilitaryLogistics", "CollierNames", Graph::Access<0>::S76())
	SG_STATIC_OF("Unity.SpaceGod.Core.MilitaryLogistics", "TankerNames", Graph::Access<0>::S77())
	SG_STATIC_OF("Unity.SpaceGod.Core.Newswire", "RelayTraffic", Graph::Access<0>::S78())
	SG_STATIC_OF("Unity.SpaceGod.Core.Outlaws", "HunterNames", Graph::Access<0>::S79())
	SG_STATIC_OF("Unity.SpaceGod.Core.PactKind", "Alliance", Graph::Access<0>::S80())
	SG_STATIC_OF("Unity.SpaceGod.Core.PactKind", "NonAggression", Graph::Access<0>::S81())
	SG_STATIC_OF("Unity.SpaceGod.Core.PactKind", "Trade", Graph::Access<0>::S82())
	SG_STATIC_OF("Unity.SpaceGod.Core.PactKind", "Tribute", Graph::Access<0>::S83())
	SG_STATIC_OF("Unity.SpaceGod.Core.PactKind", "Vassalage", Graph::Access<0>::S84())
	SG_STATIC_OF("Unity.SpaceGod.Core.PactKind", "all", Graph::Access<0>::S85())
	SG_STATIC_OF("Unity.SpaceGod.Core.PirateClans", "BreakawayNames", Graph::Access<0>::S86())
	SG_STATIC_OF("Unity.SpaceGod.Core.PirateClans", "RunnerNames", Graph::Access<0>::S87())
	SG_STATIC_OF("Unity.SpaceGod.Core.ProductionFocus", "Balanced", Graph::Access<0>::S88())
	SG_STATIC_OF("Unity.SpaceGod.Core.ProductionFocus", "Civil", Graph::Access<0>::S89())
	SG_STATIC_OF("Unity.SpaceGod.Core.ProductionFocus", "Extraction", Graph::Access<0>::S90())
	SG_STATIC_OF("Unity.SpaceGod.Core.ProductionFocus", "War", Graph::Access<0>::S91())
	SG_STATIC_OF("Unity.SpaceGod.Core.ProductionFocus", "all", Graph::Access<0>::S92())
	SG_STATIC_OF("Unity.SpaceGod.Core.Radiators", "Panels", Graph::Access<0>::S93())
	SG_STATIC_OF("Unity.SpaceGod.Core.Recipe", "Fixed", Graph::Access<0>::S94())
	SG_STATIC_OF("Unity.SpaceGod.Core.ResearchTrack", "Industry", Graph::Access<0>::S95())
	SG_STATIC_OF("Unity.SpaceGod.Core.ResearchTrack", "Marines", Graph::Access<0>::S96())
	SG_STATIC_OF("Unity.SpaceGod.Core.ResearchTrack", "Shipyards", Graph::Access<0>::S97())
	SG_STATIC_OF("Unity.SpaceGod.Core.ResearchTrack", "all", Graph::Access<0>::S98())
	SG_STATIC_OF("Unity.SpaceGod.Core.ShipClassSpec", "Specs", Graph::Access<0>::S99())
	SG_STATIC_OF("Unity.SpaceGod.Core.SimClock", "Levels", Graph::Access<0>::S100())
	SG_STATIC_OF("Unity.SpaceGod.Core.SpinalMounts", "Coilgun", Graph::Access<0>::S101())
	SG_STATIC_OF("Unity.SpaceGod.Core.SpinalMounts", "Plasma", Graph::Access<0>::S102())
	SG_STATIC_OF("Unity.SpaceGod.Core.SpinalMounts", "Railgun", Graph::Access<0>::S103())
	SG_STATIC_OF("Unity.SpaceGod.Core.TradeDesk", "RankWorth", Graph::Access<0>::S104())
	SG_STATIC_OF("Unity.SpaceGod.Core.War", "HullNames", Graph::Access<0>::S105())
	SG_STATIC_OF("Unity.SpaceGod.Core.War", "RaiderNames", Graph::Access<0>::S106())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "AntimatterTorpedo", Graph::Access<0>::S107())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "AutoTurret", Graph::Access<0>::S108())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "Autocannon", Graph::Access<0>::S109())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "DogfightMissile", Graph::Access<0>::S110())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "Emp", Graph::Access<0>::S111())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "FighterGuns", Graph::Access<0>::S112())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "Flak", Graph::Access<0>::S113())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "Flares", Graph::Access<0>::S114())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "Heavy", Graph::Access<0>::S115())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "HeavyTorpedo", Graph::Access<0>::S116())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "Interceptor", Graph::Access<0>::S117())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "Laser", Graph::Access<0>::S118())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "LightRailgun", Graph::Access<0>::S119())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "Mines", Graph::Access<0>::S120())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "Missiles", Graph::Access<0>::S121())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "Pods", Graph::Access<0>::S122())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "PointDefense", Graph::Access<0>::S123())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "Railgun", Graph::Access<0>::S124())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "TachyonLance", Graph::Access<0>::S125())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponSpec", "Torpedo", Graph::Access<0>::S126())
	SG_STATIC_OF("Unity.SpaceGod.Core.World", "BeltHavens", Graph::Access<0>::S127())
	SG_STATIC_OF("Unity.SpaceGod.Core.World", "CaptureTugNames", Graph::Access<0>::S128())
	SG_STATIC_OF("Unity.SpaceGod.Core.World", "CourierNames", Graph::Access<0>::S129())
	SG_STATIC_OF("Unity.SpaceGod.Core.World", "FreighterNames", Graph::Access<0>::S130())
	SG_STATIC_OF("Unity.SpaceGod.Core.World", "MinerNames", Graph::Access<0>::S131())
	SG_STATIC_OF("Unity.SpaceGod.Core.World", "TrojanHavens", Graph::Access<0>::S132())
	SG_STATIC_OF("Unity.SpaceGod.Core.World", "TugNames", Graph::Access<0>::S133())
	SG_STATIC_OF("Unity.SpaceGod.Core.World", "WorksEscortClasses", Graph::Access<0>::S134())
	SG_STATIC_OF("Unity.SpaceGod.Core.World", "WorksFreighterNames", Graph::Access<0>::S135())
}
