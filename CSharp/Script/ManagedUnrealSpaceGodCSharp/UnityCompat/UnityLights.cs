using System;
using System.Collections.Generic;
using UnrealSharp.Core;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;

namespace UnityEngine
{
    /// <summary>
    /// Unity's Light components as URP's forward renderer applies them (US-T57). In Unity.SpaceGod only URP Lit
    /// materials take lights (the imported glTF craft in the model viewer); the game's own shaders are unlit and light
    /// themselves from the Sun globals, so a Light touches nothing else, here as there. Per frame: the enabled
    /// directional Lights the screen camera can see (Light.cullingMask overlapping the camera's), and for a renderer on
    /// layer L those whose culling mask has L; URP's main light is the brightest (RenderSettings.sun is never set), the
    /// rest are additional lights. Each goes to M_Lit's light slots (Shared/Plugins/SpaceGodRender/Shaders/Lit.ush) as
    /// the direction towards the light and Light.color.linear * intensity (URP: lightsUseLinearIntensity).
    /// </summary>
    internal static class UnityLights
    {
        /// <summary>M_Lit's slots: the main light, then two additional ones.</summary>
        static readonly string[] Slots = { "_MainLight", "_AdditionalLight0", "_AdditionalLight1" };
        static readonly FName[] Names = BuildNames();

        static Camera? camera;
        static List<Light>? visible;
        static readonly Dictionary<int, FLinearColor[]> byLayer = new Dictionary<int, FLinearColor[]>();
        static readonly Dictionary<UMaterialInstanceDynamic, FLinearColor[]> pushed =
            new Dictionary<UMaterialInstanceDynamic, FLinearColor[]>(ReferenceEqualityComparer.Instance);

        static FName[] BuildNames()
        {
            var n = new FName[Slots.Length * 2];
            for (int i = 0; i < Slots.Length; i++)
            {
                n[i * 2] = new FName(Slots[i] + "Direction");
                n[i * 2 + 1] = new FName(Slots[i] + "Color");
            }
            return n;
        }

        /// <summary>Start of UnityBridge.Sync: the lights are gathered again (lazily) for the camera drawing the screen.</summary>
        public static void BeginFrame(Camera? screenCamera)
        {
            camera = screenCamera;
            visible = null;
            byLayer.Clear();
        }

        /// <summary>A material drawn with URP Lit (M_Lit), which Unity's lights reach.</summary>
        public static bool IsLit(Material? m) => m != null && ShaderBinding.IsLit(m.shader);

        static List<Light> Visible()
        {
            if (visible != null) return visible;
            visible = new List<Light>();
            int cameraMask = camera?.cullingMask ?? -1;
            foreach (var l in Object.FindObjectsByType<Light>(FindObjectsSortMode.None))
            {
                if (!l.enabled || l.type != LightType.Directional || l.intensity <= 0) continue;
                if ((l.cullingMask & cameraMask) == 0) continue;
                visible.Add(l);
            }
            // URP's GetMainLightIndex: RenderSettings.sun, else the brightest directional light (the first of equals).
            int main = -1;
            for (int i = 0; i < visible.Count; i++)
                if (main < 0 || visible[i].intensity > visible[main].intensity) main = i;
            if (main > 0)
            {
                var m = visible[main];
                visible.RemoveAt(main);
                visible.Insert(0, m);
            }
            return visible;
        }

        static float Linear(float c) => c <= 0.04045f ? c / 12.92f : Mathf.Pow((c + 0.055f) / 1.055f, 2.4f);

        static FLinearColor[] For(int layer)
        {
            if (byLayer.TryGetValue(layer, out var v)) return v;
            v = new FLinearColor[Names.Length];
            int slot = 0;
            foreach (var l in Visible())
            {
                if (((l.cullingMask >> (layer & 31)) & 1) == 0) continue;
                if (slot >= Slots.Length) break;
                // Towards the light: a directional light shines along its transform's forward.
                var d = UnrealSpace.ToUnreal(-l.transform.forward);
                var c = l.color;
                v[slot * 2] = new FLinearColor((float)d.X, (float)d.Y, (float)d.Z, 0);
                v[slot * 2 + 1] = new FLinearColor(Linear(c.r) * l.intensity, Linear(c.g) * l.intensity, Linear(c.b) * l.intensity, 1);
                slot++;
            }
            byLayer[layer] = v;
            return v;
        }

        /// <summary>Puts the lights of <paramref name="layer"/> into a Lit material instance (only when they changed).</summary>
        public static void Apply(UMaterialInstanceDynamic mid, int layer)
        {
            var v = For(layer);
            if (pushed.TryGetValue(mid, out var old) && Same(old, v)) return;
            for (int i = 0; i < v.Length; i++) mid.SetVectorParameterValue(Names[i], v[i]);
            pushed[mid] = v;
        }

        static bool Same(FLinearColor[] a, FLinearColor[] b)
        {
            for (int i = 0; i < a.Length; i++)
                if (a[i].R != b[i].R || a[i].G != b[i].G || a[i].B != b[i].B || a[i].A != b[i].A) return false;
            return true;
        }
    }
}
