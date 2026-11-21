// US-T446: the 3D half of the possession show (see PossessionFx.h).
#include "Views/PossessionFx.h"

#include "UnitySpaceApp.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using Rendering::MeshFactory;
	using Tune = PossessionTune;

	namespace
	{
		float Smooth(float a, float b, float x)
		{
			float t = Mathf::Clamp01((x - a) / (b - a));
			return t * t * (3 - 2 * t);
		}

		/// <summary>Up from 0 at <paramref name="in"/> to 1 at <paramref name="peak"/>, back to 0 at <paramref name="out"/>.</summary>
		float Bump(float t, float in, float peak, float out)
		{
			return t < peak ? Smooth(in, peak, t) : 1 - Smooth(peak, out, t);
		}

		/// <summary>A fixed 0..1 per mote and channel.</summary>
		float Hash01(int i, int channel)
		{
			uint32_t h = static_cast<uint32_t>(i + 1) * 2654435761u ^ static_cast<uint32_t>(channel + 11) * 2246822519u;
			h ^= h >> 15;
			h *= 2246822519u;
			h ^= h >> 13;
			return static_cast<float>(h & 0xFFFF) / 65535.0f;
		}

		Vector3 Lerp(Vector3 a, Vector3 b, float t) { return a + (b - a) * t; }
	}

	cs::Ref<Mesh>& PossessionFx::quad()
	{
		static cs::Ref<Mesh> value;
		return value;
	}

	PossessionFx::PossessionFx(UnitySpaceApp* app_, const cs::Ref<Transform>& parent_) : app(app_), parent(parent_)
	{
		if (!quad()) quad() = MeshFactory::Quad();
		halo = Quad("Possession rim", haloM, 1.25f);
		sweep = Quad("Possession sheen", sweepM, 1.7f);
		for (int i = 0; i < Tune::MoteCount; i++)
		{
			cs::Ref<Material> m;
			motes.Add(Quad(cs::Cat("Possession mote ", i), m, 2.0f));
			moteM.Add(m);
		}
	}

	cs::Ref<Transform> PossessionFx::Quad(const cs::String& name, cs::Ref<Material>& material, float falloff)
	{
		auto go = NewGameObject(name);
		go->transform->SetParent(parent, false);
		AddComponent<MeshFilter>(go)->sharedMesh = quad();
		auto mr = AddComponent<MeshRenderer>(go);
		// Owned (it goes with the system's views); one per quad, as its brightness changes every frame.
		material = Materials::Create(Materials::Glow(), name);
		material->SetFloat("_Falloff", falloff);
		material->SetFloat("_Intensity", 0.0f);
		mr->SetSharedMaterial(material);
		mr->shadowCastingMode = ShadowCastingMode::Off;
		go->SetActive(false);
		return go->transform;
	}

	void PossessionFx::Show(const cs::Ref<Transform>& t, const cs::Ref<Material>& m, bool on)
	{
		if (t->gameObject()->activeSelf != on) t->gameObject()->SetActive(on);
		if (!on) m->SetFloat("_Intensity", 0.0f);
	}

	void PossessionFx::Update(const PossessionFrame& f)
	{
		MotesLit = 0;
		HaloNow = SweepNow = 0;
		float dist = f.Rel.magnitude();
		bool on = f.Show && f.Radius > 0 && dist > 1e-9f;
		if (!on)
		{
			Show(halo, haloM, false);
			Show(sweep, sweepM, false);
			for (int i = 0; i < motes.Count(); i++) Show(motes[i], moteM[i], false);
			return;
		}
		auto cam = app->UnityCamera->transform;
		auto camFwd = cam->forward(), camUp = cam->up(), camRight = cam->right();
		float ppu = static_cast<float>(app->PixelsPerUnitAtUnitDistance());
		float screen = Mathf::Max(static_cast<float>(Screen::height), 240.0f) / 1080.0f;
		float r = f.Radius;
		float t = f.T;
		float bright = f.Brightness;

		// Where the camera looks at the hull from, and the hull's long axis as the screen sees it (as EmbodiedShine).
		auto d = f.Rel / dist;
		auto fwd = f.Forward.sqrMagnitude() > 1e-12f ? f.Forward.normalized() : camUp;
		auto along = fwd - d * Vector3::Dot(fwd, d);
		float alen = along.magnitude();
		along = alen > 1e-3f ? along / alen : (camUp - d * Vector3::Dot(camUp, d)).normalized();
		auto look = Quaternion::LookRotation(d, along);
		float shimmer = f.Still ? 1.0f : 1.0f + 0.12f * Mathf::Sin(t * 11.0f) + 0.06f * Mathf::Sin(t * 23.0f + 1.3f);

		// ---- the rim glow: just behind the hull (or round the suit), so the silhouette's edge is what lights
		float haloEnv = f.Entering ? Bump(t, Tune::HaloInS, Tune::HaloPeakS, Tune::HaloOutS) : Bump(t, 0.0f, 0.3f, 1.45f) * 0.8f;
		HaloNow = haloEnv;
		Show(halo, haloM, haloEnv > 0.003f);
		if (haloEnv > 0.003f)
		{
			if (f.Person)
			{
				halo->localPosition = f.Rel + d * (r * 0.35f);
				halo->localScale = Vector3(r * 1.6f, r * 1.6f, 1);
			}
			else
			{
				float lengthOnScreen = 2 * r * Mathf::Max(alen, 0.3f);
				halo->localPosition = f.Rel + d * (r * 0.55f);
				halo->localScale = Vector3(r * 0.85f, lengthOnScreen * 1.15f + r * 0.3f, 1);
			}
			halo->SetRotation(look);
			haloM->SetColor("_Color", Tune::Energy);
			haloM->SetFloat("_Intensity", Tune::HaloIntensity * haloEnv * shimmer * bright * (f.Person ? Tune::PersonGlowShare : 1.0f));
		}

		// ---- the sheen: a band of light sliding over the near side of the plating, bow to stern going in, back out leaving
		float s = f.Entering ? (t - Tune::SweepFromS) / (Tune::SweepToS - Tune::SweepFromS) : (t - 0.05f) / 0.9f;
		bool sweeping = !f.Person && s > 0 && s < 1;
		Show(sweep, sweepM, sweeping);
		if (sweeping)
		{
			float u = f.Entering ? Mathf::Lerp(0.92f, -0.92f, s) : Mathf::Lerp(-0.92f, 0.92f, s);
			float perPx = dist / ppu;
			float env = Mathf::Pow(Mathf::Sin(Mathf::PI * s), 1.5f);
			SweepNow = env;
			sweep->localPosition = f.Rel + fwd * (u * r) - d * (r * 0.3f);
			sweep->SetRotation(look);
			sweep->localScale = Vector3(r * 0.75f, Mathf::Max(r * 0.2f, perPx * 4), 1);
			sweepM->SetColor("_Color", Color::Lerp(Tune::EnergyCore, Tune::Energy, 0.35f));
			sweepM->SetFloat("_Intensity", Tune::SweepIntensity * env * bright * (f.Entering ? 1.0f : 0.7f));
		}

		// ---- the motes: from the edge of the god's view into the hull (out of it and fading, letting go)
		float halfFov = Mathf::Max(app->UnityCamera->fieldOfView, 10.0f) * 0.5f * Mathf::Deg2Rad;
		float aspect = Mathf::Max(static_cast<float>(Screen::width), 1.0f) / Mathf::Max(static_cast<float>(Screen::height), 1.0f);
		for (int i = 0; i < motes.Count(); i++)
		{
			const auto& m = motes[i];
			const auto& mm = moteM[i];
			float leave = Tune::MoteFirstS + i * Tune::MoteStaggerS;
			float u = (t - leave) / Tune::MoteCrossS;
			if (f.Still || u <= 0 || u >= 1)
			{
				Show(m, mm, false);
				continue;
			}
			// Fixed per mote: its bearing round the view (golden-angle spaced), how far out it starts, where on the hull it lands.
			float bearing = i * 2.39996f + 0.4f;
			float out = Tune::MoteStartSpread + 0.35f * Hash01(i, 1);
			float landing = Hash01(i, 2) * 2 - 1;
			float curlSign = Hash01(i, 3) < 0.5f ? -1.0f : 1.0f;
			// The start: ahead of the eye, out towards the edge of the view on that bearing.
			float z0 = dist * 0.16f;
			float lateral = z0 * Mathf::Tan(halfFov) * out;
			auto edge = camFwd * z0 + camRight * (Mathf::Cos(bearing) * lateral * aspect) + camUp * (Mathf::Sin(bearing) * lateral);
			// The landing: on the near side of the plating, spread along the hull (round the suit, for a person).
			Vector3 hull = f.Person
				? f.Rel - d * (r * 0.3f) + (camRight * Mathf::Cos(bearing) + camUp * Mathf::Sin(bearing)) * (r * 0.45f)
				: f.Rel + fwd * (landing * r * 0.78f) - d * (r * 0.28f) + Vector3::Cross(d, along) * ((Hash01(i, 4) - 0.5f) * r * 0.25f);
			Vector3 from = f.Entering ? edge : hull;
			Vector3 to = f.Entering ? hull : Lerp(hull, edge, Tune::MoteLeaveReach);
			// Going in it gathers pace into the hull; coming out it slows as it thins away.
			float e = f.Entering ? u * u * (3 - 2 * u) : 1 - (1 - u) * (1 - u);
			auto span = to - from;
			float spanLen = span.magnitude();
			if (spanLen < 1e-9f)
			{
				Show(m, mm, false);
				continue;
			}
			auto perp = Vector3::Cross(span / spanLen, camFwd);
			if (perp.sqrMagnitude() < 1e-8f) perp = camUp;
			perp = perp.normalized() * curlSign;
			auto p = Lerp(from, to, e) + perp * (spanLen * Tune::MoteCurl * Mathf::Sin(Mathf::PI * e));
			// Its tangent, for the trail: the path a little further on.
			float e2 = Mathf::Min(1.0f, e + 0.02f);
			auto p2 = Lerp(from, to, e2) + perp * (spanLen * Tune::MoteCurl * Mathf::Sin(Mathf::PI * e2));
			auto tangent = p2 - p;
			float pd = p.magnitude();
			if (pd < 1e-9f)
			{
				Show(m, mm, false);
				continue;
			}
			auto toMote = p / pd;
			auto onScreen = tangent - toMote * Vector3::Dot(tangent, toMote);
			if (onScreen.sqrMagnitude() < 1e-14f) onScreen = camUp;
			onScreen = onScreen.normalized();
			float perPx = pd / ppu;
			float core = Tune::MotePx * screen * perPx;
			float trail = core * Tune::MoteTrail;
			float alpha = f.Entering
				? Smooth(0.0f, 0.18f, u) * (0.55f + 0.45f * Smooth(0.7f, 0.97f, u)) * (1 - Smooth(0.94f, 1.0f, u))
				: Smooth(0.0f, 0.08f, u) * (1 - Smooth(0.35f, 1.0f, u));
			Show(m, mm, alpha > 0.003f);
			if (alpha <= 0.003f) continue;
			MotesLit++;
			// The streak trails behind the mote's head.
			m->localPosition = p - onScreen * (trail * 0.3f);
			m->SetRotation(Quaternion::LookRotation(toMote, onScreen));
			m->localScale = Vector3(core, trail, 1);
			mm->SetColor("_Color", Color::Lerp(Tune::EnergyCore, Tune::Energy, 0.25f + 0.5f * Hash01(i, 5)));
			mm->SetFloat("_Intensity", Tune::MoteIntensity * alpha * bright);
		}
	}
}
