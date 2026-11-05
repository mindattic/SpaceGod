// US-T132: the painted sky of a place beyond Sol, made from its biome.

#include "Rendering/SystemSkyBake.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace SpaceGod::Rendering
{
	using Core::Biome;
	using Core::BiomeKind;
	using Core::BiomeRgb;

	namespace
	{
		constexpr double Pi = 3.14159265358979323846;

		// ---------------------------------------------------------------- small vector and colour helpers

		SkyVec V(double x, double y, double z) { return SkyVec{ x, y, z }; }
		SkyVec Add(SkyVec a, SkyVec b) { return V(a.X + b.X, a.Y + b.Y, a.Z + b.Z); }
		SkyVec Sub(SkyVec a, SkyVec b) { return V(a.X - b.X, a.Y - b.Y, a.Z - b.Z); }
		SkyVec Mul(SkyVec a, double s) { return V(a.X * s, a.Y * s, a.Z * s); }
		double Dot(SkyVec a, SkyVec b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
		SkyVec Cross(SkyVec a, SkyVec b) { return V(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X); }
		SkyVec Norm(SkyVec a)
		{
			double l = std::sqrt(Dot(a, a));
			return l > 1e-12 ? Mul(a, 1 / l) : V(0, 0, 1);
		}

		struct Rgb
		{
			double R = 0, G = 0, B = 0;
		};
		Rgb C(const BiomeRgb& c) { return Rgb{ c.R, c.G, c.B }; }
		Rgb C(double r, double g, double b) { return Rgb{ r, g, b }; }
		Rgb operator+(Rgb a, Rgb b) { return Rgb{ a.R + b.R, a.G + b.G, a.B + b.B }; }
		Rgb operator*(Rgb a, double s) { return Rgb{ a.R * s, a.G * s, a.B * s }; }
		Rgb operator*(Rgb a, Rgb b) { return Rgb{ a.R * b.R, a.G * b.G, a.B * b.B }; }
		Rgb Mix(Rgb a, Rgb b, double t) { return Rgb{ a.R + (b.R - a.R) * t, a.G + (b.G - a.G) * t, a.B + (b.B - a.B) * t }; }

		double Clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
		double Smooth(double e0, double e1, double x)
		{
			double t = Clamp01((x - e0) / (e1 - e0));
			return t * t * (3 - 2 * t);
		}

		// ---------------------------------------------------------------- random

		uint64_t Mix64(uint64_t z)
		{
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
			return z ^ (z >> 31);
		}

		struct Rng
		{
			uint64_t State;
			explicit Rng(uint64_t seed) : State(seed) {}
			double Next() { return (Mix64(State += 0x9E3779B97F4A7C15ULL) >> 11) * (1.0 / 9007199254740992.0); }
			double Range(double a, double b) { return a + (b - a) * Next(); }
			SkyVec Direction()
			{
				double z = Range(-1, 1), phi = Range(0, 2 * Pi), s = std::sqrt(std::max(0.0, 1 - z * z));
				return V(s * std::cos(phi), s * std::sin(phi), z);
			}
		};

		double Unit(uint64_t h) { return (h >> 11) * (1.0 / 9007199254740992.0); }

		uint64_t Hash(int64_t x, int64_t y, int64_t z, uint64_t salt)
		{
			uint64_t h = salt;
			h = Mix64(h ^ (static_cast<uint64_t>(x) * 0x9E3779B97F4A7C15ULL));
			h = Mix64(h ^ (static_cast<uint64_t>(y) * 0xC2B2AE3D27D4EB4FULL));
			h = Mix64(h ^ (static_cast<uint64_t>(z) * 0x165667B19E3779F9ULL));
			return h;
		}

		// ---------------------------------------------------------------- noise: Ken Perlin's improved noise (2002)

		const int* Perm()
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
			static const struct Table
			{
				int q[512];
				Table() { for (int i = 0; i < 512; i++) q[i] = p[i & 255]; }
			} table;
			return table.q;
		}

		double Fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
		double Lerp(double t, double a, double b) { return a + t * (b - a); }
		double Grad(int hash, double x, double y, double z)
		{
			int h = hash & 15;
			double u = h < 8 ? x : y, v = h < 4 ? y : h == 12 || h == 14 ? x : z;
			return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
		}

		/// About -1..1.
		double Perlin(double x, double y, double z)
		{
			const int* P = Perm();
			double fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
			int X = static_cast<int>(static_cast<int64_t>(fx) & 255), Y = static_cast<int>(static_cast<int64_t>(fy) & 255), Z = static_cast<int>(static_cast<int64_t>(fz) & 255);
			x -= fx; y -= fy; z -= fz;
			double u = Fade(x), v = Fade(y), w = Fade(z);
			int A = P[X] + Y, AA = P[A] + Z, AB = P[A + 1] + Z, B = P[X + 1] + Y, BA = P[B] + Z, BB = P[B + 1] + Z;
			return Lerp(w,
				Lerp(v, Lerp(u, Grad(P[AA], x, y, z), Grad(P[BA], x - 1, y, z)),
				        Lerp(u, Grad(P[AB], x, y - 1, z), Grad(P[BB], x - 1, y - 1, z))),
				Lerp(v, Lerp(u, Grad(P[AA + 1], x, y, z - 1), Grad(P[BA + 1], x - 1, y, z - 1)),
				        Lerp(u, Grad(P[AB + 1], x, y - 1, z - 1), Grad(P[BB + 1], x - 1, y - 1, z - 1))));
		}
		double Perlin(SkyVec p) { return Perlin(p.X, p.Y, p.Z); }

		/// Fractal noise, about -0.6..0.6.
		double Fbm(SkyVec p, int octaves)
		{
			double sum = 0, amp = 0.5, norm = 0;
			for (int i = 0; i < octaves; i++)
			{
				sum += Perlin(p) * amp;
				norm += amp;
				p = Mul(p, 2.03);
				p.X += 17.1;
				amp *= 0.5;
			}
			return sum / norm;
		}

		// ---------------------------------------------------------------- painted pieces

		/// Point stars: a jittered star per cell of a 3D grid the sky's sphere passes through; one layer.
		Rgb Stars(SkyVec d, double pixel, double cellsPerPixel, double odds, double bright, bool halos, uint64_t salt)
		{
			Rgb out;
			double n = 1.0 / (pixel * cellsPerPixel);
			SkyVec p = Mul(d, n);
			int64_t bx = static_cast<int64_t>(std::floor(p.X - 0.5)), by = static_cast<int64_t>(std::floor(p.Y - 0.5)), bz = static_cast<int64_t>(std::floor(p.Z - 0.5));
			double sigma = 0.75 * pixel;
			for (int i = 0; i < 8; i++)
			{
				int64_t cx = bx + (i & 1), cy = by + ((i >> 1) & 1), cz = bz + ((i >> 2) & 1);
				uint64_t h = Hash(cx, cy, cz, salt);
				if (Unit(h) > odds) continue;
				uint64_t h1 = Mix64(h + 1), h2 = Mix64(h + 2), h3 = Mix64(h + 3), h4 = Mix64(h + 4), h5 = Mix64(h + 5);
				SkyVec diff = Sub(p, V(cx + Unit(h1), cy + Unit(h2), cz + Unit(h3)));
				double along = Dot(diff, d);
				if (std::abs(along) > 0.35) continue; // off the shell: never drawn, so no star is cut by the cell window
				double perp2 = std::max(0.0, Dot(diff, diff) - along * along);
				double ang = std::sqrt(perp2) / n;
				double mag = std::pow(Unit(h4), 9.0) * bright + 0.05 * bright;
				double v = mag * std::exp(-ang * ang / (2 * sigma * sigma));
				if (halos) v += mag * 0.05 * std::exp(-ang / (pixel * 3.5));
				if (v < 1e-4) continue;
				double t = Unit(h5);
				Rgb tint = t < 0.3 ? C(0.70, 0.80, 1.0) : t < 0.82 ? C(1.0, 0.97, 0.93) : C(1.0, 0.80, 0.60);
				out = out + tint * v;
			}
			return out;
		}

		Rgb Galaxy(const SkyGalaxy& g, SkyVec d)
		{
			double c = Dot(d, g.Direction);
			if (c < std::cos(g.Size * 4)) return Rgb{};
			double x = Dot(d, g.U) / g.Size, y = Dot(d, g.V) / g.Size;
			y /= std::max(0.18, std::cos(g.Tilt));
			double r = std::sqrt(x * x + y * y), th = std::atan2(y, x);
			double arms = std::pow(0.5 + 0.5 * std::cos(2 * th - g.Spin * std::log(r + 0.08) * 3), 2.5);
			double disk = std::exp(-r * 2.6), core = std::exp(-r * r * 40);
			double v = g.Brightness * (disk * (0.25 + 0.75 * arms) + core * 1.4);
			return Mix(C(0.72, 0.80, 1.0), C(1.0, 0.86, 0.66), std::min(1.0, core * 1.5)) * v;
		}

		Rgb Sun(const SkySun& s, SkyVec d, double pixel)
		{
			double ang = std::acos(std::clamp(Dot(d, s.Direction), -1.0, 1.0));
			if (ang > 1.2) return Rgb{};
			double a = s.AngularRadius;
			double disc = Clamp01((a - ang) / std::max(pixel, a * 0.08));
			double glow = 0.9 * std::exp(-ang / (a * 1.8)) + 0.35 * std::exp(-ang / (a * 9)) + 0.12 * std::exp(-ang / (a * 40));
			return C(s.Color) * (disc * 3.0 + glow);
		}

		/// A world painted into the sky over <paramref name="behind"/>: its lit face, rim, halo and ring, with the gas in
		/// front of it (<paramref name="haze"/>) laid over at <paramref name="hazeShare"/>.
		Rgb World(const SkyWorld& w, const SkyLayout& sky, SkyVec d, double pixel, Rgb behind, Rgb haze, double hazeShare)
		{
			double sinA = std::sin(w.AngularRadius);
			double reach = w.Ringed ? 2.4 : 1.25;
			if (Dot(d, w.Direction) < std::cos(std::min(1.5, w.AngularRadius * reach))) return behind;
			double dist = 1 / sinA;
			SkyVec centre = Mul(w.Direction, dist);
			double b = Dot(d, centre);
			double m2 = std::max(0.0, dist * dist - b * b);
			double m = std::sqrt(m2);
			Rgb out = behind;
			bool hit = b > 0 && m2 < 1;
			double tHit = hit ? b - std::sqrt(1 - m2) : 1e30;
			const Biome& biome = sky.Biome;

			// Halo: the lit edge of the atmosphere scattering past the limb.
			if (b > 0 && m < 1.25)
			{
				SkyVec edge = Norm(Sub(Mul(d, b), centre));
				double facing = Smooth(-0.35, 0.7, Dot(edge, w.Sunward));
				double beyond = std::max(0.0, m - 1) * sinA;
				out = out + C(w.Atmosphere) * (std::exp(-beyond / (w.AngularRadius * 0.045)) * facing * 0.85);
			}
			if (hit)
			{
				SkyVec n = Norm(Sub(Mul(d, tHit), centre));
				double ndl = Dot(n, w.Sunward);
				double lit = Smooth(-0.12, 0.65, ndl);
				SkyVec q = Add(Mul(n, 2.6), V(w.Phase, w.Phase * 0.7, -w.Phase * 1.3));
				Rgb albedo;
				if (w.Banded)
				{
					double lat = Dot(n, w.Pole);
					double turb = Fbm(q, 4);
					double tone = 0.5 + 0.5 * std::sin(lat * 15 + turb * 5.5);
					double fine = Fbm(Mul(q, 3.2), 3);
					albedo = Mix(C(w.Dark), C(w.Light), Clamp01(tone * 0.75 + fine * 0.6 + 0.15));
				}
				else
				{
					double h = Fbm(q, 5);
					double craters = 1 - std::abs(Perlin(Mul(q, 3.4)));
					albedo = Mix(C(w.Dark), C(w.Light), Smooth(-0.35, 0.4, h)) * (0.78 + 0.35 * craters * craters);
				}
				Rgb face = albedo * (0.02 + 1.05 * lit);
				if (w.Lava)
				{
					double vein = std::pow(1 - std::abs(Perlin(Mul(q, 1.2))), 22.0);
					face = face + C(biome.Hot) * (vein * (0.35 + 0.9 * (1 - lit)));
				}
				// The gas around it lights the night side a little; the atmosphere glows at the rim.
				face = face + C(biome.Deep) * (0.6 * (1 - lit));
				double view = std::max(0.0, -Dot(d, n));
				double rim = std::pow(1 - view, 2.5) * (0.1 + 0.9 * Smooth(-0.3, 0.45, ndl));
				face = face + C(w.Atmosphere) * (rim * 0.95);
				face = Mix(face, haze, hazeShare);
				double alpha = Clamp01((1 - m) * sinA / pixel + 0.5);
				out = Mix(out, face, alpha);
			}
			if (w.Ringed)
			{
				double denom = Dot(d, w.Pole);
				if (std::abs(denom) > 1e-6)
				{
					double t = Dot(centre, w.Pole) / denom;
					if (t > 0 && t < tHit)
					{
						double rr = std::sqrt(Dot(Sub(Mul(d, t), centre), Sub(Mul(d, t), centre)));
						if (rr > 1.35 && rr < 2.3)
						{
							double bands = Clamp01(0.55 + 0.6 * Perlin(rr * 7, w.Phase, 0.5) + 0.25 * Perlin(rr * 31, w.Phase, 3.5));
							double a = (0.25 + 0.5 * bands) * Smooth(1.35, 1.45, rr) * Smooth(2.3, 2.1, rr);
							// The ring's far side (behind the world) is in the world's shadow when the light is behind it.
							SkyVec at = Norm(Sub(Mul(d, t), centre));
							double lit = 0.35 + 0.65 * Smooth(-0.6, 0.4, Dot(at, w.Sunward));
							Rgb ring = Mix(C(w.Light), C(biome.Hot), 0.35) * lit;
							out = Mix(out, Mix(ring, haze, hazeShare), a);
						}
					}
				}
			}
			return out;
		}

		BiomeRgb Scale(BiomeRgb c, float s) { return BiomeRgb{ std::min(1.0f, c.R * s), std::min(1.0f, c.G * s), std::min(1.0f, c.B * s) }; }
		BiomeRgb Blend(BiomeRgb a, BiomeRgb b, float t) { return BiomeRgb{ a.R + (b.R - a.R) * t, a.G + (b.G - a.G) * t, a.B + (b.B - a.B) * t }; }

		/// A direction <paramref name="angle"/> radians from <paramref name="from"/>, at a random bearing.
		SkyVec Around(SkyVec from, double angle, Rng& rng)
		{
			SkyVec u = Norm(Cross(from, std::abs(from.Z) < 0.9 ? V(0, 0, 1) : V(1, 0, 0)));
			SkyVec v = Cross(from, u);
			double bearing = rng.Range(0, 2 * Pi);
			SkyVec side = Add(Mul(u, std::cos(bearing)), Mul(v, std::sin(bearing)));
			return Norm(Add(Mul(from, std::cos(angle)), Mul(side, std::sin(angle))));
		}

		SkyWorld MakeWorld(const Biome& b, SkyVec dir, double radius, bool giant, SkyVec heart, Rng& rng)
		{
			SkyWorld w;
			w.Direction = dir;
			w.AngularRadius = radius;
			w.Pole = Norm(Add(V(0, 0, 1), Mul(rng.Direction(), 0.45)));
			// Lit from the heart, which lies beyond it: farther the heart, more of the world is turned away (a crescent).
			double beyond = rng.Range(0.2, 2.2);
			w.Sunward = Norm(Sub(Mul(heart, beyond), dir));
			w.Phase = rng.Range(-50, 50);
			w.Banded = giant && rng.Next() < 0.6;
			w.Lava = !w.Banded && b.Kind == BiomeKind::CrimsonInferno && rng.Next() < 0.75;
			if (w.Banded)
			{
				w.Light = Blend(b.Hot, b.Mid, static_cast<float>(rng.Range(0.2, 0.6)));
				w.Dark = rng.Next() < 0.5 ? Scale(b.Mid, 0.55f) : Scale(b.Accent, 0.6f);
			}
			else
			{
				// Rock: the palette washed toward grey.
				BiomeRgb grey{ 0.55f, 0.53f, 0.5f };
				w.Light = Blend(grey, b.Mid, static_cast<float>(rng.Range(0.25, 0.55)));
				w.Dark = Scale(Blend(grey, b.Deep, 0.5f), 0.35f);
				if (w.Lava) w.Dark = BiomeRgb{ 0.12f, 0.03f, 0.02f };
			}
			if (b.Kind == BiomeKind::BlackVoid)
			{
				w.Light = BiomeRgb{ 0.5f, 0.5f, 0.52f };
				w.Dark = BiomeRgb{ 0.04f, 0.04f, 0.045f };
			}
			w.Atmosphere = giant ? Blend(b.Hot, b.Accent, static_cast<float>(rng.Range(0.0, 0.5))) : Scale(b.Mid, 0.5f);
			w.Ringed = giant && (b.Kind == BiomeKind::BinarySunset || (w.Banded && rng.Next() < 0.3));
			return w;
		}
	}

	SkyLayout SystemSkyBake::Layout(const Biome& biome)
	{
		SkyLayout s;
		s.Biome = biome;
		Rng rng(biome.Seed ^ 0x5C1EB0A7ULL);
		BiomeKind k = biome.Kind;
		s.Band = rng.Direction();
		s.BandWidth = rng.Range(0.32, 0.65);
		SkyVec r = rng.Direction();
		// The heart sits in or near the main wall of gas, as a nebula's brightest knot does.
		s.Heart = Norm(Sub(r, Mul(s.Band, Dot(r, s.Band) * 0.8)));
		s.HeartU = Norm(Cross(s.Heart, std::abs(s.Heart.Z) < 0.9 ? V(0, 0, 1) : V(1, 0, 0)));
		s.HeartV = Cross(s.Heart, s.HeartU);
		s.AccentAt = rng.Direction();
		// The glare streak lies along the ecliptic's sense of level, as a lens would draw it in the game's view.
		s.Streak = Norm(Sub(V(0, 0, 1), Mul(s.Heart, s.Heart.Z)));
		s.Offset = V(rng.Range(-200, 200), rng.Range(-200, 200), rng.Range(-200, 200));
		s.Scale = rng.Range(1.1, 1.8);
		s.Warp = rng.Range(0.5, 0.9);
		s.HeartGlow = 1.0;
		s.Stars = 1.0;
		s.Filaments = 0;
		switch (k)
		{
			case BiomeKind::CrimsonInferno: s.Shafts = 0.9; s.StreakStrength = 0.6; break;
			case BiomeKind::GoldenDust: s.Shafts = 1.0; s.StreakStrength = 0.9; break;
			case BiomeKind::IcyCyan: s.Shafts = 0.8; s.StreakStrength = 1.0; break;
			case BiomeKind::EmeraldGas: s.Shafts = 0.6; s.StreakStrength = 0.35; break;
			case BiomeKind::VioletStorm: s.Shafts = 0.5; s.StreakStrength = 0.35; s.Filaments = 0.7; break;
			case BiomeKind::AzureReef: s.Shafts = 0.6; s.StreakStrength = 0.4; break;
			case BiomeKind::RoseTide: s.Shafts = 0.6; s.StreakStrength = 0.4; break;
			case BiomeKind::BinarySunset: s.Shafts = 0.7; s.StreakStrength = 0.7; break;
			case BiomeKind::BlackVoid: s.Shafts = 0; s.StreakStrength = 0; s.HeartGlow = 0.12; s.Stars = 1.25; s.Filaments = 0; break;
			default: break;
		}

		// Giant worlds and their moons, at infinity: placed off the heart so it lights them from beside or behind.
		bool giant = k == BiomeKind::BinarySunset || (k != BiomeKind::BlackVoid ? rng.Next() < 0.85 : rng.Next() < 0.35);
		if (giant)
		{
			double radius = k == BiomeKind::BlackVoid ? rng.Range(0.05, 0.1) : rng.Range(0.12, 0.3);
			SkyVec dir = Around(s.Heart, radius + rng.Range(0.15, 0.7), rng);
			s.Worlds.push_back(MakeWorld(biome, dir, radius, true, s.Heart, rng));
			int moons = static_cast<int>(rng.Range(0, 3));
			for (int i = 0; i < moons; i++)
				s.Worlds.push_back(MakeWorld(biome, Around(dir, radius * rng.Range(1.8, 3.6), rng), rng.Range(0.012, 0.035), false, s.Heart, rng));
			if (k != BiomeKind::BlackVoid && rng.Next() < 0.25)
			{
				double r2 = rng.Range(0.04, 0.09);
				s.Worlds.push_back(MakeWorld(biome, Around(Mul(s.Heart, -1), rng.Range(0.3, 1.2), rng), r2, true, s.Heart, rng));
			}
		}
		if (k == BiomeKind::BinarySunset && !s.Worlds.empty())
		{
			// The partner sun sets on the giant's limb: the heart moves to it, so the shafts and the fill come from it too.
			const SkyWorld& g = s.Worlds[0];
			SkySun sun;
			sun.AngularRadius = rng.Range(0.012, 0.022);
			sun.Direction = Around(g.Direction, g.AngularRadius * 1.02, rng);
			sun.Color = biome.Hot;
			s.Suns.push_back(sun);
			s.Heart = sun.Direction;
			s.HeartU = Norm(Cross(s.Heart, std::abs(s.Heart.Z) < 0.9 ? V(0, 0, 1) : V(1, 0, 0)));
			s.HeartV = Cross(s.Heart, s.HeartU);
			s.Streak = Norm(Sub(V(0, 0, 1), Mul(s.Heart, s.Heart.Z)));
			for (auto& w : s.Worlds) w.Sunward = Norm(Sub(Mul(s.Heart, 2.0), w.Direction));
		}

		int galaxies = k == BiomeKind::BlackVoid ? static_cast<int>(rng.Range(3, 6)) : static_cast<int>(rng.Range(1, 4));
		for (int i = 0; i < galaxies; i++)
		{
			SkyGalaxy g;
			g.Direction = rng.Direction();
			g.U = Norm(Cross(g.Direction, std::abs(g.Direction.Z) < 0.9 ? V(0, 0, 1) : V(1, 0, 0)));
			g.V = Cross(g.Direction, g.U);
			double turn = rng.Range(0, 2 * Pi);
			SkyVec u = Add(Mul(g.U, std::cos(turn)), Mul(g.V, std::sin(turn)));
			g.V = Cross(g.Direction, u);
			g.U = u;
			g.Size = rng.Range(0.012, 0.045);
			g.Tilt = rng.Range(0.0, 1.35);
			g.Spin = rng.Range(1.2, 2.8) * (rng.Next() < 0.5 ? -1 : 1);
			g.Brightness = k == BiomeKind::BlackVoid ? rng.Range(0.25, 0.5) : rng.Range(0.12, 0.3);
			s.Galaxies.push_back(g);
		}
		return s;
	}

	void SystemSkyBake::Sample(const SkyLayout& sky, SkyVec d, double pixel, double& r, double& g, double& b)
	{
		const Biome& bio = sky.Biome;
		Rgb col;
		double dens = 0, lanes = 0, band = 0;
		Rgb gasLight;
		double hd = Dot(d, sky.Heart);
		double heartNear = std::pow(Clamp01(hd * 0.5 + 0.5), 3.0);

		if (bio.Gas > 0.1)
		{
			SkyVec q = Add(Mul(d, sky.Scale), sky.Offset);
			SkyVec warp = V(Fbm(Add(q, V(5.2, 1.3, 7.7)), 3), Fbm(Add(q, V(1.7, 9.2, 3.4)), 3), Fbm(Add(q, V(8.3, 2.8, 4.1)), 3));
			SkyVec w = Add(q, Mul(warp, sky.Warp));
			double n = Fbm(w, 5);
			double base = Clamp01(n * 1.15 + 0.5);
			double bd = Dot(d, sky.Band) / sky.BandWidth;
			band = std::exp(-bd * bd);
			double structure = std::max(band, heartNear * 0.9);
			double field = base * 0.8 + structure * 0.55 * bio.Gas + (bio.Gas - 0.8) * 0.4 - 0.32;
			dens = Smooth(0.0, 0.7, field);

			double t1 = Smooth(0.05, 0.6, dens), t2 = Smooth(0.45, 1.0, dens) * (0.3 + 0.7 * heartNear);
			Rgb c = Mix(C(bio.Deep), C(bio.Mid), t1);
			c = Mix(c, C(bio.Hot), t2 * 0.85);
			double pool = std::pow(Clamp01(Dot(d, sky.AccentAt) * 0.5 + 0.5), 2.5);
			double acc = Smooth(0.42, 0.85, Fbm(Add(Mul(w, 1.3), V(31, 7, 13)), 3) * 1.2 + 0.5) * pool * (0.4 + dens);
			c = Mix(c, C(bio.Accent), std::min(0.85, acc));
			double detail = 0.7 + 0.6 * Clamp01(Fbm(Add(Mul(w, 4.0), V(3, 41, 19)), 3) + 0.5);
			double emit = bio.Glow * (0.03 + std::pow(dens, 1.6) * (0.4 + 0.8 * heartNear)) * detail;
			// Dark dust: soft clouds in front of the glow, with ragged edges, as a nebula's dark lanes are.
			double dust = Fbm(Add(Mul(w, 1.6), V(61, 23, 5)), 5);
			lanes = Smooth(0.06, 0.3, dust) * Smooth(0.08, 0.45, dens) * 0.9;
			emit *= 1 - lanes * 0.85;
			gasLight = c * emit;
			col = gasLight + C(bio.Dust) * (lanes * 0.2);
			if (sky.Filaments > 0)
			{
				double fl = std::pow(1 - std::abs(Perlin(Add(Mul(w, 3.1), V(11, 77, 29)))), 26.0) * sky.Filaments * Smooth(0.15, 0.7, dens);
				col = col + C(bio.Accent) * (fl * 0.9) + C(bio.Hot) * (fl * 0.3);
			}
			col = col + C(bio.Deep) * ((0.25 + 0.5 * base) * bio.Gas * 0.6) + C(bio.Mid) * (0.035 * bio.Gas * (0.5 + band));
		}

		// The heart: a blazing knot, its glare, a lens streak and light shafts through the gas.
		double ang = std::acos(std::clamp(hd, -1.0, 1.0));
		double heart = sky.HeartGlow * (2.2 * std::exp(-ang / 0.005) + 0.55 * std::exp(-ang / 0.045) + 0.22 * std::exp(-ang / 0.22) + 0.07 * std::exp(-ang / 0.8));
		col = col + C(bio.Hot) * (heart * std::max(0.3, static_cast<double>(bio.Glow)));
		if (sky.StreakStrength > 0 && hd > 0)
		{
			double s = Dot(d, sky.Streak);
			double streak = sky.StreakStrength * (std::exp(-(s / 0.0025) * (s / 0.0025)) + 0.12 * std::exp(-(s / 0.02) * (s / 0.02))) * std::exp(-ang / 0.22);
			col = col + Mix(C(bio.Hot), C(1, 1, 1), 0.5) * (streak * 1.2);
		}
		if (sky.Shafts > 0 && hd > -0.3)
		{
			double x = Dot(d, sky.HeartU), y = Dot(d, sky.HeartV);
			double len = std::sqrt(x * x + y * y) + 1e-9;
			double cx = x / len, cy = y / len;
			double rays = Perlin(cx * 5 + 3.1, cy * 5 + 7.3, 0.37) * 0.5 + 0.5;
			double fine = Perlin(cx * 14 + 1.7, cy * 14 + 4.9, 2.71) * 0.5 + 0.5;
			double ray = std::pow(rays * 0.7 + fine * 0.3, 3.0) * 2.4;
			double fall = std::exp(-ang / 0.7) * Smooth(0.0, 0.06, ang);
			double shaft = sky.Shafts * ray * fall * (0.3 + 0.7 * dens) * (1 - lanes * 0.7);
			col = col + C(bio.Hot) * (shaft * 0.45);
		}

		// Stars: thicker along the wall, dimmed where the gas is thickest.
		double veil = 1 - 0.75 * Smooth(0.4, 1.0, dens);
		double crowd = 1 + 1.6 * band;
		Rgb stars = Stars(d, pixel, 4.5, std::min(0.95, 0.32 * crowd * sky.Stars), 0.8 * sky.Stars, false, bio.Seed ^ 0x51A55ULL)
			+ Stars(d, pixel, 15.0, std::min(0.95, 0.5 * crowd), 2.6 * sky.Stars, true, bio.Seed ^ 0xB16B055ULL);
		col = col + stars * veil;
		for (const auto& gx : sky.Galaxies) col = col + Galaxy(gx, d) * veil;
		for (const auto& sun : sky.Suns) col = col + Sun(sun, d, pixel);

		// Worlds over all of that, with a little of the lit gas still in front of them.
		Rgb haze = gasLight * 1.4 + C(bio.Deep) * 0.3;
		for (const auto& w : sky.Worlds) col = World(w, sky, d, pixel, col, haze, 0.12 + 0.25 * dens);
		// The partner sun sets behind the world's limb but its glare still spills over it.
		for (const auto& sun : sky.Suns)
		{
			double a = std::acos(std::clamp(Dot(d, sun.Direction), -1.0, 1.0));
			col = col + C(sun.Color) * (0.5 * std::exp(-a / (sun.AngularRadius * 2.5)));
		}

		// A soft shoulder so the hot heart and the shafts roll off instead of clipping flat.
		r = 1 - std::exp(-col.R * 1.15);
		g = 1 - std::exp(-col.G * 1.15);
		b = 1 - std::exp(-col.B * 1.15);
	}

	std::vector<std::vector<uint8_t>> SystemSkyBake::Bake(const SkyLayout& sky, int width, int threads, const std::atomic<bool>* cancel)
	{
		std::vector<std::vector<uint8_t>> levels;
		if (width < 8 || (width & (width - 1)) != 0) return levels;
		int height = width / 2;
		std::vector<uint8_t> top(static_cast<size_t>(width) * height * 4);
		double pixel = 2 * Pi / width;
		std::atomic<int> next{ 0 };
		std::atomic<bool> stopped{ false };
		auto toByte = [](double v)
		{
			int i = static_cast<int>(v * 255 + 0.5);
			return static_cast<uint8_t>(i < 0 ? 0 : i > 255 ? 255 : i);
		};
		auto work = [&]()
		{
			for (;;)
			{
				if (cancel && cancel->load()) { stopped = true; return; }
				int row = next.fetch_add(1);
				if (row >= height) return;
				double lat = (0.5 - (row + 0.5) / height) * Pi;
				double cl = std::cos(lat), sl = std::sin(lat);
				uint8_t* o = top.data() + static_cast<size_t>(row) * width * 4;
				for (int col = 0; col < width; col++, o += 4)
				{
					// u = 0.5 - longitude / 2pi, as the Sky shader reads it.
					double lon = (0.5 - (col + 0.5) / width) * 2 * Pi;
					double r, g, b;
					Sample(sky, V(cl * std::cos(lon), cl * std::sin(lon), sl), pixel, r, g, b);
					o[0] = toByte(b);
					o[1] = toByte(g);
					o[2] = toByte(r);
					o[3] = 255;
				}
			}
		};
		{
			std::vector<std::thread> pool;
			for (int t = 1; t < std::max(1, threads); t++) pool.emplace_back(work);
			work();
			for (auto& t : pool) t.join();
		}
		if (stopped) return levels;
		levels.push_back(std::move(top));
		for (int w = width, h = height; w > 1 || h > 1;)
		{
			int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
			const std::vector<uint8_t>& from = levels.back();
			std::vector<uint8_t> to(static_cast<size_t>(nw) * nh * 4);
			for (int y = 0; y < nh; y++)
				for (int x = 0; x < nw; x++)
					for (int c = 0; c < 4; c++)
					{
						int x0 = std::min(w - 1, x * 2), x1 = std::min(w - 1, x * 2 + 1), y0 = std::min(h - 1, y * 2), y1 = std::min(h - 1, y * 2 + 1);
						int s = from[(static_cast<size_t>(y0) * w + x0) * 4 + c] + from[(static_cast<size_t>(y0) * w + x1) * 4 + c]
							+ from[(static_cast<size_t>(y1) * w + x0) * 4 + c] + from[(static_cast<size_t>(y1) * w + x1) * 4 + c];
						to[(static_cast<size_t>(y) * nw + x) * 4 + c] = static_cast<uint8_t>((s + 2) / 4);
					}
			levels.push_back(std::move(to));
			w = nw;
			h = nh;
		}
		return levels;
	}
}
