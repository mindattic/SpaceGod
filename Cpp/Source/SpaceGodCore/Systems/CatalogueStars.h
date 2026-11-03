#pragma once
// Port of Assets/UnitySpace/Core/Systems/CatalogueStars.cs (US-T17).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Systems/SystemSources.h"
#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class SystemSpec;

	/// <summary>
	/// The real stars near Sol (U.SPC-T413), as a <see cref="SystemSource"/>: the nearest stars the interstellar view has
	/// always shown (<see cref="Galaxy.Neighbours"/>), and every other system of the owner's chart of the nearest stars out to
	/// <see cref="ChartedToLy"/> (U.SPC-T454, docs/NEAREST_STARS.md), by its 7 ly divisions, the bright ones past
	/// <see cref="RadiusLy"/> landmarks of Earth's sky. A hand-built entry of the same name (<see cref="HandBuiltSystems"/>)
	/// wins its star. It claims the sphere of <see cref="RadiusLy"/> about Sol and <see cref="ClearLy"/> about each of its
	/// stars, so the procedural galaxy (<see cref="ProceduralSystems"/>) never puts an invented star where a real one is, or
	/// in the real neighbourhood; the gaps between them wider than a gate's reach are bridged by <see cref="VoidStations"/>.
	///
	/// The list is far from every star out there (some 14,000 systems lie within 100 ly, most of them faint red dwarfs):
	/// it is the notable ones, and more can be added here as rows. Positions are the chart's, for 1 January 2027 (Gaia DR3
	/// and Hipparcos), listed with the rest of each star's values in Systems/SOURCES.md. A star without a mass in its row
	/// has one estimated from its temperature as if it were on the main sequence, and says so. Known planets of these stars
	/// are not modelled yet: each is built as its star, with the habitat orbit settlers would use.
	/// </summary>
	// The rows are compiled in (Beyond), as in the C#: nothing is read from docs/nearest_stars_2027.csv at run time.
	class SPACEGODCORE_API CatalogueStars final : public SystemSource
	{
	public:
		static constexpr const char* SourceName = "Catalogue";

		/// <summary>The sphere about Sol the catalogue holds: no star is generated within it.</summary>
		static constexpr double RadiusLy = 20;

		/// <summary>The clear space kept about each catalogue star, inside or outside <see cref="RadiusLy"/>.</summary>
		static constexpr double ClearLy = 3;

		cs::String Name() const override { return SourceName; }
		int Order() const override { return 100; }
		bool ChartedFromStart() const override { return true; }

	private:
		static cs::Ref<cs::List<cs::Ref<StarEntry>>>& stars();

	public:
		/// <summary>Every star the catalogue lists, nearest first.</summary>
		static const cs::List<cs::Ref<StarEntry>>& Stars();

	private:
		static cs::Ref<StarEntry> R(const cs::String& name, double x, double y, double z, const cs::String& type, double mag, double teff, double mass, double radius,
			double luminosity, const cs::String& aka = cs::String());

	public:
		/// <summary>A catalogue star past <see cref="RadiusLy"/> this bright to the eye (or brighter) is a landmark of Earth's sky.</summary>
		static constexpr double LandmarkMagnitude = 2.5;

	private:
		/// <summary>A value a row does not give (see <see cref="Beyond"/>).</summary>
		static constexpr double NaN = cs::Double::NaN;

		static cs::Dictionary<cs::String, cs::String, cs::OrdinalIgnoreCaseComparer>& aliases();

	public:
		/// <summary>The other name a catalogue star goes by (Wolf 1061's Gliese 628), or null.</summary>
		static cs::String AlsoKnownAs(const cs::String& name);

	private:
		/// <summary>
		/// Every system of the owner's chart of the nearest stars out to <see cref="ChartedToLy"/> (docs/NEAREST_STARS.md, U.SPC-T454)
		/// past the neighbours: its position for 1 January 2027 straight from docs/nearest_stars_2027.csv (Sol-centric J2000 ecliptic
		/// light-years, from Gaia DR3 and Hipparcos parallaxes and motions, so the pasted list's slips are not in it), its type,
		/// visual magnitude and temperature, and, where the star is not an ordinary dwarf (or is one already known well), its mass,
		/// radius and luminosity; NaN there estimates them from the temperature as a main-sequence star. Sources in Systems/SOURCES.md.
		/// </summary>
		static cs::List<cs::Ref<StarEntry>> Beyond();

	public:
		/// <summary>How far out the catalogue charts real stars: the owner's 100 ly chart (U.SPC-T454).</summary>
		static constexpr double ChartedToLy = 100;

		bool Claims(DVec3 ly) const override;

		cs::List<cs::Ref<StarEntry>> StarsNear(DVec3 centreLy, double radiusLy, int seed) override;

	private:
		static cs::Ref<StarEntry> Copy(const cs::Ref<StarEntry>& e);

	public:
		cs::Ref<SystemSpec> Define(const cs::Ref<StarEntry>& star, int seed) override;

	private:
		static cs::String Ly(const cs::Ref<StarEntry>& e);
	};
}
