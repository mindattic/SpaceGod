#include "Smoke/FeatureAudit.h"
#include "Smoke/ScriptedDevice.h"
#include "Smoke/FeatureAuditDisplay.h"
#include "Smoke/FeatureAuditSettings.h"
#include "Smoke/FeatureAuditEditing.h"

#include "Bootstrap.h"
#include "Target.h"
#include "UnitySpaceApp.h"
#include "SpaceGodLog.h"
#include "Camera/SpaceCamera.h"
#include "Combat/FlightModels.h"
#include "Input/InputDevice.h"
#include "UnityCompat/UnityRuntime.h"
#include "Views/SkyView.h"
#include "Rendering/ShipModels.h"
#include "UnitySpaceAppActor.h"
#include "UnrealSpace.h"
#include "UnityCompat/UnityScene.h"
#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/ModularHull.h"
#include "Combat/War.h"
#include "Systems/FtlNavigation.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/HelmFtl.h"
#include "Systems/JumpGate.h"
#include "Rendering/Skyboxes.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <limits>

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

		// ---------------------------------------------------------------- swim (US-T221: US-T184, swim never hooks on a planet)

		int BodyIndex(const char* name);

		struct SwimRun
		{
			double startAlt = 0, endAlt = 0, minAlt = 1e300, moved = 0, commanded = 0, worstRatio = 1e300, minSpeedFrac = 1e300;
			int frames = 0;
		};

		/// Holds W in the free camera from a pose beside a body, `n` frames, measuring altitude above the displayed surface each
		/// frame against the swim rate the camera itself commands (CurrentSpeed). From frame `warm` on (the velocity eases up to
		/// the commanded one first) each frame must make at least its share of that rate along `along`.
		SwimRun SwimFrom(int body, DVec3 normal, DVec3 along, double startAlt, int n, int warm)
		{
			SwimRun r;
			double radius = app->Scale()->DisplayRadius((*app->Catalog())[body]);
			double dt = Time::deltaTime > 0 ? Time::deltaTime : 1.0 / 60;
			DVec3 centre = app->TargetScene(Target::OfBody(body));
			DVec3 pos = centre + normal * (radius + startAlt);
			auto fwd = Vector3(static_cast<float>(along.X), static_cast<float>(along.Y), static_cast<float>(along.Z));
			app->SpaceCamera->SetPose(pos, Quaternion::LookRotation(fwd, Vector3::up()));
			r.startAlt = startAlt;
			DVec3 last = app->SpaceCamera->ScenePosition - centre; // relative to the body: it rides its orbit (and the warp) under the camera
			kb->Hold(Key::W);
			for (int i = 0; i < n; i++)
			{
				frames(1);
				dt = Time::deltaTime > 0 ? Time::deltaTime : dt;
				DVec3 now = app->SpaceCamera->ScenePosition - app->TargetScene(Target::OfBody(body));
				double alt = now.Length() - radius;
				r.minAlt = cs::Math::Min(r.minAlt, alt);
				r.endAlt = alt;
				if (i >= warm)
				{
					double step = (now - last).Length(), want = app->SpaceCamera->CurrentSpeed * dt;
					r.moved += step;
					r.commanded += want;
					if (want > 0) r.worstRatio = cs::Math::Min(r.worstRatio, step / want);
				}
				last = now;
				r.frames++;
			}
			kb->Release(Key::W);
			frames(1);
			return r;
		}

		DVec3 AwayFrom(int body, int other)
		{
			DVec3 d = app->TargetScene(Target::OfBody(body)) - app->TargetScene(Target::OfBody(other));
			return d.LengthSquared() > 0 ? d.Normalized() : DVec3::UnitY;
		}

		void SwimProbes()
		{
			CloseEverything();
			app->SpaceCamera->ExitFocus();
			int earth = BodyIndex("Earth"), moon = BodyIndex("Moon");
			if (earth < 0 || moon < 0) { Result("swim.escape", false, "no Earth or Moon in the catalog"); return; }
			DVec3 savedPos = app->SpaceCamera->ScenePosition;
			Quaternion savedRot = app->SpaceCamera->Rotation;
			double goal0 = app->ScaleGoal;
			constexpr int N = 180, Warm = 30;
			for (double goal : { 1.0, 0.0 })
			{
				if (app->ScaleGoal != goal) Tap(Key::G);
				for (int i = 0; i < 900 && cs::Math::Abs(app->Scale()->Blend() - app->ScaleGoal) > 1e-9; i++) frames(1);
				cs::String scale = goal > 0.5 ? "gameplay" : "realistic";
				for (int body : { earth, moon })
				{
					int other = body == earth ? moon : earth;
					cs::String name = (*app->Catalog())[body]->Name;
					double radius = app->Scale()->DisplayRadius((*app->Catalog())[body]);
					double startAlt = radius * 0.01;
					DVec3 up = AwayFrom(body, other);
					// Straight away from the skin: altitude must climb steadily and many times over, never stalling.
					{
						SwimRun r = SwimFrom(body, up, up, startAlt, N, Warm);
						bool ok = r.endAlt > r.startAlt * 5 && r.worstRatio >= 0.5 && r.minAlt >= startAlt * 0.99;
						Result(cs::Cat("swim.escape.", scale, ".", name), ok,
							cs::Cat("W held ", r.frames, " frames from ", cs::Fmt(startAlt, "G4"), " units above ", name, ": altitude ", cs::Fmt(r.endAlt, "G4"), " (x",
								cs::Fmt(r.endAlt / startAlt, "F1"), "), moved ", cs::Fmt(r.moved, "G4"), " of ", cs::Fmt(r.commanded, "G4"), " commanded, worst frame ",
								cs::Fmt(r.worstRatio, "F2"), " of its commanded step, lowest altitude ", cs::Fmt(r.minAlt, "G4")));
					}
					// Along the skin: keeps swimming, whatever the push, and never ends inside the body.
					{
						DVec3 t = DVec3::Cross(up, DVec3::UnitY);
						if (t.LengthSquared() < 1e-6) t = DVec3::Cross(up, DVec3::UnitX);
						t = t.Normalized();
						SwimRun r = SwimFrom(body, up, t, startAlt, N, Warm);
						bool ok = r.commanded > 0 && r.moved >= r.commanded * 0.8 && r.worstRatio >= 0.5 && r.minAlt >= -1e-9 * radius;
						Result(cs::Cat("swim.skim.", scale, ".", name), ok,
							cs::Cat("W held ", r.frames, " frames along the surface of ", name, " from ", cs::Fmt(startAlt, "G4"), " units up: moved ", cs::Fmt(r.moved, "G4"),
								" of ", cs::Fmt(r.commanded, "G4"), " commanded, worst frame ", cs::Fmt(r.worstRatio, "F2"), " of its commanded step, lowest altitude ",
								cs::Fmt(r.minAlt, "G4"), " (radius ", cs::Fmt(radius, "G4"), ")"));
					}
				}
			}
			if (app->ScaleGoal != goal0) Tap(Key::G);
			for (int i = 0; i < 900 && cs::Math::Abs(app->Scale()->Blend() - app->ScaleGoal) > 1e-9; i++) frames(1);
			app->SpaceCamera->SetPose(savedPos, savedRot);
			frames(2);
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
			// US-T222: a hull out flying free, not one on a hyperlane (its helm rides the lane on, where F and F3 are the ride's)
			// or in a fight: which the first Tab finds is the hour's traffic (the SFS Resolute is on the Venus-Earth lane at some).
			for (int i = 0; i < 12 && app->Selected.IsShip() && (app->Selected.Ship->Phase == ShipPhase::Lane || app->Selected.Ship->Battle != nullptr); i++)
				Tap(Key::Tab);
			if (!app->Selected.IsShip() && !SelectAShip()) { Result("helm.pilot", false, "Tab found no ship to select"); return; }
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

		// ---------------------------------------------------------------- input ownership (US-T90)

		/// One owner per input per frame: at the helm the stick, the look and F are the craft's (the camera only follows
		/// it), in command they are the camera's; and F pressed in the very frame P takes a helm is spent on the command
		/// view's focus, not also on the new helm's flight assist.
		///
		/// US-T118: the hull is the probe's own, staged a fighter 1 AU above the star, out of every lane, fight and orbit:
		/// the ship left selected by the probes before (after the focus probes' months at warp) could be riding a lane (where
		/// F is not the flight assist's), in a fight or killed under the helm, and the probe measured that instead.
		cs::Ref<Ship> StageAtHelm(const cs::String& name, ShipClass cls, int body, DVec3 offsetKm, DVec3 forward);

		void InputOwnershipProbes()
		{
			CloseEverything();
			auto ship = StageAtHelm("Audit Input Fighter", ShipClass::Fighter, app->Catalog()->Root()->Index, DVec3(0, 0, Astro::AuKm), DVec3::UnitY);
			if (!ship) { Result("input.camera-follows", false, cs::Cat("P on the staged fighter: piloting ", B(app->IsPiloting()))); return; }
			Settle();
			const auto& cam = app->SpaceCamera;
			auto hull = Target::OfShip(PilotedShip());
			// F at the helm, before anything is flown: flight assist only, the chase stays on the hull.
			bool assist = app->PilotFlightAssist();
			Tap(Key::F);
			bool afterF = app->PilotFlightAssist();
			bool toggled = afterF != assist;
			bool stillChase = cam->PilotLocked && cam->FocusTarget == hull;
			Tap(Key::F);

			double d0 = (cam->ScenePosition - app->TargetScene(hull)).Length();
			// W, the right button and the mouse all at once: the hull's burn, its second trigger and its aim, none the camera's.
			kb->Hold(Key::W);
			kb->HoldButton(Input::MouseButton::Right);
			for (int i = 0; i < 40; i++) { kb->mouseDelta = Vector2(30, 12); frames(1); }
			kb->ReleaseButton(Input::MouseButton::Right);
			kb->Release(Key::W);
			frames(1);
			// The chase is placed before the hull's view moves in a frame, so it may trail by one frame's travel of the hull;
			// a camera swimming on the keys would be off by far more than that.
			DVec3 hullBefore = app->TargetScene(hull);
			frames(1);
			double step = (app->TargetScene(hull) - hullBefore).Length();
			double d1 = (cam->ScenePosition - app->TargetScene(hull)).Length();
			double r = app->TargetRadius(hull);
			bool follows = cam->Mode != CameraMode::Free && cam->PilotLocked && cam->FocusTarget == hull;
			// The chase sits at most 20 hull radii back (its widest zoom), a little lifted: never further, however long the
			// keys are held.
			Result("input.camera-follows", app->IsPiloting() && follows && d1 <= r * 20 * 1.3 + step * 2 + 1e-9,
				cs::Cat("W + right-drag at the helm of the ", PilotedName(), ": camera ", Cameras::ToString(cam->Mode), " locked ", B(cam->PilotLocked),
					" on ", Describe(cam->FocusTarget), ", ", cs::Fmt(d0, "G4"), " -> ", cs::Fmt(d1, "G4"), " from the hull (radius ", cs::Fmt(r, "G4"),
					", its step a frame ", cs::Fmt(step, "G4"), ")"));

			Tap(Key::P);
			frames(3);
			// F and P in the same frame from command: F is the command view's (a focus), so the helm P takes starts with its
			// flight assist on, as every helm taken does.
			app->Select(Target::OfShip(ship));
			kb->Hold(Key::F);
			kb->Tap(Key::P);
			frames(1);
			kb->Release(Key::F);
			frames(2);
			bool sameFrame = app->IsPiloting() && app->PilotFlightAssist();
			Result("input.assist-vs-focus", toggled && stillChase && sameFrame,
				cs::Cat("F at the helm: assist ", B(assist), " -> ", B(afterF), ", chase kept ", B(stillChase),
					"; F+P in one frame: piloting ", B(app->IsPiloting()), ", assist on ", B(app->PilotFlightAssist())));
			for (int i = 0; i < 3 && app->AtControls(); i++) { Tap(Key::P); frames(3); }
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

		// ---------------------------------------------------------------- US-T58: staging

		cs::String F(double v, const char* f) { return cs::Fmt(v, f); }

		/// Releases whatever is at the controls (P), so the next probe starts from command.
		void ToCommand()
		{
			for (int i = 0; i < 3 && app->AtControls(); i++) { Tap(Key::P); frames(3); }
			CloseEverything();
		}

		// ---------------------------------------------------------------- US-T222: traffic beside a helm

		int DetachedModules(const cs::Ref<Ship>& s)
		{
			int n = 0;
			if (s != nullptr && s->Combat != nullptr)
				for (const auto& m : s->Combat->Structure()->Modules)
					if (m->Detached) n++;
			return n;
		}

		/// A helm taken in a parked ship's path does it no harm (US-T222): a battleship on an orbit at 1.1 km/s, 4 km short of
		/// the spot, and a second one parked there whose helm is taken at once and held still, as the audit's own P on the
		/// SFS Resolute once was beside the SFS Dauntless (which broke it up). Ten seconds on, the passer has gone by: the
		/// fleet must have kept it off the held hull, and neither may have lost hull or a section.
		void TrafficProbes()
		{
			ToCommand();
			int earth = BodyIndex("Earth");
			if (earth < 0) { Result("traffic.no-collision-damage", false, "no Earth"); return; }
			double jd = app->Clock->Jd;
			DVec3 at(0, 0, 60000);
			constexpr double speedKmS = 1.1, leadKm = 4, radiusKm = 2000;
			auto passer = app->Fleet()->SpawnAt("Audit Passing Warship", ShipClass::Battleship, app->World()->Sol, earth, at, DVec3::UnitY, jd);
			passer->Ports = app->World()->FreeShipPorts();
			// Its orbit: at phase 0 it is `leadKm` short of `at`, heading (V) straight for it.
			auto orbit = passer->Orbit;
			orbit->U = DVec3::UnitX;
			orbit->V = DVec3::UnitY;
			orbit->RadiusKm = radiusKm;
			orbit->Center = at - DVec3::UnitY * leadKm - DVec3::UnitX * radiusKm;
			orbit->EpochJd = jd;
			orbit->PhaseAtEpoch = 0;
			orbit->RateRadPerDay = speedKmS * Astro::SecondsPerDay / radiusKm;
			auto ship = StageAtHelm("Audit Held Warship", ShipClass::Battleship, earth, at, DVec3::UnitX);
			if (!ship) { Result("traffic.no-collision-damage", false, cs::Cat("P on the staged battleship: piloting ", B(app->IsPiloting()))); return; }
			double hullShip = ship->Combat->HullFraction(), hullPasser = passer->Combat->HullFraction();
			int lostShip = DetachedModules(ship), lostPasser = DetachedModules(passer);
			double reach = 0.5 * (ship->LengthKm() + passer->LengthKm());
			double closestPlan = 1e300, closest = 1e300;
			for (int f = 0; f < 600 && app->IsPiloting() && !passer->Destroyed; f++)
			{
				frames(1);
				double now = app->Clock->Jd;
				// (Where the helm has the hull, not the spot its schedule was pinned at.)
				DVec3 held = app->Ephemeris()->Helio(app->PilotFrameBody()) + app->PilotOffsetKm();
				DVec3 plan = app->Ephemeris()->Helio(earth) + orbit->OffsetAt(now);
				closestPlan = cs::Math::Min(closestPlan, (plan - held).Length());
				closest = cs::Math::Min(closest, (app->Fleet()->StateOf(passer, now).HelioKm - held).Length());
			}
			bool piloting = app->IsPiloting() && PilotedShip() == ship;
			bool clean = !ship->Destroyed && !passer->Destroyed && ship->Combat->HullFraction() >= hullShip && passer->Combat->HullFraction() >= hullPasser &&
				DetachedModules(ship) == lostShip && DetachedModules(passer) == lostPasser;
			Result("traffic.no-collision-damage", piloting && clean && closestPlan < reach && closest >= reach,
				cs::Cat("helm of the ", ship->Name, " taken in the path of the ", passer->Name, " (", F(speedKmS, "0.0"), " km/s, ", F(leadKm, "0"), " km short): its plan passed ",
					F(closestPlan * 1000, "0"), " m from the held hull, it flew by ", F(closest * 1000, "0"), " m off (hulls ", F(reach * 1000, "0"), " m centre to centre); piloting ", B(piloting),
					"; hull ", F(hullShip * 100, "0.0"), "% -> ", F(ship->Combat->HullFraction() * 100, "0.0"), "%, passer ", F(hullPasser * 100, "0.0"), "% -> ",
					F(passer->Combat->HullFraction() * 100, "0.0"), "%; sections lost ", DetachedModules(ship) - lostShip, " and ", DetachedModules(passer) - lostPasser,
					"; toast '", Toast(), "'"));
			ToCommand();
			// The passer stops where it is, out of the way of the probes after.
			orbit->Center = orbit->OffsetAt(app->Clock->Jd);
			orbit->RadiusKm = 0;
			orbit->RateRadPerDay = 0;
		}

		/// Stages a hull for a probe: a home-side `cls` parked at `offsetKm` from `body`, nose on `forward` (Fleet.SpawnAt,
		/// as Shift+V and the spawn grid make theirs), then selected and taken with P. The flying from there is the player's keys.
		cs::Ref<Ship> StageAtHelm(const cs::String& name, ShipClass cls, int body, DVec3 offsetKm, DVec3 forward)
		{
			ToCommand();
			auto ship = app->Fleet()->SpawnAt(name, cls, app->World()->Sol, body, offsetKm, forward, app->Clock->Jd);
			ship->Ports = app->World()->FreeShipPorts();
			app->Select(Target::OfShip(ship));
			Tap(Key::P);
			frames(2);
			return app->IsPiloting() && PilotedShip() == ship ? ship : cs::Ref<Ship>();
		}

		bool BattleAt(int body)
		{
			for (const auto& b : app->World()->War->Battles)
				if (!b->Over && b->Body == body) return true;
			return false;
		}

		// ---------------------------------------------------------------- US-T58: combat at the controls

		/// The helm's crosshair (where the mouse has swung the free aim; what the guns fire down).
		DVec3 PilotAim() { return app->PilotAimDirection(); }

		/// A hull's whole fight left in it: hull, shields and every subsystem's hit points.
		double Integrity(const cs::Ref<ShipCombat>& c)
		{
			double t = c->Hull + c->Shield();
			for (const auto& s : c->Subsystems) t += cs::Math::Max(0.0, s->Hp);
			return t;
		}

		/// From the piloted hull to `t`, unit: in the fight's frame once there is one.
		DVec3 DirectionTo(const cs::Ref<Ship>& me, const cs::Ref<Ship>& t)
		{
			const auto& b = me->Battle;
			auto mu = b ? b->UnitOf(me) : cs::Ref<BattleUnit>();
			auto tu = b ? b->UnitOf(t) : cs::Ref<BattleUnit>();
			if (mu && tu && tu->Present())
			{
				// Led as a pilot leads a crossing target: where it will be when a round at the guns' muzzle speed gets there.
				double muzzle = 0;
				for (const auto& s : mu->Combat->Subsystems)
					if (s->Def->Weapon && !s->Def->Weapon->Missile) muzzle = cs::Math::Max(muzzle, s->Def->Weapon->MuzzleSpeedKmS);
				DVec3 to = tu->Pos - mu->Pos;
				if (muzzle > 0) to += (tu->Vel - mu->Vel) * (to.Length() / muzzle);
				return to.Normalized();
			}
			return (app->Fleet()->StateOf(t, app->Clock->Jd).HelioKm - app->PlayerHelioKm()).Normalized();
		}

		double AngleDeg(DVec3 a, DVec3 b)
		{
			return cs::Math::Acos(cs::Math::Max(-1.0, cs::Math::Min(1.0, DVec3::Dot(a.Normalized(), b.Normalized())))) * 180 / cs::Math::PI;
		}

		/// One frame of the mouse swinging the crosshair toward `want`: the error across the aim's right and up axes, turned
		/// into pixels at the look sensitivity (half of it a frame), the signs those the calibration found.
		void SteerMouse(DVec3 want, int signX, int signY)
		{
			DVec3 aim = PilotAim().Normalized();
			DVec3 right = DVec3::Cross(aim, app->PilotUp()).Normalized();
			DVec3 up = DVec3::Cross(right, aim).Normalized();
			double perPx = Cameras::SpaceCamera::LookSensitivity() * cs::Math::PI / 180;
			double x = cs::Math::Max(-120.0, cs::Math::Min(120.0, 0.5 * DVec3::Dot(want, right) / perPx));
			double y = cs::Math::Max(-120.0, cs::Math::Min(120.0, 0.5 * DVec3::Dot(want, up) / perPx));
			kb->mouseDelta = Vector2(static_cast<float>(signX * x), static_cast<float>(signY * y));
			frames(1);
		}

		void CombatProbes()
		{
			ToCommand();
			Tap(Key::V, { Key::LeftShift });
			frames(3);
			cs::Ref<Ship> me = app->IsPiloting() ? PilotedShip() : cs::Ref<Ship>();
			if (!me) { Result("combat.mouse-aim", false, "Shift+V: no practice fighter at the helm"); return; }
			// A pirate freighter 1.5 km out, some 18 degrees off the nose (spawned as the spawn grid's call does).
			double jd = app->Clock->Jd;
			DVec3 fwd = app->PilotForward();
			DVec3 side = DVec3::Cross(fwd, app->PilotUp()).Normalized();
			auto target = app->Fleet()->SpawnAt("Audit Target", ShipClass::Freighter, app->World()->Pirates, app->PilotFrameBody(),
				app->PilotOffsetKm() + fwd * 1.5 + side * 0.5, fwd, jd);
			target->Ports = app->World()->FreeShipPorts();
			frames(2);

			// Which way the mouse swings the crosshair: one frame of each axis.
			DVec3 a0 = PilotAim().Normalized();
			DVec3 r0 = DVec3::Cross(a0, app->PilotUp()).Normalized();
			DVec3 u0 = DVec3::Cross(r0, a0).Normalized();
			kb->mouseDelta = Vector2(20, 0);
			frames(1);
			int signX = DVec3::Dot(PilotAim() - a0, r0) >= 0 ? 1 : -1;
			DVec3 a1 = PilotAim().Normalized();
			kb->mouseDelta = Vector2(0, 20);
			frames(1);
			int signY = DVec3::Dot(PilotAim() - a1, u0) >= 0 ? 1 : -1;

			double before = AngleDeg(PilotAim(), DirectionTo(me, target));
			int steps = 0;
			while (steps < 180 && AngleDeg(PilotAim(), DirectionTo(me, target)) > 0.5) { SteerMouse(DirectionTo(me, target), signX, signY); steps++; }
			double after = AngleDeg(PilotAim(), DirectionTo(me, target));
			Result("combat.mouse-aim", after < 1, cs::Cat("mouse: crosshair ", F(before, "F1"), " -> ", F(after, "F2"), " deg off the ", target->Name, " in ", steps, " frames"));

			// US-T112: the trigger is probed in a fight the probe lays out itself, not one the click above would open wherever
			// the practice fighter happens to be (TryStartPlayerAttack there may start no fight, or put the two sides at
			// stations km apart for the AI to close): the dogfight demo (US-T88) seats the player in a fighter in a fresh fight
			// against four pirates, and the corvette among them is pinned 0.8 km dead ahead, inside the guns' reach, so all
			// that is left is the input: the left button held (in bursts, as a player does: held until the guns run hot, let go
			// until they cool) fires the guns down the crosshair, the mouse keeping it on the lead.
			app->StartDogfightDemo(false);
			frames(3);
			me = app->IsPiloting() ? PilotedShip() : cs::Ref<Ship>();
			auto mine = me && me->Battle ? me->Battle->UnitOf(me) : cs::Ref<BattleUnit>();
			cs::Ref<BattleUnit> foe;
			if (mine)
				for (const auto& u : me->Battle->Units)
					if (u->Ship && u->Side != mine->Side && u->Present() && (!foe || Contains(u->Ship->Name, "Corvette"))) foe = u;
			if (!foe)
			{
				Result("combat.mouse-fire", false, cs::Cat("dogfight demo: piloting ", B(me != nullptr), ", fight ", me && me->Battle ? "on" : "none", ", no enemy hull in it"));
				ToCommand();
				return;
			}
			target = foe->Ship;
			DVec3 ahead = mine->Forward.Normalized();
			auto pin = [&]()
			{
				auto u = me->Battle ? me->Battle->UnitOf(me) : cs::Ref<BattleUnit>();
				auto t = me->Battle ? me->Battle->UnitOf(target) : cs::Ref<BattleUnit>();
				if (!u || !t || !t->Present()) return;
				t->Pos = u->Pos + ahead * 0.8;
				t->Vel = u->Vel;
				t->Forward = -ahead;
			};
			pin();
			app->Select(Target::OfShip(target));
			Tap(Key::T);
			cs::String locked = Describe(app->SpaceCamera->PilotLockTarget);
			double whole = Integrity(foe->Combat ? foe->Combat : target->Combat), least = whole, heat = 0, worst = 0, range = 0;
			cs::String fireToast = Toast();
			int held = 0, frame = 0;
			bool trigger = false;
			double closest = std::numeric_limits<double>::max();
			for (; frame < 1200; frame++)
			{
				pin();
				auto unit = me->Battle ? me->Battle->UnitOf(me) : cs::Ref<BattleUnit>();
				bool hot = unit && (unit->Overheated || unit->Heat > 0.8);
				bool cool = !unit || unit->Heat < 0.3;
				bool want = unit && (trigger ? !hot : cool);
				if (trigger && !want) { kb->ReleaseButton(Input::MouseButton::Left); trigger = false; }
				else if (!trigger && want) { kb->HoldButton(Input::MouseButton::Left); trigger = true; }
				SteerMouse(DirectionTo(me, target), signX, signY);
				if (trigger) held++;
				unit = me->Battle ? me->Battle->UnitOf(me) : cs::Ref<BattleUnit>();
				auto tu = me->Battle ? me->Battle->UnitOf(target) : cs::Ref<BattleUnit>();
				if (unit)
				{
					heat = cs::Math::Max(heat, unit->Heat);
					if (trigger) worst = cs::Math::Max(worst, AngleDeg(unit->PlayerAim, DirectionTo(me, target)));
					if (tu) range = (tu->Pos - unit->Pos).Length();
					// The guns' rounds (this side's, in flight): how close any came to the target's hull.
					if (tu)
						for (const auto& r : me->Battle->Rounds)
							if (r->Side == unit->Side && !r->Gone) closest = cs::Math::Min(closest, cs::Math::Max(0.0, (r->Pos - tu->Pos).Length() - tu->Radius()));
				}
				least = cs::Math::Min(least, Integrity(tu && tu->Combat ? tu->Combat : target->Combat));
				if (target->Destroyed || (held > 120 && least < whole && closest < 0.1)) break;
				if (me->Destroyed || !app->IsPiloting() || !me->Battle) break;
			}
			if (trigger) kb->ReleaseButton(Input::MouseButton::Left);
			frames(1);
			Result("combat.mouse-fire", me->Battle != nullptr && heat > 0 && least < whole && closest < 0.1,
				cs::Cat("dogfight demo, T: locked ", locked, "; left button held ", held, " of ", cs::Math::Min(frame + 1, 1200), " frames: fight ", me->Battle ? "on" : "none", ", gun heat ", F(heat, "F2"), ", ",
					"crosshair within ", F(worst, "F2"), " deg of the lead, ", F(range, "F2"), " km off, rounds to ", F(closest == std::numeric_limits<double>::max() ? -1 : closest, "F3"), " km of its hull, the ", target->Name, " ", F(whole, "F0"), " -> ", F(least, "F0"),
					target->Destroyed ? " (destroyed)" : "", "; toast '", fireToast, "'"));
			ToCommand();
		}

		// ---------------------------------------------------------------- US-T58: the player's own transits

		/// A hyperlane (U.SPC-T41): a fighter staged just outside a lane's on-ramp ring, flown in on W, rides it (Fleet.JoinLane,
		/// the clock compressed) to the far ramp and comes off there.
		///
		/// US-T118: a hull in a fight is not let into a lane (UpdateLaneEntry), and the staged fighter can be drawn into one on
		/// its way to the ramp (once, the combat probe's own pirate freighter, left where the practice fighter was, struck its
		/// colours to it there). Then nothing is recorded: an INFO line says so and the next ramp clear of a fight is tried.
		void LaneProbes()
		{
			ToCommand();
			const auto& lanes = app->Fleet()->Lanes;
			cs::Ref<Lane> lane;
			cs::Ref<Ship> ship;
			int at = -1, to = -1, f = 0, tried = 0;
			bool riding = false;
			cs::String lost;
			for (const auto& l : lanes->Lanes)
			{
				if (!lanes->Carries(l) || l->IsGateSpur() || BattleAt(l->A) || BattleAt(l->B)) continue;
				for (int end = 0; end < 2 && !riding && tried < 4; end++)
				{
					int from = end == 0 ? l->A : l->B;
					double jd = app->Clock->Jd;
					DVec3 dir = lanes->RampDirection(l, from, jd);
					DVec3 ramp = lanes->RampOffset(l, from, jd);
					tried++;
					lane = l;
					at = from;
					to = end == 0 ? l->B : l->A;
					ship = StageAtHelm("Audit Lane Fighter", ShipClass::Fighter, at, ramp + dir * (Hyperlanes::GateRadiusKm + 0.5), -dir);
					if (!ship)
					{
						Result("transit.lane-enter", false, cs::Cat("P on the staged fighter: piloting ", B(app->IsPiloting())));
						return;
					}
					kb->Hold(Key::W);
					for (f = 0; f < 1800 && !app->RidingLane() && ship->Battle == nullptr && !ship->Destroyed; f++) frames(1);
					kb->Release(Key::W);
					frames(1);
					riding = app->RidingLane();
					if (riding || (ship->Battle == nullptr && !ship->Destroyed)) break;
					lost = cs::Cat("the fighter off the ", (*app->Catalog())[at]->Name, " ramp of the ", l->Name, " lane ", ship->Destroyed ? cs::String("destroyed") : cs::Cat("drawn into the ", ship->Battle->Name),
						" after ", f, " frames (toast '", Toast(), "')");
					Log(cs::Cat("AUDIT transit.lane-enter INFO: ", lost, "; trying another ramp"));
					ToCommand();
					ship = nullptr;
				}
				if (riding || ship || tried >= 4) break;
			}
			if (!ship && !riding)
			{
				Result("transit.lane-enter", false, lost.empty() ? cs::String("no lane carrying traffic, clear of a fight") : cs::Cat("every ramp tried was lost to a fight; last: ", lost));
				return;
			}
			Result("transit.lane-enter", riding,
				cs::Cat("W held ", f, " frames from ", F(Hyperlanes::GateRadiusKm + 0.5, "F1"), " km off the ", (*app->Catalog())[at]->Name, " ramp: riding ", B(riding), "; toast '", Toast(), "'"));
			if (!riding) { ToCommand(); return; }

			int ride = 0;
			for (; ride < 9000 && app->RidingLane(); ride++) frames(1);
			double off = (app->PlayerHelioKm() - lanes->RampHelio(lane, to, app->Clock->Jd)).Length();
			Result("transit.lane-ride", !app->RidingLane() && app->IsPiloting() && off < Hyperlanes::GateRadiusKm * 2 && StartsWith(Toast(), "Off the"),
				cs::Cat("the ", lane->Name, " lane ridden ", ride, " frames to ", (*app->Catalog())[to]->Name, ": ", F(off, "F0"), " km from its ramp, piloting ",
					B(app->IsPiloting()), "; toast '", Toast(), "'"));
			ToCommand();
		}

		/// A jump gate (U.SPC-T8): a fighter staged just outside a gate's activation range, across its spur (so it is not
		/// swept into the spur's ring), flown in on W ("In range of ... Y to activate") and through on Y (GateTransit.TryPerform,
		/// Activate on the far system, the helm taken again on the hull that came out).
		void GateProbes()
		{
			ToCommand();
			double jd = app->Clock->Jd;
			auto here = app->ActiveSystem;
			cs::Ref<JumpGate> gate;
			for (const auto& g : here->Gates())
				if (g->Destination() && g->Link && g->Link->Open(here, app->World()->Sol, jd)) { gate = g; break; }
			if (!gate) { Result("transit.gate", false, cs::Cat("no gate open to the ", app->World()->Sol->Name, " in ", here->Name)); return; }
			const auto& lanes = app->Fleet()->Lanes;
			DVec3 along = DVec3::UnitX;
			for (const auto& s : lanes->GateSpurs)
				if (s->Gate == gate) along = lanes->RampDirection(s, s->B, jd);
			DVec3 across = DVec3::Cross(along, cs::Math::Abs(along.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX).Normalized();
			int root = app->Catalog()->Root()->Index;
			DVec3 start = gate->PositionKm - app->Ephemeris()->Helio(root) + across * (JumpGate::ActivationRangeKm + 0.5);
			auto ship = StageAtHelm("Audit Gate Fighter", ShipClass::Fighter, root, start, -across);
			if (!ship)
			{
				Result("transit.gate", false, cs::Cat("P on the staged fighter: piloting ", B(app->IsPiloting())));
				return;
			}
			kb->Hold(Key::W);
			int f = 0;
			for (; f < 1800 && !StartsWith(Toast(), "In range of"); f++) frames(1);
			kb->Release(Key::W);
			frames(1);
			cs::String range = Toast();
			auto destination = gate->Destination();
			// Y; the toast read after the jump's own frame (the next one says the far gate is in range, as the hull comes out beside it).
			kb->Tap(Key::Y);
			frames(1);
			cs::String jumped = Toast();
			frames(5);
			bool there = app->ActiveSystem == destination && app->IsPiloting() && destination->World->Fleet->IsPresent(PilotedShip());
			Result("transit.gate", there && StartsWith(jumped, "Jumped to"),
				cs::Cat("W held ", f, " frames: '", range, "'; Y: in ", app->ActiveSystem->Name, " (bound for ", destination->Name, "), piloting ", B(app->IsPiloting()),
					" (", PilotedName(), ", in its fleet ", B(there), "); toast '", jumped, "', then '", Toast(), "'"));
			// Left at the helm: the FTL probe stages its hull beside where this one came out.
		}

		/// The helm's own FTL drive (U.SPC-T417, T420): an explorer at the helm, Page Down to plot the nearest star, End for
		/// the autopilot (out past the rim, onto the star, spooled and engaged), the crossing, and the drop-out at the far
		/// star with the helm back in hand.
		void FtlProbes()
		{
			// Beside the hull at the helm (the one the gate probe brought out), 50 km off; else 1 AU out from the star.
			auto from = app->ActiveSystem;
			int body = app->Catalog()->Root()->Index;
			DVec3 at(0, Astro::AuKm, 0);
			DVec3 fwd = DVec3::UnitY;
			if (app->IsPiloting())
			{
				body = app->PilotFrameBody();
				fwd = app->PilotForward();
				at = app->PilotOffsetKm() + DVec3::Cross(fwd, app->PilotUp()).Normalized() * 50;
			}
			auto ship = StageAtHelm("Audit Explorer", ShipClass::Explorer, body, at, fwd);
			if (!ship) { Result("transit.ftl-engage", false, cs::Cat("P on the staged explorer: piloting ", B(app->IsPiloting()))); return; }
			for (int i = 0; i < 12; i++)
			{
				Tap(Key::PageDown);
				const auto& c = app->PlottedCourse;
				if (c && c->Target && c->Target->System && !c->Target->IsBeacon() && c->Flyable()) break;
			}
			auto course = app->PlottedCourse;
			cs::String plotted = Toast();
			cs::Ref<StarSystem> bound = course && course->Target ? course->Target->System : cs::Ref<StarSystem>();
			Tap(Key::End);
			cs::String engaged = Toast();
			int f = 0;
			for (; f < 12000 && !app->InFtlCruise() && app->IsPiloting(); f++) frames(1);
			bool cruising = app->InFtlCruise();
			Result("transit.ftl-engage", cruising && bound != nullptr,
				cs::Cat("PgDn: '", plotted, "'; End: '", engaged, "'; ", f, " frames on: crossing ", B(cruising), "; toast '", Toast(), "'"));
			if (!cruising) { ToCommand(); return; }

			int c = 0;
			for (; c < 12000 && app->InFtlCruise(); c++) frames(1);
			frames(5);
			Result("transit.ftl-arrive", !app->InFtlCruise() && app->ActiveSystem == bound && app->IsPiloting() && PilotedShip() == ship && StartsWith(Toast(), "Dropped out of FTL"),
				cs::Cat(c, " frames between the stars from ", from->Name, ": in ", app->ActiveSystem->Name, " (bound for ", bound ? bound->Name : cs::String(), "), piloting ",
					B(app->IsPiloting()), " (", PilotedName(), "); toast '", Toast(), "'"));
			ToCommand();
		}

		// ---------------------------------------------------------------- focus (US-T78)

		/// Focuses one target as F does (FocusOn), lets the flight end and a few frames pass at the given warp, then checks
		/// in Unreal's own space, from the components actually drawn, that the model is where the camera's focus is: the
		/// middle of its drawn model (a body's sphere, a ship's hull without its plume or FX, boxed in the model's own
		/// frame) on the focus point (FocusScene - camera, through UnrealSpace), and dead ahead of the rendered camera.
		///
		/// US-T115: the flight and the model's streaming in run at real time, and only the last frames at the given warp, so
		/// as little game time passes as can be: at 1 month/s the system's wars go on around the probe, and a hull can be
		/// killed, land, or be drawn into a battle (whose BATTLE TIME drops the warp to real time for a focused hull,
		/// UnitySpaceApp::BattleTime) before it is measured. When <paramref name="lost"/> is given and that happens to a
		/// ship, nothing is recorded: the reason is put in it and the caller tries another hull.
		void FocusCheck(const cs::String& id, const Target& target, int warp, cs::String* lost = nullptr)
		{
			auto* host = AUnitySpaceAppActor::Find(GWorld);
			auto* camera = host ? host->FindComponentByClass<UCameraComponent>() : nullptr;
			if (!camera)
			{
				Result(id, false, "no app actor camera");
				return;
			}
			app->Clock->SetWarp(0);
			app->SpaceCamera->FocusOn(target);
			Settle();
			// A ship's model is streamed in once it is near (U.SPC-T414); a focused hull is always wanted.
			for (int i = 0; i < 1200 && app->ModelRenderersOf(target).Count() == 0; i++) frames(1);
			app->Clock->SetWarp(warp);
			frames(20);

			if (lost && target.IsShip() && target.Ship)
			{
				const auto& s = target.Ship;
				ShipPhase phase = s->Controller()->Phase;
				cs::String why;
				if (s->Destroyed || s->GateDeparted) why = "destroyed or gone";
				else if (phase == ShipPhase::Landed || phase == ShipPhase::Descent || phase == ShipPhase::Ascent) why = cs::Cat("planetside (", ToString(phase), ")");
				else if (s->Battle != nullptr) why = "drawn into a battle";
				else if (app->Clock->WarpIndex != warp) why = cs::Cat("warp dropped to ", app->Clock->Warp().Label);
				else if (!(app->SpaceCamera->FocusTarget == target)) why = cs::Cat("focus moved to ", Describe(app->SpaceCamera->FocusTarget));
				if (!why.empty())
				{
					*lost = cs::Cat(Describe(target), " ", why, " (toast '", Toast(), "')");
					return;
				}
			}

			FVector focus = UnrealSpace::ToUnreal(app->Relative(app->SpaceCamera->FocusTarget == target ? app->FocusScene(target) : app->TargetScene(target)));
			FQuat frame = UnrealSpace::ToUnreal(app->TargetRotation(target));
			double radiusCm = FMath::Max(app->TargetRadius(target) * UnrealSpace::CmPerUnit, 1e-9);
			FBox box(ForceInit);
			int drawn = 0;
			for (const auto& r : app->ModelRenderersOf(target))
			{
				UPrimitiveComponent* c = r ? UnityScene::ComponentOf(*r) : nullptr;
				if (!c || !c->IsVisible()) continue;
				drawn++;
				// The component's own box, corner by corner into the model's frame about the focus point.
				FBox local = c->CalcBounds(FTransform::Identity).GetBox();
				const FTransform& world = c->GetComponentTransform();
				for (int i = 0; i < 8; i++)
				{
					FVector corner((i & 1) ? local.Max.X : local.Min.X, (i & 2) ? local.Max.Y : local.Min.Y, (i & 4) ? local.Max.Z : local.Min.Z);
					box += frame.UnrotateVector(world.TransformPosition(corner) - focus);
				}
			}
			if (drawn == 0 || !box.IsValid)
			{
				cs::String why;
				if (target.IsShip() && target.Ship)
				{
					const auto& s = target.Ship;
					why = cs::Cat("; phase ", ToString(s->Phase), "/", ToString(s->Controller()->Phase), ", destroyed ", B(s->Destroyed), ", gone ", B(s->GateDeparted),
						", battle ", B(s->Battle != nullptr), ", view ", B(app->ShipModelShown(s, 1e12)), ", camera ", Cameras::ToString(app->SpaceCamera->Mode),
						" on ", Describe(app->SpaceCamera->FocusTarget), ", warp ", app->Clock->Warp().Label, ", toast '", Toast(), "'");
				}
				Result(id, false, cs::Cat(Describe(target), ": nothing drawn (", app->ModelRenderersOf(target).Count(), " renderers)", why));
				return;
			}
			FVector centre = focus + frame.RotateVector(box.GetCenter());
			double off = FVector::Dist(centre, focus) / radiusCm;
			// Where the model's root is (TargetScene), for the record: what the camera orbited before US-T78.
			double rootOff = FVector::Dist(centre, UnrealSpace::ToUnreal(app->Relative(app->TargetScene(target)))) / radiusCm;
			FVector toModel = centre - camera->GetComponentLocation();
			double angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(toModel.GetSafeNormal(), camera->GetForwardVector()), -1.0, 1.0)));
			bool ok = off < 1e-3 && angle < 0.05 && app->SpaceCamera->Mode == CameraMode::Focus && app->SpaceCamera->FocusTarget == target;
			Result(id, ok, cs::Cat(Describe(target), ", scale ", cs::Fmt(app->Scale()->Blend(), "F2"), ", warp ", app->Clock->Warp().Label, ": drawn model's middle ",
				cs::Fmt(off, "G4"), " radii from the focus, ", cs::Fmt(angle, "F4"), " deg off the view axis (", drawn, " parts; its root ", cs::Fmt(rootOff, "F3"), " radii off); camera ",
				Cameras::ToString(app->SpaceCamera->Mode), " on ", Describe(app->SpaceCamera->FocusTarget)));
		}

		/// US-T115: the ship is picked afresh for each check, the first in fleet order that is fit to be drawn right now: one
		/// picked once for all six went on living through the warp probes, and at 1 month/s it was soon killed in one of the
		/// system's battles (or landed), its view let go and the camera's focus dropped, so the checks after measured
		/// nothing ("nothing drawn (0 renderers)"). A procedural hull: an imported (Kenney) craft's model never streams in
		/// headless (-nullrhi). If the hull is lost before it is measured (see FocusCheck), the next one is tried.
		void FocusShipCheck(const cs::String& id, int warp)
		{
			cs::List<cs::Ref<Ship>> tried;
			cs::String lost;
			for (int attempt = 0; attempt < 8; attempt++)
			{
				cs::Ref<Ship> ship;
				for (const auto& s : app->Fleet()->Ships())
				{
					ShipPhase phase = s->Controller()->Phase;
					if (!s->Destroyed && !s->GateDeparted && !s->Derelict && phase != ShipPhase::Landed && phase != ShipPhase::Descent && phase != ShipPhase::Ascent
						&& s->Phase != ShipPhase::Lane && s->Battle == nullptr && Rendering::ShipModels::KenneyModelFor(s).empty()
						&& s != app->World()->PilotedShip && !tried.Contains(s))
					{
						ship = s;
						break;
					}
				}
				if (!ship)
				{
					Result(id, false, cs::Cat("no hull fit to focus", lost.empty() ? cs::String() : cs::Cat("; last lost: ", lost)));
					return;
				}
				tried.Add(ship);
				cs::String why;
				FocusCheck(id, Target::OfShip(ship), warp, &why);
				if (why.empty()) return;
				lost = why;
				Log(cs::Cat("AUDIT ", id, " INFO: ", why, "; trying another hull"));
			}
			Result(id, false, cs::Cat("every hull tried was lost before it was measured; last: ", lost));
		}

		void FocusProbes()
		{
			CloseEverything();
			int earth = -1, moon = -1;
			for (int i = 0; i < app->Catalog()->Count(); i++)
			{
				if ((*app->Catalog())[i]->Name == "Earth") earth = i;
				if ((*app->Catalog())[i]->Name == "Moon") moon = i;
			}
			int warp0 = app->Clock->WarpIndex;
			double goal0 = app->ScaleGoal;
			for (double goal : { 1.0, 0.0 })
			{
				if (app->ScaleGoal != goal) Tap(Key::G);
				for (int i = 0; i < 900 && cs::Math::Abs(app->Scale()->Blend() - app->ScaleGoal) > 1e-9; i++) frames(1);
				cs::String scale = goal > 0.5 ? "gameplay" : "realistic";
				for (int warp : { 0, cs::Math::Min(app->Clock->Levels().Count() - 1, 8) })
				{
					cs::String w = warp == 0 ? "rest" : "warp";
					if (earth >= 0) FocusCheck(cs::Cat("focus.", scale, "-", w, "-planet"), Target::OfBody(earth), warp);
					if (moon >= 0) FocusCheck(cs::Cat("focus.", scale, "-", w, "-moon"), Target::OfBody(moon), warp);
					FocusShipCheck(cs::Cat("focus.", scale, "-", w, "-ship"), warp);
				}
			}
			app->Clock->SetWarp(warp0);
			if (app->ScaleGoal != goal0) Tap(Key::G);
			app->SpaceCamera->ExitFocus();
			CloseEverything();
		}

		// ---------------------------------------------------------------- roles (US-T167, docs/ROLES.md)

		int BodyIndex(const char* name)
		{
			for (int i = 0; i < app->Catalog()->Count(); i++)
				if ((*app->Catalog())[i]->Name == name) return i;
			return -1;
		}

		/// At the controls the camera is the craft's: with a planet selected, F, the planet's number key and Home leave it
		/// where it was (a chase locked on the hull; a seat's rig untouched).
		bool CameraHeldAtControls(int earth, cs::String& detail)
		{
			if (earth >= 0) app->Select(Target::OfBody(earth));
			auto mode = app->SpaceCamera->Mode;
			auto focus = app->SpaceCamera->FocusTarget;
			Tap(Key::F);
			Tap(Key::Digit3);
			Tap(Key::Home);
			frames(2);
			bool held = app->AtControls() && app->SpaceCamera->Mode == mode && app->SpaceCamera->FocusTarget == focus;
			detail = cs::Cat("F, 3, Home with Earth selected: camera ", Cameras::ToString(app->SpaceCamera->Mode), " on ", Describe(app->SpaceCamera->FocusTarget));
			return held;
		}

		/// One embodied hull: its flight family, that F / 3 / Home never refocus its chase, and that R (drift) is the
		/// light and heavy fighter's alone, never a capital's or a hauler's.
		void HullRole(const char* role, ShipClass cls, FlightFamily family, bool canDrift, int earth)
		{
			cs::String id = cs::Cat("roles.", role);
			auto ship = StageAtHelm(cs::Cat("Audit Role ", role), cls, app->Catalog()->Root()->Index, DVec3(0, 0, Astro::AuKm), DVec3::UnitY);
			if (!ship) { Result(cs::Cat(id, ".helm"), false, cs::Cat("P on the staged ", ToString(cls), ": piloting ", B(app->IsPiloting()))); return; }
			Settle();
			auto got = FlightModelRegistry::Default()->FamilyOf(cls);
			Result(cs::Cat(id, ".family"), got == family, cs::Cat(ToString(cls), " flies as ", ToString(got), ", expected ", ToString(family)));
			cs::String detail;
			bool held = CameraHeldAtControls(earth, detail) && app->SpaceCamera->PilotLocked;
			Result(cs::Cat(id, ".focus-never-grabs"), held, detail);
			kb->Hold(Key::R);
			frames(12);
			bool drifting = Contains(app->PilotFlightNote, "DRIFT");
			kb->Release(Key::R);
			frames(2);
			Result(cs::Cat(id, ".drift"), drifting == canDrift, cs::Cat("R held at the helm of the ", ToString(cls), ": drifting ", B(drifting), ", expected ", B(canDrift)));
			ToCommand();
		}

		/// A seat (gun, station, launched craft, EVA worker): F / 3 / Home leave the seat's own camera alone.
		void SeatRole(const char* role, Key k, std::initializer_list<Key> mods, int earth)
		{
			cs::String id = cs::Cat("roles.", role);
			ToCommand();
			if (!SelectAShip()) { Log(cs::Cat("AUDIT ", id, ".focus-never-grabs INFO: no ship to select")); return; }
			Tap(k, mods);
			frames(5);
			if (!app->IsSeated()) { Log(cs::Cat("AUDIT ", id, ".focus-never-grabs INFO: no seat taken ('", Toast(), "')")); ToCommand(); return; }
			cs::String detail;
			Result(cs::Cat(id, ".focus-never-grabs"), CameraHeldAtControls(earth, detail), detail);
			ToCommand();
		}

		/// The free SpaceGod: the command keys are the camera's (F focuses what is selected), and the flight keys (cruise,
		/// tractor, drift) do nothing.
		void FreeRole(int earth)
		{
			ToCommand();
			if (earth < 0) return;
			app->SpaceCamera->ExitFocus();
			app->Select(Target::OfBody(earth));
			cs::String toast = Toast();
			Tap(Key::J, { Key::LeftShift });
			Tap(Key::T, { Key::LeftShift });
			Result("roles.free.flight-keys-idle", !app->AtControls() && Toast() == toast, cs::Cat("Shift+J, Shift+T in command: toast '", Toast(), "'"));
			Tap(Key::F);
			Settle();
			Result("roles.free.focus-grabs", app->SpaceCamera->Mode != CameraMode::Free && app->SpaceCamera->FocusTarget == Target::OfBody(earth),
				cs::Cat("F with Earth selected: camera ", Cameras::ToString(app->SpaceCamera->Mode), " on ", Describe(app->SpaceCamera->FocusTarget)));
			app->SpaceCamera->ExitFocus();
			CloseEverything();
		}

		void RoleProbes()
		{
			CloseEverything();
			int earth = BodyIndex("Earth");
			HullRole("fighter", ShipClass::Fighter, FlightFamily::LightFighter, true, earth);
			HullRole("heavy", ShipClass::Gunship, FlightFamily::HeavyFighter, true, earth);
			HullRole("capital", ShipClass::Battleship, FlightFamily::Capital, false, earth);
			HullRole("carrier", ShipClass::Carrier, FlightFamily::Capital, false, earth);
			HullRole("hauler", ShipClass::Freighter, FlightFamily::Hauler, false, earth);
			SeatRole("turret", Key::Slash, {}, earth);
			SeatRole("worker", Key::P, { Key::LeftShift }, earth);
			SeatRole("launched-craft", Key::Slash, { Key::LeftShift }, earth);
			FreeRole(earth);
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
		Probe("display", [] { MsaaProbes(Result, frames); DisplayModeProbes(Result, frames); }); // US-T55, US-T56
		Probe("time", TimeProbes);
		Probe("view", ViewProbes);
		Probe("swim", SwimProbes); // US-T221
		Probe("screens", ScreenProbes);
		Probe("nav", NavProbes);
		Probe("saves", SaveProbes);
		Probe("helm", HelmProbes);
		Probe("practice", PracticeProbes);
		Probe("seats", SeatProbes);
		Probe("diplomacy", DiplomacyProbes);
		Probe("traffic", TrafficProbes); // US-T222
		// US-T52, US-T53: checks of this copy's own GUI facade (UnityCompat's TextEditor) and model viewer camera, which the
		// C# copies get from Unity or the C# facade. US-T70: opt-in (-SpaceGodAuditFacade), so the default audit is the 85
		// probes every copy runs, in the same order, and its log compares row for row with the C# copies'.
		if (Bootstrap::HasCommandLineFlag("-SpaceGodAuditFacade"))
		{
			Probe("text", [] { TextEditProbes(Result); });
			Probe("viewer", [] { ModelViewerProbes(Result, frames); });
			Probe("focus", FocusProbes); // US-T78
			Probe("input", InputOwnershipProbes); // US-T90
			Probe("roles", RoleProbes); // US-T167: docs/ROLES.md, per embodied role
		}
		// US-T58: what the binding probes left code-present: combat at the controls with the mouse, and the player's own
		// transits (a hyperlane ridden, a jump gate gone through, an FTL crossing to another star). The last two leave Sol,
		// so they come last.
		Probe("combat", CombatProbes);
		Probe("lane", LaneProbes);
		Probe("gate", GateProbes);
		Probe("ftl", FtlProbes);
		Log(cs::Cat("AUDIT done: ", pass, " PASS, ", fail, " FAIL"));
	}
}
