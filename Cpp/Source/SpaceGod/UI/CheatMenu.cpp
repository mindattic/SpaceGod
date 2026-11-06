// US-T126: the dev menu (see UI/CheatMenu.h), its registry, and the sg.CheatMenu / sg.Cheat console commands.

#include "UI/CheatMenu.h"

#include "GameLaunch.h"
#include "Camera/StarTravel.h"
#include "Input/InputDevice.h"
#include "SpaceGodLog.h"
#include "UI/Hud.h"
#include "UI/UiStyle.h"
#include "UnitySpaceApp.h"
#include "UnitySpaceAppActor.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace SpaceGod::UI
{
	using Input::InputDevice;
	using Input::Key;

	namespace
	{
		const cs::String FilterControl = "sg-cheat-filter";

		UWorld* GameWorld()
		{
			if (!GEngine) return nullptr;
			for (const FWorldContext& c : GEngine->GetWorldContexts())
				if (c.WorldType == EWorldType::Game || c.WorldType == EWorldType::PIE) return c.World();
			return nullptr;
		}

		UnitySpaceApp* RunningApp()
		{
			if (!GEngine) return nullptr;
			for (const FWorldContext& c : GEngine->GetWorldContexts())
			{
				if (c.WorldType != EWorldType::Game && c.WorldType != EWorldType::PIE) continue;
				auto* actor = AUnitySpaceAppActor::Find(c.World());
				if (actor != nullptr && actor->GetApp() != nullptr) return actor->GetApp();
			}
			return nullptr;
		}

		cs::String Lower(const cs::String& s) { return cs::Strings::ToLowerInvariant(s); }

		/// Every word typed is somewhere in the line (its label, what it does, where it lives).
		bool Matches(const cs::String& haystack, const cs::String& filter)
		{
			cs::String hay = Lower(haystack);
			cs::String word;
			auto test = [&]() { return word.empty() || hay.find(word) != cs::String::npos; };
			for (char c : Lower(filter))
			{
				if (c == ' ')
				{
					if (!test()) return false;
					word.clear();
				}
				else word += c;
			}
			return test();
		}

		bool StartsWithIgnoreCase(const cs::String& s, const cs::String& prefix)
		{
			return Lower(s).rfind(Lower(prefix), 0) == 0;
		}

		/// The first token of a console line: the command's name.
		cs::String CommandName(const cs::String& line)
		{
			auto space = line.find(' ');
			return space == cs::String::npos ? line : line.substr(0, space);
		}
	}

	// ---------------------------------------------------------------- entries and the registry

	CheatEntry CheatEntry::Action(const cs::String& label, const cs::String& detail, std::function<void(UnitySpaceApp&)> run)
	{
		CheatEntry e;
		e.Label = label;
		e.Detail = detail;
		e.Run = std::move(run);
		return e;
	}

	CheatEntry CheatEntry::Menu(const cs::String& label, const cs::String& detail, std::function<std::vector<CheatEntry>(UnitySpaceApp&)> children)
	{
		CheatEntry e;
		e.Label = label;
		e.Detail = detail;
		e.Children = std::move(children);
		return e;
	}

	CheatEntry CheatEntry::Console(const cs::String& label, const cs::String& detail, const cs::String& command)
	{
		CheatEntry e = Action(label, detail, [command](UnitySpaceApp&)
		{
			if (GEngine) GEngine->Exec(GameWorld(), UTF8_TO_TCHAR(command.c_str()));
		});
		e.Keys = command;
		cs::String name = CommandName(command);
		e.Blocker = [name](UnitySpaceApp&)
		{
			return IConsoleManager::Get().FindConsoleObject(UTF8_TO_TCHAR(name.c_str())) != nullptr ? cs::String() : cs::Cat("not in this build yet (", name, ")");
		};
		return e;
	}

	std::vector<CheatRegistry::Category>& CheatRegistry::Mutable()
	{
		static std::vector<Category> categories;
		return categories;
	}

	CheatRegistry::Category& CheatRegistry::Find(const cs::String& name)
	{
		auto& all = Mutable();
		for (auto& c : all)
			if (c.Name == name) return c;
		Category c;
		c.Name = name;
		all.push_back(c);
		return all.back();
	}

	void CheatRegistry::DeclareCategory(const cs::String& name, int order, const cs::String& blurb)
	{
		auto& c = Find(name);
		c.Order = order;
		c.Blurb = blurb;
		std::stable_sort(Mutable().begin(), Mutable().end(), [](const Category& a, const Category& b) { return a.Order < b.Order; });
	}

	void CheatRegistry::Add(const cs::String& category, CheatEntry entry)
	{
		auto& entries = Find(category).Entries;
		auto at = std::upper_bound(entries.begin(), entries.end(), entry.Order, [](int order, const CheatEntry& e) { return order < e.Order; });
		entries.insert(at, std::move(entry));
	}

	std::vector<void (*)()>& CheatRegistry::Pending()
	{
		static std::vector<void (*)()> pending;
		return pending;
	}

	void CheatRegistry::Defer(void (*registerEntries)()) { Pending().push_back(registerEntries); }

	const std::vector<CheatRegistry::Category>& CheatRegistry::Categories()
	{
		while (!Pending().empty())
		{
			auto next = Pending().front();
			Pending().erase(Pending().begin());
			next();
		}
		return Mutable();
	}

	// ---------------------------------------------------------------- the menu

	void CheatMenu::Open()
	{
		IsOpen = true;
		stack.clear();
		filter.clear();
		focusFilter = true;
		clickedRow = clickedCategory = -1;
		clickedBack = false;
	}

	void CheatMenu::Close()
	{
		IsOpen = false;
		focusFilter = false;
	}

	bool CheatMenu::ShowCategory(const cs::String& name)
	{
		const auto& cats = CheatRegistry::Categories();
		for (int i = 0; i < static_cast<int>(cats.size()); i++)
			if (StartsWithIgnoreCase(cats[i].Name, name))
			{
				category = i;
				stack.clear();
				filter.clear();
				selected = scroll = 0;
				return true;
			}
		return false;
	}

	void CheatMenu::Back()
	{
		if (!stack.empty()) stack.pop_back();
		selected = scroll = 0;
	}

	void CheatMenu::Clamp()
	{
		int n = static_cast<int>(rows.size());
		selected = n == 0 ? 0 : std::max(0, std::min(selected, n - 1));
		if (selected < scroll) scroll = selected;
		if (selected >= scroll + visibleRows) scroll = selected - visibleRows + 1;
		scroll = std::max(0, std::min(scroll, std::max(0, n - visibleRows)));
	}

	void CheatMenu::Rebuild(UnitySpaceApp& app)
	{
		rows.clear();
		const auto& cats = CheatRegistry::Categories();
		if (cats.empty()) return;
		category = std::max(0, std::min(category, static_cast<int>(cats.size()) - 1));
		auto add = [&](const CheatEntry& e, const cs::String& where)
		{
			Row r;
			r.Entry = e;
			r.Where = where;
			if (e.Blocker) r.Blocker = e.Blocker(app);
			if (e.State) r.State = e.State(app);
			rows.push_back(std::move(r));
		};
		if (!filter.empty())
		{
			// Everything, a submenu's lines too: "mars" finds the cruise to Mars and the teleport there.
			for (const auto& c : cats)
				for (const auto& e : c.Entries)
				{
					if (Matches(cs::Cat(e.Label, " ", e.Detail, " ", c.Name), filter)) add(e, c.Name);
					if (!e.IsMenu()) continue;
					cs::String where = cs::Cat(c.Name, "  >  ", e.Label);
					for (const auto& child : e.Children(app))
						if (Matches(cs::Cat(child.Label, " ", child.Detail, " ", where), filter)) add(child, where);
				}
			return;
		}
		const auto& entries = !stack.empty() ? stack.back().Entries : cats[category].Entries;
		for (const auto& e : entries) add(e, cs::String());
	}

	void CheatMenu::Activate(UnitySpaceApp& app, int index)
	{
		if (index < 0 || index >= static_cast<int>(rows.size())) return;
		Row r = rows[index];
		if (!r.Blocker.empty())
		{
			app.ShowToast(cs::Cat(r.Entry.Label, ": ", r.Blocker));
			return;
		}
		if (r.Entry.IsMenu())
		{
			stack.push_back(Level{ r.Entry.Label, r.Entry.Children(app) });
			filter.clear();
			selected = scroll = 0;
			return;
		}
		if (!r.Entry.Run) return;
		if (!r.Entry.KeepOpen) Close();
		r.Entry.Run(app);
	}

	void CheatMenu::Update(UnitySpaceApp& app, float realSeconds)
	{
		slide = IsOpen ? std::min(1.0f, slide + realSeconds / 0.14f) : std::max(0.0f, slide - realSeconds / 0.12f);
		if (!IsOpen) return;
		const auto& cats = CheatRegistry::Categories();
		int n = static_cast<int>(cats.size());
		if (n == 0) return;

		// What was clicked in the last GUI pass, against the rows it drew.
		if (clickedCategory >= 0)
		{
			category = clickedCategory;
			stack.clear();
			filter.clear();
			selected = scroll = 0;
		}
		if (clickedBack) Back();
		int run = clickedRow;
		clickedRow = clickedCategory = -1;
		clickedBack = false;
		if (run >= 0)
		{
			Activate(app, run);
			if (!IsOpen) return;
		}

		const InputDevice& kb = InputDevice::current();
		auto pressed = [&](Key k) { return kb[k].wasPressedThisFrame; };
		bool shift = kb[Key::LeftShift].isPressed || kb[Key::RightShift].isPressed;
		if (pressed(Key::Escape))
		{
			if (!filter.empty()) filter.clear();
			else if (!stack.empty()) Back();
			else
			{
				Close();
				return;
			}
		}
		if (pressed(Key::Tab))
		{
			category = (category + (shift ? n - 1 : 1)) % n;
			stack.clear();
			filter.clear();
			selected = scroll = 0;
		}
		Rebuild(app);
		Clamp();
		bool typing = !filter.empty();
		if (!typing && (pressed(Key::LeftArrow) || pressed(Key::Backspace)))
		{
			if (!stack.empty()) Back();
			else if (pressed(Key::LeftArrow))
			{
				category = (category + n - 1) % n;
				selected = scroll = 0;
			}
			Rebuild(app);
			Clamp();
		}
		int count = static_cast<int>(rows.size());
		if (pressed(Key::UpArrow)) selected = count == 0 ? 0 : (selected + count - 1) % count;
		if (pressed(Key::DownArrow)) selected = count == 0 ? 0 : (selected + 1) % count;
		if (pressed(Key::PageUp)) selected -= visibleRows;
		if (pressed(Key::PageDown)) selected += visibleRows;
		if (pressed(Key::Home)) selected = 0;
		if (pressed(Key::End)) selected = count - 1;
		if (kb.scrollY != 0)
		{
			float notches = std::abs(kb.scrollY) < 2.0f ? kb.scrollY : kb.scrollY / 120.0f;
			selected -= static_cast<int>(notches > 0 ? std::ceil(notches) : std::floor(notches)) * 3;
		}
		Clamp();
		bool open = !typing && pressed(Key::RightArrow);
		if (open && (count == 0 || !rows[selected].Entry.IsMenu()))
		{
			// Right on an action steps to the next category, as Left steps back.
			if (stack.empty())
			{
				category = (category + 1) % n;
				selected = scroll = 0;
			}
			open = false;
		}
		if (open || pressed(Key::Enter) || pressed(Key::NumpadEnter)) Activate(app, selected);
		if (!IsOpen) return;
		Rebuild(app);
		Clamp();
	}

	// ---------------------------------------------------------------- drawing

	void CheatMenu::Draw(UnitySpaceApp& app)
	{
		EnsureStyles();
		if (slide <= 0)
		{
			// The HUD's hint for the key, out of the way at the bottom right (gone with the HUD's panels and a cleared screen).
			if (!app.ShowHud || app.ScreenCleared) return;
			// US-T208: a full-screen screen (spawn, manual, galaxy map) owns the corner: the hint would sit on its buttons and borders.
			if (app.ShowSpawn || app.ShowHelp || (app.ShowGalaxyMap && !app.Travel->Active)) return;
			auto r = Rect(Screen::width - 196.0f, Screen::height - 24.0f, 186, 18);
			if (!app.UiRects.Contains(r)) app.UiRects.Add(r); // US-T212
			GUI::Box(r, GUIContent::none(), hint);
			GUI::Label(r, "Shift+F1  DEV MENU", hint);
			return;
		}
		float ease = 1 - (1 - slide) * (1 - slide) * (1 - slide);
		float w = std::min(static_cast<float>(Screen::width) - 32, 1180.0f);
		float h = std::min(static_cast<float>(Screen::height) - 40, std::max(360.0f, Screen::height * 0.62f));
		float x = (Screen::width - w) * 0.5f;
		float y = -h + h * ease;
		auto box = Rect(x, y, w, h);
		GUI::Box(box, GUIContent::none(), panel);

		const auto& cats = CheatRegistry::Categories();
		GUI::Label(Rect(x + 18, y + 12, 300, 24), "DEV MENU", title);
		GUI::Label(Rect(x + 130, y + 16, w - 480, 18), "every dev and debug function in one place  ·  Shift+F1 or Esc to close", small);

		// The filter, focused from the moment the menu opens: typing searches every entry.
		auto fieldRect = Rect(x + w - 318, y + 12, 300, 26);
		GUI::SetNextControlName(FilterControl);
		filter = GUI::TextField(fieldRect, filter, 40, field);
		if (focusFilter && IsOpen)
		{
			GUI::FocusControl(FilterControl);
			focusFilter = GUI::GetNameOfFocusedControl() != FilterControl;
		}
		if (filter.empty() && GUI::GetNameOfFocusedControl() == FilterControl)
			GUI::Label(Rect(fieldRect.x + 10, fieldRect.y + 5, 260, 18), "type to filter (try: pirate, mars, warp)", small);

		// The categories down the left.
		float top = y + 50;
		float colW = 200;
		for (int i = 0; i < static_cast<int>(cats.size()); i++)
		{
			auto r = Rect(x + 12, top + i * 32, colW, 28);
			bool on = i == category && filter.empty();
			if (GUI::Button(r, cats[i].Name, on ? tabSelected : tab)) clickedCategory = i;
		}

		// The list, with where it is above it.
		float lx = x + colW + 26, lw = w - colW - 40;
		cs::String crumb;
		if (!filter.empty()) crumb = cs::Cat("FOUND ", rows.size(), " FOR \"", filter, "\"");
		else if (!cats.empty())
		{
			crumb = cs::Strings::ToUpperInvariant(cats[category].Name);
			for (const auto& l : stack) crumb = cs::Cat(crumb, "  >  ", cs::Strings::ToUpperInvariant(l.Title));
		}
		GUI::Label(Rect(lx, top, lw - 120, 20), crumb, text);
		if (!stack.empty() && filter.empty() && GUI::Button(Rect(lx + lw - 110, top - 2, 108, 22), "<  BACK", tab)) clickedBack = true;
		if (filter.empty() && stack.empty() && !cats.empty() && !cats[category].Blurb.empty())
			GUI::Label(Rect(lx + 200, top + 2, lw - 330, 18), cats[category].Blurb, small);

		constexpr float rowH = 28;
		float listTop = top + 26;
		float listBottom = y + h - 34;
		visibleRows = std::max(3, static_cast<int>((listBottom - listTop) / rowH));
		auto mouse = Event::current().mousePosition;
		if (rows.empty()) GUI::Label(Rect(lx + 8, listTop + 6, lw - 16, 20), filter.empty() ? "Nothing here yet." : "Nothing matches.", small);
		for (int i = scroll; i < static_cast<int>(rows.size()) && i < scroll + visibleRows; i++)
		{
			const auto& r = rows[i];
			auto rr = Rect(lx, listTop + (i - scroll) * rowH, lw, rowH - 2);
			bool sel = i == selected;
			if (sel) GUI::Box(rr, GUIContent::none(), rowSelected);
			else if (rr.Contains(mouse)) GUI::Box(rr, GUIContent::none(), row);
			bool blockedRow = !r.Blocker.empty();
			GUI::contentColor = blockedRow ? Color(1, 1, 1, 0.45f) : Color::white();
			cs::String label = r.Entry.IsMenu() ? cs::Cat(r.Entry.Label, "  >") : r.Entry.Label;
			float labelW = std::min(340.0f, lw * 0.38f);
			GUI::Label(Rect(rr.x + 10, rr.y + 5, labelW - 12, 18), label, sel ? title : text);
			GUI::contentColor = Color::white();
			cs::String detail = blockedRow ? r.Blocker : !r.Where.empty() ? cs::Cat(r.Where, "  ·  ", r.Entry.Detail) : r.Entry.Detail;
			float rightW = 230;
			GUI::Label(Rect(rr.x + labelW, rr.y + 6, rr.width - labelW - rightW - 10, 18), detail, blockedRow ? blocked : small);
			cs::String right = !r.State.empty() ? r.State : r.Entry.Keys;
			if (!right.empty()) GUI::Label(Rect(rr.xMax() - rightW - 8, rr.y + 5, rightW, 18), right, !r.State.empty() ? state : keys);
			if (GUI::Button(rr, GUIContent::none(), GUIStyle::none()))
			{
				selected = i;
				clickedRow = i;
			}
		}
		if (scroll > 0) GUI::Label(Rect(lx + lw - 60, listTop - 18, 60, 16), "more ^", keys);
		if (scroll + visibleRows < static_cast<int>(rows.size())) GUI::Label(Rect(lx + lw - 60, listBottom - 4, 60, 16), "more v", keys);

		GUI::Label(Rect(x + 18, y + h - 26, w - 36, 18),
			"Up / Down pick   ·   Enter or click runs   ·   Right opens   ·   Left / Backspace back   ·   Tab next category   ·   type to filter   ·   the game runs on meanwhile; the craft holds its heading",
			small);
	}

	void CheatMenu::EnsureStyles()
	{
		if (stylesReady) return;
		stylesReady = true;
		// The shared look (US-T199, UiStyle.h) at menu depth, as the pause menu's: it sits over the view.
		namespace S = UiStyle;
		namespace T = UiStyle::Type;
		panelTexture = S::PanelFrame(S::Depth::Menu);
		rowHoverTexture = S::Flat(S::RowHover, Color::clear());
		rowSelectedTexture = S::Flat(S::RowSelected, S::ControlOnEdge);
		tabTexture = S::ControlTexture();
		tabSelectedTexture = S::ControlOnTexture();
		fieldTexture = S::FieldTexture();
		hintTexture = S::Flat(S::InkAt(S::Depth::Faint), S::Edge);

		panel = S::PanelStyle(panelTexture);
		title = S::TitleStyle(T::Title);
		title->clipping = TextClipping::Clip;
		text = S::TextStyle(T::Body);
		text->clipping = TextClipping::Clip;
		small = S::TextStyle(T::Caption, S::Dim);
		small->clipping = TextClipping::Clip;
		blocked = cs::New<GUIStyle>(*small);
		blocked->normal.textColor = S::Warning;
		state = S::TextStyle(T::Label, S::Accent, FontStyle::Bold);
		state->alignment = TextAnchor::UpperRight;
		state->clipping = TextClipping::Clip;
		keys = S::TextStyle(T::Caption, S::Dim);
		keys->alignment = TextAnchor::UpperRight;
		keys->clipping = TextClipping::Clip;
		row = S::PanelStyle(rowHoverTexture, 0);
		rowSelected = S::PanelStyle(rowSelectedTexture, 1);
		tab = S::ButtonStyle(T::Body, tabTexture, tabSelectedTexture);
		tab->alignment = TextAnchor::MiddleLeft;
		tab->padding = RectOffset(static_cast<int>(S::Space::Inset), S::Space::TextPad, 0, 0);
		tabSelected = S::ButtonOnStyle(tab, tabSelectedTexture);
		field = S::FieldStyle(T::Body, fieldTexture);
		hint = S::TextStyle(T::Caption, S::Dim);
		hint->alignment = TextAnchor::MiddleCenter;
		hint->border = RectOffset(1, 1, 1, 1);
		hint->normal.background = hintTexture;
	}

	void CheatMenu::DestroyOwnedTextures()
	{
		panelTexture = rowHoverTexture = rowSelectedTexture = tabTexture = tabSelectedTexture = fieldTexture = hintTexture = nullptr;
		stylesReady = false;
	}

	// ---------------------------------------------------------------- the console

	bool CheatMenu::RunPath(UnitySpaceApp& app, const cs::String& path, cs::String& said)
	{
		std::vector<cs::String> parts;
		cs::String part;
		for (char c : path)
		{
			if (c == '/')
			{
				parts.push_back(part);
				part.clear();
			}
			else part += c;
		}
		parts.push_back(part);
		const CheatRegistry::Category* cat = nullptr;
		for (const auto& c : CheatRegistry::Categories())
			if (StartsWithIgnoreCase(c.Name, parts[0])) { cat = &c; break; }
		if (cat == nullptr)
		{
			said = cs::Cat("no category '", parts[0], "'");
			return false;
		}
		std::vector<CheatEntry> level = cat->Entries;
		for (size_t i = 1; i < parts.size(); i++)
		{
			const CheatEntry* hit = nullptr;
			for (const auto& e : level)
				if (StartsWithIgnoreCase(e.Label, parts[i])) { hit = &e; break; }
			if (hit == nullptr)
			{
				said = cs::Cat("nothing starting '", parts[i], "'");
				return false;
			}
			cs::String blocker = hit->Blocker ? hit->Blocker(app) : cs::String();
			if (!blocker.empty())
			{
				said = cs::Cat(hit->Label, ": ", blocker);
				return false;
			}
			if (hit->IsMenu())
			{
				if (i + 1 == parts.size())
				{
					said = cs::Cat(hit->Label, " is a menu: name one of its lines");
					return false;
				}
				auto next = hit->Children(app);
				level = next;
				continue;
			}
			said = hit->Label;
			if (hit->Run) hit->Run(app);
			return true;
		}
		said = cs::Cat("name an entry in ", cat->Name);
		return false;
	}

	namespace
	{
		/// sg.CheatMenu [category] [shot <file> [quit]]: opens the menu (on a category); with shot, waits for a game (starting a
		/// new one from the main menu), opens it, screenshots it with the UI and optionally quits.
		void StartMenuCommand(const TArray<FString>& args)
		{
			FString cat, shot;
			bool quit = false;
			for (int i = 0; i < args.Num(); i++)
			{
				if (args[i].Equals(TEXT("shot"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) shot = args[++i];
				else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
				else cat = args[i];
			}
			struct State { double started = FPlatformTime::Seconds(); double openedAt = -1; double shotAt = -1; bool askedNewGame = false; };
			auto st = std::make_shared<State>();
			cs::String catName = TCHAR_TO_UTF8(*cat);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([st, catName, shot, quit](float)
			{
				double now = FPlatformTime::Seconds();
				auto* app = RunningApp();
				if (app == nullptr)
				{
					if (!st->askedNewGame && now - st->started > 2.0)
					{
						st->askedNewGame = true;
						UE_LOG(LogSpaceGod, Display, TEXT("sg.CheatMenu: no game running; starting a new one"));
						GameLaunch::NewGame();
					}
					return now - st->started < 180.0;
				}
				if (st->openedAt < 0)
				{
					// A few seconds for the scene to lay itself out, when it has only just come up.
					if (!shot.IsEmpty() && now - st->started < 8.0) return true;
					auto& menu = *app->CheatMenuUi();
					if (!menu.IsOpen) menu.Open();
					if (!catName.empty()) menu.ShowCategory(catName);
					st->openedAt = now;
					UE_LOG(LogSpaceGod, Display, TEXT("sg.CheatMenu: open"));
					return !shot.IsEmpty();
				}
				if (st->shotAt < 0)
				{
					if (now - st->openedAt < 1.5) return true;
					FScreenshotRequest::RequestScreenshot(shot, true, false);
					st->shotAt = now;
					UE_LOG(LogSpaceGod, Display, TEXT("sg.CheatMenu: screenshot %s"), *shot);
					return true;
				}
				if (now - st->shotAt < 1.5) return true;
				if (quit) FPlatformMisc::RequestExit(false);
				return false;
			}));
		}

		FAutoConsoleCommand GCheatMenuCommand(
			TEXT("sg.CheatMenu"),
			TEXT("US-T126: open the dev menu (Shift+F1): sg.CheatMenu [category]; sg.CheatMenu [category] shot <file> [quit] screenshots it."),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { StartMenuCommand(args); }));

		FAutoConsoleCommand GCheatCommand(
			TEXT("sg.Cheat"),
			TEXT("US-T126: run a dev menu entry by its path, each step a label's start: sg.Cheat \"Enemies/Pirate raiders, small\", sg.Cheat \"Time/Time warp/1 day\"."),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
			{
				auto* app = RunningApp();
				if (app == nullptr)
				{
					UE_LOG(LogSpaceGod, Warning, TEXT("sg.Cheat: no game running"));
					return;
				}
				FString joined = FString::Join(args, TEXT(" "));
				cs::String said;
				bool ran = CheatMenu::RunPath(*app, TCHAR_TO_UTF8(*joined), said);
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Cheat: %s %s"), ran ? TEXT("ran") : TEXT("did not run:"), UTF8_TO_TCHAR(said.c_str()));
			}));
	}
}
