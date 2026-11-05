// Hud: the role panel of whatever seat is held (US-T135). Declared in Hud.Boards.inl.

#include "UI/Hud.h"

#include "UnitySpaceApp.h"

#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Embodiment/Embodiment.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	void Hud::DrawRolePanel()
	{
		auto held = app->Seat();
		if (held == nullptr || app->ShowPicker) return;
		auto p = held->RolePanel();
		const auto& tex = Texture2D::whiteTexture();
		auto old = GUI::contentColor;
		auto oldColor = GUI::color;

		// The keys of the role, down the left.
		constexpr float pad = 10, rowH = 18, keyW = 150, w = 440;
		float h = pad * 2 + rowH * (p.Abilities.Count() + 1);
		auto r = Rect(16, Screen::height - 130 - h, w, h);
		GUI::Box(r, GUIContent::none(), panel);
		GUI::contentColor = Color(0.6f, 0.95f, 1.0f, 1.0f);
		GUI::Label(Rect(r.x + pad, r.y + pad - 2, w - 2 * pad, rowH), cs::Cat(p.Title, "  \xC2\xB7  ", held->Name()), small);
		for (int i = 0; i < p.Abilities.Count(); i++)
		{
			const auto& a = p.Abilities[i];
			float y = r.y + pad - 2 + rowH * (i + 1);
			GUI::contentColor = a.Ready ? Color::white() : Dim;
			GUI::Label(Rect(r.x + pad, y, keyW, rowH), a.Key, small);
			GUI::contentColor = a.Ready ? Color(0.85f, 0.9f, 0.95f, 1.0f) : Dim;
			GUI::Label(Rect(r.x + pad + keyW, y, w - keyW - 2 * pad, rowH), a.Label, small);
		}

		// The part of the hull it is working, or the nearest work: a bracket on it.
		auto sub = held->Subject();
		if (sub.Part != nullptr && sub.Unit != nullptr && sub.Battle != nullptr && Event::current().type == EventType::Repaint)
		{
			Vector2 at;
			float depth = 0;
			if (app->ToGui(app->BattleScene(sub.Battle, sub.Unit->PointOf(sub.Part->Def)), at, depth))
			{
				GUI::color = p.Meter.Show ? Color(0.55f, 1.0f, 0.65f, 0.95f) : Color(1.0f, 0.75f, 0.3f, 0.95f);
				constexpr float s = 16, t = 2, arm = 6;
				for (int sx = -1; sx <= 1; sx += 2)
					for (int sy = -1; sy <= 1; sy += 2)
					{
						float cx = at.x + sx * s, cy = at.y + sy * s;
						GUI::DrawTexture(Rect(sx < 0 ? cx : cx - arm, cy - t * 0.5f, arm, t), tex);
						GUI::DrawTexture(Rect(cx - t * 0.5f, sy < 0 ? cy : cy - arm, t, arm), tex);
					}
				GUI::color = oldColor;
			}
		}

		// The role's meter (the weld's timing), centred above the seat line.
		if (p.Meter.Show)
		{
			constexpr float mw = 420, mh = 74;
			auto m = Rect((Screen::width - mw) * 0.5f, Screen::height - 108 - mh, mw, mh);
			GUI::Box(m, GUIContent::none(), panel);
			auto bar = Rect(m.x + 14, m.y + 26, mw - 28, 14);
			GUI::color = Color(0.15f, 0.18f, 0.22f, 0.9f);
			GUI::DrawTexture(bar, tex);
			float lo = static_cast<float>(Mathf::Clamp01(static_cast<float>(p.Meter.ZoneLo))), hi = static_cast<float>(Mathf::Clamp01(static_cast<float>(p.Meter.ZoneHi)));
			GUI::color = Color(0.35f, 0.95f, 0.55f, 0.85f);
			GUI::DrawTexture(Rect(bar.x + bar.width * lo, bar.y, bar.width * (hi - lo), bar.height), tex);
			float mx = bar.x + bar.width * Mathf::Clamp01(static_cast<float>(p.Meter.Marker));
			GUI::color = Color(1.0f, 0.85f, 0.35f, 1.0f);
			GUI::DrawTexture(Rect(mx - 2, bar.y - 4, 4, bar.height + 8), tex);
			// The job's progress and the weld's quality underneath.
			auto prog = Rect(bar.x, bar.yMax() + 6, bar.width, 5);
			GUI::color = Color(0.15f, 0.18f, 0.22f, 0.9f);
			GUI::DrawTexture(prog, tex);
			GUI::color = Color(0.5f, 0.8f, 1.0f, 0.9f);
			GUI::DrawTexture(Rect(prog.x, prog.y, prog.width * Mathf::Clamp01(static_cast<float>(p.Meter.Progress)), prog.height), tex);
			GUI::color = oldColor;
			GUI::contentColor = Dim;
			GUI::Label(Rect(m.x + 14, m.y + 5, mw - 28, 18), cs::Cat(p.Meter.Label, "  \xC2\xB7  tap left mouse in the green  \xC2\xB7  weld quality ",
				cs::Fmt(p.Meter.Quality * 100, "0"), "%  \xC2\xB7  job ", cs::Fmt(p.Meter.Progress * 100, "0"), "%"), small);
			if (!p.Meter.Verdict.empty())
			{
				GUI::contentColor = p.Meter.VerdictGood ? Color(0.55f, 1.0f, 0.65f, 1.0f) : Color(1.0f, 0.5f, 0.35f, 1.0f);
				GUI::Label(Rect(m.x + 14, m.y + mh - 22, mw - 28, 18), p.Meter.Verdict, small);
			}
		}
		GUI::contentColor = old;
		GUI::color = oldColor;
	}
}
