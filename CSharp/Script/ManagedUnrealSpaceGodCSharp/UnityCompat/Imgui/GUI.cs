using System;
using System.Collections.Generic;

// Unity's immediate-mode GUI (UnityEngine.GUI / GUIUtility), the way the original's OnGUI code uses it: Label, Box,
// Button, DrawTexture, TextField, HorizontalSlider, groups, clips and scroll views, the colour/enabled/matrix state,
// hot and keyboard controls. Each OnGUI pass runs once per event (Layout, then the input event; Layout, then
// Repaint), and only Repaint draws, as in Unity. Drawing goes to IGuiCanvas in screen pixels (the Slate overlay in a
// window, US-T54, else AHUD's Canvas; a counting canvas headless).
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

    /// <summary>US-T54: a canvas that clips what it draws (Slate's clipping zones), as Unity's GUIClip does: text at a
    /// clip edge is cut through its glyphs, a rotated quad exactly at the clip.</summary>
    public interface IClippingGuiCanvas : IGuiCanvas
    {
        /// <summary>The clip (screen pixels) for the draws after it; null: none.</summary>
        void SetClip(Rect? screen);
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
            if (!matrix.isIdentity && Canvas is IClippingGuiCanvas clipping)
            {
                // US-T54: the canvas clips (Slate's clipping zone), so the rotated quad is cut exactly where it leaves the
                // clip, corners included, as Unity's GUIClip cuts it.
                clipping.SetClip(clips.Count > 0 ? Top.Clip : null);
                var c = matrix.MultiplyPoint(screen.center);
                Canvas.DrawTexture(tex, new Rect(c.x - screen.width * 0.5f, c.y - screen.height * 0.5f, screen.width, screen.height), uv, tint, matrix.AngleDegrees);
                ImguiHost.Stats.Textures++;
                return;
            }
            if (!matrix.isIdentity)
            {
                var centre = matrix.MultiplyPoint(screen.center);
                // Clipped to the scroll view / group as Unity's clip does: the quad is cut along its own length where its
                // centre line leaves the clip (exact for the 1-2 px lines and arrows the UI rotates; a thick quad's
                // corners can overhang the clip by up to half its thickness).
                var along = matrix.MultiplyPoint(screen.center + new Vector2(1, 0)) - centre;
                float half = screen.width * 0.5f, t0 = -half, t1 = half;
                if (clips.Count > 0 && !ClipLine(centre, along, Top.Clip, ref t0, ref t1)) return;
                if (t0 > -half || t1 < half)
                {
                    centre += along * ((t0 + t1) * 0.5f);
                    uv = new Rect(uv.x + (t0 + half) / screen.width * uv.width, uv.y, (t1 - t0) / screen.width * uv.width, uv.height);
                    screen.width = t1 - t0;
                }
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
            (Canvas as IClippingGuiCanvas)?.SetClip(clips.Count > 0 ? clip : null);
            Canvas.DrawTexture(tex, screen, uv, tint, 0);
            ImguiHost.Stats.Textures++;
        }

        /// <summary>Liang-Barsky: narrows [t0, t1] of the line p + d t to the part inside <paramref name="clip"/>.</summary>
        static bool ClipLine(Vector2 p, Vector2 d, Rect clip, ref float t0, ref float t1)
        {
            bool Edge(float q, float r, ref float a, ref float b)
            {
                if (Math.Abs(q) < 1e-6f) return r >= 0;
                float t = r / q;
                if (q < 0) { if (t > b) return false; if (t > a) a = t; }
                else { if (t < a) return false; if (t < b) b = t; }
                return true;
            }
            return Edge(-d.x, p.x - clip.xMin, ref t0, ref t1) && Edge(d.x, clip.xMax - p.x, ref t0, ref t1)
                && Edge(-d.y, p.y - clip.yMin, ref t0, ref t1) && Edge(d.y, clip.yMax - p.y, ref t0, ref t1) && t1 > t0;
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
        /// that clips): on a clipping canvas (Slate, US-T54) through the glyphs; on AHUD's Canvas, which cannot cut a
        /// glyph, characters past either side are dropped and a line less than half inside vertically is not drawn.</summary>
        internal static void DrawTextLine(string text, Vector2 local, int size, FontStyle style, Color tint, Rect? styleClip, float lineHeight)
        {
            if (string.IsNullOrEmpty(text) || tint.a <= 0) return;
            var p = ToScreenPoint(local);
            var clip = Top.Clip;
            if (styleClip.HasValue) clip = Intersect(clip, ToScreen(styleClip.Value));
            if (!matrix.isIdentity) p = matrix.MultiplyPoint(p);
            if (Canvas is IClippingGuiCanvas clipping)
            {
                // US-T54: the canvas cuts the run at the clip, through the glyphs, as Unity does.
                if (p.y >= clip.yMax || p.y + lineHeight <= clip.yMin || p.x >= clip.xMax) return;
                if (p.x + Canvas.Measure(text, size, style) <= clip.xMin) return;
                clipping.SetClip(clips.Count > 0 || styleClip.HasValue ? clip : null);
                Canvas.DrawText(text, p, size, style, tint);
                ImguiHost.Stats.Texts++;
                return;
            }
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
            style ??= GUIStyle.none;
            bool focused = GUIUtility.keyboardControl == id;
            var ed = TextEditing.For(id, text, focused);
            switch (e.GetTypeForControl(id))
            {
                case EventType.MouseDown:
                    if (Hit(position))
                    {
                        GUIUtility.hotControl = id;
                        GUIUtility.keyboardControl = id;
                        ImguiHost.FocusTaken = true;
                        // Unity's TextEditor: the click puts the cursor at the nearest character boundary; a double click
                        // selects the word, a triple click the whole line; shift-click extends the selection.
                        int at = style.TextFieldIndexAt(position, text, ed.Scroll, e.mousePosition.x);
                        if (e.clickCount == 2) ed.SelectWord(text, at);
                        else if (e.clickCount >= 3) ed.SelectAll(text);
                        else if (e.shift) ed.Cursor = at;
                        else ed.Cursor = ed.Select = at;
                        ed.DragWords = e.clickCount == 2;
                        e.Use();
                    }
                    break;
                case EventType.MouseDrag:
                    if (GUIUtility.hotControl == id)
                    {
                        int at = style.TextFieldIndexAt(position, text, ed.Scroll, e.mousePosition.x);
                        if (ed.DragWords) ed.ExtendToWord(text, at);
                        else ed.Cursor = at;
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
                    if (ed.HandleKey(e, ref text, maxLength))
                    {
                        e.Use();
                        if (ed.Changed) changed = true;
                    }
                    break;
                case EventType.Repaint:
                {
                    var state = style.StateFor(Hover(position, id), GUIUtility.hotControl == id, false, focused);
                    if (state.background != null) DrawSliced(style.overflow.Add(position), state.background, style.border, BackgroundTint());
                    if (focused) style.DrawTextFieldContent(position, text, state.textColor, ed);
                    else style.DrawContent(position, GUIContent.Temp(text), state.textColor);
                    break;
                }
            }
            return text;
        }

        /// <summary>Unity's clipboard (GUIUtility.systemCopyBuffer): the Windows clipboard, else a process-local one.</summary>
        internal static class Clipboard
        {
            static string local = "";

            [System.Runtime.InteropServices.DllImport("user32.dll")] static extern bool OpenClipboard(IntPtr owner);
            [System.Runtime.InteropServices.DllImport("user32.dll")] static extern bool CloseClipboard();
            [System.Runtime.InteropServices.DllImport("user32.dll")] static extern bool EmptyClipboard();
            [System.Runtime.InteropServices.DllImport("user32.dll")] static extern IntPtr GetClipboardData(uint format);
            [System.Runtime.InteropServices.DllImport("user32.dll")] static extern IntPtr SetClipboardData(uint format, IntPtr mem);
            [System.Runtime.InteropServices.DllImport("kernel32.dll")] static extern IntPtr GlobalAlloc(uint flags, UIntPtr bytes);
            [System.Runtime.InteropServices.DllImport("kernel32.dll")] static extern IntPtr GlobalLock(IntPtr mem);
            [System.Runtime.InteropServices.DllImport("kernel32.dll")] static extern bool GlobalUnlock(IntPtr mem);
            const uint UnicodeText = 13;

            public static string Text
            {
                get
                {
                    if (!OperatingSystem.IsWindows()) return local;
                    try
                    {
                        if (!OpenClipboard(IntPtr.Zero)) return local;
                        try
                        {
                            var h = GetClipboardData(UnicodeText);
                            if (h == IntPtr.Zero) return "";
                            var p = GlobalLock(h);
                            try { return System.Runtime.InteropServices.Marshal.PtrToStringUni(p) ?? ""; }
                            finally { GlobalUnlock(h); }
                        }
                        finally { CloseClipboard(); }
                    }
                    catch { return local; }
                }
                set
                {
                    local = value ?? "";
                    if (!OperatingSystem.IsWindows()) return;
                    try
                    {
                        if (!OpenClipboard(IntPtr.Zero)) return;
                        try
                        {
                            EmptyClipboard();
                            var bytes = (local.Length + 1) * 2;
                            var h = GlobalAlloc(0x0002 /* GMEM_MOVEABLE */, (UIntPtr)bytes);
                            var p = GlobalLock(h);
                            System.Runtime.InteropServices.Marshal.Copy((local + "\0").ToCharArray(), 0, p, local.Length + 1);
                            GlobalUnlock(h);
                            SetClipboardData(UnicodeText, h);
                        }
                        finally { CloseClipboard(); }
                    }
                    catch { }
                }
            }
        }

        /// <summary>
        /// Unity's TextEditor for the single-line GUI.TextField: a cursor and a selection anchor (select) per focused
        /// control, scroll offset keeping the cursor in view, and its Windows key map: Left/Right (Ctrl: by word),
        /// Home/End (Up/Down in one line likewise), Shift extends the selection, Backspace/Delete (Ctrl: a word),
        /// Ctrl+A/C/X/V; typing replaces the selection; a field gaining focus from the keyboard selects all.
        /// </summary>
        internal sealed class TextEditing
        {
            static TextEditing current;
            public int Id, Cursor, Select;
            public float Scroll;
            public bool DragWords, Changed;

            public static TextEditing For(int id, string text, bool focused)
            {
                if (current == null || current.Id != id)
                {
                    if (!focused) return Scratch;
                    current = new TextEditing { Id = id };
                    current.SelectAll(text); // TextEditor.OnFocus (single line): select all
                }
                current.Cursor = Math.Clamp(current.Cursor, 0, text.Length);
                current.Select = Math.Clamp(current.Select, 0, text.Length);
                return current;
            }

            static readonly TextEditing Scratch = new TextEditing { Id = -1 };

            public bool HasSelection => Cursor != Select;
            int Min => Math.Min(Cursor, Select);
            int Max => Math.Max(Cursor, Select);

            public void SelectAll(string text) { Select = 0; Cursor = text.Length; }

            static bool IsWord(char c) => char.IsLetterOrDigit(c) || c == '_';

            static int WordStart(string t, int i)
            {
                while (i > 0 && !IsWord(t[i - 1])) i--;
                while (i > 0 && IsWord(t[i - 1])) i--;
                return i;
            }

            static int WordEnd(string t, int i)
            {
                while (i < t.Length && !IsWord(t[i])) i++;
                while (i < t.Length && IsWord(t[i])) i++;
                return i;
            }

            public void SelectWord(string t, int at)
            {
                int s = at, e = at;
                if (at < t.Length && IsWord(t[at]) || at > 0 && IsWord(t[at - 1]))
                {
                    while (s > 0 && IsWord(t[s - 1])) s--;
                    while (e < t.Length && IsWord(t[e])) e++;
                }
                else if (at < t.Length) e = at + 1;
                Select = s;
                Cursor = e;
            }

            public void ExtendToWord(string t, int at)
            {
                int anchor = Select;
                if (at >= anchor) { Cursor = at < t.Length && IsWord(t[Math.Max(0, at - 1)]) ? WordEnd(t, at) : at; }
                else Cursor = WordStart(t, at);
            }

            string Delete(string t, int from, int to)
            {
                Changed = true;
                Cursor = Select = from;
                return t.Remove(from, to - from);
            }

            string Insert(string t, string s, int maxLength)
            {
                if (HasSelection) t = Delete(t, Min, Max);
                if (maxLength >= 0 && t.Length + s.Length > maxLength) s = s.Substring(0, Math.Max(0, maxLength - t.Length));
                if (s.Length == 0) return t;
                Changed = true;
                t = t.Insert(Cursor, s);
                Cursor = Select = Cursor + s.Length;
                return t;
            }

            /// <returns>Whether the key was the field's (the event is then used).</returns>
            public bool HandleKey(Event e, ref string text, int maxLength)
            {
                Changed = false;
                bool shift = e.shift, ctrl = e.control || e.command;
                void Move(int to) { Cursor = to; if (!shift) Select = to; }
                switch (e.keyCode)
                {
                    case KeyCode.LeftArrow:
                        if (!shift && !ctrl && HasSelection) Move(Min);
                        else Move(ctrl ? WordStart(text, Cursor) : Math.Max(0, Cursor - 1));
                        return true;
                    case KeyCode.RightArrow:
                        if (!shift && !ctrl && HasSelection) Move(Max);
                        else Move(ctrl ? WordEnd(text, Cursor) : Math.Min(text.Length, Cursor + 1));
                        return true;
                    case KeyCode.Home:
                    case KeyCode.UpArrow:
                        Move(0);
                        return true;
                    case KeyCode.End:
                    case KeyCode.DownArrow:
                        Move(text.Length);
                        return true;
                    case KeyCode.Backspace:
                        if (HasSelection) text = Delete(text, Min, Max);
                        else if (Cursor > 0) text = Delete(text, ctrl ? WordStart(text, Cursor) : Cursor - 1, Cursor);
                        return true;
                    case KeyCode.Delete:
                        if (HasSelection) text = Delete(text, Min, Max);
                        else if (Cursor < text.Length) text = Delete(text, Cursor, ctrl ? WordEnd(text, Cursor) : Cursor + 1);
                        return true;
                }
                if (ctrl)
                {
                    switch (e.keyCode)
                    {
                        case KeyCode.A: SelectAll(text); return true;
                        case KeyCode.C: if (HasSelection) Clipboard.Text = text.Substring(Min, Max - Min); return true;
                        case KeyCode.X:
                            if (HasSelection) { Clipboard.Text = text.Substring(Min, Max - Min); text = Delete(text, Min, Max); }
                            return true;
                        case KeyCode.V:
                        {
                            var paste = Clipboard.Text.Replace("\r", "").Replace("\n", "");
                            text = Insert(text, paste, maxLength);
                            return true;
                        }
                    }
                    return false;
                }
                // Single line: Return and Tab are not typed (the game sees them).
                if (e.character >= ' ' && e.character != '\u007f')
                {
                    text = Insert(text, e.character.ToString(), maxLength);
                    return true;
                }
                return false;
            }
        }

        // ---------------------------------------------------------------- HorizontalSlider

        static float sliderGrab;

        public static float HorizontalSlider(Rect position, float value, float leftValue, float rightValue) =>
            HorizontalSlider(position, value, leftValue, rightValue, skin.horizontalSlider, skin.horizontalSliderThumb);

        public static float HorizontalSlider(Rect position, float value, float leftValue, float rightValue, GUIStyle slider, GUIStyle thumb)
        {
            int id = GUIUtility.GetControlID(FocusType.Passive);
            // Unity's SliderHandler (US-T57): the thumb is ThumbSize wide (the thumb's fixed width, else its horizontal
            // padding) inside the track's padding, at full height less that padding; the styles' overflows shape both.
            float thumbW = thumb.fixedWidth != 0 ? thumb.fixedWidth : thumb.padding.horizontal;
            float span = Math.Max(1f, position.width - slider.padding.horizontal - thumbW);
            float range = rightValue - leftValue;
            float t = range == 0 ? 0 : Math.Clamp((value - leftValue) / range, 0f, 1f);
            var thumbRect = new Rect(position.x + slider.padding.left + t * span, position.y + slider.padding.top, thumbW,
                position.height - slider.padding.vertical);
            var e = Event.current;
            switch (e.GetTypeForControl(id))
            {
                case EventType.MouseDown:
                    if (Hit(position))
                    {
                        GUIUtility.hotControl = id;
                        sliderGrab = thumbRect.Contains(e.mousePosition) ? e.mousePosition.x - thumbRect.x : thumbW * 0.5f;
                        value = leftValue + Math.Clamp((e.mousePosition.x - sliderGrab - position.x - slider.padding.left) / span, 0f, 1f) * range;
                        changed = true;
                        e.Use();
                    }
                    break;
                case EventType.MouseDrag:
                    if (GUIUtility.hotControl == id)
                    {
                        value = leftValue + Math.Clamp((e.mousePosition.x - sliderGrab - position.x - slider.padding.left) / span, 0f, 1f) * range;
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
                    slider.Draw(position, false, false, false, false);
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
            // Unity's GUI.Scroller -> SliderHandler (US-T57): inside the track's padding, the thumb is ThumbSize (the
            // thumb's fixed length, else its padding along the bar) plus the visible part at the track's pixels per value.
            float pad = vertical ? track.padding.vertical : track.padding.horizontal;
            float padStart = vertical ? track.padding.top : track.padding.left;
            float thumbSize = vertical ? (thumb.fixedHeight != 0 ? thumb.fixedHeight : thumb.padding.vertical)
                : (thumb.fixedWidth != 0 ? thumb.fixedWidth : thumb.padding.horizontal);
            float length = vertical ? bar.height : bar.width;
            float max = Math.Max(0f, total - visible);
            value = Math.Clamp(value, 0, max);
            float perValue = (length - pad - thumbSize) / Math.Max(1e-6f, total);
            float thumbLen = Math.Min(visible, total) * perValue + thumbSize;
            float at = padStart + value * perValue;
            var thumbRect = vertical
                ? new Rect(bar.x + track.padding.left, bar.y + at, bar.width - track.padding.horizontal, thumbLen)
                : new Rect(bar.x + at, bar.y + track.padding.top, thumbLen, bar.height - track.padding.vertical);
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
                        value = Math.Clamp((Along(e.mousePosition) - state.Grab - padStart) / Math.Max(1e-6f, perValue), 0, max);
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
                    thumb.Draw(thumbRect, false, false, false, false);
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
