# Capture: the game's MindAttic.Capture host (US-T72)

How this copy records itself through MindAttic.Capture, as [docs/CAPTURE.md](../docs/CAPTURE.md) says all three
SpaceGod copies do. In Unity, MindAttic.Capture's package runs inside the player. C++ can't load .NET, so here the same
library runs beside the game in a small host process the game starts and drives.

```
 game (C++)                                   capture.exe host (Capture/SpaceGod.Capture)
 ──────────                                   ───────────────────────────────────────────
 -capture on the command line?
   └─ first frame with a window: start ───>   host --hwnd <game window> --pid <game> --app SpaceGod.Unreal.Cpp
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
- **Output.** `R:\MindAttic\Capture\SpaceGod.Unreal.Cpp\` (or `Videos\MindAttic\Capture\...` without `R:`).
- **Prerequisites.** The .NET 10 runtime (the SDK that builds it installs it), and ffmpeg on PATH for video and
  replays. ffmpeg is not bundled.
- **Name.** The exe is `capture.exe` with product name `MindAttic.Capture.Agent`, the names MindAttic.Capture trusts for
  its detached `capture finalize` / `capture recover`. The host implements those two commands too, so the game never
  waits for an export.

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

Host to game (stdout): `ready` (`any`, `enabled`, `folder`, `session`, or `error`), `result` (`id`, `ok`, `files`,
`error`), `log` (`level`, `message`; written to the game's log as `[MindAttic.Capture] ...`), `stopped`.

Game side: `Cpp/Source/SpaceGod/Capture/MindAtticCapture.h` (CaptureUnity's surface) and `CaptureEvents.h` (the
event catalogue).
