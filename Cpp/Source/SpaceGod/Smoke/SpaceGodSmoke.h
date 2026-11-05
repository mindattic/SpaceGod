#pragma once

// The C++ track's checks run from the command line (tools/play.ps1 -Track Cpp -Mode smoke|views|viewshot|shots, US-T38),
// the same scripts as the C# track's (CSharp/.../SpaceGameMode.cs, Runtime/UI/UiSmoke.cs, Runtime/Views/ViewsSmoke.cs),
// logging the same lines under LogSpaceGod so the two logs can be compared side by side:
//
//  -SpaceGodSmoke (headless): the UI tour -- the main menu's pages, New Game, the HUD, every HUD panel, the galaxy map
//      and its modes, the pause menu and its Settings/Display pages, Help -- one GUI frame each, moving between screens
//      by clicking the real buttons by label; then the app loop at 1/60 s with a scripted keyboard and mouse: idle
//      frames, '.' (time warp), W held (free swim), a right-drag look, '3' twice (select and focus Earth), a save/load
//      round trip through Core/Persistence; then quits. The profile lives in Saved/SmokeProfiles.
//  -SpaceGodViews (headless) / -SpaceGodShot=<file> (windowed): Sol's views built without the app (bodies, sky,
//      starfield, interstellar stars, every ship's model, Earth's orbit line), logged; the windowed run keeps them on
//      screen and saves a screenshot after 90 frames.
//  -SpaceGodUiShots -capturedir=<dir> (windowed): screenshots of the main menu, the HUD, Help, the galaxy map, the
//      market + cargo boards and the pause menu as <dir>/cpp-ui-*.png; then quits.
//
// Unlike the C# track, a scene change is a map load here (GameLaunch), so the checks are driven from the core ticker
// and pick the run up again in the next scene's world.

class UWorld;

namespace SpaceGod::Smoke
{
	/// Called by ASpaceGameMode::StartPlay before the scene's Bootstrap: starts the run asked for on the command line
	/// (once per process; later scenes find it running). Returns true when this scene is the views check, which builds
	/// its own scene instead of the app's.
	bool BeginScene(UWorld* world);
}
