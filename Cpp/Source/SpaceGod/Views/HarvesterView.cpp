// US-T146: draws the gas giants' works (see HarvesterView.h).
#include "Views/HarvesterView.h"

#include "HarvestPlay.h"
#include "UnitySpaceApp.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/ShipMeshFactory.h"
#include "Rendering/ShipModels.h"
#include "Rendering/UscKit.h"
#if __has_include("Rendering/SscKit.h")
#include "Rendering/SscKit.h"
#define SG_HARVEST_SSC 1
#endif

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Economy/GasHarvest.h"
#include "Economy/Settlement.h"
#include "Infrastructure/Infrastructure.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"

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

		cs::Ref<Material> Glow(const cs::String& name, Color c, float intensity, float falloff)
		{
			auto m = Materials::Create(Materials::Glow(), name);
			m->SetColor("_Color", c);
			m->SetFloat("_Intensity", intensity);
			m->SetFloat("_Falloff", falloff);
			return m;
		}

		cs::Ref<Transform> Sprite(const cs::Ref<Transform>& parent, const cs::String& name, const cs::Ref<Material>& m)
		{
			auto go = NewGameObject(name);
			go->transform->SetParent(parent, false);
			AddComponent<MeshFilter>(go)->sharedMesh = QuadMesh();
			auto r = AddComponent<MeshRenderer>(go);
			r->SetSharedMaterial(m);
			r->shadowCastingMode = ShadowCastingMode::Off;
			r->receiveShadows = false;
			return go->transform;
		}

		void Show(const cs::Ref<Transform>& t, bool shown)
		{
			if (t == nullptr) return;
			auto go = t->gameObject();
			if (go != nullptr && go->activeSelf != shown) go->SetActive(shown);
		}

		Vector3 V(DVec3 d) { return Vector3(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z)); }

		/// <summary>A sprite facing the camera (at the origin of the relative frame), <paramref name="size"/> across.</summary>
		void Billboard(const cs::Ref<Transform>& t, Vector3 at, float size)
		{
			float d = at.magnitude();
			Vector3 toCamera = d > 1e-6f ? -at / d : Vector3::back();
			t->localPosition = at;
			t->localRotation = Quaternion::LookRotation(-toCamera, Vector3::up());
			t->localScale = Vector3::one() * size;
		}

		/// <summary>A sprite stretched from <paramref name="a"/> to <paramref name="b"/>, turned about that line to face the camera.</summary>
		void Beam(const cs::Ref<Transform>& t, Vector3 a, Vector3 b, float width)
		{
			Vector3 axis = b - a;
			float length = axis.magnitude();
			if (length < 1e-9f) { Show(t, false); return; }
			axis = axis / length;
			Vector3 mid = (a + b) * 0.5f;
			Vector3 toCamera = mid.magnitude() > 1e-9f ? -mid.normalized() : Vector3::back();
			Vector3 facing = toCamera - axis * Vector3::Dot(toCamera, axis);
			if (facing.sqrMagnitude() < 1e-8f) facing = Vector3::Cross(axis, Mathf::Abs(axis.y) < 0.9f ? Vector3::up() : Vector3::right());
			t->localPosition = mid;
			t->localRotation = Quaternion::LookRotation(-facing.normalized(), axis);
			t->localScale = Vector3(width, length, 1);
		}

		/// <summary>The gas the giant gives up, by its colour: amber over the warm-banded Jupiters, cyan over the ice giants.</summary>
		Color GasColour(const cs::Ref<BodyDefinition>& def)
		{
			float r = def->Color.R, g = def->Color.G, b = def->Color.B;
			bool icy = b > r;
			return icy ? Color(0.45f + 0.2f * r, 0.85f, 1.0f) : Color(1.0f, 0.62f + 0.2f * g, 0.3f + 0.2f * b);
		}
	}

	HarvesterView::HarvesterView(UnitySpaceApp* app_, const cs::Ref<Transform>& parent_) : app(app_), parent(parent_) {}

	DVec3 HarvesterView::PlatformScene(UnitySpaceApp* app, const cs::Ref<HarvestOperation>& op, int platform)
	{
		auto st = op->Platforms[platform]->Structure;
		return app->BodyScene(op->Body) + app->Scale()->LocalOffset((*app->Catalog())[op->Body], st->Orbit->OffsetAt(app->Clock->Jd));
	}

	double HarvesterView::PlatformSize(UnitySpaceApp* app, const cs::Ref<HarvestOperation>& op, int platform)
	{
		return app->Scale()->ShipLength(op->Platforms[platform]->Structure->LengthKm);
	}

	void HarvesterView::Build()
	{
		built = true;
		auto world = app->World();
		if (world == nullptr) return;
		auto harvest = world->EnsureHarvest(app->Clock->Jd);
		for (const auto& op : harvest->Operations)
		{
			auto def = (*app->Catalog())[op->Body];
			Color gas = GasColour(def);
			for (int i = 0; i < op->Platforms.Count(); i++)
			{
				const auto& p = op->Platforms[i];
				Platform v;
				v.Op = op;
				v.Index = i;
				v.Phase = static_cast<float>(p->Seed % 1000) * 0.001f * 6.2831853f;
				auto rootGo = NewGameObject(p->Name);
				rootGo->transform->SetParent(parent, false);
				v.Root = rootGo->transform;
				// The tank farm: Space Station Creator's storage complex when imported, else spheres of gas on a truss round a hub.
				bool pack = false;
#ifdef SG_HARVEST_SSC
				cs::List<cs::Ref<Renderer>> renderers;
				// Complete prebuilt stations used whole (US-T149): the pack's storage complexes and pressure-tank cylinders.
				static const char* models[] = { "Storage18", "Cylinderical12", "Storage04", "Cylinderical18" };
				pack = Rendering::SscKit::Available() && Rendering::SscKit::Station(v.Root, models[p->Seed % 4], 1.0f, op->Owner, renderers, Rendering::SscKit::Condition::Working, true, p->Seed);
#endif
				if (!pack)
				{
					auto hub = ShipModels::Part(v.Root, ShipMeshFactory::GetStationBody(p->Name), "Hub");
					hub->transform()->localScale = Vector3::one() * 0.35f;
					for (int k = 0; k < 2; k++)
					{
						auto truss = ShipModels::Part(v.Root, ShipMeshFactory::GetTruss(op->Owner, 4, 4, 6), "Tanks");
						truss->transform()->localPosition = Vector3(k == 0 ? 0.32f : -0.32f, 0, 0);
						truss->transform()->localScale = Vector3::one() * 0.9f;
					}
				}
				// The intake boom's glow under the platform, the plume down to the clouds, the flare where it dips, the skimmer.
				v.PlumeMaterial = Glow(cs::Cat(p->Name, " plume"), gas, 0.9f, 1.4f);
				v.IntakeMaterial = Glow(cs::Cat(p->Name, " intake"), Color(gas.r, gas.g * 0.95f + 0.05f, gas.b * 0.9f + 0.1f), 2.2f, 2.0f);
				v.Plume = Sprite(parent, cs::Cat(p->Name, " plume"), v.PlumeMaterial);
				v.Intake = Sprite(parent, cs::Cat(p->Name, " intake flare"), v.IntakeMaterial);
				v.Beacon = Sprite(parent, cs::Cat(p->Name, " beacon"), Glow(cs::Cat(p->Name, " beacon"), Color(0.7f, 0.9f, 1.0f), 1.6f, 2.4f));
				v.Fire = Sprite(parent, cs::Cat(p->Name, " fire"), Glow(cs::Cat(p->Name, " fire"), Color(1.0f, 0.45f, 0.15f), 1.8f, 1.8f));
				auto skimmerGo = NewGameObject(cs::Cat(p->Name, " skimmer"));
				skimmerGo->transform->SetParent(parent, false);
				v.Skimmer = skimmerGo->transform;
				// The skimmer: the purchased pack's courier hull as it comes (USC), else the procedural one.
				{
					cs::List<cs::Ref<Renderer>> into;
					bool usc = Rendering::UscKit::Available()
						&& Rendering::UscKit::Piece(v.Skimmer, Rendering::UscKit::ModelOf(ShipClass::Courier, CivilCraft::None, p->Seed, op->Owner), 1.0f, op->Owner, into);
					if (!usc) ShipModels::Part(v.Skimmer, ShipMeshFactory::Get(ShipClass::Courier, op->Owner), "Skimmer hull");
				}
				v.SkimmerGlow = Sprite(parent, cs::Cat(p->Name, " skimmer glow"), Glow(cs::Cat(p->Name, " skimmer glow"), gas, 2.0f, 2.2f));
				platforms.Add(v);
			}
			for (const auto& t : op->Tankers)
			{
				Tanker v;
				v.Op = op;
				v.T = t;
				auto go = NewGameObject(t->Name);
				go->transform->SetParent(parent, false);
				v.Root = go->transform;
				// A gas tanker: the purchased pack's freighter hull as it comes (USC), else the procedural truss of fuel spheres.
				cs::List<cs::Ref<Renderer>> into;
				bool usc = Rendering::UscKit::Available()
					&& Rendering::UscKit::Piece(v.Root, Rendering::UscKit::ModelOf(ShipClass::Freighter, CivilCraft::None, t->Seed, op->Owner), 1.0f, op->Owner, into);
				if (!usc) ShipModels::Part(v.Root, ShipMeshFactory::GetTruss(op->Owner, 2, 2, 6), "Tanker hull");
				v.Engine = Sprite(parent, cs::Cat(t->Name, " drive"), Glow(cs::Cat(t->Name, " drive"), Color(0.5f, 0.8f, 1.0f), 1.6f, 2.2f));
				tankers.Add(v);
			}
		}
	}

	void HarvesterView::Update()
	{
		HarvestPlay::Tick(*app); // US-T146: the player's side of it (contracts, waves, the keys)
		auto world = app->World();
		if (world == nullptr) return;
		if (!built) Build();
		if (platforms.Count() == 0) return;
		auto harvest = world->Harvest;
		if (harvest == nullptr) return;
		double jd = app->Clock->Jd;
		const auto& scale = *app->Scale();
		float now = Time::unscaledTime;
		for (auto& v : platforms)
		{
			auto def = (*app->Catalog())[v.Op->Body];
			auto p = v.Op->Platforms[v.Index];
			auto st = p->Structure;
			auto bodyScene = app->BodyScene(v.Op->Body);
			auto offset = st->Orbit->OffsetAt(jd);
			auto scene = bodyScene + scale.LocalOffset(def, offset);
			auto ahead = bodyScene + scale.LocalOffset(def, st->Orbit->OffsetAt(jd + 1e-4));
			Vector3 at = app->Relative(scene);
			Vector3 out = V(scene - bodyScene);
			out = out.sqrMagnitude() > 1e-12f ? out.normalized() : Vector3::up();
			Vector3 along = V(ahead - scene);
			along = along - out * Vector3::Dot(along, out);
			along = along.sqrMagnitude() > 1e-12f ? along.normalized() : Vector3::Cross(out, Vector3::right()).normalized();
			float size = static_cast<float>(scale.ShipLength(st->LengthKm));
			v.Root->localPosition = at;
			// Long axis along the orbit, its belly (the intake boom) to the planet.
			v.Root->localRotation = Quaternion::LookRotation(along, out);
			v.Root->localScale = Vector3::one() * size;

			bool down = p->Down(jd);
			// The plume: from the platform's belly down to the cloud tops under it (through the mapping, so it spans the
			// true gap at either scale), the skimmer climbing and diving along it, a flare where it dips.
			auto dip = bodyScene + scale.LocalOffset(def, offset.Normalized() * (def->RadiusKm * 1.002));
			Vector3 bottom = app->Relative(dip);
			Vector3 top = at - out * (size * 0.4f);
			float gap = (top - bottom).magnitude();
			float width = Mathf::Max(size * 0.5f, gap * 0.035f);
			float pulse = 0.85f + 0.15f * Mathf::Sin(now * 1.7f + v.Phase);
			Show(v.Plume, !down);
			Show(v.Intake, !down);
			Show(v.Skimmer, !down);
			Show(v.SkimmerGlow, !down);
			Show(v.Fire, down);
			if (!down)
			{
				Beam(v.Plume, top, bottom, width * pulse);
				v.PlumeMaterial->SetFloat("_Intensity", 0.75f + 0.35f * pulse * static_cast<float>(p->Condition));
				Billboard(v.Intake, bottom, width * 3.2f * pulse);
				// The skimmer's round trip, a minute or so: a slow climb laden, a quick dive, a pause in the clouds scooping.
				float cycle = std::fmod(now / 70.0f + v.Phase / 6.2831853f, 1.0f);
				float h = cycle < 0.45f ? cycle / 0.45f : cycle < 0.6f ? 1.0f : 1.0f - (cycle - 0.6f) / 0.4f;
				h = h * h * (3 - 2 * h);
				Vector3 s = bottom + (top - bottom) * h;
				Vector3 heading = cycle < 0.45f ? (top - bottom) : (bottom - top);
				v.Skimmer->localPosition = s;
				v.Skimmer->localRotation = Quaternion::LookRotation(heading.sqrMagnitude() > 1e-12f ? heading.normalized() : along, along);
				v.Skimmer->localScale = Vector3::one() * (size * 0.18f);
				Billboard(v.SkimmerGlow, s, Mathf::Max(size * 0.45f, width * 0.9f));
			}
			else
			{
				float flicker = 0.6f + 0.4f * Mathf::Sin(now * 9.0f + v.Phase) * Mathf::Sin(now * 5.3f);
				Billboard(v.Fire, at + along * (size * 0.2f), size * 0.7f * flicker);
			}
			// Running lights: a slow white blink so the platform stands out against the clouds; out while it is down.
			Show(v.Beacon, !down);
			if (!down) Billboard(v.Beacon, at + out * (size * 0.55f), size * (std::fmod(now + v.Phase, 2.0f) < 0.25f ? 0.5f : 0.18f));
		}
		for (auto& v : tankers)
		{
			auto t = v.T;
			bool shown = !t->Lost;
			Show(v.Root, shown);
			Show(v.Engine, shown && t->Leg != TankerLeg::Waiting);
			if (!shown) continue;
			int pi = cs::Math::Clamp(t->Platform, 0, v.Op->Platforms.Count() - 1);
			auto home = PlatformScene(app, v.Op, pi);
			double homeSize = PlatformSize(app, v.Op, pi);
			auto st = v.Op->Platforms[pi]->Structure;
			auto def = (*app->Catalog())[v.Op->Body];
			auto ahead = app->BodyScene(v.Op->Body) + scale.LocalOffset(def, st->Orbit->OffsetAt(jd + 1e-4));
			Vector3 along = V(ahead - home);
			along = along.sqrMagnitude() > 1e-12f ? along.normalized() : Vector3::forward();
			float size = static_cast<float>(scale.ShipLength(0.6));
			DVec3 scene;
			Vector3 heading = along;
			if (t->Leg == TankerLeg::Waiting || t->To == nullptr)
			{
				// Astern of its platform, waiting for a full tank.
				scene = home - DVec3(along.x, along.y, along.z) * (homeSize * 1.4 + size * (1.2 + (t->Seed % 3)));
			}
			else
			{
				auto port = app->BodyScene(t->To->Body);
				double f = t->Progress(jd);
				f = f * f * (3 - 2 * f);
				if (t->Leg == TankerLeg::Returning) f = 1 - f;
				scene = home + (port - home) * f;
				Vector3 dir = V(port - home);
				if (dir.sqrMagnitude() > 1e-12f) heading = t->Leg == TankerLeg::Outbound ? dir.normalized() : -dir.normalized();
			}
			Vector3 at = app->Relative(scene);
			v.Root->localPosition = at;
			v.Root->localRotation = Quaternion::LookRotation(heading, Vector3::up());
			v.Root->localScale = Vector3::one() * size;
			if (t->Leg != TankerLeg::Waiting) Billboard(v.Engine, at - heading * (size * 0.6f), size * 0.8f);
		}
	}
}
