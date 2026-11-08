// FeatureAudit's tutorial probe (US-T254): the guided tutorial played headless to its end (FeatureAuditTutorial.h).
#include "Smoke/FeatureAuditTutorial.h"
#include "Smoke/ScriptedDevice.h"

#include "GameSettingsHost.h"
#include "SimClock.h"
#include "Tutorial.h"
#include "UnitySpaceApp.h"
#include "Camera/SpaceCamera.h"
#include "Settings/GameSetting.h"
#include "Settings/SettingsRegistry.h"

namespace SpaceGod::Smoke::FeatureAudit
{
	using namespace SpaceGod::Core;
	using Input::Key;
	using Input::MouseButton;

	namespace
	{
		/// The most frames a step is given to see its action (a headless frame can be very short: the held steps count
		/// real seconds).
		constexpr int StepFrames = 3000;
	}

	void TutorialProbes(UnitySpaceApp& app, ScriptedDevice& kb, const TutorialReport& result, const std::function<void(int)>& frames)
	{
		auto tap = [&](Key k, std::initializer_list<Key> mods)
		{
			for (Key m : mods) kb.Hold(m);
			kb.Tap(k);
			frames(1);
			for (Key m : mods) kb.Release(m);
			frames(1);
		};
		auto index = [&]() { return app.TutorialProgress() != nullptr ? app.TutorialProgress()->Index() : -1; };
		auto running = [&]() { return app.TutorialProgress() != nullptr && app.TutorialProgress()->Running(); };

		auto setting = GameSettingsHost::Registry()->Find("controls.tutorialHints");
		result("tutorial.hints-setting", setting != nullptr && setting == UnitySpaceApp::TutorialHintsSetting(),
			cs::Cat("controls.tutorialHints ", setting != nullptr ? cs::Cat("in the registry, ", setting->ValueText()) : cs::String("missing")));

		// From the free view, nothing held, nothing open.
		if (app.AtControls()) tap(Key::P, {});
		app.ShowHelp = app.ShowMarket = app.ShowGalaxyMap = false;
		app.SpaceCamera->ExitFocus();
		app.Select(Target::None());
		app.SetNavigatorOpen(false);
		int warp0 = app.Clock->WarpIndex;
		double scale0 = app.ScaleGoal;

		app.StartTutorial();
		frames(2);
		result("tutorial.start", running() && index() == 0, cs::Cat("running ", running() ? "True" : "False", ", step ", index()));
		if (!running()) return;

		// Each step: its action, by the keys it teaches, until the tutorial moves on (or the frames run out).
		auto step = [&](int expected, const std::function<void(int frame)>& act, const std::function<void()>& after)
		{
			const auto& info = Tutorial::Steps()[expected];
			if (index() != expected || !running())
			{
				result(cs::Cat("tutorial.", info.Id), false, cs::Cat("not reached: the tutorial is at step ", index()));
				return;
			}
			int f = 0;
			for (; f < StepFrames && running() && index() == expected; f++) act(f);
			bool ok = !running() ? app.TutorialProgress()->Finished() : index() == expected + 1;
			result(cs::Cat("tutorial.", info.Id), ok, cs::Cat(info.Title, " (", info.Keys, "): ", ok ? "done" : "still waiting", " after ", f, " frames; toast '", app.Toast, "'"));
			if (after) after();
		};
		auto pressing = [](std::function<void()> press) { return [press](int frame) { (void)frame; press(); }; }; // pressed again each try until it moves on

		step(0, [&](int) { kb.Hold(Key::W); kb.HoldButton(MouseButton::Right); kb.mouseDelta = Vector2(6, 0); frames(1); },
			[&] { kb.Release(Key::W); kb.ReleaseButton(MouseButton::Right); frames(2); });
		step(1, [&](int frame) { tap(frame % 2 == 0 ? Key::Tab : Key::Digit3, {}); }, nullptr);
		step(2, [&](int frame) { if (frame == 0) tap(Key::F, {}); else frames(1); }, nullptr);
		step(3, pressing([&] { tap(Key::Period, {}); }), nullptr);
		step(4, pressing([&] { tap(Key::G, {}); }), nullptr);
		step(5, [&](int) { app.SetNavigatorOpen(true); frames(1); }, [&] { app.SetNavigatorOpen(false); });
		step(6, [&](int frame) { if (frame % 30 == 0) tap(Key::V, { Key::LeftShift }); else frames(1); }, nullptr);
		step(7, [&](int) { kb.Hold(Key::W); kb.HoldButton(MouseButton::Left); frames(1); },
			[&] { kb.Release(Key::W); kb.ReleaseButton(MouseButton::Left); frames(2); });
		step(8, [&](int frame)
		{
			if (frame % 2 == 0) tap(Key::T, {});
			else { kb.HoldButton(MouseButton::Right); frames(1); kb.ReleaseButton(MouseButton::Right); frames(1); }
		}, nullptr);
		step(9, [&](int frame) { if (frame % 30 == 0) tap(Key::H, { Key::LeftShift }); else frames(1); },
			[&] { if (app.CommsOpen()) tap(Key::H, { Key::LeftShift }); });
		step(10, [&](int frame) { if (frame % 30 == 0) tap(Key::P, {}); else frames(1); }, nullptr);
		step(11, pressing([&] { tap(Key::M, {}); }), [&] { app.ShowMarket = false; });
		step(12, pressing([&] { tap(Key::LeftBracket, {}); }), [&] { app.ShowGalaxyMap = false; });

		bool finished = app.TutorialProgress()->Finished();
		result("tutorial.finish", finished && !running(),
			cs::Cat("finished ", finished ? "True" : "False", " after ", Tutorial::StepCount(), " steps"));

		// Back as the probes found it.
		if (app.AtControls()) tap(Key::P, {});
		for (int i = 0; i < 20 && app.Clock->WarpIndex > warp0; i++) tap(Key::Comma, {});
		for (int i = 0; i < 20 && app.Clock->WarpIndex < warp0; i++) tap(Key::Period, {});
		if (app.ScaleGoal != scale0) tap(Key::G, {});
		app.SpaceCamera->ExitFocus();
		app.Select(Target::None());
		frames(5);
	}
}
