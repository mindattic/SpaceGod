// Not a C# file (US-T36): the ObjectGraph registrations (docs/PERSISTENCE.md) of the Combat/ types a
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
	SG_ENUM(BatteryOrder, "Unity.SpaceGod.Core.BatteryOrder")

	SG_ENUM(BoardingOrder, "Unity.SpaceGod.Core.BoardingOrder")

	SG_ENUM(CompartmentKind, "Unity.SpaceGod.Core.CompartmentKind")

	SG_ENUM(DamageControlFocus, "Unity.SpaceGod.Core.DamageControlFocus")

	SG_ENUM(DeckStage, "Unity.SpaceGod.Core.DeckStage")

	SG_ENUM(DeployKind, "Unity.SpaceGod.Core.DeployKind")

	SG_ENUM(DogfightManeuver, "Unity.SpaceGod.Core.DogfightManeuver")

	SG_ENUM(EngineTelegraph, "Unity.SpaceGod.Core.EngineTelegraph")

	SG_ENUM(FighterPhase, "Unity.SpaceGod.Core.FighterPhase")

	SG_ENUM(FighterRole, "Unity.SpaceGod.Core.FighterRole")

	SG_ENUM(FleetFormation, "Unity.SpaceGod.Core.FleetFormation")

	SG_ENUM(FxKind, "Unity.SpaceGod.Core.FxKind")

	SG_ENUM(HardpointSize, "Unity.SpaceGod.Core.HardpointSize")

	SG_ENUM(HullCategory, "Unity.SpaceGod.Core.HullCategory")

	SG_ENUM(HullModuleKind, "Unity.SpaceGod.Core.HullModuleKind")

	SG_ENUM(MissileKind, "Unity.SpaceGod.Core.MissileKind")

	SG_ENUM(ModuleKind, "Unity.SpaceGod.Core.ModuleKind")

	SG_ENUM(MountType, "Unity.SpaceGod.Core.MountType")

	SG_ENUM(OrderKind, "Unity.SpaceGod.Core.OrderKind")

	SG_ENUM(PhaserTuning, "Unity.SpaceGod.Core.PhaserTuning")

	SG_ENUM(PodState, "Unity.SpaceGod.Core.PodState")

	SG_ENUM(Relation, "Unity.SpaceGod.Core.Relation")

	SG_ENUM(StrikePhase, "Unity.SpaceGod.Core.StrikePhase")

	SG_ENUM(SubsystemKind, "Unity.SpaceGod.Core.SubsystemKind")

	SG_ENUM(SurrenderTerms, "Unity.SpaceGod.Core.SurrenderTerms")

	SG_ENUM(UnitKind, "Unity.SpaceGod.Core.UnitKind")

	SG_ENUM(WeaponGroup, "Unity.SpaceGod.Core.WeaponGroup")

	SG_ENUM(WingOrder, "Unity.SpaceGod.Core.WingOrder")

	SG_STRUCT(BoardingNote, "Unity.SpaceGod.Core.BoardingNote",
		SG_F(Compartment), SG_F(Seconds), SG_F(Text))

	SG_STRUCT(CombatFx, "Unity.SpaceGod.Core.CombatFx",
		SG_F(A), SG_F(B), SG_F(Kind), SG_F(Seq), SG_F(Side), SG_F(Size), SG_F(Sub), SG_F(Unit))

	SG_STRUCT(PilotCruiseState, "Unity.SpaceGod.Core.PilotCruiseState",
		SG_F(RampT), SG_F(hasLastHull), SG_F(lastHull))

	SG_STRUCT(PilotState, "Unity.SpaceGod.Core.PilotState",
		SG_F(AngularVelocity), SG_F(Cruise), SG_F(Forward), SG_F(Pos), SG_F(Telegraph), SG_F(Up), SG_F(Vel))

	SG_STRUCT(SalvageStack, "Unity.SpaceGod.Core.SalvageStack",
		SG_F(Good), SG_F(Tonnes))

	SG_SERIALIZE(Battle, "Unity.SpaceGod.Core.Battle",
		SG_P(FxSeq), SG_P(Mopping), SG_P(Over), SG_P(Winner), SG_F(AggressorSide), SG_F(Anchor), SG_F(ApproachAxis),
		SG_F(Boards), SG_F(Body), SG_F(CargoDemanded), SG_F(Decoys), SG_ARRAY(Dread), SG_F(Embedded),
		SG_F(EnforcingSide), SG_F(FighterStep), SG_F(Flown), SG_F(Fresh), SG_F(Fx), SG_F(Id), SG_F(Launch),
		SG_F(Manned), SG_F(Mines), SG_F(Missiles), SG_F(Name), SG_F(ReconAloft), SG_ARRAY(Robbed), SG_F(Rounds),
		SG_F(Seconds), SG_ARRAY(SideLeaders), SG_F(Site), SG_F(StartJd), SG_F(SurrenderDecided), SG_F(UnitCaptured),
		SG_F(UnitDestroyed), SG_F(UnitSurrendered), SG_F(Units), SG_F(WantsAlive), SG_F(accumulator),
		SG_F(burnedOut), SG_F(nextUnitId), SG_F(rng), SG_ARRAY(sideStrength))

	SG_SERIALIZE(BattleUnit, "Unity.SpaceGod.Core.BattleUnit",
		SG_P(PlayerFlown), SG_F(BoardTarget), SG_F(BreacherBayReload), SG_F(BreakSide), SG_F(Broadside),
		SG_F(Captured), SG_F(CargoDemandMade), SG_F(CargoDemandTimer), SG_F(CargoDemandedBy), SG_F(CargoRefused),
		SG_F(Circle), SG_F(Combat), SG_F(CommandedBy), SG_F(CraftHull), SG_F(Dead), SG_F(Disrupted), SG_F(DockSide),
		SG_F(Docked), SG_F(Faction), SG_F(Fighter), SG_F(Fled), SG_F(Fleeing), SG_F(Fort), SG_F(Forward), SG_F(Heat),
		SG_F(HullHeat), SG_F(Id), SG_F(Kind), SG_F(LatchLocal), SG_F(LatchTimer), SG_F(Latched), SG_F(Leader),
		SG_F(LengthKm), SG_F(LockProgress), SG_F(Maneuver), SG_F(ManeuverLeft), SG_F(MoraleThreshold),
		SG_F(Overheated), SG_F(Platform), SG_F(PlayerAim), SG_F(PlayerDecoyRequested), SG_F(PlayerEmpHeld),
		SG_F(PlayerFireHeld), SG_F(PlayerHelm), SG_F(PlayerLockTarget), SG_F(PlayerMineRequested),
		SG_F(PlayerSecondaryHeld), SG_F(PlayerSubTarget), SG_F(PlayerVolleyAim), SG_F(PlayerVolleyRelease),
		SG_F(Pos), SG_F(Power), SG_F(RetargetIn), SG_F(Roof), SG_F(SeenVel), SG_F(Ship), SG_F(ShuttleLaunchIn),
		SG_F(Side), SG_F(Slot), SG_F(Strike), SG_F(Stunned), SG_F(SurrenderOpenToBoarding), SG_F(SurrenderOutcome),
		SG_F(SurrenderTimer), SG_F(Surrendering), SG_F(TakenFromSide), SG_F(Target), SG_F(TurnSpin), SG_F(Up),
		SG_F(Vel), SG_F(VolleyCharge), SG_F(Wander), SG_F(dockSideFor), SG_F(playerControlled))

	SG_SERIALIZE(BoardingParty, "Unity.SpaceGod.Core.BoardingParty",
		SG_F(Faction), SG_F(Landed), SG_F(Order), SG_F(Strength))

	SG_SERIALIZE(BroadsideCommand, "Unity.SpaceGod.Core.BroadsideCommand",
		SG_F(BraceCooldown), SG_F(BraceLeft), SG_F(Focus), SG_F(Formation), SG_F(ReadyingLoaded), SG_F(ReadyingOn),
		SG_F(Releasing), SG_F(Repaired), SG_F(Restored), SG_F(RollBearing), SG_F(RollChosen), SG_F(RollDeg),
		SG_F(RollReconsider), SG_F(RollWeakPatches), SG_F(RunHot), SG_F(SpreadFire), SG_ARRAY(orders))

	SG_SERIALIZE(Captives, "Unity.SpaceGod.Core.Captives",
		SG_F(Buyer), SG_F(Captors), SG_F(From), SG_F(Haven), SG_F(Marines), SG_F(Owners), SG_F(People),
		SG_F(Prisoners), SG_F(RansomJd), SG_F(SinceJd))

	SG_SERIALIZE(CombatSpec, "Unity.SpaceGod.Core.CombatSpec",
		SG_F(Accel), SG_F(Armor), SG_F(BeamFraction), SG_F(BoostDurationS), SG_F(BoostSpeedMultiplier), SG_F(Crew),
		SG_F(Decel), SG_F(DesignName), SG_F(DryMassTonnes), SG_F(ExhaustVelocityKmS), SG_F(Hull),
		SG_F(LateralThrustMN), SG_ARRAY(Layout), SG_F(LengthKm), SG_F(MainThrustMN), SG_F(Marines), SG_F(MassKt),
		SG_F(MaxSpeed), SG_F(PitchYawTorqueMNKm), SG_F(PropellantTonnes), SG_F(ReconCraft), SG_F(RetroThrustMN),
		SG_F(RollTorqueMNKm), SG_F(Shield), SG_F(ShieldRegen), SG_F(Shuttles), SG_F(SpinalPlants), SG_F(Squadrons),
		SG_F(TurnRateDegS), SG_F(VerticalThrustMN))

	SG_SERIALIZE(Compartment, "Unity.SpaceGod.Core.Compartment",
		SG_F(Boarders), SG_F(Buried), SG_F(Crew), SG_F(Index), SG_F(Kind), SG_F(Links), SG_F(Marines), SG_F(Name),
		SG_F(SealIn), SG_F(Survivors), SG_F(Taken), SG_F(Vented), SG_F(Y), SG_F(Z))

	SG_SERIALIZE(CraftPilot, "Unity.SpaceGod.Core.CraftPilot",
		SG_F(Discipline), SG_F(Reaction), SG_F(Seed), SG_F(Skill), SG_F(Sorties))

	SG_SERIALIZE(Crews, "Unity.SpaceGod.Core.Crews",
		SG_F(CaptivesReleased), SG_F(HavenOf), SG_F(Held), SG_F(Launched), SG_F(Pods), SG_F(RaidersTaken),
		SG_F(Resolved), SG_F(nextId), SG_F(rng))

	SG_SERIALIZE(DeckVisit, "Unity.SpaceGod.Core.DeckVisit",
		SG_F(After), SG_F(Along), SG_F(Deck), SG_F(Offset), SG_F(Relaunch), SG_F(Service), SG_F(Speed),
		SG_F(SpotAlong), SG_F(Stage), SG_F(Stowing))

	SG_SERIALIZE(Decoy, "Unity.SpaceGod.Core.Decoy",
		SG_F(Gone), SG_F(Life), SG_F(Pos), SG_F(Side), SG_F(Vel))

	SG_SERIALIZE(Diplomacy, "Unity.SpaceGod.Core.Diplomacy",
		SG_F(blockades), SG_F(embargoes), SG_F(relations), SG_F(treaties))

	SG_SERIALIZE(Embargo, "Unity.SpaceGod.Core.Embargo",
		SG_F(Against), SG_F(By), SG_F(Reason), SG_F(SinceJd), SG_F(UntilJd))

	SG_SERIALIZE(Embedded, "Unity.SpaceGod.Core.Embedded",
		SG_F(Breacher), SG_F(Inward), SG_F(Local), SG_F(Side), SG_F(Unit))

	SG_SERIALIZE(EscapePod, "Unity.SpaceGod.Core.EscapePod",
		SG_F(Anchor), SG_F(Body), SG_F(Breacher), SG_F(Captain), SG_F(DriftDir), SG_F(Enemy), SG_F(Haven), SG_F(Id),
		SG_F(LaunchedJd), SG_F(LifeSupportDays), SG_F(Local), SG_F(MergeKey), SG_F(Name), SG_F(Owners), SG_F(People),
		SG_F(Pods), SG_F(ResolvedJd), SG_F(Site), SG_F(State), SG_F(TakenBy))

	SG_SERIALIZE(FacilitySite, "Unity.SpaceGod.Core.FacilitySite",
		SG_P(Buried), SG_F(Berths), SG_F(Kind), SG_F(Link), SG_F(Name), SG_F(Y), SG_F(Z))

	SG_SERIALIZE(FighterControl, "Unity.SpaceGod.Core.FighterControl",
		SG_F(Aim), SG_F(AngularVelocity), SG_F(Boost), SG_F(FlightAssistOff), SG_F(Roll), SG_F(Secondary),
		SG_F(SecondaryGroup), SG_F(StrafeRight), SG_F(StrafeUp), SG_F(Throttle), SG_F(Up))

	SG_SERIALIZE(FighterState, "Unity.SpaceGod.Core.FighterState",
		SG_F(Bomber), SG_F(BreakPoint), SG_F(Carrier), SG_F(Deck), SG_F(Discipline), SG_F(Drill), SG_F(HavenCheckIn),
		SG_F(Home), SG_F(Lead), SG_F(Order), SG_F(Phase), SG_F(PhaseTime), SG_F(Reaction), SG_F(Role),
		SG_F(RunClosest), SG_F(RunEntry), SG_F(Seed), SG_F(Skill), SG_F(Slot), SG_F(Squadron), SG_F(Stranded))

	SG_SERIALIZE(FlightDeck, "Unity.SpaceGod.Core.FlightDeck",
		SG_F(Aboard), SG_F(Bay), SG_F(BowAlong), SG_F(Carrier), SG_F(Final), SG_F(MiddleLocal), SG_F(Name),
		SG_F(NextLaunch), SG_F(SternAlong))

	SG_SERIALIZE(GroupOrder, "Unity.SpaceGod.Core.GroupOrder",
		SG_F(Body), SG_F(Kind), SG_F(SinceJd))

	SG_SERIALIZE(HullFragment, "Unity.SpaceGod.Core.HullFragment",
		SG_F(Centroid), SG_F(Hardpoints), SG_F(Hulk), SG_F(MassShare), SG_F(Modules), SG_F(Shattered))

	SG_SERIALIZE(HullJoint, "Unity.SpaceGod.Core.HullJoint",
		SG_F(A), SG_F(At), SG_F(B), SG_F(MaxStrength), SG_F(Strength))

	SG_SERIALIZE(HullModule, "Unity.SpaceGod.Core.HullModule",
		SG_F(Centre), SG_F(Detached), SG_F(HalfExtent), SG_F(Hardpoints), SG_F(Id), SG_F(Kind), SG_F(MassShare),
		SG_F(MaxStructure), SG_F(Section), SG_F(Side), SG_F(Structure))

	SG_SERIALIZE(Interior, "Unity.SpaceGod.Core.Interior",
		SG_F(Boarder), SG_F(BoarderObjective), SG_F(BoarderOrder), SG_F(BoarderQuality), SG_F(BoarderSide),
		SG_F(BoardersLanded), SG_F(BoardersLost), SG_F(Captured), SG_F(Compartments), SG_F(CrewLost),
		SG_F(DefenderObjective), SG_F(DefenderOrder), SG_F(DefenderQuality), SG_F(Log), SG_F(MarinesLost),
		SG_F(Parties), SG_F(Training), SG_F(reactIn))

	SG_SERIALIZE(KineticRound, "Unity.SpaceGod.Core.KineticRound",
		SG_F(Aimed), SG_F(ArriveIn), SG_F(Damage), SG_F(Gone), SG_F(Hit), SG_F(Life), SG_F(Pos), SG_F(Railgun),
		SG_F(Side), SG_F(SmallCraft), SG_F(Target), SG_F(TargetSub), SG_F(Vel))

	SG_SERIALIZE(LaneSite, "Unity.SpaceGod.Core.LaneSite",
		SG_F(FromOffset), SG_F(Hop), SG_F(Progress), SG_F(Side), SG_F(ToOffset))

	SG_SERIALIZE(Loadout, "Unity.SpaceGod.Core.Loadout",
		SG_F(Base), SG_F(BaseCapacityTonnes), SG_F(Category), SG_F(Class), SG_F(Facilities), SG_ARRAY(Fitted),
		SG_F(InternalSpace), SG_F(Slots), SG_F(SmallCraft),
		b.Typed("StockFacilities", &Self::StockFacilities, +[]() -> const ::SpaceGod::Core::Graph::NetType* { using namespace ::SpaceGod::Core::Registrations; using namespace ::SpaceGod::Core::Graph; return IReadOnlyListOf(TypeOf<cs::Ref<FacilitySite>>()); }))

	SG_SERIALIZE(Mine, "Unity.SpaceGod.Core.Mine",
		SG_F(ArmIn), SG_F(BlastKm), SG_F(Damage), SG_F(Gone), SG_F(Life), SG_F(Pos), SG_F(Side), SG_F(Vel))

	SG_SERIALIZE(Missile, "Unity.SpaceGod.Core.Missile",
		SG_F(Adrift), SG_F(Damage), SG_F(Decoy), SG_F(EmpRadiusKm), SG_F(EmpSeconds), SG_F(Gone), SG_F(Hp),
		SG_F(Kind), SG_F(Life), SG_F(Marines), SG_F(Owner), SG_F(Pos), SG_F(Prey), SG_F(Side), SG_F(SmallCraft),
		SG_F(Source), SG_F(Speed), SG_F(Target), SG_F(TargetSub), SG_F(Torpedo), SG_F(Vel))

	SG_SERIALIZE(ModularHull, "Unity.SpaceGod.Core.ModularHull",
		SG_F(Joints), SG_F(Modules), SG_F(Root))

	SG_SERIALIZE(OutfitSlot, "Unity.SpaceGod.Core.OutfitSlot",
		SG_F(BearsOnBow), SG_F(LayoutIndex), SG_F(Size), SG_F(Spinal), SG_F(Stock), SG_F(Type))

	SG_SERIALIZE(PicketWarning, "Unity.SpaceGod.Core.PicketWarning",
		SG_F(By), SG_F(Gate), SG_F(Hulls), SG_F(Intruder), SG_F(Jd), SG_F(Owner), SG_F(Picket), SG_F(SelfJump))

	SG_SERIALIZE(PowerDistribution, "Unity.SpaceGod.Core.PowerDistribution",
		SG_ARRAY(pips))

	SG_SERIALIZE(Pursuer, "Unity.SpaceGod.Core.Pursuer",
		SG_F(EmpCharge), SG_F(EmpReadyIn), SG_F(Leader), SG_F(Pos), SG_F(Seconds), SG_F(Ships), SG_F(Vel))

	SG_SERIALIZE(Pursuit, "Unity.SpaceGod.Core.Pursuit",
		SG_F(Body), SG_F(Groups), SG_F(lastChased), SG_F(rng), SG_F(world))

	SG_SERIALIZE(ShipCombat, "Unity.SpaceGod.Core.ShipCombat",
		SG_F(BoostFraction), SG_F(Hangar), SG_F(Hull), SG_F(Interior), SG_F(PhaserTuning), SG_F(Readiness),
		SG_F(ShuttleBay), SG_F(Spec), SG_F(StationHeld), SG_F(Subsystems), SG_F(structure))

	SG_SERIALIZE(Subsystem, "Unity.SpaceGod.Core.Subsystem",
		SG_F(Aim), SG_F(Ammo), SG_F(Cooldown), SG_F(Def), SG_F(Hp), SG_F(Index), SG_F(LastHitSeconds),
		SG_F(MaxShield), SG_F(Restoring), SG_F(Shield))

	SG_SERIALIZE(SubsystemDef, "Unity.SpaceGod.Core.SubsystemDef",
		SG_F(Buried), SG_F(ConeAxis), SG_F(ConeHalfDeg), SG_F(FacesAft), SG_F(Kind), SG_F(MaxHp), SG_F(Rating),
		SG_F(Spinal), SG_F(Weapon), SG_F(X), SG_F(Y), SG_F(Z))

	SG_SERIALIZE(Treaty, "Unity.SpaceGod.Core.Treaty",
		SG_F(A), SG_F(B), SG_F(Ceded), SG_F(Jd), SG_F(Terms), SG_F(TruceUntilJd), SG_F(Winner))

	SG_SERIALIZE(War, "Unity.SpaceGod.Core.War",
		SG_F(BattleEnded), SG_F(BattleStarted), SG_F(Battles), SG_F(CorvettesShortSince), SG_F(Crews), SG_F(Ended),
		SG_F(Heat), SG_F(HeatDrawn), SG_F(Orders), SG_F(PicketWarnings), SG_F(Plundered), SG_F(PriorOrders),
		SG_F(SurgeSlipChance), SG_F(Wrecks), SG_F(assaultDays), SG_F(battleCount), SG_F(besiegers),
		SG_F(builtWarships), SG_F(captures), SG_F(ceasefireUntilJd), SG_F(danger), SG_F(gateBattles), SG_F(laneSeen),
		SG_F(lastBattleEndJd), SG_F(lastJd), SG_F(losses), SG_F(nextBattleId), SG_F(nextDailyJd),
		SG_F(nextPodSweepJd), SG_F(pilotNotices), SG_F(pulseReady), SG_F(raidersRaised), SG_F(retreating), SG_F(rng),
		SG_F(siegeDays), SG_F(warStartJd), SG_F(world))

	SG_SERIALIZE(WeaponSpec, "Unity.SpaceGod.Core.WeaponSpec",
		SG_F(Accuracy), SG_F(Ammo), SG_F(AntiSmall), SG_F(BurstKm), SG_F(Cooldown), SG_F(Damage), SG_F(Deploy),
		SG_F(EmpRadiusKm), SG_F(EmpSeconds), SG_F(FalloffEdgeShare), SG_F(Lance), SG_F(Missile),
		SG_F(MissileSpeedKmS), SG_F(MuzzleSpeedKmS), SG_F(RangeKm), SG_F(Salvo), SG_F(SeeksSmallCraft))

	SG_SERIALIZE(WingCraft, "Unity.SpaceGod.Core.WingCraft",
		SG_F(Berth), SG_F(Carrier), SG_F(Pilot), SG_F(Role))

	SG_SERIALIZE(Wreck, "Unity.SpaceGod.Core.Wreck",
		SG_F(Anchor), SG_F(Body), SG_F(CreatedJd), SG_F(Faction), SG_F(Id), SG_F(LengthKm), SG_F(LifetimeDays),
		SG_F(Local), SG_F(Name), SG_F(Pieces), SG_F(Pos), SG_F(Salvage), SG_F(Ship), SG_F(Site))

	SG_SERIALIZE(WreckPiece, "Unity.SpaceGod.Core.WreckPiece",
		SG_F(Fragment), SG_F(Index), SG_F(Scrap))

	SG_SERIALIZE(Wrecks, "Unity.SpaceGod.Core.Wrecks",
		SG_F(DefaultLifetimeDays), SG_F(Items), SG_F(nextId))
}
