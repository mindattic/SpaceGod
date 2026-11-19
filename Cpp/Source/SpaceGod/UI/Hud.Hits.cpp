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
#include "Combat/Dogfighting.h"
#include "Combat/Fighters.h"
#include "Combat/FlightDecks.h"
#include "Embodiment/MountedTurret.h"
#include "PlayerOptions.h" // US-T252

#include <cmath>

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

		// US-T309: the held hull's own damage report.
		constexpr Color LostSystemColor{1.0f, 0.36f, 0.26f, 1.0f};
		constexpr Color RestoredSystemColor{0.5f, 0.9f, 0.55f, 1.0f};

		/// The side of the hull a hardpoint sits on (hull frame: right, up, forward), by its largest offset.
		const char* SideOf(const cs::Ref<SubsystemDef>& d)
		{
			double ax = std::abs(d->X), ay = std::abs(d->Y), az = std::abs(d->Z);
			if (ax < 1e-6 && ay < 1e-6 && az < 1e-6) return "CORE";
			if (az >= ax && az >= ay) return d->Z > 0 ? "FORE" : "AFT";
			if (ax >= ay) return d->X > 0 ? "STBD" : "PORT";
			return d->Y > 0 ? "DORSAL" : "VENTRAL";
		}

		// US-T312: the wing's radio calls.
		constexpr Color WingCallColor{0.62f, 0.86f, 1.0f, 1.0f};
		constexpr Color WingLossColor{1.0f, 0.42f, 0.3f, 1.0f};
		/// How long a call on a hull holds before a change of hull is called again (a threat flickering between two).
		constexpr float WingCallHoldSeconds = 2.0f;

		// US-T300: the threat warning.
		constexpr Color MissileWarnColor{1.0f, 0.3f, 0.22f, 1.0f};
		constexpr Color LockWarnColor{1.0f, 0.64f, 0.26f, 1.0f};

		/// A round that blows rather than boards (a boarding pod or a Breacher drills in), as the warning tone counts them.
		bool Warhead(MissileKind k) { return k != MissileKind::Pod && k != MissileKind::Breacher; }

		/// The weight of a warhead's warning: the banner names the heaviest coming.
		int ThreatRank(MissileKind k)
		{
			return k == MissileKind::Antimatter ? 3 : k == MissileKind::Torpedo ? 2 : k == MissileKind::Emp ? 1 : 0;
		}

		const char* ThreatName(MissileKind k)
		{
			return k == MissileKind::Antimatter || k == MissileKind::Torpedo ? "TORPEDO" : k == MissileKind::Emp ? "EMP" : "MISSILE";
		}

		/// Whether a hostile hull that has <paramref name="me"/> as its target holds its nose within 8 degrees of it (the
		/// lock tone's test, Audio/FlightSounds).
		bool NoseOn(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& me)
		{
			if (b->Over) return false;
			const double cosLock = std::cos(8.0 * 3.141592653589793 / 180.0);
			for (const auto& u : b->Units)
			{
				if (u == nullptr || u == me || u->Dead || u->Fled || u->Target != me || !b->Hostile(u, me)) continue;
				auto to = me->Pos - u->Pos;
				double d = to.Length();
				if (d > 1e-9 && DVec3::Dot(u->Forward, to / d) > cosLock) return true;
			}
			return false;
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

	void Hud::AddHitFeed(const cs::String& what, Color tint, float now)
	{
		HitFeedLine line;
		line.Text = what;
		line.Tint = tint;
		line.Born = now;
		hitFeed.Add(line);
		if (hitFeed.Count() > HitFeedMax) hitFeed.RemoveRange(0, hitFeed.Count() - HitFeedMax);
	}

	void Hud::ConfirmKill(const cs::Ref<BattleUnit>& unit, const cs::String& how, Color tint, float now)
	{
		killMarkAt = now;
		AddHitFeed(cs::Cat(unit->Name(), " ", how), tint, now);
	}

	void Hud::TrackWing(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& me, float now, bool takeStock)
	{
		if (me->Fighter == nullptr)
		{
			hitWing.Clear();
			hitWingCalled = nullptr;
			return;
		}
		auto members = Wing::Members(b, me);
		auto order = me->Fighter->Order;
		// The hull the order has the wing on: the lead's lock under Attack my target, whatever is on the lead under Cover
		// me; none in the vee or at will (each picks its own there).
		cs::Ref<BattleUnit> on;
		if (members.Count() > 0 && !b->Over)
		{
			if (order == WingOrder::AttackMyTarget && me->Target != nullptr && me->Target->Present()) on = me->Target;
			else if (order == WingOrder::CoverMe) on = Wing::Threat(b, me);
		}
		if (takeStock)
		{
			hitWing.Clear();
			for (const auto& m : members) hitWing[m] = FlightDecks::Away(m);
			hitWingCalled = on;
			hitWingCalledAt = now;
			return;
		}

		// Shot down, or out of the fight on a deck or gone: a wingman no longer flying with the squadron.
		cs::List<cs::Ref<BattleUnit>> gone;
		for (const auto& u : hitWing.Keys())
			if (!members.Contains(u)) gone.Add(u);
		for (const auto& u : gone)
		{
			if (u != nullptr && u->Dead)
			{
				hitWing.Remove(u);
				AddHitFeed(cs::Cat("WING  ", u->Name(), " is down  (", members.Count(), " left)"), WingLossColor, now);
			}
			// Down on a deck (it comes back as itself, U.SPC-T518): away until it flies again.
			else if (u != nullptr && !u->Fled) hitWing[u] = true;
			else hitWing.Remove(u);
		}
		for (const auto& m : members)
		{
			bool away = FlightDecks::Away(m);
			bool was = false;
			if (!hitWing.TryGetValue(m, was))
			{
				// A craft newly out of the hangar joins quietly.
				hitWing[m] = away;
				continue;
			}
			if (away == was) continue;
			hitWing[m] = away;
			// A recall at the end of the shooting takes them all home; that is not each one's news.
			if (b->Mopping || b->Over) continue;
			AddHitFeed(away ? cs::Cat("WING  ", m->Name(), " heading home to rearm") : cs::Cat("WING  ", m->Name(), " back in the fight"), WingCallColor, now);
		}

		if (on == hitWingCalled) return;
		auto was = hitWingCalled;
		// A threat that flickers from one hull to another, or in and out of reach, is called once, not every frame; a
		// splash is called at once.
		bool splash = was != nullptr && was->Dead;
		if (!splash && now - hitWingCalledAt < WingCallHoldSeconds) return;
		hitWingCalled = on;
		hitWingCalledAt = now;
		if (on != nullptr)
			AddHitFeed(cs::Cat("WING  ", order == WingOrder::CoverMe ? "breaking on " : "engaging ", on->Name()), WingCallColor, now);
		else if (members.Count() > 0 && !b->Over && (order == WingOrder::AttackMyTarget || order == WingOrder::CoverMe))
			AddHitFeed(splash ? cs::Cat("WING  splash, ", was->Name(), " down; forming up")
				: order == WingOrder::CoverMe ? cs::String("WING  you're clear; forming up") : cs::String("WING  no target; forming up"), WingCallColor, now);
	}

	void Hud::TrackOwnDamage(const cs::Ref<BattleUnit>& me, float now, bool takeStock)
	{
		const auto& subs = me->Combat->Subsystems;
		// A refit or a new hull under the same seat changes the list: take stock again rather than report it all.
		if (hitSubDown.Count() != subs.Count()) takeStock = true;
		if (takeStock)
		{
			hitSubDown.Clear();
			for (const auto& s : subs) hitSubDown.Add(s->Destroyed());
			return;
		}
		int i = 0;
		for (const auto& s : subs)
		{
			bool down = s->Destroyed();
			if (down != hitSubDown[i])
			{
				hitSubDown[i] = down;
				(down ? hitTally.SystemsLost : hitTally.SystemsRestored)++;
				HitFeedLine line;
				line.Text = cs::Cat(down ? "LOST  " : "RESTORED  ", SideOf(s->Def), " ", cs::Strings::ToUpperInvariant(SubsystemDef::Name(s->Kind())));
				line.Tint = down ? LostSystemColor : RestoredSystemColor;
				line.Born = now;
				hitFeed.Add(line);
				if (hitFeed.Count() > HitFeedMax) hitFeed.RemoveRange(0, hitFeed.Count() - HitFeedMax);
			}
			i++;
		}
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
				(shield ? hitTally.TakenShield : hitTally.TakenHull)++;
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
					(kind == HitMark::Shield ? hitTally.Shield : kind == HitMark::WeakPoint ? hitTally.Weak : hitTally.Hull)++;
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
						hitTally.Destroyed++;
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
						hitTally.Struck++;
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
			hitTally.Disabled++;
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
			if (hitBattle != nullptr && hitUnit != nullptr) CloseHitTally(hitBattle, hitUnit, now);
			DrawAfterAction(now);
			hitBattle = nullptr;
			hitUnit = nullptr;
			hitArcs.Clear();
			hitFeed.Clear();
			hitStruck.Clear();
			hitDisabled.Clear();
			hitWing.Clear();
			hitWingCalled = nullptr;
			return;
		}
		bool freshSeat = battle != hitBattle || me != hitUnit;
		if (freshSeat)
		{
			// A new fight or a new seat: what came before is not this unit's. Leaving a fight reports it; a new seat in the
			// same fight starts its own tally.
			if (hitBattle != nullptr && hitUnit != nullptr && battle != hitBattle) CloseHitTally(hitBattle, hitUnit, now);
			hitTally = HitTally();
			hitTally.SecondsFrom = battle->Seconds;
			hitTally.HullFrom = HullShare(me);
			hitTally.Where = battle->Name;
			hitTallyDone = false;
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
		// Damage taken while the HUD was hidden or the game paused is not news: take stock instead.
		bool stale = now - hitSeenAt > 0.5f;
		TakeHitFx(battle, me, mount, now);
		TrackOwnDamage(me, now, freshSeat || stale);
		TrackWing(battle, me, now, freshSeat || stale);
		if (battle->Over || me->Dead || me->Fled) CloseHitTally(battle, me, now);

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

		if (me->Present()) DrawThreatWarning(battle, me, at, now);

		DrawAfterAction(now);

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

	void Hud::DrawThreatWarning(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& me, Vector2 at, float now)
	{
		constexpr float ringRadius = 92.0f;
		int count = 0, rank = -1;
		auto worst = MissileKind::Missile;
		double soonest = -1;
		for (const auto& m : battle->Missiles)
		{
			if (m == nullptr || m->Gone || m->Target != me || m->Side == me->Side || !Warhead(m->Kind)) continue;
			count++;
			int r = ThreatRank(m->Kind);
			if (r > rank) { rank = r; worst = m->Kind; }

			auto rel = m->Pos - me->Pos;
			double dist = rel.Length();
			auto from = dist > 1e-12 ? rel / dist : -me->Forward;
			double closing = dist > 1e-12 ? -DVec3::Dot(m->Vel - me->Vel, from) : 0;
			double impact = closing > 1e-9 ? dist / closing : -1;
			if (impact >= 0 && (soonest < 0 || impact < soonest)) soonest = impact;

			// Brighter as the impact nears: full under a couple of seconds out.
			float urgency = impact >= 0 ? Mathf::Clamp01(1.0f - static_cast<float>(impact) / 10.0f) : 0.25f;
			auto tint = WithAlpha(MissileWarnColor, 0.5f + 0.5f * urgency);

			// A chevron on the ring, pointing in at the reticle from the way the round comes.
			auto bearing = HitBearing(from);
			auto across = Vector2(-bearing.y, bearing.x);
			float size = r >= 2 ? 12.0f : 8.0f;
			auto tip = at + bearing * ringRadius;
			auto back = tip + bearing * size;
			DrawLine(tip, back + across * (size * 0.8f), tint, 2.5f);
			DrawLine(tip, back - across * (size * 0.8f), tint, 2.5f);

			// The round itself, while it is on screen.
			Vector2 p;
			float depth = 0;
			if (app->ToGui(app->BattleScene(battle, m->Pos), p, depth))
			{
				DrawRing(p, r >= 2 ? 11.0f : 8.0f, tint);
				WorldLabels wl(worldLabelPass);
				DrawLabel(impact >= 0 ? cs::Cat(cs::Fmt(impact, "0.0"), "s") : cs::String(ThreatName(m->Kind)), p + Vector2(13, -8), tint, true);
			}
		}

		cs::String banner;
		Color tint;
		if (count > 0)
		{
			banner = cs::Cat(ThreatName(worst), count > 1 ? cs::Cat("  x", count) : cs::String(""), soonest >= 0 ? cs::Cat("  ", cs::Fmt(soonest, "0.0"), "s") : cs::String(""));
			tint = MissileWarnColor;
		}
		else if (NoseOn(battle, me))
		{
			banner = "LOCK";
			tint = LockWarnColor;
		}
		if (banner.empty()) return;

		// Flashing, or steady and dimmer under Reduce flashes (US-T252).
		float alpha = PlayerOptions::Calm() ? 0.8f : 0.55f + 0.45f * Mathf::Sin(now * (count > 0 ? 14.0f : 7.0f));
		auto size = label->CalcSize(GUIContent(banner));
		DrawLabel(banner, Vector2(at.x - size.x * 0.5f, at.y - ringRadius - 22.0f - size.y), WithAlpha(tint, alpha), false);
	}

	void Hud::CloseHitTally(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& me, float now)
	{
		if (hitTallyDone) return;
		hitTallyDone = true;
		auto& t = hitTally;
		// A fight the unit neither fired in nor took fire in (a seat taken for a look) is not worth a report.
		if (t.Shield + t.Hull + t.Weak + t.TakenShield + t.TakenHull + t.Destroyed + t.Disabled + t.Struck + t.SystemsLost == 0) return;
		t.SecondsTo = battle->Seconds;
		t.HullTo = me->Dead ? 0.0 : HullShare(me);
		if (me->Dead) { t.Outcome = "HULL LOST"; t.Tint = HostileColor; }
		else if (battle->Over && battle->Winner == me->Side) { t.Outcome = "VICTORY"; t.Tint = Color(0.55f, 1.0f, 0.62f, 1.0f); }
		else if (battle->Over && battle->Winner < 0) { t.Outcome = "BROKE OFF"; t.Tint = Accent; }
		else if (battle->Over) { t.Outcome = "DEFEAT"; t.Tint = HostileColor; }
		else if (me->Fled) { t.Outcome = "WITHDREW"; t.Tint = LockWarnColor; }
		else { t.Outcome = "LEFT THE FIGHT"; t.Tint = Accent; }
		hitReport = t;
		hitReportAt = now;
	}

	void Hud::DrawAfterAction(float now)
	{
		float age = now - hitReportAt;
		if (age < 0 || age > AfterActionSeconds) return;
		const auto& t = hitReport;
		float alpha = Mathf::Min(1.0f, (AfterActionSeconds - age) / 1.5f);

		int secs = Mathf::Max(0, Mathf::RoundToInt(static_cast<float>(t.SecondsTo - t.SecondsFrom)));
		cs::String length = cs::Cat(secs / 60, ":", secs % 60 < 10 ? "0" : "", secs % 60);
		auto pct = [](double share) { return cs::Cat(Mathf::RoundToInt(Mathf::Clamp01(static_cast<float>(share)) * 100), "%"); };
		int hits = t.Shield + t.Hull + t.Weak;

		cs::List<cs::String> lines;
		lines.Add(cs::Cat(t.Where.empty() ? cs::String("the fight") : t.Where, "  \xC2\xB7  ", length));
		lines.Add(hits == 0 ? cs::String("No hits landed")
			: cs::Cat(hits, hits == 1 ? " hit" : " hits", "  \xC2\xB7  shield ", t.Shield, "  hull ", t.Hull, "  weak point ", t.Weak));
		int downed = t.Destroyed + t.Disabled + t.Struck;
		lines.Add(downed == 0 ? cs::String("No hulls downed")
			: cs::Cat(t.Destroyed, " destroyed  \xC2\xB7  ", t.Disabled, " disabled  \xC2\xB7  ", t.Struck, " struck colours"));
		lines.Add(cs::Cat("Taken  shield ", t.TakenShield, "  hull ", t.TakenHull, "  \xC2\xB7  hull ", pct(t.HullFrom), " \xE2\x86\x92 ", pct(t.HullTo)));
		if (t.SystemsLost > 0 || t.SystemsRestored > 0) // US-T309
			lines.Add(cs::Cat("Systems lost ", t.SystemsLost, t.SystemsRestored > 0 ? cs::Cat("  \xC2\xB7  ", t.SystemsRestored, " brought back") : cs::String()));

		constexpr float w = 340, pad = 12, rowH = 18;
		float h = pad * 2 + rowH * (1 + lines.Count());
		auto r = Rect(Screen::width * 0.5f - w * 0.5f, 96.0f, w, h);
		auto old = GUI::color;
		GUI::color = WithAlpha(Color(1, 1, 1, 1), alpha);
		GUI::Box(r, GUIContent::none(), panel);
		GUI::color = old;
		float y = r.y + pad;
		DrawLabel(cs::Cat("AFTER ACTION  \xC2\xB7  ", t.Outcome), Vector2(r.x + pad, y), WithAlpha(t.Tint, alpha), false);
		y += rowH + 2;
		for (const auto& l : lines)
		{
			DrawLabel(FitText(l, minorLabel, w - 2 * pad), Vector2(r.x + pad, y), WithAlpha(Dim, alpha), true);
			y += rowH;
		}
	}
}
