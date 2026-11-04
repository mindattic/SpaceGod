using System;
using System.Collections.Generic;
using UnrealSharp.Core;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;

namespace UnityEngine
{
    /// <summary>
    /// The ONE place a Unity shader becomes an Unreal material (US-T27), on SpaceGodRender's conventions
    /// (Shared/Plugins/SpaceGodRender/README.md, US-T24). Each of Unity.SpaceGod's shaders ("Unity.SpaceGod/Planet", or
    /// RenderShaders' bare "Planet") is drawn with the plugin's parent material /SpaceGodRender/Materials/M_&lt;Shader&gt;
    /// (the engine's default material if one is ever missing). Parameters keep their Unity names ("_Color"); Unity's
    /// SetColor values go through the sRGB -> linear curve (Unity.SpaceGod is a linear-space project), SetVector values
    /// do not; a float array _Drift[k] is component k % 4 of the vector _Drift_{k / 4}; positions and directions in
    /// Unity's world (<see cref="IsSpatial"/>) are passed in Unreal's axes (UnrealSpace). Globals (Shader.SetGlobal*) go
    /// to MPC_SpaceGod.
    /// </summary>
    public static class ShaderBinding
    {
        public const string MaterialRoot = "/SpaceGodRender/Materials";
        public const string GlobalsCollection = "/SpaceGodRender/Materials/MPC_SpaceGod.MPC_SpaceGod";

        static readonly Dictionary<string, UMaterialInterface?> Parents = new Dictionary<string, UMaterialInterface?>(StringComparer.Ordinal);

        /// <summary>The bare shader name: "Unity.SpaceGod/Planet" -> "Planet".</summary>
        public static string ShaderName(Shader? shader)
        {
            string n = shader?.name ?? "";
            int slash = n.LastIndexOf('/');
            return slash >= 0 ? n.Substring(slash + 1) : n;
        }

        /// <summary>Unity's render queue as Unreal's translucency sort priority (README: Draw order).</summary>
        public static int SortPriority(Shader? shader) => ShaderName(shader) switch
        {
            "Stars" => -1000,
            "Line" => -10,
            "Glow" => 10,
            "Fx" => 20,
            _ => 0,
        };

        /// <summary>Sky and Stars are moved out to _SkyDistance by World Position Offset: never cull them.</summary>
        public static bool IsFarField(Shader? shader) => ShaderName(shader) is "Sky" or "Stars";

        /// <summary>The material Unity's glTF import gave a Kenney craft (ShipModels re-materials them with Ship at once).</summary>
        public const string ImportedShader = "glTF/Imported";

        /// <summary>Drawn with URP Lit (M_Lit, the one material Unity's Light components reach, US-T57): the glTF
        /// import's material (glTFast's glTF-pbrMetallicRoughness shader graph, a URP Lit target; the model viewer shows
        /// it on the raw prefab) and URP's own Lit shader.</summary>
        public static bool IsLit(Shader? shader) => shader?.name == ImportedShader || ShaderName(shader) == "Lit";

        /// <summary>The Unreal parent material for <paramref name="shader"/>.</summary>
        public static UMaterialInterface? Parent(Shader? shader)
        {
            string name = IsLit(shader) ? "Lit" : ShaderName(shader);
            if (Parents.TryGetValue(name, out var cached)) return cached;
            UMaterialInterface? m = TryLoad($"{MaterialRoot}/M_{name}.M_{name}");
            if (m == null)
            {
                m = TryLoad("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial");
                if (shader?.name != ImportedShader) UnityBridge.Log($"ShaderBinding: no {MaterialRoot}/M_{name}; drawing {shader?.name} with the default material", 1);
            }
            Parents[name] = m;
            return m;
        }

        static UMaterialInterface? TryLoad(string path)
        {
            try { return UObject.StaticLoadObject<UMaterialInterface>(path); }
            catch (Exception) { return null; }
        }

        /// <summary>Vector properties holding a position or direction in Unity's world (converted to Unreal axes).</summary>
        public static bool IsSpatial(string unityName) => unityName switch
        {
            "_AxisX" or "_AxisY" or "_AxisZ" or "_PlanetPosRadius" or "_SunRelative" or "_SunPositionWS" or "_Sun2PositionWS" => true,
            _ => false,
        };

        static float ToLinear(float c) => c <= 0.04045f ? c / 12.92f : Mathf.Pow((c + 0.055f) / 1.055f, 2.4f);

        internal static FLinearColor ToUnreal(string unityName, Vector4 v, bool isColor)
        {
            if (isColor) return new FLinearColor(ToLinear(v.x), ToLinear(v.y), ToLinear(v.z), v.w);
            if (!IsSpatial(unityName)) return new FLinearColor(v.x, v.y, v.z, v.w);
            var p = UnrealSpace.ToUnreal(new Vector3(v.x, v.y, v.z));
            return new FLinearColor((float)p.X, (float)p.Y, (float)p.Z, v.w);
        }

        /// <summary>Unity's default for a colour property never set (white, as the shaders' Properties blocks declare).</summary>
        public static Color DefaultColor(Shader? shader, string name) => Color.white;

        public static bool HasProperty(Shader? shader, string name) => name == "_Color" || name == "_MainTex";

        static void SetArray(UMaterialInstanceDynamic mid, string name, float[] values)
        {
            for (int k = 0; k * 4 < values.Length; k++)
            {
                float At(int i) => i < values.Length ? values[i] : 0f;
                mid.SetVectorParameterValue(new FName($"{name}_{k}"), new FLinearColor(At(k * 4), At(k * 4 + 1), At(k * 4 + 2), At(k * 4 + 3)));
            }
        }

        /// <summary>Pushes <paramref name="m"/>'s values into its Unreal instance.</summary>
        internal static void Push(Material m, UMaterialInstanceDynamic mid)
        {
            foreach (var kv in m.Floats) mid.SetScalarParameterValue(new FName(kv.Key), kv.Value);
            foreach (var kv in m.Vectors) mid.SetVectorParameterValue(new FName(kv.Key), ToUnreal(kv.Key, kv.Value, m.ColorNames.Contains(kv.Key)));
            foreach (var kv in m.Textures)
            {
                var t = kv.Value?.Unreal;
                if (t != null) mid.SetTextureParameterValue(new FName(kv.Key), t);
            }
            foreach (var kv in m.Arrays) SetArray(mid, kv.Key, kv.Value);
        }

        internal static void Push(MaterialPropertyBlock b, UMaterialInstanceDynamic mid)
        {
            foreach (var kv in b.Floats) mid.SetScalarParameterValue(new FName(kv.Key), kv.Value);
            foreach (var kv in b.Vectors) mid.SetVectorParameterValue(new FName(kv.Key), ToUnreal(kv.Key, kv.Value, b.ColorNames.Contains(kv.Key)));
            foreach (var kv in b.Textures)
            {
                var t = kv.Value?.Unreal;
                if (t != null) mid.SetTextureParameterValue(new FName(kv.Key), t);
            }
        }
    }
}
