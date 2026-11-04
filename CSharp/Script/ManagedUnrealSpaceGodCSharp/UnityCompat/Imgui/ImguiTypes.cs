using System;
using System.Collections.Generic;

// Unity's IMGUI value types (GUIStyle, GUIStyleState, GUIContent, RectOffset, the enums), with Unity's defaults, so
// the original's UI code (Hud, menus, screens) ports line for line. GUIStyle does its own text layout (alignment,
// word wrap, clipping, padding) on top of the backend's measure, as Unity's native GUIStyle does; the backend
// (IGuiCanvas) only fills rectangles, draws textures and draws single lines of text.
namespace UnityEngine
{
    public enum TextAnchor
    {
        UpperLeft,
        UpperCenter,
        UpperRight,
        MiddleLeft,
        MiddleCenter,
        MiddleRight,
        LowerLeft,
        LowerCenter,
        LowerRight,
    }

    public enum FontStyle
    {
        Normal,
        Bold,
        Italic,
        BoldAndItalic,
    }

    public enum TextClipping
    {
        Overflow,
        Clip,
        Ellipsis,
    }

    public enum ImagePosition
    {
        ImageLeft,
        ImageAbove,
        ImageOnly,
        TextOnly,
    }

    public enum ScaleMode
    {
        StretchToFill,
        ScaleAndCrop,
        ScaleToFit,
    }

    public enum FocusType
    {
        Keyboard = 1,
        Passive = 2,
    }

    /// <summary>Unity's font handle. The UI only ever uses the default font (Arial, as Unity's built-in one), by size and style.</summary>
    public sealed class Font
    {
        public string name = "Arial";
        public int fontSize;
    }

    public sealed class RectOffset
    {
        public int left, right, top, bottom;

        public RectOffset() { }

        public RectOffset(int left, int right, int top, int bottom)
        {
            this.left = left;
            this.right = right;
            this.top = top;
            this.bottom = bottom;
        }

        public int horizontal => left + right;
        public int vertical => top + bottom;

        public Rect Add(Rect r) => new Rect(r.x - left, r.y - top, r.width + horizontal, r.height + vertical);
        public Rect Remove(Rect r) => new Rect(r.x + left, r.y + top, r.width - horizontal, r.height - vertical);

        internal RectOffset Clone() => new RectOffset(left, right, top, bottom);
    }

    public sealed class GUIStyleState
    {
        public Texture2D background;
        /// <summary>Unity's default for a new state: black.</summary>
        public Color textColor = Color.black;

        internal GUIStyleState Clone() => new GUIStyleState { background = background, textColor = textColor };
    }

    public sealed class GUIContent
    {
        public static readonly GUIContent none = new GUIContent("");

        public string text;
        public Texture image;
        public string tooltip;

        public GUIContent() => text = "";
        public GUIContent(string text) => this.text = text ?? "";
        public GUIContent(string text, string tooltip) : this(text) => this.tooltip = tooltip;
        public GUIContent(Texture image) : this("") => this.image = image;
        public GUIContent(string text, Texture image) : this(text) => this.image = image;

        internal static GUIContent Temp(string s) => new GUIContent(s);
    }

    public sealed class GUIStyle
    {
        public static readonly GUIStyle none = new GUIStyle();

        public string name = "";
        public GUIStyleState normal = new GUIStyleState();
        public GUIStyleState hover = new GUIStyleState();
        public GUIStyleState active = new GUIStyleState();
        public GUIStyleState focused = new GUIStyleState();
        public GUIStyleState onNormal = new GUIStyleState();
        public GUIStyleState onHover = new GUIStyleState();
        public GUIStyleState onActive = new GUIStyleState();
        public GUIStyleState onFocused = new GUIStyleState();
        public RectOffset border = new RectOffset();
        public RectOffset padding = new RectOffset();
        public RectOffset margin = new RectOffset();
        public RectOffset overflow = new RectOffset();
        public Font font;
        public int fontSize;
        public FontStyle fontStyle;
        public TextAnchor alignment;
        public bool wordWrap;
        public bool richText;
        public TextClipping clipping;
        public ImagePosition imagePosition;
        public Vector2 contentOffset;
        public float fixedWidth, fixedHeight;
        public bool stretchWidth = true, stretchHeight;

        public GUIStyle() { }

        public GUIStyle(GUIStyle other)
        {
            if (other == null) return;
            name = other.name;
            normal = other.normal.Clone();
            hover = other.hover.Clone();
            active = other.active.Clone();
            focused = other.focused.Clone();
            onNormal = other.onNormal.Clone();
            onHover = other.onHover.Clone();
            onActive = other.onActive.Clone();
            onFocused = other.onFocused.Clone();
            border = other.border.Clone();
            padding = other.padding.Clone();
            margin = other.margin.Clone();
            overflow = other.overflow.Clone();
            font = other.font;
            fontSize = other.fontSize;
            fontStyle = other.fontStyle;
            alignment = other.alignment;
            wordWrap = other.wordWrap;
            richText = other.richText;
            clipping = other.clipping;
            imagePosition = other.imagePosition;
            contentOffset = other.contentOffset;
            fixedWidth = other.fixedWidth;
            fixedHeight = other.fixedHeight;
            stretchWidth = other.stretchWidth;
            stretchHeight = other.stretchHeight;
        }

        public static implicit operator GUIStyle(string skinStyleName) => GUI.skin.GetStyle(skinStyleName);

        /// <summary>The size used when the style names none: Unity's built-in font's.</summary>
        public const int DefaultFontSize = 13;

        internal int EffectiveSize => fontSize > 0 ? fontSize : font != null && font.fontSize > 0 ? font.fontSize : DefaultFontSize;

        public float lineHeight => GUI.Canvas.LineHeight(EffectiveSize, fontStyle);

        public Vector2 CalcSize(GUIContent content)
        {
            var lines = (content?.text ?? "").Split('\n');
            float w = 0;
            foreach (var l in lines) w = Math.Max(w, GUI.Canvas.Measure(l, EffectiveSize, fontStyle));
            float h = lines.Length * lineHeight;
            if (content?.image != null)
            {
                w += content.image.width;
                h = Math.Max(h, content.image.height);
            }
            var size = new Vector2(Mathf.Ceil(w) + padding.horizontal, Mathf.Ceil(h) + padding.vertical);
            if (fixedWidth > 0) size.x = fixedWidth;
            if (fixedHeight > 0) size.y = fixedHeight;
            return size;
        }

        public float CalcHeight(GUIContent content, float width)
        {
            if (fixedHeight > 0) return fixedHeight;
            var lines = Layout(content?.text ?? "", width - padding.horizontal);
            return Mathf.Ceil(lines.Count * lineHeight) + padding.vertical;
        }

        public void CalcMinMaxWidth(GUIContent content, out float minWidth, out float maxWidth)
        {
            maxWidth = CalcSize(content).x;
            if (!wordWrap) minWidth = maxWidth;
            else
            {
                minWidth = 0;
                foreach (var word in (content?.text ?? "").Split(' ', '\n'))
                    minWidth = Math.Max(minWidth, GUI.Canvas.Measure(word, EffectiveSize, fontStyle) + padding.horizontal);
            }
        }

        /// <summary>The text broken into the lines it is drawn on: at newlines, and (word wrap on) where a word would run past <paramref name="width"/>.</summary>
        internal List<string> Layout(string text, float width)
        {
            var result = new List<string>();
            foreach (var para in text.Split('\n'))
            {
                if (!wordWrap || width <= 0 || GUI.Canvas.Measure(para, EffectiveSize, fontStyle) <= width)
                {
                    result.Add(para);
                    continue;
                }
                // Whole words while they fit (measured at word ends only); a word too long for a line on its own breaks
                // where it stops fitting.
                int lineStart = 0, lastFit = -1, i = 0;
                while (i <= para.Length)
                {
                    if (i == para.Length || para[i] == ' ')
                    {
                        if (GUI.Canvas.Measure(para.Substring(lineStart, i - lineStart), EffectiveSize, fontStyle) <= width) lastFit = i;
                        else if (lastFit > lineStart)
                        {
                            result.Add(para.Substring(lineStart, lastFit - lineStart));
                            lineStart = lastFit;
                            while (lineStart < para.Length && para[lineStart] == ' ') lineStart++;
                            lastFit = -1;
                            i = lineStart;
                            continue;
                        }
                        else
                        {
                            int cut = lineStart + 1;
                            while (cut < i && GUI.Canvas.Measure(para.Substring(lineStart, cut + 1 - lineStart), EffectiveSize, fontStyle) <= width) cut++;
                            result.Add(para.Substring(lineStart, cut - lineStart));
                            lineStart = cut;
                            lastFit = -1;
                            i = lineStart;
                            continue;
                        }
                        if (i == para.Length)
                        {
                            if (lineStart < para.Length || result.Count == 0) result.Add(para.Substring(lineStart));
                            break;
                        }
                    }
                    i++;
                }
            }
            return result;
        }

        /// <summary>The state drawn: Unity takes hover/active/focused (or their on- versions) only where that state has a
        /// background; otherwise the normal state, text colour included.</summary>
        internal GUIStyleState StateFor(bool isHover, bool isActive, bool on, bool hasKeyboardFocus)
        {
            var s = on && onNormal.background != null ? onNormal : normal;
            if (on)
            {
                if (isHover && onHover.background != null) s = onHover;
                if (isHover && isActive && onActive.background != null) s = onActive;
                if (hasKeyboardFocus && onFocused.background != null) s = onFocused;
            }
            else
            {
                if (isHover && hover.background != null) s = hover;
                if (isHover && isActive && active.background != null) s = active;
                if (hasKeyboardFocus && focused.background != null) s = focused;
            }
            return s;
        }

        public void Draw(Rect position, bool isHover, bool isActive, bool on, bool hasKeyboardFocus) =>
            Draw(position, GUIContent.none, isHover, isActive, on, hasKeyboardFocus);

        public void Draw(Rect position, GUIContent content, bool isHover, bool isActive, bool on, bool hasKeyboardFocus)
        {
            if (Event.current.type != EventType.Repaint) return;
            var state = StateFor(isHover, isActive, on, hasKeyboardFocus);
            if (state.background != null)
                GUI.DrawSliced(overflow.Add(position), state.background, border, GUI.BackgroundTint());
            DrawContent(position, content, state.textColor);
        }

        internal void DrawContent(Rect position, GUIContent content, Color textColor, int caret = -1)
        {
            var area = padding.Remove(position);
            area.x += contentOffset.x;
            area.y += contentOffset.y;
            if (content?.image != null)
            {
                var img = content.image;
                float s = Math.Min(1f, Math.Min(area.width / Math.Max(1, img.width), area.height / Math.Max(1, img.height)));
                var ir = new Rect(area.x, area.y + (area.height - img.height * s) * 0.5f, img.width * s, img.height * s);
                GUI.DrawTextureTinted(ir, img, GUI.ContentTint(Color.white));
                area.x += ir.width;
                area.width -= ir.width;
            }
            string text = content?.text ?? "";
            if (text.Length == 0 && caret < 0) return;
            var lines = Layout(text, area.width);
            float lh = lineHeight;
            float total = lines.Count * lh;
            float y = area.y;
            int v = (int)alignment / 3, hAlign = (int)alignment % 3;
            if (v == 1) y = area.y + (area.height - total) * 0.5f;
            else if (v == 2) y = area.yMax - total;
            y = Mathf.Round(y);
            var tint = GUI.ContentTint(textColor);
            Rect? clip = clipping == TextClipping.Overflow ? (Rect?)null : area;
            for (int i = 0; i < lines.Count; i++, y += lh)
            {
                string line = lines[i];
                float w = GUI.Canvas.Measure(line, EffectiveSize, fontStyle);
                float x = hAlign == 0 ? area.x : hAlign == 1 ? area.x + (area.width - w) * 0.5f : area.xMax - w;
                GUI.DrawTextLine(line, new Vector2(Mathf.Round(x), y), EffectiveSize, fontStyle, tint, clip, lh);
                if (caret >= 0 && i == lines.Count - 1)
                    GUI.DrawCaret(new Vector2(Mathf.Round(x) + w + 1, y), lh, tint);
            }
        }

        /// <summary>Unity's TextEditor selection colour (the default skin's settings.selectionColor).</summary>
        internal static readonly Color SelectionColor = new Color(61 / 255f, 128 / 255f, 223 / 255f, 166 / 255f);

        /// <summary>Where a single-line field's text starts (x, y) inside <paramref name="position"/>, before scrolling.</summary>
        Vector2 FieldOrigin(Rect position)
        {
            var area = padding.Remove(position);
            area.x += contentOffset.x;
            area.y += contentOffset.y;
            float lh = lineHeight;
            int v = (int)alignment / 3;
            float y = v == 1 ? area.y + (area.height - lh) * 0.5f : v == 2 ? area.yMax - lh : area.y;
            return new Vector2(area.x, Mathf.Round(y));
        }

        /// <summary>The character boundary nearest <paramref name="mouseX"/> (local) in a single-line field.</summary>
        internal int TextFieldIndexAt(Rect position, string text, float scroll, float mouseX)
        {
            float x = mouseX - (FieldOrigin(position).x - scroll);
            int best = 0;
            float prev = 0;
            for (int i = 1; i <= text.Length; i++)
            {
                float w = GUI.Canvas.Measure(text.Substring(0, i), EffectiveSize, fontStyle);
                if (x < (prev + w) * 0.5f) return i - 1;
                prev = w;
                best = i;
            }
            return best;
        }

        /// <summary>A focused single-line field as Unity's TextEditor draws it: the text scrolled so the cursor stays in
        /// view and clipped to the padded area, the selection highlighted behind it, the cursor a 1-px line.</summary>
        internal void DrawTextFieldContent(Rect position, string text, Color textColor, GUI.TextEditing ed)
        {
            var area = padding.Remove(position);
            var origin = FieldOrigin(position);
            float lh = lineHeight;
            int size = EffectiveSize;
            float cursorX = GUI.Canvas.Measure(text.Substring(0, ed.Cursor), size, fontStyle);
            // TextEditor.UpdateScrollOffset: keep the cursor inside the visible width.
            float view = Math.Max(1, area.xMax - origin.x);
            if (cursorX - ed.Scroll > view - 1) ed.Scroll = cursorX - view + 1;
            if (cursorX - ed.Scroll < 0) ed.Scroll = cursorX;
            float full = GUI.Canvas.Measure(text, size, fontStyle);
            if (full - ed.Scroll < view - 1) ed.Scroll = Math.Max(0, full - view + 1);
            float x0 = Mathf.Round(origin.x - ed.Scroll);
            GUI.BeginClip(area);
            var local = new Vector2(x0 - area.x, origin.y - area.y);
            if (ed.HasSelection)
            {
                int a = Math.Min(ed.Cursor, ed.Select), b = Math.Max(ed.Cursor, ed.Select);
                float sx = GUI.Canvas.Measure(text.Substring(0, a), size, fontStyle);
                float ex = GUI.Canvas.Measure(text.Substring(0, b), size, fontStyle);
                GUI.DrawTextureTinted(new Rect(local.x + sx, local.y, ex - sx, lh), Texture2D.whiteTexture, SelectionColor);
            }
            GUI.DrawTextLine(text, local, size, fontStyle, GUI.ContentTint(textColor), null, lh);
            GUI.DrawCaret(new Vector2(local.x + cursorX, local.y), lh, new Color(1, 1, 1, GUI.color.a)); // settings.cursorColor
            GUI.EndClip();
        }
    }

    /// <summary>Unity's built-in game skin: box, button, toggle, label, text field / area, window, scrollbars, slider.</summary>
    public sealed class GUISkin
    {
        public Font font = new Font();
        public GUIStyle box, button, label, textField, textArea, toggle, window, horizontalSlider, horizontalSliderThumb,
            verticalScrollbar, verticalScrollbarThumb, horizontalScrollbar, horizontalScrollbarThumb, scrollView;

        public GUIStyle GetStyle(string name) => FindStyle(name) ?? GUIStyle.none;

        public GUIStyle FindStyle(string name)
        {
            switch (name?.ToLowerInvariant())
            {
                case "box": return box;
                case "button": return button;
                case "label": return label;
                case "textfield": return textField;
                case "textarea": return textArea;
                case "toggle": return toggle;
                case "window": return window;
                default: return null;
            }
        }

        /// <summary>Unity's runtime default skin, exactly: GENERATED (BuiltinSkin.cs, tools/unity_extract.py) from the
        /// original player's GameSkin: every style's states, border, margin, padding, overflow, alignment, clipping and
        /// fixed sizes, and its textures pixel for pixel; the font is Unity's built-in Arial at 13 (US-T57).</summary>
        internal static GUISkin BuiltIn() => BuiltinSkin.Make();
    }

    [Flags]
    public enum EventModifiers
    {
        None = 0,
        Shift = 1,
        Control = 2,
        Alt = 4,
        Command = 8,
        Numeric = 16,
        CapsLock = 32,
        FunctionKey = 64,
    }

    public enum EventType
    {
        MouseDown = 0,
        MouseUp = 1,
        MouseMove = 2,
        MouseDrag = 3,
        KeyDown = 4,
        KeyUp = 5,
        ScrollWheel = 6,
        Repaint = 7,
        Layout = 8,
        DragUpdated = 9,
        DragPerform = 10,
        DragExited = 15,
        Ignore = 11,
        Used = 12,
        ValidateCommand = 13,
        ExecuteCommand = 14,
        ContextClick = 16,
        MouseEnterWindow = 20,
        MouseLeaveWindow = 21,
    }

    /// <summary>Unity's KeyCode, with its values (the subset IMGUI events carry here).</summary>
    public enum KeyCode
    {
        None = 0,
        Backspace = 8,
        Tab = 9,
        Return = 13,
        Pause = 19,
        Escape = 27,
        Space = 32,
        Quote = 39,
        Comma = 44,
        Minus = 45,
        Period = 46,
        Slash = 47,
        Alpha0 = 48, Alpha1, Alpha2, Alpha3, Alpha4, Alpha5, Alpha6, Alpha7, Alpha8, Alpha9,
        Semicolon = 59,
        Equals = 61,
        LeftBracket = 91,
        Backslash = 92,
        RightBracket = 93,
        BackQuote = 96,
        A = 97, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Delete = 127,
        KeypadDivide = 267,
        KeypadMultiply = 268,
        KeypadMinus = 269,
        KeypadPlus = 270,
        KeypadEnter = 271,
        UpArrow = 273,
        DownArrow = 274,
        RightArrow = 275,
        LeftArrow = 276,
        Insert = 277,
        Home = 278,
        End = 279,
        PageUp = 280,
        PageDown = 281,
        F1 = 282, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        RightShift = 303,
        LeftShift = 304,
        RightControl = 305,
        LeftControl = 306,
        RightAlt = 307,
        LeftAlt = 308,
    }
}

namespace UnityEngine
{
    /// <summary>Rect members the UI uses that the facade's Rect (UnityMath.cs) does not declare; an instance member
    /// added there later takes precedence over these.</summary>
    public static class GuiRectExtensions
    {
        /// <summary>Unity's Rect.Overlaps: the two intersect with a positive area.</summary>
        public static bool Overlaps(this Rect a, Rect b) => b.xMax > a.xMin && b.xMin < a.xMax && b.yMax > a.yMin && b.yMin < a.yMax;
    }
}
