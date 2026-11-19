// US-T310: the system map's find. A field over the map's top left (Ctrl+F, or a click) lists the worlds, stations, gates,
// ships and fields whose name holds what is typed, the matches bracketed on the map; Up / Down / Enter or a click picks one,
// which selects it (its card on the right, as a click on the map does) and centres the map on it. Declarations in
// SystemMapScreen.h; the galaxy map's own find (US-T306) works the same way.

#include "UI/SystemMapScreen.h"

#include "UnitySpaceApp.h"
#include "UI/UiStyle.h"

#include <algorithm>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	void SystemMapScreen::Closed()
	{
		if (finding) GUIUtility::keyboardControl = 0;
		finding = false;
		focusFind = false;
	}

	DVec3 SystemMapScreen::YouKm() const
	{
		for (const auto& it : items)
			if (it.Kind == ItemKind::You) return it.Km;
		return app->PlayerHelioKm();
	}

	void SystemMapScreen::FindKeys()
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

	void SystemMapScreen::RefreshFind()
	{
		// Again when the words change or the items were rebuilt (ships come and go as the sensors see them).
		if (findFor == findText && findBuiltAt == builtAt) return;
		if (findFor != findText) findPick = 0;
		findFor = findText;
		findBuiltAt = builtAt;
		findHits.clear();
		cs::String q = cs::Strings::Trim(cs::Strings::ToLowerInvariant(findText));
		if (q.empty()) return;

		struct Ranked
		{
			cs::String Key;
			int Rank;
			double Km;
		};
		std::vector<Ranked> all;
		auto you = YouKm();
		for (const auto& it : items)
		{
			if (it.Kind == ItemKind::You || it.Name.empty()) continue;
			cs::String lower = cs::Strings::ToLowerInvariant(it.Name);
			if (!cs::Strings::Contains(lower, q)) continue;
			all.push_back({ it.Key, cs::Strings::StartsWith(lower, q) ? 0 : 1, (it.Km - you).Length() });
		}
		// A name that starts with it first, then the nearest.
		std::stable_sort(all.begin(), all.end(), [](const Ranked& a, const Ranked& b)
		{
			return a.Rank != b.Rank ? a.Rank < b.Rank : a.Km < b.Km;
		});
		for (int i = 0; i < static_cast<int>(all.size()) && i < MaxFindHits; i++) findHits.push_back(all[i].Key);
		findPick = std::clamp(findPick, 0, std::max(0, static_cast<int>(findHits.size()) - 1));
	}

	void SystemMapScreen::GoTo(const cs::String& key)
	{
		const Item* it = Find(key);
		findText.clear();
		Closed();
		if (it == nullptr) return;
		selectedKey = it->Key;
		if (!it->What.IsNone()) app->Select(it->What);
		// The map centred on it, close enough to tell it from what it orbits (an area: its whole reach in view).
		centreKm = it->Km;
		if (it->ReachKm > 0) kmPerPx = cs::Math::Max(0.5, it->ReachKm * 2.6 / 600.0);
		else if (it->HasParent) kmPerPx = cs::Math::Min(kmPerPx, cs::Math::Max(0.5, (it->Km - it->ParentKm).Length() / 60.0));
	}

	void SystemMapScreen::DrawFind()
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
			GUI::contentColor = UiStyle::Dim;
			GUI::Label(Rect(findRect.x + 10, findRect.y + 6, findRect.width - 20, 18),
				finding ? "a world, station, gate or ship: earth, ceres..." : "Ctrl+F to find a world, station or ship", small);
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
			GUI::Box(Rect(findRect.x, findRect.yMax() + 2, findRect.width, 24), GUIContent::none(), panel);
			GUI::contentColor = UiStyle::Dim;
			GUI::Label(Rect(findRect.x + 10, findRect.yMax() + 5, findRect.width - 20, 18), "nothing here by that name", small);
			GUI::contentColor = old;
			return;
		}
		GUI::Box(findListRect, GUIContent::none(), panel);
		for (int i = 0; i < static_cast<int>(findHits.size()); i++)
		{
			const Item* it = Find(findHits[i]);
			if (it == nullptr) continue;
			auto row = Rect(findListRect.x + 2, findListRect.y + 2 + FindRowH * i, findListRect.width - 4, FindRowH - 2);
			if (GUI::Button(row, cs::Cat(it->Name, "     ", it->KindLabel, "  \xC2\xB7  ", RangeOf(*it)), i == findPick ? buttonOn : button))
			{
				GoTo(findHits[i]);
				return;
			}
		}
	}

	void SystemMapScreen::DrawFindMarks(Rect local)
	{
		if (findHits.empty()) return;
		float pulse = 0.5f + 0.5f * Mathf::Sin(Time::unscaledTime * 4);
		for (int i = 0; i < static_cast<int>(findHits.size()); i++)
		{
			const Item* it = Find(findHits[i]);
			if (it == nullptr) continue;
			auto p = Project(local, it->Km);
			if (!local.Contains(p)) continue;
			bool picked = i == findPick;
			float s = picked ? 12 + 3 * pulse : 9;
			Color c = picked ? UiStyle::Accent : Color(UiStyle::Accent.r, UiStyle::Accent.g, UiStyle::Accent.b, 0.6f);
			// Corner brackets about it.
			constexpr float arm = 5;
			for (float sx : { -1.0f, 1.0f })
				for (float sy : { -1.0f, 1.0f })
				{
					auto corner = p + Vector2(sx * s, sy * s);
					Line(corner, corner + Vector2(-sx * arm, 0), c, 1.5f);
					Line(corner, corner + Vector2(0, -sy * arm), c, 1.5f);
				}
			if (picked) Circle(local, p, s + 6, Color(c.r, c.g, c.b, 0.4f + 0.4f * pulse), 1.2f);
		}
	}
}
