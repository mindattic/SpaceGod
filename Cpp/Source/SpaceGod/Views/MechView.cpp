// US-T83: the walking mech drawn (see MechView.h).
#include "Views/MechView.h"

#include "UnitySpaceApp.h"
#include "Rendering/Materials.h"
#include "Rendering/ShipModels.h"

#include "Combat/Battle.h"
#include "Combat/CombatState.h" // US-T270: the hull's spec, for its plating
#include "Combat/War.h"
#include "Embodiment/MechWalker.h"
#include "World.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using Rendering::ShipModels;

	namespace
	{
		struct MechVertex
		{
			int16_t x, y, z;
			int8_t nx, ny, nz;
			uint8_t c;
		};

		struct MechColour
		{
			uint8_t r, g, b;
		};

		struct MechPartData
		{
			const char* Name;
			const MechVertex* Vertices;
			int VertexCount;
			const uint16_t* Indices;
			int IndexCount;
		};

#include "Rendering/MechMeshData.inl"

		// The rig, from Unreal.Soulslike's mech_rig.json (SoulsMech.cpp, MechRig): Unreal space, cm, +X forward, +Y right,
		// +Z up, the origin on the ground under the hips. Converted to the view's frame (Unity axes, one unit the mech's
		// height) by U().
		constexpr float HeightCm = 720.0f;
		const Vector3 Pelvis(-17.5f, 0.0f, 300.9f);
		const Vector3 Torso(-17.5f, 0.0f, 363.1f);
		const Vector3 Canopy(0.3f, 0.0f, 547.0f);
		const Vector3 Arm[2] = { Vector3(-13.8f, -77.7f, 534.7f), Vector3(-13.8f, 77.7f, 534.7f) };
		const Vector3 Hip[2] = { Vector3(-17.5f, -105.6f, 300.9f), Vector3(-17.5f, 105.6f, 300.9f) };
		const Vector3 Knee[2] = { Vector3(109.4f, -105.6f, 205.0f), Vector3(109.4f, 105.6f, 205.0f) };
		const Vector3 Ankle[2] = { Vector3(-62.2f, -105.6f, 135.4f), Vector3(-62.2f, 105.6f, 135.4f) };
		const Vector3 Foot[2] = { Vector3(94.4f, -105.6f, 10.5f), Vector3(94.4f, 105.6f, 10.5f) };
		constexpr float FootFloor = -12.6f; // the sole below the foot's pivot
		const Vector3 Muzzle[2] = { Vector3(265.3f, -36.9f, -146.2f), Vector3(265.3f, 36.9f, -146.2f) }; // in the arm's frame
		const Vector3 JetNozzle[2] = { Vector3(-95.0f, -55.0f, 150.0f), Vector3(-95.0f, 55.0f, 150.0f) };  // in the torso's frame
		constexpr float StandRaise = 45.0f;
		constexpr float StrideCm = static_cast<float>(MechWalker::StrideKm * 1e5);

		/// Unreal cm (x forward, y right, z up) to the view's frame (x right, y up, z forward), in mech heights.
		Vector3 U(Vector3 ue) { return Vector3(ue.y, ue.z, ue.x) / HeightCm; }

		/// The bird leg as a pantograph (SoulsMech.cpp, MechRig::Leg): the metatarsal stays (nearly) parallel to the thigh, so
		/// thigh + metatarsal act as one long segment of length C at a fixed angle PhiC off the thigh, and the leg solves as two.
		struct LegConstants
		{
			float L1 = 0, L2 = 0, L3 = 0;
			float ThighRest = 0, ShinRest = 0, MetaRest = 0;
			float Delta = 0, C = 0, PhiC = 0, Bend = 1;
		};

		float PlanarAngle(Vector3 v) { return std::atan2(v.z, v.x); }

		float DeltaAngle(float from, float to)
		{
			float d = std::fmod(to - from + Mathf::PI, 2 * Mathf::PI);
			if (d < 0) d += 2 * Mathf::PI;
			return d - Mathf::PI;
		}

		const LegConstants& Leg()
		{
			static const LegConstants k = []
			{
				LegConstants r;
				Vector3 u = Knee[0] - Hip[0], v = Ankle[0] - Knee[0], w = Foot[0] - Ankle[0];
				r.L1 = u.magnitude(); r.L2 = v.magnitude(); r.L3 = w.magnitude();
				r.ThighRest = PlanarAngle(u); r.ShinRest = PlanarAngle(v); r.MetaRest = PlanarAngle(w);
				r.Delta = r.MetaRest - r.ThighRest;
				float cx = r.L1 + r.L3 * std::cos(r.Delta), cz = r.L3 * std::sin(r.Delta);
				r.C = std::sqrt(cx * cx + cz * cz);
				r.PhiC = std::atan2(cz, cx);
				Vector3 t = Foot[0] - Hip[0];
				r.Bend = DeltaAngle(PlanarAngle(t), r.ThighRest + r.PhiC) >= 0 ? 1.0f : -1.0f;
				return r;
			}();
			return k;
		}

		/// A leg segment's frame (Unreal space): pitched up by <paramref name="pitch"/> radians (X toward Z) off its rest, then
		/// the leg's plane swung out about X by <paramref name="roll"/>; as the view's rotation.
		Quaternion Segment(float pitch, float roll)
		{
			auto rollX = [roll](Vector3 v) { return Vector3(v.x, v.y * std::cos(roll) - v.z * std::sin(roll), v.y * std::sin(roll) + v.z * std::cos(roll)); };
			Vector3 fwd = rollX(Vector3(std::cos(pitch), 0, std::sin(pitch)));
			Vector3 up = rollX(Vector3(-std::sin(pitch), 0, std::cos(pitch)));
			return Quaternion::LookRotation(Vector3(fwd.y, fwd.z, fwd.x), Vector3(up.y, up.z, up.x));
		}

		Vector3 RollAbout(Vector3 v, float roll)
		{
			return Vector3(v.x, v.y * std::cos(roll) - v.z * std::sin(roll), v.y * std::sin(roll) + v.z * std::cos(roll));
		}

		double MoveToward(double value, double target, double step)
		{
			return value < target ? std::min(target, value + step) : std::max(target, value - step);
		}

		float Ease(float s) { return s * s * (3 - 2 * s); }
	}

	MechView::MechView(UnitySpaceApp* app_, const cs::Ref<Transform>& parent_) : app(app_), parent(parent_)
	{
		paint.SetColor("_Tint", Color(1.0f, 1.0f, 1.0f));
		BuildMeshes();
		fx = NewGameObject("Mech jets and muzzles");
		fx->transform->SetParent(parent, false);
		fxMesh = Materials::Own(cs::New<Mesh>("Mech fx"));
		fxMesh->MarkDynamic();
		AddComponent<MeshFilter>(fx)->sharedMesh = fxMesh;
		auto mr = AddComponent<MeshRenderer>(fx);
		mr->SetSharedMaterial(Materials::Create(Materials::Fx(), "MechFx"));
		mr->shadowCastingMode = ShadowCastingMode::Off;
		mr->receiveShadows = false;
		fx->SetActive(false);
	}

	MechView::~MechView()
	{
		for (auto& r : rigs)
			if (r.Root != nullptr) Destroy(r.Root->gameObject());
		if (fx != nullptr) Destroy(fx);
	}

	void MechView::BuildMeshes()
	{
		for (int p = 0; p < 13; p++)
		{
			const MechPartData& d = MechParts[p];
			std::vector<Vector3> v, n;
			std::vector<Color> c;
			std::vector<int> t;
			v.reserve(d.VertexCount);
			n.reserve(d.VertexCount);
			c.reserve(d.VertexCount);
			for (int i = 0; i < d.VertexCount; i++)
			{
				const MechVertex& mv = d.Vertices[i];
				v.push_back(Vector3(mv.x, mv.y, mv.z) / (10.0f * HeightCm)); // tenths of a cm, to mech heights
				n.push_back(Vector3(mv.nx, mv.ny, mv.nz).normalized());
				const MechColour& col = MechPalette[mv.c];
				c.push_back(Color(col.r / 255.0f, col.g / 255.0f, col.b / 255.0f));
			}
			t.reserve(d.IndexCount);
			for (int i = 0; i < d.IndexCount; i++) t.push_back(d.Indices[i]);
			auto mesh = Materials::Own(cs::New<Mesh>(cs::Cat("Mech ", d.Name)));
			mesh->SetVertices(v);
			mesh->SetNormals(n);
			mesh->SetColors(c);
			mesh->SetTriangles(t, 0);
			meshes[p] = mesh;
		}
	}

	MechView::Rig MechView::MakeRig()
	{
		Rig r;
		auto go = NewGameObject("Mech");
		go->transform->SetParent(parent, false);
		r.Root = go->transform;
		auto part = [&](int p, const cs::Ref<Transform>& under)
		{
			auto g = NewGameObject(cs::Cat("Mech ", MechParts[p].Name));
			g->transform->SetParent(under, false);
			AddComponent<MeshFilter>(g)->sharedMesh = meshes[p];
			auto mr = AddComponent<MeshRenderer>(g);
			mr->SetSharedMaterial(ShipModels::ProceduralMaterial());
			mr->shadowCastingMode = ShadowCastingMode::Off;
			mr->receiveShadows = false;
			mr->SetPropertyBlock(paint);
			r.Parts[p] = g->transform;
		};
		part(0, r.Root);           // pelvis
		part(1, r.Parts[0]);       // torso, on the pelvis
		part(2, r.Parts[1]);       // canopy, on the torso
		part(3, r.Parts[1]);       // arms, on the torso
		part(4, r.Parts[1]);
		for (int p = 5; p < 13; p++) part(p, r.Root); // legs, solved in the chassis's frame
		r.Parts[0]->localPosition = U(Pelvis);
		r.Parts[1]->localPosition = U(Torso - Pelvis);
		r.Parts[2]->localPosition = U(Canopy - Torso);
		r.Parts[3]->localPosition = U(Arm[0] - Torso);
		r.Parts[4]->localPosition = U(Arm[1] - Torso);
		go->SetActive(false);
		return r;
	}

	void MechView::Update()
	{
		cs::List<cs::Ref<MechWalker>> mechs;
		if (app->World() != nullptr && app->World()->War != nullptr)
			for (const auto& b : app->World()->War->Battles)
				if (b != nullptr && !b->Over)
					for (const auto& m : MechWalker::DeployedIn(b)) mechs.Add(m);
		while (rigs.Count() < mechs.Count()) rigs.Add(MakeRig());
		vertices.Clear();
		colors.Clear();
		uvs.Clear();
		indices.Clear();
		float dt = Time::deltaTime;
		std::unordered_map<const MechWalker*, Anim> kept;
		for (int i = 0; i < rigs.Count(); i++)
		{
			bool shown = false;
			if (i < mechs.Count())
			{
				auto& m = mechs[i];
				Anim a = anims.count(m.get()) ? anims[m.get()] : Anim{ 0, 1, m->GaitPhase };
				DVec3 scene, up, forward;
				double height = 0;
				if (app->MechPose(m, scene, up, forward, height))
				{
					auto& root = rigs[i].Root;
					float h = static_cast<float>(height);
					root->localPosition = app->Relative(scene);
					root->localRotation = Quaternion::LookRotation(Vector3(static_cast<float>(forward.X), static_cast<float>(forward.Y), static_cast<float>(forward.Z)),
						Vector3(static_cast<float>(up.X), static_cast<float>(up.Y), static_cast<float>(up.Z)));
					root->localScale = Vector3(h, h, h);
					Pose(rigs[i], m, a, dt);
					shown = true;
				}
				kept[m.get()] = a;
			}
			auto go = rigs[i].Root->gameObject();
			if (go->activeSelf != shown) go->SetActive(shown);
		}
		anims.swap(kept);
		bool lit = indices.Count() > 0;
		if (fx->activeSelf != lit) fx->SetActive(lit);
		if (!lit) return;
		fxMesh->Clear();
		fxMesh->SetVertices(vertices);
		fxMesh->SetColors(colors);
		fxMesh->SetUVs(0, uvs);
		fxMesh->SetIndices(indices, MeshTopology::Triangles, 0, false);
		fxMesh->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 1e9f));
	}

	void MechView::Pose(Rig& rig, const cs::Ref<MechWalker>& m, Anim& a, float dt)
	{
		bool onHull = m->OnHull();
		float speedFrac = onHull ? static_cast<float>(std::min(1.0, std::abs(m->Speed) / MechWalker::MaxForwardKmS)) : 0.0f;
		bool moving = onHull && m->GaitPhase - a.LastPhase > 1e-7;
		a.LastPhase = m->GaitPhase;
		a.Activity = MoveToward(a.Activity, moving ? 1.0 : 0.0, dt * 3.0);
		if (std::abs(m->Speed) > 1e-6) a.Sign = m->Speed > 0 ? 1 : -1;

		// The feet (US-T270): on a hull each plants on the plating and stays there while the body walks over it, then steps by
		// the walk cycle (the legs half a cycle apart) to a spot ahead found on the plating; tucked up in flight. Worked in the
		// hull's frame at the drawn scale: a drawn cm is MechHeightLengths / HeightCm hull lengths.
		float lift = (38.0f + 34.0f * speedFrac) * static_cast<float>(a.Activity);
		Vector3 foot[2];
		float swing[2] = { -1, -1 };
		float footPitch[2] = { 0, 0 };
		float lowest = 0;
		auto hull = onHull ? m->Hull : nullptr;
		auto spec = hull != nullptr && hull->Combat != nullptr ? hull->Combat->Spec : nullptr;
		const double cm = UnitySpaceApp::MechHeightLengths / HeightCm;
		DVec3 roof = onHull ? m->RoofLocal() : DVec3(0, 1, 0);
		DVec3 ahead = m->Legs, right = DVec3::Cross(roof, ahead);
		auto toHull = [&](Vector3 c) { return MechWalker::Plating(spec, m->Local + (ahead * c.x + right * c.y + roof * c.z) * cm); };
		auto toRig = [&](DVec3 p)
		{
			auto d = (p - m->Local) / cm;
			return Vector3(static_cast<float>(DVec3::Dot(d, ahead)), static_cast<float>(DVec3::Dot(d, right)), static_cast<float>(DVec3::Dot(d, roof)));
		};
		// Half a stride as the body covers the plating at the drawn scale (a mech drawn larger than life takes shorter steps for
		// its size), kept within the legs' reach.
		float amp = 0;
		if (hull != nullptr && hull->LengthKm > 0)
			amp = Mathf::Clamp(static_cast<float>(0.25 * MechWalker::StrideKm / hull->LengthKm / cm), StrideCm * 0.04f, StrideCm * 0.3f);
		if (hull.get() != a.HullWas) a.Planted[0] = a.Planted[1] = a.Swinging[0] = a.Swinging[1] = false;
		a.HullWas = hull.get();
		for (int leg = 0; leg < 2; leg++)
		{
			Vector3 home(Foot[leg].x, Foot[leg].y, 0);
			if (!onHull)
			{
				foot[leg] = home + Vector3(-50.0f, 0, -FootFloor + 120.0f + (leg == 0 ? 0.0f : 25.0f));
				footPitch[leg] = -20;
				continue;
			}
			double t = m->GaitPhase + leg * 0.5;
			float f = static_cast<float>(t - std::floor(t));
			bool stepping = f >= 0.5f && a.Activity > 0.02;
			if (!a.Planted[leg])
			{
				a.Plant[leg] = toHull(home);
				a.Planted[leg] = true;
				a.Swinging[leg] = false;
			}
			if (stepping && !a.Swinging[leg])
			{
				a.Swinging[leg] = true;
				a.From[leg] = a.Plant[leg];
				a.To[leg] = toHull(home + Vector3(amp * static_cast<float>(a.Sign * a.Activity), 0, 0));
			}
			else if (!stepping && a.Swinging[leg])
			{
				a.Swinging[leg] = false;
				a.Plant[leg] = a.To[leg];
			}
			if (!a.Swinging[leg])
			{
				// Left behind out of reach (a turn on the spot, a shove): it is set down again under the hip.
				Vector3 at = toRig(a.Plant[leg]);
				if ((Vector3(at.x, at.y, 0) - home).magnitude() > StrideCm * 0.6f) a.Plant[leg] = toHull(home);
			}
			DVec3 sole;
			float raise = 0;
			if (a.Swinging[leg])
			{
				float s = Mathf::Clamp((f - 0.5f) * 2, 0.0f, 1.0f);
				swing[leg] = s;
				sole = MechWalker::Plating(spec, a.From[leg] + (a.To[leg] - a.From[leg]) * Ease(s));
				raise = lift * std::sin(Mathf::PI * s);
				footPitch[leg] = 14.0f * std::sin(Mathf::PI * std::min(s * 1.2f, 1.0f)) * static_cast<float>(a.Activity);
			}
			else sole = a.Plant[leg];
			Vector3 at = toRig(sole);
			lowest = std::min(lowest, at.z);
			foot[leg] = at + Vector3(0, 0, -FootFloor + raise);
			// The sole laid along the plating's slope under it, toe down where the hull falls away ahead.
			DVec3 nf = MechWalker::PlatingNormal(spec, sole);
			footPitch[leg] -= static_cast<float>(std::atan2(DVec3::Dot(nf, ahead), DVec3::Dot(nf, roof)) * 57.29577951308232);
		}
		// The hips: bob and roll over the planted foot, lean into the walk, drop under a landing.
		float bob = 0, roll = 0, sway = 0;
		for (int leg = 0; leg < 2; leg++)
		{
			if (swing[leg] < 0) continue;
			float s = std::sin(Mathf::PI * swing[leg]) * static_cast<float>(a.Activity);
			bob += s * (4.0f + 9.0f * speedFrac);
			roll += (leg == 0 ? 1.0f : -1.0f) * s * (1.5f + 1.5f * speedFrac);
			sway += (leg == 0 ? -1.0f : 1.0f) * s * 2.5f * speedFrac;
		}
		float landing = onHull && m->SinceLanding < 0.5 ? 1.0f - static_cast<float>(m->SinceLanding / 0.5) : 0.0f;
		// US-T270: settled toward the lower foot where the plating falls away under it.
		float hipZ = StandRaise - 22.0f * speedFrac - 75.0f * landing * landing + bob + 0.8f * lowest;
		float lean = onHull ? -(speedFrac * 3.0f) * static_cast<float>(a.Sign) - 6.0f * landing : -8.0f;
		Vector3 pelvisLoc = Pelvis + Vector3(0, 0, hipZ);
		Quaternion pelvisRot = Quaternion::Euler(-lean, sway, -roll);
		rig.Parts[0]->localPosition = U(pelvisLoc);
		rig.Parts[0]->localRotation = pelvisRot;
		Quaternion torsoRot = Quaternion::Euler(lean * 0.6f, static_cast<float>(m->TorsoYawDeg), roll * 0.7f);
		rig.Parts[1]->localRotation = torsoRot;
		float recoil[2];
		Quaternion armRot[2];
		for (int arm = 0; arm < 2; arm++)
		{
			recoil[arm] = static_cast<float>(m->MuzzleFlash[arm] / 0.06);
			rig.Parts[3 + arm]->localPosition = U(Arm[arm] - Torso - Vector3(recoil[arm] * 22.0f, 0, 0));
			armRot[arm] = Quaternion::Euler(-(static_cast<float>(m->AimPitchDeg) + recoil[arm] * 3.0f), 0, 0);
			rig.Parts[3 + arm]->localRotation = armRot[arm];
		}

		// The legs, each solved to its foot (SoulsMech.cpp, TickPose), in the chassis's frame.
		const LegConstants& k = Leg();
		for (int leg = 0; leg < 2; leg++)
		{
			Vector3 hipC = pelvisLoc + (Hip[leg] - Pelvis);
			Vector3 d = foot[leg] - hipC;
			// Swing the leg's plane out about the forward axis so it holds the foot (hip abduction).
			float down = std::max(-d.z, 1.0f);
			float phi = std::atan2(d.y, down);
			float tx = d.x, tz = -std::sqrt(d.y * d.y + d.z * d.z) * (d.z <= 0 ? 1.0f : -1.0f);
			float dist = Mathf::Clamp(std::sqrt(tx * tx + tz * tz), std::abs(k.C - k.L2) + 1.0f, k.C + k.L2 - 1.0f);
			float beta = std::atan2(tz, tx);
			float cosA = Mathf::Clamp((k.C * k.C + dist * dist - k.L2 * k.L2) / (2 * k.C * dist), -1.0f, 1.0f);
			float gamma = beta + k.Bend * std::acos(cosA);
			float uAng = gamma - k.PhiC;
			float wAng = uAng + k.Delta;
			float cvx = k.C * std::cos(gamma), cvz = k.C * std::sin(gamma);
			float vx = (dist * std::cos(beta) - cvx) / k.L2, vz = (dist * std::sin(beta) - cvz) / k.L2;
			float vAng = std::atan2(vz, vx);
			Vector3 kneeP(k.L1 * std::cos(uAng), 0, k.L1 * std::sin(uAng));
			Vector3 ankleP = kneeP + Vector3(k.L2 * vx, 0, k.L2 * vz);
			Vector3 footP = ankleP + Vector3(k.L3 * std::cos(wAng), 0, k.L3 * std::sin(wAng));
			auto& p = rig.Parts;
			int base = 5 + leg * 4;
			p[base + 0]->localPosition = U(hipC);
			p[base + 0]->localRotation = Segment(uAng - k.ThighRest, phi);
			p[base + 1]->localPosition = U(hipC + RollAbout(kneeP, phi));
			p[base + 1]->localRotation = Segment(vAng - k.ShinRest, phi);
			p[base + 2]->localPosition = U(hipC + RollAbout(ankleP, phi));
			p[base + 2]->localRotation = Segment(wAng - k.MetaRest, phi);
			p[base + 3]->localPosition = U(hipC + RollAbout(footP, phi));
			p[base + 3]->localRotation = Quaternion::Euler(-footPitch[leg], 0, 0);
		}

		// The jets and the muzzle flashes: camera-facing quads, placed through the rig in the system root's frame.
		bool firing = m->MuzzleFlash[0] > 0 || m->MuzzleFlash[1] > 0;
		if (!m->JetsLit && !firing) return;
		auto& root = rig.Root;
		float h = root->localScale.x;
		auto toScene = [&](Vector3 rootLocal) { return root->localPosition + root->localRotation * (rootLocal * h); };
		Vector3 torsoAt = U(pelvisLoc) + pelvisRot * U(Torso - Pelvis);
		Quaternion torsoFrame = pelvisRot * torsoRot;
		auto cam = app->UnityCamera->transform;
		float now = Time::unscaledTime;
		if (m->JetsLit)
		{
			Vector3 back = (root->localRotation * (torsoFrame * U(Vector3(-0.45f, 0, -1)).normalized())).normalized();
			for (int j = 0; j < 2; j++)
			{
				Vector3 nozzle = toScene(torsoAt + torsoFrame * U(JetNozzle[j]));
				float flicker = 0.8f + 0.2f * Mathf::Sin(now * 47.0f + j * 1.7f);
				Sprite(nozzle, h * 0.16f * flicker, Color(0.55f, 0.75f, 1.0f), cam->right(), cam->up());
				for (int k2 = 1; k2 <= 4; k2++)
				{
					float f = k2 / 4.0f;
					Sprite(nozzle + back * (h * 0.22f * f), h * (0.13f - 0.02f * k2) * flicker, Color(1.0f, 0.55f + 0.1f * k2, 0.2f) * (1 - f * 0.6f), cam->right(), cam->up());
				}
			}
		}
		for (int arm = 0; arm < 2; arm++)
		{
			if (m->MuzzleFlash[arm] <= 0) continue;
			Vector3 armAt = torsoAt + torsoFrame * U(Arm[arm] - Torso);
			Vector3 tip = toScene(armAt + torsoFrame * (armRot[arm] * U(Muzzle[arm])));
			float f = static_cast<float>(m->MuzzleFlash[arm] / 0.06);
			Sprite(tip, h * 0.12f * f, Color(1.0f, 0.8f, 0.45f), cam->right(), cam->up());
			Sprite(tip, h * 0.05f * f, Color::white(), cam->right(), cam->up());
		}
	}

	void MechView::Sprite(Vector3 c, float radius, Color color, Vector3 right, Vector3 up)
	{
		right = right * radius;
		up = up * radius;
		int i = vertices.Count();
		vertices.Add(c - right - up);
		vertices.Add(c + right - up);
		vertices.Add(c + right + up);
		vertices.Add(c - right + up);
		for (int k = 0; k < 4; k++) colors.Add(color);
		// The Fx shader's soft round spot (BattleFx::Shape 0).
		uvs.Add(Vector3(0, 0, 0));
		uvs.Add(Vector3(1, 0, 0));
		uvs.Add(Vector3(1, 1, 0));
		uvs.Add(Vector3(0, 1, 0));
		indices.Add(i); indices.Add(i + 1); indices.Add(i + 2);
		indices.Add(i); indices.Add(i + 2); indices.Add(i + 3);
	}
}
