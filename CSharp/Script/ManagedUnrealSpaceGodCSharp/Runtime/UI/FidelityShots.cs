using System;
using System.Collections;
using System.Linq;
using System.Reflection;
using Unity.SpaceGod.Core;
using UnityEngine;
using UnrealSharp.Log;

namespace Unity.SpaceGod.UI
{
    /// <summary>
    /// <c>-SpaceGodFidelityShots</c> (tools/play.ps1 -Track CSharp -Mode fidelity, US-T40): in a real window, the screens
    /// that check the C# track against Unity.SpaceGod where the port was not exact before -- the asteroid belt from the
    /// system overview (GPU-propagated, 1-3 px), a Kenney miner and courier up close (import orientation and scale), the
    /// model viewer (F9: its own camera, layer and background), the spawn grid's ship thumbnails (layer-isolated, the
    /// original's background) -- each saved as <c>docs/images/csharp-fidelity-*.png</c> (<c>-capturedir=</c>), with the
    /// frame times logged; then quits.
    /// </summary>
    public static class FidelityShots
    {
        const string Log = "LogSpaceGod";

        static IEnumerator Frames(int n)
        {
            for (int i = 0; i < n; i++) yield return null;
        }

        /// <summary>Waits for the camera to arrive (SceneTour waits 2.6 s of real time, at Unity's frame rate).</summary>
        static IEnumerator Settle(UnitySpaceApp app, float seconds)
        {
            float end = Time.unscaledTime + seconds;
            int frames = 0;
            while (Time.unscaledTime < end || frames < 30) { frames++; yield return null; }
        }

        public static IEnumerator Run()
        {
            yield return Frames(60);
            if (MainMenu.Showing) GameLaunch.NewGame();
            yield return Frames(120);
            var app = Bootstrap.App;
            if (app == null)
            {
                UnrealLogger.LogError(Log, "Fidelity shots: no game");
                Application.Quit();
                yield break;
            }
            LogFrameTimes("warm");

            app.FocusSystemOverview();
            yield return Settle(app, 3f);
            LogFrameTimes("system overview");
            yield return CaptureRunner.Shot("csharp-fidelity-belt-gameplay");

            foreach (var cls in new[] { ShipClass.Miner, ShipClass.Courier })
            {
                var ship = app.Fleet.Ships.FirstOrDefault(s => !s.Destroyed && s.Controller.Phase != ShipPhase.Landed && s.Spec.Class == cls);
                if (ship == null) continue;
                app.Select(Target.OfShip(ship));
                app.SpaceCamera.FocusOn(Target.OfShip(ship), 3.5);
                yield return Settle(app, 3f);
                Describe(app, ship);
                yield return CaptureRunner.Shot("csharp-fidelity-kenney-" + cls.ToString().ToLowerInvariant());
            }

            // The model viewer as F9 opens it (on its first model, then the courier's racer).
            var viewer = UnityEngine.Object.FindAnyObjectByType<ModelViewer>();
            var open = typeof(ModelViewer).GetMethod("Open", BindingFlags.Instance | BindingFlags.NonPublic);
            var close = typeof(ModelViewer).GetMethod("Close", BindingFlags.Instance | BindingFlags.NonPublic);
            if (viewer != null && open != null)
            {
                open.Invoke(viewer, new object[] { 0 });
                yield return Frames(20);
                UnrealLogger.Log(Log, "Fidelity shots: model viewer open, screen camera " + UnityBridge.ScreenCamera?.gameObject.name);
                yield return CaptureRunner.Shot("csharp-fidelity-model-viewer");
                close?.Invoke(viewer, null);
                yield return Frames(5);
            }
            else UnrealLogger.LogError(Log, "Fidelity shots: no ModelViewer");

            app.ShowSpawn = true;
            yield return Frames(20);
            UnrealLogger.Log(Log, "Fidelity shots: spawn grid, last thumbnail capture drew " + UnityBridge.LastCaptureShown + " primitive(s)");
            yield return CaptureRunner.Shot("csharp-fidelity-spawn-thumbnails");
            app.ShowSpawn = false;

            UnrealLogger.Log(Log, "Fidelity shots: done, in " + CaptureRunner.ShotFolder);
            Application.Quit();
        }

        /// <summary>Logs where a focused ship's model is and what draws it (camera, near plane, each renderer).</summary>
        static void Describe(UnitySpaceApp app, Ship ship)
        {
            var views = typeof(UnitySpaceApp).GetField("shipViews", BindingFlags.Instance | BindingFlags.NonPublic)?.GetValue(app) as Views.ShipView[];
            var view = views != null && ship.Id < views.Length ? views[ship.Id] : null;
            var cam = app.UnityCamera;
            var lines = $"Fidelity shots: {ship.Name} ({ship.Spec.Class}): camera near {cam.nearClipPlane:G3}, fov {cam.fieldOfView:F1}";
            if (view == null) { UnrealLogger.Log(Log, lines + ", no ship view"); return; }
            lines += $", root at {view.Root.transform.position} scale {view.Root.transform.lossyScale} active {view.Root.activeInHierarchy}";
            foreach (var r in view.Root.GetComponentsInChildren<Renderer>(true))
            {
                var m = r.sharedMaterial;
                lines += $"\n   {r.gameObject.name}: enabled {r.enabled} active {r.gameObject.activeInHierarchy} layer {r.gameObject.layer} pos {r.transform.position} scale {r.transform.lossyScale} " +
                    $"mesh {(r.GetComponent<MeshFilter>()?.sharedMesh?.name ?? "-")} material {(m != null ? m.shader?.name : "-")} block {(r.Block != null ? "yes" : "no")}";
            }
            UnrealLogger.Log(Log, lines);
        }

        static void LogFrameTimes(string where) =>
            UnrealLogger.Log(Log, $"Fidelity shots [{where}]: {UnityBridge.Timing}, GUI {ASpaceHud.GuiMs:F1} ms");
    }
}
