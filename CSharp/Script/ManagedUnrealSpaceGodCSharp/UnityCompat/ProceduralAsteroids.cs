using System;
using System.Threading.Tasks;

namespace UnityEngine
{
    /// <summary>
    /// AsteroidBelt's Graphics.RenderPrimitives (Asteroids.shader: ~111,000 orbits propagated per vertex from a
    /// StructuredBuffer) on Unreal. A material cannot read a buffer, and UProceduralMeshComponent keeps UVs at half
    /// precision, so SpaceGodRender's M_Asteroids (a float texture indexed by a full-precision TexCoord0) cannot be fed
    /// from C# through UnrealSharp. Instead the shader's own maths (Kepler's equation by 5 Newton steps, the Sun's
    /// realistic/gameplay distance mapping, the brightness ramp by H and the inside-the-belt dimming) runs here on a
    /// worker thread whenever the elements' time or the mapping moves, and the belt is drawn as one point cloud with
    /// M_Points, its transform placed at _SunRelative every frame (so camera motion costs nothing).
    /// Not exact: every asteroid is one pixel (Unity: 1 px at H 15.5 up to ~3 px for Vesta, with a soft round dot),
    /// and positions refresh at most every <see cref="RefreshSeconds"/> instead of every frame.
    /// </summary>
    internal sealed class ProceduralAsteroids
    {
        public const float RefreshSeconds = 0.25f;

        static readonly Color DefaultColor = new Color(0.82f, 0.78f, 0.72f, 1);

        readonly GameObject go;
        readonly Mesh mesh;
        readonly MeshRenderer renderer;
        Task<(Vector3[] v, Color[] c)>? pending;
        float lastStart = -1e9f;
        Vector4 lastKey;
        bool built;

        ProceduralAsteroids()
        {
            go = new GameObject("AsteroidBelt (procedural)");
            mesh = new Mesh { name = "Asteroids" };
            go.AddComponent<MeshFilter>().sharedMesh = mesh;
            renderer = go.AddComponent<MeshRenderer>();
            renderer.shadowCastingMode = Rendering.ShadowCastingMode.Off;
            renderer.receiveShadows = false;
            var points = new Material(Shader.Find("Unity.SpaceGod/Points")) { name = "Asteroids" };
            points.SetFloat("_Brightness", 1f);
            renderer.sharedMaterial = points;
        }

        static ProceduralAsteroids? instance;

        internal static void Draw(RenderParams rp, MeshTopology topology, int vertexCount)
        {
            var m = rp.material;
            if (!m.Buffers.TryGetValue("_Asteroids", out var buffer) || buffer.Data.Length == 0) return;
            instance ??= new ProceduralAsteroids();
            instance.Frame(m, buffer.Data);
        }

        int drawnFrame = -1;

        /// <summary>Unity draws a procedural call only on the frames it is made: hidden on any frame AsteroidBelt.Render
        /// did not run (UnityBridge.Sync).</summary>
        internal static void EndFrame()
        {
            if (instance != null && instance.drawnFrame != Time.frameCount) instance.renderer.enabled = false;
        }

        static float Linear(float c) => c <= 0.04045f ? c / 12.92f : MathF.Pow((c + 0.055f) / 1.055f, 2.4f);

        void Frame(Material m, float[] data)
        {
            drawnFrame = Time.frameCount;
            var sunRelative = (Vector3)m.GetVector("_SunRelative");
            go.transform.localPosition = sunRelative;
            renderer.enabled = true;

            if (pending != null && pending.IsCompleted)
            {
                if (pending.Status == TaskStatus.RanToCompletion)
                {
                    var (v, c) = pending.Result;
                    var indices = new int[v.Length];
                    for (int i = 0; i < indices.Length; i++) indices[i] = i;
                    mesh.Clear();
                    mesh.vertices = v;
                    mesh.colors = c;
                    mesh.SetIndices(indices, MeshTopology.Points, 0, false);
                    mesh.bounds = new Bounds(Vector3.zero, Vector3.one * 2e9f);
                    built = true;
                }
                pending = null;
            }

            float dt = m.GetFloat("_DtDays");
            var map0 = m.GetVector("_SunMap0");
            var map1 = m.GetVector("_SunMap1");
            var key = new Vector4(dt, map0.x, map0.z, map1.x);
            bool moved = !built || key != lastKey;
            if (pending == null && moved && Time.unscaledTime - lastStart >= RefreshSeconds)
            {
                lastStart = Time.unscaledTime;
                lastKey = key;
                // _Color is a colour property (Unity converts it to linear); vertex colours reach M_Points raw.
                var srgb = m.Vectors.ContainsKey("_Color") ? m.GetColor("_Color") : DefaultColor;
                var color = new Color(Linear(srgb.r), Linear(srgb.g), Linear(srgb.b), srgb.a);
                float brightness = m.Floats.ContainsKey("_Brightness") ? m.GetFloat("_Brightness") : 1f;
                float cameraFromSun = sunRelative.magnitude;
                pending = Task.Run(() => Propagate(data, dt, map0, map1, cameraFromSun, color, brightness));
            }
        }

        static float Smoothstep(float a, float b, float x)
        {
            float t = Mathf.Clamp01((x - a) / (b - a));
            return t * t * (3 - 2 * t);
        }

        /// <summary>Asteroids.hlsl's vertex maths, per asteroid: positions relative to the Sun (Unity axes) and colours.</summary>
        static (Vector3[] v, Color[] c) Propagate(float[] k, float dtDays, Vector4 map0, Vector4 map1, float cameraFromSun, Color color, float brightness)
        {
            int n = k.Length / 12;
            var v = new Vector3[n];
            var c = new Color[n];
            Parallel.For(0, n, id =>
            {
                int o = id * 12;
                float e = k[o], meanMotion = k[o + 1], m0 = k[o + 2], h = k[o + 3];
                float m = m0 + meanMotion * dtDays;
                m -= 6.2831853f * MathF.Floor(m / 6.2831853f + 0.5f);
                float ecc = m + e * MathF.Sin(m);
                for (int it = 0; it < 5; it++) ecc -= (ecc - e * MathF.Sin(ecc) - m) / (1f - e * MathF.Cos(ecc));
                float cosE = MathF.Cos(ecc), sinE = MathF.Sin(ecc);
                float ax = k[o + 4] * (cosE - e) + k[o + 8] * sinE;
                float ay = k[o + 5] * (cosE - e) + k[o + 9] * sinE;
                float az = k[o + 6] * (cosE - e) + k[o + 10] * sinE;
                float d = MathF.Max(MathF.Sqrt(ax * ax + ay * ay + az * az), 1e-4f);
                float trueScale = d * map0.y;
                float gameScale = map0.z + map0.w * (MathF.Pow(d, map1.x) - map1.y);
                float scene = MathF.Exp(Mathf.LerpUnclamped(MathF.Log(trueScale), MathF.Log(gameScale), map0.x));
                // ecliptic -> Unity axes
                v[id] = new Vector3(ax, az, ay) * (scene / d);
                float b = brightness * Mathf.LerpUnclamped(0.9f, 0.16f, Mathf.Clamp01((h - 5f) / 10.5f));
                b *= Mathf.LerpUnclamped(0.2f, 1f, Smoothstep(0.5f, 1.1f, cameraFromSun / scene));
                c[id] = new Color(color.r, color.g, color.b, b);
            });
            return (v, c);
        }
    }
}
