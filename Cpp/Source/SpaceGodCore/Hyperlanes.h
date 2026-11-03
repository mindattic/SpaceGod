#pragma once
// Port of Unity.SpaceGod Core/Hyperlanes.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Astro.h"
#include "DVec3.h"

#include <limits>
#include <tuple>
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class BodyCatalog;
	class Ephemeris;
	class JumpGate;
	class Ship;

	/// <summary>A hyperlane between two planetary systems (each named by its planet, the body orbiting the Sun).</summary>
	class Lane final
	{
	public:
		int Id = 0;
		int A = 0, B = 0;
		cs::String Name;

		/// <summary>Sideways bow of the lane as a fraction of its length; lanes always bow away from the Sun.</summary>
		double Bend = 0;

		/// <summary>
		/// The nearest star (or barycentre) both ends orbit within: the Sun for every lane in Sol, a
		/// star for a lane inside its own system, the A-B barycentre for Alpha Centauri's long lanes.
		/// Lanes bow away from it.
		/// </summary>
		int Hub = 0;

		/// <summary>
		/// The jump gate a gate spur leads to (U.SPC-T379), or null for an ordinary lane. A spur runs from
		/// <see cref="A"/>, a planetary system on the network, to <see cref="B"/>, the system's root, where its
		/// far ramp sits right at the gate (see <see cref="Hyperlanes.GateSpurs"/>).
		/// </summary>
		cs::Ref<JumpGate> Gate;

		/// <summary>Whether this is a spur to a jump gate rather than a lane between two planetary systems.</summary>
		bool IsGateSpur() const { return Gate != nullptr; }

		/// <summary>
		/// A heavy lane (U.SPC-T483): built wide and slow to carry what no ordinary lane will take, an ore tug pushing a
		/// captured asteroid or a block of comet ice of up to <see cref="Hyperlanes.HeavyLaneMaxTonnes"/>, at
		/// <see cref="Hyperlanes.HeavyLaneKmS"/>. Ordinary traffic rides it like any other lane.
		/// </summary>
		bool Heavy = false;

		int Other(int system) const { return system == A ? B : A; }
	};

	/// <summary>One lane ridden in one direction.</summary>
	struct LaneHop
	{
		cs::Ref<SpaceGod::Core::Lane> Lane;
		int From = 0, To = 0;

		LaneHop() = default;
		LaneHop(cs::Ref<SpaceGod::Core::Lane> lane, int from, int to)
		{
			Lane = lane;
			From = from;
			To = to;
		}
	};

	/// <summary>
	/// The X3-style highway network. Each planetary system has an on-ramp for each of its lanes, a
	/// fixed distance out past its moons, facing the system at the lane's other end. A ship crosses
	/// to the ramp under its own drive, rides the lane at up to <see cref="CruiseKmS"/> (it eases in
	/// and out, arriving at rest relative to the far ramp), then crosses from the far ramp to its
	/// destination, again under its own drive. Lanes bow gently to one side and swing wide of the
	/// Sun. Within one planet's system (Earth to the Moon, Io to Callisto) ships just fly.
	///
	/// Flown at the helm (U.SPC-T41, U.SPC-T379) a lane is an X3 Rebirth highway: a chain of rings
	/// (<see cref="RingCount"/>, about every <see cref="RingSpacingKm"/>) strung from ramp to ramp, which
	/// speed a rider up through the first half of the ride and brake it through the second
	/// (<see cref="Progress"/>). A pilot reaches a ramp under its own drive (thrusters, boost, then the
	/// cruise drive), flies in through its ring gate and rides the same <see cref="Fleet.JoinLane"/> path
	/// AI traffic does; there is no separate pilot-only lane. Each system's network also leads on to its
	/// jump gates by a <see cref="GateSpurs">gate spur</see> per gate, joined by flying through either end's
	/// ring, so a pilot can ride from a planet to a gate, jump, and pick up the far system's own network
	/// at the gate it comes out of.
	/// </summary>
	/// <remarks>
	/// C++: a route (Route, HeavyRoute, RouteFor) is a cs::Ref&lt;cs::List&lt;LaneHop&gt;&gt;: C# returns null for
	/// no route and hands out the cached list itself.
	/// </remarks>
	class SPACEGODCORE_API Hyperlanes final
	{
	public:
		cs::List<cs::Ref<Lane>> Lanes;

		/// <summary>
		/// How dangerous each lane has been lately (ambushes), in days of extra cost when planning a
		/// route, so traffic shifts to safer lanes until it fades.
		/// </summary>
		cs::Dictionary<cs::Ref<Lane>, double> Danger;

		/// <summary>Sets a lane's danger; planners see the change at once (cached routes are dropped).</summary>
		void SetDanger(const cs::Ref<Lane>& lane, double value);

		// ---------------------------------------------------------------- upkeep (U.SPC-T481)

		/// <summary>
		/// Each lane's condition (U.SPC-T481), 1 fully tended to 0 dark: how well its accelerator rings are powered and
		/// aligned. A lane not listed is in full repair. It runs down without tenders working it and is restored by them
		/// (see <see cref="LaneUpkeep"/>); as it falls the lane rides slower (<see cref="SpeedFactor"/>) and less reliably
		/// (<see cref="Reliability"/>), and at <see cref="LaneCondition.FailAt"/> it fails and carries nothing
		/// (<see cref="Carries"/>), so routes go round it. Gate spurs are not tended and never wear.
		/// </summary>
		cs::Dictionary<cs::Ref<Lane>, double> Condition;

		/// <summary>The condition of <paramref name="lane"/> (see <see cref="Condition"/>), 1 when it has never worn.</summary>
		double ConditionOf(const cs::Ref<Lane>& lane) const;

		/// <summary>Sets a lane's condition (clamped to 0-1). Planners see a lane fail or come back at once (cached routes
		/// are dropped); a lane merely slowing is seen at the next half day, as its changing length is.</summary>
		void SetCondition(const cs::Ref<Lane>& lane, double value);

		/// <summary>Whether <paramref name="lane"/> still carries traffic: not failed (<see cref="LaneCondition.Carries"/>).</summary>
		bool Carries(const cs::Ref<Lane>& lane) const;

		/// <summary>The share of its full pace <paramref name="lane"/> rides at, for its condition (<see cref="LaneCondition.SpeedFactor"/>).</summary>
		double SpeedFactor(const cs::Ref<Lane>& lane) const;

		/// <summary>The chance a ride of <paramref name="lane"/> goes through without a stall (<see cref="LaneCondition.Reliability"/>).</summary>
		double Reliability(const cs::Ref<Lane>& lane) const;

		/// <summary>Days to ride a whole <paramref name="lane"/> of this length at its condition's pace (<see cref="RideDays(double)"/>
		/// over <see cref="SpeedFactor"/>).</summary>
		double RideDays(const cs::Ref<Lane>& lane, double lengthKm) const;

	private:
		cs::Ref<BodyCatalog> catalog;
		cs::Ref<Ephemeris> ephemeris;

	public:
		/// <summary>Peak speed in a lane: 4% of light, so Earth to Mars takes hours, not weeks.</summary>
		static constexpr double CruiseKmS = 12000;

		/// <summary>Lanes stay this far from the Sun; other stars scale it with the square root of their luminosity (equal heating).</summary>
		static constexpr double SunKeepOutKm = 0.3 * Astro::AuKm;

		/// <summary>
		/// The longest a ride takes, days. A lane's peak speed is raised past <see cref="CruiseKmS"/>
		/// when it is that long: the 13,000 AU between Alpha Centauri A-B and Proxima is crossed in days
		/// (deep fiction), while in Sol only the longest outer lanes are sped up a little.
		/// </summary>
		static constexpr double MaxRideDays = 5;

		explicit Hyperlanes(cs::Ref<Ephemeris> ephemeris);

		static cs::Ref<Hyperlanes> CreateDefault(cs::Ref<Ephemeris> ephemeris);

		// ---------------------------------------------------------------- heavy lanes (U.SPC-T483)

		/// <summary>How fast a heavy lane carries a tug and its load at its peak, km/s: a twentieth of an ordinary lane's cruise.</summary>
		static constexpr double HeavyLaneKmS = 600;

		/// <summary>The biggest load a heavy lane will carry, tonnes: a rock a few hundred metres across.</summary>
		static constexpr double HeavyLaneMaxTonnes = 150e6;

		/// <summary>Makes the lane between <paramref name="a"/> and <paramref name="b"/> a heavy lane; returns it, or null if there is none.</summary>
		cs::Ref<Lane> MarkHeavy(const cs::String& a, const cs::String& b);

		/// <summary>Days to ride a whole heavy lane of this length with a load, eased in and out like any ride but at <see cref="HeavyLaneKmS"/>.</summary>
		static double HeavyRideDays(double lengthKm) { return 2 * cs::Math::Max(0.0, lengthKm) / HeavyLaneKmS / Astro::SecondsPerDay; }

		/// <summary>
		/// Days <paramref name="ship"/> takes to ride <paramref name="lengthKm"/> of <paramref name="lane"/>: an ore tug with a
		/// load latched on (U.SPC-T498, <see cref="Ship.Tow"/>) rides a heavy lane at its heavy pace (<see cref="HeavyRideDays(double)"/>);
		/// everything else at the lane's own. A worn lane is slower for both.
		/// </summary>
		double RideDays(const cs::Ref<Ship>& ship, const cs::Ref<Lane>& lane, double lengthKm) const;

		/// <summary>
		/// The lanes <paramref name="ship"/> rides from one planetary system to another: an ore tug with a load latched on
		/// (U.SPC-T498) only heavy lanes (<see cref="HeavyRoute"/>, null where none join them: it pushes through open space),
		/// everyone else the quickest route over any (<see cref="Route"/>).
		/// </summary>
		cs::Ref<cs::List<LaneHop>> RouteFor(const cs::Ref<Ship>& ship, int fromSystem, int toSystem, double jd);

		/// <summary>
		/// The quickest route between two systems over heavy lanes alone (what a tug with a big load can take), or null
		/// when they are not joined by heavy lanes (or are the same system).
		/// </summary>
		cs::Ref<cs::List<LaneHop>> HeavyRoute(int fromSystem, int toSystem, double jd);

		/// <summary>
		/// Alpha Centauri's network: short lanes inside each star's system, and two long trunk lanes
		/// across the 13,000 AU between the A-B pair and Proxima (the only practical way across).
		/// </summary>
		static cs::Ref<Hyperlanes> CreateAlphaCentauri(cs::Ref<Ephemeris> ephemeris);

		// ---------------------------------------------------------------- the ring chain (U.SPC-T379)

		/// <summary>How far apart the rings strung along a lane are, km: a few dozen between Earth and Mars.</summary>
		static constexpr double RingSpacingKm = 2e6;

		/// <summary>The fewest and most rings strung along any one lane.</summary>
		static constexpr int MinRings = 6; static constexpr int MaxRings = 240;

		/// <summary>How many rings are strung along a lane of this length (between its two ramps' ring gates).</summary>
		static int RingCount(double lengthKm);

		/// <summary>
		/// Where ring <paramref name="k"/> (0 nearest whichever ramp the curve is measured from) of
		/// <paramref name="count"/> sits, as a fraction along the lane: evenly spaced strictly between the ramps,
		/// so the chain reads the same ridden either way.
		/// </summary>
		static double RingFraction(int k, int count) { return (k + 1.0) / (count + 1.0); }

		/// <summary>How many of a lane's <paramref name="count"/> rings a rider <paramref name="progress"/> of the way along has flown through.</summary>
		static int RingsPassed(double progress, int count);

		/// <summary>Whether the rings are speeding a rider up (the first half of its ride, by time) rather than braking it for the far ramp.</summary>
		static bool RingsAccelerate(double timeFraction) { return timeFraction < 0.5; }

		// ---------------------------------------------------------------- gate spurs (U.SPC-T379)

		/// <summary>
		/// The spurs that lead the network on to the system's jump gates, one per gate (<see cref="AddGateSpur"/>).
		/// Kept apart from <see cref="Lanes"/>: AI traffic's routes, tolls, law and raiders work the lanes between
		/// planetary systems and reach the gates by their own in-system legs (see <see cref="InterstellarTraffic"/>),
		/// so a spur is for the helm, ridden by the same <see cref="Fleet.JoinLane"/> as any lane.
		/// </summary>
		cs::List<cs::Ref<Lane>> GateSpurs;

		/// <summary>Spur ids start here, clear of the lanes' own (the length cache is keyed by id).</summary>
		static constexpr int GateSpurIdBase = 100000;

		/// <summary>
		/// How far in front of its gate (toward the network) a spur's far ramp sits, km: inside the gate's
		/// <see cref="JumpGate.ActivationRangeKm"/>, so a ride to the gate ends where the gate can be opened.
		/// </summary>
		static constexpr double GateRampInsetKm = 200;

		/// <summary>Where a gate stands, in this system's coordinates (relative to its root, as <see cref="StarSystem.GateInRange"/> takes them).</summary>
		static DVec3 GateHelio(const cs::Ref<JumpGate>& gate);

		/// <summary>
		/// Strings a spur from the network to <paramref name="gate"/>: from whichever planetary system on a lane
		/// lies nearest the gate at <paramref name="jd"/>, to a ramp just in front of the gate. Null when the system
		/// has no lanes to lead on from (a frontier system, a Void Base). Adding the same gate again returns its spur.
		/// </summary>
		cs::Ref<Lane> AddGateSpur(const cs::Ref<JumpGate>& gate, double jd);

		/// <summary>What a lane's end at <paramref name="at"/> is called: its planetary system, or a spur's gate.</summary>
		cs::String EndName(const cs::Ref<Lane>& lane, int at) const;

	private:
		/// <summary>Where a lane's end at <paramref name="at"/> is anchored: its planet, or for a spur's far end, its gate.</summary>
		DVec3 AnchorHelio(const cs::Ref<Lane>& lane, int at, double jd);

	public:
		/// <summary>
		/// The gate spur whose ring (at either end) the segment from <paramref name="fromHelio"/> to
		/// <paramref name="toHelio"/> (true km) flies through, heading along the spur, as the hop that rides it away
		/// from that end, or null. A spur is joined by flying through its ring the way it runs, not by being
		/// near it, so a hull just come out of the gate, or brought off at the ramp, is not swept back in.
		/// </summary>
		cs::Nullable<LaneHop> GateSpurRingCrossed(DVec3 fromHelio, DVec3 toHelio, double jd);

		cs::Ref<Lane> Add(const cs::String& a, const cs::String& b, double bend);

	private:
		/// <summary>The nearest star or barycentre that both bodies orbit within.</summary>
		int CommonStar(int a, int b) const;

	public:
		/// <summary>
		/// The planetary system a body belongs to: the body itself, or the planet it orbits. Anything
		/// orbiting a star directly (a planet, a station) is a system of its own.
		/// </summary>
		int SystemOf(int body) const;

		/// <summary>How close lanes may pass a star (km): <see cref="SunKeepOutKm"/> scaled for its luminosity.</summary>
		double KeepOutKm(int star) const;

		/// <summary>
		/// The star a lane between two points (true km) must swing round: the one the straight line
		/// passes closest to, measured in its own keep-out radii. In Sol it is always the Sun.
		/// </summary>
		int KeepOutStar(DVec3 aHelio, DVec3 bHelio, double jd);

		/// <summary>
		/// A point on a lane in true km, between ramp points <paramref name="a"/> and <paramref name="b"/>
		/// (heliocentric). A ship riding it passes the star and shape fixed when it joined
		/// (<paramref name="star"/>, <paramref name="shape"/>), so its curve cannot flip mid-ride
		/// as the ramps move; -1 decides them now.
		/// </summary>
		DVec3 CurveKm(const cs::Ref<Lane>& lane, DVec3 a, DVec3 b, double s, double jd, int star = -1, int shape = -1);

		double CurveLengthKm(const cs::Ref<Lane>& lane, DVec3 a, DVec3 b, double jd, int star = -1, int shape = -1);

		/// <summary>Which shape the curve takes between these points now: 0 bowed, 1 swung round the star.</summary>
		int ShapeOf(const cs::Ref<Lane>& lane, DVec3 a, DVec3 b, int star, double jd);

	private:
		static bool Detours(DVec3 a, DVec3 b, DVec3 keepOutCentre, double keepOut);

	public:
		/// <summary>How far out a system's ramps sit: past its moons, inside the planet's drawn neighbourhood.</summary>
		double RampRadiusKm(int system) const;

		/// <summary>The ramp's direction from its planet: toward the system at the lane's other end.</summary>
		DVec3 RampDirection(const cs::Ref<Lane>& lane, int at, double jd);

		/// <summary>
		/// The ramp's offset from body <paramref name="at"/>: out past its moons toward the lane's other end, or for a
		/// gate spur's far end, just in front of its gate (<see cref="GateRampInsetKm"/>).
		/// </summary>
		DVec3 RampOffset(const cs::Ref<Lane>& lane, int at, double jd);

		DVec3 RampHelio(const cs::Ref<Lane>& lane, int at, double jd);

		/// <summary>
		/// Radius of the ring gate at each ramp, km (U.SPC-T41): a hull flown in through it joins the lane.
		/// AI traffic burns to the ramp's centre; a pilot only has to thread the ring.
		/// </summary>
		static constexpr double GateRadiusKm = 1500;

		/// <summary>
		/// The on-ramp whose ring gate a point (true heliocentric km) is inside, as the hop that rides
		/// its lane away from that ramp's system, or null. The nearest gate wins where two overlap.
		/// </summary>
		cs::Nullable<LaneHop> OnRampAt(DVec3 helioKm, double jd);

		/// <summary>
		/// The lanes a rider coming off at <paramref name="system"/>'s ramp could carry on along (the
		/// off-ramp's exit choice), never straight back down <paramref name="cameIn"/>: the lanes on to other
		/// planetary systems, then the spurs on to the system's jump gates. A spur's far end, at its gate,
		/// leads on to nothing but the gate. A failed lane (U.SPC-T481) is not offered.
		/// </summary>
		cs::List<LaneHop> OnwardHops(int system, const cs::Ref<Lane>& cameIn) const;

		/// <summary>
		/// A point along a lane from <paramref name="a"/> to <paramref name="b"/> (s from 0 to 1): a
		/// quadratic curve bowed outward by <paramref name="bend"/> and pushed out to stay
		/// <paramref name="keepOut"/> from the origin (the Sun). Works in true km or scene units alike.
		/// </summary>
		static DVec3 Curve(DVec3 a, DVec3 b, double bend, double keepOut, double s, DVec3 up);

		/// <summary>
		/// The general lane curve: bowed away from <paramref name="hub"/> (the star or barycentre the
		/// lane belongs to), and swung round <paramref name="keepOutCentre"/> (the star the line passes
		/// nearest) at <paramref name="keepOut"/>. In Sol both are the Sun at the origin.
		/// </summary>
		static DVec3 Curve(DVec3 a, DVec3 b, double bend, DVec3 hub, DVec3 keepOutCentre, double keepOut, double s, DVec3 up, int shape = -1);

	private:
		static DVec3 Quadratic(DVec3 a, DVec3 c, DVec3 b, double t);

	public:
		static DVec3 CurveTangent(DVec3 a, DVec3 b, double bend, double keepOut, double s, DVec3 up);

		static DVec3 CurveTangent(DVec3 a, DVec3 b, double bend, DVec3 hub, DVec3 keepOutCentre, double keepOut, double s, DVec3 up, int shape = -1);

		static double CurveLength(DVec3 a, DVec3 b, double bend, double keepOut, DVec3 up);

		static double CurveLength(DVec3 a, DVec3 b, double bend, DVec3 hub, DVec3 keepOutCentre, double keepOut, DVec3 up, int shape = -1);

		/// <summary>Ease in, cruise, ease out: distance fraction at time fraction u, at rest at both ends.</summary>
		static double Progress(double u);

		/// <summary>
		/// Days to ride a whole lane of this length (the eased profile peaks at twice the mean speed),
		/// never more than <see cref="MaxRideDays"/>. Part of a lane takes that fraction of the time.
		/// </summary>
		static double RideDays(double lengthKm) { return cs::Math::Min(2 * lengthKm / CruiseKmS / Astro::SecondsPerDay, MaxRideDays); }

	private:
		// C# keys these caches by value tuples; cs::Hash has no tuple hash, so TupleHash stands in.
		struct TupleHash
		{
			template <class... T> size_t operator()(const std::tuple<T...>& t) const
			{
				size_t h = 0;
				std::apply([&](const auto&... v) { ((h = cs::HashCombine(h, std::hash<std::decay_t<decltype(v)>>{}(v))), ...); }, t);
				return h;
			}
		};
		using LengthKey = std::tuple<int, long long>;
		using RouteKey = std::tuple<int, int, long long>;
		using RouteTreeMap = cs::Dictionary<int, LaneHop>;

		// Lane geometry changes over days, and planning asks for it constantly: cache per half day.
		cs::Dictionary<LengthKey, double, TupleHash> lengthCache;
		cs::Dictionary<RouteKey, cs::Ref<cs::List<LaneHop>>, TupleHash> routeCache;
		// The quickest hop into every system from one, per origin and half day: planning asks for many routes from the
		// same place, and one sweep of Dijkstra's answers them all.
		cs::Dictionary<LengthKey, cs::Ref<RouteTreeMap>, TupleHash> routeTrees;
		long long cacheBucket = std::numeric_limits<long long>::min();

		long long Bucket(double jd);

	public:
		double LaneLengthKm(const cs::Ref<Lane>& lane, double jd);

		/// <summary>
		/// Fewest-hours route between two systems (Dijkstra on ride times at the given date), or null. Each lane is priced at
		/// its condition's pace plus the stalls its reliability promises (U.SPC-T481), and a failed lane is left out, so
		/// traffic goes round it, or, with no way round, flies there under its own drive.
		/// </summary>
		cs::Ref<cs::List<LaneHop>> Route(int fromSystem, int toSystem, double jd);

	private:
		cs::Ref<cs::List<LaneHop>> FindRoute(int fromSystem, int toSystem, double jd, bool heavyOnly = false);

		/// <summary>The last hop of the quickest route from <paramref name="fromSystem"/> into every system it reaches.</summary>
		cs::Ref<RouteTreeMap> RouteTree(int fromSystem, double jd, bool heavyOnly);
	private:
		Hyperlanes() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(Hyperlanes); // ObjectGraph registration (Persistence/Registrations)
	};
}
