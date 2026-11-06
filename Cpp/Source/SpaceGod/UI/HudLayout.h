#pragma once

// HudLayout (US-T152..T155): the HUD's docked panels take their rects from screen regions instead of fixed pixel spots,
// so two of them can never be drawn over each other.
//
// The rule:
//   - The screen has four docking regions: TopCentre (under the clock bar: the news, then alerts and banners), TopRight
//     (the market or opportunities, then the HUNTED list), BottomRight (the reactor, the sensors, then the selection
//     card above them) and BottomCentre (the status line, in the band left free beside the right-hand column).
//   - A region hands out space in the order panels ask for it, stacking each new panel past the last (downward from a
//     top edge, upward from a bottom edge) with a fixed gap. A panel never picks its own y.
//   - The right-hand column's two stacks share one height: a panel that can scroll (the selection card) asks how much
//     is free between them (FreeRight) and sizes itself to fit, instead of running into the other stack.
//   - The bottom line only uses the band between the left edge and the right-hand column (BottomBand) and shortens its
//     text to fit, so it never runs off the screen or under the column. A bottom-left slot (TakeBottomLeft: the bottom
//     drawer's handle, US-T200) is taken from that band's left end first, so the line starts past it.
//   - Begin() resets the regions at the top of every Hud::Draw call (every IMGUI event), and every panel takes its space
//     whatever the event, so a Layout pass and the Repaint after it place every panel (and its buttons) alike.
// Screen space, in GUI pixels: the same at the gameplay and the realistic scale.

#include "UnityCompat/UnityTypes.h"
#include "UI/UiStyle.h"

#include <algorithm>

namespace SpaceGod::UI
{
	enum class HudRegion
	{
		TopCentre,
		TopRight,
		BottomRight,
	};

	class HudLayout final
	{
	public:
		/// On the shared spacing grid (US-T199, UiStyle::Space): 6 between stacked panels, 10 from the screen's edge.
		static constexpr float Gap = UiStyle::Space::Gap;
		static constexpr float Margin = UiStyle::Space::Margin;
		/// Under the clock and scale bar at the top middle (Hud::DrawTopBar: y 10, 52 tall).
		static constexpr float TopBarBottom = 62;
		/// The strip along the bottom right left to the dev menu's hint (CheatMenu: "Shift+F1  DEV MENU", 18 tall).
		static constexpr float DevHintStrip = 24;

		void Begin(float safeLeft, float safeRight, float screenW, float screenH)
		{
			left = safeLeft;
			right = safeRight;
			width = screenW;
			height = screenH;
			topCentreY = TopBarBottom + Gap + 2;
			topRightY = TopBarBottom + Margin;
			bottomRightY = screenH - Margin - DevHintStrip;
			bottomColumnX = screenW;
			bottomLeftX = safeLeft + Margin;
			publishedColumnX = screenW;
		}

		/// US-T200: a w x h slot at the bottom left of the status band (the bottom drawer's handle), sitting on the bottom
		/// margin like the status line; the band BottomBand hands the status line starts past it.
		Rect TakeBottomLeft(float w, float h)
		{
			auto r = Rect(bottomLeftX, height - Margin - h, w, h);
			bottomLeftX += w + Gap;
			return r;
		}

		/// US-T200: the left edge of whatever BottomRight holds (the screen's right edge while it holds nothing).
		float BottomColumnLeft() const { return bottomColumnX; }

		/// A w x h panel docked in a region: under (top regions) or over (bottom regions) what the region holds already.
		/// Centred in TopCentre, against the right edge in the right-hand regions.
		Rect Take(HudRegion region, float w, float h)
		{
			switch (region)
			{
			case HudRegion::TopCentre:
			{
				auto r = Rect((width - w) * 0.5f, topCentreY, w, h);
				topCentreY += h + Gap;
				return r;
			}
			case HudRegion::TopRight:
			{
				auto r = Rect(right - Margin - w, topRightY, w, h);
				topRightY += h + Gap;
				return r;
			}
			default:
			{
				auto r = Rect(right - Margin - w, bottomRightY - h, w, h);
				bottomRightY -= h + Gap;
				bottomColumnX = std::min(bottomColumnX, r.x);
				publishedColumnX = bottomColumnX;
				return r;
			}
			}
		}

		/// The height still free in the right-hand column, between what TopRight and BottomRight hold.
		float FreeRight() const { return std::max(0.0f, bottomRightY - topRightY); }

		/// The next y TopCentre would hand out (for a caption that only needs its top edge).
		float TopCentreNext() const { return topCentreY; }

		/// The band the bottom line may use, [x0, x1]: from the left edge to the left edge of whatever BottomRight holds
		/// (TopRight never reaches the bottom), and never over the dev menu's hint in the bottom right corner.
		void BottomBand(float& x0, float& x1) const
		{
			x0 = bottomLeftX; // left + Margin, past any bottom-left slot (US-T200)
			x1 = std::min(bottomColumnX, width - DevHintWidth) - Margin;
		}

		/// US-T217: the left edge of the right-hand column as the last Hud::Draw laid it (the screen's right edge when it held
		/// nothing), for panels drawn after the Hud (the showcase tour's strip) that must stop short of it.
		static float PublishedColumnLeft() { return publishedColumnX; }

		/// CheatMenu's "Shift+F1  DEV MENU" hint: 196 px in from the right edge of the screen.
		static constexpr float DevHintWidth = 196;

	private:
		static inline float publishedColumnX = 100000;
		float left = 0, right = 0, width = 0, height = 0;
		float topCentreY = 0, topRightY = 0, bottomRightY = 0, bottomColumnX = 0, bottomLeftX = 0;
	};
}
