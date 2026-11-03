// US-T39: the headless two-instance co-op proof (new file; tools/play.ps1 -Track CSharp -Mode coop-smoke).
using System;
using System.Diagnostics;
using Unity.SpaceGod.Core;
using Unity.SpaceGod.Input;
using UnrealSharp.Engine;

namespace Unity.SpaceGod.Coop
{
    /// <summary>
    /// <c>-SpaceGodCoopSmoke=host|client</c>: a listen server and a client on localhost (-nullrhi). The client joins (the
    /// host's save comes over and both load it), then plays a few things as a player would, by the keys and the picker: '.'
    /// (time warp up: a request the host grants for everyone), '3' (select Earth: every peer sees it), and a station's seat
    /// through the picker (a take command the host's frame grants, through SeatAuthority on every peer). Both log a digest
    /// of the shared state every <see cref="CoopPeer.DigestEvery"/> frames; the host compares them and logs each match or
    /// desync. After <see cref="ClientFrames"/> replayed frames the client says bye and both quit.
    /// </summary>
    public static class CoopSmoke
    {
        public static readonly string Role = Bootstrap.CommandLineValue("-SpaceGodCoopSmoke")?.ToLowerInvariant();
        public static bool Active => Role != null;
        public const int ClientFrames = 900;
        static readonly TimeSpan Timeout = TimeSpan.FromMinutes(6);
        static readonly Stopwatch Since = Stopwatch.StartNew();

        /// <summary>The client's scripted keyboard and mouse (the link puts it in front of the real device).</summary>
        public static ScriptedInputDevice Script;

        static long byeAt = -1;
        static long replayed;
        static int quitIn = -1;
        static bool finished;
        static string warpBefore;

        static void Finish(string why, CoopPeer peer)
        {
            if (finished) return;
            finished = true;
            CoopPeer.Log("smoke (" + Role + ") done: " + why + "; frame " + peer.Frame + ", " +
                         (peer.IsHost ? peer.DigestMatches + " digest matches, " + peer.Desyncs + " desyncs" : peer.ClockCorrections + " clock corrections") +
                         ", " + Since.Elapsed.TotalSeconds.ToString("F0") + " s; quitting");
            SystemLibrary.ExecuteConsoleCommand("quit");
        }

        public static void HostFrame(CoopPeer peer, UnitySpaceApp app, FrameRecord rec)
        {
            if (Role != "host" || finished) return;
            if (peer.AnyBye && byeAt < 0) byeAt = rec.Frame;
            if (byeAt >= 0 && rec.Frame >= byeAt + 30) Finish("the client said bye", peer);
            else if (Since.Elapsed > Timeout) Finish("TIMEOUT", peer);
        }

        /// <summary>Each engine tick on the client, before its frames: the countdown to quitting and the timeout.</summary>
        public static void ClientTick(CoopPeer peer)
        {
            if (Role != "client" || finished) return;
            if (quitIn > 0 && --quitIn == 0) Finish("said bye", peer);
            else if (Since.Elapsed > Timeout) Finish("TIMEOUT", peer);
        }

        /// <summary>After each replayed host frame on the client: the scripted player.</summary>
        public static void ClientFrame(CoopPeer peer, UnitySpaceApp app)
        {
            if (Role != "client" || finished || Script == null) return;
            long n = ++replayed;
            switch (n)
            {
                case 30:
                    warpBefore = app.Clock.Warp.Label;
                    Script.Tap(Key.Period);
                    CoopPeer.Log("smoke: client presses '.' (time warp up) at frame " + peer.Frame + ", warp " + warpBefore);
                    break;
                case 150:
                    CoopPeer.Log("smoke: client's warp " + warpBefore + " -> " + app.Clock.Warp.Label + " (the host's clock) at frame " + peer.Frame);
                    break;
                case 180:
                    Script.Tap(Key.Digit3);
                    CoopPeer.Log("smoke: client presses '3' (select) at frame " + peer.Frame);
                    break;
                case 240:
                    CoopPeer.Log("smoke: client selected " + app.CoopTargetName(app.CoopTargetKey(app.Selected)) + "; the host's roster has it as '" +
                                 peer.Players[peer.LocalSeat]?.Selection + "'");
                    break;
                case 270:
                {
                    var stations = Embodiables.StationsIn(app.World);
                    IEmbodiable seat = stations.Count > 0 ? stations[0] : null;
                    if (seat == null)
                        foreach (var b in app.World.War.Battles)
                        {
                            var seats = Embodiables.SeatsIn(app.World, b);
                            foreach (var s in seats)
                                if (!(s is ShipHelm)) { seat = s; break; }
                            if (seat != null) break;
                        }
                    if (seat == null) { CoopPeer.Warn("smoke: no seat to take in " + app.ActiveSystem.Name); break; }
                    CoopPeer.Log("smoke: client picks seat '" + seat.Name + "' (key " + app.CoopSeatKey(seat) + ") at frame " + peer.Frame);
                    app.PickSeat(seat);
                    break;
                }
                case 480:
                    CoopPeer.Log("smoke: client holds " + (app.Seat?.Name ?? "nothing") + " at frame " + peer.Frame + ", warp " + app.Clock.Warp.Label);
                    break;
                case ClientFrames:
                    peer.SayBye();
                    quitIn = 30;
                    break;
            }
        }
    }
}
