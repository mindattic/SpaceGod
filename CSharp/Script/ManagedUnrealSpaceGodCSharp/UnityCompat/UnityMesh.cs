using System;
using System.Collections.Generic;
using UnityEngine.Rendering;

namespace UnityEngine.Rendering
{
    public enum IndexFormat { UInt16, UInt32 }
    public enum ShadowCastingMode { Off, On, TwoSided, ShadowsOnly }
}

namespace UnityEngine
{
    public enum MeshTopology { Triangles = 0, Quads = 2, Lines = 3, LineStrip = 4, Points = 5 }

    /// <summary>
    /// Unity's Mesh: vertex streams and per-submesh indices, as the ported code sets them. Unreal has no line or point
    /// topology, so <see cref="UnityBridge"/> expands Lines/LineStrip/Points into camera-facing quads for the materials
    /// to widen to a pixel (the convention is in <see cref="UnrealMeshData"/>). A mesh can instead stand for an
    /// imported Unreal static mesh (a Kenney craft), see <see cref="StaticMesh"/>.
    /// </summary>
    public sealed class Mesh : Object
    {
        Vector3[] verticesValue = Array.Empty<Vector3>();
        Vector3[]? normalsValue;
        Vector4[]? tangentsValue;
        readonly Vector4[]?[] uvs = new Vector4[]?[4];
        readonly int[] uvDims = new int[4];
        Color[]? colorsValue;
        readonly List<(int[] Indices, MeshTopology Topology)> subMeshes = new List<(int[], MeshTopology)>();
        Bounds boundsValue;
        bool boundsSet;

        /// <summary>Bumped on every change; UnityBridge rebuilds Unreal sections when it moves.</summary>
        internal int version;
        void Touch() => version++;

        /// <summary>An imported Unreal static mesh this stands for (null for a procedural one).</summary>
        public UnrealSharp.Engine.UStaticMesh? StaticMesh { get; internal set; }

        /// <summary>For an imported static mesh: the rotation and scale (Unity space) that bring Unreal's import of the
        /// model back to the mesh Unity's importer made (one unit long on +Z, see Resources' ShipModels.json).</summary>
        internal (Quaternion Rotation, Vector3 Scale)? Correction { get; set; }

        public Mesh() { }

        public IndexFormat indexFormat { get; set; }
        public void MarkDynamic() { }
        public bool isReadable => true;
        public int vertexCount => verticesValue.Length;
        public int subMeshCount
        {
            get => Math.Max(1, subMeshes.Count);
            set
            {
                while (subMeshes.Count < value) subMeshes.Add((Array.Empty<int>(), MeshTopology.Triangles));
                while (subMeshes.Count > value) subMeshes.RemoveAt(subMeshes.Count - 1);
                Touch();
            }
        }

        public Vector3[] vertices
        {
            get => (Vector3[])verticesValue.Clone();
            set
            {
                verticesValue = value != null ? (Vector3[])value.Clone() : Array.Empty<Vector3>();
                Touch();
            }
        }
        public Vector3[] normals
        {
            get => normalsValue != null ? (Vector3[])normalsValue.Clone() : Array.Empty<Vector3>();
            set { normalsValue = value != null && value.Length > 0 ? (Vector3[])value.Clone() : null; Touch(); }
        }
        public Vector4[] tangents
        {
            get => tangentsValue != null ? (Vector4[])tangentsValue.Clone() : Array.Empty<Vector4>();
            set { tangentsValue = value != null && value.Length > 0 ? (Vector4[])value.Clone() : null; Touch(); }
        }
        public Vector2[] uv { get => GetUv2(0); set => SetUv(0, value); }
        public Vector2[] uv2 { get => GetUv2(1); set => SetUv(1, value); }
        public Vector2[] uv3 { get => GetUv2(2); set => SetUv(2, value); }
        public Vector2[] uv4 { get => GetUv2(3); set => SetUv(3, value); }
        public Color[] colors
        {
            get => colorsValue != null ? (Color[])colorsValue.Clone() : Array.Empty<Color>();
            set { colorsValue = value != null && value.Length > 0 ? (Color[])value.Clone() : null; Touch(); }
        }
        public Color32[] colors32
        {
            get
            {
                var c = colors;
                var r = new Color32[c.Length];
                for (int i = 0; i < c.Length; i++) r[i] = c[i];
                return r;
            }
            set
            {
                if (value == null || value.Length == 0) { colorsValue = null; Touch(); return; }
                colorsValue = new Color[value.Length];
                for (int i = 0; i < value.Length; i++) colorsValue[i] = value[i];
                Touch();
            }
        }
        public int[] triangles
        {
            get
            {
                var all = new List<int>();
                foreach (var s in subMeshes) if (s.Topology == MeshTopology.Triangles) all.AddRange(s.Indices);
                return all.ToArray();
            }
            set
            {
                subMeshes.Clear();
                subMeshes.Add(((int[])value.Clone(), MeshTopology.Triangles));
                if (!boundsSet) RecalculateBoundsInternal();
                Touch();
            }
        }

        Vector2[] GetUv2(int channel)
        {
            var src = uvs[channel];
            if (src == null) return Array.Empty<Vector2>();
            var r = new Vector2[src.Length];
            for (int i = 0; i < src.Length; i++) r[i] = new Vector2(src[i].x, src[i].y);
            return r;
        }
        void SetUv(int channel, Vector2[]? value)
        {
            if (value == null || value.Length == 0) { uvs[channel] = null; Touch(); return; }
            var r = new Vector4[value.Length];
            for (int i = 0; i < value.Length; i++) r[i] = new Vector4(value[i].x, value[i].y, 0, 0);
            uvs[channel] = r;
            uvDims[channel] = 2;
            Touch();
        }

        public void SetVertices(List<Vector3> v) { verticesValue = v.ToArray(); Touch(); }
        public void SetVertices(Vector3[] v) { verticesValue = (Vector3[])v.Clone(); Touch(); }
        public void SetVertices(Vector3[] v, int start, int length) { verticesValue = new ArraySegment<Vector3>(v, start, length).ToArray(); Touch(); }
        public void SetNormals(List<Vector3> n) { normalsValue = n.ToArray(); Touch(); }
        public void SetNormals(Vector3[] n) { normalsValue = (Vector3[])n.Clone(); Touch(); }
        public void SetTangents(List<Vector4> t) { tangentsValue = t.ToArray(); Touch(); }
        public void SetColors(List<Color> c) { colorsValue = c.ToArray(); Touch(); }
        public void SetColors(Color[] c) { colorsValue = (Color[])c.Clone(); Touch(); }
        public void SetColors(Color[] c, int start, int length) { colorsValue = new ArraySegment<Color>(c, start, length).ToArray(); Touch(); }
        public void SetColors(List<Color32> c)
        {
            colorsValue = new Color[c.Count];
            for (int i = 0; i < c.Count; i++) colorsValue[i] = c[i];
            Touch();
        }
        public void SetUVs(int channel, List<Vector2> u) => SetUv(channel, u.ToArray());
        public void SetUVs(int channel, Vector2[] u) => SetUv(channel, u);
        public void SetUVs(int channel, List<Vector3> u)
        {
            var r = new Vector4[u.Count];
            for (int i = 0; i < u.Count; i++) r[i] = u[i];
            uvs[channel] = r;
            uvDims[channel] = 3;
            Touch();
        }
        public void SetUVs(int channel, List<Vector4> u)
        {
            uvs[channel] = u.ToArray();
            uvDims[channel] = 4;
            Touch();
        }
        public void SetUVs(int channel, Vector4[] u)
        {
            uvs[channel] = (Vector4[])u.Clone();
            uvDims[channel] = 4;
            Touch();
        }

        public void SetTriangles(int[] t, int submesh, bool calculateBounds = true) => SetIndices(t, MeshTopology.Triangles, submesh, calculateBounds);
        public void SetTriangles(List<int> t, int submesh, bool calculateBounds = true) => SetIndices(t.ToArray(), MeshTopology.Triangles, submesh, calculateBounds);
        public void SetIndices(int[] indices, MeshTopology topology, int submesh, bool calculateBounds = true)
        {
            while (subMeshes.Count <= submesh) subMeshes.Add((Array.Empty<int>(), MeshTopology.Triangles));
            subMeshes[submesh] = ((int[])indices.Clone(), topology);
            if (calculateBounds) RecalculateBoundsInternal();
            Touch();
        }
        public void SetIndices(List<int> indices, MeshTopology topology, int submesh, bool calculateBounds = true) =>
            SetIndices(indices.ToArray(), topology, submesh, calculateBounds);
        public void SetIndices(int[] indices, int start, int length, MeshTopology topology, int submesh, bool calculateBounds = true) =>
            SetIndices(new ArraySegment<int>(indices, start, length).ToArray(), topology, submesh, calculateBounds);
        public int[] GetIndices(int submesh) => submesh < subMeshes.Count ? (int[])subMeshes[submesh].Indices.Clone() : Array.Empty<int>();
        public int[] GetTriangles(int submesh) => GetIndices(submesh);
        public MeshTopology GetTopology(int submesh) => submesh < subMeshes.Count ? subMeshes[submesh].Topology : MeshTopology.Triangles;
        public void GetVertices(List<Vector3> into) { into.Clear(); into.AddRange(verticesValue); }

        public void Clear() => Clear(true);
        public void Clear(bool keepVertexLayout)
        {
            verticesValue = Array.Empty<Vector3>();
            normalsValue = null;
            tangentsValue = null;
            for (int i = 0; i < uvs.Length; i++) uvs[i] = null;
            colorsValue = null;
            subMeshes.Clear();
            Touch();
        }

        public Bounds bounds
        {
            get => boundsValue;
            set { boundsValue = value; boundsSet = true; }
        }
        public void RecalculateBounds() => RecalculateBoundsInternal();
        void RecalculateBoundsInternal()
        {
            if (verticesValue.Length == 0) { boundsValue = new Bounds(Vector3.zero, Vector3.zero); return; }
            var min = verticesValue[0];
            var max = verticesValue[0];
            foreach (var v in verticesValue) { min = Vector3.Min(min, v); max = Vector3.Max(max, v); }
            var b = new Bounds();
            b.SetMinMax(min, max);
            boundsValue = b;
        }
        public void RecalculateNormals()
        {
            var n = new Vector3[verticesValue.Length];
            foreach (var s in subMeshes)
            {
                if (s.Topology != MeshTopology.Triangles) continue;
                var t = s.Indices;
                for (int i = 0; i + 2 < t.Length; i += 3)
                {
                    var a = verticesValue[t[i]];
                    var face = Vector3.Cross(verticesValue[t[i + 1]] - a, verticesValue[t[i + 2]] - a);
                    n[t[i]] += face;
                    n[t[i + 1]] += face;
                    n[t[i + 2]] += face;
                }
            }
            for (int i = 0; i < n.Length; i++) n[i] = n[i].normalized;
            normalsValue = n;
            Touch();
        }
        public void RecalculateTangents() { }
        public void UploadMeshData(bool markNoLongerReadable) { }
        public void Optimize() { }

        internal Vector3[] RawVertices => verticesValue;
        internal Vector3[]? RawNormals => normalsValue;
        internal Vector4[]? RawUv(int channel) => uvs[channel];
        internal Color[]? RawColors => colorsValue;
        internal IReadOnlyList<(int[] Indices, MeshTopology Topology)> RawSubMeshes => subMeshes;

        UnrealMeshData? built;
        int builtVersion = -1;

        /// <summary>This mesh as Unreal sections (one per submesh), rebuilt only after it changes.</summary>
        internal UnrealMeshData Unreal
        {
            get
            {
                if (built == null || builtVersion != version)
                {
                    built = UnrealMeshData.Build(this);
                    builtVersion = version;
                }
                return built;
            }
        }

        internal override Object CloneInternal(Transform? parent, bool worldSpace)
        {
            var m = new Mesh { name = name, StaticMesh = StaticMesh, indexFormat = indexFormat, Correction = Correction };
            m.verticesValue = (Vector3[])verticesValue.Clone();
            m.normalsValue = (Vector3[]?)normalsValue?.Clone();
            m.colorsValue = (Color[]?)colorsValue?.Clone();
            for (int i = 0; i < 4; i++) { m.uvs[i] = (Vector4[]?)uvs[i]?.Clone(); m.uvDims[i] = uvDims[i]; }
            foreach (var s in subMeshes) m.subMeshes.Add(((int[])s.Indices.Clone(), s.Topology));
            m.boundsValue = boundsValue;
            m.boundsSet = boundsSet;
            return m;
        }
    }

    public class MeshFilter : Component
    {
        Mesh? meshValue;
        public Mesh? sharedMesh { get => meshValue; set => meshValue = value; }
        /// <summary>Unity's per-filter copy on first access.</summary>
        public Mesh? mesh
        {
            get
            {
                if (meshValue != null && !ownsMesh)
                {
                    meshValue = Object.Instantiate(meshValue);
                    ownsMesh = true;
                }
                return meshValue;
            }
            set { meshValue = value; ownsMesh = true; }
        }
        bool ownsMesh;
        internal override Component CloneInternalComponent()
        {
            var c = (MeshFilter)MemberwiseClone();
            c.ownsMesh = false;
            return c;
        }
    }
}
