// US-T306: the galaxy map's find. A field over the map's top left (Ctrl+F, or a click) lists the systems and charted stars
// whose name holds what is typed, the matches bracketed on the map; Up / Down / Enter or a click picks one, which selects
// it (its card on the right) and centres the map on it. Declarations in GalaxyMapScreen.h.

#include "UI/GalaxyMapScreen.h"

#include "UnitySpaceApp.h"
#include "Information/GalaxyMap.h"
#include "Systems/Galaxy.h"
#include "Systems/Polities.h"

#include <algorithm>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	void GalaxyMapScreen::Closed()
	{
		if (finding) GUIUtility::keyboardControl = 0;
		finding = false;
		focusFind = false;
	}

	void GalaxyMapScreen::FindKeys()
	{
		Event& e = Event::current();
		if (e.type != EventType::KeyDown) return;
		if (e.keyCode == KeyCode::F && (e.control || e.command))
		{
			focusFind = true;
			e.Use();
			return;
		}
		if (!finding) return;
		if (e.keyCode == KeyCode::Escape)
		{
			// Esc empties the field first, then leaves it.
			if (!findText.empty()) findText.clear();
			else Closed();
			e.Use();
		}
		else if (e.keyCode == KeyCode::Return || e.keyCode == KeyCode::KeypadEnter)
		{
			if (!findHits.empty()) GoTo(findHits[std::clamp(findPick, 0, static_cast<int>(findHits.size()) - 1)]);
			else Closed();
			e.Use();
		}
		else if (e.keyCode == KeyCode::DownArrow && !findHits.empty())
		{
			findPick = std::min(findPick + 1, static_cast<int>(findHits.size()) - 1);
			e.Use();
		}
		else if (e.keyCode == KeyCode::UpArrow && !findHits.empty())
		{
			findPick = std::max(findPick - 1, 0);
			e.Use();
		}
	}

	void GalaxyMapScreen::RefreshFind()
	{
		if (findFor == findText && findModel == model.get()) return;
		if (findFor != findText) findPick = 0;
		findFor = findText;
		findModel = model.get();
		findHits.clear();
		cs::String q = cs::Strings::Trim(cs::Strings::ToLowerInvariant(findText));
		if (q.empty() || model == nullptr) return;

		struct Ranked
		{
			FindHit Hit;
			int Rank;
			double Ly;
		};
		std::vector<Ranked> all;
		auto here = app->ActiveSystem->PositionLy;
		cs::HashSet<cs::String> names;
		for (const auto& m : model->Systems)
		{
			cs::String lower = cs::Strings::ToLowerInvariant(m->System->Name);
			names.Add(lower);
			if (!cs::Strings::Contains(lower, q)) continue;
			double ly = (m->System->PositionLy - here).Length();
			FindHit h;
			h.System = m->System;
			h.Name = m->System->Name;
			h.Ly = m->System->PositionLy;
			cs::String who = m->Here ? cs::String("you are here")
				: !m->Known() ? cs::String("no news")
				: m->Holder != nullptr ? m->Holder->Name : cs::String("unclaimed");
			h.Note = m->Here ? who : cs::Cat(who, "  \xC2\xB7  ", cs::Fmt(ly, "0.0"), " ly");
			all.push_back({ h, cs::Strings::StartsWith(lower, q) ? 0 : 2, ly });
		}
		for (const auto& n : chartStars)
		{
			cs::String lower = cs::Strings::ToLowerInvariant(n.Name);
			if (names.Contains(lower) || !cs::Strings::Contains(lower, q)) continue;
			double ly = (n.PositionLy - here).Length();
			FindHit h;
			h.Star = n;
			h.Name = n.Name;
			h.Ly = n.PositionLy;
			h.Note = cs::Cat("charted star  \xC2\xB7  ", cs::Fmt(ly, "0.0"), " ly");
			all.push_back({ h, cs::Strings::StartsWith(lower, q) ? 1 : 3, ly });
		}
		// A name that starts with it first, the systems before the plain stars, then the nearest.
		std::stable_sort(all.begin(), all.end(), [](const Ranked& a, const Ranked& b)
		{
			return a.Rank != b.Rank ? a.Rank < b.Rank : a.Ly < b.Ly;
		});
		for (int i = 0; i < static_cast<int>(all.size()) && i < MaxFindHits; i++) findHits.push_back(all[i].Hit);
		findPick = std::clamp(findPick, 0, std::max(0, static_cast<int>(findHits.size()) - 1));
	}

	void GalaxyMapScreen::GoTo(const FindHit& h)
	{
		if (h.System != nullptr)
		{
			selected = h.System;
			selectedStar.reset();
		}
		else if (h.Star.has_value()) selectedStar = *h.Star;
		// The map turns about the one found, centred.
		centreLy = h.Ly;
		pan = Vector2::zero();
		findText.clear();
		Closed();
	}

	void GalaxyMapScreen::DrawFind(Rect canvas)
	{
		GUI::SetNextControlName(FindControl);
		findText = GUI::TextField(findRect, findText, 40, field);
		if (focusFind)
		{
			GUI::FocusControl(FindControl);
			focusFind = GUI::GetNameOfFocusedControl() != FindControl;
		}
		finding = GUI::GetNameOfFocusedControl() == FindControl;
		auto old = GUI::contentColor;
		if (findText.empty())
		{
			GUI::contentColor = Dim;
			GUI::Label(Rect(findRect.x + 10, findRect.y + 6, findRect.width - 20, 18),
				finding ? "a system or a star: sol, vega, tau..." : "Ctrl+F to find a star or system", small);
			GUI::contentColor = old;
			return;
		}
		if (GUI::Button(Rect(findRect.xMax() - 26, findRect.y + 3, 22, 22), "X", button))
		{
			findText.clear();
			focusFind = true;
		}
		if (findHits.empty())
		{
			GUI::Box(Rect(findRect.x, findRect.yMax() + 2, findRect.width, 24), GUIContent::none(), Panel());
			GUI::contentColor = Dim;
			GUI::Label(Rect(findRect.x + 10, findRect.yMax() + 5, findRect.width - 20, 18), "no system or charted star by that name", small);
			GUI::contentColor = old;
			return;
		}
		GUI::Box(findListRect, GUIContent::none(), Panel());
		for (int i = 0; i < static_cast<int>(findHits.size()); i++)
		{
			const auto& h = findHits[i];
			auto row = Rect(findListRect.x + 2, findListRect.y + 2 + FindRowH * i, findListRect.width - 4, FindRowH - 2);
			if (GUI::Button(row, cs::Cat(h.Name, "     ", h.Note), i == findPick ? buttonOn : button))
			{
				GoTo(h);
				return;
			}
		}
		(void)canvas;
	}

	void GalaxyMapScreen::DrawFindMarks(Rect local)
	{
		if (findHits.empty()) return;
		float pulse = 0.5f + 0.5f * Mathf::Sin(Time::unscaledTime * 4);
		for (int i = 0; i < static_cast<int>(findHits.size()); i++)
		{
			auto p = Project(local, findHits[i].Ly);
			if (!local.Contains(p)) continue;
			if (i == findPick)
			{
				Brackets(p, 14 + 3 * pulse, Accent);
				Ring(p, 20 + 4 * pulse, Color(Accent.r, Accent.g, Accent.b, 0.5f + 0.4f * pulse), 1.5f);
			}
			else Brackets(p, 11, Color(Accent.r, Accent.g, Accent.b, 0.6f));
		}
	}
}
