#include "UnityCompat/UnityGui.h"

#include "UnityCompat/UnityRuntime.h"

#include <unordered_map>
#include <vector>

// The controls below follow Unity's own IMGUI (UnityCsReference GUI.cs / GUIStyle.cs and the native GUIStyle::Draw):
// the same control-id handshake, the same event handling per control, the same state choice when drawing (a hover /
// active / focused state counts only when it has a background, the "hover text colour needs a hover background"
// quirk), the same scroll-view sizing (15 px scrollbars, a wheel notch is delta 3 scrolled 20 px a unit). Where Unity's
// own numbers are not known (its skin), the values are the C# track's (US-T28, UnityCompat/Imgui), so both look the same. Text is laid out here (lines, word wrap, alignment) on the renderer's measurements, so what CalcSize /
// CalcHeight report is exactly what Repaint draws.

namespace SpaceGod::UnityCompat
{
	namespace
	{
		IGuiRenderer* renderer = nullptr;

		/// The event of the current pass: the host's (so Use() reaches it), or a Repaint stand-in outside a pass.
		Event fallbackEvent;
		Event* currentEvent = &fallbackEvent;

		/// The screen-space mouse of the current event, before any group shifted it.
		Vector2 screenMouse;

		struct ClipFrame
		{
			Rect clip;        // visible screen rect (intersection of every enclosing clip)
			Vector2 origin;   // screen position of this frame's (0, 0)
			bool pushedToRenderer = false;
		};
		std::vector<ClipFrame> clips;

		struct ScrollFrame
		{
			int id = 0;
			Rect position;    // in the coordinates the scroll view was begun in
			Rect viewRect;
			Rect clipRect;
		};
		std::vector<ScrollFrame> scrolls;

		/// Unity's ScrollViewState, by control id: the position EndScrollView moved (the wheel) and applies next time.
		struct ScrollState
		{
			Vector2 scrollPosition;
			bool apply = false;
		};
		std::unordered_map<int, ScrollState> scrollStates;

		/// Per-control scratch for scrollbars and sliders being dragged.
		float dragOffset = 0;

		int nextControlId = 1;
		cs::String pendingControlName;
		std::unordered_map<int, cs::String> controlNames;
		cs::String pendingFocusName;

		constexpr int DefaultFontSize = 13;
		constexpr float ScrollbarSize = 15;

		bool IsRepaint() { return currentEvent->type == EventType::Repaint; }

		Rect Intersect(const Rect& a, const Rect& b)
		{
			float x0 = Mathf::Max(a.x, b.x), y0 = Mathf::Max(a.y, b.y);
			float x1 = Mathf::Min(a.xMax(), b.xMax()), y1 = Mathf::Min(a.yMax(), b.yMax());
			return Rect(x0, y0, Mathf::Max(0.0f, x1 - x0), Mathf::Max(0.0f, y1 - y0));
		}

		Vector2 Origin() { return clips.empty() ? Vector2() : clips.back().origin; }

		Rect ToScreen(const Rect& r)
		{
			Vector2 o = Origin();
			return Rect(r.x + o.x, r.y + o.y, r.width, r.height);
		}

		/// Unity hides the mouse from controls outside the visible clip (so a scrolled-away row never takes a click).
		void UpdateLocalMouse()
		{
			if (clips.empty())
			{
				currentEvent->mousePosition = screenMouse;
				return;
			}
			const ClipFrame& top = clips.back();
			if (top.clip.Contains(screenMouse)) currentEvent->mousePosition = screenMouse - top.origin;
			else currentEvent->mousePosition = Vector2(-10000, -10000);
		}

		void PushClipFrame(const Rect& screenClip, Vector2 origin)
		{
			ClipFrame f;
			f.clip = clips.empty() ? screenClip : Intersect(clips.back().clip, screenClip);
			f.origin = origin;
			f.pushedToRenderer = IsRepaint() && renderer != nullptr;
			if (f.pushedToRenderer) renderer->PushClip(f.clip);
			clips.push_back(f);
			UpdateLocalMouse();
		}

		void PopClipFrame()
		{
			if (clips.size() <= 1) return; // the screen's own frame stays
			if (clips.back().pushedToRenderer && renderer) renderer->PopClip();
			clips.pop_back();
			UpdateLocalMouse();
		}

		Color Tint(Color c, Color by) { return Color(c.r * by.r, c.g * by.g, c.b * by.b, c.a * by.a); }

		Color DisabledFade(Color c)
		{
			if (!GUI::enabled) c.a *= 0.5f;
			return c;
		}

		// ---------------------------------------------------------------- text layout

		/// UTF-8 code point boundaries: the byte offset after the code point starting at i.
		size_t NextCodePoint(const cs::String& s, size_t i)
		{
			unsigned char c = static_cast<unsigned char>(s[i]);
			size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
			return i + n < s.size() ? i + n : s.size();
		}

		float MeasureWidth(const cs::String& line, int size, FontStyle style)
		{
			if (line.empty() || !renderer) return 0;
			return renderer->MeasureLine(line, size, style).x;
		}

		float LineHeightOf(int size, FontStyle style)
		{
			return renderer ? renderer->LineHeight(size, style) : static_cast<float>(size) * 1.15f;
		}

		/// Breaks `text` into the lines drawn: at every '\n', and (wrapWidth > 0) greedily at spaces, a word longer
		/// than the width broken where it overflows, as Unity's TextGenerator does.
		std::vector<cs::String> LayoutLines(const cs::String& text, int size, FontStyle style, float wrapWidth)
		{
			std::vector<cs::String> lines;
			size_t start = 0;
			while (true)
			{
				size_t nl = text.find('\n', start);
				cs::String para = text.substr(start, nl == cs::String::npos ? cs::String::npos : nl - start);
				if (!para.empty() && para.back() == '\r') para.pop_back();
				if (wrapWidth <= 0 || MeasureWidth(para, size, style) <= wrapWidth) lines.push_back(para);
				else
				{
					cs::String line;
					size_t i = 0;
					while (i < para.size())
					{
						// The next word with the spaces before it.
						size_t j = i;
						while (j < para.size() && para[j] == ' ') j++;
						while (j < para.size() && para[j] != ' ') j = NextCodePoint(para, j);
						cs::String word = para.substr(i, j - i);
						cs::String candidate = line + word;
						if (line.empty() || MeasureWidth(candidate, size, style) <= wrapWidth)
						{
							if (line.empty())
							{
								// A first word: drop its leading spaces after a wrap, and break it if it alone is too wide.
								size_t k = 0;
								if (!lines.empty()) while (k < word.size() && word[k] == ' ') k++;
								word = word.substr(k);
								if (MeasureWidth(word, size, style) > wrapWidth)
								{
									cs::String part;
									size_t p = 0;
									while (p < word.size())
									{
										size_t q = NextCodePoint(word, p);
										cs::String next = part + word.substr(p, q - p);
										if (!part.empty() && MeasureWidth(next, size, style) > wrapWidth)
										{
											lines.push_back(part);
											part = word.substr(p, q - p);
										}
										else part = next;
										p = q;
									}
									line = part;
								}
								else line = word;
							}
							else line = candidate;
						}
						else
						{
							lines.push_back(line);
							size_t k = 0;
							while (k < word.size() && word[k] == ' ') k++;
							line.clear();
							i = i + k; // re-run the word without its spaces on a fresh line
							continue;
						}
						i = j;
					}
					lines.push_back(line);
				}
				if (nl == cs::String::npos) break;
				start = nl + 1;
			}
			if (lines.empty()) lines.push_back(cs::String());
			return lines;
		}

		void DrawTextBlock(const GUIStyle& style, const Rect& contentRect, const cs::String& text, Color color)
		{
			if (text.empty() || !renderer) return;
			int size = style.EffectiveFontSize();
			float wrap = style.wordWrap ? contentRect.width : 0;
			auto lines = LayoutLines(text, size, style.fontStyle, wrap);
			float lh = LineHeightOf(size, style.fontStyle);
			float blockH = lh * static_cast<float>(lines.size());
			int a = static_cast<int>(style.alignment);
			int row = a / 3, col = a % 3;
			float y = row == 0 ? contentRect.y : row == 1 ? contentRect.y + (contentRect.height - blockH) * 0.5f : contentRect.yMax() - blockH;
			bool clip = style.clipping != TextClipping::Overflow;
			if (clip) PushClipFrame(ToScreen(contentRect), Origin());
			Vector2 o = Origin();
			for (const auto& line : lines)
			{
				float w = MeasureWidth(line, size, style.fontStyle);
				float x = col == 0 ? contentRect.x : col == 1 ? contentRect.x + (contentRect.width - w) * 0.5f : contentRect.xMax() - w;
				// Whole pixels, as Unity's text mesh snaps: crisp glyphs.
				Vector2 at(Mathf::Floor(x + o.x + 0.5f), Mathf::Floor(y + o.y + 0.5f));
				if (!line.empty()) renderer->DrawText(at, line, size, style.fontStyle, color, GUI::matrix);
				y += lh;
			}
			if (clip) PopClipFrame();
		}

		void DrawBackground(const Rect& r, const cs::Ref<Texture2D>& tex, const RectOffset& border)
		{
			if (!tex || !renderer) return;
			Color tint = DisabledFade(Tint(GUI::color, GUI::backgroundColor));
			renderer->DrawTexture(ToScreen(r), tex.get(), Rect(0, 0, 1, 1), tint, border, GUI::matrix);
		}

		const GUIStyleState& ChooseState(const GUIStyle& s, bool isHover, bool isActive, bool on, bool hasKeyboardFocus)
		{
			// Native GUIStyle::Draw: a state replaces normal only if it has a background.
			const GUIStyleState* result = &s.normal;
			if (on)
			{
				if (s.onNormal.background) result = &s.onNormal;
				if (isHover && s.onHover.background) result = &s.onHover;
				if (isHover && isActive && s.onActive.background) result = &s.onActive;
				if (hasKeyboardFocus && s.onFocused.background) result = &s.onFocused;
			}
			else
			{
				if (isHover && s.hover.background) result = &s.hover;
				if (isHover && isActive && s.active.background) result = &s.active;
				if (hasKeyboardFocus && s.focused.background) result = &s.focused;
			}
			return *result;
		}

		/// A skin swatch: an 8 px square with a rim and cut corners (rounded at 2 px), as the C# track's skin.
		cs::Ref<Texture2D> Rounded(Color fill, Color edge)
		{
			const int n = 8;
			auto t = cs::New<Texture2D>(n, n, TextureFormat::RGBA32, false);
			t->name = "Skin";
			t->hideFlags = HideFlags::DontSave;
			for (int y = 0; y < n; y++)
				for (int x = 0; x < n; x++)
				{
					bool corner = (x == 0 || x == n - 1) && (y == 0 || y == n - 1);
					bool rim = x == 0 || y == 0 || x == n - 1 || y == n - 1;
					t->SetPixel(x, y, corner ? Color::clear() : rim ? edge : fill);
				}
			t->Apply();
			return t;
		}

		/// Unity's dark runtime skin, approximated exactly as the C# track's GUISkin.BuiltIn (US-T28) so both tracks
		/// look the same: the default font at 13, its padding and margins, and its greys; 15 px scrollbars.
		GUISkin MakeSkin()
		{
			GUISkin s;
			Color light(0.9f, 0.9f, 0.9f);
			auto boxBg = Rounded(Color(0.1f, 0.1f, 0.1f, 0.5f), Color(0.2f, 0.2f, 0.2f, 0.6f));
			auto buttonBg = Rounded(Color(0.33f, 0.33f, 0.33f, 1.0f), Color(0.14f, 0.14f, 0.14f, 1.0f));
			auto buttonHover = Rounded(Color(0.4f, 0.4f, 0.4f, 1.0f), Color(0.14f, 0.14f, 0.14f, 1.0f));
			auto buttonDown = Rounded(Color(0.2f, 0.2f, 0.2f, 1.0f), Color(0.1f, 0.1f, 0.1f, 1.0f));
			auto fieldBg = Rounded(Color(0.16f, 0.16f, 0.16f, 1.0f), Color(0.1f, 0.1f, 0.1f, 1.0f));
			auto track = Rounded(Color(0.12f, 0.12f, 0.12f, 0.6f), Color(0.12f, 0.12f, 0.12f, 0.6f));
			auto thumb = Rounded(Color(0.5f, 0.5f, 0.5f, 0.95f), Color(0.35f, 0.35f, 0.35f, 0.95f));
			RectOffset b(2, 2, 2, 2);

			s.box = cs::New<GUIStyle>();
			s.box->name = "box";
			s.box->normal.background = boxBg;
			s.box->normal.textColor = Color(0.8f, 0.8f, 0.8f);
			s.box->border = b;
			s.box->padding = RectOffset(4, 4, 4, 4);
			s.box->margin = RectOffset(4, 4, 4, 4);
			s.box->alignment = TextAnchor::UpperCenter;
			s.box->wordWrap = true;

			s.button = cs::New<GUIStyle>();
			s.button->name = "button";
			s.button->border = b;
			s.button->padding = RectOffset(6, 6, 3, 3);
			s.button->margin = RectOffset(4, 4, 4, 4);
			s.button->alignment = TextAnchor::MiddleCenter;
			s.button->normal.background = buttonBg;
			s.button->normal.textColor = light;
			s.button->hover.background = buttonHover;
			s.button->hover.textColor = Color::white();
			s.button->active.background = buttonDown;
			s.button->active.textColor = Color::white();

			s.label = cs::New<GUIStyle>();
			s.label->name = "label";
			s.label->padding = RectOffset(2, 2, 1, 2);
			s.label->margin = RectOffset(4, 4, 2, 2);
			s.label->wordWrap = true;
			s.label->normal.textColor = light;

			s.textField = cs::New<GUIStyle>();
			s.textField->name = "textfield";
			s.textField->border = b;
			s.textField->padding = RectOffset(3, 3, 3, 3);
			s.textField->margin = RectOffset(4, 4, 2, 2);
			s.textField->clipping = TextClipping::Clip;
			s.textField->normal.background = fieldBg;
			s.textField->normal.textColor = Color(0.8f, 0.8f, 0.8f);
			s.textField->focused.background = fieldBg;
			s.textField->focused.textColor = Color::white();
			s.textArea = cs::New<GUIStyle>(*s.textField);
			s.textArea->name = "textarea";
			s.textArea->wordWrap = true;

			s.toggle = cs::New<GUIStyle>(*s.button);
			s.toggle->name = "toggle";
			s.toggle->onNormal.background = buttonDown;
			s.toggle->onNormal.textColor = Color::white();
			s.toggle->alignment = TextAnchor::MiddleLeft;
			s.window = cs::New<GUIStyle>(*s.box);
			s.window->name = "window";

			s.verticalScrollbar = cs::New<GUIStyle>();
			s.verticalScrollbar->name = "verticalscrollbar";
			s.verticalScrollbar->normal.background = track;
			s.verticalScrollbar->border = b;
			s.verticalScrollbar->fixedWidth = ScrollbarSize;
			s.verticalScrollbarThumb = cs::New<GUIStyle>();
			s.verticalScrollbarThumb->name = "verticalscrollbarthumb";
			s.verticalScrollbarThumb->normal.background = thumb;
			s.verticalScrollbarThumb->border = b;
			s.verticalScrollbarThumb->fixedWidth = ScrollbarSize;
			s.horizontalScrollbar = cs::New<GUIStyle>();
			s.horizontalScrollbar->name = "horizontalscrollbar";
			s.horizontalScrollbar->normal.background = track;
			s.horizontalScrollbar->border = b;
			s.horizontalScrollbar->fixedHeight = ScrollbarSize;
			s.horizontalScrollbarThumb = cs::New<GUIStyle>();
			s.horizontalScrollbarThumb->name = "horizontalscrollbarthumb";
			s.horizontalScrollbarThumb->normal.background = thumb;
			s.horizontalScrollbarThumb->border = b;
			s.horizontalScrollbarThumb->fixedHeight = ScrollbarSize;
			s.horizontalSlider = cs::New<GUIStyle>();
			s.horizontalSlider->name = "horizontalslider";
			s.horizontalSlider->normal.background = track;
			s.horizontalSlider->border = b;
			s.horizontalSlider->fixedHeight = 12;
			s.horizontalSliderThumb = cs::New<GUIStyle>();
			s.horizontalSliderThumb->name = "horizontalsliderthumb";
			s.horizontalSliderThumb->normal.background = thumb;
			s.horizontalSliderThumb->hover.background = buttonHover;
			s.horizontalSliderThumb->border = b;
			s.horizontalSliderThumb->fixedWidth = 12;
			s.horizontalSliderThumb->fixedHeight = 12;
			s.verticalSlider = cs::New<GUIStyle>(*s.horizontalSlider);
			s.verticalSliderThumb = cs::New<GUIStyle>(*s.horizontalSliderThumb);
			s.scrollView = cs::New<GUIStyle>();
			s.scrollView->name = "scrollview";
			return s;
		}

		/// A scrollbar (Unity's GUI.Scroller): value in [left, right - size], dragged by its thumb, paged by the track.
		float Scroller(const Rect& position, float value, float size, float left, float right, const GUIStyle& slider,
			const GUIStyle& thumb, bool horiz)
		{
			int id = GUIUtility::GetControlID(FocusType::Passive);
			float range = right - left;
			float maxValue = right - size;
			if (maxValue < left) maxValue = left;
			value = Mathf::Clamp(value, left, maxValue);
			float length = horiz ? position.width : position.height;
			float thumbLen = range > 0 ? Mathf::Max(length * size / range, 10.0f) : length;
			if (thumbLen > length) thumbLen = length;
			float travel = length - thumbLen;
			float t = maxValue > left ? (value - left) / (maxValue - left) : 0;
			Rect thumbRect = horiz
				? Rect(position.x + travel * t, position.y, thumbLen, position.height)
				: Rect(position.x, position.y + travel * t, position.width, thumbLen);
			Event& e = Event::current();
			Vector2 m = e.mousePosition;
			float along = horiz ? m.x : m.y;
			float start = horiz ? position.x : position.y;
			switch (e.type)
			{
				case EventType::MouseDown:
					if (GUI::enabled && position.Contains(m) && e.button == 0)
					{
						if (thumbRect.Contains(m)) dragOffset = along - (horiz ? thumbRect.x : thumbRect.y);
						else
						{
							// Page towards the click, then drag from the thumb's middle.
							float thumbStart = horiz ? thumbRect.x : thumbRect.y;
							value = along < thumbStart ? value - size : value + size;
							value = Mathf::Clamp(value, left, maxValue);
							dragOffset = thumbLen * 0.5f;
						}
						GUIUtility::hotControl = id;
						e.Use();
						GUI::changed = true;
					}
					break;
				case EventType::MouseDrag:
					if (GUIUtility::hotControl == id)
					{
						float p = along - dragOffset - start;
						value = travel > 0 ? left + Mathf::Clamp01(p / travel) * (maxValue - left) : left;
						e.Use();
						GUI::changed = true;
					}
					break;
				case EventType::MouseUp:
					if (GUIUtility::hotControl == id)
					{
						GUIUtility::hotControl = 0;
						e.Use();
					}
					break;
				case EventType::Repaint:
				{
					slider.Draw(position, GUIContent::none(), false, false, false, false);
					t = maxValue > left ? (Mathf::Clamp(value, left, maxValue) - left) / (maxValue - left) : 0;
					thumbRect = horiz
						? Rect(position.x + travel * t, position.y, thumbLen, position.height)
						: Rect(position.x, position.y + travel * t, position.width, thumbLen);
					thumb.Draw(thumbRect, GUIContent::none(), thumbRect.Contains(m), GUIUtility::hotControl == id, false, false);
					break;
				}
				default:
					break;
			}
			return value;
		}
	}

	// ---------------------------------------------------------------- GuiMatrix

	GuiMatrix GuiMatrix::operator*(const GuiMatrix& b) const
	{
		GuiMatrix r;
		r.m00 = m00 * b.m00 + m01 * b.m10;
		r.m01 = m00 * b.m01 + m01 * b.m11;
		r.m02 = m00 * b.m02 + m01 * b.m12 + m02;
		r.m10 = m10 * b.m00 + m11 * b.m10;
		r.m11 = m10 * b.m01 + m11 * b.m11;
		r.m12 = m10 * b.m02 + m11 * b.m12 + m12;
		return r;
	}

	GuiMatrix GuiMatrix::inverse() const
	{
		float det = m00 * m11 - m01 * m10;
		if (det == 0) return GuiMatrix();
		float inv = 1.0f / det;
		GuiMatrix r;
		r.m00 = m11 * inv;
		r.m01 = -m01 * inv;
		r.m10 = -m10 * inv;
		r.m11 = m00 * inv;
		r.m02 = -(r.m00 * m02 + r.m01 * m12);
		r.m12 = -(r.m10 * m02 + r.m11 * m12);
		return r;
	}

	GuiMatrix GuiMatrix::Translate(Vector2 t)
	{
		GuiMatrix r;
		r.m02 = t.x;
		r.m12 = t.y;
		return r;
	}

	GuiMatrix GuiMatrix::Rotate(float degrees)
	{
		// GUI space is y-down, so Unity's Quaternion.Euler(0, 0, angle) turns clockwise on screen.
		float rad = degrees * Mathf::Deg2Rad;
		float c = Mathf::Cos(rad), s = Mathf::Sin(rad);
		GuiMatrix r;
		r.m00 = c;
		r.m01 = -s;
		r.m10 = s;
		r.m11 = c;
		return r;
	}

	// ---------------------------------------------------------------- GUIContent, GUIStyle

	const GUIContent& GUIContent::none()
	{
		static const GUIContent n;
		return n;
	}

	const GUIStyle& GUIStyle::none()
	{
		static const GUIStyle n = []
		{
			GUIStyle s;
			s.name = "none";
			return s;
		}();
		return n;
	}

	int GUIStyle::EffectiveFontSize() const { return fontSize > 0 ? fontSize : DefaultFontSize; }

	float GUIStyle::lineHeight() const { return LineHeightOf(EffectiveFontSize(), fontStyle); }

	Vector2 GUIStyle::CalcSize(const GUIContent& content) const
	{
		int size = EffectiveFontSize();
		auto lines = LayoutLines(content.text, size, fontStyle, 0);
		float w = 0;
		for (const auto& l : lines) w = Mathf::Max(w, MeasureWidth(l, size, fontStyle));
		float h = LineHeightOf(size, fontStyle) * static_cast<float>(lines.size());
		if (content.image)
		{
			if (content.text.empty())
			{
				w = static_cast<float>(content.image->width);
				h = static_cast<float>(content.image->height);
			}
			else
			{
				w += static_cast<float>(content.image->width);
				h = Mathf::Max(h, static_cast<float>(content.image->height));
			}
		}
		Vector2 r(w + padding.horizontal(), h + padding.vertical());
		if (fixedWidth > 0) r.x = fixedWidth;
		if (fixedHeight > 0) r.y = fixedHeight;
		return r;
	}

	float GUIStyle::CalcHeight(const GUIContent& content, float width) const
	{
		if (fixedHeight > 0) return fixedHeight;
		int size = EffectiveFontSize();
		float wrap = wordWrap ? Mathf::Max(1.0f, width - padding.horizontal()) : 0;
		auto lines = LayoutLines(content.text, size, fontStyle, wrap);
		float h = LineHeightOf(size, fontStyle) * static_cast<float>(lines.size());
		if (content.image && content.text.empty()) h = static_cast<float>(content.image->height);
		return h + padding.vertical();
	}

	void GUIStyle::Draw(const Rect& position, const GUIContent& content, bool isHover, bool isActive, bool on, bool hasKeyboardFocus) const
	{
		if (!IsRepaint()) return;
		const GUIStyleState& state = ChooseState(*this, isHover, isActive, on, hasKeyboardFocus);
		DrawBackground(overflow.Add(position), state.background, border);
		Rect contentRect = padding.Remove(position);
		contentRect.x += contentOffset.x;
		contentRect.y += contentOffset.y;
		if (content.image && renderer)
		{
			// Image only (no text): fitted in the content rect, centred as the alignment says.
			float iw = static_cast<float>(content.image->width), ih = static_cast<float>(content.image->height);
			if (iw > 0 && ih > 0)
			{
				float scale = Mathf::Min(1.0f, Mathf::Min(contentRect.width / iw, contentRect.height / ih));
				float w = iw * scale, h = ih * scale;
				Rect r(contentRect.x + (contentRect.width - w) * 0.5f, contentRect.y + (contentRect.height - h) * 0.5f, w, h);
				if (!content.text.empty()) r.x = contentRect.x;
				Color tint = DisabledFade(Tint(GUI::color, GUI::contentColor));
				renderer->DrawTexture(ToScreen(r), content.image.get(), Rect(0, 0, 1, 1), tint, RectOffset(), GUI::matrix);
				if (!content.text.empty())
				{
					contentRect.x += w;
					contentRect.width -= w;
				}
			}
		}
		Color textColor = DisabledFade(Tint(Tint(state.textColor, GUI::color), GUI::contentColor));
		DrawTextBlock(*this, contentRect, content.text, textColor);
	}

	void GUIStyle::Draw(const Rect& position, const GUIContent& content, int controlID, bool on) const
	{
		Vector2 m = Event::current().mousePosition;
		bool isActive = GUIUtility::hotControl == controlID;
		bool isHover = GUI::enabled && position.Contains(m) && (GUIUtility::hotControl == 0 || isActive);
		Draw(position, content, isHover, isActive, on, GUIUtility::keyboardControl == controlID);
	}

	// ---------------------------------------------------------------- Event

	bool Event::isMouse() const
	{
		return type == EventType::MouseDown || type == EventType::MouseUp || type == EventType::MouseMove
			|| type == EventType::MouseDrag || type == EventType::ContextClick;
	}

	void Event::Use()
	{
		// Repaint and Layout cannot be used (Unity logs an error and ignores it).
		if (type == EventType::Repaint || type == EventType::Layout) return;
		type = EventType::Used;
	}

	Event& Event::current() { return *currentEvent; }

	// ---------------------------------------------------------------- GUIUtility

	int GUIUtility::GetControlID(FocusType focus)
	{
		int id = nextControlId++;
		if (focus == FocusType::Keyboard)
		{
			if (!pendingControlName.empty())
			{
				controlNames[id] = pendingControlName;
				if (!pendingFocusName.empty() && pendingFocusName == pendingControlName)
				{
					keyboardControl = id;
					pendingFocusName.clear();
				}
				pendingControlName.clear();
			}
		}
		return id;
	}

	int GUIUtility::GetControlID(int, FocusType focus) { return GetControlID(focus); }
	int GUIUtility::GetControlID(int, FocusType focus, const Rect&) { return GetControlID(focus); }

	void GUIUtility::ExitGUI() { throw ExitGUIException(); }

	void GUIUtility::RotateAroundPivot(float angle, Vector2 pivotPoint)
	{
		Vector2 p = GUIToScreenPoint(pivotPoint);
		GUI::matrix = GuiMatrix::Translate(p) * GuiMatrix::Rotate(angle) * GuiMatrix::Translate(Vector2(-p.x, -p.y)) * GUI::matrix;
	}

	void GUIUtility::ScaleAroundPivot(Vector2 scale, Vector2 pivotPoint)
	{
		Vector2 p = GUIToScreenPoint(pivotPoint);
		GuiMatrix s;
		s.m00 = scale.x;
		s.m11 = scale.y;
		GUI::matrix = GuiMatrix::Translate(p) * s * GuiMatrix::Translate(Vector2(-p.x, -p.y)) * GUI::matrix;
	}

	Vector2 GUIUtility::GUIToScreenPoint(Vector2 guiPoint) { return guiPoint + Origin(); }
	Vector2 GUIUtility::ScreenToGUIPoint(Vector2 screenPoint) { return screenPoint - Origin(); }

	// ---------------------------------------------------------------- GUI: host side

	void GUI::SetRenderer(IGuiRenderer* r) { renderer = r; }
	IGuiRenderer* GUI::Renderer() { return renderer; }

	GUI::FrameStats& GUI::Stats()
	{
		static FrameStats stats;
		return stats;
	}

	std::unordered_map<cs::String, Rect>& GUI::DrawnButtons()
	{
		static std::unordered_map<cs::String, Rect> buttons;
		return buttons;
	}

	cs::String GUI::FrameStats::ToString() const
	{
		return cs::Cat(Labels, " labels, ", Boxes, " boxes, ", Buttons, " buttons, ", Textures, " texture quads, ", Texts, " text runs (",
			Passes, " OnGUI passes)");
	}

	void GUI::BeginFrame()
	{
		// Names map control ids of the frame they were drawn in.
		controlNames.clear();
	}

	void GUI::RunOnGUI(const std::function<void()>& onGui, Event& e, int handlerIndex)
	{
		// GUIUtility.BeginGUI: a fresh state for every OnGUI call.
		color = Color::white();
		backgroundColor = Color::white();
		contentColor = Color::white();
		enabled = true;
		changed = false;
		matrix = GuiMatrix::identity();
		nextControlId = (handlerIndex + 1) * 1000000 + 1;
		pendingControlName.clear();

		currentEvent = &e;
		screenMouse = e.mousePosition;
		clips.clear();
		scrolls.clear();
		ClipFrame root;
		root.clip = Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height));
		clips.push_back(root);
		UpdateLocalMouse();

		try
		{
			onGui();
		}
		catch (const ExitGUIException&)
		{
			// GUIUtility.ExitGUI: the pass ends here, as Unity's.
		}

		while (clips.size() > 1) PopClipFrame();
		clips.clear();
		scrolls.clear();
		e.mousePosition = screenMouse;
		currentEvent = &fallbackEvent;
		matrix = GuiMatrix::identity();
	}

	GUISkin& GUI::skin()
	{
		static GUISkin s = MakeSkin();
		return s;
	}

	// ---------------------------------------------------------------- GUI: controls

	void GUI::Label(const Rect& position, const cs::String& text) { Label(position, GUIContent(text), *skin().label); }
	void GUI::Label(const Rect& position, const GUIContent& content) { Label(position, content, *skin().label); }
	void GUI::Label(const Rect& position, const cs::String& text, const GUIStyle& style) { Label(position, GUIContent(text), style); }

	void GUI::Label(const Rect& position, const GUIContent& content, const GUIStyle& style)
	{
		if (!IsRepaint()) return;
		style.Draw(position, content, false, false, false, false);
		Stats().Labels++;
	}

	void GUI::Box(const Rect& position, const cs::String& text) { Box(position, GUIContent(text), *skin().box); }

	void GUI::Box(const Rect& position, const GUIContent& content, const GUIStyle& style)
	{
		int id = GUIUtility::GetControlID(FocusType::Passive);
		(void)id;
		if (!IsRepaint()) return;
		style.Draw(position, content, false, false, false, false);
		Stats().Boxes++;
	}

	bool GUI::Button(const Rect& position, const cs::String& text) { return Button(position, GUIContent(text), *skin().button); }
	bool GUI::Button(const Rect& position, const GUIContent& content) { return Button(position, content, *skin().button); }

	bool GUI::Button(const Rect& position, const GUIContent& content, const GUIStyle& style)
	{
		int id = GUIUtility::GetControlID(FocusType::Passive);
		Event& e = Event::current();
		switch (e.type)
		{
			case EventType::MouseDown:
				if (enabled && position.Contains(e.mousePosition))
				{
					GUIUtility::hotControl = id;
					e.Use();
				}
				return false;
			case EventType::MouseUp:
				if (GUIUtility::hotControl == id)
				{
					GUIUtility::hotControl = 0;
					e.Use();
					if (position.Contains(e.mousePosition))
					{
						changed = true;
						Stats().Clicks++;
						return true;
					}
				}
				return false;
			case EventType::MouseDrag:
				if (GUIUtility::hotControl == id) e.Use();
				return false;
			case EventType::Repaint:
				style.Draw(position, content, id, false);
				Stats().Buttons++;
				if (enabled && !content.text.empty() && !clips.empty())
				{
					Rect screen = Intersect(ToScreen(position), clips.back().clip);
					if (screen.width > 0 && screen.height > 0) DrawnButtons().emplace(content.text, screen);
				}
				return false;
			default:
				return false;
		}
	}

	bool GUI::Toggle(const Rect& position, bool value, const cs::String& text) { return Toggle(position, value, GUIContent(text), *skin().toggle); }

	bool GUI::Toggle(const Rect& position, bool value, const GUIContent& content, const GUIStyle& style)
	{
		int id = GUIUtility::GetControlID(FocusType::Passive);
		Event& e = Event::current();
		switch (e.type)
		{
			case EventType::MouseDown:
				if (enabled && position.Contains(e.mousePosition))
				{
					GUIUtility::hotControl = id;
					e.Use();
				}
				break;
			case EventType::MouseUp:
				if (GUIUtility::hotControl == id)
				{
					GUIUtility::hotControl = 0;
					e.Use();
					if (position.Contains(e.mousePosition))
					{
						changed = true;
						return !value;
					}
				}
				break;
			case EventType::MouseDrag:
				if (GUIUtility::hotControl == id) e.Use();
				break;
			case EventType::Repaint:
				style.Draw(position, content, id, value);
				break;
			default:
				break;
		}
		return value;
	}

	cs::String GUI::TextField(const Rect& position, const cs::String& text) { return TextField(position, text, -1, *skin().textField); }
	cs::String GUI::TextField(const Rect& position, const cs::String& text, int maxLength) { return TextField(position, text, maxLength, *skin().textField); }
	cs::String GUI::TextField(const Rect& position, const cs::String& text, const GUIStyle& style) { return TextField(position, text, -1, style); }

	cs::String GUI::TextField(const Rect& position, const cs::String& text, int maxLength, const GUIStyle& style)
	{
		int id = GUIUtility::GetControlID(FocusType::Keyboard);
		Event& e = Event::current();
		cs::String result = text;
		bool focused = GUIUtility::keyboardControl == id;
		switch (e.type)
		{
			case EventType::MouseDown:
				if (enabled && position.Contains(e.mousePosition))
				{
					GUIUtility::keyboardControl = id;
					GUIUtility::hotControl = id;
					e.Use();
				}
				else if (focused) GUIUtility::keyboardControl = 0; // a click elsewhere takes the focus away
				break;
			case EventType::MouseUp:
				if (GUIUtility::hotControl == id)
				{
					GUIUtility::hotControl = 0;
					e.Use();
				}
				break;
			case EventType::KeyDown:
				if (!focused || !enabled) break;
				if (e.keyCode == KeyCode::Backspace)
				{
					if (!result.empty())
					{
						// Remove the last UTF-8 code point.
						size_t i = result.size() - 1;
						while (i > 0 && (static_cast<unsigned char>(result[i]) & 0xC0) == 0x80) i--;
						result.erase(i);
						changed = true;
					}
					e.Use();
				}
				else if (e.character >= 32 && e.character != 127)
				{
					// A single-line field takes printable characters up to maxLength (in UTF-16 units, as C#'s).
					int length = 0;
					cs::ForEachUtf16(result, [&](uint32_t) { length++; });
					int add = e.character > 0xFFFF ? 2 : 1;
					if (maxLength < 0 || length + add <= maxLength)
					{
						char32_t c = e.character;
						if (c < 0x80) result += static_cast<char>(c);
						else if (c < 0x800)
						{
							result += static_cast<char>(0xC0 | (c >> 6));
							result += static_cast<char>(0x80 | (c & 0x3F));
						}
						else if (c < 0x10000)
						{
							result += static_cast<char>(0xE0 | (c >> 12));
							result += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
							result += static_cast<char>(0x80 | (c & 0x3F));
						}
						else
						{
							result += static_cast<char>(0xF0 | (c >> 18));
							result += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
							result += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
							result += static_cast<char>(0x80 | (c & 0x3F));
						}
						changed = true;
					}
					e.Use();
				}
				break;
			case EventType::Repaint:
			{
				style.Draw(position, GUIContent(result), id, false);
				if (focused && renderer && Mathf::Repeat(Time::unscaledTime, 1.0f) < 0.5f)
				{
					// The caret after the text.
					Rect content = style.padding.Remove(position);
					float w = MeasureWidth(result, style.EffectiveFontSize(), style.fontStyle);
					float lh = style.lineHeight();
					float y = content.y + (content.height - lh) * 0.5f;
					Color c = DisabledFade(Tint(Tint(style.focused.textColor, color), contentColor));
					renderer->DrawTexture(ToScreen(Rect(Mathf::Min(content.x + w + 1, content.xMax() - 1), y, 1, lh)),
						Texture2D::whiteTexture().get(), Rect(0, 0, 1, 1), c, RectOffset(), matrix);
				}
				break;
			}
			default:
				break;
		}
		return result;
	}

	float GUI::HorizontalSlider(const Rect& position, float value, float leftValue, float rightValue)
	{
		return HorizontalSlider(position, value, leftValue, rightValue, *skin().horizontalSlider, *skin().horizontalSliderThumb);
	}

	float GUI::HorizontalSlider(const Rect& position, float value, float leftValue, float rightValue, const GUIStyle& slider, const GUIStyle& thumb)
	{
		int id = GUIUtility::GetControlID(FocusType::Passive);
		Event& e = Event::current();
		float thumbW = thumb.fixedWidth > 0 ? thumb.fixedWidth : 10;
		float travel = Mathf::Max(0.0f, position.width - thumbW);
		auto fromMouse = [&](float mx)
		{
			float t = travel > 0 ? Mathf::Clamp01((mx - position.x - thumbW * 0.5f) / travel) : 0;
			return leftValue + (rightValue - leftValue) * t;
		};
		switch (e.type)
		{
			case EventType::MouseDown:
				if (enabled && position.Contains(e.mousePosition) && e.button == 0)
				{
					GUIUtility::hotControl = id;
					value = fromMouse(e.mousePosition.x);
					changed = true;
					e.Use();
				}
				break;
			case EventType::MouseDrag:
				if (GUIUtility::hotControl == id)
				{
					value = fromMouse(e.mousePosition.x);
					changed = true;
					e.Use();
				}
				break;
			case EventType::MouseUp:
				if (GUIUtility::hotControl == id)
				{
					GUIUtility::hotControl = 0;
					e.Use();
				}
				break;
			case EventType::Repaint:
			{
				float trackH = slider.fixedHeight > 0 ? slider.fixedHeight : position.height;
				slider.Draw(Rect(position.x, position.y + (position.height - trackH) * 0.5f, position.width, trackH), GUIContent::none(), false, false, false, false);
				float range = rightValue - leftValue;
				float t = range != 0 ? Mathf::Clamp01((value - leftValue) / range) : 0;
				float thumbH = thumb.fixedHeight > 0 ? thumb.fixedHeight : position.height;
				Rect tr(position.x + travel * t, position.y + (position.height - thumbH) * 0.5f, thumbW, thumbH);
				thumb.Draw(tr, GUIContent::none(), tr.Contains(e.mousePosition) && enabled, GUIUtility::hotControl == id, false, false);
				break;
			}
			default:
				break;
		}
		return value;
	}

	void GUI::DrawTexture(const Rect& position, const cs::Ref<Texture2D>& image, ScaleMode scaleMode, bool alphaBlend, float imageAspect)
	{
		DrawTexture(position, static_cast<const Texture*>(image.get()), scaleMode, alphaBlend, imageAspect);
	}

	void GUI::DrawTexture(const Rect& position, const Texture* image, ScaleMode scaleMode, bool, float imageAspect)
	{
		if (!IsRepaint() || !image || !renderer) return;
		Rect r = position;
		Rect uv(0, 0, 1, 1);
		float aspect = imageAspect > 0 ? imageAspect : (image->height > 0 ? static_cast<float>(image->width) / image->height : 1.0f);
		float rectAspect = position.height > 0 ? position.width / position.height : 1.0f;
		if (scaleMode == ScaleMode::ScaleToFit)
		{
			if (rectAspect > aspect)
			{
				float w = position.height * aspect;
				r = Rect(position.x + (position.width - w) * 0.5f, position.y, w, position.height);
			}
			else
			{
				float h = aspect > 0 ? position.width / aspect : position.height;
				r = Rect(position.x, position.y + (position.height - h) * 0.5f, position.width, h);
			}
		}
		else if (scaleMode == ScaleMode::ScaleAndCrop)
		{
			if (rectAspect > aspect)
			{
				float v = aspect / rectAspect;
				uv = Rect(0, (1 - v) * 0.5f, 1, v);
			}
			else
			{
				float u = rectAspect / aspect;
				uv = Rect((1 - u) * 0.5f, 0, u, 1);
			}
		}
		renderer->DrawTexture(ToScreen(r), image, uv, DisabledFade(color), RectOffset(), matrix);
	}

	void GUI::DrawTextureWithTexCoords(const Rect& position, const cs::Ref<Texture2D>& image, const Rect& texCoords, bool)
	{
		if (!IsRepaint() || !image || !renderer) return;
		renderer->DrawTexture(ToScreen(position), image.get(), texCoords, DisabledFade(color), RectOffset(), matrix);
	}

	// ---------------------------------------------------------------- groups, clips, scroll views

	void GUI::BeginGroup(const Rect& position)
	{
		Rect screen = ToScreen(position);
		PushClipFrame(screen, Vector2(screen.x, screen.y));
	}

	void GUI::BeginGroup(const Rect& position, const GUIContent& content, const GUIStyle& style)
	{
		int id = GUIUtility::GetControlID(FocusType::Passive);
		(void)id;
		if (IsRepaint()) style.Draw(position, content, false, false, false, false);
		BeginGroup(position);
	}

	void GUI::EndGroup() { PopClipFrame(); }

	void GUI::BeginClip(const Rect& position) { BeginGroup(position); }

	void GUI::BeginClip(const Rect& position, Vector2 scrollOffset, Vector2 renderOffset, bool)
	{
		Rect screen = ToScreen(position);
		PushClipFrame(screen, Vector2(screen.x + scrollOffset.x + renderOffset.x, screen.y + scrollOffset.y + renderOffset.y));
	}

	void GUI::EndClip() { PopClipFrame(); }

	Vector2 GUI::BeginScrollView(const Rect& position, Vector2 scrollPosition, const Rect& viewRect)
	{
		return BeginScrollView(position, scrollPosition, viewRect, false, false, *skin().horizontalScrollbar, *skin().verticalScrollbar);
	}

	Vector2 GUI::BeginScrollView(const Rect& position, Vector2 scrollPosition, const Rect& viewRect, bool alwaysShowHorizontal, bool alwaysShowVertical)
	{
		return BeginScrollView(position, scrollPosition, viewRect, alwaysShowHorizontal, alwaysShowVertical,
			*skin().horizontalScrollbar, *skin().verticalScrollbar);
	}

	Vector2 GUI::BeginScrollView(const Rect& position, Vector2 scrollPosition, const Rect& viewRect,
		const GUIStyle& horizontalScrollbar, const GUIStyle& verticalScrollbar)
	{
		return BeginScrollView(position, scrollPosition, viewRect, false, false, horizontalScrollbar, verticalScrollbar);
	}

	Vector2 GUI::BeginScrollView(const Rect& position, Vector2 scrollPosition, const Rect& viewRect,
		bool alwaysShowHorizontal, bool alwaysShowVertical, const GUIStyle& horizontalScrollbar, const GUIStyle& verticalScrollbar)
	{
		int id = GUIUtility::GetControlID(FocusType::Passive);
		ScrollState& state = scrollStates[id];
		if (state.apply)
		{
			scrollPosition = state.scrollPosition;
			state.apply = false;
		}

		bool noneH = &horizontalScrollbar == &GUIStyle::none(), noneV = &verticalScrollbar == &GUIStyle::none();
		float vbarW = verticalScrollbar.fixedWidth > 0 ? verticalScrollbar.fixedWidth : ScrollbarSize;
		float hbarH = horizontalScrollbar.fixedHeight > 0 ? horizontalScrollbar.fixedHeight : ScrollbarSize;
		Rect clipRect = position;
		bool needV = alwaysShowVertical || viewRect.height > clipRect.height;
		if (needV) clipRect.width -= vbarW;
		bool needH = alwaysShowHorizontal || viewRect.width > clipRect.width;
		if (needH)
		{
			clipRect.height -= hbarH;
			// The horizontal bar can make the content overflow vertically too (Unity's second check).
			if (!needV && viewRect.height > clipRect.height)
			{
				needV = true;
				clipRect.width -= vbarW;
			}
		}
		float maxY = Mathf::Max(0.0f, viewRect.height - clipRect.height), maxX = Mathf::Max(0.0f, viewRect.width - clipRect.width);
		scrollPosition.y = Mathf::Clamp(scrollPosition.y, 0.0f, maxY);
		scrollPosition.x = Mathf::Clamp(scrollPosition.x, 0.0f, maxX);

		if (needV)
			scrollPosition.y = Scroller(Rect(clipRect.xMax(), position.y, vbarW, clipRect.height), scrollPosition.y,
				Mathf::Min(clipRect.height, viewRect.height), 0, viewRect.height, noneV ? *skin().verticalScrollbar : verticalScrollbar,
				*skin().verticalScrollbarThumb, false);
		if (needH)
			scrollPosition.x = Scroller(Rect(position.x, clipRect.yMax(), clipRect.width, hbarH), scrollPosition.x,
				Mathf::Min(clipRect.width, viewRect.width), 0, viewRect.width, noneH ? *skin().horizontalScrollbar : horizontalScrollbar,
				*skin().horizontalScrollbarThumb, true);

		ScrollFrame f;
		f.id = id;
		f.position = position;
		f.viewRect = viewRect;
		f.clipRect = clipRect;
		scrolls.push_back(f);
		state.scrollPosition = scrollPosition;

		Rect screen = ToScreen(clipRect);
		PushClipFrame(screen, Vector2(screen.x + Mathf::Round(-scrollPosition.x - viewRect.x), screen.y + Mathf::Round(-scrollPosition.y - viewRect.y)));
		return scrollPosition;
	}

	void GUI::EndScrollView() { EndScrollView(true); }

	void GUI::EndScrollView(bool handleScrollWheel)
	{
		if (scrolls.empty()) return;
		ScrollFrame f = scrolls.back();
		scrolls.pop_back();
		PopClipFrame();
		Event& e = Event::current();
		if (handleScrollWheel && e.type == EventType::ScrollWheel && f.position.Contains(e.mousePosition))
		{
			ScrollState& state = scrollStates[f.id];
			state.scrollPosition.x = Mathf::Clamp(state.scrollPosition.x + e.delta.x * 20.0f, 0.0f, Mathf::Max(0.0f, f.viewRect.width - f.clipRect.width));
			state.scrollPosition.y = Mathf::Clamp(state.scrollPosition.y + e.delta.y * 20.0f, 0.0f, Mathf::Max(0.0f, f.viewRect.height - f.clipRect.height));
			state.apply = true;
			e.Use();
		}
	}

	// ---------------------------------------------------------------- focus by name

	void GUI::SetNextControlName(const cs::String& name) { pendingControlName = name; }

	cs::String GUI::GetNameOfFocusedControl()
	{
		auto it = controlNames.find(GUIUtility::keyboardControl);
		return it == controlNames.end() ? cs::String() : it->second;
	}

	void GUI::FocusControl(const cs::String& name)
	{
		for (const auto& [id, n] : controlNames)
			if (n == name)
			{
				GUIUtility::keyboardControl = id;
				return;
			}
		pendingFocusName = name;
	}
}
