using System;
using System.Collections;
using System.Reflection;
using UnityEngine;
using UnrealSharp.Log;

namespace Unity.SpaceGod.UI
{
    /// <summary>
    /// The UI half of the headless smoke run (<c>-SpaceGodSmoke</c>): goes through every screen the way a player would
    /// -- the main menu and its pages, New Game, the HUD, the pause menu and its Settings and Display pages, Help, the
    /// galaxy map and its modes, each panel the HUD opens -- running one IMGUI frame on each (the measuring canvas: no
    /// pixels headless) and logging what it drew. Moving between screens clicks the real buttons by their label
    /// (mouse down and up over the rect the button was drawn at), so the event path, hot controls and the screens'
    /// own button handling are exercised, not just their drawing. Panels the original opens from a key are opened by
    /// the app's own property, as the key does.
    /// </summary>
    public static class UiSmoke
    {
        const string Log = "LogSpaceGod";

        static readonly MeasuringCanvas Canvas = new MeasuringCanvas();
        static readonly GuiInput Input = new GuiInput { Present = true, Mouse = new Vector2(-1000, -1000) };
        static Action<int> frames;
        static int failures;

        /// <summary>Runs the UI tour. <paramref name="stepFrames"/> runs n frames of the app loop (the game mode's).</summary>
        /// <returns>Whether every screen drew and every click landed.</returns>
        public static bool Run(Action<int> stepFrames)
        {
            frames = stepFrames;
            failures = 0;
            if (MainMenu.Showing)
            {
                Draw("main menu");
                Click("Profiles", "main menu > profiles");
                Click("Back", "main menu");
                Click("Settings", "main menu > settings");
                Click("Back", "main menu");
                Click("Help", "main menu > help");
                Key(KeyCode.DownArrow, "main menu > help, next chapter (Down)");
                Click("Next", "main menu > help, next chapter (Next)");
                Click("Back", "main menu");
                Click("Asset Library POC", "main menu > asset library POC");
                Click("Back", "main menu");
                Click("New Game", "new game");
                frames(2);
            }
            var app = Bootstrap.App;
            if (app == null)
            {
                Fail("no game after New Game");
                return false;
            }

            Draw("game HUD");
            Toggle(v => app.ShowHelp = v, "help (H)");
            Toggle(v => app.ShowGalaxyMap = v, "galaxy map ([)", () =>
            {
                Click("DIPLOMACY", "galaxy map > diplomacy");
                Click("COMMAND", "galaxy map > command");
                Click("Close", "game HUD");
            });
            Toggle(v => app.ShowMarket = v, "market board");
            Toggle(v => app.ShowCargo = v, "cargo hold");
            Toggle(v => app.ShowTrade = v, "trade desk");
            Toggle(v => app.ShowFleet = v, "fleet");
            Toggle(v => app.ShowNews = v, "news");
            Toggle(v => app.ShowContracts = v, "contracts");
            Toggle(v => app.ShowBounties = v, "bounties");
            Toggle(v => app.ShowOutfit = v, "outfitting");
            Toggle(v => app.ShowSaves = v, "saves");
            Toggle(v => app.ShowSpawn = v, "spawn & pilot (V)");
            Toggle(v => app.ScreenCleared = v, "clear screen (F11)");

            var pause = typeof(UnitySpaceApp).GetField("pauseMenu", BindingFlags.Instance | BindingFlags.NonPublic)?.GetValue(app) as PauseMenu;
            if (pause == null) Fail("no pause menu on the app");
            else
            {
                pause.Open();
                Draw("pause menu (Esc)");
                Click("Settings", "pause menu > settings");
                Click("Display", "pause menu > settings > display");
                Click("Back", "pause menu > settings");
                Click("Back", "pause menu");
                Click("Help", "help from the pause menu");
                if (app.ShowHelp) app.ShowHelp = false;
                if (pause.IsOpen) pause.Close();
                Draw("game HUD");
            }

            UnrealLogger.Log(Log, failures == 0 ? "UI smoke: every screen drew" : $"UI smoke: {failures} step(s) failed");
            return failures == 0;
        }

        static void Fail(string what)
        {
            failures++;
            UnrealLogger.LogError(Log, "UI smoke: " + what);
        }

        /// <summary>One frame of the app, then one IMGUI frame; logs what was drawn.</summary>
        static void Draw(string screen)
        {
            frames(1);
            Frame();
            var s = ImguiHost.Stats;
            if (s.Labels + s.Boxes + s.Buttons + s.Textures == 0) Fail(screen + ": drew nothing");
            else UnrealLogger.Log(Log, $"UI smoke [{screen}]: {s}");
        }

        static void Frame()
        {
            ImguiHost.Frame(Canvas, Input, ASpaceHud.Handlers(), Time.unscaledTime);
            Input.Clear();
        }

        /// <summary>Clicks the button labelled <paramref name="label"/> where the last frame drew it, then draws the next screen.</summary>
        static void Click(string label, string next)
        {
            if (!ImguiHost.Buttons.TryGetValue(label, out var r))
            {
                Fail($"no '{label}' button to click on the way to {next}");
                return;
            }
            Input.Mouse = r.center;
            Frame(); // the move
            Input.Pressed[0] = Input.Held[0] = true;
            Frame();
            Input.Held[0] = false;
            Input.Released[0] = true;
            frames(1);
            Frame();
            int clicks = ImguiHost.Stats.Clicks;
            Input.Mouse = new Vector2(-1000, -1000);
            if (clicks == 0) Fail($"'{label}' did not take the click");
            Draw(next);
        }

        static void Key(KeyCode code, string next)
        {
            Input.KeysDown.Add((code, '\0'));
            Frame();
            Draw(next);
        }

        static void Toggle(Action<bool> set, string screen, Action inside = null)
        {
            set(true);
            Draw(screen);
            inside?.Invoke();
            set(false);
        }

        // ---------------------------------------------------------------- windowed screenshots

        /// <summary>
        /// <c>-SpaceGodUiShots</c> (tools/play.ps1 -Track CSharp -Mode shots): in a real window, a screenshot of each main
        /// screen with the HUD drawn (Unreal's <c>shot showui</c>, saved under <c>-capturedir=</c> as CaptureRunner's are):
        /// the main menu, the game HUD, Help, the galaxy map, the pause menu and its Display page; then quits.
        /// </summary>
        public static IEnumerator Shots()
        {
            static IEnumerator Frames(int n)
            {
                for (int i = 0; i < n; i++) yield return null;
            }
            yield return Frames(90);
            if (MainMenu.Showing)
            {
                yield return CaptureRunner.Shot("csharp-ui-main-menu");
                GameLaunch.NewGame();
            }
            yield return Frames(240);
            var app = Bootstrap.App;
            if (app == null)
            {
                UnrealLogger.LogError(Log, "UI shots: no game");
                Application.Quit();
                yield break;
            }
            yield return CaptureRunner.Shot("csharp-ui-hud");
            app.ShowHelp = true;
            yield return Frames(10);
            yield return CaptureRunner.Shot("csharp-ui-help");
            app.ShowHelp = false;
            app.ShowGalaxyMap = true;
            yield return Frames(30);
            yield return CaptureRunner.Shot("csharp-ui-galaxy-map");
            app.ShowGalaxyMap = false;
            app.ShowMarket = true;
            app.ShowCargo = true;
            yield return Frames(10);
            yield return CaptureRunner.Shot("csharp-ui-market-cargo");
            app.ShowMarket = false;
            app.ShowCargo = false;
            var pause = typeof(UnitySpaceApp).GetField("pauseMenu", BindingFlags.Instance | BindingFlags.NonPublic)?.GetValue(app) as PauseMenu;
            if (pause != null)
            {
                pause.Open();
                yield return Frames(10);
                yield return CaptureRunner.Shot("csharp-ui-pause-menu");
                pause.Close();
            }
            UnrealLogger.Log(Log, "UI shots: done, in " + CaptureRunner.ShotFolder);
            Application.Quit();
        }
    }
}
