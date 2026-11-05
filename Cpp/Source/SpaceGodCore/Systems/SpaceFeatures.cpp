// US-T130: the nebulae and ice fields of a place (see SpaceFeatures.h).

#include "Systems/SpaceFeatures.h"

#include "Astro.h"
#include "Systems/VoidSites.h"
#include "Systems/VoidStations.h"

#include <map>
#include <mutex>

namespace SpaceGod::Core
{
	namespace
	{
		/// <summary>The place's own stream: SplitMix64 from the FNV-1a of its name and a tag.</summary>
		class Stream
		{
			uint64_t state;

		public:
			explicit Stream(const cs::String& name)
			{
				uint64_t h = 0xCBF29CE484222325ull;
				for (unsigned char c : name) { h ^= c; h *= 0x100000001B3ull; }
				state = h ^ 0xD1B54A32D192ED03ull;
			}

			uint64_t Next()
			{
				uint64_t z = (state += 0x9E3779B97F4A7C15ull);
				z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
				z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
				return z ^ (z >> 31);
			}

			double U() { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }
			double Range(double a, double b) { return a + (b - a) * U(); }
			double Gauss() { return (U() + U() + U() + U() - 2.0) * 1.2; }
			DVec3 InBall()
			{
				for (;;)
				{
					DVec3 v(Range(-1, 1), Range(-1, 1), Range(-1, 1));
					if (v.LengthSquared() <= 1) return v;
				}
			}
		};

		/// <summary>Nebula palettes: two or three colours each, the gas's emission lines as a painter would push them.</summary>
		const cs::List<cs::List<Rgb>>& Palettes()
		{
			static const cs::List<cs::List<Rgb>> p = {
				{ Rgb(0.95f, 0.3f, 0.55f), Rgb(0.35f, 0.25f, 0.9f), Rgb(1.0f, 0.6f, 0.4f) },
				{ Rgb(0.2f, 0.85f, 0.8f), Rgb(0.3f, 0.45f, 1.0f), Rgb(0.7f, 0.95f, 0.6f) },
				{ Rgb(1.0f, 0.45f, 0.15f), Rgb(0.85f, 0.2f, 0.25f), Rgb(1.0f, 0.8f, 0.4f) },
				{ Rgb(0.6f, 0.3f, 0.95f), Rgb(0.95f, 0.35f, 0.75f), Rgb(0.3f, 0.75f, 1.0f) },
				{ Rgb(0.3f, 0.95f, 0.45f), Rgb(0.15f, 0.55f, 0.75f), Rgb(0.85f, 0.95f, 0.4f) },
			};
			return p;
		}

		Nebula MakeNebula(Stream& rng, const cs::String& name, DVec3 centre, double radius, int puffs)
		{
			Nebula n;
			n.Name = name;
			n.CentreKm = centre;
			n.RadiusKm = radius;
			const auto& palette = Palettes()[static_cast<int>(rng.Next() % static_cast<uint64_t>(Palettes().Count()))];
			// A few knots where the gas is thickest, each with layers of puffs round it, and a thin haze between them.
			int knots = 3 + static_cast<int>(rng.Next() % 3);
			cs::List<DVec3> at;
			for (int k = 0; k < knots; k++) at.Add(centre + rng.InBall() * (radius * 0.55));
			for (int i = 0; i < puffs; i++)
			{
				NebulaPuff p;
				bool haze = i % 4 == 3;
				auto knot = at[i % knots];
				p.OffsetKm = haze ? centre + rng.InBall() * (radius * 0.85)
					: knot + DVec3(rng.Gauss(), rng.Gauss(), rng.Gauss() * 0.6) * (radius * 0.18);
				p.RadiusKm = radius * (haze ? rng.Range(0.3, 0.5) : rng.Range(0.1, 0.3));
				auto c = palette[static_cast<int>(rng.Next() % static_cast<uint64_t>(palette.Count()))];
				float shade = static_cast<float>(rng.Range(0.75, 1.15));
				p.Color = Rgb(cs::Math::Min(1.0f, c.R * shade), cs::Math::Min(1.0f, c.G * shade), cs::Math::Min(1.0f, c.B * shade));
				p.Alpha = static_cast<float>(haze ? rng.Range(0.08, 0.16) : rng.Range(0.16, 0.34));
				n.Puffs.Add(p);
			}
			return n;
		}

		IceFieldCloud MakeIceField(Stream& rng, const cs::String& name, DVec3 centre, double radius, int chunks)
		{
			IceFieldCloud f;
			f.Name = name;
			f.CentreKm = centre;
			f.RadiusKm = radius;
			for (int i = 0; i < chunks; i++)
			{
				IceChunk c;
				// Flattened toward the field's mid-plane, thicker at its heart.
				auto p = rng.InBall();
				c.OffsetKm = centre + DVec3(p.X, p.Y, p.Z * 0.35) * radius;
				c.RadiusKm = 0.05 * cs::Math::Pow(40.0, rng.U() * rng.U());
				c.Seed = static_cast<uint32_t>(rng.Next());
				f.Chunks.Add(c);
			}
			return f;
		}

		PlaceFeatures Build(const cs::String& root)
		{
			PlaceFeatures f;
			// Sol is kept as it really is.
			if (root.empty() || root == "Sun" || root == "Sol" || root == "Sol System") return f;
			Stream rng(cs::Cat(root, "/features"));
			if (cs::Strings::StartsWith(root, VoidStations::Prefix))
			{
				// A void station's nebula is centred on its generator, wrapping the haven's rock (VoidSites).
				auto plan = VoidSites::Plan(root);
				if (plan.Mix.Nebula && plan.NebulaRadiusKm > 0)
					f.Nebulae.Add(MakeNebula(rng, plan.NebulaName, DVec3::Zero, plan.NebulaRadiusKm, 90));
				return f;
			}
			// Void Bases (the gate relays) stay bare.
			if (cs::Strings::StartsWith(root, "Void Base")) return f;
			// A star system: sometimes a nebula a few AU out, sometimes a cloud of ice chunks.
			auto Away = [&](double loAu, double hiAu)
			{
				double a = rng.Range(0, cs::Math::PI * 2), z = rng.Range(-0.25, 0.25);
				double d = rng.Range(loAu, hiAu) * Astro::AuKm;
				return DVec3(cs::Math::Cos(a), cs::Math::Sin(a), z).Normalized() * d;
			};
			if (rng.U() < 0.35)
			{
				auto centre = Away(3, 14);
				static const cs::List<cs::String> clouds = { "Veil", "Shroud", "Bloom", "Pall", "Caul", "Gauze", "Haze", "Plume" };
				f.Nebulae.Add(MakeNebula(rng, cs::Cat("the ", root, " ", clouds[static_cast<int>(rng.Next() % 8)]), centre,
					rng.Range(0.8, 3.0) * Astro::AuKm, 90));
			}
			if (rng.U() < 0.3)
			{
				auto centre = Away(4, 20);
				f.IceFields.Add(MakeIceField(rng, cs::Cat(root, " Ice Field"), centre, rng.Range(0.2, 0.8) * Astro::AuKm, 420));
			}
			return f;
		}

		std::mutex& Gate() { static std::mutex m; return m; }
		// Derived, never saved: rebuilt from the place's name whenever it is needed.
		std::map<cs::String, PlaceFeatures>& Cache() { static std::map<cs::String, PlaceFeatures> m; return m; }
	}

	double Nebula::DensityAt(DVec3 offsetKm) const
	{
		if ((offsetKm - CentreKm).LengthSquared() > RadiusKm * RadiusKm * 2.25) return 0;
		double d = 0;
		for (const auto& p : Puffs)
		{
			double r2 = (offsetKm - p.OffsetKm).LengthSquared() / (p.RadiusKm * p.RadiusKm);
			if (r2 < 9) d += p.Alpha * 4 * cs::Math::Exp(-r2);
		}
		return d;
	}

	const PlaceFeatures& SpaceFeatures::Of(const cs::String& root)
	{
		std::lock_guard<std::mutex> lock(Gate());
		auto& cache = Cache();
		auto it = cache.find(root);
		if (it != cache.end()) return it->second;
		return cache[root] = Build(root);
	}

	double SpaceFeatures::SensorMask(const cs::String& root, DVec3 offsetKm)
	{
		const auto& f = Of(root);
		double density = 0;
		for (const auto& n : f.Nebulae) density += n.DensityAt(offsetKm);
		if (density <= 0) return 1;
		return cs::Math::Max(DeepMask, 1 / (1 + 3 * density));
	}

	double SpaceFeatures::HavenScanKm(double reachKm, double maskAtHaven, double maskAtScanner, bool pinging)
	{
		double mask = cs::Math::Min(maskAtHaven, maskAtScanner);
		if (mask >= 0.999) return reachKm;
		// In a nebula a haven gives itself away only to a hull close enough to see it, or to a ping's sweep.
		return pinging ? cs::Math::Min(reachKm * mask * 3, PingInNebulaKm) : cs::Math::Min(reachKm * mask, FlyInKm);
	}
}
