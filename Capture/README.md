# Capture: the game's MindAttic.Capture host (US-T72)

How this copy records itself through MindAttic.Capture, as [docs/CAPTURE.md](../docs/CAPTURE.md) says all three
SpaceGod copies do. In Unity, MindAttic.Capture's package runs inside the player. C++ can't load .NET, so here the same
library runs beside the game in a small host process the game starts and drives.

```
 game (C++)                                   capture.exe host (Capture/SpaceGod.Capture)
 ──────────                                   ───────────────────────────────────────────
 -capture on the command line?
   └─ first frame with a window: start ───>   host --hwnd <game window> --pid <game> --app SpaceGod
                                               --defaults <Content/Capture/SpaceGod.capture.json> --cmdline "<game's>"
                                               builds CaptureOptions as CaptureUnity does, starts CaptureEngine on the
                                               game window (Windows Graphics Capture), answers {"op":"ready",...}
 CaptureEvents::Raise ─ stdin JSON line ──>   engine.Raise / Marker / ScreenshotAsync / ExecuteCommandAsync
 [capture] event lines ─ "note" ──────────>   <session>.game.log (a Shipping game writes no log of its own)
                         <── stdout JSON ──   log lines, screenshot results
 quit (or the game's process ends) ───────>   stops; the session export runs detached (capture finalize)
```

- **Off unless asked.** Nothing starts unless `-capture` is literally on the command line. Without a mode after it
  (`CaptureArgs.Any` false), the host says so and exits.
- **Where it is.** `tools/package.ps1` publishes the host to `<package>/Capture/capture.exe`, next to the package's
  `Engine/` folder. An editor or uncooked run uses a build of `Capture/SpaceGod.Capture` in the repo
  (`dotnet build -c Release`). `MINDATTIC_CAPTURE_HOST` overrides both.
- **Output.** `R:\MindAttic\Capture\SpaceGod\` (this repo's name, docs/CAPTURE.md section 2; before the promotion,
  US-T76, it was `SpaceGod.Unreal.Cpp\`, which the archived SpaceGod.Unreal.Cpp repo still writes) (or `Videos\MindAttic\Capture\...` without `R:`).
- **Prerequisites.** The .NET 10 runtime (the SDK that builds it installs it), and ffmpeg on PATH for video and
  replays. ffmpeg is not bundled.
- **Name.** The exe is `capture.exe` with product name `MindAttic.Capture.Agent`, the names MindAttic.Capture trusts for
  its detached `capture finalize` / `capture recover`. The host implements those two commands too, so the game never
  waits for an export.

## Frames: capture with no window (US-T163)

Under `-RenderOffScreen` Unreal opens no window (a NullApplication) yet the GPU renders every frame, so there is no
window for Windows Graphics Capture. Then (or with `-capture-source=frames`) the game feeds the host its own frames:

```
 game (C++, Capture/FrameFeed.cpp)                     host (FrameFeed.cs)
 ─────────────────────────────────                     ───────────────────
 maps a shared ring {GUID}, starts the host ───────>   host --frames {GUID} ... (instead of --hwnd)
                              <── ready {"fps":60} ──   FramesCaptureHost: same CaptureEngine, tape, ffmpeg (NVENC
 every 1/fps: Slate's OnBackBufferReadyToPresent        with "hq"), session folder, markers, events, screenshots
   copies the backbuffer into an async GPU readback     a pump at fps sends the newest whole frame to the tape
   (4 deep); a finished one (fence passed, never        (the last again while nothing newer came, as WGC does);
   waited on) is written to the ring as BGRA ───────>   a screenshot is the first frame fed after the request
```

- **Ring layout** (little-endian): a 64-byte header (`uint32` magic `SGFR`, version 1, `int32` width, height, slots
  (3), headerBytes (4096), `int64` latest sequence at 24, `int32` closed at 32, `int32` fps at 36), 32-byte slot records
  from byte 64 (`int64` seq, 0 while the game writes the slot; `int64` timestamp), frames of width x height x 4 top-down
  BGRA from headerBytes. The host keeps a frame only if its slot's seq reads the same before and after the copy.
- **Cost.** No game-thread or GPU wait: a readback is copied out on the render thread once its fence has passed; when
  all four are still on the GPU the frame is skipped (counted), not waited for. `-CaptureStats` logs, every 5 s,
  `[MindAttic.Capture] feed: game N fps (...); fed N frames/s ...; render thread X ms avg per fed frame ...`, and on its
  own (without `-capture`) a `baseline:` line to compare against.
- **Launch.** `tools/play.ps1 -Mode dogfight -Offscreen -Seconds 30`, or by hand:
  `UnrealEditor.exe Cpp/SpaceGod.uproject /Engine/Maps/Entry -game -RenderOffScreen -ResX=1600 -ResY=900 -nosound -capture video hq -dogfight`
  (`-ExecCmds="sg.Tour auto quit"` for the showcase tour). `-nullrhi` renders nothing and can never give images.

## Protocol

One JSON object a line. Game to host (stdin):

| op | fields | does |
|---|---|---|
| `raise` | `event`, `data` | `Capture.Raise`: the capture.json rules see it, the manifest records it |
| `marker` | `name`, `data` | a chapter in the exported video |
| `screenshot` | `id`, `tag`, `data` | `ScreenshotAsync`; answered with a `result` of the same `id` |
| `command` | `command` | `ExecuteCommandAsync`: `screenshot`, `toggle-recording`, `save-replay` (Alt+F9/F10/F11) |
| `note` | `text` | appends a line to the session's `.game.log` |
| `quit` | | stop and export |

Host to game (stdout): `ready` (`any`, `enabled`, `folder`, `session`, `fps`, `source` (`window` or `frames`), or `error`), `result` (`id`, `ok`, `files`,
`error`), `log` (`level`, `message`; written to the game's log as `[MindAttic.Capture] ...`), `stopped`.

Game side: `Cpp/Source/SpaceGod/Capture/MindAtticCapture.h` (CaptureUnity's surface) and `CaptureEvents.h` (the
event catalogue).
