// Port of Unity.SpaceGod Runtime/Views/PodBeacons.cs (US-T30).
#include "Views/PodBeacons.h"

#include "UnitySpaceApp.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/ShipModels.h"

#include "Combat/Crews.h"
#include "Combat/War.h"
#include "RenderLod.h"
#include "ScaleModel.h"
#include "World.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using Rendering::MeshFactory;
	using Rendering::ShipModels;

	cs::Ref<Mesh>& PodBeacons::capsule()
	{
		static cs::Ref<Mesh> value;
		return value;
	}

	cs::Ref<Mesh>& PodBeacons::quad()
	{
		static cs::Ref<Mesh> value;
		return value;
	}

	cs::Ref<Material>& PodBeacons::beaconMaterial()
	{
		static cs::Ref<Material> value;
		return value;
	}

	PodBeacons::PodBeacons(UnitySpaceApp* app_, const cs::Ref<Transform>& parent_)
		: app(app_), parent(parent_)
	{
		block.SetColor(ColorId, Color(0.85f, 0.55f, 0.2f));
		if (!Alive(beaconMaterial()))
		{
			beaconMaterial() = Materials::MarkShared(Materials::Create(Materials::Glow(), "PodBeacon"));
			beaconMaterial()->SetColor("_Color", Color(0.35f, 0.95f, 1.0f));
			beaconMaterial()->SetFloat("_Intensity", 1.8f);
			beaconMaterial()->SetFloat("_Falloff", 2.0f);
		}
	}

	void PodBeacons::Update()
	{
		const auto& pods = app->World()->War->Crews->Pods;
		auto cam = app->UnityCamera->transform;
		double ppu = app->PixelsPerUnitAtUnitDistance();
		float now = Time::unscaledTime;
		for (const auto& p : pods)
		{
			if (!p->Drifting()) continue;
			cs::Ref<View> v;
			if (!views.TryGetValue(p, v)) views[p] = v = Create(p);
			auto centre = app->PodScene(p);
			auto rel = app->Relative(centre);
			float length = static_cast<float>(app->Scale()->ShipLength(PodLengthKm));
			// The capsules while they can be made out, then the beacon alone, then nothing below a pixel (U.SPC-T415).
			auto tier = RenderLod::Tier(RenderLod::Pod(), RenderLod::ApparentPx(length * 4.0f, rel.magnitude(), ppu));
			bool model = RenderLod::DrawsModel(tier);
			for (const auto& c : v->Capsules)
				if (c->gameObject()->activeSelf != model) c->gameObject()->SetActive(model);
			if (tier == LodTier::Culled)
			{
				v->Beacon->gameObject()->SetActive(false);
				continue;
			}
			for (int k = 0; k < v->Capsules.Count(); k++)
			{
				auto t = v->Capsules[k];
				// Each capsule keeps its own place in the loose cluster, a few pod lengths apart.
				float a = k * 2.399f + p->Id, r = length * (1.5f + 1.2f * k);
				auto off = Vector3(Mathf::Cos(a) * r, Mathf::Sin(a * 1.7f) * r * 0.4f, Mathf::Sin(a) * r);
				t->localPosition = rel + off;
				t->localRotation = Quaternion::Euler(now * (6 + k * 2), now * (4 + k * 3), now * (3 + k));
				t->localScale = Vector3(length * 0.45f, length * 0.5f, length * 0.45f);
			}
			// The beacon blinks once a second: a few pixels wide at any range.
			bool on = Mathf::Repeat(now + p->Id * 0.37f, 1.0f) < 0.35f;
			v->Beacon->gameObject()->SetActive(on);
			if (on)
			{
				float px = 6.0f * Mathf::Max(rel.magnitude(), 1e-6f) / static_cast<float>(ppu);
				v->Beacon->localPosition = rel;
				v->Beacon->SetRotation(Quaternion::LookRotation(v->Beacon->position() - cam->position(), cam->up()));
				v->Beacon->localScale = Vector3::one() * Mathf::Max(px, length * 2.0f);
			}
		}
		gone.Clear();
		for (const auto& p : views.Keys())
			if (!p->Drifting() || !pods.Contains(p)) gone.Add(p);
		for (const auto& p : gone)
		{
			auto v = views.At(p);
			for (const auto& t : v->Capsules) Destroy(t->gameObject());
			Destroy(v->Beacon->gameObject());
			views.Remove(p);
		}
	}

	cs::Ref<PodBeacons::View> PodBeacons::Create(const cs::Ref<EscapePod>& p)
	{
		if (!capsule()) capsule() = MeshFactory::BuiltinCapsule(); // Resources.GetBuiltinResource<Mesh>("Capsule.fbx")
		if (!quad()) quad() = MeshFactory::Quad();
		auto v = cs::New<View>();
		v->Capsules = cs::NewArray<cs::Ref<Transform>>(Mathf::Clamp(p->Pods, 1, MaxCapsules));
		for (int k = 0; k < v->Capsules.Count(); k++)
		{
			auto go = NewGameObject(cs::Cat(p->Name, " escape pod ", k));
			go->transform->SetParent(parent, false);
			AddComponent<MeshFilter>(go)->sharedMesh = capsule();
			auto mr = AddComponent<MeshRenderer>(go);
			mr->SetSharedMaterial(ShipModels::ProceduralMaterial());
			mr->shadowCastingMode = ShadowCastingMode::Off;
			mr->SetPropertyBlock(block);
			v->Capsules[k] = go->transform;
		}
		auto beacon = NewGameObject(cs::Cat(p->Name, " pod beacon"));
		beacon->transform->SetParent(parent, false);
		AddComponent<MeshFilter>(beacon)->sharedMesh = quad();
		auto br = AddComponent<MeshRenderer>(beacon);
		br->SetSharedMaterial(beaconMaterial());
		br->shadowCastingMode = ShadowCastingMode::Off;
		v->Beacon = beacon->transform;
		return v;
	}
}
