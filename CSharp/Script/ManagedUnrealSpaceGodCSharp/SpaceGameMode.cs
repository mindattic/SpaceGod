using System.Globalization;
using UnrealSharp.Attributes;
using UnrealSharp.Engine;
using UnrealSharp.Log;
using Unity.SpaceGod.Core;

namespace ManagedUnrealSpaceGodCSharp;

/// <summary>
/// The C# track's game mode: it starts Unity.SpaceGod's own simulation, unchanged, the way UnitySpaceApp.Awake
/// does (a <see cref="SimClock"/> and <see cref="Galaxy.CreateDefault"/>), and steps it the way
/// UnitySpaceApp.Update does (<c>Clock.Advance(dt); Galaxy.Update(Clock.Jd)</c>).
/// The engine-side game (views, HUD, camera, co-op) is later work (MIGRATION.md, order of work 5-6).
/// </summary>
[UClass]
public partial class ASpaceGameMode : AGameModeBase
{
    const string Log = "LogSpaceGod";

    /// <summary>The fixed start the C++ track's parity checks use (JD 2461348.0 = 2026-11-03 12:00 UTC), so both tracks log comparable numbers.</summary>
    public const double StartJd = 2461348.0;

    /// <summary>Ticks stepped at BeginPlay as the smoke proof, at 1/60 s each.</summary>
    const int SmokeTicks = 5;

    SimClock? clock;
    Galaxy? galaxy;
    StarSystem? sol;
    int earth = -1;
    double sinceReport;

    public override void BeginPlay()
    {
        base.BeginPlay();

        var started = System.Diagnostics.Stopwatch.StartNew();
        clock = new SimClock(StartJd);
        clock.SetWarp(SimClock.DefaultWarpIndex);
        galaxy = Galaxy.CreateDefault(clock.Jd);
        sol = galaxy.Find("Sol System") ?? galaxy.Systems[0];
        earth = sol.Catalog.TryGet("Earth", out var e) ? e.Index : -1;
        UnrealLogger.Log(Log, $"Unity.SpaceGod Core up in {started.ElapsedMilliseconds} ms: {galaxy.Systems.Count} systems, " +
            $"{sol.Catalog.Count} bodies in {sol.Name}, {sol.World.TraderCount()} traders (.NET {System.Environment.Version})");

        for (int i = 1; i <= SmokeTicks; i++)
        {
            Step(1.0 / 60.0);
            Report($"tick {i}");
        }

        if (System.Environment.GetCommandLineArgs().Any(a => a.Equals("-SpaceGodSmoke", StringComparison.OrdinalIgnoreCase)))
        {
            // Through the engine, not Environment.Exit: the runtime is hosted by Unreal and must shut down with it.
            UnrealLogger.Log(Log, "SpaceGodSmoke: done, quitting");
            SystemLibrary.ExecuteConsoleCommand("quit");
        }
    }

    public override void Tick(float deltaSeconds)
    {
        base.Tick(deltaSeconds);
        if (galaxy == null) return;
        Step(deltaSeconds);
        sinceReport += deltaSeconds;
        if (sinceReport >= 5)
        {
            sinceReport = 0;
            Report("running");
        }
    }

    void Step(double realSeconds)
    {
        clock!.Advance(realSeconds);
        galaxy!.Update(clock.Jd);
    }

    void Report(string label)
    {
        var p = earth >= 0 ? sol!.Ephemeris.Helio(earth) : DVec3.Zero;
        var inv = CultureInfo.InvariantCulture;
        UnrealLogger.Log(Log, string.Format(inv,
            "[{0}] JD {1:F6} = {2:yyyy-MM-dd HH:mm:ss} UTC, warp {3}; Earth helio km = ({4:F1}, {5:F1}, {6:F1}), |r| = {7:F6} AU",
            label, clock!.Jd, clock.Utc, clock.Warp.Label, p.X, p.Y, p.Z, p.Length / Astro.AuKm));
    }
}
