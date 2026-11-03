#pragma once
// Port of Unity.SpaceGod Core/Trajectories.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	/// <summary>
	/// What a hull's drive is good for when it plans a trip (U.SPC-T95). A torch (warships, couriers,
	/// breachers, fighters) has the thrust to fly a flip-and-burn anywhere and does, paying to hold its
	/// straight line against gravity. A low-thrust hauler (freighters, ore haulers) flies coasting
	/// transfer orbits, two short burns and a long fall, whenever the time is worth less than the
	/// propellant a flip-and-burn would throw out.
	/// </summary>
	enum class DriveKind : int
	{
		Torch,
		LowThrust,
	};
	SPACEGODCORE_API const char* ToString(DriveKind v);

	/// <summary>
	/// Two-body orbital mechanics for planning transfers: universal-variable Kepler propagation, a
	/// Lambert solver, Hohmann transfers and their launch windows, and the patched-conic pieces (sphere
	/// of influence, escape and capture burns, the hyperbola a ship flies out of a world's well).
	/// Every function takes any consistent units (km and seconds, or km and days with mu in km³/day²).
	/// </summary>
	class SPACEGODCORE_API Conics
	{
	public:
		/// <summary>Stumpff function C(z) = (1 - cos √z) / z (its hyperbolic form for z &lt; 0).</summary>
		static double StumpffC(double z);

		/// <summary>Stumpff function S(z) = (√z - sin √z) / z^1.5 (its hyperbolic form for z &lt; 0).</summary>
		static double StumpffS(double z);

		/// <summary>
		/// Where a body starting at <paramref name="r0"/> moving at <paramref name="v0"/> is after
		/// <paramref name="dt"/> (negative runs it backwards) under a centre of gravitational parameter
		/// <paramref name="mu"/>: the universal Kepler equation in χ, solved by safeguarded Newton, then
		/// the Lagrange coefficients. Works for ellipses, parabolas and hyperbolas alike.
		/// </summary>
		static void Propagate(DVec3 r0, DVec3 v0, double mu, double dt, DVec3& r, DVec3& v);

		/// <summary>Position after <paramref name="dt"/> (see <see cref="Propagate"/>).</summary>
		static DVec3 PositionAfter(DVec3 r0, DVec3 v0, double mu, double dt);

		/// <summary>
		/// Lambert's problem: the orbit that takes a body from <paramref name="r1"/> to
		/// <paramref name="r2"/> in <paramref name="tof"/> about a centre of parameter
		/// <paramref name="mu"/>, going round the way <paramref name="normal"/> makes prograde (the short
		/// way if the turn from r1 to r2 is anticlockwise about it, the long way otherwise), in under one
		/// revolution. Universal variables (Bate, Mueller and White; Curtis algorithm 5.2), the root in z
		/// found by bisection on the monotonic time-of-flight function. Returns false when there is no
		/// such orbit (the ends coincide or lie exactly opposite, or the numbers are not finite).
		/// </summary>
		static bool Lambert(DVec3 r1, DVec3 r2, double tof, double mu, DVec3 normal, DVec3& v1, DVec3& v2);

	private:
		static bool IsFinite(DVec3 v);

	public:
		/// <summary>Circular orbital speed at radius r.</summary>
		static double CircularSpeed(double mu, double r) { return cs::Math::Sqrt(mu / r); }

		/// <summary>Escape speed at radius r.</summary>
		static double EscapeSpeed(double mu, double r) { return cs::Math::Sqrt(2 * mu / r); }

		/// <summary>Time of flight of the Hohmann transfer between circular orbits r1 and r2: half the ellipse that touches both.</summary>
		static double HohmannTime(double mu, double r1, double r2);

		/// <summary>The two burns of a Hohmann transfer from circular r1 to circular r2 (onto the ellipse, then off it).</summary>
		static void HohmannDeltaV(double mu, double r1, double r2, double& dv1, double& dv2);

		/// <summary>How often two bodies with periods p1 and p2 line up the same way again (the window repeat).</summary>
		static double SynodicPeriod(double p1, double p2)
		{
			return cs::Math::Abs(p1 - p2) < 1e-12 ? cs::Double::PositiveInfinity : cs::Math::Abs(p1 * p2 / (p1 - p2));
		}

		/// <summary>
		/// How far ahead of the departure world (radians, prograde) the arrival world must be when a
		/// Hohmann transfer leaves, so it is at the far apsis as the ship gets there: π minus the arc it
		/// covers in the transfer time. Negative for an inbound transfer (the target trails).
		/// </summary>
		static double HohmannLeadAngle(double mu, double r1, double r2)
		{
			return cs::Math::PI - cs::Math::Sqrt(mu / (r2 * r2 * r2)) * HohmannTime(mu, r1, r2);
		}

		/// <summary>
		/// Days (in the units of the rates) until the next Hohmann window, given the target's present
		/// angle ahead of the departure world <paramref name="phase"/> and both mean motions: when the
		/// phase, turning at n2 - n1, comes round to the lead angle.
		/// </summary>
		static double WaitForPhase(double phase, double lead, double n1, double n2);

		/// <summary>Laplace's sphere of influence: a·(m/M)^(2/5), from the gravitational parameters.</summary>
		static double SoiRadius(double aKm, double gm, double gmParent)
		{
			return gm <= 0 || gmParent <= 0 ? 0 : aKm * cs::Math::Pow(gm / gmParent, 0.4);
		}

		/// <summary>
		/// The burn at periapsis that takes a ship from a parking orbit at radius <paramref name="rPark"/>
		/// moving at <paramref name="vPark"/> onto the escape hyperbola leaving at <paramref name="vInf"/>
		/// (the Oberth effect: deep in the well, a little speed buys a lot of excess). The same burn in
		/// reverse is the capture into orbit on arrival.
		/// </summary>
		static double EscapeBurn(double mu, double rPark, double vInf, double vPark)
		{
			return cs::Math::Abs(cs::Math::Sqrt(vInf * vInf + 2 * mu / rPark) - vPark);
		}

		/// <summary>
		/// The delta-v (km/s) a flip-and-burn from rest at <paramref name="rKm"/> straight out of a well
		/// of <paramref name="mu"/> (km³/s²) spends holding its line against gravity, at a thrust of
		/// <paramref name="accelKmS2"/>: ∫ μ/r² dt with r = r0 + a·t²/2, which is (π/4)·μ·√(2 / (a·r0³)).
		/// </summary>
		static double GravityLossLeaving(double mu, double rKm, double accelKmS2)
		{
			return mu <= 0 || rKm <= 0 || accelKmS2 <= 0 ? 0 : cs::Math::PI / 4 * mu * cs::Math::Sqrt(2 / (accelKmS2 * rKm * rKm * rKm));
		}
	};

	/// <summary>
	/// A hyperbolic flyby leg about one world: periapsis <see cref="Rp"/>, excess speed
	/// <see cref="VInf"/>, in the plane of <see cref="P"/> (towards periapsis) and <see cref="Q"/>
	/// (the way it moves there). Time is measured from periapsis: positive leaving, negative arriving.
	/// </summary>
	class SPACEGODCORE_API Hyperbola final
	{
	public:
		double Mu = 0, Rp = 0, VInf = 0, E = 0, A = 0;
		DVec3 P, Q;

		/// <summary>
		/// The escape leg from periapsis <paramref name="rp"/> whose outgoing asymptote runs along
		/// <paramref name="vInf"/>, turning about <paramref name="normal"/> (the parking orbit's).
		/// </summary>
		static cs::Ref<Hyperbola> Departure(double mu, double rp, DVec3 vInf, DVec3 normal) { return Build(mu, rp, vInf, normal, true); }

		/// <summary>The capture leg arriving along <paramref name="vInf"/> (its incoming excess velocity) down to periapsis <paramref name="rp"/>.</summary>
		static cs::Ref<Hyperbola> Arrival(double mu, double rp, DVec3 vInf, DVec3 normal) { return Build(mu, rp, vInf, normal, false); }

		/// <summary>
		/// The escape or capture leg whose periapsis is exactly <paramref name="periapsis"/> (where the
		/// ship's parking orbit has it when it burns), in the plane of that point and
		/// <paramref name="vInf"/>, moving at periapsis toward v∞'s side. Its asymptote runs along v∞
		/// only when the point is the right angle round from it (θ∞, see <see cref="AsymptoteError"/>);
		/// a planner times the burn for that.
		/// </summary>
		static cs::Ref<Hyperbola> Through(double mu, DVec3 periapsis, DVec3 vInf, DVec3 fallbackNormal);

	private:
		static cs::Ref<Hyperbola> Build(double mu, double rp, DVec3 vInf, DVec3 normal, bool leaving);

	public:
		/// <summary>Half the turn the leg makes, θ∞ = acos(-1/e): the angle from periapsis to either asymptote.</summary>
		double AsymptoteAngle() const { return cs::Math::Acos(-1 / E); }

		/// <summary>The speed at periapsis, √(v∞² + 2μ/rp).</summary>
		double PeriapsisSpeed() const { return cs::Math::Sqrt(VInf * VInf + 2 * Mu / Rp); }

		/// <summary>
		/// How far (radians) the asymptote of a leg through <paramref name="periapsis"/> misses
		/// <paramref name="vInf"/>: zero when the point lies θ∞ round from v∞, as a well-timed escape burn has it.
		/// </summary>
		static double AsymptoteError(double mu, DVec3 periapsis, DVec3 vInf, bool leaving);

		/// <summary>Mean motion on the hyperbola, √(μ / -a³).</summary>
		double MeanMotion() const { return cs::Math::Sqrt(Mu / (-A * -A * -A)); }

		/// <summary>Position relative to the world <paramref name="t"/> from periapsis (hyperbolic Kepler's equation, M = e·sinh F - F).</summary>
		DVec3 PositionAt(double t) const;

		/// <summary>Velocity relative to the world <paramref name="t"/> from periapsis.</summary>
		DVec3 VelocityAt(double t) const;

		/// <summary>Time from periapsis to reach radius <paramref name="r"/> on the way out (at least 0).</summary>
		double TimeToRadius(double r) const;

	private:
		static double Asinh(double x) { return cs::Math::Log(x + cs::Math::Sqrt(x * x + 1)); }
	};

	/// <summary>
	/// A planned coasting transfer between two ends in one frame (the Sun between worlds, a planet
	/// between its moons): a Lambert conic under the frame's gravity, flown from an escape burn to a
	/// capture burn. When an end is a world with a sphere of influence well outside the ship's orbit, it
	/// is a patched conic: the ship climbs out on an escape hyperbola from its parking orbit, hands over
	/// to the frame's conic as it passes the edge of the sphere, and drops in on the target's capture
	/// hyperbola to circularise in its new parking orbit. Times in days, distances in km, velocities in
	/// km/day, mu in km³/day².
	/// </summary>
	class SPACEGODCORE_API ConicTransfer final
	{
	public:
		/// <summary>The frame's gravitational parameter, km³/day².</summary>
		double Mu = 0;

		/// <summary>
		/// Where (relative to the frame) the frame's conic starts, and how fast: where the ship crosses out
		/// of the departure world's sphere of influence (patched), or the ship itself as it leaves.
		/// </summary>
		DVec3 R0, V0;

		/// <summary>Where the frame's conic ends and how fast: the edge of the arrival world's sphere, or the arrival point.</summary>
		DVec3 R1, V1;

		/// <summary>Flight time, days, end to end.</summary>
		double Days = 0;

		/// <summary>Days spent on the escape hyperbola before the handoff, and on the capture hyperbola after it (0 unpatched).</summary>
		double ExitDays = 0, EntryDays = 0;

		/// <summary>The escape (or departure) burn and the capture (or circularisation) burn, km/s.</summary>
		double DepartureBurnKmS = 0, ArrivalBurnKmS = 0;

		/// <summary>The small trims at the sphere-of-influence handoffs that match each hyperbola to the frame's conic, km/s.</summary>
		double CorrectionBurnKmS = 0;

		/// <summary>How long each end's burn takes at the ship's thrust, days.</summary>
		double DepartureBurnDays = 0, ArrivalBurnDays = 0;

		/// <summary>Total delta-v of the transfer, km/s.</summary>
		double DeltaVKmS() const { return DepartureBurnKmS + ArrivalBurnKmS + CorrectionBurnKmS; }

		/// <summary>The escape hyperbola out of the departure world's well, or null if the ship starts on the conic.</summary>
		cs::Ref<Hyperbola> Escape;

		/// <summary>The capture hyperbola into the arrival world's well, or null if the ship ends on the conic.</summary>
		cs::Ref<Hyperbola> Capture;

		/// <summary>The departure world's state (relative to the frame) at departure and the arrival world's at arrival.</summary>
		DVec3 FromR0, FromV0, ToR1, ToV1;

		/// <summary>The spheres of influence the ship hands over at, km.</summary>
		double FromSoiKm = 0, ToSoiKm = 0;

		/// <summary>
		/// Position relative to the frame <paramref name="tau"/> days into the transfer: on the escape
		/// hyperbola about the departure world until it crosses out of its sphere, then on the frame's
		/// conic, then on the capture hyperbola about the arrival world (patched conics: each piece starts
		/// where the last left off).
		/// </summary>
		DVec3 OffsetAt(double tau) const;
	};
}
