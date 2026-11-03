#pragma once

// SEAM (US-T29) for the UI ticket's port of Runtime/UI/MainMenu.cs (US-T31): keep these two entry points when the
// real class lands here. In Unity the menu is a MonoBehaviour that lives in the MainMenu scene; in Unreal that scene
// is the empty map opened with ?Scene=MainMenu (GameLaunch), so:
//  - Bootstrap::EnsureApp calls MainMenu::Create(world) when that scene loads (Unity: the scene's MainMenu object);
//  - ASpaceHUD::DrawHUD calls MainMenu::DrawGui(world, canvas) every frame while no app exists (Unity: its OnGUI).
// The menu starts the game with GameLaunch::NewGame() / GameLaunch::Load(slot).

class UWorld;
class UCanvas;

namespace SpaceGod::UI
{
	class MainMenu final
	{
	public:
		/// The menu scene has loaded: make the scene's menu (once per world).
		static void Create(UWorld* world);

		/// Its OnGUI pass, on the HUD's canvas.
		static void DrawGui(UWorld* world, UCanvas* canvas);
	};
}
