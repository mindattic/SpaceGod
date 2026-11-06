// US-T191: the hyperspace toll (see Rendering/HyperspaceToll.h). Procedural only: veins grown from a fixed seed, the
// eyes, the soft shapes and the vignette drawn into textures made here.

#include "Rendering/HyperspaceToll.h"

#include "UnitySpaceApp.h"
#include "ModelViewer.h"
#include "Streaming.h"
#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "UnityCompat/GuiHost.h"
#include "UnityCompat/UnityRuntime.h"
#include "UnityCompat/UnityScene.h"
#include "UnityCompat/UnityTexture.h"

#include "Fleet.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Systems/DeepSpace.h"
#include "Systems/Galaxy.h"

#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace SpaceGod::Rendering
{
	using namespace SpaceGod::UnityCompat;

	namespace
	{
		TAutoConsoleVariable<int32> CVarToll(TEXT("sg.HyperspaceToll"), 1,
			TEXT("US-T191: the hyperspace toll, the veins and darkness that creep in with time spent in warp (0 off)."));
		TAutoConsoleVariable<float> CVarTollForce(TEXT("sg.HyperspaceTollForce"), -1.0f,
			TEXT("US-T191: hold the hyperspace toll's severity at this value, 0..1 (-1: as the exposure has it)."));

		constexpr float Pi = 3.14159265f;

		float Clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
		float Lerp(float a, float b, float t) { return a + (b - a) * t; }
		float Smooth(float e0, float e1, float x)
		{
			float t = Clamp01((x - e0) / (e1 - e0));
			return t * t * (3 - 2 * t);
		}
		float Frac(float x) { return x - std::floor(x); }
		float Hash(float k) { return Frac(std::sin(k * 12.9898f + 4.1414f) * 43758.5453f); }

		/// <summary>How far out a point of the screen is (u in -1..1 each way, the screen's edges at 1): a rounded square, so
		/// every edge goes first and the corners not long before.</summary>
		float Reach(Vector2 u)
		{
			float x = std::fabs(u.x), y = std::fabs(u.y);
			return std::pow(x * x * x * x + y * y * y * y, 0.25f);
		}

		/// <summary>A small deterministic generator, so the veins grow the same every run.</summary>
		struct Rng
		{
			uint32_t s;
			float Next()
			{
				s ^= s << 13; s ^= s >> 17; s ^= s << 5;
				return (s & 0xFFFFFF) / 16777216.0f;
			}
			float Range(float a, float b) { return a + (b - a) * Next(); }
		};

		struct Segment
		{
			Vector2 a, b;   // screen-relative, u
			float ra, rb;   // their reach
			float thick;    // px at 1080 lines
			float seed;     // the vein's own, for its uneven creep
			int depth;      // 0 trunk, more for branches
		};

		struct State
		{
			HyperspaceExposure exposure;
			float beatPhase = 0, beat = 0, time = 0;
			bool registered = false, hidden = true, enabled = true;
			std::vector<Segment> veins;
			cs::Ref<Texture2D> line, vignette, disc, sclera, iris;
			TWeakObjectPtr<APostProcessVolume> volume;
		};

		State& S()
		{
			static State s;
			return s;
		}

		// ---------------------------------------------------------------- the veins

		void Grow(std::vector<Segment>& out, Rng& rng, Vector2 p, Vector2 dir, float thick, int depth, int steps, float seed)
		{
			for (int i = 0; i < steps && out.size() < 1500; i++)
			{
				// Wandering, drawn on toward the centre, never straight.
				Vector2 centre = Vector2(-p.x, -p.y);
				float cl = std::sqrt(centre.x * centre.x + centre.y * centre.y);
				if (cl > 1e-4f) centre = Vector2(centre.x / cl, centre.y / cl);
				float turn = rng.Range(-0.55f, 0.55f);
				float c = std::cos(turn), s = std::sin(turn);
				Vector2 d = Vector2(dir.x * c - dir.y * s, dir.x * s + dir.y * c);
				d = Vector2(d.x * 0.84f + centre.x * 0.16f, d.y * 0.84f + centre.y * 0.16f);
				float dl = std::sqrt(d.x * d.x + d.y * d.y);
				dir = Vector2(d.x / dl, d.y / dl);
				float step = 0.042f * rng.Range(0.7f, 1.3f) * (depth == 0 ? 1.0f : 0.8f);
				Vector2 q = Vector2(p.x + dir.x * step, p.y + dir.y * step);
				float taper = 1.0f - 0.6f * static_cast<float>(i) / static_cast<float>(steps);
				out.push_back({ p, q, Reach(p), Reach(q), thick * taper, seed, depth });
				if (Reach(q) < 0.1f) return;
				// Branching, fewer and finer the further from the trunk.
				if (depth < 3 && rng.Next() < (depth == 0 ? 0.17f : 0.12f))
				{
					float side = rng.Next() < 0.5f ? -1.0f : 1.0f;
					float a = side * rng.Range(0.45f, 1.05f);
					Vector2 bd = Vector2(dir.x * std::cos(a) - dir.y * std::sin(a), dir.x * std::sin(a) + dir.y * std::cos(a));
					int left = steps - i;
					Grow(out, rng, q, bd, thick * taper * 0.62f, depth + 1, std::max(3, static_cast<int>(left * rng.Range(0.35f, 0.7f))), seed + rng.Next());
				}
				p = q;
			}
		}

		void BuildVeins(State& st)
		{
			Rng rng{ 0x9E3779B9u };
			constexpr int Roots = 30;
			constexpr float Edge = 1.04f, Perimeter = 8 * Edge;
			for (int k = 0; k < Roots; k++)
			{
				// Spread round the edge of the screen, a little unevenly.
				float along = (k + rng.Range(0.0f, 0.7f)) / Roots * Perimeter;
				Vector2 p;
				if (along < 2 * Edge) p = Vector2(-Edge + along, -Edge);
				else if (along < 4 * Edge) p = Vector2(Edge, -Edge + (along - 2 * Edge));
				else if (along < 6 * Edge) p = Vector2(Edge - (along - 4 * Edge), Edge);
				else p = Vector2(-Edge, Edge - (along - 6 * Edge));
				float l = std::sqrt(p.x * p.x + p.y * p.y);
				Vector2 dir = Vector2(-p.x / l, -p.y / l);
				Grow(st.veins, rng, p, dir, rng.Range(4.5f, 8.5f), 0, static_cast<int>(rng.Range(20, 30)), rng.Next() * 100.0f);
			}
		}

		// ---------------------------------------------------------------- the textures

		cs::Ref<Texture2D> MakeTexture(int w, int h)
		{
			auto t = cs::New<Texture2D>(w, h, TextureFormat::RGBA32, false);
			t->wrapMode = TextureWrapMode::Clamp;
			t->filterMode = FilterMode::Bilinear;
			t->hideFlags = HideFlags::DontSave;
			return t;
		}

		void BuildTextures(State& st)
		{
			// A soft line: opaque along its middle, fading to nothing at its sides (organic, not a hard quad).
			st.line = MakeTexture(4, 32);
			for (int y = 0; y < 32; y++)
			{
				float v = (y + 0.5f) / 16.0f - 1.0f;
				float a = std::exp(-v * v * 3.2f);
				for (int x = 0; x < 4; x++) st.line->SetPixel(x, y, Color(1, 1, 1, a));
			}
			st.line->Apply(false);

			// The darkness: clear in the middle, closing over the edges (a rounded square, as the veins reach).
			constexpr int V = 128;
			st.vignette = MakeTexture(V, V);
			for (int y = 0; y < V; y++)
				for (int x = 0; x < V; x++)
				{
					Vector2 u = Vector2((x + 0.5f) / V * 2 - 1, (y + 0.5f) / V * 2 - 1);
					float a = Smooth(0.38f, 1.0f, Reach(u));
					st.vignette->SetPixel(x, y, Color(1, 1, 1, a));
				}
			st.vignette->Apply(false);

			// A soft disc, for the shapes looming at the edges.
			constexpr int D = 64;
			st.disc = MakeTexture(D, D);
			for (int y = 0; y < D; y++)
				for (int x = 0; x < D; x++)
				{
					float dx = (x + 0.5f) / D * 2 - 1, dy = (y + 0.5f) / D * 2 - 1;
					float r = std::sqrt(dx * dx + dy * dy);
					float a = Clamp01(1 - r);
					st.disc->SetPixel(x, y, Color(1, 1, 1, a * a));
				}
			st.disc->Apply(false);

			// An eye's white: an almond, sickly and bloodshot toward its lids.
			constexpr int EW = 64, EH = 32;
			st.sclera = MakeTexture(EW, EH);
			for (int y = 0; y < EH; y++)
				for (int x = 0; x < EW; x++)
				{
					float nx = (x + 0.5f) / EW * 2 - 1, ny = (y + 0.5f) / EH * 2 - 1;
					float lid = std::pow(Clamp01(1 - nx * nx), 1.1f);
					float inside = lid - std::fabs(ny);
					float a = Clamp01(inside / 0.18f) * Clamp01((1 - std::fabs(nx)) / 0.08f);
					float shade = lid > 0 ? Clamp01(inside / lid) : 0;
					float veins = 0.5f + 0.5f * std::sin(nx * 31 + std::sin(ny * 9) * 3);
					Color c = Color(0.78f, 0.72f * (0.45f + 0.55f * shade) * (0.85f + 0.15f * veins), 0.46f * (0.3f + 0.7f * shade), a);
					st.sclera->SetPixel(x, y, c);
				}
			st.sclera->Apply(false);

			// The iris: amber going to blood at its heart, streaked, with a goat's slit for a pupil.
			constexpr int I = 48;
			st.iris = MakeTexture(I, I);
			for (int y = 0; y < I; y++)
				for (int x = 0; x < I; x++)
				{
					float nx = (x + 0.5f) / I * 2 - 1, ny = (y + 0.5f) / I * 2 - 1;
					float r = std::sqrt(nx * nx + ny * ny);
					float a = Clamp01((1 - r) / 0.1f);
					float streak = 0.8f + 0.2f * std::sin(std::atan2(ny, nx) * 23);
					Color c = Color(Lerp(0.72f, 0.98f, r) * streak, Lerp(0.06f, 0.6f, r * r) * streak, Lerp(0.03f, 0.1f, r), a);
					float slit = 0.17f * std::sqrt(Clamp01(1 - ny * ny));
					if (std::fabs(nx) < slit) c = Color(0, 0, 0, a);
					st.iris->SetPixel(x, y, c);
				}
			st.iris->Apply(false);
		}

		// ---------------------------------------------------------------- drawing

		Vector2 ToScreen(Vector2 u)
		{
			return Vector2(Screen::width * 0.5f * (1 + u.x), Screen::height * 0.5f * (1 + u.y));
		}

		/// <summary>The slow writhing of the veins: a smooth field over the screen, so a node shared by two segments moves as one.</summary>
		Vector2 Writhe(Vector2 p, float t, float amp)
		{
			float ox = std::sin(p.y * 9.1f + t * 0.83f) + 0.5f * std::sin(p.x * 17.3f - t * 1.37f + p.y * 4);
			float oy = std::sin(p.x * 8.3f - t * 0.71f) + 0.5f * std::sin(p.y * 15.7f + t * 1.21f - p.x * 3);
			return Vector2(p.x + ox * amp, p.y + oy * amp);
		}

		void Fill(Color c)
		{
			if (c.a < 0.002f) return;
			GUI::color = c;
			GUI::DrawTexture(Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height)), Texture2D::whiteTexture());
		}

		void Centred(const cs::Ref<Texture2D>& tex, Vector2 centre, float w, float h, Color c)
		{
			if (c.a < 0.002f) return;
			GUI::color = c;
			GUI::DrawTexture(Rect(centre.x - w * 0.5f, centre.y - h * 0.5f, w, h), tex);
		}

		void Stroke(const cs::Ref<Texture2D>& tex, Vector2 a, Vector2 b, float thickness, Color c)
		{
			float dx = b.x - a.x, dy = b.y - a.y;
			float length = std::sqrt(dx * dx + dy * dy);
			if (length < 0.5f || c.a < 0.002f) return;
			GuiMatrix matrix = GUI::matrix;
			GUIUtility::RotateAroundPivot(std::atan2(dy, dx) * 180.0f / Pi, a);
			GUI::color = c;
			// Run on a little past each end so the joints of a vein close up.
			float over = thickness * 0.35f;
			GUI::DrawTexture(Rect(a.x - over, a.y - thickness * 0.5f, length + 2 * over, thickness), tex);
			GUI::matrix = matrix;
		}
	}

	// ---------------------------------------------------------------- exposure

	void HyperspaceExposure::Update(float intensity, float dt)
	{
		if (dt <= 0) return;
		intensity = Clamp01(intensity);
		if (intensity > 0) Seconds = std::min(MaxSeconds, Seconds + intensity * dt);
		else Seconds = std::max(0.0f, Seconds - dt * (DecayFloor + DecayShare * Seconds));
		// Eased, so a sudden start or end creeps rather than snaps (faster on the way in than out).
		float target = Target();
		float rate = target > Shown ? 2.5f : 1.2f;
		Shown += (target - Shown) * Clamp01(rate * dt);
		if (Shown < 1e-4f && target == 0) Shown = 0;
	}

	float HyperspaceExposure::Target() const
	{
		return Seconds <= 0 ? 0.0f : 1.0f - std::exp(-Seconds / GrowSeconds);
	}

	// ---------------------------------------------------------------- the frame

	float HyperspaceToll::Intensity(UnitySpaceApp& app)
	{
		float i = 0;
		// The camera's own trip: through the gates, or across the stars as hard as it warps.
		if (app.Travel != nullptr && app.Travel->Active)
			i = std::max(i, app.Travel->ThroughGate ? 1.0f : 0.35f + 0.65f * Clamp01(app.Travel->Warp()));
		// The hull's own FTL drive: out between the stars, not pulled out by a well nor waiting at a void stop.
		if (app.InFtlCruise() && app.Clock != nullptr)
		{
			const auto& c = app.FtlCruise;
			if (!c->Crossing && !c->AtVoidStop(app.Clock->Jd)) i = 1;
		}
		// Through a gate at the helm.
		if (app.HelmGateGlow() > 0.001f) i = 1;
		// A ship followed in the god view that no system holds: inside a gate, or out between the stars on its own drive.
		if (i < 1 && !app.IsPiloting() && app.SpaceCamera != nullptr && app.Galaxy != nullptr &&
			app.SpaceCamera->Mode == Cameras::SpaceCamera::CameraMode::Focus && app.SpaceCamera->FocusTarget.IsShip())
		{
			const auto& ship = app.SpaceCamera->FocusTarget.Ship;
			if (ship != nullptr && !ship->Destroyed)
			{
				bool held = false;
				for (const auto& s : app.Galaxy->Systems)
				{
					const auto& ships = s->World->Fleet->Ships();
					if (ship->Id >= 0 && ship->Id < ships.Count() && ships[ship->Id] == ship) { held = true; break; }
				}
				if (!held) i = 1;
			}
		}
		return i;
	}

	void HyperspaceToll::Tick(UnitySpaceApp* app, float dt)
	{
		State& st = S();
		if (!st.registered)
		{
			st.registered = true;
			// Under the app's HUD (order 0), which stays readable over it.
			GuiHost::Register(&st, [] { Draw(); }, -10);
		}
		st.enabled = CVarToll.GetValueOnGameThread() != 0;
		dt = std::min(std::max(dt, 0.0f), 0.1f) * Time::timeScale; // the pause menu holds it where it is
		st.hidden = app == nullptr || !app->enabled || ModelViewer::CapturesInput();
		if (app == nullptr) st.exposure.Reset();
		else st.exposure.Update(st.hidden ? 0.0f : Intensity(*app), dt);

		float sev = Severity();
		st.time += dt;
		// The heartbeat: quicker and harder the deeper the toll.
		float period = Lerp(1.25f, 0.62f, sev);
		st.beatPhase = Frac(st.beatPhase + dt / period);
		float p = st.beatPhase;
		float lub = std::exp(-std::pow(std::min(p, 1 - p) / 0.05f, 2.0f));
		float dub = std::exp(-std::pow((p - 0.2f) / 0.06f, 2.0f));
		st.beat = Clamp01(lub + 0.65f * dub);

		// The view itself: colour drained out, its edges split, a vignette and grain, in a volume of its own over the game's look.
		UWorld* world = UnityScene::World();
		if (!world) return;
		APostProcessVolume* v = st.volume.IsValid() && st.volume->GetWorld() == world ? st.volume.Get() : nullptr;
		if (!v && sev > 0.001f)
		{
			FActorSpawnParameters spawn;
			spawn.ObjectFlags |= RF_Transient;
			v = world->SpawnActor<APostProcessVolume>(spawn);
			if (!v) return;
			v->bUnbound = true;
			v->Priority = 1000; // over the render backend's (100)
			st.volume = v;
		}
		if (!v) return;
		v->BlendWeight = st.hidden || !st.enabled ? 0.0f : sev;
		FPostProcessSettings& s = v->Settings;
		s.bOverride_ColorSaturation = true;
		s.ColorSaturation = FVector4(0.18f, 0.14f, 0.2f, 1);
		s.bOverride_SceneFringeIntensity = true;
		s.SceneFringeIntensity = 5.0f * (0.55f + 0.45f * st.beat);
		s.bOverride_VignetteIntensity = true;
		s.VignetteIntensity = 1.0f;
		s.bOverride_FilmGrainIntensity = true;
		s.FilmGrainIntensity = 0.55f;
	}

	float HyperspaceToll::Severity()
	{
		float force = CVarTollForce.GetValueOnGameThread();
		if (force >= 0) return Clamp01(force);
		return S().exposure.Severity();
	}

	float HyperspaceToll::Beat() { return S().beat; }

	void HyperspaceToll::Draw()
	{
		State& st = S();
		if (Event::current().type != EventType::Repaint || st.hidden || !st.enabled) return;
		float sev = Severity();
		if (sev < 0.003f) return;
		if (st.veins.empty()) BuildVeins(st);
		if (st.line == nullptr) BuildTextures(st);

		Color oldColor = GUI::color;
		GuiMatrix oldMatrix = GUI::matrix;
		float W = static_cast<float>(Screen::width), H = static_cast<float>(Screen::height);
		float px = H / 1080.0f;
		float t = st.time, beat = st.beat;
		Vector2 centre = Vector2(W * 0.5f, H * 0.5f);
		float ease = std::pow(sev, 0.8f);
		// Where the veins have crept to: from just off the edges to almost the middle of the screen.
		float front = Lerp(1.06f, 0.13f, ease);

		// Shapes looming at the edges, slow and vast, breathing.
		for (int i = 0; i < 7; i++)
		{
			float h = Hash(i + 1.7f);
			float a = (i / 7.0f + h * 0.1f) * 2 * Pi + t * 0.012f * (h < 0.5f ? -1 : 1);
			Vector2 u = Vector2(std::cos(a), std::sin(a));
			u = Vector2(u.x / Reach(u) * 1.02f, u.y / Reach(u) * 1.02f);
			float size = H * Lerp(0.45f, 0.85f, Hash(i + 9.3f)) * (1 + 0.08f * std::sin(t * 0.4f + i * 1.3f)) * (0.6f + 0.6f * ease);
			Centred(st.disc, ToScreen(u), size * 1.25f, size, Color(0.012f, 0.0f, 0.022f, Clamp01(sev * 1.4f) * 0.5f));
		}

		// The darkness closing in behind the veins, swelling with each beat, its edge split red.
		float k = std::max(1.0f, (front + 0.08f) / 0.38f) * (1 - 0.025f * beat * sev);
		float dark = std::pow(sev, 0.6f);
		Centred(st.vignette, centre, W * k * 1.04f, H * k * 1.04f, Color(0.45f, 0.0f, 0.04f, dark * 0.35f));
		Centred(st.vignette, centre, W * k, H * k, Color(0.025f, 0.0f, 0.03f, dark * 0.96f));
		Centred(st.vignette, centre, W * k * 0.975f, H * k * 0.975f, Color(0.0f, 0.3f, 0.35f, dark * 0.08f));
		Fill(Color(0.035f, 0.0f, 0.03f, 0.38f * sev * sev));

		// The veins: a dark halo, then the vessel itself, livid on the beat. Each creeps on at its own uneven pace.
		float amp = 0.005f + 0.011f * sev;
		float swell = 1 + 0.35f * beat;
		for (int pass = 0; pass < 2; pass++)
		{
			for (const auto& seg : st.veins)
			{
				float f = front + 0.07f * std::sin(t * 0.27f + seg.seed) + 0.03f * seg.depth;
				if (seg.ra <= f) continue;
				Vector2 a = seg.a, b = seg.b;
				if (seg.rb < f)
				{
					// The growing tip: only as far as the front.
					float share = (seg.ra - f) / std::max(1e-4f, seg.ra - seg.rb);
					b = Vector2(a.x + (b.x - a.x) * share, a.y + (b.y - a.y) * share);
				}
				float fade = Clamp01((seg.ra - f) / 0.08f);
				Vector2 sa = ToScreen(Writhe(a, t, amp)), sb = ToScreen(Writhe(b, t, amp));
				float th = seg.thick * px * swell * (0.55f + 0.45f * ease);
				if (pass == 0)
				{
					if (th > 1.2f) Stroke(st.line, sa, sb, th * 2.6f, Color(0.012f, 0.0f, 0.018f, 0.7f * fade));
				}
				else
				{
					Color core = Color(Lerp(0.3f, 0.66f, beat), 0.02f + 0.03f * beat, Lerp(0.07f, 0.17f, beat), 0.88f * fade);
					Stroke(st.line, sa, sb, std::max(1.0f, th), core);
				}
			}
		}

		// Deep in, eyes open in the dark at the edges and turn to the ship.
		for (int i = 0; i < 10; i++)
		{
			float h1 = Hash(i * 3.1f + 0.5f), h2 = Hash(i * 7.7f + 2.2f), h3 = Hash(i * 1.9f + 5.4f);
			float threshold = 0.3f + 0.45f * h1;
			float r = Lerp(0.8f, 0.95f, h2);
			float show = Clamp01((sev - threshold) / 0.12f) * Clamp01((r - 0.05f - front) / 0.1f);
			if (show <= 0) continue;
			float period = Lerp(7.0f, 13.0f, h3);
			float phase = Frac(t / period + h1);
			float open = Smooth(0.0f, 0.18f, phase) * (1 - Smooth(0.72f, 0.84f, phase));
			// A blink part way through the stare.
			open *= 1 - (1 - Smooth(0.0f, 0.03f, std::fabs(phase - 0.45f))) * 0.95f;
			if (open < 0.02f) continue;
			float a = (i / 10.0f + h2 * 0.06f) * 2 * Pi + 0.3f;
			Vector2 u = Vector2(std::cos(a), std::sin(a));
			u = Vector2(u.x / Reach(u) * r, u.y / Reach(u) * r);
			Vector2 at = ToScreen(Writhe(u, t * 0.5f, amp));
			float w = H * Lerp(0.045f, 0.085f, h3), eh = w * 0.5f, oh = eh * open;
			float alpha = show * 0.55f;
			Centred(st.sclera, at, w, oh, Color(1, 1, 1, alpha));
			// The iris, clipped to the lids, looking at the middle of the screen (and trembling).
			float dx = centre.x - at.x, dy = centre.y - at.y, dl = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
			float look = w * 0.16f;
			Vector2 ic = Vector2(at.x + dx / dl * look + std::sin(t * 23 + i) * w * 0.008f, at.y + dy / dl * look * 0.4f);
			Rect lids = Rect(at.x - w * 0.5f, at.y - oh * 0.5f, w, oh);
			GUI::BeginGroup(lids);
			float iw = eh * (0.95f + 0.12f * beat);
			GUI::color = Color(1, 1, 1, alpha * 1.3f);
			GUI::DrawTexture(Rect(ic.x - lids.x - iw * 0.5f, ic.y - lids.y - iw * 0.5f, iw, iw), st.iris);
			GUI::EndGroup();
		}

		// The heartbeat through the whole view.
		Fill(Color(0.32f, 0.0f, 0.03f, beat * 0.11f * sev));

		GUI::color = oldColor;
		GUI::matrix = oldMatrix;
	}
}
