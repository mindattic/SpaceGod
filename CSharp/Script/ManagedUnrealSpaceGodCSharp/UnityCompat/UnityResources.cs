using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Threading.Tasks;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;

namespace UnityEngine
{
    public class AsyncOperation
    {
        Action<AsyncOperation>? completedHandlers;
        public bool isDone { get; internal set; }
        public float progress => isDone ? 1f : 0f;
        public bool allowSceneActivation { get; set; } = true;
        public int priority { get; set; }

        /// <summary>As Unity: a handler added after the operation finished runs at once.</summary>
        public event Action<AsyncOperation> completed
        {
            add
            {
                if (isDone) value(this);
                else completedHandlers += value;
            }
            remove => completedHandlers -= value;
        }

        internal void Finish()
        {
            isDone = true;
            var h = completedHandlers;
            completedHandlers = null;
            h?.Invoke(this);
        }
    }

    public sealed class ResourceRequest : AsyncOperation
    {
        public Object? asset { get; internal set; }
    }

    /// <summary>
    /// Unity's Resources folder, served from Unreal content. A Unity path "Textures/earth_color" is the asset
    /// "&lt;root&gt;/Textures/earth_color" under the first root that has it: the shared SpaceGodRender plugin's content
    /// (/SpaceGodRender), then this track's (/Game/SpaceGod). Imported by CSharp/Content/Python/setup_csharp_assets.py
    /// from Unity.SpaceGod's Assets/UnitySpace/Resources. Ship models (Ships/*) are static meshes, wrapped in a
    /// GameObject prototype as Unity's glTF import makes them; binary data (Data/*.bytes, TextAsset) is read as a file
    /// from the shared plugin's Content/Resources/Data (<see cref="DataDirectories"/>).
    /// </summary>
    public static class Resources
    {
        public static readonly string[] Roots = { "/SpaceGodRender/Resources", "/Game/SpaceGod" };
        static readonly Dictionary<UObject, string> loaded = new Dictionary<UObject, string>();

        static string Leaf(string path)
        {
            int s = path.LastIndexOf('/');
            return s >= 0 ? path.Substring(s + 1) : path;
        }

        static IEnumerable<string> Candidates(string path)
        {
            foreach (var r in Roots) yield return $"{r}/{path}.{Leaf(path)}";
        }

        public static T? Load<T>(string path) where T : Object => Load(path, typeof(T)) as T;
        public static Object? Load(string path) => Load(path, typeof(Object));

        public static Object? Load(string path, Type type)
        {
            if (type == typeof(TextAsset)) return LoadBytes(path);
            if (type == typeof(Shader)) return Shader.Find(path);
            if (!UnityBridge.HasHost) return null;
            if (type == typeof(GameObject)) return LoadModel(path);
            foreach (var p in Candidates(path))
            {
                try { return Wrap(UObject.StaticLoadObject<UTexture2D>(p), path); }
                catch (Exception) { }
            }
            return null;
        }

        public static ResourceRequest LoadAsync<T>(string path) where T : Object => LoadAsync(path, typeof(T));
        public static ResourceRequest LoadAsync(string path) => LoadAsync(path, typeof(Object));

        public static ResourceRequest LoadAsync(string path, Type type)
        {
            var request = new ResourceRequest();
            if (type == typeof(TextAsset) || type == typeof(Shader) || !UnityBridge.HasHost)
            {
                var asset = Load(path, type);
                UnityBridge.Post(() => { request.asset = asset; request.Finish(); });
                return request;
            }
            if (type == typeof(GameObject)) _ = LoadModelAsync(path, request);
            else _ = LoadAsyncFrom(Candidates(path).GetEnumerator(), path, type, request);
            return request;
        }

        static async Task LoadAsyncFrom(IEnumerator<string> candidates, string path, Type type, ResourceRequest request)
        {
            Object? result = null;
            while (candidates.MoveNext())
            {
                try
                {
                    result = Wrap(await UObject.AsyncStaticLoadObject<UTexture2D>(candidates.Current), path);
                    if (result != null) break;
                }
                catch (Exception) { }
            }
            UnityBridge.Post(() => { request.asset = result; request.Finish(); });
        }

        static Object? Wrap(UObject? o, string path)
        {
            switch (o)
            {
                case UTexture2D t:
                    loaded[t] = path;
                    return Texture2D.FromUnreal(t, Leaf(path));

                default:
                    return null;
            }
        }

        /// <summary>Every asset of type <typeparamref name="T"/> under a Resources folder. Ship models ("Ships") are the ones
        /// ShipModels.json lists; textures are not enumerated (nothing ported asks for them).</summary>
        public static T[] LoadAll<T>(string path) where T : Object
        {
            var found = new List<T>();
            if (typeof(T) == typeof(GameObject) && path.TrimEnd('/') == "Ships")
            {
                InfoFor("");
                foreach (var name in models!.Keys)
                    if (Load(path.TrimEnd('/') + "/" + name, typeof(GameObject)) is T t) found.Add(t);
            }
            return found.ToArray();
        }

        public static void UnloadAsset(Object asset)
        {
            // Unreal unloads an asset once nothing references it: dropping the wrapper's reference is enough.
            if (asset is Texture t && t.Unreal != null) { loaded.Remove(t.Unreal); t.Unreal = null; }
        }

        public static AsyncOperation UnloadUnusedAssets()
        {
            var op = new AsyncOperation();
            UnityBridge.Post(op.Finish);
            return op;
        }

        // ---------------------------------------------------------------- binary data

        /// <summary>Where a Unity Resources path ("Data/asteroids") is looked up as a file: the shared SpaceGodRender
        /// plugin's Content/Resources (both tracks read Data/asteroids.bytes and Data/ShipModels.json there, US-T40),
        /// then this track's old Content/SpaceGod.</summary>
        public static IEnumerable<string> DataDirectories
        {
            get
            {
                string? plugin = null;
                try { if (UPluginBlueprintLibrary.GetPluginContentDir("SpaceGodRender", out var dir) && dir.Length > 0) plugin = dir; }
                catch { }
                if (plugin != null) yield return Path.Combine(plugin, "Resources");
                string? content = null;
                try { content = Path.Combine(SystemLibrary.ProjectContentDirectory, "SpaceGod"); }
                catch { }
                if (content != null) yield return content;
                yield return Path.Combine(AppContext.BaseDirectory, "SpaceGod");
            }
        }

        static TextAsset? LoadBytes(string path)
        {
            foreach (var dir in DataDirectories)
                foreach (var ext in new[] { ".bytes", ".json", ".txt", "" })
                {
                    var file = Path.Combine(dir, path.Replace('/', Path.DirectorySeparatorChar) + ext);
                    if (File.Exists(file)) return new TextAsset(File.ReadAllBytes(file)) { name = Leaf(path) };
                }
            return null;
        }

        // ---------------------------------------------------------------- ship models

        sealed class PartInfo
        {
            public string Mesh { get; set; } = "";
            public string Material { get; set; } = "";
            public float[] BaseColor { get; set; } = { 1, 1, 1, 1 };
            // glTF metallicFactor / roughnessFactor / emissiveFactor (US-T57; glTF's defaults when absent).
            public float Metallic { get; set; } = 1;
            public float Roughness { get; set; } = 1;
            public float[] Emissive { get; set; } = { 0, 0, 0 };
        }

        sealed class ModelInfo
        {
            public PartInfo[] Parts { get; set; } = Array.Empty<PartInfo>();
            /// <summary>Unity-space correction: Euler degrees and uniform scale taking Unreal's import to Unity's.</summary>
            public float[] CorrectionEuler { get; set; } = { 0, 0, 0 };
            public float CorrectionScale { get; set; } = 1;
        }

        static Dictionary<string, ModelInfo>? models;

        static ModelInfo? InfoFor(string model)
        {
            if (models == null)
            {
                models = new Dictionary<string, ModelInfo>(StringComparer.Ordinal);
                var asset = LoadBytes("Data/ShipModels");
                if (asset != null)
                {
                    try
                    {
                        var parsed = JsonSerializer.Deserialize<Dictionary<string, ModelInfo>>(asset.bytes,
                            new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
                        if (parsed != null) models = parsed;
                    }
                    catch (Exception e) { Debug.LogWarning("ShipModels.json: " + e.Message); }
                }
            }
            return models.TryGetValue(model, out var info) ? info : null;
        }

        static readonly Shader ImportedShader = Shader.Find(ShaderBinding.ImportedShader);

        static GameObject? LoadModel(string path)
        {
            var info = InfoFor(Leaf(path));
            if (info == null) return null;
            var meshes = new List<UStaticMesh>();
            foreach (var p in info.Parts)
            {
                try { meshes.Add(UObject.StaticLoadObject<UStaticMesh>(p.Mesh)); }
                catch (Exception) { return null; }
            }
            return ModelPrototype(info, meshes, path);
        }

        static async Task LoadModelAsync(string path, ResourceRequest request)
        {
            GameObject? result = null;
            var info = InfoFor(Leaf(path));
            if (info != null)
            {
                var meshes = new List<UStaticMesh>();
                try
                {
                    foreach (var p in info.Parts) meshes.Add(await UObject.AsyncStaticLoadObject<UStaticMesh>(p.Mesh));
                    result = ModelPrototype(info, meshes, path);
                }
                catch (Exception e) { Debug.LogWarning("Resources: " + path + ": " + e.Message); }
            }
            UnityBridge.Post(() => { request.asset = result; request.Finish(); });
        }

        /// <summary>The imported model as Unity's glTF importer hands it over: a GameObject with one child renderer per
        /// glTF node, in node order, each with a material carrying the file's material name and base colour (ShipModels
        /// reads both). An asset: only its Instantiate copies are in the scene.</summary>
        static float Gamma(float l) => l <= 0.0031308f ? l * 12.92f : 1.055f * Mathf.Pow(l, 1f / 2.4f) - 0.055f;

        /// <summary>The imported part's bounds in Unity's frame, as Unity's glTF import gives the mesh (the asset's box
        /// through the same correction UnityBridge draws it with), so renderer.bounds frame the model as Unity's do
        /// (ModelViewer fits its camera to them; a unit box placed the camera too far, US-T57).</summary>
        static Bounds ImportedBounds(UStaticMesh mesh, (Quaternion Rotation, Vector3 Scale) correction)
        {
            try
            {
                var box = mesh.BoundingBox;
                var b = new Bounds();
                for (int i = 0; i < 8; i++)
                {
                    var corner = new FVector((i & 1) != 0 ? box.Max.X : box.Min.X, (i & 2) != 0 ? box.Max.Y : box.Min.Y, (i & 4) != 0 ? box.Max.Z : box.Min.Z);
                    var u = correction.Rotation * Vector3.Scale(UnrealSpace.DirectionFromUnreal(corner), correction.Scale);
                    if (i == 0) b = new Bounds(u, Vector3.zero);
                    else b.Encapsulate(u);
                }
                return b;
            }
            catch (Exception) { return new Bounds(Vector3.zero, Vector3.one); }
        }
        static GameObject ModelPrototype(ModelInfo info, List<UStaticMesh> meshes, string path)
        {
            string model = Leaf(path);
            var go = new GameObject(model) { isAsset = true };
            var correction = (Quaternion.Euler(info.CorrectionEuler[0], info.CorrectionEuler[1], info.CorrectionEuler[2]), Vector3.one * info.CorrectionScale);
            for (int i = 0; i < meshes.Count; i++)
            {
                var part = info.Parts[i];
                loaded[meshes[i]] = path;
                var child = new GameObject(i == 0 ? model : model + "_" + i);
                child.transform.SetParent(go.transform, false);
                var unityMesh = new Mesh { name = child.name, StaticMesh = meshes[i], Correction = correction };
                unityMesh.bounds = ImportedBounds(meshes[i], correction);
                child.AddComponent<MeshFilter>().sharedMesh = unityMesh;
                var r = child.AddComponent<MeshRenderer>();
                var c = part.BaseColor;
                var m = new Material(ImportedShader) { name = part.Material };
                // glTF factors are linear; Unity's glTF import sets the material colour in gamma space (Color.gamma).
                m.SetColor("_BaseColor", new Color(Gamma(c[0]), Gamma(c[1]), Gamma(c[2]), c.Length > 3 ? c[3] : 1));
                // The rest of glTFast's glTF-pbrMetallicRoughness (a URP Lit target) in URP Lit's terms, drawn by M_Lit (US-T57):
                // smoothness = 1 - roughness, the emissive factor (linear) as a colour like the base colour.
                m.SetFloat("_Metallic", part.Metallic);
                m.SetFloat("_Smoothness", 1f - part.Roughness);
                var e = part.Emissive;
                m.SetColor("_EmissionColor", new Color(Gamma(e[0]), Gamma(e[1]), Gamma(e[2]), 1));
                r.sharedMaterial = m;
            }
            return go;
        }
        // ---------------------------------------------------------------- Unity's built-in meshes

        static readonly Dictionary<string, Mesh> builtins = new Dictionary<string, Mesh>(StringComparer.Ordinal);

        /// <summary>Unity's built-in primitives, rebuilt to the same geometry (the Cube: 24 vertices, unit size; the
        /// Capsule: radius 0.5, height 2 along Y).</summary>
        public static T GetBuiltinResource<T>(string name) where T : Object
        {
            if (typeof(T) != typeof(Mesh)) throw new NotSupportedException("GetBuiltinResource<" + typeof(T).Name + ">");
            if (!builtins.TryGetValue(name, out var m))
            {
                m = name switch
                {
                    "Cube.fbx" => BuiltinMeshes.Cube(),
                    "Capsule.fbx" => BuiltinMeshes.Capsule(),
                    "Sphere.fbx" => BuiltinMeshes.Sphere(),
                    "Quad.fbx" => BuiltinMeshes.Quad(),
                    _ => throw new NotSupportedException("Built-in mesh " + name),
                };
                builtins[name] = m;
            }
            return (T)(Object)m;
        }
    }

    internal static class BuiltinMeshes
    {
        public static Mesh Cube()
        {
            var v = new List<Vector3>();
            var n = new List<Vector3>();
            var uv = new List<Vector2>();
            var t = new List<int>();
            void Face(Vector3 normal, Vector3 u, Vector3 w)
            {
                int b = v.Count;
                var c = normal * 0.5f;
                v.Add(c - u * 0.5f - w * 0.5f); v.Add(c + u * 0.5f - w * 0.5f); v.Add(c + u * 0.5f + w * 0.5f); v.Add(c - u * 0.5f + w * 0.5f);
                for (int i = 0; i < 4; i++) n.Add(normal);
                uv.Add(new Vector2(0, 0)); uv.Add(new Vector2(1, 0)); uv.Add(new Vector2(1, 1)); uv.Add(new Vector2(0, 1));
                // Clockwise seen from outside (Unity's front face): u x w points inward.
                t.Add(b); t.Add(b + 2); t.Add(b + 1);
                t.Add(b); t.Add(b + 3); t.Add(b + 2);
            }
            Face(Vector3.forward, Vector3.right, Vector3.up);
            Face(Vector3.back, Vector3.left, Vector3.up);
            Face(Vector3.up, Vector3.right, Vector3.back);
            Face(Vector3.down, Vector3.right, Vector3.forward);
            Face(Vector3.right, Vector3.back, Vector3.up);
            Face(Vector3.left, Vector3.forward, Vector3.up);
            var m = new Mesh { name = "Cube" };
            m.SetVertices(v);
            m.SetNormals(n);
            m.SetUVs(0, uv);
            m.SetTriangles(t, 0);
            FixWinding(m);
            return m;
        }

        /// <summary>Makes every triangle wind clockwise seen from outside (along its normals), Unity's front face.</summary>
        static void FixWinding(Mesh m)
        {
            var v = m.RawVertices;
            var n = m.RawNormals!;
            var t = m.GetIndices(0);
            for (int i = 0; i + 2 < t.Length; i += 3)
            {
                var face = Vector3.Cross(v[t[i + 1]] - v[t[i]], v[t[i + 2]] - v[t[i]]);
                // Left-handed: clockwise seen from outside means the cross product points along the outward normal.
                if (Vector3.Dot(face, n[t[i]]) < 0) (t[i + 1], t[i + 2]) = (t[i + 2], t[i + 1]);
            }
            m.SetTriangles(t, 0);
        }

        static Mesh Revolved(string name, int segments, int rings, Func<float, (float y, float r, float ny, float nr)> profile)
        {
            var v = new List<Vector3>();
            var n = new List<Vector3>();
            var uv = new List<Vector2>();
            var t = new List<int>();
            for (int j = 0; j <= rings; j++)
            {
                float s = (float)j / rings;
                var (y, r, ny, nr) = profile(s);
                for (int i = 0; i <= segments; i++)
                {
                    float a = Mathf.PI * 2f * i / segments;
                    v.Add(new Vector3(Mathf.Cos(a) * r, y, Mathf.Sin(a) * r));
                    n.Add(new Vector3(Mathf.Cos(a) * nr, ny, Mathf.Sin(a) * nr).normalized);
                    uv.Add(new Vector2((float)i / segments, s));
                }
            }
            int cols = segments + 1;
            for (int j = 0; j < rings; j++)
                for (int i = 0; i < segments; i++)
                {
                    int a = j * cols + i, b = a + 1, c = a + cols, d = c + 1;
                    t.Add(a); t.Add(c); t.Add(b);
                    t.Add(b); t.Add(c); t.Add(d);
                }
            var m = new Mesh { name = name };
            m.SetVertices(v);
            m.SetNormals(n);
            m.SetUVs(0, uv);
            m.SetTriangles(t, 0);
            FixWinding(m);
            return m;
        }

        public static Mesh Sphere() => Revolved("Sphere", 24, 16, s =>
        {
            float lat = (s - 0.5f) * Mathf.PI;
            return (Mathf.Sin(lat) * 0.5f, Mathf.Cos(lat) * 0.5f, Mathf.Sin(lat), Mathf.Cos(lat));
        });

        public static Mesh Capsule() => Revolved("Capsule", 24, 18, s =>
        {
            // Bottom hemisphere (0..0.5 of the rings' first half), straight sides, top hemisphere.
            if (s < 1f / 3f)
            {
                float lat = (s * 3f - 1f) * Mathf.PI * 0.5f;
                return (-0.5f + Mathf.Sin(lat) * 0.5f, Mathf.Cos(lat) * 0.5f, Mathf.Sin(lat), Mathf.Cos(lat));
            }
            if (s > 2f / 3f)
            {
                float lat = (s * 3f - 2f) * Mathf.PI * 0.5f;
                return (0.5f + Mathf.Sin(lat) * 0.5f, Mathf.Cos(lat) * 0.5f, Mathf.Sin(lat), Mathf.Cos(lat));
            }
            return (Mathf.Lerp(-0.5f, 0.5f, s * 3f - 1f), 0.5f, 0, 1);
        });

        public static Mesh Quad()
        {
            var m = new Mesh { name = "Quad" };
            m.vertices = new[] { new Vector3(-0.5f, -0.5f, 0), new Vector3(0.5f, -0.5f, 0), new Vector3(-0.5f, 0.5f, 0), new Vector3(0.5f, 0.5f, 0) };
            m.uv = new[] { new Vector2(0, 0), new Vector2(1, 0), new Vector2(0, 1), new Vector2(1, 1) };
            m.normals = new[] { Vector3.back, Vector3.back, Vector3.back, Vector3.back };
            m.triangles = new[] { 0, 3, 1, 3, 0, 2 };
            return m;
        }
    }
}
