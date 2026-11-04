using System;
using System.Collections;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using Unity.SpaceGod.Core;
using Unity.SpaceGod.UI;
using UnityEngine;
using UnrealSharp.Engine;
using UnrealSharp.Log;

namespace Unity.SpaceGod.Race
{
    /// <summary>The track's own per-frame times handed to the shared sampler (US-T46): the simulation steps of
    /// <see cref="UnitySpaceApp"/>'s Update (Galaxy.Update and the adventures), the whole Unity frame (the game mode's
    /// Step: input, every Update, the push to Unreal) and the IMGUI pass.</summary>
    public static class RaceProbe
    {
        public static readonly bool On = Bootstrap.HasCommandLineFlag("-SpaceGodRace");
        public static double SimMs, AppMs, GuiMs;
        public static long Now() => Stopwatch.GetTimestamp();
        public static double Since(long t0) => Stopwatch.GetElapsedTime(t0).TotalMilliseconds;
    }

    /// <summary>
    /// <c>-SpaceGodRace</c> (US-T46, docs/RACE.md): the race's scripted tour, the same script and the same frame counts as
    /// the C++ track's (Cpp/Source/UnrealSpaceGod/Smoke/SpaceGodRace.cpp). Run with <c>-UseFixedTimeStep -FPS=60</c> so
    /// every frame is 1/60 s of game time (the same simulation on both tracks, frame for frame) while the engine runs
    /// as fast as it can (no frame cap, no VSync): New Game at the screenshots' fixed moment and seed, then Sol idle,
    /// a staged battle off Uranus at battle time, a trip to Alpha Centauri (and home), a year at the highest warp, and
    /// three saves and loads; the shared sampler (SpaceGodRender's SpaceGodRace.cpp) times every frame and writes the results.
    /// </summary>
    public static class RaceRun
    {
        const string Log = "LogSpaceGod";
        public const double StartJd = 2461348.0; // 2026-11-03 12:00 UTC, the screenshots' (US-T45)
        public const int Seed = 1;
        public const int Warm = 120, IdleFrames = 1200, BattleWarm = 60, BattleFrames = 1200, MaxWarpFrames = 30, ArrivedFrames = 300;

        static void Cmd(string c) => SystemLibrary.ExecuteConsoleCommand(c);
        static string F(double v) => v.ToString("0.###", CultureInfo.InvariantCulture);
        static void Note(string k, double v) => Cmd("sg.Race.Note " + k + " " + F(v));

        static string Arg(string name)
        {
            foreach (var a in Environment.GetCommandLineArgs())
                if (a.StartsWith(name + "=", StringComparison.OrdinalIgnoreCase)) return a.Substring(name.Length + 1).Trim('"');
            return null;
        }

        /// <summary>One frame, handing the frame's probe times to the sampler.</summary>
        static IEnumerator Frame()
        {
            yield return null;
            Cmd("sg.Race.Frame " + F(RaceProbe.SimMs) + " " + F(RaceProbe.AppMs) + " " + F(RaceProbe.GuiMs));
            RaceProbe.SimMs = 0;
        }

        static IEnumerator Frames(int n)
        {
            for (int i = 0; i < n; i++) yield return Frame();
        }

        static void Uncapped()
        {
            QualitySettings.vSyncCount = 0;
            Application.targetFrameRate = -1;
            Cmd("r.VSync 0");
            Cmd("t.MaxFPS 0");
        }

        public static IEnumerator Run()
        {
            string results = Arg("-SpaceGodRaceOut") ?? Path.Combine(Paths.ProjectSavedDir(), "race-csharp.json");
            Cmd("sg.Race.Note track CSharp");
            Cmd("sg.Race.Note runtime .NET_" + Environment.Version);
            Bridge.UnrealHost.NoPlayerInput = true;
            UnitySpaceApp.StartJd = StartJd;
            while (!MainMenu.Showing && Bootstrap.App == null) yield return null;
            Cmd("sg.Race.NoteAge processToMenuMs");
            // The menu up for 30 frames (its first frames end before New Game's galaxy is built), then New Game.
            for (int i = 0; i < 30; i++) yield return null;
            var t = RaceProbe.Now();
            if (MainMenu.Showing)
            {
                UnityEngine.Random.InitState(Seed);
                GameLaunch.NewGame();
            }
            UnitySpaceApp app = null;
            for (int i = 0; i < 6000; i++)
            {
                app = Bootstrap.App;
                if (app != null && app.Clock.Jd > StartJd) break; // the first frame the game has run: playable
                yield return null;
            }
            if (app == null) { UnrealLogger.LogError(Log, "SpaceGodRace: no game"); Application.Quit(); yield break; }
            Cmd("sg.Race.NoteAge processToPlayableMs");
            Note("newGameToPlayableMs", RaceProbe.Since(t));
            Uncapped();

            // Sol, as the game opens (default warp, the camera beside Earth).
            yield return Frames(Warm);
            Uncapped();
            Cmd("sg.Race.Phase sol-idle");
            yield return Frames(IdleFrames);
            Cmd("sg.Race.End");
            Note("solIdleEndJd", app.Clock.Jd);
            Note("solIdleShips", app.Fleet.Ships.Count);
            Cmd("memreport -full");

            // Three and a half days at 1 week/s (each frame 2.8 hours of game time; every system stepped every frame). The two highest
            // warps, 1 yr/s and 1 month/s, trip US-T59 on both tracks within weeks of game time (a ring captured mid-queue).
            Uncapped();
            app.Clock.Paused = false;
            app.Clock.SetWarp(SimClock.Levels.Length - 3);
            Cmd("sg.Race.Phase week-warp");
            yield return Frames(MaxWarpFrames);
            Cmd("sg.Race.End");
            app.Clock.SetWarp(SimClock.DefaultWarpIndex);
            Note("weekWarpEndJd", app.Clock.Jd);
            yield return Frames(30);

            // A staged battle (SceneTour's broadside showcase): 2 battleships + a cruiser each for Sol and Reach off
            // Uranus, watched at battle time, then 1 min/s.
            int uranus = app.Catalog["Uranus"].Index;
            double jd = app.Clock.Jd;
            var dir = new DVec3(1, 0.15, 0.4).Normalized;
            var side = DVec3.Cross(new DVec3(0, 1, 0), dir).Normalized;
            var classes = new[] { ShipClass.Battleship, ShipClass.Battleship, ShipClass.Cruiser };
            Faction a = app.World.Sol, d = app.World.Reach;
            for (int i = 0; i < classes.Length; i++)
                app.Fleet.SpawnAt("Showcase " + a.Name + " " + (i + 1), classes[i], a, uranus, dir * (2000 + i * 400), -dir, jd);
            for (int i = 0; i < classes.Length; i++)
                app.Fleet.SpawnAt("Showcase " + d.Name + " " + (i + 1), classes[i], d, uranus, dir * (2000 + i * 400) + side * 3000, dir, jd);
            var battle = app.World.War.TryStartBattle(uranus, a, jd);
            if (battle != null)
            {
                app.Clock.Paused = false;
                app.WatchBattle(battle);
                app.Clock.SetWarp(2);
                yield return Frames(BattleWarm);
                Cmd("sg.Race.Phase battle");
                int n = 0;
                for (; n < BattleFrames; n++) yield return Frame();
                Cmd("sg.Race.End");
                Note("battleUnits", battle.Units.Count);
                Note("battleOver", battle.Over ? 1 : 0);
            }
            else UnrealLogger.LogError(Log, "SpaceGodRace: no battle");
            app.Clock.SetWarp(SimClock.DefaultWarpIndex);
            yield return Frames(30);

            // To Alpha Centauri (StarTravel: the jump between the stars, the system promoted), a while there, and home
            // again (not timed).
            var ac = app.Galaxy.Find("Alpha Centauri");
            var sol = app.ActiveSystem;
            if (ac != null)
            {
                // The view's own trip (a followed ship, the battle's flagship, would be sent on a crew course instead).
                app.SpaceCamera.ExitFocus();
                app.Select(Target.None);
                Cmd("sg.Race.Phase galaxy-ftl");
                app.TravelTo(ac);
                int f = 0;
                while (app.Travel.Active && f < 3000) { f++; yield return Frame(); }
                Note("ftlTravelFrames", f);
                yield return Frames(ArrivedFrames);
                Cmd("sg.Race.End");
                Note("ftlArrivedIn", app.ActiveSystem.Name == "Alpha Centauri" ? 1 : 0);
                app.TravelTo(sol);
                for (int g = 0; app.Travel.Active && g < 3000; g++) yield return null;
                yield return Frames(30);
            }

            // Saves and loads (Core/Persistence, the original's format), three of each, timed.
            double saveSum = 0, loadSum = 0, saveMin = double.MaxValue, loadMin = double.MaxValue;
            for (int k = 0; k < 3; k++)
            {
                var s0 = RaceProbe.Now();
                bool ok = app.SaveTo("race", "US-T46 race");
                double sMs = RaceProbe.Since(s0);
                yield return Frames(5);
                var l0 = RaceProbe.Now();
                ok &= app.LoadFrom("race");
                double lMs = RaceProbe.Since(l0);
                app = Bootstrap.App;
                yield return Frames(5);
                if (!ok) UnrealLogger.LogError(Log, "SpaceGodRace: save/load failed");
                saveSum += sMs; loadSum += lMs; saveMin = Math.Min(saveMin, sMs); loadMin = Math.Min(loadMin, lMs);
                Note("save" + (k + 1) + "Ms", sMs);
                Note("load" + (k + 1) + "Ms", lMs);
            }
            Note("saveMinMs", saveMin);
            Note("loadMinMs", loadMin);
            Note("saveMeanMs", saveSum / 3);
            Note("loadMeanMs", loadSum / 3);
            Note("endJd", app.Clock.Jd);
            Cmd("sg.Race.Write " + results);
            UnrealLogger.Log(Log, "SpaceGodRace: done -> " + results);
            yield return Frames(2);
            Application.Quit();
        }
    }
}
