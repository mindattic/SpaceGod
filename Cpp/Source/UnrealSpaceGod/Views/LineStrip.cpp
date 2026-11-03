// Port of Unity.SpaceGod Runtime/Views/LineStrip.cs (US-T30).
#include "Views/LineStrip.h"
#include "Rendering/Materials.h"

namespace SpaceGod::Views
{
	using Rendering::Materials;

	cs::Ref<Material>& LineStrip::sharedMaterial()
	{
		static cs::Ref<Material> material;
		return material;
	}

	LineStrip::LineStrip(const cs::String& name, const cs::Ref<Transform>& parent)
	{
		Root = NewGameObject(name);
		Root->transform->SetParent(parent, false);
		mesh = Materials::Own(cs::New<Mesh>(name));
		mesh->MarkDynamic();
		AddComponent<MeshFilter>(Root)->sharedMesh = mesh;
		renderer = AddComponent<MeshRenderer>(Root);
		if (!Alive(sharedMaterial())) sharedMaterial() = Materials::MarkShared(Materials::Create(Materials::Line(), "Line"));
		renderer->SetSharedMaterial(sharedMaterial());
		renderer->shadowCastingMode = ShadowCastingMode::Off;
		renderer->receiveShadows = false;
	}

	void LineStrip::Dispose()
	{
		Materials::DestroyMesh(mesh);
		Destroy(Root);
	}

	void LineStrip::SetPoints(const cs::List<Vector3>& points, int count, Color color, float alphaStart, float alphaEnd)
	{
		if (!allocated || vertices.Count() != count)
		{
			vertices = cs::List<Vector3>(std::vector<Vector3>(static_cast<size_t>(count)));
			colors = cs::List<Color>(std::vector<Color>(static_cast<size_t>(count)));
			indices = cs::List<int>(std::vector<int>(static_cast<size_t>(count)));
			for (int i = 0; i < count; i++) indices[i] = i;
			mesh->Clear();
			allocated = true;
		}
		for (int i = 0; i < count; i++)
		{
			vertices[i] = points[i];
			float t = count > 1 ? static_cast<float>(i) / (count - 1) : 0;
			colors[i] = Color(color.r, color.g, color.b, color.a * Mathf::Lerp(alphaStart, alphaEnd, t));
		}
		mesh->SetVertices(vertices);
		mesh->SetColors(colors);
		mesh->SetIndices(indices, MeshTopology::LineStrip, 0, false);
		mesh->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 1e9f));
	}

	void LineStrip::SetTint(Color tint)
	{
		block.SetColor(ColorId, tint);
		renderer->SetPropertyBlock(block);
	}
}
