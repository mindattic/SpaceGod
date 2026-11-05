#pragma once
// US-T132: the painted sky of a place beyond Sol, made from its biome.

#include "Systems/Biomes.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace SpaceGod::Rendering
{
	/// <summary>A direction or colour in the sky bake (plain doubles: the bake runs on worker threads, away from Unreal).</summary>
	struct SkyVec
	{
		double X = 0, Y = 0, Z = 0;
	};

	/// <summary>A giant world or moon painted into the sky: at infinity, so it looks the same at real and game scale.</summary>
	struct SkyWorld
	{
		SkyVec Direction, Pole;
		/// <summary>Where its light comes from: toward the sky's heart, seen from the world, so its lit side and rim agree with the sky.</summary>
		SkyVec Sunward;
		double AngularRadius = 0;
		Core::BiomeRgb Light, Dark, Atmosphere;
		bool Banded = false, Ringed = false, Lava = false;
		double Phase = 0; // noise offset
	};

	/// <summary>A distant sun painted into the sky (a binary's partner): a disc and its glare.</summary>
	struct SkySun
	{
		SkyVec Direction;
		double AngularRadius = 0;
		Core::BiomeRgb Color;
	};

	/// <summary>A far galaxy: a tilted spiral smudge.</summary>
	struct SkyGalaxy
	{
		SkyVec Direction, U, V;
		double Size = 0, Tilt = 0, Spin = 0, Brightness = 0;
	};

	/// <summary>
	/// The layout of a place's sky (US-T132), drawn from its biome and seed: where its gas walls run, where its bright heart
	/// is (the fill light comes from there), its painted worlds, suns and galaxies. Directions are ecliptic unit vectors
	/// (z north), as every other skybox's frame (SkyFrame::Ecliptic).
	/// </summary>
	struct SkyLayout
	{
		Core::Biome Biome;
		SkyVec Heart;      // the sky's brightest point: shafts and streak start there, the fill light comes from it
		SkyVec HeartU, HeartV; // two axes square to the heart (the shafts' angle around it)
		SkyVec Band;       // the normal of the great circle the main gas wall runs along
		double BandWidth = 0.5;
		SkyVec AccentAt;   // where the second gas colour pools
		SkyVec Streak;     // the normal of the glare streak's great circle through the heart
		SkyVec Offset;     // noise offset
		double Scale = 2, Warp = 0.8, Shafts = 0, StreakStrength = 0, HeartGlow = 0, Stars = 1, Filaments = 0;
		std::vector<SkyWorld> Worlds;
		std::vector<SkySun> Suns;
		std::vector<SkyGalaxy> Galaxies;
	};

	/// <summary>
	/// Paints a place's sky (US-T132) as an equirectangular panorama in the ecliptic frame, the layout the Sky shader
	/// reads: nebula walls and gas clouds (domain-warped noise), dust lanes, light shafts and a glare streak from the
	/// heart, dense star fields thicker along the wall, distant galaxies, and giant worlds and moons with their rims lit
	/// from the heart. Pure and thread-safe; deterministic for a seed.
	/// </summary>
	class SystemSkyBake
	{
	public:
		/// <summary>The layout of the sky of a place with <paramref name="biome"/>.</summary>
		static SkyLayout Layout(const Core::Biome& biome);

		/// <summary>The sky's colour (display, 0..1) toward ecliptic unit direction <paramref name="d"/>; <paramref name="pixel"/> is
		/// the angular size of a pixel in radians (stars are drawn about that wide).</summary>
		static void Sample(const SkyLayout& sky, SkyVec d, double pixel, double& r, double& g, double& b);

		/// <summary>
		/// The panorama, <paramref name="width"/> by width/2 (a power of two, 8 or more), as BGRA8 levels top row first (latitude
		/// +90 at the top, longitude 0 in the middle, rising to the left), the whole mip chain, ready for TextureFactory::FromMips.
		/// Rows are made on <paramref name="threads"/> threads; <paramref name="cancel"/> stops it (an empty result).
		/// </summary>
		static std::vector<std::vector<uint8_t>> Bake(const SkyLayout& sky, int width, int threads, const std::atomic<bool>* cancel = nullptr);
	};
}
