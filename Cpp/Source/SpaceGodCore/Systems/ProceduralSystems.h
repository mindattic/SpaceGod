#pragma once
// Port of Assets/UnitySpace/Core/Systems/ProceduralSystems.cs (US-T17).

#include <initializer_list>
#include "Cs/Cs.h"
#include "Astro.h"
#include "DVec3.h"
#include "Systems/SystemSources.h"

namespace SpaceGod::Core
{
	class SystemSpec;

	/// <summary>
	/// The Milky Way's shape (U.SPC-T413), for placing the generated stars: where its centre and plane lie from Sol, and how
	/// thickly stars fill it. A thin exponential disc (scale length 2.6 kpc, scale height 300 pc, after Bland-Hawthorn and
	/// Gerhard 2016) fading out past 50,000 light-years from the centre, plus a central bulge, normalised so the density at
	/// Sol is the local one: about 0.0032 star systems a cubic light-year, as the catalogued systems within 12 light-years
	/// have it. The centre is Sagittarius A* (RA 17h45m40.04s, Dec -29°00'28.2"), 8.178 kpc off (GRAVITY 2019); the north
	/// galactic pole is at RA 12h51m26.28s, Dec +27°07'42.0" (J2000); the Sun sits 20.8 pc above the plane (Bennett and
	/// Bovy 2019). Spiral arms are left out.
	/// </summary>
	class SPACEGODCORE_API GalacticFrame
	{
	public:
		/// <summary>Sol's distance from the galactic centre, light-years.</summary>
		static constexpr double SunToCentreLy = 26673;

		/// <summary>Sol's height above the galactic plane, light-years.</summary>
		static constexpr double SunHeightLy = 68;

		/// <summary>Star systems a cubic light-year at Sol.</summary>
		static constexpr double LocalDensity = 0.0032;

		/// <summary>The disc's scale length and height, light-years.</summary>
		static constexpr double ScaleLengthLy = 8480; static constexpr double ScaleHeightLy = 980;

		/// <summary>How far from the centre the disc runs at full strength before it fades, light-years.</summary>
		static constexpr double DiscRadiusLy = 50000;

		/// <summary>The fade past <see cref="DiscRadiusLy"/>, light-years.</summary>
		static constexpr double EdgeFadeLy = 2000;

		/// <summary>The bulge's density at the centre (a cubic light-year) and its scale, light-years.</summary>
		static constexpr double BulgeDensity = 0.1; static constexpr double BulgeScaleLy = 1500;

	private:
		static DVec3 InPlane(DVec3 v);

	public:
		/// <summary>Unit vector toward the north galactic pole (J2000 ecliptic axes).</summary>
		static inline const DVec3 North = Astro::DirectionFromRaDec(Astro::Hms(12, 51, 26.28), Astro::Dms(27, 7, 42.0));

		/// <summary>Unit vector from Sol toward the galactic centre, in the plane.</summary>
		static inline const DVec3 ToCentre = InPlane(Astro::DirectionFromRaDec(Astro::Hms(17, 45, 40.04), Astro::Dms(29, 0, 28.2, true)));

		/// <summary>Where the galactic centre is, light-years from Sol.</summary>
		static DVec3 CentreLy();

		/// <summary>A place's distance from the galaxy's axis (<paramref name="radiusLy"/>) and height off its plane (<paramref name="heightLy"/>).</summary>
		static void Cylindrical(DVec3 ly, double& radiusLy, double& heightLy);

		/// <summary>Star systems a cubic light-year at <paramref name="ly"/> (from Sol).</summary>
		static double Density(DVec3 ly);
	};

	/// <summary>A small, fast random stream that comes out the same on every runtime (SplitMix64), for the galaxy's seeds.</summary>
	class SPACEGODCORE_API SeededRandom final
	{
		uint64_t state = 0;

	public:
		explicit SeededRandom(uint64_t seed) : state(seed) {}

		uint64_t NextULong()
		{
			uint64_t z = state += 0x9E3779B97F4A7C15ULL;
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
			return z ^ (z >> 31);
		}

		/// <summary>Uniform in [0, 1).</summary>
		double NextDouble() { return static_cast<double>(NextULong() >> 11) * (1.0 / 9007199254740992.0); }

		double Range(double lo, double hi) { return lo + (hi - lo) * NextDouble(); }

		/// <summary>Log-uniform between <paramref name="lo"/> and <paramref name="hi"/> (both above zero).</summary>
		double LogRange(double lo, double hi) { return cs::Math::Exp(Range(cs::Math::Log(lo), cs::Math::Log(hi))); }

		/// <summary>A Poisson count with mean <paramref name="mean"/> (normal approximation above 30).</summary>
		int Poisson(double mean);

		/// <summary>A seed made from <paramref name="parts"/>, each mixed in turn.</summary>
		// C# `params long[] parts`: call it as SeededRandom::Mix(a, b, c), each part converted to a long.
		template <class... A> static uint64_t Mix(A... parts) { return MixParts({ static_cast<long long>(parts)... }); }
		static uint64_t MixParts(std::initializer_list<long long> parts);
	};

	/// <summary>
	/// The rest of the galaxy, made up from its seed (U.SPC-T413): every star system beyond the real ones near Sol, out to the
	/// far edge of the disc, and none of them built until somebody comes near. Space is cut into cells
	/// <see cref="CellLy"/> on a side; each cell's stars come from the seed and the cell alone (how many from the galaxy's
	/// density there, <see cref="GalacticFrame.Density"/>; where in the cell, and what each one is, from its own stream), so
	/// any part of the galaxy can be charted in any order and comes out the same every time, and nothing about one cell
	/// depends on another having been looked at. A star's planets come from its own seed when it is built.
	///
	/// The mix of stars is the solar neighbourhood's (RECONS): three in four red dwarfs, then K, G, F, A and the rare B
	/// stars, one in twenty a white dwarf and a few brown dwarfs; masses within a class are uniform and radius, luminosity
	/// and temperature follow from them on the main sequence. Planets are drawn so most stars have some (a few per star on
	/// average, rocky ones most often), spaced as real systems are (each 1.4 to 3 times the last one's distance), and a
	/// system now and then has a debris belt past its last planet. All of it is fiction, labelled so.
	/// </summary>
	class SPACEGODCORE_API ProceduralSystems final : public SystemSource
	{
	public:
		static constexpr const char* SourceName = "Procedural";

		/// <summary>The edge of a cell, light-years.</summary>
		static constexpr double CellLy = 10;

		/// <summary>The most stars one cell holds (the galactic core is far denser than anything a chart can show).</summary>
		static constexpr int MaxPerCell = 400;

		/// <summary>The prefix of every generated star's designation.</summary>
		static constexpr const char* Prefix = "SGC";

		cs::String Name() const override { return SourceName; }
		int Order() const override { return 1000; }

		cs::List<cs::Ref<StarEntry>> StarsNear(DVec3 centreLy, double radiusLy, int seed) override;

	private:
		static long long Cell(double ly) { return cs::ToLong(cs::Math::Floor(ly / CellLy)); }

	public:
		/// <summary>Every star of cell (<paramref name="x"/>, <paramref name="y"/>, <paramref name="z"/>) in the galaxy of <paramref name="seed"/>.</summary>
		static cs::List<cs::Ref<StarEntry>> StarsInCell(int seed, long long x, long long y, long long z);

		/// <summary>A generated star's name: the catalogue prefix and its seed's digits, so it is unique and the same whenever it is made.</summary>
		static cs::String Designation(uint64_t starSeed);

		// ---------------------------------------------------------------- stars

	private:
		/// <summary>The kinds of star, with how common each is and its mass and temperature ranges (low mass, high mass).</summary>
		struct StarClass { cs::String cls; double share = 0, m0 = 0, m1 = 0, t0 = 0, t1 = 0; };
		static const cs::List<StarClass>& Classes();

	public:
		/// <summary>The star made from <paramref name="starSeed"/>: its class, mass, radius, temperature and luminosity.</summary>
		static cs::Ref<StarEntry> Star(uint64_t starSeed);

		// ---------------------------------------------------------------- systems

		cs::Ref<SystemSpec> Define(const cs::Ref<StarEntry>& star, int seed) override;

	private:
		static cs::String Kind(const cs::String& spectral);

		static cs::String Format(double v, const char* f) { return cs::Fmt(v, f); }
	};
}
