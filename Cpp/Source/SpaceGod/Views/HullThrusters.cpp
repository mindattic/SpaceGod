// US-T231: every hull's thrusters, placed on its own surface and firing with its thrust, turn, roll and braking.
#include "Views/HullThrusters.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using Rendering::MeshFactory;

	namespace
	{
		cs::Ref<Mesh> plumeMesh;
		cs::Ref<Material> rcsMaterial, mainMaterial;

		/// <summary>The spin's change below this share of full reads as nothing: a steady swing is quiet.</summary>
		constexpr float DeadShare = 0.06f;
		/// <summary>How fast an RCS jet dies away once its push is no longer wanted, 1/s: short, so a pulse still reads.</summary>
		constexpr float FadePerS = 9.0f;
		/// <summary>How fast the smoothed spin change follows the raw one, 1/s.</summary>
		constexpr float AccelFollowHz = 25.0f;
		/// <summary>How fast the main engines and retros light and die, 1/s: a drive spools, it does not blink.</summary>
		constexpr float DriveRisePerS = 5.0f, DriveFadePerS = 3.0f;
		/// <summary>More turn than this in one frame is a jump (a rebuilt view, a time warp), not a spin.</summary>
		constexpr float JumpRad = 25.0f * Mathf::Deg2Rad;
		/// <summary>A jet whose lever gives it less than this share of the strongest's turn about an axis is not used for it.</summary>
		constexpr float LeverShare = 0.3f;
		/// <summary>How finely a hull's triangles are sampled for its surface, root units (the hull is one long).</summary>
		constexpr float SampleStep = 0.03f;

		/// <summary>A point in <paramref name="t"/>'s frame taken up through its parents into <paramref name="root"/>'s.</summary>
		Vector3 IntoRoot(const Transform* t, const Transform* root, Vector3 p)
		{
			while (t != nullptr && t != root)
			{
				p = t->localPosition + t->localRotation * Vector3::Scale(t->localScale, p);
				auto up = t->parent.lock();
				t = up.get();
			}
			return p;
		}

		/// <summary>A box's surface as a grid of points on each face.</summary>
		void SampleBox(Vector3 mn, Vector3 mx, const Transform* t, const Transform* root, std::vector<Vector3>& pts)
		{
			constexpr int n = 4;
			for (int axis = 0; axis < 3; axis++)
				for (int side = 0; side < 2; side++)
					for (int i = 0; i <= n; i++)
						for (int j = 0; j <= n; j++)
						{
							float u = static_cast<float>(i) / n, v = static_cast<float>(j) / n;
							float a[3] = { 0, 0, 0 };
							a[axis] = side;
							a[(axis + 1) % 3] = u;
							a[(axis + 2) % 3] = v;
							Vector3 p(mn.x + (mx.x - mn.x) * a[0], mn.y + (mx.y - mn.y) * a[1], mn.z + (mx.z - mn.z) * a[2]);
							pts.push_back(IntoRoot(t, root, p));
						}
		}

		/// <summary>The hull's surface as points in the root's frame: its meshes' triangles, sampled finely enough that a
		/// broad flat face has points across it (not only at its corners), and an imported part's own box.</summary>
		void SampleHull(const cs::Ref<Transform>& root, const cs::List<cs::Ref<Renderer>>& hull, std::vector<Vector3>& pts)
		{
			for (const auto& r : hull)
			{
				if (!r) continue;
				auto t = r->transform();
				auto mesh = r->DrawnMesh();
				if (!t || !mesh) continue;
				if (mesh->Vertices.empty())
				{
					auto b = mesh->bounds();
					if (b.size().sqrMagnitude() > 0) SampleBox(b.min(), b.max(), t.get(), root.get(), pts);
					continue;
				}
				std::vector<Vector3> v(mesh->Vertices.size());
				for (size_t i = 0; i < v.size(); i++) v[i] = IntoRoot(t.get(), root.get(), mesh->Vertices[i]);
				pts.insert(pts.end(), v.begin(), v.end());
				for (const auto& sub : mesh->SubMeshes)
				{
					if (sub.Topology != MeshTopology::Triangles) continue;
					const auto& ix = sub.Indices;
					for (size_t k = 0; k + 2 < ix.size(); k += 3)
					{
						int ia = ix[k], ib = ix[k + 1], ic = ix[k + 2];
						int count = static_cast<int>(v.size());
						if (ia < 0 || ib < 0 || ic < 0 || ia >= count || ib >= count || ic >= count) continue;
						Vector3 a = v[ia], b = v[ib], c = v[ic];
						float edge = Mathf::Max(Vector3::Distance(a, b), Mathf::Max(Vector3::Distance(b, c), Vector3::Distance(c, a)));
						int steps = static_cast<int>(Mathf::Clamp(Mathf::Ceil(edge / SampleStep), 1.0f, 12.0f));
						if (steps == 1)
						{
							pts.push_back((a + b + c) / 3.0f);
							continue;
						}
						for (int i = 0; i <= steps; i++)
							for (int j = 0; j <= steps - i; j++)
							{
								if ((i == 0 && j == 0) || (i == steps) || (j == steps)) continue; // the corners are in already
								float u = static_cast<float>(i) / steps, w = static_cast<float>(j) / steps;
								pts.push_back(a + (b - a) * u + (c - a) * w);
							}
					}
				}
			}
		}

		/// <summary>
		/// The hull's outermost surface point along <paramref name="d"/> (across the hull, from its keel line) near station
		/// <paramref name="z"/>: the band round the station widens until it holds some of the hull. <paramref name="sideways"/>
		/// keeps it off to the side of that way (a dorsal jet on the spine, not a wingtip).
		/// </summary>
		bool Outermost(const std::vector<Vector3>& pts, Vector3 c, float length, float z, Vector3 d, float sideways, Vector3& at)
		{
			for (float band = 0.04f * length; band <= 0.6f * length; band *= 1.6f)
			{
				float best = Mathf::NegativeInfinity;
				bool any = false;
				for (const auto& p : pts)
				{
					float dz = Mathf::Abs(p.z - z);
					if (dz > band) continue;
					Vector3 r(p.x - c.x, p.y - c.y, 0);
					float along = Vector3::Dot(r, d);
					float side = (r - d * along).magnitude();
					float score = along - sideways * side - 0.3f * dz;
					if (score > best)
					{
						best = score;
						at = p;
						any = true;
					}
				}
				if (any) return true;
			}
			return false;
		}
	}

	cs::Ref<Material> HullThrusters::RcsMaterial()
	{
		if (Alive(rcsMaterial)) return rcsMaterial;
		rcsMaterial = Materials::MarkShared(Materials::Create(Materials::Glow(), "HullRcsJet"));
		rcsMaterial->SetColor("_Color", Color(0.85f, 0.93f, 1.0f));
		rcsMaterial->SetFloat("_Intensity", 1.8f);
		rcsMaterial->SetFloat("_Falloff", 1.4f);
		return rcsMaterial;
	}

	cs::Ref<Material> HullThrusters::MainMaterial()
	{
		if (Alive(mainMaterial)) return mainMaterial;
		mainMaterial = Materials::MarkShared(Materials::Create(Materials::Glow(), "HullMainJet"));
		mainMaterial->SetColor("_Color", Color(0.45f, 0.75f, 1.0f));
		mainMaterial->SetFloat("_Intensity", 1.6f);
		mainMaterial->SetFloat("_Falloff", 1.6f);
		return mainMaterial;
	}

	HullThrusters::HullThrusters(const cs::Ref<Transform>& root_, const cs::List<cs::Ref<Renderer>>& hull, const cs::List<Vector3>& engines)
		: root(root_)
	{
		if (!Alive(plumeMesh)) plumeMesh = MeshFactory::Plume();

		std::vector<Vector3> pts;
		SampleHull(root, hull, pts);
		if (pts.empty())
		{
			// No hull drawn yet (or an odd import): a nominal fighter's box, one long and a third wide.
			SampleBox(Vector3(-0.175f, -0.075f, -0.5f), Vector3(0.175f, 0.075f, 0.5f), nullptr, nullptr, pts);
		}
		Vector3 mn = pts[0], mx = pts[0];
		for (const auto& p : pts)
		{
			mn = Vector3::Min(mn, p);
			mx = Vector3::Max(mx, p);
		}
		Vector3 c = (mn + mx) * 0.5f;
		float length = Mathf::Max(mx.z - mn.z, 0.05f);
		float halfW = Mathf::Max((mx.x - mn.x) * 0.5f, 0.01f), halfH = Mathf::Max((mx.y - mn.y) * 0.5f, 0.01f);
		float rcs = Mathf::Clamp(Mathf::Min(halfW, halfH) * 0.22f + 0.008f, 0.012f, 0.045f);

		// ---- Main engines, on the stern: over the engine hardpoints aft of the middle where it has them, else across its
		// aft face as its shape fits. Each sits on the rearmost plating behind its column, so its plume clears the hull.
		float sternBand = mn.z + 0.07f * length;
		float sMinX = mx.x, sMaxX = mn.x, sMinY = mx.y, sMaxY = mn.y;
		for (const auto& p : pts)
		{
			if (p.z > sternBand) continue;
			sMinX = Mathf::Min(sMinX, p.x);
			sMaxX = Mathf::Max(sMaxX, p.x);
			sMinY = Mathf::Min(sMinY, p.y);
			sMaxY = Mathf::Max(sMaxY, p.y);
		}
		float sw = Mathf::Max(sMaxX - sMinX, 0.01f), sh = Mathf::Max(sMaxY - sMinY, 0.01f);
		Vector3 sc((sMinX + sMaxX) * 0.5f, (sMinY + sMaxY) * 0.5f, mn.z);

		cs::List<Vector3> columns;
		for (const auto& e : engines)
		{
			if (e.z >= c.z) continue;
			bool near = false;
			for (const auto& k : columns)
				if (Vector2::Distance(Vector2(k.x, k.y), Vector2(e.x, e.y)) < 0.02f) near = true;
			if (!near && columns.Count() < 6) columns.Add(e);
		}
		float nozzle;
		if (columns.Count() > 0)
		{
			float gap = sw;
			for (int i = 0; i < columns.Count(); i++)
				for (int j = i + 1; j < columns.Count(); j++)
					gap = Mathf::Min(gap, Vector2::Distance(Vector2(columns[i].x, columns[i].y), Vector2(columns[j].x, columns[j].y)));
			nozzle = Mathf::Clamp(Mathf::Min(gap * 0.75f, Mathf::Min(sw, sh) * 0.6f), 0.03f, 0.2f);
		}
		else
		{
			int across = sw > 2.2f * sh ? (sw > 4.0f * sh ? 3 : 2) : 1;
			int up = sh > 2.2f * sw ? 2 : 1;
			for (int i = 0; i < across; i++)
				for (int j = 0; j < up; j++)
					columns.Add(Vector3(sc.x + (i - (across - 1) * 0.5f) * sw / across, sc.y + (j - (up - 1) * 0.5f) * sh / up, sternBand));
			nozzle = Mathf::Clamp(Mathf::Min(sw / across, sh / up) * 0.55f, 0.03f, 0.2f);
		}
		Vector3 sternSum = Vector3::zero();
		for (const auto& col : columns)
		{
			// The rearmost plating within the nozzle's reach of the column; failing that, the nearest of the stern's own.
			bool found = false;
			float rear = 0;
			for (const auto& p : pts)
			{
				if (Vector2::Distance(Vector2(p.x, p.y), Vector2(col.x, col.y)) > nozzle * 0.6f) continue;
				if (p.z > col.z + 0.08f * length) continue;
				if (!found || p.z < rear) rear = p.z;
				found = true;
			}
			Vector3 at(col.x, col.y, rear);
			if (!found)
			{
				float bestD = Mathf::Infinity;
				for (const auto& p : pts)
				{
					if (p.z > sternBand) continue;
					float d = Vector2::Distance(Vector2(p.x, p.y), Vector2(col.x, col.y));
					if (d < bestD)
					{
						bestD = d;
						at = p;
					}
				}
			}
			Add(Kind::Main, at, Vector3::back(), c, nozzle, Mathf::Clamp(nozzle * 4.5f, 0.12f, 0.7f));
			sternSum += at;
			mains++;
		}
		sternCentre = mains > 0 ? sternSum / static_cast<float>(mains) : sc;

		// ---- RCS pods at the bow's and the stern's four corners: the hull's outermost plating that way at that station.
		float bow = c.z + (mx.z - c.z) * 0.72f, stern = c.z + (mn.z - c.z) * 0.72f;
		for (int end = 0; end < 2; end++)
			for (int sx = -1; sx <= 1; sx += 2)
				for (int sy = -1; sy <= 1; sy += 2)
				{
					Vector3 d = Vector3(sx * halfW, sy * halfH, 0).normalized();
					Vector3 at;
					if (!Outermost(pts, c, length, end == 0 ? bow : stern, d, 0, at)) continue;
					Add(Kind::Rcs, at, Vector3(0, static_cast<float>(sy), 0), c, rcs, rcs * 3.5f);
					Add(Kind::Rcs, at, Vector3(static_cast<float>(sx), 0, 0), c, rcs, rcs * 3.5f);
					// The bow pods carry the retros: they fire forward to brake.
					if (end == 0) Add(Kind::Retro, at, Vector3::forward(), c, rcs * 1.2f, rcs * 5.0f);
				}

		// ---- Roll jets on each flank amidships, up and down.
		for (int sx = -1; sx <= 1; sx += 2)
		{
			Vector3 at;
			if (!Outermost(pts, c, length, c.z, Vector3(static_cast<float>(sx), 0, 0), 0.5f, at)) continue;
			Add(Kind::Rcs, at, Vector3::up(), c, rcs, rcs * 3.5f);
			Add(Kind::Rcs, at, Vector3::down(), c, rcs, rcs * 3.5f);
		}

		// Each axis's turning shares against its strongest jet's: a jet with too short a lever about an axis is not used for it.
		float maxPitch = 0, maxYaw = 0, maxRoll = 0;
		for (const auto& j : jets)
		{
			maxPitch = Mathf::Max(maxPitch, Mathf::Abs(j.Pitch));
			maxYaw = Mathf::Max(maxYaw, Mathf::Abs(j.Yaw));
			maxRoll = Mathf::Max(maxRoll, Mathf::Abs(j.Roll));
		}
		auto share = [](float v, float max) {
			if (!(max > 1e-6f)) return 0.0f;
			float s = v / max;
			return Mathf::Abs(s) < LeverShare ? 0.0f : s;
		};
		for (auto& j : jets)
		{
			j.Pitch = share(j.Pitch, maxPitch);
			j.Yaw = share(j.Yaw, maxYaw);
			j.Roll = share(j.Roll, maxRoll);
		}
	}

	void HullThrusters::Add(Kind kind, Vector3 at, Vector3 exhaust, Vector3 centre, float width, float length)
	{
		auto go = NewGameObject(kind == Kind::Main ? "MainJet" : kind == Kind::Retro ? "RetroJet" : "RcsJet");
		go->transform->SetParent(root, false);
		// A hair off the plating, so the flame's root is not lost in it.
		go->transform->localPosition = at + exhaust * 0.004f;
		// The plume mesh trails along its own -Z from the nozzle: turned so it trails along the exhaust.
		go->transform->localRotation = Quaternion::LookRotation(-exhaust, Mathf::Abs(exhaust.y) > 0.9f ? Vector3::forward() : Vector3::up());
		go->transform->localScale = Vector3::zero();
		AddComponent<MeshFilter>(go)->sharedMesh = plumeMesh;
		auto r = AddComponent<MeshRenderer>(go);
		r->SetSharedMaterial(kind == Kind::Main ? MainMaterial() : RcsMaterial());
		r->shadowCastingMode = ShadowCastingMode::Off;
		r->receiveShadows = false;
		r->enabled = false;
		Jet j;
		j.T = go->transform;
		j.R = r;
		j.K = kind;
		if (kind == Kind::Rcs)
		{
			// Its push is against its exhaust; its turn, that push about the hull's middle. Signs as the hull reads them:
			// exhaust down at the bow lifts the nose, exhaust to port at the bow swings it to starboard, exhaust down on
			// the port beam rolls it clockwise from behind.
			Vector3 torque = Vector3::Cross(at - centre, -exhaust);
			j.Pitch = -torque.x;
			j.Yaw = torque.y;
			j.Roll = -torque.z;
		}
		j.Width = width;
		j.Length = length;
		j.Phase = static_cast<float>(jets.Count()) * 1.7f;
		jets.Add(j);
	}

	void HullThrusters::Update(Quaternion rotation, float thrust, float brake, float fullAccel, float intensity, float dt, float now)
	{
		if (!(dt > 0)) return;
		// The spin about the hull's own axes, from its turn since the last frame: a turn about +X dips the nose, about +Y
		// swings it to starboard, about +Z rolls it anticlockwise from behind. The first frame (or the first after a jump)
		// has no spin yet, and the next no spin to change from: only the one after reads a change.
		if (hasLast)
		{
			Quaternion d = Quaternion::Inverse(lastRotation) * rotation;
			if (d.w < 0) d = Quaternion(-d.x, -d.y, -d.z, -d.w);
			float half = Mathf::Acos(Mathf::Clamp(d.w, -1.0f, 1.0f));
			if (2 * half > JumpRad || dt > 0.25f)
			{
				spinPitch = spinYaw = spinRoll = 0;
				accPitch = accYaw = accRoll = 0;
				spinKnown = false;
			}
			else
			{
				float s = Mathf::Sin(half);
				Vector3 w = s > 1e-6f ? Vector3(d.x, d.y, d.z) * (2 * half / s / dt) : Vector3(d.x, d.y, d.z) * (2 / dt);
				float pitch = -w.x, yaw = w.y, roll = -w.z;
				if (spinKnown)
				{
					float k = 1 - Mathf::Exp(-dt * AccelFollowHz);
					accPitch += ((pitch - spinPitch) / dt - accPitch) * k;
					accYaw += ((yaw - spinYaw) / dt - accYaw) * k;
					accRoll += ((roll - spinRoll) / dt - accRoll) * k;
				}
				spinPitch = pitch;
				spinYaw = yaw;
				spinRoll = roll;
				spinKnown = true;
			}
		}
		bool settled = spinKnown;
		lastRotation = rotation;
		hasLast = true;

		float full = fullAccel > 1e-6f ? fullAccel : 1.0f;
		float lit = Mathf::Clamp01(intensity);
		mainLevel = 0;
		for (auto& j : jets)
		{
			if (j.K == Kind::Rcs)
			{
				float push = settled ? (j.Pitch * accPitch + j.Yaw * accYaw + j.Roll * accRoll) / full : 0.0f;
				float want = Mathf::Clamp01((push - DeadShare) / (1 - DeadShare));
				j.Level = want > j.Level ? want : Mathf::Max(want, j.Level - FadePerS * dt);
			}
			else
			{
				float want = Mathf::Clamp01(j.K == Kind::Main ? thrust : brake);
				j.Level = want > j.Level ? Mathf::Min(want, j.Level + DriveRisePerS * dt) : Mathf::Max(want, j.Level - DriveFadePerS * dt);
				if (j.K == Kind::Main) mainLevel = Mathf::Max(mainLevel, j.Level);
			}
			bool on = j.Level > 0.02f;
			j.R->enabled = on;
			if (!on)
			{
				j.T->localScale = Vector3::zero();
				continue;
			}
			float length, width;
			if (j.K == Kind::Main)
			{
				// A drive's flame breathes rather than sputters.
				float flicker = 0.93f + 0.07f * Mathf::Sin(now * 31.0f + j.Phase) * Mathf::Sin(now * 11.0f + j.Phase * 1.9f);
				length = j.Length * (0.35f + 0.65f * j.Level) * flicker;
				width = j.Width * (0.7f + 0.3f * j.Level);
			}
			else
			{
				// A thruster's flame flickers; it lengthens with the push, and is a stub even at a touch.
				float flicker = 0.82f + 0.18f * Mathf::Sin(now * 57.0f + j.Phase) * Mathf::Sin(now * 23.0f + j.Phase * 2.3f);
				float glow = j.K == Kind::Retro ? 1.0f : lit;
				length = j.Length * (0.3f + 0.7f * j.Level) * flicker * (0.6f + 0.4f * glow);
				width = j.Width * (0.6f + 0.4f * j.Level) * (0.7f + 0.3f * glow);
			}
			j.T->localScale = Vector3(width, width, length);
		}
	}

	void HullThrusters::Hide()
	{
		for (auto& j : jets)
		{
			j.Level = 0;
			if (j.R) j.R->enabled = false;
			if (j.T) j.T->localScale = Vector3::zero();
		}
		mainLevel = 0;
		hasLast = false;
		spinKnown = false;
		spinPitch = spinYaw = spinRoll = 0;
		accPitch = accYaw = accRoll = 0;
	}
}
