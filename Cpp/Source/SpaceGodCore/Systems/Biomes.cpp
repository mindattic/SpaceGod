// US-T132: the biome of every place beyond Sol.

#include "Systems/Biomes.h"

#include <algorithm>

namespace SpaceGod::Core
{
	namespace
	{
		uint64_t SplitMix(uint64_t& state)
		{
			uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
			return z ^ (z >> 31);
		}

		float Unit(uint64_t& state) { return static_cast<float>((SplitMix(state) >> 11) * (1.0 / 9007199254740992.0)); }

		struct Row
		{
			BiomeKind Kind;
			const char* Id;
			const char* Name;
			const char* Mood;
			float Weight;
			BiomeRgb Deep, Mid, Hot, Accent, Dust;
			float Gas, Glow;
			BiomeRgb KeyTint;
			float Fill;
			BiomeRgb HazeColor;
			float Haze;
			BiomeFields Fields; // Nebula, IceField, Graveyard, Asteroids, Dust, Storms, Heat
			const char* Hazards;
		};

		// The biomes beyond Sol and how often each turns up (Weight). Art direction: Freelancer's painted systems and Black
		// Prophecy's colour-drowned ones, where the sky is a wall of lit gas and every hull sits in its light.
		const Row Table[] = {
			{ BiomeKind::Sol, "sol", "Sol", "Home: the real sky and the Sun's own light.", 0,
				{ 0, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1 }, { 0, 0, 0 }, { 0, 0, 0 }, 0, 0, { 1, 1, 1 }, 0, { 0, 0, 0 }, 0,
				{ 0, 0, 0, 0.3f, 0, 0, 0 }, "" },
			{ BiomeKind::CrimsonInferno, "crimson-inferno", "Crimson Inferno", "A furnace sky: walls of red gas lit from a white-hot heart.", 1,
				{ 0.10f, 0.012f, 0.008f }, { 0.72f, 0.11f, 0.03f }, { 1.0f, 0.66f, 0.26f }, { 0.95f, 0.34f, 0.05f }, { 0.05f, 0.005f, 0.0f },
				0.85f, 1.0f, { 1.0f, 0.64f, 0.46f }, 0.45f, { 0.62f, 0.09f, 0.02f }, 0.5f,
				{ 0.8f, 0, 0.3f, 0.4f, 0.3f, 0.3f, 1.0f }, "Stellar heat, burning gas, flares" },
			{ BiomeKind::GoldenDust, "golden-dust", "Golden Dust", "Amber haze thick with dust, a sun glaring through it.", 1,
				{ 0.10f, 0.05f, 0.01f }, { 0.80f, 0.46f, 0.11f }, { 1.0f, 0.93f, 0.64f }, { 0.92f, 0.60f, 0.24f }, { 0.12f, 0.06f, 0.02f },
				0.8f, 1.0f, { 1.0f, 0.83f, 0.56f }, 0.45f, { 0.72f, 0.43f, 0.12f }, 0.55f,
				{ 0.4f, 0, 0.6f, 0.8f, 1.0f, 0.1f, 0.3f }, "Dust banks, poor sensor range, rock fields" },
			{ BiomeKind::IcyCyan, "icy-cyan", "Icy Cyan", "A cold blue-white glow, frost and wreckage drifting in it.", 1,
				{ 0.012f, 0.06f, 0.09f }, { 0.22f, 0.56f, 0.72f }, { 0.88f, 0.98f, 1.0f }, { 0.46f, 0.76f, 0.96f }, { 0.02f, 0.05f, 0.07f },
				0.7f, 0.9f, { 0.76f, 0.92f, 1.0f }, 0.4f, { 0.30f, 0.60f, 0.76f }, 0.45f,
				{ 0.5f, 1.0f, 0.7f, 0.3f, 0.3f, 0.1f, 0 }, "Ice fields, comet debris, cold" },
			{ BiomeKind::EmeraldGas, "emerald-gas", "Emerald Gas", "Green gas giants of cloud, banked in teal.", 1,
				{ 0.0f, 0.06f, 0.04f }, { 0.11f, 0.58f, 0.33f }, { 0.76f, 1.0f, 0.62f }, { 0.14f, 0.60f, 0.66f }, { 0.0f, 0.03f, 0.02f },
				0.85f, 0.95f, { 0.82f, 1.0f, 0.76f }, 0.4f, { 0.16f, 0.55f, 0.30f }, 0.45f,
				{ 1.0f, 0.1f, 0.2f, 0.4f, 0.3f, 0.4f, 0.1f }, "Nebula clouds that blind sensors, toxic gas" },
			{ BiomeKind::VioletStorm, "violet-storm", "Violet Storm", "Purple storm fronts torn by electric-blue filaments.", 1,
				{ 0.05f, 0.0f, 0.10f }, { 0.46f, 0.13f, 0.72f }, { 0.96f, 0.68f, 1.0f }, { 0.26f, 0.46f, 1.0f }, { 0.03f, 0.0f, 0.05f },
				0.8f, 1.0f, { 0.86f, 0.72f, 1.0f }, 0.45f, { 0.40f, 0.16f, 0.60f }, 0.45f,
				{ 0.8f, 0, 0.3f, 0.2f, 0.2f, 1.0f, 0.1f }, "Ion storms, lightning, radiation" },
			{ BiomeKind::AzureReef, "azure-reef", "Azure Reef", "Deep blue nebula reefs with pink tidal edges.", 1,
				{ 0.0f, 0.02f, 0.10f }, { 0.11f, 0.31f, 0.86f }, { 0.66f, 0.88f, 1.0f }, { 0.86f, 0.36f, 0.70f }, { 0.0f, 0.01f, 0.05f },
				0.75f, 0.9f, { 0.80f, 0.88f, 1.0f }, 0.4f, { 0.13f, 0.30f, 0.80f }, 0.4f,
				{ 0.8f, 0.4f, 0.2f, 0.6f, 0.2f, 0.2f, 0 }, "Nebula reefs, rock fields, ice" },
			{ BiomeKind::RoseTide, "rose-tide", "Rose Tide", "Magenta gas washing against a teal shore.", 1,
				{ 0.08f, 0.01f, 0.06f }, { 0.80f, 0.23f, 0.50f }, { 1.0f, 0.82f, 0.86f }, { 0.16f, 0.66f, 0.70f }, { 0.05f, 0.0f, 0.03f },
				0.8f, 0.95f, { 1.0f, 0.80f, 0.88f }, 0.4f, { 0.70f, 0.25f, 0.50f }, 0.4f,
				{ 0.9f, 0.1f, 0.2f, 0.5f, 0.4f, 0.2f, 0.1f }, "Nebula, dust, rock fields" },
			{ BiomeKind::BinarySunset, "binary-sunset", "Binary Sunset", "A second sun setting behind a ringed giant in a dusk sky.", 0.8f,
				{ 0.07f, 0.02f, 0.10f }, { 0.80f, 0.33f, 0.30f }, { 1.0f, 0.82f, 0.46f }, { 0.46f, 0.21f, 0.66f }, { 0.04f, 0.01f, 0.05f },
				0.55f, 0.9f, { 1.0f, 0.79f, 0.60f }, 0.6f, { 0.70f, 0.30f, 0.35f }, 0.35f,
				{ 0.4f, 0.1f, 0.3f, 0.7f, 0.3f, 0.2f, 0.6f }, "Two suns' heat, rock fields" },
			{ BiomeKind::BlackVoid, "black-void", "Black Void", "Nothing but black and one blazing star.", 0.6f,
				{ 0.0f, 0.0f, 0.0f }, { 0.05f, 0.05f, 0.07f }, { 1.0f, 0.97f, 0.92f }, { 0.10f, 0.10f, 0.15f }, { 0.0f, 0.0f, 0.0f },
				0.05f, 0.3f, { 1.0f, 1.0f, 1.0f }, 0.04f, { 0.0f, 0.0f, 0.0f }, 0.0f,
				{ 0.05f, 0, 0.5f, 0.2f, 0, 0.1f, 0.2f }, "Isolation: no cover, nothing to hide in" },
		};
		static_assert(sizeof(Table) / sizeof(Table[0]) == static_cast<int>(BiomeKind::Count), "one row per biome");

		BiomeRgb Vary(BiomeRgb c, uint64_t& state)
		{
			// Each place's palette is its biome's, nudged a little channel by channel so no two skies match exactly.
			auto one = [&](float v) { return std::clamp(v * (1.0f + (Unit(state) - 0.5f) * 0.16f), 0.0f, 1.0f); };
			c.R = one(c.R);
			c.G = one(c.G);
			c.B = one(c.B);
			return c;
		}
	}

	uint64_t Biomes::SeedFor(const std::string& placeName)
	{
		uint64_t h = 0xCBF29CE484222325ULL;
		for (unsigned char c : placeName)
		{
			h ^= c;
			h *= 0x100000001B3ULL;
		}
		uint64_t s = h ^ 0x5132A7E1B10E5EEDULL;
		return SplitMix(s);
	}

	BiomeKind Biomes::KindOf(const std::string& placeName)
	{
		if (IsSol(placeName)) return BiomeKind::Sol;
		uint64_t state = SeedFor(placeName) ^ 0xB10E;
		float total = 0;
		for (const Row& r : Table) total += r.Weight;
		float pick = Unit(state) * total;
		for (const Row& r : Table)
		{
			if (r.Weight <= 0) continue;
			if (pick < r.Weight) return r.Kind;
			pick -= r.Weight;
		}
		return BiomeKind::CrimsonInferno;
	}

	Biome Biomes::Of(BiomeKind kind, uint64_t seed)
	{
		int i = std::clamp(static_cast<int>(kind), 0, static_cast<int>(BiomeKind::Count) - 1);
		const Row& r = Table[i];
		Biome b;
		b.Kind = r.Kind;
		b.Id = r.Id;
		b.Name = r.Name;
		b.Mood = r.Mood;
		b.Seed = seed;
		uint64_t state = seed ^ 0xC0105ULL;
		bool vary = kind != BiomeKind::Sol;
		b.Deep = vary ? Vary(r.Deep, state) : r.Deep;
		b.Mid = vary ? Vary(r.Mid, state) : r.Mid;
		b.Hot = vary ? Vary(r.Hot, state) : r.Hot;
		b.Accent = vary ? Vary(r.Accent, state) : r.Accent;
		b.Dust = r.Dust;
		b.Gas = r.Gas;
		b.Glow = r.Glow;
		b.KeyTint = r.KeyTint;
		b.Fill = r.Fill;
		b.HazeColor = r.HazeColor;
		b.Haze = r.Haze;
		b.Fields = r.Fields;
		b.Hazards = r.Hazards;
		return b;
	}

	Biome Biomes::For(const std::string& placeName) { return Of(KindOf(placeName), SeedFor(placeName)); }

	const char* Biomes::ToString(BiomeKind kind)
	{
		int i = static_cast<int>(kind);
		return i >= 0 && i < static_cast<int>(BiomeKind::Count) ? Table[i].Id : "";
	}
}
