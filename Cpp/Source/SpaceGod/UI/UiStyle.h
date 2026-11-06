#pragma once

// UiStyle (US-T199): the one look every HUD panel and menu draws with, so the whole UI reads as one made thing.
//
//   - Palette:   one ink for surfaces, one cyan accent, one off-white for body text, one dim for captions, and the
//                status colours (good / warning / danger). Panels differ only in how opaque they are (Depth).
//   - Type:      one scale of font sizes (Type::...), from the front-end display heading down to captions; every style
//                takes its size from it instead of a literal.
//   - Frame:     a panel is ink with a hairline edge and a bright corner tick at each corner (a 9-slice, so the ticks keep
//                their size whatever the panel's); buttons, tabs and fields are the same ink, a step lighter, with a
//                hairline; rows are a tint only.
//   - Spacing:   a 4 px grid (Space::...); HudLayout's docking gap and margin sit on it.
//   - Glyphs:    the shared marks a panel draws as text (fold open / shut, the separator dot), so every collapsible
//                panel shows the same one.
//
// The styles below are factories: a screen keeps its own GUIStyle members (and the textures they hold, which it drops in
// its DestroyOwnedTextures) but builds them here, so a change of look is made once.

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

namespace SpaceGod::UI::UiStyle
{
	// ---------------------------------------------------------------- palette

	/// The accent: titles that matter, the active control's text, highlights.
	inline constexpr Color Accent{0.45f, 0.85f, 1.0f};
	/// Body text: off-white, easier on the eye over space than pure white.
	inline constexpr Color Text{0.9f, 0.94f, 0.97f};
	/// Headings and titles: full white, bold.
	inline constexpr Color Heading{1.0f, 1.0f, 1.0f};
	/// Captions, hints, key legends.
	inline constexpr Color Dim{0.62f, 0.7f, 0.78f};
	inline constexpr Color Good{0.55f, 1.0f, 0.6f};
	inline constexpr Color Warning{1.0f, 0.72f, 0.35f};
	inline constexpr Color Danger{1.0f, 0.4f, 0.35f};

	/// The surface ink every panel is filled with (its alpha set by Depth).
	inline constexpr Color Ink{0.025f, 0.045f, 0.07f};
	/// A panel's hairline edge, and its corner ticks.
	inline constexpr Color Edge{0.3f, 0.58f, 0.72f, 0.5f};
	inline constexpr Color Tick{0.45f, 0.85f, 1.0f, 0.9f};
	/// Controls: a button's resting fill and edge, and its lit (hover / pressed / chosen) fill and edge.
	inline constexpr Color ControlFill{0.11f, 0.17f, 0.23f, 0.95f};
	inline constexpr Color ControlEdge{0.3f, 0.5f, 0.6f, 0.6f};
	inline constexpr Color ControlOnFill{0.1f, 0.3f, 0.42f, 0.95f};
	inline constexpr Color ControlOnEdge{0.45f, 0.85f, 1.0f, 0.85f};
	/// A text field's well: darker than the panel, with an accent edge.
	inline constexpr Color FieldFill{0.015f, 0.025f, 0.045f, 0.95f};
	/// List rows: a hovered row's tint and a chosen row's.
	inline constexpr Color RowHover{0.2f, 0.45f, 0.6f, 0.28f};
	inline constexpr Color RowSelected{0.2f, 0.55f, 0.75f, 0.45f};
	/// The veil drawn over the view behind a modal menu.
	inline constexpr Color Shade{0, 0, 0, 0.45f};

	/// How opaque a panel is: the HUD's over the live view, a menu's over the paused view, a full screen's.
	enum class Depth
	{
		Hud,
		Overlay,
		Menu,
		Screen,
	};

	constexpr float Alpha(Depth d)
	{
		switch (d)
		{
		case Depth::Hud: return 0.8f;
		case Depth::Overlay: return 0.88f;
		case Depth::Menu: return 0.93f;
		default: return 0.96f;
		}
	}

	constexpr Color InkAt(Depth d) { return Color(Ink.r, Ink.g, Ink.b, Alpha(d)); }

	// ---------------------------------------------------------------- type scale (font em, px)

	namespace Type
	{
		inline constexpr int Display = 40; // the front end's game title
		inline constexpr int Screen = 24;  // a full screen's title (Help)
		inline constexpr int Chapter = 19; // a full screen's chapter
		inline constexpr int Title = 15;   // a panel's title
		inline constexpr int Heading = 14; // a section heading, a front-end button, a toast
		inline constexpr int Body = 13;    // body text, menu buttons, fields
		inline constexpr int Label = 12;   // dense panels: HUD rows, HUD buttons, map text
		inline constexpr int Caption = 11; // captions, hints, key legends
	}

	// ---------------------------------------------------------------- spacing grid

	namespace Space
	{
		inline constexpr float Unit = 4;
		inline constexpr float Tight = 4;   // between a label and its value
		inline constexpr float Gap = 6;     // between stacked panels (HudLayout::Gap)
		inline constexpr float Pad = 8;     // a panel's inner padding
		inline constexpr float Margin = 10; // from the screen's edge (HudLayout::Margin)
		inline constexpr float Inset = 12;  // a menu's inner padding
		inline constexpr int TextPad = 6;   // a row's or button's text inset
	}

	// ---------------------------------------------------------------- glyphs

	namespace Glyph
	{
		/// A collapsible panel's title mark, open and shut (Arial's WGL4 triangles).
		inline constexpr const char* FoldOpen = "\xE2\x96\xBC";  // U+25BC
		inline constexpr const char* FoldShut = "\xE2\x96\xBA";  // U+25BA
		/// Between the parts of a title.
		inline constexpr const char* Separator = "  \xC2\xB7  "; // " · "
	}

	/// "<mark>  TITLE": a collapsible panel's title bar text.
	cs::String Fold(bool open, const cs::String& titleText);

	// ---------------------------------------------------------------- textures

	/// A flat fill with a 1 px edge (none when the edge is clear): the 8 x 8 9-slice every control uses (border 2).
	cs::Ref<Texture2D> Flat(Color fill, Color edge);

	/// A panel: the fill, a hairline edge and a bright tick along each corner (16 x 16, a 9-slice of PanelBorder).
	cs::Ref<Texture2D> PanelFrame(Color fill, Color edge = Edge, Color tick = Tick);
	inline cs::Ref<Texture2D> PanelFrame(Depth d) { return PanelFrame(InkAt(d)); }
	inline constexpr int PanelBorder = 6;

	inline cs::Ref<Texture2D> ControlTexture() { return Flat(ControlFill, ControlEdge); }
	inline cs::Ref<Texture2D> ControlOnTexture() { return Flat(ControlOnFill, ControlOnEdge); }
	inline cs::Ref<Texture2D> FieldTexture() { return Flat(FieldFill, ControlOnEdge); }

	// ---------------------------------------------------------------- styles

	/// A panel's background style (a PanelFrame, or any 9-slice of `border`).
	cs::Ref<GUIStyle> PanelStyle(const cs::Ref<Texture2D>& frame, int border = PanelBorder);

	/// Text: `size` from Type, the colour from the palette.
	cs::Ref<GUIStyle> TextStyle(int size, Color color = Text, FontStyle fontStyle = FontStyle::Normal);
	inline cs::Ref<GUIStyle> TitleStyle(int size = Type::Title) { return TextStyle(size, Heading, FontStyle::Bold); }
	cs::Ref<GUIStyle> CaptionStyle(int size = Type::Caption);

	/// A button: resting on `normal`, lit on `on` when hovered or pressed (its text the accent while pressed).
	cs::Ref<GUIStyle> ButtonStyle(int size, const cs::Ref<Texture2D>& normal, const cs::Ref<Texture2D>& on);
	/// The chosen one of a set: the button lit and bold.
	cs::Ref<GUIStyle> ButtonOnStyle(const cs::Ref<GUIStyle>& button, const cs::Ref<Texture2D>& on);

	/// A list row: plain, tinted on hover; and the chosen row.
	cs::Ref<GUIStyle> RowStyle(int size, const cs::Ref<Texture2D>& hover);
	cs::Ref<GUIStyle> RowSelectedStyle(const cs::Ref<GUIStyle>& row, const cs::Ref<Texture2D>& selected);

	/// A text field in its accent-edged well.
	cs::Ref<GUIStyle> FieldStyle(int size, const cs::Ref<Texture2D>& well);
}
