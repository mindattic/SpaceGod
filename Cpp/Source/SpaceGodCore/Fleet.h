#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Fleet.cs (see docs/MIGRATION.md).
//
// Shapes chosen where the table leaves room (keep them when porting a body against this header):
//  - C# value tuples are std::pair (two members: .first/.second, in the C# element order) or std::tuple (more);
//    dictionaries keyed by one hash them with Fleet::ValueTupleHash.
//  - Ship.Journey is a cs::List<LaneHop> held by value: C#'s null journey is the empty list (a journey is never an empty
//    non-null list, so `Journey != null` is `Journey.Count() > 0`). Hyperlanes.Route/RouteFor/HeavyRoute return
//    cs::Ref<cs::List<LaneHop>> (nullptr = null, MIGRATION.md ruling 1); a journey copies the route's hops.
//  - Ship.Path / PathCumulative (`DVec3[]` / `double[]`) are cs::List by value: `Path = null` is `Path = cs::List<DVec3>()`.
//  - Array / List parameters are taken by value (cs::List<T>), `params T[]` too; `string` is cs::String (null -> "").
//  - Class parameters are cs::Ref<T> by value; struct parameters by value; out / ref -> T&.

#include <tuple>
#include <utility>

#include "Cs/Cs.h"
#include "Astro.h"
#include "DVec3.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Ship;
	class Faction;
	class Ephemeris;
	class Infrastructure;
	class BodyCatalog;
	class Hyperlanes;
	class DockingRing;
	class ParkingOrbit;
	class ConicTransfer;
	struct ShipState;
	struct LaneHop;
	struct TripEstimate;
	struct Rgb;
	enum class ShipClass : int;
	enum class ShipRole : int;
	enum class DriveKind : int;

	/// <summary>
	/// Every ship in the system and its schedule. Ships alternate between parking in a circular
	/// orbit (real Keplerian rate for the body's GM) and brachistochrone transits: constant thrust
	/// toward the destination, flip at the midpoint, constant braking. Each transit is planned
	/// once at departure as an intercept against where the destination will be on arrival, so the
	/// whole schedule is deterministic and can be evaluated at any time. A hop within one planet's
	/// system is flown in that planet's frame, which carries the ship along the planet's orbit.
	/// </summary>
	class SPACEGODCORE_API Fleet final
	{
	private:
		// C++ only: hashes the value-tuple keys of helioCache and conicOptions (cs::Hash has no tuple support).
		struct ValueTupleHash
		{
			template <class A, class B> size_t operator()(const std::pair<A, B>& p) const
			{
				return cs::HashCombine(cs::Hash<A>{}(p.first), cs::Hash<B>{}(p.second));
			}
			template <class... T> size_t operator()(const std::tuple<T...>& t) const
			{
				size_t h = 0;
				std::apply([&](const auto&... v) { ((h = cs::HashCombine(h, cs::Hash<std::decay_t<decltype(v)>>{}(v))), ...); }, t);
				return h;
			}
		};

	public:
		const cs::List<cs::Ref<Ship>>& Ships() const { return ships; }
		const cs::List<cs::Ref<Faction>>& Factions() const { return factions; }

	private:
		cs::Ref<Ephemeris> ephemeris;
		cs::Ref<BodyCatalog> catalog;
		cs::List<cs::Ref<Ship>> ships = cs::List<cs::Ref<Ship>>();
		cs::List<cs::Ref<Faction>> factions = cs::List<cs::Ref<Faction>>();
		// mutable: Rng() is a (const) property method handing out the generator, as C# hands out the reference.
		mutable cs::Random rng;

	public:
		/// <summary>Transits are routed around the Sun at this distance.</summary>
		static constexpr double SunKeepOutKm = 0.15 * Astro::AuKm;

		/// <summary>Hops within a planet's system are routed around the planet at this many radii.</summary>
		static constexpr double PlanetKeepOutRadii = 2.0;

		/// <summary>A ship waits for its parking orbit to face the way out only if that takes less than this (days).</summary>
		static constexpr double MaxFacingWaitDays = 2.0;

		Fleet(cs::Ref<Ephemeris> ephemeris, int seed);

		// ---------------------------------------------------------------- simulation

		/// <summary>The hyperlane network; null for the classic all-brachistochrone fleet.</summary>
		cs::Ref<Hyperlanes> Lanes;

		/// <summary>Called with each event's date before it is processed, so the world can catch up to it.</summary>
		cs::Action<double> BeforeEvent;

		/// <summary>Called when a ship reaches its destination's parking orbit, before it picks the next one.</summary>
		cs::Action<cs::Ref<Ship>, double> Arrived;

		/// <summary>Called when a lander reaches a ring's queue: it is now in orbit there and can be fought (it trades on landing).</summary>
		cs::Action<cs::Ref<Ship>, double> ReachedRing;

		/// <summary>Picks a ship's next destination body (never the one it is at), or returns -1 for the built-in choice.</summary>
		cs::Func<int, cs::Ref<Ship>, int, double> ChooseNext;

		/// <summary>
		/// Worlds a free ship will not pick on a whim: a known haven or an armed port held by someone hostile to it,
		/// whose platforms would open on it each time it came (U.SPC-T291). Null for none.
		/// </summary>
		cs::Func<bool, cs::Ref<Ship>, int> Shuns;

		/// <summary>Overrides how long a ship lingers in port, given the default; null keeps the default.</summary>
		cs::Func<double, cs::Ref<Ship>, double> Dwell;

		/// <summary>Ships that run their own schedule (not escorts, not destroyed, not held by something else).</summary>
		cs::Func<bool, const cs::Ref<Ship>&> IsHeld; // const& (US-T42): called per ship per step

		/// <summary>Docking rings: ships that <see cref="Lands"/> reach a ringed world through one of its rings.</summary>
		cs::List<cs::Ref<DockingRing>> Rings = cs::List<cs::Ref<DockingRing>>();

		/// <summary>Which ships go down to the surface at ringed worlds (merchants); null for none.</summary>
		cs::Func<bool, const cs::Ref<Ship>&> Lands; // const& (US-T42): called per ship per step

		/// <summary>
		/// Which ships fly on real propellant (see <see cref="Propulsion"/>): every transit is charged its
		/// delta-v from the tanks by the rocket equation, a ship that cannot cover a trip waits in port for
		/// propellant, and one that runs dry in flight drifts until a tanker reaches it. Null for none (the
		/// classic fleet, whose drives never run out).
		/// </summary>
		cs::Func<bool, const cs::Ref<Ship>&> BurnsPropellant; // const& (US-T42): called per ship per step

		/// <summary>Whether a ship short of propellant for its next trip can wait at a body for its tanks to be filled (a port that sells it).</summary>
		cs::Func<bool, cs::Ref<Ship>, int> CanRefuelAt;
		/// <summary>Tops a ship up in port whatever its tank level, for a trip it is short of propellant for (U.SPC-T545).</summary>
		cs::Action<cs::Ref<Ship>, int, double> ForceTopUp;

		/// <summary>Raised when a ship runs dry: it drifts where its tanks gave out and calls for a tanker.</summary>
		cs::Action<cs::Ref<Ship>, double> RanDry;

		/// <summary>Raised when a tanker reaches a dry ship, with the tonnes it pumped across (0 if someone else already had).</summary>
		cs::Action<cs::Ref<Ship>, double, double> TankerArrived;

		/// <summary>Raised as a ship joins a hyperlane (where its toll is paid).</summary>
		cs::Action<cs::Ref<Ship>, LaneHop, double> LaneJoined;

		/// <summary>
		/// The chance a transit on propellant springs a leak (a micrometeoroid through a tank, a stuck valve)
		/// and vents most of what it carries, so it runs dry part way and drifts until a tanker comes. 0 for never.
		/// </summary>
		double LeakChance = 0;

		/// <summary>Raised when a transit springs a leak (see <see cref="LeakChance"/>), with the tonnes vented.</summary>
		cs::Action<cs::Ref<Ship>, double, double> Leaked;

	private:
		/// <summary>Rolls for leaks apart from the schedule's own dice, so turning them on changes nothing else.</summary>
		cs::Random leaks;

		bool Burns(cs::Ref<Ship> ship);

	public:
		/// <summary>Ships see each other coming and sidestep (see <see cref="StepAvoidance"/>).</summary>
		bool AvoidCollisions = true;

		/// <summary>
		/// Run at a coarse level of detail (U.SPC-T104): the fleet of a system nobody is looking at (see
		/// <see cref="Galaxy.Focus"/>). Its pilots do not sidestep one another (that is flying round each
		/// other for the eye, not the schedule), and a slow boat looks for its launch window on a coarser grid
		/// of dates and flight times. Every schedule, route, cargo and propellant charge is still the real
		/// one, so a fleet promoted back to full detail flies on from where it is without a jump.
		/// </summary>
		bool Coarse() const;
		void SetCoarse(bool value);

	private:
		bool coarse = false;

	public:
		/// <summary>How far ahead (days) ships look for a close pass: about an hour and a quarter.</summary>
		static constexpr double AvoidanceHorizonDays = 0.05;

		/// <summary>
		/// Where a hull flown from a helm really is right now (US-T151): it runs no schedule (see <see cref="IsHeld"/>), so the
		/// sidestepping in <see cref="StepAvoidance"/> cannot know its pose, yet every free ship must still keep clear of it.
		/// The app reports the hull it flies once a frame; a report goes stale after <see cref="HeldReportDays"/>, or as soon
		/// as the hull is no longer held. Not saved: the helm reports again on its first frame after a load.
		/// </summary>
		void ReportHeld(const cs::Ref<Ship>& ship, DVec3 helioKm, DVec3 velocityKmS, double jd);

		/// <summary>How long (days) a held hull's reported pose stands for it: about two minutes, extrapolated on its velocity.</summary>
		static constexpr double HeldReportDays = 0.0015;

	private:
		// (DVec3 offset, DVec3 velocity): .first is the offset, .second the velocity.
		cs::Dictionary<cs::Ref<Ship>, std::pair<DVec3, DVec3>> evasion = cs::Dictionary<cs::Ref<Ship>, std::pair<DVec3, DVec3>>();
		cs::Dictionary<cs::Ref<Ship>, cs::Ref<Ship>> evadingFrom = cs::Dictionary<cs::Ref<Ship>, cs::Ref<Ship>>();
		double lastAvoidanceJd = cs::Double::NaN;

		/// <summary>A held hull's last reported pose (US-T151, <see cref="ReportHeld"/>); transient, never saved.</summary>
		struct HeldPose
		{
			cs::Ref<Ship> Hull;
			DVec3 HelioKm;
			DVec3 VelocityKmPerDay;
			double Jd = 0;
		};
		cs::List<HeldPose> heldPoses;

	public:
		/// <summary>
		/// US-T209: no two hulls on schedules ever interpenetrate. The predictive sidestep above (<see cref="StepAvoidance"/>)
		/// turns ships aside smoothly, but it is limited by their drives and counts only true km, while fleet traffic is drawn
		/// through the scale model, where hardware is drawn at one linear scale (7 units/km, US-T179) in space whose km are
		/// compressed many times over (a 1 km hull spans ~140 km of low orbit and tens of thousands of km of open space). So
		/// each step, after the sidestep, every free hull is pushed clear of every other and of the stations: a stateless,
		/// deterministic relaxation of the positions the schedule (and sidestep) put them at that date (overlapping pairs only,
		/// found by sweep and prune; the smaller hull gives way more; never pushed down toward the world it is near; pairs in
		/// a fixed order), so both co-op peers compute the same offsets from the same state. Two solves: one at true size, part
		/// of <see cref="StateOf"/> (the sim), and one at the size hulls are drawn at <see cref="DrawnBlend"/>, which only
		/// <see cref="DrawnOffsetOf"/> adds (the view). Not saved: it is recomputed from the state on the next step.
		/// </summary>
		bool SeparateHulls = true;

		/// <summary>The scale blend the view draws at (<see cref="ScaleModel.Blend"/>: 0 realistic, 1 gameplay), for the drawn solve.</summary>
		double DrawnBlend = 1.0;

		/// <summary>
		/// US-T226: the hull the view is framed on (the one flown from the helm; null for none) and the way the camera looks at
		/// it (ecliptic axes). In the drawn solve a hull near it is kept clear not only of its sphere but off the camera's line
		/// through it, out to <see cref="ViewReach"/> times the clearance ahead of and behind it at the gameplay scale, pushed
		/// square to that line: at the gameplay scale a hull a few hundred km dead ahead is drawn a few hull lengths off the
		/// nose, and pushed straight on along the nose it sat in line with the camera, drawn over the helm's hull. View only
		/// (it moves nothing <see cref="StateOf"/> reports), so it may differ between co-op peers.
		/// </summary>
		cs::Ref<Ship> DrawnViewHull;
		DVec3 DrawnViewAxis;

		/// <summary>How far along the camera's line (in clearances) the drawn solve keeps hulls off it at the gameplay scale (US-T226).</summary>
		static constexpr double ViewReach = 3.0;

		/// <summary>Something solid that scheduled hulls keep clear of (a station or yard), at its true size and the body it orbits.</summary>
		struct HullObstacle
		{
			DVec3 HelioKm;
			double RadiusKm = 0;
			double LengthKm = 0;
			int Body = 0;
		};

		/// <summary>The stations and yards in the system at a date (true km), for <see cref="SeparateHulls"/>; null for none.</summary>
		cs::Func<cs::List<HullObstacle>, double> Obstacles;

		/// <summary>The orbital structures of <paramref name="infrastructure"/> as obstacles at <paramref name="jd"/> (what a world wires <see cref="Obstacles"/> to).</summary>
		static cs::List<HullObstacle> StructuresOf(const cs::Ref<Infrastructure>& infrastructure, const cs::Ref<Ephemeris>& ephemeris, double jd);

		/// <summary>Relaxation passes of the separation solve: a hull pushed out of one neighbour is checked against the rest.</summary>
		static constexpr int SeparationPasses = 6;

		/// <summary>Clearance kept beyond touching, as a share of the two hulls' radii.</summary>
		static constexpr double SeparationMargin = 0.1;

		/// <summary>Everything the view adds to a ship's scheduled position: its sidestep and both separations (true km).</summary>
		DVec3 DrawnOffsetOf(cs::Ref<Ship> ship);

	private:
		cs::Dictionary<cs::Ref<Ship>, DVec3> separation = cs::Dictionary<cs::Ref<Ship>, DVec3>();
		cs::Dictionary<cs::Ref<Ship>, DVec3> drawnSeparation = cs::Dictionary<cs::Ref<Ship>, DVec3>();

		/// <summary>Runs the two separation solves at <paramref name="jd"/> (see <see cref="SeparateHulls"/>).</summary>
		void StepSeparation(double jd);

		/// <summary>Whether a ship is a hull of its own on its schedule here, for the separation: not an escort (drawn at its leader's
		/// slot), not held, fighting, lost, derelict or down at a port.</summary>
		bool Separable(cs::Ref<Ship> s);

		/// <summary>Scene units per true km where a hull at <paramref name="helioKm"/> near <paramref name="body"/> is drawn at
		/// <paramref name="blend"/>: the smaller of the scale model's tangential and radial rates there (the tighter one).
		/// With <paramref name="tangential"/>, the tangential rate alone: the one the view draws a sidestep at (US-T226).</summary>
		double SceneUnitsPerKm(int body, DVec3 helioKm, double blend, bool tangential = false);

	public:
		/// <summary>How far (km) a ship has stepped aside from its planned path to keep clear of another (its true-size separation included).</summary>
		DVec3 EvasionOf(cs::Ref<Ship> ship);

		/// <summary>The ship it is giving way to right now, or null.</summary>
		cs::Ref<Ship> EvadingFrom(cs::Ref<Ship> ship);

		/// <summary>The closest two ships may pass (km): a margin plus a few hull lengths each.</summary>
		static double SafeDistanceKm(cs::Ref<Ship> a, cs::Ref<Ship> b);

		/// <summary>A ship at a body and able to fight there: holding orbit, or waiting at its ring.</summary>
		static bool IsAt(cs::Ref<Ship> ship, int body);

		/// <summary>
		/// Advances every ship's schedule up to <paramref name="jd"/>, processing departures and
		/// arrivals in date order across the whole fleet so that what one ship does (buying out a
		/// market, say) is seen by the next.
		/// </summary>
		void Update(double jd);

	private:
		/// <summary>Flying under the ship's own drive (in orbit, burning between worlds, holding off a lane or a ring): it can sidestep.</summary>
		static bool Manoeuvrable(cs::Ref<Ship> s);

		/// <summary>
		/// Collision avoidance. Each ship projects every other one's course an hour or so ahead (from
		/// where both are and how they are moving); if they would pass closer than
		/// <see cref="SafeDistanceKm"/>, both turn aside, away from each other along the line of the
		/// near miss, the smaller ship giving way more, and drift back onto their planned paths once
		/// clear. The schedule is untouched: this is the pilots flying round each other.
		/// </summary>
		void StepAvoidance(double jd);

		bool RunsSchedule(cs::Ref<Ship> ship);

	public:
		/// <summary>State at <paramref name="jd"/>, including any sidestep to keep clear of other ships; parked ships use the ephemeris' current frame, which must be at the same date.</summary>
		ShipState StateOf(cs::Ref<Ship> ship, double jd);

		/// <summary>
		/// The ship's heliocentric velocity (km/day) on its schedule, before any sidestep. A parked or
		/// ring-bound ship is placed in its body's cached frame, which holds still across the finite
		/// difference, so that body's own motion round the Sun is added back: without it every such
		/// ship looked tens of km/s off from a ship in flight beside it.
		/// </summary>
		DVec3 HelioVelocityOf(cs::Ref<Ship> ship, double jd);

	private:
		/// <summary>Where the ship's schedule puts it, before any sidestep.</summary>
		ShipState BaseStateOf(cs::Ref<Ship> ship, double jd);

		/// <summary>
		/// A ship on a transfer orbit (U.SPC-T95): falling along its conic, nose along its velocity, its
		/// drive lit only for the escape burn as it leaves and the capture burn as it arrives.
		/// </summary>
		ShipState ConicState(cs::Ref<Ship> s, double jd);

		/// <summary>
		/// A ship at a docking ring, in the body's turning frame: queued (easing in from its holding
		/// point to the ring over the wait), descending (straight down under the ring to the city,
		/// heating as it passes the entry interface), landed, or climbing back up.
		/// </summary>
		ShipState RingState(cs::Ref<Ship> s, double jd);

	public:
		/// <summary>Spacing of the queue up a ring's approach line, km per ship (one per clearance interval).</summary>
		static constexpr double QueueSpacingKm = 25;

	private:
		/// <summary>
		/// Where a queued ship waits (relative to the ringed body): in line up the approach path above
		/// the ring, one <see cref="QueueSpacingKm"/> per ship ahead of it, creeping down as each is
		/// cleared and reaching the ring as its own turn comes. It joins the line from wherever it
		/// arrived over the first quarter-hour.
		/// </summary>
		DVec3 QueuePosition(cs::Ref<Ship> s, double jd);

		/// <summary>Distance from the ringed body's centre during a descent or climb at fraction u.</summary>
		static double RingRadiusAt(cs::Ref<Ship> s, double u);

	public:
		/// <summary>Distance fraction along the lane at a date (eased in and out).</summary>
		static double LaneProgress(cs::Ref<Ship> s, double jd);

	private:
		/// <summary>
		/// Where a riding ship is: on its lane, plus whatever is left of a sideways offset it is easing
		/// back in from (after a lurk or an interdiction).
		/// </summary>
		DVec3 RidePoint(cs::Ref<Ship> s, double jd, double progress);

	public:
		/// <summary>How much of its sideways hold a ship rejoining a lane still has (1 → 0 over the first couple of hours).</summary>
		static double LaneRejoin(cs::Ref<Ship> s, double jd);

		/// <summary>Heliocentric position on the ship's lane at a date and distance fraction.</summary>
		DVec3 LanePoint(cs::Ref<Ship> s, double jd, double progress);

		/// <summary>
		/// Position relative to the ship's <see cref="Ship.FrameBody"/> at a fraction of its transit
		/// (by distance along the path, like <see cref="ShipState.Progress"/>).
		/// </summary>
		static DVec3 TransitOffset(cs::Ref<Ship> s, double progress);

		/// <summary>Position relative to the ship's <see cref="Ship.FrameBody"/> a time into its transit (days).</summary>
		static DVec3 TransitOffsetAt(cs::Ref<Ship> s, double tau);

	private:
		// A steady acceleration (v1 - v0) / T on top of the flip-and-burn takes the ship from its
		// departure velocity to its arrival velocity; its displacement is v0·t + (v1 - v0)·t² / 2T.
		static DVec3 Drift(cs::Ref<Ship> s, double tau);

		static DVec3 DriftVelocity(cs::Ref<Ship> s, double tau);

		static DVec3 DriftAcceleration(cs::Ref<Ship> s);

	public:
		/// <summary>
		/// Position (relative to the ship's <see cref="Ship.FrameBody"/>, before drift) and travel
		/// direction at a distance along a transiting ship's flip-and-burn path.
		/// </summary>
		static void PointAtDistance(cs::Ref<Ship> s, double dist, DVec3& pos, DVec3& dir);

		/// <summary>Time remaining in the current phase, days.</summary>
		static double RemainingDays(cs::Ref<Ship> ship, double jd);

	private:
		/// <summary>When the ship's schedule next needs attention: the end of its phase, or the moment its tanks run dry part way through a transit.</summary>
		static double NextEventJd(cs::Ref<Ship> s);

		void AdvancePhase(cs::Ref<Ship> ship);

		// ---------------------------------------------------------------- docking rings

		/// <summary>The way a ship arrives at a body: in orbit, or to the holding point of the ring with the shortest queue.</summary>
		cs::Ref<ParkingOrbit> ArrivalFor(cs::Ref<Ship> ship, int body);

	public:
		/// <summary>Minimum time from the holding point to the ring (an unhurried approach).</summary>
		static constexpr double MinQueueDays = 12.0 / 1440;

	private:
		/// <summary>Arrived off the ring: take the next free slot to descend (rings clear one ship per interval).</summary>
		void JoinQueue(cs::Ref<Ship> ship);

		void BeginDescent(cs::Ref<Ship> ship);

		void BeginAscent(cs::Ref<Ship> ship);

		/// <summary>Days a ship landing at a ringed world adds for the queue, the descent and the climb out.</summary>
		double RingOverheadDays(cs::Ref<Ship> ship, int body, double arriveJd);

		/// <summary>
		/// Leaves port. A trip to another planetary system goes by hyperlane when there is one: first
		/// a burn to the first lane's ramp; otherwise (or locally) one burn straight to the destination.
		/// </summary>
		void Depart(cs::Ref<Ship> ship);

		/// <summary>How near a gate a hull must be to count as at it, to set out along its spur, km.</summary>
		static constexpr double AtGateKm = 100000;

		/// <summary>
		/// The lanes a hull on a plotted course takes across a system to <paramref name="gate"/> (U.SPC-T516): out along the spur
		/// of the gate it has just come through (or from its planetary system), across the network between, and in along the
		/// spur of the gate it is bound for. False, leaving no journey, where no lane runs to that gate (a frontier system, a Void
		/// Base) or the network does not join, and the hull flies straight across as any ship bound for a gate does.
		/// </summary>
		bool TryGateJourney(cs::Ref<Ship> ship, int from, DVec3 gate, double jd);

		/// <summary>
		/// A spot out in open space near a world for a free ship to drift in: a slow orbit at any tilt,
		/// anywhere from three radii out to well past its moons (but inside the world's neighbourhood).
		/// </summary>
		cs::Ref<ParkingOrbit> WanderOrbit(int body, cs::Ref<Ship> ship);

		/// <summary>A hyperlane ramp as a fixed point attached to its planet.</summary>
		cs::Ref<ParkingOrbit> RampOrbit(LaneHop hop, int system, double jd);

		static cs::Ref<ParkingOrbit> FixedPoint(int body, DVec3 offset);

		/// <summary>
		/// At the ramp the burn has brought it to: joins the lane, or, when the lane has failed since the route was planned
		/// (U.SPC-T481), goes round it by what is left of the network, or with no way round, on under its own drive.
		/// </summary>
		void ReachRamp(cs::Ref<Ship> ship, double jd);

	public:
		/// <summary>
		/// A lane's rings slip out of alignment under a rider (U.SPC-T481, see <see cref="Hyperlanes.Reliability"/>): the ride
		/// stalls <paramref name="at"/> of the way along, the hull easing to a stop just off the lane, and picks up again
		/// <paramref name="days"/> later, from there. Only a ride just joined, not one already part-way or held at the helm.
		/// </summary>
		bool StallRide(cs::Ref<Ship> ship, double at, double days);

	private:
		/// <summary>Joins the lane at its ramp (where the burn has just brought the ship).</summary>
		void StartLane(cs::Ref<Ship> ship, double jd);

	public:
		/// <summary>Back into the lane from where the ship stopped (a lurk over, or an ambush survived), and on to its end.</summary>
		void ResumeLane(cs::Ref<Ship> ship, double jd);

		/// <summary>Drops out of the lane where the ship is, a little to one side (an interdiction pulse).</summary>
		void PullOut(cs::Ref<Ship> ship, double jd, double progress, DVec3 side);

		/// <summary>A sideways offset (km, ecliptic) from a lane at a point, to wait just outside it.</summary>
		DVec3 OffLane(cs::Ref<Ship> s, double jd, double progress, double km, int sign);

	private:
		/// <summary>Leaves the lane at the far ramp: on to the next lane's ramp, or the last burn home.</summary>
		void EndLane(cs::Ref<Ship> ship);

	public:
		// ---------------------------------------------------------------- the player's own rides (U.SPC-T41)

		/// <summary>
		/// A piloted hull flown in through an on-ramp's ring gate joins the lane where it is, by the same
		/// <see cref="StartLane"/> AI traffic rides: a one-hop journey ending at the far ramp. The hull is
		/// held (<see cref="IsHeld"/>), so its pilot brings it off there with <see cref="RampReached"/>; let
		/// go of mid-ride, the fleet carries it on to that system like any other ship.
		/// </summary>
		void JoinLane(cs::Ref<Ship> ship, LaneHop hop, DVec3 helioKm, double jd);

	private:
		/// <summary>
		/// Where a ride's crew make for if its pilot lets go of it: the far ramp's system, or, on a spur to a jump gate
		/// (U.SPC-T379), which AI crews do not take on their own, back to the planetary system the spur leaves from.
		/// </summary>
		static int RideDestination(LaneHop hop);

	public:
		/// <summary>
		/// The ring chain as a rider sees it (U.SPC-T379): how many rings its lane strings (<see cref="Hyperlanes.RingCount"/>),
		/// how many it has flown through, and whether they are speeding it up or braking it for the far ramp.
		/// </summary>
		static int RideRings(cs::Ref<Ship> ship, double jd, int& passed, bool& accelerating);

		/// <summary>Whether a ride has reached the far ramp (a held rider waits there for its pilot's call).</summary>
		static bool RideOver(cs::Ref<Ship> ship, double jd);

		/// <summary>
		/// A piloted ride has reached its far ramp: carry on along <paramref name="next"/> (one of
		/// <see cref="Hyperlanes.OnwardHops"/>) from right where the hull is, or, with none, come off and be
		/// pinned there under the pilot's hand, facing <paramref name="facing"/>.
		/// </summary>
		void RampReached(cs::Ref<Ship> ship, cs::Nullable<LaneHop> next, DVec3 facing, double jd);

		// ---------------------------------------------------------------- the player's answers to an interdiction (U.SPC-T42)

		/// <summary>How many times the lane's pace a surging rider goes (<see cref="SurgeRide"/>).</summary>
		static constexpr double SurgeFactor = 2.5;

		/// <summary>How far from the lane line, km, a hull out of it (dropped out, or pulled out and fought clear) can still get back in where it left.</summary>
		static constexpr double RejoinKm = 100000;

		/// <summary>
		/// A pilot boosting through: the rest of the ride goes at <see cref="SurgeFactor"/> times its pace, from right where
		/// the hull is (its progress along the lane does not jump), so it reaches every point ahead, and its off-ramp, sooner.
		/// Once a ride; false if the hull is not riding or has already surged.
		/// </summary>
		static bool SurgeRide(cs::Ref<Ship> ship, double jd);

		/// <summary>
		/// A pilot drops out of the lane early, on their own terms: off it right where the hull is, pinned there in the
		/// system root's frame facing <paramref name="facing"/>, the point it left kept (<see cref="Ship.LaneHold"/>) so it
		/// can get back in there (<see cref="RejoinLane"/>).
		/// </summary>
		void DropOut(cs::Ref<Ship> ship, double jd, DVec3 facing);

		/// <summary>Where a hull out of its lane would get back in: the lane line at the point it left, true heliocentric km.</summary>
		DVec3 RejoinPoint(cs::Ref<Ship> ship, double jd);

		/// <summary>Whether a hull at <paramref name="helioKm"/> that left its lane part-way can get back in where it left.</summary>
		bool CanRejoin(cs::Ref<Ship> ship, DVec3 helioKm, double jd);

		/// <summary>
		/// Back into the lane a hull left part-way (dropped out, or pulled out by a pulse and fought clear): it eases in from
		/// where it is (<paramref name="helioKm"/>) to the point it left and rides on from there to the far ramp, like a
		/// ship that survived an ambush (<see cref="ResumeLane"/>). False if it is not near enough (<see cref="CanRejoin"/>).
		/// </summary>
		bool RejoinLane(cs::Ref<Ship> ship, DVec3 helioKm, double jd);

		/// <summary>
		/// Changes course mid-flight: from wherever the ship is now, still moving as it is, a new
		/// flip-and-burn to <paramref name="to"/>. For a burn between worlds (not a hyperlane ride).
		/// Returns false if the ship is not on such a burn.
		/// </summary>
		bool Divert(cs::Ref<Ship> ship, int to, double jd);

	private:
		/// <summary>A flip-and-burn from the ship's current orbit (or ramp) to <paramref name="to"/>, arriving in the orbit the factory gives.</summary>
		void Burn(cs::Ref<Ship> ship, double departJd, int to, cs::Func<cs::Ref<ParkingOrbit>> arrivalOrbit,
			cs::Nullable<DVec3> initialVelocity = std::nullopt, bool allowConic = false);

		// ---------------------------------------------------------------- propellant

		/// <summary>
		/// Takes the transit just planned out of the tanks: its delta-v (<see cref="Propulsion.TransitDeltaV"/>)
		/// at the ship's present mass, by the rocket equation. If the tanks cannot cover it all, the ship
		/// sets out anyway and its drive cuts out when they run dry (<see cref="Ship.StrandJd"/>): the burn
		/// is constant thrust, so that is the same share of the way through in time as of the delta-v.
		/// </summary>
		void ChargePropellant(cs::Ref<Ship> ship);

		/// <summary>Gives back the propellant of the part of a transit that will not now be flown (a change of course).</summary>
		void RefundUnflown(cs::Ref<Ship> ship, double jd);

		/// <summary>Delta-v a ship on <paramref name="conic"/> has burned by <paramref name="jd"/>: the departure burn, the
		/// handoff trims spread over the coast, then the capture burn, km/s.</summary>
		static double ConicDeltaVSpent(cs::Ref<Ship> ship, cs::Ref<ConicTransfer> conic, double jd);

		static double Share(double t, double from, double to);

		/// <summary>
		/// The tanks have run dry part way through a transit: the ship drifts dead where the drive cut out,
		/// fixed in the transit's frame, still bound where it was going, and calls for a tanker
		/// (<see cref="RanDry"/>), which reaches it in a couple of days.
		/// </summary>
		void Strand(cs::Ref<Ship> ship, double jd);

	public:
		/// <summary>Calls a tanker out to a ship that cannot move for want of propellant: it waits where it is until one comes.</summary>
		void CallTanker(cs::Ref<Ship> ship, double jd);

	private:
		/// <summary>
		/// The tanker is alongside (or a passing pilot answered the call first): it fills the ship's tanks
		/// and the ship carries on to where it was bound.
		/// </summary>
		void TankerArrives(cs::Ref<Ship> ship);

		/// <summary>Set while a ship a tanker has reached plans the rest of its leg (see <see cref="ChargePropellant"/>).</summary>
		bool tankerAlongside = false;

		/// <summary>Back under way from wherever the ship is: the rest of the leg it was flying (to its next ramp, or its destination).</summary>
		void Resume(cs::Ref<Ship> ship, double jd);

		void ResumeLeg(cs::Ref<Ship> ship, double jd, bool inFlight);

	public:
		/// <summary>Whether a ship flying on propellant lacks what its drive needs to get from <paramref name="from"/> to <paramref name="to"/>.</summary>
		bool ShortOfPropellant(cs::Ref<Ship> ship, int from, int to, double jd);

	private:
		/// <summary>
		/// How wide a transit flown in a frame gives its centre: <see cref="SunKeepOutKm"/> for a star
		/// (scaled for its luminosity, so Proxima's close planets stay reachable), a couple of radii
		/// for a planet, nothing for an empty barycentre (its stars are steered round instead).
		/// </summary>
		double FrameKeepOutKm(int frame);

		/// <summary>
		/// Plans the transit onto <paramref name="ship"/>. Returns false if the path would pass inside
		/// the keep-out around the frame's centre, or through either end's body or the planets they orbit.
		/// </summary>
		bool Plan(cs::Ref<Ship> ship, int frame, int from, int to, double departJd, DVec3 start, DVec3 startVelocity,
			cs::Ref<ParkingOrbit> arrival, double keepOut, bool matchVelocity);

		/// <summary>Samples a planned transit against the frame's keep-out and the bodies near its ends.</summary>
		bool Clear(cs::Ref<Ship> ship, int frame, int from, int to, double keepOut);

		/// <summary>Closest approach to the origin of the segment from <paramref name="a"/> to <paramref name="b"/>.</summary>
		static double SegmentDistance(DVec3 a, DVec3 b);

		void Arrive(cs::Ref<Ship> ship, bool landed = false);

	public:
		/// <summary>
		/// Holds a hull fixed at <paramref name="offsetKm"/> in <paramref name="body"/>'s frame, nose on
		/// <paramref name="forward"/>, as of <paramref name="jd"/>: where a pilot let go of it, the raiders cast it
		/// off, or its captors took it. Whatever trip it had planned before is forgotten -- its departure is due
		/// now and it is bound nowhere but here -- so the next <see cref="ScheduleNext"/> plans afresh from this
		/// spot (for its new owner, if it changed hands) rather than the hull sitting frozen until the old plan's
		/// departure and then setting off on it (U.SPC-T178).
		/// </summary>
		static void PinAt(cs::Ref<Ship> ship, int body, DVec3 offsetKm, DVec3 forward, double jd);

		/// <summary>Picks the next destination and the departure time: dwell, then wait for the parking orbit to face the target.</summary>
		void ScheduleNext(cs::Ref<Ship> ship, double nowJd);

	private:
		/// <summary>A queued ship's current spot, frozen into a new holding point to ease in from.</summary>
		cs::Ref<ParkingOrbit> FixedHold(cs::Ref<Ship> ship, double jd);

		int ChooseDestination(cs::Ref<Ship> ship, int here, double jd);

		/// <summary>Somewhere a free ship (or a taxi, or someone's own craft) fancies, mostly close: weight
		/// 1 / (0.05 AU + distance)², so neighbouring moons and planets come up far more often than the far side of the
		/// system. Never a place it shuns; with nowhere else, it stays put.</summary>
		int Whim(cs::Ref<Ship> ship, int here, double jd);

		double DwellDays(cs::Ref<Ship> ship);

		cs::Ref<ParkingOrbit> NewParkingOrbit(int body, cs::Ref<Ship> ship);

		double Jitter(double amount);

		/// <summary>
		/// The frame a transit is flown in: the lowest body that both ends orbit within (or are), so
		/// Earth for Earth to the Moon, Jupiter for Io to Europa, the Sun between planetary systems.
		/// </summary>
		int CommonFrame(int a, int b);

		DVec3 RelativeTo(int frame, int body, double jd);

		// Planning asks where the same bodies are at the same instants over and over (each Lambert try,
		// each intercept bisection): remember the last few thousand answers.
		cs::Dictionary<std::pair<int, double>, DVec3, ValueTupleHash> helioCache = cs::Dictionary<std::pair<int, double>, DVec3, ValueTupleHash>();

		DVec3 HelioAt(int body, double jd);

		/// <summary>Velocity of <paramref name="body"/> relative to <paramref name="frame"/>, km/day (central difference).</summary>
		DVec3 VelocityRelativeTo(int frame, int body, double jd);

		/// <summary>
		/// The planets that the ends' moons orbit, as spheres to steer around, in path coordinates
		/// (the arrival side is shifted like the path's end, since drift is added on top of the path).
		/// </summary>
		/// (DVec3 center, double radius): .first is the centre, .second the radius.
		cs::List<std::pair<DVec3, double>> Avoidances(int frame, int from, int to, double departJd, double arriveJd, DVec3 shift);

		/// <summary>True if <paramref name="body"/> is <paramref name="end"/> or one of the bodies it orbits.</summary>
		bool OnChain(int body, int end);

		/// <summary>
		/// How wide a ship leaving or joining a moon gives its planet: a good part of the moon system,
		/// so it never skims the world, but inside the moon's own orbit so the moon stays reachable.
		/// </summary>
		double Berth(int planet, int moon, double jd);

		/// <summary>
		/// Straight line, or a dog-leg around the frame's centre when the line would pass within
		/// <paramref name="keepOutKm"/>, then detours around any of <paramref name="avoid"/> a leg runs through.
		/// </summary>
		static cs::List<DVec3> BuildPath(DVec3 start, DVec3 end, double keepOutKm, cs::List<std::pair<DVec3, double>> avoid = cs::List<std::pair<DVec3, double>>());

		static double PolylineLength(cs::List<DVec3> p);

		static cs::List<double> Cumulative(cs::List<DVec3> p);

		cs::String CargoFor(cs::Ref<Ship> ship, int origin);

		cs::String Pick(cs::List<cs::String> options);

	public:
		// ---------------------------------------------------------------- setup

		cs::Ref<Faction> AddFaction(cs::String name, cs::String prefix, Rgb color);

		/// <summary>Gives a faction that already exists (an interstellar polity, see <see cref="Polities"/>) a presence in this system.</summary>
		cs::Ref<Faction> AddFaction(cs::Ref<Faction> f);

		cs::Ref<Ship> AddShip(cs::String name, ShipClass c, cs::Ref<Faction> faction, ShipRole role);
		/// <summary>US-T215: strips a doubled faction prefix (an escort named from its leader's already-prefixed name) from every hull's name; for saves made before the fix.</summary>
		void NormaliseNames();

		/// <summary>
		/// Adds a ship built elsewhere than <see cref="AddShip"/> straight into this fleet's list: a jump gate's
		/// arrival or a cruise dropping out (see <see cref="GateTransit"/>, <see cref="LetGo"/>) -- the same hull
		/// that left another fleet's list, not a copy (U.SPC-T453), since every other ship's id there is its fixed
		/// index into that list (ships are never removed, only marked <see cref="Ship.Destroyed"/> in place) --
		/// shuffling an existing one out from the middle would desync every id after it.
		/// </summary>
		void Receive(cs::Ref<Ship> ship);

		/// <summary>
		/// Whether <paramref name="ship"/> itself still sits in this very fleet's list at its own id (not a gone
		/// marker that has taken its slot, see <see cref="LetGo"/>): a tracker outside the fleet (an outlaw's
		/// captain, a sensor book) that holds a ship reference across a long game should check this before reading
		/// anything this fleet's own catalogue and ephemeris give it a position with -- once it is carried on
		/// elsewhere (U.SPC-T453), that position belongs to a different system's fleet, never this one's.
		/// </summary>
		bool IsPresent(cs::Ref<Ship> ship);

		/// <summary>
		/// Takes <paramref name="ship"/> itself out of this fleet to go on somewhere else, the same object (U.SPC-T417: a
		/// hull jumping out under its own FTL drive is carried, not copied). Its slot keeps a marker of it, gone
		/// (<see cref="Ship.Tombstone"/>), so no other id here moves; escorts it leaves behind follow the marker, so the
		/// fleet's own succession picks a new leader among them as if it had gone through a gate. Its bookings here end.
		/// Returns the marker, or null if it is not in this fleet.
		/// </summary>
		cs::Ref<Ship> LetGo(cs::Ref<Ship> ship);

		/// <summary>
		/// Creates a ship of a class/design (see <see cref="AddShip"/>) and parks it at an exact
		/// body-relative offset facing <paramref name="forward"/>, for a dev/sandbox spawn rather than
		/// a real yard -- parked the same way a jump gate's arrival parks the hull at the far gate
		/// (see <see cref="GateTransit.TryPerform"/>). Free role with every visible body as a port, so
		/// it behaves like any independent ship if ever released from piloting.
		/// </summary>
		cs::Ref<Ship> SpawnAt(cs::String name, ShipClass c, cs::Ref<Faction> faction, int frameBody, DVec3 offsetKm, DVec3 forward, double jd);

		void AddEscort(cs::Ref<Ship> leader, cs::Ref<Ship> escort, DVec3 slot);

		/// <summary>Parks a ship at a body with a randomised departure so a fresh fleet does not move in lockstep.</summary>
		void Station(cs::Ref<Ship> ship, int body, double jd);

		/// <summary>As <see cref="Station(Ship, int, double)"/>, in a given orbit (a new hull leaving its yard).</summary>
		void Station(cs::Ref<Ship> ship, int body, double jd, cs::Ref<ParkingOrbit> orbit);

	private:
		int Body(cs::String name);

		cs::List<int> Bodies(cs::List<cs::String> names);

	public:
		/// <summary>
		/// The default traffic: three navies patrolling with carrier and battleship groups, plus
		/// freighters, ore haulers and couriers working the ports. The schedule is pre-rolled for
		/// a few weeks so ships start spread along their routes instead of all parked.
		/// </summary>
		static cs::Ref<Fleet> CreateDefault(cs::Ref<Ephemeris> ephemeris, double jd, int seed = 1977);

		static DVec3 SlotFor(int index);

	private:
		static const cs::List<DVec3>& EscortSlots();

	public:
		/// <summary>
		/// Rough flight time between two bodies for planning: the brachistochrone over their current
		/// separation, plus a day of manoeuvring.
		/// </summary>
		double EstimateTravelDays(cs::Ref<Ship> ship, int from, int to, double jd);

		/// <summary>A planned burn's delta-v runs this much over the straight flip-and-burn (dog-legs round worlds), plus <see cref="LegMatchKmS"/>.</summary>
		static constexpr double LegMargin = 1.2;

		/// <summary>Delta-v a planner allows per burn for matching velocity with its parking orbits and ramps, km/s.</summary>
		static constexpr double LegMatchKmS = 3;

		/// <summary>
		/// A trip as a planner sees it: the days of <see cref="EstimateTravelDays"/>, plus the delta-v its own
		/// drive supplies (each burn's 2·√(a·d) over the straight distance, with a margin for dog-legs and
		/// velocity matching) and the lanes it rides. Hyperlane rides cost no delta-v; they are tolled instead.
		/// </summary>
		TripEstimate EstimateTrip(cs::Ref<Ship> ship, int from, int to, double jd);

		/// <summary>
		/// As <see cref="EstimateTrip(Ship, int, int, double)"/>, and whether a slow boat would rather fly
		/// straight across on a transfer orbit than pay the lanes' tolls (U.SPC-T95). With
		/// <see cref="OrbitalTransfers"/> on, each burn also carries its gravity correction, and a
		/// low-thrust hauler prices each leg as the cheaper of the flip-and-burn and a transfer orbit
		/// (its launch window and flight time against its propellant, at the hull's time value).
		/// </summary>
		TripEstimate EstimateTrip(cs::Ref<Ship> ship, int from, int to, double jd, bool& direct);

		// ---------------------------------------------------------------- transfer orbits (U.SPC-T95)

		/// <summary>
		/// Real orbital mechanics for traffic (U.SPC-T95). On, every ship flies the trajectory its drive
		/// suits: a torch keeps its flip-and-burn and pays a gravity correction for holding its line
		/// through the wells at either end and the frame's own; a low-thrust hauler weighs that against a
		/// transfer orbit under the frame's gravity (a Lambert conic, patched onto escape and capture
		/// hyperbolas at worlds with a sphere of influence) and flies whichever costs its owner less,
		/// waiting in port for a launch window when that pays. Off, the classic all-flip-and-burn fleet.
		/// </summary>
		bool OrbitalTransfers = false;

		/// <summary>A drive of at least this (g) is a torch: it flies flip-and-burns everywhere.</summary>
		static constexpr double TorchAccelG = 0.25;

		/// <summary>The longest a slow boat will wait in port for a launch window, days.</summary>
		static constexpr double MaxWindowWaitDays = 800;

		/// <summary>How many times the ship's distance from a world its sphere of influence must reach for the trip to be flown as a patched conic.</summary>
		static constexpr double PatchFactor = 2;

		/// <summary>Cargo under a supply contract is wanted urgently: its hull's time is worth this many times more.</summary>
		static constexpr double UrgentCargoFactor = 4;

	private:
		/// <summary>Flight times a slow boat tries, as fractions of the leg's Hohmann time.</summary>
		static const cs::List<double>& TransferTimeFactors();

		/// <summary>The fewer flight times a window search first sweeps its whole span with.</summary>
		static const cs::List<double>& CoarseTimeFactors();

	public:
		static DriveKind DriveOf(cs::Ref<Ship> ship);

	private:
		// An ore tug works to its operation's timetable, laden or not (U.SPC-T498): it never waits on a launch window.
		bool UsesConics(cs::Ref<Ship> ship);

		// Nor does a navy's supply ship (U.SPC-T525), at its base or on its way to a war fleet that is waiting on it.
		static bool NavySupply(cs::Ref<Ship> ship);

	public:
		/// <summary>
		/// What a day of the hull's time is worth to its owner (credits) when it trades time for propellant:
		/// its crew's days (<see cref="Economy.RunningCostPerDay"/>), what its cargo cost (money tied up on
		/// the way, 0.3% a day), and <see cref="UrgentCargoFactor"/> times as much for contracted supplies.
		/// An empty or cheaply laden hauler's time is worth little, so it takes the slow boat.
		/// </summary>
		static double TimeValuePerDay(cs::Ref<Ship> ship);

		/// <summary>Overrides what a day of a hull's time is worth to its planner; null for <see cref="TimeValuePerDay"/>.</summary>
		cs::Func<double, cs::Ref<Ship>> TimeValue;

	private:
		double TimeValueOf(cs::Ref<Ship> ship);

	public:
		/// <summary>What a leg costs the ship's owner: its days at its time value and its propellant at the going rate.</summary>
		double LegCost(cs::Ref<Ship> ship, double days, double deltaVKmS);

	private:
		/// <summary>The frame's gravitational parameter in km³/day².</summary>
		double FrameMu(int frame);

		/// <summary>The pole a frame's transfers go round prograde about: the ecliptic's for a star, the body's spin axis otherwise.</summary>
		DVec3 FrameNormal(int frame);

		/// <summary>
		/// The world whose well a ship at <paramref name="body"/> climbs out of to fly free in
		/// <paramref name="frame"/>: the body on its chain that orbits the frame directly (Earth for the
		/// Moon in the Sun's frame), or -1 if it is the frame itself.
		/// </summary>
		int WellOf(int frame, int body);

		/// <summary>A world's sphere of influence against the frame it orbits, km, where it is now.</summary>
		double SoiKm(int frame, int well, double jd);

		/// <summary>The parking orbit radius a planner assumes at a world it has not yet reached, km.</summary>
		double NominalParkingKm(int body);

		/// <summary>A parking orbit the planner assumes at a world (equatorial, at <see cref="NominalParkingKm"/>), for weighing windows.</summary>
		cs::Ref<ParkingOrbit> NominalOrbit(int body);

		/// <summary>
		/// Solves the transfer orbit from <paramref name="departure"/> (at <paramref name="from"/>) to
		/// <paramref name="arrival"/> (at <paramref name="to"/>) leaving at <paramref name="departJd"/> and
		/// taking <paramref name="days"/>, in <paramref name="frame"/>: a Lambert conic between the ends,
		/// patched onto an escape hyperbola where the ship leaves a world whose sphere of influence is well
		/// outside its orbit (and a capture hyperbola where it arrives at one), and the burns at each end.
		/// A free (non-fixed) arrival orbit is phased so the ship circularises where the capture leaves it.
		/// Returns null when there is no such orbit, or its legs inside the spheres would overlap.
		/// </summary>
		cs::Ref<ConicTransfer> SolveConic(cs::Ref<Ship> ship, int frame, int from, int to, double departJd, double days,
			cs::Ref<ParkingOrbit> departure, cs::Ref<ParkingOrbit> arrival);

		/// <summary>The Hohmann time between the ends of a leg about its frame, days (the yardstick for the flight times tried).</summary>
		double HohmannDaysFor(int frame, int from, int to, double jd, cs::Ref<ParkingOrbit> departure, cs::Ref<ParkingOrbit> arrival);

		/// <summary>
		/// Weighs a transfer orbit against the flip-and-burn just planned onto <paramref name="ship"/>, leaving
		/// now: the flight times it tries (and the one its window was planned for), each priced at
		/// <see cref="LegCost"/>; if the cheapest beats the flip-and-burn and keeps clear of everything, it
		/// becomes the transit and its arrival orbit is returned. Otherwise the flip-and-burn stands (null).
		/// </summary>
		cs::Ref<ParkingOrbit> TryConic(cs::Ref<Ship> ship, int frame, int from, int to, double departJd,
			cs::Func<cs::Ref<ParkingOrbit>> arrivalOrbit, double keepOut);

		/// <summary>
		/// Sets a slow boat's departure for its next trip (U.SPC-T95): over the coming synodic period
		/// (capped at <see cref="MaxWindowWaitDays"/>, and at however long waiting would cost more than
		/// flying now) it tries leaving on each of a grid of dates with each of a range of flight times,
		/// and keeps the cheapest wait plus transfer. If that beats a flip-and-burn now, it waits in port for
		/// that window and records the plan; otherwise it returns false and leaves as usual.
		/// </summary>
		bool PlanWindow(cs::Ref<Ship> ship, int here, int next, double earliestJd, double& departJd);

		/// <summary>Mean motion (rad/day) of a world about the frame, from its distance now.</summary>
		double MeanMotion(int frame, int well, double jd);

		/// <summary>
		/// One end of a leg as the trip estimate sees it: a world going round the frame (whose well the
		/// ship climbs out of or falls into, from a parking orbit <see cref="Rp"/> out), or a point at
		/// <see cref="R"/> from the frame's centre, held still (a hyperlane ramp) or in circular orbit.
		/// </summary>
		struct LegEnd
		{
			int Well = 0;
			double R = 0, Rp = 0;
			bool Still = false, Patched = false;

			LegEnd() = default;
			LegEnd(int well, double r, double rp, bool still, bool patched)
			{
				Well = well;
				R = r;
				Rp = rp;
				Still = still;
				Patched = patched;
			}
		};

		/// <summary>A world (or a parking orbit round the frame's own centre) as a leg end.</summary>
		LegEnd BodyEnd(int frame, int body, cs::Ref<Ship> ship, double jd);

		/// <summary>A hyperlane ramp (a point held still off its planet) as a leg end in that planet's frame.</summary>
		LegEnd RampEnd(int system);

		/// <summary>
		/// A flip-and-burn's gravity correction as the estimate sees it (U.SPC-T95): climbing straight out of
		/// each end's well from rest (<see cref="Conics.GravityLossLeaving"/>), plus the frame's own pull
		/// across the flight between two worlds that orbit it.
		/// </summary>
		double GravityLossEstimate(cs::Ref<Ship> ship, int frame, LegEnd start, LegEnd end, double days);

		/// <summary>
		/// A flip-and-burn's gravity correction as flown (U.SPC-T95): the pull of the frame's centre and of
		/// every world on either end's chain, summed along the planned path, ∫ Σ μ/d² dt. A torch spends
		/// that holding its straight line instead of falling off it.
		/// </summary>
		double GravityLoss(cs::Ref<Ship> ship, int frame, int from, int to);

		// Key (int, int, int, long, long, long, long, long); value (double days, double dv)[]: .first days, .second dv.
		cs::Dictionary<std::tuple<int, int, int, long long, long long, long long, long long, long long>, cs::List<std::pair<double, double>>, ValueTupleHash> conicOptions =
			cs::Dictionary<std::tuple<int, int, int, long long, long long, long long, long long, long long>, cs::List<std::pair<double, double>>, ValueTupleHash>();

		/// <summary>
		/// The cheapest transfer orbit for a leg as the trip estimate sees it, priced at
		/// <see cref="LegCost"/>: a Hohmann transfer (waiting for its window when both ends are worlds
		/// going round the frame), or a faster Lambert arc leaving now. False if the frame has no gravity.
		/// </summary>
		bool BestConic(cs::Ref<Ship> ship, int frame, LegEnd start, LegEnd end, double jd, double& days, double& dv);

		static long long Bucket(double km);

		// Returns (double days, double dv)[]: .first days, .second dv; C#'s null is the empty list.
		cs::List<std::pair<double, double>> ConicOptions(int frame, LegEnd start, LegEnd end, double jd);

		/// <summary>Where the first leg of a trip heads: the first lane's ramp, or the destination itself.</summary>
		DVec3 FirstLegTarget(int here, int next, double jd);

	public:
		cs::Random& Rng() const;

		// escorts: params (ShipClass cls, string name)[] -> .first cls, .second name.
		cs::Ref<Ship> Group(cs::Ref<Faction> faction, double jd, ShipClass leaderClass, cs::String leaderName, cs::List<cs::String> route,
			cs::List<std::pair<ShipClass, cs::String>> escorts);
	private:
		Fleet() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(Fleet); // ObjectGraph registration (Persistence/Registrations)
	};
}
