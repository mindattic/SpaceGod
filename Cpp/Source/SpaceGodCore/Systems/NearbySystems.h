#pragma once
// Port of Assets/UnitySpace/Core/Systems/NearbySystems.cs (US-T17).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class SystemSpec;
	class StarSpec;
	class PlanetSpec;

	/// <summary>
	/// The nearest real star systems beyond Alpha Centauri, as catalogue entries for
	/// <see cref="StarSystemBuilder"/>. Every number is sourced in Core/Systems/SOURCES.md:
	///  - positions and parallaxes, stellar masses, radii, temperatures, luminosities and rotation:
	///    the Wikipedia article of each star (from Gaia DR3 and the papers it cites), the NASA
	///    Exoplanet Archive's stellar values where an article gives none;
	///  - planets: the NASA Exoplanet Archive's composite table (pscomppars, queried 2026-10-16):
	///    period, semi-major axis, eccentricity, mass (minimum mass for radial-velocity planets),
	///    the archive's radius estimate, argument and time of periastron where listed;
	///  - candidates the archive does not list (or lists from one unconfirmed detection) come from
	///    their discovery papers and are flagged <see cref="BodyStatus.Candidate"/>;
	///  - binary orbits and debris discs: the orbit and disc papers named beside them.
	/// Orbital planes and phases nobody has measured are assumed and say so. The societies settled on
	/// them (<see cref="NearbySocieties"/>) are fiction, kept apart from these entries.
	/// </summary>
	class SPACEGODCORE_API NearbySystems
	{
		static constexpr double JupiterMassSolar = 9.547919e-4;

	public:
		/// <summary>Every catalogued system beyond Alpha Centauri, nearest first.</summary>
		// IEnumerable + yield: built eagerly (making an entry has no side effects).
		static cs::List<cs::Ref<SystemSpec>> All();

	private:
		static cs::Ref<StarSpec> Star(const cs::String& name, const cs::String& spectral, double mass, double radius, double luminosity, double teff, double rotationDays, const cs::String& summary);

		/// <summary>A planet from the NASA Exoplanet Archive's composite table.</summary>
		static cs::Ref<PlanetSpec> Archive(const cs::String& name, const cs::String& host, double periodDays, double aAu, double e, double massEarth, double radiusEarth,
			double argPeriDeg, int year, const cs::String& reference, const cs::String& summary, bool minimumMass = true, double periastronJd = cs::Double::NaN);

		/// <summary>A candidate from its discovery paper (radius from the Chen and Kipping 2017 mass-radius relation).</summary>
		static cs::Ref<PlanetSpec> Candidate(const cs::String& name, const cs::String& host, double periodDays, double aAu, double e, double minMassEarth, int year,
			const cs::String& reference, const cs::String& summary);

	public:
		/// <summary>
		/// Chen and Kipping 2017 (ApJ 834, 17) mean mass-radius relation, Earth units: R = 1.008 M^0.279
		/// for rocky worlds below 2.04 Earth masses, R ∝ M^0.589 for Neptune-like ones up to 0.414
		/// Jupiter masses, then nearly flat (M^-0.044) for gas giants.
		/// </summary>
		static double RadiusFromMass(double massEarth);

		// ---------------------------------------------------------------- 5.96 ly
		static cs::Ref<SystemSpec> BarnardsStar();
		// ---------------------------------------------------------------- 6.51 ly
		static cs::Ref<SystemSpec> Luhman16();
		// ---------------------------------------------------------------- 7.86 ly
		static cs::Ref<SystemSpec> Wolf359();
		// ---------------------------------------------------------------- 8.30 ly
		static cs::Ref<SystemSpec> Lalande21185();
		// ---------------------------------------------------------------- 8.61 ly
		static cs::Ref<SystemSpec> Sirius();
		// ---------------------------------------------------------------- 10.47 ly
		static cs::Ref<SystemSpec> EpsilonEridani();
		// ---------------------------------------------------------------- 11.01 ly
		static cs::Ref<SystemSpec> Ross128();
		// ---------------------------------------------------------------- 11.87 ly
		static cs::Ref<SystemSpec> EpsilonIndi();
		// ---------------------------------------------------------------- 11.91 ly
		static cs::Ref<SystemSpec> TauCeti();
		// ---------------------------------------------------------------- 11.98 ly
		static cs::Ref<SystemSpec> Gj1061();
		// ---------------------------------------------------------------- 12.35 ly
		static cs::Ref<SystemSpec> LuytensStar();
		// ---------------------------------------------------------------- 12.50 ly
		static cs::Ref<SystemSpec> TeegardensStar();
	};
}
