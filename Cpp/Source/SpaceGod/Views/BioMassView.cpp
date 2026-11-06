// US-T125: the Bio Mass drawn (see BioMassView.h); US-T192 its blooms and tumors, and the growths riding a hull.
#include "Views/BioMassView.h"

#include "UnitySpaceApp.h"
#include "Rendering/Materials.h"
#include "Target.h"
#include "Views/LineStrip.h"

#include "BodyCatalog.h"
#include "Combat/BioMass.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "RenderLod.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "SimClock.h"
#include "Systems/JumpGate.h"
#include "World.h"

#include <cmath>
#include <map>
#include <random>
#include <vector>

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;

	// ---------------------------------------------------------------- the model

	struct BioMassView::Shape
	{
		struct Lump { Vector3 Dir; float Amp, Width, Freq, Phase; };
		struct Mouth { Vector3 Dir, U, V; float Size, Phase, Freq; };
		struct Lobe { Vector3 Dir; float Dist, Radius, Freq, Phase; };
		struct Bone { Vector3 Dir, Side; float Length, Width; };
		struct Hair { Vector3 Dir, Side; float Length, Phase; };

		// The unit icosphere every part is cut from.
		std::vector<Vector3> sphere, lobeSphere;
		std::vector<int> sphereTris, lobeTris;
		std::vector<float> rough;       // static per-vertex roughness (folds, veins)
		std::vector<Color> bodyColors, lobeColors[4];

		std::vector<Lump> lumps;
		std::vector<Mouth> mouths;
		std::vector<Lobe> lobes;
		std::vector<Bone> bones;
		std::vector<Hair> hairs;
		/// US-T192: a tumor's roots, sunk into what it is on (its local -Y faces its host).
		std::vector<Bone> roots;

		// US-T192: what it is, and how it moves: a bloom writhes fast on thick tendrils, a tumor pulses like a heart.
		BioForm form = BioForm::Globule;
		float timeScale = 1, beatAmp = 0, hairWidth = 0.012f, swayAmp = 0.35f;
		Color hairColor{0.10f, 0.06f, 0.04f}, hairTip{0.10f, 0.06f, 0.04f};

		// The mesh's streams, rewritten each shaping.
		std::vector<Vector3> v, n;
		std::vector<Color> c;
		std::vector<int> tris;
	};

	namespace
	{
		constexpr int TeethPerMouth = 11, HairSegments = 4;

		void Icosphere(int levels, std::vector<Vector3>& verts, std::vector<int>& tris)
		{
			const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
			verts = { { -1, t, 0 }, { 1, t, 0 }, { -1, -t, 0 }, { 1, -t, 0 }, { 0, -1, t }, { 0, 1, t }, { 0, -1, -t }, { 0, 1, -t },
				{ t, 0, -1 }, { t, 0, 1 }, { -t, 0, -1 }, { -t, 0, 1 } };
			for (auto& p : verts) p = p.normalized();
			tris = { 0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
				3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1 };
			for (int l = 0; l < levels; l++)
			{
				std::map<std::pair<int, int>, int> mid;
				auto midpoint = [&](int a, int b)
				{
					auto key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
					auto it = mid.find(key);
					if (it != mid.end()) return it->second;
					verts.push_back(((verts[a] + verts[b]) * 0.5f).normalized());
					return mid[key] = static_cast<int>(verts.size()) - 1;
				};
				std::vector<int> next;
				next.reserve(tris.size() * 4);
				for (size_t i = 0; i < tris.size(); i += 3)
				{
					int a = tris[i], b = tris[i + 1], c = tris[i + 2];
					int ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
					next.insert(next.end(), { a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca });
				}
				tris.swap(next);
			}
		}

		Vector3 AnyPerpendicular(Vector3 d)
		{
			Vector3 a = std::fabs(d.y) < 0.9f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
			return Vector3::Cross(d, a).normalized();
		}

		Color Mix(Color a, Color b, float t)
		{
			t = Mathf::Clamp01(t);
			return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
		}

		// The palette (alpha 0 marks an emissive face on the ship shader: the wet glow deep in a mouth).
		constexpr Color Flesh(0.50f, 0.09f, 0.08f), Tissue(0.86f, 0.44f, 0.40f), Fat(0.80f, 0.62f, 0.34f), Vein(0.22f, 0.04f, 0.13f),
			Bruise(0.38f, 0.10f, 0.22f), BoneWhite(0.88f, 0.84f, 0.72f), Tooth(0.95f, 0.92f, 0.80f), HairBrown(0.10f, 0.06f, 0.04f),
			Gullet(0.40f, 0.02f, 0.03f, 0.0f), Lip(0.62f, 0.12f, 0.14f), BloomPink(0.95f, 0.38f, 0.52f), RootDark(0.18f, 0.03f, 0.08f);

		float Smooth(float x) { x = Mathf::Clamp01(x); return x * x * (3 - 2 * x); }

		/// US-T192: a tumor's beat, a double thump (lub-dub) about once a second and a half, 0 between beats.
		float Heartbeat(float t)
		{
			float lub = std::max(0.0f, std::sin(t * 7.0f));
			float dub = std::max(0.0f, std::sin(t * 7.0f - 0.9f));
			float gate = Smooth(std::sin(t * 3.5f) * 4.0f); // one pair a cycle, then rest
			return gate * (std::pow(lub, 10.0f) + 0.6f * std::pow(dub, 10.0f));
		}

		/// How far the surface is (unit radii) along unit direction d at time t: the lumps swelling and sagging, the
		/// peristaltic wave, the mouths sunk into it, and a hit's ripple.
		float Surface(const BioMassView::Shape& s, Vector3 d, float rough, float t, float flinch, Vector3 hitDir)
		{
			float r = 0.86f + rough;
			for (const auto& l : s.lumps)
			{
				float k = Vector3::Dot(d, l.Dir);
				float w = std::exp((k - 1) / l.Width);
				r += l.Amp * w * (0.55f + 0.45f * std::sin(t * l.Freq * 6.2832f + l.Phase));
			}
			// A slow wave rolling round it, like meat settling.
			r += 0.035f * std::sin(5.0f * d.y + 3.0f * d.x - t * 0.9f) + 0.02f * std::sin(7.0f * d.z - t * 1.4f);
			for (const auto& m : s.mouths)
			{
				float k = Vector3::Dot(d, m.Dir);
				float edge = std::cos(m.Size);
				if (k < edge - 0.12f) continue;
				float open = 0.5f + 0.5f * std::sin(t * m.Freq * 6.2832f + m.Phase);
				float inside = Smooth((k - edge) / (1 - edge));
				float lip = Smooth(1 - std::fabs(k - edge) / 0.12f);
				r += 0.07f * lip - (0.12f + 0.28f * open) * inside;
			}
			if (flinch > 0)
			{
				float k = Vector3::Dot(d, hitDir);
				r += flinch * (0.06f * std::sin(18 * k - t * 11) * Smooth((k + 0.2f) / 1.2f) - 0.08f * Smooth(k));
			}
			return r;
		}

		void Prism(BioMassView::Shape& s, const Vector3* ring0, const Vector3* ring1, Color c0, Color c1)
		{
			// Three-sided tube between two rings of three: thin parts (hair, a tooth's flank) seen from any side.
			int base = static_cast<int>(s.v.size());
			for (int i = 0; i < 3; i++) { s.v.push_back(ring0[i]); s.c.push_back(c0); }
			for (int i = 0; i < 3; i++) { s.v.push_back(ring1[i]); s.c.push_back(c1); }
			for (int i = 0; i < 3; i++)
			{
				int j = (i + 1) % 3;
				s.tris.insert(s.tris.end(), { base + i, base + 3 + i, base + j, base + j, base + 3 + i, base + 3 + j });
			}
		}

		void Cone(BioMassView::Shape& s, Vector3 base, Vector3 tip, Vector3 side, float width, Color cBase, Color cTip)
		{
			Vector3 axis = (tip - base).normalized();
			Vector3 u = side.normalized(), w = Vector3::Cross(axis, u).normalized();
			int b = static_cast<int>(s.v.size());
			for (int i = 0; i < 4; i++)
			{
				float a = i * 1.5708f;
				s.v.push_back(base + (u * std::cos(a) + w * std::sin(a)) * width);
				s.c.push_back(cBase);
			}
			s.v.push_back(tip);
			s.c.push_back(cTip);
			for (int i = 0; i < 4; i++) s.tris.insert(s.tris.end(), { b + i, b + 4, b + (i + 1) % 4 });
		}
	}

	std::shared_ptr<BioMassView::Shape> BioMassView::Build(int seed, BioForm form)
	{
		auto s = std::make_shared<Shape>();
		s->form = form;
		std::mt19937 rng(static_cast<uint32_t>(seed) * 2654435761u + 12345u);
		std::uniform_real_distribution<float> u01(0.0f, 1.0f);
		auto dir = [&]
		{
			float z = u01(rng) * 2 - 1, a = u01(rng) * 6.2832f, r = std::sqrt(std::max(0.0f, 1 - z * z));
			return Vector3(r * std::cos(a), r * std::sin(a), z);
		};
		bool bloom = form == BioForm::Bloom, tumor = form == BioForm::Tumor;
		// A bloom is small on screen and there are many: a coarser body.
		Icosphere(bloom ? 2 : 3, s->sphere, s->sphereTris);
		Icosphere(bloom ? 1 : 2, s->lobeSphere, s->lobeTris);

		if (bloom)
		{
			// A small, fast, writhing cluster: a knot of bulbs round one gaping mouth, on long thick tendrils that lash.
			s->timeScale = 3.2f;
			s->hairWidth = 0.05f;
			s->swayAmp = 0.9f;
			s->hairColor = Color(0.62f, 0.14f, 0.30f);
			s->hairTip = Color(1.0f, 0.42f, 0.55f, 0.0f); // the tips glow
			int lumps = 3 + static_cast<int>(u01(rng) * 2);
			for (int i = 0; i < lumps; i++)
				s->lumps.push_back({ dir(), 0.18f + 0.2f * u01(rng), 0.2f + 0.25f * u01(rng), 0.35f + 0.4f * u01(rng), u01(rng) * 6.2832f });
			Vector3 d = dir();
			Vector3 a = AnyPerpendicular(d);
			s->mouths.push_back({ d, a, Vector3::Cross(d, a), 0.45f + 0.12f * u01(rng), u01(rng) * 6.2832f, 0.3f + 0.2f * u01(rng) });
			int lobes = 4 + static_cast<int>(u01(rng) * 3);
			for (int i = 0; i < lobes; i++)
				s->lobes.push_back({ dir(), 0.85f + 0.3f * u01(rng), 0.36f + 0.16f * u01(rng), 0.25f + 0.25f * u01(rng), u01(rng) * 6.2832f });
			int tendrils = 6 + static_cast<int>(u01(rng) * 4);
			for (int i = 0; i < tendrils; i++)
			{
				Vector3 t = dir();
				s->hairs.push_back({ t, AnyPerpendicular(t), 0.9f + 0.7f * u01(rng), u01(rng) * 6.2832f });
			}
		}
		else if (tumor)
		{
			// A large, slow growth: a cauliflower of swellings over a heart-like beat, veined thick, rooted in its host.
			s->timeScale = 0.55f;
			s->beatAmp = 0.07f;
			int lumps = 9 + static_cast<int>(u01(rng) * 4);
			for (int i = 0; i < lumps; i++)
				s->lumps.push_back({ dir(), 0.1f + 0.16f * u01(rng), 0.06f + 0.12f * u01(rng), 0.1f + 0.2f * u01(rng), u01(rng) * 6.2832f });
			int mouths = 1 + static_cast<int>(u01(rng) * 2);
			for (int i = 0; i < mouths; i++)
			{
				Vector3 d = dir();
				if (d.y < 0) d.y = -d.y; // on its free side, away from its host
				d = d.normalized();
				Vector3 a = AnyPerpendicular(d);
				s->mouths.push_back({ d, a, Vector3::Cross(d, a), 0.2f + 0.1f * u01(rng), u01(rng) * 6.2832f, 0.06f + 0.08f * u01(rng) });
			}
			int lobes = 5 + static_cast<int>(u01(rng) * 3);
			for (int i = 0; i < lobes; i++)
			{
				Vector3 d = dir();
				d.y = std::fabs(d.y) * 0.8f - 0.1f; // swelling up and out from where it is rooted
				s->lobes.push_back({ d.normalized(), 0.55f + 0.2f * u01(rng), 0.36f + 0.22f * u01(rng), 0.05f + 0.07f * u01(rng), u01(rng) * 6.2832f });
			}
			int bones = 1 + static_cast<int>(u01(rng) * 2);
			for (int i = 0; i < bones; i++)
			{
				Vector3 d = dir();
				s->bones.push_back({ d, AnyPerpendicular(d), 0.25f + 0.3f * u01(rng), 0.04f + 0.03f * u01(rng) });
			}
			int roots = 6 + static_cast<int>(u01(rng) * 4);
			for (int i = 0; i < roots; i++)
			{
				Vector3 d = dir();
				d.y = -0.35f - 0.6f * std::fabs(d.y); // the lower side, the host's
				d = d.normalized();
				s->roots.push_back({ d, AnyPerpendicular(d), 0.55f + 0.5f * u01(rng), 0.07f + 0.06f * u01(rng) });
			}
		}
		else
		{
			int lumps = 5 + static_cast<int>(u01(rng) * 3);
			for (int i = 0; i < lumps; i++)
				s->lumps.push_back({ dir(), 0.12f + 0.2f * u01(rng), 0.12f + 0.25f * u01(rng), 0.18f + 0.35f * u01(rng), u01(rng) * 6.2832f });
			int mouths = 2 + static_cast<int>(u01(rng) * 2);
			for (int i = 0; i < mouths; i++)
			{
				Vector3 d = dir();
				Vector3 a = AnyPerpendicular(d);
				s->mouths.push_back({ d, a, Vector3::Cross(d, a), 0.28f + 0.14f * u01(rng), u01(rng) * 6.2832f, 0.12f + 0.15f * u01(rng) });
			}
			int lobes = 3 + static_cast<int>(u01(rng) * 2);
			for (int i = 0; i < lobes; i++)
				s->lobes.push_back({ dir(), 0.62f + 0.22f * u01(rng), 0.32f + 0.2f * u01(rng), 0.08f + 0.12f * u01(rng), u01(rng) * 6.2832f });
			int bones = 3 + static_cast<int>(u01(rng) * 3);
			for (int i = 0; i < bones; i++)
			{
				Vector3 d = dir();
				s->bones.push_back({ d, AnyPerpendicular(d), 0.35f + 0.45f * u01(rng), 0.035f + 0.03f * u01(rng) });
			}
			// Hair in a few matted patches.
			for (int p = 0; p < 3; p++)
			{
				Vector3 centre = dir();
				for (int i = 0; i < 24; i++)
				{
					Vector3 d = (centre + dir() * 0.35f).normalized();
					s->hairs.push_back({ d, AnyPerpendicular(d), 0.25f + 0.35f * u01(rng), u01(rng) * 6.2832f });
				}
			}
		}

		// Static roughness and colour: folds and veins from a few random waves, mottled flesh, pale fatty tissue on top
		// of the swellings, bruised round the mouths.
		Vector3 w1 = dir() * (6 + 4 * u01(rng)), w2 = dir() * (11 + 6 * u01(rng)), w3 = dir() * (19 + 8 * u01(rng));
		float p1 = u01(rng) * 6.28f, p2 = u01(rng) * 6.28f, p3 = u01(rng) * 6.28f;
		for (const auto& d : s->sphere)
		{
			float a = std::sin(Vector3::Dot(d, w1) + p1), b = std::sin(Vector3::Dot(d, w2) + p2), c = std::sin(Vector3::Dot(d, w3) + p3);
			s->rough.push_back(0.035f * a + 0.02f * b + 0.01f * c);
			float vein = Smooth(1 - std::fabs(b + 0.5f * c) * 3.0f);
			float tissue = Smooth(0.5f + 0.8f * a);
			float lumpy = 0;
			for (const auto& l : s->lumps) lumpy = std::max(lumpy, std::exp((Vector3::Dot(d, l.Dir) - 1) / l.Width));
			Color col = Mix(Flesh, Tissue, tissue * 0.7f);
			col = Mix(col, Fat, Smooth(lumpy * 1.3f - 0.5f) * 0.6f);
			for (const auto& m : s->mouths)
			{
				float k = Vector3::Dot(d, m.Dir), edge = std::cos(m.Size);
				col = Mix(col, Bruise, Smooth((k - edge + 0.35f) / 0.35f) * 0.5f);
				col = Mix(col, Lip, Smooth(1 - std::fabs(k - edge) / 0.08f));
				if (k > edge + 0.04f) col = Mix(Lip, Gullet, Smooth((k - edge) / (1 - edge) * 2.0f));
			}
			col = Mix(col, Vein, vein * (tumor ? 0.95f : 0.75f));
			// US-T192: a bloom is raw and bright (pink, bruised purple); a tumor pale and swollen, darkening to its roots.
			if (bloom) col = Mix(col, Mix(BloomPink, Bruise, 0.5f - 0.5f * a), 0.45f);
			if (tumor) col = Mix(Mix(col, Fat, 0.25f * tissue), Bruise, Smooth(-d.y * 1.4f) * 0.55f);
			s->bodyColors.push_back(col);
		}
		for (int k = 0; k < 4; k++)
			for (const auto& d : s->lobeSphere)
			{
				float a = std::sin(Vector3::Dot(d, w2) + p1 + k);
				Color col = Mix(Mix(Tissue, Flesh, 0.5f + 0.5f * a), Vein, Smooth(1 - std::fabs(a) * 4) * (tumor ? 0.85f : 0.6f));
				if (bloom) col = Mix(col, BloomPink, 0.4f);
				if (tumor) col = Mix(col, Fat, 0.3f);
				s->lobeColors[k].push_back(col);
			}
		return s;
	}

	void BioMassView::Animate(Shape& s, Mesh& mesh, float t, float flinch, Vector3 hitDir)
	{
		s.v.clear();
		s.c.clear();
		s.tris.clear();
		// US-T192: a bloom moves fast, a tumor slow; a tumor's whole mass swells with its beat (in real seconds).
		float beat = 1 + s.beatAmp * Heartbeat(t);
		t *= s.timeScale;

		// The body.
		for (size_t i = 0; i < s.sphere.size(); i++)
			s.v.push_back(s.sphere[i] * Surface(s, s.sphere[i], s.rough[i], t, flinch, hitDir));
		s.c = s.bodyColors;
		s.tris = s.sphereTris;

		// The lobes, half sunk in it, swelling and sagging out of step with it: the metaball look.
		for (size_t k = 0; k < s.lobes.size(); k++)
		{
			const auto& l = s.lobes[k];
			float breathe = std::sin(t * l.Freq * 6.2832f + l.Phase);
			float dist = l.Dist + 0.08f * breathe, radius = l.Radius * (1 + 0.12f * std::sin(t * l.Freq * 9.0f + l.Phase * 2));
			Vector3 centre = l.Dir * dist;
			int base = static_cast<int>(s.v.size());
			for (const auto& d : s.lobeSphere)
			{
				// Squashed where it presses into the body (flattened toward it), round on its free side.
				float into = Vector3::Dot(d, l.Dir);
				float squash = into < 0 ? 1 + 0.35f * into : 1;
				float ripple = 1 + 0.05f * std::sin(9 * d.x + 7 * d.y - t * 1.1f + l.Phase);
				s.v.push_back(centre + d * (radius * squash * ripple));
			}
			const auto& cols = s.lobeColors[k % 4];
			s.c.insert(s.c.end(), cols.begin(), cols.end());
			for (int idx : s.lobeTris) s.tris.push_back(base + idx);
		}

		// Teeth round every mouth, splayed outward as it opens.
		for (const auto& m : s.mouths)
		{
			float open = 0.5f + 0.5f * std::sin(t * m.Freq * 6.2832f + m.Phase);
			for (int i = 0; i < TeethPerMouth; i++)
			{
				float a = i * 6.2832f / TeethPerMouth + 0.3f * std::sin(i * 2.7f);
				float rim = m.Size * (0.92f + 0.05f * std::sin(i * 1.9f));
				Vector3 d = (m.Dir * std::cos(rim) + (m.U * std::cos(a) + m.V * std::sin(a)) * std::sin(rim)).normalized();
				float r = Surface(s, d, 0, t, flinch, hitDir);
				Vector3 root = d * (r - 0.02f);
				Vector3 inward = (m.Dir * 0.6f - d * 0.2f).normalized();
				Vector3 lean = (d * (0.4f + 0.6f * open) + inward * (1.2f - open)).normalized();
				float len = 0.09f + 0.06f * (0.5f + 0.5f * std::sin(i * 3.3f));
				Cone(s, root, root + lean * len, m.U, 0.022f, Tooth, Tooth);
			}
		}

		// Bone breaking the surface: a long shard with a knuckle where it comes out.
		for (const auto& b : s.bones)
		{
			float r = Surface(s, b.Dir, 0, t, flinch, hitDir);
			Vector3 root = b.Dir * (r - 0.1f);
			Vector3 tip = b.Dir * (r + b.Length) + b.Side * (b.Length * 0.25f);
			Cone(s, root, tip, b.Side, b.Width, BoneWhite, Mix(BoneWhite, Tooth, 0.5f));
			Cone(s, root, b.Dir * (r + 0.05f), b.Side, b.Width * 2.2f, Mix(BoneWhite, Flesh, 0.3f), BoneWhite);
		}

		// US-T192: a tumor's roots, thick and dark, sunk down into its host (its local -Y), creeping as it beats.
		for (const auto& b : s.roots)
		{
			float r = Surface(s, b.Dir, 0, t, flinch, hitDir);
			Vector3 root = b.Dir * (r - 0.15f);
			float creep = 0.12f * std::sin(t * 0.8f + b.Length * 9.0f);
			Vector3 bend = (b.Dir + Vector3(0, -1.0f, 0) + b.Side * creep).normalized();
			Vector3 knee = root + bend * (b.Length * 0.55f);
			Vector3 tip = knee + (bend + Vector3(0, -0.6f, 0) - b.Side * creep).normalized() * (b.Length * 0.5f);
			Cone(s, root, knee, b.Side, b.Width, Bruise, Mix(Bruise, RootDark, 0.6f));
			Cone(s, knee - bend * (b.Length * 0.08f), tip, b.Side, b.Width * 0.55f, Mix(Bruise, RootDark, 0.6f), RootDark);
		}

		// Hair, swaying slowly as the meat under it moves; on a bloom, thick tendrils that lash and curl (US-T192).
		bool bloom = s.form == BioForm::Bloom;
		int segments = bloom ? 7 : HairSegments;
		for (const auto& h : s.hairs)
		{
			float r = Surface(s, h.Dir, 0, t, flinch, hitDir);
			Vector3 side = h.Side, other = Vector3::Cross(h.Dir, side);
			Vector3 prev[3], point = h.Dir * (r - 0.01f);
			float w = s.hairWidth;
			for (int k = 0; k < 3; k++)
			{
				float a = k * 2.094f;
				prev[k] = point + (side * std::cos(a) + other * std::sin(a)) * w;
			}
			Color cPrev = s.hairColor;
			for (int seg = 1; seg <= segments; seg++)
			{
				float f = static_cast<float>(seg) / segments;
				float sway = std::sin(t * 0.7f + h.Phase + f * 1.6f) * s.swayAmp * f * f;
				float curl = bloom ? 0.7f * f * std::sin(t * 1.1f + h.Phase * 2 + f * 3.0f) : 0.25f * f * std::sin(h.Phase * 3);
				Vector3 along = (h.Dir + side * sway + other * curl).normalized();
				point = point + along * (h.Length / segments);
				Vector3 ring[3];
				float wk = w * (1 - 0.8f * f);
				for (int k = 0; k < 3; k++)
				{
					float a = k * 2.094f;
					ring[k] = point + (side * std::cos(a) + other * std::sin(a)) * wk;
				}
				Color c = bloom ? Mix(s.hairColor, s.hairTip, f * f) : s.hairColor;
				Prism(s, prev, ring, cPrev, c);
				cPrev = c;
				for (int k = 0; k < 3; k++) prev[k] = ring[k];
			}
		}

		if (beat != 1)
			for (auto& p : s.v) p = p * beat;

		mesh.indexFormat = s.v.size() > 65000 ? IndexFormat::UInt32 : IndexFormat::UInt16;
		mesh.SetVertices(s.v);
		mesh.SetColors(s.c);
		mesh.SetTriangles(s.tris, 0, false);
		mesh.RecalculateNormals();
		Bounds b;
		b.center = Vector3::zero();
		b.extents = Vector3::one() * (bloom ? 3.0f : s.form == BioForm::Tumor ? 2.4f : 2.0f);
		mesh.SetBounds(b);
	}

	// ---------------------------------------------------------------- the view

	cs::Ref<Material>& BioMassView::material()
	{
		static cs::Ref<Material> value;
		return value;
	}

	BioMassView::BioMassView(UnitySpaceApp* app_, const cs::Ref<Transform>& parent_) : app(app_), parent(parent_)
	{
		if (!Alive(material()))
		{
			auto m = Materials::Create(Materials::Ship(), "BioMass");
			m->SetFloat("_UseVertexColor", 1);
			m->SetColor("_EngineColor", Color(0.95f, 0.18f, 0.10f));
			m->SetFloat("_EngineGlow", 0.7f);
			material() = Materials::MarkShared(m);
		}
	}

	BioMassView::~BioMassView() = default;

	cs::Ref<Ship> BioMassView::HostShip(UnitySpaceApp* app, const BioGlobule& g)
	{
		if (g.Host != BioHost::Ship) return nullptr;
		auto world = app->World();
		if (world == nullptr || world->Fleet == nullptr) return nullptr;
		const auto& ships = world->Fleet->Ships();
		// A hull's Id is its index in the fleet: the quick look first, the search only if that ever stops holding.
		cs::Ref<Ship> s;
		if (g.HostId >= 0 && g.HostId < ships.Count() && ships[g.HostId] != nullptr && ships[g.HostId]->Id == g.HostId) s = ships[g.HostId];
		else
			for (const auto& c : ships)
				if (c != nullptr && c->Id == g.HostId) { s = c; break; }
		return s != nullptr && !s->Destroyed ? s : nullptr;
	}

	BioMassView::Placement BioMassView::Place(UnitySpaceApp* app, const BioGlobule& g)
	{
		Placement p;
		p.Radius = static_cast<float>(app->Scale()->ShipLength(g.RadiusKm()));
		// US-T192: a growth on a hull rides the hull as it is drawn: a spot on its skin (from its seed) in the hull's own
		// frame, so it turns with it, half sunk in, and no bigger than the hull can carry.
		if (auto ship = HostShip(app, g))
		{
			auto target = Target::OfShip(ship);
			DVec3 centre = app->FocusScene(target);
			Quaternion rot = app->TargetRotation(target);
			float len = static_cast<float>(app->ShipDisplayLength(ship));
			uint32_t h = static_cast<uint32_t>(g.Seed) * 2654435761u;
			float z = ((h & 0xffffu) / 65535.0f) * 2 - 1, a = ((h >> 16) / 65535.0f) * 6.2832f, rr = std::sqrt(std::max(0.0f, 1 - z * z));
			Vector3 d(rr * std::cos(a), rr * std::sin(a), z);
			const Vector3 half(0.11f, 0.08f, 0.42f); // the hull taken as a long ellipsoid, nose along +Z
			Vector3 skin(d.x * half.x, d.y * half.y, d.z * half.z);
			Vector3 normal = Vector3(d.x / half.x, d.y / half.y, d.z / half.z).normalized();
			p.Radius = std::min(p.Radius, len * (g.Form == BioForm::Bloom ? 0.14f : 0.42f));
			Vector3 local = skin * len + normal * (p.Radius * 0.35f);
			Vector3 off = rot * local;
			p.Scene = centre + DVec3(off.x, off.y, off.z);
			p.Down = -(rot * normal);
			p.Rooted = true;
			return p;
		}
		// On a station or asteroid: on the body as it is drawn, on the side it came in from.
		if (g.Host == BioHost::Body && g.HostId >= 0 && g.HostId < app->Catalog()->Count() && g.Anchor.LengthSquared() > 0)
		{
			double R = app->BodyDisplayRadius(g.HostId);
			if (R > 0)
			{
				DVec3 out = g.Anchor.Normalized().SwapYZ();
				p.Scene = app->BodyScene(g.HostId) + out * (R + p.Radius * 0.45);
				p.Down = Vector3(static_cast<float>(-out.X), static_cast<float>(-out.Y), static_cast<float>(-out.Z));
				p.Rooted = true;
				return p;
			}
		}
		p.Scene = app->BodyScene(g.Body) + app->Scale()->LocalOffset((*app->Catalog())[g.Body], g.Anchor) + g.Local.SwapYZ() * app->BattleKmScale();
		if (g.Host == BioHost::Body && g.HostId >= 0 && g.HostId < app->Catalog()->Count())
		{
			DVec3 to = app->BodyScene(g.HostId) - p.Scene;
			if (to.LengthSquared() > 0)
			{
				to = to.Normalized();
				p.Down = Vector3(static_cast<float>(to.X), static_cast<float>(to.Y), static_cast<float>(to.Z));
				p.Rooted = true;
			}
		}
		return p;
	}

	DVec3 BioMassView::SceneOf(UnitySpaceApp* app, const BioGlobule& g) { return Place(app, g).Scene; }

	BioMassView::View BioMassView::Create(BioGlobule* g)
	{
		View v;
		v.Go = NewGameObject(cs::Cat("Bio Mass ", g->Id));
		v.Go->transform->SetParent(parent, false);
		v.Body = Materials::Own(cs::New<::SpaceGod::UnityCompat::Mesh>(cs::Cat("Bio Mass ", g->Id)));
		v.Body->MarkDynamic();
		v.Kind = g->Form;
		v.Form = Build(g->Seed, g->Form);
		Animate(*v.Form, *v.Body, Time::unscaledTime + (g->Seed % 1000) * 0.01f, 0, Vector3(0, 1, 0));
		AddComponent<MeshFilter>(v.Go)->sharedMesh = v.Body;
		auto mr = AddComponent<MeshRenderer>(v.Go);
		mr->SetSharedMaterial(material());
		mr->shadowCastingMode = ShadowCastingMode::Off;
		return v;
	}

	void BioMassView::Tracer(Vector3 from, Vector3 to, Color color)
	{
		if (tracersUsed >= tracers.Count()) tracers.Add(cs::New<LineStrip>("Bio Mass tracer", parent));
		auto line = tracers[tracersUsed++];
		cs::List<Vector3> pts = { from, to };
		line->SetPoints(pts, 2, color, 0.25f, 1.0f);
		line->SetVisible(true);
	}

	void BioMassView::Update()
	{
		tracersUsed = 0;
		auto world = app->World();
		auto bio = world ? world->BioMass : nullptr;
		double jd = app->Clock->Jd;
		float now = Time::unscaledTime;
		double ppu = app->PixelsPerUnitAtUnitDistance();
		auto cat = app->Catalog();
		auto eph = app->Ephemeris();

		std::unordered_map<BioGlobule*, bool> seen;
		if (bio != nullptr && cat != nullptr)
			for (const auto& g : bio->Globules)
			{
				if (!g->Alive() || g->Body < 0 || g->Body >= cat->Count()) continue;
				seen[g.get()] = true;
				auto it = views.find(g.get());
				if (it == views.end())
				{
					it = views.emplace(g.get(), Create(g.get())).first;
					it->second.Owner = g;
				}
				auto& v = it->second;
				// US-T192: a bloom that took root is a tumor now: a new model for its new shape.
				if (v.Kind != g->Form)
				{
					v.Kind = g->Form;
					v.Form = Build(g->Seed, g->Form);
					v.LastShaped = -1;
					Animate(*v.Form, *v.Body, now + (g->Seed % 1000) * 0.01f, 0, Vector3(0, 1, 0));
				}
				auto place = Place(app, *g);
				DVec3 scene = place.Scene;
				Vector3 rel = app->Relative(scene);
				float radius = place.Radius;
				float px = static_cast<float>(radius * 2 * ppu / Mathf::Max(rel.magnitude(), 1e-6f));
				bool shown = px > 1.5f;
				if (v.Go->activeSelf != shown) v.Go->SetActive(shown);
				if (!shown) continue;

				// A hit lately (in sim time, so a paused game holds still): the meat flinches away from it.
				double since = (jd - g->LastHitJd) * 86400.0;
				bool hit = !cs::Double::IsNaN(g->LastHitJd) && since >= 0 && since < 3.0;
				float flinch = hit ? 1.0f : 0.0f;
				DVec3 fromScene = scene;
				if (hit)
				{
					switch (g->LastShooter)
					{
						case BioShooter::Orbital:
							if (g->LastShooterIndex >= 0 && g->LastShooterIndex < cat->Count()) fromScene = app->BodyScene(g->LastShooterIndex);
							break;
						case BioShooter::Gate:
							if (g->LastShooterIndex >= 0 && g->LastShooterIndex < world->Gates.Count())
								fromScene = app->BodyScene(0) + app->Scale()->LocalOffset(cat->Root(), world->Gates[g->LastShooterIndex]->PositionKm - eph->Helio(0));
							break;
						case BioShooter::Ship:
							for (const auto& s : world->Fleet->Ships())
								if (s->Id == g->LastShooterIndex && !s->Destroyed) { fromScene = app->ShipScene(s); break; }
							break;
						default: break;
					}
				}
				Vector3 hitDir = Vector3(0, 1, 0);
				if (hit && (fromScene - scene).Length() > 0)
				{
					auto d = (fromScene - scene).Normalized();
					hitDir = Vector3(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z));
				}

				// Re-shaped while it is big enough on screen to see it move (at most 30 times a second); a small one breathes.
				float t = now + (g->Seed % 1000) * 0.01f;
				bool near = px > 40;
				if (near && now - v.LastShaped > 1.0f / 30.0f)
				{
					Animate(*v.Form, *v.Body, t, flinch, hitDir);
					v.LastShaped = now;
				}
				// A small one breathes (a tumor beats, a bloom quivers fast); a big one moves in its mesh.
				float breathe = near ? 1.0f
					: g->Form == BioForm::Tumor ? 1.0f + 0.07f * Heartbeat(t)
					: g->Form == BioForm::Bloom ? 1.0f + 0.08f * std::sin(t * 6.5f)
					: 1.0f + 0.06f * std::sin(t * 1.3f);
				auto tr = v.Go->transform;
				tr->localPosition = rel;
				float spin = static_cast<float>(g->Seed % 360);
				if (place.Rooted && place.Down.sqrMagnitude() > 0)
				{
					// Latched on: its roots (local -Y) into its host; a bloom twists where it bites, a tumor holds still.
					float twist = g->Form == BioForm::Bloom ? spin + 40.0f * std::sin(t * 2.1f) : spin;
					tr->localRotation = Quaternion::FromToRotation(Vector3(0, -1, 0), place.Down.normalized()) * Quaternion::AngleAxis(twist, Vector3(0, 1, 0));
				}
				else if (g->Form == BioForm::Bloom)
					tr->localRotation = Quaternion::Euler(t * 31.0f, t * 47.0f + spin, t * 13.0f); // darting, tumbling
				else
					tr->localRotation = Quaternion::Euler(t * 1.5f, t * 2.3f + spin, 0);
				tr->localScale = Vector3::one() * (radius * breathe);

				// Tracers: from a ring of gun satellites round the body, from the gate, or from the hull.
				if (hit && g->LastShooter != BioShooter::None)
				{
					Color c = g->LastShooter == BioShooter::Ship ? Color(1.0f, 0.85f, 0.4f) : Color(1.0f, 0.55f, 0.25f);
					Vector3 from = app->Relative(fromScene);
					bool pulse = Mathf::Repeat(now * 7.0f + g->Id, 1.0f) < 0.6f;
					if (g->LastShooter == BioShooter::Orbital && g->LastShooterIndex >= 0)
					{
						float body = static_cast<float>(app->BodyDisplayRadius(g->LastShooterIndex));
						Vector3 to = (rel - from).normalized();
						Vector3 a = Vector3::Cross(to, Vector3(0, 1, 0)).normalized(), b = Vector3::Cross(to, a);
						int guns = bio->BodyGunCount(g->LastShooterIndex);
						for (int k = 0; k < guns; k++)
						{
							float ang = k * 6.2832f / guns + now * 0.05f;
							Vector3 gun = from + to * (body * 1.15f) + (a * std::cos(ang) + b * std::sin(ang)) * (body * 0.9f);
							if (pulse || k % 2 == 0) Tracer(gun, rel, c);
						}
					}
					else if (pulse) Tracer(from, rel, c);
				}
			}

		for (int i = tracersUsed; i < tracers.Count(); i++) tracers[i]->SetVisible(false);
		for (auto it = views.begin(); it != views.end();)
		{
			if (seen.count(it->first)) { ++it; continue; }
			Materials::DestroyMesh(it->second.Body);
			Destroy(it->second.Go);
			it = views.erase(it);
		}
	}
}
