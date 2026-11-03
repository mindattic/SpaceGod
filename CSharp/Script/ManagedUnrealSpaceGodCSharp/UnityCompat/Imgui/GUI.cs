using System;
using System.Collections.Generic;

// Unity's immediate-mode GUI (UnityEngine.GUI / GUIUtility), the way the original's OnGUI code uses it: Label, Box,
// Button, DrawTexture, TextField, HorizontalSlider, groups, clips and scroll views, the colour/enabled/matrix state,
// hot and keyboard controls. Each OnGUI pass runs once per event (Layout, then the input event; Layout, then
// Repaint), and only Repaint draws, as in Unity. Drawing goes to IGuiCanvas in screen pixels (AHUD's Canvas in a
// window, a counting canvas headless).
namespace UnityEngine
{
    /// <summary>Thrown by <see cref="GUIUtility.ExitGUI"/> to end the current OnGUI pass at once, as in Unity.</summary>
    public sealed class ExitGUIException : Exception { }

    /// <summary>GUI.matrix: a 2D affine transform (the rotations GUIUtility.RotateAroundPivot makes).</summary>
    public struct GuiMatrix
    {
        public float a, b, c, d, tx, ty; // x' = a x + c y + tx, y' = b x + d y + ty

        public static GuiMatrix identity => new GuiMatrix { a = 1, d = 1 };
        public bool isIdentity => a == 1 && b == 0 && c == 0 && d == 1 && tx == 0 && ty == 0;

        public Vector2 MultiplyPoint(Vector2 p) => new Vector2(a * p.x + c * p.y + tx, b * p.x + d * p.y + ty);

        public static GuiMatrix operator *(GuiMatrix m, GuiMatrix n) => new GuiMatrix
        {
            a = m.a * n.a + m.c * n.b,
            b = m.b * n.a + m.d * n.b,
            c = m.a * n.c + m.c * n.d,
            d = m.b * n.c + m.d * n.d,
            tx = m.a * n.tx + m.c * n.ty + m.tx,
            ty = m.b * n.tx + m.d * n.ty + m.ty,
        };

        internal float AngleDegrees => MathF.Atan2(b, a) * 180f / MathF.PI;
    }

    /// <summary>What the GUI draws on: screen pixels, origin top-left, Unity colours (gamma).</summary>
    public interface IGuiCanvas
    {
        /// <summary>Width in pixels of one line of <paramref name="text"/> at Unity font size <paramref name="size"/>.</summary>
        float Measure(string text, int size, FontStyle style);
        float LineHeight(int size, FontStyle style);
        /// <summary>Draws <paramref name="texture"/>'s <paramref name="uv"/> part (0..1, v down) into <paramref name="screen"/>, tinted,
        /// rotated <paramref name="rotation"/> degrees clockwise about its centre.</summary>
        void DrawTexture(Texture texture, Rect screen, Rect uv, Color tint, float rotation);
        void DrawText(string text, Vector2 screen, int size, FontStyle style, Color tint);
    }

    public static class GUIUtility
    {
        public static int hotControl;
        public static int keyboardControl;
        internal static int nextId;

        public static int GetControlID(FocusType focus) => ++nextId;
        public static int GetControlID(int hint, FocusType focus) => ++nextId;
        public static int GetControlID(FocusType focus, Rect position) => ++nextId;

        public static void ExitGUI() => throw new ExitGUIException();

        public static void RotateAroundPivot(float angle, Vector2 pivotPoint)
        {
            var p = GUI.ToScreenPoint(pivotPoint);
            float r = angle * MathF.PI / 180f, cos = MathF.Cos(r), sin = MathF.Sin(r);
            var rot = new GuiMatrix { a = cos, b = sin, c = -sin, d = cos, tx = p.x - cos * p.x + sin * p.y, ty = p.y - sin * p.x - cos * p.y };
            GUI.matrix = rot * GUI.matrix;
        }

        public static Vector2 GUIToScreenPoint(Vector2 guiPoint) => GUI.ToScreenPoint(guiPoint);
        public static Vector2 ScreenToGUIPoint(Vector2 screenPoint) => GUI.ToLocalPoint(screenPoint);
    }

    public static class GUI
    {
        public static Color color = Color.white;
        public static Color contentColor = Color.white;
        public static Color backgroundColor = Color.white;
        public static bool enabled = true;
        public static bool changed;
        public static int depth;
        public static GuiMatrix matrix = GuiMatrix.identity;
        public static string tooltip = "";

        static GUISkin builtIn;
        static GUISkin current;

        public static GUISkin skin
        {
            get => current ??= builtIn ??= GUISkin.BuiltIn();
            set => current = value;
        }

        /// <summary>Where this pass draws (set by <see cref="ImguiHost"/>).</summary>
        public static IGuiCanvas Canvas { get; internal set; } = new MeasuringCanvas();

        // ---------------------------------------------------------------- clip stack

        struct ClipFrame
        {
            public Rect Clip;     // visible area, screen pixels
            public Vector2 Origin; // screen position of the local (0, 0)
        }

        static readonly List<ClipFrame> clips = new List<ClipFrame>();
        static readonly Vector2 Far = new Vector2(-100000f, -100000f);

        static ClipFrame Top => clips.Count > 0 ? clips[clips.Count - 1] : new ClipFrame { Clip = new Rect(0, 0, Screen.width, Screen.height) };

        internal static Vector2 ToScreenPoint(Vector2 local) => local + Top.Origin;

        internal static Vector2 ToLocalPoint(Vector2 screen)
        {
            var top = Top;
            if (clips.Count > 0 && !top.Clip.Contains(screen)) return Far;
            return screen - top.Origin;
        }

        static Rect ToScreen(Rect local)
        {
            var o = Top.Origin;
            return new Rect(local.x + o.x, local.y + o.y, local.width, local.height);
        }

        static Rect Intersect(Rect a, Rect b)
        {
            float x0 = Math.Max(a.xMin, b.xMin), y0 = Math.Max(a.yMin, b.yMin);
            float x1 = Math.Min(a.xMax, b.xMax), y1 = Math.Min(a.yMax, b.yMax);
            return new Rect(x0, y0, Math.Max(0, x1 - x0), Math.Max(0, y1 - y0));
        }

        static void PushClip(Rect localRect, Vector2 contentOffset)
        {
            var screen = ToScreen(localRect);
            var clip = Intersect(screen, Top.Clip);
            clips.Add(new ClipFrame { Clip = clip, Origin = new Vector2(screen.x, screen.y) + contentOffset });
        }

        static void PopClip()
        {
            if (clips.Count > 0) clips.RemoveAt(clips.Count - 1);
        }

        public static void BeginGroup(Rect position) => PushClip(position, Vector2.zero);
        public static void BeginGroup(Rect position, GUIStyle style)
        {
            Box(position, GUIContent.none, style);
            PushClip(position, Vector2.zero);
        }
        public static void EndGroup() => PopClip();
        public static void BeginClip(Rect position) => PushClip(position, Vector2.zero);
        public static void BeginClip(Rect position, Vector2 scrollOffset, Vector2 renderOffset, bool resetOffset) => PushClip(position, scrollOffset + renderOffset);
        public static void EndClip() => PopClip();

        /// <summary>Starts a pass: Unity's per-OnGUI reset of the GUI state.</summary>
        internal static void BeginPass(IGuiCanvas canvas)
        {
            Canvas = canvas;
            color = contentColor = backgroundColor = Color.white;
            enabled = true;
            changed = false;
            matrix = GuiMatrix.identity;
            clips.Clear();
            scrollStack.Clear();
            GUIUtility.nextId = 0;
            nextControlName = null;
        }

        internal static void EndPass()
        {
            clips.Clear();
            scrollStack.Clear();
        }

        // ---------------------------------------------------------------- tints

        static float EnabledAlpha => enabled ? 1f : 0.5f;

        internal static Color BackgroundTint()
        {
            var c = color * backgroundColor;
            c.a *= EnabledAlpha;
            return c;
        }

        internal static Color ContentTint(Color textColor)
        {
            var c = color * contentColor * textColor;
            c.a *= EnabledAlpha;
            return c;
        }

        // ---------------------------------------------------------------- primitives (local rects)

        /// <summary>Draws a texture into a local rect, clipped to the current clip (exactly, by cutting its UVs) unless rotated.</summary>
        internal static void DrawTextureTinted(Rect local, Texture tex, Color tint) => DrawTextureUv(local, tex, new Rect(0, 0, 1, 1), tint);

        static void DrawTextureUv(Rect local, Texture tex, Rect uv, Color tint)
        {
            if (tex == null || tint.a <= 0 || local.width == 0 || local.height == 0) return;
            var screen = ToScreen(local);
            if (!matrix.isIdentity)
            {
                var centre = matrix.MultiplyPoint(screen.center);
                var r = new Rect(centre.x - screen.width * 0.5f, centre.y - screen.height * 0.5f, screen.width, screen.height);
                Canvas.DrawTexture(tex, r, uv, tint, matrix.AngleDegrees);
                ImguiHost.Stats.Textures++;
                return;
            }
            var clip = Top.Clip;
            var cut = Intersect(screen, clip);
            if (cut.width <= 0 || cut.height <= 0) return;
            if (cut.width != screen.width || cut.height != screen.height)
            {
                float u0 = uv.x + (cut.x - screen.x) / screen.width * uv.width;
                float v0 = uv.y + (cut.y - screen.y) / screen.height * uv.height;
                uv = new Rect(u0, v0, cut.width / screen.width * uv.width, cut.height / screen.height * uv.height);
                screen = cut;
            }
            Canvas.DrawTexture(tex, screen, uv, tint, 0);
            ImguiHost.Stats.Textures++;
        }

        /// <summary>A style's background: the texture nine-sliced by <paramref name="border"/> (in texture pixels) over the rect.</summary>
        internal static void DrawSliced(Rect local, Texture2D tex, RectOffset border, Color tint)
        {
            if (border == null || (border.left == 0 && border.right == 0 && border.top == 0 && border.bottom == 0))
            {
                DrawTextureUv(local, tex, new Rect(0, 0, 1, 1), tint);
                return;
            }
            float tw = tex.width, th = tex.height;
            float l = Math.Min(border.left, local.width * 0.5f), r = Math.Min(border.right, local.width * 0.5f);
            float t = Math.Min(border.top, local.height * 0.5f), b = Math.Min(border.bottom, local.height * 0.5f);
            float[] xs = { local.x, local.x + l, local.xMax - r, local.xMax };
            float[] ys = { local.y, local.y + t, local.yMax - b, local.yMax };
            float[] us = { 0, border.left / tw, 1 - border.right / tw, 1 };
            float[] vs = { 0, border.top / th, 1 - border.bottom / th, 1 };
            for (int j = 0; j < 3; j++)
                for (int i = 0; i < 3; i++)
                {
                    var dst = new Rect(xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j]);
                    if (dst.width <= 0 || dst.height <= 0) continue;
                    DrawTextureUv(dst, tex, new Rect(us[i], vs[j], us[i + 1] - us[i], vs[j + 1] - vs[j]), tint);
                }
        }

        /// <summary>One line of text at a local position, cut to the current clip (and <paramref name="styleClip"/>, a style
        /// that clips): characters past either side are dropped; a line less than half inside vertically is not drawn
        /// (Canvas text cannot be cut through a glyph).</summary>
        internal static void DrawTextLine(string text, Vector2 local, int size, FontStyle style, Color tint, Rect? styleClip, float lineHeight)
        {
            if (string.IsNullOrEmpty(text) || tint.a <= 0) return;
            var p = ToScreenPoint(local);
            var clip = Top.Clip;
            if (styleClip.HasValue) clip = Intersect(clip, ToScreen(styleClip.Value));
            if (!matrix.isIdentity) p = matrix.MultiplyPoint(p);
            float visible = Math.Min(p.y + lineHeight, clip.yMax) - Math.Max(p.y, clip.yMin);
            if (visible < lineHeight * 0.5f) return;
            float w = Canvas.Measure(text, size, style);
            if (p.x < clip.xMin - 0.5f)
            {
                int cut = 0;
                while (cut < text.Length && p.x + Canvas.Measure(text.Substring(0, cut), size, style) < clip.xMin - 0.5f) cut++;
                if (cut >= text.Length) return;
                p.x += Canvas.Measure(text.Substring(0, cut), size, style);
                text = text.Substring(cut);
                w = Canvas.Measure(text, size, style);
            }
            if (p.x + w > clip.xMax + 0.5f)
            {
                int lo = 0, hi = text.Length;
                while (lo < hi)
                {
                    int mid = (lo + hi + 1) / 2;
                    if (p.x + Canvas.Measure(text.Substring(0, mid), size, style) <= clip.xMax + 0.5f) lo = mid;
                    else hi = mid - 1;
                }
                if (lo == 0) return;
                text = text.Substring(0, lo);
            }
            Canvas.DrawText(text, p, size, style, tint);
            ImguiHost.Stats.Texts++;
        }

        internal static void DrawCaret(Vector2 local, float height, Color tint)
        {
            if (((int)(Time.unscaledTime * 2f) & 1) != 0) return;
            DrawTextureUv(new Rect(local.x, local.y + 1, 1, height - 2), Texture2D.whiteTexture, new Rect(0, 0, 1, 1), tint);
        }

        static bool Hit(Rect position) => position.Contains(Event.current.mousePosition);

        static bool Hover(Rect position, int id) =>
            enabled && (GUIUtility.hotControl == 0 || GUIUtility.hotControl == id) && Hit(position);

        // ---------------------------------------------------------------- Label, Box, DrawTexture

        public static void Label(Rect position, string text) => Label(position, GUIContent.Temp(text), skin.label);
        public static void Label(Rect position, Texture image) => Label(position, new GUIContent(image), skin.label);
        public static void Label(Rect position, GUIContent content) => Label(position, content, skin.label);
        public static void Label(Rect position, string text, GUIStyle style) => Label(position, GUIContent.Temp(text), style);
        public static void Label(Rect position, Texture image, GUIStyle style) => Label(position, new GUIContent(image), style);

        public static void Label(Rect position, GUIContent content, GUIStyle style)
        {
            if (Event.current.type != EventType.Repaint) return;
            (style ?? GUIStyle.none).Draw(position, content, false, false, false, false);
            ImguiHost.Stats.Labels++;
        }

        public static void Box(Rect position, string text) => Box(position, GUIContent.Temp(text), skin.box);
        public static void Box(Rect position, GUIContent content) => Box(position, content, skin.box);
        public static void Box(Rect position, string text, GUIStyle style) => Box(position, GUIContent.Temp(text), style);

        public static void Box(Rect position, GUIContent content, GUIStyle style)
        {
            int id = GUIUtility.GetControlID(FocusType.Passive);
            if (Event.current.type != EventType.Repaint) return;
            (style ?? GUIStyle.none).Draw(position, content, Hover(position, id), false, false, false);
            ImguiHost.Stats.Boxes++;
        }

        public static void DrawTexture(Rect position, Texture image) => DrawTexture(position, image, ScaleMode.StretchToFill, true, 0);
        public static void DrawTexture(Rect position, Texture image, ScaleMode scaleMode) => DrawTexture(position, image, scaleMode, true, 0);
        public static void DrawTexture(Rect position, Texture image, ScaleMode scaleMode, bool alphaBlend) => DrawTexture(position, image, scaleMode, alphaBlend, 0);

        public static void DrawTexture(Rect position, Texture image, ScaleMode scaleMode, bool alphaBlend, float imageAspect)
        {
            if (Event.current.type != EventType.Repaint || image == null) return;
            var tint = color;
            tint.a *= EnabledAlpha;
            float aspect = imageAspect > 0 ? imageAspect : (float)image.width / Math.Max(1, image.height);
            float rectAspect = position.width / Math.Max(1e-5f, position.height);
            switch (scaleMode)
            {
                case ScaleMode.ScaleToFit:
                    if (aspect > rectAspect)
                    {
                        float h = position.width / aspect;
                        position = new Rect(position.x, position.y + (position.height - h) * 0.5f, position.width, h);
                    }
                    else
                    {
                        float w = position.height * aspect;
                        position = new Rect(position.x + (position.width - w) * 0.5f, position.y, w, position.height);
                    }
                    DrawTextureUv(position, image, new Rect(0, 0, 1, 1), tint);
                    break;
                case ScaleMode.ScaleAndCrop:
                    if (aspect > rectAspect)
                    {
                        float uw = rectAspect / aspect;
                        DrawTextureUv(position, image, new Rect((1 - uw) * 0.5f, 0, uw, 1), tint);
                    }
                    else
                    {
                        float vh = aspect / rectAspect;
                        DrawTextureUv(position, image, new Rect(0, (1 - vh) * 0.5f, 1, vh), tint);
                    }
                    break;
                default:
                    DrawTextureUv(position, image, new Rect(0, 0, 1, 1), tint);
                    break;
            }
        }

        public static void DrawTextureWithTexCoords(Rect position, Texture image, Rect texCoords)
        {
            if (Event.current.type != EventType.Repaint || image == null) return;
            var tint = color;
            tint.a *= EnabledAlpha;
            // Unity's tex coords are v up; the canvas's v down.
            DrawTextureUv(position, image, new Rect(texCoords.x, 1 - texCoords.y - texCoords.height, texCoords.width, texCoords.height), tint);
        }

        // ---------------------------------------------------------------- Button

        public static bool Button(Rect position, string text) => Button(position, GUIContent.Temp(text), skin.button);
        public static bool Button(Rect position, Texture image) => Button(position, new GUIContent(image), skin.button);
        public static bool Button(Rect position, GUIContent content) => Button(position, content, skin.button);
        public static bool Button(Rect position, string text, GUIStyle style) => Button(position, GUIContent.Temp(text), style);
        public static bool Button(Rect position, Texture image, GUIStyle style) => Button(position, new GUIContent(image), style);

        public static bool Button(Rect position, GUIContent content, GUIStyle style)
        {
            int id = GUIUtility.GetControlID(FocusType.Passive);
            var e = Event.current;
            switch (e.GetTypeForControl(id))
            {
                case EventType.MouseDown:
                    if (Hit(position))
                    {
                        GUIUtility.hotControl = id;
                        e.Use();
                    }
                    break;
                case EventType.MouseDrag:
                    if (GUIUtility.hotControl == id) e.Use();
                    break;
                case EventType.MouseUp:
                    if (GUIUtility.hotControl == id)
                    {
                        GUIUtility.hotControl = 0;
                        e.Use();
                        if (Hit(position))
                        {
                            changed = true;
                            ImguiHost.Stats.Clicks++;
                            return true;
                        }
                    }
                    break;
                case EventType.Repaint:
                    (style ?? GUIStyle.none).Draw(position, content, Hover(position, id), GUIUtility.hotControl == id, false, false);
                    ImguiHost.Stats.Buttons++;
                    if (enabled && !string.IsNullOrEmpty(content?.text)) ImguiHost.NoteButton(content.text, Intersect(ToScreen(position), Top.Clip));
                    break;
            }
            return false;
        }

        public static bool Toggle(Rect position, bool value, string text) => Toggle(position, value, GUIContent.Temp(text), skin.button);

        public static bool Toggle(Rect position, bool value, GUIContent content, GUIStyle style)
        {
            int id = GUIUtility.GetControlID(FocusType.Passive);
            var e = Event.current;
            switch (e.GetTypeForControl(id))
            {
                case EventType.MouseDown:
                    if (Hit(position)) { GUIUtility.hotControl = id; e.Use(); }
                    break;
                case EventType.MouseUp:
                    if (GUIUtility.hotControl == id)
                    {
                        GUIUtility.hotControl = 0;
                        e.Use();
                        if (Hit(position)) { changed = true; return !value; }
                    }
                    break;
                case EventType.Repaint:
                    (style ?? GUIStyle.none).Draw(position, content, Hover(position, id), GUIUtility.hotControl == id, value, false);
                    break;
            }
            return value;
        }

        // ---------------------------------------------------------------- TextField

        static string nextControlName;
        static readonly Dictionary<string, int> namedControls = new Dictionary<string, int>();
        static string focusRequest;

        public static void SetNextControlName(string name) => nextControlName = name;

        public static string GetNameOfFocusedControl()
        {
            foreach (var kv in namedControls)
                if (kv.Value == GUIUtility.keyboardControl && kv.Value != 0) return kv.Key;
            return "";
        }

        public static void FocusControl(string name) => focusRequest = name;

        public static string TextField(Rect position, string text) => TextField(position, text, -1, skin.textField);
        public static string TextField(Rect position, string text, int maxLength) => TextField(position, text, maxLength, skin.textField);
        public static string TextField(Rect position, string text, GUIStyle style) => TextField(position, text, -1, style);

        public static string TextField(Rect position, string text, int maxLength, GUIStyle style)
        {
            text ??= "";
            int id = GUIUtility.GetControlID(FocusType.Keyboard);
            if (nextControlName != null)
            {
                namedControls[nextControlName] = id;
                if (focusRequest == nextControlName)
                {
                    GUIUtility.keyboardControl = id;
                    focusRequest = null;
                }
                nextControlName = null;
            }
            var e = Event.current;
            bool focused = GUIUtility.keyboardControl == id;
            switch (e.GetTypeForControl(id))
            {
                case EventType.MouseDown:
                    if (Hit(position))
                    {
                        GUIUtility.hotControl = id;
                        GUIUtility.keyboardControl = id;
                        ImguiHost.FocusTaken = true;
                        e.Use();
                    }
                    break;
                case EventType.MouseUp:
                    if (GUIUtility.hotControl == id)
                    {
                        GUIUtility.hotControl = 0;
                        e.Use();
                    }
                    break;
                case EventType.KeyDown:
                    if (!focused) break;
                    if (e.keyCode == KeyCode.Backspace)
                    {
                        if (text.Length > 0) text = text.Substring(0, text.Length - 1);
                        changed = true;
                        e.Use();
                    }
                    else if (e.character >= ' ' && e.character != '\u007f')
                    {
                        if (maxLength < 0 || text.Length < maxLength) text += e.character;
                        changed = true;
                        e.Use();
                    }
                    else if (e.keyCode == KeyCode.LeftArrow || e.keyCode == KeyCode.RightArrow || e.keyCode == KeyCode.Home || e.keyCode == KeyCode.End
                             || e.keyCode == KeyCode.Delete)
                        e.Use();
                    break;
                case EventType.Repaint:
                {
                    style ??= GUIStyle.none;
                    var state = style.StateFor(Hover(position, id), GUIUtility.hotControl == id, false, focused);
                    if (state.background != null) DrawSliced(style.overflow.Add(position), state.background, style.border, BackgroundTint());
                    style.DrawContent(position, GUIContent.Temp(text), state.textColor, focused ? text.Length : -1);
                    break;
                }
            }
            return text;
        }

        // ---------------------------------------------------------------- HorizontalSlider

        static float sliderGrab;

        public static float HorizontalSlider(Rect position, float value, float leftValue, float rightValue) =>
            HorizontalSlider(position, value, leftValue, rightValue, skin.horizontalSlider, skin.horizontalSliderThumb);

        public static float HorizontalSlider(Rect position, float value, float leftValue, float rightValue, GUIStyle slider, GUIStyle thumb)
        {
            int id = GUIUtility.GetControlID(FocusType.Passive);
            float thumbW = thumb.fixedWidth > 0 ? thumb.fixedWidth : 12;
            float thumbH = thumb.fixedHeight > 0 ? thumb.fixedHeight : position.height;
            float trackH = slider.fixedHeight > 0 ? slider.fixedHeight : position.height;
            float span = Math.Max(1f, position.width - thumbW);
            float range = rightValue - leftValue;
            float t = range == 0 ? 0 : Math.Clamp((value - leftValue) / range, 0f, 1f);
            var thumbRect = new Rect(position.x + t * span, position.y + (position.height - thumbH) * 0.5f, thumbW, thumbH);
            var e = Event.current;
            switch (e.GetTypeForControl(id))
            {
                case EventType.MouseDown:
                    if (Hit(position))
                    {
                        GUIUtility.hotControl = id;
                        sliderGrab = thumbRect.Contains(e.mousePosition) ? e.mousePosition.x - thumbRect.x : thumbW * 0.5f;
                        value = leftValue + Math.Clamp((e.mousePosition.x - sliderGrab - position.x) / span, 0f, 1f) * range;
                        changed = true;
                        e.Use();
                    }
                    break;
                case EventType.MouseDrag:
                    if (GUIUtility.hotControl == id)
                    {
                        value = leftValue + Math.Clamp((e.mousePosition.x - sliderGrab - position.x) / span, 0f, 1f) * range;
                        changed = true;
                        e.Use();
                    }
                    break;
                case EventType.MouseUp:
                    if (GUIUtility.hotControl == id)
                    {
                        GUIUtility.hotControl = 0;
                        e.Use();
                    }
                    break;
                case EventType.Repaint:
                    slider.Draw(new Rect(position.x, position.y + (position.height - trackH) * 0.5f, position.width, trackH), false, false, false, false);
                    thumb.Draw(thumbRect, Hover(thumbRect, id), GUIUtility.hotControl == id, false, false);
                    break;
            }
            return value;
        }

        // ---------------------------------------------------------------- scroll views

        sealed class ScrollState
        {
            public Vector2 Position;
            public bool Apply;
            public float Grab;
        }

        struct ScrollFrame
        {
            public int Id;
            public Rect Position, Clip, View;
            public ScrollState State;
        }

        static readonly Dictionary<int, ScrollState> scrollStates = new Dictionary<int, ScrollState>();
        static readonly List<ScrollFrame> scrollStack = new List<ScrollFrame>();

        public static Vector2 BeginScrollView(Rect position, Vector2 scrollPosition, Rect viewRect) =>
            BeginScrollView(position, scrollPosition, viewRect, false, false);

        public static Vector2 BeginScrollView(Rect position, Vector2 scrollPosition, Rect viewRect, bool alwaysShowHorizontal, bool alwaysShowVertical) =>
            BeginScrollView(position, scrollPosition, viewRect, alwaysShowHorizontal, alwaysShowVertical, skin.horizontalScrollbar, skin.verticalScrollbar);

        public static Vector2 BeginScrollView(Rect position, Vector2 scrollPosition, Rect viewRect, GUIStyle horizontalScrollbar, GUIStyle verticalScrollbar) =>
            BeginScrollView(position, scrollPosition, viewRect, false, false, horizontalScrollbar, verticalScrollbar);

        public static Vector2 BeginScrollView(Rect position, Vector2 scrollPosition, Rect viewRect, bool alwaysShowHorizontal, bool alwaysShowVertical,
            GUIStyle horizontalScrollbar, GUIStyle verticalScrollbar)
        {
            int id = GUIUtility.GetControlID(FocusType.Passive);
            if (!scrollStates.TryGetValue(id, out var state)) scrollStates[id] = state = new ScrollState();
            if (state.Apply)
            {
                scrollPosition = state.Position;
                state.Apply = false;
            }
            float vbarW = verticalScrollbar?.fixedWidth > 0 ? verticalScrollbar.fixedWidth : 15;
            float hbarH = horizontalScrollbar?.fixedHeight > 0 ? horizontalScrollbar.fixedHeight : 15;
            var clip = position;
            bool needV = alwaysShowVertical || viewRect.height > clip.height;
            if (needV) clip.width -= vbarW;
            bool needH = alwaysShowHorizontal || viewRect.width > clip.width;
            if (needH)
            {
                clip.height -= hbarH;
                if (!needV && viewRect.height > clip.height)
                {
                    needV = true;
                    clip.width -= vbarW;
                }
            }
            float maxY = Math.Max(0, viewRect.height - clip.height), maxX = Math.Max(0, viewRect.width - clip.width);
            scrollPosition.y = Math.Clamp(scrollPosition.y, 0, maxY);
            scrollPosition.x = Math.Clamp(scrollPosition.x, 0, maxX);

            if (needV)
                scrollPosition.y = Scrollbar(id * 2 + 1000000, new Rect(clip.xMax, position.y, vbarW, clip.height), scrollPosition.y, clip.height, viewRect.height,
                    true, verticalScrollbar ?? skin.verticalScrollbar, skin.verticalScrollbarThumb, state);
            if (needH)
                scrollPosition.x = Scrollbar(id * 2 + 1000001, new Rect(position.x, clip.yMax, clip.width, hbarH), scrollPosition.x, clip.width, viewRect.width,
                    false, horizontalScrollbar ?? skin.horizontalScrollbar, skin.horizontalScrollbarThumb, state);

            scrollStack.Add(new ScrollFrame { Id = id, Position = position, Clip = clip, View = viewRect, State = state });
            PushClip(clip, new Vector2(-scrollPosition.x - viewRect.x, -scrollPosition.y - viewRect.y));
            state.Position = scrollPosition;
            return scrollPosition;
        }

        static float Scrollbar(int id, Rect bar, float value, float visible, float total, bool vertical, GUIStyle track, GUIStyle thumb, ScrollState state)
        {
            float length = vertical ? bar.height : bar.width;
            float thumbLen = Math.Max(Math.Min(length, 12f), length * Math.Min(1f, visible / Math.Max(1f, total)));
            float travel = Math.Max(1f, length - thumbLen);
            float max = Math.Max(0f, total - visible);
            float at = max <= 0 ? 0 : value / max * travel;
            var thumbRect = vertical ? new Rect(bar.x, bar.y + at, bar.width, thumbLen) : new Rect(bar.x + at, bar.y, thumbLen, bar.height);
            var e = Event.current;
            float Along(Vector2 p) => vertical ? p.y - bar.y : p.x - bar.x;
            switch (e.GetTypeForControl(id))
            {
                case EventType.MouseDown:
                    if (Hit(bar))
                    {
                        float m = Along(e.mousePosition);
                        if (thumbRect.Contains(e.mousePosition)) state.Grab = m - at;
                        else
                        {
                            // A click on the track pages towards it, as Unity's does.
                            value = Math.Clamp(value + (m < at ? -visible : visible), 0, max);
                            state.Grab = thumbLen * 0.5f;
                        }
                        GUIUtility.hotControl = id;
                        e.Use();
                    }
                    break;
                case EventType.MouseDrag:
                    if (GUIUtility.hotControl == id)
                    {
                        value = Math.Clamp((Along(e.mousePosition) - state.Grab) / travel * max, 0, max);
                        e.Use();
                    }
                    break;
                case EventType.MouseUp:
                    if (GUIUtility.hotControl == id)
                    {
                        GUIUtility.hotControl = 0;
                        e.Use();
                    }
                    break;
                case EventType.Repaint:
                    track.Draw(bar, false, false, false, false);
                    var inner = vertical ? new Rect(thumbRect.x + 2, thumbRect.y + 1, thumbRect.width - 4, thumbRect.height - 2)
                        : new Rect(thumbRect.x + 1, thumbRect.y + 2, thumbRect.width - 2, thumbRect.height - 4);
                    thumb.Draw(inner, false, false, false, false);
                    break;
            }
            return value;
        }

        public static void EndScrollView() => EndScrollView(true);

        public static void EndScrollView(bool handleScrollWheel)
        {
            if (scrollStack.Count == 0) return;
            var f = scrollStack[scrollStack.Count - 1];
            scrollStack.RemoveAt(scrollStack.Count - 1);
            PopClip();
            var e = Event.current;
            if (handleScrollWheel && e.type == EventType.ScrollWheel && f.Position.Contains(e.mousePosition))
            {
                var s = f.State;
                float maxY = Math.Max(0, f.View.height - f.Clip.height), maxX = Math.Max(0, f.View.width - f.Clip.width);
                var p = s.Position;
                p.x = Math.Clamp(p.x + e.delta.x * 20f, 0, maxX);
                p.y = Math.Clamp(p.y + e.delta.y * 20f, 0, maxY);
                if (p.x != s.Position.x || p.y != s.Position.y)
                {
                    s.Position = p;
                    s.Apply = true;
                    e.Use();
                }
            }
        }

        public static void ScrollTo(Rect position) { }
    }
}
