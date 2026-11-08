// Not a C# file (US-T36): the ObjectGraph registrations (docs/PERSISTENCE.md) of the Systems/ types a
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
#include "Embodiment/Influence.h" // US-T140: Polities.Influence
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
#include "Systems/Territory.h" // US-T190
#include "Systems/DiscoveryLog.h" // US-T259
#include "Trajectories.h"
#include "World.h"
#include "Persistence/Registrations/Registrations.h"

namespace SpaceGod::Core
{
	namespace
	{
		// (US-T41) A C# array or List field that stays null until something sets it (no initialiser): the port holds that null
		// as the empty list (docs/MIGRATION.md), so an empty one is written as null, as the C# writes it. Written as empty it
		// reads back in C# as an empty array where the C# code tests for null (Ship.Plunder: `ship.Plunder == null`).
		template <class Base> struct EmptyIsNull : Base
		{
			template <class C> static void Write(Graph::Writer& w, C& v) { if (v.Count() == 0) w.Payload().Var(0); else Base::Write(w, v); }
		};
		template <class T, class E> Graph::ClassBuilder<T>& NullArray(Graph::ClassBuilder<T>& b, const char* name, cs::List<E> T::* m) { return b.template FieldWith<EmptyIsNull<Graph::ArrayCodec<E>>>(name, m); }
		template <class T, class E> Graph::ClassBuilder<T>& NullList(Graph::ClassBuilder<T>& b, const char* name, cs::List<E> T::* m) { return b.template FieldWith<EmptyIsNull<Graph::Codec<cs::List<E>>>>(name, m); }
	}
}
#define SG_NULLARRAY(member) NullArray(b, #member, &Self::member)
#define SG_NULLLIST(member) NullList(b, #member, &Self::member)

namespace SpaceGod::Core
{
	SG_ENUM(CampaignMission, "Unity.SpaceGod.Core.CampaignMission")

	SG_ENUM(CampaignStage, "Unity.SpaceGod.Core.CampaignStage")

	SG_ENUM(DeepSpaceObjectKind, "Unity.SpaceGod.Core.DeepSpaceObjectKind")

	SG_ENUM(ForceKind, "Unity.SpaceGod.Core.ForceKind")

	SG_ENUM(GateJob, "Unity.SpaceGod.Core.GateJob")

	SG_ENUM(GateStage, "Unity.SpaceGod.Core.GateStage")

	SG_ENUM(HabitableZone, "Unity.SpaceGod.Core.HabitableZone")

	SG_ENUM(ObjectiveKind, "Unity.SpaceGod.Core.ObjectiveKind")

	SG_ENUM(PolityKind, "Unity.SpaceGod.Core.PolityKind")

	SG_ENUM(VoyagePurpose, "Unity.SpaceGod.Core.VoyagePurpose")

	SG_INTERFACE(IDeepSpaceEncounter, "Unity.SpaceGod.Core.IDeepSpaceEncounter")

	SG_STRUCT(CruiseProfile, "Unity.SpaceGod.Core.CruiseProfile",
		SG_F(Ly), SG_F(LyPerDay), SG_F(RampDays), SG_F(SpoolDays))

	SG_STRUCT(NearbyStar, "Unity.SpaceGod.Core.NearbyStar",
		SG_F(ApparentMagnitude), SG_F(Color), SG_F(Name), SG_F(PositionLy), SG_F(SpectralType), SG_F(TeffK))

	SG_SERIALIZE(Appraisal, "Unity.SpaceGod.Core.Appraisal",
		SG_F(Reasons), SG_F(Score))

	SG_SERIALIZE(BinaryOrbitSpec, "Unity.SpaceGod.Core.BinaryOrbitSpec",
		SG_F(AAu), SG_F(ArgPeriDeg), SG_F(E), SG_F(IDeg), SG_F(NodeDeg), SG_F(PeriastronYear), SG_F(PeriodYears))

	SG_SERIALIZE(BuildPlan, "Unity.SpaceGod.Core.BuildPlan",
		SG_PARRAY(GroupFittings), SG_ARRAY(Cycle), SG_F(Description), SG_F(Name), SG_F(NavyShare))

	SG_SERIALIZE(Campaign, "Unity.SpaceGod.Core.Campaign",
		SG_F(ArrivedJd), SG_F(BesiegedDays), SG_F(Besieging), SG_F(EndedJd), SG_F(Faction), SG_F(Flagship),
		SG_F(Force), SG_F(GateBattles), SG_F(GatesTaken), SG_F(Goal), SG_F(Home), SG_F(Lost), SG_F(Mission),
		SG_F(Objective), SG_F(PortsRaided), SG_F(Raided), SG_F(Relieved), SG_F(SetOutJd), SG_F(SettlementsTaken),
		SG_F(Stage), SG_F(Supply), SG_F(War))

	SG_SERIALIZE(CoBelligerence, "Unity.SpaceGod.Core.CoBelligerence",
		SG_F(Ally), SG_F(Enemy), SG_F(Friend), SG_F(SinceJd))

	SG_SERIALIZE(CompanionSpec, "Unity.SpaceGod.Core.CompanionSpec",
		SG_F(DecDeg), SG_F(DistanceLy), SG_F(E), SG_F(MeanAnomalyDeg), SG_F(Node), SG_F(PositionAngleDeg),
		SG_F(RaDeg), SG_F(SeparationAu))

	SG_SERIALIZE(Conflict, "Unity.SpaceGod.Core.Conflict",
		SG_F(Aggressor), SG_F(Campaigns), SG_F(Defender), SG_F(EndedJd), SG_F(Goals), SG_F(LastActiveJd),
		SG_F(StartJd), SG_F(Treaty))

	SG_SERIALIZE(Cruise, "Unity.SpaceGod.Core.Cruise",
		SG_F(Aboard), SG_F(CarriedVelocityKmS), SG_F(Crossing), SG_F(Drive), SG_F(End), SG_F(From), SG_F(Hired),
		SG_F(HoldUntilJd), SG_F(Holding), SG_F(InterdictedBy), SG_F(LegJd), SG_F(News), SG_F(Piloted), SG_F(Plan),
		SG_F(Profile), SG_F(SetOutJd), SG_F(Ships), SG_F(Start), SG_F(Target), SG_F(To), SG_F(ToName),
		SG_OPT(ChargeJd), SG_OPT(StopJd), SG_OPT(Stopover), SG_OPT(Stops), // US-T86: jumps and void stops, absent from older saves
		SG_OPT(PingJd), SG_OPT(RaidSeen), SG_OPT(RaidStrikeJd), SG_OPT(RaidStruck), SG_OPT(RunDark), // US-T124: the ghosts at a void stop, absent from older saves
		SG_OPT(DreadJumps), SG_OPT(WatcherSign), SG_OPT(WatcherSigns)) // US-T178: the Watcher in the void, absent from older saves

	SG_SERIALIZE(CruiseDriveSpec, "Unity.SpaceGod.Core.CruiseDriveSpec",
		SG_P(ChargePropellantShare), SG_P(CruiseC), SG_P(Name), SG_P(PropellantSharePerLy), SG_P(RampDays),
		SG_P(RatedCooldownDays), SG_P(RatedSpoolDays),
		SG_OPT(MaxJumpLy)) // US-T86: a drive's longest jump, absent from older saves (which load with the capital drive's 7 ly)

	SG_SERIALIZE(CruiseMeeting, "Unity.SpaceGod.Core.CruiseMeeting",
		SG_IMPLEMENTS(IDeepSpaceEncounter))

	SG_SERIALIZE(Deal, "Unity.SpaceGod.Core.Deal",
		SG_F(Appraise), SG_F(Description), SG_F(Enact), SG_F(HasAmount), SG_F(Name), SG_F(Unavailable),
		b.Typed("Words", &Self::Words, +[]() -> const ::SpaceGod::Core::Graph::NetType* { using namespace ::SpaceGod::Core::Registrations; using namespace ::SpaceGod::Core::Graph; return DelegateType(2, true, { TypeOf<cs::Ref<Proposal>>(), TypeOf<cs::String>() }); }))

	SG_SERIALIZE(DebrisDisc, "Unity.SpaceGod.Core.DebrisDisc",
		SG_F(HostIndex), SG_F(HostName), SG_F(InnerKm), SG_F(Name), SG_F(Normal), SG_F(OuterKm), SG_F(Summary))

	SG_SERIALIZE(DeepSpace, "Unity.SpaceGod.Core.DeepSpace",
		SG_P(Rescued), SG_P(SurveyFindsMade), SG_F(Arrived), SG_F(Encounters), SG_F(ReachedStar), SG_F(RouteLife),
		SG_F(ambushes), SG_F(cruises), SG_F(events), SG_F(galaxy), SG_F(lifeRng), SG_F(nextMarkJd), SG_F(objects),
		SG_F(projectors), SG_F(rng), SG_F(routes), SG_F(seededRoutes), SG_F(surveyedStars),
		SG_OPT(VoidRaidLore), SG_OPT(VoidRaids), SG_OPT(raidRng), // US-T124: the Void Raiders, absent from older saves
		SG_OPT(WatcherLore), SG_OPT(WatcherSigns), SG_OPT(watcherRng)) // US-T178: the Watcher in the void, absent from older saves

	SG_SERIALIZE(DeepSpaceAmbush, "Unity.SpaceGod.Core.DeepSpaceAmbush",
		SG_F(Band), SG_F(LegJd), SG_F(Projector), SG_F(UntilJd), SG_F(Where))

	SG_SERIALIZE(DeepSpaceEvent, "Unity.SpaceGod.Core.DeepSpaceEvent",
		SG_F(Jd), SG_F(Text), SG_F(Where),
		SG_OPT(Kind)) // US-T262: what kind of line, absent from older saves

	SG_SERIALIZE(DeepSpaceObject, "Unity.SpaceGod.Core.DeepSpaceObject",
		SG_F(ExpiresJd), SG_F(Gm), SG_F(Heard), SG_F(HomeName), SG_F(IceTonnes), SG_F(Kind), SG_F(LostBy),
		SG_F(LureBy), SG_F(Name), SG_F(OffNetwork), SG_F(Owner), SG_F(Position), SG_F(RadiusKm), SG_F(RouteA),
		SG_F(RouteB), SG_F(SalvageCredits), SG_F(SinceJd), SG_F(SurveyCredits), SG_F(SurveyedBy), SG_F(Survivors))

	SG_SERIALIZE(DeepSpaceOutposts, "Unity.SpaceGod.Core.DeepSpaceOutposts",
		SG_IMPLEMENTS(IDeepSpaceEncounter))

	SG_SERIALIZE(DeepSpaceRoute, "Unity.SpaceGod.Core.DeepSpaceRoute",
		SG_F(A), SG_F(AtA), SG_F(AtB), SG_F(B), SG_F(OnNetwork))

	SG_SERIALIZE(DiscSpec, "Unity.SpaceGod.Core.DiscSpec",
		SG_F(Host), SG_F(IDeg), SG_F(InnerAu), SG_F(Name), SG_F(OuterAu), SG_F(Source), SG_F(Summary))

	SG_SERIALIZE(DistressCalls, "Unity.SpaceGod.Core.DistressCalls",
		SG_IMPLEMENTS(IDeepSpaceEncounter))

	SG_SERIALIZE(FleetObjective, "Unity.SpaceGod.Core.FleetObjective",
		SG_F(Body), SG_F(Campaign), SG_F(Charge), SG_F(Kind), SG_F(SinceJd), SG_F(Visited), SG_F(Where))

	SG_SERIALIZE(FleetSupply, "Unity.SpaceGod.Core.FleetSupply",
		SG_F(Base), SG_F(Convoy), SG_F(ConvoysArrived), SG_F(ConvoysLost), SG_F(Munitions), SG_F(OutOfSupply),
		SG_F(OutOfSupplySinceJd), SG_F(Propellant), SG_F(Spares))

	SG_SERIALIZE(FtlInterdiction, "Unity.SpaceGod.Core.FtlInterdiction",
		SG_IMPLEMENTS(IDeepSpaceEncounter))

	SG_SERIALIZE(Galaxy, "Unity.SpaceGod.Core.Galaxy",
		SG_P(ChartVersion), SG_P(Colonisation), SG_P(Construction), SG_P(DeepSpace), SG_P(Focus), SG_P(Network),
		SG_P(Polities), SG_P(Traffic), SG_P(Wars), SG_F(CoarseStepDays), SG_F(Discoveries), SG_F(NearbyStars),
		SG_F(Seed), SG_F(StarDiscovered), SG_F(SystemAdded), SG_F(SystemOpened), SG_F(SystemRemoved), SG_F(Systems),
		SG_F(Viewpoint), SG_F(charted), SG_F(exploredAtLy), SG_F(lookedJd), SG_F(streamed), SG_F(undiscovered),
		SG_F(undiscoveredNames),
		SG_OPT(Territory), // US-T190: faction territory, absent from older saves (made on the first update)
		SG_OPT(DiscoveryLog), // US-T259: the discovery log, absent from older saves (made on the game's first sweep)
		SG_OPT(DifficultyRecord), // US-T253: the difficulty, absent from older saves (the setting then stands)
		SG_OPT(SetupRecord), // US-T247: how the game began (New Game setup), absent from older saves
		b.Loaded(&::SpaceGod::Core::Registrations::RewireGalaxy))

	SG_SERIALIZE(GateConstruction, "Unity.SpaceGod.Core.GateConstruction",
		SG_P(Built), SG_P(Opened), SG_P(Repaired), SG_F(Completed), SG_F(galaxy), SG_F(nextDailyJd),
		SG_F(nextPlanJd), SG_F(projects), SG_F(rng))

	SG_SERIALIZE(GateCrossing, "Unity.SpaceGod.Core.GateCrossing",
		SG_F(Faction), SG_F(From), SG_F(Jd), SG_F(Link), SG_F(Purpose), SG_F(Ships), SG_F(Tonnes))

	SG_SERIALIZE(GateLink, "Unity.SpaceGod.Core.GateLink",
		SG_P(A), SG_P(AtA), SG_P(AtB), SG_P(B), SG_P(Ly))

	SG_SERIALIZE(GateNetwork, "Unity.SpaceGod.Core.GateNetwork",
		SG_F(links), SG_F(systems))

	SG_SERIALIZE(GateProject, "Unity.SpaceGod.Core.GateProject",
		SG_F(BuildStartJd), SG_F(Builder), SG_F(Constructors), SG_F(CrewLostJd), SG_F(DoneJd), SG_F(From),
		SG_F(FromIntegrity), SG_F(Frontier), SG_F(Gate), SG_F(Ly), SG_ARRAY(Need), SG_F(Spent), SG_F(Stage),
		SG_F(StartJd), SG_F(SurveyDays), SG_F(TargetName), SG_F(To), SG_ARRAY(Used), SG_F(Work), SG_F(WorkDays),
		SG_F(WorkDone))

	SG_SERIALIZE(GravityProjector, "Unity.SpaceGod.Core.GravityProjector",
		SG_F(Active), SG_F(Name), SG_F(Owner), SG_F(Position), SG_F(RadiusKm))

	SG_SERIALIZE(IceRefuelling, "Unity.SpaceGod.Core.IceRefuelling",
		SG_IMPLEMENTS(IDeepSpaceEncounter))

	SG_SERIALIZE(InterstellarColonisation, "Unity.SpaceGod.Core.InterstellarColonisation",
		SG_F(galaxy))

	SG_SERIALIZE(InterstellarTraffic, "Unity.SpaceGod.Core.InterstellarTraffic",
		SG_P(Clans), SG_P(Crossings), SG_P(News), SG_P(TollsPaid), SG_F(Arrivals), SG_F(ambushRng), SG_F(atGates),
		SG_F(crossingLog), SG_F(cruiseRng), SG_F(expeditionsWanted), SG_F(galaxy), SG_F(inGates), SG_F(nextDailyJd),
		SG_F(outCache), SG_F(rng))

	SG_SERIALIZE(InterstellarWars, "Unity.SpaceGod.Core.InterstellarWars",
		SG_F(AutoDeclare), SG_F(DeclareChancePerMonth), SG_F(Logistics), SG_F(campaigns), SG_F(ended), SG_F(galaxy),
		SG_F(landings), SG_F(nextDailyJd), SG_F(nextDeclareJd), SG_F(rng), SG_F(takenGates), SG_F(wars))

	SG_SERIALIZE(JumpGate, "Unity.SpaceGod.Core.JumpGate",
		SG_P(Home), SG_P(Link), SG_P(Name), SG_P(Owner), SG_P(Partner), SG_P(PositionKm), SG_F(Ambushes),
		SG_F(BlockadeUntilJd), SG_F(Forts), SG_F(Integrity), SG_F(MaxForts), SG_F(PicketsShortSinceJd),
		SG_F(Traffic))

	SG_SERIALIZE(Landing, "Unity.SpaceGod.Core.Landing",
		SG_F(By), SG_F(Campaign), SG_F(SinceJd), SG_F(Target), SG_F(Troops))

	SG_SERIALIZE(LaneUpkeep, "Unity.SpaceGod.Core.LaneUpkeep",
		SG_F(LaneChanged), SG_F(Tending), SG_F(lastJd), SG_F(named), SG_F(owners), SG_F(ownersJd), SG_F(raised),
		SG_F(rng), SG_F(world))

	SG_SERIALIZE(MilitaryLogistics, "Unity.SpaceGod.Core.MilitaryLogistics",
		SG_F(colliersNamed), SG_F(convoys), SG_F(galaxy), SG_F(tankersNamed))

	SG_SERIALIZE(Pact, "Unity.SpaceGod.Core.Pact",
		SG_F(A), SG_F(Amount), SG_F(B), SG_F(Kind), SG_F(SinceJd), SG_F(UntilJd))

	SG_SERIALIZE(PactKind, "Unity.SpaceGod.Core.PactKind",
		SG_F(CallsToArms), SG_F(Daily), SG_F(Days), SG_F(Description), SG_F(Directed), SG_F(KeepsPeace), SG_F(Name),
		SG_F(Warmth))

	SG_SERIALIZE(Passage, "Unity.SpaceGod.Core.Passage",
		SG_F(EnterJd), SG_F(ExitJd), SG_F(From), SG_F(Hired), SG_F(News), SG_F(Ships), SG_F(To))

	SG_SERIALIZE(PirateClans, "Unity.SpaceGod.Core.PirateClans",
		SG_P(BountiesPaid), SG_P(Expansions), SG_P(Expeditions), SG_P(Foundings), SG_P(Hunts), SG_P(Moves),
		SG_P(Runs), SG_P(Sales), SG_P(SmuggledValue), SG_P(Splits), SG_F(founded), SG_F(galaxy), SG_F(pledges),
		SG_F(resolved), SG_F(rng), SG_F(runnersBuilt), SG_F(traffic))

	SG_SERIALIZE(PlanetSpec, "Unity.SpaceGod.Core.PlanetSpec",
		SG_F(AAu), SG_F(ArgPeriDeg), SG_F(AtmosphereColor), SG_F(AtmosphereStrength), SG_F(Color),
		SG_F(DiscoveryYear), SG_F(E), SG_F(GasGiant), SG_F(Host), SG_F(IDeg), SG_F(MassEarth), SG_F(MassIsMinimum),
		SG_F(MeanAnomalyDeg), SG_F(Name), SG_F(NodeDeg), SG_F(PeriastronJd), SG_F(PeriodDays), SG_F(RadiusEarth),
		SG_F(RotationHours), SG_F(Source), SG_F(Status), SG_F(Summary), SG_F(TidallyLocked),
		SG_F(TiltFromReferenceDeg))

	SG_SERIALIZE(Polities, "Unity.SpaceGod.Core.Polities",
		SG_P(Command), SG_P(Diplomacy), SG_P(Statecraft),
		b.Typed("InTransit", &Self::InTransit, +[]() -> const ::SpaceGod::Core::Graph::NetType* { using namespace ::SpaceGod::Core::Registrations; using namespace ::SpaceGod::Core::Graph; return DelegateType(1, true, { IEnumerableOf(TypeOf<cs::Ref<Ship>>()) }); }),
		SG_F(factions), SG_F(homes), SG_F(worlds),
		SG_OPT(Influence)) // US-T140: the SpaceGod's Influence, absent from older saves

	SG_SERIALIZE(PolityDirectives, "Unity.SpaceGod.Core.PolityDirectives",
		SG_F(Build), SG_F(Faction), SG_F(Levels), SG_F(Production), SG_F(Research), SG_F(ResearchPoints))

	SG_SERIALIZE(ProductionFocus, "Unity.SpaceGod.Core.ProductionFocus",
		SG_F(Description), SG_F(Name), SG_ARRAY(favoured))

	SG_SERIALIZE(Proposal, "Unity.SpaceGod.Core.Proposal",
		SG_F(Amount), SG_F(Deal), SG_F(From), SG_F(Jd), SG_F(To))

	SG_SERIALIZE(ResearchTrack, "Unity.SpaceGod.Core.ResearchTrack",
		SG_F(BaseCost), SG_F(Description), SG_F(MaxLevel), SG_F(Name), SG_F(OnLevel))

	SG_SERIALIZE(RouteAmbushes, "Unity.SpaceGod.Core.RouteAmbushes",
		SG_IMPLEMENTS(IDeepSpaceEncounter))

	SG_SERIALIZE(StarDiscoveredEvent, "Unity.SpaceGod.Core.StarDiscoveredEvent",
		SG_F(By), SG_F(Jd), SG_F(Name), SG_F(PositionLy))

	SG_SERIALIZE(StarSpec, "Unity.SpaceGod.Core.StarSpec",
		SG_F(LuminositySolar), SG_F(MassSolar), SG_F(Name), SG_F(RadiusSolar), SG_F(RotationDays), SG_F(Spectral),
		SG_F(Summary), SG_F(TeffK))

	SG_SERIALIZE(StarSystem, "Unity.SpaceGod.Core.StarSystem",
		SG_P(Catalog),
		b.Typed("<Discs>k__BackingField", &Self::Discs, +[]() -> const ::SpaceGod::Core::Graph::NetType* { using namespace ::SpaceGod::Core::Registrations; using namespace ::SpaceGod::Core::Graph; return IReadOnlyListOf(TypeOf<cs::Ref<DebrisDisc>>()); }),
		SG_P(Ephemeris), SG_P(ExtentKm), SG_P(IsVoidBase), SG_P(Name), SG_P(Origin), SG_P(OverviewSpanKm),
		SG_P(PositionLy), SG_P(Spec), SG_P(World), SG_F(OpenedJd), SG_F(gates))

	SG_SERIALIZE(Statecraft, "Unity.SpaceGod.Core.Statecraft",
		SG_F(AutoDiplomacy), SG_F(Galaxy), SG_F(Polities), SG_F(coBelligerents), SG_F(inbox), SG_F(nextDailyJd),
		SG_F(nextRoundJd), SG_F(opinions), SG_F(pacts), SG_F(powerCache), SG_F(record), SG_F(rng), SG_F(warsBegun))

	SG_SERIALIZE(StationSpec, "Unity.SpaceGod.Core.StationSpec",
		SG_F(AAu), SG_F(HabitatSite), SG_F(Host), SG_F(MeanAnomalyDeg), SG_F(Name), SG_F(Summary))

	SG_SERIALIZE(StellarNode, "Unity.SpaceGod.Core.StellarNode",
		SG_F(A), SG_F(B), SG_F(Name), SG_F(Orbit), SG_F(Star), SG_F(Summary))

	SG_SERIALIZE(StrategicCommand, "Unity.SpaceGod.Core.StrategicCommand",
		SG_F(Galaxy), SG_F(Player), SG_F(directives), SG_F(nextDailyJd), SG_F(objectives), SG_F(polities))

	SG_SERIALIZE(SupplyConvoy, "Unity.SpaceGod.Core.SupplyConvoy",
		SG_F(Base), SG_F(Campaign), SG_F(Colliers), SG_F(Cost), SG_F(DueJd), SG_F(Faction), SG_F(From), SG_F(Hulls),
		SG_F(Munitions), SG_F(Propellant), SG_F(SetOutJd), SG_F(Spares), SG_F(Tankers), SG_F(Tonnes))

	SG_SERIALIZE(SurveyFinds, "Unity.SpaceGod.Core.SurveyFinds",
		SG_IMPLEMENTS(IDeepSpaceEncounter))

	SG_SERIALIZE(SystemSpec, "Unity.SpaceGod.Core.SystemSpec",
		SG_F(Companions), SG_F(DecDeg), SG_F(Discs), SG_F(DistanceLy), SG_F(Name), SG_F(NeighbourName),
		SG_F(OverviewSpanAu), SG_F(Planets), SG_F(Primary), SG_F(RaDeg), SG_F(ReferenceIDeg), SG_F(ReferenceNodeDeg),
		SG_F(Stations), SG_F(Summary))

	SG_SERIALIZE(Voyage, "Unity.SpaceGod.Core.Voyage",
		SG_F(AtGate), SG_F(AtGateJd), SG_F(ByCruise), SG_F(Campaign), SG_F(Convoy), SG_F(Destination), SG_F(DropDir),
		SG_F(ExpectedSale), SG_F(Faction), SG_F(HoldGate), SG_F(Home), SG_ARRAY(HomePorts), SG_F(HomeRole),
		SG_NULLARRAY(HomeRoute), SG_F(Leg), SG_F(Ly), SG_F(Origin), SG_F(PlannedMarket), SG_F(PlannedOnNewsJd),
		SG_F(Purpose), SG_F(Quarry), SG_F(Route), SG_F(SetOutJd), SG_F(SiteName), SG_F(StarName), SG_F(Station),
		SG_F(StationName), SG_F(StayUntilJd), SG_F(TargetBody), SG_F(Tolls), SG_F(WaitingSinceJd))

	SG_SERIALIZE(WreckSalvage, "Unity.SpaceGod.Core.WreckSalvage",
		SG_IMPLEMENTS(IDeepSpaceEncounter))
}
