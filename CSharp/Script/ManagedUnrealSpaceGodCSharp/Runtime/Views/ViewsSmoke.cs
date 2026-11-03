using System;
using System.Collections.Generic;
using System.Globalization;
using Unity.SpaceGod.Core;
using Unity.SpaceGod.Rendering;
using UnityEngine;
using UnrealSharp;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;

namespace Unity.SpaceGod.Views
{
    /// <summary>
    /// The views' smoke check (US-T27), independent of the app (US-T26): builds Sol the way UnitySpaceApp.Awake and
    /// UpdateViews do -- ScaleModel at gameplay blend, Scale.Layout, the camera nine display radii off Earth looking
    /// at it, Starfield, SkyView, InterstellarView, one BodyView per body placed camera-relative with its body-fixed
    /// frame (BodyOrientation.Frame) and weather, the star lighting -- plus a ShipView for every ship of the system
    /// (Kenney craft and procedural hulls alike, hidden: their placement is the app's), and a LineStrip for Earth's
    /// orbit; pushes one frame to Unreal through UnityBridge and logs what was built. With a camera actor it also shows
    /// that frame (the windowed screenshot run).
    /// </summary>
    public sealed class ViewsSmoke
    {
        const string Log = "LogSpaceGod";
        readonly Galaxy galaxy;
        readonly StarSystem sol;
        readonly ScaleModel scale = new ScaleModel { Blend = 1 };
        DVec3[] scene = Array.Empty<DVec3>();
        DVec3 cameraScene;
        readonly List<BodyView> bodies = new List<BodyView>();
        readonly List<ShipView> ships = new List<ShipView>();
        Camera camera = null!;
        Transform root = null!;
        SkyView sky = null!;
        InterstellarView interstellar = null!;
        LineStrip orbit = null!;
        ACameraActor? cameraActor;

        public ViewsSmoke(Galaxy galaxy)
        {
            this.galaxy = galaxy;
            sol = galaxy.Find("Sol System") ?? galaxy.Systems[0];
        }

        Vector3 Relative(DVec3 s)
        {
            var d = s - cameraScene;
            return new Vector3((float)d.X, (float)d.Y, (float)d.Z);
        }

        /// <summary>Builds every view and pushes the first frame; returns the log line.</summary>
        public string Build(AActor host, double jd, bool showOnScreen)
        {
            var started = System.Diagnostics.Stopwatch.StartNew();
            UnityBridge.Begin(host);
            var catalog = sol.Catalog;
            scene = new DVec3[catalog.Count];
            scale.Layout(sol.Ephemeris, scene);

            // The camera as UnitySpaceApp.Awake places it: nine display radii off Earth, looking at it.
            var camGo = new GameObject("Main Camera") { tag = "MainCamera" };
            camera = camGo.AddComponent<Camera>();
            camera.fieldOfView = 60f;
            camera.nearClipPlane = 0.01f;
            Camera.main = camera;
            int earth = catalog["Earth"].Index;
            cameraScene = scene[earth] + new DVec3(-0.6, 0.35, -1).Normalized * (scale.DisplayRadius(catalog[earth]) * 9);
            var look = (scene[earth] - cameraScene).Normalized;
            camGo.transform.rotation = Quaternion.Euler((float)(-Math.Asin(look.Y) * Mathf.Rad2Deg), (float)(Math.Atan2(look.X, look.Z) * Mathf.Rad2Deg), 0);
            RenderBackend.Active.SetupCamera(camera, CameraRole.Main);

            root = new GameObject("World").transform;
            var starfield = Starfield.Create(root);
            sky = new SkyView(root, starfield);
            sky.Show(sol.Name);
            interstellar = new InterstellarView(galaxy, root);
            var systemRoot = new GameObject(sol.Name).transform;
            systemRoot.SetParent(root, false);
            for (int i = 0; i < catalog.Count; i++) bodies.Add(new BodyView(catalog[i], systemRoot));

            int kenney = 0, procedural = 0, failed = 0;
            foreach (var ship in sol.World.Fleet.Ships)
            {
                if (ship.Destroyed) continue;
                try
                {
                    var view = new ShipView(ship, systemRoot);
                    view.SetVisible(false);
                    ships.Add(view);
                    if (ShipModels.KenneyModelFor(ship) != null) kenney++; else procedural++;
                }
                catch (Exception e)
                {
                    if (failed++ < 3) UnityBridge.Log($"ShipView {ship.Name}: {e.GetType().Name}: {e.Message}", 2);
                }
            }

            orbit = new LineStrip("Earth orbit", systemRoot);
            Place();
            var s = UnityBridge.Sync();

            if (showOnScreen) ShowThroughCamera(host);
            return string.Format(CultureInfo.InvariantCulture,
                "Views up in {0} ms: {1} bodies, {2} ship views ({3} Kenney-class, {4} procedural, {5} failed), starfield + sky ({6}) + interstellar ({7} stars) + 1 line; Unreal: {8}",
                started.ElapsedMilliseconds, bodies.Count, ships.Count, kenney, procedural, failed, sky.Current?.Id ?? "none",
                galaxy.Systems.Count, s);
        }

        /// <summary>Positions everything for this frame, as UnitySpaceApp.UpdateViews does.</summary>
        void Place()
        {
            var catalog = sol.Catalog;
            var eph = sol.Ephemeris;
            double jd = eph.Jd;
            var forward = camera.transform.rotation * Vector3.forward;
            for (int i = 0; i < catalog.Count; i++)
            {
                var rel = eph.Relative(i);
                var orbitNormal = DVec3.Cross(rel, eph.RelativeVelocityKmS(i));
                BodyOrientation.Frame(catalog[i], jd, -rel, orbitNormal, out var north, out var meridian);
                float radius = catalog[i].IsVisible ? (float)scale.DisplayRadius(catalog[i]) : 1f;
                bodies[i].Apply(Relative(scene[i]), radius, north, meridian, false, forward);
                bodies[i].UpdateWeather(jd, 3600);
            }
            // The Sun lights the shaders (UnitySpaceApp.UpdateLights, Sol: the Sun alone).
            RenderBackend.Active.Lighting = new StarLighting
            {
                SunPosition = Relative(scene[0]),
                SunColor = UnitySpaceApp.LightColor(catalog[0], 1),
            };
            interstellar.Update(sol.PositionLy, camera.transform.rotation, 0f);

            // Earth's orbit around the Sun: 256 samples of a circle at its scene distance, in the ecliptic.
            int earth = catalog["Earth"].Index;
            double r = (scene[earth] - scene[0]).Length;
            var points = new Vector3[257];
            var sun = Relative(scene[0]);
            for (int k = 0; k < points.Length; k++)
            {
                double a = Math.PI * 2 * k / (points.Length - 1);
                points[k] = sun + new Vector3((float)(Math.Cos(a) * r), 0, (float)(Math.Sin(a) * r));
            }
            orbit.SetPoints(points, points.Length, new Color(0.4f, 0.6f, 1f, 0.5f));
        }

        /// <summary>One more frame (the windowed run keeps the scene up until its screenshot).</summary>
        public void Frame(float dt)
        {
            UnityBridge.Tick(dt);
            sky.Update(dt);
            Place();
            UnityBridge.Sync();
            ApplyCamera();
        }

        void ShowThroughCamera(AActor host)
        {
            var controller = UGameplayStatics.GetPlayerController(0);
            if (controller == null) return;
            cameraActor = UObject.SpawnActor<ACameraActor>(new TSubclassOf<ACameraActor>(), new FTransform(), ESpawnActorCollisionHandlingMethod.AlwaysSpawn, null, host);
            if (cameraActor == null) return;
            camera.Unreal = cameraActor.CameraComponent;
            RenderBackend.Active.SetupCamera(camera, CameraRole.Main);
            controller.SetViewTargetWithBlend(cameraActor);
            // Unreal's near plane is global (reversed-Z, no far plane): a thousandth of the way to Earth.
            int earth = sol.Catalog["Earth"].Index;
            double near = Math.Max(1e-4, (scene[earth] - cameraScene).Length * 1e-3);
            SystemLibrary.ExecuteConsoleCommand("r.SetNearClipPlane " + near.ToString("R", CultureInfo.InvariantCulture));
            ApplyCamera();
        }

        void ApplyCamera()
        {
            if (cameraActor == null) return;
            // The camera stays at the origin (the scene is camera-relative) and turns as the Unity camera does.
            cameraActor.SetActorTransform(new FTransform(UnrealSpace.CameraRotationToUnreal(camera.transform.rotation), new FVector(0, 0, 0)));
            var c = cameraActor.CameraComponent;
            if (c != null)
            {
                c.FieldOfView = 2f * Mathf.Atan(Mathf.Tan(camera.fieldOfView * 0.5f * Mathf.Deg2Rad) * (16f / 9f)) * Mathf.Rad2Deg;
                c.ConstrainAspectRatio = false;
            }
        }
    }
}
