# MindAttic project agent entrypoint

Read the shared protocol at ..\mindattic-agent-standard\AGENTS.md and this project's own
documentation. Common prompt commands are implemented by the shared runner; do not add a second
copy under a provider-specific command folder.

This repo is the Unreal Engine counterpart of `Unity.SpaceGod`. The simulation core under
`Cpp/Source/UnrealSpaceGod/Sim/` is a faithful port of `Unity.SpaceGod/Assets/UnitySpace/Core/`: when
the Unity data or maths change, port the change here value for value rather than re-deriving it.
Unreal Engine is not installed on every workstation; when it is missing, say so instead of claiming
a build passed.
