#pragma once

// HudLayout (US-T152..T155): the HUD's alerts and banners take their rects from a screen region instead of fixed pixel
// spots, so two of them can never be drawn over each other.
//
// Since US-T440 every HUD panel is a drawer (UI/HudDrawers.h: the drawers place themselves on the screen's edges or float),
// so what is left here is the default placement of what is not a panel:
//   - TopCentre: the banners and alerts at the top middle (the Bio Mass warning, the squad's banner, a plotted course, the
//     autopilot offer, the embody prompts), stacked downward from under whatever the top edge's drawers take (the top bar,
//     the news), centred, with a fixed gap. A banner never picks its own y.
//   - Begin() resets the region at the top of every Hud::Draw call (every IMGUI event), and every banner takes its space
//     whatever the event, so a Layout pass and the Repaint after it place every banner (and its buttons) alike.
//   - PublishedColumnLeft: the inner bound of the right edge's drawers as the last Hud::Draw laid them out, for what is
//     drawn after the Hud (the showcase tour's strip) and must stop short of the right-hand column.
// Screen space, in GUI pixels: the same at the gameplay and the realistic scale.

#include "UnityCompat/UnityTypes.h"
#include "UI/UiStyle.h"

#include <algorithm>

namespace SpaceGod::UI
{
	enum class HudRegion
	{
		TopCentre,
	};

	class HudLayout final
	{
	public:
		/// On the shared spacing grid (US-T199, UiStyle::Space): 6 between stacked panels, 10 from the screen's edge.
		static constexpr float Gap = UiStyle::Space::Gap;
		static constexpr float Margin = UiStyle::Space::Margin;
		/// Under the clock and scale bar at the top middle (Hud::DrawTopBar: y 10, 52 tall): where the banners start at the least.
		static constexpr float TopBarBottom = 62;
		/// The strip along the bottom right left to the dev menu's hint (CheatMenu: "Shift+F1  DEV MENU", 18 tall).
		static constexpr float DevHintStrip = 24;
		/// CheatMenu's "Shift+F1  DEV MENU" hint: 196 px in from the right edge of the screen.
		static constexpr float DevHintWidth = 196;

		/// US-T440: the banners start under <paramref name="topY"/> (the bottom of the top edge's drawers), never higher than
		/// under the top bar's place.
		void Begin(float topY, float screenW)
		{
			width = screenW;
			topCentreY = std::max(TopBarBottom + Gap + 2, topY + Gap);
		}

		/// A w x h banner in TopCentre, centred, under what the region holds already.
		Rect Take(HudRegion, float w, float h)
		{
			auto r = Rect((width - w) * 0.5f, topCentreY, w, h);
			topCentreY += h + Gap;
			return r;
		}

		/// The next y TopCentre would hand out (for a caption that only needs its top edge).
		float TopCentreNext() const { return topCentreY; }

		/// US-T217: the left edge of the right-hand column as the last Hud::Draw laid it (the screen's right edge when it held
		/// nothing), for panels drawn after the Hud (the showcase tour's strip) that must stop short of it. US-T440: the inner
		/// bound of the right edge's drawers.
		static float PublishedColumnLeft() { return publishedColumnX; }
		static void PublishColumnLeft(float x) { publishedColumnX = x; }

	private:
		static inline float publishedColumnX = 100000;
		float width = 0, topCentreY = 0;
	};
}
