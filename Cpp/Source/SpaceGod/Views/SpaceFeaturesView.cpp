// US-T130: draws a place's nebulae and ice fields (see SpaceFeaturesView.h).
#include "Views/SpaceFeaturesView.h"

#include "UnitySpaceApp.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "ScaleModel.h"
#include "Systems/SpaceFeatures.h"

#include <cmath>

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using Rendering::MeshFactory;

	namespace
	{
		cs::Ref<Mesh>& QuadMesh()
		{
			static cs::Ref<Mesh> mesh;
			if (!Alive(mesh)) mesh = MeshFactory::Quad();
			return mesh;
		}

		/// <summary>How many scene units a true length <paramref name="km"/> spans where the root's mapping puts a point
		/// <paramref name="distanceKm"/> out: the geometric mean of the mapping's radial and its sideways stretch there.</summary>
		double SceneSpan(const ScaleModel& scale, const cs::Ref<BodyDefinition>& root, double distanceKm, double km)
		{
			double d = cs::Math::Max(distanceKm, km);
			double radial = (scale.LocalDistance(root, d + km) - scale.LocalDistance(root, cs::Math::Max(0.0, d - km))) * 0.5;
			double sideways = scale.LocalDistance(root, d) / d * km;
			return std::sqrt(cs::Math::Max(1e-12, radial) * cs::Math::Max(1e-12, sideways));
		}

		uint32_t Hash(uint32_t x)
		{
			x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
			return x;
		}
		float Unit(uint32_t& s) { s = Hash(s + 0x9E3779B9u); return static_cast<float>(s >> 8) * (1.0f / 16777216.0f); }
	}

	SpaceFeaturesView::SpaceFeaturesView(UnitySpaceApp* app_, const cs::Ref<Transform>& parent)
		: app(app_)
	{
		const auto& features = SpaceFeatures::Of((*app->Catalog())[0]->Name);
		for (const auto& n : features.Nebulae)
			for (const auto& p : n.Puffs)
			{
				auto go = NewGameObject(cs::Cat(n.Name, " puff"));
				go->transform->SetParent(parent, false);
				AddComponent<MeshFilter>(go)->sharedMesh = QuadMesh();
				auto r = AddComponent<MeshRenderer>(go);
				auto m = Materials::Create(Materials::Glow(), cs::Cat(n.Name, " gas"));
				m->SetColor("_Color", Color(p.Color.R, p.Color.G, p.Color.B));
				m->SetFloat("_Intensity", p.Alpha);
				m->SetFloat("_Falloff", 1.4f);
				r->SetSharedMaterial(m);
				r->shadowCastingMode = ShadowCastingMode::Off;
				r->receiveShadows = false;
				puffs.Add(Puff { p.OffsetKm, p.RadiusKm, go->transform });
			}
		for (int i = 0; i < features.IceFields.Count(); i++)
		{
			const auto& f = features.IceFields[i];
			auto go = NewGameObject(f.Name);
			go->transform->SetParent(parent, false);
			Field field;
			field.CentreKm = f.CentreKm;
			field.Index = i;
			field.Shape = Materials::Own(cs::New<Mesh>(f.Name));
			field.Shape->MarkDynamic();
			AddComponent<MeshFilter>(go)->sharedMesh = field.Shape;
			auto r = AddComponent<MeshRenderer>(go);
			auto m = Materials::Create(Materials::Ship(), cs::Cat(f.Name, " ice"));
			m->SetFloat("_UseVertexColor", 1);
			m->SetColor("_EngineColor", Color(0.55f, 0.85f, 1.0f));
			m->SetFloat("_EngineGlow", 1.2f);
			r->SetSharedMaterial(m);
			r->shadowCastingMode = ShadowCastingMode::Off;
			field.Root = go->transform;
			fields.Add(field);
		}
	}

	void SpaceFeaturesView::LayField(Field& field)
	{
		const auto& f = SpaceFeatures::Of((*app->Catalog())[0]->Name).IceFields[field.Index];
		auto root = (*app->Catalog())[0];
		const auto& scale = *app->Scale();
		auto centre = scale.LocalOffset(root, f.CentreKm);
		cs::List<Vector3> vertices, normals;
		cs::List<Color> colors;
		cs::List<int> triangles;
		static const int faces[8][3] = { { 0, 2, 4 }, { 2, 1, 4 }, { 1, 3, 4 }, { 3, 0, 4 }, { 2, 0, 5 }, { 1, 2, 5 }, { 3, 1, 5 }, { 0, 3, 5 } };
		for (const auto& c : f.Chunks)
		{
			auto at = scale.LocalOffset(root, c.OffsetKm) - centre;
			Vector3 p(static_cast<float>(at.X), static_cast<float>(at.Y), static_cast<float>(at.Z));
			float size = static_cast<float>(scale.DisplayRadius(c.RadiusKm));
			uint32_t s = c.Seed;
			Vector3 corner[6];
			for (int k = 0; k < 6; k++)
			{
				Vector3 axis = k < 2 ? Vector3::right() : k < 4 ? Vector3::up() : Vector3::forward();
				float sign = k % 2 == 0 ? 1.0f : -1.0f;
				corner[k] = p + (axis * sign * (0.7f + 0.6f * Unit(s)) + Vector3(Unit(s) - 0.5f, Unit(s) - 0.5f, Unit(s) - 0.5f) * 0.5f) * size;
			}
			float shade = 0.75f + 0.3f * Unit(s);
			for (int k = 0; k < 8; k++)
			{
				auto a = corner[faces[k][0]], b = corner[faces[k][1]], d = corner[faces[k][2]];
				auto n = Vector3::Cross(b - a, d - a);
				if (Vector3::Dot(n, (a + b + d) / 3.0f - p) < 0) { std::swap(b, d); n = -n; }
				// One face in five glints: emissive (vertex alpha 0) in the ice's own blue.
				bool glint = Unit(s) < 0.2f;
				Color col = glint ? Color(0.8f, 0.95f, 1.0f, 0) : Color(0.72f * shade, 0.84f * shade, 0.95f * shade, 1);
				int i = vertices.Count();
				for (auto v : { a, b, d })
				{
					vertices.Add(v);
					normals.Add(n.normalized());
					colors.Add(col);
				}
				triangles.Add(i);
				triangles.Add(i + 1);
				triangles.Add(i + 2);
			}
		}
		field.Shape->Clear();
		field.Shape->SetVertices(vertices);
		field.Shape->SetNormals(normals);
		field.Shape->SetColors(colors);
		field.Shape->SetTriangles(triangles, 0);
		field.Shape->RecalculateBounds();
	}

	void SpaceFeaturesView::Update()
	{
		if (puffs.Count() == 0 && fields.Count() == 0) return;
		auto root = (*app->Catalog())[0];
		const auto& scale = *app->Scale();
		auto rootScene = app->BodyScene(0);
		for (auto& p : puffs)
		{
			auto offset = scale.LocalOffset(root, p.OffsetKm);
			auto relative = app->Relative(rootScene + offset);
			float distance = relative.magnitude();
			Vector3 toCamera = distance > 1e-6f ? -relative / distance : Vector3::back();
			p.Quad->localPosition = relative;
			p.Quad->localRotation = Quaternion::LookRotation(-toCamera, Vector3::up());
			// The quad's glow fades to nothing at its edge, so it is drawn a little past the puff's own radius.
			float size = static_cast<float>(SceneSpan(scale, root, p.OffsetKm.Length(), p.RadiusKm) * 2.4);
			p.Quad->localScale = Vector3::one() * size;
		}
		bool relayout = laidOutBlend != scale.Blend();
		for (auto& f : fields)
		{
			if (relayout || f.Shape->vertexCount() == 0) LayField(f);
			f.Root->localPosition = app->Relative(rootScene + scale.LocalOffset(root, f.CentreKm));
			f.Root->localRotation = Quaternion::identity();
		}
		laidOutBlend = scale.Blend();
	}
}
