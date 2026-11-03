#pragma once
// Port of Assets/UnitySpace/Core/Systems/AlphaCentauri.cs (US-T17).

#include "Cs/Cs.h"
#include "Astro.h"
#include "DVec3.h"
#include "BodyDefinition.h"
#include "Systems/StarSystemBuilder.h"

namespace SpaceGod::Core
{
	class BinaryOrbitSpec;
	class SystemSpec;

	/// <summary>
	/// The Alpha Centauri system, the Sun's nearest neighbour: the binary Alpha Centauri A and B
	/// circling their barycentre every 80 years, the red dwarf Proxima Centauri 13,000 AU away on a
	/// half-million-year orbit around the pair, and the planets found so far. All positions are in
	/// the same J2000 ecliptic axes as the Sol system, with the A-B barycentre at the origin. Built
	/// from its catalogue entry (<see cref="Spec"/>) by <see cref="StarSystemBuilder"/>, like every
	/// other nearby system.
	///
	/// Sources (see Core/Systems/SOURCES.md):
	///  - A-B orbit, masses, distance: Akeson et al. 2021, AJ 162, 14 (arXiv 2104.10086).
	///  - Radii, luminosities, temperatures, rotation: Wikipedia "Alpha Centauri", "Proxima Centauri"
	///    and the papers they cite.
	///  - Proxima's orbit around A-B: Kervella, Thévenin and Lovis 2017, A&amp;A 598, L7 (arXiv 1611.03495).
	///  - Proxima b and d: NASA Exoplanet Archive; c (Damasso et al. 2020) is disputed and no longer listed there.
	///  - Alpha Cen A "S1": Beichman et al. 2025, ApJL (JWST/MIRI, August 2024), a candidate not yet re-detected.
	///
	/// What is not known is assumed and said so in each summary: the planets' orbital planes and
	/// phases (only periods, separations and minimum masses are measured), the stars' spin poles,
	/// and the orientation of Proxima's orbital plane. The stations are game-layer fiction.
	/// </summary>
	class AlphaCentauri
	{
	public:
		static constexpr double SunGm = StarSystemBuilder::SunGm;
	private:
		static constexpr double EarthRadiusKm = StarSystemBuilder::EarthRadiusKm;
		static constexpr double JupiterRadiusKm = 71492;

	public:
		static constexpr double MassA = 1.0788, MassB = 0.9092, MassProxima = 0.1221;

		/// <summary>Distance of the A-B pair (light-years, Akeson et al. 2021) and of Proxima (light-years).</summary>
		static constexpr double DistanceLy = 4.344, ProximaDistanceLy = 4.2465;

		/// <summary>Alpha Centauri A's position on the sky (ICRS J2000).</summary>
		static inline const double RaDeg = Astro::Hms(14, 39, 36.494), DecDeg = Astro::Dms(60, 50, 2.37, true);

		/// <summary>Proxima Centauri's position on the sky (ICRS J2000).</summary>
		static inline const double ProximaRaDeg = Astro::Hms(14, 29, 42.946), ProximaDecDeg = Astro::Dms(62, 40, 46.16, true);

		/// <summary>A-B relative orbit (Akeson et al. 2021): Campbell elements of B about A (a = 17.493" at 1.3319 pc).</summary>
		static const cs::Ref<BinaryOrbitSpec>& Orbit();

		/// <summary>Unit vector (ecliptic J2000) from the Sun toward the A-B pair.</summary>
		static DVec3 Direction();

		/// <summary>Position of the A-B pair relative to the Sun, light-years (ecliptic J2000).</summary>
		static DVec3 PositionLy();

		/// <summary>Proxima's position relative to the A-B pair today, km (ecliptic J2000), from the two stars' sky positions and distances.</summary>
		static DVec3 ProximaOffsetKm();

		/// <summary>The normal of the A-B orbit (ecliptic J2000), also used as the assumed pole of the stars and of Proxima's planets.</summary>
		static DVec3 BinaryNormal();

		/// <summary>Alpha Centauri's catalogue entry.</summary>
		static cs::Ref<SystemSpec> Spec();

		static cs::List<cs::Ref<BodyDefinition>> Definitions();

		/// <summary>
		/// Approximate visible colour of a blackbody photosphere (normalised so the brightest channel
		/// is 1), from a fit to the CIE colour of Planck spectra (Tanner Helland's approximation).
		/// </summary>
		static Rgb StarColor(double teffK);
	};
}
