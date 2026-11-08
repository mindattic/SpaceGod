// US-T254: where the HUD elements the guided tutorial points at are drawn (Hud::TutorialHighlightRect, declared in
// Hud.Main.inl), from the numbers Hud::DrawTopBar and Hud::DrawNavigator draw them with.

#include "UI/Hud.h"

#include "Tutorial.h"

namespace SpaceGod::UI
{
	Rect Hud::TutorialHighlightRect(int highlight)
	{
		float w = static_cast<float>(Screen::width), h = static_cast<float>(Screen::height);
		// The top bar: 640 x 52, centred, 10 down (DrawTopBar).
		auto bar = Rect((w - 640) * 0.5f, 10, 640, 52);
		switch (static_cast<Core::TutorialHighlight>(highlight))
		{
			case Core::TutorialHighlight::TimeControls: return Rect(bar.x + 196, bar.y + 8, 176, 36); // << || >> Now
			case Core::TutorialHighlight::ScaleControl: return Rect(bar.x + 386, bar.y + 5, 244, 44); // the scale button and its bar
			case Core::TutorialHighlight::Navigator: return Rect(SafeLeft() + 10, 10, 268, 34); // its title bar (DrawNavigator)
			case Core::TutorialHighlight::Reticle: return Rect(w * 0.5f - 36, h * 0.5f - 36, 72, 72);
			default: return Rect(0, 0, 0, 0);
		}
	}
}
