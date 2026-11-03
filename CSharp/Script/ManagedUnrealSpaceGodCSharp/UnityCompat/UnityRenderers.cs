using System;
using System.Collections.Generic;
using UnityEngine.Rendering;

namespace UnityEngine
{
    /// <summary>
    /// Unity's Renderer: what to draw a GameObject's mesh with. UnityBridge gives each one that is enabled, active and
    /// has a mesh an Unreal primitive (UProceduralMeshComponent, or UStaticMeshComponent for an imported model) and
    /// keeps its transform, visibility, mesh and materials in step once a frame.
    /// </summary>
    public class Renderer : Component
    {
        bool enabledValue = true;
        Material?[] materialsValue = Array.Empty<Material?>();
        internal MaterialPropertyBlock? Block;
        internal int blockVersion;
        internal int materialsVersion;
        internal UnityBridge.Primitive? Primitive;

        public bool enabled
        {
            get => enabledValue;
            set { if (enabledValue != value) { enabledValue = value; UnityBridge.MarkHierarchyChanged(); } }
        }
        public ShadowCastingMode shadowCastingMode { get; set; } = ShadowCastingMode.On;
        public bool receiveShadows { get; set; } = true;
        public int sortingOrder { get; set; }
        public bool isVisible => enabledValue && gameObject.activeInHierarchy;

        public Material? sharedMaterial
        {
            get => materialsValue.Length > 0 ? materialsValue[0] : null;
            set
            {
                if (materialsValue.Length == 0) materialsValue = new Material?[1];
                if (ReferenceEquals(materialsValue[0], value)) return;
                materialsValue[0] = value;
                materialsVersion++;
            }
        }
        public Material?[] sharedMaterials
        {
            get => (Material?[])materialsValue.Clone();
            set { materialsValue = value != null ? (Material?[])value.Clone() : Array.Empty<Material?>(); materialsVersion++; }
        }
        /// <summary>Unity's per-renderer copy of its material, made on first access.</summary>
        public Material? material
        {
            get
            {
                var m = sharedMaterial;
                if (m == null) return null;
                if (!ownsMaterial)
                {
                    m = new Material(m);
                    ownsMaterial = true;
                    sharedMaterial = m;
                }
                return m;
            }
            set { sharedMaterial = value; ownsMaterial = true; }
        }
        bool ownsMaterial;
        public Material?[] materials { get => sharedMaterials; set => sharedMaterials = value; }

        internal Material?[] RawMaterials => materialsValue;

        public void SetPropertyBlock(MaterialPropertyBlock? block)
        {
            if (block == null) { if (Block != null) { Block = null; blockVersion++; } return; }
            if (block.SameAs(Block)) return;
            Block = block.Copy();
            blockVersion++;
        }
        public void GetPropertyBlock(MaterialPropertyBlock dest)
        {
            dest.Clear();
            if (Block == null) return;
            foreach (var kv in Block.Floats) dest.Floats[kv.Key] = kv.Value;
            foreach (var kv in Block.Vectors) dest.Vectors[kv.Key] = kv.Value;
            foreach (var kv in Block.Textures) dest.Textures[kv.Key] = kv.Value;
        }
        public bool HasPropertyBlock() => Block != null;

        /// <summary>The mesh to draw (MeshFilter's, or a particle system's own).</summary>
        internal virtual Mesh? DrawMesh => GetComponent<MeshFilter>()?.sharedMesh;
        /// <summary>Whether the mesh is already in world space (particles), drawn with an identity transform.</summary>
        internal virtual bool WorldSpaceGeometry => false;

        public Bounds bounds
        {
            get
            {
                var m = DrawMesh;
                if (m == null) return new Bounds(transform.position, Vector3.zero);
                var b = m.bounds;
                var c = transform.TransformPoint(b.center);
                var e = Vector3.Scale(b.extents, transform.lossyScale);
                return new Bounds(c, new Vector3(Mathf.Abs(e.x), Mathf.Abs(e.y), Mathf.Abs(e.z)) * 2f);
            }
        }

        internal override void OnAddedInternal() => UnityBridge.Register(this);
        internal override void OnDestroyInternal()
        {
            if (destroyed) return;
            base.OnDestroyInternal();
            UnityBridge.Unregister(this);
        }
        internal override Component CloneInternalComponent()
        {
            var c = (Renderer)MemberwiseClone();
            c.Primitive = null;
            c.materialsValue = (Material?[])materialsValue.Clone();
            c.Block = Block?.Copy();
            return c;
        }
    }

    public class MeshRenderer : Renderer { }

    public enum CameraClearFlags { Skybox = 1, Color = 2, SolidColor = 2, Depth = 3, Nothing = 4 }

    /// <summary>
    /// Unity's Camera, as the views read it (Camera.main's transform for billboards, its lens for pixel sizes). The
    /// app (US-T26) owns the real one and keeps <see cref="main"/> in step with the Unreal view.
    /// </summary>
    public class Camera : Behaviour
    {
        static Camera? mainCamera;
        public static Camera? main
        {
            get => mainCamera != null && !mainCamera.destroyed ? mainCamera : null;
            set => mainCamera = value;
        }
        public float fieldOfView { get; set; } = 60f;
        public float nearClipPlane { get; set; } = 0.3f;
        public float farClipPlane { get; set; } = 1000f;
        public float aspect { get; set; } = 16f / 9f;
        public int pixelWidth { get; set; } = 1600;
        public int pixelHeight { get; set; } = 900;
        public CameraClearFlags clearFlags { get; set; } = CameraClearFlags.Skybox;
        public Color backgroundColor { get; set; } = Color.black;
        public int cullingMask { get; set; } = -1;
        public float depth { get; set; }
        public bool orthographic { get; set; }
        public float orthographicSize { get; set; } = 5;
        public RenderTexture? targetTexture { get; set; }
        public bool allowHDR { get; set; } = true;
        public bool allowMSAA { get; set; } = true;

        /// <summary>The Unreal camera this one drives (set by the app, US-T26); RenderBackend applies the look to it.</summary>
        public UnrealSharp.Engine.UCameraComponent? Unreal { get; set; }
        public void Render() => UnityBridge.RenderCamera(this);

        public Vector3 WorldToScreenPoint(Vector3 p)
        {
            var local = transform.InverseTransformDirection(p - transform.position);
            if (local.z <= 0) return new Vector3(-1, -1, local.z);
            float f = 0.5f * pixelHeight / Mathf.Tan(fieldOfView * 0.5f * Mathf.Deg2Rad);
            return new Vector3(pixelWidth * 0.5f + local.x / local.z * f, pixelHeight * 0.5f + local.y / local.z * f, local.z);
        }
    }

    public enum RenderTextureFormat { ARGB32 = 0, Depth = 1, ARGBHalf = 2, Default = 7, DefaultHDR = 9 }

    public sealed class RenderTexture : Texture
    {
        public RenderTexture(int width, int height, int depth, RenderTextureFormat format = RenderTextureFormat.Default)
        {
            this.width = width;
            this.height = height;
            Format = format;
        }
        public RenderTextureFormat Format { get; }
        public int antiAliasing { get; set; } = 1;
        public bool Create() { UnityBridge.EnsureRenderTarget(this); return true; }
        public void Release() => Unreal = null;
        public bool IsCreated() => Unreal != null;
        public static RenderTexture? active { get; set; }
    }

    public enum LightType { Spot = 0, Directional = 1, Point = 2 }
    public enum LightShadows { None = 0, Hard = 1, Soft = 2 }

    /// <summary>A light (ShipThumbnails' key light). Kept as data: the ported shaders light themselves from their own
    /// Sun parameters, as the originals do, so no Unreal light is spawned for it.</summary>
    public class Light : Behaviour
    {
        public LightType type { get; set; } = LightType.Directional;
        public Color color { get; set; } = Color.white;
        public float intensity { get; set; } = 1f;
        public float range { get; set; } = 10f;
        public LightShadows shadows { get; set; }
        public int cullingMask { get; set; } = -1;
    }

    public static class LayerMask
    {
        static readonly List<string> Names = new List<string> { "Default", "TransparentFX", "Ignore Raycast", "", "Water", "UI" };
        public static int NameToLayer(string name)
        {
            int i = Names.IndexOf(name);
            if (i >= 0) return i;
            Names.Add(name);
            return Names.Count - 1;
        }
        public static int GetMask(params string[] names)
        {
            int m = 0;
            foreach (var n in names) m |= 1 << NameToLayer(n);
            return m;
        }
    }

    /// <summary>Unity's RenderParams for Graphics.RenderPrimitives.</summary>
    public struct RenderParams
    {
        public Material material;
        public Bounds worldBounds;
        public ShadowCastingMode shadowCastingMode;
        public bool receiveShadows;
        public RenderParams(Material mat) { material = mat; worldBounds = default; shadowCastingMode = ShadowCastingMode.On; receiveShadows = true; }
    }

    public static class Graphics
    {
        /// <summary>A procedural draw (AsteroidBelt): handed to whoever registered for the material's shader, see
        /// <see cref="UnityBridge.ProceduralDrawers"/>.</summary>
        public static void RenderPrimitives(in RenderParams rp, MeshTopology topology, int vertexCount, int instanceCount = 1) =>
            UnityBridge.DrawProcedural(rp, topology, vertexCount);
    }

    /// <summary>A mesh's binary data asset (Resources' .bytes files).</summary>
    public sealed class TextAsset : Object
    {
        public TextAsset(byte[] bytes) { this.bytes = bytes; }
        public byte[] bytes { get; }
        public string text => System.Text.Encoding.UTF8.GetString(bytes);
    }
}
