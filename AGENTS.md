# MindAttic project agent entrypoint

Read the shared protocol at ..\mindattic-agent-standard\AGENTS.md and this project's own
documentation. Common prompt commands are implemented by the shared runner; do not add a second
copy under a provider-specific command folder.

This repo is SpaceGod in Unreal Engine 5.8, C++ only: one of three self-contained copies of the same
game (with SpaceGod.Unreal.CSharp and SpaceGod.Unity.CSharp). The simulation core under
`Cpp/Source/SpaceGodCore/` is a faithful port of the original Unity game's `Assets/UnitySpace/Core/`, and
`Cpp/Source/UnrealSpaceGod/` of its `Runtime/` (docs/MIGRATION.md). Keep it self-contained: nothing may
read a sibling repo at build or run time. `tools/bench.ps1` must keep matching `Bench/expected-digests.json`.
Deploy with `run.ps1` (non-interactive) or `deploy.bat` (the Desktop shortcut's menu).
Unreal Engine is not installed on every workstation; when it is missing, say so instead of claiming
a build passed.
