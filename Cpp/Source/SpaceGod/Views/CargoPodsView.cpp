// US-T95: the drifting cargo pods and the tractor beam, drawn (see CargoPodsView.h).
#include "Views/CargoPodsView.h"

#include "UnitySpaceApp.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/ShipModels.h"

#include "Combat/CargoPods.h"
#include "Combat/War.h"
#include "Combat/Wreck.h"
#include "RenderLod.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using Rendering::MeshFactory;
	using Rendering::ShipModels;

	namespace
	{
		// The Fx shader's shapes (BattleFx::Shape): a soft round spot, and a beam's soft-edged strip.
		constexpr float Radial = 0, Beam = 1;
	}

	cs::Ref<Mesh>& CargoPodsView::cube()
	{
		static cs::Ref<Mesh> value;
		return value;
	}

	CargoPodsView::CargoPodsView(UnitySpaceApp* app_, const cs::Ref<Transform>& parent_)
		: app(app_), parent(parent_)
	{
		block.SetColor("_Tint", Color(0.95f, 0.62f, 0.18f));
		stolenBlock.SetColor("_Tint", Color(0.85f, 0.25f, 0.2f));
		auto go = NewGameObject("Cargo pods FX");
		go->transform->SetParent(parent, false);
		auto m = cs::New<Mesh>("Cargo pods FX");
		m->indexFormat = IndexFormat::UInt32;
		fx = Materials::Own(m);
		fx->MarkDynamic();
		AddComponent<MeshFilter>(go)->sharedMesh = fx;
		auto mr = AddComponent<MeshRenderer>(go);
		mr->SetSharedMaterial(Materials::Create(Materials::Fx(), "TractorFx"));
		mr->shadowCastingMode = ShadowCastingMode::Off;
		mr->receiveShadows = false;
	}

	void CargoPodsView::Update()
	{
		vertices.Clear();
		colors.Clear();
		uvs.Clear();
		indices.Clear();
		auto world = app->World();
		auto wrecks = world != nullptr && world->War != nullptr ? world->War->Wrecks : nullptr;
		auto pods = wrecks != nullptr ? wrecks->Pods : nullptr;
		double ppu = app->PixelsPerUnitAtUnitDistance();
		float now = Time::unscaledTime;
		double jd = app->Clock->Jd;
		float length = static_cast<float>(app->Scale()->ShipLength(PodLengthKm));
		// Markers only within a few thousand km (at the scale's ship mapping): a debris field's, not the whole system's.
		float markerReach = static_cast<float>(app->Scale()->ShipLength(3000.0));
		auto held = app->TractorPod();

		if (pods != nullptr)
			for (const auto& p : pods->Items)
			{
				if (p->Expired(jd)) continue;
				cs::Ref<Transform> t;
				if (!views.TryGetValue(p, t)) views[p] = t = Create(p);
				auto rel = app->Relative(app->CargoPodScene(p));
				float dist = rel.magnitude();
				auto tier = RenderLod::Tier(RenderLod::Pod(), RenderLod::ApparentPx(length * 2.0f, dist, ppu));
				bool model = RenderLod::DrawsModel(tier);
				if (t->gameObject()->activeSelf != model) t->gameObject()->SetActive(model);
				if (model)
				{
					t->localPosition = rel;
					float spin = static_cast<float>(p->Id % 7) + 3;
					t->localRotation = Quaternion::Euler(now * spin, now * (spin * 0.7f + 2), now * (spin * 0.4f + 1));
					t->localScale = Vector3(length, length * 0.55f, length * 0.55f);
				}
				if (dist > markerReach && p != held) continue;
				// A faint amber marker, blinking slowly, a few pixels wide at any range inside the reach.
				float blink = Mathf::Repeat(now * 0.7f + p->Id * 0.31f, 1.0f) < 0.5f ? 1.0f : 0.45f;
				auto c = (p->Stolen ? Color(1.0f, 0.35f, 0.25f) : Color(1.0f, 0.72f, 0.25f)) * (0.55f * blink);
				Sprite(rel, PixelSize(rel, 4.5f), c);
			}

		// The beam, from the hull flown to the pod it holds.
		auto ship = world != nullptr ? world->PilotedShip : nullptr;
		if (held != nullptr && ship != nullptr && !ship->Destroyed)
		{
			auto a = app->Relative(app->ShipScene(ship));
			auto b = app->Relative(app->CargoPodScene(held));
			float pulse = 0.75f + 0.25f * Mathf::Sin(now * 9.0f);
			Color beam(0.45f, 1.0f, 0.78f);
			Ribbon(a, b, 14.0f, beam * (0.3f * pulse));
			Ribbon(a, b, 5.0f, beam * (0.9f * pulse));
			Ribbon(a, b, 1.8f, Color::white());
			// Rings running down the beam toward the hull: the pull, at a glance.
			for (int k = 0; k < 5; k++)
			{
				float f = Mathf::Repeat(now * 0.9f + k / 5.0f, 1.0f);
				auto at = Vector3::Lerp(b, a, f);
				Sprite(at, PixelSize(at, 9.0f + 6.0f * (1 - f)), beam * (0.8f * (1 - f * 0.5f)));
			}
			Sprite(b, PixelSize(b, 16.0f * pulse), beam * 0.8f);
			Sprite(a, PixelSize(a, 11.0f), Color::white() * (0.6f * pulse));
		}

		// US-T264: the mining beam, a flickering orange-white cutting lance from the hull to the face it heats, sparks at the face.
		DVec3 mineFrom, mineTo;
		if (app->MiningBeamScene(mineFrom, mineTo))
		{
			auto a = app->Relative(mineFrom);
			auto b = app->Relative(mineTo);
			float flicker = 0.8f + 0.2f * Mathf::Sin(now * 37.0f) * Mathf::Sin(now * 11.0f);
			Color cut(1.0f, 0.55f, 0.18f);
			Ribbon(a, b, 12.0f, cut * (0.3f * flicker));
			Ribbon(a, b, 4.0f, cut * (0.9f * flicker));
			Ribbon(a, b, 1.5f, Color::white());
			for (int k = 0; k < 4; k++)
			{
				float f = Mathf::Repeat(now * 2.3f + k * 0.37f, 1.0f);
				Sprite(b, PixelSize(b, 6.0f + 14.0f * f), Color(1.0f, 0.8f, 0.4f) * (0.9f * (1 - f)));
			}
			Sprite(b, PixelSize(b, 18.0f * flicker), cut * 0.9f);
		}

		gone.Clear();
		for (const auto& p : views.Keys())
			if (pods == nullptr || p->Expired(jd) || !pods->Items.Contains(p)) gone.Add(p);
		for (const auto& p : gone)
		{
			Destroy(views.At(p)->gameObject());
			views.Remove(p);
		}

		fx->Clear();
		fx->SetVertices(vertices);
		fx->SetColors(colors);
		fx->SetUVs(0, uvs);
		fx->SetIndices(indices, MeshTopology::Triangles, 0, false);
		fx->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 1e9f));
	}

	cs::Ref<Transform> CargoPodsView::Create(const cs::Ref<CargoPod>& p)
	{
		if (!cube()) cube() = MeshFactory::BuiltinCube();
		auto go = NewGameObject(cs::Cat("Cargo pod ", p->Id, " from ", p->From));
		go->transform->SetParent(parent, false);
		AddComponent<MeshFilter>(go)->sharedMesh = cube();
		auto mr = AddComponent<MeshRenderer>(go);
		mr->SetSharedMaterial(ShipModels::ProceduralMaterial());
		mr->shadowCastingMode = ShadowCastingMode::Off;
		mr->SetPropertyBlock(p->Stolen ? stolenBlock : block);
		return go->transform;
	}

	float CargoPodsView::PixelSize(Vector3 p, float pixels) const
	{
		return pixels * Mathf::Max(p.magnitude(), 1e-6f) / static_cast<float>(app->PixelsPerUnitAtUnitDistance());
	}

	void CargoPodsView::Sprite(Vector3 c, float radius, Color color)
	{
		auto cam = app->UnityCamera->transform;
		auto right = cam->right() * radius;
		auto up = cam->up() * radius;
		int i = vertices.Count();
		vertices.Add(c - right - up);
		vertices.Add(c + right - up);
		vertices.Add(c + right + up);
		vertices.Add(c - right + up);
		Corners(color, Radial);
		indices.Add(i); indices.Add(i + 1); indices.Add(i + 2);
		indices.Add(i); indices.Add(i + 2); indices.Add(i + 3);
	}

	void CargoPodsView::Ribbon(Vector3 a, Vector3 b, float pixels, Color color)
	{
		auto mid = (a + b) * 0.5f;
		auto along = b - a;
		if (along.sqrMagnitude() < 1e-12f) return;
		auto side = Vector3::Cross(along, mid).normalized();
		float wa = PixelSize(a, pixels) * 0.5f, wb = PixelSize(b, pixels) * 0.5f;
		int i = vertices.Count();
		vertices.Add(a - side * wa);
		vertices.Add(b - side * wb);
		vertices.Add(b + side * wb);
		vertices.Add(a + side * wa);
		Corners(color, Beam);
		indices.Add(i); indices.Add(i + 1); indices.Add(i + 2);
		indices.Add(i); indices.Add(i + 2); indices.Add(i + 3);
	}

	void CargoPodsView::Corners(Color color, float shape)
	{
		for (int k = 0; k < 4; k++) colors.Add(color);
		uvs.Add(Vector3(0, 0, shape));
		uvs.Add(Vector3(1, 0, shape));
		uvs.Add(Vector3(1, 1, shape));
		uvs.Add(Vector3(0, 1, shape));
	}
}
