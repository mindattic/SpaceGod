// Port of Unity.SpaceGod Assets/UnitySpace/Core/Rendering/ProceduralSky.cs (see docs/MIGRATION.md).

#include "Rendering/ProceduralSky.h"

#include "Cs/Cs.h"
#include "Systems/ProceduralSystems.h"

#include <thread>
#include <vector>

namespace SpaceGod::Core
{
	uint64_t ProceduralSky::SeedFor(const cs::String& systemName)
	{
		uint64_t h = 0xCBF29CE484222325ULL;
		// foreach (char c in systemName ?? ""): the UTF-16 code units, as C#'s char.
		cs::ForEachUtf16(systemName, [&](uint32_t c)
		{
			h ^= c;
			h *= 0x100000001B3ULL;
		});
		return SeededRandom::Mix(static_cast<long long>(h), 595);
	}

	cs::List<NebulaLayer> ProceduralSky::Layers(uint64_t seed)
	{
		SeededRandom rng(seed);
		cs::List<NebulaLayer> layers;
		do
		{
			// The nine draws in C#'s left-to-right argument order.
			double r = rng.NextDouble(), g = rng.NextDouble(), b = rng.NextDouble();
			double scale = rng.NextDouble() * 0.5 + 0.25;
			double intensity = rng.NextDouble() * 0.2 + 0.9;
			double falloff = rng.NextDouble() * 3.0 + 3.0;
			double ox = rng.NextDouble() * 2000 - 1000;
			double oy = rng.NextDouble() * 2000 - 1000;
			double oz = rng.NextDouble() * 2000 - 1000;
			layers.Add(NebulaLayer(r, g, b, scale, intensity, falloff, ox, oy, oz));
		} while (rng.NextDouble() >= 0.5 && layers.Count() < 6);
		return layers;
	}

	void ProceduralSky::Sample(const cs::List<NebulaLayer>& layers, double x, double y, double z, double& r, double& g, double& b)
	{
		r = g = b = 0;
		for (int i = 0; i < layers.Count(); i++)
		{
			const auto& l = layers[i];
			double c = cs::Math::Min(1.0, Nebula(x * l.Scale + l.OffsetX, y * l.Scale + l.OffsetY, z * l.Scale + l.OffsetZ) * l.Intensity);
			c = cs::Math::Pow(cs::Math::Max(0.0, c), l.Falloff);
			r = r * (1 - c) + l.R * c;
			g = g * (1 - c) + l.G * c;
			b = b * (1 - c) + l.B * c;
		}
	}

	cs::List<uint8_t> ProceduralSky::BakePanorama(uint64_t seed, int width, cs::List<int>& mipOffsets, const std::atomic<bool>* cancel)
	{
		if (width < 4 || (width & (width - 1)) != 0) throw cs::ArgumentException("A power of two, 4 or more. (Parameter 'width')");
		int height = width / 2;
		auto layers = Layers(seed);
		cs::List<cs::List<int>> levels;
		int total = 0;
		for (int w = width, h = height; ; w = cs::Math::Max(1, w / 2), h = cs::Math::Max(1, h / 2))
		{
			levels.Add(cs::List<int>{ w, h, total });
			total += w * h * 4;
			if (w == 1 && h == 1) break;
		}
		auto data = cs::NewArray<uint8_t>(total);
		// Parallel.For(0, height, { MaxDegreeOfParallelism = Math.Max(1, Environment.ProcessorCount - 2) }, row => ...):
		// each row writes only its own bytes, so the result does not depend on which thread makes which row.
		int cores = static_cast<int>(std::thread::hardware_concurrency());
		int workers = cs::Math::Max(1, cores - 2);
		std::atomic<int> nextRow{ 0 };
		std::atomic<bool> cancelled{ false };
		uint8_t* bytes = data.Raw().data();
		auto work = [&]()
		{
			for (;;)
			{
				if (cancel != nullptr && cancel->load()) { cancelled = true; return; }
				int row = nextRow.fetch_add(1);
				if (row >= height) return;
				double lat = ((row + 0.5) / height - 0.5) * cs::Math::PI;
				double cl = cs::Math::Cos(lat), sl = cs::Math::Sin(lat);
				int o = row * width * 4;
				for (int col = 0; col < width; col++, o += 4)
				{
					// u = 0.5 - longitude / 2pi, as SkyboxCatalog.PanoramaUv reads it.
					double lon = (0.5 - (col + 0.5) / width) * 2 * cs::Math::PI;
					double r, g, b;
					Sample(layers, cl * cs::Math::Cos(lon), cl * cs::Math::Sin(lon), sl, r, g, b);
					bytes[o] = ToByte(r);
					bytes[o + 1] = ToByte(g);
					bytes[o + 2] = ToByte(b);
					bytes[o + 3] = 255;
				}
			}
		};
		{
			std::vector<std::thread> threads;
			for (int t = 1; t < workers; t++) threads.emplace_back(work);
			work();
			for (auto& t : threads) t.join();
		}
		if (cancelled) throw cs::InvalidOperationException("The operation was canceled.");
		for (int i = 1; i < levels.Count(); i++) Downsample(data, levels[i - 1], levels[i]);
		mipOffsets = cs::NewArray<int>(levels.Count());
		for (int i = 0; i < levels.Count(); i++) mipOffsets[i] = levels[i][2];
		return data;
	}

	uint8_t ProceduralSky::ToByte(double v) { return static_cast<uint8_t>(cs::Math::Max(0, cs::Math::Min(255, cs::ToInt(v * 255 + 0.5)))); }

	void ProceduralSky::Downsample(cs::List<uint8_t>& data, const cs::List<int>& from, const cs::List<int>& to)
	{
		int fw = from[0], fh = from[1], fo = from[2], tw = to[0], th = to[1], too = to[2];
		uint8_t* d = data.Raw().data(); // unchecked indexing for speed: every index stays inside its level, as in C#
		for (int y = 0; y < th; y++)
			for (int x = 0; x < tw; x++)
				for (int c = 0; c < 4; c++)
				{
					int x0 = cs::Math::Min(fw - 1, x * 2), x1 = cs::Math::Min(fw - 1, x * 2 + 1), y0 = cs::Math::Min(fh - 1, y * 2), y1 = cs::Math::Min(fh - 1, y * 2 + 1);
					int s = d[fo + (y0 * fw + x0) * 4 + c] + d[fo + (y0 * fw + x1) * 4 + c] + d[fo + (y1 * fw + x0) * 4 + c] + d[fo + (y1 * fw + x1) * 4 + c];
					d[too + (y * tw + x) * 4 + c] = static_cast<uint8_t>((s + 2) / 4);
				}
	}

	double ProceduralSky::Nebula(double x, double y, double z)
	{
		double scale = 64; // 2^6
		double dx = 0, dy = 0, dz = 0;
		for (int i = 0; i < 6; i++)
		{
			double nx = Noise(x * scale + dx, y * scale + dy, z * scale + dz);
			double ny = Noise(y * scale + dx, z * scale + dy, x * scale + dz);
			double nz = Noise(z * scale + dx, x * scale + dy, y * scale + dz);
			dx = nx; dy = ny; dz = nz;
			scale *= 0.5;
		}
		return Noise(x * scale + dx, y * scale + dy, z * scale + dz);
	}

	const cs::List<int>& ProceduralSky::P()
	{
		static const cs::List<int> value = BuildPermutation();
		return value;
	}

	cs::List<int> ProceduralSky::BuildPermutation()
	{
		static const int p[] =
		{
			151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
			247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
			74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
			65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
			52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
			119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
			218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
			184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
		};
		auto q = cs::NewArray<int>(512);
		for (int i = 0; i < 512; i++) q[i] = p[i & 255];
		return q;
	}

	double ProceduralSky::Perlin(double x, double y, double z)
	{
		const int* P = ProceduralSky::P().Raw().data(); // the table, unchecked for speed (every index is below 512)
		double fx = cs::Math::Floor(x), fy = cs::Math::Floor(y), fz = cs::Math::Floor(z);
		int X = static_cast<int>(cs::ToLong(fx) & 255), Y = static_cast<int>(cs::ToLong(fy) & 255), Z = static_cast<int>(cs::ToLong(fz) & 255);
		x -= fx; y -= fy; z -= fz;
		double u = Fade(x), v = Fade(y), w = Fade(z);
		int A = P[X] + Y, AA = P[A] + Z, AB = P[A + 1] + Z, B = P[X + 1] + Y, BA = P[B] + Z, BB = P[B + 1] + Z;
		return Lerp(w,
			Lerp(v, Lerp(u, Grad(P[AA], x, y, z), Grad(P[BA], x - 1, y, z)),
			        Lerp(u, Grad(P[AB], x, y - 1, z), Grad(P[BB], x - 1, y - 1, z))),
			Lerp(v, Lerp(u, Grad(P[AA + 1], x, y, z - 1), Grad(P[BA + 1], x - 1, y, z - 1)),
			        Lerp(u, Grad(P[AB + 1], x, y - 1, z - 1), Grad(P[BB + 1], x - 1, y - 1, z - 1))));
	}

	double ProceduralSky::Grad(int hash, double x, double y, double z)
	{
		int h = hash & 15;
		double u = h < 8 ? x : y, v = h < 4 ? y : h == 12 || h == 14 ? x : z;
		return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
	}
}
