#include "Smoke/FeatureAudit.h"
#include "Smoke/ScriptedDevice.h"
#include "Smoke/FeatureAuditSettings.h"
#include "Smoke/FeatureAuditEditing.h"

#include "Target.h"
#include "UnitySpaceApp.h"
#include "UnrealSpaceGodLog.h"
#include "Camera/SpaceCamera.h"
#include "Input/InputDevice.h"
#include "UnityCompat/UnityRuntime.h"
#include "Views/SkyView.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ships.h"
#include "SimClock.h"
#include "Rendering/Skyboxes.h"

#include <algorithm>
#include <cctype>
#include <exception>

namespace SpaceGod::Smoke::FeatureAudit
{
	using namespace SpaceGod::Core;
	using namespace SpaceGod::UnityCompat;
	using Input::Key;
	using CameraMode = Cameras::SpaceCamera::CameraMode;

	namespace
	{
		UnitySpaceApp* app = nullptr;
		ScriptedDevice* kb = nullptr;
		std::function<void(int)> frames;
		int pass = 0, fail = 0;

		void Log(const cs::String& line) { UE_LOG(LogSpaceGod, Display, TEXT("%s"), UTF8_TO_TCHAR(line.c_str())); }

		void Result(const cs::String& id, bool ok, const cs::String& detail)
		{
			if (ok) pass++; else fail++;
			Log(cs::Cat("AUDIT ", id, ok ? " PASS: " : " FAIL: ", detail));
		}

		const char* B(bool v) { return v ? "True" : "False"; }

		bool Contains(const cs::String& s, const cs::String& part)
		{
			cs::String a = s, b = part;
			std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a.find(b) != cs::String::npos;
		}

		bool StartsWith(const cs::String& s, const cs::String& prefix) { return s.rfind(prefix, 0) == 0; }

		void Tap(Key k, std::initializer_list<Key> mods = {})
		{
			for (Key m : mods) kb->Hold(m);
			kb->Tap(k);
			frames(1);
			for (Key m : mods) kb->Release(m);
			frames(1);
		}

		/// A key that flips a flag: pressed once it must flip, pressed again flip back. The flag is put back after.
		void Toggle(const cs::String& id, Key k, bool& flag, std::initializer_list<Key> mods = {})
		{
			bool before = flag;
			Tap(k, mods);
			bool on = flag;
			Tap(k, mods);
			bool off = flag;
			flag = before;
			Result(id, on != before && off == before, cs::Cat(B(before), " -> ", B(on), " -> ", B(off)));
		}

		const cs::String& Toast() { return app->Toast; }

		void CloseEverything()
		{
			UnitySpaceApp& a = *app;
			a.ShowHelp = a.ShowMarket = a.ShowCargo = a.ShowOutfit = a.ShowBounties = a.ShowContracts = false;
			a.ShowFleet = a.ShowTrade = a.ShowGalaxyMap = a.ShowNews = a.ShowSpawn = a.ShowSaves = a.ShowPicker = false;
			a.ScreenCleared = false;
			a.ShowHud = true;
			Time::timeScale = 1;
		}

		cs::String Describe(const Target& t)
		{
			if (t.IsBody()) return cs::Cat("body ", (*app->Catalog())[t.Body]->Name);
			if (t.IsShip()) return cs::Cat("ship ", t.Ship ? t.Ship->Name : cs::String());
			return ToString(t.Kind);
		}

		// ---------------------------------------------------------------- time

		void TimeProbes()
		{
			bool paused = app->Clock->PauseHeld();
			Tap(Key::Space);
			bool p1 = app->Clock->PauseHeld();
			Tap(Key::Space);
			Result("time.pause", p1 != paused && app->Clock->PauseHeld() == paused,
				cs::Cat("Space: ", B(paused), " -> ", B(p1), " -> ", B(app->Clock->PauseHeld())));

			cs::String w0 = app->Clock->Warp().Label;
			Tap(Key::Period);
			cs::String w1 = app->Clock->Warp().Label;
			Tap(Key::Comma);
			cs::String w2 = app->Clock->Warp().Label;
			Result("time.warp", w1 != w0 && w2 == w0, cs::Cat("'.' ", w0, " -> ", w1, ", ',' -> ", w2));
			Tap(Key::Equals);
			cs::String w3 = app->Clock->Warp().Label;
			Tap(Key::Minus);
			Result("time.warp-alt", w3 != w0 && app->Clock->Warp().Label == w0, cs::Cat("'=' ", w0, " -> ", w3, ", '-' -> ", app->Clock->Warp().Label));

			Tap(Key::T);
			double hours = cs::Math::Abs((app->Clock->Utc() - cs::DateTime::UtcNow()).TotalHours());
			Result("time.reset", hours < 1, cs::Cat("T: clock ", app->Clock->Utc().ToString("yyyy-MM-dd HH:mm"), " UTC, ", cs::Fmt(hours, "F2"), " h from now"));
		}

		// ---------------------------------------------------------------- view

		void ViewProbes()
		{
			double g0 = app->ScaleGoal;
			Tap(Key::G);
			double g1 = app->ScaleGoal;
			Tap(Key::Backquote);
			Result("view.scale", g1 != g0 && app->ScaleGoal == g0, cs::Cat("G ", g0, " -> ", g1, ", ` -> ", app->ScaleGoal));
			frames(60);

			Toggle("view.orbits", Key::O, app->ShowOrbits);
			Toggle("view.labels", Key::L, app->ShowLabels);
			Toggle("view.lanes", Key::N, app->ShowLanes);
			Toggle("view.satellites", Key::K, app->ShowSatellites);
			{
				bool before = app->AutoReorient();
				Tap(Key::R);
				bool on = app->AutoReorient();
				Tap(Key::R);
				bool off = app->AutoReorient();
				Result("view.reorient", on != before && off == before, cs::Cat(B(before), " -> ", B(on), " -> ", B(off)));
			}

			cs::String s0 = app->Sky->Current()->Name;
			Tap(Key::K, { Key::LeftShift });
			cs::String s1 = app->Sky->Current()->Name;
			Tap(Key::K, { Key::LeftShift, Key::LeftAlt });
			cs::String s2 = app->Sky->Current()->Name;
			Result("view.sky", s1 != s0 && s2 == s0, cs::Cat("Shift+K '", s0, "' -> '", s1, "', Shift+Alt+K -> '", s2, "'"));
		}

		// ---------------------------------------------------------------- screens and panels

		void ScreenProbes()
		{
			UnitySpaceApp& a = *app;
			CloseEverything();
			Toggle("ui.help", Key::H, a.ShowHelp);
			Toggle("ui.help-f1", Key::F1, a.ShowHelp);
			Toggle("ui.hud", Key::U, a.ShowHud);
			Toggle("ui.clear-screen", Key::F11, a.ScreenCleared);
			Toggle("ui.market", Key::M, a.ShowMarket);
			Toggle("ui.outfit", Key::F2, a.ShowOutfit);
			Toggle("ui.bounties", Key::F4, a.ShowBounties);
			Toggle("ui.contracts", Key::F7, a.ShowContracts);
			Toggle("ui.fleet", Key::F10, a.ShowFleet);
			Toggle("ui.trade-desk", Key::Backslash, a.ShowTrade);
			Toggle("ui.galaxy-map", Key::LeftBracket, a.ShowGalaxyMap);
			Toggle("ui.news", Key::X, a.ShowNews);
			Toggle("ui.spawn", Key::V, a.ShowSpawn);
			Toggle("ui.saves", Key::F6, a.ShowSaves);
			Toggle("ui.picker", Key::F9, a.ShowPicker);
			CloseEverything();

			int presses = 0;
			while (Time::timeScale != 0 && presses < 6) { Tap(Key::Escape); presses++; }
			bool opened = Time::timeScale == 0;
			Tap(Key::Escape);
			bool resumed = Time::timeScale != 0;
			Result("ui.pause-menu", opened && resumed, cs::Cat("Esc x", presses, " opened: ", B(opened), ", Esc resumed: ", B(resumed)));
			CloseEverything();
		}

		// ---------------------------------------------------------------- navigation

		/// Runs frames until the camera's flight to a focus ends (at most 300).
		void Settle()
		{
			for (int i = 0; i < 300 && app->SpaceCamera->Mode == CameraMode::Travel; i++) frames(1);
		}

		void NavProbes()
		{
			Tap(Key::Home);
			Settle();
			Result("nav.home", app->Selected.IsBody() && app->Selected.Body == 0 && app->SpaceCamera->Mode == CameraMode::Focus,
				cs::Cat("Home: selected ", Describe(app->Selected), ", camera ", Cameras::ToString(app->SpaceCamera->Mode)));

			Tap(Key::Digit3);
			cs::String sel = Describe(app->Selected);
			Tap(Key::F);
			Settle();
			Result("nav.number-focus", sel == "body Earth" && app->SpaceCamera->Mode == CameraMode::Focus && Describe(app->SpaceCamera->FocusTarget) == "body Earth",
				cs::Cat("3: ", sel, "; F: camera ", Cameras::ToString(app->SpaceCamera->Mode), " on ", Describe(app->SpaceCamera->FocusTarget)));

			Tap(Key::Tab);
			cs::String first = Describe(app->Selected);
			Tap(Key::Tab);
			cs::String second = Describe(app->Selected);
			Result("nav.cycle-ships", app->Selected.IsShip() && first != second, cs::Cat("Tab: ", first, ", Tab: ", second));

			cs::String toast = Toast();
			CameraMode mode = app->SpaceCamera->Mode;
			cs::String focus = Describe(app->SpaceCamera->FocusTarget);
			Tap(Key::B);
			frames(10);
			bool handled = Toast() != toast || app->SpaceCamera->Mode != mode || Describe(app->SpaceCamera->FocusTarget) != focus;
			Result("nav.watch-battle", handled, cs::Cat("B: camera ", Cameras::ToString(app->SpaceCamera->Mode), " on ", Describe(app->SpaceCamera->FocusTarget), "; toast '", Toast(), "'"));
			CloseEverything();
		}

		// ---------------------------------------------------------------- saves

		void SaveProbes()
		{
			cs::String toast = Toast();
			Tap(Key::F5);
			cs::String saved = Toast();
			double jd = app->Clock->Jd;
			frames(30);
			Tap(Key::F8);
			frames(2);
			Result("saves.quicksave", saved != toast && Contains(saved, "save"), cs::Cat("F5: toast '", saved, "'"));
			Result("saves.quickload", cs::Math::Abs(app->Clock->Jd - jd) * 86400 < 3600,
				cs::Cat("F8: JD back to ", cs::Fmt(app->Clock->Jd, "F6"), " (saved ", cs::Fmt(jd, "F6"), "); toast '", Toast(), "'"));
			CloseEverything();
		}

		// ---------------------------------------------------------------- helm

		const cs::Ref<Ship>& PilotedShip() { return app->Piloted.Ship; }
		cs::String PilotedName() { return PilotedShip() ? PilotedShip()->Name : cs::String(); }

		bool SelectAShip()
		{
			for (int i = 0; i < 12 && !app->Selected.IsShip(); i++) Tap(Key::Tab);
			return app->Selected.IsShip();
		}

		void HelmProbes()
		{
			if (!SelectAShip()) { Result("helm.pilot", false, "Tab found no ship to select"); return; }
			cs::String name = app->Selected.Ship->Name;
			Tap(Key::P);
			frames(5);
			Result("helm.pilot", app->IsPiloting() && PilotedName() == name,
				cs::Cat("P on ", name, ": piloting ", B(app->IsPiloting()), " (", PilotedName(), "); toast '", Toast(), "'"));
			if (!app->IsPiloting()) return;

			DVec3 from = app->SpaceCamera->ScenePosition;
			kb->Hold(Key::W);
			frames(60);
			kb->Release(Key::W);
			frames(1);
			double moved = (app->SpaceCamera->ScenePosition - from).Length();
			Result("helm.thrust", moved > 0, cs::Cat("W held 60 frames: chase camera moved ", cs::Fmt(moved, "G4"), " units"));

			cs::String toast = Toast();
			Tap(Key::F);
			cs::String fa = Toast();
			Tap(Key::F);
			Result("helm.flight-assist", fa != toast && Contains(fa, "assist"), cs::Cat("F: toast '", fa, "', F: '", Toast(), "'"));

			bool silent = PilotedShip()->SilentRunning;
			Tap(Key::F3);
			bool s1 = PilotedShip()->SilentRunning;
			Tap(Key::F3);
			Result("helm.silent-running", s1 != silent && PilotedShip()->SilentRunning == silent,
				cs::Cat("F3: ", B(silent), " -> ", B(s1), " -> ", B(PilotedShip()->SilentRunning)));

			Toggle("helm.cargo", Key::I, app->ShowCargo);

			Tap(Key::Escape);
			bool menu = Time::timeScale == 0;
			Tap(Key::Escape);
			Result("helm.esc-pause-menu", menu && Time::timeScale != 0 && app->IsPiloting(),
				cs::Cat("Esc at the helm opened the pause menu: ", B(menu), "; still piloting: ", B(app->IsPiloting())));
			CloseEverything();

			Tap(Key::P);
			frames(5);
			Result("helm.release", !app->IsPiloting(), cs::Cat("P again: piloting ", B(app->IsPiloting()), "; toast '", Toast(), "'"));
		}

		void KeyToast(const cs::String& id, Key k, std::initializer_list<Key> mods = {})
		{
			cs::String toast = Toast();
			Tap(k, mods);
			// No new toast is not a failure: the original's handler can be silent for this hull (no lane left, no
			// phasers, nothing to lock); logged as INFO for comparing the two tracks' logs line by line.
			if (Toast() != toast) Result(id, true, cs::Cat(Input::ToString(k), ": toast '", Toast(), "'"));
			else Log(cs::Cat("AUDIT ", id, " INFO: ", Input::ToString(k), ": no new toast ('", Toast(), "')"));
		}

		void PracticeProbes()
		{
			Tap(Key::V, { Key::LeftShift });
			frames(5);
			Result("helm.practice-fighter", app->IsPiloting() && PilotedShip() && Contains(PilotedShip()->Name, "Practice"),
				cs::Cat("Shift+V: piloting ", B(app->IsPiloting()), " (", PilotedName(), " ", PilotedShip() ? ToString(PilotedShip()->Spec->Class) : "", "); toast '", Toast(), "'"));
			if (app->IsPiloting())
			{
				cs::String toast = Toast();
				Tap(Key::PageDown);
				Result("helm.ftl-plot", Toast() != toast || app->PlottedCourse != nullptr,
					cs::Cat("PgDn: course ", app->PlottedCourse ? "plotted" : "none", "; toast '", Toast(), "'"));
				// Keys the helm answers with a toast (a refusal included): the binding reaches the original's handler.
				KeyToast("helm.ftl-engage", Key::Enter);
				KeyToast("helm.ftl-autopilot", Key::End);
				KeyToast("helm.lane-reenter", Key::Y);
				KeyToast("helm.phasers", Key::Insert);
				KeyToast("helm.run-hot", Key::Insert, { Key::LeftShift });
				KeyToast("helm.lock", Key::T);
				Tap(Key::P);
				frames(5);
			}
			CloseEverything();
		}

		// ---------------------------------------------------------------- seats (take charge)

		void SeatProbes()
		{
			if (!SelectAShip()) { Result("seats.man-gun", false, "Tab found no ship to select"); return; }
			cs::String name = app->Selected.Ship->Name;
			cs::String toast = Toast();
			Tap(Key::Slash);
			frames(5);
			bool seated = app->IsSeated();
			Result("seats.man-gun", seated || Toast() != toast, cs::Cat("/ on ", name, ": seated ", B(seated), "; toast '", Toast(), "'"));
			if (seated)
			{
				Tap(Key::Backspace);
				frames(5);
				Result("seats.up-to-helm", app->IsPiloting() && !app->IsSeated(),
					cs::Cat("Backspace: piloting ", B(app->IsPiloting()), ", seated ", B(app->IsSeated()), "; toast '", Toast(), "'"));
				Tap(Key::P);
				frames(5);
			}
			Result("seats.back-to-command", !app->AtControls(), cs::Cat("P: at controls ", B(app->AtControls())));

			toast = Toast();
			Tap(Key::Quote);
			frames(5);
			Result("seats.take-charge", app->AtControls() || Toast() != toast, cs::Cat("': at controls ", B(app->AtControls()), "; toast '", Toast(), "'"));
			if (app->AtControls()) { Tap(Key::P); frames(5); }
			CloseEverything();
		}

		// ---------------------------------------------------------------- diplomacy

		void DiplomacyProbes()
		{
			if (!SelectAShip()) { Result("diplomacy.hail", false, "Tab found no ship to select"); return; }
			bool hailed = false;
			for (int i = 0; i < 30 && !hailed; i++)
			{
				Tap(Key::F12);
				frames(2);
				hailed = app->ShowGalaxyMap || !StartsWith(Toast(), "Nobody to hail");
				if (!hailed) Tap(Key::Tab);
			}
			Result("diplomacy.hail", hailed, cs::Cat("F12 on ", Describe(app->Selected), ": galaxy map ", B(app->ShowGalaxyMap), "; toast '", Toast(), "'"));
			CloseEverything();
			Tap(Key::Escape);
			CloseEverything();
		}

		void Probe(const char* group, void (*body)())
		{
			try { body(); }
			catch (const std::exception& e) { Result(cs::Cat(group, ".exception"), false, e.what()); }
		}
	}

	void Run(UnitySpaceApp& theApp, ScriptedDevice& script, const std::function<void(int)>& stepFrames)
	{
		app = &theApp;
		kb = &script;
		frames = stepFrames;
		pass = fail = 0;
		Probe("audio", [] { AudioProbes(Result, frames); }); // US-T50
		Probe("render", [] { RenderProbes(Result, frames); }); // US-T51
		Probe("time", TimeProbes);
		Probe("view", ViewProbes);
		Probe("screens", ScreenProbes);
		Probe("nav", NavProbes);
		Probe("saves", SaveProbes);
		Probe("helm", HelmProbes);
		Probe("practice", PracticeProbes);
		Probe("seats", SeatProbes);
		Probe("diplomacy", DiplomacyProbes);
		Probe("text", [] { TextEditProbes(Result); }); // US-T52
		Probe("viewer", [] { ModelViewerProbes(Result, frames); }); // US-T53
		Log(cs::Cat("AUDIT done: ", pass, " PASS, ", fail, " FAIL"));
	}
}
