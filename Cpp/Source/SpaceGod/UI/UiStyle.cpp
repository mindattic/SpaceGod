#include "UI/UiStyle.h"

namespace SpaceGod::UI::UiStyle
{
	cs::String Fold(bool open, const cs::String& titleText)
	{
		return cs::Cat(open ? Glyph::FoldOpen : Glyph::FoldShut, "  ", titleText);
	}

	cs::Ref<Texture2D> Flat(Color fill, Color edge)
	{
		constexpr int n = 8;
		auto t = cs::New<Texture2D>(n, n, TextureFormat::RGBA32, false);
		t->hideFlags = HideFlags::DontSave;
		t->filterMode = FilterMode::Point;
		for (int y = 0; y < n; y++)
			for (int x = 0; x < n; x++)
				t->SetPixel(x, y, x == 0 || y == 0 || x == n - 1 || y == n - 1 ? (edge.a > 0 ? edge : fill) : fill);
		t->Apply();
		return t;
	}

	cs::Ref<Texture2D> PanelFrame(Color fill, Color edge, Color tick)
	{
		// 16 x 16, a 9-slice of PanelBorder (6): the corner cells keep their pixels whatever the panel's size, so each
		// corner's tick (the outer edge lit for 5 px each way, the corner itself a 2 px block) stays as crisp on a small
		// readout as on a full menu. The edges between are the hairline; the middle is the fill.
		constexpr int n = 16, tickLen = 5;
		auto t = cs::New<Texture2D>(n, n, TextureFormat::RGBA32, false);
		t->hideFlags = HideFlags::DontSave;
		t->filterMode = FilterMode::Point;
		bool hasEdge = edge.a > 0;
		for (int y = 0; y < n; y++)
			for (int x = 0; x < n; x++)
			{
				int dx = x < n / 2 ? x : n - 1 - x; // distance from the nearest vertical edge
				int dy = y < n / 2 ? y : n - 1 - y;
				bool onEdge = dx == 0 || dy == 0;
				bool inTick = (dx == 0 && dy < tickLen) || (dy == 0 && dx < tickLen) || (dx <= 1 && dy <= 1);
				Color c = fill;
				if (inTick && tick.a > 0) c = tick;
				else if (onEdge && hasEdge) c = edge;
				t->SetPixel(x, y, c);
			}
		t->Apply();
		return t;
	}

	cs::Ref<GUIStyle> PanelStyle(const cs::Ref<Texture2D>& frame, int border)
	{
		auto s = cs::New<GUIStyle>();
		s->normal.background = frame;
		s->border = RectOffset(border, border, border, border);
		return s;
	}

	cs::Ref<GUIStyle> TextStyle(int size, Color color, FontStyle fontStyle)
	{
		auto s = cs::New<GUIStyle>();
		s->fontSize = size;
		s->fontStyle = fontStyle;
		s->normal.textColor = color;
		return s;
	}

	cs::Ref<GUIStyle> CaptionStyle(int size)
	{
		auto s = TextStyle(size, Dim);
		s->wordWrap = true;
		return s;
	}

	cs::Ref<GUIStyle> ButtonStyle(int size, const cs::Ref<Texture2D>& normal, const cs::Ref<Texture2D>& on)
	{
		auto s = cs::New<GUIStyle>();
		s->fontSize = size;
		s->alignment = TextAnchor::MiddleCenter;
		s->border = RectOffset(2, 2, 2, 2);
		s->normal.background = normal;
		s->normal.textColor = Text;
		s->hover.background = on;
		s->hover.textColor = Heading;
		s->active.background = on;
		s->active.textColor = Accent;
		return s;
	}

	cs::Ref<GUIStyle> ButtonOnStyle(const cs::Ref<GUIStyle>& button, const cs::Ref<Texture2D>& on)
	{
		auto s = cs::New<GUIStyle>(*button);
		s->normal.background = on;
		s->normal.textColor = Heading;
		s->fontStyle = FontStyle::Bold;
		return s;
	}

	cs::Ref<GUIStyle> RowStyle(int size, const cs::Ref<Texture2D>& hover)
	{
		auto s = cs::New<GUIStyle>();
		s->fontSize = size;
		s->alignment = TextAnchor::MiddleLeft;
		s->padding = RectOffset(Space::TextPad, 4, 0, 0);
		s->clipping = TextClipping::Clip;
		s->normal.textColor = Text;
		s->hover.background = hover;
		s->hover.textColor = Heading;
		return s;
	}

	cs::Ref<GUIStyle> RowSelectedStyle(const cs::Ref<GUIStyle>& row, const cs::Ref<Texture2D>& selected)
	{
		auto s = cs::New<GUIStyle>(*row);
		s->normal.background = selected;
		s->normal.textColor = Heading;
		s->fontStyle = FontStyle::Bold;
		return s;
	}

	cs::Ref<GUIStyle> FieldStyle(int size, const cs::Ref<Texture2D>& well)
	{
		auto s = cs::New<GUIStyle>();
		s->fontSize = size;
		s->alignment = TextAnchor::MiddleLeft;
		s->padding = RectOffset(8, 8, 0, 0);
		s->border = RectOffset(2, 2, 2, 2);
		s->clipping = TextClipping::Clip;
		s->normal.background = well;
		s->normal.textColor = Heading;
		s->focused.background = well;
		s->focused.textColor = Heading;
		s->hover.background = well;
		s->hover.textColor = Heading;
		return s;
	}
}
