#pragma once
// US-T449: the frontier. Past the neighbourhood people know, the galaxy grows less charted, less travelled and stranger
// the farther out it lies from Sol: its stars are put on the charts only nearer and nearer the pilot, nobody's traffic
// goes there, a system's worlds are unknown until a scanner or a visit reveals them, and more and more of its systems
// are odd -- pulsars, black holes, white dwarfs in their shells, brown dwarf pairs, rogue planets, binaries and triples,
// stars in nebulae, comet clouds, worlds on wild orbits and, rarest, the ruin of something ancient.
//
// All of it is the generated galaxy's (ProceduralSystems): the hand-built systems, the real catalogue stars and the void
// stations are as they always were. Whether a generated star is strange, and how, comes from its own seed and where it is
// (Classify), on a stream of its own, so every star that is not strange is exactly the star it was, every peer of a co-op
// game makes the same strange ones, and a star let go and reached again is the same. A strange star is charted as an
// unidentified object (Chart) and furnished when it is built (Furnish); what it is is saved with its catalogue entry
// (SystemSpec::Oddity). Nothing simulated near Sol changes: the Bench never comes within StartLy of any of it.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Systems/StarSystemSpec.h"

namespace SpaceGod::Core
{
	class BodyDefinition;
	class StarEntry;
	class StarSystem;

	class SPACEGODCORE_API Frontier
	{
	public:
		/// <summary>Where the frontier begins, light-years from Sol: past the settled systems and the known neighbourhood.</summary>
		static constexpr double StartLy = 50;

		/// <summary>Where it is at its deepest (<see cref="Depth"/> 1), light-years from Sol.</summary>
		static constexpr double FullLy = 400;

		/// <summary>How deep into the frontier a place lies: 0 inside <see cref="StartLy"/>, rising to 1 at <see cref="FullLy"/>.</summary>
		static double Depth(DVec3 ly);

		/// <summary>The bands the galaxy map draws the gradient in, light-years from Sol (the last is <see cref="FullLy"/>).</summary>
		static constexpr double BandLy[4] = { StartLy, 100, 200, FullLy };

		/// <summary>What the map calls the space inside each band's edge.</summary>
		static const char* BandName(int band);

		/// <summary>The share of generated stars at <paramref name="depth"/> that are strange: none inside the frontier.</summary>
		static double OddChance(double depth) { return depth <= 0 ? 0.0 : cs::Math::Min(0.6, 0.06 + 0.5 * depth); }

		/// <summary>
		/// Whether the generated star of <paramref name="starSeed"/> at <paramref name="ly"/> is strange, and how: from its own
		/// stream, so the same star is always the same, and every star that is not strange is untouched.
		/// </summary>
		static FrontierKind Classify(uint64_t starSeed, DVec3 ly);

		/// <summary>
		/// How near the pilot a frontier star is put on the charts (<see cref="Galaxy.Explore"/>): the chart radius, cut
		/// down the deeper the star lies, to no less than <see cref="MinChartLy"/>. Out there, little is known until you go.
		/// </summary>
		static double ChartReachLy(double chartLy, double depth) { return cs::Math::Max(MinChartLy, chartLy * (1 - 0.7 * depth)); }
		static constexpr double MinChartLy = 5;

		/// <summary>A strange star as the charts show it from afar: an unidentified object, its light what an observer would see.</summary>
		static void Chart(StarEntry& e, FrontierKind kind);

		/// <summary>
		/// Makes the freshly defined <paramref name="spec"/> (a generated lone star with its planets and habitat site) into a
		/// system of <paramref name="kind"/>, from its own stream, and marks it (<see cref="SystemSpec.Oddity"/>).
		/// </summary>
		static void Furnish(SystemSpec& spec, FrontierKind kind, uint64_t starSeed, int seed);

		/// <summary>Tells the views' derived data what a strange system holds (its nebula or comet cloud): when it is defined or loaded.</summary>
		static void Note(const SystemSpec& spec);

		/// <summary>What is strange about <paramref name="s"/> (None for an ordinary system).</summary>
		static FrontierKind KindOf(const StarSystem& s);

		/// <summary>
		/// Whether <paramref name="s"/> is uncharted ground: out on the frontier, unsettled, no relay: its worlds are unknown
		/// until a scanner or a visit reveals them (the views hide their names and markers until then).
		/// </summary>
		static bool IsUncharted(const StarSystem& s);

		/// <summary>How near (km) a body of an uncharted system must come to the eye, the helm or a ping to be revealed.</summary>
		static double RevealKm(const BodyDefinition& b);
		static constexpr double RevealMinKm = 2.0e7;
		static constexpr double RevealRadii = 6000;

		/// <summary>The kind's name in a line ("Pulsar").</summary>
		static const char* Title(FrontierKind kind);

		/// <summary>What a surveyor would say of it, a sentence or two.</summary>
		static const char* Describe(FrontierKind kind);

		/// <summary>What survey data of the phenomenon is worth before distance, credits (<see cref="Exploration"/>).</summary>
		static double SurveyValue(FrontierKind kind);

		// What the remnants are, as their star's spectral type reads (saved with every body, so the views need nothing else).
		static constexpr const char* PulsarSpectral = "PSR";
		static constexpr const char* BlackHoleSpectral = "BH";
		static constexpr const char* RogueSpectral = "Rogue";
		/// <summary>The ending of a relic's body name.</summary>
		static constexpr const char* RelicSuffix = " Relic";

		static bool IsPulsar(const BodyDefinition& b);
		static bool IsBlackHole(const BodyDefinition& b);
		static bool IsRogue(const BodyDefinition& b);
		static bool IsRelic(const BodyDefinition& b);

		/// <summary>The Schwarzschild radius of a black hole of <paramref name="massSolar"/>, km.</summary>
		static double HorizonKm(double massSolar) { return 2.953 * massSolar; }

		/// <summary>A main-sequence star's spectral type ("K4V") from its temperature.</summary>
		static cs::String MainSequenceType(double teffK);
	};
}
