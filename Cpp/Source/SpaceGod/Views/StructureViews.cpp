// Port of Unity.SpaceGod Runtime/Views/StructureViews.cs (US-T30).
#include "Views/StructureViews.h"

#include "UnitySpaceApp.h"
#include "Rendering/ShipMeshFactory.h"
#include "Rendering/ShipModels.h"
#include "Rendering/StarSparrowKit.h"
#include "Rendering/UscKit.h"

#include "BodyCatalog.h"
#include "BodyOrientation.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "Infrastructure/Infrastructure.h"
#include "Lagrange.h"
#include "RenderLod.h"
#include "ScaleModel.h"
#include "SimClock.h"
#include "Ships.h"
#include "World.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::ShipMeshFactory;
	using Rendering::ShipModels;

	namespace
	{
		/// Where a hull part's middle is along the hull on the slip (its own frame: -0.5 the stern, +0.5 the bow).
		float PartZ(const cs::Ref<Transform>& hull, const cs::Ref<Renderer>& r)
		{
			auto m = WorldMatrix::Of(*hull);
			Vector3 f = m.MultiplyVector(Vector3::forward());
			return Vector3::Dot(r->bounds().center - m.t, f) / Mathf::Max(f.sqrMagnitude(), 1e-20f);
		}
	}

	StructureViews::StructureViews(UnitySpaceApp* app_, const cs::Ref<Transform>& parent)
		: app(app_)
	{
		auto infra = app->World()->Infrastructure;
		if (infra == nullptr) return;
		for (const auto& s : infra->Structures) views.Add(Create(s, parent));
		for (const auto& r : infra->Rings)
		{
			auto go = NewGameObject(r->Name);
			go->transform->SetParent(parent, false);
			auto filter = AddComponent<MeshFilter>(go);
			filter->sharedMesh = ShipMeshFactory::GetDockingRing(r->Owner());
			auto mr = AddComponent<MeshRenderer>(go);
			mr->SetSharedMaterial(ShipModels::ProceduralMaterial());
			mr->shadowCastingMode = ShadowCastingMode::Off;
			auto rv = cs::New<RingView>();
			rv->Ring = r;
			rv->Root = go;
			rv->Renderer = mr;
			rv->Filter = filter;
			rv->Owner = r->Owner();
			rings.Add(rv);
		}
	}

	cs::Ref<StructureViews::View> StructureViews::Create(const cs::Ref<OrbitalStructure>& s, const cs::Ref<Transform>& parent)
	{
		auto root = NewGameObject(s->Name);
		root->transform->SetParent(parent, false);
		auto v = cs::New<View>();
		v->S = s;
		v->Root = root;
		v->Spin = UnityRandom::value() * 360;
		v->Owner = s->Owner();
		auto model = Part(root->transform, ModelMesh(s), "Model");
		v->Model = GetComponent<MeshFilter>(model->gameObject());
		cs::List<cs::Ref<Renderer>> list{ model };
		if (s->Kind == StructureKind::Shipyard)
		{
			// The cradle holds a hull about 80% of its length, nose along +Z.
			v->Slip = NewGameObject("Slip")->transform;
			v->Slip->SetParent(root->transform, false);
			v->Slip->localScale = Vector3::one() * 0.8f;
			v->Ribs = cs::NewArray<cs::Ref<Transform>>(12);
			for (int i = 0; i < v->Ribs.Count(); i++)
			{
				auto rib = Part(v->Slip, ShipMeshFactory::GetSkeletonRib(), "Rib");
				rib->transform()->localPosition = Vector3(0, 0, -0.46f + i * 0.084f);
				rib->transform()->localScale = Vector3(1.6f, 1.6f, 1.0f);
				v->Ribs[i] = rib->transform();
				list.Add(rib);
			}
		}
		v->Renderers = list.ToArray();
		return v;
	}

	cs::Ref<Mesh> StructureViews::ModelMesh(const cs::Ref<OrbitalStructure>& s)
	{
		switch (s->Kind)
		{
			case StructureKind::Shipyard: return ShipMeshFactory::GetShipyard(s->Owner());
			case StructureKind::Factory: return ShipMeshFactory::GetFactory(s->Owner(), s->Industry);
			case StructureKind::Habitat: return ShipMeshFactory::GetHabitat(s->Owner());
			case StructureKind::CrewedStation: return ShipMeshFactory::GetCrewedStation(s->Name == "Tiangong");
			// A depot's tank farm and a gateway's crew ring borrow the factory and habitat frames.
			case StructureKind::Depot: return ShipMeshFactory::GetFactory(s->Owner(), IndustryKind::Farm);
			case StructureKind::Gateway: return ShipMeshFactory::GetHabitat(s->Owner());
			default: return ShipMeshFactory::GetSpacecraft(s->Name);
		}
	}

	cs::Ref<Renderer> StructureViews::Part(const cs::Ref<Transform>& parent, const cs::Ref<Mesh>& mesh, const cs::String& name)
	{
		auto go = NewGameObject(name);
		go->transform->SetParent(parent, false);
		AddComponent<MeshFilter>(go)->sharedMesh = mesh;
		auto mr = AddComponent<MeshRenderer>(go);
		mr->SetSharedMaterial(ShipModels::ProceduralMaterial());
		mr->shadowCastingMode = ShadowCastingMode::Off;
		return mr;
	}

	void StructureViews::Layout()
	{
		auto eph = app->Ephemeris();
		auto scale = app->Scale();
		const auto& scene = app->BodyScenes();
		double jd = app->Clock->Jd;
		Scenes.Clear();
		RingScenes.Clear();
		Sizes.Clear();
		for (const auto& v : views)
		{
			auto s = v->S;
			// A Lagrange point is mapped near the body it sits by: L1 and L2 near the smaller body,
			// L3 to L5 on the larger one's mapping, so Jupiter's L4 stays on Jupiter's drawn orbit.
			Scenes[s] = s->AtLagrange() ? scale->MapNear(eph, scene, Lagrange::MappedNear(app->Catalog(), s->Body, s->Lagrange), s->Helio(eph))
				: scene[s->Body] + scale->LocalOffset((*app->Catalog())[s->Body], s->Orbit->OffsetAt(jd));
			Sizes[s] = scale->ShipLength(s->LengthKm);
		}
		for (const auto& rv : rings)
		{
			auto r = rv->Ring;
			RingScenes[r] = scene[r->Body] + scale->LocalOffset((*app->Catalog())[r->Body], r->RingPoint()->OffsetAt(jd));
			Sizes[r] = scale->ShipLength(RingDiameterKm);
		}
	}

	void StructureViews::Update(bool visible)
	{
		const auto& scene = app->BodyScenes();
		double jd = app->Clock->Jd;
		double ppu = app->PixelsPerUnitAtUnitDistance();

		for (const auto& v : views)
		{
			auto s = v->S;
			auto p = Scenes.At(s);
			double size = Sizes.At(s);
			auto rel = app->Relative(p);
			// Too small to see, not drawn (U.SPC-T415); the HUD names it once it is big enough.
			bool show = visible && RenderLod::DrawsModel(RenderLod::Tier(RenderLod::Structure(), RenderLod::ApparentPx(size, rel.magnitude(), ppu)));
			if (v->Root->activeSelf != show) v->Root->SetActive(show);
			if (!show) continue;
			// Taken in a siege: repainted in the new owner's livery.
			if (v->Owner != s->Owner())
			{
				v->Owner = s->Owner();
				v->Model->sharedMesh = ModelMesh(s);
			}

			auto t = v->Root->transform;
			t->localPosition = rel;
			t->localScale = Vector3::one() * static_cast<float>(size);
			// Long axis along the orbit; habitats face their ring to the planet's pole and spin.
			if (s->AtLagrange())
			{
				auto toSun = app->Relative(scene[0]) - rel;
				t->localRotation = Quaternion::LookRotation(Vector3::Cross(toSun, Vector3::up()).normalized() + Vector3::up() * 0.01f, -toSun);
			}
			else
			{
				auto tangent = s->Orbit->TangentAt(jd).SwapYZ();
				auto up = s->Orbit->OffsetAt(jd).SwapYZ().Normalized();
				auto f = Vector3(static_cast<float>(tangent.X), static_cast<float>(tangent.Y), static_cast<float>(tangent.Z));
				auto u = Vector3(static_cast<float>(up.X), static_cast<float>(up.Y), static_cast<float>(up.Z));
				if (s->Kind == StructureKind::Habitat)
				{
					v->Spin = cs::Mod(v->Spin + Time::deltaTime * 12.0f, 360.0f);
					t->localRotation = Quaternion::LookRotation(u, f) * Quaternion::Euler(0, 0, v->Spin);
				}
				else t->localRotation = Quaternion::LookRotation(f, u);
			}

			block.SetFloat(EngineGlowId, 1.4f);
			block.SetColor(EngineColorId, Color(1.0f, 0.85f, 0.6f));
			block.SetColor(ColorId, Color::white());
			block.SetFloat(HighlightId, app->Selected.Structure == s || app->Hovered.Structure == s ? 1.0f : 0.0f);
			for (const auto& r : v->Renderers) r->SetPropertyBlock(block);
			if (v->Slip != nullptr) UpdateSlip(v);
		}

		for (const auto& rv : rings)
		{
			auto r = rv->Ring;
			auto b = (*app->Catalog())[r->Body];
			auto offset = r->RingPoint()->OffsetAt(jd);
			auto p = RingScenes.At(r);
			double size = Sizes.At(r);
			auto rel = app->Relative(p);
			bool show = visible && RenderLod::DrawsModel(RenderLod::Tier(RenderLod::DockingRing(), RenderLod::ApparentPx(size, rel.magnitude(), ppu)));
			rv->Renderer->enabled = show;
			if (!show) continue;
			if (rv->Owner != r->Owner())
			{
				rv->Owner = r->Owner();
				rv->Filter->sharedMesh = ShipMeshFactory::GetDockingRing(r->Owner());
			}
			auto t = rv->Root->transform;
			t->localPosition = rel;
			t->localScale = Vector3::one() * static_cast<float>(size);
			// The ring's axis points down at its city; ships pass through it on their way in.
			auto down = (-offset).SwapYZ().Normalized();
			auto d = Vector3(static_cast<float>(down.X), static_cast<float>(down.Y), static_cast<float>(down.Z));
			auto north = BodyOrientation::MapNorth(b).SwapYZ();
			t->localRotation = Quaternion::LookRotation(d, Vector3(static_cast<float>(north.X), static_cast<float>(north.Y), static_cast<float>(north.Z)));
			block.SetFloat(EngineGlowId, 1.6f + 0.6f * Mathf::Sin(Time::unscaledTime * 3.0f + r->Id));
			block.SetColor(EngineColorId, Color(0.55f, 0.9f, 1.0f));
			block.SetColor(ColorId, Color::white());
			block.SetFloat(HighlightId, app->Selected.Ring == r || app->Hovered.Ring == r ? 1.0f : 0.0f);
			rv->Renderer->SetPropertyBlock(block);
		}
	}

	void StructureViews::UpdateSlip(const cs::Ref<View>& v)
	{
		auto settlement = v->S->Settlement;
		cs::Nullable<ShipClass> cls = settlement != nullptr ? settlement->OnSlip : cs::Nullable<ShipClass>(); // settlement?.OnSlip
		double progress = v->S->SlipProgress();
		int ribs = !cls.has_value() ? 0 : Mathf::Clamp(Mathf::CeilToInt(static_cast<float>(progress / 0.6 * v->Ribs.Count())), 0, v->Ribs.Count());
		for (int i = 0; i < v->Ribs.Count(); i++) v->Ribs[i]->gameObject()->SetActive(i < ribs && progress < 0.97);
		bool plated = cls.has_value() && progress >= 0.6;
		if (plated && (!Alive(v->Hull) || v->HullClass != cls || v->HullOwner != settlement->Owner))
		{
			if (Alive(v->Hull)) Destroy(v->Hull);
			v->Hull = NewGameObject("Hull on the slip");
			v->Hull->transform->SetParent(v->Slip, false);
			// The whole hull, bow included (a warship's Stern section alone is only its after half): Ultimate Spaceships
			// Creator's when that pack is there (US-T142), else the Star Sparrow kit's (US-T110), its parts plated on stern
			// first as the work goes forward.
			cs::List<cs::Ref<Renderer>> parts;
			cs::Ref<Transform> stern, bow;
			v->HullParts.Clear();
			auto layout = Rendering::StarSparrowKit::LayoutOf(*cls, nullptr);
			if (Rendering::UscKit::Build(*cls, CivilCraft::None, 0, settlement->Owner, layout, v->Hull->transform, parts, stern, bow)
				|| Rendering::StarSparrowKit::Build(*cls, CivilCraft::None, 0, settlement->Owner, layout, v->Hull->transform, parts, stern, bow))
				v->HullParts = parts;
			else [[maybe_unused]] auto mr = Part(v->Hull->transform, ShipMeshFactory::GetSection(*cls, settlement->Owner, ShipMeshFactory::WholeHull), "Hull");
			v->HullClass = cls;
			v->HullOwner = settlement->Owner;
		}
		if (Alive(v->Hull))
		{
			v->Hull->SetActive(plated);
			if (plated)
			{
				// Plating creeps forward from the stern: the hull shows along the slip as far as it has got.
				float done = Mathf::Clamp01(static_cast<float>((progress - 0.6) / 0.37));
				if (v->HullParts.Count() > 0)
				{
					// Kit parts: each fitted once the plating has reached its middle.
					float front = -0.5f + done;
					for (const auto& r : v->HullParts)
						r->enabled = PartZ(v->Hull->transform, r) <= front + 0.02f;
				}
				else
				{
					v->Hull->transform->localScale = Vector3(1, 1, Mathf::Max(0.05f, done));
					v->Hull->transform->localPosition = Vector3(0, 0, -0.5f * (1 - done));
				}
				for (const auto& r : GetComponentsInChildren<Renderer>(v->Hull)) r->SetPropertyBlock(block);
			}
		}
	}
}
