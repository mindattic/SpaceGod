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
#include "Input/InputDevice.h"
#include "UnityCompat/UnityRuntime.h"
#include "Views/SkyView.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
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

		// ---------------------------------------------------------------- US-T58: staging

		cs::String F(double v, const char* f) { return cs::Fmt(v, f); }

		/// Releases whatever is at the controls (P), so the next probe starts from command.
		void ToCommand()
		{
			for (int i = 0; i < 3 && app->AtControls(); i++) { Tap(Key::P); frames(3); }
			CloseEverything();
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

			// The target selected (what a left-click on it does in the god view), T locks it; the left button opens fire
			// (weapons free on a lone merchant), and held down the guns fire down the crosshair, the mouse keeping it on the target.
			app->Select(Target::OfShip(target));
			Tap(Key::T);
			cs::String locked = Describe(app->SpaceCamera->PilotLockTarget);
			double whole = Integrity(target->Combat), least = whole, heat = 0, worst = 0, range = 0;
			cs::String fireToast;
			int held = 0, frame = 0;
			bool trigger = false;
			// The click opens the fight, which puts the two sides at their stations some km apart (War.TryStartPlayerAttack):
			// the player runs in on W, under cruise (J) while far out, and inside the guns' reach fires in bursts, as a
			// player does: held until the guns run hot, let go until they cool, the mouse keeping the crosshair on the lead.
			kb->Hold(Key::W);
			double nearest = std::numeric_limits<double>::max(), closest = std::numeric_limits<double>::max(), first = -1;
			bool cruising = false;
			for (; frame < 3600; frame++)
			{
				auto unit = me->Battle ? me->Battle->UnitOf(me) : cs::Ref<BattleUnit>();
				auto tu = me->Battle ? me->Battle->UnitOf(target) : cs::Ref<BattleUnit>();
				double to = unit && tu ? (tu->Pos - unit->Pos).Length() : 0;
				bool far = to > 3, inReach = unit && to < 1.5;
				if (far != cruising) { if (far) kb->Hold(Key::J); else kb->Release(Key::J); cruising = far; }
				bool hot = unit && (unit->Overheated || unit->Heat > 0.8);
				bool cool = !unit || unit->Heat < 0.3;
				bool want = !unit ? frame < 2 : inReach && (trigger ? !hot : cool);
				if (trigger && !want) { kb->ReleaseButton(Input::MouseButton::Left); trigger = false; }
				else if (!trigger && want) { kb->HoldButton(Input::MouseButton::Left); trigger = true; }
				SteerMouse(DirectionTo(me, target), signX, signY);
				if (trigger) held++;
				if (fireToast.empty() && me->Battle) fireToast = Toast();
				unit = me->Battle ? me->Battle->UnitOf(me) : cs::Ref<BattleUnit>();
				tu = me->Battle ? me->Battle->UnitOf(target) : cs::Ref<BattleUnit>();
				if (unit)
				{
					heat = cs::Math::Max(heat, unit->Heat);
					if (trigger && inReach) worst = cs::Math::Max(worst, AngleDeg(unit->PlayerAim, DirectionTo(me, target)));
					if (tu) { range = (tu->Pos - unit->Pos).Length(); nearest = cs::Math::Min(nearest, range); if (first < 0) first = range; }
					// The guns' rounds (this side's, in flight): how close any came to the target's hull.
					if (tu)
						for (const auto& r : me->Battle->Rounds)
							if (r->Side == unit->Side && !r->Gone) closest = cs::Math::Min(closest, cs::Math::Max(0.0, (r->Pos - tu->Pos).Length() - tu->Radius()));
				}
				least = cs::Math::Min(least, Integrity(tu && tu->Combat ? tu->Combat : target->Combat));
				if (target->Destroyed || (held > 120 && least < whole && closest < 0.1)) break;
				if (me->Destroyed || !app->IsPiloting()) break;
			}
			kb->Release(Key::J);
			if (trigger) kb->ReleaseButton(Input::MouseButton::Left);
			kb->Release(Key::W);
			frames(1);
			Result("combat.mouse-fire", me->Battle != nullptr && heat > 0 && least < whole && closest < 0.1,
				cs::Cat("T: locked ", locked, "; left button held ", held, " of ", cs::Math::Min(frame + 1, 3600), " frames: fight ", me->Battle ? "on" : "none", ", gun heat ", F(heat, "F2"), ", ",
					"crosshair within ", F(worst, "F2"), " deg of the lead, ", F(nearest == std::numeric_limits<double>::max() ? 0 : nearest, "F2"), " to ", F(range, "F2"), " km off, first ", F(first, "F2"), " km, rounds to ", F(closest == std::numeric_limits<double>::max() ? -1 : closest, "F3"), " km of its hull, the ", target->Name, " ", F(whole, "F0"), " -> ", F(least, "F0"),
					target->Destroyed ? " (destroyed)" : "", "; toast '", fireToast, "'"));
			ToCommand();
		}

		// ---------------------------------------------------------------- US-T58: the player's own transits

		/// A hyperlane (U.SPC-T41): a fighter staged just outside a lane's on-ramp ring, flown in on W, rides it (Fleet.JoinLane,
		/// the clock compressed) to the far ramp and comes off there.
		void LaneProbes()
		{
			ToCommand();
			const auto& lanes = app->Fleet()->Lanes;
			double jd = app->Clock->Jd;
			cs::Ref<Lane> lane;
			for (const auto& l : lanes->Lanes)
				if (lanes->Carries(l) && !l->IsGateSpur() && !BattleAt(l->A) && !BattleAt(l->B)) { lane = l; break; }
			if (!lane) { Result("transit.lane-enter", false, "no lane carrying traffic, clear of a fight"); return; }
			int at = lane->A, to = lane->B;
			DVec3 dir = lanes->RampDirection(lane, at, jd);
			DVec3 ramp = lanes->RampOffset(lane, at, jd);
			auto ship = StageAtHelm("Audit Lane Fighter", ShipClass::Fighter, at, ramp + dir * (Hyperlanes::GateRadiusKm + 0.5), -dir);
			if (!ship)
			{
				Result("transit.lane-enter", false, cs::Cat("P on the staged fighter: piloting ", B(app->IsPiloting())));
				return;
			}
			kb->Hold(Key::W);
			int f = 0;
			for (; f < 1800 && !app->RidingLane(); f++) frames(1);
			kb->Release(Key::W);
			frames(1);
			bool riding = app->RidingLane();
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
		Probe("screens", ScreenProbes);
		Probe("nav", NavProbes);
		Probe("saves", SaveProbes);
		Probe("helm", HelmProbes);
		Probe("practice", PracticeProbes);
		Probe("seats", SeatProbes);
		Probe("diplomacy", DiplomacyProbes);
		// US-T52, US-T53: checks of this copy's own GUI facade (UnityCompat's TextEditor) and model viewer camera, which the
		// C# copies get from Unity or the C# facade. US-T70: opt-in (-SpaceGodAuditFacade), so the default audit is the 85
		// probes every copy runs, in the same order, and its log compares row for row with the C# copies'.
		if (Bootstrap::HasCommandLineFlag("-SpaceGodAuditFacade"))
		{
			Probe("text", [] { TextEditProbes(Result); });
			Probe("viewer", [] { ModelViewerProbes(Result, frames); });
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
