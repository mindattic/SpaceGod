// Port of Unity.SpaceGod Runtime/Views/ShipView.cs (US-T30).
#include "Views/ShipView.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/ShipMeshFactory.h"
#include "Rendering/ShipModels.h"
#include "Rendering/StarSparrowKit.h"
#include "Rendering/UscKit.h"

#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Economy/BeltMining.h"
#include "Ships.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using Rendering::MeshFactory;
	using Rendering::ShipMeshFactory;
	using Rendering::ShipModels;

	class ShipView::Part final
	{
	public:
		cs::Ref<Subsystem> Sub;
		cs::Ref<Transform> Mount, Barrels;
		cs::List<cs::Ref<Renderer>> Renderers;
		cs::Ref<GameObject> Scar;
		cs::Ref<Glow> Fire;
		bool ShownDestroyed = false;
		bool Turret = false;
		Quaternion Rest;
	};

	class ShipView::Glow final
	{
	public:
		cs::Ref<Transform> T;
		cs::Ref<Renderer> R;
		float Size = 0, Phase = 0;
		cs::Ref<Part> Owner;
		bool Engine = false;
	};

	namespace
	{
		cs::Ref<Mesh> plumeMesh, quadMesh;
		cs::Ref<Material> plumeMaterial, fireMaterial, engineMaterial, shieldRingMaterial;
	}

	const cs::List<float>& ShipView::BreachAt()
	{
		static cs::List<float> at{ 0.85f, 0.7f, 0.55f, 0.4f, 0.28f, 0.16f };
		return at;
	}

	ShipView::ShipView(const cs::Ref<Core::Ship>& ship, const cs::Ref<Transform>& parent)
	{
		Ship = ship;
		BuiltFor = ship->Combat;
		Root = NewGameObject(ship->Name);
		Root->transform->SetParent(parent, false);
		ShipModels::Build(ship, Root->transform, renderers, stern, bow);
		if (!bow && stern->gameObject() && stern->gameObject()->name == ShipModels::TrussSection)
		{
			truss = GetComponent<MeshFilter>(stern);
			trussShown = ShipModels::TrussLoad(ship);
		}
		// A kit-built truss freighter (US-T110) carries its containers as parts: as many shown as its load fills.
		if (!bow && !truss)
			for (const auto& r : renderers)
				if (r->gameObject() && r->gameObject()->name.rfind(Rendering::StarSparrowKit::CargoName, 0) == 0) cargo.Add(r);
		EnsureStatics();

		auto plumeGo = NewGameObject("Plume");
		plumeGo->transform->SetParent(Root->transform, false);
		plumeGo->transform->localPosition = Vector3(0, 0, -0.5f);
		plumeGo->transform->localScale = Vector3(0.12f, 0.12f, 0.6f);
		AddComponent<MeshFilter>(plumeGo)->sharedMesh = plumeMesh;
		plume = AddComponent<MeshRenderer>(plumeGo);
		plume->SetSharedMaterial(plumeMaterial);
		plume->shadowCastingMode = ShadowCastingMode::Off;

		exhaust = BuildExhaust(Root->transform);

		BuildShieldRing();

		if (!bow) return;

		const auto& subs = ship->Combat->Subsystems;
		for (int i = 0; i < subs.Count(); i++) parts.Add(BuildPart(subs[i]));

		// Breach points along the upper hull, fixed per ship so the damage reads consistently.
		cs::Random rng(ship->Id * 7919 + 13);
		for (int i = 0; i < BreachAt().Count(); i++)
		{
			float z = static_cast<float>(rng.NextDouble() * 0.8 - 0.42);
			float x = static_cast<float>(rng.NextDouble() - 0.5) * 0.09f;
			float y = static_cast<float>(rng.NextDouble() * 0.5 - 0.1) * 0.06f;
			breaches.Add(NewGlow(SectionFor(z), Vector3(x, y, z), 0.035f, fireMaterial, nullptr));
		}
	}

	void ShipView::EnsureStatics()
	{
		if (!Alive(plumeMesh)) plumeMesh = MeshFactory::Plume();
		if (!Alive(quadMesh)) quadMesh = MeshFactory::Quad();
		if (Alive(plumeMaterial)) return;
		plumeMaterial = GlowMaterial("Plume", Color(0.45f, 0.75f, 1.0f), 1.4f, 1.6f);
		fireMaterial = GlowMaterial("Fire", Color(1.0f, 0.5f, 0.18f), 1.6f, 1.8f);
		engineMaterial = GlowMaterial("EngineGlow", Color(0.5f, 0.8f, 1.0f), 1.5f, 2.2f);
		shieldRingMaterial = Materials::MarkShared(Materials::Create(Materials::Fx(), "ShieldRing"));
	}

	cs::Ref<Material> ShipView::GlowMaterial(const cs::String& name, Color c, float intensity, float falloff)
	{
		auto m = Materials::MarkShared(Materials::Create(Materials::Glow(), name));
		m->SetColor("_Color", c);
		m->SetFloat("_Intensity", intensity);
		m->SetFloat("_Falloff", falloff);
		return m;
	}

	/// <summary>
	/// A stream of small glowing embers trailing from the engine nozzle, alongside (not instead
	/// of) the existing teardrop plume mesh -- the plume reads as the steady glow of the drive
	/// itself, this is the turbulent exhaust it throws off. Sized and simulated in the ship's own
	/// local space with hierarchy scaling, so it shrinks and grows correctly as the ship's own
	/// root transform is rescaled frame to frame (everything in this sim is drawn at whatever
	/// scale the camera's current distance calls for, never a fixed real-world size).
	/// </summary>
	cs::Ref<ParticleSystem> ShipView::BuildExhaust(const cs::Ref<Transform>& parent)
	{
		auto go = NewGameObject("Exhaust");
		go->transform->SetParent(parent, false);
		// Cone shape emits along local +Z by default; the hull's nose is +Z (see ShipModels'
		// doc comment), so the whole emitter is turned to face aft instead of flipping the shape.
		go->transform->localPosition = Vector3(0, 0, -0.5f);
		go->transform->localRotation = Quaternion::Euler(0, 180, 0);

		auto ps = AddComponent<ParticleSystem>(go);
		auto& main = ps->main;
		main.loop = true;
		main.playOnAwake = false;
		main.simulationSpace = UnityCompat::ParticleSystemSimulationSpace::Local;
		main.scalingMode = UnityCompat::ParticleSystemScalingMode::Hierarchy;
		main.startLifetime = 0.3f;
		main.startSpeed = 1.1f;
		main.startSize = UnityCompat::MinMaxCurve(0.03f, 0.06f);
		main.startColor = Color(0.75f, 0.88f, 1.0f, 1.0f);
		main.maxParticles = 150;

		auto& emission = ps->emission;
		emission.rateOverTime = 70.0f;

		auto& shape = ps->shape;
		shape.shapeType = UnityCompat::ParticleSystemShapeType::Cone;
		shape.angle = 7.0f;
		shape.radius = 0.015f;

		auto& colorOverLifetime = ps->colorOverLifetime;
		colorOverLifetime.enabled = true;
		UnityCompat::Gradient fade;
		fade.SetKeys(
			{ UnityCompat::GradientColorKey(Color(0.9f, 0.95f, 1.0f), 0.0f), UnityCompat::GradientColorKey(Color(0.4f, 0.7f, 1.0f), 1.0f) },
			{ UnityCompat::GradientAlphaKey(1.0f, 0.0f), UnityCompat::GradientAlphaKey(0.0f, 1.0f) });
		colorOverLifetime.color = fade;

		auto& sizeOverLifetime = ps->sizeOverLifetime;
		sizeOverLifetime.enabled = true;
		sizeOverLifetime.size = UnityCompat::MinMaxCurve(1.0f, UnityCompat::AnimationCurve::Linear(0, 1, 1, 0.2f));

		auto r = GetComponent<ParticleSystemRenderer>(go);
		r->SetSharedMaterial(plumeMaterial);
		r->renderMode = UnityCompat::ParticleSystemRenderMode::Billboard;
		r->shadowCastingMode = ShadowCastingMode::Off;
		r->receiveShadows = false;
		r->alignment = UnityCompat::ParticleSystemRenderSpace::View;

		ps->Stop(true, UnityCompat::ParticleSystemStopBehavior::StopEmittingAndClear);
		return ps;
	}

	/// <summary>
	/// Builds the shield fields' geometry once (U.SPC-T501): one glowing disc per shield emitter, over the patch of hull
	/// its field covers, in the hull's own normalized (nose +Z, up +Y) space. Each disc sits just off the hull over its
	/// emitter, facing out from the keel, as wide as half the way to the nearest other emitter (the patch's own reach,
	/// see <see cref="ShipCombat.PatchAt"/>). Every vertex shares one UV so the Fx shader's radial shape term comes out
	/// a constant multiplier everywhere; the shader just makes it glow additively from the vertex colour, which is
	/// where each emitter's own field gets shown (recoloured every frame in <see cref="UpdateShieldRing"/>, never
	/// rebuilt), so a patch shot away shows as a dark hole in the hull's shields.
	/// </summary>
	void ShipView::BuildShieldRing()
	{
		for (const auto& s : Ship->Combat->Subsystems)
			if (s->Kind() == SubsystemKind::ShieldGenerator) shieldGenerators.Add(s);
		if (shieldGenerators.Count() == 0) return;

		constexpr int segs = 16;
		constexpr float lift = 0.03f;
		cs::List<Vector3> verts;
		cs::List<Vector3> uvs;
		cs::List<Color> cols;
		cs::List<int> tris;
		shieldWedgeStart = cs::List<int>(std::vector<int>(static_cast<size_t>(shieldGenerators.Count())));
		shieldWedgeCount = cs::List<int>(std::vector<int>(static_cast<size_t>(shieldGenerators.Count())));

		for (int gi = 0; gi < shieldGenerators.Count(); gi++)
		{
			const auto& def = shieldGenerators[gi]->Def;
			Vector3 at(static_cast<float>(def->X), static_cast<float>(def->Y), static_cast<float>(def->Z));
			// The patch reaches halfway to the nearest other emitter; a lone emitter covers the whole hull.
			float nearest = std::numeric_limits<float>::max();
			for (const auto& o : shieldGenerators)
			{
				if (o == shieldGenerators[gi]) continue;
				nearest = Mathf::Min(nearest, Vector3::Distance(at, Vector3(static_cast<float>(o->Def->X), static_cast<float>(o->Def->Y), static_cast<float>(o->Def->Z))));
			}
			float radius = nearest == std::numeric_limits<float>::max() ? 0.5f : Mathf::Clamp(nearest * 0.5f, 0.05f, 0.5f);
			// Out from the keel line at the emitter's station; one sitting on the keel faces up.
			Vector3 normal(at.x, at.y, 0);
			normal = normal.sqrMagnitude() > 1e-6f ? normal.normalized() : Vector3::up();
			Vector3 u = Vector3::Cross(normal, Vector3::forward());
			if (u.sqrMagnitude() < 1e-6f) u = Vector3::right();
			u.Normalize();
			Vector3 v = Vector3::Cross(normal, u).normalized();
			Vector3 centre = at + normal * lift;

			shieldWedgeStart[gi] = verts.Count();
			int hub = verts.Count();
			verts.Add(centre);
			uvs.Add(Vector3(0.5f, 0.5f, 0.0f));
			cols.Add(Color::clear());
			for (int k = 0; k < segs; k++)
			{
				float t = k * Mathf::PI * 2 / segs;
				verts.Add(centre + (u * Mathf::Cos(t) + v * Mathf::Sin(t)) * radius);
				uvs.Add(Vector3(0.5f, 0.5f, 0.0f));
				cols.Add(Color::clear());
			}
			for (int k = 0; k < segs; k++)
			{
				int a = hub + 1 + k, b = hub + 1 + (k + 1) % segs;
				// Both windings, so the field reads whichever side the camera is on.
				tris.Add(hub); tris.Add(a); tris.Add(b);
				tris.Add(hub); tris.Add(b); tris.Add(a);
			}
			shieldWedgeCount[gi] = verts.Count() - shieldWedgeStart[gi];
		}

		shieldRingMesh = Materials::Own(cs::New<Mesh>("ShieldFields"));
		shieldRingMesh->SetVertices(verts);
		shieldRingMesh->SetUVs(0, uvs);
		shieldRingMesh->SetColors(cols);
		shieldRingMesh->SetTriangles(tris, 0);
		shieldRingMesh->RecalculateBounds();

		auto go = NewGameObject("ShieldFields");
		go->transform->SetParent(Root->transform, false);
		AddComponent<MeshFilter>(go)->sharedMesh = shieldRingMesh;
		shieldRingRenderer = AddComponent<MeshRenderer>(go);
		shieldRingRenderer->SetSharedMaterial(shieldRingMaterial);
		shieldRingRenderer->shadowCastingMode = ShadowCastingMode::Off;
		shieldRingRenderer->enabled = false;
		shieldVertexColors = cols.ToArray();
	}

	/// <summary>Recolours each patch's disc from its own emitter's live field (none for one shot away); only shown while fighting.</summary>
	void ShipView::UpdateShieldRing(bool fighting)
	{
		if (!shieldRingRenderer) return;
		shieldRingRenderer->enabled = fighting && visible;
		if (!fighting || !visible) return;
		for (int gi = 0; gi < shieldGenerators.Count(); gi++)
		{
			const auto& gen = shieldGenerators[gi];
			Color c = gen->Destroyed() ? Color::clear() : ShieldColor(static_cast<float>(gen->ShieldFraction()));
			int start = shieldWedgeStart[gi], count = shieldWedgeCount[gi];
			for (int v = 0; v < count; v++) shieldVertexColors[start + v] = c;
		}
		shieldRingMesh->SetColors(shieldVertexColors);
	}

	cs::Ref<Transform> ShipView::SectionFor(float z) const { return bow && z >= ShipMeshFactory::SplitZ ? bow : stern; }

	cs::Ref<ShipView::Part> ShipView::BuildPart(const cs::Ref<Subsystem>& sub)
	{
		const auto& d = sub->Def;
		auto part = cs::New<Part>();
		part->Sub = sub;
		auto section = SectionFor(static_cast<float>(d->Z));
		Vector3 pos(static_cast<float>(d->X), static_cast<float>(d->Y), static_cast<float>(d->Z));
		// Mounts under the keel hang upside down.
		bool under = d->Y < -0.005;
		part->Rest = Quaternion::Euler(0, d->FacesAft ? 180.0f : 0.0f, under ? 180.0f : 0.0f);

		if (sub->Kind() == SubsystemKind::Engine)
		{
			auto g = NewGlow(section, pos + Vector3(0, 0, -0.035f), 0.06f, engineMaterial, part);
			g->Engine = true;
		}
		// The reactor, and a bridge buried at the centre of the hull with no viewports (U.SPC-T492), show nothing outside.
		else if (sub->Kind() == SubsystemKind::Reactor || d->Buried)
		{
		}
		else
		{
			auto mount = NewGameObject(SubsystemDef::Name(sub->Kind()))->transform;
			mount->SetParent(section, false);
			mount->localPosition = pos;
			mount->localRotation = part->Rest;
			part->Mount = mount;
			// A spinal mount is a gun the length of the hull laid down the keel from its muzzle (U.SPC-T490). A gun turret is
			// Ultimate Spaceships Creator's turret module and barrels when that pack is there (US-T142), else the Star
			// Sparrow kit's when the hull is (US-T110): a pod barbette and its barrels.
			cs::List<cs::Ref<Renderer>> list;
			bool usc = !d->Spinal && Rendering::UscKit::Turret(mount, sub->Kind(), Ship->Faction, false, list);
			bool kit = usc || (!d->Spinal && Rendering::StarSparrowKit::Turret(mount, sub->Kind(), Ship->Faction, false, list));
			if (!kit)
				list.Add(ShipModels::Part(mount, d->Spinal ? ShipMeshFactory::GetSpinal(sub->Kind(), Ship->Faction)
					: ShipMeshFactory::GetPart(sub->Kind(), Ship->Faction, false), "Base"));
			SubsystemKind k = sub->Kind();
			part->Turret = !d->Spinal && (k == SubsystemKind::HeavyLaser || k == SubsystemKind::LaserTurret || k == SubsystemKind::PointDefense ||
				k == SubsystemKind::PlasmaCannon || k == SubsystemKind::FlakCannon || k == SubsystemKind::VulcanCannon);
			if (part->Turret)
			{
				auto barrels = NewGameObject("Barrels")->transform;
				barrels->SetParent(mount, false);
				barrels->localPosition = Vector3(0, k == SubsystemKind::HeavyLaser || k == SubsystemKind::PlasmaCannon ? 0.014f : k == SubsystemKind::LaserTurret ? 0.009f
					: k == SubsystemKind::FlakCannon || k == SubsystemKind::VulcanCannon ? 0.011f : 0.008f, 0);
				part->Barrels = barrels;
				if (!kit || !(usc ? Rendering::UscKit::Turret(barrels, k, Ship->Faction, true, list)
						: Rendering::StarSparrowKit::Turret(barrels, k, Ship->Faction, true, list)))
					list.Add(ShipModels::Part(barrels, ShipMeshFactory::GetPart(k, Ship->Faction, true), "Barrels"));
			}
			part->Renderers = list;
		}

		auto scar = ShipModels::Part(section, ShipMeshFactory::GetScar(), "Scar");
		scar->transform()->localPosition = pos;
		scar->transform()->localRotation = part->Rest;
		scar->enabled = false;
		part->Scar = scar->gameObject();
		part->Fire = NewGlow(section, pos + Vector3(0, under ? -0.012f : 0.012f, 0), 0.05f, fireMaterial, part);
		part->Fire->R->enabled = false;
		return part;
	}

	cs::Ref<ShipView::Glow> ShipView::NewGlow(const cs::Ref<Transform>& parent, Vector3 localPos, float size, const cs::Ref<Material>& material, const cs::Ref<Part>& owner)
	{
		auto go = NewGameObject("Glow");
		go->transform->SetParent(parent, false);
		go->transform->localPosition = localPos;
		AddComponent<MeshFilter>(go)->sharedMesh = quadMesh;
		auto mr = AddComponent<MeshRenderer>(go);
		mr->SetSharedMaterial(material);
		mr->shadowCastingMode = ShadowCastingMode::Off;
		auto g = cs::New<Glow>();
		g->T = go->transform;
		g->R = mr;
		g->Size = size;
		g->Phase = UnityRandom::value() * 10;
		g->Owner = owner;
		mr->enabled = false;
		glows.Add(g);
		return g;
	}

	// ---------------------------------------------------------------- per frame

	Vector3 ShipView::HullCentre() const { return HullBox().center; }

	Bounds ShipView::HullBox() const
	{
		// Each hull mesh's box corners through its own transform into the root's frame (unit length): the root's
		// world placement taken back out, so the answer is the same whenever the root was last placed.
		auto root = Root ? Root->transform : nullptr;
		if (!root) return Bounds();
		float scale = root->localScale.x;
		if (!(scale > 0)) return Bounds();
		auto toLocal = Quaternion::Inverse(root->rotation());
		Vector3 origin = root->position();
		bool any = false;
		Bounds box;
		for (const auto& r : renderers)
		{
			if (!r || !r->transform()) continue;
			auto mesh = r->DrawnMesh();
			if (!mesh) continue;
			auto m = WorldMatrix::Of(*r->transform());
			Bounds local = mesh->bounds();
			Vector3 mn = local.min(), mx = local.max();
			for (int i = 0; i < 8; i++)
			{
				auto world = m.MultiplyPoint(Vector3((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z));
				auto p = toLocal * (world - origin) / scale;
				if (!any) box = Bounds(p, Vector3::zero());
				else box.Encapsulate(p);
				any = true;
			}
		}
		return any ? box : Bounds();
	}

	void ShipView::Apply(Vector3 relative, Quaternion rotation, float length, bool burning, bool highlighted)
	{
		auto t = Root->transform;
		t->localPosition = relative;
		t->localRotation = rotation;
		t->localScale = Vector3::one() * length;
		stern->localPosition = Vector3::zero();
		stern->localRotation = Quaternion::identity();
		if (bow)
		{
			bow->localPosition = Vector3::zero();
			bow->localRotation = Quaternion::identity();
		}
		if (truss)
		{
			// The pods clamped on show its load: loaded in port, dropped off where it sells.
			auto load = ShipModels::TrussLoad(Ship);
			if (load != trussShown)
			{
				trussShown = load;
				truss->sharedMesh = ShipMeshFactory::GetTruss(Ship->Faction, std::get<0>(load), std::get<1>(load), std::get<2>(load));
			}
		}

		UpdateTow();
		if (cargo.Count() > 0)
		{
			int loaded = std::get<1>(ShipModels::TrussLoad(Ship));
			for (int i = 0; i < cargo.Count(); i++) cargo[i]->enabled = visible && i < loaded;
		}

		block.SetFloat(EngineGlowId, burning ? 2.2f : 0.35f);
		block.SetFloat(HighlightId, highlighted ? 1.0f : 0.0f);
		block.SetColor(ColorId, Color::white());
		SetBlock();
		if (towRenderer) towRenderer->SetPropertyBlock(block);
		plume->enabled = visible && burning;
		SetExhaust(visible && detailed && burning);
	}

	/// <summary>
	/// An ore tug's load (U.SPC-T498): the rock or block of ice in its claws, drawn just ahead of its push plate at its
	/// true size against the hull (the root is scaled to the hull's length, so a unit here is one hull length), and gone
	/// when it lets go.
	/// </summary>
	void ShipView::UpdateTow()
	{
		auto load = Ship->Tow;
		if (!load)
		{
			if (towRenderer) towRenderer->enabled = false;
			towShown = nullptr;
			return;
		}
		if (!towRenderer)
		{
			auto go = NewGameObject("Tow");
			go->transform->SetParent(Root->transform, false);
			towFilter = AddComponent<MeshFilter>(go);
			towRenderer = AddComponent<MeshRenderer>(go);
			towRenderer->SetSharedMaterial(ShipModels::ProceduralMaterial());
			towRenderer->shadowCastingMode = ShadowCastingMode::Off;
			towRenderer->receiveShadows = false;
		}
		if (towShown != load)
		{
			towShown = load;
			towFilter->sharedMesh = ShipMeshFactory::GetTowLoad(load->Class == AsteroidClass::CometIce());
			float across = Ship->LengthKm() > 0 ? static_cast<float>(load->DiameterM / 1000 / Ship->LengthKm()) : 1.0f;
			auto t = towFilter->transform();
			t->localScale = Vector3::one() * across;
			// Resting against the push plate, its near side in the claws' grip.
			t->localPosition = Vector3(0, 0, 0.38f + across * 0.45f);
			// A fixed tumble per load, so each block reads as its own lump of rock.
			int seed = 0;
			for (char ch : load->Designation) seed = cs::WrapAdd(cs::WrapMul(seed, 31), static_cast<int>(static_cast<unsigned char>(ch)));
			t->localRotation = Quaternion::Euler(static_cast<float>((seed & 0x7fff) % 360), 37.0f + ((seed >> 15) & 0xff), 11.0f);
		}
		towRenderer->enabled = visible;
	}

	/// <summary>Starts/stops the exhaust emitter only on an actual state change -- <see cref="ParticleSystem.Stop"/>
	/// defaults to letting already-spawned embers finish fading out rather than clearing them instantly,
	/// which reads as the drive spooling down rather than snapping off.</summary>
	void ShipView::SetExhaust(bool want)
	{
		if (want == exhaustPlaying) return;
		exhaustPlaying = want;
		if (want) exhaust->Play();
		else exhaust->Stop();
	}

	void ShipView::ApplyCombat(const cs::Ref<ShipCombat>& combat, bool fighting, bool thrusting, float now, float dt)
	{
		if (!visible || !detailed) return;
		UpdateShieldRing(fighting);
		if (!bow) return;
		Quaternion inverse = Quaternion::Inverse(Root->transform->rotation());
		for (const auto& p : parts)
		{
			const auto& sub = p->Sub;
			if (sub->Destroyed() != p->ShownDestroyed)
			{
				p->ShownDestroyed = sub->Destroyed();
				for (const auto& r : p->Renderers) r->enabled = !sub->Destroyed();
				GetComponent<Renderer>(p->Scar)->enabled = sub->Destroyed() && p->Mount != nullptr;
				p->Fire->R->enabled = sub->Destroyed();
			}
			if (p->Turret && !sub->Destroyed()) Aim(*p, sub, fighting, inverse, dt);
		}
		float hull = static_cast<float>(combat->HullFraction());
		for (int i = 0; i < breaches.Count(); i++) breaches[i]->R->enabled = hull < BreachAt()[i];
		for (const auto& g : glows)
		{
			if (!g->Engine) continue;
			bool alive = !g->Owner->Sub->Destroyed();
			g->R->enabled = alive;
			g->Size = thrusting ? 0.075f : 0.045f;
		}
		Billboard(now);
	}

	/// <summary>Traverses the mount about its own up axis and elevates the barrels, both rate-limited.</summary>
	void ShipView::Aim(Part& p, const cs::Ref<Subsystem>& sub, bool fighting, Quaternion inverseShip, float dt)
	{
		Quaternion goalMount = p.Rest, goalBarrels = Quaternion::identity();
		if (fighting && sub->Aim.LengthSquared() > 1e-9)
		{
			auto a = sub->Aim.SwapYZ();
			Vector3 world(static_cast<float>(a.X), static_cast<float>(a.Y), static_cast<float>(a.Z));
			// Into the section's frame (sections stay aligned with the root unless wrecked).
			Vector3 local = inverseShip * world;
			Vector3 mountLocal = Quaternion::Inverse(Quaternion::Euler(0, 0, p.Rest.eulerAngles().z)) * local;
			Vector3 flat(mountLocal.x, 0, mountLocal.z);
			if (flat.sqrMagnitude() > 1e-8f)
			{
				goalMount = Quaternion::Euler(0, 0, p.Rest.eulerAngles().z) * Quaternion::LookRotation(flat.normalized(), Vector3::up());
				float elevation = Mathf::Atan2(mountLocal.y, flat.magnitude()) * Mathf::Rad2Deg;
				goalBarrels = Quaternion::Euler(-Mathf::Clamp(elevation, -8.0f, 80.0f), 0, 0);
			}
		}
		SubsystemKind k = sub->Kind();
		float traverse = (k == SubsystemKind::PointDefense ? 360.0f : k == SubsystemKind::FlakCannon ? 240.0f
			: k == SubsystemKind::HeavyLaser || k == SubsystemKind::PlasmaCannon ? 50.0f : 90.0f) * dt;
		p.Mount->localRotation = Quaternion::RotateTowards(p.Mount->localRotation, goalMount, traverse);
		p.Barrels->localRotation = Quaternion::RotateTowards(p.Barrels->localRotation, goalBarrels, traverse);
	}

	void ShipView::Billboard(float now)
	{
		Camera* cam = Camera::main();
		if (!cam) return;
		Vector3 camPos = cam->transform->position();
		for (const auto& g : glows)
		{
			if (!g->R->enabled) continue;
			// World position through every ancestor's scale (Unity's Transform.position).
			Vector3 p = WorldMatrix::Of(*g->T).t;
			g->T->SetRotation(Quaternion::LookRotation(p - camPos, cam->transform->up()));
			float flicker = g->Engine ? 1.0f : 0.75f + 0.25f * Mathf::Sin(now * 13.0f + g->Phase) * Mathf::Sin(now * 7.3f + g->Phase * 2.0f);
			g->T->localScale = Vector3::one() * (g->Size * flicker);
		}
	}

	bool ShipView::Wreck(Vector3 relative, Quaternion rotation, float length, float age, float now)
	{
		constexpr float Life = WreckBurnS;
		if (age > Life)
		{
			SetVisible(false);
			return false;
		}
		SetVisible(true);
		if (shieldRingRenderer) shieldRingRenderer->enabled = false;
		auto t = Root->transform;
		t->localPosition = relative;
		t->localRotation = rotation;
		t->localScale = Vector3::one() * length;
		if (wreckSpin == 0) wreckSpin = 6.0f + (Ship->Id % 5) * 3.0f;
		if (bow)
		{
			PlaceBrokenHalves(age);
			for (const auto& p : parts)
			{
				for (const auto& r : p->Renderers) r->enabled = false;
				GetComponent<Renderer>(p->Scar)->enabled = p->Mount != nullptr;
				p->Fire->R->enabled = age < Life * 0.7f && (p->Sub->Index % 2 == 0);
			}
			for (const auto& g : breaches) g->R->enabled = age < Life * 0.8f;
			for (const auto& g : glows)
				if (g->Engine) g->R->enabled = false;
			Billboard(now);
		}
		else
		{
			stern->localRotation = Quaternion::Euler(age * 9.0f, age * 5.0f, age * 14.0f);
		}
		float burnt = Mathf::Clamp01(age / 1.5f);
		block.SetFloat(EngineGlowId, 0.0f);
		block.SetFloat(HighlightId, 0.0f);
		block.SetColor(ColorId, Color::Lerp(Color::white(), Color(0.22f, 0.2f, 0.19f), burnt));
		SetBlock();
		plume->enabled = false;
		HardStopExhaust();
		return true;
	}

	/// <summary>A warship's bow and stern drifting apart and tumbling, <paramref name="age"/> real seconds after it broke.</summary>
	void ShipView::PlaceBrokenHalves(float age)
	{
		if (wreckSpin == 0) wreckSpin = 6.0f + (Ship->Id % 5) * 3.0f;
		float drift = Mathf::Sqrt(age) * 0.07f;
		bow->localPosition = Vector3(0.01f * age, 0.015f * age, drift);
		bow->localRotation = Quaternion::Euler(age * wreckSpin * 0.6f, age * wreckSpin * 0.3f, age * wreckSpin);
		stern->localPosition = Vector3(-0.01f * age, -0.01f * age, -drift);
		stern->localRotation = Quaternion::Euler(-age * wreckSpin * 0.4f, 0, -age * wreckSpin * 0.7f);
	}

	void ShipView::BurntOut(Vector3 relative, Quaternion rotation, float length, float now)
	{
		Derelict(relative, rotation, length, now);
		if (!bow) return;
		PlaceBrokenHalves(WreckBurnS);
		for (const auto& p : parts)
		{
			for (const auto& r : p->Renderers) r->enabled = false;
			GetComponent<Renderer>(p->Scar)->enabled = p->Mount != nullptr;
			p->Fire->R->enabled = false;
		}
		for (const auto& g : breaches) g->R->enabled = false;
	}

	void ShipView::Derelict(Vector3 relative, Quaternion rotation, float length, float now)
	{
		SetVisible(true);
		if (shieldRingRenderer) shieldRingRenderer->enabled = false;
		auto t = Root->transform;
		t->localPosition = relative;
		t->localRotation = rotation * Quaternion::Euler(now * 1.3f, now * 0.7f, now * 2.1f);
		t->localScale = Vector3::one() * length;
		stern->localPosition = Vector3::zero();
		stern->localRotation = Quaternion::identity();
		if (bow)
		{
			bow->localPosition = Vector3::zero();
			bow->localRotation = Quaternion::identity();
		}
		for (const auto& g : glows) g->R->enabled = false;
		block.SetFloat(EngineGlowId, 0.0f);
		block.SetFloat(HighlightId, 0.0f);
		block.SetColor(ColorId, Color(0.35f, 0.33f, 0.32f));
		SetBlock();
		plume->enabled = false;
		HardStopExhaust();
	}

	/// <summary>A wrecked or derelict hull cuts its exhaust immediately rather than letting it fade
	/// (see <see cref="SetExhaust"/>) -- the drive is gone, not just throttled down.</summary>
	void ShipView::HardStopExhaust()
	{
		exhaustPlaying = false;
		exhaust->Stop(true, UnityCompat::ParticleSystemStopBehavior::StopEmittingAndClear);
	}

	void ShipView::SetBlock()
	{
		for (const auto& r : renderers) r->SetPropertyBlock(block);
		for (const auto& p : parts)
			for (const auto& r : p->Renderers) r->SetPropertyBlock(block);
	}

	void ShipView::SetDetail(bool full)
	{
		if (detailed == full) return;
		detailed = full;
		if (!visible) return;
		for (const auto& p : parts)
		{
			for (const auto& r : p->Renderers) r->enabled = full && !p->Sub->Destroyed();
			GetComponent<Renderer>(p->Scar)->enabled = full && p->Sub->Destroyed() && p->Mount != nullptr;
			p->Fire->R->enabled = full && p->Sub->Destroyed();
			// Re-read by ApplyCombat on the next detailed frame.
			p->ShownDestroyed = p->Sub->Destroyed();
		}
		if (!full)
		{
			HardStopExhaust();
			for (const auto& g : glows) g->R->enabled = false;
			for (const auto& g : breaches) g->R->enabled = false;
			if (shieldRingRenderer) shieldRingRenderer->enabled = false;
		}
	}

	void ShipView::SetVisible(bool value)
	{
		if (visible == value) return;
		visible = value;
		for (const auto& r : renderers) r->enabled = value;
		if (towRenderer) towRenderer->enabled = value && Ship->Tow != nullptr;
		for (const auto& p : parts)
		{
			for (const auto& r : p->Renderers) r->enabled = value && detailed && !p->Sub->Destroyed();
			GetComponent<Renderer>(p->Scar)->enabled = value && detailed && p->Sub->Destroyed() && p->Mount != nullptr;
			p->Fire->R->enabled = value && detailed && p->Sub->Destroyed();
		}
		if (!value)
		{
			plume->enabled = false;
			HardStopExhaust();
			for (const auto& g : glows) g->R->enabled = false;
			if (shieldRingRenderer) shieldRingRenderer->enabled = false;
		}
		else
		{
			for (const auto& p : parts) p->ShownDestroyed = !p->Sub->Destroyed();
		}
	}
}
