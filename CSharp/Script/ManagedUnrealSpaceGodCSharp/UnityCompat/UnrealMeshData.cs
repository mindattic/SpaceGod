using System;
using System.Collections.Generic;
using UnrealSharp.CoreUObject;

namespace UnityEngine
{
    /// <summary>
    /// A Unity mesh converted for UProceduralMeshComponent on the SpaceGodRender conventions
    /// (Shared/Plugins/SpaceGodRender/README.md): Unity axes to Unreal's (UnrealSpace), triangles re-wound, every Unity
    /// uv written as (u, 1 - v), vertex colours raw, and the topologies Unreal cannot draw expanded into quads the
    /// materials widen to Unity's one pixel in World Position Offset:
    ///  - points (M_Stars, M_Points): 4 vertices at the point, TexCoord0 = the corner (+-1, +-1);
    ///  - lines (M_Line): a ribbon of 4 vertices per segment, 2 at each end; each carries the OTHER end (object space,
    ///    Unreal axes) in TexCoord1.xy + TexCoord2.x and its side (-1/+1) in TexCoord2.y; TexCoord0 is the Unity uv.
    /// A Unity uv with a third component (Fx's shape / shield age) goes to TexCoord1.x when the mesh has no second uv set.
    /// </summary>
    internal sealed class UnrealMeshData
    {
        internal sealed class Section
        {
            public readonly List<FVector> Vertices = new List<FVector>();
            public readonly List<int> Triangles = new List<int>();
            public readonly List<FVector> Normals = new List<FVector>();
            public readonly List<FVector2D> Uv0 = new List<FVector2D>(), Uv1 = new List<FVector2D>(), Uv2 = new List<FVector2D>(), Uv3 = new List<FVector2D>();
            public readonly List<FLinearColor> Colors = new List<FLinearColor>();
            public MeshTopology Topology;
            public int TriangleCount => Triangles.Count / 3;
        }

        public readonly List<Section> Sections = new List<Section>();
        public int TriangleCount { get; private set; }

        public static UnrealMeshData Build(Mesh mesh)
        {
            var data = new UnrealMeshData();
            var v = mesh.RawVertices;
            var n = mesh.RawNormals;
            var uv0 = mesh.RawUv(0);
            var uv1 = mesh.RawUv(1);
            var c = mesh.RawColors;
            foreach (var (indices, topology) in mesh.RawSubMeshes)
            {
                var s = new Section { Topology = topology };
                switch (topology)
                {
                    case MeshTopology.Triangles:
                    case MeshTopology.Quads:
                        BuildTriangles(s, v, n, uv0, uv1, c, indices, topology == MeshTopology.Quads);
                        break;
                    case MeshTopology.Points:
                        BuildPoints(s, v, c, indices);
                        break;
                    case MeshTopology.Lines:
                        for (int i = 0; i + 1 < indices.Length; i += 2) AddSegment(s, v, uv0, c, indices[i], indices[i + 1]);
                        break;
                    case MeshTopology.LineStrip:
                        for (int i = 0; i + 1 < indices.Length; i++) AddSegment(s, v, uv0, c, indices[i], indices[i + 1]);
                        break;
                }
                data.Sections.Add(s);
                data.TriangleCount += s.TriangleCount;
            }
            return data;
        }

        static FLinearColor Col(Color[]? c, int i) => c != null && i < c.Length ? new FLinearColor(c[i].r, c[i].g, c[i].b, c[i].a) : new FLinearColor(1, 1, 1, 1);

        /// <summary>A Unity uv in Unreal's sense: V runs from the top of an image.</summary>
        static FVector2D Uv(Vector4[]? uv, int i) => uv != null && i < uv.Length ? new FVector2D(uv[i].x, 1.0 - uv[i].y) : new FVector2D(0, 1);

        static void BuildTriangles(Section s, Vector3[] v, Vector3[]? n, Vector4[]? uv0, Vector4[]? uv1, Color[]? c, int[] indices, bool quads)
        {
            // Every vertex, in its own order (the streams are per vertex, not per index).
            for (int i = 0; i < v.Length; i++)
            {
                s.Vertices.Add(UnrealSpace.ToUnreal(v[i]));
                s.Normals.Add(n != null && i < n.Length ? UnrealSpace.DirectionToUnreal(n[i]) : new FVector(0, 0, 1));
                s.Uv0.Add(Uv(uv0, i));
                if (uv1 != null) s.Uv1.Add(Uv(uv1, i));
                else if (uv0 != null) s.Uv1.Add(i < uv0.Length ? new FVector2D(uv0[i].z, uv0[i].w) : new FVector2D(0, 0));
                s.Colors.Add(Col(c, i));
            }
            if (quads)
            {
                for (int i = 0; i + 3 < indices.Length; i += 4)
                {
                    AddTri(s, indices[i], indices[i + 1], indices[i + 2]);
                    AddTri(s, indices[i], indices[i + 2], indices[i + 3]);
                }
            }
            else
            {
                for (int i = 0; i + 2 < indices.Length; i += 3) AddTri(s, indices[i], indices[i + 1], indices[i + 2]);
            }
        }

        static void AddTri(Section s, int a, int b, int c)
        {
            s.Triangles.Add(a);
            if (UnrealSpace.FlipWinding) { s.Triangles.Add(c); s.Triangles.Add(b); }
            else { s.Triangles.Add(b); s.Triangles.Add(c); }
        }

        static readonly (float X, float Y)[] Corners = { (-1, -1), (1, -1), (1, 1), (-1, 1) };

        static void BuildPoints(Section s, Vector3[] v, Color[]? c, int[] indices)
        {
            foreach (int i in indices)
            {
                if (i < 0 || i >= v.Length) continue;
                int b = s.Vertices.Count;
                var p = UnrealSpace.ToUnreal(v[i]);
                var normal = UnrealSpace.DirectionToUnreal(v[i].normalized);
                foreach (var (x, y) in Corners)
                {
                    s.Vertices.Add(p);
                    s.Normals.Add(normal);
                    s.Uv0.Add(new FVector2D(x, y));
                    s.Colors.Add(Col(c, i));
                }
                // The point materials are two-sided: one winding is enough.
                s.Triangles.Add(b); s.Triangles.Add(b + 1); s.Triangles.Add(b + 2);
                s.Triangles.Add(b); s.Triangles.Add(b + 2); s.Triangles.Add(b + 3);
            }
        }

        static void AddSegment(Section s, Vector3[] v, Vector4[]? uv0, Color[]? c, int ia, int ib)
        {
            if (ia < 0 || ib < 0 || ia >= v.Length || ib >= v.Length) return;
            var pa = UnrealSpace.ToUnreal(v[ia]);
            var pb = UnrealSpace.ToUnreal(v[ib]);
            var dir = UnrealSpace.DirectionToUnreal((v[ib] - v[ia]).normalized);
            int b0 = s.Vertices.Count;
            void Add(FVector p, FVector other, int src, float side)
            {
                s.Vertices.Add(p);
                s.Normals.Add(dir);
                s.Uv0.Add(Uv(uv0, src));
                s.Uv1.Add(new FVector2D(other.X, other.Y));
                s.Uv2.Add(new FVector2D(other.Z, side));
                s.Colors.Add(Col(c, src));
            }
            // The shader's side is relative to the direction toward the other end, which flips at B: B's sides are
            // swapped so b0..b0+3 are A-left, A-right, B-left, B-right.
            Add(pa, pb, ia, -1);
            Add(pa, pb, ia, 1);
            Add(pb, pa, ib, 1);
            Add(pb, pa, ib, -1);
            s.Triangles.Add(b0); s.Triangles.Add(b0 + 2); s.Triangles.Add(b0 + 1);
            s.Triangles.Add(b0 + 1); s.Triangles.Add(b0 + 2); s.Triangles.Add(b0 + 3);
        }
    }
}
