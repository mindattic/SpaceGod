// Hud sights: the fighter seat's two reticles (US-T129) and the off-screen target arrows (US-T128). Declarations in Hud.Main.inl.

#include "UI/Hud.h"

#include "UnitySpaceApp.h"
#include "UI/Icons.h"

#include "Combat/Battle.h"
#include "Combat/Diplomacy.h"
#include "Combat/Pursuit.h"
#include "Combat/War.h"
#include "Embodiment/FighterSeat.h"
#include "Embodiment/MountedTurret.h"
#include "Embodiment/StationSeat.h"
#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Systems/FtlNavigation.h"
#include "World.h"
#include "Ships.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	// ---------------------------------------------------------------- the seat's two reticles (US-T129)

	void Hud::DrawSeatNose()
	{
		auto f = cs::As<FighterSeat>(app->HeldSeat());
		if (f == nullptr || f->Unit == nullptr || f->Unit->Forward.LengthSquared() < 1e-12) return;
		// The view is the mouse's (the crosshair in the middle); the nose swings over to it at the craft's own rate.
		Vector2 nose;
		if (!DirectionToGui(f->Unit->Forward.Normalized(), nose)) return;
		auto centre = Vector2(Screen::width * 0.5f, Screen::height * 0.5f);
		float apart = (nose - centre).magnitude();
		if (apart < 3) return;
		if (apart > 22) DrawLine(nose, centre, Color(1, 1, 1, 0.18f), 1.0f);
		DrawBoresight(nose, Color(1, 1, 1, 0.85f));
	}

	// ---------------------------------------------------------------- off-screen target arrows (US-T128)

	namespace
	{
		/// One arrow to draw: where it lies (camera-relative, Unity axes), how far (km) against how far is "close".
		struct EdgeArrowItem
		{
			Vector3 Rel;
			double Km = 0;
			double ReachKm = 1;
			Color Tint;
			cs::String Label;
			bool Ringed = false;
		};

		Vector3 ToUnity(DVec3 eclipticDirection)
		{
			auto d = eclipticDirection.SwapYZ();
			return Vector3(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z));
		}
	}

	cs::Ref<BattleUnit> Hud::HeldUnit(cs::Ref<Battle>& battle)
	{
		battle = nullptr;
		if (app->IsPiloting())
		{
			auto me = app->PilotUnit();
			if (me != nullptr) battle = app->Piloted.Ship->Battle;
			return me;
		}
		auto held = app->HeldSeat();
		if (auto f = cs::As<FighterSeat>(held)) { battle = f->Battle; return f->Unit; }
		if (auto m = cs::As<MountedTurret>(held)) { battle = m->Battle; return m->Unit; }
		if (auto st = cs::As<StationSeat>(held)) return st->Unit(battle);
		return nullptr;
	}

	cs::Ref<Faction> Hud::HeldFaction()
	{
		auto held = app->HeldSeat();
		if (held != nullptr) return held->Faction();
		return app->IsPiloting() && app->Piloted.Ship != nullptr ? app->Piloted.Ship->Faction : nullptr;
	}

	bool Hud::DrawSelectionArrow(DVec3 scene, const cs::String& name)
	{
		if (app->HeldSeat() == nullptr && !app->IsPiloting()) return false;
		auto rel = app->Relative(scene);
		cs::Ref<Battle> battle;
		auto me = HeldUnit(battle);
		auto t = app->Selected;
		// In this fight, by the fight's own km and the craft's gun reach; anywhere else, by the scene's distance in true km
		// (the scale model undone where the camera is) against the scanners' reach.
		auto unit = battle != nullptr && t.IsShip() ? battle->UnitOf(t.Ship) : t.IsUnit() && t.Battle == battle ? t.Unit : nullptr;
		double km = unit != nullptr && me != nullptr ? (unit->Pos - me->Pos).Length() : SceneSpeedToKmS(rel.magnitude());
		double reach = unit != nullptr && me != nullptr ? ArrowReachKm(me) : War::ScanRangeKm;
		auto tint = unit != nullptr && me != nullptr ? (battle->Hostile(me, unit) ? HostileColor : unit->Side == me->Side ? ArrowFriendlyColor : ArrowNeutralColor)
			: t.IsShip() ? StanceColor(HeldFaction(), t.Ship->Faction) : Accent;
		DrawEdgeArrow(rel, ArrowSize(km, reach), tint, cs::Cat(name, "  ", RangeText(km)), unit != nullptr && me != nullptr && unit == me->PlayerLockTarget);
		return true;
	}

	double Hud::LongestGunKm(const cs::Ref<BattleUnit>& u)
	{
		double best = 0;
		if (u == nullptr || u->Combat == nullptr) return best;
		for (const auto& s : u->Combat->Subsystems)
			if (!s->Destroyed() && s->Def != nullptr && s->Def->Weapon != nullptr) best = cs::Math::Max(best, s->Def->Weapon->RangeKm);
		return best;
	}

	double Hud::ArrowReachKm(const cs::Ref<BattleUnit>& me)
	{
		double gun = LongestGunKm(me);
		return ArrowReachGuns * (gun > 0 ? gun : ArrowFallbackGunKm);
	}

	Color Hud::StanceColor(const cs::Ref<Faction>& mine, const cs::Ref<Faction>& theirs)
	{
		if (mine == nullptr || theirs == nullptr) return ArrowNeutralColor;
		if (mine == theirs) return ArrowFriendlyColor;
		auto diplomacy = app->World()->Diplomacy();
		return diplomacy != nullptr && diplomacy->Hostile(mine, theirs) ? HostileColor : ArrowNeutralColor;
	}

	float Hud::ArrowSize(double km, double reachKm)
	{
		float close = 1.0f - Mathf::Clamp01(static_cast<float>(km / cs::Math::Max(reachKm, 1e-9)));
		return Mathf::Lerp(ArrowMinPx, ArrowMaxPx, Mathf::Pow(close, 1.5f));
	}

	void Hud::DrawEdgeArrow(Vector3 rel, float size, Color tint, const cs::String& text_, bool ringed)
	{
		auto local = Quaternion::Inverse(app->SpaceCamera->Rotation) * rel;
		auto dir = Vector2(local.x, -local.y);
		// Dead astern there is no side to point to: point down, where turning round starts.
		dir = dir.sqrMagnitude() < 1e-12f ? Vector2(0, 1) : dir.normalized();
		auto centre = Vector2(Screen::width * 0.5f, Screen::height * 0.5f);
		float margin = ArrowMaxPx * 0.5f + 14.0f;
		float sx = (centre.x - margin) / Mathf::Max(Mathf::Abs(dir.x), 1e-4f);
		float sy = (centre.y - margin) / Mathf::Max(Mathf::Abs(dir.y), 1e-4f);
		auto p = centre + dir * Mathf::Min(sx, sy);

		float close = Mathf::InverseLerp(ArrowMinPx, ArrowMaxPx, size);
		auto color = Color(tint.r, tint.g, tint.b, Mathf::Lerp(0.6f, 1.0f, close));
		auto matrix = GUI::matrix;
		GUIUtility::RotateAroundPivot(Mathf::Atan2(dir.y, dir.x) * Mathf::Rad2Deg + 90.0f, p);
		DrawIcon(Icons::EdgeArrow(), p, size, color);
		GUI::matrix = matrix;
		if (ringed) DrawRing(p, size * 0.5f + 5, LockColor);
		if (text_.empty()) return;
		WorldLabels wl(worldLabelPass); // US-T214
		float w = minorLabel->CalcSize(GUIContent(text_)).x;
		auto at = p - dir * (size * 0.5f + 8);
		DrawLabel(text_, Vector2(dir.x > 0.3f ? at.x - w : dir.x < -0.3f ? at.x : at.x - w * 0.5f, dir.y > 0.3f ? at.y - 18 : dir.y < -0.3f ? at.y + 2 : at.y - 8),
			color, true);
	}

	void Hud::DrawOffscreenArrows()
	{
		if (Event::current().type != EventType::Repaint || app->Travel->Active) return;
		// Only at the controls: the command view keeps its own pointer to the selection (DrawOffscreenPointer).
		auto held = app->HeldSeat();
		if (held == nullptr && !app->IsPiloting()) return;
		cs::Ref<Battle> battle;
		auto me = HeldUnit(battle);
		auto mine = HeldFaction();

		// "Close" is a multiple of this craft's longest gun reach: true km, so an arrow reads the same at either scale.
		double reach = ArrowReachKm(me);
		cs::List<EdgeArrowItem> items;
		auto onScreen = [this](Vector3 rel)
		{
			Vector2 p;
			return app->RelativeToGui(rel, p) && p.x >= 0 && p.y >= 0 && p.x <= Screen::width && p.y <= Screen::height;
		};
		auto selectedShip = app->Selected.IsShip() ? app->Selected.Ship : nullptr;

		// In a fight: every hostile, and the hull locked whatever it is (the selection keeps its own pointer).
		if (battle != nullptr && me != nullptr)
		{
			auto locked = me->PlayerLockTarget;
			for (const auto& o : battle->Units)
			{
				if (o == me || !o->Present() || o->Captured || (selectedShip != nullptr && o->Ship == selectedShip) || (app->Selected.IsUnit() && app->Selected.Unit == o)) continue;
				bool isLock = o == locked;
				bool hostile = battle->Hostile(me, o);
				if (!hostile && !isLock) continue;
				auto rel = app->Relative(app->BattleScene(battle, o->Pos));
				if (onScreen(rel)) continue;
				EdgeArrowItem a;
				a.Rel = rel;
				a.Km = (o->Pos - me->Pos).Length();
				a.ReachKm = reach;
				a.Tint = hostile ? HostileColor : o->Side == me->Side ? ArrowFriendlyColor : ArrowNeutralColor;
				a.Ringed = isLock;
				a.Label = isLock ? cs::Cat(o->Name(), "  ", RangeText(a.Km)) : cs::String();
				items.Add(a);
			}
		}
		// Out of a fight at the helm: the hunters running the ship down.
		else if (app->IsPiloting() && app->World()->Pursuit != nullptr && app->World()->Pursuit->Active())
		{
			auto at = app->PilotOffsetKm();
			for (const auto& g : app->World()->Pursuit->Groups)
			{
				auto to = g->Pos - at;
				double km = to.Length();
				if (km < 1e-9) continue;
				auto rel = ToUnity(to / km) * 1e6f;
				if (onScreen(rel)) continue;
				EdgeArrowItem a;
				a.Rel = rel;
				a.Km = km;
				a.ReachKm = reach;
				a.Tint = StanceColor(mine, g->Faction());
				a.Label = cs::Cat(g->Leader->Name, "  ", RangeText(km));
				items.Add(a);
			}
		}
		// The nearest first; only so many, so a fleet battle does not ring the screen.
		items.Sort([](const EdgeArrowItem& a, const EdgeArrowItem& b) { return cs::Compare(a.Km, b.Km); });
		int drawn = 0;
		for (const auto& a : items)
		{
			if (drawn >= ArrowMaxCount && !a.Ringed) continue;
			// The nearest hostile is named by its range too.
			auto named = !a.Label.empty() ? a.Label : drawn == 0 ? RangeText(a.Km) : cs::String();
			DrawEdgeArrow(a.Rel, ArrowSize(a.Km, a.ReachKm), a.Tint, named, a.Ringed);
			drawn++;
		}

		// The objective: a plotted course's next star or beacon, light-years off as often as not, so sized against the scanners' reach.
		DVec3 dir;
		double km = 0;
		if (app->IsPiloting() && app->PlottedCourse != nullptr && app->PlottedBearing(dir, km) && dir.LengthSquared() > 1e-12)
		{
			auto rel = ToUnity(dir.Normalized()) * 1e6f;
			if (!onScreen(rel))
				DrawEdgeArrow(rel, ArrowSize(km, War::ScanRangeKm), StreakColor, cs::Cat(cs::Strings::ToUpperInvariant(app->PlottedCourse->Target->Name), "  ", app->Distance(app->PlottedCourse->Target)), false);
		}
	}
}
