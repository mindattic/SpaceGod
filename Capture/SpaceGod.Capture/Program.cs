using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Text.Json;
using MindAttic.Capture;
using MindAttic.Capture.Hosting;
using MindAttic.Capture.Windows;

namespace SpaceGod.Capture;

/// <summary>
/// The game's MindAttic.Capture host (US-T72): what MindAttic.Capture's Unity package does inside a Unity player, done
/// beside the Unreal game, which cannot load .NET. See Capture/README.md for the protocol.
/// <list type="bullet">
/// <item><c>capture host --hwnd 0x.. --pid N --app Name [--version V] --cmdline "the game's command line"</c>: builds the
/// options (nothing at all when the command line asks for no capture mode, reported as <c>"any":false</c>)
/// options exactly as CaptureUnity does (defaults, then the game's own <c>-capture</c> / <c>-capture-config</c>), starts
/// an engine on the game's window, then reads one JSON request per stdin line and writes one JSON reply/notice per
/// stdout line. It stops (exporting the session) on <c>{"op":"quit"}</c>, when stdin closes, or when the game exits.</item>
/// <item><c>capture finalize &lt;tapeDir&gt;</c> / <c>capture recover &lt;root&gt;</c>: the detached session export the engine
/// hands a finished tape to (the installed agent's commands of the same names), so the game never waits on ffmpeg.</item>
/// </list>
/// </summary>
internal static class Program
{
    private static readonly object WriteLock = new();
    private static readonly TextWriter Out = new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false)) { AutoFlush = true };

    public static async Task<int> Main(string[] argv)
    {
        var command = argv.Length > 0 ? argv[0].ToLowerInvariant() : "";
        var log = new TextWriterCaptureLog(Console.Error, CaptureLogLevel.Info);
        try
        {
            switch (command)
            {
                case "host":
                    return await HostAsync(argv.Skip(1).ToArray());
                case "finalize" when argv.Length > 1:
                    foreach (var f in await SessionFinalizer.RunAsync(argv[1], new SystemTextCaptureJson(), log, CancellationToken.None))
                        Console.WriteLine(f);
                    return 0;
                case "recover":
                {
                    var defaults = new CaptureOptions();
                    var roots = argv.Length > 1 ? [argv[1]] : new[] { defaults.OutputRoot, defaults.FallbackRoot };
                    var n = 0;
                    foreach (var root in roots.Distinct(StringComparer.OrdinalIgnoreCase))
                        n += await SessionFinalizer.RecoverAsync(root, new SystemTextCaptureJson(), log, CancellationToken.None);
                    Console.Error.WriteLine($"Recovered {n} session(s).");
                    return 0;
                }
                default:
                    Console.Error.WriteLine("SpaceGod capture host: capture host --hwnd 0x.. --pid N --app Name --cmdline \"...\" | capture finalize <tapeDir> | capture recover [root]");
                    return 1;
            }
        }
        catch (CaptureException ex)
        {
            Console.Error.WriteLine($"error: {ex.Message}");
            return (int)ex.Error;
        }
    }

    private static async Task<int> HostAsync(string[] argv)
    {
        string? Opt(string name)
        {
            for (var i = 0; i + 1 < argv.Length; i++)
                if (string.Equals(argv[i], "--" + name, StringComparison.OrdinalIgnoreCase)) return argv[i + 1];
            return null;
        }

        var log = new LineLog();
        var hwndText = Opt("hwnd") ?? throw new CaptureException(CaptureError.InvalidArgument, "host needs --hwnd.");
        var hwnd = (nint)long.Parse(hwndText.StartsWith("0x", StringComparison.OrdinalIgnoreCase) ? hwndText[2..] : hwndText,
            hwndText.StartsWith("0x", StringComparison.OrdinalIgnoreCase) ? NumberStyles.HexNumber : NumberStyles.Integer, CultureInfo.InvariantCulture);
        var gameArgs = SplitCommandLine(Opt("cmdline") ?? "");

        // CaptureUnity.BuildOptions, line for line but for the host: the app's name and version, then the -capture flags.
        var options = new CaptureOptions { AppName = Opt("app") ?? "SpaceGod", AppVersion = Opt("version"), Log = log };
        var args = CaptureArgs.Parse(gameArgs);
        if (args.Ignored != null)
            log.Info($"\"-capture {args.Ignored}\" is not a capture mode (video, screen N); left for the game.");
        // -capture followed only by something that isn't a mode: CaptureUnity.Args.Any is false and nothing starts.
        if (!args.Any)
        {
            Send(new { op = "ready", any = false, enabled = false });
            return 0;
        }
        options.Apply(args);
        // docs/CAPTURE.md section 4 (Unity: CaptureEvents.ApplyDefaults): under -capture without -capture-config, the
        // game's shipped SpaceGod.capture.json (--defaults) goes into the options before the engine starts; the replay
        // ring stays armed only when the session records video.
        if (args.ConfigPath == null && Opt("defaults") is { Length: > 0 } defaults)
        {
            try
            {
                if (File.Exists(defaults)) options.ApplyConfigFile(defaults);
                else log.Warning($"[capture] no {Path.GetFileName(defaults)} at {defaults}: no default rules");
            }
            catch (CaptureException ex)
            {
                log.Log(CaptureLogLevel.Error, $"[capture] {Path.GetFileName(defaults)} not applied: {ex.Message}");
            }
            if (!options.SessionRecording) options.ReplayArmed = false;
        }
        // docs/CAPTURE.md section 5: the game reads its capture hotkeys (Alt+F9/F10/F11) through its own input and sends
        // them as commands; a capture.json "hotkeys" map is never bound, least of all as global Windows hotkeys.
        options.Hotkeys.Clear();
        options.DetachedFinalizerPath = Environment.ProcessPath;

        CaptureEngine engine;
        try
        {
            engine = await CaptureEngine.StartNewAsync(new DesktopCaptureHost(CaptureTarget.Window(hwnd), new DesktopHostOptions()), options);
        }
        catch (CaptureException ex)
        {
            Send(new { op = "ready", any = true, enabled = false, error = ex.Message });
            return (int)ex.Error;
        }

        await using (engine)
        {
            Send(new { op = "ready", any = true, enabled = engine.Enabled, folder = engine.SessionFolder, session = engine.SessionId });
            var stop = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            if (int.TryParse(Opt("pid"), out var pid))
            {
                try
                {
                    var game = Process.GetProcessById(pid);
                    game.EnableRaisingEvents = true;
                    game.Exited += (_, _) => stop.TrySetResult();
                    if (game.HasExited) stop.TrySetResult();
                }
                catch (ArgumentException) { stop.TrySetResult(); }
            }
            var reader = Task.Run(async () =>
            {
                var stdin = new StreamReader(Console.OpenStandardInput(), new UTF8Encoding(false));
                while (await stdin.ReadLineAsync() is { } line)
                {
                    if (line.Length == 0) continue;
                    try
                    {
                        if (!await HandleAsync(engine, line)) break;
                    }
                    catch (Exception ex) when (ex is JsonException or KeyNotFoundException or InvalidOperationException or FormatException or ArgumentException)
                    {
                        log.Warning($"Bad request \"{line}\": {ex.Message}");
                    }
                }
                stop.TrySetResult();
            });
            await stop.Task;
            log.Info("Game gone or asked to stop; exporting the session.");
        }
        Send(new { op = "stopped" });
        return 0;
    }

    /// <summary>One request; false for quit.</summary>
    private static async Task<bool> HandleAsync(CaptureEngine engine, string line)
    {
        using var doc = JsonDocument.Parse(line);
        var body = doc.RootElement;
        string? Str(string name) => body.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;
        var op = Str("op") ?? "";
        switch (op)
        {
            case "raise":
                engine.Raise(Str("event") ?? throw new ArgumentException("raise needs \"event\"."), Data(body));
                return true;
            case "marker":
                engine.Marker(Str("name") ?? throw new ArgumentException("marker needs \"name\"."), Data(body));
                return true;
            case "screenshot":
            {
                var id = body.TryGetProperty("id", out var idEl) ? idEl.GetInt32() : 0;
                var tag = Str("tag") ?? "screenshot";
                // Answered when written, so a tour can wait on its shot as Unity's does (while (!shot.IsCompleted) yield).
                _ = engine.ScreenshotAsync(tag, new ScreenshotOptions { Data = Data(body) }).ContinueWith(t =>
                {
                    var r = t.IsCompletedSuccessfully ? t.Result : null;
                    Send(new { op = "result", id, ok = r?.Succeeded ?? false, files = r?.Files ?? [], error = r?.Error ?? t.Exception?.GetBaseException().Message });
                }, TaskScheduler.Default);
                return true;
            }
            case "command":
                _ = engine.ExecuteCommandAsync(Str("command") ?? "");
                return true;
            case "note":
                // A line of the game's own log (its "[capture] event ..." lines), also kept beside the session as
                // <session folder>.game.log, since a Shipping game writes no log of its own.
                try
                {
                    var folder = engine.SessionFolder;
                    File.AppendAllText(Path.Combine(folder, Path.GetFileName(folder.TrimEnd('\\', '/')) + ".game.log"),
                        DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff", CultureInfo.InvariantCulture) + " " + (Str("text") ?? "") + Environment.NewLine);
                }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
                return true;
            case "quit":
                return false;
            default:
                throw new ArgumentException($"Unknown op \"{op}\".");
        }
    }

    private static CaptureData? Data(JsonElement body)
    {
        if (!body.TryGetProperty("data", out var d) || d.ValueKind != JsonValueKind.Object) return null;
        var data = new CaptureData();
        foreach (var p in d.EnumerateObject())
        {
            data[p.Name] = p.Value.ValueKind switch
            {
                JsonValueKind.Number => p.Value.GetDouble(),
                JsonValueKind.True => true,
                JsonValueKind.False => false,
                JsonValueKind.String => p.Value.GetString(),
                JsonValueKind.Null => null,
                _ => p.Value.ToString(),
            };
        }
        return data;
    }

    private static void Send(object message)
    {
        var json = JsonSerializer.Serialize(message);
        lock (WriteLock) Out.WriteLine(json);
    }

    /// <summary>Windows command-line splitting (CommandLineToArgvW's quote and backslash rules).</summary>
    internal static List<string> SplitCommandLine(string text)
    {
        var result = new List<string>();
        var sb = new StringBuilder();
        var inQuotes = false;
        var any = false;
        for (var i = 0; i < text.Length; i++)
        {
            var c = text[i];
            if (c == '\\')
            {
                var n = 0;
                while (i < text.Length && text[i] == '\\') { n++; i++; }
                if (i < text.Length && text[i] == '"')
                {
                    sb.Append('\\', n / 2);
                    if (n % 2 == 1) sb.Append('"');
                    else { inQuotes = !inQuotes; }
                    any = true;
                }
                else
                {
                    sb.Append('\\', n);
                    i--;
                }
                any = true;
                continue;
            }
            if (c == '"') { inQuotes = !inQuotes; any = true; continue; }
            if (char.IsWhiteSpace(c) && !inQuotes)
            {
                if (any || sb.Length > 0) result.Add(sb.ToString());
                sb.Clear();
                any = false;
                continue;
            }
            sb.Append(c);
            any = true;
        }
        if (any || sb.Length > 0) result.Add(sb.ToString());
        return result;
    }

    /// <summary>Engine log lines go to the game (it writes them to its own log as CaptureUnity's UnityCaptureLog does).</summary>
    private sealed class LineLog : ICaptureLog
    {
        public void Log(CaptureLogLevel level, string message, Exception? exception = null)
        {
            if (level < CaptureLogLevel.Info) return;
            Send(new { op = "log", level = level.ToString(), message = exception is null ? message : message + " " + exception.Message });
        }

        public void Info(string message) => Log(CaptureLogLevel.Info, message);
        public void Warning(string message) => Log(CaptureLogLevel.Warning, message);
    }
}
