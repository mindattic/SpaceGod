#pragma once
// Port of Unity.SpaceGod Core/Ephemeris.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Astro.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class BodyCatalog;

	/// <summary>
	/// Computes where every body is at a Julian date by two-body Kepler propagation of its
	/// elements. Positions are km in the J2000 ecliptic frame; "relative" means relative to the
	/// parent body, "helio" means relative to the Sun's centre.
	///
	/// Analytic propagation (not numerical integration) is deliberate: it is exact for any time
	/// warp, never drifts, and can be evaluated at arbitrary future dates, which ship flight
	/// planning needs for intercepts.
	/// </summary>
	class Ephemeris final
	{
	public:
		cs::Ref<BodyCatalog> Catalog;
		double Jd = cs::Double::NaN;

	private:
		struct FrameBasis
		{
		private:
			DVec3 x;
			DVec3 y;
			DVec3 z;

			FrameBasis(DVec3 x, DVec3 y, DVec3 z)
			{
				this->x = x;
				this->y = y;
				this->z = z;
			}

		public:
			FrameBasis() = default;

			// C#: static readonly FrameBasis Ecliptic = new FrameBasis(DVec3.UnitX, DVec3.UnitY, DVec3.UnitZ);
			static const FrameBasis& Ecliptic();

			/// <summary>Equatorial frame of a pole: x toward the equator's ascending node on the ecliptic.</summary>
			static FrameBasis FromPole(DVec3 pole);

			DVec3 ToEcliptic(DVec3 v) const { return x * v.X + y * v.Y + z * v.Z; }
		};

		cs::List<DVec3> relative;
		cs::List<DVec3> helio;
		cs::List<DVec3> velocity;
		cs::List<FrameBasis> frames;
		int moonIndex = -1;
		int earthIndex = -1;
		double moonMassFraction = 0;

		// HelioAt's last answer for each body, and the lunar theory's (see HelioAt).
		cs::List<double> memoJd;
		cs::List<DVec3> memoHelio;
		double lunarJd = cs::Double::NaN;
		DVec3 lunarPos;

	public:
		explicit Ephemeris(cs::Ref<BodyCatalog> catalog);

		DVec3 Relative(int body) const { return relative[body]; }
		DVec3 Helio(int body) const { return helio[body]; }

		/// <summary>Velocity relative to the parent, km/s.</summary>
		DVec3 RelativeVelocityKmS(int body) const { return velocity[body] / Astro::SecondsPerDay; }

		void Update(double jd);

		/// <summary>Heliocentric position of one body at an arbitrary date, without touching the cached frame.</summary>
		/// <remarks>
		/// Each body keeps the last date it was asked for and the answer (U.SPC-T104): a flight plan samples a
		/// world, its moons and the frame they share at the same date one after another, and the moons' parents
		/// and the Earth's lunar theory are then worked out once, not once a moon. The answer is the same either way.
		/// </remarks>
		DVec3 HelioAt(int body, double jd);

	private:
		DVec3 RelativeAt(int body, double jd);

		/// <summary>The Moon about the Earth by the lunar theory, worked out once for each date asked in turn.</summary>
		DVec3 Lunar(double jd);

	public:
		/// <summary>
		/// Samples one full orbit starting at the body's current position and running backwards
		/// along it, so a fading alpha ramp along the samples reads as a trail behind the body.
		/// </summary>
		void SampleOrbitTrail(int body, double jd, cs::List<DVec3>& output) const;

		/// <summary>Orbital speed relative to the parent (km/s) from vis-viva.</summary>
		double OrbitalSpeedKmS(int body) const;
	};
}
