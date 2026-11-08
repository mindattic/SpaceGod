// US-T250: when the gamepad is the GUI's rather than the game's (declared in UnitySpaceApp.Main.inl; the pad itself is
// Input/Gamepad, its menu focus UnityCompat/GuiPadNav). The app's actor copies this into Input::Gamepad::MenuMode after
// every Update, so the next frame's InputState and GUI passes agree on who has the pad.

#include "UnitySpaceApp.h"

#include "UI/CheatMenu.h"
#include "UI/Hud.h"
#include "UI/PauseMenu.h"

namespace SpaceGod
{
	bool UnitySpaceApp::PadMenusOpen() const
	{
		if (pauseMenu != nullptr && pauseMenu->IsOpen) return true;
		if (cheatMenu != nullptr && cheatMenu->IsOpen) return true; // US-T126
		if (commsOpen) return true; // US-T137
		if (hud != nullptr && hud->DrawerOpen()) return true; // US-T200
		return ClickPanelOpen();
	}
}
