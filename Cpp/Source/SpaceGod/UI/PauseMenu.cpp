#include "UI/PauseMenu.h"
#include "UI/UiMotion.h" // US-T255

#include "ActiveProfile.h"
#include "GameSettingsHost.h"
#include "Input/Gamepad.h" // US-T437
#include "Input/KeyBindings.h" // US-T249
#include "PlayerOptions.h" // US-T252
#include "Settings/GameSetting.h"
#include "Settings/SettingsRegistry.h"
#include "Shared/Profiles/ProfileStore.h"
#include "UI/CreditsScreen.h" // US-T246
#include "UI/DisplayOptions.h"
#include "UI/Hud.h"
#include "UI/UiInput.h" // US-T311
#include "UnityCompat/GuiPadNav.h" // US-T437

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
		mainPick = -1; // US-T311
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
		leaving = Leave::None;
		captureAction = captureSlot = -1; // US-T249
		captureSwallow = false; // US-T437
		UnityCompat::GuiPadNav::Hold = false;
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
		UpdateSwallow(); // US-T437
		if (captureAction >= 0)
		{
			UpdateCapture(escape); // US-T249: Esc cancels the listening slot, not the page
			UpdateSwallow();
			return;
		}
		if (captureSwallow) return; // US-T437: the input just bound is still held: it does nothing else
		if (!escape)
		{
			if (page == Page::Main) MainKeys(); // US-T311
			else if (page == Page::Settings) SettingsKeys(); // US-T322
			return;
		}
		if (leaving != Leave::None)
		{
			leaving = Leave::None; // US-T252: Esc answers the question with Stay
			return;
		}
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
			case Page::Credits: // US-T246
				page = Page::Main;
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
		UiMotion::Scope motion(page == Page::Main ? "pause.main" : page == Page::Settings ? "pause.settings" : page == Page::Credits ? "pause.credits" : "pause.display");
		bool enabled = GUI::enabled;
		bool asking = leaving != Leave::None && page == Page::Main; // US-T252
		GUI::enabled = confirm == nullptr && !asking;
		switch (page)
		{
			case Page::Main: DrawMain(); break;
			case Page::Settings: DrawSettings(); break;
			case Page::Credits: // US-T246
				if (credits->Draw("Esc to go back")) page = Page::Main;
				break;
			default: DrawDisplay(); break;
		}
		GUI::enabled = enabled;
		if (confirm != nullptr) DrawConfirm();
		else if (asking) DrawLeave();
	}

	Rect PauseMenu::Centred(float w, float h) const
	{
		return Rect((Screen::width - w) * 0.5f, (Screen::height - h) * 0.5f, w, h);
	}

	void PauseMenu::DrawMain()
	{
		// US-T271: one more button, the Encyclopedia; US-T293: and Society; US-T311: and the keys' hint under them.
		static const char* const labels[MainCount] = { "Resume", "Help", "Encyclopedia", "Society", "Save / Load", "Settings", "Credits", "Main Menu", "Quit" };
		const float w = 320, h = 498;
		auto r = Centred(w, h);
		GUI::Box(r, GUIContent::none(), panel);
		GUI::Label(Rect(r.x + 20, r.y + 14, w - 40, 22), "PAUSED", title);
		GUI::Label(Rect(r.x + 20, r.y + 38, w - 40, 18), "Every system stands still  ·  Esc to resume", small);
		const float y = r.y + 70;
		for (int i = 0; i < MainCount; i++)
			if (GUI::Button(Rect(r.x + 20, y + i * 44, w - 40, 34), labels[i], i == mainPick ? buttonActive : button)) PressMain(i, false);
		GUI::Label(Rect(r.x + 20, y + MainCount * 44, w - 40, 18), "Up / Down to pick  ·  Enter to press", small);
	}

	void PauseMenu::PressMain(int i, bool keyed)
	{
		switch (i)
		{
			case 0: Close(); break;
			case 1:
				Close();
				if (ShowHelp) ShowHelp();
				break;
			case 2: // US-T271
				Close();
				if (ShowEncyclopedia) ShowEncyclopedia();
				break;
			case 3: // US-T293
				Close();
				if (ShowSociety) ShowSociety();
				break;
			case 4:
				Close();
				if (ShowSaves) ShowSaves();
				break;
			case 5: page = Page::Settings; break;
			case 6: // US-T246
				if (!credits) credits = cs::New<CreditsScreen>();
				credits->Restart();
				page = Page::Credits;
				break;
			case 7: AskLeave(Leave::MainMenu, keyed); break;
			case 8: AskLeave(Leave::Quit, keyed); break;
			default: break;
		}
	}

	void PauseMenu::AskLeave(Leave what, bool keyed)
	{
		// US-T252: Confirm before quitting (Settings > Gameplay) asks first; a driven run leaves in one click.
		if (!PlayerOptions::ConfirmQuit()->Value || PlayerOptions::ScriptedRun())
		{
			LeaveNow(what);
			return;
		}
		leaving = what;
		leavePick = 1; // US-T311: Stay first, so a second Enter never leaves by accident
		leaveKeyed = keyed;
	}

	void PauseMenu::MainKeys()
	{
		if (!UiInput::present()) return;
		auto pressed = [](KeyCode k) { return UiInput::WasPressedThisFrame(k); };
		bool enter = pressed(KeyCode::Return) || pressed(KeyCode::KeypadEnter) || pressed(KeyCode::Space);
		if (leaving != Leave::None)
		{
			if (pressed(KeyCode::LeftArrow) || pressed(KeyCode::RightArrow) || pressed(KeyCode::A) || pressed(KeyCode::D) || pressed(KeyCode::Tab))
			{
				leavePick = 1 - leavePick;
				leaveKeyed = true;
			}
			else if (enter)
			{
				if (leavePick == 0) LeaveNow(leaving);
				else leaving = Leave::None;
			}
			return;
		}
		int step = pressed(KeyCode::UpArrow) || pressed(KeyCode::W) ? -1 : pressed(KeyCode::DownArrow) || pressed(KeyCode::S) ? 1 : 0;
		if (step != 0) mainPick = mainPick < 0 ? (step > 0 ? 0 : MainCount - 1) : (mainPick + step + MainCount) % MainCount;
		else if (enter && mainPick >= 0) PressMain(mainPick, true);
	}

	void PauseMenu::DrawSettings()
	{
		const auto& registry = GameSettingsHost::Registry();
		auto groups = registry->Groups();
		// US-T252: the player's own groups first, in their order; every other group is design tuning, folded under Advanced.
		cs::List<cs::String> playerGroups, advancedGroups;
		for (const auto& g : PlayerOptions::PlayerGroups())
			if (groups.Contains(g)) playerGroups.Add(g);
		for (const auto& g : groups)
			if (!PlayerOptions::IsPlayerGroup(g)) advancedGroups.Add(g);
		if (group.empty() || !groups.Contains(group))
			group = playerGroups.Count() > 0 ? playerGroups[0] : groups.Count() > 0 ? groups[0] : cs::String();

		// US-T437: the bindings' three slots and their modes want a wider panel (the page, or a search that finds them).
		bool bindingsPage = cs::Strings::IsNullOrWhiteSpace(search) && group == Input::KeyBindings::Group;
		bool wide = bindingsPage || !cs::Strings::IsNullOrWhiteSpace(search);
		float w = Mathf::Min(wide ? 1120.0f : 820.0f, Screen::width - 40.0f), h = Mathf::Min(wide ? 640.0f : 560.0f, Screen::height - 40.0f);
		auto r = Centred(w, h);
		GUI::Box(r, GUIContent::none(), panel);
		GUI::Label(Rect(r.x + 20, r.y + 14, w - 40, 22), "SETTINGS", title);
		GUI::Label(Rect(r.x + 20, r.y + 38, w - 340, 18),
			cs::Cat("Changes take effect at once and are kept in profile ", ActiveProfile::Current()->Name, "  ·  Esc to go back"), small);

		// Search, across every group.
		GUI::Label(Rect(r.xMax() - 320, r.y + 16, 60, 24), "Search", text);
		GUI::SetNextControlName("SettingsSearch"); // US-T322: the keys leave it alone while it is typed in
		cs::String typed = GUI::TextField(Rect(r.xMax() - 260, r.y + 14, 240, 26), search, 48, field);
		if (typed != search)
		{
			search = typed;
			settingsScroll = Vector2::zero();
			settingsPick = -1;
		}
		bool searching = !cs::Strings::IsNullOrWhiteSpace(search);

		// The groups.
		const float navW = 190;
		float top = r.y + 70, bottom = r.yMax() - 56;
		{
			// US-T252: Gameplay, Graphics, Display, Audio, Controls, Accessibility; then "Advanced (simulation)", folded until
			// opened, over the design groups. Scrolls when it runs past the panel (a small window, or a large UI scale).
			const float step = 34, indent = 12;
			bool displayShown = false;
			int rowsNeeded = playerGroups.Count() + 1 + (advancedGroups.Count() > 0 ? 1 : 0) + (advancedOpen ? advancedGroups.Count() : 0);
			auto navView = Rect(r.x + 20, top, navW, bottom - top);
			float navH = rowsNeeded * step;
			bool overflow = navH > navView.height;
			float bw = overflow ? navW - 18 : navW;
			navScroll = GUI::BeginScrollView(navView, navScroll, Rect(0, 0, bw, Mathf::Max(navH, 1.0f)));
			float ny = 0;
			auto displayButton = [&]
			{
				if (GUI::Button(Rect(0, ny, bw, 30), "Display", button)) OpenDisplay();
				ny += step;
				displayShown = true;
			};
			for (const auto& g : playerGroups)
			{
				// US-T249: Key bindings sits under Controls, indented as a page of it.
				float gx = g == Input::KeyBindings::Group ? indent : 0.0f;
				NavGroup(g, Rect(gx, ny, bw - gx, 30), searching);
				ny += step;
				if (g == SettingGroups::Graphics) displayButton();
			}
			if (!displayShown) displayButton();
			if (advancedGroups.Count() > 0)
			{
				bool shownInside = !searching && advancedGroups.Contains(group);
				cs::String head = cs::Cat(advancedOpen ? "-  " : "+  ", "Advanced (simulation)");
				if (GUI::Button(Rect(0, ny, bw, 30), head, shownInside && !advancedOpen ? buttonActive : button))
				{
					advancedOpen = !advancedOpen;
					// Folding it away from under the group shown goes back to the player's first group.
					if (!advancedOpen && advancedGroups.Contains(group) && playerGroups.Count() > 0)
					{
						group = playerGroups[0];
						settingsScroll = Vector2::zero();
					}
				}
				ny += step;
				if (advancedOpen)
					for (const auto& g : advancedGroups)
					{
						NavGroup(g, Rect(indent, ny, bw - indent, 30), searching);
						ny += step;
					}
			}
			GUI::EndScrollView();
		}

		// The settings.
		float px = r.x + 20 + navW + 20, pw = r.xMax() - 20 - px;
		auto shown = searching ? registry->Search(search) : !group.empty() ? registry->InGroup(group) : cs::List<cs::Ref<GameSetting>>();
		bool displayFound = searching && MatchesAll(DisplaySearchWords, search);
		int rows = shown.Count() + (displayFound ? 1 : 0);
		cs::String heading = searching ? cs::Cat(rows, " found for \"", cs::Strings::Trim(search), "\"") : !group.empty() ? group : cs::String("Nothing to set");
		GUI::Label(Rect(px, top, pw, 22), heading, text);
		float listTop = top + 28;
		const float headerH = 26;
		if (bindingsPage) // US-T249: how the page works; US-T437: any input, three slots, a mode each
		{
			GUI::Label(Rect(px + 110, top + 3, pw - 110, 18),
				captureAction >= 0 ? cs::String("Press any key (with Shift, Ctrl or Alt for a chord), mouse button, wheel, pad button, trigger or stick")
					: cs::String("Click a slot, then press any key, mouse button, wheel, pad button, trigger or stick to bind it"), small);
			GUI::Label(Rect(px, top + 22, pw, 18),
				captureAction >= 0 ? cs::String("Esc cancels (or wait)  ·  Clear empties the slot  ·  the left and right mouse buttons bind only with Shift, Ctrl or Alt held")
					: cs::String("The mode under a slot steps Press / Hold / Double-tap / Long press  ·  Esc, the number keys, the left / right mouse buttons and the sticks' moves stay fixed"), small);
			// The slots' column heads, over the rows' slots (DrawKeyBinding lays them out the same way).
			float cw = pw - 18;
			const float gap = 6, resetW = 60;
			float slotW = Mathf::Clamp((cw * 0.62f - resetW - 2 * gap) / Input::KeyBindingSetting::Slots, 96.0f, 168.0f);
			float controlW = slotW * Input::KeyBindingSetting::Slots + gap * (Input::KeyBindingSetting::Slots - 1);
			float cx = px + cw - controlW - resetW - 24 + 12;
			for (int i = 0; i < Input::KeyBindingSetting::Slots; i++)
				GUI::Label(Rect(cx + i * (slotW + gap), top + 42, slotW, 18), cs::Strings::ToUpperInvariant(Input::KeyBindingSetting::SlotName(i)), text);
			listTop = top + 62;

			// US-T437: every action by its group (its name's prefix), groups in the order first met, actions in the page's.
			cs::List<cs::String> order;
			for (const auto& s : shown)
			{
				cs::String g = Input::KeyBindings::GroupOf(Input::KeyBindings::ActionOf(*s));
				if (!order.Contains(g)) order.Add(g);
			}
			cs::List<cs::Ref<GameSetting>> grouped;
			for (const auto& g : order)
				for (const auto& s : shown)
					if (Input::KeyBindings::GroupOf(Input::KeyBindings::ActionOf(*s)) == g) grouped.Add(s);
			shown = grouped;
		}

		auto view = Rect(px, listTop, pw, bottom - listTop);
		// US-T437: each row's top (a group's header above its first action on the bindings page).
		shownRowTops.Clear();
		cs::List<std::pair<cs::String, float>> headers;
		float contentH = displayFound ? SettingRowH : 0.0f;
		{
			cs::String last;
			for (int i = 0; i < shown.Count(); i++)
			{
				if (bindingsPage)
				{
					cs::String g = Input::KeyBindings::GroupOf(Input::KeyBindings::ActionOf(*shown[i]));
					if (g != last)
					{
						headers.Add(std::make_pair(g, contentH));
						contentH += headerH;
						last = g;
					}
				}
				shownRowTops.Add(contentH);
				contentH += SettingRowH;
			}
		}
		if (!scrollToGroup.empty())
		{
			for (const auto& hd : headers)
				if (hd.first == scrollToGroup) settingsScroll.y = hd.second;
			for (int i = 0; i < shown.Count(); i++) // or an action by its name, two rows above it
				if (bindingsPage && Input::KeyBindings::ShortName(Input::KeyBindings::ActionOf(*shown[i])) == scrollToGroup)
					settingsScroll.y = Mathf::Max(0.0f, shownRowTops[i] - 2 * SettingRowH);
			scrollToGroup.clear();
		}
		auto content = Rect(0, 0, pw - 18, Mathf::Max(contentH, 1.0f));
		// US-T322: what the keys act on next frame.
		shownSettings = shown;
		displayRowShown = displayFound;
		settingsViewH = view.height;
		navGroups = playerGroups;
		if (advancedOpen) navGroups.AddRange(advancedGroups);
		if (settingsPick >= shown.Count()) settingsPick = shown.Count() - 1;
		settingsScroll = GUI::BeginScrollView(view, settingsScroll, content);
		float y = 0;
		if (displayFound)
		{
			GUI::Label(Rect(4, y + 4, content.width - 280, 20), "Display", label);
			GUI::Label(Rect(4, y + 24, content.width - 280, 30), "Resolution, aspect ratio, window mode and VSync.", small);
			if (GUI::Button(Rect(content.width - 266, y + 10, 200, 28), "Open Display", button)) OpenDisplay();
			y += SettingRowH;
		}
		for (const auto& hd : headers) // US-T437: the bindings page's group heads
		{
			GUI::Label(Rect(4, hd.second + 4, content.width - 8, headerH - 6), cs::Strings::ToUpperInvariant(hd.first), text);
			GUI::DrawTexture(Rect(4, hd.second + headerH - 3, content.width - 8, 1), buttonActiveTexture);
		}
		for (int i = 0; i < shown.Count(); i++)
		{
			y = shownRowTops[i];
			bool isBinding = cs::As<Input::KeyBindingSetting>(shown[i]) != nullptr;
			if (i == settingsPick) GUI::DrawTexture(Rect(0, y, content.width, SettingRowH - 2), rowSelectedTexture); // US-T322
			if (isBinding) DrawKeyBinding(shown[i], Rect(0, y, content.width, SettingRowH), searching, i == settingsPick); // US-T437
			else DrawSetting(shown[i], Rect(0, y, content.width, SettingRowH), searching);
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
		// US-T322: the keys' hint, between Reset and Back.
		GUI::Label(Rect(px + 230, by + 6, r.xMax() - 20 - 110 - 10 - (px + 230), 18),
			bindingsPage ? "Up / Down pick  ·  Left / Right slot  ·  Enter listens  ·  M mode  ·  Backspace clears  ·  Del resets  ·  PgUp / PgDn group" // US-T437
				: "Up / Down pick  ·  Left / Right change  ·  Enter flips  ·  Del resets  ·  PgUp / PgDn group  ·  Ctrl+F search", small);
		if (GUI::Button(Rect(r.xMax() - 20 - 110, by, 110, 30), "Back", button))
		{
			search = ""; // the button leaves the page outright; only Esc stops at clearing a search first
			Back();
		}
	}

	void PauseMenu::NavGroup(const cs::String& g, Rect r, bool searching)
	{
		if (GUI::Button(r, g, !searching && g == group ? buttonActive : button)) ShowGroup(g);
	}

	void PauseMenu::ShowGroup(const cs::String& g)
	{
		group = g;
		search = "";
		settingsScroll = Vector2::zero();
		settingsPick = -1; // US-T322
		captureAction = captureSlot = -1; // US-T249: a slot listening on the page left behind stops
	}

	void PauseMenu::SettingsKeys()
	{
		if (!UiInput::present()) return;
		auto pressed = [](KeyCode k) { return UiInput::WasPressedThisFrame(k); };
		bool ctrl = UiInput::IsPressed(KeyCode::LeftControl) || UiInput::IsPressed(KeyCode::RightControl);
		if (GUI::GetNameOfFocusedControl() == "SettingsSearch")
		{
			// The caret's keys while Search is typed in; Enter or Down leaves it for the list.
			if (pressed(KeyCode::Return) || pressed(KeyCode::KeypadEnter) || pressed(KeyCode::DownArrow))
			{
				GUIUtility::keyboardControl = 0;
				if (shownSettings.Count() > 0) settingsPick = 0;
			}
			return;
		}
		if (ctrl && pressed(KeyCode::F))
		{
			GUI::FocusControl("SettingsSearch");
			return;
		}

		if (pressed(KeyCode::PageUp) || pressed(KeyCode::PageDown))
		{
			if (navGroups.Count() == 0) return;
			int at = navGroups.IndexOf(group);
			int step = pressed(KeyCode::PageDown) ? 1 : -1;
			int next = at < 0 ? 0 : (at + step + navGroups.Count()) % navGroups.Count();
			ShowGroup(navGroups[next]);
			return;
		}

		int count = shownSettings.Count();
		if (count == 0) return;
		int move = pressed(KeyCode::UpArrow) || pressed(KeyCode::W) ? -1 : pressed(KeyCode::DownArrow) || pressed(KeyCode::S) ? 1 : 0;
		if (move != 0)
		{
			settingsPick = settingsPick < 0 ? (move > 0 ? 0 : count - 1) : (settingsPick + move + count) % count;
			// The list follows the pick (the Display row, when a search found it, sits above the settings).
			float rowTop = RowTop(settingsPick); // US-T437: the bindings page's rows sit under their groups' heads
			if (rowTop < settingsScroll.y) settingsScroll.y = rowTop;
			else if (rowTop + SettingRowH > settingsScroll.y + settingsViewH) settingsScroll.y = rowTop + SettingRowH - settingsViewH;
			return;
		}
		if (settingsPick < 0 || settingsPick >= count) return;

		const auto& setting = shownSettings[settingsPick];
		if (pressed(KeyCode::Delete))
		{
			setting->Reset();
			return;
		}
		if (auto binding = cs::As<Input::KeyBindingSetting>(setting)) // US-T437: the slots, by the keys
		{
			if (pressed(KeyCode::RightArrow) || pressed(KeyCode::D)) bindingColumn = Mathf::Min(bindingColumn + 1, Input::KeyBindingSetting::Slots - 1);
			else if (pressed(KeyCode::LeftArrow) || pressed(KeyCode::A)) bindingColumn = Mathf::Max(bindingColumn - 1, 0);
			else if (pressed(KeyCode::M)) StepTrigger(setting, bindingColumn);
			else if (pressed(KeyCode::Backspace)) binding->Set(bindingColumn, Input::InputBinding());
			else if (pressed(KeyCode::Return) || pressed(KeyCode::KeypadEnter) || pressed(KeyCode::Space))
				StartCapture(static_cast<int>(Input::KeyBindings::ActionOf(*setting)), bindingColumn);
			return;
		}
		int change = pressed(KeyCode::RightArrow) || pressed(KeyCode::D) ? 1 : pressed(KeyCode::LeftArrow) || pressed(KeyCode::A) ? -1 : 0;
		bool enter = pressed(KeyCode::Return) || pressed(KeyCode::KeypadEnter) || pressed(KeyCode::Space);
		if (change == 0 && !enter) return;

		if (auto toggle = cs::As<ToggleSetting>(setting))
		{
			toggle->Set(change > 0 ? true : change < 0 ? false : !toggle->Value);
		}
		else if (auto range = cs::As<RangeSetting>(setting))
		{
			// A step, or a twentieth of the range for a setting that takes any value.
			double step = range->Step > 0 ? range->Step : (range->Max - range->Min) / 20.0;
			range->Set(range->Value + (change != 0 ? change : 1) * step);
		}
		else if (auto choice = cs::As<ChoiceSetting>(setting))
		{
			choice->Cycle(change != 0 ? change : 1);
		}
	}

	void PauseMenu::DrawLeave()
	{
		const float w = 380, h = 132;
		auto r = Centred(w, h);
		GUI::DrawTexture(Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height)), shadeTexture); // over the first page
		GUI::Box(r, GUIContent::none(), panel);
		bool quit = leaving == Leave::Quit;
		GUI::Label(Rect(r.x + 20, r.y + 14, w - 40, 22), quit ? "Quit the game?" : "Leave for the main menu?", title);
		GUI::Label(Rect(r.x + 20, r.y + 40, w - 40, 36),
			quit ? "The game autosaves on the way out." : "The game autosaves on the way.  (Settings > Gameplay stops this question.)", small);
		float bw = (w - 50) * 0.5f;
		// US-T311: once the keys are used here, the one they pick is the one lit.
		bool leaveLit = !leaveKeyed || leavePick == 0;
		if (GUI::Button(Rect(r.x + 20, r.yMax() - 46, bw, 30), quit ? "Quit" : "Main Menu", leaveLit ? buttonActive : button)) LeaveNow(leaving);
		else if (GUI::Button(Rect(r.x + 30 + bw, r.yMax() - 46, bw, 30), "Stay", leaveKeyed && leavePick == 1 ? buttonActive : button)) leaving = Leave::None;
	}

	void PauseMenu::LeaveNow(Leave what)
	{
		leaving = Leave::None;
		if (what == Leave::MainMenu)
		{
			Close();
			if (ToMainMenu) ToMainMenu();
		}
		else if (what == Leave::Quit)
		{
			Time::timeScale = timeScaleBefore;
			Quit();
		}
	}

	void PauseMenu::DrawSetting(const cs::Ref<GameSetting>& setting, Rect r, bool showGroup)
	{
		if (cs::As<Input::KeyBindingSetting>(setting))
		{
			DrawKeyBinding(setting, r, showGroup, false); // US-T249
			return;
		}
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

	void PauseMenu::DrawKeyBinding(const cs::Ref<GameSetting>& setting, Rect r, bool showGroup, bool picked)
	{
		using namespace ::SpaceGod::Input;
		auto binding = cs::As<KeyBindingSetting>(setting);
		KeyAction action = KeyBindings::ActionOf(*setting);
		const float gap = 6, resetW = 60;
		// US-T437: three slots, Primary / Secondary / Tertiary, as wide as the row allows (the page's panel is wider).
		const float slotW = Mathf::Clamp((r.width * 0.62f - resetW - 2 * gap) / KeyBindingSetting::Slots, 96.0f, 168.0f);
		const float controlW = slotW * KeyBindingSetting::Slots + gap * (KeyBindingSetting::Slots - 1);
		float textW = r.width - controlW - resetW - 24;
		bool listeningHere = captureAction == static_cast<int>(action);
		// An input just taken: the mouse's release and the pad's press must not click a slot again (UpdateSwallow).
		bool clicks = !captureSwallow;

		// An input another action live in the same place also answers to (shared defaults aside: those are told apart by
		// where each is live) is named under the label, so a clash is seen where it is made.
		cs::String clash;
		for (const InputBinding& c : binding->Value)
			for (KeyAction other : KeyBindings::Conflicts(action, c))
				clash = cs::Cat(clash, clash.empty() ? "" : ", ", c.Display(), " is also ", KeyBindings::Name(other));
		cs::String name = showGroup ? cs::Cat(setting->Group, "  ·  ", setting->Label) : KeyBindings::ShortName(action);
		GUI::Label(Rect(r.x + 4, r.y + 4, textW, 20), name, label);
		GUI::Label(Rect(r.x + 4, r.y + 24, textW, SettingRowH - 26), clash.empty() ? setting->Description : cs::Cat("CLASH: ", clash), small);

		float cx = r.x + textW + 12;
		for (int i = 0; i < KeyBindingSetting::Slots; i++)
		{
			bool listening = listeningHere && captureSlot == i;
			const InputBinding& c = binding->Value[i];
			float x = cx + i * (slotW + gap);
			// The keys' pick: the picked row's picked slot is underlined.
			if (picked && i == bindingColumn && !listeningHere) GUI::DrawTexture(Rect(x - 2, r.y + 2, slotW + 4, SettingRowH - 6), rowSelectedTexture);
			cs::String shown = listening ? cs::String("press anything...") : c.Empty() ? cs::String("-") : cs::String(c.With(Trigger::Auto).Display());
			if (GUI::Button(Rect(x, r.y + 4, slotW, 26), shown, listening ? buttonActive : button) && clicks)
			{
				if (listening) captureAction = captureSlot = -1; // clicked again: stop listening
				else StartCapture(static_cast<int>(action), i);
			}
			// The slot's trigger mode, stepped by a click. Lit when it is not the action's own.
			bool enabled = GUI::enabled;
			GUI::enabled = enabled && !c.Empty() && !listening;
			cs::String mode = c.Empty() ? cs::String(" ") : cs::String(TriggerLabel(Resolve(c.trigger, binding->HeldKind)));
			if (GUI::Button(Rect(x, r.y + 33, slotW, 20), mode, c.trigger != Trigger::Auto ? modeActive : modeButton) && clicks)
				StepTrigger(setting, i);
			GUI::enabled = enabled;
		}

		bool enabled = GUI::enabled;
		if (listeningHere)
		{
			if (GUI::Button(Rect(r.xMax() - resetW, r.y + 4, resetW, 26), "Clear", button) && clicks)
			{
				binding->Set(captureSlot, InputBinding());
				captureAction = captureSlot = -1;
			}
			return;
		}
		GUI::enabled = enabled && !setting->IsDefault();
		if (GUI::Button(Rect(r.xMax() - resetW, r.y + 4, resetW, 26), "Reset", button) && clicks) setting->Reset();
		GUI::enabled = enabled;
	}

	void PauseMenu::StartCapture(int action, int slot)
	{
		using namespace ::SpaceGod::Input;
		if (action < 0 || action >= static_cast<int>(KeyAction::Count)) return;
		const auto& binding = KeyBindings::Binding(static_cast<KeyAction>(action));
		// The bound slots come first: an empty one fills the first gap.
		if (slot < 0 || slot >= KeyBindingSetting::Slots || binding->Value[slot].Empty())
		{
			slot = KeyBindingSetting::Slots - 1;
			for (int i = KeyBindingSetting::Slots - 1; i >= 0; i--)
				if (binding->Value[i].Empty()) slot = i;
		}
		captureAction = action;
		captureSlot = slot;
		captureFresh = true;
		captureStartedAt = Time::unscaledTime;
		UnityCompat::GuiPadNav::Hold = true;
	}

	void PauseMenu::StepTrigger(const cs::Ref<GameSetting>& setting, int slot)
	{
		using namespace ::SpaceGod::Input;
		auto binding = cs::As<KeyBindingSetting>(setting);
		if (binding == nullptr || slot < 0 || slot >= KeyBindingSetting::Slots || binding->Value[slot].Empty()) return;
		int now = static_cast<int>(Resolve(binding->Value[slot].trigger, binding->HeldKind));
		int next = now + 1 >= static_cast<int>(Trigger::Count) ? static_cast<int>(Trigger::Press) : now + 1;
		binding->SetTrigger(slot, static_cast<Trigger>(next));
	}

	void PauseMenu::UpdateSwallow()
	{
		using namespace ::SpaceGod::Input;
		if (captureSwallow)
		{
			const InputDevice& kb = InputDevice::current();
			const GamepadDevice& pad = GamepadDevice::current();
			bool busy = false;
			if (kb.present)
				for (MouseButton m : { MouseButton::Left, MouseButton::Right, MouseButton::Middle, MouseButton::Back, MouseButton::Forward })
					busy = busy || kb.Button(m).isPressed || kb.Button(m).wasReleasedThisFrame;
			if (pad.present)
			{
				for (int i = static_cast<int>(PadButton::None) + 1; i < static_cast<int>(PadButton::Count); i++)
					busy = busy || pad[static_cast<PadButton>(i)].isPressed || pad[static_cast<PadButton>(i)].wasReleasedThisFrame;
				busy = busy || pad.leftStick.sqrMagnitude() > 0.09f || pad.rightStick.sqrMagnitude() > 0.09f || pad.leftTrigger > 0.3f || pad.rightTrigger > 0.3f;
			}
			if (!busy) captureSwallow = false;
		}
		UnityCompat::GuiPadNav::Hold = IsOpen && (captureAction >= 0 || captureSwallow);
	}

	float PauseMenu::RowTop(int i) const
	{
		if (i >= 0 && i < shownRowTops.Count()) return shownRowTops[i];
		return (i + (displayRowShown ? 1 : 0)) * SettingRowH;
	}

	void PauseMenu::ShowKeyBindings(const cs::String& g)
	{
		if (!IsOpen) OpenSettings();
		page = Page::Settings;
		ShowGroup(Input::KeyBindings::Group);
		scrollToGroup = g;
	}

	void PauseMenu::ListenOn(int action, int slot)
	{
		StartCapture(action, slot);
		captureFresh = false; // shown listening at once (the shot holds still)
		captureStartedAt = Time::unscaledTime + 3600.0f;
	}

	void PauseMenu::UpdateCapture(bool escape)
	{
		using namespace ::SpaceGod::Input;
		if (page != Page::Settings || captureAction >= static_cast<int>(KeyAction::Count))
		{
			captureAction = captureSlot = -1;
			return;
		}
		const GamepadDevice& pad = GamepadDevice::current();
		if (captureFresh)
		{
			// What is held as the listening starts (a stick still pushed from moving the focus) is not taken.
			KeyBindings::BeginCapture(pad);
			captureFresh = false;
			captureStartedAt = Time::unscaledTime;
		}
		// Taken first: the pad's B (or a binding's own Esc-like press) binds rather than cancels.
		InputBinding taken;
		if (KeyBindings::Capture(InputDevice::current(), pad, taken))
		{
			const auto& binding = KeyBindings::Binding(static_cast<KeyAction>(captureAction));
			taken.trigger = binding->Value[captureSlot].trigger; // the slot keeps its mode
			binding->Set(captureSlot, taken);
			captureAction = captureSlot = -1;
			captureSwallow = true;
			UnityCompat::GuiPadNav::Hold = true;
			return;
		}
		if (escape || Time::unscaledTime - captureStartedAt > static_cast<float>(KeyBindings::Tuning().CaptureTimeoutS))
			captureAction = captureSlot = -1;
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
		// US-T437: a binding slot's trigger mode, a small button under the slot (lit when it is not the action's own).
		modeButton = S::ButtonStyle(T::Caption, buttonTexture, buttonActiveTexture);
		modeActive = S::ButtonOnStyle(modeButton, buttonActiveTexture);
	}

	void PauseMenu::DestroyOwnedTextures()
	{
		// Object.Destroy(texture): dropping the last reference.
		panelTexture = shadeTexture = buttonTexture = buttonActiveTexture = rowHoverTexture = rowSelectedTexture = fieldTexture = nullptr;
		if (credits) credits->DestroyOwnedTextures(); // US-T246
	}
}
