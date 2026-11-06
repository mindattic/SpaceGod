// US-T195: the hyperlanes drawn as highway tunnels (see LaneTunnel.h).
#include "Views/LaneTunnel.h"

#include "UnitySpaceApp.h"
#include "Rendering/Materials.h"

#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "ScaleModel.h"
#include "Ships.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;

	namespace
	{
		// The warp-field palette: a deep blue wall shading to violet, blue-white rings and streaks.
		constexpr Color WallBlue = Color(0.22f, 0.45f, 1.0f);
		constexpr Color WallViolet = Color(0.55f, 0.3f, 1.0f);
		constexpr Color RippleWhite = Color(0.75f, 0.88f, 1.0f);
		constexpr Color StreakWhite = Color(0.82f, 0.9f, 1.0f);
		constexpr Color StreakViolet = Color(0.7f, 0.52f, 1.0f);
		// The gate spurs keep LaneView's amber.
		constexpr Color SpurWall = Color(1.0f, 0.62f, 0.25f);
		constexpr Color SpurWallDeep = Color(0.95f, 0.4f, 0.2f);
		constexpr Color SpurRipple = Color(1.0f, 0.88f, 0.65f);
	}

	float LaneTunnel::DrawnRadius(float trueRadius, double blend)
	{
		double t = cs::Math::Max(static_cast<double>(trueRadius), 1e-9);
		double game = cs::Math::Max(t, static_cast<double>(GameTubeRadius));
		return static_cast<float>(cs::Math::Max(t, ScaleModel::LogLerp(t, game, blend)));
	}

	LaneTunnel::LaneTunnel(UnitySpaceApp* app_, const cs::Ref<Transform>& parent)
		: app(app_)
	{
		auto go = NewGameObject("LaneTunnels");
		go->transform->SetParent(parent, false);
		auto m = cs::New<Mesh>("LaneTunnels");
		m->indexFormat = IndexFormat::UInt32;
		mesh = Materials::Own(m);
		mesh->MarkDynamic();
		AddComponent<MeshFilter>(go)->sharedMesh = mesh;
		renderer = AddComponent<MeshRenderer>(go);
		renderer->SetSharedMaterial(Materials::Create(Materials::Fx(), "LaneTunnels"));
		renderer->shadowCastingMode = ShadowCastingMode::Off;
	}

	void LaneTunnel::Begin(bool visible)
	{
		shown = visible;
		renderer->enabled = visible;
		vertices.Clear();
		colors.Clear();
		uvs.Clear();
		indices.Clear();
	}

	void LaneTunnel::End()
	{
		if (!shown) return;
		mesh->Clear();
		mesh->SetVertices(vertices);
		mesh->SetColors(colors);
		mesh->SetUVs(0, uvs);
		mesh->SetIndices(indices, MeshTopology::Triangles, 0, false);
		mesh->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 1e9f));
	}

	void LaneTunnel::Draw(const cs::Ref<Lane>& lane, const cs::List<Vector3>& curve, int samples, float radius, float gateA, float gateB,
		const cs::Ref<Ship>& rider, float now)
	{
		if (!shown || radius <= 0 || samples < 2) return;
		fr.Clear();
		pts.Clear();
		float riderF = 0;
		if (rider != nullptr)
		{
			// The ride: along the rider's own curve (the one the hull is placed on, from the ramp it set out from), coarse
			// over the lane and dense through a window round the hull, so the camera rides inside a smooth tube.
			auto eph = app->Ephemeris();
			auto lanes = app->Fleet()->Lanes;
			auto scale = app->Scale();
			const auto& scene = app->BodyScenes();
			riderF = static_cast<float>(cs::Math::Max(0.0, cs::Math::Min(1.0, Core::Fleet::LaneProgress(rider, eph->Jd))));
			if (rider->LaneLeg.From != lane->A)
			{
				float g = gateA;
				gateA = gateB;
				gateB = g;
			}
			auto sceneAt = [&](float f)
			{
				DVec3 dir;
				return app->Relative(scale->LaneScene(eph, scene, lanes, rider, f, dir));
			};
			coarseFr.Clear();
			coarsePts.Clear();
			float total = 0;
			for (int i = 0; i <= samples; i++)
			{
				float f = static_cast<float>(i) / samples;
				coarseFr.Add(f);
				coarsePts.Add(sceneAt(f));
				if (i > 0) total += (coarsePts[i] - coarsePts[i - 1]).magnitude();
			}
			float wf = total > 0 ? RideWindowRadii * radius / total : 1.0f;
			float lo = Mathf::Max(0.0f, riderF - wf), hi = Mathf::Min(1.0f, riderF + wf);
			for (int i = 0; i <= samples && coarseFr[i] < lo; i++)
			{
				fr.Add(coarseFr[i]);
				pts.Add(coarsePts[i]);
			}
			for (int j = 0; j <= RideDense; j++)
			{
				float f = lo + (hi - lo) * j / RideDense;
				fr.Add(f);
				pts.Add(sceneAt(f));
			}
			for (int i = 0; i <= samples; i++)
			{
				if (coarseFr[i] <= hi) continue;
				fr.Add(coarseFr[i]);
				pts.Add(coarsePts[i]);
			}
		}
		else
		{
			for (int i = 0; i <= samples; i++)
			{
				fr.Add(static_cast<float>(i) / samples);
				pts.Add(curve[i]);
			}
		}
		Frame(radius, gateA, gateB);
		float rideSpeed = rider != nullptr ? app->LaneRideSpeed : 0.0f;
		float pixels = Wall(lane->IsGateSpur(), now, rideSpeed, rider != nullptr);
		if (pixels < 3.0f) return;

		float total = ds[ds.Count() - 1];
		if (total <= 0) return;
		bool spur = lane->IsGateSpur();
		// Ambient traffic of light: streaks flowing each way along the walls, to read as a live highway from outside.
		float ambient = Mathf::Clamp01((pixels - 3.0f) / 12.0f);
		for (int j = 0; j < AmbientStreaks; j++)
		{
			float seed = lane->Id * 31.7f + j * 1.31f;
			float h1 = Hash(seed), h2 = Hash(seed + 7.1f), h3 = Hash(seed + 3.3f), h4 = Hash(seed + 11.9f);
			float dir = (j & 1) != 0 ? 1.0f : -1.0f;
			float head = cs::Mod(h3 + dir * now * 0.02f * (0.6f + 0.8f * h2) + 2.0f, 1.0f);
			float len = radius * (3.0f + 6.0f * h1) / total;
			float tail = head - dir * len;
			if (tail < 0 || tail > 1) continue;
			auto c = Color::Lerp(StreakWhite, spur ? SpurRipple : StreakViolet, h2) * (0.55f * ambient * (0.4f + 0.6f * h4));
			Streak(tail, head, h1 * 2.0f * Mathf::PI + j, 0.72f + 0.24f * h4, c);
		}
		if (rider == nullptr) return;

		// Inside the ridden tunnel: streaks rushing back past the hull, longer and faster with the ride's speed.
		float strength = app->LaneStreaks();
		float window = 0.6f * RideWindowRadii * radius;
		for (int j = 0; j < RideStreaks; j++)
		{
			float seed = 517.0f + lane->Id * 13.3f + j * 2.17f;
			float h1 = Hash(seed), h2 = Hash(seed + 5.7f), h3 = Hash(seed + 9.1f), h4 = Hash(seed + 1.9f), h5 = Hash(seed + 15.3f);
			float rate = radius * (1.5f + 30.0f * rideSpeed) * (0.6f + 0.8f * h2) / (2.0f * window);
			float o = window * (1.0f - 2.0f * cs::Mod(h3 + now * rate, 1.0f));
			float head = riderF + o / total;
			float len = radius * (0.6f + 8.0f * strength * rideSpeed) * (0.5f + h1) / total;
			float a = Mathf::Clamp01(head), b = Mathf::Clamp01(head + len);
			if (b - a <= 0) continue;
			float q = o / window;
			float fade = 1.0f - q * q;
			auto c = Color::Lerp(StreakWhite, spur ? SpurRipple : StreakViolet, h2) * (1.3f * strength * fade * (0.3f + 0.7f * h5 * h5));
			Streak(a, b, h4 * 2.0f * Mathf::PI, 0.55f + 0.42f * h1, c);
		}
	}

	LaneTunnel::Point LaneTunnel::At(float f) const
	{
		int n = fr.Count();
		if (f <= fr[0]) return { pts[0], us[0], vs[0], rs[0] };
		if (f >= fr[n - 1]) return { pts[n - 1], us[n - 1], vs[n - 1], rs[n - 1] };
		int lo = 0, hi = n - 1;
		while (hi - lo > 1)
		{
			int mid = (lo + hi) / 2;
			if (fr[mid] <= f) lo = mid;
			else hi = mid;
		}
		float span = fr[hi] - fr[lo];
		float t = span > 0 ? (f - fr[lo]) / span : 0.0f;
		return { Vector3::Lerp(pts[lo], pts[hi], t), Vector3::Lerp(us[lo], us[hi], t), Vector3::Lerp(vs[lo], vs[hi], t), Mathf::Lerp(rs[lo], rs[hi], t) };
	}

	void LaneTunnel::Frame(float radius, float gateA, float gateB)
	{
		int n = pts.Count();
		us.Clear();
		vs.Clear();
		rs.Clear();
		ds.Clear();
		float d = 0;
		for (int i = 0; i < n; i++)
		{
			if (i > 0) d += (pts[i] - pts[i - 1]).magnitude();
			ds.Add(d);
			auto t = pts[Mathf::Min(i + 1, n - 1)] - pts[Mathf::Max(i - 1, 0)];
			t = t.sqrMagnitude() > 1e-20f ? t.normalized() : Vector3::forward();
			// Parallel transport: each frame the last one turned onto this tangent, so the seams and streaks never twist.
			Vector3 u;
			if (i == 0) u = Vector3::Cross(t, Mathf::Abs(Vector3::Dot(t, Vector3::up())) > 0.98f ? Vector3::right() : Vector3::up());
			else u = us[i - 1] - t * Vector3::Dot(us[i - 1], t);
			if (u.sqrMagnitude() < 1e-12f) u = Vector3::Cross(t, Mathf::Abs(Vector3::Dot(t, Vector3::up())) > 0.98f ? Vector3::right() : Vector3::up());
			u.Normalize();
			us.Add(u);
			vs.Add(Vector3::Cross(t, u));
		}
		// Each end funnels into its ring gate over a few gate widths (never more than a third of the lane).
		float total = d;
		for (int i = 0; i < n; i++)
		{
			float r = radius;
			if (gateA > 0)
			{
				float span = Mathf::Min(8.0f * Mathf::Max(radius, gateA), total * 0.3f);
				float s = span > 0 ? Mathf::SmoothStep(0.0f, 1.0f, ds[i] / span) : 1.0f;
				r += (gateA - radius) * (1.0f - s);
			}
			if (gateB > 0)
			{
				float span = Mathf::Min(8.0f * Mathf::Max(radius, gateB), total * 0.3f);
				float s = span > 0 ? Mathf::SmoothStep(0.0f, 1.0f, (total - ds[i]) / span) : 1.0f;
				r += (gateB - radius) * (1.0f - s);
			}
			rs.Add(Mathf::Max(r, radius * 0.05f));
		}
	}

	float LaneTunnel::Wall(bool spur, float now, float rideSpeed, bool inside)
	{
		int n = pts.Count();
		if (n < 2) return 0;
		float maxPixels = 0;
		for (int i = 0; i < n; i++) maxPixels = Mathf::Max(maxPixels, rs[i] / PixelSize(pts[i], 1.0f));
		// Under a pixel or so across the tube is the ribbon's job.
		if (maxPixels < 1.0f) return 0;
		int sides = maxPixels > 80 ? 28 : maxPixels > 20 ? 18 : 10;

		float total = ds[n - 1];
		float radius = rs[n / 2];
		// The rippling rings: a wavelength of several tube widths, and never shorter than the samples can carry
		// (where they are coarse the rings fade out rather than alias).
		float wavelength = Mathf::Max(9.0f * radius, inside ? 0.0f : 3.0f * total / (n - 1));
		float rate = inside ? 0.4f + 2.6f * rideSpeed : 0.25f;
		Color blue = spur ? SpurWall : WallBlue, violet = spur ? SpurWallDeep : WallViolet, ripCol = spur ? SpurRipple : RippleWhite;

		int base = vertices.Count();
		for (int i = 0; i < n; i++)
		{
			float seg = (ds[Mathf::Min(i + 1, n - 1)] - ds[Mathf::Max(i - 1, 0)]) * 0.5f;
			float carried = seg > 0 ? Mathf::Clamp01((wavelength / seg - 2.5f) * 0.5f) : 1.0f;
			float phase = ds[i] / wavelength + now * rate;
			float ripple = Mathf::Pow(0.5f + 0.5f * Mathf::Cos(2.0f * Mathf::PI * phase), 6.0f) * carried;
			float visible = Mathf::Clamp01((rs[i] / PixelSize(pts[i], 1.0f) - 0.8f) / 2.5f);
			for (int k = 0; k <= sides; k++)
			{
				float theta = k * 2.0f * Mathf::PI / sides;
				auto normal = us[i] * Mathf::Cos(theta) + vs[i] * Mathf::Sin(theta);
				auto p = pts[i] + normal * rs[i];
				float dist = p.magnitude();
				float facing = dist > 1e-6f ? Mathf::Abs(Vector3::Dot(normal, p / dist)) : 1.0f;
				// Brightest where the eye grazes the wall: the tube's glowing edges from outside, the far walls from inside.
				float rim = 1.0f - facing;
				float glow = 0.05f + 0.95f * rim * rim * rim;
				float hue = 0.5f + 0.5f * Mathf::Sin(3.0f * theta + ds[i] / wavelength * 0.9f - now * 0.7f);
				auto c = Color::Lerp(blue, violet, hue) * (0.04f + 0.42f * glow) + ripCol * (ripple * (0.12f + 0.5f * glow));
				vertices.Add(p);
				colors.Add(c * visible);
				// The Fx beam shape across each face between two seams: bright lines running the tube's length.
				uvs.Add(Vector3(0.5f, static_cast<float>(k & 1), 1));
			}
		}
		int stride = sides + 1;
		for (int i = 0; i < n - 1; i++)
		{
			for (int k = 0; k < sides; k++)
			{
				int a = base + i * stride + k, b = a + 1, c = a + stride, d = c + 1;
				indices.Add(a);
				indices.Add(c);
				indices.Add(d);
				indices.Add(a);
				indices.Add(d);
				indices.Add(b);
			}
		}
		return maxPixels;
	}

	void LaneTunnel::Streak(float f0, float f1, float angle, float depth, Color color)
	{
		auto p0 = At(f0), p1 = At(f1);
		float ca = Mathf::Cos(angle), sa = Mathf::Sin(angle);
		auto a = p0.centre + (p0.u * ca + p0.v * sa) * (p0.radius * depth);
		auto b = p1.centre + (p1.u * ca + p1.v * sa) * (p1.radius * depth);
		auto along = b - a;
		if (along.sqrMagnitude() < 1e-12f) return;
		auto side = Vector3::Cross(along, (a + b) * 0.5f);
		if (side.sqrMagnitude() < 1e-20f) return;
		side.Normalize();
		float wa = Mathf::Max(p0.radius * 0.018f, PixelSize(a, 1.3f)) * 0.5f;
		float wb = Mathf::Max(p1.radius * 0.018f, PixelSize(b, 1.3f)) * 0.5f;
		int i = vertices.Count();
		vertices.Add(a - side * wa);
		vertices.Add(b - side * wb);
		vertices.Add(b + side * wb);
		vertices.Add(a + side * wa);
		for (int k = 0; k < 4; k++) colors.Add(color);
		// The Fx beam: a bright core across, tapered at both ends.
		uvs.Add(Vector3(0, 0, 1));
		uvs.Add(Vector3(1, 0, 1));
		uvs.Add(Vector3(1, 1, 1));
		uvs.Add(Vector3(0, 1, 1));
		indices.Add(i);
		indices.Add(i + 1);
		indices.Add(i + 2);
		indices.Add(i);
		indices.Add(i + 2);
		indices.Add(i + 3);
	}

	float LaneTunnel::PixelSize(Vector3 p, float pixels) const
	{
		return pixels * Mathf::Max(p.magnitude(), 1e-6f) / static_cast<float>(app->PixelsPerUnitAtUnitDistance());
	}

	float LaneTunnel::Hash(float n)
	{
		float x = Mathf::Sin(n * 12.9898f) * 43758.5453f;
		return x - Mathf::Floor(x);
	}
}
