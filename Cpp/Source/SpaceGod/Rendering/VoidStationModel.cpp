// US-T84: the metal Dyson-sphere model every Void Station (and Void Base) is drawn as.
#include "Rendering/VoidStationModel.h"
#include "Rendering/Materials.h"
#include "Rendering/ShipModels.h"

#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <tuple>

namespace SpaceGod::Rendering
{
	namespace
	{
		/// <summary>Flat-shaded, vertex-coloured triangles (vertex alpha 0 marks an emissive face, as on every procedural hull).</summary>
		class Builder
		{
			cs::List<Vector3> vertices;
			cs::List<Vector3> normals;
			cs::List<Color> colors;
			cs::List<int> triangles;

			void Emit(std::initializer_list<Vector3> ps, Vector3 n, Color color)
			{
				int i = vertices.Count();
				for (auto p : ps)
				{
					vertices.Add(p);
					normals.Add(n);
					colors.Add(color);
				}
				for (int k = 1; k + 1 < static_cast<int>(ps.size()); k++)
				{
					triangles.Add(i);
					triangles.Add(i + k);
					triangles.Add(i + k + 1);
				}
			}

		public:
			/// <summary>A face turned to look along <paramref name="outward"/>: front faces wind so that Cross(b - a, c - a)
			/// points out of the solid, as the hull builder's do.</summary>
			void Tri(Vector3 a, Vector3 b, Vector3 c, Vector3 outward, Color color)
			{
				auto n = Vector3::Cross(b - a, c - a);
				if (Vector3::Dot(n, outward) < 0) std::swap(b, c);
				Emit({ a, b, c }, outward.normalized(), color);
			}

			void Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 outward, Color color)
			{
				auto n = Vector3::Cross(b - a, c - a);
				if (Vector3::Dot(n, outward) < 0) std::swap(b, d);
				Emit({ a, b, c, d }, outward.normalized(), color);
			}

			/// <summary>A box girder from <paramref name="p0"/> to <paramref name="p1"/>: <paramref name="width"/> across,
			/// <paramref name="depth"/> along <paramref name="up"/>.</summary>
			void Beam(Vector3 p0, Vector3 p1, Vector3 up, float width, float depth, Color color)
			{
				auto axis = p1 - p0;
				float length = axis.magnitude();
				if (length <= 1e-6f) return;
				auto f = axis / length;
				auto r = Vector3::Cross(up, f);
				if (r.sqrMagnitude() < 1e-10f) r = Vector3::Cross(Mathf::Abs(f.y) < 0.9f ? Vector3::up() : Vector3::right(), f);
				r = r.normalized() * (width * 0.5f);
				auto u = Vector3::Cross(f, r).normalized() * (depth * 0.5f);
				auto C = [&](Vector3 p, float sr, float su) { return p + r * sr + u * su; };
				Quad(C(p0, 1, 1), C(p1, 1, 1), C(p1, 1, -1), C(p0, 1, -1), r, color);
				Quad(C(p0, -1, 1), C(p0, -1, -1), C(p1, -1, -1), C(p1, -1, 1), -r, color);
				Quad(C(p0, 1, 1), C(p0, -1, 1), C(p1, -1, 1), C(p1, 1, 1), u, color);
				Quad(C(p0, 1, -1), C(p1, 1, -1), C(p1, -1, -1), C(p0, -1, -1), -u, color);
				Quad(C(p0, 1, 1), C(p0, 1, -1), C(p0, -1, -1), C(p0, -1, 1), -f, color);
				Quad(C(p1, 1, 1), C(p1, -1, 1), C(p1, -1, -1), C(p1, 1, -1), f, color);
			}

			/// <summary>A triangular plate <paramref name="thickness"/> thick, its faces along <paramref name="n"/>.</summary>
			void Plate(Vector3 a, Vector3 b, Vector3 c, Vector3 n, float thickness, Color color)
			{
				auto h = n.normalized() * (thickness * 0.5f);
				auto centre = (a + b + c) / 3.0f;
				Tri(a + h, b + h, c + h, n, color);
				Tri(a - h, b - h, c - h, -n, color);
				std::array<Vector3, 3> p { a, b, c };
				for (int k = 0; k < 3; k++)
				{
					auto x = p[k], y = p[(k + 1) % 3];
					Quad(x + h, y + h, y - h, x - h, (x + y) * 0.5f - centre, color);
				}
			}

			cs::Ref<Mesh> ToMesh(const cs::String& name)
			{
				auto mesh = cs::New<Mesh>(name);
				mesh->SetVertices(vertices);
				mesh->SetNormals(normals);
				mesh->SetColors(colors);
				mesh->SetTriangles(triangles, 0);
				mesh->RecalculateBounds();
				return mesh;
			}
		};

		using Tri3 = std::array<Vector3, 3>;

		/// <summary>The geodesic sphere's triangles: an icosahedron with each face cut <paramref name="frequency"/> ways along
		/// each edge, pushed out to the unit sphere.</summary>
		cs::List<Tri3> Geodesic(int frequency)
		{
			const float t = (1.0f + Mathf::Sqrt(5.0f)) * 0.5f;
			std::array<Vector3, 12> v {
				Vector3(-1, t, 0), Vector3(1, t, 0), Vector3(-1, -t, 0), Vector3(1, -t, 0),
				Vector3(0, -1, t), Vector3(0, 1, t), Vector3(0, -1, -t), Vector3(0, 1, -t),
				Vector3(t, 0, -1), Vector3(t, 0, 1), Vector3(-t, 0, -1), Vector3(-t, 0, 1) };
			static const int faces[20][3] = {
				{ 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 },
				{ 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
				{ 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 },
				{ 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
			cs::List<Tri3> out;
			for (const auto& face : faces)
			{
				Vector3 A = v[face[0]], B = v[face[1]], C = v[face[2]];
				auto P = [&](int i, int j) { return (A + (B - A) * (static_cast<float>(i) / frequency) + (C - A) * (static_cast<float>(j) / frequency)).normalized(); };
				for (int i = 0; i < frequency; i++)
					for (int j = 0; i + j < frequency; j++)
					{
						out.Add(Tri3 { P(i, j), P(i + 1, j), P(i, j + 1) });
						if (i + j + 1 < frequency) out.Add(Tri3 { P(i + 1, j), P(i + 1, j + 1), P(i, j + 1) });
					}
			}
			return out;
		}

		/// <summary>A repeatable 0..1 value for a direction (the same panel is plated, or open, at every level of detail).</summary>
		float Hash(Vector3 p, float salt)
		{
			float s = Mathf::Sin(p.x * 127.1f + p.y * 311.7f + p.z * 74.7f + salt * 19.19f) * 43758.5453f;
			return s - std::floor(s);
		}

		using Key = std::tuple<int, int, int>;
		Key KeyOf(Vector3 p) { return Key(static_cast<int>(std::lround(p.x * 4096)), static_cast<int>(std::lround(p.y * 4096)), static_cast<int>(std::lround(p.z * 4096))); }

		const Color Steel(0.62f, 0.63f, 0.66f, 1.0f);
		const Color Girder(0.46f, 0.47f, 0.5f, 1.0f);
		const Color DarkPlate(0.3f, 0.31f, 0.34f, 1.0f);
		const Color BrightPlate(0.78f, 0.79f, 0.8f, 1.0f);
		const Color Lamp(0.95f, 0.85f, 1.0f, 0.0f);
		const Color Ember(0.9f, 0.7f, 1.0f, 0.0f);

		/// <summary>The open band about the equator, where the frame has no plating and the accretion ring shows.</summary>
		constexpr float OpenBand = 0.2f;

		cs::Ref<Mesh> BuildShell(bool near)
		{
			Builder b;
			// Far: an icosahedron cut three ways (180 panels, 270 girders). Near: every far panel split in four.
			auto far = Geodesic(3);
			cs::List<std::pair<Tri3, Tri3>> panels; // (the far panel it belongs to, the panel drawn)
			for (const auto& t : far)
			{
				if (!near) { panels.Add({ t, t }); continue; }
				auto ab = ((t[0] + t[1]) * 0.5f).normalized(), bc = ((t[1] + t[2]) * 0.5f).normalized(), ca = ((t[2] + t[0]) * 0.5f).normalized();
				panels.Add({ t, Tri3 { t[0], ab, ca } });
				panels.Add({ t, Tri3 { ab, t[1], bc } });
				panels.Add({ t, Tri3 { ca, bc, t[2] } });
				panels.Add({ t, Tri3 { ab, bc, ca } });
			}

			// Girders along every edge, once each; hub nodes at every joint up close.
			const float girderR = 0.985f;
			const float width = near ? 0.014f : 0.022f, depth = near ? 0.026f : 0.03f;
			std::map<std::pair<Key, Key>, bool> edges;
			std::map<Key, Vector3> joints;
			for (const auto& [parent, p] : panels)
				for (int k = 0; k < 3; k++)
				{
					Vector3 x = p[k], y = p[(k + 1) % 3];
					Key kx = KeyOf(x), ky = KeyOf(y);
					joints[kx] = x;
					auto key = kx < ky ? std::make_pair(kx, ky) : std::make_pair(ky, kx);
					if (edges.count(key)) continue;
					edges[key] = true;
					b.Beam(x * girderR, y * girderR, (x + y).normalized(), width, depth, Girder);
				}
			if (near)
				for (const auto& [key, j] : joints)
				{
					auto side = Mathf::Abs(j.y) < 0.9f ? Vector3::up() : Vector3::right();
					b.Beam(j * (girderR - 0.022f), j * (girderR + 0.018f), side, 0.03f, 0.03f, Steel);
				}

			// Plating: most of each hemisphere, never the equatorial band, with gaps where a far panel is left open.
			for (const auto& [parent, p] : panels)
			{
				auto centre = (parent[0] + parent[1] + parent[2]).normalized();
				if (Mathf::Abs(centre.y) < OpenBand) continue;
				if (Hash(centre, 1) > 0.66f) continue;
				float shade = Hash(centre, 2);
				Color color = shade < 0.06f ? Ember : shade < 0.3f ? DarkPlate : shade < 0.45f ? BrightPlate : Steel;
				auto mid = (p[0] + p[1] + p[2]) / 3.0f;
				const float plateR = 0.962f, inset = near ? 0.86f : 0.9f;
				Tri3 q;
				for (int k = 0; k < 3; k++) q[k] = (mid + (p[k] - mid) * inset) * plateR;
				b.Plate(q[0], q[1], q[2], mid.normalized(), 0.016f, color);
				// Up close, a darker rib across each plate.
				if (near && color.a > 0.5f)
					b.Beam((q[0] + q[1]) * 0.5f * 1.004f, q[2] * 1.004f, mid.normalized(), 0.008f, 0.008f, DarkPlate);
			}

			// The rims of the open band: two heavy hoops the plating stands on, studded with lamps.
			for (float s : { -1.0f, 1.0f })
			{
				const int segments = near ? 72 : 40;
				float y = s * OpenBand, r = Mathf::Sqrt(1 - y * y) * 0.975f;
				for (int k = 0; k < segments; k++)
				{
					float a0 = Mathf::PI * 2 * k / segments, a1 = Mathf::PI * 2 * (k + 1) / segments;
					Vector3 p0(Mathf::Cos(a0) * r, y, Mathf::Sin(a0) * r), p1(Mathf::Cos(a1) * r, y, Mathf::Sin(a1) * r);
					b.Beam(p0, p1, Vector3(p0.x, 0, p0.z).normalized(), 0.03f, 0.04f, Steel);
					if (k % (segments / 8) == 0)
						b.Beam(p0 * 1.03f, p0 * 1.055f, Vector3::up(), 0.022f, 0.022f, Lamp);
				}
			}

			// Spires at the poles, a beacon on each tip.
			for (float s : { -1.0f, 1.0f })
			{
				b.Beam(Vector3(0, s * 0.9f, 0), Vector3(0, s * 1.06f, 0), Vector3::right(), 0.05f, 0.05f, Steel);
				b.Beam(Vector3(0, s * 1.06f, 0), Vector3(0, s * 1.09f, 0), Vector3::right(), 0.035f, 0.035f, Lamp);
			}
			return b.ToMesh(near ? "VoidStationShellNear" : "VoidStationShellFar");
		}

		/// <summary>A flat ring of girder segments in the XZ plane.</summary>
		void Hoop(Builder& b, float radius, float width, float depth, int segments, const std::function<Color(int)>& colorOf)
		{
			for (int k = 0; k < segments; k++)
			{
				float a0 = Mathf::PI * 2 * k / segments, a1 = Mathf::PI * 2 * (k + 1) / segments;
				Vector3 p0(Mathf::Cos(a0) * radius, 0, Mathf::Sin(a0) * radius), p1(Mathf::Cos(a1) * radius, 0, Mathf::Sin(a1) * radius);
				b.Beam(p0, p1, Vector3::up(), width, depth, colorOf(k));
			}
		}

		cs::Ref<Mesh>& Cached(int slot)
		{
			static cs::Ref<Mesh> meshes[5];
			return meshes[slot];
		}
	}

	cs::Ref<Mesh> VoidStationModel::Shell(bool near)
	{
		auto& mesh = Cached(near ? 1 : 0);
		if (!Alive(mesh)) mesh = BuildShell(near);
		return mesh;
	}

	cs::Ref<Mesh> VoidStationModel::AccretionRing()
	{
		auto& mesh = Cached(2);
		if (Alive(mesh)) return mesh;
		Builder b;
		// Bright arcs broken by dark ones, so its turning shows; a fainter inner ring within.
		Hoop(b, 0.56f, 0.06f, 0.014f, 48, [](int k) { return k % 6 == 5 ? DarkPlate : Ember; });
		Hoop(b, 0.47f, 0.025f, 0.01f, 40, [](int k) { return k % 4 == 3 ? DarkPlate : Lamp; });
		mesh = b.ToMesh("VoidStationAccretion");
		return mesh;
	}

	cs::Ref<Mesh> VoidStationModel::Gimbal(int which)
	{
		auto& mesh = Cached(3 + (which != 0 ? 1 : 0));
		if (Alive(mesh)) return mesh;
		Builder b;
		float radius = which == 0 ? 1.05f : 1.105f;
		Hoop(b, radius, 0.03f, 0.045f, 72, [](int) { return Steel; });
		Hoop(b, radius - 0.018f, 0.008f, 0.05f, 72, [](int) { return DarkPlate; });
		// Clamp blocks and running lights round the rim.
		int blocks = which == 0 ? 6 : 4;
		for (int k = 0; k < blocks; k++)
		{
			float a = Mathf::PI * 2 * (k + 0.5f) / blocks;
			Vector3 dir(Mathf::Cos(a), 0, Mathf::Sin(a));
			Vector3 tangent(-dir.z, 0, dir.x);
			b.Beam(dir * radius - tangent * 0.05f, dir * radius + tangent * 0.05f, Vector3::up(), 0.04f, 0.07f, Girder);
			b.Beam(dir * (radius + 0.02f) - tangent * 0.012f, dir * (radius + 0.02f) + tangent * 0.012f, Vector3::up(), 0.01f, 0.02f, Lamp);
		}
		mesh = b.ToMesh(which == 0 ? "VoidStationGimbalInner" : "VoidStationGimbalOuter");
		return mesh;
	}

	cs::Ref<Material> VoidStationModel::CreateMetal(const cs::String& name)
	{
		auto m = Materials::Create(Materials::Ship(), name);
		m->SetFloat("_UseVertexColor", 1);
		// The generator's violet in every lamp and glowing plate, a little stronger than an engine bell.
		m->SetColor("_EngineColor", Color(0.72f, 0.42f, 1.0f));
		m->SetFloat("_EngineGlow", 1.4f);
		ShipModels::Plate(m, 9.0f);
		return m;
	}
}
