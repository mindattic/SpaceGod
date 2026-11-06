using System.Diagnostics;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using MindAttic.Capture;
using MindAttic.Capture.Hosting;
using MindAttic.Capture.Windows;

namespace SpaceGod.Capture;

/// <summary>
/// US-T163: the game's own frames, for a game with no window to record (<c>-RenderOffScreen</c>) or one run with
/// <c>-capture-source=frames</c>. The game reads each presented backbuffer back from the GPU (asynchronously) and
/// writes it into a shared-memory ring (Cpp/Source/SpaceGod/Capture/FrameFeed.cpp); this reads the newest whole frame.
/// Layout (little-endian): header at 0 (magic "SGFR", version, width, height, slots, headerBytes, int64 latest at 24,
/// int32 closed at 32, int32 fps at 36), 32-byte slot records from 64 (int64 seq: 0 while being written), frames of
/// width*height*4 top-down BGRA from headerBytes. A frame is taken only when its slot's seq reads the same before and
/// after the copy, so a frame the game is overwriting is never torn into the tape.
/// </summary>
internal sealed unsafe class FrameFeed : IDisposable
{
    private const uint Magic = 0x52464753;
    private readonly MemoryMappedFile _file;
    private readonly MemoryMappedViewAccessor _view;
    private readonly byte* _base;

    private FrameFeed(MemoryMappedFile file, MemoryMappedViewAccessor view, byte* basePtr)
    {
        _file = file;
        _view = view;
        _base = basePtr;
        if (*(uint*)_base != Magic) throw new CaptureException(CaptureError.InvalidArgument, "The game's frame feed has no SGFR header.");
        Width = *(int*)(_base + 8);
        Height = *(int*)(_base + 12);
        Slots = *(int*)(_base + 16);
        HeaderBytes = *(int*)(_base + 20);
        if (Width < 2 || Height < 2 || Slots < 2 || HeaderBytes < 64 + Slots * 32)
            throw new CaptureException(CaptureError.InvalidArgument, $"The game's frame feed header is not valid ({Width}x{Height}, {Slots} slots).");
    }

    /// <summary>Opens the ring the game named on <c>--frames</c> (a {GUID}).</summary>
    public static FrameFeed Open(string name)
    {
        MemoryMappedFile file;
        try
        {
            file = MemoryMappedFile.OpenExisting(name, MemoryMappedFileRights.Read);
        }
        catch (Exception ex) when (ex is FileNotFoundException or IOException or UnauthorizedAccessException)
        {
            throw new CaptureException(CaptureError.TargetNotFound, $"The game's frame feed {name} is not there: {ex.Message}", ex);
        }
        var view = file.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read);
        byte* p = null;
        view.SafeMemoryMappedViewHandle.AcquirePointer(ref p);
        return new FrameFeed(file, view, p + view.PointerOffset);
    }

    public int Width { get; }
    public int Height { get; }
    public int Slots { get; }
    public int HeaderBytes { get; }
    public int FrameBytes => Width * Height * 4;

    /// <summary>The newest whole frame's sequence number; 0 before the first.</summary>
    public long Latest => Volatile.Read(ref *(long*)(_base + 24));

    /// <summary>The game stopped feeding.</summary>
    public bool Closed => Volatile.Read(ref *(int*)(_base + 32)) != 0;

    /// <summary>Copies the newest frame into <paramref name="dst"/> when it is newer than <paramref name="after"/>: its
    /// sequence number, or 0 when there is none newer (or it was overwritten mid-copy three times running).</summary>
    public long TryCopyLatest(Span<byte> dst, long after)
    {
        for (var attempt = 0; attempt < 3; attempt++)
        {
            var seq = Latest;
            if (seq <= after) return 0;
            var slot = (int)(seq % Slots);
            ref var slotSeq = ref *(long*)(_base + 64 + slot * 32);
            if (Volatile.Read(ref slotSeq) != seq) continue;
            new ReadOnlySpan<byte>(_base + HeaderBytes + (long)slot * FrameBytes, FrameBytes).CopyTo(dst);
            Interlocked.MemoryBarrier();
            if (Volatile.Read(ref slotSeq) == seq) return seq;
        }
        return 0;
    }

    public void Dispose()
    {
        _view.SafeMemoryMappedViewHandle.ReleasePointer();
        _view.Dispose();
        _file.Dispose();
    }
}

/// <summary>
/// The capture host for the game's own frames (what CaptureRunner is inside a Unity player): video and screenshots from
/// <see cref="FrameFeed"/>, the same tape, ffmpeg pipeline, session folder, markers and events as window capture; audio
/// is the system mix, as the desktop host's.
/// </summary>
internal sealed class FramesCaptureHost(FrameFeed feed) : ICaptureHost
{
    public string Name => "frames";

    public HostCapabilities Capabilities => HostCapabilities.None;

    public ValueTask<CapturedImage> CaptureStillAsync(ScreenshotOptions options, CancellationToken ct) => new(Task.Run(() =>
    {
        // A frame rendered after the request (it trails the game by the readback's few frames), else the newest there is.
        var asked = feed.Latest;
        var clock = Stopwatch.StartNew();
        while (feed.Latest <= asked && clock.Elapsed < TimeSpan.FromSeconds(asked == 0 ? 10 : 1) && !feed.Closed)
        {
            ct.ThrowIfCancellationRequested();
            Thread.Sleep(5);
        }
        var bgra = new byte[feed.FrameBytes];
        var got = false;
        for (var attempt = 0; attempt < 10 && !got; attempt++)
        {
            got = feed.TryCopyLatest(bgra, 0) > 0;
            if (!got) Thread.Sleep(2);
        }
        if (!got) throw new CaptureException(CaptureError.CaptureFailed, "The game has fed no frame yet.");
        return new CapturedImage(feed.Width, feed.Height, bgra);
    }, ct));

    public VideoSource CreateVideoSource(int fps) => new FrameFeedVideoSource(feed, fps);

    public IReadOnlyList<IAudioTrackCapture> CreateAudioTracks(AudioMode mode) => DesktopCaptureHost.CreateWasapiTracks(mode);
}

/// <summary>
/// The tape's frames from the feed, at a constant rate as WgcWindowSource sends a window's: the newest frame each tick,
/// the last one again while the game has fed nothing newer (a hitch shows as a held frame, never as a gap in time).
/// </summary>
internal sealed class FrameFeedVideoSource(FrameFeed feed, int fps) : PushVideoSource
{
    private Thread? _pump;
    private volatile bool _running;

    public override int Width => feed.Width;
    public override int Height => feed.Height;
    public override bool RealTime => true;
    public override string Description => $"game frames {feed.Width}x{feed.Height}";

    public override Task StartAsync(IFrameSink sink, CancellationToken ct)
    {
        _running = true;
        _pump = new Thread(() => Pump(sink)) { IsBackground = true, Name = "SpaceGod frame feed pump", Priority = ThreadPriority.AboveNormal };
        _pump.Start();
        return Task.CompletedTask;
    }

    private void Pump(IFrameSink sink)
    {
        TimeBeginPeriod(1);
        try
        {
            var frame = new byte[feed.FrameBytes];
            var spare = new byte[feed.FrameBytes];
            long have = 0;
            var interval = Stopwatch.Frequency / (double)fps;
            var clock = Stopwatch.StartNew();
            long n = 0;
            while (_running)
            {
                var due = (long)(n * interval);
                var wait = due - clock.ElapsedTicks;
                if (wait > 0)
                {
                    var ms = (int)(wait * 1000 / Stopwatch.Frequency);
                    if (ms > 1) Thread.Sleep(ms - 1);
                    else Thread.SpinWait(50);
                    continue;
                }
                n++;
                // Into the spare buffer: a copy the game overwrote midway is dropped, never sent.
                var seq = feed.TryCopyLatest(spare, have);
                if (seq > 0)
                {
                    have = seq;
                    (frame, spare) = (spare, frame);
                }
                if (have > 0) sink.WriteFrame(frame);
                if (clock.ElapsedTicks - (long)(n * interval) > Stopwatch.Frequency) n = (long)(clock.ElapsedTicks / interval);
            }
        }
        finally
        {
            TimeEndPeriod(1);
        }
    }

    public override Task StopAsync()
    {
        _running = false;
        _pump?.Join(TimeSpan.FromSeconds(2));
        return Task.CompletedTask;
    }

    [DllImport("winmm.dll", EntryPoint = "timeBeginPeriod")]
    private static extern uint TimeBeginPeriod(uint ms);

    [DllImport("winmm.dll", EntryPoint = "timeEndPeriod")]
    private static extern uint TimeEndPeriod(uint ms);
}
