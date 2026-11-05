// US-T131: draws a place's ship graveyards (see GraveyardView.h).
#include "Views/GraveyardView.h"

#include "UnitySpaceApp.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/ShipMeshFactory.h"
#include "Rendering/ShipModels.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "ScaleModel.h"
#include "Systems/Graveyards.h"

#include <cmath>

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using Rendering::MeshFactory;
	using Rendering::ShipMeshFactory;
	using Rendering::ShipModels;

	namespace
	{
		cs::Ref<Mesh>& QuadMesh()
		{
			static cs::Ref<Mesh> mesh;
			if (!Alive(mesh)) mesh = MeshFactory::Quad();
			return mesh;
		}

		uint32_t Hash(uint32_t x)
		{
			x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
			return x;
		}
		float Unit(uint32_t& s) { s = Hash(s + 0x9E3779B9u); return static_cast<float>(s >> 8) * (1.0f / 16777216.0f); }

		/// <summary>The span a true length covers where the root's mapping puts a point (geometric mean of its radial and sideways stretch).</summary>
		double SceneSpan(const ScaleModel& scale, const cs::Ref<BodyDefinition>& root, double distanceKm, double km)
		{
			double d = cs::Math::Max(distanceKm, km);
			double radial = (scale.LocalDistance(root, d + km) - scale.LocalDistance(root, cs::Math::Max(0.0, d - km))) * 0.5;
			double sideways = scale.LocalDistance(root, d) / d * km;
			return std::sqrt(cs::Math::Max(1e-12, radial) * cs::Math::Max(1e-12, sideways));
		}

		void AddFace(cs::List<Vector3>& v, cs::List<Vector3>& n, cs::List<Color>& c, cs::List<int>& t, Vector3 a, Vector3 b, Vector3 d, Vector3 centre, Color col)
		{
			auto nn = Vector3::Cross(b - a, d - a);
			if (Vector3::Dot(nn, (a + b + d) / 3.0f - centre) < 0) { std::swap(b, d); nn = -nn; }
			int i = v.Count();
			for (auto p : { a, b, d })
			{
				v.Add(p);
				n.Add(nn.normalized());
				c.Add(col);
			}
			t.Add(i);
			t.Add(i + 1);
			t.Add(i + 2);
		}
	}

	GraveyardView::GraveyardView(UnitySpaceApp* app_, const cs::Ref<Transform>& parent)
		: app(app_)
	{
		const auto& yards = Graveyards::Of((*app->Catalog())[0]);
		for (int gi = 0; gi < yards.Count(); gi++)
		{
			const auto& g = yards[gi];
			uint32_t s = g.Seed;
			// The haze: dust catching the light, warm or cold, thickest at the heart.
			int hazes = g.Hulks.Count() > 0 ? 28 : 8;
			for (int i = 0; i < hazes; i++)
			{
				auto go = NewGameObject(cs::Cat(g.Name, " haze"));
				go->transform->SetParent(parent, false);
				AddComponent<MeshFilter>(go)->sharedMesh = QuadMesh();
				auto r = AddComponent<MeshRenderer>(go);
				auto m = Materials::Create(Materials::Glow(), cs::Cat(g.Name, " dust"));
				Color c = g.Warm ? Color(1.0f, 0.42f + 0.25f * Unit(s), 0.12f + 0.12f * Unit(s)) : Color(0.55f + 0.2f * Unit(s), 0.75f + 0.15f * Unit(s), 1.0f);
				m->SetColor("_Color", c);
				m->SetFloat("_Intensity", 0.12f + 0.14f * Unit(s));
				m->SetFloat("_Falloff", 1.5f);
				r->SetSharedMaterial(m);
				r->shadowCastingMode = ShadowCastingMode::Off;
				r->receiveShadows = false;
				Vector3 p(Unit(s) * 2 - 1, Unit(s) * 2 - 1, (Unit(s) * 2 - 1) * 0.5f);
				auto off = g.CentreKm + DVec3(p.x, p.y, p.z) * (g.RadiusKm * 0.7);
				puffs.Add(Puff { off, g.RadiusKm * (0.3 + 0.35 * Unit(s)), go->transform });
			}
			// The wreckage.
			{
				auto go = NewGameObject(cs::Cat(g.Name, " wreckage"));
				go->transform->SetParent(parent, false);
				Field f;
				f.Index = gi;
				f.CentreKm = g.CentreKm;
				f.Shape = Materials::Own(cs::New<Mesh>(cs::Cat(g.Name, " wreckage")));
				f.Shape->MarkDynamic();
				AddComponent<MeshFilter>(go)->sharedMesh = f.Shape;
				auto r = AddComponent<MeshRenderer>(go);
				auto m = Materials::Create(Materials::Ship(), cs::Cat(g.Name, " scrap"));
				m->SetFloat("_UseVertexColor", 1);
				m->SetColor("_EngineColor", g.Warm ? Color(1.0f, 0.5f, 0.18f) : Color(0.75f, 0.9f, 1.0f));
				m->SetFloat("_EngineGlow", 0.9f);
				r->SetSharedMaterial(m);
				r->shadowCastingMode = ShadowCastingMode::Off;
				f.Root = go->transform;
				fields.Add(f);
			}
			// The derelict stations: the station model, dark, a module broken off beside it.
			for (const auto& st : g.Stations)
			{
				auto m = Materials::Create(Materials::Ship(), cs::Cat(st.Name, " hull"));
				m->SetFloat("_UseVertexColor", 1);
				m->SetFloat("_EngineGlow", 0.0f);
				m->SetColor("_Color", g.Warm ? Color(0.42f, 0.34f, 0.28f) : Color(0.3f, 0.33f, 0.37f));
				ShipModels::Plate(m, 10.0f);
				auto mesh = ShipMeshFactory::GetStationBody(st.Name);
				auto Part = [&](const cs::String& name)
				{
					auto go = NewGameObject(name);
					go->transform->SetParent(parent, false);
					AddComponent<MeshFilter>(go)->sharedMesh = mesh;
					auto r = AddComponent<MeshRenderer>(go);
					r->SetSharedMaterial(m);
					r->shadowCastingMode = ShadowCastingMode::Off;
					return go->transform;
				};
				uint32_t ss = st.Seed;
				stations.Add(Station { st.OffsetKm, st.SizeKm, 0.5f + Unit(ss), Part(st.Name), Part(cs::Cat(st.Name, " module")) });
			}
		}
	}

	void GraveyardView::LayField(Field& field)
	{
		const auto& g = Graveyards::Of((*app->Catalog())[0])[field.Index];
		auto root = (*app->Catalog())[0];
		const auto& scale = *app->Scale();
		auto centre = scale.LocalOffset(root, g.CentreKm);
		cs::List<Vector3> v, n;
		cs::List<Color> c;
		cs::List<int> t;
		uint32_t s = g.Seed ^ 0x5bd1e995u;
		Color metal = g.Warm ? Color(0.3f, 0.26f, 0.23f, 1) : Color(0.55f, 0.6f, 0.66f, 1);
		Color glow = g.Warm ? Color(1.0f, 0.6f, 0.3f, 0) : Color(0.85f, 0.95f, 1.0f, 0);
		for (int i = 0; i < g.ChunkOffsetsKm.Count(); i++)
		{
			auto at = scale.LocalOffset(root, g.ChunkOffsetsKm[i]) - centre;
			Vector3 p(static_cast<float>(at.X), static_cast<float>(at.Y), static_cast<float>(at.Z));
			// Sized as ship debris is (ScaleModel.ShipLength), like the hulks they lie among.
			float size = static_cast<float>(scale.ShipLength(g.ChunkRadiiKm[i] * 2)) * 0.5f;
			float shade = 0.6f + 0.5f * Unit(s);
			Color col(metal.r * shade, metal.g * shade, metal.b * shade, 1);
			bool box = Unit(s) < (g.Warm ? 0.3f : 0.55f);
			// A random orientation: an orthonormal frame from two random directions.
			Vector3 ax(Unit(s) - 0.5f, Unit(s) - 0.5f, Unit(s) - 0.5f);
			if (ax.sqrMagnitude() < 1e-4f) ax = Vector3::right();
			ax = ax.normalized();
			Vector3 ay = Vector3::Cross(ax, Mathf::Abs(ax.y) < 0.9f ? Vector3::up() : Vector3::right()).normalized();
			Vector3 az = Vector3::Cross(ax, ay);
			if (box)
			{
				// A container: a long box, its sides in shades, one in ten a lit or frosted face.
				float lx = size * (1.2f + Unit(s)), ly = size * 0.5f, lz = size * 0.5f;
				Vector3 corner[8];
				for (int k = 0; k < 8; k++)
					corner[k] = p + ax * (k & 1 ? lx : -lx) + ay * (k & 2 ? ly : -ly) + az * (k & 4 ? lz : -lz);
				static const int quads[6][4] = { { 0, 1, 3, 2 }, { 4, 6, 7, 5 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 5, 7, 3 } };
				for (const auto& q : quads)
				{
					Color fc = Unit(s) < 0.1f ? glow : Color(col.r * (0.8f + 0.3f * Unit(s)), col.g * 0.95f, col.b * 0.95f, 1);
					AddFace(v, n, c, t, corner[q[0]], corner[q[1]], corner[q[2]], p, fc);
					AddFace(v, n, c, t, corner[q[0]], corner[q[2]], corner[q[3]], p, fc);
				}
			}
			else
			{
				// A torn plate or chunk: a jagged octahedron.
				Vector3 corner[6];
				for (int k = 0; k < 6; k++)
				{
					Vector3 axis = k < 2 ? ax : k < 4 ? ay : az;
					float sign = k % 2 == 0 ? 1.0f : -1.0f;
					float stretch = k < 2 ? 1.6f : k < 4 ? 1.0f : 0.35f;
					corner[k] = p + axis * (sign * size * stretch * (0.6f + 0.6f * Unit(s)));
				}
				static const int faces[8][3] = { { 0, 2, 4 }, { 2, 1, 4 }, { 1, 3, 4 }, { 3, 0, 4 }, { 2, 0, 5 }, { 1, 2, 5 }, { 3, 1, 5 }, { 0, 3, 5 } };
				for (const auto& f : faces)
					AddFace(v, n, c, t, corner[f[0]], corner[f[1]], corner[f[2]], p, Unit(s) < 0.06f ? glow : col);
			}
		}
		if (v.Count() > 65000) field.Shape->indexFormat = IndexFormat::UInt32;
		field.Shape->Clear();
		field.Shape->SetVertices(v);
		field.Shape->SetNormals(n);
		field.Shape->SetColors(c);
		field.Shape->SetTriangles(t, 0);
		field.Shape->RecalculateBounds();
	}

	void GraveyardView::Update()
	{
		if (puffs.Count() == 0 && fields.Count() == 0 && stations.Count() == 0) return;
		auto root = (*app->Catalog())[0];
		const auto& scale = *app->Scale();
		auto rootScene = app->BodyScene(0);
		float now = Time::unscaledTime;
		for (auto& p : puffs)
		{
			auto relative = app->Relative(rootScene + scale.LocalOffset(root, p.OffsetKm));
			float distance = relative.magnitude();
			Vector3 toCamera = distance > 1e-6f ? -relative / distance : Vector3::back();
			p.Quad->localPosition = relative;
			p.Quad->localRotation = Quaternion::LookRotation(-toCamera, Vector3::up());
			p.Quad->localScale = Vector3::one() * static_cast<float>(SceneSpan(scale, root, p.OffsetKm.Length(), p.RadiusKm) * 2.4);
		}
		bool relayout = laidOutBlend != scale.Blend();
		for (auto& f : fields)
		{
			if (relayout || f.Shape->vertexCount() == 0) LayField(f);
			f.Root->localPosition = app->Relative(rootScene + scale.LocalOffset(root, f.CentreKm));
			// The whole field turns very slowly, so the wreckage seems to tumble past the hulks.
			f.Root->localRotation = Quaternion::AngleAxis(now * 0.15f, Vector3::up());
		}
		for (auto& st : stations)
		{
			auto relative = app->Relative(rootScene + scale.LocalOffset(root, st.OffsetKm));
			float length = static_cast<float>(scale.ShipLength(st.SizeKm));
			st.Body->localPosition = relative;
			st.Body->localRotation = Quaternion::Euler(now * 0.6f * st.Spin, now * 0.25f, now * 0.4f * st.Spin);
			st.Body->localScale = Vector3::one() * length;
			// A module torn off, drifting a little way from it and tumbling on its own.
			st.Module->localPosition = relative + Vector3(0.9f, 0.35f, -0.6f) * length;
			st.Module->localRotation = Quaternion::Euler(now * 2.1f, now * 1.3f * st.Spin, now * 0.8f);
			st.Module->localScale = Vector3::one() * (length * 0.35f);
		}
		laidOutBlend = scale.Blend();
	}
}
