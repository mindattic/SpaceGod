// US-T254: where the HUD elements the guided tutorial points at are drawn (Hud::TutorialHighlightRect, declared in
// Hud.Main.inl). US-T440: the top bar and the navigator are drawers, so it asks the drawers' manager where each was drawn
// (HudDrawers::PublishedContent / PublishedRect), and the time and scale controls are found inside the top bar's content
// from the numbers Hud::DrawTopBar draws them with.

#include "UI/Hud.h"

#include "Tutorial.h"

namespace SpaceGod::UI
{
	Rect Hud::TutorialHighlightRect(int highlight)
	{
		float w = static_cast<float>(Screen::width), h = static_cast<float>(Screen::height);
		switch (static_cast<Core::TutorialHighlight>(highlight))
		{
			case Core::TutorialHighlight::TimeControls:
			case Core::TutorialHighlight::ScaleControl:
			{
				// The top bar's content: 640 x 52 (DrawTopBar); nothing to point at while its drawer is not drawn.
				auto bar = HudDrawers::PublishedContent(DrawerTopBarId);
				if (bar.width <= 0) return Rect(0, 0, 0, 0);
				return static_cast<Core::TutorialHighlight>(highlight) == Core::TutorialHighlight::TimeControls
					? Rect(bar.x + 196, bar.y + 8, 176, 36) // << || >> Now
					: Rect(bar.x + 386, bar.y + 5, 244, 44); // the scale button and its bar
			}
			case Core::TutorialHighlight::Navigator:
			{
				// Its window's title bar and the navigator's own fold button under it.
				auto win = HudDrawers::PublishedRect(DrawerNavigatorId);
				if (win.width <= 0) return Rect(0, 0, 0, 0);
				return Rect(win.x, win.y, win.width, Mathf::Min(win.height, HudDrawers::TitleH + 34));
			}
			case Core::TutorialHighlight::Reticle: return Rect(w * 0.5f - 36, h * 0.5f - 36, 72, 72);
			default: return Rect(0, 0, 0, 0);
		}
	}
}
