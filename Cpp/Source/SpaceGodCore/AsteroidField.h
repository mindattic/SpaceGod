#pragma once
// Port of Unity.SpaceGod Core/AsteroidField.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

#include <cstdint>

namespace SpaceGod::Core
{
	class BodyDefinition;
	class ScaleModel;

	/// <summary>
	/// The main asteroid belt and Jupiter's Trojans as real objects: osculating heliocentric
	/// elements from the JPL Small-Body Database (SBDB query API), propagated two-body with
	/// Kepler's equation.
	///
	/// Data (Resources/Data/asteroids.bytes, built from the SBDB query of 2026-10-11 with
	/// full-prec=1): every numbered main-belt asteroid (sb-class MBA) brighter than absolute
	/// magnitude H 15.5, and every Jupiter Trojan (TJN) brighter than H 15; Ceres is left out
	/// because it is a full body. All elements share one osculating epoch.
	///
	/// Binary layout (little-endian): "AST1", int32 count, float64 epoch JD (TDB), then per
	/// asteroid 36 bytes: float32 a (AU), e, i, Ω, ω, M (deg, J2000 ecliptic), H; int32 number;
	/// uint8 group (0 main belt, 1 Trojan); 3 bytes padding. Sorted brightest first.
	///
	/// Two-body propagation ignores Jupiter's perturbations: away from the epoch a belt asteroid
	/// drifts from its true place by roughly 0.01 AU per year (see the tests against JPL Horizons).
	/// Kirkwood gaps and the Trojan clouds are in the data itself, not modelled.
	/// </summary>
	class AsteroidField final
	{
	public:
		/// <summary>Gaussian gravitational constant: mean motion (rad/day) of a 1 AU orbit around the Sun.</summary>
		static constexpr double GaussK = 0.01720209895;

		static constexpr int RecordBytes = 36;

		int Count = 0;
		double EpochJd = 0;

		cs::List<float> A;
		cs::List<float> E;
		cs::List<float> InclinationDeg;
		cs::List<float> NodeDeg;
		cs::List<float> ArgPeriDeg;
		cs::List<float> MeanAnomalyDeg;
		cs::List<float> H;
		cs::List<int> Number;
		cs::List<uint8_t> Group;

		/// <summary>Mean motion, rad/day.</summary>
		cs::List<double> MeanMotion;

	private:
		// Perifocal basis: P toward perihelion, Q 90° ahead in the orbit plane (ecliptic J2000 unit vectors).
		cs::List<double> px, py, pz, qx, qy, qz;

		AsteroidField(int count, double epochJd);

	public:
		static cs::Ref<AsteroidField> Parse(const cs::List<uint8_t>& data);

	private:
		void Derive(int i);

	public:
		/// <summary>The geometric albedo assumed for an asteroid's size from its brightness (a typical belt value).</summary>
		static constexpr double TypicalAlbedo = 0.14;

		/// <summary>
		/// One asteroid's diameter, km, from its absolute magnitude: D = 1329 km / √p · 10^(−H/5) with the
		/// <see cref="TypicalAlbedo"/> (so H 15 is about 3.6 km): the size of the rock a hull can fly into (U.SPC-T60).
		/// </summary>
		double DiameterKm(int i) const;

		/// <summary>Heliocentric position (AU, ecliptic J2000) of one asteroid.</summary>
		DVec3 PositionAu(int i, double jd) const;

		/// <summary>
		/// Positions (AU, ecliptic J2000) of a range of asteroids into <paramref name="xyz"/>
		/// (3 floats each, written at the asteroid's own index). Allocation-free, so it can run on a
		/// stride of the field every frame or on a worker thread.
		/// </summary>
		void PositionsAu(double jd, int start, int count, cs::List<float>& xyz) const;

		static constexpr int GpuFloatsPerAsteroid = 12;

		/// <summary>
		/// Packs the per-asteroid constants for the GPU (Asteroids.shader solves Kepler per vertex):
		/// (e, n rad/day, M at epoch rad, H), (a·P, 0), (b·Q, 0) with b = a·√(1-e²).
		/// </summary>
		void PackGpu(cs::List<float>& dest) const;

		/// <summary>
		/// Single-precision mirror of the vertex shader's propagation (same packing, same iteration
		/// count), so the tests can hold the GPU path to the double-precision one.
		/// </summary>
		static void GpuPosition(const cs::List<float>& packed, int i, float dtDays, float& x, float& y, float& z);

		/// <summary>Index of a numbered asteroid, or -1.</summary>
		int IndexOfNumber(int number) const;

		/// <summary>
		/// The Sun's gameplay/realistic distance mapping (<see cref="ScaleModel.LocalDistance"/> for the
		/// Sun) reduced to the constants the asteroid shader needs: (blend, units per AU at realistic
		/// scale, gameplay Sun radius, gameplay units per AU^p) and (p, (R_sun/AU)^p).
		/// </summary>
		static void SunMapping(const cs::Ref<ScaleModel>& scale, const cs::Ref<BodyDefinition>& sun, cs::List<float>& c0, cs::List<float>& c1);

		/// <summary>Single-precision mirror of the shader's Sun mapping: scene distance for a heliocentric distance in AU.</summary>
		static float SceneDistanceFromSun(const cs::List<float>& c0, const cs::List<float>& c1, float dAu);
	};
}
