using System;
using System.Collections.Generic;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;
using UnrealSharp.SlateCore;

namespace UnityEngine
{
    /// <summary>
    /// The headless canvas (-nullrhi, the smoke run, and before the first HUD frame): it draws nothing and measures
    /// text with Arial's advance widths, so layout code that sizes panels by their text runs as it does on screen.
    /// </summary>
    public sealed class MeasuringCanvas : IGuiCanvas
    {
        // Arial's advance widths, per 1000 em, for ' ' (32) to '~' (126).
        static readonly short[] Advance =
        {
            278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
            556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
            1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
            667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
            333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
            556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584,
        };

        public float Measure(string text, int size, FontStyle style)
        {
            if (string.IsNullOrEmpty(text)) return 0;
            float units = 0;
            foreach (char c in text) units += c >= 32 && c <= 126 ? Advance[c - 32] : 556;
            float bold = style == FontStyle.Bold || style == FontStyle.BoldAndItalic ? 1.06f : 1f;
            return units * size / 1000f * bold;
        }

        public float LineHeight(int size, FontStyle style) => MathF.Ceiling(size * 1.15f);
        public void DrawTexture(Texture texture, Rect screen, Rect uv, Color tint, float rotation) { }
        public void DrawText(string text, Vector2 screen, int size, FontStyle style, Color tint) { }
    }

    /// <summary>
    /// The GUI on an <see cref="AHUD"/>'s Canvas, during its DrawHUD: textures as tiles (translucent, tinted, rotated
    /// about their centre), text with Arial loaded from the system font folder (what Unity's built-in font is on
    /// Windows), in its regular, bold, italic and bold-italic faces; Roboto (the engine's font) where Arial is not
    /// installed. Unity's colours are gamma (sRGB) values; Canvas takes linear ones, so each is converted.
    /// </summary>
    public sealed class HudCanvas : IGuiCanvas
    {
        // Canvas rasterises a runtime font's glyphs at its LegacyFontSize and scales the quads (FCanvasTextItem), so
        // one 16 pt font drawn at every size blurred big text (the menu's 40 px title read as a soft glow) and thinned
        // small text. Each face is kept per point size instead, drawn at a scale near 1 (US-T45), as the C++ track's
        // Slate text is at its exact size.
        const int FallbackBaseSize = 16; // Roboto's LegacyFontSize (the engine asset's): every size a scale of it

        readonly AHUD hud;
        readonly Action<UObject> keep;
        readonly HashSet<UTexture> kept = new HashSet<UTexture>();
        readonly UFont[] fonts = new UFont[4];
        readonly string?[] faceFiles = new string?[4];
        readonly Dictionary<(int, int), UFont> sized = new Dictionary<(int, int), UFont>();
        readonly Dictionary<(string, int, int), float> widths = new Dictionary<(string, int, int), float>();
        readonly Dictionary<(int, int), float> lineHeights = new Dictionary<(int, int), float>();

        /// <summary>Which font file each face came from (for the log).</summary>
        public string FontSource { get; private set; } = "";

        public HudCanvas(AHUD hud, Action<UObject> keep)
        {
            this.hud = hud;
            this.keep = keep;
            string fontsDir = Environment.GetFolderPath(Environment.SpecialFolder.Fonts);
            string[] files = { "arial.ttf", "arialbd.ttf", "ariali.ttf", "arialbi.ttf" };
            UFont fallback = null;
            for (int i = 0; i < 4; i++)
            {
                string path = string.IsNullOrEmpty(fontsDir) ? "" : System.IO.Path.Combine(fontsDir, files[i]);
                if (System.IO.File.Exists(path))
                {
                    faceFiles[i] = path;
                    fonts[i] = FromFile(path, hud, FallbackBaseSize);
                    keep(fonts[i]);
                    if (i == 0) FontSource = path;
                }
                else
                {
                    fallback ??= UObject.StaticLoadObject<UFont>("/Engine/EngineFonts/Roboto.Roboto");
                    fonts[i] = fallback;
                    if (i == 0) FontSource = "/Engine/EngineFonts/Roboto (no Arial in " + fontsDir + ")";
                }
            }
        }

        /// <summary>A runtime-cached UFont with one typeface read from a font file.</summary>
        static UFont FromFile(string path, UObject outer, int points)
        {
            var font = UObject.NewObject<UFont>(outer);
            font.FontCacheType = EFontCacheType.Runtime;
            font.LegacyFontSize = points;
            var data = new FFontData(path.Replace('\\', '/'), EFontHinting.Default, EFontLoadingPolicy.LazyLoad, 0, null!, null!, new List<byte>());
            var composite = font.CompositeFont;
            composite.DefaultTypeface = new FTypeface(new List<FTypefaceEntry> { new FTypefaceEntry(new UnrealSharp.Core.FName("Regular"), data) });
            font.CompositeFont = composite;
            return font;
        }

        /// <summary>Unity sizes are pixels; Slate's points are 96 DPI.</summary>
        static float Points(int size) => size * 0.75f;

        /// <summary>The whole point size a face is rasterised at for a Unity pixel size.</summary>
        static int BasePoints(int size) => Math.Max(1, (int)MathF.Round(Points(size)));

        int Face(FontStyle style) => faceFiles[(int)style] != null ? (int)style : 0;

        UFont FontFor(FontStyle style, int size)
        {
            int face = Face(style);
            string? file = faceFiles[face];
            if (file == null) return fonts[(int)style] ?? fonts[0]; // Roboto: one size, scaled
            var key = (face, BasePoints(size));
            if (!sized.TryGetValue(key, out var font))
            {
                font = FromFile(file, hud, key.Item2);
                keep(font);
                sized[key] = font;
            }
            return font;
        }

        float Scale(int size, FontStyle style) => Points(size) / (faceFiles[Face(style)] != null ? BasePoints(size) : FallbackBaseSize);

        public float Measure(string text, int size, FontStyle style)
        {
            if (string.IsNullOrEmpty(text)) return 0;
            var key = (text, size, (int)style);
            if (widths.TryGetValue(key, out float w)) return w;
            hud.GetTextSize(text, out w, out float h, FontFor(style, size), Scale(size, style));
            if (widths.Count > 20000) widths.Clear();
            widths[key] = w;
            return w;
        }

        public float LineHeight(int size, FontStyle style)
        {
            var key = (size, (int)style);
            if (lineHeights.TryGetValue(key, out float h)) return h;
            hud.GetTextSize("Hg", out _, out h, FontFor(style, size), Scale(size, style));
            if (h <= 0) h = MathF.Ceiling(size * 1.15f);
            lineHeights[key] = h;
            return h;
        }

        static float Linear(float c) => c <= 0.04045f ? c / 12.92f : MathF.Pow((c + 0.055f) / 1.055f, 2.4f);

        /// <summary>A Unity (gamma) colour as the linear colour Canvas expects.</summary>
        public static FLinearColor ToLinear(Color c) => new FLinearColor(Linear(Math.Clamp(c.r, 0, 1)), Linear(Math.Clamp(c.g, 0, 1)), Linear(Math.Clamp(c.b, 0, 1)), Math.Clamp(c.a, 0, 1));

        public void DrawTexture(Texture texture, Rect screen, Rect uv, Color tint, float rotation)
        {
            var native = texture.Unreal;
            if (native == null || native.IsDestroyed) return;
            if (kept.Add(native)) keep(native);
            hud.DrawTexture(native, screen.x, screen.y, screen.width, screen.height, uv.x, uv.y, uv.width, uv.height,
                ToLinear(tint), EBlendMode.BLEND_Translucent, 1f, false, rotation, new FVector2D(0.5, 0.5));
        }

        public void DrawText(string text, Vector2 screen, int size, FontStyle style, Color tint) =>
            hud.DrawText(text, ToLinear(tint), screen.x, screen.y, FontFor(style, size), Scale(size, style), false);
    }
}
