using System;
using System.Collections.Generic;
using Unity.SpaceGod.Core;
using Unity.SpaceGod.Input;
using UnrealSharp.Log;

namespace Unity.SpaceGod.Audit
{
    /// <summary>
    /// US-T48 faithfulness audit (<c>-SpaceGodAudit</c>, tools/play.ps1 -Mode audit): after the smoke run, presses the
    /// original's bindings (Core/ControlsHelp.cs) on the scripted device, one feature at a time, and checks the app state
    /// the original's code path changes. Logs <c>AUDIT &lt;id&gt; PASS|FAIL: detail</c>; the C++ track's
    /// Smoke/FeatureAudit.cpp runs the same probes in the same order with the same ids, so the two logs line up.
    /// Each probe puts back what it changed, so one failing does not cascade into the next.
    /// </summary>
    public static class FeatureAudit
    {
        const string Log = "LogSpaceGod";

        static UnitySpaceApp app;
        static ScriptedInputDevice kb;
        static Action<int> frames;
        static int pass, fail;

        public static void Run(UnitySpaceApp theApp, ScriptedInputDevice device, Action<int> stepFrames)
        {
            app = theApp;
            kb = device;
            frames = stepFrames;
            pass = fail = 0;
            Probe("audio", () => SettingsAudit.Audio(Result, frames)); // US-T50
            Probe("render", () => SettingsAudit.Render(Result, frames)); // US-T51
            Probe("display", () => { DisplayAudit.Msaa(Result, frames); DisplayAudit.Modes(Result, frames); }); // US-T55, US-T56
            Probe("time", Time);
            Probe("view", View);
            Probe("screens", Screens);
            Probe("nav", Nav);
            Probe("saves", Saves);
            Probe("helm", Helm);
            Probe("practice", Practice);
            Probe("seats", Seats);
            Probe("diplomacy", Diplomacy);
            UnrealLogger.Log(Log, $"AUDIT done: {pass} PASS, {fail} FAIL");
        }

        static void Probe(string group, Action body)
        {
            try { body(); }
            catch (Exception e) { Result(group + ".exception", false, e.GetType().Name + ": " + e.Message); }
        }

        static void Result(string id, bool ok, string detail)
        {
            if (ok) pass++; else fail++;
            UnrealLogger.Log(Log, $"AUDIT {id} {(ok ? "PASS" : "FAIL")}: {detail}");
        }

        /// <summary>Taps <paramref name="k"/> with <paramref name="mods"/> held, runs a frame, lets go, runs another.</summary>
        static void Tap(Key k, params Key[] mods)
        {
            foreach (var m in mods) kb.Hold(m);
            kb.Tap(k);
            frames(1);
            foreach (var m in mods) kb.Release(m);
            frames(1);
        }

        /// <summary>A key that flips a flag: pressed once it must flip, pressed again flip back. The flag is put back after.</summary>
        static void Toggle(string id, Key k, Func<bool> get, Action<bool> set, params Key[] mods)
        {
            bool before = get();
            Tap(k, mods);
            bool on = get();
            Tap(k, mods);
            bool off = get();
            set?.Invoke(before);
            Result(id, on != before && off == before, $"{before} -> {on} -> {off}");
        }

        static string Toast => app.Toast ?? "";

        static void CloseEverything()
        {
            app.ShowHelp = app.ShowMarket = app.ShowCargo = app.ShowOutfit = app.ShowBounties = app.ShowContracts = false;
            app.ShowFleet = app.ShowTrade = app.ShowGalaxyMap = app.ShowNews = app.ShowSpawn = app.ShowSaves = app.ShowPicker = false;
            app.ScreenCleared = false;
            app.ShowHud = true;
            UnityEngine.Time.timeScale = 1;
        }

        // ---------------------------------------------------------------- time

        static void Time()
        {
            bool paused = app.Clock.PauseHeld;
            Tap(Key.Space);
            bool p1 = app.Clock.PauseHeld;
            Tap(Key.Space);
            Result("time.pause", p1 != paused && app.Clock.PauseHeld == paused, $"Space: {paused} -> {p1} -> {app.Clock.PauseHeld}");

            string w0 = app.Clock.Warp.Label;
            Tap(Key.Period);
            string w1 = app.Clock.Warp.Label;
            Tap(Key.Comma);
            string w2 = app.Clock.Warp.Label;
            Result("time.warp", w1 != w0 && w2 == w0, $"'.' {w0} -> {w1}, ',' -> {w2}");
            Tap(Key.Equals);
            string w3 = app.Clock.Warp.Label;
            Tap(Key.Minus);
            Result("time.warp-alt", w3 != w0 && app.Clock.Warp.Label == w0, $"'=' {w0} -> {w3}, '-' -> {app.Clock.Warp.Label}");

            Tap(Key.T);
            double hours = Math.Abs((app.Clock.Utc - DateTime.UtcNow).TotalHours);
            Result("time.reset", hours < 1, $"T: clock {app.Clock.Utc:yyyy-MM-dd HH:mm} UTC, {hours:F2} h from now");
        }

        // ---------------------------------------------------------------- view

        static void View()
        {
            double g0 = app.ScaleGoal;
            Tap(Key.G);
            double g1 = app.ScaleGoal;
            Tap(Key.Backquote);
            Result("view.scale", g1 != g0 && app.ScaleGoal == g0, $"G {g0} -> {g1}, ` -> {app.ScaleGoal}");
            frames(60);

            Toggle("view.orbits", Key.O, () => app.ShowOrbits, v => app.ShowOrbits = v);
            Toggle("view.labels", Key.L, () => app.ShowLabels, v => app.ShowLabels = v);
            Toggle("view.lanes", Key.N, () => app.ShowLanes, v => app.ShowLanes = v);
            Toggle("view.satellites", Key.K, () => app.ShowSatellites, v => app.ShowSatellites = v);
            Toggle("view.reorient", Key.R, () => app.AutoReorient, null);

            string s0 = app.Sky.Current.Name;
            Tap(Key.K, Key.LeftShift);
            string s1 = app.Sky.Current.Name;
            Tap(Key.K, Key.LeftShift, Key.LeftAlt);
            Result("view.sky", s1 != s0 && app.Sky.Current.Name == s0, $"Shift+K '{s0}' -> '{s1}', Shift+Alt+K -> '{app.Sky.Current.Name}'");
        }

        // ---------------------------------------------------------------- screens and panels

        static void Screens()
        {
            CloseEverything();
            Toggle("ui.help", Key.H, () => app.ShowHelp, v => app.ShowHelp = v);
            Toggle("ui.help-f1", Key.F1, () => app.ShowHelp, v => app.ShowHelp = v);
            Toggle("ui.hud", Key.U, () => app.ShowHud, v => app.ShowHud = v);
            Toggle("ui.clear-screen", Key.F11, () => app.ScreenCleared, v => app.ScreenCleared = v);
            Toggle("ui.market", Key.M, () => app.ShowMarket, v => app.ShowMarket = v);
            Toggle("ui.outfit", Key.F2, () => app.ShowOutfit, v => app.ShowOutfit = v);
            Toggle("ui.bounties", Key.F4, () => app.ShowBounties, v => app.ShowBounties = v);
            Toggle("ui.contracts", Key.F7, () => app.ShowContracts, v => app.ShowContracts = v);
            Toggle("ui.fleet", Key.F10, () => app.ShowFleet, v => app.ShowFleet = v);
            Toggle("ui.trade-desk", Key.Backslash, () => app.ShowTrade, v => app.ShowTrade = v);
            Toggle("ui.galaxy-map", Key.LeftBracket, () => app.ShowGalaxyMap, v => app.ShowGalaxyMap = v);
            Toggle("ui.news", Key.X, () => app.ShowNews, v => app.ShowNews = v);
            Toggle("ui.spawn", Key.V, () => app.ShowSpawn, v => app.ShowSpawn = v);
            Toggle("ui.saves", Key.F6, () => app.ShowSaves, v => app.ShowSaves = v);
            Toggle("ui.picker", Key.F9, () => app.ShowPicker, v => app.ShowPicker = v);
            CloseEverything();

            // Esc backs out (panels, focus, selection) and then opens the pause menu, which stops time; Esc again resumes.
            int presses = 0;
            while (UnityEngine.Time.timeScale != 0 && presses < 6) { Tap(Key.Escape); presses++; }
            bool opened = UnityEngine.Time.timeScale == 0;
            Tap(Key.Escape);
            bool resumed = UnityEngine.Time.timeScale != 0;
            Result("ui.pause-menu", opened && resumed, $"Esc x{presses} opened: {opened}, Esc resumed: {resumed}");
            CloseEverything();
        }

        // ---------------------------------------------------------------- navigation

        static string Describe(Target t) =>
            t.IsBody ? "body " + app.Catalog[t.Body].Name : t.IsShip ? "ship " + t.Ship.Name : t.Kind.ToString();

        /// <summary>Runs frames until the camera's flight to a focus ends (at most 300).</summary>
        static void Settle()
        {
            for (int i = 0; i < 300 && app.SpaceCamera.Mode == Cameras.SpaceCamera.CameraMode.Travel; i++) frames(1);
        }

        static void Nav()
        {
            Tap(Key.Home);
            Settle();
            Result("nav.home", app.Selected.IsBody && app.Selected.Body == 0 && app.SpaceCamera.Mode == Cameras.SpaceCamera.CameraMode.Focus,
                $"Home: selected {Describe(app.Selected)}, camera {app.SpaceCamera.Mode}");

            Tap(Key.Digit3);
            string sel = Describe(app.Selected);
            Tap(Key.F);
            Settle();
            Result("nav.number-focus", sel == "body Earth" && app.SpaceCamera.Mode == Cameras.SpaceCamera.CameraMode.Focus && Describe(app.SpaceCamera.FocusTarget) == "body Earth",
                $"3: {sel}; F: camera {app.SpaceCamera.Mode} on {Describe(app.SpaceCamera.FocusTarget)}");

            Tap(Key.Tab);
            string first = Describe(app.Selected);
            Tap(Key.Tab);
            string second = Describe(app.Selected);
            Result("nav.cycle-ships", app.Selected.IsShip && first != second, $"Tab: {first}, Tab: {second}");

            string toast = Toast;
            var mode = app.SpaceCamera.Mode;
            var focus = Describe(app.SpaceCamera.FocusTarget);
            Tap(Key.B);
            frames(10);
            bool handled = Toast != toast || app.SpaceCamera.Mode != mode || Describe(app.SpaceCamera.FocusTarget) != focus;
            Result("nav.watch-battle", handled, $"B: camera {app.SpaceCamera.Mode} on {Describe(app.SpaceCamera.FocusTarget)}; toast '{Toast}'");
            CloseEverything();
        }

        // ---------------------------------------------------------------- saves

        static void Saves()
        {
            string toast = Toast;
            Tap(Key.F5);
            string saved = Toast;
            double jd = app.Clock.Jd;
            frames(30);
            Tap(Key.F8);
            frames(2);
            Result("saves.quicksave", saved != toast && saved.IndexOf("save", StringComparison.OrdinalIgnoreCase) >= 0, $"F5: toast '{saved}'");
            Result("saves.quickload", Math.Abs(app.Clock.Jd - jd) * 86400 < 3600, $"F8: JD back to {app.Clock.Jd:F6} (saved {jd:F6}); toast '{Toast}'");
            CloseEverything();
        }

        // ---------------------------------------------------------------- helm

        static Ship PilotedShip => app.Piloted.Ship;

        static bool SelectAShip()
        {
            for (int i = 0; i < 12 && !app.Selected.IsShip; i++) Tap(Key.Tab);
            return app.Selected.IsShip;
        }

        static void Helm()
        {
            if (!SelectAShip()) { Result("helm.pilot", false, "Tab found no ship to select"); return; }
            string name = app.Selected.Ship.Name;
            Tap(Key.P);
            frames(5);
            Result("helm.pilot", app.IsPiloting && PilotedShip?.Name == name, $"P on {name}: piloting {app.IsPiloting} ({PilotedShip?.Name}); toast '{Toast}'");
            if (!app.IsPiloting) return;

            var from = app.SpaceCamera.ScenePosition;
            kb.Hold(Key.W);
            frames(60);
            kb.Release(Key.W);
            frames(1);
            double moved = (app.SpaceCamera.ScenePosition - from).Length;
            Result("helm.thrust", moved > 0, $"W held 60 frames: chase camera moved {moved:G4} units");

            string toast = Toast;
            Tap(Key.F);
            string fa = Toast;
            Tap(Key.F);
            Result("helm.flight-assist", fa != toast && fa.IndexOf("assist", StringComparison.OrdinalIgnoreCase) >= 0, $"F: toast '{fa}', F: '{Toast}'");

            bool silent = PilotedShip.SilentRunning;
            Tap(Key.F3);
            bool s1 = PilotedShip.SilentRunning;
            Tap(Key.F3);
            Result("helm.silent-running", s1 != silent && PilotedShip.SilentRunning == silent, $"F3: {silent} -> {s1} -> {PilotedShip.SilentRunning}");

            Toggle("helm.cargo", Key.I, () => app.ShowCargo, v => app.ShowCargo = v);

            Tap(Key.Escape);
            bool menu = UnityEngine.Time.timeScale == 0;
            Tap(Key.Escape);
            Result("helm.esc-pause-menu", menu && UnityEngine.Time.timeScale != 0 && app.IsPiloting, $"Esc at the helm opened the pause menu: {menu}; still piloting: {app.IsPiloting}");
            CloseEverything();

            Tap(Key.P);
            frames(5);
            Result("helm.release", !app.IsPiloting, $"P again: piloting {app.IsPiloting}; toast '{Toast}'");
        }

        static void KeyToast(string id, Key k, params Key[] mods)
        {
            string toast = Toast;
            Tap(k, mods);
            // No new toast is not a failure: the original's handler can be silent for this hull (no lane left, no
            // phasers, nothing to lock); logged as INFO for comparing the two tracks' logs line by line.
            if (Toast != toast) Result(id, true, $"{k}: toast '{Toast}'");
            else UnrealLogger.Log(Log, $"AUDIT {id} INFO: {k}: no new toast ('{Toast}')");
        }

        static void Practice()
        {
            Tap(Key.V, Key.LeftShift);
            frames(5);
            Result("helm.practice-fighter", app.IsPiloting && PilotedShip != null && PilotedShip.Name.Contains("Practice"),
                $"Shift+V: piloting {app.IsPiloting} ({PilotedShip?.Name} {PilotedShip?.Spec?.Class}); toast '{Toast}'");
            if (app.IsPiloting)
            {
                string toast = Toast;
                Tap(Key.PageDown);
                Result("helm.ftl-plot", Toast != toast || app.PlottedCourse != null, $"PgDn: course {(app.PlottedCourse != null ? "plotted" : "none")}; toast '{Toast}'");
                // Keys the helm answers with a toast (a refusal included): the binding reaches the original's handler.
                KeyToast("helm.ftl-engage", Key.Enter);
                KeyToast("helm.ftl-autopilot", Key.End);
                KeyToast("helm.lane-reenter", Key.Y);
                KeyToast("helm.phasers", Key.Insert);
                KeyToast("helm.run-hot", Key.Insert, Key.LeftShift);
                KeyToast("helm.lock", Key.T);
                Tap(Key.P);
                frames(5);
            }
            CloseEverything();
        }

        // ---------------------------------------------------------------- seats (take charge)

        static void Seats()
        {
            if (!SelectAShip()) { Result("seats.man-gun", false, "Tab found no ship to select"); return; }
            string name = app.Selected.Ship.Name;
            string toast = Toast;
            Tap(Key.Slash);
            frames(5);
            bool seated = app.IsSeated;
            Result("seats.man-gun", seated || Toast != toast, $"/ on {name}: seated {seated}; toast '{Toast}'");
            if (seated)
            {
                Tap(Key.Backspace);
                frames(5);
                Result("seats.up-to-helm", app.IsPiloting && !app.IsSeated, $"Backspace: piloting {app.IsPiloting}, seated {app.IsSeated}; toast '{Toast}'");
                Tap(Key.P);
                frames(5);
            }
            Result("seats.back-to-command", !app.AtControls, $"P: at controls {app.AtControls}");

            toast = Toast;
            Tap(Key.Quote);
            frames(5);
            Result("seats.take-charge", app.AtControls || Toast != toast, $"': at controls {app.AtControls}; toast '{Toast}'");
            if (app.AtControls) { Tap(Key.P); frames(5); }
            CloseEverything();
        }

        // ---------------------------------------------------------------- diplomacy

        static void Diplomacy()
        {
            if (!SelectAShip()) { Result("diplomacy.hail", false, "Tab found no ship to select"); return; }
            // F12 on ships in turn until one belongs to another polity ("Nobody to hail" for one's own).
            bool hailed = false;
            for (int i = 0; i < 30 && !hailed; i++)
            {
                Tap(Key.F12);
                frames(2);
                hailed = app.ShowGalaxyMap || !Toast.StartsWith("Nobody to hail");
                if (!hailed) Tap(Key.Tab);
            }
            Result("diplomacy.hail", hailed, $"F12 on {Describe(app.Selected)}: galaxy map {app.ShowGalaxyMap}; toast '{Toast}'");
            CloseEverything();
            Tap(Key.Escape);
            CloseEverything();
        }
    }
}
