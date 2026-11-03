using System;
using System.Collections.Generic;
using UnrealSharp.Core;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;
using UnrealSharp.ProceduralMeshComponent;

namespace UnityEngine
{
    /// <summary>
    /// AsteroidBelt's Graphics.RenderPrimitives (Asteroids.shader: ~111,000 orbits propagated per vertex from a
    /// StructuredBuffer, every frame) on Unreal, on the GPU as the original: SpaceGodRender's M_Asteroids solves Kepler's
    /// equation per vertex in World Position Offset from AsteroidField.PackGpu's float4s in a float RGBA texture
    /// (_Asteroids, PF_A32B32G32R32F), sizes 1-3 px by H, the brightness ramp and the inside-the-belt dimming (README:
    /// Asteroids). Two things UnrealSharp cannot do directly are worked around here:
    ///  - a float texture: the packed elements are written as a DDS (legacy D3DFMT_A32B32G32R32F) and loaded with
    ///    RenderingLibrary.ImportBufferAsTexture2D, which keeps RGBA32F exactly;
    ///  - full-precision UVs: UProceduralMeshComponent stores TexCoords as halves, exact for integers only up to 2048, so
    ///    the belt is drawn in chunks of <see cref="ChunkAsteroids"/> asteroids, each its own component whose child MID
    ///    (parented to the belt's one instance, so the per-frame uniforms are set once) has its own slice of the elements.
    /// The vertex positions are ignored by the material; they span a unit box scaled up by BoundsScale, so the components are never culled.
    /// </summary>
    internal sealed class ProceduralAsteroids
    {
        /// <summary>Asteroids per component: TexCoord0.x = the index within the chunk must be exact as a half.</summary>
        public const int ChunkAsteroids = 2048;
        const int TextureWidth = 2048;
        // Vertex positions span a unit box (WPO moves each vertex by WorldCR - PositionCR, so positions far from the
        // camera would cost float precision); BoundsScale grows that box over the whole belt so it is never culled.
        const float BoundsExtent = 1f, BoundsScale = 1e10f;

        sealed class Chunk
        {
            public UProceduralMeshComponent Component = null!;
            public UMaterialInstanceDynamic? Mid;
            public UTexture2D? Texture;
        }

        readonly List<Chunk> chunks = new List<Chunk>();
        readonly Material material;
        UMaterialInterface? boundParent;
        bool shown;
        int drawnFrame = -1;

        public static int Count => instance?.chunks.Count ?? 0;

        ProceduralAsteroids(Material m, float[] data)
        {
            material = m;
            var host = UnityBridge.Host!;
            int total = data.Length / 12;
            for (int start = 0; start < total; start += ChunkAsteroids)
            {
                int n = Math.Min(ChunkAsteroids, total - start);
                var chunk = new Chunk();
                chunk.Component = host.AddComponentByClass<UProceduralMeshComponent>(false, new FTransform(), c =>
                {
                    c.CastShadow = false;
                    c.CollisionEnabled = ECollisionEnabled.NoCollision;
                });
                chunk.Component.BoundsScale = BoundsScale;
                chunk.Component.TranslucencySortPriority = ShaderBinding.SortPriority(m.shader);
                BuildMesh(chunk.Component, n);
                chunk.Texture = Texture(data, start, n);
                chunks.Add(chunk);
            }
            UnityBridge.Log($"AsteroidBelt: {total} asteroids on the GPU (M_Asteroids) in {chunks.Count} chunks", 0);
        }

        static ProceduralAsteroids? instance;

        internal static void Draw(RenderParams rp, MeshTopology topology, int vertexCount)
        {
            var m = rp.material;
            if (!UnityBridge.HasHost) return;
            if (!m.Buffers.TryGetValue("_Asteroids", out var buffer) || buffer.Data.Length == 0) return;
            instance ??= new ProceduralAsteroids(m, buffer.Data);
            instance.Frame(m);
        }

        /// <summary>Unity draws a procedural call only on the frames it is made: hidden on any frame AsteroidBelt.Render
        /// did not run (UnityBridge.Sync).</summary>
        internal static void EndFrame()
        {
            if (instance != null && instance.drawnFrame != Time.frameCount) instance.Show(false);
        }

        void Show(bool v)
        {
            if (shown == v) return;
            shown = v;
            foreach (var c in chunks) c.Component.SetVisibility(v);
        }

        void Frame(Material m)
        {
            drawnFrame = Time.frameCount;
            // Unity's RenderParams.layer is 0 (Default): drawn only by a screen camera that renders that layer (the
            // model viewer's does not).
            var screen = UnityBridge.ScreenCamera;
            if (screen != null && (screen.cullingMask & 1) == 0) { Show(false); return; }
            // Each chunk's instance carries the per-frame uniforms (_DtDays, _SunMap0/1, _SunRelative, _Color ...) and
            // its own _Asteroids. Not parented to a shared instance: one that only C# and child instances hold is
            // collected by Unreal's GC, and the children then re-cache a dead parent (a fatal error).
            boundParent ??= ShaderBinding.Parent(m.shader);
            if (boundParent == null) return;
            bool push = pushedVersion != m.version;
            pushedVersion = m.version;
            foreach (var c in chunks)
            {
                if (c.Mid != null) continue;
                c.Mid = MaterialLibrary.CreateDynamicMaterialInstance(boundParent, new FName("AsteroidChunk"));
                if (c.Texture != null) c.Mid.SetTextureParameterValue(new FName("_Asteroids"), c.Texture);
                c.Component.SetMaterial(0, c.Mid);
                push = true;
            }
            if (push)
                foreach (var c in chunks) ShaderBinding.Push(m, c.Mid!);
            Show(true);
        }

        int pushedVersion = -1;

        static void BuildMesh(UProceduralMeshComponent component, int n)
        {
            int count = n * 6;
            var vertices = new List<FVector>(count);
            var uv0 = new List<FVector2D>(count);
            var triangles = new List<int>(count);
            for (int i = 0; i < n; i++)
                for (int c = 0; c < 6; c++)
                {
                    int k = i * 6 + c;
                    // Positions are ignored (WPO places every vertex); the eight corners of a huge box give the bounds.
                    vertices.Add(new FVector((k & 1) != 0 ? BoundsExtent : -BoundsExtent,
                        (k & 2) != 0 ? BoundsExtent : -BoundsExtent, (k & 4) != 0 ? BoundsExtent : -BoundsExtent));
                    // README: Asteroids' TexCoord0 = (index, corner 0..5), not flipped.
                    uv0.Add(new FVector2D(i, c));
                    triangles.Add(k);
                }
            var empty2 = new List<FVector2D>();
            component.CreateMeshSection(0, vertices, triangles, new List<FVector>(), uv0, empty2, empty2, empty2,
                new List<FLinearColor>(), new List<FProcMeshTangent>(), false);
        }

        /// <summary>Asteroids [start, start + n) as PF_A32B32G32R32F texels, row-major, <see cref="TextureWidth"/> wide.</summary>
        static UTexture2D? Texture(float[] data, int start, int n)
        {
            int texels = n * 3;
            int height = Math.Max(1, (texels + TextureWidth - 1) / TextureWidth);
            var floats = new float[TextureWidth * height * 4];
            Array.Copy(data, start * 12, floats, 0, n * 12);
            try { return RenderingLibrary.ImportBufferAsTexture2D(Dds.EncodeRgba32F(TextureWidth, height, floats)); }
            catch (Exception e) { UnityBridge.Log("AsteroidBelt texture: " + e.Message, 1); return null; }
        }
    }

    /// <summary>A minimal DDS writer: one mip of 32-bit float RGBA (legacy FourCC 116 = D3DFMT_A32B32G32R32F, which
    /// Unreal's DDS reader maps to ERawImageFormat::RGBA32F), rows top-down as given.</summary>
    internal static class Dds
    {
        public static byte[] EncodeRgba32F(int width, int height, float[] rgba)
        {
            var bytes = new byte[128 + width * height * 16];
            void U32(int at, uint v) => BitConverter.TryWriteBytes(new Span<byte>(bytes, at, 4), v);
            U32(0, 0x20534444);            // "DDS "
            U32(4, 124);                   // dwSize
            U32(8, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000); // CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
            U32(12, (uint)height);
            U32(16, (uint)width);
            U32(20, (uint)(width * 16));   // pitch
            U32(28, 1);                    // mip count
            U32(76, 32);                   // ddspf.dwSize
            U32(80, 0x4);                  // DDPF_FOURCC
            U32(84, 116);                  // D3DFMT_A32B32G32R32F
            U32(108, 0x1000);              // DDSCAPS_TEXTURE
            Buffer.BlockCopy(rgba, 0, bytes, 128, Math.Min(rgba.Length * 4, width * height * 16));
            return bytes;
        }
    }
}
