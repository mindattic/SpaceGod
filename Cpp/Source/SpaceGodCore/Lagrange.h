#pragma once
// Port of Unity.SpaceGod Core/Lagrange.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

#include <utility>

namespace SpaceGod::Core
{
	class BodyCatalog;
	class Ephemeris;

	/// <summary>The five equilibrium points of a two-body pair.</summary>
	enum class LagrangePoint : int
	{
		None = 0,

		/// <summary>Between the two, on the line joining them (Sun-Earth L1: SOHO, 1.5 million km sunward).</summary>
		L1 = 1,

		/// <summary>Beyond the smaller body, on the far side from the larger (Sun-Earth L2: JWST).</summary>
		L2 = 2,

		/// <summary>On the far side of the larger body, a little inside the smaller one's orbit.</summary>
		L3 = 3,

		/// <summary>60° ahead of the smaller body along its orbit (stable: Jupiter's Greek camp).</summary>
		L4 = 4,

		/// <summary>60° behind it (stable: Jupiter's Trojan camp).</summary>
		L5 = 5,
	};
	const char* ToString(LagrangePoint v);

	/// <summary>
	/// The Lagrange points of every body pair, from the circular restricted three-body problem
	/// applied to the pair's instantaneous geometry: L1, L2 and L3 on the line through the two at
	/// the distances the collinear quintics give for the pair's mass ratio, L4 and L5 at the
	/// third corners of the equilateral triangles in the smaller body's orbital plane. This is
	/// how JPL Horizons defines its Lagrange-point ephemerides (SEMB-L1 to L5 for the Sun and the
	/// Earth-Moon barycentre, EM-L1 to L5 for Earth and the Moon). Each point moves with its pair,
	/// so anything put there stays co-orbital.
	/// </summary>
	class Lagrange
	{
	private:
		static inline const double Sin60 = cs::Math::Sqrt(3) / 2;

	public:
		/// <summary>The smaller body's share of the pair's mass, μ = m₂ / (m₁ + m₂).</summary>
		static double MassRatio(double gmPrimary, double gmSecondary) { return gmSecondary / (gmPrimary + gmSecondary); }

		/// <summary>
		/// For L1 and L2, the point's distance from the smaller body; for L3, its distance from the
		/// larger; as a fraction of their separation. Newton's method on the collinear quintics
		/// (Szebehely 1967, Theory of Orbits, §4.5). Zero for L4 and L5.
		/// </summary>
		static double CollinearDistance(double mu, LagrangePoint point);

		/// <summary>
		/// A point's offset from the larger body, given the smaller one's offset from it
		/// (<paramref name="relative"/>) and the direction of its orbital angular momentum
		/// (<paramref name="normal"/>, r × v).
		/// </summary>
		static DVec3 Offset(DVec3 relative, DVec3 normal, double mu, LagrangePoint point);

		/// <summary>
		/// The pair a body's Lagrange points belong to: it and what it orbits. None for a body
		/// orbiting a barycentre (a binary star's members) or with nothing to orbit.
		/// </summary>
		static bool HasPoints(const cs::Ref<BodyCatalog>& catalog, int secondary);

		/// <summary>Every pair with Lagrange points: (larger, smaller) by catalogue index.</summary>
		/// <remarks>C#: IEnumerable&lt;(int primary, int secondary)&gt;; first = primary, second = secondary.</remarks>
		static cs::List<std::pair<int, int>> Pairs(const cs::Ref<BodyCatalog>& catalog);

	private:
		/// <summary>
		/// Where the smaller body's mass sits seen from a star: a planet with its moons (Earth and the
		/// Moon as their barycentre, as JPL's Sun-EMB points use) and their combined GM. A moon is
		/// just itself.
		/// </summary>
		static DVec3 MassCentre(const cs::Ref<Ephemeris>& eph, int body, double jd, double& gm);

	public:
		/// <summary>
		/// A Lagrange point of a body (<paramref name="secondary"/>) and what it orbits, heliocentric km
		/// at a date. A planet's points use it and its moons together, a moon's use the moon alone.
		/// </summary>
		static DVec3 HelioAt(const cs::Ref<Ephemeris>& eph, int secondary, LagrangePoint point, double jd);

		/// <summary>A Lagrange point's offset from its pair's larger body, km.</summary>
		static DVec3 OffsetAt(const cs::Ref<Ephemeris>& eph, int secondary, LagrangePoint point, double jd);

		/// <summary>
		/// The body a Lagrange point is drawn near at gameplay scale: L1 and L2 sit close to the smaller
		/// body, L3, L4 and L5 out on the larger one's side of things, at the smaller one's distance.
		/// </summary>
		static int MappedNear(const cs::Ref<BodyCatalog>& catalog, int secondary, LagrangePoint point);

		/// <summary>"Sun-Earth L2", "Earth-Moon L1".</summary>
		static cs::String Name(const cs::Ref<BodyCatalog>& catalog, int secondary, LagrangePoint point);

		/// <summary>
		/// How far ahead of <paramref name="host"/> a body is along the orbit (degrees, -180 to 180),
		/// measured in the host's orbital plane: about +60 for a body in the host's L4 swarm, -60 in L5.
		/// </summary>
		static double LeadDeg(const cs::Ref<Ephemeris>& eph, int host, int body, double jd);
	};
}
