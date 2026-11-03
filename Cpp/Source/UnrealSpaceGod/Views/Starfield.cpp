// Port of Unity.SpaceGod Runtime/Views/Starfield.cs (US-T30).
#include "Views/Starfield.h"

#include "Rendering/Materials.h"
#include "Views/BodyView.h"

#include "Astro.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;

	cs::Ref<Material>& Starfield::sharedMaterial()
	{
		static cs::Ref<Material> value;
		return value;
	}

	cs::Ref<GameObject> Starfield::Create(const cs::Ref<Transform>& parent, int count, int seed, bool shared)
	{
		cs::Random rng(seed);
		auto galacticPole = BodyView::ToUnity(Astro::PoleFromRaDec(192.86, 27.13)).normalized();
		auto vertices = cs::NewArray<Vector3>(count);
		auto colors = cs::NewArray<Color>(count);
		auto indices = cs::NewArray<int>(count);

		for (int i = 0; i < count; i++)
		{
			Vector3 dir = RandomDirection(rng);
			if (i % 3 == 0)
			{
				// Squash toward the galactic plane.
				float h = Vector3::Dot(dir, galacticPole);
				dir = (dir - galacticPole * h * 0.88f).normalized();
			}
			vertices[i] = dir;
			indices[i] = i;

			double r = rng.NextDouble();
			float brightness = static_cast<float>(0.12 + 0.88 * cs::Math::Pow(r, 7));
			float temperature = static_cast<float>(rng.NextDouble());
			auto tint = temperature < 0.25f ? Color(0.7f, 0.8f, 1.0f)
				: temperature < 0.8f ? Color(1.0f, 0.97f, 0.92f)
				: Color(1.0f, 0.8f, 0.6f);
			colors[i] = Color(tint.r, tint.g, tint.b, brightness);
		}

		auto mesh = cs::New<Mesh>("Starfield"); // new Mesh { name = "Starfield" }
		mesh->SetVertices(vertices);
		mesh->SetColors(colors);
		mesh->SetIndices(indices, MeshTopology::Points, 0, false);
		mesh->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 1e9f));

		auto go = NewGameObject("Starfield");
		go->transform->SetParent(parent, false);
		AddComponent<MeshFilter>(go)->sharedMesh = mesh;
		auto mr = AddComponent<MeshRenderer>(go);
		cs::Ref<Material> material = shared ? sharedMaterial() : cs::Ref<Material>();
		if (!Alive(material))
		{
			material = Materials::Create(Materials::Stars(), "Stars");
			material->SetFloat("_Brightness", 1.1f);
			if (shared) sharedMaterial() = Materials::MarkShared(material);
		}
		mr->SetSharedMaterial(material);
		mr->shadowCastingMode = ShadowCastingMode::Off;
		return go;
	}

	Vector3 Starfield::RandomDirection(cs::Random& rng)
	{
		double z = rng.NextDouble() * 2 - 1;
		double a = rng.NextDouble() * cs::Math::PI * 2;
		double s = cs::Math::Sqrt(1 - z * z);
		return Vector3(static_cast<float>(s * cs::Math::Cos(a)), static_cast<float>(z), static_cast<float>(s * cs::Math::Sin(a)));
	}
}
