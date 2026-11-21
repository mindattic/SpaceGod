// US-T447: the world's effects: exhaust clouds, boost puffs, RCS vapour, venting breaches, a fight's sparks, debris,
// shockwaves and smoke, gate transits and drives engaging. Tunables in Views/EffectsTune.h; see docs/Effects.md.
#include "Views/WorldFx.h"
#include "Views/EffectsTune.h"

#include "UnitySpaceApp.h"
#include "PlayerOptions.h"
#include "Rendering/Materials.h"
#include "Rendering/Pipeline/RenderBackend.h"
#include "UI/UiMotion.h"

#include <algorithm>
#include <cmath>

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using Tune = EffectsTune;

	namespace
	{
		/// <summary>M_Fx's shapes (Fx.ush): a radial glow, a beam across a ribbon, a thin ring.</summary>
		constexpr int ShapeRadial = 0, ShapeBeam = 1, ShapeRing = -1, ShapeStreak = 10; // ShapeRing: Fx.ush's thin ring (US-T447)

		float EaseOut(float t)
		{
			float u = 1 - Mathf::Clamp01(t);
			return 1 - u * u;
		}

		Color Scaled(Color c, float k) { return Color(c.r * k, c.g * k, c.b * k, c.a); }
	}

	WorldFx::WorldFx(UnitySpaceApp* app_, const cs::Ref<Transform>& parent)
		: app(app_)
	{
		auto make = [&](const char* name, const cs::Ref<Material>& material, cs::Ref<Mesh>& mesh)
		{
			auto go = NewGameObject(name);
			go->transform->SetParent(parent, false);
			auto m = cs::New<Mesh>(name);
			m->indexFormat = IndexFormat::UInt32;
			mesh = Materials::Own(m);
			mesh->MarkDynamic();
			AddComponent<MeshFilter>(go)->sharedMesh = mesh;
			auto mr = AddComponent<MeshRenderer>(go);
			mr->SetSharedMaterial(material);
			mr->shadowCastingMode = ShadowCastingMode::Off;
			mr->receiveShadows = false;
		};
		// The clouds: translucent, lit by the star, drawn back to front (M_Smoke, sorted under the additive glows).
		make("World FX clouds", Materials::Create(Rendering::RenderBackend::Active()->LoadShader("Smoke"), "WorldFx clouds"), smokeMesh);
		make("World FX glows", Materials::Create(Materials::Fx(), "WorldFx glows"), glowMesh);
		clouds.reserve(1024);
		sparks.reserve(512);
	}

	float WorldFx::Rand()
	{
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return static_cast<float>(rng & 0xFFFFFF) / static_cast<float>(0x1000000);
	}

	Vector3 WorldFx::RandDir()
	{
		for (int k = 0; k < 8; k++)
		{
			Vector3 v(Rand(-1, 1), Rand(-1, 1), Rand(-1, 1));
			float m = v.sqrMagnitude();
			if (m > 1e-4f && m <= 1) return v / Mathf::Sqrt(m);
		}
		return Vector3::up();
	}

	Vector3 WorldFx::Scatter(Vector3 dir, float spread)
	{
		auto v = dir + RandDir() * spread;
		return v.sqrMagnitude() > 1e-8f ? v.normalized() : dir;
	}

	bool WorldFx::Calm() const { return UI::UiMotion::ProfileReducesMotion(); }

	float WorldFx::Flashes01() const { return PlayerOptions::FlashScale(); }

	float WorldFx::PixelSize(Vector3 p, float px) const
	{
		return px * Mathf::Max(p.magnitude(), 1e-6f) / static_cast<float>(app->PixelsPerUnitAtUnitDistance());
	}

	DVec3 WorldFx::PosAt(DVec3 p0, Vector3 v0, float drag, float age)
	{
		// Slowing as it goes: v = v0 e^(-drag t), so it has gone v0 (1 - e^(-drag t)) / drag; exact backward as forward.
		float k = drag > 1e-4f ? (1 - Mathf::Exp(-drag * age)) / drag : age;
		return p0 + D(v0 * k);
	}

	void WorldFx::AddCloud(const Cloud& c)
	{
		if (static_cast<int>(clouds.size()) >= Tune::MaxClouds) clouds.erase(clouds.begin(), clouds.begin() + Tune::MaxClouds / 16);
		clouds.push_back(c);
	}

	void WorldFx::AddSpark(const Spark& s)
	{
		if (static_cast<int>(sparks.size()) >= Tune::MaxSparks) sparks.erase(sparks.begin(), sparks.begin() + Tune::MaxSparks / 16);
		sparks.push_back(s);
	}

	void WorldFx::AddFlash(const Flash& f)
	{
		if (static_cast<int>(flashes.size()) >= Tune::MaxFlashes) flashes.erase(flashes.begin());
		flashes.push_back(f);
	}

	void WorldFx::Begin(float frameDt)
	{
		// The hold rewind (US-T421): its clock runs the effects backward; when it ends the moments undone go, and the rest
		// carry on from the moment played on from without a jump (as BattleFx).
		float real = Time::unscaledTime;
		float resume = 0;
		if (app->HoldRewindFxEnded(fxEndsSeen, resume))
		{
			auto undone = [&](float born) { return born > resume + 1e-4f; };
			clouds.erase(std::remove_if(clouds.begin(), clouds.end(), [&](const Cloud& c) { return undone(c.Born); }), clouds.end());
			sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [&](const Spark& s) { return undone(s.Born); }), sparks.end());
			flashes.erase(std::remove_if(flashes.begin(), flashes.end(), [&](const Flash& f) { return undone(f.Born); }), flashes.end());
			float shift = real - resume;
			for (auto& c : clouds) c.Born += shift;
			for (auto& s : sparks) s.Born += shift;
			for (auto& f : flashes) f.Born += shift;
			for (auto& kv : emitters) kv.second.Has = false;
		}
		now = app->HoldRewindFxClock(real);
		rewinding = now != real;
		dt = rewinding ? 0.0f : frameDt;
		if (rewinding) return;
		clouds.erase(std::remove_if(clouds.begin(), clouds.end(), [&](const Cloud& c) { return now - c.Born > c.Life; }), clouds.end());
		sparks.erase(std::remove_if(sparks.begin(), sparks.end(), [&](const Spark& s) { return now - s.Born > s.Life; }), sparks.end());
		flashes.erase(std::remove_if(flashes.begin(), flashes.end(), [&](const Flash& f) { return now - f.Born > f.Life; }), flashes.end());
	}

	void WorldFx::Emit(const FxHull& h)
	{
		if (rewinding || h.Key == nullptr) return;
		auto& e = emitters[h.Key];
		e.Seen = now;
		float len = Mathf::Max(h.Length, 1e-9f);
		bool had = e.Has;
		if (had && dt > 0)
		{
			auto d = h.Scene - e.Last;
			if (d.Length() > Tune::JumpLengths * len) had = false;
			else
			{
				Vector3 v(static_cast<float>(d.X / dt), static_cast<float>(d.Y / dt), static_cast<float>(d.Z / dt));
				e.Vel = Vector3::Lerp(e.Vel, v, 1 - Mathf::Exp(-dt * 8.0f));
			}
		}
		if (!had)
		{
			e.Vel = Vector3::zero();
			e.Carry = e.RcsCarry = e.VentCarry = 0;
			e.LastBoost = -1;
			e.Boosting = false;
		}
		DVec3 last = e.Last;
		e.Last = h.Scene;
		e.Has = true;
		if (!had || !(dt > 0) || h.Px < Tune::EmitMinPx) return;
		if (e.Vel.magnitude() > Tune::MaxEmitLengthsPerS * len) return; // high warp: strung out, not a trail
		if (e.Live >= Tune::MaxCloudsPerEmitter) return;

		float near01 = Mathf::Clamp01((h.Px - Tune::EmitMinPx) / (Tune::EmitFullPx - Tune::EmitMinPx));
		float rateShare = Mathf::Lerp(Tune::FarRateShare, 1.0f, near01);
		float sizeBoost = Mathf::Lerp(Tune::FarSizeBoost, 1.0f, near01);
		float heft = Mathf::Clamp01(h.Heft);
		DVec3 moved = h.Scene - last;

		// ---- boost: read from the charge falling, held a moment (the charge moves only when the battle steps)
		bool boostNow = h.BoostCharge >= 0 && e.LastBoost >= 0 && (e.LastBoost - h.BoostCharge) / dt > Tune::BoostDrainPerS;
		e.LastBoost = h.BoostCharge;
		bool boosting;
		{
			bool before = e.Boosting;
			if (boostNow) e.BoostUntil = now + Tune::BoostHoldS;
			boosting = e.BoostUntil > now;
			e.Boosting = boosting;
			if (boosting && !before && !h.Wreck && h.Mains.Count() > 0)
			{
				// The kick as it opens: a ring of puffs round the nozzles and a flash on them.
				DVec3 mid;
				Vector3 aft = Vector3::zero();
				float width = 0;
				for (const auto& j : h.Mains) { mid = mid + j.At; aft += j.Exhaust; width = Mathf::Max(width, j.Width); }
				mid = mid / static_cast<double>(h.Mains.Count());
				aft = aft.sqrMagnitude() > 1e-8f ? aft.normalized() : Vector3::back();
				Vector3 a = Vector3::Cross(aft, Mathf::Abs(aft.y) < 0.9f ? Vector3::up() : Vector3::right()).normalized();
				Vector3 b = Vector3::Cross(aft, a);
				for (int k = 0; k < Tune::BoostRingPuffs; k++)
				{
					float ang = Mathf::PI * 2 * k / Tune::BoostRingPuffs;
					Cloud c;
					c.P0 = mid;
					c.V0 = e.Vel * Tune::ExhaustInherit + ((a * Mathf::Cos(ang) + b * Mathf::Sin(ang)) * 0.8f + aft * 0.6f) * (len * Mathf::Lerp(1.4f, 0.5f, heft));
					c.Drag = 3.0f;
					c.Born = now;
					c.Life = Mathf::Lerp(0.6f, 1.8f, heft);
					c.Size0 = width * 1.4f * sizeBoost;
					c.Size1 = c.Size0 * 4.0f;
					c.Tint = Tune::BoostColor;
					c.Opacity = Mathf::Lerp(0.25f, 0.45f, heft);
					c.Albedo = 0.85f;
					c.Heat = 2.2f;
					c.Cool = 4.0f;
					c.Seed = Rand();
					c.Key = h.Key;
					AddCloud(c);
				}
				Flash f;
				f.At = mid;
				f.Born = now;
				f.Life = Tune::BoostFlashS;
				f.R0 = len * Tune::BoostFlashLengths * 0.3f;
				f.R1 = len * Tune::BoostFlashLengths;
				f.MinPx = 10;
				f.Tint = Scaled(Tune::BoostColor, 1.0f * Flashes01());
				f.Shape = ShapeRadial;
				AddFlash(f);
			}
		}

		// ---- main engines: soft clouds out of each lit nozzle, left behind in world space (a turn curves the trail)
		if (!h.Wreck && h.Mains.Count() > 0)
		{
			float rate = Mathf::Lerp(Tune::ExhaustRateLight, Tune::ExhaustRateHeavy, heft) * rateShare * (boosting ? Tune::BoostRate : 1.0f);
			float life = Mathf::Lerp(Tune::ExhaustLifeLight, Tune::ExhaustLifeHeavy, heft);
			float speed = Mathf::Lerp(Tune::ExhaustSpeedLight, Tune::ExhaustSpeedHeavy, heft) * len * (boosting ? Tune::BoostSpeed : 1.0f);
			float grow = Mathf::Lerp(Tune::ExhaustGrowLight, Tune::ExhaustGrowHeavy, heft);
			float opacity = Mathf::Lerp(Tune::ExhaustOpacityLight, Tune::ExhaustOpacityHeavy, heft);
			for (const auto& j : h.Mains)
			{
				if (j.Level < 0.05f) continue;
				float size0 = j.Width * Tune::ExhaustStartWidths * sizeBoost * (boosting ? Tune::BoostSize : 1.0f);
				// Enough clouds for the rate, and enough to keep the trail whole however far the hull went this frame.
				float want = rate * j.Level * dt + static_cast<float>(moved.Length()) / Mathf::Max(size0 * 0.9f, 1e-9f) * 0.5f;
				e.Carry += want;
				int n = Mathf::Min(static_cast<int>(e.Carry), 24);
				e.Carry -= static_cast<float>(static_cast<int>(e.Carry));
				for (int k = 0; k < n; k++)
				{
					// Spread back over the frame: born where the nozzle was then, as long ago.
					float f = (k + Rand()) / n;
					Cloud c;
					c.P0 = j.At - moved * static_cast<double>(f);
					c.V0 = e.Vel * Tune::ExhaustInherit + Scatter(j.Exhaust, Tune::ExhaustScatter) * (speed * Rand(0.75f, 1.2f));
					c.Drag = Tune::ExhaustDrag;
					c.Born = now - f * dt;
					c.Life = life * Rand(0.8f, 1.2f);
					c.Size0 = size0 * Rand(0.85f, 1.15f);
					c.Size1 = c.Size0 * grow * Rand(0.8f, 1.2f);
					c.Tint = boosting ? Tune::BoostColor : Tune::DriveColor;
					c.Opacity = opacity * Mathf::Lerp(0.55f, 1.0f, j.Level);
					c.Albedo = Tune::ExhaustAlbedo;
					c.Heat = Tune::ExhaustHeat * j.Level * (boosting ? Tune::BoostHeat : 1.0f);
					c.Cool = Tune::ExhaustCoolPerS;
					c.Seed = Rand();
					c.Key = h.Key;
					AddCloud(c);
				}
			}
		}

		// ---- RCS: a small cold puff of vapour from each jet as it fires
		if (!h.Wreck)
		{
			for (const auto& j : h.Rcs)
			{
				if (j.Level < Tune::RcsLitLevel) continue;
				e.RcsCarry += Tune::RcsRate * rateShare * j.Level * dt;
				int n = Mathf::Min(static_cast<int>(e.RcsCarry), 6);
				e.RcsCarry -= static_cast<float>(static_cast<int>(e.RcsCarry));
				for (int k = 0; k < n; k++)
				{
					float f = Rand();
					Cloud c;
					c.P0 = j.At - moved * static_cast<double>(f);
					c.V0 = e.Vel * 0.6f + Scatter(j.Exhaust, 0.3f) * (Tune::RcsSpeed * len * Rand(0.6f, 1.2f));
					c.Drag = 3.0f;
					c.Born = now - f * dt;
					c.Life = Tune::RcsLife * Rand(0.7f, 1.2f);
					c.Size0 = j.Width * Tune::RcsStartWidths * sizeBoost;
					c.Size1 = c.Size0 * Tune::RcsGrow;
					c.Tint = Tune::VapourColor;
					c.Opacity = Tune::RcsOpacity * Mathf::Clamp01(j.Level * 1.5f);
					c.Albedo = Tune::RcsAlbedo;
					c.Heat = 0;
					c.Seed = Rand();
					c.Key = h.Key;
					AddCloud(c);
				}
			}
		}

		// ---- breaches venting: gas streaming out, a little fire in it now and then; a wreck smokes darker and longer
		for (int v = 0; v < h.Vents.Count(); v++)
		{
			e.VentCarry += Tune::VentRate * rateShare * dt;
			int n = Mathf::Min(static_cast<int>(e.VentCarry), 4);
			e.VentCarry -= static_cast<float>(static_cast<int>(e.VentCarry));
			Vector3 out = v < h.VentOut.Count() ? h.VentOut[v] : Vector3::up();
			for (int k = 0; k < n; k++)
			{
				bool fire = Rand() < 0.28f;
				Cloud c;
				c.P0 = h.Vents[v];
				c.V0 = e.Vel * 0.7f + Scatter(out, 0.35f) * (Tune::VentSpeed * len * Rand(0.6f, 1.3f));
				c.Drag = 1.2f;
				c.Born = now - Rand() * dt;
				c.Life = (h.Wreck ? Tune::WreckSmokeLife : Tune::VentLife) * Rand(0.7f, 1.2f);
				c.Size0 = Tune::VentStartLengths * len * sizeBoost * Rand(0.7f, 1.3f);
				c.Size1 = c.Size0 * Tune::VentGrow;
				c.Tint = fire ? Tune::FireColor : Tune::VentColor;
				c.Opacity = Tune::VentOpacity;
				c.Albedo = h.Wreck ? Tune::WreckSmokeAlbedo : Tune::VentAlbedo;
				c.Heat = fire ? Tune::VentFireHeat : 0.0f;
				c.Cool = 2.0f;
				c.Seed = Rand();
				c.Key = h.Key;
				AddCloud(c);
			}
		}
	}

	// ---------------------------------------------------------------- one-shot effects

	void WorldFx::Impact(DVec3 at, Vector3 out, float hullLength, float weight)
	{
		if (rewinding) return;
		float len = Mathf::Max(hullLength, 1e-9f);
		weight = Mathf::Clamp01(weight);
		int n = Mathf::RoundToInt(Mathf::Lerp(static_cast<float>(Tune::ImpactSparksMin), static_cast<float>(Tune::ImpactSparksMax), weight) * (Calm() ? Tune::CalmSparkShare : 1.0f));
		for (int k = 0; k < n; k++)
		{
			Spark s;
			s.P0 = at;
			s.V0 = Scatter(out, 0.85f) * (Tune::ImpactSparkSpeed * len * Rand(0.35f, 1.2f));
			s.Drag = Tune::ImpactSparkDrag;
			s.Born = now;
			s.Life = Tune::ImpactSparkLife * Rand(0.5f, 1.3f);
			s.Tint = Scaled(Tune::SparkColor, Rand(1.3f, 2.2f));
			s.Streak = true;
			s.Px = Rand(1.0f, 1.8f);
			AddSpark(s);
		}
		for (int k = 0; k < Tune::ImpactDebris; k++)
		{
			Spark s;
			s.P0 = at;
			s.V0 = Scatter(out, 1.0f) * (0.3f * len * Rand(0.3f, 1.0f));
			s.Drag = 0.6f;
			s.Born = now;
			s.Life = Tune::ImpactDebrisLife * Rand(0.6f, 1.2f);
			s.Tint = Color(0.75f, 0.55f, 0.35f);
			s.Streak = false;
			s.Size = len * 0.006f;
			s.Px = 1.6f;
			AddSpark(s);
		}
		Cloud c;
		c.P0 = at;
		c.V0 = out * (0.08f * len);
		c.Drag = 1.0f;
		c.Born = now;
		c.Life = Tune::ImpactSmokeLife;
		c.Size0 = Tune::ImpactSmokeLengths * len * (0.6f + weight);
		c.Size1 = c.Size0 * 3.2f;
		c.Tint = Tune::BlastColor;
		c.Opacity = 0.42f;
		c.Albedo = 0.35f;
		c.Heat = 1.2f;
		c.Cool = 3.0f;
		c.Seed = Rand();
		AddCloud(c);
	}

	void WorldFx::Blast(DVec3 at, float hullLength, float share)
	{
		if (rewinding) return;
		float len = Mathf::Max(hullLength * share, 1e-9f);
		float sparkShare = (Calm() ? Tune::CalmSparkShare : 1.0f) * Mathf::Lerp(0.4f, 1.0f, share);
		if (!Calm())
		{
			// The shockwave: a thin ring racing out, and a slower, fainter one behind it.
			Flash f;
			f.At = at;
			f.Born = now;
			f.Life = Tune::ShockS;
			f.R0 = len * 0.25f;
			f.R1 = len * Tune::ShockLengths;
			f.MinPx = 8;
			f.Tint = Scaled(Tune::ShockColor, 0.9f * Mathf::Max(Flashes01(), 0.4f));
			f.Shape = ShapeRing;
			AddFlash(f);
			f.Life = Tune::ShockSecondS;
			f.R1 = len * Tune::ShockLengths * 0.6f;
			f.Tint = Scaled(Tune::BlastColor, 0.45f * Mathf::Max(Flashes01(), 0.4f));
			AddFlash(f);
		}
		int sparksN = Mathf::RoundToInt(Tune::BlastSparks * sparkShare);
		for (int k = 0; k < sparksN; k++)
		{
			Spark s;
			s.P0 = at;
			s.V0 = RandDir() * (Tune::BlastSparkSpeed * len * Rand(0.3f, 1.2f));
			s.Drag = 1.2f;
			s.Born = now;
			s.Life = Tune::BlastSparkLife * Rand(0.5f, 1.2f);
			s.Tint = Scaled(Tune::SparkColor, Rand(1.4f, 2.4f));
			s.Streak = true;
			s.Px = Rand(1.2f, 2.2f);
			AddSpark(s);
		}
		int embers = Mathf::RoundToInt(Tune::BlastEmbers * sparkShare);
		for (int k = 0; k < embers; k++)
		{
			Spark s;
			s.P0 = at + D(RandDir() * (len * 0.1f));
			s.V0 = RandDir() * (Tune::EmberSpeed * len * Rand(0.2f, 1.0f));
			s.Drag = Tune::EmberDrag;
			s.Born = now;
			s.Life = Tune::EmberLife * Rand(0.5f, 1.2f);
			s.Tint = Scaled(Tune::BlastColor, Rand(1.4f, 2.2f));
			s.Streak = false;
			s.Size = len * Rand(0.008f, 0.02f);
			s.Px = Rand(1.4f, 2.6f);
			AddSpark(s);
		}
		int cloudsN = Mathf::Max(3, Mathf::RoundToInt(Tune::BlastClouds * Mathf::Lerp(0.4f, 1.0f, share)));
		for (int k = 0; k < cloudsN; k++)
		{
			Cloud c;
			c.P0 = at + D(RandDir() * (len * 0.15f));
			c.V0 = RandDir() * (Tune::BlastCloudSpeed * len * Rand(0.3f, 1.0f));
			c.Drag = 0.5f;
			c.Born = now - Rand() * 0.05f;
			c.Life = Tune::BlastCloudLife * Rand(0.7f, 1.1f) * Mathf::Lerp(0.5f, 1.0f, share);
			c.Size0 = Tune::BlastCloudLengths * len * Rand(0.6f, 1.2f);
			c.Size1 = c.Size0 * Tune::BlastCloudGrow * Rand(0.8f, 1.2f);
			c.Tint = Tune::BlastColor;
			c.Opacity = Tune::BlastCloudOpacity;
			c.Albedo = Tune::BlastCloudAlbedo;
			c.Heat = Tune::BlastCloudHeat * Rand(0.7f, 1.2f);
			c.Cool = Tune::BlastCloudCoolPerS;
			c.Seed = Rand();
			AddCloud(c);
		}
	}

	void WorldFx::BreachJet(DVec3 at, Vector3 out, float hullLength)
	{
		if (rewinding) return;
		float len = Mathf::Max(hullLength, 1e-9f);
		for (int k = 0; k < Tune::BreachJetPuffs; k++)
		{
			// Staggered over the jet's span (born ahead of now: drawn as their time comes).
			float t = static_cast<float>(k) / Tune::BreachJetPuffs;
			Cloud c;
			c.P0 = at;
			c.V0 = Scatter(out, 0.18f) * (0.6f * len * Rand(0.7f, 1.2f) * (1 - 0.5f * t));
			c.Drag = 1.4f;
			c.Born = now + t * Tune::BreachJetS;
			c.Life = 1.2f * Rand(0.8f, 1.2f);
			c.Size0 = 0.025f * len;
			c.Size1 = c.Size0 * 4.5f;
			c.Tint = Tune::VentColor;
			c.Opacity = 0.5f * (1 - 0.6f * t);
			c.Albedo = 0.9f;
			c.Heat = 0;
			c.Seed = Rand();
			AddCloud(c);
		}
	}

	void WorldFx::GateTransit(DVec3 at, Vector3 forward, float hullLength, bool arriving)
	{
		if (rewinding) return;
		float len = Mathf::Max(hullLength, 1e-9f);
		forward = forward.sqrMagnitude() > 1e-8f ? forward.normalized() : Vector3::forward();
		float fl = Flashes01();
		Flash ring;
		ring.At = at;
		ring.Shape = ShapeRing;
		ring.MinPx = 10;
		ring.Tint = Scaled(Tune::GateColor, 1.2f);
		Flash core;
		core.At = at;
		core.Shape = ShapeRadial;
		core.MinPx = 16;
		core.Life = Tune::GateFlashS;
		core.R0 = len * 0.4f;
		core.R1 = len * 1.8f;
		core.Tint = Scaled(Tune::GateCore, 1.5f * fl);
		Flash streak;
		streak.Shape = ShapeStreak;
		streak.MinPx = 3.0f;
		streak.Life = Tune::GateFlashS * 1.3f;
		streak.R1 = len * 9.0f;
		streak.Tint = Scaled(Tune::GateColor, 1.6f);
		if (!arriving)
		{
			// Leaving: space folds shut on it (the ring closing), then the flash and the streak on through.
			ring.Born = now;
			ring.Life = Tune::GateFoldS;
			ring.R0 = len * Tune::GateRingLengths;
			ring.R1 = len * 0.15f;
			core.Born = now + Tune::GateFoldS * 0.8f;
			streak.Born = core.Born;
			streak.At = at;
			streak.Dir = forward;
		}
		else
		{
			// Arriving: the flash first, then the fold opening out round it and the streak it came in on.
			core.Born = now;
			ring.Born = now + 0.08f;
			ring.Life = Tune::GateFoldS * 1.3f;
			ring.R0 = len * 0.15f;
			ring.R1 = len * Tune::GateRingLengths;
			streak.Born = now;
			streak.At = at - D(forward * (len * 9.0f));
			streak.Dir = forward;
		}
		AddFlash(ring);
		AddFlash(core);
		AddFlash(streak);
		// Ionised wisps left hanging in the gate's light.
		for (int k = 0; k < 10; k++)
		{
			Cloud c;
			c.P0 = at + D(RandDir() * (len * 0.3f));
			c.V0 = (Scatter(arriving ? forward : -forward, 0.7f)) * (len * Rand(0.3f, 0.9f));
			c.Drag = 1.2f;
			c.Born = arriving ? now : core.Born;
			c.Life = Rand(1.2f, 2.2f);
			c.Size0 = len * 0.25f;
			c.Size1 = len * 1.3f;
			c.Tint = Tune::GateColor;
			c.Opacity = 0.3f;
			c.Albedo = 0.6f;
			c.Heat = 1.8f;
			c.Cool = 1.4f;
			c.Seed = Rand();
			AddCloud(c);
		}
	}

	void WorldFx::CruiseEngage(DVec3 at, Vector3 forward, float hullLength)
	{
		if (rewinding) return;
		float len = Mathf::Max(hullLength, 1e-9f);
		forward = forward.sqrMagnitude() > 1e-8f ? forward.normalized() : Vector3::forward();
		DVec3 stern = at - D(forward * (len * 0.5f));
		if (!Calm())
		{
			Flash ring;
			ring.At = stern;
			ring.Born = now;
			ring.Life = Tune::CruiseRingS;
			ring.R0 = len * 0.3f;
			ring.R1 = len * Tune::CruiseRingLengths;
			ring.MinPx = 10;
			ring.Tint = Scaled(Tune::CruiseColor, 1.0f);
			ring.Shape = ShapeRing;
			AddFlash(ring);
		}
		Flash f;
		f.At = stern;
		f.Born = now;
		f.Life = 0.4f;
		f.R0 = len * 0.2f;
		f.R1 = len * 0.9f;
		f.MinPx = 12;
		f.Tint = Scaled(Tune::CruiseColor, 1.8f * Flashes01());
		f.Shape = ShapeRadial;
		AddFlash(f);
		for (int k = 0; k < 16; k++)
		{
			Cloud c;
			c.P0 = stern;
			c.V0 = Scatter(-forward, 0.5f) * (len * Rand(0.5f, 1.4f));
			c.Drag = 1.6f;
			c.Born = now;
			c.Life = Rand(1.2f, 2.4f);
			c.Size0 = len * 0.12f;
			c.Size1 = len * 0.8f;
			c.Tint = Tune::CruiseColor;
			c.Opacity = 0.35f;
			c.Albedo = 0.8f;
			c.Heat = 2.0f;
			c.Cool = 2.5f;
			c.Seed = Rand();
			AddCloud(c);
		}
	}

	void WorldFx::FtlJump(DVec3 at, Vector3 forward, float hullLength, bool arriving)
	{
		if (rewinding) return;
		float len = Mathf::Max(hullLength, 1e-9f);
		forward = forward.sqrMagnitude() > 1e-8f ? forward.normalized() : Vector3::forward();
		Flash f;
		f.At = at;
		f.Born = now;
		f.Life = Tune::FtlFlashS;
		f.R0 = len * 0.5f;
		f.R1 = len * Tune::FtlFlashLengths;
		f.MinPx = 20;
		f.Tint = Scaled(Tune::FtlColor, 1.4f * Flashes01());
		f.Shape = ShapeRadial;
		AddFlash(f);
		if (!Calm())
		{
			Flash ring = f;
			ring.Shape = ShapeRing;
			ring.Life = Tune::FtlFlashS * 1.6f;
			ring.R0 = len * 0.3f;
			ring.R1 = len * Tune::FtlFlashLengths * 1.6f;
			ring.MinPx = 12;
			ring.Tint = Scaled(Tune::FtlColor, 0.9f);
			AddFlash(ring);
		}
		Flash streak;
		streak.Shape = ShapeStreak;
		streak.Born = now;
		streak.Life = Tune::FtlFlashS * 1.4f;
		streak.R1 = len * Tune::FtlStreakLengths;
		streak.MinPx = 4.0f;
		streak.Tint = Scaled(Tune::FtlColor, 1.8f);
		streak.Dir = forward;
		streak.At = arriving ? at - D(forward * streak.R1) : at;
		AddFlash(streak);
	}

	// ---------------------------------------------------------------- drawing

	void WorldFx::Draw()
	{
		for (auto& kv : emitters) kv.second.Live = 0;
		for (const auto& c : clouds)
			if (c.Key != nullptr)
			{
				auto it = emitters.find(c.Key);
				if (it != emitters.end()) it->second.Live++;
			}
		if (!rewinding)
			for (auto it = emitters.begin(); it != emitters.end();)
			{
				if (now - it->second.Seen > 2.0f) it = emitters.erase(it);
				else ++it;
			}

		auto cam = app->UnityCamera->transform;
		Vector3 right = cam->right(), up = cam->up();
		DrawClouds(right, up);
		DrawGlows(right, up);
	}

	void WorldFx::DrawClouds(Vector3 right, Vector3 up)
	{
		sv.Clear();
		sc.Clear();
		su.Clear();
		si.Clear();
		order.clear();
		auto fwd = app->UnityCamera->transform->forward();
		struct Live { Vector3 P; float S; Color C; float Packed, Heat; };
		static thread_local std::vector<Live> live;
		live.clear();
		for (const auto& c : clouds)
		{
			float age = now - c.Born;
			if (age < 0 || age > c.Life) continue;
			float t = age / c.Life;
			auto p = app->Relative(PosAt(c.P0, c.V0, c.Drag, age));
			float u = 1 - t;
			float size = c.Size0 + (c.Size1 - c.Size0) * (1 - u * u);
			float depth = Vector3::Dot(p, fwd);
			if (depth < -size) continue;
			if (size < PixelSize(p, 0.6f)) continue;
			// Thinning as it spreads and ages: in fast, out slowly.
			float alpha = c.Opacity * Mathf::Clamp01(t / 0.06f) * Mathf::Pow(u, 1.8f);
			if (alpha < 0.004f) continue;
			float heat = c.Heat * Mathf::Exp(-c.Cool * age);
			float packed = Mathf::Floor(Mathf::Clamp01(c.Albedo) * 15.0f) + Mathf::Clamp(c.Seed, 0.0f, 0.99f);
			order.push_back({ p.sqrMagnitude(), static_cast<int>(live.size()) });
			live.push_back({ p, size, Color(c.Tint.r, c.Tint.g, c.Tint.b, Mathf::Clamp01(alpha)), packed, heat });
		}
		// Back to front: a translucent cloud over the ones behind it.
		std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
		for (const auto& [d, idx] : order)
		{
			const auto& l = live[idx];
			int i = sv.Count();
			auto r = right * l.S, up2 = up * l.S;
			sv.Add(l.P - r - up2);
			sv.Add(l.P + r - up2);
			sv.Add(l.P + r + up2);
			sv.Add(l.P - r + up2);
			for (int k = 0; k < 4; k++) sc.Add(l.C);
			su.Add(Vector4(0, 0, l.Packed, l.Heat));
			su.Add(Vector4(1, 0, l.Packed, l.Heat));
			su.Add(Vector4(1, 1, l.Packed, l.Heat));
			su.Add(Vector4(0, 1, l.Packed, l.Heat));
			si.Add(i); si.Add(i + 1); si.Add(i + 2);
			si.Add(i); si.Add(i + 2); si.Add(i + 3);
		}
		CloudsDrawn = static_cast<int>(order.size());
		smokeMesh->Clear();
		smokeMesh->SetVertices(sv);
		smokeMesh->SetColors(sc);
		smokeMesh->SetUVs(0, su);
		smokeMesh->SetIndices(si, MeshTopology::Triangles, 0, false);
		smokeMesh->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 1e9f));
	}

	void WorldFx::DrawGlows(Vector3 right, Vector3 up)
	{
		gv.Clear();
		gc.Clear();
		gu.Clear();
		gi.Clear();
		for (const auto& s : sparks)
		{
			float age = now - s.Born;
			if (age < 0 || age > s.Life) continue;
			float t = age / s.Life;
			float fade = 1 - t;
			auto head = app->Relative(PosAt(s.P0, s.V0, s.Drag, age));
			if (s.Streak)
			{
				auto tail = app->Relative(PosAt(s.P0, s.V0, s.Drag, Mathf::Max(0.0f, age - 0.07f)));
				GlowRibbon(tail, head, s.Px, Scaled(s.Tint, fade * fade));
			}
			else
			{
				// An ember: cooling from its colour to a dull red as it drifts, flickering.
				float flick = 0.75f + 0.25f * Mathf::Sin(age * 17.0f + s.Px * 7.3f);
				auto c = Color::Lerp(s.Tint, Color(0.55f, 0.1f, 0.03f), Mathf::Sqrt(t));
				GlowQuad(head, right, up, Mathf::Max(s.Size, PixelSize(head, s.Px)), Scaled(c, Mathf::Pow(fade, 1.2f) * flick), ShapeRadial);
			}
		}
		for (const auto& f : flashes)
		{
			float age = now - f.Born;
			if (age < 0 || age > f.Life) continue;
			float t = age / f.Life;
			if (f.Shape == ShapeStreak)
			{
				auto a = app->Relative(f.At);
				auto b = app->Relative(f.At + D(f.Dir * (f.R1 * EaseOut(t * 2.0f))));
				GlowRibbon(a, b, f.MinPx * (1 - 0.5f * t), Scaled(f.Tint, 1 - t));
				continue;
			}
			auto c = app->Relative(f.At);
			float r = Mathf::Lerp(f.R0, f.R1, EaseOut(t));
			float fade = f.Shape == ShapeRing ? (1 - t) : (1 - t) * (1 - t);
			GlowQuad(c, right, up, Mathf::Max(r, PixelSize(c, f.MinPx * (f.Shape == ShapeRing ? (0.4f + t) : 1.0f))), Scaled(f.Tint, fade), f.Shape);
		}
		glowMesh->Clear();
		glowMesh->SetVertices(gv);
		glowMesh->SetColors(gc);
		glowMesh->SetUVs(0, gu);
		glowMesh->SetIndices(gi, MeshTopology::Triangles, 0, false);
		glowMesh->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 1e9f));
	}

	void WorldFx::GlowQuad(Vector3 c, Vector3 right, Vector3 up, float r, Color color, int shape)
	{
		int i = gv.Count();
		auto rr = right * r, uu = up * r;
		gv.Add(c - rr - uu);
		gv.Add(c + rr - uu);
		gv.Add(c + rr + uu);
		gv.Add(c - rr + uu);
		float z = static_cast<float>(shape);
		for (int k = 0; k < 4; k++) gc.Add(color);
		gu.Add(Vector3(0, 0, z));
		gu.Add(Vector3(1, 0, z));
		gu.Add(Vector3(1, 1, z));
		gu.Add(Vector3(0, 1, z));
		gi.Add(i); gi.Add(i + 1); gi.Add(i + 2);
		gi.Add(i); gi.Add(i + 2); gi.Add(i + 3);
	}

	void WorldFx::GlowRibbon(Vector3 a, Vector3 b, float px, Color color)
	{
		auto along = b - a;
		if (along.sqrMagnitude() < 1e-14f) return;
		auto mid = (a + b) * 0.5f;
		auto side = Vector3::Cross(along, mid).normalized();
		float wa = PixelSize(a, px) * 0.5f, wb = PixelSize(b, px) * 0.5f;
		int i = gv.Count();
		gv.Add(a - side * wa);
		gv.Add(b - side * wb);
		gv.Add(b + side * wb);
		gv.Add(a + side * wa);
		for (int k = 0; k < 4; k++) gc.Add(color);
		float z = static_cast<float>(ShapeBeam);
		gu.Add(Vector3(0, 0, z));
		gu.Add(Vector3(1, 0, z));
		gu.Add(Vector3(1, 1, z));
		gu.Add(Vector3(0, 1, z));
		gi.Add(i); gi.Add(i + 1); gi.Add(i + 2);
		gi.Add(i); gi.Add(i + 2); gi.Add(i + 3);
	}
}
