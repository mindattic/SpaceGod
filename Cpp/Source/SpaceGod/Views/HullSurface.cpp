// US-T433: a hull's plating as points with the way each faces (see HullSurface.h).
#include "Views/HullSurface.h"
#include "Rendering/ImportedGeometry.h"

#include <algorithm>

namespace SpaceGod::Views
{
	namespace
	{
		/// <summary>How finely a hull's triangles are sampled, root units (the hull is one long).</summary>
		constexpr float SampleStep = 0.03f;
		/// <summary>The most points kept: past this a dense import is thinned evenly (every k-th triangle).</summary>
		constexpr size_t MaxTriangles = 40000;
		/// <summary>The most points a whole hull keeps (thinned evenly past it), so placing its jets stays a moment's work.</summary>
		constexpr size_t MaxSamples = 60000;

		/// <summary>A node's frame into <paramref name="root"/>'s: point and direction maps through every node between.</summary>
		struct Into
		{
			std::vector<const Transform*> Chain;

			Into(const Transform* t, const Transform* root)
			{
				while (t != nullptr && t != root)
				{
					Chain.push_back(t);
					auto up = t->parent.lock();
					t = up.get();
				}
			}

			Vector3 Point(Vector3 p) const
			{
				for (const auto* t : Chain) p = t->localPosition + t->localRotation * Vector3::Scale(t->localScale, p);
				return p;
			}
		};

		/// <summary>A triangle's points (its middle, and across it when it is big) with its face's way out.</summary>
		void SampleTriangle(Vector3 a, Vector3 b, Vector3 c, Vector3 n, std::vector<HullSurface::Sample>& out)
		{
			float edge = Mathf::Max(Vector3::Distance(a, b), Mathf::Max(Vector3::Distance(b, c), Vector3::Distance(c, a)));
			int steps = static_cast<int>(Mathf::Clamp(Mathf::Ceil(edge / SampleStep), 1.0f, 12.0f));
			if (steps == 1)
			{
				out.push_back({ (a + b + c) / 3.0f, n });
				return;
			}
			for (int i = 0; i <= steps; i++)
				for (int j = 0; j <= steps - i; j++)
				{
					float u = static_cast<float>(i) / steps, w = static_cast<float>(j) / steps;
					out.push_back({ a + (b - a) * u + (c - a) * w, n });
				}
		}

		/// <summary>
		/// A mesh's triangles into samples. Which way a face looks out is taken from its winding and then checked against the
		/// whole mesh: if most of its area faces in towards the mesh's middle, the winding is the other way round (the frame
		/// change from an import mirrors it) and every face is turned.
		/// </summary>
		void SampleMesh(const std::vector<Vector3>& v, const std::vector<int>& tris, std::vector<HullSurface::Sample>& out)
		{
			if (v.empty() || tris.size() < 3) return;
			Vector3 mid = Vector3::zero();
			for (const auto& p : v) mid += p;
			mid = mid / static_cast<float>(v.size());
			size_t count = tris.size() / 3;
			size_t stride = count > MaxTriangles ? (count + MaxTriangles - 1) / MaxTriangles : 1;
			std::vector<HullSurface::Sample> mine;
			float outward = 0;
			int n = static_cast<int>(v.size());
			for (size_t k = 0; k < count; k += stride)
			{
				int ia = tris[k * 3], ib = tris[k * 3 + 1], ic = tris[k * 3 + 2];
				if (ia < 0 || ib < 0 || ic < 0 || ia >= n || ib >= n || ic >= n) continue;
				Vector3 a = v[ia], b = v[ib], c = v[ic];
				Vector3 cross = Vector3::Cross(b - a, c - a);
				float area2 = cross.magnitude();
				if (!(area2 > 1e-12f)) continue;
				Vector3 normal = cross / area2;
				outward += Vector3::Dot(normal, (a + b + c) / 3.0f - mid) * area2;
				SampleTriangle(a, b, c, normal, mine);
			}
			if (outward < 0)
				for (auto& s : mine) s.N = -s.N;
			out.insert(out.end(), mine.begin(), mine.end());
		}

		/// <summary>A box's six faces as a grid of points each, with the face's way out.</summary>
		void SampleBox(Vector3 mn, Vector3 mx, const Into& into, std::vector<HullSurface::Sample>& out)
		{
			constexpr int n = 4;
			for (int axis = 0; axis < 3; axis++)
				for (int side = 0; side < 2; side++)
				{
					float d[3] = { 0, 0, 0 };
					d[axis] = side == 0 ? -1.0f : 1.0f;
					Vector3 local(d[0], d[1], d[2]);
					// The face's way out through the frames between (their turn; a mirroring scale flips it).
					Vector3 o = into.Point(Vector3::zero());
					Vector3 face = (into.Point(local) - o).normalized();
					for (int i = 0; i <= n; i++)
						for (int j = 0; j <= n; j++)
						{
							float u = static_cast<float>(i) / n, w = static_cast<float>(j) / n;
							float a[3] = { 0, 0, 0 };
							a[axis] = static_cast<float>(side);
							a[(axis + 1) % 3] = u;
							a[(axis + 2) % 3] = w;
							Vector3 p(mn.x + (mx.x - mn.x) * a[0], mn.y + (mx.y - mn.y) * a[1], mn.z + (mx.z - mn.z) * a[2]);
							out.push_back({ into.Point(p), face });
						}
				}
		}
	}

	HullSurface::HullSurface(const cs::Ref<Transform>& root, const cs::List<cs::Ref<Renderer>>& hull)
	{
		for (const auto& r : hull)
		{
			if (!r) continue;
			auto t = r->transform();
			auto mesh = r->DrawnMesh();
			if (!t || !mesh) continue;
			Into into(t.get(), root.get());
			std::vector<Vector3> v;
			std::vector<int> tris;
			if (!mesh->Vertices.empty())
			{
				v.reserve(mesh->Vertices.size());
				for (const auto& p : mesh->Vertices) v.push_back(into.Point(p));
				for (const auto& sub : mesh->SubMeshes)
					if (sub.Topology == MeshTopology::Triangles) tris.insert(tris.end(), sub.Indices.begin(), sub.Indices.end());
			}
			else if (const auto* g = Rendering::ImportedGeometry::Of(mesh))
			{
				// US-T433: an imported part's own triangles, as the engine keeps them.
				v.reserve(g->Vertices.size());
				for (const auto& p : g->Vertices) v.push_back(into.Point(p));
				tris = g->Triangles;
			}
			if (!tris.empty()) SampleMesh(v, tris, samples);
			else
			{
				auto b = mesh->bounds();
				if (b.size().sqrMagnitude() > 0) SampleBox(b.min(), b.max(), into, samples);
			}
		}
		if (samples.size() > MaxSamples)
		{
			size_t stride = (samples.size() + MaxSamples - 1) / MaxSamples;
			std::vector<Sample> kept;
			kept.reserve(samples.size() / stride + 1);
			for (size_t i = 0; i < samples.size(); i += stride) kept.push_back(samples[i]);
			samples.swap(kept);
		}
		if (samples.empty())
		{
			// No hull drawn yet (or an odd import): a nominal fighter's box, one long and a third wide.
			SampleBox(Vector3(-0.175f, -0.075f, -0.5f), Vector3(0.175f, 0.075f, 0.5f), Into(nullptr, nullptr), samples);
		}
		mn = mx = samples[0].P;
		for (const auto& s : samples)
		{
			mn = Vector3::Min(mn, s.P);
			mx = Vector3::Max(mx, s.P);
		}
	}

	bool HullSurface::Clear(Vector3 at, Vector3 fire, float radius) const
	{
		// Nothing of the hull ahead of the nozzle along its exhaust, within its plume's width, for a good way out.
		float reach = 0.35f * Length();
		float lane = Mathf::Max(radius * 0.9f, 0.004f);
		for (const auto& s : samples)
		{
			Vector3 d = s.P - at;
			float along = Vector3::Dot(d, fire);
			if (along < lane || along > reach) continue;
			if ((d - fire * along).sqrMagnitude() < lane * lane) return false;
		}
		return true;
	}

	bool HullSurface::Place(float z, Vector3 side, Vector3 fire, float sideways, float radius, Vector3& at, Vector3& normal) const
	{
		if (samples.empty()) return false;
		Vector3 c = Centre();
		float length = Length();
		Vector3 across = side.sqrMagnitude() > 1e-8f ? side.normalized() : fire;
		struct Pick { float Score; size_t Index; };
		// Plating that faces the way the jet fires, then (should none there) the outermost plating whatever it faces.
		for (float facing : { 0.5f, -2.0f })
			for (float band = 0.04f * length; band <= 0.6f * length; band *= 1.6f)
			{
				std::vector<Pick> picks;
				for (size_t i = 0; i < samples.size(); i++)
				{
					const auto& s = samples[i];
					float dz = Mathf::Abs(s.P.z - z);
					if (dz > band || Vector3::Dot(s.N, fire) < facing) continue;
					Vector3 r(s.P.x - c.x, s.P.y - c.y, s.P.z - z);
					// Across the hull for the side, along the jet's own way for the outermost that way.
					Vector3 flat(r.x, r.y, across.z != 0 ? r.z : 0);
					float along = Vector3::Dot(flat, across);
					float off = (flat - across * along).magnitude();
					picks.push_back({ along - sideways * off - 0.3f * dz + 0.02f * length * Vector3::Dot(s.N, fire), i });
				}
				if (picks.empty()) continue;
				// The best few that fire clear of the hull; failing that, the best.
				size_t tries = std::min<size_t>(picks.size(), 48);
				std::partial_sort(picks.begin(), picks.begin() + tries, picks.end(), [](const Pick& a, const Pick& b) { return a.Score > b.Score; });
				for (size_t k = 0; k < tries; k++)
				{
					const auto& s = samples[picks[k].Index];
					if (!Clear(s.P, fire, radius)) continue;
					at = s.P;
					normal = s.N;
					return true;
				}
				if (facing < 0)
				{
					at = samples[picks[0].Index].P;
					normal = samples[picks[0].Index].N;
					return true;
				}
			}
		return false;
	}
}
