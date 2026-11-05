// US-T123: the models of the places about a void station (see VoidSiteModel.h).
#include "Rendering/VoidSiteModel.h"
#include "Rendering/Materials.h"

#include "ScaleModel.h"
#include "Systems/VoidSites.h"

#include <array>
#include <cmath>
#include <functional>

using SpaceGod::Core::VoidSight;
using SpaceGod::Core::VoidSite;
using SpaceGod::Core::VoidSiteKind;

namespace SpaceGod::Rendering
{
	namespace
	{
		/// <summary>A small deterministic stream for shapes (xorshift32 from the site's seed).</summary>
		class Dice
		{
			uint32_t s;

		public:
			explicit Dice(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
			uint32_t Next()
			{
				s ^= s << 13;
				s ^= s >> 17;
				s ^= s << 5;
				return s;
			}
			float U() { return static_cast<float>(Next() >> 8) * (1.0f / 16777216.0f); }
			float Range(float a, float b) { return a + (b - a) * U(); }
			Vector3 Direction()
			{
				for (;;)
				{
					Vector3 v(Range(-1, 1), Range(-1, 1), Range(-1, 1));
					float m = v.sqrMagnitude();
					if (m > 0.01f && m <= 1) return v / Mathf::Sqrt(m);
				}
			}
		};

		/// <summary>Flat-shaded, vertex-coloured triangles (vertex alpha 0 marks an emissive face, as on every procedural hull).</summary>
		class Builder
		{
		public:
			cs::List<Vector3> vertices;
			cs::List<Vector3> normals;
			cs::List<Color> colors;
			cs::List<int> triangles;

			void Tri(Vector3 a, Vector3 b, Vector3 c, Vector3 outward, Color color)
			{
				auto n = Vector3::Cross(b - a, c - a);
				if (Vector3::Dot(n, outward) < 0) std::swap(b, c);
				n = Vector3::Cross(b - a, c - a);
				auto normal = n.sqrMagnitude() > 1e-14f ? n.normalized() : outward.normalized();
				int i = vertices.Count();
				for (auto p : { a, b, c })
				{
					vertices.Add(p);
					normals.Add(normal);
					colors.Add(color);
				}
				triangles.Add(i);
				triangles.Add(i + 1);
				triangles.Add(i + 2);
			}

			void Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 outward, Color color)
			{
				Tri(a, b, c, outward, color);
				Tri(a, c, d, outward, color);
			}

			/// <summary>A box girder from <paramref name="p0"/> to <paramref name="p1"/>.</summary>
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

			/// <summary>A ring of <paramref name="segments"/> girders in the plane through <paramref name="centre"/> normal to <paramref name="axis"/>.</summary>
			void Hoop(Vector3 centre, Vector3 axis, float radius, float width, float depth, int segments, const std::function<Color(int)>& colorOf,
				const std::function<bool(int)>& keep)
			{
				auto a = axis.normalized();
				auto u = Vector3::Cross(a, Mathf::Abs(a.y) < 0.9f ? Vector3::up() : Vector3::right()).normalized();
				auto v = Vector3::Cross(a, u);
				for (int k = 0; k < segments; k++)
				{
					if (!keep(k)) continue;
					float a0 = Mathf::PI * 2 * k / segments, a1 = Mathf::PI * 2 * (k + 1) / segments;
					auto p0 = centre + (u * Mathf::Cos(a0) + v * Mathf::Sin(a0)) * radius;
					auto p1 = centre + (u * Mathf::Cos(a1) + v * Mathf::Sin(a1)) * radius;
					Beam(p0, p1, a, width, depth, colorOf(k));
				}
			}

			void Append(const Builder& other)
			{
				int base = vertices.Count();
				vertices.AddRange(other.vertices);
				normals.AddRange(other.normals);
				colors.AddRange(other.colors);
				for (int t : other.triangles) triangles.Add(base + t);
			}

			void Scale(float s)
			{
				for (auto& v : vertices) v = v * s;
			}

			float Reach() const
			{
				float r = 0;
				for (const auto& v : vertices) r = Mathf::Max(r, v.magnitude());
				return r;
			}

			cs::Ref<Mesh> ToMesh(const cs::String& name)
			{
				auto mesh = cs::New<Mesh>(name);
				if (vertices.Count() > 65000) mesh->indexFormat = IndexFormat::UInt32;
				mesh->SetVertices(vertices);
				mesh->SetNormals(normals);
				mesh->SetColors(colors);
				mesh->SetTriangles(triangles, 0);
				mesh->RecalculateBounds();
				return mesh;
			}
		};

		using Tri3 = std::array<Vector3, 3>;

		/// <summary>An icosahedron with each face cut <paramref name="frequency"/> ways, on the unit sphere.</summary>
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
			for (const auto& f : faces)
			{
				Vector3 a = v[f[0]], b = v[f[1]], c = v[f[2]];
				auto P = [&](int i, int j) { int k = frequency - i - j; return ((a * static_cast<float>(i) + b * static_cast<float>(j) + c * static_cast<float>(k)) / static_cast<float>(frequency)).normalized(); };
				for (int i = 0; i < frequency; i++)
					for (int j = 0; j < frequency - i; j++)
					{
						out.Add(Tri3 { P(i + 1, j), P(i, j + 1), P(i, j) });
						if (i + j + 2 <= frequency) out.Add(Tri3 { P(i + 1, j), P(i + 1, j + 1), P(i, j + 1) });
					}
			}
			return out;
		}

		/// <summary>A lumpy rock's surface: a radius for every direction, from a handful of seeded bumps and dents and a squash.</summary>
		struct Lumps
		{
			Vector3 centres[10];
			float amps[10], widths[10];
			Vector3 squash;

			explicit Lumps(Dice& d, float roughness, float elongation)
			{
				for (int j = 0; j < 10; j++)
				{
					centres[j] = d.Direction();
					amps[j] = d.Range(-0.22f, 0.18f) * roughness;
					widths[j] = d.Range(3, 14);
				}
				squash = Vector3(1, d.Range(1 - elongation, 1), d.Range(1 - elongation, 1));
			}

			Vector3 At(Vector3 dir) const
			{
				float r = 1;
				for (int j = 0; j < 10; j++) r += amps[j] * Mathf::Exp(-(1 - Vector3::Dot(dir, centres[j])) * widths[j]);
				auto p = dir * r;
				return Vector3(p.x * squash.x, p.y * squash.y, p.z * squash.z);
			}
		};

		Color Shade(Color c, float k, float alpha = 1)
		{
			return Color(Mathf::Clamp01(c.r * k), Mathf::Clamp01(c.g * k), Mathf::Clamp01(c.b * k), alpha);
		}

		/// <summary>
		/// A faceted rock of radius about <paramref name="size"/> at <paramref name="centre"/>. <paramref name="cuts"/> fracture planes
		/// slice it flat (a chunk's broken faces, coloured <paramref name="fracture"/>); <paramref name="crack"/> runs a glowing fault
		/// round it; <paramref name="veins"/> picks out glowing veins.
		/// </summary>
		void Rock(Builder& b, Dice& d, Vector3 centre, float size, Color base, int frequency, float roughness, float elongation,
			int cuts, Color fracture, bool crack, bool veins, Color glow)
		{
			Lumps lumps(d, roughness, elongation);
			Vector3 planeN[3];
			float planeD[3];
			for (int c = 0; c < cuts && c < 3; c++)
			{
				planeN[c] = d.Direction();
				planeD[c] = d.Range(0.25f, 0.6f);
			}
			Vector3 crackN = d.Direction();
			Vector3 veinW = d.Direction() * d.Range(9, 16);
			float veinPhase = d.Range(0, 6.28f);
			auto Shape = [&](Vector3 dir, bool& cut)
			{
				auto p = lumps.At(dir);
				cut = false;
				for (int c = 0; c < cuts && c < 3; c++)
				{
					float h = Vector3::Dot(p, planeN[c]);
					if (h > planeD[c])
					{
						p = p - planeN[c] * (h - planeD[c]);
						cut = true;
					}
				}
				if (crack)
				{
					// The fault: a jagged band round a great circle, sunk into the surface.
					float wobble = 0.05f * Mathf::Sin(Mathf::Atan2(dir.z, dir.x) * 7 + dir.y * 5);
					if (Mathf::Abs(Vector3::Dot(dir, crackN) + wobble) < 0.07f) p = p * 0.82f;
				}
				return p;
			};
			for (const auto& f : Geodesic(frequency))
			{
				bool c0, c1, c2;
				auto a = Shape(f[0], c0), bb = Shape(f[1], c1), c = Shape(f[2], c2);
				auto mid = (f[0] + f[1] + f[2]).normalized();
				Color color = Shade(base, d.Range(0.82f, 1.12f));
				if (c0 && c1 && c2) color = fracture;
				if (crack && Mathf::Abs(Vector3::Dot(mid, crackN) + 0.05f * Mathf::Sin(Mathf::Atan2(mid.z, mid.x) * 7 + mid.y * 5)) < 0.05f)
					color = Color(glow.r, glow.g, glow.b, 0);
				if (veins && Mathf::Abs(Mathf::Sin(Vector3::Dot(mid, veinW) + veinPhase)) < 0.16f) color = Color(glow.r, glow.g, glow.b, 0);
				b.Tri(centre + a * size, centre + bb * size, centre + c * size, mid, color);
			}
		}

		/// <summary>A crystal: a stretched bipyramid standing out from <paramref name="at"/> along <paramref name="dir"/>.</summary>
		void Crystal(Builder& b, Dice& d, Vector3 at, Vector3 dir, float length, float width, Color body, Color tip)
		{
			auto u = Vector3::Cross(dir, Mathf::Abs(dir.y) < 0.9f ? Vector3::up() : Vector3::right()).normalized() * width;
			auto v = Vector3::Cross(dir, u.normalized()) * width;
			auto top = at + dir * length, bottom = at - dir * (length * 0.2f);
			auto mid = at + dir * (length * 0.55f);
			Vector3 ring[4] = { mid + u, mid + v, mid - u, mid - v };
			for (int k = 0; k < 4; k++)
			{
				auto p = ring[k], q = ring[(k + 1) % 4];
				auto out = ((p + q) * 0.5f - mid);
				b.Tri(p, q, top, out + dir * 0.3f * width, k % 2 == 0 ? tip : body);
				b.Tri(p, q, bottom, out - dir * 0.3f * width, body);
			}
			(void)d;
		}

		Color Base(const VoidSite& site) { return Color(site.Color.R, site.Color.G, site.Color.B, 1); }

		void BuildSight(Builder& b, Dice& d, const VoidSite& site, Color glow)
		{
			Color base = Base(site);
			Color lit(glow.r, glow.g, glow.b, 0);
			switch (site.Sight)
			{
				case VoidSight::GlassMoonlet:
				{
					Rock(b, d, Vector3::zero(), 0.62f, Shade(base, 0.55f), 3, 1.0f, 0.15f, 0, base, false, false, glow);
					for (int k = 0; k < 22; k++)
					{
						auto dir = d.Direction();
						Crystal(b, d, dir * 0.55f, (dir + d.Direction() * 0.25f).normalized(), d.Range(0.25f, 0.55f), d.Range(0.04f, 0.09f),
							Color(0.7f, 0.9f, 1.0f, 1), lit);
					}
					break;
				}
				case VoidSight::FrozenFountain:
				{
					Rock(b, d, Vector3::zero(), 0.55f, base, 3, 1.1f, 0.3f, 0, base, false, false, glow);
					// The frozen jet: a spire of ice, tapering and leaning a little as it climbs.
					auto axis = d.Direction();
					auto lean = Vector3::Cross(axis, d.Direction()).normalized();
					const int rings = 9, sides = 8;
					auto u0 = Vector3::Cross(axis, Mathf::Abs(axis.y) < 0.9f ? Vector3::up() : Vector3::right()).normalized();
					auto v0 = Vector3::Cross(axis, u0);
					Vector3 prev[sides];
					for (int r = 0; r <= rings; r++)
					{
						float t = static_cast<float>(r) / rings;
						auto c = axis * (0.45f + 0.7f * t) + lean * (0.08f * t * t);
						float width = Mathf::Lerp(0.11f, 0.012f, t) * (1 + 0.25f * Mathf::Sin(t * 19));
						Vector3 ring[sides];
						for (int s = 0; s < sides; s++)
						{
							float a = Mathf::PI * 2 * s / sides + t * 0.8f;
							ring[s] = c + (u0 * Mathf::Cos(a) + v0 * Mathf::Sin(a)) * width;
						}
						if (r > 0)
							for (int s = 0; s < sides; s++)
							{
								int n = (s + 1) % sides;
								auto out = (ring[s] + ring[n]) * 0.5f - c;
								Color ice = (r + s) % 5 == 0 ? Color(0.75f, 0.9f, 1.0f, 0) : Color(0.82f, 0.9f, 0.97f, 1);
								b.Quad(prev[s], prev[n], ring[n], ring[s], out, ice);
							}
						for (int s = 0; s < sides; s++) prev[s] = ring[s];
					}
					for (int k = 0; k < 8; k++)
					{
						auto dir = (axis * 0.7f + d.Direction() * 0.6f).normalized();
						Crystal(b, d, dir * 0.5f, dir, d.Range(0.12f, 0.25f), 0.035f, Color(0.85f, 0.92f, 1.0f, 1), Color(0.85f, 0.92f, 1.0f, 1));
					}
					break;
				}
				case VoidSight::DerelictArk:
				{
					Color hull = base, dark = Shade(base, 0.6f), pale = Shade(base, 1.35f);
					// The spine, its drive block astern and its bow.
					b.Beam(Vector3(0, -1.0f, 0), Vector3(0, 1.0f, 0), Vector3::right(), 0.12f, 0.12f, hull);
					b.Beam(Vector3(0, -1.08f, 0), Vector3(0, -0.8f, 0), Vector3::right(), 0.3f, 0.3f, dark);
					b.Beam(Vector3(0, 0.82f, 0), Vector3(0, 1.08f, 0), Vector3::right(), 0.2f, 0.16f, pale);
					// Ring habitats on spokes; the aft one broken open.
					for (int h = 0; h < 3; h++)
					{
						float y = -0.45f + 0.45f * h;
						bool broken = h == 0;
						b.Hoop(Vector3(0, y, 0), Vector3::up(), 0.5f, 0.09f, 0.07f, 28, [&](int k) { return k % 7 == 3 ? lit : (k % 2 ? hull : pale); },
							[&](int k) { return !broken || (k < 17 || k > 21); });
						for (int s = 0; s < 4; s++)
						{
							float a = Mathf::PI * 0.5f * s + h * 0.4f;
							b.Beam(Vector3(0, y, 0), Vector3(Mathf::Cos(a) * 0.48f, y, Mathf::Sin(a) * 0.48f), Vector3::up(), 0.03f, 0.03f, dark);
						}
					}
					// Cargo pods and their running lights along the spine.
					for (int k = 0; k < 8; k++)
					{
						float y = -0.7f + 0.2f * k;
						float a = d.Range(0, 6.28f);
						Vector3 side(Mathf::Cos(a) * 0.12f, y, Mathf::Sin(a) * 0.12f);
						b.Beam(side, side + Vector3(0, 0.12f, 0), Vector3::right(), 0.1f, 0.1f, k % 3 == 0 ? dark : hull);
						b.Beam(side * 1.6f, side * 1.6f + Vector3(0, 0.02f, 0), Vector3::right(), 0.025f, 0.025f, lit);
					}
					break;
				}
				case VoidSight::StoneArch:
				{
					// A torus of rock, its tube lumpy, nothing in the middle.
					const int segments = 40, sides = 9;
					const float major = 0.82f, minor = 0.17f;
					Lumps lumps(d, 0.6f, 0.0f);
					auto P = [&](int i, int j)
					{
						float a = Mathf::PI * 2 * i / segments, c = Mathf::PI * 2 * j / sides;
						Vector3 radial(Mathf::Cos(a), 0, Mathf::Sin(a));
						Vector3 tube = radial * Mathf::Cos(c) + Vector3::up() * Mathf::Sin(c);
						float r = minor * lumps.At(tube.normalized()).magnitude() * (1 + 0.1f * Mathf::Sin(a * 5));
						return radial * major + tube * r;
					};
					for (int i = 0; i < segments; i++)
						for (int j = 0; j < sides; j++)
						{
							auto p0 = P(i, j), p1 = P(i + 1, j), p2 = P(i + 1, j + 1), p3 = P(i, j + 1);
							float a = Mathf::PI * 2 * (i + 0.5f) / segments;
							auto out = (p0 + p2) * 0.5f - Vector3(Mathf::Cos(a), 0, Mathf::Sin(a)) * major;
							b.Quad(p0, p1, p2, p3, out, Shade(base, d.Range(0.8f, 1.15f)));
						}
					break;
				}
				default:
					// The Lantern: a dark rock veined with light.
					Rock(b, d, Vector3::zero(), 0.95f, Shade(base, 0.45f), 3, 1.0f, 0.3f, 0, base, false, true, glow);
					break;
			}
		}
	}

	Color VoidSiteModel::GlowColor(const VoidSite& site, bool& halo)
	{
		halo = false;
		switch (site.Kind)
		{
			case VoidSiteKind::Planetoid:
			case VoidSiteKind::Chunk:
				return site.Exploded ? Color(1.0f, 0.45f, 0.15f) : Color(0.9f, 0.5f, 0.25f);
			case VoidSiteKind::Sight:
				switch (site.Sight)
				{
					case VoidSight::GlassMoonlet: halo = true; return Color(0.45f, 0.85f, 1.0f);
					case VoidSight::FrozenFountain: return Color(0.6f, 0.85f, 1.0f);
					case VoidSight::DerelictArk: return Color(1.0f, 0.75f, 0.35f);
					case VoidSight::StoneArch: return Color(0.9f, 0.85f, 0.7f);
					default: halo = true; return Color(0.35f, 1.0f, 0.5f);
				}
			default:
				return Color(0.8f, 0.6f, 0.4f);
		}
	}

	cs::Ref<Mesh> VoidSiteModel::Body(const VoidSite& site)
	{
		Dice d(site.ShapeSeed);
		Builder b;
		bool halo = false;
		Color glow = GlowColor(site, halo);
		Color base = Base(site);
		switch (site.Kind)
		{
			case VoidSiteKind::FieldRock:
				Rock(b, d, Vector3::zero(), 1, base, 3, site.Icy ? 0.8f : 1.2f, d.Range(0.2f, 0.55f), 0, base, false, false, glow);
				break;
			case VoidSiteKind::Chunk:
				// Broken off: sliced flat on one or two sides, the fresh faces paler (or still glowing, if it blew apart).
				Rock(b, d, Vector3::zero(), 1, base, 3, 1.3f, d.Range(0.2f, 0.5f), 1 + static_cast<int>(d.Next() % 2),
					site.Exploded ? Color(glow.r, glow.g * 0.8f, glow.b * 0.6f, 0) : Shade(base, 1.45f), false, false, glow);
				break;
			case VoidSiteKind::Planetoid:
				if (site.Exploded)
					// Gutted: a bite taken out of it, its inside still glowing.
					Rock(b, d, Vector3::zero(), 1, base, 4, 0.9f, 0.12f, 2, Color(glow.r, glow.g, glow.b, 0), false, false, glow);
				else
					// Whole but split by one great fault that glows faintly from deep inside.
					Rock(b, d, Vector3::zero(), 1, base, 4, 0.7f, 0.08f, 0, base, true, false, glow);
				break;
			case VoidSiteKind::Sight:
				BuildSight(b, d, site, glow);
				break;
			default:
				Rock(b, d, Vector3::zero(), 1, base, 2, 1, 0.3f, 0, base, false, false, glow);
				break;
		}
		// One radius across at most, so it is the body's size at either scale (a sight's spire or crystals to 1.2).
		float reach = b.Reach();
		float limit = site.Kind == VoidSiteKind::Sight ? 1.2f : 1.0f;
		if (reach > limit) b.Scale(limit / reach);
		return Materials::Own(b.ToMesh(cs::Cat("VoidSite ", site.Name)));
	}

	cs::Ref<Material> VoidSiteModel::CreateMaterial(const VoidSite& site, const cs::String& name)
	{
		bool halo = false;
		auto glow = GlowColor(site, halo);
		auto m = Materials::Create(Materials::Ship(), name);
		m->SetFloat("_UseVertexColor", 1);
		m->SetColor("_EngineColor", glow);
		m->SetFloat("_EngineGlow", site.Kind == VoidSiteKind::Sight ? 1.3f : 0.9f);
		return m;
	}

	cs::List<VoidSiteModel::Fragment> VoidSiteModel::Debris(const VoidSite& site)
	{
		cs::List<Fragment> list;
		int count = 0;
		float inner = 0, outer = 0, small = 0, large = 0;
		if (site.Kind == VoidSiteKind::Planetoid)
		{
			count = site.Exploded ? 240 : 120;
			inner = static_cast<float>(Core::VoidSites::ShellInnerRadii);
			outer = static_cast<float>(Core::VoidSites::ShellOuterRadii);
			small = 0.004f;
			large = site.Exploded ? 0.022f : 0.016f;
		}
		else if (site.Kind == VoidSiteKind::FieldRock)
		{
			count = 18;
			inner = 1.3f;
			outer = static_cast<float>(Core::VoidSites::RubbleRadii);
			small = 0.02f;
			large = 0.07f;
		}
		else return list;
		Dice d(site.ShapeSeed ^ 0xA5A5F00Du);
		Color base = Base(site);
		bool halo = false;
		Color glow = GlowColor(site, halo);
		// A cracked planetoid shed its rubble in a band about its fault; a shattered one threw it all round.
		Vector3 band = d.Direction();
		for (int k = 0; k < count; k++)
		{
			Fragment f;
			auto dir = d.Direction();
			if (site.Kind == VoidSiteKind::Planetoid && !site.Exploded)
			{
				float h = Vector3::Dot(dir, band);
				dir = (dir - band * (h * 0.75f)).normalized();
			}
			f.Direction = dir;
			f.Radii = d.Range(inner, outer);
			f.Size = small * Mathf::Pow(large / small, d.U() * d.U());
			for (int c = 0; c < 6; c++)
			{
				Vector3 axis = c < 2 ? Vector3::right() : c < 4 ? Vector3::up() : Vector3::forward();
				float sign = c % 2 == 0 ? 1.0f : -1.0f;
				f.Corners[c] = (axis * sign + d.Direction() * 0.45f);
			}
			bool hot = site.Exploded && d.U() < 0.12f;
			f.Tint = hot ? Color(glow.r, glow.g, glow.b, 0) : Shade(base, d.Range(0.7f, 1.15f));
			list.Add(f);
		}
		return list;
	}

	void VoidSiteModel::LayDebris(const cs::Ref<Mesh>& mesh, const cs::List<Fragment>& fragments, double blend)
	{
		Builder b;
		static const int faces[8][3] = { { 0, 2, 4 }, { 2, 1, 4 }, { 1, 3, 4 }, { 3, 0, 4 }, { 2, 0, 5 }, { 1, 2, 5 }, { 3, 1, 5 }, { 0, 3, 5 } };
		for (const auto& f : fragments)
		{
			// Where a moon that far out would be drawn (the scale model's local mapping, in body radii), and as big as a
			// body that size would be drawn beside this one.
			double game = 1.0 + Core::ScaleModel::GameLocalC * std::pow(std::max(0.0, static_cast<double>(f.Radii) - 1.0), Core::ScaleModel::GameLocalP);
			float at = static_cast<float>(Core::ScaleModel::LogLerp(f.Radii, game, blend));
			float size = static_cast<float>(Core::ScaleModel::LogLerp(f.Size, std::pow(static_cast<double>(f.Size), Core::ScaleModel::GameRadiusExponent), blend));
			auto centre = f.Direction * at;
			for (const auto& face : faces)
			{
				auto p0 = centre + f.Corners[face[0]] * size, p1 = centre + f.Corners[face[1]] * size, p2 = centre + f.Corners[face[2]] * size;
				b.Tri(p0, p1, p2, (p0 + p1 + p2) / 3.0f - centre, f.Tint);
			}
		}
		mesh->Clear();
		mesh->SetVertices(b.vertices);
		mesh->SetNormals(b.normals);
		mesh->SetColors(b.colors);
		mesh->SetTriangles(b.triangles, 0);
		mesh->RecalculateBounds();
	}
}
