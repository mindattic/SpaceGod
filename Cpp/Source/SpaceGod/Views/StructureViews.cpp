// Port of Unity.SpaceGod Runtime/Views/StructureViews.cs (US-T30).
#include "Views/StructureViews.h"

#include "Views/HullSurface.h"

#include "UnitySpaceApp.h"
#include "Rendering/ShipMeshFactory.h"
#include "Rendering/ShipModels.h"
#include "Rendering/SscKit.h"
#include "Rendering/StarSparrowKit.h"
#include "Rendering/UscKit.h"

#include "BodyCatalog.h"
#include "BodyOrientation.h"
#include "Combat/CombatSpecs.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "Infrastructure/Berths.h"
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
		: app(app_), viewParent(parent)
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

	void StructureViews::Add(const cs::Ref<OrbitalStructure>& s)
	{
		if (s == nullptr) return;
		for (const auto& v : views)
			if (v->S == s) return;
		views.Add(Create(s, viewParent));
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
		// US-T435: the model turns under a spinner of its own, so the root (and the docking spine on it) stays despun.
		v->Spinner = NewGameObject("Spin")->transform;
		v->Spinner->SetParent(root->transform, false);
		auto model = Part(v->Spinner, ModelMesh(s), "Model");
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
		AttachStation(v);
		LayoutBerths(v);
		return v;
	}

	void StructureViews::AttachStation(const cs::Ref<View>& v)
	{
		auto s = v->S;
		if (!Rendering::SscKit::Available()) return;
		cs::String model = Rendering::SscKit::ForStructure(s->Kind, s->Industry, s->Name);
		if (model.empty()) return;
		auto modelRenderer = GetComponent<MeshRenderer>(v->Model->gameObject());
		if (Alive(v->Gantry))
		{
			// US-T435: laid out again round the rebuilt model (LayoutBerths), so off the list first.
			v->Renderers.Remove(v->Gantry);
			Destroy(v->Gantry->gameObject());
			v->Gantry = nullptr;
		}
		if (v->BaseRenderers < 0) v->BaseRenderers = v->Renderers.Count();
		if (Alive(v->Station))
		{
			// Rebuilt in a new owner's livery: its renderers come off the list first.
			cs::List<cs::Ref<Renderer>> kept;
			for (int i = 0; i < v->BaseRenderers && i < v->Renderers.Count(); i++) kept.Add(v->Renderers[i]);
			v->Renderers = kept;
			Destroy(v->Station);
		}
		auto holder = NewGameObject("Station");
		holder->transform->SetParent(v->Spinner != nullptr ? v->Spinner : v->Root->transform, false);
		cs::List<cs::Ref<Renderer>> parts;
		// A habitat's (and gateway's) ring turns about the model's +Z, as the procedural ring does.
		bool ring = s->Kind == StructureKind::Habitat || s->Kind == StructureKind::Gateway;
		if (!Rendering::SscKit::Station(holder->transform, model, 1.0f, s->Owner(), parts, Rendering::SscKit::Condition::Working, ring))
		{
			Destroy(holder);
			return;
		}
		v->Station = holder;
		if (modelRenderer) modelRenderer->enabled = false;
		for (const auto& r : parts) v->Renderers.Add(r);
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
		// US-T100: stations the simulation builds as the system grows are drawn from the frame they are put up.
		if (auto infra = app->World() != nullptr ? app->World()->Infrastructure : nullptr)
			if (infra->Structures.Count() > views.Count())
				for (const auto& s : infra->Structures) Add(s);
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
				if (Alive(v->Station)) AttachStation(v);
				LayoutBerths(v);
			}

			auto t = v->Root->transform;
			t->localPosition = rel;
			t->localScale = Vector3::one() * static_cast<float>(size);
			// Long axis along the orbit; habitats face their ring to the planet's pole and spin (the wheel only: US-T435's
			// docking spine through its hub, and the ships berthed on it, are despun).
			DVec3 framed;
			double framedSize = 0;
			Quaternion rotation;
			if (Frame(v, framed, framedSize, rotation)) t->localRotation = rotation;
			if (s->Kind == StructureKind::Habitat && !s->AtLagrange() && v->Spinner != nullptr)
			{
				v->Spin = cs::Mod(v->Spin + Time::deltaTime * 12.0f, 360.0f);
				v->Spinner->localRotation = Quaternion::Euler(0, 0, v->Spin);
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
		// US-T435: the hull on the slip at its own length against the yard's (a yard is built for capital ships, so a frigate
		// sits small in its cradle), never past the cradle's 80%.
		if (cls.has_value() && v->S->LengthKm > 0)
		{
			const auto& spec = CombatSpec::Of(*cls);
			double share = spec != nullptr ? spec->LengthKm / v->S->LengthKm : 0.8;
			v->Slip->localScale = Vector3::one() * Mathf::Clamp(static_cast<float>(share), 0.05f, 0.8f);
		}
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

	// ---------------------------------------------------------------- berths (US-T435)

	cs::Ref<StructureViews::View> StructureViews::ViewOf(const cs::Ref<OrbitalStructure>& s) const
	{
		for (const auto& v : views)
			if (v->S == s) return v;
		return nullptr;
	}

	bool StructureViews::Frame(const cs::Ref<View>& v, DVec3& scene, double& size, Quaternion& rotation) const
	{
		if (v == nullptr) return false;
		auto s = v->S;
		auto eph = app->Ephemeris();
		auto scale = app->Scale();
		const auto& bodies = app->BodyScenes();
		double jd = app->Clock->Jd;
		scene = s->AtLagrange() ? scale->MapNear(eph, bodies, Lagrange::MappedNear(app->Catalog(), s->Body, s->Lagrange), s->Helio(eph))
			: bodies[s->Body] + scale->LocalOffset((*app->Catalog())[s->Body], s->Orbit->OffsetAt(jd));
		size = scale->ShipLength(s->LengthKm);
		if (s->AtLagrange())
		{
			auto d = bodies[0] - scene;
			auto toSun = Vector3(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z));
			rotation = Quaternion::LookRotation(Vector3::Cross(toSun, Vector3::up()).normalized() + Vector3::up() * 0.01f, -toSun);
			return true;
		}
		auto tangent = s->Orbit->TangentAt(jd).SwapYZ();
		auto up = s->Orbit->OffsetAt(jd).SwapYZ().Normalized();
		auto f = Vector3(static_cast<float>(tangent.X), static_cast<float>(tangent.Y), static_cast<float>(tangent.Z));
		auto u = Vector3(static_cast<float>(up.X), static_cast<float>(up.Y), static_cast<float>(up.Z));
		// A habitat faces its wheel to the planet's pole (the wheel's spin is its spinner's, not the root's).
		rotation = s->Kind == StructureKind::Habitat ? Quaternion::LookRotation(u, f) : Quaternion::LookRotation(f, u);
		return true;
	}

	namespace
	{
		Vector3 AbsOf(Vector3 a) { return Vector3(Mathf::Abs(a.x), Mathf::Abs(a.y), Mathf::Abs(a.z)); }

		/// A box spanning la along axis a, lb along b and lc along c (each a unit axis, either way).
		Vector3 Span(Vector3 a, float la, Vector3 b, float lb, Vector3 c, float lc) { return AbsOf(a) * la + AbsOf(b) * lb + AbsOf(c) * lc; }

		int Milli(float x) { return Mathf::RoundToInt(x * 1000.0f); }
	}

	void StructureViews::LayoutBerths(const cs::Ref<View>& v)
	{
		if (Alive(v->Gantry))
		{
			v->Renderers.Remove(v->Gantry);
			Destroy(v->Gantry->gameObject());
		}
		v->Gantry = nullptr;
		v->Berths.clear();
		auto s = v->S;
		int capacity = s->BerthCount();
		int perPier = Berths::PerPier(s->Kind, s->LengthKm);
		if (capacity <= 0 || perPier <= 0 || s->LengthKm <= 0) return;

		// The model as drawn (the pack's station, else the procedural one), measured in the root's unit frame with its wheel
		// unturned.
		cs::List<cs::Ref<Renderer>> drawn;
		if (Alive(v->Station))
			for (int i = cs::Math::Max(0, v->BaseRenderers); i < v->Renderers.Count(); i++) drawn.Add(v->Renderers[i]);
		else if (v->Renderers.Count() > 0) drawn.Add(v->Renderers[0]);
		Quaternion spun = v->Spinner != nullptr ? v->Spinner->localRotation : Quaternion::identity();
		if (v->Spinner != nullptr) v->Spinner->localRotation = Quaternion::identity();
		HullSurface hull(v->Root->transform, drawn);
		if (v->Spinner != nullptr) v->Spinner->localRotation = spun;
		Vector3 mn = hull.Min(), mx = hull.Max(), c = hull.Centre();

		float L = static_cast<float>(s->LengthKm);
		float pitch = static_cast<float>(BerthTuning::PitchKm) / L;
		float arm = static_cast<float>(BerthTuning::ArmKm) / L;
		float t = Mathf::Clamp(0.06f / L, 0.003f, 0.02f); // a girder about 60 m thick
		float hold = static_cast<float>(BerthTuning::HoldSpacingKm) / L;
		std::vector<Rendering::ShipMeshFactory::BerthBox> boxes;
		auto box = [&boxes](Vector3 centre, Vector3 size, bool light)
		{
			Rendering::ShipMeshFactory::BerthBox b;
			b.Center = centre;
			b.Size = size;
			b.Light = light;
			boxes.push_back(b);
		};

		if (Berths::Spins(s->Kind))
		{
			// A despun docking spine along the wheel's axis (+Z) through its hub, out past the wheel on both sides; four ways
			// round it a berth every pitch, each hull nose-in on an arm off the spine.
			int perSide = perPier / 2;
			float face = Mathf::Max(mx.z - c.z, c.z - mn.z);
			float r = Mathf::Max(2.0f * t, 0.012f);
			float z0 = face + arm;
			float tip = z0 + perSide * pitch;
			box(c, Vector3(2 * r, 2 * r, 2 * tip), false);
			const Vector3 ways[] = { Vector3::right(), Vector3::left(), Vector3::up(), Vector3::down() };
			for (int side = 0; side < 2; side++)
				for (int k = 0; k < perSide; k++)
				{
					float z = (side == 0 ? 1.0f : -1.0f) * (z0 + (k + 0.5f) * pitch);
					for (const auto& w : ways)
					{
						auto across = Mathf::Abs(w.x) > 0.5f ? Vector3::up() : Vector3::right();
						Vector3 at = c + Vector3(0, 0, z);
						box(at + w * (r + arm * 0.5f), Span(w, arm, across, t, Vector3::forward(), t), false);
						Vector3 nose = at + w * (r + arm);
						box(nose, Vector3::one() * (t * 1.8f), true);
						View::Slot slot;
						slot.Nose = nose;
						slot.In = -w;
						slot.Up = Vector3::forward();
						v->Berths.push_back(slot);
					}
				}
			float rim = Mathf::Max(Mathf::Max(mx.x - c.x, c.x - mn.x), Mathf::Max(mx.y - c.y, c.y - mn.y));
			v->HoldOut = Vector3::right();
			v->HoldFrom = c + Vector3::right() * (rim + hold);
		}
		else
		{
			// Four piers down the station's length (its longer horizontal axis), one off each flank, a berth every pitch;
			// each hull nose-in on an arm out from the plating.
			bool alongZ = (mx.z - mn.z) >= (mx.x - mn.x);
			Vector3 A = alongZ ? Vector3::forward() : Vector3::right();
			Vector3 H = alongZ ? Vector3::right() : Vector3::forward();
			float span = perPier * pitch;
			float first = -0.5f * span + 0.5f * pitch;
			struct Flank { Vector3 N; float Out; Vector3 Across; };
			const Flank flanks[] = {
				{ H, Vector3::Dot(mx - c, H), Vector3::up() },
				{ -H, Vector3::Dot(c - mn, H), Vector3::up() },
				{ Vector3::up(), mx.y - c.y, H },
				{ Vector3::down(), c.y - mn.y, H },
			};
			for (const auto& f : flanks)
			{
				// The pier: a girder along the flank at the arms' ends.
				box(c + f.N * (f.Out + arm), Span(A, span + 0.3f * pitch, f.N, t, f.Across, 2 * t), false);
				for (int k = 0; k < perPier; k++)
				{
					Vector3 at = c + A * (first + k * pitch);
					box(at + f.N * (f.Out + arm * 0.5f), Span(f.N, arm, A, t, f.Across, t), false);
					Vector3 nose = at + f.N * (f.Out + arm + t);
					box(nose, Vector3::one() * (t * 1.8f), true);
					View::Slot slot;
					slot.Nose = nose;
					slot.In = -f.N;
					slot.Up = f.Across;
					v->Berths.push_back(slot);
				}
			}
			float reach = Mathf::Max(0.5f * span, Mathf::Max(Vector3::Dot(mx - c, A), Vector3::Dot(c - mn, A)));
			v->HoldOut = A;
			v->HoldFrom = c + A * (reach + hold);
		}

		cs::String key = cs::Cat(static_cast<int>(s->Kind), " ", perPier, " ", Milli(L), " ", Milli(mn.x), ",", Milli(mn.y), ",", Milli(mn.z),
			" ", Milli(mx.x), ",", Milli(mx.y), ",", Milli(mx.z));
		v->Gantry = Part(v->Root->transform, Rendering::ShipMeshFactory::GetBerthGantry(key, s->Owner(), boxes), "Berths");
		v->Renderers.Add(v->Gantry);
	}

	bool StructureViews::BerthPose(const cs::Ref<OrbitalStructure>& s, int berth, double lengthScene, DVec3& scene, Quaternion& rotation) const
	{
		auto v = ViewOf(s);
		if (v == nullptr || berth < 0 || berth >= static_cast<int>(v->Berths.size())) return false;
		DVec3 at;
		double size = 0;
		Quaternion frame;
		if (!Frame(v, at, size, frame)) return false;
		const auto& slot = v->Berths[static_cast<size_t>(berth)];
		Vector3 in = frame * slot.In;
		Vector3 nose = frame * (slot.Nose * static_cast<float>(size));
		scene = at + DVec3(nose.x, nose.y, nose.z) - DVec3(in.x, in.y, in.z) * (0.5 * lengthScene);
		rotation = Quaternion::LookRotation(in, frame * slot.Up);
		return true;
	}

	bool StructureViews::HoldPose(const cs::Ref<OrbitalStructure>& s, int place, double lengthScene, DVec3& scene, Quaternion& rotation) const
	{
		auto v = ViewOf(s);
		if (v == nullptr || place < 0 || v->Berths.empty()) return false;
		DVec3 at;
		double size = 0;
		Quaternion frame;
		if (!Frame(v, at, size, frame)) return false;
		Vector3 out = frame * v->HoldOut;
		Vector3 from = frame * (v->HoldFrom * static_cast<float>(size));
		double spacing = cs::Math::Max(BerthTuning::HoldSpacingKm / s->LengthKm * size, 1.4 * lengthScene);
		double along = place * spacing + 0.5 * lengthScene;
		scene = at + DVec3(from.x, from.y, from.z) + DVec3(out.x, out.y, out.z) * along;
		auto upAxis = Mathf::Abs(Vector3::Dot(v->HoldOut, Vector3::up())) > 0.9f ? Vector3::right() : Vector3::up();
		rotation = Quaternion::LookRotation(-out, frame * upAxis);
		return true;
	}
}
