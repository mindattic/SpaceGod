using System;
using System.Collections.Generic;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;

namespace UnityEngine
{
    /// <summary>A Unity shader, by name ("Unity.SpaceGod/Planet"); ShaderBinding finds the Unreal material for it.</summary>
    public sealed class Shader : Object
    {
        static readonly Dictionary<string, Shader> ByName = new Dictionary<string, Shader>(StringComparer.Ordinal);
        static readonly Dictionary<string, int> Ids = new Dictionary<string, int>(StringComparer.Ordinal);
        static readonly List<string> Names = new List<string>();

        Shader(string name) { this.name = name; }

        public static Shader Find(string name)
        {
            if (!ByName.TryGetValue(name, out var s)) ByName[name] = s = new Shader(name);
            return s;
        }

        public static int PropertyToID(string name)
        {
            if (Ids.TryGetValue(name, out int id)) return id;
            id = Names.Count;
            Names.Add(name);
            Ids[name] = id;
            return id;
        }
        internal static string NameOf(int id) => Names[id];
        public bool isSupported => true;

        // Shader globals (the star lighting every hand-written shader reads): Unreal side a material parameter
        // collection, see ShaderBinding.GlobalsCollection; UnityBridge pushes what changed once a frame.
        internal static readonly Dictionary<string, Vector4> GlobalVectors = new Dictionary<string, Vector4>(StringComparer.Ordinal);
        internal static readonly Dictionary<string, float> GlobalFloats = new Dictionary<string, float>(StringComparer.Ordinal);
        internal static int globalsVersion;
        public static void SetGlobalVector(string n, Vector4 v) { GlobalVectors[n] = v; GlobalColorNames.Remove(n); globalsVersion++; }
        public static void SetGlobalVector(int id, Vector4 v) => SetGlobalVector(NameOf(id), v);
        internal static readonly HashSet<string> GlobalColorNames = new HashSet<string>(StringComparer.Ordinal);
        public static void SetGlobalColor(string n, Color c) { SetGlobalVector(n, c); GlobalColorNames.Add(n); }
        public static void SetGlobalColor(int id, Color c) => SetGlobalColor(NameOf(id), c);
        public static void SetGlobalFloat(string n, float v) { GlobalFloats[n] = v; globalsVersion++; }
        public static void SetGlobalFloat(int id, float v) => SetGlobalFloat(NameOf(id), v);
        public static Vector4 GetGlobalVector(string n) => GlobalVectors.TryGetValue(n, out var v) ? v : Vector4.zero;
        public static Vector4 GetGlobalVector(int id) => GetGlobalVector(NameOf(id));
        public static float GetGlobalFloat(string n) => GlobalFloats.TryGetValue(n, out var v) ? v : 0f;
        public static float GetGlobalFloat(int id) => GetGlobalFloat(NameOf(id));
    }

    public enum TextureWrapMode { Repeat, Clamp, Mirror, MirrorOnce }
    public enum FilterMode { Point, Bilinear, Trilinear }
    public enum TextureFormat { Alpha8 = 1, RGB24 = 3, RGBA32 = 4, ARGB32 = 5, RGBAFloat = 20, RGBAHalf = 17, BGRA32 = 14 }

    public class Texture : Object
    {
        public virtual int width { get; protected set; }
        public virtual int height { get; protected set; }
        public TextureWrapMode wrapMode { get; set; }
        public TextureWrapMode wrapModeU { get; set; }
        public TextureWrapMode wrapModeV { get; set; }
        public FilterMode filterMode { get; set; } = FilterMode.Bilinear;
        public int anisoLevel { get; set; } = 1;

        /// <summary>The Unreal texture this is (an imported asset, or one made from pixels on <see cref="Texture2D.Apply()"/>).</summary>
        public UTexture? Unreal { get; internal set; }
    }

    public sealed class Texture2D : Texture
    {
        Color32[]? pixels;
        public TextureFormat format { get; }
        public Texture2D(int width, int height) : this(width, height, TextureFormat.RGBA32, true) { }
        public Texture2D(int width, int height, TextureFormat format, bool mipChain) : this(width, height, format, mipChain ? -1 : 1, false) { }
        public Texture2D(int width, int height, TextureFormat format, bool mipChain, bool linear) : this(width, height, format, mipChain ? -1 : 1, linear) { }
        public Texture2D(int width, int height, TextureFormat format, int mipCount, bool linear)
        {
            this.width = width;
            this.height = height;
            this.format = format;
            mipChain = mipCount < 0;
            this.linear = linear;
        }

        // US-T54: what Apply uploads (Unity's full mip chain when asked for one; linear: not sRGB).
        readonly bool mipChain, linear;

        internal static Texture2D FromUnreal(UTexture2D texture, string name)
        {
            var t = new Texture2D(1, 1) { name = name, Unreal = texture };
            try { t.width = texture.SizeX; t.height = texture.SizeY; } catch { }
            return t;
        }

        Color32[] Pixels => pixels ??= new Color32[Math.Max(1, width * height)];

        static Texture2D? white, black;
        static Texture2D Solid(Color c)
        {
            var t = new Texture2D(4, 4, TextureFormat.RGBA32, false) { name = "Default", hideFlags = HideFlags.DontSave };
            for (int i = 0; i < 16; i++) t.Pixels[i] = c;
            t.Apply(false, false);
            return t;
        }
        /// <summary>Unity's built-in white and black textures (made on first use).</summary>
        public static Texture2D whiteTexture => white != null && white.Unreal != null ? white : white = Solid(Color.white);
        public static Texture2D blackTexture => black != null && black.Unreal != null ? black : black = Solid(Color.black);
        public Color32[] GetPixels32() => (Color32[])Pixels.Clone();
        public Color[] GetPixels()
        {
            var p = Pixels;
            var r = new Color[p.Length];
            for (int i = 0; i < p.Length; i++) r[i] = p[i];
            return r;
        }
        public void LoadRawTextureData(byte[] data) => SetPixelData(data, 0);
        public bool LoadImage(byte[] data)
        {
            if (!UnityBridge.HasHost) return false;
            try
            {
                var t = UnrealSharp.Engine.RenderingLibrary.ImportBufferAsTexture2D(data);
                if (t == null) return false;
                Unreal = t;
                width = t.SizeX;
                height = t.SizeY;
                return true;
            }
            catch { return false; }
        }
        public void SetPixel(int x, int y, Color c) => Pixels[y * width + x] = c;
        public Color GetPixel(int x, int y) => Pixels[y * width + x];
        public void SetPixels(Color[] colors)
        {
            var p = Pixels;
            for (int i = 0; i < p.Length && i < colors.Length; i++) p[i] = colors[i];
        }
        public void SetPixels32(Color32[] colors) => Array.Copy(colors, Pixels, Math.Min(colors.Length, Pixels.Length));

        /// <summary>Raw RGBA32 bytes of one mip (only mip 0 is uploaded: Unreal makes its own chain).</summary>
        public void SetPixelData(byte[] data, int mipLevel, int sourceDataStartIndex = 0)
        {
            if (mipLevel != 0) return;
            var p = Pixels;
            for (int i = 0; i < p.Length; i++)
            {
                int o = sourceDataStartIndex + i * 4;
                if (o + 3 >= data.Length) break;
                p[i] = new Color32(data[o], data[o + 1], data[o + 2], data[o + 3]);
            }
        }

        public void Apply() => Apply(true, false);
        public void Apply(bool updateMipmaps, bool makeNoLongerReadable = false)
        {
            if (pixels == null) return;
            var made = UnityBridge.TextureFromPixels(width, height, pixels);
            Unreal = made;
            // US-T54: the import makes one level with the default sampler; a texture with Unity's mip chain (the icons,
            // drawn at 10-30 px from 64, the skin) or point filtering is re-uploaded as Unity's Apply makes it -- the box-filtered
            // chain down to 1x1, its filter and wrap -- as the C++ track's GuiHost uploads it, so it minifies as the original.
            if (made != null && width * height > 1 && (mipChain || filterMode == FilterMode.Point) && SlateGui.Available)
                SlateGui.Upload(made.NativeObject, pixels, width, height, mipChain, filterMode, wrapMode, linear);
            if (makeNoLongerReadable) pixels = null;
        }
    }

    /// <summary>
    /// A Unity material: a shader plus property values. Unreal side, one UMaterialInstanceDynamic of the shader's
    /// SpaceGodRender material (see <see cref="ShaderBinding"/>), its parameters pushed by UnityBridge when they change.
    /// </summary>
    public class Material : Object
    {
        public Shader shader { get; set; }
        public int renderQueue { get; set; } = -1;
        internal readonly Dictionary<string, float> Floats = new Dictionary<string, float>(StringComparer.Ordinal);
        internal readonly Dictionary<string, Vector4> Vectors = new Dictionary<string, Vector4>(StringComparer.Ordinal);
        internal readonly Dictionary<string, Texture?> Textures = new Dictionary<string, Texture?>(StringComparer.Ordinal);
        internal readonly Dictionary<string, float[]> Arrays = new Dictionary<string, float[]>(StringComparer.Ordinal);
        internal readonly Dictionary<string, ComputeData> Buffers = new Dictionary<string, ComputeData>(StringComparer.Ordinal);
        /// <summary>Vectors set as colours (SetColor): Unity, a linear-space project, converts those sRGB -> linear.</summary>
        internal readonly HashSet<string> ColorNames = new HashSet<string>(StringComparer.Ordinal);
        internal int version;
        internal int pushedVersion = -1;
        internal UMaterialInstanceDynamic? Instance;

        public Material(Shader shader) { this.shader = shader; name = shader?.name ?? ""; }
        public Material(Material source)
        {
            shader = source.shader;
            name = source.name;
            renderQueue = source.renderQueue;
            foreach (var kv in source.Floats) Floats[kv.Key] = kv.Value;
            foreach (var kv in source.Vectors) Vectors[kv.Key] = kv.Value;
            foreach (var kv in source.Textures) Textures[kv.Key] = kv.Value;
            foreach (var kv in source.Arrays) Arrays[kv.Key] = (float[])kv.Value.Clone();
            foreach (var n in source.ColorNames) ColorNames.Add(n);
        }

        void Touch() => version++;
        public Color color { get => GetColor("_Color"); set => SetColor("_Color", value); }
        public Texture? mainTexture { get => GetTexture("_MainTex"); set => SetTexture("_MainTex", value); }

        public void SetFloat(string n, float v) { Floats[n] = v; Touch(); }
        public void SetFloat(int id, float v) => SetFloat(Shader.NameOf(id), v);
        public void SetInt(string n, int v) => SetFloat(n, v);
        public void SetInteger(string n, int v) => SetFloat(n, v);
        public void SetColor(string n, Color c) { Vectors[n] = c; ColorNames.Add(n); Touch(); }
        public void SetColor(int id, Color c) => SetColor(Shader.NameOf(id), c);
        public void SetVector(string n, Vector4 v) { Vectors[n] = v; ColorNames.Remove(n); Touch(); }
        public void SetVector(int id, Vector4 v) => SetVector(Shader.NameOf(id), v);
        public void SetTexture(string n, Texture? t) { Textures[n] = t; Touch(); }
        public void SetTexture(int id, Texture? t) => SetTexture(Shader.NameOf(id), t);
        public void SetFloatArray(string n, float[] values) { Arrays[n] = (float[])values.Clone(); Touch(); }
        public void SetFloatArray(int id, float[] values) => SetFloatArray(Shader.NameOf(id), values);
        public void SetFloatArray(string n, List<float> values) => SetFloatArray(n, values.ToArray());
        public void SetBuffer(string n, GraphicsBuffer buffer) { Buffers[n] = buffer; Touch(); }
        public void EnableKeyword(string k) { }
        public void DisableKeyword(string k) { }

        public float GetFloat(string n) => Floats.TryGetValue(n, out var v) ? v : 0f;
        public float GetFloat(int id) => GetFloat(Shader.NameOf(id));
        public Color GetColor(string n) => Vectors.TryGetValue(n, out var v) ? (Color)v : ShaderBinding.DefaultColor(shader, n);
        public Color GetColor(int id) => GetColor(Shader.NameOf(id));
        public Vector4 GetVector(string n) => Vectors.TryGetValue(n, out var v) ? v : Vector4.zero;
        public Texture? GetTexture(string n) => Textures.TryGetValue(n, out var t) ? t : null;
        public bool HasProperty(string n) => Floats.ContainsKey(n) || Vectors.ContainsKey(n) || Textures.ContainsKey(n) || ShaderBinding.HasProperty(shader, n);
        public bool HasProperty(int id) => HasProperty(Shader.NameOf(id));

        internal override void OnDestroyInternal()
        {
            base.OnDestroyInternal();
            Instance = null;
        }
    }

    /// <summary>Per-renderer overrides on top of its shared material (Unity's MaterialPropertyBlock).</summary>
    public sealed class MaterialPropertyBlock
    {
        internal readonly Dictionary<string, float> Floats = new Dictionary<string, float>(StringComparer.Ordinal);
        internal readonly Dictionary<string, Vector4> Vectors = new Dictionary<string, Vector4>(StringComparer.Ordinal);
        internal readonly Dictionary<string, Texture?> Textures = new Dictionary<string, Texture?>(StringComparer.Ordinal);
        internal readonly HashSet<string> ColorNames = new HashSet<string>(StringComparer.Ordinal);
        public void SetFloat(string n, float v) => Floats[n] = v;
        public void SetFloat(int id, float v) => Floats[Shader.NameOf(id)] = v;
        public void SetColor(string n, Color c) { Vectors[n] = c; ColorNames.Add(n); }
        public void SetColor(int id, Color c) => SetColor(Shader.NameOf(id), c);
        public void SetVector(string n, Vector4 v) { Vectors[n] = v; ColorNames.Remove(n); }
        public void SetVector(int id, Vector4 v) => SetVector(Shader.NameOf(id), v);
        public void SetTexture(string n, Texture t) => Textures[n] = t;
        public void SetTexture(int id, Texture t) => Textures[Shader.NameOf(id)] = t;
        public void Clear() { Floats.Clear(); Vectors.Clear(); Textures.Clear(); ColorNames.Clear(); }
        public bool isEmpty => Floats.Count == 0 && Vectors.Count == 0 && Textures.Count == 0;

        internal MaterialPropertyBlock Copy()
        {
            var b = new MaterialPropertyBlock();
            foreach (var kv in Floats) b.Floats[kv.Key] = kv.Value;
            foreach (var kv in Vectors) b.Vectors[kv.Key] = kv.Value;
            foreach (var kv in Textures) b.Textures[kv.Key] = kv.Value;
            foreach (var n in ColorNames) b.ColorNames.Add(n);
            return b;
        }
        internal bool SameAs(MaterialPropertyBlock? o)
        {
            if (o == null || o.Floats.Count != Floats.Count || o.Vectors.Count != Vectors.Count || o.Textures.Count != Textures.Count) return false;
            foreach (var kv in Floats) if (!o.Floats.TryGetValue(kv.Key, out var v) || v != kv.Value) return false;
            foreach (var kv in Vectors) if (!o.Vectors.TryGetValue(kv.Key, out var v) || !v.Equals(kv.Value)) return false;
            foreach (var kv in Textures) if (!o.Textures.TryGetValue(kv.Key, out var v) || !ReferenceEquals(v, kv.Value)) return false;
            return true;
        }
    }

    /// <summary>Data a material reads per element that Unity keeps in a GPU buffer (AsteroidBelt's orbital elements).</summary>
    public class ComputeData
    {
        internal float[] Data = Array.Empty<float>();
    }

    public sealed class GraphicsBuffer : ComputeData, IDisposable
    {
        public enum Target { Vertex = 1, Index = 2, Structured = 16, Raw = 32 }
        public int count { get; }
        public int stride { get; }
        public GraphicsBuffer(Target target, int count, int stride) { this.count = count; this.stride = stride; }
        public void SetData(float[] data) => Data = (float[])data.Clone();
        public void Release() => Data = Array.Empty<float>();
        public void Dispose() => Release();
    }
}
