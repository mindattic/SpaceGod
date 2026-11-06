// Port of Unity.SpaceGod Runtime/Views/SatelliteSwarm.cs (US-T30).
#include "Views/SatelliteSwarm.h"

#include "UnitySpaceApp.h"
#include "Camera/SpaceCamera.h"
#include "Rendering/Materials.h"

#include "BodyCatalog.h"
#include "Infrastructure/Infrastructure.h"
#include "ScaleModel.h"
#include "SimClock.h"
#include "World.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;

	SatelliteSwarm::SatelliteSwarm(UnitySpaceApp* app_, const cs::Ref<Transform>& parent)
		: app(app_), cloudParent(parent)
	{
		auto infra = app->World()->Infrastructure;
		if (infra == nullptr) return;
		for (const auto& c : infra->Constellations) Add(c);
	}

	void SatelliteSwarm::Sync()
	{
		// US-T100: a network the simulation puts up over a growing colony, or a shell it adds to one, is drawn from then on.
		auto world = app->World();
		auto infra = world != nullptr ? world->Infrastructure : nullptr;
		if (infra == nullptr) return;
		for (const auto& c : infra->Constellations)
		{
			cs::Ref<Cloud> drawn = nullptr;
			for (const auto& cl : clouds)
				if (cl->C == c) { drawn = cl; break; }
			if (drawn != nullptr && drawn->Offsets.Count() == c->Count()) continue;
			if (drawn != nullptr)
			{
				Destroy(drawn->T->gameObject());
				clouds.Remove(drawn);
			}
			Add(c);
		}
	}

	void SatelliteSwarm::Add(const cs::Ref<Constellation>& c)
	{
		{
			int n = c->Count();
			auto go = NewGameObject(c->Name);
			go->transform->SetParent(cloudParent, false);
			auto m = cs::New<Mesh>(c->Name); // new Mesh { name = c.Name, indexFormat = IndexFormat.UInt32 }
			m->indexFormat = IndexFormat::UInt32;
			auto mesh = Materials::Own(m);
			mesh->MarkDynamic();
			auto vertices = cs::NewArray<Vector3>(n);
			auto colors = cs::NewArray<Color>(n);
			auto indices = cs::NewArray<int>(n);
			for (int i = 0; i < n; i++)
			{
				colors[i] = Color(c->Color.R, c->Color.G, c->Color.B, 0.85f);
				indices[i] = i;
			}
			mesh->SetVertices(vertices);
			mesh->SetColors(colors);
			mesh->SetIndices(indices, MeshTopology::Points, 0, false);
			mesh->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 1e7f));
			AddComponent<MeshFilter>(go)->sharedMesh = mesh;
			auto mr = AddComponent<MeshRenderer>(go);
			auto material = Materials::Create(Materials::Points(), c->Name);
			material->SetFloat("_Brightness", 1.4f);
			mr->SetSharedMaterial(material);
			mr->shadowCastingMode = ShadowCastingMode::Off;
			auto cloud = cs::New<Cloud>();
			cloud->C = c;
			cloud->Mesh = mesh;
			cloud->Renderer = mr;
			cloud->Material = material;
			cloud->Offsets = cs::NewArray<DVec3>(n);
			cloud->Vertices = vertices;
			cloud->T = go->transform;
			clouds.Add(cloud);
		}
	}

	void SatelliteSwarm::Update(bool visible)
	{
		Sync();
		auto scale = app->Scale();
		double jd = app->Clock->Jd;
		for (const auto& cl : clouds)
		{
			auto body = (*app->Catalog())[cl->C->Body];
			auto centre = app->BodyScene(cl->C->Body);
			double camera = (app->SpaceCamera->ScenePosition - centre).Length();
			double reach = scale->LocalDistance(body, 42164 * 1.2);
			// Only worth drawing within a few times the belt's reach, and dimmed past it.
			float alpha = Mathf::Clamp01(2.2f - static_cast<float>(camera / (reach * 6)));
			cl->Renderer->enabled = visible && alpha > 0.02f;
			if (!cl->Renderer->enabled) continue;
			cl->C->Offsets(body, jd, cl->Offsets);
			for (int i = 0; i < cl->Offsets.Count(); i++)
			{
				auto o = scale->LocalOffset(body, cl->Offsets[i]);
				cl->Vertices[i] = Vector3(static_cast<float>(o.X), static_cast<float>(o.Y), static_cast<float>(o.Z));
			}
			cl->Mesh->SetVertices(cl->Vertices);
			cl->T->localPosition = app->Relative(centre);
			cl->Material->SetFloat("_Brightness", 1.4f * alpha);
		}
	}
}
