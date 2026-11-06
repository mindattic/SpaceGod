// Port of Unity.SpaceGod Runtime/Views/DebrisFields.cs (US-T30).
#include "Views/DebrisFields.h"

#include "UnitySpaceApp.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/ShipModels.h"

#include "Combat/SectionColliders.h"
#include "Combat/War.h"
#include "Combat/Wreck.h"
#include "ScaleModel.h"
#include "SimClock.h"
#include "World.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::MeshFactory;
	using Rendering::ShipModels;

	cs::Ref<Mesh>& DebrisFields::shard()
	{
		static cs::Ref<Mesh> value;
		return value;
	}

	DebrisFields::DebrisFields(UnitySpaceApp* app_, const cs::Ref<Transform>& parent_)
		: app(app_), parent(parent_)
	{
		block.SetColor(ColorId, Color(0.24f, 0.22f, 0.21f));
	}

	void DebrisFields::Update()
	{
		auto wrecks = app->World()->War->Wrecks;
		double jd = app->Clock->Jd;
		float now = Time::unscaledTime;
		for (const auto& w : wrecks->Items)
		{
			// A C# array is a reference: the dictionary's list is the one filled in place (held here by value, never shared).
			if (!fields.ContainsKey(w)) fields[w] = Create(w);
			auto& pieces = fields.At(w);
			auto centre = app->WreckScene(w);
			float length = static_cast<float>(app->Scale()->ShipLength(w->LengthKm));
			float size = length * 0.07f * static_cast<float>(w->Fade(jd));
			// US-T177: a piece drifting into a station bounces off it (the same path the pilot's collisions use).
			auto stations = SectionColliders::StationsNear(app->World(), app->World()->WreckHelio(w, jd), w->DebrisRadiusKm(jd) + w->LengthKm, jd);
			for (int k = 0; k < pieces.Count(); k++)
			{
				auto t = pieces[k];
				t->localPosition = app->Relative(centre + SectionColliders::BounceOff(DVec3::Zero, w->DebrisOffsetKm(k, jd), stations).SwapYZ() * app->BattleKmScale());
				t->localRotation = Quaternion::Euler(now * (11 + k * 3), now * (7 + k * 5), now * (5 + k * 2));
				t->localScale = Vector3(size, size * (0.3f + 0.15f * (k % 4)), size * (0.6f + 0.1f * (k % 3)));
			}
		}
		gone.Clear();
		for (const auto& w : fields.Keys())
			if (!wrecks->Items.Contains(w)) gone.Add(w);
		for (const auto& w : gone)
		{
			for (const auto& t : fields.At(w)) Destroy(t->gameObject());
			fields.Remove(w);
		}
	}

	cs::List<cs::Ref<Transform>> DebrisFields::Create(const cs::Ref<Wreck>& w)
	{
		if (!shard()) shard() = MeshFactory::BuiltinCube(); // Resources.GetBuiltinResource<Mesh>("Cube.fbx")
		auto pieces = cs::NewArray<cs::Ref<Transform>>(w->DebrisPieces());
		for (int k = 0; k < pieces.Count(); k++)
		{
			auto go = NewGameObject(cs::Cat(w->Name, " debris ", k));
			go->transform->SetParent(parent, false);
			AddComponent<MeshFilter>(go)->sharedMesh = shard();
			auto mr = AddComponent<MeshRenderer>(go);
			mr->SetSharedMaterial(ShipModels::ProceduralMaterial());
			mr->shadowCastingMode = ShadowCastingMode::Off;
			mr->SetPropertyBlock(block);
			pieces[k] = go->transform;
		}
		return pieces;
	}
}
