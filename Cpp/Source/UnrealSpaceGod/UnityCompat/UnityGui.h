#pragma once

// Unity's immediate-mode GUI (UnityEngine.GUI, GUIStyle, GUIContent, Event, GUIUtility) reproduced in C++, so the
// UI port (Runtime/UI: Hud, MainMenu, PauseMenu, HelpScreen, GalaxyMapScreen; ModelViewer) reads line for line like
// Unity.SpaceGod's C#: the same rects in pixels from the top-left, the same styles, the same event passes.
//
// How it runs (GuiHost.h): every frame the host replays Unity's event loop over each registered OnGUI handler,
// a Layout pass before every event, one pass per input event that arrived (MouseDown/Up/Drag/Move, ScrollWheel,
// KeyDown), then Layout + Repaint. Controls act on their events exactly as Unity's do (a Button fires on the MouseUp
// over it after the MouseDown on it, hot control and all); drawing happens only in Repaint and goes to an
// IGuiRenderer (Slate, see GuiHost.cpp). Text is measured by the renderer, so CalcSize/CalcHeight agree with what
// is drawn.
//
// Name rules (docs/MIGRATION.md): GUI's static properties that the C# assigns (GUI.color, GUI.enabled, GUI.matrix ...)
// are static fields (`GUI::color = c;`); GUIStyle is a class (a reference in C#), held as cs::Ref<GUIStyle>
// (`new GUIStyle(small) { wordWrap = true }` -> `auto s = cs::New<GUIStyle>(*small); s->wordWrap = true;`) and every
// GUI call takes either a `const GUIStyle&` or a `cs::Ref<GUIStyle>`; GUIStyle.none -> GUIStyle::none().
// GUIContent is a small value here (`new GUIContent(s)` -> `GUIContent(s)`); GUIContent.none -> GUIContent::none().
// Event.current -> Event::current() (a reference). GUIUtility.ExitGUI() throws ExitGUIException, which the host
// catches as Unity's does.
//
// Fonts: Unity's IMGUI font size is the em in pixels; the host draws Roboto (Unreal's Slate font) at the same em, so
// a fontSize 13 label is 13 px tall here too. Size 0 means the skin's default, 13 (Unity's built-in font in a player).

#include "UnityCompat/UnityMath.h"
#include "UnityCompat/UnityTexture.h"

#include <exception>
#include <functional>

namespace SpaceGod::UnityCompat
{
	enum class FontStyle : int { Normal = 0, Bold = 1, Italic = 2, BoldAndItalic = 3 };

	enum class TextAnchor : int
	{
		UpperLeft = 0, UpperCenter = 1, UpperRight = 2,
		MiddleLeft = 3, MiddleCenter = 4, MiddleRight = 5,
		LowerLeft = 6, LowerCenter = 7, LowerRight = 8,
	};

	enum class TextClipping : int { Overflow = 0, Clip = 1, Ellipsis = 2 };
	enum class ScaleMode : int { StretchToFill = 0, ScaleAndCrop = 1, ScaleToFit = 2 };
	enum class ImagePosition : int { ImageLeft = 0, ImageAbove = 1, ImageOnly = 2, TextOnly = 3 };

	/// UnityEngine.RectOffset: padding, margins and 9-slice borders.
	struct RectOffset
	{
		int left = 0, right = 0, top = 0, bottom = 0;
		constexpr RectOffset() = default;
		constexpr RectOffset(int left_, int right_, int top_, int bottom_) : left(left_), right(right_), top(top_), bottom(bottom_) {}
		int horizontal() const { return left + right; }
		int vertical() const { return top + bottom; }
		/// Grows a rect by this offset.
		Rect Add(const Rect& r) const { return Rect(r.x - left, r.y - top, r.width + left + right, r.height + top + bottom); }
		/// Shrinks a rect by this offset (a style's content rect: rect minus padding).
		Rect Remove(const Rect& r) const { return Rect(r.x + left, r.y + top, r.width - left - right, r.height - top - bottom); }
	};

	/// UnityEngine.GUIStyleState: a state's background and text colour.
	struct GUIStyleState
	{
		cs::Ref<Texture2D> background;
		/// new GUIStyle()'s text is black, as Unity's.
		Color textColor = Color(0, 0, 0, 1);
	};

	/// UnityEngine.GUIContent: the text (and optional image / tooltip) a control shows.
	struct GUIContent
	{
		cs::String text;
		cs::Ref<Texture> image;
		cs::String tooltip;

		GUIContent() = default;
		GUIContent(const cs::String& text_) : text(text_) {}
		GUIContent(const char* text_) : text(text_ ? text_ : "") {}
		GUIContent(const cs::String& text_, const cs::String& tooltip_) : text(text_), tooltip(tooltip_) {}
		GUIContent(const cs::Ref<Texture>& image_) : image(image_) {}
		GUIContent(const cs::String& text_, const cs::Ref<Texture>& image_) : text(text_), image(image_) {}
		static const GUIContent& none();
	};

	/// UnityEngine.GUIStyle: how a control looks (backgrounds per state, font, alignment, padding, wrapping).
	class GUIStyle
	{
	public:
		cs::String name;
		GUIStyleState normal, hover, active, focused, onNormal, onHover, onActive, onFocused;
		RectOffset border, margin, padding, overflow;
		int fontSize = 0;
		FontStyle fontStyle = FontStyle::Normal;
		TextAnchor alignment = TextAnchor::UpperLeft;
		bool wordWrap = false;
		bool richText = false;
		TextClipping clipping = TextClipping::Overflow;
		ImagePosition imagePosition = ImagePosition::ImageLeft;
		Vector2 contentOffset;
		float fixedWidth = 0, fixedHeight = 0;
		bool stretchWidth = true, stretchHeight = false;

		GUIStyle() = default;
		GUIStyle(const GUIStyle& other) = default;
		GUIStyle& operator=(const GUIStyle& other) = default;

		/// GUIStyle.none: draws nothing but the content (no background, no padding).
		static const GUIStyle& none();

		/// The size the content takes on one line (no wrapping), padding included.
		Vector2 CalcSize(const GUIContent& content) const;
		/// The height the content takes wrapped to `width` (if wordWrap), padding included.
		float CalcHeight(const GUIContent& content, float width) const;
		/// The font's line height in pixels at this style's size.
		float lineHeight() const;

		/// GUIStyle.Draw: background for the state, then the content (Repaint only).
		void Draw(const Rect& position, const GUIContent& content, bool isHover, bool isActive, bool on, bool hasKeyboardFocus) const;
		void Draw(const Rect& position, const GUIContent& content, int controlID, bool on = false) const;

		/// The size in pixels the style's font gives (0 = the skin's default).
		int EffectiveFontSize() const;
	};

	/// The default skin's styles (GUI.skin.box / label / button / textField ...), dark, as Unity's built-in skin.
	class GUISkin
	{
	public:
		cs::Ref<GUIStyle> box, label, button, toggle, textField, textArea, window;
		cs::Ref<GUIStyle> horizontalSlider, horizontalSliderThumb, verticalSlider, verticalSliderThumb;
		cs::Ref<GUIStyle> horizontalScrollbar, horizontalScrollbarThumb, verticalScrollbar, verticalScrollbarThumb;
		cs::Ref<GUIStyle> scrollView;
	};

	enum class EventType : int
	{
		MouseDown = 0, MouseUp = 1, MouseMove = 2, MouseDrag = 3, KeyDown = 4, KeyUp = 5, ScrollWheel = 6,
		Repaint = 7, Layout = 8, DragUpdated = 9, DragPerform = 10, DragExited = 15, Ignore = 11, Used = 12,
		ValidateCommand = 13, ExecuteCommand = 14, ContextClick = 16, MouseEnterWindow = 20, MouseLeaveWindow = 21,
	};

	/// UnityEngine.KeyCode (the values IMGUI's KeyDown events carry; Unity's own numbering).
	enum class KeyCode : int
	{
		None = 0, Backspace = 8, Tab = 9, Clear = 12, Return = 13, Pause = 19, Escape = 27, Space = 32,
		Exclaim = 33, DoubleQuote = 34, Hash = 35, Dollar = 36, Percent = 37, Ampersand = 38, Quote = 39,
		LeftParen = 40, RightParen = 41, Asterisk = 42, Plus = 43, Comma = 44, Minus = 45, Period = 46, Slash = 47,
		Alpha0 = 48, Alpha1, Alpha2, Alpha3, Alpha4, Alpha5, Alpha6, Alpha7, Alpha8, Alpha9,
		Colon = 58, Semicolon = 59, Less = 60, Equals = 61, Greater = 62, Question = 63, At = 64,
		LeftBracket = 91, Backslash = 92, RightBracket = 93, Caret = 94, Underscore = 95, BackQuote = 96,
		A = 97, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
		Delete = 127,
		Keypad0 = 256, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
		KeypadPeriod = 266, KeypadDivide = 267, KeypadMultiply = 268, KeypadMinus = 269, KeypadPlus = 270,
		KeypadEnter = 271, KeypadEquals = 272,
		UpArrow = 273, DownArrow = 274, RightArrow = 275, LeftArrow = 276, Insert = 277, Home = 278, End = 279,
		PageUp = 280, PageDown = 281,
		F1 = 282, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15,
		Numlock = 300, CapsLock = 301, ScrollLock = 302, RightShift = 303, LeftShift = 304, RightControl = 305,
		LeftControl = 306, RightAlt = 307, LeftAlt = 308,
		Mouse0 = 323, Mouse1, Mouse2, Mouse3, Mouse4, Mouse5, Mouse6,
	};

	enum class EventModifiers : int { None = 0, Shift = 1, Control = 2, Alt = 4, Command = 8, Numeric = 16, CapsLock = 32, FunctionKey = 64 };

	/// UnityEngine.Event: the event the current OnGUI pass handles. mousePosition is in the current group's
	/// coordinates (GUI.BeginGroup / BeginScrollView shift it as Unity's do).
	class Event
	{
	public:
		EventType type = EventType::Repaint;
		/// Unity's rawType: the type before Use() (a used event's type becomes Used).
		EventType rawType = EventType::Repaint;
		Vector2 mousePosition;
		Vector2 delta;
		int button = 0;
		int clickCount = 0;
		KeyCode keyCode = KeyCode::None;
		/// The typed character of a KeyDown (UTF-32), 0 for a key without one.
		char32_t character = 0;
		EventModifiers modifiers = EventModifiers::None;
		bool shift = false, control = false, alt = false, command = false;

		bool isMouse() const;
		bool isKey() const { return type == EventType::KeyDown || type == EventType::KeyUp; }
		bool isScrollWheel() const { return type == EventType::ScrollWheel; }

		/// Marks the event used: later controls (and OnGUI handlers) see type Used.
		void Use();

		/// Event.current.
		static Event& current();
	};

	/// GUIUtility.ExitGUI()'s exception: ends the current OnGUI pass at once (the host catches it).
	struct ExitGUIException final : std::exception
	{
		const char* what() const noexcept override { return "ExitGUI"; }
	};

	enum class FocusType : int { Native = 0, Keyboard = 1, Passive = 2 };

	/// A 2-D affine transform standing in for GUI.matrix (only rotation about a pivot is used: GUIUtility.RotateAroundPivot).
	struct GuiMatrix
	{
		// | m00 m01 m02 |   x
		// | m10 m11 m12 | * y
		float m00 = 1, m01 = 0, m02 = 0, m10 = 0, m11 = 1, m12 = 0;
		static GuiMatrix identity() { return GuiMatrix(); }
		bool isIdentity() const { return m00 == 1 && m01 == 0 && m02 == 0 && m10 == 0 && m11 == 1 && m12 == 0; }
		Vector2 MultiplyPoint(Vector2 p) const { return Vector2(m00 * p.x + m01 * p.y + m02, m10 * p.x + m11 * p.y + m12); }
		GuiMatrix operator*(const GuiMatrix& b) const;
		GuiMatrix inverse() const;
		static GuiMatrix Translate(Vector2 t);
		static GuiMatrix Rotate(float degrees);
	};

	/// UnityEngine.GUIUtility.
	struct GUIUtility
	{
		static inline int hotControl = 0;
		static inline int keyboardControl = 0;
		static int GetControlID(FocusType focus);
		static int GetControlID(int hint, FocusType focus);
		static int GetControlID(int hint, FocusType focus, const Rect& position);
		/// Throws ExitGUIException.
		[[noreturn]] static void ExitGUI();
		/// Rotates GUI.matrix by `angle` degrees about `pivotPoint` (GUI coordinates).
		static void RotateAroundPivot(float angle, Vector2 pivotPoint);
		static void ScaleAroundPivot(Vector2 scale, Vector2 pivotPoint);
		/// A point in the current group's coordinates -> screen pixels (top-left origin), and back.
		static Vector2 GUIToScreenPoint(Vector2 guiPoint);
		static Vector2 ScreenToGUIPoint(Vector2 screenPoint);
	};

	/// What GUI hands the host to draw (in screen pixels, top-left origin, before GUI.matrix).
	class IGuiRenderer
	{
	public:
		virtual ~IGuiRenderer() = default;
		/// One line's width and the font's line height, in pixels.
		virtual Vector2 MeasureLine(const cs::String& text, int fontSize, FontStyle style) = 0;
		virtual float LineHeight(int fontSize, FontStyle style) = 0;
		/// A textured quad, or a 9-slice one when `border` has any side (texture pixels kept unscaled at the edges).
		/// uv: the texture's sub-rect in 0..1, Unity's convention (y up from the bottom).
		virtual void DrawTexture(const Rect& screen, const Texture* texture, const Rect& uv, Color tint, const RectOffset& border,
			const GuiMatrix& matrix) = 0;
		/// One line of text whose top-left is `topLeft`.
		virtual void DrawText(Vector2 topLeft, const cs::String& text, int fontSize, FontStyle style, Color color, const GuiMatrix& matrix) = 0;
		/// Clip later drawing to a screen rect (nested clips intersect) until the matching PopClip.
		virtual void PushClip(const Rect& screen) = 0;
		virtual void PopClip() = 0;
	};

	/// UnityEngine.GUI.
	struct GUI
	{
		// ---- state (Unity resets it at the start of every OnGUI call)
		static inline Color color = Color::white();
		static inline Color backgroundColor = Color::white();
		static inline Color contentColor = Color::white();
		static inline bool enabled = true;
		static inline bool changed = false;
		static inline int depth = 0;
		static inline GuiMatrix matrix;
		static GUISkin& skin();

		// ---- controls
		static void Label(const Rect& position, const cs::String& text);
		static void Label(const Rect& position, const GUIContent& content);
		static void Label(const Rect& position, const cs::String& text, const GUIStyle& style);
		static void Label(const Rect& position, const GUIContent& content, const GUIStyle& style);
		static void Label(const Rect& position, const cs::String& text, const cs::Ref<GUIStyle>& style) { Label(position, text, *style); }
		static void Label(const Rect& position, const GUIContent& content, const cs::Ref<GUIStyle>& style) { Label(position, content, *style); }
		static void Label(const Rect& position, const char* text, const GUIStyle& style) { Label(position, cs::String(text), style); }
		static void Label(const Rect& position, const char* text, const cs::Ref<GUIStyle>& style) { Label(position, cs::String(text), *style); }

		static void Box(const Rect& position, const cs::String& text);
		static void Box(const Rect& position, const GUIContent& content, const GUIStyle& style);
		static void Box(const Rect& position, const cs::String& text, const GUIStyle& style) { Box(position, GUIContent(text), style); }
		static void Box(const Rect& position, const GUIContent& content, const cs::Ref<GUIStyle>& style) { Box(position, content, *style); }
		static void Box(const Rect& position, const cs::String& text, const cs::Ref<GUIStyle>& style) { Box(position, GUIContent(text), *style); }
		static void Box(const Rect& position, const char* text, const cs::Ref<GUIStyle>& style) { Box(position, GUIContent(text), *style); }

		static bool Button(const Rect& position, const cs::String& text);
		static bool Button(const Rect& position, const GUIContent& content);
		static bool Button(const Rect& position, const GUIContent& content, const GUIStyle& style);
		static bool Button(const Rect& position, const cs::String& text, const GUIStyle& style) { return Button(position, GUIContent(text), style); }
		static bool Button(const Rect& position, const char* text, const GUIStyle& style) { return Button(position, GUIContent(text), style); }
		static bool Button(const Rect& position, const GUIContent& content, const cs::Ref<GUIStyle>& style) { return Button(position, content, *style); }
		static bool Button(const Rect& position, const cs::String& text, const cs::Ref<GUIStyle>& style) { return Button(position, GUIContent(text), *style); }
		static bool Button(const Rect& position, const char* text, const cs::Ref<GUIStyle>& style) { return Button(position, GUIContent(text), *style); }
		static bool Button(const Rect& position, const cs::Ref<Texture>& image, const cs::Ref<GUIStyle>& style) { return Button(position, GUIContent(image), *style); }

		static bool Toggle(const Rect& position, bool value, const cs::String& text);
		static bool Toggle(const Rect& position, bool value, const GUIContent& content, const GUIStyle& style);

		static cs::String TextField(const Rect& position, const cs::String& text);
		static cs::String TextField(const Rect& position, const cs::String& text, int maxLength);
		static cs::String TextField(const Rect& position, const cs::String& text, const GUIStyle& style);
		static cs::String TextField(const Rect& position, const cs::String& text, int maxLength, const GUIStyle& style);
		static cs::String TextField(const Rect& position, const cs::String& text, int maxLength, const cs::Ref<GUIStyle>& style) { return TextField(position, text, maxLength, *style); }
		static cs::String TextField(const Rect& position, const cs::String& text, const cs::Ref<GUIStyle>& style) { return TextField(position, text, -1, *style); }

		static float HorizontalSlider(const Rect& position, float value, float leftValue, float rightValue);
		static float HorizontalSlider(const Rect& position, float value, float leftValue, float rightValue, const GUIStyle& slider, const GUIStyle& thumb);

		static void DrawTexture(const Rect& position, const cs::Ref<Texture2D>& image, ScaleMode scaleMode = ScaleMode::StretchToFill,
			bool alphaBlend = true, float imageAspect = 0);
		static void DrawTexture(const Rect& position, const cs::Ref<Texture>& image, ScaleMode scaleMode = ScaleMode::StretchToFill,
			bool alphaBlend = true, float imageAspect = 0) { DrawTexture(position, image.get(), scaleMode, alphaBlend, imageAspect); }
		static void DrawTexture(const Rect& position, const Texture* image, ScaleMode scaleMode = ScaleMode::StretchToFill,
			bool alphaBlend = true, float imageAspect = 0);
		static void DrawTextureWithTexCoords(const Rect& position, const cs::Ref<Texture2D>& image, const Rect& texCoords, bool alphaBlend = true);

		// ---- groups, clips and scroll views
		static void BeginGroup(const Rect& position);
		static void BeginGroup(const Rect& position, const GUIContent& content, const GUIStyle& style);
		static void EndGroup();
		static void BeginClip(const Rect& position);
		static void BeginClip(const Rect& position, Vector2 scrollOffset, Vector2 renderOffset, bool resetOffset);
		static void EndClip();
		static Vector2 BeginScrollView(const Rect& position, Vector2 scrollPosition, const Rect& viewRect);
		static Vector2 BeginScrollView(const Rect& position, Vector2 scrollPosition, const Rect& viewRect,
			bool alwaysShowHorizontal, bool alwaysShowVertical);
		static Vector2 BeginScrollView(const Rect& position, Vector2 scrollPosition, const Rect& viewRect,
			const GUIStyle& horizontalScrollbar, const GUIStyle& verticalScrollbar);
		static Vector2 BeginScrollView(const Rect& position, Vector2 scrollPosition, const Rect& viewRect,
			bool alwaysShowHorizontal, bool alwaysShowVertical, const GUIStyle& horizontalScrollbar, const GUIStyle& verticalScrollbar);
		static void EndScrollView();
		static void EndScrollView(bool handleScrollWheel);

		// ---- keyboard focus by name
		static void SetNextControlName(const cs::String& name);
		static cs::String GetNameOfFocusedControl();
		static void FocusControl(const cs::String& name);

		// ---------------------------------------------------------------- host side (GuiHost)

		/// The renderer for Repaint passes (and the text metrics for every pass).
		static void SetRenderer(IGuiRenderer* renderer);
		static IGuiRenderer* Renderer();
		/// Runs one OnGUI handler for `e` (the host's event loop calls it per handler per pass): resets the GUI state
		/// as Unity's BeginGUI does, runs the handler, catches ExitGUIException, closes groups left open.
		/// `handlerIndex` keeps control ids distinct between handlers.
		static void RunOnGUI(const std::function<void()>& onGui, const Event& e, int handlerIndex);
		/// Called by the host at the start of every frame (before its passes): drops the per-frame name map etc.
		static void BeginFrame();
	};
}

namespace SpaceGod
{
	// As UnityTypes.h does for the rest of UnityEngine: the GUI types unqualified under SpaceGod.
	using UnityCompat::FontStyle;
	using UnityCompat::TextAnchor;
	using UnityCompat::TextClipping;
	using UnityCompat::ScaleMode;
	using UnityCompat::ImagePosition;
	using UnityCompat::RectOffset;
	using UnityCompat::GUIStyleState;
	using UnityCompat::GUIContent;
	using UnityCompat::GUIStyle;
	using UnityCompat::GUISkin;
	using UnityCompat::EventType;
	using UnityCompat::KeyCode;
	using UnityCompat::EventModifiers;
	using UnityCompat::Event;
	using UnityCompat::ExitGUIException;
	using UnityCompat::FocusType;
	using UnityCompat::GuiMatrix;
	using UnityCompat::GUIUtility;
	using UnityCompat::GUI;
	using UnityCompat::Color32;
	using UnityCompat::Texture;
	using UnityCompat::TextureFormat;
	using UnityCompat::FilterMode;
	using UnityCompat::TextureWrapMode;
	using UnityCompat::HideFlags;
}
