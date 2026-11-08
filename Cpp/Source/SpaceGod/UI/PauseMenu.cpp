#include "UI/PauseMenu.h"
#include "UI/UiMotion.h" // US-T255

#include "ActiveProfile.h"
#include "GameSettingsHost.h"
#include "Settings/GameSetting.h"
#include "Settings/SettingsRegistry.h"
#include "Shared/Profiles/ProfileStore.h"
#include "UI/DisplayOptions.h"
#include "UI/Hud.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	void PauseMenu::Open()
	{
		if (IsOpen) return;
		IsOpen = true;
		settingsOnly = false;
		page = Page::Main;
		notice.clear();
		timeScaleBefore = Time::timeScale;
		Time::timeScale = 0;
	}

	void PauseMenu::OpenSettings()
	{
		Open();
		settingsOnly = true;
		page = Page::Settings;
	}

	void PauseMenu::Close()
	{
		if (!IsOpen) return;
		if (confirm != nullptr) Revert();
		IsOpen = false;
		Time::timeScale = timeScaleBefore;
	}

	void PauseMenu::Update(bool escape, float realSeconds)
	{
		if (!IsOpen) return;
		if (confirm != nullptr)
		{
			if (escape || confirm->Tick(realSeconds)) Revert();
			return;
		}
		if (!escape) return;
		Back();
	}

	void PauseMenu::Back()
	{
		switch (page)
		{
			case Page::Display:
				// Leaving the page discards what was picked but not applied.
				page = Page::Settings;
				break;
			case Page::Settings:
				// Esc clears a search before it leaves the page.
				if (!search.empty()) search = "";
				else if (settingsOnly) Close();
				else page = Page::Main;
				break;
			default:
				Close();
				break;
		}
	}

	void PauseMenu::OpenDisplay()
	{
		page = Page::Display;
		allResolutions = DisplayOptions::Resolutions();
		pending = DisplayOptions::Current;
		notice.clear();
		// Open on the screen's own aspect, so a widescreen or ultrawide player starts among their own sizes.
		PickAspect(DisplaySettings::AspectOf(pending.Width, pending.Height));
	}

	void PauseMenu::PickAspect(const cs::String& a)
	{
		aspect = a;
		resolutions = DisplaySettings::OfAspect(allResolutions, aspect);
		if (resolutions.Count() == 0)
		{
			aspect = "All";
			resolutions = allResolutions;
		}
		int i = DisplaySettings::IndexOfNearest(resolutions, pending.Width, pending.Height);
		resolutionScroll = Vector2(0, Mathf::Max(0, i - 4) * RowH);
	}

	void PauseMenu::ApplyPending()
	{
		auto previous = DisplayOptions::Current;
		DisplayOptions::Apply(pending);
		confirm = cs::New<DisplayConfirmation>(previous, pending);
		notice.clear();
	}

	void PauseMenu::Keep()
	{
		DisplayOptions::Save();
		confirm = nullptr;
		notice = "Display settings kept.";
	}

	void PauseMenu::Revert()
	{
		auto previous = confirm->Previous;
		confirm = nullptr;
		DisplayOptions::Apply(previous);
		pending = previous;
		notice = "Reverted to the previous display settings.";
	}

	void PauseMenu::Quit()
	{
		// The app's own quit path autosaves (UnitySpaceApp.OnApplicationQuit).
		// C#: UnityEditor.EditorApplication.isPlaying = false in the editor; Application.Quit() in a player.
		Application::Quit();
	}

	// ---------------------------------------------------------------- drawing

	void PauseMenu::Draw()
	{
		if (!IsOpen) return;
		EnsureStyles();
		{
			// US-T255: the shade fades in as the menu opens, and each page eases in as it is turned to.
			UiMotion::Scope shade("pause.shade", 0);
			GUI::DrawTexture(Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height)), shadeTexture);
		}
		UiMotion::Scope motion(page == Page::Main ? "pause.main" : page == Page::Settings ? "pause.settings" : "pause.display");
		bool enabled = GUI::enabled;
		GUI::enabled = confirm == nullptr;
		switch (page)
		{
			case Page::Main: DrawMain(); break;
			case Page::Settings: DrawSettings(); break;
			default: DrawDisplay(); break;
		}
		GUI::enabled = enabled;
		if (confirm != nullptr) DrawConfirm();
	}

	Rect PauseMenu::Centred(float w, float h) const
	{
		return Rect((Screen::width - w) * 0.5f, (Screen::height - h) * 0.5f, w, h);
	}

	void PauseMenu::DrawMain()
	{
		const float w = 320, h = 346;
		auto r = Centred(w, h);
		GUI::Box(r, GUIContent::none(), panel);
		GUI::Label(Rect(r.x + 20, r.y + 14, w - 40, 22), "PAUSED", title);
		GUI::Label(Rect(r.x + 20, r.y + 38, w - 40, 18), "Every system stands still  ·  Esc to resume", small);
		float y = r.y + 70;
		if (GUI::Button(Rect(r.x + 20, y, w - 40, 34), "Resume", button)) Close();
		if (GUI::Button(Rect(r.x + 20, y + 44, w - 40, 34), "Help", button))
		{
			Close();
			if (ShowHelp) ShowHelp();
		}
		if (GUI::Button(Rect(r.x + 20, y + 88, w - 40, 34), "Save / Load", button))
		{
			Close();
			if (ShowSaves) ShowSaves();
		}
		if (GUI::Button(Rect(r.x + 20, y + 132, w - 40, 34), "Settings", button)) page = Page::Settings;
		if (GUI::Button(Rect(r.x + 20, y + 176, w - 40, 34), "Main Menu", button))
		{
			Close();
			if (ToMainMenu) ToMainMenu();
		}
		if (GUI::Button(Rect(r.x + 20, y + 220, w - 40, 34), "Quit", button))
		{
			Time::timeScale = timeScaleBefore;
			Quit();
		}
	}

	void PauseMenu::DrawSettings()
	{
		const auto& registry = GameSettingsHost::Registry();
		auto groups = registry->Groups();
		if (group.empty() || !groups.Contains(group)) group = groups.Count() > 0 ? groups[0] : cs::String();

		float w = Mathf::Min(820, Screen::width - 40), h = Mathf::Min(560, Screen::height - 40);
		auto r = Centred(w, h);
		GUI::Box(r, GUIContent::none(), panel);
		GUI::Label(Rect(r.x + 20, r.y + 14, w - 40, 22), "SETTINGS", title);
		GUI::Label(Rect(r.x + 20, r.y + 38, w - 340, 18),
			cs::Cat("Changes take effect at once and are kept in profile ", ActiveProfile::Current()->Name, "  ·  Esc to go back"), small);

		// Search, across every group.
		GUI::Label(Rect(r.xMax() - 320, r.y + 16, 60, 24), "Search", text);
		cs::String typed = GUI::TextField(Rect(r.xMax() - 260, r.y + 14, 240, 26), search, 48, field);
		if (typed != search)
		{
			search = typed;
			settingsScroll = Vector2::zero();
		}
		bool searching = !cs::Strings::IsNullOrWhiteSpace(search);

		// The groups.
		const float navW = 190;
		float top = r.y + 70, bottom = r.yMax() - 56;
		float ny = top;
		if (GUI::Button(Rect(r.x + 20, ny, navW, 30), "Display", button)) OpenDisplay();
		ny += 36;
		for (const auto& g : groups)
		{
			if (ny + 30 > bottom) break;
			if (GUI::Button(Rect(r.x + 20, ny, navW, 30), g, !searching && g == group ? buttonActive : button))
			{
				group = g;
				search = "";
				settingsScroll = Vector2::zero();
			}
			ny += 34;
		}

		// The settings.
		float px = r.x + 20 + navW + 20, pw = r.xMax() - 20 - px;
		auto shown = searching ? registry->Search(search) : !group.empty() ? registry->InGroup(group) : cs::List<cs::Ref<GameSetting>>();
		bool displayFound = searching && MatchesAll(DisplaySearchWords, search);
		int rows = shown.Count() + (displayFound ? 1 : 0);
		cs::String heading = searching ? cs::Cat(rows, " found for \"", cs::Strings::Trim(search), "\"") : !group.empty() ? group : cs::String("Nothing to set");
		GUI::Label(Rect(px, top, pw, 22), heading, text);

		auto view = Rect(px, top + 28, pw, bottom - top - 28);
		auto content = Rect(0, 0, pw - 18, Mathf::Max(rows * SettingRowH, 1.0f));
		settingsScroll = GUI::BeginScrollView(view, settingsScroll, content);
		float y = 0;
		if (displayFound)
		{
			GUI::Label(Rect(4, y + 4, content.width - 280, 20), "Display", label);
			GUI::Label(Rect(4, y + 24, content.width - 280, 30), "Resolution, aspect ratio, window mode and VSync.", small);
			if (GUI::Button(Rect(content.width - 266, y + 10, 200, 28), "Open Display", button)) OpenDisplay();
			y += SettingRowH;
		}
		for (const auto& setting : shown)
		{
			DrawSetting(setting, Rect(0, y, content.width, SettingRowH), searching);
			y += SettingRowH;
		}
		if (rows == 0)
			GUI::Label(Rect(4, 4, content.width - 8, 40), searching ? "No setting matches that." : "No system has declared a setting yet.", small);
		GUI::EndScrollView();

		// Reset the group shown (or everything found), and Back.
		float by = r.yMax() - 46;
		cs::List<cs::Ref<GameSetting>> resettable;
		for (const auto& s : shown)
			if (!s->IsDefault()) resettable.Add(s);
		bool enabled = GUI::enabled;
		GUI::enabled = enabled && resettable.Count() > 0;
		cs::String resetLabel = searching ? cs::String("Reset these") : cs::Cat("Reset ", group);
		if (GUI::Button(Rect(px, by, 220, 30), resetLabel, button))
			for (const auto& s : resettable) s->Reset();
		GUI::enabled = enabled;
		if (GUI::Button(Rect(r.xMax() - 20 - 110, by, 110, 30), "Back", button))
		{
			search = ""; // the button leaves the page outright; only Esc stops at clearing a search first
			Back();
		}
	}

	void PauseMenu::DrawSetting(const cs::Ref<GameSetting>& setting, Rect r, bool showGroup)
	{
		const float controlW = 250, resetW = 60;
		float textW = r.width - controlW - resetW - 24;
		GUI::Label(Rect(r.x + 4, r.y + 4, textW, 20), showGroup ? cs::Cat(setting->Group, "  ·  ", setting->Label) : setting->Label, label);
		GUI::Label(Rect(r.x + 4, r.y + 24, textW, SettingRowH - 26), setting->Description, small);

		float cx = r.x + textW + 12, cy = r.y + 10;
		if (auto toggle = cs::As<ToggleSetting>(setting))
		{
			float half = (controlW - 6) * 0.5f;
			if (GUI::Button(Rect(cx, cy, half, 28), "On", toggle->Value ? buttonActive : button)) toggle->Set(true);
			if (GUI::Button(Rect(cx + half + 6, cy, half, 28), "Off", !toggle->Value ? buttonActive : button)) toggle->Set(false);
		}
		else if (auto range = cs::As<RangeSetting>(setting))
		{
			const float valueW = 86;
			float f = GUI::HorizontalSlider(Rect(cx, cy + 8, controlW - valueW - 8, 16), static_cast<float>(range->Fraction()), 0.0f, 1.0f);
			if (!Mathf::Approximately(f, static_cast<float>(range->Fraction()))) range->SetFraction(f);
			GUI::Label(Rect(cx + controlW - valueW, cy + 2, valueW, 24), range->ValueText(), text);
		}
		else if (auto choice = cs::As<ChoiceSetting>(setting))
		{
			if (GUI::Button(Rect(cx, cy, 32, 28), "<", button)) choice->Cycle(-1);
			GUI::Label(Rect(cx + 36, cy, controlW - 72, 28), choice->Selected(), rowSelected);
			if (GUI::Button(Rect(cx + controlW - 32, cy, 32, 28), ">", button)) choice->Cycle(1);
		}
		else
		{
			GUI::Label(Rect(cx, cy + 4, controlW, 24), setting->ValueText(), text);
		}

		bool enabled = GUI::enabled;
		GUI::enabled = enabled && !setting->IsDefault();
		if (GUI::Button(Rect(r.xMax() - resetW, cy, resetW, 28), "Reset", button)) setting->Reset();
		GUI::enabled = enabled;
	}

	bool PauseMenu::MatchesAll(const cs::String& words, const cs::String& query)
	{
		// C#: Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries)
		for (const auto& q : cs::Strings::Split(cs::Strings::ToLowerInvariant(query), " \t"))
		{
			if (q.empty()) continue;
			if (cs::Strings::IndexOf(words, q) < 0) return false;
		}
		return true;
	}

	void PauseMenu::DrawDisplay()
	{
		const float w = 600, h = 456;
		auto r = Centred(w, h);
		GUI::Box(r, GUIContent::none(), panel);
		GUI::Label(Rect(r.x + 20, r.y + 14, w - 40, 22), "DISPLAY", title);
		GUI::Label(Rect(r.x + 20, r.y + 38, w - 40, 18),
			cs::Cat("Nothing changes until Apply  ·  now: ", DisplayOptions::Current.ToString()), small);

		// Aspect ratio first (U.SPC-T284): it narrows the resolution list below to widescreen, ultrawide and so on.
		GUI::Label(Rect(r.x + 20, r.y + 64, 120, 20), "Aspect ratio", text);
		cs::List<cs::String> aspects;
		aspects.Add("All");
		for (const auto& a : DisplaySettings::Aspects())
			if (DisplaySettings::OfAspect(allResolutions, a).Count() > 0) aspects.Add(a);
		float aw = (w - 40 - (aspects.Count() - 1) * 6) / aspects.Count();
		for (int i = 0; i < aspects.Count(); i++)
			if (GUI::Button(Rect(r.x + 20 + i * (aw + 6), r.y + 88, aw, 28), aspects[i], aspect == aspects[i] ? buttonActive : button))
				PickAspect(aspects[i]);

		// Resolution: one row per size, the one picked highlighted.
		const float listW = 270, listH = 260;
		float top = r.y + 126;
		GUI::Label(Rect(r.x + 20, top, listW, 20), "Resolution", text);
		auto view = Rect(r.x + 20, top + 24, listW, listH);
		GUI::Box(view, GUIContent::none(), panel);
		auto content = Rect(0, 0, listW - 18, resolutions.Count() * RowH);
		resolutionScroll = GUI::BeginScrollView(view, resolutionScroll, content);
		for (int i = 0; i < resolutions.Count(); i++)
		{
			auto res = resolutions[i];
			bool picked = res.SameSize(pending.Width, pending.Height);
			cs::String rowLabel = cs::Cat(res.ToString(), (aspect == "All" ? cs::Cat("   ", DisplaySettings::AspectOf(res.Width, res.Height)) : cs::String()));
			if (GUI::Button(Rect(0, i * RowH, content.width, RowH - 2), rowLabel, picked ? rowSelected : row))
				pending = pending.WithSize(res.Width, res.Height);
		}
		GUI::EndScrollView();

		// Window mode and VSync.
		float x = r.x + 20 + listW + 24, cw = r.xMax() - 20 - x;
		GUI::Label(Rect(x, top, cw, 20), "Mode", text);
		const DisplayMode modes[] = { DisplayMode::Windowed, DisplayMode::Borderless, DisplayMode::Exclusive };
		const int modeCount = 3;
		for (int i = 0; i < modeCount; i++)
			if (GUI::Button(Rect(x, top + 24 + i * 34, cw, 28), DisplaySettings::ModeName(modes[i]), pending.Mode == modes[i] ? buttonActive : button))
				pending = pending.WithMode(modes[i]);
		float vy = top + 24 + modeCount * 34 + 16;
		GUI::Label(Rect(x, vy, cw, 20), "VSync", text);
		float half = (cw - 8) * 0.5f;
		if (GUI::Button(Rect(x, vy + 24, half, 28), "On", pending.VSync ? buttonActive : button)) pending = pending.WithVSync(true);
		if (GUI::Button(Rect(x + half + 8, vy + 24, half, 28), "Off", !pending.VSync ? buttonActive : button)) pending = pending.WithVSync(false);

		if (!notice.empty()) GUI::Label(Rect(x, vy + 66, cw, 40), notice, small);
		// A size the screen does not list itself can be a window or a borderless window, not a mode to switch the screen to.
		else if (pending.Mode == DisplayMode::Exclusive && !allResolutions.Exists([this](const DisplayResolution& m) { return m.SameSize(pending.Width, pending.Height) && m.RefreshHz > 0; }))
			GUI::Label(Rect(x, vy + 66, cw, 40), "The screen does not offer this size as a mode: borderless windowed will show it", small);

		// Apply only once something differs from what is on the screen; Cancel throws the picks away.
		float by = r.yMax() - 46;
		bool enabled = GUI::enabled;
		GUI::enabled = enabled && pending != DisplayOptions::Current;
		if (GUI::Button(Rect(r.xMax() - 20 - 230, by, 110, 30), "Apply", button)) ApplyPending();
		GUI::enabled = enabled;
		if (GUI::Button(Rect(r.xMax() - 20 - 110, by, 110, 30), "Cancel", button)) page = Page::Settings;
	}

	void PauseMenu::DrawConfirm()
	{
		const float w = 380, h = 132;
		auto r = Centred(w, h);
		GUI::DrawTexture(Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height)), shadeTexture); // over the Display page
		GUI::Box(r, GUIContent::none(), panel);
		GUI::Label(Rect(r.x + 20, r.y + 14, w - 40, 22), "Keep these display settings?", title);
		GUI::Label(Rect(r.x + 20, r.y + 40, w - 40, 22), cs::Cat("Reverting in ", confirm->SecondsShown()), countdown);
		if (GUI::Button(Rect(r.x + 20, r.yMax() - 46, (w - 50) * 0.5f, 30), "Keep", buttonActive)) Keep();
		else if (GUI::Button(Rect(r.x + 30 + (w - 50) * 0.5f, r.yMax() - 46, (w - 50) * 0.5f, 30), "Revert", button)) Revert();
	}

	void PauseMenu::EnsureStyles()
	{
		if (stylesReady) return;
		stylesReady = true;
		// The shared look (US-T199, UiStyle.h) at menu depth: the menu sits over the whole view.
		namespace S = UiStyle;
		namespace T = UiStyle::Type;
		panelTexture = S::PanelFrame(S::Depth::Menu);
		shadeTexture = S::Flat(S::Shade, Color::clear());
		buttonTexture = S::ControlTexture();
		buttonActiveTexture = S::ControlOnTexture();
		rowHoverTexture = S::Flat(S::RowHover, Color::clear());
		rowSelectedTexture = S::Flat(S::RowSelected, Color::clear());

		panel = S::PanelStyle(panelTexture);
		title = S::TitleStyle(T::Title);
		text = S::TextStyle(T::Body);
		text->clipping = TextClipping::Clip;
		small = S::CaptionStyle(T::Caption);
		label = S::TextStyle(T::Body, S::Heading, FontStyle::Bold);
		label->clipping = TextClipping::Clip;
		fieldTexture = S::FieldTexture();
		field = S::FieldStyle(T::Body, fieldTexture);
		countdown = S::TextStyle(T::Heading, S::Accent, FontStyle::Bold);
		button = S::ButtonStyle(T::Body, buttonTexture, buttonActiveTexture);
		buttonActive = S::ButtonOnStyle(button, buttonActiveTexture);
		row = S::RowStyle(T::Label, rowHoverTexture);
		rowSelected = S::RowSelectedStyle(row, rowSelectedTexture);
	}

	void PauseMenu::DestroyOwnedTextures()
	{
		// Object.Destroy(texture): dropping the last reference.
		panelTexture = shadeTexture = buttonTexture = buttonActiveTexture = rowHoverTexture = rowSelectedTexture = fieldTexture = nullptr;
	}
}
