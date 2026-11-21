// US-T165: the attitude (RCS) thrusters on a flown hull, firing as it swings.
#include "Views/RcsJets.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Views/HullSurface.h"

#include "HAL/IConsoleManager.h"

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using Rendering::MeshFactory;

	namespace
	{
		cs::Ref<Mesh> jetMesh;
		cs::Ref<Material> jetMaterial;

		/// <summary>The spin's change below this share of full reads as nothing: a steady swing is quiet.</summary>
		constexpr float DeadShare = 0.06f;

		/// <summary>How fast a jet dies away once its push is no longer wanted, 1/s: short, so a pulse still reads.</summary>
		constexpr float FadePerS = 9.0f;

		/// <summary>How fast the smoothed spin change follows the raw one, 1/s.</summary>
		constexpr float AccelFollowHz = 25.0f;
	}

	cs::Ref<Material> RcsJets::JetMaterial()
	{
		if (Alive(jetMaterial)) return jetMaterial;
		jetMaterial = Materials::MarkShared(Materials::Create(Materials::Glow(), "RcsJet"));
		jetMaterial->SetColor("_Color", Color(0.85f, 0.93f, 1.0f));
		jetMaterial->SetFloat("_Intensity", 1.8f);
		jetMaterial->SetFloat("_Falloff", 1.4f);
		return jetMaterial;
	}

	RcsJets::RcsJets(const cs::Ref<Transform>& parent_)
		: parent(parent_)
	{
		if (!Alive(jetMesh)) jetMesh = MeshFactory::Plume();
		// US-T433: the hull as its renderers draw it under the root (a kit's imported parts by their own triangles), not a
		// box: every jet goes on plating that faces the way it fires. A hull with nothing drawn yet is a nominal fighter's box.
		cs::List<cs::Ref<Renderer>> hull;
		if (parent && parent->gameObject())
			for (const auto& r : GetComponentsInChildren<Renderer>(parent->gameObject(), true))
				if (r && r->gameObject() && r->gameObject()->name != "RcsJet") hull.Add(r);
		HullSurface surface(parent, hull);
		Vector3 c = surface.Centre(), mn = surface.Min(), mx = surface.Max();
		float bow = c.z + (mx.z - c.z) * 0.8f, stern = c.z + (mn.z - c.z) * 0.8f;
		float jet = Mathf::Clamp((mx - mn).magnitude() * 0.035f, 0.02f, 0.06f);
		auto place = [&](float z, Vector3 side, Vector3 exhaust, float sideways, float pitch, float yaw, float roll)
		{
			Vector3 at, normal;
			if (surface.Place(z, side, exhaust, sideways, jet * 0.5f, at, normal)) Add(at, exhaust, pitch, yaw, roll, jet);
		};
		// The bow and stern quads: a pitch or a yaw is a couple, the bow pushed one way and the stern the other.
		// Exhaust down at the bow pushes the nose up; exhaust up at the stern does too.
		place(bow, Vector3::down(), Vector3::down(), 0.6f, 1, 0, 0);
		place(bow, Vector3::up(), Vector3::up(), 0.6f, -1, 0, 0);
		place(stern, Vector3::up(), Vector3::up(), 0.6f, 1, 0, 0);
		place(stern, Vector3::down(), Vector3::down(), 0.6f, -1, 0, 0);
		// Exhaust to port at the bow pushes the nose to starboard; exhaust to starboard at the stern does too.
		place(bow, Vector3::left(), Vector3::left(), 0.6f, 0, 1, 0);
		place(bow, Vector3::right(), Vector3::right(), 0.6f, 0, -1, 0);
		place(stern, Vector3::right(), Vector3::right(), 0.6f, 0, 1, 0);
		place(stern, Vector3::left(), Vector3::left(), 0.6f, 0, -1, 0);
		// The beams: a clockwise roll (from behind) lifts the port side and drops the starboard; each jet on the beam's own
		// top or underside, as far out as it reaches.
		place(c.z, Vector3::left(), Vector3::down(), 0.0f, 0, 0, 1);
		place(c.z, Vector3::left(), Vector3::up(), 0.0f, 0, 0, -1);
		place(c.z, Vector3::right(), Vector3::up(), 0.0f, 0, 0, 1);
		place(c.z, Vector3::right(), Vector3::down(), 0.0f, 0, 0, -1);
	}

	void RcsJets::Add(Vector3 at, Vector3 exhaust, float pitch, float yaw, float roll, float size)
	{
		auto go = NewGameObject("RcsJet");
		go->transform->SetParent(parent, false);
		// A hair off the plating, so the flame's root is not lost in it.
		go->transform->localPosition = at + exhaust * 0.004f;
		// The plume mesh trails along its own -Z from the nozzle: turned so it trails along the exhaust.
		go->transform->localRotation = Quaternion::LookRotation(-exhaust, Mathf::Abs(exhaust.y) > 0.9f ? Vector3::forward() : Vector3::up());
		go->transform->localScale = Vector3::zero();
		AddComponent<MeshFilter>(go)->sharedMesh = jetMesh;
		auto r = AddComponent<MeshRenderer>(go);
		r->SetSharedMaterial(JetMaterial());
		r->shadowCastingMode = ShadowCastingMode::Off;
		r->receiveShadows = false;
		r->enabled = false;
		Jet j;
		j.T = go->transform;
		j.R = r;
		j.Pitch = pitch;
		j.Yaw = yaw;
		j.Roll = roll;
		j.Width = size;
		j.Length = size * 3.5f;
		j.Phase = static_cast<float>(jets.Count()) * 1.7f;
		jets.Add(j);
	}

	void RcsJets::Update(float pitchUp, float yawRight, float rollClockwise, float fullAccel, float intensity, bool show, float dt, float now)
	{
		if (!show || !(dt > 0))
		{
			if (!show) Hide();
			return;
		}
		if (hasLast)
		{
			float k = 1 - Mathf::Exp(-dt * AccelFollowHz);
			accPitch += ((pitchUp - lastPitch) / dt - accPitch) * k;
			accYaw += ((yawRight - lastYaw) / dt - accYaw) * k;
			accRoll += ((rollClockwise - lastRoll) / dt - accRoll) * k;
		}
		lastPitch = pitchUp;
		lastYaw = yawRight;
		lastRoll = rollClockwise;
		hasLast = true;
		float full = fullAccel > 1e-6f ? fullAccel : 1.0f;
		firing = 0;
		// sg.ThrustersLit (HullThrusters): every jet lit, to see where each sits.
		static IConsoleVariable* allLit = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.ThrustersLit"));
		bool all = allLit != nullptr && allLit->GetInt() != 0;
		for (auto& j : jets)
		{
			float push = (j.Pitch * accPitch + j.Yaw * accYaw + j.Roll * accRoll) / full;
			float want = all ? 1.0f : Mathf::Clamp01((push - DeadShare) / (1 - DeadShare));
			j.Level = want > j.Level ? want : Mathf::Max(want, j.Level - FadePerS * dt);
			firing = Mathf::Max(firing, want);
			bool on = j.Level > 0.02f;
			j.R->enabled = on;
			if (!on)
			{
				j.T->localScale = Vector3::zero();
				continue;
			}
			// A thruster's flame flickers; it lengthens with the push, and is a stub even at a touch.
			float flicker = 0.82f + 0.18f * Mathf::Sin(now * 57.0f + j.Phase) * Mathf::Sin(now * 23.0f + j.Phase * 2.3f);
			float lit = Mathf::Clamp01(intensity);
			float length = j.Length * (0.3f + 0.7f * j.Level) * flicker * (0.6f + 0.4f * lit);
			float width = j.Width * (0.6f + 0.4f * j.Level) * (0.7f + 0.3f * lit);
			j.T->localScale = Vector3(width, width, length);
		}
	}

	void RcsJets::Hide()
	{
		for (auto& j : jets)
		{
			j.Level = 0;
			if (j.R) j.R->enabled = false;
			if (j.T) j.T->localScale = Vector3::zero();
		}
		hasLast = false;
		firing = 0;
		accPitch = accYaw = accRoll = 0;
	}
}
