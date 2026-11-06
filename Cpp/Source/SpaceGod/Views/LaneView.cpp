// Port of Unity.SpaceGod Runtime/Views/LaneView.cs (US-T30).
#include "Views/LaneView.h"
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

	void LaneView::Flash(const cs::Ref<Lane>& lane, int at)
	{
		flashLane = lane;
		flashAt = at;
		flashTime = Time::unscaledTime;
	}

	float LaneView::FlashOf(const cs::Ref<Lane>& lane, int at, float now) const
	{
		return lane == flashLane && at == flashAt ? Mathf::Clamp01(1.0f - (now - flashTime) / FlashSeconds) : 0.0f;
	}

	LaneView::LaneView(UnitySpaceApp* app_, const cs::Ref<Transform>& parent)
		: app(app_)
	{
		auto go = NewGameObject("Hyperlanes");
		go->transform->SetParent(parent, false);
		auto m = cs::New<Mesh>("Hyperlanes");
		m->indexFormat = IndexFormat::UInt32;
		mesh = Materials::Own(m);
		mesh->MarkDynamic();
		AddComponent<MeshFilter>(go)->sharedMesh = mesh;
		renderer = AddComponent<MeshRenderer>(go);
		renderer->SetSharedMaterial(Materials::Create(Materials::Fx(), "Hyperlanes"));
		renderer->shadowCastingMode = ShadowCastingMode::Off;
		tunnel = cs::New<LaneTunnel>(app, parent);
	}

	void LaneView::Update(bool visible)
	{
		renderer->enabled = visible;
		auto lanes = app->Fleet()->Lanes;
		tunnel->Begin(visible && lanes != nullptr);
		if (!visible || lanes == nullptr) return;
		vertices.Clear();
		colors.Clear();
		uvs.Clear();
		indices.Clear();

		auto eph = app->Ephemeris();
		auto scale = app->Scale();
		const auto& scene = app->BodyScenes();
		float now = Time::unscaledTime;
		int count = lanes->Lanes.Count() + lanes->GateSpurs.Count();
		for (int n = 0; n < count; n++)
		{
			auto lane = n < lanes->Lanes.Count() ? lanes->Lanes[n] : lanes->GateSpurs[n - lanes->Lanes.Count()];
			auto color = lane->IsGateSpur() ? SpurColor : LaneColor;
			auto aKm = lanes->RampHelio(lane, lane->A, eph->Jd);
			auto bKm = lanes->RampHelio(lane, lane->B, eph->Jd);
			for (int i = 0; i <= Samples; i++)
			{
				DVec3 discard; // out _
				curve[i] = app->Relative(scale->LaneCurveScene(eph, scene, lanes, lane, lane->A, aKm, lane->B, bKm, static_cast<double>(i) / Samples, discard));
			}
			for (int i = 0; i < Samples; i++)
			{
				Ribbon(curve[i], curve[i + 1], 6.0f, color * 0.16f);
				Ribbon(curve[i], curve[i + 1], 1.4f, color * 0.45f);
			}
			Sprite(curve[0], 16, color * 0.7f, 2);
			Sprite(curve[Samples], 16, color * 0.7f, 2);
			float gateA = Gate(lane, lane->A, aKm, curve[0], curve[1], now);
			float gateB = Gate(lane, lane->B, bKm, curve[Samples], curve[Samples - 1], now);
			float tube = TubeRadius(lane);
			RingChain(lane, color, now, tube);
			tunnel->Draw(lane, curve, Samples, tube, gateA, gateB, Rider(lane), now);
			// Ambient flow: a faint pulse each way, just to read as a live highway at any zoom,
			// even when nothing is actually riding it right now.
			for (int k = 0; k < 2; k++)
			{
				float phase = cs::Mod(now * 0.06f + k / 2.0f + lane->Id * 0.137f, 1.0f);
				Sprite(Along(phase), 4, PulseColor * 0.35f, 0);
			}
			// Real traffic: every ship actually riding this lane, or holding just off it after an
			// interrupted ride, drawn at its own live progress and tinted by faction -- the lane is
			// the simulation's real interplanetary transport, not a decorative loop (see
			// Hyperlanes.cs); a pulse reaching the far ramp should correspond to an actual ship.
			for (const auto& ship : app->Fleet()->Ships())
			{
				if (ship->Destroyed) continue;
				double progress;
				if (ship->Phase == ShipPhase::Lane && ship->LaneLeg.Lane == lane) progress = Core::Fleet::LaneProgress(ship, eph->Jd);
				else if ((ship->Phase == ShipPhase::Lurk || ship->Phase == ShipPhase::Adrift) && ship->LaneLeg.Lane == lane) progress = ship->LaneHold;
				else continue;
				// Progress counts from the ramp the ship set out from; the curve runs A to B.
				if (ship->LaneLeg.From != lane->A) progress = 1 - progress;
				auto fc = ship->Faction->Color;
				Sprite(Along(static_cast<float>(progress)), 7, Color(fc.R, fc.G, fc.B) * 1.3f, 0);
			}
		}

		mesh->Clear();
		mesh->SetVertices(vertices);
		mesh->SetColors(colors);
		mesh->SetUVs(0, uvs);
		mesh->SetIndices(indices, MeshTopology::Triangles, 0, false);
		mesh->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 1e9f));
		tunnel->End();
	}

	float LaneView::TubeRadius(const cs::Ref<Lane>& lane)
	{
		double trueKm = app->Fleet()->Lanes->LaneLengthKm(lane, app->Ephemeris()->Jd);
		if (trueKm <= 0) return 0;
		float sceneLen = 0;
		for (int i = 0; i < Samples; i++) sceneLen += (curve[i + 1] - curve[i]).magnitude();
		float trueRadius = static_cast<float>(Hyperlanes::GateRadiusKm * sceneLen / trueKm);
		return trueRadius > 0 ? LaneTunnel::DrawnRadius(trueRadius, app->Scale()->Blend()) : 0.0f;
	}

	cs::Ref<Ship> LaneView::Rider(const cs::Ref<Lane>& lane)
	{
		return app->RidingLane() && app->Piloted.Ship->LaneLeg.Lane == lane ? app->Piloted.Ship : nullptr;
	}

	float LaneView::Gate(const cs::Ref<Lane>& lane, int at, DVec3 rampKm, Vector3 centre, Vector3 inward, float now)
	{
		auto axis = inward - centre;
		if (axis.sqrMagnitude() < 1e-12f) return 0;
		axis.Normalize();
		auto eph = app->Ephemeris();
		auto radial = app->Fleet()->Lanes->RampDirection(lane, at, eph->Jd);
		auto across = DVec3::Cross(radial, DVec3::UnitZ);
		across = across.LengthSquared() > 1e-12 ? across.Normalized() : DVec3::UnitX;
		auto c = app->Scale()->MapNear(eph, app->BodyScenes(), at, rampKm);
		auto e = app->Scale()->MapNear(eph, app->BodyScenes(), at, rampKm + across * Hyperlanes::GateRadiusKm);
		float flash = FlashOf(lane, at, now);
		float trueRadius = static_cast<float>((e - c).Length());
		float radius = Mathf::Max(trueRadius, PixelSize(centre, 9)) * (1.0f + 0.25f * flash);
		auto u = Vector3::Cross(axis, Mathf::Abs(Vector3::Dot(axis, Vector3::up())) > 0.98f ? Vector3::right() : Vector3::up()).normalized();
		auto v = Vector3::Cross(axis, u);
		auto color = GateColor * (0.75f + 2.5f * flash);
		float width = 2.2f + 5.0f * flash;
		auto prev = centre + u * radius;
		for (int k = 1; k <= RingSegments; k++)
		{
			float t = k * Mathf::PI * 2.0f / RingSegments;
			auto p = centre + (u * Mathf::Cos(t) + v * Mathf::Sin(t)) * radius;
			Ribbon(prev, p, width, color);
			prev = p;
		}
		if (flash > 0) Sprite(centre, 18 + 70 * flash, PulseColor * (2.2f * flash), 0);
		return trueRadius;
	}

	void LaneView::RingChain(const cs::Ref<Lane>& lane, Color color, float now, float ringRadius)
	{
		auto lanes = app->Fleet()->Lanes;
		auto eph = app->Ephemeris();
		double trueKm = lanes->LaneLengthKm(lane, eph->Jd);
		if (trueKm <= 0 || ringRadius <= 0) return;

		// The rings sit on the tunnel's wall (US-T195): the tube's drawn radius, not the bare true one.
		cs::Ref<Ship> rider = Rider(lane);
		int rings = Hyperlanes::RingCount(rider != nullptr ? rider->LaneLengthKm : trueKm);
		int passed = rider != nullptr ? Hyperlanes::RingsPassed(Core::Fleet::LaneProgress(rider, eph->Jd), rings) : -10;
		for (int k = 0; k < rings; k++)
		{
			double s = Hyperlanes::RingFraction(k, rings);
			Vector3 centre, axis;
			if (rider != nullptr)
			{
				// The rider's curve runs from the ramp it set out from: ring k from there.
				DVec3 dir;
				centre = app->Relative(app->Scale()->LaneScene(eph, app->BodyScenes(), lanes, rider, s, dir));
				axis = Vector3(static_cast<float>(dir.X), static_cast<float>(dir.Y), static_cast<float>(dir.Z));
			}
			else
			{
				float fs = static_cast<float>(s);
				centre = Along(fs);
				axis = Along(Mathf::Min(1.0f, fs + 0.002f)) - Along(Mathf::Max(0.0f, fs - 0.002f));
			}
			if (axis.sqrMagnitude() < 1e-12f) continue;
			// Below a couple of pixels a ring is not drawn at all: the ribbon carries the lane from afar.
			if (ringRadius < PixelSize(centre, 2.5f)) continue;
			axis.Normalize();
			// The magnetic pulse running down the chain, and the rings the rider has just flown through flaring.
			float wave = 0.55f + 0.45f * Mathf::Sin(k * 0.9f - now * 5.0f);
			float flare = rider != nullptr ? Mathf::Clamp01(1.0f - Mathf::Abs(passed - 1 - k) * 0.5f) : 0.0f;
			Hoop(centre, axis, ringRadius, 1.6f + 2.5f * flare, color * (0.45f + 0.5f * wave + 1.6f * flare));
		}
	}

	void LaneView::Hoop(Vector3 centre, Vector3 axis, float radius, float width, Color color)
	{
		auto u = Vector3::Cross(axis, Mathf::Abs(Vector3::Dot(axis, Vector3::up())) > 0.98f ? Vector3::right() : Vector3::up()).normalized();
		auto v = Vector3::Cross(axis, u);
		auto prev = centre + u * radius;
		for (int k = 1; k <= RingSegments; k++)
		{
			float t = k * Mathf::PI * 2.0f / RingSegments;
			auto p = centre + (u * Mathf::Cos(t) + v * Mathf::Sin(t)) * radius;
			Ribbon(prev, p, width, color);
			prev = p;
		}
	}

	Vector3 LaneView::Along(float s)
	{
		float f = s * Samples;
		int i = Mathf::Clamp(cs::ToInt(f), 0, Samples - 1);
		return Vector3::Lerp(curve[i], curve[i + 1], f - i);
	}

	float LaneView::PixelSize(Vector3 p, float pixels) { return pixels * Mathf::Max(p.magnitude(), 1e-6f) / static_cast<float>(app->PixelsPerUnitAtUnitDistance()); }

	void LaneView::Ribbon(Vector3 a, Vector3 b, float pixels, Color color)
	{
		auto along = b - a;
		if (along.sqrMagnitude() < 1e-12f) return;
		auto side = Vector3::Cross(along, (a + b) * 0.5f).normalized();
		float wa = PixelSize(a, pixels) * 0.5f, wb = PixelSize(b, pixels) * 0.5f;
		int i = vertices.Count();
		vertices.Add(a - side * wa);
		vertices.Add(b - side * wb);
		vertices.Add(b + side * wb);
		vertices.Add(a + side * wa);
		// A long continuous ribbon: shape it across only (uv.x mid-range keeps the ends untapered).
		Corners(color, 1, 0.5f, 0.5f);
		Indices(i);
	}

	void LaneView::Sprite(Vector3 c, float pixels, Color color, float shape)
	{
		auto cam = app->UnityCamera->transform;
		float r = PixelSize(c, pixels);
		auto right = cam->right() * r;
		auto up = cam->up() * r;
		int i = vertices.Count();
		vertices.Add(c - right - up);
		vertices.Add(c + right - up);
		vertices.Add(c + right + up);
		vertices.Add(c - right + up);
		Corners(color, shape, 0, 1);
		Indices(i);
	}

	void LaneView::Corners(Color color, float shape, float u0, float u1)
	{
		for (int k = 0; k < 4; k++) colors.Add(color);
		uvs.Add(Vector3(u0, 0, shape));
		uvs.Add(Vector3(u1, 0, shape));
		uvs.Add(Vector3(u1, 1, shape));
		uvs.Add(Vector3(u0, 1, shape));
	}

	void LaneView::Indices(int i)
	{
		indices.Add(i);
		indices.Add(i + 1);
		indices.Add(i + 2);
		indices.Add(i);
		indices.Add(i + 2);
		indices.Add(i + 3);
	}
}
