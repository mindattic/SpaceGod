#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Rendering/ProceduralSky.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

#include <atomic>
#include <cstdint>

namespace SpaceGod::Core
{
	/// <summary>One nebula layer of a seeded sky: its colour and the noise it is cut from.</summary>
	struct NebulaLayer
	{
		double R = 0, G = 0, B = 0, Scale = 0, Intensity = 0, Falloff = 0, OffsetX = 0, OffsetY = 0, OffsetZ = 0;

		NebulaLayer() = default;
		NebulaLayer(double r, double g, double b, double scale, double intensity, double falloff, double ox, double oy, double oz)
		{
			R = r; G = g; B = b;
			Scale = scale; Intensity = intensity; Falloff = falloff;
			OffsetX = ox; OffsetY = oy; OffsetZ = oz;
		}
	};

	/// <summary>
	/// A star system's own sky (U.SPC-T595): layered, domain-warped Perlin nebulae made from the system's seed, ported from
	/// wwwtyro's Space 3D (github.com/wwwtyro/space-3d, Unlicense): one to a few layers, each a random colour at a random
	/// scale, intensity and falloff, blended over black. The same name always gives the same sky. It is laid out as an
	/// ecliptic-frame panorama (<see cref="SkyFrame.Ecliptic"/>) so the view draws it like any other; the point-cloud
	/// stars are drawn over it. Pure and thread-safe: the view bakes it on a worker thread, so it never stalls a frame.
	/// </summary>
	class SPACEGODCORE_API ProceduralSky
	{
	public:
		/// <summary>The seed a system's sky is made from: its name, hashed the same on every runtime (FNV-1a).</summary>
		static uint64_t SeedFor(const cs::String& systemName);

		/// <summary>The nebula layers of the sky made from <paramref name="seed"/>: at least one, each further one at even odds.</summary>
		static cs::List<NebulaLayer> Layers(uint64_t seed);

		/// <summary>The sky's colour (0..1 each) toward ecliptic direction (<paramref name="x"/>, <paramref name="y"/>, <paramref name="z"/>), unit length.</summary>
		static void Sample(const cs::List<NebulaLayer>& layers, double x, double y, double z, double& r, double& g, double& b);

		/// <summary>
		/// The sky as an RGBA32 panorama <paramref name="width"/> by width/2 (row 0 at the bottom, latitude -90), with its whole
		/// mip chain after it, ready to upload: <paramref name="mipOffsets"/> gives where each level starts. Rows are made in parallel
		/// (leaving two cores to the game); <paramref name="cancel"/> stops a bake no longer wanted.
		/// </summary>
		// C#'s CancellationToken is a flag here (null: never cancelled); a cancelled bake throws, as Parallel.For's
		// OperationCanceledException does (Cs.h has none: cs::InvalidOperationException stands in).
		static cs::List<uint8_t> BakePanorama(uint64_t seed, int width, cs::List<int>& mipOffsets, const std::atomic<bool>* cancel = nullptr);

	private:
		static uint8_t ToByte(double v);

		static void Downsample(cs::List<uint8_t>& data, const cs::List<int>& from, const cs::List<int>& to);

		/// <summary>Space 3D's nebula: six octaves of noise, each displacing the next, then one more read at the base scale.</summary>
		static double Nebula(double x, double y, double z);

		/// <summary>Perlin noise remapped to about 0..1, as Space 3D's <c>0.5 * cnoise + 0.5</c>.</summary>
		static double Noise(double x, double y, double z) { return 0.5 * Perlin(x, y, z) + 0.5; }

		// Ken Perlin's improved noise (2002), with his reference permutation.
		static const cs::List<int>& P();

		static cs::List<int> BuildPermutation();

		static double Perlin(double x, double y, double z);

		static double Fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
		static double Lerp(double t, double a, double b) { return a + t * (b - a); }

		static double Grad(int hash, double x, double y, double z);
	};
}
