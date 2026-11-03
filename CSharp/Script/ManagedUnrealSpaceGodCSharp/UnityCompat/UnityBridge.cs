using System;
using System.Collections.Generic;
using System.Reflection;
using UnrealSharp.Core;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;
using UnrealSharp.Log;
using UnrealSharp.ProceduralMeshComponent;

namespace UnityEngine
{
    /// <summary>
    /// Where the Unity facade meets Unreal (US-T27). The app calls <see cref="Begin"/> once with an actor to hang the
    /// scene's primitives on, then each frame <see cref="Tick"/> (clock, finished loads, particles, MonoBehaviour.Update)
    /// before its own Update and <see cref="Sync"/> after it (LateUpdate; then every Unity renderer that is enabled,
    /// active and has something to draw gets an Unreal primitive whose transform, mesh, materials and visibility are
    /// brought up to date, only where they changed). Unity's scene is camera-relative, so the Unreal camera stays at
    /// the origin looking where Camera.main does.
    /// </summary>
    public static class UnityBridge
    {
        public const string LogCategory = "LogSpaceGod";

        static AActor? host;
        static readonly List<Renderer> renderers = new List<Renderer>();
        static readonly HashSet<Renderer> rendererSet = new HashSet<Renderer>();
        static readonly List<MonoBehaviour> behaviours = new List<MonoBehaviour>();
        static readonly List<ParticleSystem> particleSystems = new List<ParticleSystem>();
        static readonly List<Action> pendingLoads = new List<Action>();
        static int hierarchyVersion;
        static UMaterialParameterCollection? globals;
        static bool globalsLooked;
        static int globalsPushed = -1;

        /// <summary>Whether the Unreal side exists (false in a pure .NET run: everything then stays managed data).</summary>
        public static bool HasHost => host != null;
        public static AActor? Host => host;

        /// <summary>Procedural draws (Graphics.RenderPrimitives) by shader name: AsteroidBelt's GPU-propagated belt.</summary>
        public static readonly Dictionary<string, Action<RenderParams, MeshTopology, int>> ProceduralDrawers =
            new Dictionary<string, Action<RenderParams, MeshTopology, int>>(StringComparer.Ordinal)
            {
                ["Asteroids"] = ProceduralAsteroids.Draw,
            };

        /// <summary>Hangs the scene's Unreal primitives on <paramref name="actor"/> (it gets a root at the origin if it has none).</summary>
        public static void Begin(AActor actor)
        {
            // Not on the given actor itself: a game mode (an AInfo) is hidden in game, and so would be everything on it.
            // The scene gets its own plain actor at the origin, owned by it.
            var scene = UObject.SpawnActor<AActor>(new UnrealSharp.TSubclassOf<AActor>(), new FTransform(),
                ESpawnActorCollisionHandlingMethod.AlwaysSpawn, null, actor);
            host = scene ?? actor;
            if (host.RootComponent == null)
                host.AddComponentByClass<USceneComponent>(false, new FTransform());
        }

        public static void Log(string message, int severity)
        {
            switch (severity)
            {
                case 2: UnrealLogger.LogError(LogCategory, message); break;
                case 1: UnrealLogger.LogWarning(LogCategory, message); break;
                default: UnrealLogger.Log(LogCategory, message); break;
            }
        }

        public static string PersistentDataPath
        {
            get
            {
                try { return System.IO.Path.Combine(SystemLibrary.ProjectSavedDirectory, "SpaceGod"); }
                catch { return System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Unreal.SpaceGod"); }
            }
        }

        internal static void MarkHierarchyChanged() => hierarchyVersion++;

        internal static void Register(Renderer r) { if (rendererSet.Add(r)) renderers.Add(r); }
        internal static void Unregister(Renderer r)
        {
            if (!rendererSet.Remove(r)) return;
            renderers.Remove(r);
            r.Primitive?.Destroy();
            r.Primitive = null;
        }
        internal static void Register(MonoBehaviour b) => behaviours.Add(b);
        internal static void Unregister(MonoBehaviour b) => behaviours.Remove(b);
        internal static void Register(ParticleSystem p) => particleSystems.Add(p);
        internal static void Unregister(ParticleSystem p) => particleSystems.Remove(p);

        /// <summary>Runs <paramref name="complete"/> on the game thread at the next <see cref="Tick"/> (finished async loads).</summary>
        internal static void Post(Action complete) { lock (pendingLoads) pendingLoads.Add(complete); }

        // ---------------------------------------------------------------- MonoBehaviour messages

        static readonly Dictionary<(Type, string), MethodInfo?> Messages = new Dictionary<(Type, string), MethodInfo?>();
        internal static void Invoke(MonoBehaviour b, string message)
        {
            var key = (b.GetType(), message);
            if (!Messages.TryGetValue(key, out var m))
                Messages[key] = m = b.GetType().GetMethod(message, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic, null, Type.EmptyTypes, null);
            if (m == null) return;
            try { m.Invoke(b, null); }
            catch (TargetInvocationException e) { Debug.LogException(e.InnerException ?? e); }
        }

        /// <summary>The OnGUI of every live MonoBehaviour that has one (AsteroidBelt's labels), for the IMGUI host (US-T28's
        /// ImguiHost.Frame) to run with the app's own.</summary>
        public static List<Action> GuiHandlers()
        {
            var list = new List<Action>();
            foreach (var b in behaviours)
            {
                if (!b.enabled || !b.gameObject.activeInHierarchy) continue;
                var m = b.GetType().GetMethod("OnGUI", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic, null, Type.EmptyTypes, null);
                if (m != null) list.Add((Action)Delegate.CreateDelegate(typeof(Action), b, m));
            }
            return list;
        }

        // ---------------------------------------------------------------- per frame

        /// <summary>Start of a frame: Unity's clock, loads that finished, particles, then MonoBehaviour.Update.</summary>
        public static void Tick(float realDeltaSeconds)
        {
            Time.unscaledDeltaTime = realDeltaSeconds;
            Time.unscaledTime += realDeltaSeconds;
            Time.deltaTime = realDeltaSeconds * Time.timeScale;
            Time.time += Time.deltaTime;
            Time.frameCount++;

            Action[] done;
            lock (pendingLoads)
            {
                done = pendingLoads.ToArray();
                pendingLoads.Clear();
            }
            foreach (var a in done)
            {
                try { a(); }
                catch (Exception e) { Debug.LogException(e); }
            }

            foreach (var p in particleSystems.ToArray())
                if (p.gameObject.activeInHierarchy) p.Simulate(Time.deltaTime);
            foreach (var b in behaviours.ToArray())
                if (b.enabled && b.gameObject.activeInHierarchy) Invoke(b, "Update");
        }

        public struct Stats
        {
            public int Renderers, Primitives, Visible, Triangles, MeshUploads, MaterialInstances;
            public override string ToString() =>
                $"{Renderers} renderers, {Primitives} Unreal primitives ({Visible} visible, {Triangles} triangles), {MeshUploads} mesh uploads, {MaterialInstances} material instances";
        }

        public static Stats Last;
        static int meshUploads, materialInstances;

        /// <summary>End of a frame (Unity's LateUpdate and render): pushes everything drawn to Unreal.</summary>
        public static Stats Sync()
        {
            foreach (var b in behaviours.ToArray())
                if (b.enabled && b.gameObject.activeInHierarchy) Invoke(b, "LateUpdate");

            ProceduralAsteroids.EndFrame();
            var cam = Camera.main;
            var camPos = cam != null ? cam.transform.position : Vector3.zero;
            var camRot = cam != null ? cam.transform.rotation : Quaternion.identity;
            PushGlobals();

            var s = new Stats { Renderers = renderers.Count };
            meshUploads = 0;
            foreach (var r in renderers)
            {
                if (r is ParticleSystemRenderer pr && r.enabled && r.gameObject.activeInHierarchy) pr.Rebuild(camPos, camRot);
                var mesh = r.DrawMesh;
                bool want = r.enabled && mesh != null && mesh.vertexCount + (mesh.StaticMesh != null ? 1 : 0) > 0 && r.gameObject.activeInHierarchy;
                if (!want)
                {
                    r.Primitive?.SetVisible(false);
                    continue;
                }
                if (host == null) { s.Visible++; s.Triangles += mesh!.StaticMesh == null ? mesh.Unreal.TriangleCount : 0; continue; }
                r.Primitive ??= new Primitive(r);
                if (r.Primitive.Update(r, mesh!))
                {
                    s.Visible++;
                    s.Triangles += r.Primitive.Triangles;
                }
            }
            foreach (var r in renderers) if (r.Primitive != null) s.Primitives++;
            s.MeshUploads = meshUploads;
            s.MaterialInstances = materialInstances;
            Last = s;
            return s;
        }

        static void PushGlobals()
        {
            if (globalsPushed == Shader.globalsVersion) return;
            globalsPushed = Shader.globalsVersion;
            if (host == null) return;
            if (!globalsLooked)
            {
                globalsLooked = true;
                try { globals = UObject.StaticLoadObject<UMaterialParameterCollection>(ShaderBinding.GlobalsCollection); }
                catch { globals = null; }
            }
            if (globals == null) return;
            foreach (var kv in Shader.GlobalVectors)
                MaterialLibrary.SetVectorParameterValue(globals, new FName(kv.Key), ShaderBinding.ToUnreal(kv.Key, kv.Value, Shader.GlobalColorNames.Contains(kv.Key)));
            foreach (var kv in Shader.GlobalFloats)
                MaterialLibrary.SetScalarParameterValue(globals, new FName(kv.Key), kv.Value);
        }

        internal static UMaterialInstanceDynamic? InstanceOf(Material m)
        {
            if (host == null) return null;
            if (m.Instance == null)
            {
                var parent = ShaderBinding.Parent(m.shader);
                if (parent == null) return null;
                m.Instance = MaterialLibrary.CreateDynamicMaterialInstance(parent, new FName(m.name.Length > 0 ? Sanitize(m.name) : "Material"));
                m.pushedVersion = -1;
                materialInstances++;
            }
            if (m.pushedVersion != m.version)
            {
                ShaderBinding.Push(m, m.Instance);
                m.pushedVersion = m.version;
            }
            return m.Instance;
        }

        static string Sanitize(string n)
        {
            var c = n.ToCharArray();
            for (int i = 0; i < c.Length; i++) if (!char.IsLetterOrDigit(c[i])) c[i] = '_';
            return new string(c);
        }

        /// <summary>One Unity renderer's Unreal primitive.</summary>
        internal sealed class Primitive
        {
            UPrimitiveComponent? component;
            UProceduralMeshComponent? procedural;
            UStaticMeshComponent? staticMesh;
            Mesh? shownMesh;
            int shownMeshVersion = -1, shownTransform = -1, shownMaterials = -1, shownBlock = -1, sections;
            readonly List<(UMaterialInstanceDynamic? Mid, UMaterialInstanceDynamic? Parent)> blockInstances = new List<(UMaterialInstanceDynamic?, UMaterialInstanceDynamic?)>();
            bool visible;
            public int Triangles;

            public Primitive(Renderer r) { }

            public void SetVisible(bool v)
            {
                if (visible == v || component == null) return;
                visible = v;
                component.SetVisibility(v);
            }

            public void Destroy()
            {
                try { component?.DestroyComponent(component); } catch { }
                component = null;
            }

            /// <summary>Brings the primitive up to date; false if it has nothing to show.</summary>
            public bool Update(Renderer r, Mesh mesh)
            {
                bool isStatic = mesh.StaticMesh != null;
                if (component == null || (isStatic ? staticMesh == null : procedural == null))
                {
                    Destroy();
                    var h = host!;
                    if (isStatic)
                        component = staticMesh = h.AddComponentByClass<UStaticMeshComponent>(false, new FTransform(), c =>
                        {
                            c.CastShadow = false;
                            c.CollisionEnabled = ECollisionEnabled.NoCollision;
                        });
                    else
                        component = procedural = h.AddComponentByClass<UProceduralMeshComponent>(false, new FTransform(), c =>
                        {
                            c.CastShadow = false;
                            c.CollisionEnabled = ECollisionEnabled.NoCollision;
                        });
                    shownMesh = null;
                    shownMeshVersion = shownTransform = shownMaterials = shownBlock = -1;
                    visible = true;
                }

                // Mesh.
                if (!ReferenceEquals(shownMesh, mesh) || shownMeshVersion != mesh.version)
                {
                    shownMesh = mesh;
                    shownMeshVersion = mesh.version;
                    shownMaterials = -1;
                    meshUploads++;
                    if (isStatic)
                    {
                        staticMesh!.SetStaticMesh(mesh.StaticMesh!);
                        sections = Math.Max(1, r.RawMaterials.Length);
                        Triangles = 0;
                    }
                    else
                    {
                        var data = mesh.Unreal;
                        procedural!.ClearAllMeshSections();
                        int i = 0;
                        foreach (var s in data.Sections)
                        {
                            if (s.Vertices.Count > 0 && s.Triangles.Count > 0)
                                procedural.CreateMeshSection(i, s.Vertices, s.Triangles, s.Normals, s.Uv0, s.Uv1, s.Uv2, s.Uv3, s.Colors,
                                    new List<FProcMeshTangent>(), false);
                            i++;
                        }
                        sections = data.Sections.Count;
                        Triangles = data.TriangleCount;
                    }
                }

                // Transform: Unity world -> Unreal world (identity for geometry already in world space).
                int tv = r.WorldSpaceGeometry ? 0 : r.transform.version;
                if (tv != shownTransform)
                {
                    shownTransform = tv;
                    FTransform t;
                    if (r.WorldSpaceGeometry) t = new FTransform();
                    else
                    {
                        var rot = r.transform.rotation;
                        var scale = r.transform.lossyScale;
                        if (mesh.Correction is { } c)
                        {
                            rot = rot * c.Rotation;
                            scale = Vector3.Scale(scale, c.Scale);
                        }
                        t = UnrealSpace.ToUnreal(r.transform.position, rot, scale);
                    }
                    component!.SetWorldTransform(t, false, out _, true);
                }

                // Materials (+ the renderer's property block on per-primitive instances).
                if (shownMaterials != r.materialsVersion + MaterialsStamp(r) || shownBlock != r.blockVersion)
                {
                    shownMaterials = r.materialsVersion + MaterialsStamp(r);
                    shownBlock = r.blockVersion;
                    var mats = r.RawMaterials;
                    var first = mats.Length > 0 ? mats[0] : null;
                    if (first != null)
                    {
                        // Unity's render queue -> translucency sort; the far field (sky, stars) is never culled.
                        component!.TranslucencySortPriority = ShaderBinding.SortPriority(first.shader);
                        if (ShaderBinding.IsFarField(first.shader)) component.BoundsScale = 1e6f;
                    }
                    for (int i = 0; i < sections && mats.Length > 0; i++)
                    {
                        var m = mats[Math.Min(i, mats.Length - 1)];
                        if (m == null) continue;
                        var parent = InstanceOf(m);
                        if (parent == null) continue;
                        if (r.Block == null)
                        {
                            component!.SetMaterial(i, parent);
                            continue;
                        }
                        while (blockInstances.Count <= i) blockInstances.Add((null, null));
                        // Our own record of the parent: the glue hands back a fresh wrapper for mid.Parent each time.
                        var (mid, ofParent) = blockInstances[i];
                        if (mid == null || !ReferenceEquals(ofParent, parent))
                        {
                            mid = MaterialLibrary.CreateDynamicMaterialInstance(parent, new FName("Block"));
                            blockInstances[i] = (mid, parent);
                            materialInstances++;
                        }
                        ShaderBinding.Push(r.Block, mid);
                        component!.SetMaterial(i, mid);
                    }
                }
                else
                {
                    // Shared materials whose values moved since: push them (the primitives follow their instances).
                    foreach (var m in r.RawMaterials) if (m != null && m.pushedVersion != m.version) InstanceOf(m);
                }

                SetVisible(true);
                return true;
            }

            static int MaterialsStamp(Renderer r)
            {
                // A material made into an instance after the renderer was first shown re-binds once.
                int stamp = 0;
                foreach (var m in r.RawMaterials) if (m?.Instance != null) stamp += 1 << 20;
                return stamp;
            }
        }

        // ---------------------------------------------------------------- textures, cameras, procedural draws

        /// <summary>An Unreal texture made from RGBA pixels (a swatch, a seeded sky, an icon): encoded as a PNG (alpha
        /// kept) and handed to RenderingLibrary.ImportBufferAsTexture2D, the runtime import UnrealSharp exposes.</summary>
        internal static UTexture2D? TextureFromPixels(int width, int height, Color32[] pixels)
        {
            if (host == null) return null;
            try { return RenderingLibrary.ImportBufferAsTexture2D(Png.Encode(width, height, pixels)); }
            catch (Exception e) { Log("TextureFromPixels: " + e.Message, 1); return null; }
        }
        internal static void EnsureRenderTarget(RenderTexture rt)
        {
            if (rt.Unreal != null || host == null) return;
            try { rt.Unreal = RenderingLibrary.CreateRenderTarget2D(rt.width, rt.height, ETextureRenderTargetFormat.RTF_RGBA8); }
            catch (Exception e) { Log("RenderTexture: " + e.Message, 1); }
        }

        static USceneCaptureComponent2D? capture;

        /// <summary>Camera.Render into its targetTexture (ShipThumbnails): a scene capture from the camera's pose.</summary>
        internal static void RenderCamera(Camera camera)
        {
            if (host == null || camera.targetTexture == null) return;
            EnsureRenderTarget(camera.targetTexture);
            if (camera.targetTexture.Unreal is not UTextureRenderTarget2D target) return;
            Sync();
            capture ??= host.AddComponentByClass<USceneCaptureComponent2D>(false, new FTransform(), c =>
            {
                c.CaptureEveryFrame = false;
                c.CaptureOnMovement = false;
            });
            // Unity's fieldOfView is vertical, Unreal's horizontal; Unity's +Z forward is Unreal's +X, so the pose maps as is.
            float aspect = camera.targetTexture.height > 0 ? (float)camera.targetTexture.width / camera.targetTexture.height : camera.aspect;
            capture.FieldofView = 2f * Mathf.Atan(Mathf.Tan(camera.fieldOfView * 0.5f * Mathf.Deg2Rad) * aspect) * Mathf.Rad2Deg;
            capture.TextureTarget = target;
            capture.SetWorldTransform(UnrealSpace.ToUnreal(camera.transform.position, camera.transform.rotation, Vector3.one), false, out _, true);
            capture.CaptureScene();
        }

        internal static void DrawProcedural(RenderParams rp, MeshTopology topology, int vertexCount)
        {
            if (rp.material == null) return;
            if (ProceduralDrawers.TryGetValue(ShaderBinding.ShaderName(rp.material.shader), out var draw)) draw(rp, topology, vertexCount);
        }
    }
}
