#pragma once
// US-T132: the biome of every place beyond Sol.

#include "Cs/Cs.h" // SPACEGODCORE_API

#include <cstdint>
#include <string>

namespace SpaceGod::Core
{
	/// <summary>
	/// What a place beyond Sol looks and feels like (US-T132). Outside Sol the galaxy is not a simulation of real catalog
	/// skies: every system and every void station is its own wild, painted place, and its biome is the one choice that sets
	/// its sky, the colour of its light, its fog and dust, and the hazards and fields it leans to. Sol is <see cref="Sol"/>
	/// (the real sky and light, untouched).
	/// </summary>
	enum class BiomeKind : int
	{
		Sol,
		CrimsonInferno,
		GoldenDust,
		IcyCyan,
		EmeraldGas,
		VioletStorm,
		AzureReef,
		RoseTide,
		BinarySunset,
		BlackVoid,
		Count,
	};

	/// <summary>A colour in display terms, 0..1 each.</summary>
	struct BiomeRgb
	{
		float R = 0, G = 0, B = 0;
	};

	/// <summary>
	/// The fields and hazards a biome leans to, each 0 (never) to 1 (its signature), for content to read: the void-content
	/// work picks ice fields, nebulas or graveyards to match. Advisory and read-only; nothing in the simulation reads them.
	/// </summary>
	struct BiomeFields
	{
		float Nebula = 0;     // gas clouds and nebula volumes
		float IceField = 0;   // ice and comet fields
		float Graveyard = 0;  // wreck graveyards, debris
		float Asteroids = 0;  // rock fields
		float Dust = 0;       // dust banks, low visibility
		float Storms = 0;     // ion or plasma storms, lightning, radiation
		float Heat = 0;       // stellar heat, flares, burning gas
	};

	/// <summary>
	/// One place's biome (US-T132). <see cref="Biomes.For"/> makes the same one from the same name on every run and every
	/// machine. The palette is the sky's (display colours); the light is graded to it: the star's light is tinted by
	/// <see cref="KeyTint"/>, a fill of <see cref="Mid"/> comes from the sky's bright heart, and <see cref="Haze"/> is the
	/// fog and dust tint laid over the frame.
	/// </summary>
	struct Biome
	{
		BiomeKind Kind = BiomeKind::Sol;

		/// <summary>A stable id ("crimson-inferno"), for saves, logs and content tables.</summary>
		const char* Id = "sol";

		/// <summary>The name shown to the player ("Crimson Inferno").</summary>
		const char* Name = "Sol";

		/// <summary>One line of what it is like.</summary>
		const char* Mood = "";

		/// <summary>The place's own seed (its name hashed): the layout of its sky and anything else drawn from it.</summary>
		uint64_t Seed = 0;

		/// <summary>The sky's palette: the dark of its gas, its body, its hot heart, a second gas colour and its dust lanes.</summary>
		BiomeRgb Deep, Mid, Hot, Accent, Dust;

		/// <summary>How much of the sky its gas fills (0 bare, 1 walled in) and how bright it glows.</summary>
		float Gas = 0, Glow = 0;

		/// <summary>The star's light is multiplied by this.</summary>
		BiomeRgb KeyTint{ 1, 1, 1 };

		/// <summary>How strong the fill from the sky's heart is, against the star's light (0..1).</summary>
		float Fill = 0;

		/// <summary>The fog and dust tint laid over everything (its colour, and how strong, 0..1).</summary>
		BiomeRgb HazeColor;
		float Haze = 0;

		BiomeFields Fields;

		/// <summary>The hazards in words, for the manual and tooltips.</summary>
		const char* Hazards = "";
	};

	/// <summary>
	/// The biome lookup (US-T132): read-only and pure, the same answer for the same name every time. A place is named as
	/// the galaxy names it: a system ("Alpha Centauri"), a void station ("Void Station 3"), or any other place with a
	/// stable name. Sol (named "Sol System" or "Sol") is always <see cref="BiomeKind.Sol"/>.
	/// </summary>
	class SPACEGODCORE_API Biomes
	{
	public:
		/// <summary>The biome of the place named <paramref name="placeName"/>.</summary>
		static Biome For(const std::string& placeName);

		/// <summary>The biome of <paramref name="kind"/>, with the palette varied by <paramref name="seed"/> as a place's is.</summary>
		static Biome Of(BiomeKind kind, uint64_t seed);

		/// <summary>The kind a place is, without the rest.</summary>
		static BiomeKind KindOf(const std::string& placeName);

		/// <summary>Whether this is Sol, which keeps its real sky and light.</summary>
		static bool IsSol(const std::string& placeName) { return placeName == "Sol System" || placeName == "Sol"; }

		/// <summary>The place's seed: its name hashed (FNV-1a over its bytes, then mixed).</summary>
		static uint64_t SeedFor(const std::string& placeName);

		static const char* ToString(BiomeKind kind);
	};
}
