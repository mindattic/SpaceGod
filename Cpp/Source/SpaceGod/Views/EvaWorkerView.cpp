// US-T158: the EVA repair worker drawn on the hull (see EvaWorkerView.h).
#include "Views/EvaWorkerView.h"

#include "UnitySpaceApp.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/ShipModels.h"

#include "Embodiment/RepairWorker.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using Rendering::MeshFactory;
	using Rendering::ShipModels;

	EvaWorkerView::EvaWorkerView(UnitySpaceApp* app_, const cs::Ref<Transform>& parent) : app(app_)
	{
		suit.SetColor("_Tint", Color(0.92f, 0.93f, 0.95f));
		visor.SetColor("_Tint", Color(0.95f, 0.72f, 0.2f));
		pack.SetColor("_Tint", Color(0.55f, 0.58f, 0.62f));
		band.SetColor("_Tint", Color(1.0f, 0.45f, 0.1f));
		auto go = NewGameObject("EVA worker");
		go->transform->SetParent(parent, false);
		root = go->transform;
		auto cube = MeshFactory::BuiltinCube();
		auto capsule = MeshFactory::BuiltinCapsule();
		auto sphere = MeshFactory::UvSphere(24, 12);
		// One unit tall, feet at the origin on the plating, +Y off the hull, +Z where it faces. Crouched over the work.
		Part(capsule, Vector3(-0.11f, 0.2f, 0.04f), Vector3(0.15f, 0.21f, 0.15f), Vector3(-12, 0, 0), suit, "leg L");
		Part(capsule, Vector3(0.11f, 0.2f, 0.04f), Vector3(0.15f, 0.21f, 0.15f), Vector3(-12, 0, 0), suit, "leg R");
		Part(capsule, Vector3(0, 0.58f, 0.04f), Vector3(0.4f, 0.24f, 0.3f), Vector3(14, 0, 0), suit, "torso");
		Part(cube, Vector3(0, 0.62f, -0.16f), Vector3(0.36f, 0.4f, 0.16f), Vector3(14, 0, 0), pack, "backpack");
		Part(cube, Vector3(0, 0.5f, -0.25f), Vector3(0.37f, 0.05f, 0.05f), Vector3(14, 0, 0), band, "backpack band");
		Part(sphere, Vector3(0, 0.93f, 0.1f), Vector3(0.17f, 0.17f, 0.17f), Vector3(0, 0, 0), suit, "helmet");
		Part(cube, Vector3(0, 0.93f, 0.23f), Vector3(0.2f, 0.11f, 0.05f), Vector3(10, 0, 0), visor, "visor");
		Part(capsule, Vector3(-0.21f, 0.6f, 0.2f), Vector3(0.1f, 0.2f, 0.1f), Vector3(70, 0, 8), suit, "arm L");
		Part(capsule, Vector3(0.19f, 0.56f, 0.24f), Vector3(0.1f, 0.21f, 0.1f), Vector3(78, 0, -8), suit, "arm R");
		Part(cube, Vector3(0.17f, 0.45f, 0.44f), Vector3(0.05f, 0.05f, 0.2f), Vector3(30, 0, 0), pack, "torch");

		// The spark: camera-facing quads in the Fx shader, rebuilt each frame (as the cargo pods' beam).
		spark = NewGameObject("EVA weld spark");
		spark->transform->SetParent(parent, false);
		sparkMesh = Materials::Own(cs::New<Mesh>("EVA weld spark"));
		sparkMesh->MarkDynamic();
		AddComponent<MeshFilter>(spark)->sharedMesh = sparkMesh;
		auto mr = AddComponent<MeshRenderer>(spark);
		mr->SetSharedMaterial(Materials::Create(Materials::Fx(), "EvaSparkFx"));
		mr->shadowCastingMode = ShadowCastingMode::Off;
		mr->receiveShadows = false;
		root->gameObject()->SetActive(false);
		spark->SetActive(false);
	}

	EvaWorkerView::~EvaWorkerView()
	{
		if (root != nullptr) Destroy(root->gameObject());
		if (spark != nullptr) Destroy(spark);
	}

	void EvaWorkerView::Part(const cs::Ref<Mesh>& mesh, Vector3 at, Vector3 scale, Vector3 euler, const MaterialPropertyBlock& block, const char* name)
	{
		auto go = NewGameObject(name);
		go->transform->SetParent(root, false);
		go->transform->localPosition = at;
		go->transform->localRotation = Quaternion::Euler(euler.x, euler.y, euler.z);
		go->transform->localScale = scale;
		AddComponent<MeshFilter>(go)->sharedMesh = mesh;
		auto mr = AddComponent<MeshRenderer>(go);
		mr->SetSharedMaterial(ShipModels::ProceduralMaterial());
		mr->shadowCastingMode = ShadowCastingMode::Off;
		mr->SetPropertyBlock(block);
	}

	void EvaWorkerView::Update()
	{
		auto w = cs::As<RepairWorker>(app->Seat());
		DVec3 scene, up, forward;
		double height = 0;
		bool shown = w != nullptr && app->WorkerPose(w, scene, up, forward, height);
		if (root->gameObject()->activeSelf != shown) root->gameObject()->SetActive(shown);
		bool welding = shown && w->Job != nullptr;
		if (spark->activeSelf != welding) spark->SetActive(welding);
		if (!shown) return;
		auto rel = app->Relative(scene);
		auto upV = Vector3(static_cast<float>(up.X), static_cast<float>(up.Y), static_cast<float>(up.Z));
		auto fwV = Vector3(static_cast<float>(forward.X), static_cast<float>(forward.Y), static_cast<float>(forward.Z));
		float h = static_cast<float>(height);
		root->localPosition = rel;
		root->localRotation = Quaternion::LookRotation(fwV, upV);
		root->localScale = Vector3(h, h, h);
		if (!welding) return;

		// The torch's tip, a flicker of white-blue with an amber flare and a few sparks thrown off it.
		vertices.Clear();
		colors.Clear();
		uvs.Clear();
		indices.Clear();
		auto tip = rel + root->localRotation * (Vector3(0.17f, 0.36f, 0.56f) * h);
		auto cam = app->UnityCamera->transform;
		float now = Time::unscaledTime;
		float flicker = 0.7f + 0.3f * Mathf::Sin(now * 53.0f) * Mathf::Sin(now * 31.0f);
		Sprite(tip, h * 0.5f * flicker, Color(1.0f, 0.55f, 0.15f) * 0.55f, cam->right(), cam->up());
		Sprite(tip, h * 0.18f * flicker, Color(0.75f, 0.9f, 1.0f), cam->right(), cam->up());
		Sprite(tip, h * 0.07f, Color::white(), cam->right(), cam->up());
		for (int k = 0; k < 6; k++)
		{
			float f = Mathf::Repeat(now * 2.3f + k * 0.37f, 1.0f);
			float a = k * 2.4f + now * 0.6f;
			auto dir = root->localRotation * Vector3(Mathf::Cos(a), 0.6f + 0.4f * Mathf::Sin(a * 1.7f), Mathf::Sin(a));
			Sprite(tip + dir * (h * 0.45f * f), h * 0.035f * (1 - f), Color(1.0f, 0.75f, 0.35f) * (1 - f), cam->right(), cam->up());
		}
		sparkMesh->Clear();
		sparkMesh->SetVertices(vertices);
		sparkMesh->SetColors(colors);
		sparkMesh->SetUVs(0, uvs);
		sparkMesh->SetIndices(indices, MeshTopology::Triangles, 0, false);
		sparkMesh->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 1e9f));
	}

	void EvaWorkerView::Sprite(Vector3 c, float radius, Color color, Vector3 right, Vector3 up)
	{
		right = right * radius;
		up = up * radius;
		int i = vertices.Count();
		vertices.Add(c - right - up);
		vertices.Add(c + right - up);
		vertices.Add(c + right + up);
		vertices.Add(c - right + up);
		for (int k = 0; k < 4; k++) colors.Add(color);
		// The Fx shader's soft round spot (BattleFx::Shape 0).
		uvs.Add(Vector3(0, 0, 0));
		uvs.Add(Vector3(1, 0, 0));
		uvs.Add(Vector3(1, 1, 0));
		uvs.Add(Vector3(0, 1, 0));
		indices.Add(i); indices.Add(i + 1); indices.Add(i + 2);
		indices.Add(i); indices.Add(i + 2); indices.Add(i + 3);
	}
}
