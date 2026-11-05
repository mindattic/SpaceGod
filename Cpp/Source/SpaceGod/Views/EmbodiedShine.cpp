// US-T136: the shine on a unit another player embodies (see EmbodiedShine.h).
#include "Views/EmbodiedShine.h"

#include "UnitySpaceApp.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using Rendering::MeshFactory;

	namespace
	{
		Vector3 V(DVec3 v) { return Vector3(static_cast<float>(v.X), static_cast<float>(v.Y), static_cast<float>(v.Z)); }

		float Smooth(float a, float b, float x)
		{
			float t = Mathf::Clamp01((x - a) / (b - a));
			return t * t * (3 - 2 * t);
		}

		/// <summary>A stable hash of a sweep's number and the seat, 0..1: which sweeps flare.</summary>
		float Hash01(int n, int seat)
		{
			uint32_t h = static_cast<uint32_t>(n) * 2654435761u ^ static_cast<uint32_t>(seat + 7) * 2246822519u;
			h ^= h >> 15;
			h *= 2246822519u;
			h ^= h >> 13;
			return static_cast<float>(h & 0xFFFF) / 65535.0f;
		}
	}

	cs::Ref<Mesh>& EmbodiedShine::quad()
	{
		static cs::Ref<Mesh> value;
		return value;
	}

	Color EmbodiedShine::SeatColor(int seat)
	{
		switch (((seat % 4) + 4) % 4)
		{
			case 0: return Color(1.0f, 0.76f, 0.28f);  // P1 amber
			case 1: return Color(0.30f, 0.88f, 1.0f);  // P2 cyan
			case 2: return Color(1.0f, 0.40f, 0.86f);  // P3 rose
			default: return Color(0.66f, 1.0f, 0.36f); // P4 lime
		}
	}

	EmbodiedShine::EmbodiedShine(UnitySpaceApp* app_, const cs::Ref<Transform>& parent_) : app(app_), parent(parent_)
	{
		if (!quad()) quad() = MeshFactory::Quad();
	}

	cs::Ref<Transform> EmbodiedShine::Quad(const cs::String& name, cs::Ref<Material>& material, float falloff)
	{
		auto go = NewGameObject(name);
		go->transform->SetParent(parent, false);
		AddComponent<MeshFilter>(go)->sharedMesh = quad();
		auto mr = AddComponent<MeshRenderer>(go);
		// One material per quad (its colour and brightness change every frame); owned, so it goes with the system's views.
		material = Materials::Create(Materials::Glow(), name);
		material->SetFloat("_Falloff", falloff);
		material->SetFloat("_Intensity", 0.0f);
		mr->SetSharedMaterial(material);
		mr->shadowCastingMode = ShadowCastingMode::Off;
		go->SetActive(false);
		return go->transform;
	}

	cs::Ref<EmbodiedShine::Glint> EmbodiedShine::Create(int index)
	{
		auto g = cs::New<Glint>();
		cs::String n = cs::Cat("Shine ", index, " ");
		g->Halo = Quad(n + "rim", g->HaloM, 1.3f);
		g->Sweep = Quad(n + "sweep", g->SweepM, 1.7f);
		g->FlareA = Quad(n + "flare a", g->FlareAM, 1.2f);
		g->FlareB = Quad(n + "flare b", g->FlareBM, 1.2f);
		g->FlareCore = Quad(n + "flare core", g->FlareCoreM, 2.4f);
		g->FarA = Quad(n + "glint a", g->FarAM, 1.1f);
		g->FarB = Quad(n + "glint b", g->FarBM, 1.1f);
		g->FarCore = Quad(n + "glint core", g->FarCoreM, 2.2f);
		return g;
	}

	void EmbodiedShine::Show(const cs::Ref<Transform>& t, const cs::Ref<Material>& m, bool on)
	{
		if (t->gameObject()->activeSelf != on) t->gameObject()->SetActive(on);
		if (!on) m->SetFloat("_Intensity", 0.0f);
	}

	void EmbodiedShine::Hide(const cs::Ref<Glint>& g)
	{
		Show(g->Halo, g->HaloM, false);
		Show(g->Sweep, g->SweepM, false);
		Show(g->FlareA, g->FlareAM, false);
		Show(g->FlareB, g->FlareBM, false);
		Show(g->FlareCore, g->FlareCoreM, false);
		Show(g->FarA, g->FarAM, false);
		Show(g->FarB, g->FarBM, false);
		Show(g->FarCore, g->FarCoreM, false);
	}

	void EmbodiedShine::Update()
	{
		NearCount = FarCount = HiddenCount = 0;
		auto shining = app->ShiningNow();
		auto cam = app->UnityCamera->transform;
		auto camPos = cam->position();
		auto camUp = cam->up();
		float ppu = static_cast<float>(app->PixelsPerUnitAtUnitDistance());
		float screen = Mathf::Max(static_cast<float>(Screen::height), 240.0f) / 1080.0f;
		float now = Time::unscaledTime;
		int used = 0;
		for (const auto& s : shining)
		{
			if (!s.Shows())
			{
				HiddenCount++;
				continue;
			}
			while (pool.Count() <= used) pool.Add(Create(pool.Count()));
			auto g = pool[used++];

			auto rel = app->Relative(s.Centre);
			float r = static_cast<float>(s.Radius);
			float dist = rel.magnitude();
			if (dist < 1e-6f || r <= 0)
			{
				Hide(g);
				continue;
			}
			// Where the camera looks at it from, and the hull's long axis as the screen sees it.
			auto d = rel / dist;
			auto fwd = V(s.Forward);
			if (fwd.sqrMagnitude() < 1e-12f) fwd = camUp;
			fwd = fwd.normalized();
			auto along = fwd - d * Vector3::Dot(fwd, d);
			float alen = along.magnitude();
			along = alen > 1e-3f ? along / alen : (camUp - d * Vector3::Dot(camUp, d)).normalized();
			auto across = Vector3::Cross(d, along).normalized();
			auto color = SeatColor(s.Player.Seat);

			// The beat: each player's own offset, so two shines never pulse in step; frozen for screenshots.
			float offset = s.Player.Seat * 1.73f + (s.Fake ? 0.6f : 0.0f);
			float clock = FreezePhase >= 0 ? FreezePhase * SweepPeriodS : Mathf::Repeat(now + offset, SweepPeriodS);
			int sweepNo = FreezePhase >= 0 ? 0 : Mathf::FloorToInt((now + offset) / SweepPeriodS);
			float sweep = clock / SweepActiveS; // 0..1 while the sweep runs
			bool sweeping = sweep < 1;
			// The flare: where the sweep crests, on about two sweeps in three.
			bool flares = FreezePhase >= 0 || Hash01(sweepNo, s.Player.Seat) < 0.66f;
			float flare = flares && sweep > 0.38f && sweep < 0.78f ? Mathf::Sin(Mathf::PI * (sweep - 0.38f) / 0.4f) : 0.0f;
			flare = flare * flare;

			float px = 2 * r / dist * ppu;
			float nearW = Smooth(FarPx, NearPx, px);
			float farW = 1 - nearW;
			float perPx = dist / ppu; // scene units per pixel at the hull
			auto look = Quaternion::LookRotation(d, along);

			// ---- close up: the rim glow, the sweep and the flare
			bool near = nearW > 0.01f;
			Show(g->Halo, g->HaloM, near);
			Show(g->Sweep, g->SweepM, near && sweeping);
			Show(g->FlareA, g->FlareAM, near && flare > 0.01f);
			Show(g->FlareB, g->FlareBM, near && flare > 0.01f);
			Show(g->FlareCore, g->FlareCoreM, near && flare > 0.01f);
			if (near)
			{
				NearCount++;
				// The rim: just behind the hull, so the hull hides its heart and only the edge of the silhouette is lit.
				float lengthOnScreen = 2 * r * Mathf::Max(alen, 0.3f);
				g->Halo->localPosition = rel + d * (r * 0.55f);
				g->Halo->SetRotation(look);
				g->Halo->localScale = Vector3(r * 0.8f, lengthOnScreen * 1.15f + r * 0.25f, 1);
				float breathe = 0.5f + 0.5f * Mathf::Sin(now * 1.3f + offset);
				g->HaloM->SetColor("_Color", color);
				g->HaloM->SetFloat("_Intensity", (0.32f + 0.12f * breathe) * nearW);

				// The sweep: a band of light sliding bow to stern across the near side of the plating.
				float u = Mathf::Lerp(0.92f, -0.92f, Mathf::Clamp01(sweep));
				auto onHull = rel + fwd * (u * r) - d * (r * 0.32f);
				if (sweeping)
				{
					g->Sweep->localPosition = onHull;
					g->Sweep->SetRotation(look);
					g->Sweep->localScale = Vector3(r * 0.7f, Mathf::Max(r * 0.24f, perPx * 4), 1);
					float env = Mathf::Pow(Mathf::Sin(Mathf::PI * Mathf::Clamp01(sweep)), 1.5f);
					g->SweepM->SetColor("_Color", Color::Lerp(Color::white(), color, 0.3f));
					g->SweepM->SetFloat("_Intensity", 2.4f * env * nearW);
				}
				// The star-glint: a four-pointed star where the sweep crests, at least ~50 px so it pops at any size.
				if (flare > 0.01f)
				{
					float size = Mathf::Max(r * 1.15f, perPx * 50 * screen) * (0.55f + 0.45f * flare);
					auto at = onHull - d * (r * 0.1f);
					auto tilt = Quaternion::AngleAxis(22.0f, d) * across;
					auto armColor = Color::Lerp(Color::white(), color, 0.4f);
					g->FlareA->localPosition = at;
					g->FlareA->SetRotation(Quaternion::LookRotation(d, tilt));
					g->FlareA->localScale = Vector3(size * 0.06f, size, 1);
					g->FlareAM->SetColor("_Color", armColor);
					g->FlareAM->SetFloat("_Intensity", 1.7f * flare * nearW);
					g->FlareB->localPosition = at;
					g->FlareB->SetRotation(Quaternion::LookRotation(d, Vector3::Cross(d, tilt)));
					g->FlareB->localScale = Vector3(size * 0.06f, size * 0.75f, 1);
					g->FlareBM->SetColor("_Color", armColor);
					g->FlareBM->SetFloat("_Intensity", 1.5f * flare * nearW);
					g->FlareCore->localPosition = at;
					g->FlareCore->SetRotation(look);
					g->FlareCore->localScale = Vector3::one() * (size * 0.3f);
					g->FlareCoreM->SetColor("_Color", Color::Lerp(Color::white(), color, 0.15f));
					g->FlareCoreM->SetFloat("_Intensity", 1.8f * flare * nearW);
				}
			}

			// ---- far off: a glint sprite of fixed screen size (scaled with the screen), twinkling, flaring on the beat
			bool far = farW > 0.01f;
			Show(g->FarA, g->FarAM, far);
			Show(g->FarB, g->FarBM, far);
			Show(g->FarCore, g->FarCoreM, far);
			if (far)
			{
				FarCount++;
				float twinkle = 0.85f + 0.15f * Mathf::Sin(now * 3.1f + offset * 2);
				float arm = GlintArmPx * screen * perPx * twinkle * (1 + 0.9f * flare);
				float thick = Mathf::Max(arm * 0.1f, 2.6f * screen * perPx);
				float spin = 18.0f + (FreezePhase >= 0 ? 0.0f : now * 5.0f);
				auto screenUp = (camUp - d * Vector3::Dot(camUp, d)).normalized();
				auto armA = Quaternion::AngleAxis(spin, d) * screenUp;
				auto armB = Quaternion::AngleAxis(spin + 90.0f, d) * screenUp;
				// In front of the hull, so the hull's own model never hides it.
				auto at = rel - d * (r * 1.3f);
				g->FarA->localPosition = at;
				g->FarA->SetRotation(Quaternion::LookRotation(d, armA));
				g->FarA->localScale = Vector3(thick, arm, 1);
				g->FarAM->SetColor("_Color", color);
				g->FarAM->SetFloat("_Intensity", (1.8f + 0.9f * flare) * farW);
				g->FarB->localPosition = at;
				g->FarB->SetRotation(Quaternion::LookRotation(d, armB));
				g->FarB->localScale = Vector3(thick, arm * 0.8f, 1);
				g->FarBM->SetColor("_Color", color);
				g->FarBM->SetFloat("_Intensity", (1.6f + 0.9f * flare) * farW);
				g->FarCore->localPosition = at;
				g->FarCore->SetRotation(look);
				g->FarCore->localScale = Vector3::one() * (GlintCorePx * screen * perPx * (1 + 0.4f * flare));
				g->FarCoreM->SetColor("_Color", Color::Lerp(Color::white(), color, 0.3f));
				g->FarCoreM->SetFloat("_Intensity", 2.2f * farW);
			}
		}
		for (int i = used; i < pool.Count(); i++) Hide(pool[i]);
	}
}
