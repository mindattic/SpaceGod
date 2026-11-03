#pragma once
// Port of Assets/UnitySpace/Core/Systems/SystemSources.cs (US-T17).

#include <mutex>
#include "Cs/Cs.h"
#include "DVec3.h"
#include "GalacticPosition.h"

namespace SpaceGod::Core
{
	class SystemSpec;
	class StarSpec;
	class StarSystem;
	class Polities;
	struct NearbyStar;

	/// <summary>
	/// One star on the galaxy's charts (U.SPC-T413): enough to show it, aim a cruise at it and tell it from its neighbours,
	/// without building it. Which <see cref="SystemSource"/> charted it (<see cref="Source"/>) turns it into a full catalogue
	/// entry (<see cref="SystemSpec"/>) when a pilot comes near enough for it to be simulated; the entry is made afresh each
	/// time from the galaxy's seed, so a star is the same star whenever and however often it is reached.
	/// </summary>
	class SPACEGODCORE_API StarEntry final
	{
	public:
		cs::String Name;

		/// <summary>Where it is, light-years from Sol's root (J2000 ecliptic axes, as every system's <see cref="StarSystem.PositionLy"/>).</summary>
		DVec3 PositionLy;

		cs::String Spectral;
		double MassSolar = 0, RadiusSolar = 0, LuminositySolar = 0, TeffK = 0;

		/// <summary>Visual magnitude as seen from Sol; NaN to work it out from the luminosity (see <see cref="ToNearbyStar"/>).</summary>
		double ApparentMagnitude = cs::Double::NaN;

		/// <summary>The <see cref="SystemSource.Name"/> of the source that charted it, and so defines it.</summary>
		cs::String Source;

		/// <summary>The star's own seed, for a source that makes its system from one (the procedural galaxy).</summary>
		uint64_t Seed = 0;

		/// <summary>A source's own note of where the entry came from (a hand-built list's name, a catalogue's).</summary>
		cs::String Tag;

		GalacticPosition Origin() const;

		/// <summary>
		/// The plain star the charts show until it is simulated. A magnitude not given is worked out from the
		/// luminosity, as if it were all visual light (M = 4.83 - 2.5 log L, no bolometric correction): close enough
		/// for a dot's brightness, not a catalogue value.
		/// </summary>
		NearbyStar ToNearbyStar() const;

		cs::String ToString() const { return Name; }
	};

	/// <summary>
	/// Somewhere star systems come from (U.SPC-T413): the extension point every system outside Sol goes through, hand-built
	/// or generated. A source charts the stars it has near a place (<see cref="StarsNear"/>, cheap: nothing is built), turns
	/// one of them into a catalogue entry when it is wanted (<see cref="Define"/>), and builds that entry into a running
	/// system (<see cref="Build"/>, unsettled unless the source says otherwise). A source may claim a region
	/// (<see cref="Claims"/>), so that sources after it leave that space to it: the real stars near Sol keep the
	/// procedural galaxy out of their neighbourhood. New stars, regions and their content plug in by registering a
	/// source (<see cref="SystemSources.Register"/>) or a <see cref="SystemContent"/>, never by editing the galaxy.
	/// </summary>
	class SPACEGODCORE_API SystemSource
	{
	public:
		virtual ~SystemSource() = default;

		/// <summary>Its name, unique among the sources: a charted star names its source by it.</summary>
		virtual cs::String Name() const = 0;

		/// <summary>Where it stands among the sources: lower goes first, wins a name both chart, and may claim space from the later ones.</summary>
		virtual int Order() const { return 500; }

		/// <summary>Whether this source holds the space at <paramref name="ly"/>, so that later sources chart nothing there.</summary>
		virtual bool Claims(DVec3 ly) const { (void)ly; return false; }

		/// <summary>
		/// Whether its stars are a fixed map put in the galaxy whole at the start (U.SPC-T456, see <see cref="Galaxy.SetUpNeighbourhood"/>),
		/// known or undiscovered by their distance from Sol, rather than charted only as a pilot comes near: the real neighbourhood's
		/// sources, not the procedural galaxy.
		/// </summary>
		virtual bool ChartedFromStart() const { return false; }

		/// <summary>The stars it has within <paramref name="radiusLy"/> of <paramref name="centreLy"/>, in the galaxy of <paramref name="seed"/>.</summary>
		// IEnumerable + yield: built eagerly (the callers enumerate it whole).
		virtual cs::List<cs::Ref<StarEntry>> StarsNear(DVec3 centreLy, double radiusLy, int seed) = 0;

		/// <summary>A fresh catalogue entry for <paramref name="star"/> (one of its own), or null if it cannot define it.</summary>
		virtual cs::Ref<SystemSpec> Define(const cs::Ref<StarEntry>& star, int seed) = 0;

		/// <summary>The running system made from <paramref name="spec"/>: empty and unsettled unless a source overrides it.</summary>
		virtual cs::Ref<StarSystem> Build(const cs::Ref<StarEntry>& star, const cs::Ref<SystemSpec>& spec, double jd, const cs::Ref<Polities>& polities);

		cs::String ToString() const { return Name(); }
	};

	/// <summary>
	/// Content laid into every system a <see cref="SystemSource"/> defines (U.SPC-T413), whichever source it comes from:
	/// a derelict, a ruin, a station, a belt. It works on the fresh catalogue entry before it is built, so what it adds is
	/// built with the system; it should take any chance it needs from the star's seed, so the same star always gets it.
	/// </summary>
	class SystemContent
	{
	public:
		virtual ~SystemContent() = default;
		virtual void Furnish(const cs::Ref<StarEntry>& star, const cs::Ref<SystemSpec>& spec, int seed) = 0;
	};

	/// <summary>
	/// The registered <see cref="SystemSource"/>s, in order (U.SPC-T413): the hand-built systems first
	/// (<see cref="HandBuiltSystems"/>), then the void stations bridging the gaps between the real stars (<see cref="VoidStations"/>)
	/// and the real catalogue stars near Sol (<see cref="CatalogueStars"/>), then the rest of the galaxy, generated from its seed (<see cref="ProceduralSystems"/>). Ask it for the stars near a place and
	/// it merges what every source charts there; ask it for one of them and the source that charted it builds it.
	/// </summary>
	class SPACEGODCORE_API SystemSources
	{
		static cs::List<cs::Ref<SystemSource>>& sources();

		static std::recursive_mutex& gate();

	public:
		/// <summary>Content laid into every system the sources define (see <see cref="SystemContent"/>), in order.</summary>
		static cs::List<cs::Ref<SystemContent>>& Content();

		/// <summary>The sources, first to last.</summary>
		static cs::List<cs::Ref<SystemSource>> All();

		/// <summary>Adds <paramref name="source"/> in its <see cref="SystemSource.Order"/>, replacing any of the same name: how a new region plugs in.</summary>
		static void Register(const cs::Ref<SystemSource>& source);

		/// <summary>Takes the source called <paramref name="name"/> away; false if there was none.</summary>
		static bool Unregister(const cs::String& name);

		/// <summary>The source called <paramref name="name"/>, or null.</summary>
		static cs::Ref<SystemSource> Find(const cs::String& name);

		/// <summary>
		/// Every star within <paramref name="radiusLy"/> of <paramref name="centreLy"/> in the galaxy of <paramref name="seed"/>,
		/// nearest first: each source's in turn, a name already charted by an earlier source left to it, and nothing from a
		/// source in space an earlier one claims. The same place and seed always give the same stars.
		/// </summary>
		static cs::List<cs::Ref<StarEntry>> StarsNear(DVec3 centreLy, double radiusLy, int seed) { return StarsNear(centreLy, radiusLy, seed, nullptr); }

		/// <summary>
		/// As <see cref="StarsNear(DVec3, double, int)"/>, from only the sources <paramref name="which"/> takes (all when null); a
		/// source left out still claims its space from the later ones.
		/// </summary>
		static cs::List<cs::Ref<StarEntry>> StarsNear(DVec3 centreLy, double radiusLy, int seed, cs::Func<bool, cs::Ref<SystemSource>> which);

		/// <summary>The charted star called <paramref name="name"/> near <paramref name="nearLy"/> (within a light-year), or null.</summary>
		static cs::Ref<StarEntry> Entry(const cs::String& name, DVec3 nearLy, int seed);

		/// <summary>A fresh catalogue entry for <paramref name="star"/> from the source that charted it, furnished with the registered <see cref="Content"/>; null if none can define it.</summary>
		static cs::Ref<SystemSpec> Define(const cs::Ref<StarEntry>& star, int seed);

		/// <summary><paramref name="star"/> built into a running system from <paramref name="jd"/>; null if it cannot be.</summary>
		static cs::Ref<StarSystem> Build(const cs::Ref<StarEntry>& star, double jd, int seed, const cs::Ref<Polities>& polities = nullptr);

	private:
		// Pecaut and Mamajek 2013 (ApJS 208, 9; the 2022 online table): main-sequence temperature (K) against mass (solar), M8V to B0V.
		struct MainSequenceRow { double teff = 0; double mass = 0; };
		static const cs::List<MainSequenceRow>& MainSequence();

	public:
		/// <summary>
		/// The main-sequence mass (solar) of a star of <paramref name="teffK"/>, for a catalogue star listed by temperature
		/// only: read off <see cref="MainSequence"/> between its rows, held at its ends.
		/// </summary>
		static double MainSequenceMass(double teffK);

		/// <summary>The main-sequence radius (solar) of a star of <paramref name="massSolar"/>: M^0.8 below a solar mass, M^0.57 above.</summary>
		static double MainSequenceRadius(double massSolar)
		{
			return massSolar < 1 ? cs::Math::Pow(massSolar, 0.8) : cs::Math::Pow(massSolar, 0.57);
		}

		/// <summary>
		/// The catalogue entry for a lone star at <paramref name="star"/>'s place, with the orbit marked out for a
		/// settlers' habitat as the frontier stars have (<see cref="FrontierSystems.Habitat"/>), so a gate in opens it.
		/// </summary>
		static cs::Ref<SystemSpec> LoneStar(const cs::Ref<StarEntry>& star, const cs::Ref<StarSpec>& spec, const cs::String& summary);
	};

	/// <summary>
	/// The hand-built systems as a source (U.SPC-T413): Alpha Centauri, the nearest real systems
	/// (<see cref="NearbySystems"/>) and the frontier stars (<see cref="FrontierSystems"/>), each defined from its own
	/// hand-written entry and built the way it always was (Alpha Centauri and the nearby systems with their societies).
	/// </summary>
	class SPACEGODCORE_API HandBuiltSystems final : public SystemSource
	{
	public:
		static constexpr const char* SourceName = "Hand-built";
		cs::String Name() const override { return SourceName; }
		int Order() const override { return 0; }
		bool ChartedFromStart() const override { return true; }

	private:
		static cs::Ref<cs::List<cs::Ref<StarEntry>>>& stars();

		struct TaggedSpec { cs::String tag; cs::Ref<SystemSpec> spec; };
		// IEnumerable + yield: built eagerly (making an entry has no side effects).
		static cs::List<TaggedSpec> Specs();

	public:
		/// <summary>Every hand-built system as it is charted.</summary>
		static const cs::List<cs::Ref<StarEntry>>& Charted() { return Stars(); }

	private:
		static const cs::List<cs::Ref<StarEntry>>& Stars();

	public:
		cs::List<cs::Ref<StarEntry>> StarsNear(DVec3 centreLy, double radiusLy, int seed) override;

	private:
		static cs::Ref<StarEntry> Copy(const cs::Ref<StarEntry>& e);

	public:
		cs::Ref<SystemSpec> Define(const cs::Ref<StarEntry>& star, int seed) override;

		cs::Ref<StarSystem> Build(const cs::Ref<StarEntry>& star, const cs::Ref<SystemSpec>& spec, double jd, const cs::Ref<Polities>& polities) override;
	};
}
