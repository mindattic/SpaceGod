// Not a C# file (US-T36): the ObjectGraph registrations (docs/PERSISTENCE.md) of the Core root types a
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
	SG_ENUM(BodyKind, "Unity.SpaceGod.Core.BodyKind")

	SG_ENUM(BodyStatus, "Unity.SpaceGod.Core.BodyStatus")

	SG_ENUM(LagrangePoint, "Unity.SpaceGod.Core.LagrangePoint")

	SG_ENUM(OrbitPlane, "Unity.SpaceGod.Core.OrbitPlane")

	SG_ENUM(ShipClass, "Unity.SpaceGod.Core.ShipClass")

	SG_ENUM(ShipPhase, "Unity.SpaceGod.Core.ShipPhase")

	SG_ENUM(ShipRole, "Unity.SpaceGod.Core.ShipRole")

	SG_ENUM(WorldEventKind, "Unity.SpaceGod.Core.WorldEventKind")

	SG_STRUCT(DVec3, "Unity.SpaceGod.Core.DVec3",
		SG_F(X), SG_F(Y), SG_F(Z))

	// Ephemeris::FrameBasis is private to Ephemeris: Ephemeris's registration (a friend of both) runs this one.
	template <> bool ::SpaceGod::Core::Graph::Registration<Ephemeris::FrameBasis>::Run()
	{
		using Self = Ephemeris::FrameBasis;
		auto b = ::SpaceGod::Core::Graph::Struct<Self>("Unity.SpaceGod.Core.Ephemeris+FrameBasis");
		::SpaceGod::Core::Graph::Fields(SG_F(x), SG_F(y), SG_F(z));
		return true;
	}

	SG_STRUCT(GalacticPosition, "Unity.SpaceGod.Core.GalacticPosition",
		SG_F(LocalKm), SG_F(X), SG_F(Y), SG_F(Z))

	SG_STRUCT(LaneHop, "Unity.SpaceGod.Core.LaneHop",
		SG_F(From), SG_F(Lane), SG_F(To))

	SG_STRUCT(Rgb, "Unity.SpaceGod.Core.Rgb",
		SG_F(B), SG_F(G), SG_F(R))

	SG_STRUCT(WorldEvent, "Unity.SpaceGod.Core.WorldEvent",
		SG_F(Body), SG_F(Jd), SG_F(Kind), SG_F(Text))

	SG_SERIALIZE(BodyCatalog, "Unity.SpaceGod.Core.BodyCatalog",
		SG_F(bodies), SG_F(byName), SG_ARRAY(children), SG_F(outerEdgeKm))

	SG_SERIALIZE(BodyDefinition, "Unity.SpaceGod.Core.BodyDefinition",
		SG_P(Index), SG_P(InnermostChildKm), SG_P(MoonReachKm), SG_P(ParentIndex), SG_P(Pole), SG_P(SoiKm),
		SG_P(SystemReachKm), SG_F(AtmosphereColor), SG_F(AtmosphereStrength), SG_F(CoOrbitalWith), SG_F(Color),
		SG_F(DiscoveryYear), SG_F(Elements), SG_F(FacesParent), SG_F(GasGiant), SG_F(Gm), SG_F(HabitableZone),
		SG_F(HabitatSite), SG_F(Kind), SG_F(LaplacePoleDecDeg), SG_F(LaplacePoleRaDeg), SG_F(LuminositySolar),
		SG_F(MapNorthFlipped), SG_F(MassIsMinimum), SG_F(Name), SG_F(NormalMap), SG_F(OrbitScale), SG_F(ParentName),
		SG_F(Plane), SG_F(PoleDecDeg), SG_F(PoleRaDeg), SG_F(PrimeMeridianDeg), SG_F(PrimeMeridianRateDegPerDay),
		SG_F(RadiusKm), SG_F(RingInnerKm), SG_F(RingOuterKm), SG_F(RotationHours), SG_F(Singularity),
		SG_F(SpectralType), SG_F(Status), SG_F(Summary), SG_F(SurfaceHaze), SG_F(TeffK), SG_F(Texture),
		SG_F(TexturePoleMargin), SG_F(TintTexture), SG_F(TrojanPoint))

	SG_SERIALIZE(ConicTransfer, "Unity.SpaceGod.Core.ConicTransfer",
		SG_F(ArrivalBurnDays), SG_F(ArrivalBurnKmS), SG_F(Capture), SG_F(CorrectionBurnKmS), SG_F(Days),
		SG_F(DepartureBurnDays), SG_F(DepartureBurnKmS), SG_F(EntryDays), SG_F(Escape), SG_F(ExitDays), SG_F(FromR0),
		SG_F(FromSoiKm), SG_F(FromV0), SG_F(Mu), SG_F(R0), SG_F(R1), SG_F(ToR1), SG_F(ToSoiKm), SG_F(ToV1), SG_F(V0),
		SG_F(V1))

	SG_SERIALIZE(Ephemeris, "Unity.SpaceGod.Core.Ephemeris",
		SG_P(Catalog), SG_P(Jd), SG_F(earthIndex), SG_ARRAY(frames), SG_ARRAY(helio), SG_F(lunarJd), SG_F(lunarPos),
		SG_ARRAY(memoHelio), SG_ARRAY(memoJd), SG_F(moonIndex), SG_F(moonMassFraction), SG_ARRAY(relative),
		SG_ARRAY(velocity), ::SpaceGod::Core::Graph::Registration<Ephemeris::FrameBasis>::Run())

	SG_SERIALIZE(Faction, "Unity.SpaceGod.Core.Faction",
		SG_P(Capital), SG_P(Color), SG_P(IsHouse), SG_P(IsMilitary), SG_P(IsPirate), SG_P(Kind), SG_P(MarineQuality),
		SG_P(Name), b.String("<Prefix>k__BackingField", &Self::Prefix), SG_F(Contraband), SG_F(CrewLost), SG_F(CrewMorale), SG_F(CrewRescued),
		SG_F(Manpower), SG_F(Treasury))

	SG_SERIALIZE(Fleet, "Unity.SpaceGod.Core.Fleet",
		SG_F(Arrived), SG_F(AvoidCollisions), SG_F(BeforeEvent), SG_F(BurnsPropellant), SG_F(CanRefuelAt),
		SG_F(ChooseNext), SG_F(Dwell), SG_F(ForceTopUp), SG_F(IsHeld), SG_F(Lands), SG_F(LaneJoined), SG_F(Lanes),
		SG_F(LeakChance), SG_F(Leaked), SG_F(OrbitalTransfers), SG_F(RanDry), SG_F(ReachedRing), SG_F(Rings),
		SG_F(Shuns), SG_F(TankerArrived), SG_F(TimeValue), SG_F(catalog), SG_F(coarse), SG_ARRAYVALUES(conicOptions),
		SG_F(ephemeris), SG_F(evadingFrom), SG_F(evasion), SG_F(factions), SG_F(helioCache), SG_F(lastAvoidanceJd),
		SG_F(leaks), SG_F(rng), SG_F(ships), SG_F(tankerAlongside))

	SG_SERIALIZE(Hyperbola, "Unity.SpaceGod.Core.Hyperbola",
		SG_F(A), SG_F(E), SG_F(Mu), SG_F(P), SG_F(Q), SG_F(Rp), SG_F(VInf))

	SG_SERIALIZE(Hyperlanes, "Unity.SpaceGod.Core.Hyperlanes",
		SG_F(Condition), SG_F(Danger), SG_F(GateSpurs), SG_F(Lanes), SG_F(cacheBucket), SG_F(catalog),
		SG_F(ephemeris), SG_F(lengthCache), SG_F(routeCache), SG_F(routeTrees))

	SG_SERIALIZE(Lane, "Unity.SpaceGod.Core.Lane",
		SG_F(A), SG_F(B), SG_F(Bend), SG_F(Gate), SG_F(Heavy), SG_F(Hub), SG_F(Id), SG_F(Name))

	SG_SERIALIZE(OrbitalElements, "Unity.SpaceGod.Core.OrbitalElements",
		SG_P(ADotKmPerCentury), SG_P(AKm), SG_P(E), SG_P(EDot), SG_P(EpochJd), SG_P(IDeg), SG_P(IDot), SG_P(LDeg),
		SG_P(LDotDegPerCentury), SG_P(LDotDotDegPerCentury2), SG_P(NodeDeg), SG_P(NodeDot), SG_P(VarpiDeg),
		SG_P(VarpiDot))

	SG_SERIALIZE(ParkingOrbit, "Unity.SpaceGod.Core.ParkingOrbit",
		SG_F(Body), SG_F(Center), SG_F(EpochJd), SG_F(Fixed), SG_F(PhaseAtEpoch), SG_F(RadiusKm),
		SG_F(RateRadPerDay), SG_F(U), SG_F(V))

	SG_SERIALIZE(Ship, "Unity.SpaceGod.Core.Ship",
		SG_F(AccelKmPerDay2), SG_F(ArrivalVelocity), SG_F(BandId), SG_F(Battle), SG_F(CapacityTonnes),
		SG_F(CaptainsHeld), SG_F(Cargo), SG_F(CargoBonus), SG_F(CargoBonusPayer), SG_F(CargoCost),
		SG_F(CargoDestination), SG_F(CargoGood), SG_F(CargoTonnes), SG_F(Civil), SG_F(Colony), SG_F(Combat),
		SG_F(Company), SG_F(CompanyBookedCredits), SG_F(Conic), SG_F(Convoy), SG_F(CorvetteStation), SG_F(Craft),
		SG_F(Credits), SG_F(DepartureOrbit), SG_F(DepartureVelocity), SG_F(Derelict), SG_F(DerelictBy),
		SG_F(DerelictSinceJd), SG_F(DestinationBody), SG_F(Destroyed), SG_F(Faction), SG_F(FinalBody),
		SG_F(FormationSlot), SG_F(FrameBody), SG_F(FtlCoolUntilJd), SG_F(GateDeparted), SG_F(GateKm),
		SG_F(GravityLossKmS), SG_F(HasCargo), SG_F(HoldPoint), SG_F(Homing), SG_F(Id), SG_NULLLIST(Journey),
		SG_F(JourneyIndex), SG_F(LaneEndProgress), SG_F(LaneFromOffset), SG_F(LaneHold), SG_F(LaneHoldSide),
		SG_F(LaneLeg), SG_F(LaneLengthKm), SG_F(LaneShape), SG_F(LaneStar), SG_F(LaneStartProgress),
		SG_F(LaneSurged), SG_F(LaneToOffset), SG_F(Leader), SG_F(LegPropellant), SG_F(LoadedCredits),
		SG_F(LoadedFrom), SG_F(LoadedJd), SG_F(Loadout), SG_F(LurkAt), SG_F(LurkDays), SG_F(LurkLane), SG_F(Name),
		SG_F(Orbit), SG_F(OriginBody), SG_F(OutOfPropellant), SG_F(OutOfPropellantSinceJd), SG_NULLARRAY(Path),
		SG_NULLARRAY(PathCumulative), SG_F(PathLength), SG_F(PatrolWing), SG_F(Phase), SG_F(PhaseEndJd),
		SG_F(PhaseStartJd), SG_F(PicketGate), SG_F(PicketPostKm), SG_F(PlannedConicDays), SG_F(PlannedConicDepartJd),
		SG_F(PlannedDirect), SG_NULLARRAY(Plunder), SG_NULLARRAY(Ports), SG_F(PrisonerBounty), SG_F(Prisoners), SG_F(Ring),
		SG_F(Role), SG_NULLARRAY(Route), SG_F(RouteIndex), SG_F(Runner), SG_F(Salvage), SG_F(Scrapped),
		SG_F(SilentRunning), SG_F(Spec), SG_F(StakeoutKm), SG_F(StakeoutUntilJd), SG_F(Stalled), SG_F(StrandJd),
		SG_F(StrandedInFlight), SG_F(Tow), SG_F(TripsCompleted), SG_F(Voyage), SG_F(WaitingForPropellant),
		SG_F(Wander), SG_F(personalHold), SG_F(propellant))

	SG_SERIALIZE(ShipClassSpec, "Unity.SpaceGod.Core.ShipClassSpec",
		SG_P(AccelG), SG_P(Class), SG_P(DisplayName), SG_P(IsMilitary))

	SG_SERIALIZE(SimClock, "Unity.SpaceGod.Core.SimClock",
		SG_P(Jd), SG_P(Speed), SG_P(WarpIndex), SG_F(paused), SG_F(pausing), SG_F(resuming))

	SG_SERIALIZE(World, "Unity.SpaceGod.Core.World",
		SG_P(Adventures), SG_P(Colonisation), SG_P(Companies), SG_P(Contracts), SG_P(Economy), SG_P(Ephemeris),
		SG_P(Escorts), SG_P(Fleet), SG_P(Holdings), SG_P(Infrastructure), SG_P(Jd), SG_P(LaneUpkeep), SG_P(Law),
		SG_P(Mining), SG_P(Outlaws), SG_P(Polities), SG_P(Pursuit), SG_P(Sensors), SG_P(Shocks), SG_P(Trade),
		SG_P(War), SG_F(Belt), SG_F(Events), SG_F(Gates), SG_F(HavenSites), SG_F(HunterCap), SG_F(Independents),
		SG_F(Logged), SG_F(MerchantCap), SG_F(MerchantTarget), SG_F(NavyCap), SG_F(NavyCapAtWar), SG_F(PilotedShip),
		SG_F(PirateGroupCap), SG_F(PirateIncomePerDay), SG_F(Pirates), SG_NULLARRAY(RaiderNames), SG_F(Reach),
		SG_F(ReachedGate), SG_F(RivalA), SG_F(RivalB), SG_F(ShipAdded), SG_F(Sol), SG_F(VoyageOrders),
		SG_F(builtCount), SG_F(captureTugsNamed), SG_ARRAY(merchantNames), SG_ARRAY(minerNames), SG_F(nextRecountJd),
		SG_F(tugsNamed), SG_F(worksEscortsRaised), SG_F(worksFreightersNamed))
}
