// Hud battle plot (US-T367): in a fight, a small top-down plot at the left edge of the screen of every hull around the
// one the controls hold -- yours at the middle, nose up, the others by side in the stance colours (the colour-blind
// palette's when it is on), struck or captured ones dimmed, craft as smaller dots -- with your lock ringed and the marks
// of your standing fire (F), paint (P) and cripple (C) calls. Shift+F10 (KeyAction::BattlePlot) turns it off and on.
// Declared in Hud.Panels.inl / Hud.Main.inl.
//
// Read only: it reads the fight's units and the call fields and writes nothing back, so the Bench is unchanged. The
// plot's radius is the farthest hull out, rounded up to a round number of km (within a floor and a ceiling); a hull
// beyond the ceiling sits on the rim. Top-down means the plane square to the world's up; the hull held's nose is up.

#include "UI/Hud.h"

#include "UnitySpaceApp.h"
#include "UI/Icons.h"
#include "Input/InputDevice.h"
#include "Input/KeyBindings.h"

#include "Camera/StarTravel.h"
#include "Combat/Battle.h"

#include <algorithm>
#include <cmath>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		constexpr float PlotW = 196, PlotHeadH = 20, PlotPad = 8;
		constexpr double MinRadiusKm = 5, MaxRadiusKm = 2000;

		/// The plot's radius: the farthest hull out, rounded up to 1, 2 or 5 times a power of ten.
		double NiceRadius(double km)
		{
			km = std::clamp(km, MinRadiusKm, MaxRadiusKm);
			double step = 1;
			while (step * 10 <= km) step *= 10;
			for (double m : { 1.0, 2.0, 5.0, 10.0 })
				if (step * m >= km) return step * m;
			return step * 10;
		}

		void Dot(Vector2 p, float size, Color c)
		{
			auto old = GUI::color;
			GUI::color = c;
			GUI::DrawTexture(Rect(p.x - size * 0.5f, p.y - size * 0.5f, size, size), Icons::White());
			GUI::color = old;
		}

		void Circle(Vector2 centre, float radius, Color c, int dots)
		{
			for (int i = 0; i < dots; i++)
			{
				float a = i * 2 * Mathf::PI / dots;
				Dot(Vector2(centre.x + Mathf::Cos(a) * radius, centre.y + Mathf::Sin(a) * radius), 2, c);
			}
		}

		Color Faded(Color c, float alpha) { return Color(c.r, c.g, c.b, alpha); }
	}

	bool Hud::UpdateBattlePlotKey(bool keysFree)
	{
		if (!keysFree) return false;
		const Input::InputDevice& kb = Input::InputDevice::current();
		if (!Input::KeyBindings::Pressed(Input::KeyAction::BattlePlot, kb)) return false;
		cs::Ref<Battle> battle;
		if (HeldUnit(battle) == nullptr || battle == nullptr) return false; // out of a fight the key does nothing here
		battlePlotShown = !battlePlotShown;
		return true; // spent (F10 alone is the fleet's: it does not go off under it)
	}

	void Hud::DrawBattlePlot()
	{
		if (!battlePlotShown || app->Travel->Active || app->InFtlCruise()) return;
		cs::Ref<Battle> battle;
		auto me = HeldUnit(battle);
		if (me == nullptr || battle == nullptr || !me->Present()) return;

		// The hull held's nose in the top-down plane (world X/Z; Y is up): screen up. Its right: screen right.
		double fx = me->Forward.X, fz = me->Forward.Z;
		double fl = std::sqrt(fx * fx + fz * fz);
		if (fl < 1e-9)
		{
			fx = 0;
			fz = 1;
		}
		else
		{
			fx /= fl;
			fz /= fl;
		}
		double rx = fz, rz = -fx;

		double farthest = 0;
		for (const auto& u : battle->Units)
		{
			if (u == nullptr || u == me || !u->Present()) continue;
			double dx = u->Pos.X - me->Pos.X, dz = u->Pos.Z - me->Pos.Z;
			farthest = std::max(farthest, std::sqrt(dx * dx + dz * dz));
		}
		double radiusKm = NiceRadius(farthest * 1.05);

		float h = PlotHeadH + PlotW;
		float x = SafeLeft() + HudLayout::Margin + (navigatorOpen ? 268 + HudLayout::Gap : 0);
		float y = Mathf::Max(HudLayout::TopBarBottom + HudLayout::Margin, (Screen::height - h) * 0.5f);
		auto r = Rect(x, y, PlotW, h);
		RegisterUi(r);
		if (Event::current().type != EventType::Repaint) return;

		GUI::Box(r, GUIContent::none(), panel);
		auto oldContent = GUI::contentColor;
		GUI::contentColor = Accent;
		GUI::Label(Rect(r.x + PlotPad, r.y + 3, PlotW - 2 * PlotPad, 16),
			cs::Cat("PLOT  \xC2\xB7  ", RangeText(radiusKm), "  \xC2\xB7  ", Input::KeyBindings::Keys(Input::KeyAction::BattlePlot), " hides"), small);
		GUI::contentColor = oldContent;

		float inner = (PlotW - 2 * PlotPad) * 0.5f;
		auto centre = Vector2(r.x + PlotW * 0.5f, r.y + PlotHeadH + PlotW * 0.5f);
		Circle(centre, inner, Faded(Dim, 0.55f), 40);
		Circle(centre, inner * 0.5f, Faded(Dim, 0.3f), 24);

		auto place = [&](const cs::Ref<BattleUnit>& u)
		{
			double dx = u->Pos.X - me->Pos.X, dz = u->Pos.Z - me->Pos.Z;
			double right = dx * rx + dz * rz, up = dx * fx + dz * fz;
			auto p = Vector2(static_cast<float>(right / radiusKm * inner), static_cast<float>(-up / radiusKm * inner));
			if (p.magnitude() > inner) p = p.normalized() * inner; // past the ceiling: on the rim
			return centre + p;
		};

		for (const auto& u : battle->Units)
		{
			if (u == nullptr || u == me || !u->Present()) continue;
			bool hostile = battle->Hostile(me, u);
			Color c = hostile ? HostileColor : u->Side == me->Side ? ArrowFriendlyColor : ArrowNeutralColor;
			if (u->Captured || u->Surrendering) c = Faded(c, 0.35f);
			else if (u->Fleeing) c = Faded(c, 0.6f);
			float size = u->Kind == UnitKind::Fighter ? 3.0f : u->Kind == UnitKind::Platform ? 6.0f : 5.0f;
			Dot(place(u), size, c);
		}

		// The marks: the lock ringed, then each standing call's letter beside its hull.
		double now = battle->Seconds;
		auto mark = [&](const cs::Ref<BattleUnit>& t, const char* letter, float dx, Color c)
		{
			if (t == nullptr || !t->Present()) return;
			auto p = place(t);
			auto old = GUI::contentColor;
			GUI::contentColor = c;
			GUI::Label(Rect(p.x + dx, p.y - 8, 14, 14), letter, small);
			GUI::contentColor = old;
		};
		if (me->PlayerLockTarget != nullptr && me->PlayerLockTarget->Present())
		{
			auto lp = place(me->PlayerLockTarget);
			Circle(lp, 6, LockColor, 10);
		}
		if (now < me->FireCallUntil) mark(me->FireCallTarget, "F", 6, LockColor);
		if (now < me->PaintCallUntil) mark(me->PaintCallTarget, "P", 6 + 9, Accent);
		if (now < me->CrippleCallUntil) mark(me->CrippleCallTarget, "C", 6 + 18, HostileColor);

		// You: a bright dot with a tick along the nose.
		DrawLine(centre, centre + Vector2(0, -9), Accent, 1.5f);
		Dot(centre, 6, Accent);
	}
}
