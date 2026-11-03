// Not a C# file (US-T36): the ObjectGraph registrations (docs/PERSISTENCE.md) of the Economy/ types a
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
	SG_ENUM(CivilCraft, "Unity.SpaceGod.Core.CivilCraft")

	SG_ENUM(ColonyStage, "Unity.SpaceGod.Core.ColonyStage")

	SG_ENUM(CompanyFate, "Unity.SpaceGod.Core.CompanyFate")

	SG_ENUM(ContractKind, "Unity.SpaceGod.Core.ContractKind")

	SG_ENUM(ContractState, "Unity.SpaceGod.Core.ContractState")

	SG_ENUM(ConvoyState, "Unity.SpaceGod.Core.ConvoyState")

	SG_ENUM(Good, "Unity.SpaceGod.Core.Good")

	SG_ENUM(IndustryKind, "Unity.SpaceGod.Core.IndustryKind")

	SG_ENUM(PropertyKind, "Unity.SpaceGod.Core.PropertyKind")

	SG_ENUM(SettlementKind, "Unity.SpaceGod.Core.SettlementKind")

	SG_ENUM(ShockKind, "Unity.SpaceGod.Core.ShockKind")

	SG_ENUM(StandingOrderKind, "Unity.SpaceGod.Core.StandingOrderKind")

	SG_ENUM(TraderRank, "Unity.SpaceGod.Core.TraderRank")

	SG_STRUCT(HeavyLanePath, "Unity.SpaceGod.Core.HeavyLanePath",
		SG_F(LaneKm), SG_F(LegKm), SG_F(Name))

	SG_STRUCT(TripEstimate, "Unity.SpaceGod.Core.TripEstimate",
		SG_F(Days), SG_F(DeltaVKmS), SG_F(LaneHops))

	SG_STRUCT(TugDanger, "Unity.SpaceGod.Core.TugDanger",
		SG_F(Body), SG_F(Pressure), SG_F(Raiders))

	SG_SERIALIZE(AsteroidClass, "Unity.SpaceGod.Core.AsteroidClass",
		SG_P(Density), SG_P(Name), SG_P(Share), SG_PARRAY(Yield))

	SG_SERIALIZE(BeltMining, "Unity.SpaceGod.Core.BeltMining",
		SG_F(CaptureTugsDestroyed), SG_F(CaptureTugsTaken), SG_F(Captures), SG_F(ConvoyDelivered), SG_F(ConvoyLost),
		SG_F(ConvoySailed), SG_F(ConvoyStolen), SG_F(Convoys), SG_F(ConvoysDelivered), SG_F(ConvoysLost),
		SG_F(ConvoysStolen), SG_F(FreighterFree), SG_F(Guarded), SG_F(HaulSent), SG_F(Hauls), SG_F(HeavyLane),
		SG_F(Log), SG_F(Operations), SG_F(PiratePressure), SG_ARRAY(Plundered), SG_F(Position),
		SG_F(ProjectCompleted), SG_F(Projects), SG_F(Raiders), SG_F(TugFree), SG_F(TugHaulsDelivered),
		SG_F(TugSeized), SG_F(TugTaken), SG_F(TugThreat), SG_F(TugsDestroyed), SG_F(TugsTaken),
		SG_F(WarehouseRaided), SG_F(WarehouseRaids), SG_F(Warehouses), SG_F(YardBuilt), SG_F(convoySerial),
		SG_F(haulSerial), SG_F(nextDayJd), SG_F(rng), SG_F(rockSerial))

	SG_SERIALIZE(Captain, "Unity.SpaceGod.Core.Captain",
		SG_F(DaysUnpaid), SG_F(HiredJd), SG_F(Name), SG_F(Skill), SG_F(WagePerDay))

	SG_SERIALIZE(CapturedRock, "Unity.SpaceGod.Core.CapturedRock",
		SG_F(CapturedJd), SG_F(Class), SG_F(Designation), SG_F(DiameterM), SG_F(Remaining), SG_F(Tonnes))

	SG_SERIALIZE(CargoHold, "Unity.SpaceGod.Core.CargoHold",
		SG_F(Height), SG_F(Width), SG_F(items))

	SG_SERIALIZE(CargoItem, "Unity.SpaceGod.Core.CargoItem",
		SG_F(Good), SG_F(Rotated), SG_F(Stolen), SG_F(X), SG_F(Y))

	SG_SERIALIZE(Charter, "Unity.SpaceGod.Core.Charter",
		SG_F(Active), SG_F(Charterer), SG_F(EndJd), SG_F(EndedHow), SG_F(EndedJd), SG_F(HirePaid),
		SG_F(PaidThroughJd), SG_F(PreviousOrders), SG_F(RatePerDay), SG_F(Served), SG_F(Ship), SG_F(StartJd),
		SG_F(TradeResult))

	SG_SERIALIZE(CharterOffer, "Unity.SpaceGod.Core.CharterOffer",
		SG_F(Charterer), SG_F(Class), SG_F(Days), SG_F(ExpiresJd), SG_F(Id), SG_F(Port), SG_F(PostedJd),
		SG_F(RatePerDay))

	SG_SERIALIZE(Charters, "Unity.SpaceGod.Core.Charters",
		SG_F(All), SG_F(Offers), SG_F(desk), SG_F(nextId), SG_F(nextPostJd), SG_F(rng))

	SG_SERIALIZE(Colonisation, "Unity.SpaceGod.Core.Colonisation",
		
		b.Typed("Abroad", &Self::Abroad, +[]() -> const ::SpaceGod::Core::Graph::NetType* { using namespace ::SpaceGod::Core::Registrations; using namespace ::SpaceGod::Core::Graph; return DelegateType(4, true, { TypeOf<cs::Ref<Faction>>(), TypeOf<cs::Ref<Settlement>>(), TypeOf<double>(), IEnumerableOf(TypeOf<cs::Ref<ForeignSite>>()) }); }),
		SG_F(Embark), SG_F(Expanded), SG_F(Founded), SG_F(Incoming), SG_F(Landed), SG_F(Launched), SG_F(Lost),
		SG_F(Promoted), SG_F(Refit), SG_F(Returned), SG_F(Ruined), SG_F(SailAbroad), SG_F(Sites), SG_F(TravelDays),
		SG_F(Voyages), SG_F(launched))

	SG_SERIALIZE(ColonyShip, "Unity.SpaceGod.Core.ColonyShip",
		SG_F(ArriveJd), SG_ARRAY(Cargo), SG_F(Colonists), SG_F(DepartJd), SG_F(From), SG_F(Hull), SG_F(Into),
		SG_F(IntoEconomy), SG_F(Name), SG_F(Of), SG_F(Owner), SG_F(Reinforcing), SG_F(Returning), SG_F(Site),
		SG_F(Where))

	SG_SERIALIZE(ColonySite, "Unity.SpaceGod.Core.ColonySite",
		SG_F(Body), SG_F(Capacity), SG_F(Flux), SG_F(Habitat), SG_F(Name), SG_ARRAY(Resources), SG_F(Zone))

	SG_SERIALIZE(Companies, "Unity.SpaceGod.Core.Companies",
		SG_F(All), SG_F(FoundingCapital), SG_F(FoundingDays), SG_F(MaxCompanies), SG_F(foundedCount),
		SG_F(hullsNamed), SG_F(lastReviewJd), SG_F(nextFoundingJd), SG_F(nextReviewJd), SG_F(rng), SG_F(world))

	SG_SERIALIZE(Company, "Unity.SpaceGod.Core.Company",
		SG_F(Cash), SG_F(Chosen), SG_ARRAY(ClassPerDay), SG_F(Earned), SG_F(Fate), SG_F(FateJd), SG_F(Flag),
		SG_F(FoundedJd), SG_F(HomePort), SG_F(HullsBought), SG_F(HullsLost), SG_F(HullsScrapped), SG_F(HullsSold),
		SG_F(HullsSpent), SG_F(InDebtSinceJd), SG_F(IncomePerDay), SG_F(Livery), SG_F(MergedInto), SG_F(Name),
		SG_F(OnOrder), SG_F(OrderJd), SG_F(OrderPrice), SG_F(OrderYard), SG_F(Routes), SG_F(RunningCosts),
		SG_F(Ships), SG_F(Standing), SG_F(Tag), SG_F(TaxPaid))

	SG_SERIALIZE(Contract, "Unity.SpaceGod.Core.Contract",
		SG_F(Aboard), SG_F(Board), SG_F(Body), SG_F(BodyLabel), SG_F(ByPlayer), SG_F(Client), SG_F(ClientArrivedJd),
		SG_F(ClosedJd), SG_F(CoverAtPost), SG_F(Danger), SG_F(DeadlineDays), SG_F(Delivered), SG_F(Destination),
		SG_F(DistanceKm), SG_F(DueJd), SG_F(ExpiresJd), SG_F(Good), SG_F(Id), SG_F(Kind), SG_F(OnStation),
		SG_F(PatrolDays), SG_F(Patron), SG_F(Pay), SG_F(People), SG_F(PostedJd), SG_F(State), SG_F(TakenJd),
		SG_F(Taker), SG_F(TakerFaction), SG_F(TakerRecord), SG_F(Target), SG_F(Tonnes), SG_F(Urgency))

	SG_SERIALIZE(Contracts, "Unity.SpaceGod.Core.Contracts",
		SG_F(All), SG_F(Visited), SG_F(nextDailyJd), SG_F(nextId), SG_F(pilotBody), SG_F(pilotJd), SG_F(pilotOffset),
		SG_F(rng), SG_F(world))

	SG_SERIALIZE(EconomicShock, "Unity.SpaceGod.Core.EconomicShock",
		SG_F(Embargo), SG_F(EndJd), SG_F(Facility), SG_F(Faction), SG_F(Good), SG_F(Kind), SG_F(Label),
		SG_F(Settlement), SG_F(Severity), SG_F(StartJd), SG_F(Text))

	SG_SERIALIZE(Economy, "Unity.SpaceGod.Core.Economy",
		SG_P(Jd), SG_F(CanCrew), SG_F(ChooseBuild), SG_F(Colonisation), SG_F(Danger), SG_F(Diplomacy),
		SG_F(DockAllowed), SG_F(EstimateTravelDays), SG_F(EstimateTrip), SG_F(Loaded), SG_F(PaidFor),
		SG_F(PriceFactor), SG_F(Priority), SG_F(RouteFactor), SG_F(Settlements), SG_F(ShipBuilt), SG_F(Sold),
		SG_F(lastReviewJd), SG_F(nextReviewJd), SG_F(rng))

	SG_SERIALIZE(Facility, "Unity.SpaceGod.Core.Facility",
		SG_F(BaseLevel), SG_F(Extracted), SG_F(Impairment), SG_F(Level), SG_F(Recipe), SG_F(Reserve), SG_F(Staffing),
		SG_F(Utilisation))

	SG_SERIALIZE(ForeignSite, "Unity.SpaceGod.Core.ForeignSite",
		SG_F(Days), SG_F(Economy), SG_F(Into), SG_F(Site), SG_F(Where))

	SG_SERIALIZE(Holdings, "Unity.SpaceGod.Core.Holdings",
		SG_P(BookBalance), SG_P(LastRefusal), SG_F(House), SG_F(Ledger), SG_F(PilotFrameBody), SG_F(Properties),
		SG_F(PropertyLost), SG_F(ShipLost), SG_F(Ships), SG_F(TotalCosts), SG_F(TotalEarned), SG_F(TotalLost),
		SG_F(byShip), SG_F(captainNames), SG_F(nextTickJd), SG_F(rng), SG_F(world))

	SG_SERIALIZE(InsurancePolicy, "Unity.SpaceGod.Core.InsurancePolicy",
		SG_F(Active), SG_F(Cover), SG_F(EndedHow), SG_F(EndedJd), SG_F(Id), SG_F(Paid), SG_F(PremiumPerDay),
		SG_F(PremiumsPaid), SG_F(Property), SG_F(Ship), SG_F(SinceJd))

	SG_SERIALIZE(LedgerEntry, "Unity.SpaceGod.Core.LedgerEntry",
		SG_F(Amount), SG_F(Cash), SG_F(Jd), SG_F(Text))

	SG_SERIALIZE(MarketIntel, "Unity.SpaceGod.Core.MarketIntel",
		SG_F(desk), SG_F(sheets))

	SG_SERIALIZE(MarketShocks, "Unity.SpaceGod.Core.MarketShocks",
		SG_F(AccidentPerDay), SG_F(Active), SG_F(BesiegerOf), SG_F(BlightPerDay), SG_F(EmbargoThreshold),
		SG_F(Started), SG_F(besiegerAbsentDays), SG_F(contraband), SG_F(contrabandFor), SG_F(firstJd),
		SG_F(lastStrikeEndJd), SG_F(nextDailyJd), SG_F(rng), SG_F(smoothed), SG_F(spikeClearedJd), SG_F(spiking),
		SG_F(surveyedLevel), SG_ARRAY(systemIndex), SG_ARRAY(systemSpiking), SG_F(world))

	SG_SERIALIZE(MaterialConvoy, "Unity.SpaceGod.Core.MaterialConvoy",
		SG_F(ArriveJd), SG_ARRAY(Cargo), SG_F(DepartJd), SG_F(Freighter), SG_F(From), SG_F(Name), SG_F(State),
		SG_F(TakenBy), SG_F(To))

	SG_SERIALIZE(MaterialWarehouse, "Unity.SpaceGod.Core.MaterialWarehouse",
		SG_F(Body), SG_F(CapacityTonnes), SG_F(Name), SG_F(Owner), SG_F(Raids), SG_ARRAY(Stock),
		SG_F(TonnesLostToRaids))

	SG_SERIALIZE(MegastructureProject, "Unity.SpaceGod.Core.MegastructureProject",
		SG_F(BaseName), SG_F(Builder), SG_F(DoneJd), SG_F(Name), SG_F(SiteBody), SG_F(Spec), SG_F(Stage),
		SG_F(StartJd), SG_ARRAY(Used))

	SG_SERIALIZE(MegastructureSpec, "Unity.SpaceGod.Core.MegastructureSpec",
		SG_PARRAY(Bill), SG_P(MinDays), SG_P(Name), SG_F(Completed))

	SG_SERIALIZE(MerchantEscorts, "Unity.SpaceGod.Core.MerchantEscorts",
		SG_F(hired), SG_F(named), SG_F(nextJd), SG_F(rng), SG_F(world))

	SG_SERIALIZE(MiningOperation, "Unity.SpaceGod.Core.MiningOperation",
		SG_F(Backlog), SG_F(CaptureDays), SG_F(CaptureHulls), SG_F(CaptureTugs), SG_F(CaptureTugsLost),
		SG_F(ConvoyFreighters), SG_F(ConvoyTonnes), SG_F(ConvoysSent), SG_F(Destination), SG_F(Dust),
		SG_ARRAY(DustContent), SG_F(EscortShare), SG_F(Freighters), SG_F(FreightersLost), SG_F(GrindTonnesPerDay),
		SG_F(IceDepot), SG_F(LastConvoyJd), SG_F(MaxBacklog), SG_F(MaxHoldDays), SG_F(MaxRockM), SG_F(MinRockM),
		SG_F(Name), SG_F(OreTugs), SG_F(Owner), SG_F(RefineryBody), SG_F(RocksCaptured), SG_F(SeparateTonnesPerDay),
		SG_ARRAY(Separated), SG_F(SeparationEfficiency), SG_F(Slag), SG_F(TonnesCaptured), SG_F(TonnesGround),
		SG_F(TonnesSeparated), SG_F(TugHaulsDelivered), SG_F(TugHaulsSent), SG_F(TugIceShare), SG_F(TugMaxLoadM),
		SG_F(TugMinLoadM), SG_NULLARRAY(TugReadyJd), SG_F(TugSourceBody), SG_F(TugTonnesDelivered), SG_F(Tugs),
		SG_F(TugsLost))

	SG_SERIALIZE(OwnedShip, "Unity.SpaceGod.Core.OwnedShip",
		SG_F(Captain), SG_F(Charter), SG_F(Earned), SG_F(Lost), SG_F(LostHow), SG_F(LostJd), SG_F(MiningDays),
		SG_F(Orders), SG_F(PricePaid), SG_F(Repairs), SG_F(SeenCredits), SG_F(Ship), SG_F(SinceJd), SG_F(Spent),
		SG_F(TonnesMined), SG_F(Wages))

	SG_SERIALIZE(PriceSheet, "Unity.SpaceGod.Core.PriceSheet",
		SG_ARRAY(Available), SG_ARRAY(Buys), SG_F(Jd), SG_F(Market), SG_ARRAY(Price), SG_F(Source), SG_ARRAY(Wants))

	SG_SERIALIZE(Property, "Unity.SpaceGod.Core.Property",
		SG_P(Name), SG_F(Body), SG_F(CapacityTonnes), SG_F(Income), SG_F(Kind), SG_F(Lost), SG_F(LostJd), SG_F(Ore),
		SG_F(PricePaid), SG_F(SeenTradeVolume), SG_F(Settlement), SG_F(Share), SG_F(SinceJd), SG_ARRAY(Stock),
		SG_F(Upkeep), SG_F(YieldPerDay))

	SG_SERIALIZE(Recipe, "Unity.SpaceGod.Core.Recipe",
		SG_PARRAY(Inputs), SG_P(Kind), SG_PARRAY(Limiting), SG_P(Name), SG_PARRAY(Outputs), SG_P(WorkersPerLevel))

	SG_SERIALIZE(Settlement, "Unity.SpaceGod.Core.Settlement",
		SG_F(Attraction), SG_ARRAY(BlackStock), SG_F(Body), SG_F(BuildPoints), SG_F(DaysSinceCapture),
		SG_ARRAY(Demand), SG_F(Earnings), SG_F(Emigrated), SG_ARRAY(ExtraDemand), SG_F(Facilities), SG_F(Fallen),
		SG_F(FoundedJd), SG_F(Garrison), SG_F(HealthWellbeing), SG_F(Hidden), SG_F(Id), SG_ARRAY(Inbound),
		SG_F(IndustryFactor), SG_F(InitialPopulation), SG_F(Kind), SG_F(KnownTo), SG_F(Lawless), SG_F(MaxPlatforms),
		SG_F(Name), SG_F(NetMigration), SG_F(OccupiedFrom), SG_F(OnSlip), SG_F(Overflow), SG_F(Owner),
		SG_F(Platforms), SG_F(Population), SG_ARRAY(Price), SG_ARRAY(PropellantDemand), SG_ARRAY(PropellantSold),
		SG_F(Ration), SG_F(RebuildJd), SG_F(RuinedJd), SG_ARRAY(Satisfaction), SG_F(Site), SG_F(Staffing),
		SG_F(Stage), SG_ARRAY(Stock), SG_F(Strays), SG_ARRAY(Supply), SG_F(TradeVolumeCredits), SG_F(Unrest),
		SG_F(WageBill), SG_F(WageIndex), SG_F(WageTax), SG_F(Wellbeing))

	SG_SERIALIZE(Smuggling, "Unity.SpaceGod.Core.Smuggling",
		SG_F(desk), SG_F(rng))

	SG_SERIALIZE(StandingOrder, "Unity.SpaceGod.Core.StandingOrder",
		SG_F(Charge), SG_F(Claim), SG_F(Kind), SG_F(StopIndex), SG_ARRAY(Stops))

	SG_SERIALIZE(TradeDesk, "Unity.SpaceGod.Core.TradeDesk",
		SG_P(BestRank), SG_P(Rank), SG_F(Charters), SG_F(ChartersServed), SG_F(ClaimsPaid), SG_F(HireEarned),
		SG_F(Insurance), SG_F(Intel), SG_F(PremiumsPaid), SG_F(ReportsBought), SG_F(Smuggled), SG_F(Smuggling),
		SG_F(SmugglingTakings), SG_F(TimesCaught), SG_F(nextDailyJd), SG_F(notices), SG_F(world))

	SG_SERIALIZE(TradeInsurance, "Unity.SpaceGod.Core.TradeInsurance",
		SG_F(Policies), SG_F(desk), SG_F(nextId))

	SG_SERIALIZE(TradeRoute, "Unity.SpaceGod.Core.TradeRoute",
		SG_F(CrossSystem), SG_F(Days), SG_F(From), SG_F(LastJd), SG_F(Profit), SG_F(Runs), SG_F(To))

	SG_SERIALIZE(TugHaul, "Unity.SpaceGod.Core.TugHaul",
		SG_F(AccelMs2), SG_F(ArriveJd), SG_F(Capture), SG_F(DepartJd), SG_F(FreeJd), SG_F(FromBody), SG_F(HeavyLane),
		SG_F(Latched), SG_F(Load), SG_F(Name), SG_F(Operation), SG_F(State), SG_F(TakenBy), SG_F(ToBody),
		SG_F(ToDepot), SG_F(Tug))
}
