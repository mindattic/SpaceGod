// US-T39: co-op's side of the app (new file, a partial of the ported UnitySpaceApp; docs/MIGRATION.md "Co-op"). The ported
// originals call in here only at lines marked US-T39, and every call is the original's own behaviour when CoopPeer.Current is
// null (single-player).
using System;
using System.Collections.Generic;
using System.Text;
using Unity.SpaceGod.Cameras;
using Unity.SpaceGod.Coop;
using Unity.SpaceGod.Core;
using UnityEngine;

namespace Unity.SpaceGod
{
    public sealed partial class UnitySpaceApp
    {
        LocalSession coopWatched;
        ClockState coopLeft;
        bool coopLeftValid;
        double coopSimRate = double.NaN;
        bool coopStepped;
        string coopLastControl;
        string coopLastSelection = "n";

        // ---------------------------------------------------------------- the frame's clock step

        /// <summary>
        /// <c>Clock.Advance(dt)</c> (the original's), in lockstep in co-op. On the host: the clients' clock requests, the
        /// frame's commands, then the clock as it stands is recorded and advanced. On a client: the same commands, any change
        /// this machine made to its clock since the last frame goes to the host as a request, then the clock is put in the
        /// host's state and advanced by the host's seconds, which lands it on the host's date to the bit.
        /// </summary>
        void CoopAdvance(float dt)
        {
            var peer = CoopPeer.Current;
            if (peer == null)
            {
                Clock.Advance(dt);
                return;
            }
            CoopWatchSession();
            coopStepped = false;
            if (peer.IsHost)
            {
                var rec = peer.HostOpenFrame();
                foreach (var (seat, request) in peer.TakeClockRequests()) CoopClockRequest(seat, request);
                CoopApplyEntries(peer, rec.Entries);
                CoopShareSelection(peer);
                rec.Before = ClockState.Of(Clock);
                Clock.Advance(dt);
                rec.JdAfter = Clock.Jd;
            }
            else
            {
                var rec = peer.ClientRecord;
                if (rec == null) return; // nothing from the host yet: time holds
                CoopApplyEntries(peer, rec.Entries);
                CoopShareSelection(peer);
                if (coopLeftValid)
                {
                    // The keys (. , Space) or this machine's own rules (a seat taken drops to real time) moved the clock: the host's call.
                    if (Clock.WarpIndex != coopLeft.Warp) peer.RequestClock("warp " + Clock.WarpIndex);
                    if (Clock.PauseHeld != coopLeft.PauseHeld) peer.RequestClock("pause " + (Clock.PauseHeld ? 1 : 0));
                }
                rec.Before.ApplyTo(Clock);
                Clock.Advance(dt);
                if (Clock.Jd != rec.JdAfter)
                {
                    peer.ClockCorrections++;
                    Clock.SetJd(rec.JdAfter);
                }
                peer.Consumed = true;
            }
            coopLeft = ClockState.Of(Clock);
            coopLeftValid = true;
            coopSimRate = Clock.Warp.SimSecondsPerSecond * Clock.Speed;
            coopStepped = true;
        }

        /// <summary>A client's warp or pause, on the host's clock (the one every peer's follows).</summary>
        void CoopClockRequest(int seat, string request)
        {
            var f = request.Split(' ');
            if (f.Length < 2) return;
            if (f[0] == "warp")
            {
                Clock.SetWarp(Wire.ParseI(f[1]));
                ShowToast(CoopPeer.Current.NameOf(seat) + ": time " + Clock.Warp.Label);
            }
            else if (f[0] == "pause" && (f[1] == "1") != Clock.PauseHeld) TogglePause();
        }

        /// <summary>The frame's commands, in order, on every peer: session commands through the original's session (its
        /// SeatAuthority validates and applies them), roster entries to the co-op peer.</summary>
        void CoopApplyEntries(CoopPeer peer, List<string> entries)
        {
            foreach (var e in entries)
            {
                if (!e.StartsWith("s ", StringComparison.Ordinal))
                {
                    peer.ApplyRosterEntry(e);
                    continue;
                }
                string codec = e.Substring(2);
                string key = null;
                int at = codec.IndexOf(" @", StringComparison.Ordinal);
                if (at >= 0)
                {
                    key = codec.Substring(at + 2);
                    codec = codec.Substring(0, at);
                }
                IGameCommand cmd;
                try { cmd = CommandCodec.Read(codec); }
                catch (FormatException x)
                {
                    CoopPeer.Warn("bad command '" + codec + "': " + x.Message);
                    continue;
                }
                // A take names its seat by key: each peer's session gives the seat its id as it is first named (in the frames'
                // order, so the same id everywhere); the sender's id would mean nothing here.
                if (cmd is TakeSeatCommand take && key != null)
                {
                    var seat = CoopResolveSeat(key);
                    if (seat == null)
                    {
                        CoopPeer.Log(cmd.Player + " asked for seat '" + key + "', which is gone");
                        continue;
                    }
                    cmd = new TakeSeatCommand(take.Player, session.IdOf(seat));
                }
                session.Submit(cmd);
            }
            session.ApplyPending();
        }

        void CoopShareSelection(CoopPeer peer)
        {
            string key = CoopTargetKey(Selected);
            if (key == coopLastSelection) return;
            coopLastSelection = key;
            peer.Selected(key);
        }

        // ---------------------------------------------------------------- adventures

        /// <summary>
        /// <c>World.UpdateAdventures(Clock.Jd, PlayerHelioKm())</c> when <paramref name="wanted"/> (the original's line). In
        /// co-op the host's call is everyone's: whether it ran this frame (the host between the stars skips it) and from the
        /// host's player point, which places the adventures; a client's own camera does not.
        /// </summary>
        void CoopUpdateAdventures(bool wanted)
        {
            var peer = CoopPeer.Current;
            if (peer == null)
            {
                if (wanted) World.UpdateAdventures(Clock.Jd, PlayerHelioKm());
                return;
            }
            var rec = peer.IsHost ? peer.Open : peer.ClientRecord;
            if (rec == null) return;
            if (peer.IsHost)
            {
                rec.Adventures = wanted;
                if (wanted) rec.AdventurePoint = PlayerHelioKm();
            }
            if (rec.Adventures) World.UpdateAdventures(Clock.Jd, rec.AdventurePoint);
        }

        // ---------------------------------------------------------------- seats

        void CoopWatchSession()
        {
            if (coopWatched == session) return;
            coopWatched = session;
            session.EventRaised += CoopSessionEvent;
        }

        /// <summary>What the session's authority said, on every peer: logged, and for this machine's player the seat's view
        /// (what <see cref="TakeSeat"/> does once a seat is held), its loss, or the refusal.</summary>
        void CoopSessionEvent(IGameEvent e)
        {
            var peer = CoopPeer.Current;
            if (peer == null) return;
            bool mine = e.Player == session.LocalPlayer;
            switch (e)
            {
                case SeatTaken t:
                {
                    var seat = session.State.Seat(t.Seat);
                    CoopPeer.Log(peer.NameOf(t.Player.Seat) + " (" + t.Player + ") takes seat " + t.Seat + " (" + seat?.Name + ") at frame " + peer.Frame);
                    if (mine && seat != null) CoopSeatTakenHere(seat);
                    break;
                }
                case SeatReleased r:
                    CoopPeer.Log(peer.NameOf(r.Player.Seat) + " (" + r.Player + ") lets go of seat " + r.Seat + (r.Reason != null ? ": " + r.Reason : "") + " at frame " + peer.Frame);
                    if (mine && r.Reason != null) ReturnToCommand(r.Reason, session.State.Seat(r.Seat)?.Name);
                    break;
                case CommandRefused c:
                    CoopPeer.Log(peer.NameOf(c.Player.Seat) + "'s " + (c.Command != null ? CommandCodec.Write(c.Command).Split(' ')[0] : "command") + " refused: " + c.Reason);
                    if (mine) ShowToast("Can't: " + c.Reason);
                    break;
            }
        }

        /// <summary>The tail of <see cref="TakeSeat"/> (the original's lines), run when the host's frame hands the seat over.</summary>
        void CoopSeatTakenHere(IEmbodiable next)
        {
            var pose = next.PoseAt(Clock.Jd);
            seatAim = ToVector(pose.Forward.SwapYZ).normalized;
            Clock.SetWarp(0);
            SpaceCamera.EnterPilotChase(SeatTarget(next), next is FighterSeat ? 7 : 2.5);
            if (next is MountedTurret gun)
            {
                SpaceCamera.PilotView = Quaternion.LookRotation(seatAim, MountRoof(pose, seatAim));
                SpaceCamera.PilotEye = () => MountEye(gun);
            }
            else SpaceCamera.PilotEye = null;
            ShowToast((next is FighterSeat ? "Flying " : "Manning ") + next.Name + ", " + Clock.Warp.Label + SeatKeys(next));
        }

        /// <summary>
        /// In co-op a take goes to the host as a command, with the seat's key; the view follows when a frame grants it
        /// (<see cref="CoopSessionEvent"/>). False in single-player: <see cref="TakeSeat"/> goes on as the original.
        /// </summary>
        bool CoopTakeSeat(IEmbodiable next)
        {
            var peer = CoopPeer.Current;
            if (peer == null) return false;
            string key = CoopSeatKey(next);
            if (key == null)
            {
                ShowToast("Can't share " + next.Name + " in co-op: man a gun, a fighter or a station");
                return true;
            }
            // Id 0: the sender's session must not name the seat (that would hand out an id the others never saw).
            peer.Submit("s " + CommandCodec.Write(new TakeSeatCommand(session.LocalPlayer, 0)) + " @" + key);
            ShowToast("Asking for " + next.Name + "...");
            return true;
        }

        /// <summary>A release or a move up to the host, in co-op: to the host as a command (true); false in single-player.</summary>
        bool CoopRoutes(IGameCommand command)
        {
            var peer = CoopPeer.Current;
            if (peer == null) return false;
            if (command is ReleaseSeatCommand && HeldNonHelm == null) return true; // nothing to hand back
            if (!peer.Applying) peer.Submit("s " + CommandCodec.Write(command));
            return true;
        }

        /// <summary>
        /// Co-op's <see cref="UpdateSeat"/>: this machine's stick goes to the host as the local player's control command
        /// (when it changes), and every player's seat is stepped, here as on every peer, on the frame's sim seconds. The
        /// cockpit's extra keys (<see cref="UpdateCockpit"/>: flight assist, locks, wing orders, power) act on the Core
        /// directly and are not shared yet, so they are off in co-op.
        /// </summary>
        void CoopUpdateSeat(float dt)
        {
            var peer = CoopPeer.Current;
            var held = HeldNonHelm;
            var rot = SpaceCamera.Rotation;
            var up = rot * Vector3.up;
            if (held != null)
            {
                var look = ClickPanelOpen ? Vector2.zero : Input.MouseDelta;
                var right = rot * Vector3.right;
                seatAim = (Quaternion.AngleAxis(look.x * SpaceCamera.LookSensitivity, up) *
                           Quaternion.AngleAxis(-SpaceCamera.LookY(look.y) * SpaceCamera.LookSensitivity, right) * seatAim).normalized;
                var input = new EmbodimentInput
                {
                    Aim = new DVec3(seatAim.x, seatAim.y, seatAim.z).SwapYZ,
                    Throttle = Mathf.Clamp(Input.Move.z, -1f, 1f),
                    StrafeRight = Mathf.Clamp(Input.Move.x, -1f, 1f),
                    StrafeUp = Mathf.Clamp(Input.Move.y, -1f, 1f),
                    Roll = Mathf.Clamp(Input.Roll, -1f, 1f),
                    Boost = Input.Boost,
                    Primary = Input.Fire && !ClickPanelOpen,
                    Secondary = Input.LookHeld && !ClickPanelOpen,
                };
                string line = CommandCodec.Write(new SeatControlCommand(session.LocalPlayer, input));
                if (line != coopLastControl)
                {
                    coopLastControl = line;
                    peer.Submit("s " + line);
                }
            }
            else coopLastControl = null;
            if (!coopStepped) return;
            session.Advance(dt * coopSimRate);
            var still = HeldNonHelm;
            if (still != null && still == held)
            {
                var pose = held.PoseAt(Clock.Jd);
                var roof = ToVector(pose.Up.SwapYZ);
                SpaceCamera.PilotView = Quaternion.LookRotation(seatAim, Vector3.Dot(roof, seatAim) > 0.99f ? up : roof);
            }
        }

        /// <summary>The pilot loop (the helm) acts on the Core from this machine's keys every frame and is not lockstepped
        /// yet: in co-op it is refused (true) and the other seats are the way in.</summary>
        bool CoopRefusesHelm()
        {
            if (CoopPeer.Current == null) return false;
            ShowToast("The helm is single-player in this build: in co-op man a gun, a fighter or a station (' ; / F9)");
            return true;
        }

        /// <summary>A trip to another star from a client: the host picks the system everyone is in (true: refused).</summary>
        bool CoopRefusesTravel()
        {
            var peer = CoopPeer.Current;
            if (peer == null || peer.IsHost) return false;
            ShowToast("In co-op " + peer.NameOf(0) + " (the host) picks the star system");
            return true;
        }

        /// <summary>At the end of <see cref="Activate"/>: the host's frame tells clients to make the same system the active one.</summary>
        void CoopActivated(StarSystem system)
        {
            var peer = CoopPeer.Current;
            if (peer == null || !peer.IsHost || peer.Applying || peer.Open == null) return;
            peer.Open.Post.Add("activate " + system.Name);
        }

        /// <summary>At the end of <see cref="ApplyLoaded"/>: the clock is a new one; a load the host made itself goes to everyone.</summary>
        void CoopLoaded()
        {
            coopLeftValid = false;
            coopLastControl = null;
            var peer = CoopPeer.Current;
            if (peer == null || peer.Applying) return;
            if (peer.IsHost)
            {
                peer.Open?.Post.Clear();
                peer.RequestSync("the host loaded a save");
            }
        }

        /// <summary>What a client does after replaying a host frame's Update: the frame's post steps.</summary>
        public void CoopApplyPost(FrameRecord rec)
        {
            foreach (var p in rec.Post)
            {
                if (!p.StartsWith("activate ", StringComparison.Ordinal)) continue;
                var system = Galaxy.Find(p.Substring(9));
                if (system != null && system != ActiveSystem) Activate(system);
            }
        }

        // ---------------------------------------------------------------- keys for seats and targets

        /// <summary>
        /// A seat by what it is in the shared world, so every peer finds the same one: "F b u" a fighter's cockpit (battle b
        /// of the active system's war, its unit u), "T b u m" a gun (mount m of that unit), "S i" a station (i-th of
        /// <see cref="Embodiables.StationsIn"/>), "SB b u" a station fighting as unit u. Null for one co-op does not share.
        /// </summary>
        public string CoopSeatKey(IEmbodiable seat)
        {
            var battles = World.War.Battles;
            switch (seat)
            {
                case FighterSeat f:
                {
                    int b = battles.IndexOf(f.Battle), u = f.Battle.Units.IndexOf(f.Unit);
                    return b < 0 || u < 0 ? null : "F " + b + " " + u;
                }
                case MountedTurret t:
                {
                    int b = battles.IndexOf(t.Battle), u = t.Battle.Units.IndexOf(t.Unit), m = t.Unit.Combat.Subsystems.IndexOf(t.Mount);
                    return b < 0 || u < 0 || m < 0 ? null : "T " + b + " " + u + " " + m;
                }
                case StationSeat s:
                {
                    int i = Embodiables.StationsIn(World).IndexOf(s);
                    if (i >= 0) return "S " + i;
                    var unit = s.Unit(out var battle);
                    int b = battle != null ? battles.IndexOf(battle) : -1, u = unit != null && battle != null ? battle.Units.IndexOf(unit) : -1;
                    return b < 0 || u < 0 ? null : "SB " + b + " " + u;
                }
                default:
                    return null;
            }
        }

        public IEmbodiable CoopResolveSeat(string key)
        {
            try
            {
                var f = key.Split(' ');
                var battles = World.War.Battles;
                Battle Battle(int i) => i >= 0 && i < battles.Count ? battles[i] : null;
                BattleUnit Unit(Battle b, int i) => b != null && i >= 0 && i < b.Units.Count ? b.Units[i] : null;
                switch (f[0])
                {
                    case "F":
                    {
                        var b = Battle(Wire.ParseI(f[1]));
                        var u = Unit(b, Wire.ParseI(f[2]));
                        return u == null ? null : Embodiables.FighterOf(b, u);
                    }
                    case "T":
                    {
                        var b = Battle(Wire.ParseI(f[1]));
                        var u = Unit(b, Wire.ParseI(f[2]));
                        int m = Wire.ParseI(f[3]);
                        if (u == null || m < 0 || m >= u.Combat.Subsystems.Count) return null;
                        var mount = u.Combat.Subsystems[m];
                        foreach (var gun in Embodiables.TurretsOn(World, b, u))
                            if (gun.Mount == mount) return gun;
                        return null;
                    }
                    case "S":
                    {
                        var all = Embodiables.StationsIn(World);
                        int i = Wire.ParseI(f[1]);
                        return i >= 0 && i < all.Count ? all[i] : null;
                    }
                    case "SB":
                    {
                        var b = Battle(Wire.ParseI(f[1]));
                        return Embodiables.StationOf(World, b, Unit(b, Wire.ParseI(f[2])));
                    }
                }
            }
            catch (Exception e)
            {
                CoopPeer.Warn("bad seat key '" + key + "': " + e.Message);
            }
            return null;
        }

        /// <summary>A selection as every peer can read it back: "n", "b &lt;body&gt;", "s &lt;ship id&gt;", "u &lt;battle&gt; &lt;unit&gt;", else "o &lt;kind&gt;".</summary>
        public string CoopTargetKey(Target t)
        {
            if (t.IsNone) return "n";
            if (t.IsBody) return "b " + t.Body;
            if (t.IsShip) return "s " + t.Ship.Id;
            if (t.IsUnit)
            {
                int b = World.War.Battles.IndexOf(t.Battle), u = t.Battle.Units.IndexOf(t.Unit);
                if (b >= 0 && u >= 0) return "u " + b + " " + u;
            }
            return "o " + t.Kind;
        }

        /// <summary>A selection key's name in the active system, for the HUD.</summary>
        public string CoopTargetName(string key)
        {
            try
            {
                var f = key.Split(' ');
                switch (f[0])
                {
                    case "n": return null;
                    case "b": { int i = Wire.ParseI(f[1]); return i >= 0 && i < Catalog.Count ? Catalog[i].Name : "a body"; }
                    case "s": { int i = Wire.ParseI(f[1]); return i >= 0 && i < Fleet.Ships.Count ? Fleet.Ships[i].Name : "a ship"; }
                    case "u":
                    {
                        int b = Wire.ParseI(f[1]), u = Wire.ParseI(f[2]);
                        var bs = World.War.Battles;
                        return b < bs.Count && u < bs[b].Units.Count ? bs[b].Units[u].Name : "a unit";
                    }
                    default: return f.Length > 1 ? "a " + f[1].ToLowerInvariant() : "something";
                }
            }
            catch (Exception) { return "something"; }
        }

        // ---------------------------------------------------------------- saves, digest, presence

        /// <summary>Why a sync save cannot be taken now (between the stars, under FTL), or null.</summary>
        public string CoopSaveBlocker() => SaveBlocker();

        /// <summary>The whole simulation as Core/Persistence writes it (what a save slot holds).</summary>
        public byte[] CoopCaptureSave() => SaveGame.ToBytes(CaptureSave(), "coop", "Co-op sync");

        /// <summary>Loads those bytes as a save is loaded (the original's ApplyLoaded): every peer's world from the same bytes.</summary>
        public void CoopApplySave(byte[] bytes)
        {
            ApplyLoaded(SaveGame.FromBytes(bytes));
            CoopWatchSession();
        }

        /// <summary>
        /// A cheap digest of the shared state (the idea of Bench/CSharp's, much smaller): the clock as the frame advanced it,
        /// every system's date, detail and counts, every faction's treasury, and in the active system every ship's position,
        /// every settlement's people and stock, every battle unit, and who holds which seat; hashed with FNV-1a.
        /// </summary>
        public string CoopDigest()
        {
            var sb = new StringBuilder(64 * 1024);
            sb.Append("clock ").Append((coopLeftValid ? coopLeft : ClockState.Of(Clock)).Write()).Append('\n');
            sb.Append("active ").Append(ActiveSystem.Name).Append('\n');
            foreach (var sys in Galaxy.Systems)
            {
                var w = sys.World;
                sb.Append("system ").Append(sys.Name).Append(' ').Append(Wire.D(w.Jd)).Append(w.Coarse ? " c " : " f ")
                  .Append(w.Fleet.Ships.Count).Append(' ').Append(w.Economy.Settlements.Count).Append(' ').Append(w.War.Battles.Count)
                  .Append(' ').Append(w.Events.Count);
                foreach (var f in w.Fleet.Factions) sb.Append(' ').Append(Wire.D(f.Treasury));
                sb.Append('\n');
            }
            var world = World;
            double jd = world.Jd;
            foreach (var s in world.Fleet.Ships)
            {
                sb.Append("ship ").Append(s.Id).Append(' ').Append(s.Phase).Append(s.Destroyed ? " x" : "");
                if (!s.Destroyed)
                {
                    try
                    {
                        var st = world.Fleet.StateOf(s, jd);
                        sb.Append(' ').Append(Wire.D(st.HelioKm.X)).Append(' ').Append(Wire.D(st.HelioKm.Y)).Append(' ').Append(Wire.D(st.HelioKm.Z));
                    }
                    catch (Exception) { sb.Append(" !"); }
                }
                sb.Append('\n');
            }
            foreach (var st in world.Economy.Settlements)
            {
                sb.Append("settlement ").Append(st.Id).Append(' ').Append(Wire.D(st.Population));
                foreach (var k in st.Stock) sb.Append(' ').Append(Wire.D(k));
                sb.Append('\n');
            }
            foreach (var b in world.War.Battles)
                foreach (var u in b.Units)
                    sb.Append("unit ").Append(u.Id).Append(u.Present ? " p " : " - ").Append(Wire.D(u.Pos.X)).Append(' ').Append(Wire.D(u.Pos.Y))
                      .Append(' ').Append(Wire.D(u.Pos.Z)).Append(' ').Append(u.Combat != null ? Wire.D(u.Combat.Hull) : "-").Append('\n');
            foreach (var p in session.Players) sb.Append("seat ").Append(p.Seat).Append(' ').Append(session.State.SeatIdOf(p)).Append('\n');
            return Wire.Hex(Wire.Fnv1a64(sb.ToString()));
        }

        /// <summary>Where this machine's player is, for the others' markers: the seat held, else the camera, as a body of the
        /// active system and a true-km offset from it (every peer lays its own scene out at its own scale).</summary>
        public (int Body, DVec3 OffsetKm) CoopPresence()
        {
            var held = HeldNonHelm;
            if (held != null)
            {
                var pose = held.PoseAt(Clock.Jd);
                return (pose.FrameBody, pose.OffsetKm);
            }
            int frame = SpaceCamera.FrameAt(SpaceCamera.ScenePosition);
            return (frame, Scale.InverseLocalOffset(Catalog[frame], SpaceCamera.ScenePosition - BodyScene(frame)));
        }

        /// <summary>Co-op's HUD (<see cref="CoopHud"/>'s OnGUI): who is playing, what each holds and selected, and a marker
        /// with the name on every other player where they are.</summary>
        internal void CoopGui()
        {
            var peer = CoopPeer.Current;
            if (peer == null || !ShowHud || ScreenCleared) return;
            var color = GUI.color;
            float x = Screen.width - 340, y = 6;
            GUI.color = new Color(0.55f, 0.85f, 1f, 1f);
            GUI.Label(new Rect(x, y, 334, 20), "CO-OP  ·  " + (peer.IsHost ? "hosting" : "joined " + peer.NameOf(0)) + "  ·  frame " + peer.Frame);
            y += 18;
            foreach (var p in peer.Players)
            {
                if (p == null || p.Left) continue;
                var seat = session.State.SeatOf(p.Id);
                string sel = CoopTargetName(p.Selection);
                string line = "P" + (p.Seat + 1) + "  " + p.Name + (p.Seat == 0 ? " (host)" : "") + (p.Seat == peer.LocalSeat ? " (you)" : "") +
                              "  ·  " + (seat != null ? seat.Name : "in command") + (sel != null ? "  ·  " + sel : "");
                GUI.Label(new Rect(x, y, 334, 20), line);
                y += 18;
                if (p.Seat == peer.LocalSeat || p.Body < 0 || p.Body >= Catalog.Count || !SystemVisible) continue;
                var scene = BodyScene(p.Body) + Scale.LocalOffset(Catalog[p.Body], p.OffsetKm);
                if (ToGui(scene, out var g, out _)) GUI.Label(new Rect(g.x - 4, g.y - 10, 220, 20), "+ P" + (p.Seat + 1) + " " + p.Name);
            }
            GUI.color = color;
        }
    }

    /// <summary>The co-op HUD on the app's GameObject: its OnGUI runs with the app's own (US-T28's IMGUI host lists every live
    /// MonoBehaviour's), and draws <see cref="UnitySpaceApp.CoopGui"/>.</summary>
    public sealed class CoopHud : MonoBehaviour
    {
        void OnGUI() => Bootstrap.App?.CoopGui();
    }
}
