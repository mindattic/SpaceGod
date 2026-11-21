// US-T442: see InteriorWire.h.
#include "Views/InteriorWire.h"
#include "Rendering/Materials.h"
#include "Rendering/Pipeline/RenderBackend.h"

namespace SpaceGod::Views
{
	using Rendering::Materials;

	void InteriorWire::Lines::Segment(Vector3 a, Vector3 b, Color c)
	{
		Indices.Add(Vertices.Count());
		Vertices.Add(a);
		Colors.Add(c);
		Indices.Add(Vertices.Count());
		Vertices.Add(b);
		Colors.Add(c);
	}

	void InteriorWire::Lines::Circle(Vector3 centre, Vector3 u, Vector3 v, float radius, int steps, Color c)
	{
		if (radius <= 0 || steps < 3) return;
		auto at = [&](int i)
		{
			float a = 2.0f * Mathf::PI * static_cast<float>(i) / static_cast<float>(steps);
			return centre + (u * Mathf::Cos(a) + v * Mathf::Sin(a)) * radius;
		};
		for (int i = 0; i < steps; i++) Segment(at(i), at(i + 1), c);
	}

	void InteriorWire::Lines::Sphere(float radius, int meridians, int parallels, int steps, Color c)
	{
		// Parallels between the poles (none at a pole itself), and meridians as full great circles through both poles.
		for (int p = 1; p <= parallels; p++)
		{
			float lat = Mathf::PI * (static_cast<float>(p) / static_cast<float>(parallels + 1) - 0.5f);
			Circle(Vector3(0, radius * Mathf::Sin(lat), 0), Vector3::right(), Vector3::forward(), radius * Mathf::Cos(lat), steps, c);
		}
		for (int m = 0; m < meridians; m++)
		{
			float lon = Mathf::PI * static_cast<float>(m) / static_cast<float>(meridians);
			Circle(Vector3::zero(), Vector3(Mathf::Cos(lon), 0, Mathf::Sin(lon)), Vector3::up(), radius, steps, c);
		}
	}

	void InteriorWire::Lines::Box(Vector3 centre, Vector3 half, Color c)
	{
		auto corner = [&](int i)
		{
			return centre + Vector3((i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z);
		};
		for (int i = 0; i < 8; i++)
			for (int bit = 1; bit < 8; bit <<= 1)
				if ((i & bit) == 0) Segment(corner(i), corner(i | bit), c);
	}

	cs::Ref<Mesh> InteriorWire::Build(const cs::String& name, const Lines& lines)
	{
		auto mesh = Materials::Own(cs::New<Mesh>(name));
		mesh->SetVertices(lines.Vertices);
		mesh->SetColors(lines.Colors);
		mesh->SetIndices(lines.Indices, MeshTopology::Lines, 0, true);
		return mesh;
	}

	cs::Ref<MeshRenderer> InteriorWire::Attach(const cs::String& name, const cs::Ref<Transform>& parent, const cs::Ref<Mesh>& mesh)
	{
		auto go = NewGameObject(name);
		go->transform->SetParent(parent, false);
		AddComponent<MeshFilter>(go)->sharedMesh = mesh;
		auto r = AddComponent<MeshRenderer>(go);
		r->SetSharedMaterial(Materials::Create(Materials::Line(), name));
		r->shadowCastingMode = ShadowCastingMode::Off;
		r->receiveShadows = false;
		r->enabled = false;
		return r;
	}

	cs::Ref<Material> InteriorWire::Skin(const cs::String& name)
	{
		return Materials::Create(Rendering::RenderBackend::Active()->LoadShader("GhostTwoSided"), name);
	}

	void InteriorWire::SetSkin(const cs::Ref<Material>& skin, Color tint, float opacity)
	{
		if (!Alive(skin)) return;
		// As the occluder ghost (US-T219): a lit-white exposure makes a bright colour read opaque, so a dark tone of the tint.
		float k = 0.16f;
		skin->SetVector("_GhostColor", Vector4(0.03f + tint.r * k, 0.03f + tint.g * k, 0.04f + tint.b * k, Mathf::Clamp01(opacity)));
	}

	float InteriorWire::Fade(double depth, double ramp)
	{
		if (ramp <= 0) return depth > 0 ? 1.0f : 0.0f;
		double t = depth / ramp;
		if (t <= 0) return 0.0f;
		if (t >= 1) return 1.0f;
		return static_cast<float>(t * t * (3.0 - 2.0 * t));
	}
}
