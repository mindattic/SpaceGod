// Hud hits: hit markers, kill confirmation, the combat feed, directional damage arcs and the shield-down and
// hull-critical edge pulses for the unit embodied (US-T257). Declarations in Hud.Hits.inl.

#include "UI/Hud.h"

#include "UnitySpaceApp.h"
#include "UI/Icons.h"

#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Embodiment/MountedTurret.h"
#include "PlayerOptions.h" // US-T252

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		constexpr Color ShieldMarkColor{0.45f, 0.78f, 1.0f, 1.0f};
		constexpr Color HullMarkColor{1.0f, 1.0f, 1.0f, 1.0f};
		constexpr Color WeakMarkColor{1.0f, 0.82f, 0.25f, 1.0f};
		constexpr Color KillMarkColor{1.0f, 0.38f, 0.28f, 1.0f};
		constexpr Color ShieldArcColor{0.4f, 0.72f, 1.0f, 1.0f};
		constexpr Color HullArcColor{1.0f, 0.32f, 0.22f, 1.0f};

		Color WithAlpha(Color c, float a) { return Color(c.r, c.g, c.b, Mathf::Clamp01(a)); }

		/// The held hull's shield, all its emitters together, as a share of its rating; -1 for a hull with none.
		double ShieldShare(const cs::Ref<BattleUnit>& u)
		{
			auto c = u->Combat;
			if (c == nullptr || c->Spec == nullptr || c->Spec->Shield <= 0) return -1;
			double sum = 0;
			for (const auto& s : c->Subsystems) sum += s->Shield;
			return sum / c->Spec->Shield;
		}

		double HullShare(const cs::Ref<BattleUnit>& u)
		{
			auto c = u->Combat;
			if (c == nullptr || c->Spec == nullptr || c->Spec->Hull <= 0) return 1;
			return c->Hull / c->Spec->Hull;
		}
	}

	Vector2 Hud::HitReticle()
	{
		auto centre = Vector2(Screen::width * 0.5f, Screen::height * 0.5f);
		// A seat's sight is the middle of the screen (DrawSeatSight); the helm's is its crosshair, or its nose when the
		// helm turns the hull itself.
		if (!app->SeatLine().empty() || !app->IsPiloting()) return centre;
		Vector2 p;
		if (app->PilotControlMode != PilotControl::Helm && DirectionToGui(app->PilotAimDirection(), p)) return p;
		if (DirectionToGui(app->PilotForward(), p)) return p;
		return centre;
	}

	Vector2 Hud::HitBearing(DVec3 direction)
	{
		auto d = direction.SwapYZ();
		auto rel = Vector3(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z));
		auto local = Quaternion::Inverse(app->SpaceCamera->Rotation) * rel;
		auto dir = Vector2(local.x, -local.y);
		// Dead ahead or astern there is no side to show: below, as the edge arrows do.
		return dir.sqrMagnitude() < 1e-12f ? Vector2(0, 1) : dir.normalized();
	}

	void Hud::ConfirmKill(const cs::Ref<BattleUnit>& unit, const cs::String& how, Color tint, float now)
	{
		killMarkAt = now;
		HitFeedLine line;
		line.Text = cs::Cat(unit->Name(), " ", how);
		line.Tint = tint;
		line.Born = now;
		hitFeed.Add(line);
		if (hitFeed.Count() > HitFeedMax) hitFeed.RemoveRange(0, hitFeed.Count() - HitFeedMax);
	}

	void Hud::TakeHitFx(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& me, const cs::Ref<Subsystem>& mount, float now)
	{
		// Only what is new since the last read, and not a backlog left while the HUD was hidden or the game paused.
		int start = b->Fx.Count();
		while (start > 0 && b->Fx[start - 1].Seq > hitSeq && b->Fx.Count() - start < 4000) start--;
		hitSeq = b->FxSeq;
		bool stale = now - hitSeenAt > 0.5f;
		hitSeenAt = now;
		if (stale) return;

		auto mine = [&](const CombatFx& fx)
		{
			return fx.By != nullptr && fx.By == me && (mount == nullptr || fx.ByMount == mount);
		};
		bool marked = false;
		for (int i = start; i < b->Fx.Count(); i++)
		{
			const auto& fx = b->Fx[i];
			if (fx.Unit == nullptr) continue;

			// Fire on the held hull: an arc the way it came from.
			if (fx.Unit == me)
			{
				if (fx.Kind != FxKind::ShieldHit && !(fx.Kind == FxKind::Impact && fx.Size > 0)) continue;
				bool shield = fx.Kind == FxKind::ShieldHit;
				auto from = fx.By != nullptr && fx.By != me && (fx.By->Pos - me->Pos).LengthSquared() > 1e-18 ? fx.By->Pos - me->Pos : fx.A - me->Pos;
				if (from.LengthSquared() < 1e-18) from = -me->Forward;
				from = from.Normalized();
				bool merged = false;
				for (auto& a : hitArcs)
				{
					if (a.Shield != shield || now - a.Born > 0.3f || DVec3::Dot(a.From, from) < 0.95) continue;
					a.Born = now;
					a.Weight = Mathf::Min(a.Weight + 1, 4.0f);
					merged = true;
					break;
				}
				if (!merged)
				{
					HitArc a;
					a.From = from;
					a.Born = now;
					a.Shield = shield;
					hitArcs.Add(a);
					if (hitArcs.Count() > HitArcMax) hitArcs.RemoveRange(0, hitArcs.Count() - HitArcMax);
				}
				continue;
			}

			bool ours = mine(fx);
			switch (fx.Kind)
			{
				case FxKind::ShieldHit:
				case FxKind::Impact:
				case FxKind::SubsystemDestroyed:
				{
					if (!ours) break;
					auto kind = fx.Kind == FxKind::ShieldHit ? HitMark::Shield
						: fx.Kind == FxKind::SubsystemDestroyed || fx.WeakPoint ? HitMark::WeakPoint : HitMark::Hull;
					// The strongest of the frame's hits is the one marked.
					if (!marked || static_cast<int>(kind) > static_cast<int>(hitMark)) hitMark = kind;
					marked = true;
					hitMarkAt = now;
					// A hull already crippled when first hit is not the unit's to claim as disabled.
					if (!hitStruck.ContainsKey(fx.Unit) && fx.Unit->Combat != nullptr && fx.Unit->Combat->Disabled()) hitDisabled[fx.Unit] = now;
					hitStruck[fx.Unit] = now;
					break;
				}
				case FxKind::UnitDestroyed:
				{
					float last = -100.0f;
					// Its own fire, or a hull it had been hitting going up from what else hit it (a ram, a mine, flak).
					if (ours || (hitStruck.TryGetValue(fx.Unit, last) && now - last < 4.0f))
					{
						ConfirmKill(fx.Unit, "destroyed", KillMarkColor, now);
						hitStruck.Remove(fx.Unit);
					}
					break;
				}
				case FxKind::Hail:
				{
					// A hull breaking off to ask terms (not a raider's demand) that the unit had been hitting.
					float last = -100.0f;
					if (fx.Unit->Surrendering && hitStruck.TryGetValue(fx.Unit, last) && now - last < HitCreditSeconds)
					{
						ConfirmKill(fx.Unit, "struck colours", LockColor, now);
						hitStruck.Remove(fx.Unit);
					}
					break;
				}
				default:
					break;
			}
		}

		// Crippled by the unit's fire: no engines, the hull nearly gone or the bridge lost.
		cs::List<cs::Ref<BattleUnit>> drop;
		for (const auto& u : hitStruck.Keys())
		{
			if (now - hitStruck[u] > HitCreditSeconds || u == nullptr || !u->Present()) { drop.Add(u); continue; }
			if (hitDisabled.ContainsKey(u) || u->Combat == nullptr || !u->Combat->Disabled()) continue;
			hitDisabled[u] = now;
			ConfirmKill(u, "disabled", Color(1.0f, 0.62f, 0.3f, 1.0f), now);
		}
		for (const auto& u : drop) hitStruck.Remove(u);
	}

	void Hud::DrawEdgePulse(Color tint, float alpha)
	{
		if (PlayerOptions::Calm()) alpha *= 0.45f; // US-T252: Reduce flashes and shake (fainter, still read as a warning)
		if (alpha < 0.004f) return;
		auto old = GUI::color;
		const auto& tex = Icons::White();
		float w = static_cast<float>(Screen::width), h = static_cast<float>(Screen::height);
		constexpr int bands = 8;
		constexpr float band = 7.0f;
		for (int k = 0; k < bands; k++)
		{
			float a = alpha * (1.0f - static_cast<float>(k) / bands);
			float o = k * band;
			GUI::color = WithAlpha(tint, a);
			GUI::DrawTexture(Rect(o, o, w - 2 * o, band), tex);
			GUI::DrawTexture(Rect(o, h - o - band, w - 2 * o, band), tex);
			GUI::DrawTexture(Rect(o, o + band, band, h - 2 * o - 2 * band), tex);
			GUI::DrawTexture(Rect(w - o - band, o + band, band, h - 2 * o - 2 * band), tex);
		}
		GUI::color = old;
	}

	void Hud::DrawHitFeedback()
	{
		if (Event::current().type != EventType::Repaint) return;
		float now = Time::unscaledTime;
		cs::Ref<Battle> battle;
		auto me = app->Travel->Active ? nullptr : HeldUnit(battle);
		if (me == nullptr || battle == nullptr || me->Combat == nullptr)
		{
			hitBattle = nullptr;
			hitUnit = nullptr;
			hitArcs.Clear();
			hitFeed.Clear();
			hitStruck.Clear();
			hitDisabled.Clear();
			return;
		}
		if (battle != hitBattle || me != hitUnit)
		{
			// A new fight or a new seat: what came before is not this unit's.
			hitBattle = battle;
			hitUnit = me;
			hitSeq = battle->FxSeq;
			hitSeenAt = now;
			hitArcs.Clear();
			hitStruck.Clear();
			hitDisabled.Clear();
			hitMarkAt = killMarkAt = shieldDownAt = hullCriticalAt = -100.0f;
			hadShield = ShieldShare(me) > 0.1;
			hullWasCritical = HullShare(me) < HullCritical;
		}
		// A gun mount held fires only itself; every other seat fires all the hull's (or the craft's) weapons.
		cs::Ref<Subsystem> mount;
		if (auto m = cs::As<MountedTurret>(app->HeldSeat())) mount = m->Mount;
		TakeHitFx(battle, me, mount, now);

		// The held hull's own state: its shields going down, its hull going critical.
		double shield = ShieldShare(me), hull = HullShare(me);
		if (shield >= 0)
		{
			if (hadShield && shield <= 0.02) { shieldDownAt = now; hadShield = false; }
			else if (shield > 0.1) hadShield = true;
		}
		if (!hullWasCritical && hull < HullCritical && me->Present()) { hullCriticalAt = now; hullWasCritical = true; }
		else if (hull > HullCritical + 0.05) hullWasCritical = false;

		// The edge pulses, under everything else drawn here.
		float shieldAge = now - shieldDownAt, hullAge = now - hullCriticalAt;
		if (shieldAge < 0.9f) DrawEdgePulse(ShieldArcColor, 0.35f * (1.0f - shieldAge / 0.9f));
		if (hullAge < 1.2f) DrawEdgePulse(HullArcColor, 0.45f * (1.0f - hullAge / 1.2f));
		// Gently, for as long as the hull stays critical.
		else if (hullWasCritical && me->Present()) DrawEdgePulse(HullArcColor, 0.07f * (0.5f + 0.5f * Mathf::Sin(now * 3.0f)));

		auto at = HitReticle();

		// Where the fire is coming from: arcs round the reticle.
		constexpr float arcRadius = 64.0f, arcHalfDeg = 16.0f;
		hitArcs.RemoveAll([&](const HitArc& a) { return now - a.Born > HitArcSeconds; });
		for (const auto& a : hitArcs)
		{
			float fade = 1.0f - (now - a.Born) / HitArcSeconds;
			auto bearing = HitBearing(a.From);
			float centre = Mathf::Atan2(bearing.y, bearing.x);
			auto tint = WithAlpha(a.Shield ? ShieldArcColor : HullArcColor, fade * Mathf::Min(1.0f, 0.55f + 0.15f * a.Weight));
			float thick = a.Shield ? 3.0f : 4.0f + a.Weight * 0.5f;
			constexpr int steps = 6;
			for (int k = 0; k < steps; k++)
			{
				float a0 = centre + (-arcHalfDeg + 2 * arcHalfDeg * k / steps) * Mathf::Deg2Rad;
				float a1 = centre + (-arcHalfDeg + 2 * arcHalfDeg * (k + 1) / steps) * Mathf::Deg2Rad;
				DrawLine(at + Vector2(Mathf::Cos(a0), Mathf::Sin(a0)) * arcRadius, at + Vector2(Mathf::Cos(a1), Mathf::Sin(a1)) * arcRadius, tint, thick);
			}
			// A tick pointing in at the reticle from the middle of the arc.
			DrawLine(at + bearing * (arcRadius + 9), at + bearing * (arcRadius + 1), tint, 2.0f);
		}

		// The hit marker: four ticks round the reticle.
		float markAge = now - hitMarkAt;
		if (markAge < HitMarkSeconds)
		{
			float fade = 1.0f - markAge / HitMarkSeconds;
			bool weak = hitMark == HitMark::WeakPoint;
			auto tint = WithAlpha(hitMark == HitMark::Shield ? ShieldMarkColor : weak ? WeakMarkColor : HullMarkColor, fade);
			float gap = weak ? 10.0f : 8.0f, len = weak ? 10.0f : 7.0f, thick = weak ? 2.5f : 2.0f;
			for (int sx = -1; sx <= 1; sx += 2)
				for (int sy = -1; sy <= 1; sy += 2)
				{
					auto d = Vector2(static_cast<float>(sx), static_cast<float>(sy)).normalized();
					DrawLine(at + d * gap, at + d * (gap + len), tint, thick);
				}
		}

		// The kill marker: a larger cross and a ring opening out.
		float killAge = now - killMarkAt;
		if (killAge < KillMarkSeconds)
		{
			float t = killAge / KillMarkSeconds;
			auto tint = WithAlpha(KillMarkColor, 1.0f - t);
			for (int sx = -1; sx <= 1; sx += 2)
				for (int sy = -1; sy <= 1; sy += 2)
				{
					auto d = Vector2(static_cast<float>(sx), static_cast<float>(sy)).normalized();
					DrawLine(at + d * 12.0f, at + d * 26.0f, tint, 3.0f);
				}
			DrawRing(at, Mathf::Lerp(16.0f, 40.0f, t), tint);
		}

		// The combat feed, right of the middle of the screen, newest last.
		hitFeed.RemoveAll([&](const HitFeedLine& l) { return now - l.Born > HitFeedSeconds; });
		float y = Screen::height * 0.5f + 34.0f, x = Screen::width * 0.5f + 90.0f;
		for (const auto& l : hitFeed)
		{
			float left = HitFeedSeconds - (now - l.Born);
			DrawLabel(l.Text, Vector2(x, y), WithAlpha(l.Tint, Mathf::Min(1.0f, left)), true);
			y += 18.0f;
		}
	}
}
