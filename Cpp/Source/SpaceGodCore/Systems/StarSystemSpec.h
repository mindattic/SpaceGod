#pragma once
// Port of Assets/UnitySpace/Core/Systems/StarSystemSpec.cs (US-T17).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	/// <summary>Where a planet sits against its star's habitable zone (Kopparapu et al. 2013), see <see cref="HabitableZones"/>.</summary>
	// Declared before BodyDefinition.h is included: BodyDefinition holds one by value (an opaque
	// `enum class HabitableZone : int;` there is enough), and this file holds Rgb and BodyStatus by value.
	enum class HabitableZone : int
	{
		/// <summary>Outside it, or not a planet.</summary>
		None,

		/// <summary>Inside the optimistic zone (recent Venus to early Mars) but not the conservative one.</summary>
		Optimistic,

		/// <summary>Inside the conservative zone (runaway to maximum greenhouse).</summary>
		Conservative,
	};
	SPACEGODCORE_API const char* ToString(HabitableZone v);
}

#include "BodyDefinition.h"

namespace SpaceGod::Core
{
	/// <summary>One star (or brown dwarf) of a catalogued system: measured values only, each sourced in Systems/SOURCES.md.</summary>
	class StarSpec final
	{
	public:
		cs::String Name;
		cs::String Spectral;
		double MassSolar = 0;
		double RadiusSolar = 0;
		double LuminositySolar = 0;
		double TeffK = 0;

		/// <summary>Rotation period, days (assumed where the summary says so).</summary>
		double RotationDays = 0;

		cs::String Summary;
	};

	/// <summary>
	/// A visual binary's relative orbit: the secondary about the primary, in Campbell elements on the
	/// sky (inclination, position angle of the node, argument of periastron) as orbit papers give them.
	/// </summary>
	class BinaryOrbitSpec final
	{
	public:
		double PeriodYears = 0;
		double AAu = 0;
		double E = 0;
		double IDeg = 0;
		double NodeDeg = 0;
		double ArgPeriDeg = 0;

		/// <summary>Year of periastron (Julian years from J2000, as the papers' decimal years are read).</summary>
		double PeriastronYear = 0;
	};

	/// <summary>
	/// Either one star or a bound pair circling its own barycentre (A and B on a shared relative
	/// orbit). A pair's barycentre is an invisible body named <see cref="Name"/>.
	/// </summary>
	class SPACEGODCORE_API StellarNode final
	{
	public:
		cs::Ref<StarSpec> Star;

		cs::String Name;
		cs::Ref<StarSpec> A, B;
		cs::Ref<BinaryOrbitSpec> Orbit;

		/// <summary>The barycentre's description; null for a plain one.</summary>
		cs::String Summary;

		bool IsPair() const { return Star == nullptr; }
		double MassSolar() const { return IsPair() ? A->MassSolar + B->MassSolar : Star->MassSolar; }

		static cs::Ref<StellarNode> Single(cs::Ref<StarSpec> star);

		static cs::Ref<StellarNode> Pair(const cs::String& name, cs::Ref<StarSpec> a, cs::Ref<StarSpec> b, cs::Ref<BinaryOrbitSpec> orbit, const cs::String& summary = cs::String());
	};

	/// <summary>
	/// A wide companion (Proxima about Alpha Centauri A-B, the Epsilon Indi B pair about A), on an
	/// orbit through where it is seen today. Its offset from the primary comes either from its own
	/// sky position and distance, or from a projected separation; the orbit's plane is assumed.
	/// </summary>
	class CompanionSpec final
	{
	public:
		cs::Ref<StellarNode> Node;

		/// <summary>The companion's own sky position (ICRS) and distance; NaN to use a projected separation instead.</summary>
		double RaDeg = cs::Double::NaN, DecDeg = cs::Double::NaN, DistanceLy = cs::Double::NaN;

		/// <summary>Projected separation (AU), used when the sky position is not given.</summary>
		double SeparationAu = cs::Double::NaN;

		/// <summary>Position angle on the sky (degrees east of north) of the projected separation; assumed unless stated.</summary>
		double PositionAngleDeg = 0;

		double E = 0;

		/// <summary>Mean anomaly today (degrees): 180 puts the companion at apastron.</summary>
		double MeanAnomalyDeg = 180;
	};

	/// <summary>A planet as the catalogue lists it. NaN means not measured (the builder assumes, and the summary says so).</summary>
	class PlanetSpec final
	{
	public:
		cs::String Name;

		/// <summary>The star it orbits.</summary>
		cs::String Host;

		double PeriodDays = 0;
		double AAu = 0;
		double E = 0;
		double MassEarth = 0;

		/// <summary>The mass is m sin i (radial velocity only), a lower limit.</summary>
		bool MassIsMinimum = true;

		/// <summary>Radius in Earth radii (the NASA Exoplanet Archive's mass-radius estimate for planets never seen to transit).</summary>
		double RadiusEarth = 0;

		/// <summary>Inclination to the sky plane; NaN for the system's reference plane.</summary>
		double IDeg = cs::Double::NaN;

		/// <summary>Position angle of the node on the sky; NaN for the system's (assumed) one.</summary>
		double NodeDeg = cs::Double::NaN;

		/// <summary>Argument of periastron; NaN when unmeasured (0 is used).</summary>
		double ArgPeriDeg = cs::Double::NaN;

		/// <summary>Time of periastron (JD); sets the phase when known.</summary>
		double PeriastronJd = cs::Double::NaN;

		/// <summary>Mean anomaly at J2000 (degrees) when the phase is unknown; NaN picks a fixed one from the name.</summary>
		double MeanAnomalyDeg = cs::Double::NaN;

		/// <summary>Tilt of the orbit (degrees) out of the system's reference plane, about the line across the binary's periastron (Alpha Centauri Ab).</summary>
		double TiltFromReferenceDeg = 0;

		BodyStatus Status = BodyStatus::Confirmed;
		int DiscoveryYear = 0;

		/// <summary>Short reference for the values ("NASA Exoplanet Archive; Feng et al. 2017").</summary>
		cs::String Source;

		cs::String Summary;

		// Looks (assumed): left at their defaults the builder picks them from the planet's mass and warmth.
		cs::Nullable<Rgb> Color;
		cs::Nullable<bool> GasGiant;
		cs::Nullable<bool> TidallyLocked;
		double RotationHours = cs::Double::NaN;
		Rgb AtmosphereColor;
		float AtmosphereStrength = 0;
	};

	/// <summary>A debris belt or disc, a ring of dust and planetesimals about one star.</summary>
	class DiscSpec final
	{
	public:
		cs::String Name;
		cs::String Host;
		double InnerAu = 0;
		double OuterAu = 0;

		/// <summary>Inclination to the sky plane (measured for resolved discs); NaN for the reference plane.</summary>
		double IDeg = cs::Double::NaN;

		cs::String Source;
		cs::String Summary;
	};

	/// <summary>An invented station on its own circular orbit about a star (game layer).</summary>
	class StationSpec final
	{
	public:
		cs::String Name;
		cs::String Host;
		double AAu = 0;
		double MeanAnomalyDeg = 0;
		cs::String Summary;

		/// <summary>
		/// Not a station yet but the orbit marked out for one (U.SPC-T293): a frontier star's habitat site, where the
		/// first colony ship through its gate builds its settlers' home (see <see cref="ColonySite.Survey"/>).
		/// </summary>
		bool HabitatSite = false;
	};

	/// <summary>
	/// A real star system as data: where it is, its stars, its catalogued planets and discs, and any
	/// invented stations. <see cref="StarSystemBuilder"/> turns it into bodies. Values are sourced in
	/// Systems/SOURCES.md; only what the planet and station summaries call assumed or fiction is not.
	/// </summary>
	class SPACEGODCORE_API SystemSpec final
	{
	public:
		cs::String Name;

		/// <summary>The primary's sky position (ICRS J2000) and distance (light-years, from the parallax).</summary>
		double RaDeg = 0, DecDeg = 0, DistanceLy = 0;

		/// <summary>The name <see cref="Galaxy.Neighbours"/> lists it under, so the simulated system replaces the plain star there.</summary>
		cs::String NeighbourName;

		cs::Ref<StellarNode> Primary;
		cs::List<cs::Ref<CompanionSpec>> Companions;
		cs::List<cs::Ref<PlanetSpec>> Planets;
		cs::List<cs::Ref<DiscSpec>> Discs;
		cs::List<cs::Ref<StationSpec>> Stations;

		/// <summary>
		/// The plane assumed for everything whose orientation is not measured, as an inclination to the
		/// sky and a node position angle. A pair at the root overrides it with its own orbit plane.
		/// 60 degrees is the median inclination of randomly oriented orbits.
		/// </summary>
		double ReferenceIDeg = 60, ReferenceNodeDeg = 0;

		/// <summary>How much of the system the overview frames (AU); NaN to work it out from the planets.</summary>
		double OverviewSpanAu = cs::Double::NaN;

		cs::String Summary;

		DVec3 PositionLy() const;

		/// <summary>Distance in light-years from a parallax in milliarcseconds.</summary>
		static double LyFromParallax(double mas) { return 1000.0 / mas * 3.2615637769; }

		/// <summary>A visual binary's angular semi-major axis (arcsec) in AU at a parallax (mas).</summary>
		static double AuFromArcsec(double arcsec, double parallaxMas) { return arcsec / (parallaxMas / 1000.0); }

		// IEnumerable + yield: built eagerly.
		cs::List<cs::Ref<StarSpec>> Stars() const;

	private:
		static cs::List<cs::Ref<StarSpec>> NodeStars(const cs::Ref<StellarNode>& n);
	};

	/// <summary>
	/// The habitable zone of Kopparapu et al. 2013 (ApJ 765, 131; corrected coefficients of the 2013
	/// erratum): the stellar flux, relative to Earth's, at which an Earth-like planet's water runs away
	/// (inner edge) or its CO2 greenhouse can no longer keep it above freezing (outer edge), as a
	/// polynomial in the star's temperature (fit for 2600-7200 K; cooler or hotter stars are clamped).
	/// The optimistic edges (recent Venus 1.7763, early Mars 0.3207 at the Sun) are scaled from the
	/// conservative ones by their solar ratios, an approximation.
	/// </summary>
	class SPACEGODCORE_API HabitableZones
	{
	public:
		/// <summary>Effective flux (Earth = 1) at the runaway-greenhouse inner edge.</summary>
		static double RunawayFlux(double teffK) { return Seff(teffK, 1.0385, 1.2456e-4, 1.4612e-8, -7.6345e-12, -1.7511e-15); }

		/// <summary>Effective flux (Earth = 1) at the maximum-greenhouse outer edge.</summary>
		static double MaxGreenhouseFlux(double teffK) { return Seff(teffK, 0.3507, 5.9578e-5, 1.6707e-9, -3.0058e-12, -5.1925e-16); }

	private:
		static constexpr double RecentVenusRatio = 1.7763 / 1.0385; static constexpr double EarlyMarsRatio = 0.3207 / 0.3507;

		static double Seff(double teffK, double s0, double a, double b, double c, double d);

	public:
		/// <summary>The orbit-averaged flux a planet receives, Earth = 1: L / (a^2 sqrt(1 - e^2)).</summary>
		static double MeanFlux(double luminositySolar, double aAu, double e) { return luminositySolar / (aAu * aAu * cs::Math::Sqrt(1 - e * e)); }

		static HabitableZone Classify(double luminositySolar, double teffK, double aAu, double e);

		/// <summary>The conservative zone's inner and outer edges (AU) for a star.</summary>
		static void ConservativeEdgesAu(double luminositySolar, double teffK, double& innerAu, double& outerAu);
	};
}
