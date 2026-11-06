// Port of Unity.SpaceGod Runtime/Views/BattleFx.cs (US-T30).
#include "Views/BattleFx.h"

#include "UnitySpaceApp.h"
#include "Rendering/Materials.h"
#include "Views/ShipView.h"

#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/War.h"
#include "Settings/GameSetting.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "World.h"

#include <cstdint>

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;

	const cs::Ref<RangeSetting>& BattleFx::EffectsBudget()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("lod.battleEffects", SettingGroups::RenderBudget,
			"Battle effects budget", 4000, 500, 8000, 500,
			"The most beams, tracers, missiles and explosions drawn at once; the oldest give way first in a big fight.",
			cs::Func<cs::String, double>([](double v) { return cs::Fmt(v, "0") + " effects"; }), "weapons fire particles performance");
		return value;
	}

	int BattleFx::MaxEffects() { return EffectsBudget()->IntValue(); }

	BattleFx::BattleFx(UnitySpaceApp* app_, const cs::Ref<Transform>& parent)
		: app(app_)
	{
		auto go = NewGameObject("Battle FX");
		go->transform->SetParent(parent, false);
		auto m = cs::New<Mesh>("Battle FX");
		m->indexFormat = IndexFormat::UInt32;
		mesh = Materials::Own(m);
		mesh->MarkDynamic();
		AddComponent<MeshFilter>(go)->sharedMesh = mesh;
		auto mr = AddComponent<MeshRenderer>(go);
		mr->SetSharedMaterial(Materials::Create(Materials::Fx(), "BattleFx"));
		mr->shadowCastingMode = ShadowCastingMode::Off;
		mr->receiveShadows = false;
		// Whatever the battles already hold is history (a fight kept in War.Ended holds hundreds of events for good):
		// drawing it now would replay old fire at old sites as if it were new (U.SPC-T385).
		auto war = app->World() ? app->World()->War : nullptr;
		if (war != nullptr)
		{
			for (const auto& b : war->Battles) seen[b] = b->FxSeq;
			for (const auto& b : war->Ended) seen[b] = b->FxSeq;
		}
	}

	void BattleFx::Update()
	{
		float now = Time::unscaledTime;
		auto war = app->World()->War;
		for (const auto& b : war->Battles) Collect(b, now);
		for (const auto& b : war->Ended) Collect(b, now);
		PruneSeen(war);

		effects.RemoveAll([&](const Effect& e) { return now - e.Born > e.Life; });
		if (effects.Count() > MaxEffects()) effects.RemoveRange(0, effects.Count() - MaxEffects());

		vertices.Clear();
		colors.Clear();
		uvs.Clear();
		indices.Clear();
		for (const auto& e : effects) Draw(e, now);
		for (const auto& b : war->Battles) DrawMissiles(b);
		for (const auto& b : war->Battles) DrawRounds(b);
		for (const auto& b : war->Battles) DrawMines(b);

		mesh->Clear();
		mesh->SetVertices(vertices);
		mesh->SetColors(colors);
		mesh->SetUVs(0, uvs);
		mesh->SetIndices(indices, MeshTopology::Triangles, 0, false);
		mesh->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 1e9f));
	}

	void BattleFx::Collect(const cs::Ref<Battle>& b, float now)
	{
		int last = 0;
		seen.TryGetValue(b, last);
		if (b->FxSeq == last) return;
		// Only the newest events are worth drawing; at high warp thousands arrive per frame.
		int start = b->Fx.Count();
		int taken = 0;
		while (start > 0 && b->Fx[start - 1].Seq > last && taken < MaxNewPerBattlePerFrame)
		{
			start--;
			taken++;
		}
		for (int i = start; i < b->Fx.Count(); i++) Spawn(b, b->Fx[i], now);
		seen[b] = b->FxSeq;
	}

	void BattleFx::PruneSeen(const cs::Ref<War>& war)
	{
		if (seen.Count() <= 64) return;
		staleSeen.Clear();
		for (const auto& b : seen.Keys())
			if (!war->Battles.Contains(b) && !war->Ended.Contains(b)) staleSeen.Add(b);
		for (const auto& b : staleSeen) seen.Remove(b);
	}

	void BattleFx::Spawn(const cs::Ref<Battle>& b, const CombatFx& fx, float now)
	{
		auto side = fx.Side >= 0 && fx.Side < 2 ? b->SideLeaders[fx.Side] : nullptr;
		auto faction = side != nullptr ? Color(side->Color.R, side->Color.G, side->Color.B) : Color::white();
		Effect e;
		e.Battle = b;
		e.Kind = fx.Kind;
		e.A = fx.A;
		e.B = fx.B;
		e.Size = fx.Size;
		e.Born = now;
		e.Unit = fx.Unit;
		e.Sub = fx.Sub;
		switch (fx.Kind)
		{
			case FxKind::Beam:
			case FxKind::BeamMiss:
				e.Color = Color::Lerp(faction, Color::white(), 0.45f) * (fx.Size > 100 ? 1.6f : 1.1f);
				e.Life = fx.Size > 100 ? 0.35f : 0.22f;
				break;
			case FxKind::Railgun:
			case FxKind::RailgunMiss:
				// A hypervelocity kinetic round, not an energy pulse: near-white, thinner, and
				// gone faster than even a heavy laser beam.
				e.Color = Color::Lerp(Color::white(), Color(0.75f, 0.85f, 1.0f), 0.3f) * 2.2f;
				e.Life = 0.16f;
				break;
			case FxKind::TachyonLance:
			case FxKind::TachyonLanceMiss:
				// A tachyon lance (U.SPC-T487): no sweep and no travelling round, the whole line there at once,
				// violet-white and far brighter than any beam, lingering a moment as an afterimage.
				e.Color = Color::Lerp(Color(0.7f, 0.45f, 1.0f), Color::white(), 0.35f) * 3.0f;
				e.Life = 0.6f;
				break;
			case FxKind::PointDefense:
				e.Color = Color(1.0f, 0.85f, 0.5f) * 0.8f;
				e.Life = 0.08f;
				break;
			case FxKind::Guns:
				e.Color = Color::Lerp(faction, Color(1.0f, 0.9f, 0.6f), 0.5f);
				e.Life = 0.1f;
				break;
			case FxKind::ShieldHit:
				// Coloured at draw time by the struck patch's emitter (see DrawShieldHit); this is only the
				// fallback for a hit that names none.
				e.Color = Color(0.35f, 0.7f, 1.0f) * 0.55f;
				e.Life = 0.55f;
				if (fx.Unit != nullptr) e.Local = fx.Unit->ToLocal(fx.A);
				break;
			case FxKind::Impact:
				e.Color = Color(1.0f, 0.7f, 0.35f);
				e.Life = 0.45f;
				break;
			case FxKind::SubsystemDestroyed:
				e.Color = Color(1.0f, 0.6f, 0.25f) * 1.5f;
				e.Life = 1.4f;
				break;
			case FxKind::MissileKill:
				e.Color = Color(1.0f, 0.75f, 0.4f);
				e.Life = 0.35f;
				break;
			case FxKind::UnitDestroyed:
				e.Color = Color(1.0f, 0.8f, 0.5f) * 2.0f;
				e.Life = 3.2f;
				break;
			case FxKind::Breach:
				e.Color = Color(0.85f, 0.92f, 1.0f) * 1.2f;
				e.Life = 2.2f;
				break;
			case FxKind::PodBounce:
				e.Color = Color(0.4f, 0.75f, 1.0f);
				e.Life = 0.5f;
				break;
			case FxKind::Captured:
				e.Color = Color::Lerp(faction, Color::white(), 0.3f) * 1.4f;
				e.Life = 2.5f;
				break;
			case FxKind::Latch:
				e.Color = Color(1.0f, 0.9f, 0.6f);
				e.Life = 0.8f;
				break;
			case FxKind::Pulse:
				e.Color = Color(0.45f, 0.85f, 1.0f) * 1.3f;
				e.Life = 3.5f;
				break;
			case FxKind::Hail:
				e.Color = Color(1.0f, 1.0f, 0.8f) * 1.3f;
				e.Life = 3.0f;
				break;
			case FxKind::MineBlast:
				e.Color = Color(1.0f, 0.55f, 0.2f) * 2.0f;
				e.Life = 0.9f;
				break;
			case FxKind::EmpBurst:
				e.Color = Color(0.45f, 0.85f, 1.0f) * 1.6f;
				e.Life = 1.1f;
				break;
			case FxKind::Deployed:
				e.Color = Color::Lerp(faction, Color::white(), 0.5f);
				e.Life = 0.5f;
				break;
			case FxKind::Phaser:
			case FxKind::PhaserMiss:
				// A phaser (U.SPC-T488): amber, tinted to the faction; a lance holds as one long hard line, a sweep
				// (low damage per pulse) is a brief thin flicker.
				e.Color = Color::Lerp(Color(1.0f, 0.6f, 0.2f), faction, 0.2f) * (fx.Size > 50 ? 1.8f : 1.1f);
				e.Life = fx.Size > 50 ? 0.5f : 0.12f;
				break;
			case FxKind::Plasma:
			case FxKind::PlasmaMiss:
				// A plasma bolt (U.SPC-T488): a fat green-white slug of plasma that visibly crosses the gap.
				e.Color = Color::Lerp(Color(0.4f, 1.0f, 0.5f), Color::white(), 0.25f) * 2.0f;
				e.Life = 0.45f;
				break;
			case FxKind::FlakBurst:
				e.Color = Color(1.0f, 0.7f, 0.35f);
				e.Life = 0.6f;
				break;
			case FxKind::Vulcan:
				// A vulcan's burst (US-T101): hot orange-red tracers, brighter and redder than a gun's, gone in a blink.
				e.Color = Color::Lerp(Color(1.0f, 0.45f, 0.15f), faction, 0.15f) * 1.6f;
				e.Life = 0.07f;
				break;
			case FxKind::HiveSplit:
				// A hive round splitting (US-T101): an amber flash and its seekers' streaks fanning out.
				e.Color = Color(1.0f, 0.75f, 0.3f) * 1.8f;
				e.Life = 0.5f;
				break;
			case FxKind::FtlSpool:
				// An FTL drive charging to jump out (US-T98): a cold blue-white pulse, a touch of the faction in it.
				e.Color = Color::Lerp(Color(0.55f, 0.8f, 1.0f), faction, 0.2f) * 1.4f;
				e.Life = 1.0f;
				break;
			case FxKind::FtlAbort:
				// The charge broken: the field spits out hot and collapses.
				e.Color = Color(1.0f, 0.5f, 0.25f) * 1.6f;
				e.Life = 0.8f;
				break;
			case FxKind::FtlJump:
				e.Color = Color(0.7f, 0.88f, 1.0f) * 2.2f;
				e.Life = 0.9f;
				break;
			default:
				return;
		}
		effects.Add(e);
	}

	void BattleFx::Draw(const Effect& e, float now)
	{
		float age = now - e.Born;
		float t = age / e.Life;
		float fade = 1 - t;
		switch (e.Kind)
		{
			case FxKind::Vulcan:
			{
				// A vulcan's burst (US-T101): a short, thick tracer dash flying out from the muzzle down the line, with a
				// ragged muzzle flare that flickers as the barrels turn.
				auto a = Point(e.Battle, e.A);
				auto bFull = Point(e.Battle, e.B);
				float fly = Mathf::Clamp01(t);
				auto head = Vector3::Lerp(a, bFull, Mathf::Min(1.0f, fly * 1.2f));
				auto tail = Vector3::Lerp(a, bFull, Mathf::Max(0.0f, fly * 1.2f - 0.25f));
				Ribbon(tail, head, 2.2f, e.Color * fade);
				Ribbon(tail, head, 0.8f, Color(1.0f, 0.95f, 0.8f) * fade);
				float flick = 0.7f + 0.3f * Mathf::Sin(age * 140.0f);
				Sprite(a, PixelSize(a, 9.0f) * flick * fade, Color(1.0f, 0.7f, 0.35f) * fade, Shape::Radial);
				break;
			}
			case FxKind::HiveSplit:
			{
				// A hive round splitting (US-T101): a hot flash where the casing opened, a ring racing out, and a fan of
				// short streaks round its heading, one per seeker.
				auto c = Point(e.Battle, e.A);
				auto ahead = Point(e.Battle, e.B) - c;
				if (age < 0.12f) Sprite(c, PixelSize(c, 22.0f) * (1 - age / 0.12f), Color::white() * 1.5f, Shape::Radial);
				Sprite(c, PixelSize(c, 8.0f + 26.0f * t), e.Color * fade, Shape::Ring);
				int n = Mathf::Max(1, static_cast<int>(e.Size));
				auto cam = app->UnityCamera->transform;
				for (int k = 0; k < n; k++)
				{
					float ang = Mathf::PI * 2 * k / n;
					auto across = (cam->right() * Mathf::Cos(ang) + cam->up() * Mathf::Sin(ang)) * ahead.magnitude() * 0.6f;
					auto tip = c + (ahead + across) * (0.4f + 1.4f * t);
					Ribbon(c + (ahead + across) * (0.2f + 1.2f * t), tip, 1.2f, e.Color * fade);
				}
				break;
			}
			case FxKind::Beam:
			case FxKind::BeamMiss:
			case FxKind::PointDefense:
			case FxKind::Guns:
			case FxKind::Railgun:
			case FxKind::RailgunMiss:
			{
				auto a = Point(e.Battle, e.A);
				auto bFull = Point(e.Battle, e.B);
				// A beam sweeps onto its target across the first slice of its life instead of
				// appearing full-length the instant it's fired; a kinetic round's own travelling
				// Round entity (see DrawRounds) already reads as motion, so only the beam needs it.
				bool isBeam = e.Kind == FxKind::Beam || e.Kind == FxKind::BeamMiss;
				float sweepT = isBeam ? Mathf::Clamp01(age / Mathf::Max(e.Life * 0.3f, 1e-4f)) : 1.0f;
				auto b = Vector3::Lerp(a, bFull, sweepT);
				float px = e.Kind == FxKind::PointDefense || e.Kind == FxKind::Guns ? 1.6f
					: e.Kind == FxKind::Railgun || e.Kind == FxKind::RailgunMiss ? 1.8f
					: e.Size > 100 ? 4.0f : 2.6f;
				Ribbon(a, b, px, e.Color * fade);

				// Muzzle flash and a brief recoil kick at the gun itself, for every discrete shot
				// (not the continuous point-defence stream).
				if (e.Kind != FxKind::PointDefense)
				{
					constexpr float muzzleWindow = 0.08f;
					if (age < muzzleWindow)
					{
						float k = 1 - age / muzzleWindow;
						Sprite(a, PixelSize(a, 15.0f) * k, Color::white() * k, Shape::Radial);
						auto kick = a - (bFull - a).normalized() * PixelSize(a, 7.0f) * k;
						Ribbon(kick, a, px * 1.6f, Color::white() * k * 0.8f);
					}
				}

				// A laser leaves a scorch where it actually hit; a miss has nothing to scorch.
				if (e.Kind == FxKind::Beam)
					Sprite(bFull, PixelSize(bFull, 6.0f) * (0.7f + 0.3f * t), Color(0.3f, 0.17f, 0.1f) * fade, Shape::Radial);
				break;
			}
			case FxKind::TachyonLance:
			case FxKind::TachyonLanceMiss:
				DrawLance(e, age, fade);
				break;
			case FxKind::Phaser:
			case FxKind::PhaserMiss:
			{
				// The beam lances out across the first fifth of its life, then holds and fades; it burns a bright
				// spot where it lands on a hull.
				auto a = Point(e.Battle, e.A);
				auto bFull = Point(e.Battle, e.B);
				auto b = Vector3::Lerp(a, bFull, Mathf::Clamp01(age / Mathf::Max(e.Life * 0.2f, 1e-4f)));
				float px = e.Size > 50 ? 3.4f : 1.4f;
				Ribbon(a, b, px, e.Color * fade);
				Ribbon(a, b, px * 0.4f, Color::white() * fade * 0.8f);
				if (e.Kind == FxKind::Phaser && e.Size > 0)
					Sprite(bFull, PixelSize(bFull, e.Size > 50 ? 11.0f : 5.0f) * (0.8f + 0.2f * Mathf::Sin(age * 60.0f)), e.Color * fade, Shape::Radial);
				break;
			}
			case FxKind::Plasma:
			case FxKind::PlasmaMiss:
			{
				// The bolt is a short fat streak with a glowing head that flies from the muzzle to where it lands over
				// the first two-thirds of its life, then bursts there (a hit) or flies on and fades (a miss).
				auto a = Point(e.Battle, e.A);
				auto bFull = Point(e.Battle, e.B);
				float flight = Mathf::Clamp01(t / 0.66f);
				auto head = Vector3::Lerp(a, bFull, flight);
				auto tail = Vector3::Lerp(a, bFull, Mathf::Max(0.0f, flight - 0.18f));
				if (flight < 1)
				{
					Ribbon(tail, head, 5.0f, e.Color * 0.7f);
					Sprite(head, PixelSize(head, 14.0f), e.Color, Shape::Radial);
					if (age < 0.08f) Sprite(a, PixelSize(a, 18.0f) * (1 - age / 0.08f), Color::white(), Shape::Radial);
				}
				else if (e.Kind == FxKind::Plasma)
				{
					float burst = (t - 0.66f) / 0.34f;
					// The splash grows with what the bolt still carried: a close shot blooms, a long one fizzles.
					float px = Mathf::Lerp(10.0f, 34.0f, Mathf::Clamp01(static_cast<float>(e.Size) / 480.0f));
					Sprite(bFull, PixelSize(bFull, px) * (0.6f + burst), e.Color * (1 - burst), Shape::Radial);
				}
				break;
			}
			case FxKind::ShieldHit:
				DrawShieldHit(e, t, fade);
				break;
			case FxKind::Breach:
			{
				// Atmosphere venting from the hole: a bright puff and a jet streaming outward.
				auto c = Point(e.Battle, e.A);
				auto outward = Point(e.Battle, e.A + (e.A - e.B).Normalized() * (0.3 + 1.2 * t)) - c;
				float r = static_cast<float>(e.Size * app->BattleKmScale()) * (0.3f + t);
				Sprite(c, Mathf::Max(Mathf::Min(r, PixelSize(c, 40)), PixelSize(c, 10)), e.Color * fade * 0.8f, Shape::Radial);
				Ribbon(c, c + outward, 3.0f, e.Color * fade);
				break;
			}
			case FxKind::PodBounce:
			case FxKind::Captured:
			{
				auto c = e.Unit != nullptr && e.Unit->Present() ? Point(e.Battle, e.Unit->Pos) : Point(e.Battle, e.A);
				float r = static_cast<float>(e.Size * 0.8 * app->BattleKmScale()) * (e.Kind == FxKind::Captured ? 0.8f + t : 1.0f);
				Sprite(c, Mathf::Max(r, PixelSize(c, e.Kind == FxKind::Captured ? 22.0f : 10.0f)), e.Color * fade, Shape::Ring);
				break;
			}
			case FxKind::Hail:
			{
				// A hull hailing for terms: a slow-blinking signal ring around it, weapons cold.
				auto c = e.Unit != nullptr && e.Unit->Present() ? Point(e.Battle, e.Unit->Pos) : Point(e.Battle, e.A);
				float r = static_cast<float>(e.Size * 1.1 * app->BattleKmScale());
				float blink = Mathf::Sin(age * 6.0f) > 0 ? 1.0f : 0.35f;
				Sprite(c, Mathf::Max(r, PixelSize(c, 26)), e.Color * fade * blink, Shape::Ring);
				break;
			}
			case FxKind::MineBlast:
			{
				// A fireball swelling to the blast's reach, and its shock ring racing past it.
				auto c = Point(e.Battle, e.A);
				float r = static_cast<float>(e.Size * app->BattleKmScale());
				Sprite(c, Mathf::Max(Mathf::Min(r * (0.3f + 0.7f * Mathf::Sqrt(t)), PixelSize(c, 70)), PixelSize(c, 14)), e.Color * fade, Shape::Radial);
				Sprite(c, Mathf::Max(Mathf::Min(r * (0.5f + t), PixelSize(c, 100)), PixelSize(c, 20)), e.Color * fade * 0.6f, Shape::Ring);
				break;
			}
			case FxKind::EmpBurst:
			{
				// A crackling blue shell out to the burst's reach (the interdiction pulse in miniature).
				auto c = Point(e.Battle, e.A);
				float r = static_cast<float>(e.Size * app->BattleKmScale()) * (0.2f + 0.8f * Mathf::Sqrt(t));
				float flicker = 0.75f + 0.25f * Mathf::Sin(age * 60.0f);
				Sprite(c, Mathf::Max(r, PixelSize(c, 18)), e.Color * fade * flicker, Shape::Ring);
				if (age < 0.15f) Sprite(c, PixelSize(c, 30), Color::white() * (1 - age / 0.15f), Shape::Radial);
				break;
			}
			case FxKind::Deployed:
			{
				auto c = Point(e.Battle, e.A);
				Sprite(c, PixelSize(c, 8 + 16 * t), e.Color * fade, Shape::Ring);
				break;
			}
			case FxKind::FlakBurst:
			{
				// A flak shell (U.SPC-T489): a quick tracer out to the burst, then a hot flash and a dirty grey puff
				// swelling to the burst's reach and hanging there as it fades.
				auto a = Point(e.Battle, e.A);
				auto c = Point(e.Battle, e.B);
				constexpr float flight = 0.12f;
				if (age < flight) Ribbon(a, Vector3::Lerp(a, c, age / flight), 1.4f, Color(1.0f, 0.85f, 0.5f) * (1 - age / flight));
				else
				{
					float bt = Mathf::Clamp01((age - flight) / (e.Life - flight));
					// US-T216: a burst is hundreds of metres across at realistic scale, a grey disc swallowing a fighter and the
						// hull behind it: its reach is true, the drawn puff stops at a few dozen pixels.
						float r = Mathf::Min(static_cast<float>(e.Size * app->BattleKmScale()) * (0.35f + 0.65f * Mathf::Sqrt(bt)), PixelSize(c, 34));
					if (bt < 0.2f) Sprite(c, Mathf::Max(r * 0.6f, PixelSize(c, 7)), e.Color * 2.0f * (1 - bt / 0.2f), Shape::Radial);
					Sprite(c, Mathf::Max(r, PixelSize(c, 5)), Color(0.55f, 0.5f, 0.45f) * (1 - bt) * 0.7f, Shape::Radial);
				}
				break;
			}
			case FxKind::FtlSpool:
			case FxKind::FtlAbort:
			case FxKind::FtlJump:
				DrawEscapeJump(e, age, t, fade);
				break;
			case FxKind::Pulse:
			{
				// The interdiction pulse: a shell racing outward far past the battle.
				auto c = Point(e.Battle, e.A);
				float r = static_cast<float>(e.Size * app->BattleKmScale()) * (0.1f + 3.0f * Mathf::Sqrt(t));
				Sprite(c, Mathf::Max(r, PixelSize(c, 40 + 400 * t)), e.Color * fade, Shape::Ring);
				if (age < 0.3f) Sprite(c, PixelSize(c, 50), Color::white() * (1 - age / 0.3f), Shape::Radial);
				break;
			}
			case FxKind::UnitDestroyed:
			{
				auto c = Point(e.Battle, e.A);
				float r = static_cast<float>(e.Size * app->BattleKmScale()) * (0.6f + 2.2f * Mathf::Sqrt(t));
				// US-T216: the fireball grows with the hull but never past ~160 px, its white flash ~240.
					Sprite(c, Mathf::Max(Mathf::Min(r, PixelSize(c, 160)), PixelSize(c, 24 * (0.5f + t))), e.Color * fade * fade, Shape::Radial);
				if (age < 0.25f) Sprite(c, Mathf::Max(Mathf::Min(r * 3, PixelSize(c, 240)), PixelSize(c, 60)), Color::white() * (1 - age / 0.25f) * 0.8f, Shape::Radial);
				break;
			}
			default:
			{
				auto c = Point(e.Battle, e.A);
				float r = static_cast<float>(Mathf::Max(0.05f, static_cast<float>(e.Size) * (e.Kind == FxKind::Impact ? 0.002f : 1.0f)) * app->BattleKmScale()) * (0.5f + t);
				float minPx = e.Kind == FxKind::SubsystemDestroyed ? 14.0f : e.Kind == FxKind::Impact ? 5.0f : 6.0f;
				float cap = 0;
				if (e.Unit != nullptr)
				{
					// US-T161: sized from the struck hull's drawn size (hardware is one linear 7 units/km now, so the
					// old km floor and damage-scaled disc swamped a small hull): an impact is a fraction of the hull,
					// growing a little with the damage, and nothing here outgrows the hull.
					float hull = static_cast<float>(app->Scale()->ShipLength(e.Unit->LengthKm));
					if (e.Kind == FxKind::Impact) r = hull * Mathf::Clamp(0.06f + static_cast<float>(e.Size) * 0.00003f, 0.06f, 0.25f) * (0.5f + t);
					else r = Mathf::Min(r, hull * 0.5f);
					cap = hull * 0.5f;
				}
				float rr = Mathf::Max(r, PixelSize(c, minPx));
				if (cap > 0) rr = Mathf::Min(rr, Mathf::Max(cap, PixelSize(c, minPx)));
				Sprite(c, rr, e.Color * fade, Shape::Radial);
				break;
			}
		}
	}

	void BattleFx::DrawEscapeJump(const Effect& e, float age, float t, float fade)
	{
		// US-T98: a hull spooling its FTL drive to jump out of the fight, and how the charge ends. Each pulse of the charge
		// is a ring drawing in on the hull, wider and brighter the fuller the charge, with a glow at its heart; a broken
		// charge is the ring flickering out; the jump is a white flash where the hull was and a streak along its nose.
		auto at = e.Unit != nullptr && e.Unit->Present() ? e.Unit->Pos : e.A;
		auto c = Point(e.Battle, at);
		float hull = e.Unit != nullptr ? static_cast<float>(app->Scale()->ShipLength(e.Unit->LengthKm)) : PixelSize(c, 20);
		switch (e.Kind)
		{
			case FxKind::FtlSpool:
			{
				float charge = Mathf::Clamp01(static_cast<float>(e.Size));
				float r = Mathf::Max(hull * (0.75f + 0.6f * charge) * (1.0f - 0.3f * t), PixelSize(c, 14.0f + 16.0f * charge));
				Sprite(c, r, e.Color * fade * (0.45f + 1.1f * charge), Shape::Ring);
				Sprite(c, Mathf::Max(hull * 0.4f, PixelSize(c, 6.0f)), e.Color * fade * (0.15f + 0.6f * charge), Shape::Radial);
				break;
			}
			case FxKind::FtlAbort:
			{
				float flick = Mathf::Sin(age * 55.0f) > 0 ? 1.0f : 0.3f;
				Sprite(c, Mathf::Max(hull * (1.2f - 0.9f * t), PixelSize(c, 8.0f)), e.Color * fade * flick, Shape::Ring);
				if (age < 0.1f) Sprite(c, Mathf::Max(hull * 0.6f, PixelSize(c, 12.0f)), e.Color * (1 - age / 0.1f), Shape::Radial);
				break;
			}
			default:
			{
				auto a = Point(e.Battle, e.A);
				auto b = Point(e.Battle, e.B);
				if (age < 0.2f) Sprite(a, Mathf::Max(hull * 1.5f, PixelSize(a, 40.0f)) * (1 - age / 0.2f), Color::white() * 2.0f, Shape::Radial);
				auto head = Vector3::Lerp(a, b, Mathf::Min(1.0f, t * 3.0f));
				Ribbon(a, head, 7.0f * fade, e.Color * fade);
				Ribbon(a, head, 2.0f * fade, Color::white() * fade);
				Sprite(a, Mathf::Max(hull, PixelSize(a, 20.0f)) * (0.5f + t), e.Color * fade * 0.6f, Shape::Ring);
				break;
			}
		}
	}

	void BattleFx::DrawLance(const Effect& e, float age, float fade)
	{
		auto a = Point(e.Battle, e.A);
		auto b = Point(e.Battle, e.B);
		float flash = age < 0.1f ? 1 - age / 0.1f : 0;
		Ribbon(a, b, 9.0f * fade + 4.0f * flash, e.Color * 0.35f * fade);
		Ribbon(a, b, 2.6f, Color::white() * (0.6f * fade + flash));
		Sprite(a, PixelSize(a, 22.0f) * (0.4f + flash), e.Color * (0.4f * fade + flash), Shape::Radial);
		if (e.Kind == FxKind::TachyonLance)
			Sprite(b, PixelSize(b, 18.0f) * (0.6f + 0.4f * fade), e.Color * fade, Shape::Radial);
	}

	void BattleFx::DrawShieldHit(const Effect& e, float t, float fade)
	{
		auto u = e.Unit;
		cs::Ref<Subsystem> gen = nullptr;
		if (u != nullptr && u->Combat != nullptr && e.Sub >= 0 && e.Sub < u->Combat->Subsystems.Count())
			gen = u->Combat->Subsystems[e.Sub];
		if (gen == nullptr || u == nullptr || !u->Present())
		{
			auto c0 = u != nullptr && u->Present() ? Point(e.Battle, u->Pos) : Point(e.Battle, e.B);
			float r0 = static_cast<float>(app->Scale()->ShipLength(e.Size) * 0.75);
			Sprite(c0, Mathf::Max(r0, PixelSize(c0, 10)), e.Color * fade, Shape::Ring);
			return;
		}
		float charge = static_cast<float>(gen->ShieldFraction());
		if (charge <= 0) return;
		auto tint = ShipView::ShieldColor(charge);
		auto color = Color(tint.r, tint.g, tint.b, 1.0f) * (tint.a * fade);

		auto centre = Point(e.Battle, u->Pos);
		float shellR = static_cast<float>(app->Scale()->ShipLength(u->LengthKm) * BubbleRadius);
		if (shellR < PixelSize(centre, 14))
		{
			Sprite(centre, Mathf::Max(shellR, PixelSize(centre, 10)), color, Shape::Ring);
			return;
		}

		auto d = e.Local.LengthSquared() > 1e-12 ? e.Local.Normalized() : DVec3(0, 0, 1);
		auto reference = cs::Math::Abs(d.Y) < 0.9 ? DVec3(0, 1, 0) : DVec3(1, 0, 0);
		auto t1 = DVec3::Cross(reference, d).Normalized();
		auto t2 = DVec3::Cross(d, t1);
		double k = cs::Math::Tan(BubbleHalfAngleDeg * Mathf::Deg2Rad);
		float z = static_cast<float>(Shape::Bubble) + Mathf::Min(t, 0.999f);
		int i0 = vertices.Count();
		for (int j = 0; j <= BubbleGrid; j++)
		{
			float v = static_cast<float>(j) / BubbleGrid;
			for (int i = 0; i <= BubbleGrid; i++)
			{
				float uu = static_cast<float>(i) / BubbleGrid;
				auto dir = (d + (t1 * (uu * 2 - 1) + t2 * (v * 2 - 1)) * k).Normalized();
				vertices.Add(Point(e.Battle, u->PointLocal(dir * BubbleRadius)));
				colors.Add(color);
				uvs.Add(Vector3(uu, v, z));
			}
		}
		int row = BubbleGrid + 1;
		for (int j = 0; j < BubbleGrid; j++)
			for (int i = 0; i < BubbleGrid; i++)
			{
				int a = i0 + j * row + i;
				indices.Add(a);
				indices.Add(a + 1);
				indices.Add(a + row + 1);
				indices.Add(a);
				indices.Add(a + row + 1);
				indices.Add(a + row);
			}
	}

	void BattleFx::DrawMissiles(const cs::Ref<Battle>& b)
	{
		for (const auto& m : b->Missiles)
		{
			if (m->Gone) continue;
			auto p = Point(b, m->Pos);
			auto side = m->Side >= 0 && m->Side < 2 ? b->SideLeaders[m->Side] : nullptr;
			auto c = side != nullptr ? Color::Lerp(Color(side->Color.R, side->Color.G, side->Color.B), Color::white(), 0.6f) : Color::white();
			bool pod = m->Kind == MissileKind::Pod;
			if (pod) c = Color(1.0f, 0.75f, 0.35f);
			if (m->Kind == MissileKind::Antimatter)
			{
				// An antimatter torpedo (U.SPC-T489): a pulsing violet star with a long glowing wake.
				// (C#'s m.GetHashCode() is the object's identity hash, only a per-missile phase: the pointer stands in.)
				int hash = static_cast<int>(reinterpret_cast<std::uintptr_t>(m.get()));
				float pulse = 0.8f + 0.2f * Mathf::Sin(Time::unscaledTime * 14.0f + hash);
				auto violet = Color::Lerp(Color(0.75f, 0.4f, 1.0f), c, 0.25f);
				Sprite(p, PixelSize(p, 11 * pulse), violet * 1.8f, Shape::Radial);
				Sprite(p, PixelSize(p, 4), Color::white() * 1.6f, Shape::Radial);
				Ribbon(Point(b, m->Pos - m->Vel * 4.0), p, 2.4f, violet * 0.6f);
				continue;
			}
			if (m->Kind == MissileKind::Breacher)
			{
				// A Breacher round (U.SPC-T466): a big, hot slug in the firing side's colour with a long straight wake,
				// round its own round model (EmbeddedPods, U.SPC-T517).
				auto slug = Color::Lerp(c, Color(1.0f, 0.85f, 0.6f), 0.5f);
				Sprite(p, PixelSize(p, 9), slug * 1.6f, Shape::Radial);
				Ribbon(Point(b, m->Pos - m->Vel * 1.5), p, 2.6f, slug * 0.55f);
				continue;
			}
			if (m->Kind == MissileKind::Interceptor)
			{
				// An interceptor (U.SPC-T489): a small, hot blue-white dart with a short, thin streak.
				auto ice = Color(0.65f, 0.9f, 1.0f);
				Sprite(p, PixelSize(p, 3.5f), ice * 1.6f, Shape::Radial);
				Ribbon(Point(b, m->Pos - m->Vel * 0.8), p, 1.0f, ice * 0.6f);
				continue;
			}
			if (m->Kind == MissileKind::Rocket)
			{
				// A dumb-fire rocket (US-T101): a small hot motor flame on a long, widening grey smoke trail that thins
				// to nothing behind it.
				Sprite(p, PixelSize(p, 3.0f), Color(1.0f, 0.8f, 0.45f) * 1.8f, Shape::Radial);
				auto smoke = Color(0.62f, 0.6f, 0.58f);
				auto prev = p;
				for (int k = 1; k <= 4; k++)
				{
					auto next = Point(b, m->Pos - m->Vel * (0.12 * k));
					Ribbon(next, prev, 1.4f + 1.1f * k, smoke * (0.55f - 0.12f * k));
					prev = next;
				}
				continue;
			}
			if (m->Kind == MissileKind::Hive)
			{
				// A hive round before it splits (US-T101): a fat amber casing, its seekers' lights winking along it.
				auto amber = Color::Lerp(Color(1.0f, 0.7f, 0.25f), c, 0.2f);
				float wink = Mathf::Sin(Time::unscaledTime * 22.0f) > 0 ? 1.0f : 0.6f;
				Sprite(p, PixelSize(p, 7.0f), amber * 1.5f * wink, Shape::Radial);
				Ribbon(Point(b, m->Pos - m->Vel * 2.0), p, 2.0f, amber * 0.5f);
				continue;
			}
			if (m->Kind == MissileKind::HiveMicro)
			{
				// A hive seeker (US-T101): a tiny amber spark on a short, thin streak, one of a swarm.
				auto amber = Color(1.0f, 0.75f, 0.3f);
				Sprite(p, PixelSize(p, 2.5f), amber * 1.6f, Shape::Radial);
				Ribbon(Point(b, m->Pos - m->Vel * 0.6), p, 0.8f, amber * 0.55f);
				continue;
			}
			Sprite(p, PixelSize(p, pod ? 8.0f : m->Torpedo ? 7.0f : 5.0f), c * 1.4f, Shape::Radial);
			auto tail = Point(b, m->Pos - m->Vel * (pod ? 4.0 : m->Torpedo ? 3.0 : 2.0));
			Ribbon(tail, p, pod ? 2.2f : 1.4f, c * 0.5f);
		}
	}

	void BattleFx::DrawRounds(const cs::Ref<Battle>& b)
	{
		for (const auto& r : b->Rounds)
		{
			if (r->Gone) continue;
			auto p = Point(b, r->Pos);
			auto side = r->Side >= 0 && r->Side < 2 ? b->SideLeaders[r->Side] : nullptr;
			auto faction = side != nullptr ? Color(side->Color.R, side->Color.G, side->Color.B) : Color::white();
			if (r->Tracer)
			{
				// A vulcan's round (US-T101): every one a tracer, a long hot orange-red streak.
				auto hot = Color(1.0f, 0.5f, 0.2f) * 2.0f;
				Sprite(p, PixelSize(p, 2.2f), hot, Shape::Radial);
				Ribbon(Point(b, r->Pos - r->Vel * 0.12), p, 1.6f, hot * 0.7f);
				continue;
			}
			auto c = r->Railgun ? Color::Lerp(Color::white(), Color(0.75f, 0.85f, 1.0f), 0.3f) * 2.2f
				: Color::Lerp(faction, Color(1.0f, 0.9f, 0.6f), 0.5f);
			Sprite(p, PixelSize(p, r->Railgun ? 3.0f : 2.0f), c * 1.2f, Shape::Radial);
			auto tail = Point(b, r->Pos - r->Vel * (r->Railgun ? 0.05 : 0.08));
			Ribbon(tail, p, r->Railgun ? 1.8f : 1.4f, c * 0.6f);
		}
	}

	void BattleFx::DrawMines(const cs::Ref<Battle>& b)
	{
		float now = Time::unscaledTime;
		for (const auto& m : b->Mines)
		{
			if (m->Gone) continue;
			auto p = Point(b, m->Pos);
			auto c = SideColour(b, m->Side, Color(1.0f, 0.35f, 0.25f));
			float blink = m->Armed() ? (Mathf::Sin(now * 9.0f) > 0 ? 1.3f : 0.35f) : 0.4f;
			Sprite(p, PixelSize(p, 4.0f), c * blink, Shape::Radial);
		}
	}

	Color BattleFx::SideColour(const cs::Ref<Battle>& b, int side, Color fallback)
	{
		auto f = side >= 0 && side < 2 ? b->SideLeaders[side] : nullptr;
		return f != nullptr ? Color::Lerp(Color(f->Color.R, f->Color.G, f->Color.B), Color::white(), 0.35f) : fallback;
	}

	Vector3 BattleFx::Point(const cs::Ref<Battle>& b, DVec3 local) { return app->Relative(app->BattleScene(b, local)); }

	float BattleFx::PixelSize(Vector3 p, float pixels) { return pixels * Mathf::Max(p.magnitude(), 1e-6f) / static_cast<float>(app->PixelsPerUnitAtUnitDistance()); }

	void BattleFx::Sprite(Vector3 c, float radius, Color color, Shape shape)
	{
		auto cam = app->UnityCamera->transform;
		auto right = cam->right() * radius;
		auto up = cam->up() * radius;
		Quad(c - right - up, c + right - up, c + right + up, c - right + up, color, shape);
	}

	void BattleFx::Ribbon(Vector3 a, Vector3 b, float pixels, Color color)
	{
		auto mid = (a + b) * 0.5f;
		auto along = b - a;
		if (along.sqrMagnitude() < 1e-12f) return;
		auto side = Vector3::Cross(along, mid).normalized();
		float wa = PixelSize(a, pixels) * 0.5f, wb = PixelSize(b, pixels) * 0.5f;
		int i = vertices.Count();
		vertices.Add(a - side * wa);
		vertices.Add(b - side * wb);
		vertices.Add(b + side * wb);
		vertices.Add(a + side * wa);
		AddCorners(color, Shape::Beam);
		Indices(i);
	}

	void BattleFx::Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color, Shape shape)
	{
		int i = vertices.Count();
		vertices.Add(a);
		vertices.Add(b);
		vertices.Add(c);
		vertices.Add(d);
		AddCorners(color, shape);
		Indices(i);
	}

	void BattleFx::AddCorners(Color color, Shape shape)
	{
		float z = static_cast<float>(shape);
		for (int k = 0; k < 4; k++) colors.Add(color);
		uvs.Add(Vector3(0, 0, z));
		uvs.Add(Vector3(1, 0, z));
		uvs.Add(Vector3(1, 1, z));
		uvs.Add(Vector3(0, 1, z));
	}

	void BattleFx::Indices(int i)
	{
		indices.Add(i);
		indices.Add(i + 1);
		indices.Add(i + 2);
		indices.Add(i);
		indices.Add(i + 2);
		indices.Add(i + 3);
	}
}
