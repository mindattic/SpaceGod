# Vendor

Outside code this repo carries as plain files, so a fresh clone builds with nothing next to it.

| Folder | What | From | Commit |
|---|---|---|---|
| `MindAttic.Capture/src/MindAttic.Capture` | MindAttic.Capture's core: options, the `-capture` flags, trigger rules, the rolling tape, exports, sidecars, the session manifest (netstandard2.1 + net10.0) | [MindAttic.Capture](https://github.com/mindattic/MindAttic.Capture) `src/MindAttic.Capture` | `c4272836318e00dcdbb2965faafa0b03da3c5e66` |
| `MindAttic.Capture/src/MindAttic.Capture.Windows` | Windows capture: Windows Graphics Capture of a window, Desktop Duplication / GDI, WASAPI audio | MindAttic.Capture `src/MindAttic.Capture.Windows` | same |
| `MindAttic.Capture/Directory.Build.props` | the two projects' shared build settings | MindAttic.Capture `Directory.Build.props` | same |

Copied unchanged (US-T72). Only the game's capture host (`Capture/SpaceGod.Capture`) builds them; see
[Capture/README.md](../Capture/README.md). The Windows project's NuGet packages (NAudio.Wasapi, Vortice.Direct3D11/DXGI,
System.Drawing.Common, Microsoft.Extensions.*.Abstractions) restore from nuget.org.

To refresh: copy those folders again from a MindAttic.Capture checkout (without `bin/` and `obj/`) and update the commit.
