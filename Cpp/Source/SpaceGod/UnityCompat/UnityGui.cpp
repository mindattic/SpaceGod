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
			c.a *= GUI::fade; // US-T255: a panel fading in
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

		// Unity's runtime default skin (GUI.skin), exactly: GENERATED by tools/unity_extract.py from the original
		// player's GameSkin (every style's states, border, margin, padding, overflow, alignment, clipping, fixed sizes,
		// and its textures pixel for pixel), the C# track's BuiltinSkin.Make (US-T57).
#include "UnityCompat/UnityBuiltinSkin.inl"

		GUISkin MakeSkin() { return BuiltinSkin(); }

		/// A scrollbar (Unity's GUI.Scroller): value in [left, right - size], dragged by its thumb, paged by the track.
		float Scroller(const Rect& position, float value, float size, float left, float right, const GUIStyle& slider,
			const GUIStyle& thumb, bool horiz)
		{
			int id = GUIUtility::GetControlID(FocusType::Passive);
			float range = right - left;
			float maxValue = right - size;
			if (maxValue < left) maxValue = left;
			value = Mathf::Clamp(value, left, maxValue);
			// Unity's SliderHandler (US-T57): inside the track's padding, the thumb is ThumbSize (the thumb's fixed
			// length, else its padding along the bar) plus size at the track's pixels per value; the styles' overflows
			// shape what is drawn.
			float length = horiz ? position.width : position.height;
			float pad = horiz ? static_cast<float>(slider.padding.left + slider.padding.right) : static_cast<float>(slider.padding.top + slider.padding.bottom);
			float padStart = horiz ? static_cast<float>(slider.padding.left) : static_cast<float>(slider.padding.top);
			float thumbSize = horiz ? (thumb.fixedWidth != 0 ? thumb.fixedWidth : static_cast<float>(thumb.padding.left + thumb.padding.right))
				: (thumb.fixedHeight != 0 ? thumb.fixedHeight : static_cast<float>(thumb.padding.top + thumb.padding.bottom));
			float perValue = range > 0 ? (length - pad - thumbSize) / range : 0;
			float thumbLen = Mathf::Min(size, Mathf::Max(range, 0.0f)) * perValue + thumbSize;
			auto thumbAt = [&](float v)
			{
				float at = padStart + (v - left) * perValue;
				return horiz
					? Rect(position.x + at, position.y + slider.padding.top, thumbLen, position.height - slider.padding.top - slider.padding.bottom)
					: Rect(position.x + slider.padding.left, position.y + at, position.width - slider.padding.left - slider.padding.right, thumbLen);
			};
			Rect thumbRect = thumbAt(value);
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
						float p = along - dragOffset - start - padStart;
						value = perValue > 0 ? Mathf::Clamp(left + p / perValue, left, maxValue) : left;
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
					thumbRect = thumbAt(Mathf::Clamp(value, left, maxValue));
					thumb.Draw(thumbRect, GUIContent::none(), thumbRect.Contains(m), GUIUtility::hotControl == id, false, false);
					break;
				}
				default:
					break;
			}
			return value;
		}

		// ------------------------------------------------------------ TextEditor (US-T52)

		/// The UTF-8 text as C#'s string sees it: UTF-16 code units, so cursor and selection indices are C#'s.
		std::u16string ToUtf16(const cs::String& s)
		{
			std::u16string out;
			out.reserve(s.size());
			cs::ForEachUtf16(s, [&](uint32_t u) { out.push_back(static_cast<char16_t>(u)); });
			return out;
		}

		cs::String ToUtf8(const std::u16string& s, size_t from = 0, size_t to = std::u16string::npos)
		{
			cs::String out;
			if (to > s.size()) to = s.size();
			for (size_t i = from; i < to; i++)
			{
				uint32_t c = s[i];
				if (c >= 0xD800 && c < 0xDC00 && i + 1 < to && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
				{
					c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<uint32_t>(s[i + 1]) - 0xDC00);
					i++;
				}
				cs::Char::Append(out, c);
			}
			return out;
		}

		std::u16string ToUtf16(char32_t c)
		{
			std::u16string out;
			if (c >= 0x10000)
			{
				uint32_t v = static_cast<uint32_t>(c) - 0x10000;
				out.push_back(static_cast<char16_t>(0xD800 + (v >> 10)));
				out.push_back(static_cast<char16_t>(0xDC00 + (v & 0x3FF)));
			}
			else out.push_back(static_cast<char16_t>(c));
			return out;
		}

		/// Unity's TextEditor selection colour (the default skin's settings.selectionColor), as the C# track's.
		const Color SelectionColor(61 / 255.0f, 128 / 255.0f, 223 / 255.0f, 166 / 255.0f);

		/// Unity's TextEditor for the single-line GUI.TextField, the C# track's GUI.TextEditing (US-T40) line for line:
		/// a cursor and a selection anchor (select) per focused control, a scroll offset keeping the cursor in view, and
		/// its Windows key map: Left/Right (Ctrl: by word), Home/End (Up/Down in one line likewise), Shift extends the
		/// selection, Backspace/Delete (Ctrl: a word), Ctrl+A/C/X/V; typing replaces the selection; a field gaining
		/// focus selects all. Indices are UTF-16 units (C#'s string indices).
		struct TextEditing
		{
			int Id = -1, Cursor = 0, Select = 0;
			float Scroll = 0;
			bool DragWords = false, Changed = false;

			bool HasSelection() const { return Cursor != Select; }
			int Min() const { return Cursor < Select ? Cursor : Select; }
			int Max() const { return Cursor > Select ? Cursor : Select; }

			void SelectAll(const std::u16string& t) { Select = 0; Cursor = static_cast<int>(t.size()); }

			/// char.IsLetterOrDigit(c) || c == '_'.
			static bool IsWord(char16_t c) { return (c >= u'0' && c <= u'9') || c == u'_' || cs::Char::IsLetter(c); }

			static int WordStart(const std::u16string& t, int i)
			{
				while (i > 0 && !IsWord(t[i - 1])) i--;
				while (i > 0 && IsWord(t[i - 1])) i--;
				return i;
			}

			static int WordEnd(const std::u16string& t, int i)
			{
				int n = static_cast<int>(t.size());
				while (i < n && !IsWord(t[i])) i++;
				while (i < n && IsWord(t[i])) i++;
				return i;
			}

			void SelectWord(const std::u16string& t, int at)
			{
				int n = static_cast<int>(t.size());
				int s = at, e = at;
				if ((at < n && IsWord(t[at])) || (at > 0 && IsWord(t[at - 1])))
				{
					while (s > 0 && IsWord(t[s - 1])) s--;
					while (e < n && IsWord(t[e])) e++;
				}
				else if (at < n) e = at + 1;
				Select = s;
				Cursor = e;
			}

			void ExtendToWord(const std::u16string& t, int at)
			{
				int anchor = Select;
				int n = static_cast<int>(t.size());
				if (at >= anchor) Cursor = at < n && IsWord(t[at - 1 > 0 ? at - 1 : 0]) ? WordEnd(t, at) : at;
				else Cursor = WordStart(t, at);
			}

			void Delete(std::u16string& t, int from, int to)
			{
				Changed = true;
				Cursor = Select = from;
				t.erase(static_cast<size_t>(from), static_cast<size_t>(to - from));
			}

			void Insert(std::u16string& t, std::u16string s, int maxLength)
			{
				if (HasSelection()) Delete(t, Min(), Max());
				if (maxLength >= 0 && static_cast<int>(t.size() + s.size()) > maxLength)
					s = s.substr(0, static_cast<size_t>(maxLength - static_cast<int>(t.size()) > 0 ? maxLength - static_cast<int>(t.size()) : 0));
				if (s.empty()) return;
				Changed = true;
				t.insert(static_cast<size_t>(Cursor), s);
				Cursor = Select = Cursor + static_cast<int>(s.size());
			}

			/// Whether the key was the field's (the event is then used).
			bool HandleKey(const Event& e, std::u16string& text, int maxLength)
			{
				Changed = false;
				bool shift = e.shift, ctrl = e.control || e.command;
				int n = static_cast<int>(text.size());
				auto move = [&](int to) { Cursor = to; if (!shift) Select = to; };
				switch (e.keyCode)
				{
					case KeyCode::LeftArrow:
						if (!shift && !ctrl && HasSelection()) move(Min());
						else move(ctrl ? WordStart(text, Cursor) : (Cursor - 1 > 0 ? Cursor - 1 : 0));
						return true;
					case KeyCode::RightArrow:
						if (!shift && !ctrl && HasSelection()) move(Max());
						else move(ctrl ? WordEnd(text, Cursor) : (Cursor + 1 < n ? Cursor + 1 : n));
						return true;
					case KeyCode::Home:
					case KeyCode::UpArrow:
						move(0);
						return true;
					case KeyCode::End:
					case KeyCode::DownArrow:
						move(n);
						return true;
					case KeyCode::Backspace:
						if (HasSelection()) Delete(text, Min(), Max());
						else if (Cursor > 0) Delete(text, ctrl ? WordStart(text, Cursor) : Cursor - 1, Cursor);
						return true;
					case KeyCode::Delete:
						if (HasSelection()) Delete(text, Min(), Max());
						else if (Cursor < n) Delete(text, Cursor, ctrl ? WordEnd(text, Cursor) : Cursor + 1);
						return true;
					default:
						break;
				}
				if (ctrl)
				{
					switch (e.keyCode)
					{
						case KeyCode::A: SelectAll(text); return true;
						case KeyCode::C:
							if (HasSelection()) GUIUtility::SetSystemCopyBuffer(ToUtf8(text, static_cast<size_t>(Min()), static_cast<size_t>(Max())));
							return true;
						case KeyCode::X:
							if (HasSelection())
							{
								GUIUtility::SetSystemCopyBuffer(ToUtf8(text, static_cast<size_t>(Min()), static_cast<size_t>(Max())));
								Delete(text, Min(), Max());
							}
							return true;
						case KeyCode::V:
						{
							std::u16string paste;
							for (char16_t c : ToUtf16(GUIUtility::systemCopyBuffer()))
								if (c != u'\r' && c != u'\n') paste.push_back(c);
							Insert(text, paste, maxLength);
							return true;
						}
						default:
							return false;
					}
				}
				// Single line: Return and Tab are not typed (the game sees them).
				if (e.character >= U' ' && e.character != 0x7F)
				{
					Insert(text, ToUtf16(e.character), maxLength);
					return true;
				}
				return false;
			}
		};

		TextEditing currentEditor;
		TextEditing scratchEditor;

		/// The focused field's editor (made, selecting all, when a field first has the focus), or a scratch one.
		TextEditing& EditorFor(int id, const std::u16string& text, bool focused)
		{
			if (currentEditor.Id != id)
			{
				if (!focused)
				{
					scratchEditor = TextEditing();
					return scratchEditor;
				}
				currentEditor = TextEditing();
				currentEditor.Id = id;
				currentEditor.SelectAll(text); // TextEditor.OnFocus (single line): select all
			}
			int n = static_cast<int>(text.size());
			currentEditor.Cursor = currentEditor.Cursor < 0 ? 0 : currentEditor.Cursor > n ? n : currentEditor.Cursor;
			currentEditor.Select = currentEditor.Select < 0 ? 0 : currentEditor.Select > n ? n : currentEditor.Select;
			return currentEditor;
		}

		/// Where a single-line field's text starts (x, y) inside `position`, before scrolling (the C# FieldOrigin).
		Vector2 FieldOrigin(const GUIStyle& style, const Rect& position)
		{
			Rect area = style.padding.Remove(position);
			area.x += style.contentOffset.x;
			area.y += style.contentOffset.y;
			float lh = style.lineHeight();
			int v = static_cast<int>(style.alignment) / 3;
			float y = v == 1 ? area.y + (area.height - lh) * 0.5f : v == 2 ? area.yMax() - lh : area.y;
			return Vector2(area.x, Mathf::Round(y));
		}

		/// The character boundary nearest `mouseX` (local) in a single-line field.
		int TextFieldIndexAt(const GUIStyle& style, const Rect& position, const std::u16string& text, float scroll, float mouseX)
		{
			float x = mouseX - (FieldOrigin(style, position).x - scroll);
			int best = 0;
			float prev = 0;
			int size = style.EffectiveFontSize();
			for (size_t i = 1; i <= text.size(); i++)
			{
				float w = MeasureWidth(ToUtf8(text, 0, i), size, style.fontStyle);
				if (x < (prev + w) * 0.5f) return static_cast<int>(i) - 1;
				prev = w;
				best = static_cast<int>(i);
			}
			return best;
		}

		/// A focused single-line field as Unity's TextEditor draws it: the text scrolled so the cursor stays in view and
		/// clipped to the padded area, the selection highlighted behind it, the cursor a 1-px line (blinking).
		void DrawTextFieldContent(const GUIStyle& style, const Rect& position, const std::u16string& text, Color textColor, TextEditing& ed)
		{
			if (!renderer) return;
			Rect area = style.padding.Remove(position);
			Vector2 origin = FieldOrigin(style, position);
			float lh = style.lineHeight();
			int size = style.EffectiveFontSize();
			float cursorX = MeasureWidth(ToUtf8(text, 0, static_cast<size_t>(ed.Cursor)), size, style.fontStyle);
			// TextEditor.UpdateScrollOffset: keep the cursor inside the visible width.
			float view = Mathf::Max(1.0f, area.xMax() - origin.x);
			if (cursorX - ed.Scroll > view - 1) ed.Scroll = cursorX - view + 1;
			if (cursorX - ed.Scroll < 0) ed.Scroll = cursorX;
			cs::String full = ToUtf8(text);
			float fullW = MeasureWidth(full, size, style.fontStyle);
			if (fullW - ed.Scroll < view - 1) ed.Scroll = Mathf::Max(0.0f, fullW - view + 1);
			float x0 = Mathf::Round(origin.x - ed.Scroll);
			GUI::BeginClip(area);
			Vector2 local(x0 - area.x, origin.y - area.y);
			if (ed.HasSelection())
			{
				float sx = MeasureWidth(ToUtf8(text, 0, static_cast<size_t>(ed.Min())), size, style.fontStyle);
				float ex = MeasureWidth(ToUtf8(text, 0, static_cast<size_t>(ed.Max())), size, style.fontStyle);
				renderer->DrawTexture(ToScreen(Rect(local.x + sx, local.y, ex - sx, lh)), Texture2D::whiteTexture().get(), Rect(0, 0, 1, 1),
					SelectionColor, RectOffset(), GUI::matrix);
			}
			Vector2 o = Origin();
			if (!full.empty())
				renderer->DrawText(Vector2(Mathf::Floor(local.x + o.x + 0.5f), Mathf::Floor(local.y + o.y + 0.5f)), full, size, style.fontStyle, textColor, GUI::matrix);
			// GUI.DrawCaret: settings.cursorColor (white), on for half of each second.
			if ((static_cast<int>(Time::unscaledTime * 2.0f) & 1) == 0)
				renderer->DrawTexture(ToScreen(Rect(local.x + cursorX, local.y + 1, 1, lh - 2)), Texture2D::whiteTexture().get(), Rect(0, 0, 1, 1),
					Color(1, 1, 1, GUI::color.a), RectOffset(), GUI::matrix);
			GUI::EndClip();
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

	namespace
	{
		/// US-T255: how lit each button or toggle is (0 resting .. 1 hovered), by control id, and when it was last drawn.
		struct HoverGlow
		{
			float amount = 0;
			float at = -1;
		};
		std::unordered_map<int, HoverGlow> hoverGlows;
		float hoverPrunedAt = 0;

		float HoverAmount(int id, bool hovered)
		{
			float now = Time::unscaledTime;
			HoverGlow& g = hoverGlows[id];
			// Not drawn lately (a new control, or one back on screen): it starts at rest.
			bool fresh = g.at < 0 || now - g.at > 0.25f;
			if (fresh) g.amount = 0;
			float dt = fresh ? 0.0f : Mathf::Max(0.0f, now - g.at);
			g.at = now;
			float step = dt / Mathf::Max(0.001f, GUI::hoverSeconds);
			g.amount = hovered ? Mathf::Min(1.0f, g.amount + step) : Mathf::Max(0.0f, g.amount - step);
			if (now - hoverPrunedAt > 5)
			{
				hoverPrunedAt = now;
				for (auto it = hoverGlows.begin(); it != hoverGlows.end();)
					it = now - it->second.at > 1 ? hoverGlows.erase(it) : std::next(it);
			}
			return g.amount;
		}

		/// A button's or toggle's Repaint: Unity's look at rest and fully hovered, but with GUI::hoverSeconds set the hover
		/// background eases in over the resting one as the pointer comes on (and out as it leaves) instead of snapping.
		void DrawControl(const GUIStyle& style, const Rect& position, const GUIContent& content, int id, bool on)
		{
			if (GUI::hoverSeconds <= 0 || content.image || !renderer)
			{
				style.Draw(position, content, id, on);
				return;
			}
			Vector2 m = Event::current().mousePosition;
			bool isActive = GUIUtility::hotControl == id;
			bool isHover = GUI::enabled && position.Contains(m) && (GUIUtility::hotControl == 0 || isActive);
			bool focus = GUIUtility::keyboardControl == id;
			float lit = HoverAmount(id, isHover && !isActive);
			const GUIStyleState& rest = ChooseState(style, false, false, on, focus);
			const GUIStyleState& hover = ChooseState(style, true, false, on, focus);
			if (isActive || lit <= 0 || lit >= 1 || rest.background.get() == hover.background.get())
			{
				style.Draw(position, content, isHover, isActive, on, focus);
				return;
			}
			Rect back = style.overflow.Add(position);
			DrawBackground(back, rest.background, style.border);
			float keep = GUI::fade;
			GUI::fade = keep * lit * lit * (3 - 2 * lit); // smoothstep
			DrawBackground(back, hover.background, style.border);
			GUI::fade = keep;
			Rect contentRect = style.padding.Remove(position);
			contentRect.x += style.contentOffset.x;
			contentRect.y += style.contentOffset.y;
			const GUIStyleState& text = lit >= 0.5f ? hover : rest;
			DrawTextBlock(style, contentRect, content.text, DisabledFade(Tint(Tint(text.textColor, GUI::color), GUI::contentColor)));
		}
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
		fade = 1;
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
				DrawControl(style, position, content, id, false);
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
				DrawControl(style, position, content, id, value);
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
		// US-T52: Unity's TextEditor, as the C# track's GUI.TextField (US-T40): the cursor and the selection by mouse
		// (click, Shift+click, drag, double click a word, triple click all) and keyboard (TextEditing::HandleKey).
		int id = GUIUtility::GetControlID(FocusType::Keyboard);
		Event& e = Event::current();
		std::u16string edited = ToUtf16(text);
		bool focused = GUIUtility::keyboardControl == id;
		TextEditing& ed = EditorFor(id, edited, focused);
		switch (e.type)
		{
			case EventType::MouseDown:
				if (enabled && position.Contains(e.mousePosition))
				{
					GUIUtility::keyboardControl = id;
					GUIUtility::hotControl = id;
					// Unity's TextEditor: the click puts the cursor at the nearest character boundary; a double click
					// selects the word, a triple click the whole line; shift-click extends the selection. A field not
					// focused before the click gets its editor on the next pass, which selects all (OnFocus), as the
					// C# track's: the first click into a field selects its text.
					int at = TextFieldIndexAt(style, position, edited, ed.Scroll, e.mousePosition.x);
					if (e.clickCount == 2) ed.SelectWord(edited, at);
					else if (e.clickCount >= 3) ed.SelectAll(edited);
					else if (e.shift) ed.Cursor = at;
					else ed.Cursor = ed.Select = at;
					ed.DragWords = e.clickCount == 2;
					e.Use();
				}
				else if (focused) GUIUtility::keyboardControl = 0; // a click elsewhere takes the focus away
				break;
			case EventType::MouseDrag:
				if (GUIUtility::hotControl == id)
				{
					int at = TextFieldIndexAt(style, position, edited, ed.Scroll, e.mousePosition.x);
					if (ed.DragWords) ed.ExtendToWord(edited, at);
					else ed.Cursor = at;
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
			case EventType::KeyDown:
				if (!focused || !enabled) break;
				if (ed.HandleKey(e, edited, maxLength))
				{
					e.Use();
					if (ed.Changed)
					{
						changed = true;
						return ToUtf8(edited);
					}
				}
				break;
			case EventType::Repaint:
				if (focused)
				{
					// The style's focused background, then the text as the editor draws it.
					style.Draw(position, GUIContent::none(), id, false);
					Vector2 m = e.mousePosition;
					bool active = GUIUtility::hotControl == id;
					bool hover = enabled && position.Contains(m) && (GUIUtility::hotControl == 0 || active);
					const GUIStyleState& state = ChooseState(style, hover, active, false, true);
					Color c = DisabledFade(Tint(Tint(state.textColor, color), contentColor));
					DrawTextFieldContent(style, position, edited, c, ed);
				}
				else style.Draw(position, GUIContent(text), id, false);
				break;
			default:
				break;
		}
		return text;
	}

	GUI::TextEditorState GUI::FocusedTextEditor()
	{
		TextEditorState s;
		if (currentEditor.Id > 0 && currentEditor.Id == GUIUtility::keyboardControl)
		{
			s.id = currentEditor.Id;
			s.cursor = currentEditor.Cursor;
			s.select = currentEditor.Select;
		}
		return s;
	}

	float GUI::HorizontalSlider(const Rect& position, float value, float leftValue, float rightValue)
	{
		return HorizontalSlider(position, value, leftValue, rightValue, *skin().horizontalSlider, *skin().horizontalSliderThumb);
	}

	float GUI::HorizontalSlider(const Rect& position, float value, float leftValue, float rightValue, const GUIStyle& slider, const GUIStyle& thumb)
	{
		int id = GUIUtility::GetControlID(FocusType::Passive);
		Event& e = Event::current();
		// Unity's SliderHandler (US-T57): the thumb is ThumbSize wide (the thumb's fixed width, else its horizontal
		// padding) inside the track's padding, at full height less that padding; the styles' overflows shape both.
		float thumbW = thumb.fixedWidth != 0 ? thumb.fixedWidth : static_cast<float>(thumb.padding.left + thumb.padding.right);
		float travel = Mathf::Max(0.0f, position.width - (slider.padding.left + slider.padding.right) - thumbW);
		auto fromMouse = [&](float mx)
		{
			float t = travel > 0 ? Mathf::Clamp01((mx - position.x - slider.padding.left - thumbW * 0.5f) / travel) : 0;
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
				slider.Draw(position, GUIContent::none(), false, false, false, false);
				float range = rightValue - leftValue;
				float t = range != 0 ? Mathf::Clamp01((value - leftValue) / range) : 0;
				Rect tr(position.x + slider.padding.left + travel * t, position.y + slider.padding.top, thumbW,
					position.height - slider.padding.top - slider.padding.bottom);
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
