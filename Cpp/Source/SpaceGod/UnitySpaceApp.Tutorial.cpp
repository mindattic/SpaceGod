// US-T254: the guided tutorial and the first-time hints as the player meets them (declared in UnitySpaceApp.Tutorial.inl;
// the steps, their rules and the hints' wording are Core's Tutorial.h). Display and coaching only: nothing in the
// simulation reads it, and it never takes the keys.

#include "UnitySpaceApp.h"

#include "ActiveProfile.h"
#include "GameLaunch.h"
#include "MindAtticHeadless.h"
#include "Ships.h"
#include "SimClock.h"
#include "Tutorial.h"
#include "Camera/SpaceCamera.h"
#include "Input/InputDevice.h"
#include "Persistence/Profiles.h"
#include "Settings/GameSetting.h"
#include "Tour/ShowcaseTour.h"
#include "UI/Hud.h"
#include "UI/Icons.h"
#include "UI/UiMotion.h"
#include "UI/UiStyle.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace SpaceGod
{
	using namespace Core;

	namespace
	{
		/// A run that is driven and photographed (the smoke, the audit, shots, views, a capture, the demo, headless) offers
		/// nothing and shows no hints: its pictures stay the game's, and the profile is not written. As UiMotion's rule.
		bool ScriptedTutorialRun()
		{
			if (FMindAtticHeadless::IsActive()) return true;
			const TCHAR* cursor = FCommandLine::Get();
			FString token;
			while (FParse::Token(cursor, token, false))
				if (token.StartsWith(TEXT("-SpaceGod"), ESearchCase::IgnoreCase) || token.Equals(TEXT("-demo"), ESearchCase::IgnoreCase)
					|| token.Equals(TEXT("-capture"), ESearchCase::IgnoreCase))
					return true;
			return false;
		}

		constexpr float HintSeconds = 16, DoneSeconds = 20, DoneFlashSeconds = 1.4f;
		constexpr int StartDelayFrames = 12; // a new game's scene lays itself out first

		void Outline(Rect r, float thickness)
		{
			const auto& w = UI::Icons::White();
			GUI::DrawTexture(Rect(r.x, r.y, r.width, thickness), w);
			GUI::DrawTexture(Rect(r.x, r.yMax() - thickness, r.width, thickness), w);
			GUI::DrawTexture(Rect(r.x, r.y, thickness, r.height), w);
			GUI::DrawTexture(Rect(r.xMax() - thickness, r.y, thickness, r.height), w);
		}
	}

	const cs::Ref<ToggleSetting>& UnitySpaceApp::TutorialHintsSetting()
	{
		static const cs::Ref<ToggleSetting> value = cs::New<ToggleSetting>("controls.tutorialHints", SettingGroups::Gameplay, // US-T252: under Gameplay (the key kept)
			"Tutorial hints", true,
			"A short card the first time you meet a battle, a hyperlane, a jump gate, a void stop or a fight aboard (each once per profile). The tutorial itself is under Help.",
			"tutorial hints tips help first time new player");
		return value;
	}

	void UnitySpaceApp::SetNavigatorOpen(bool open)
	{
		if (hud != nullptr) hud->SetNavigatorOpen(open);
	}

	TutorialSignals UnitySpaceApp::TutorialSignalsNow(float dt)
	{
		TutorialSignals s;
		s.Dt = dt;
		bool at = AtControls();
		s.AtControls = at;
		s.Moving = Input.Move.sqrMagnitude() > 0.01f || (at && (Input.ThrottleStep != 0 || Input.MouseDelta.sqrMagnitude() > 4));
		s.Looking = !at && Input.LookHeld && Input.LookDelta.sqrMagnitude() > 0;
		s.SelectedNew = !Selected.IsNone() && !(Selected == tutorialLastSelected);
		tutorialLastSelected = Selected;
		s.Focused = SpaceCamera != nullptr && SpaceCamera->Mode == Cameras::SpaceCamera::CameraMode::Focus;
		s.WarpIndex = Clock != nullptr ? Clock->WarpIndex : 0;
		s.ScaleGoal = ScaleGoal;
		s.NavigatorOpen = hud != nullptr && hud->NavigatorOpen();
		s.Firing = at && Input.Fire;
		s.LockPressed = at && Input.LockTarget;
		s.Locked = at && (pilotLockTarget.IsShip() || pilotLockUnit != nullptr);
		s.MissilePressed = at && Input.LookHeld && !tutorialMissileHeld;
		tutorialMissileHeld = at && Input.LookHeld;
		s.CommsOpen = commsOpen;
		s.MarketOpen = ShowMarket;
		s.GalaxyMapOpen = ShowGalaxyMap;
		return s;
	}

	void UnitySpaceApp::StartTutorial()
	{
		if (Tour::ShowcaseTour::Running())
		{
			ShowToast("The showcase tour is running: Esc ends it, then the tutorial can start");
			return;
		}
		if (tutorial == nullptr) tutorial = cs::New<Tutorial>();
		tutorialOffer = false;
		tutorialDoneAge = -1;
		tutorialHint = -1;
		tutorialLastSelected = Selected;
		tutorialMissileHeld = Input.LookHeld;
		tutorial->Start(TutorialSignalsNow(0));
	}

	void UnitySpaceApp::StopTutorial()
	{
		if (tutorial != nullptr) tutorial->Stop();
		ShowToast("Tutorial ended: Help (F1) or the main menu starts it again");
	}

	void UnitySpaceApp::TutorialKeys()
	{
		bool running = tutorial != nullptr && tutorial->Running();
		if (!running && !tutorialOffer && tutorialDoneAge < 0 && tutorialHint < 0) return;
		const Input::InputDevice& kb = Input::InputDevice::current();
		bool shift = kb[Input::Key::LeftShift].isPressed || kb[Input::Key::RightShift].isPressed;
		if (!shift) return;
		bool next = kb[Input::Key::Enter].wasPressedThisFrame || kb[Input::Key::NumpadEnter].wasPressedThisFrame;
		bool end = kb[Input::Key::Escape].wasPressedThisFrame;
		if (!next && !end) return;
		// The card has these keys this frame: Shift+Enter does not also spool the drive, nor Shift+Esc open the Esc menu.
		Input.EngageFtl = false;
		Input.Escape = false;
		if (tutorialOffer)
		{
			if (next) StartTutorial();
			else tutorialOffer = false;
		}
		else if (running)
		{
			if (next) tutorial->Skip(TutorialSignalsNow(0));
			else StopTutorial();
			if (tutorial->Finished()) tutorialDoneAge = 0;
		}
		else if (tutorialDoneAge >= 0) tutorialDoneAge = -1;
		else DismissTutorialHint();
	}

	void UnitySpaceApp::UpdateTutorial(float dt)
	{
		// The offer (or the tutorial asked for outright) a few frames into a game started from the main menu.
		if (!tutorialAsked)
		{
			tutorialAsked = true;
			tutorialAsk = GameLaunch::TakeTutorial();
			if (tutorialAsk != 0) tutorialDueFrames = StartDelayFrames;
		}
		if (tutorialDueFrames >= 0 && --tutorialDueFrames < 0)
		{
			if (tutorialAsk == 2) StartTutorial();
			else if (tutorialAsk == 1 && !ScriptedTutorialRun() && !Tour::ShowcaseTour::Running())
			{
				try
				{
					auto profile = ActiveProfile::Current();
					if (profile != nullptr && !GameSettings::Flag(profile, TutorialHints::OfferedKey, false))
					{
						tutorialOffer = true;
						GameSettings::SetFlag(profile, TutorialHints::OfferedKey, true); // offered once, taken or not
					}
				}
				catch (const std::exception& e)
				{
					Debug::LogWarning(cs::Cat("[Unity.SpaceGod] The tutorial offer could not read or write the profile: ", e.what()));
				}
			}
		}

		if (tutorial != nullptr && tutorial->Running())
		{
			if (tutorial->Update(TutorialSignalsNow(dt)) && tutorial->Finished()) tutorialDoneAge = 0;
		}
		else if (tutorialDoneAge >= 0)
		{
			tutorialDoneAge += dt;
			if (tutorialDoneAge > DoneSeconds) tutorialDoneAge = -1;
		}
		else if (!tutorialOffer) UpdateTutorialHints(dt);
	}

	void UnitySpaceApp::UpdateTutorialHints(float dt)
	{
		if (tutorialHint >= 0)
		{
			tutorialHintAge += dt;
			if (tutorialHintAge > HintSeconds) tutorialHint = -1;
			return;
		}
		if (!TutorialHintsSetting()->Value || ScriptedTutorialRun() || Tour::ShowcaseTour::Running() || World() == nullptr) return;

		TutorialHint due = TutorialHint::Count;
		if (IsPiloting() && Piloted.Ship != nullptr && Piloted.Ship->Battle != nullptr) due = TutorialHint::FirstBattle;
		else if (IsPiloting() && RidingLane()) due = TutorialHint::FirstLane;
		else if (IsPiloting() && nearGate != nullptr) due = TutorialHint::FirstGate;
		else if (InFtlCruise() && ftlAtStop) due = TutorialHint::FirstVoidStop;
		else if (BoardingFight.has_value()) due = TutorialHint::FirstBoarding;
		if (due == TutorialHint::Count) return;

		auto profile = ActiveProfile::Current();
		if (profile == nullptr) return;
		try
		{
			if (!tutorialHintsLoaded)
			{
				tutorialHintsLoaded = true;
				tutorialHintsSeen = TutorialHints::Parse(GameSettings::Setting(profile, TutorialHints::SeenKey));
			}
			cs::String id = TutorialHints::Of(due).Id;
			if (tutorialHintsSeen.count(id) > 0) return;
			tutorialHintsSeen.insert(id);
			tutorialHint = static_cast<int>(due);
			tutorialHintAge = 0;
			GameSettings::SetSetting(profile, TutorialHints::SeenKey, TutorialHints::Format(tutorialHintsSeen));
		}
		catch (const std::exception& e)
		{
			Debug::LogWarning(cs::Cat("[Unity.SpaceGod] A tutorial hint could not read or write the profile: ", e.what()));
		}
	}

	void UnitySpaceApp::DismissTutorialHint() { tutorialHint = -1; }

	// ---------------------------------------------------------------- the cards

	void UnitySpaceApp::DrawTutorial()
	{
		bool running = tutorial != nullptr && tutorial->Running();
		if (!running && !tutorialOffer && tutorialDoneAge < 0 && tutorialHint < 0) return;
		if (ScreenCleared || Tour::ShowcaseTour::Running()) return;
		if (!tutorialStylesReady)
		{
			tutorialStylesReady = true;
			namespace S = UI::UiStyle;
			namespace T = UI::UiStyle::Type;
			tutorialPanelTexture = S::PanelFrame(S::Depth::Overlay);
			tutorialButtonTexture = S::ControlTexture();
			tutorialButtonOnTexture = S::ControlOnTexture();
			tutorialBarTexture = S::Flat(S::Accent, Color::clear());
			tutorialPanel = S::PanelStyle(tutorialPanelTexture);
			tutorialTitle = S::TitleStyle(T::Title);
			tutorialHeading = S::TextStyle(T::Caption, S::Accent, FontStyle::Bold);
			tutorialText = S::TextStyle(T::Body);
			tutorialText->wordWrap = true;
			tutorialSmall = S::CaptionStyle(T::Caption);
			tutorialSmallRight = cs::New<GUIStyle>(*tutorialSmall);
			tutorialSmallRight->alignment = TextAnchor::MiddleRight;
			tutorialKeys = S::TextStyle(T::Heading, S::Warning, FontStyle::Bold);
			tutorialKeys->alignment = TextAnchor::MiddleRight;
			tutorialButton = S::ButtonStyle(T::Label, tutorialButtonTexture, tutorialButtonOnTexture);
		}

		float sw = static_cast<float>(Screen::width), sh = static_cast<float>(Screen::height);
		auto take = [this](Rect r)
		{
			auto type = Event::current().type;
			if ((type == EventType::Layout || type == EventType::Repaint) && !UiRects.Contains(r)) UiRects.Add(r); // the world under it takes no clicks
		};
		const char* sep = UI::UiStyle::Glyph::Separator;

		// The welcome card, a profile's first New Game.
		if (tutorialOffer)
		{
			float w = 480, h = 178;
			auto r = Rect((sw - w) * 0.5f, (sh - h) * 0.5f - 60, w, h);
			UI::UiMotion::Scope motion("tutorial.offer");
			take(r);
			GUI::Box(r, GUIContent::none(), tutorialPanel);
			GUI::Label(Rect(r.x + 16, r.y + 12, w - 32, 22), "WELCOME, SPACEGOD", tutorialTitle);
			GUI::Label(Rect(r.x + 16, r.y + 38, w - 32, 72),
				cs::Cat("New here? The tutorial teaches the game by doing: ", Tutorial::StepCount(),
					" short steps, from swimming about to the galaxy map. Say no and it waits under Help (F1) and on the main menu."), tutorialText);
			if (GUI::Button(Rect(r.x + 16, r.y + 116, 200, 32), "Start the tutorial", tutorialButton)) StartTutorial();
			if (GUI::Button(Rect(r.x + 226, r.y + 116, 120, 32), "No thanks", tutorialButton)) tutorialOffer = false;
			GUI::Label(Rect(r.x + 16, r.y + 154, w - 32, 16), cs::Cat("Shift+Enter starts it", sep, "Shift+Esc says no"), tutorialSmall);
			return;
		}

		float w = Mathf::Min(580.0f, sw - 32), h = 150;
		// Bottom centre, over the status line and the drawer's handle.
		auto r = Rect((sw - w) * 0.5f, sh - 10 - 30 - 10 - h, w, h);

		if (running)
		{
			const auto& step = tutorial->Current();
			int n = tutorial->Index() + 1, total = Tutorial::StepCount();

			// The HUD element the step is about, outlined and pulsing (where the HUD shows it).
			auto spot = UI::Hud::TutorialHighlightRect(static_cast<int>(step.Highlight));
			if (spot.width > 0 && ShowHud && !ClickPanelOpen())
			{
				float pulse = 0.55f + 0.45f * Mathf::Sin(Time::unscaledTime * 5.0f);
				auto old = GUI::color;
				GUI::color = Color(UI::UiStyle::Warning.r, UI::UiStyle::Warning.g, UI::UiStyle::Warning.b, pulse);
				Outline(Rect(spot.x - 4, spot.y - 4, spot.width + 8, spot.height + 8), 2);
				GUI::color = old;
			}

			UI::UiMotion::Scope motion(cs::Cat("tutorial.step.", step.Id).c_str());
			take(r);
			GUI::Box(r, GUIContent::none(), tutorialPanel);
			bool flash = tutorial->Index() > 0 && tutorial->StepAge() < DoneFlashSeconds;
			cs::String head = flash
				? cs::Cat("TUTORIAL", sep, "STEP ", n, " OF ", total, sep, "DONE: ", Tutorial::Steps()[tutorial->Index() - 1].Title)
				: cs::Cat("TUTORIAL", sep, "STEP ", n, " OF ", total);
			auto oldColor = GUI::color;
			if (flash) GUI::color = UI::UiStyle::Good;
			GUI::Label(Rect(r.x + 16, r.y + 10, w - 32, 16), head, tutorialHeading);
			GUI::color = oldColor;
			GUI::Label(Rect(r.x + 16, r.y + 28, w - 32, 22), step.Title, tutorialTitle);
			GUI::Label(Rect(r.x + w * 0.4f, r.y + 26, w * 0.6f - 16, 24), step.Keys, tutorialKeys);
			GUI::Label(Rect(r.x + 16, r.y + 54, w - 32, 54), step.Text, tutorialText);

			// How far into a two-part step (swim and look, fly and fire, lock and fire a missile).
			float progress = tutorial->Progress();
			if (progress > 0)
			{
				auto bar = Rect(r.x + 16, r.y + 108, w - 32, 3);
				GUI::DrawTexture(bar, tutorialButtonTexture);
				GUI::DrawTexture(Rect(bar.x, bar.y, bar.width * progress, bar.height), tutorialBarTexture);
			}

			float by = r.y + h - 34;
			if (GUI::Button(Rect(r.x + 16, by, 96, 24), "Skip step", tutorialButton))
			{
				tutorial->Skip(TutorialSignalsNow(0));
				if (tutorial->Finished()) tutorialDoneAge = 0;
			}
			if (GUI::Button(Rect(r.x + 118, by, 112, 24), "End tutorial", tutorialButton)) StopTutorial();
			if (step.Step == TutorialStep::Embody && !AtControls() && GUI::Button(Rect(r.x + 236, by, 130, 24), "Practice fighter", tutorialButton))
				SpawnPracticeFighter();
			GUI::Label(Rect(r.x + 372, by, w - 388, 24), cs::Cat("Shift+Enter skips", sep, "Shift+Esc ends"), tutorialSmallRight);
			return;
		}

		if (tutorialDoneAge >= 0)
		{
			UI::UiMotion::Scope motion("tutorial.done");
			take(r);
			GUI::Box(r, GUIContent::none(), tutorialPanel);
			GUI::Label(Rect(r.x + 16, r.y + 10, w - 32, 16), "TUTORIAL", tutorialHeading);
			GUI::Label(Rect(r.x + 16, r.y + 28, w - 32, 22), "That is the heart of it", tutorialTitle);
			GUI::Label(Rect(r.x + 16, r.y + 54, w - 32, 60),
				"The manual (F1) has every chapter and every key. A short hint will come up the first time you meet a battle, a hyperlane, a jump gate, a void stop or a fight aboard (Settings > Gameplay turns them off).",
				tutorialText);
			if (GUI::Button(Rect(r.x + 16, r.y + h - 34, 96, 24), "Close", tutorialButton)) tutorialDoneAge = -1;
			return;
		}

		// A first-time hint: smaller, fading out at the end of its time.
		const auto& hint = TutorialHints::Of(static_cast<TutorialHint>(tutorialHint));
		float hh = 118;
		auto hr = Rect(r.x, r.yMax() - hh, w, hh);
		float fade = Mathf::Clamp01((HintSeconds - tutorialHintAge) / 1.5f);
		UI::UiMotion::Scope motion(UI::UiMotion::Enter(tutorialHintAge) * fade, UI::UiMotion::ToastSlide);
		take(hr);
		GUI::Box(hr, GUIContent::none(), tutorialPanel);
		GUI::Label(Rect(hr.x + 16, hr.y + 10, w - 32, 16), "HINT", tutorialHeading);
		GUI::Label(Rect(hr.x + 16, hr.y + 26, w - 32, 22), hint.Title, tutorialTitle);
		GUI::Label(Rect(hr.x + 16, hr.y + 50, w - 132, 62), hint.Text, tutorialText);
		if (GUI::Button(Rect(hr.xMax() - 106, hr.yMax() - 34, 90, 24), "Got it", tutorialButton)) DismissTutorialHint();
	}

	void UnitySpaceApp::DestroyTutorialTextures()
	{
		tutorialPanelTexture = tutorialButtonTexture = tutorialButtonOnTexture = tutorialBarTexture = nullptr;
		tutorialStylesReady = false;
	}
}
