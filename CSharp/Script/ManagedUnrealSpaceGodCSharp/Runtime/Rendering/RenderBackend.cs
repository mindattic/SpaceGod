using System;
using System.Collections.Generic;
using Unity.SpaceGod.Core;
using UnityEngine;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;

namespace Unity.SpaceGod.Rendering
{
    // Port of Unity.SpaceGod's Runtime/Rendering/Pipeline/RenderBackend.cs (US-T27). The original's interface, roles,
    // lighting and post-process types are kept as they are; what Unity split across URP and HDRP backends
    // (Pipeline/Urp, Pipeline/Hdrp, RenderPipelineSelection) is, in Unreal, one renderer, so the only backend is
    // UnrealRenderBackend below: its intent mapped onto Unreal's post-process settings (see SetupCamera).

    /// <summary>What a camera is for, so the pipeline can set it up as that pipeline needs (U.SPC-T429).</summary>
    public enum CameraRole
    {
        /// <summary>The game's own camera, drawing the universe to the screen.</summary>
        Main,

        /// <summary>A camera drawn over the game's own on the screen (the model viewer).</summary>
        Overlay,

        /// <summary>A camera rendered by hand into a texture (ship thumbnails), never part of the frame.</summary>
        Offscreen,
    }

    /// <summary>
    /// The light the hand-written shaders read (U.SPC-T429): the nearest star and a companion, in camera-relative world
    /// space (a position's w is unused; a colour's w is its strength). Set through <see cref="IRenderBackend.Lighting"/>.
    /// </summary>
    public struct StarLighting
    {
        public Vector4 SunPosition, SunColor, Sun2Position, Sun2Color;
    }

    /// <summary>
    /// A post-process look in pipeline-neutral terms (U.SPC-T429): each pipeline turns it into its own volume and
    /// overrides. <see cref="None"/> (the default) means no post-processing at all, which is how the game draws today.
    /// </summary>
    public struct PostProcessLook
    {
        public bool Enabled;

        /// <summary>Exposure compensation, in stops.</summary>
        public float ExposureEv;

        /// <summary>Bloom strength (0 for none) and the brightness it starts at.</summary>
        public float BloomIntensity, BloomThreshold;

        /// <summary>Contrast and saturation, -100 to 100 (0 leaves the image as it is).</summary>
        public float Contrast, Saturation;

        public static PostProcessLook None => default;
    }

    /// <summary>
    /// Every pipeline-specific piece of rendering, behind one interface (U.SPC-T429): the game's shaders, its cameras,
    /// the lights of preview scenes, the star lighting its shaders read, post-processing and renderer features. Game
    /// code reaches the pipeline only through <see cref="RenderBackend.Active"/>.
    /// </summary>
    public interface IRenderBackend
    {
        RenderPipelineProfile Profile { get; }

        /// <summary>This pipeline's version of the game's shader <paramref name="name"/> (one of <see cref="RenderShaders"/>).</summary>
        Shader LoadShader(string name);

        /// <summary>Readies <paramref name="camera"/> for its <paramref name="role"/> under this pipeline.</summary>
        void SetupCamera(Camera camera, CameraRole role);

        /// <summary>Turns multisample anti-aliasing on or off for <paramref name="camera"/>.</summary>
        void SetMsaa(Camera camera, bool on);

        /// <summary>Adds a directional key light under <paramref name="parent"/> that lights only <paramref name="cullingMask"/>.</summary>
        Light AddKeyLight(Transform parent, string name, Quaternion rotation, float intensity, int cullingMask, HideFlags hideFlags = HideFlags.None);

        /// <summary>The star light the game's shaders read, for every camera.</summary>
        StarLighting Lighting { get; set; }

        /// <summary>Applies <paramref name="look"/> to what <paramref name="camera"/> draws (<see cref="PostProcessLook.None"/> turns it off).</summary>
        void SetPostProcessing(Camera camera, PostProcessLook look);

        /// <summary>The renderer features the active renderer carries, by name.</summary>
        IReadOnlyList<string> RenderFeatures { get; }

        /// <summary>The photoreal choices of the settings menu (U.SPC-T430), kept to report back.</summary>
        PhotorealOptions Photoreal { get; set; }

        /// <summary>The planet atmosphere the game's own camera is at, for a physically based sky (U.SPC-T430).</summary>
        void SetAtmosphere(Camera camera, AtmosphereShell shell, Vector3 centre);
    }

    /// <summary>
    /// The active backend (U.SPC-T429). Unreal has one renderer: <see cref="UnrealRenderBackend"/> on the default profile,
    /// unless something registers another with <see cref="Use"/>.
    /// </summary>
    public static class RenderBackend
    {
        static IRenderBackend active;

        public static IRenderBackend Active => active ??= new UnrealRenderBackend(RenderPipelineProfiles.Default);

        /// <summary>Puts <paramref name="backend"/> in charge (tests, a replacement backend).</summary>
        public static void Use(IRenderBackend backend) => active = backend;

        /// <summary>Forgets the active backend, so the next use makes it afresh.</summary>
        public static void Reset() => active = null;
    }

    /// <summary>
    /// What every pipeline does the same way (U.SPC-T429): shaders by name, a plain directional light, the star lighting
    /// as shader globals (MPC_SpaceGod in Unreal, see ShaderBinding), and no post-processing or renderer features.
    /// </summary>
    public class NeutralRenderBackend : IRenderBackend
    {
        static readonly int SunPositionId = Shader.PropertyToID(RenderGlobals.SunPosition), SunColorId = Shader.PropertyToID(RenderGlobals.SunColor);
        static readonly int Sun2PositionId = Shader.PropertyToID(RenderGlobals.Sun2Position), Sun2ColorId = Shader.PropertyToID(RenderGlobals.Sun2Color);

        public NeutralRenderBackend(RenderPipelineProfile profile) => Profile = profile ?? throw new ArgumentNullException(nameof(profile));

        public RenderPipelineProfile Profile { get; }

        /// <summary>Whether MSAA was last asked for (the settings menu reads it back).</summary>
        public bool Msaa { get; private set; } = true;

        /// <summary>The shader by its Unity name; ShaderBinding finds SpaceGodRender's M_&lt;name&gt; for it.</summary>
        public virtual Shader LoadShader(string name) => Shader.Find("Unity.SpaceGod/" + name);

        public virtual void SetupCamera(Camera camera, CameraRole role) { }

        public virtual void SetMsaa(Camera camera, bool on) => Msaa = on;

        public virtual Light AddKeyLight(Transform parent, string name, Quaternion rotation, float intensity, int cullingMask, HideFlags hideFlags = HideFlags.None)
        {
            var light = new GameObject(name) { hideFlags = hideFlags }.AddComponent<Light>();
            light.transform.SetParent(parent, false);
            light.type = LightType.Directional;
            light.transform.rotation = rotation;
            light.intensity = intensity;
            light.cullingMask = cullingMask;
            return light;
        }

        public virtual StarLighting Lighting
        {
            get => new StarLighting
            {
                SunPosition = Shader.GetGlobalVector(SunPositionId), SunColor = Shader.GetGlobalVector(SunColorId),
                Sun2Position = Shader.GetGlobalVector(Sun2PositionId), Sun2Color = Shader.GetGlobalVector(Sun2ColorId),
            };
            set
            {
                Shader.SetGlobalVector(SunPositionId, value.SunPosition);
                Shader.SetGlobalVector(SunColorId, value.SunColor);
                Shader.SetGlobalVector(Sun2PositionId, value.Sun2Position);
                Shader.SetGlobalVector(Sun2ColorId, value.Sun2Color);
            }
        }

        public virtual void SetPostProcessing(Camera camera, PostProcessLook look) { }

        public virtual IReadOnlyList<string> RenderFeatures => Array.Empty<string>();

        public virtual PhotorealOptions Photoreal { get; set; } = PhotorealOptions.Default;

        public virtual void SetAtmosphere(Camera camera, AtmosphereShell shell, Vector3 centre) { }
    }

    /// <summary>
    /// Unity.SpaceGod's render-pipeline intent on Unreal's renderer. The hand-written shaders draw straight to the
    /// display's range, as the game's URP camera rendered LDR with no post-processing (<see cref="PostProcessLook.None"/>):
    /// so the main camera gets a fixed exposure (manual metering at 0 EV; SpaceGodRender's SG_Expose divides by Unreal's
    /// exposure anyway), a linear tone curve (ToneCurveAmount 0, ExpandGamut 0, as SpaceGodRender's README asks), no
    /// bloom, no motion blur, and FXAA instead of TAA/TSR, which smear one-pixel points and lines. A
    /// <see cref="PostProcessLook"/> that is enabled (URP's Volume stack) maps to exposure compensation, bloom
    /// intensity/threshold and colour contrast/saturation.
    /// </summary>
    public sealed class UnrealRenderBackend : NeutralRenderBackend
    {
        public UnrealRenderBackend(RenderPipelineProfile profile) : base(profile) { }

        bool cvarsSet;

        /// <summary>The engine-wide part of the look (anti-aliasing and the defaults no camera overrides).</summary>
        void SetCvars()
        {
            if (cvarsSet || !UnityBridge.HasHost) return;
            cvarsSet = true;
            foreach (var cmd in new[] { "r.AntiAliasingMethod 1", "r.DefaultFeature.AutoExposure 0", "r.DefaultFeature.Bloom 0", "r.DefaultFeature.MotionBlur 0" })
            {
                try { SystemLibrary.ExecuteConsoleCommand(cmd); }
                catch (Exception e) { Debug.LogWarning("[Unity.SpaceGod] " + cmd + ": " + e.Message); }
            }
        }

        public override void SetupCamera(Camera camera, CameraRole role)
        {
            SetCvars();
            if (role == CameraRole.Offscreen) return;
            Apply(camera, PostProcessLook.None);
        }

        public override void SetMsaa(Camera camera, bool on)
        {
            base.SetMsaa(camera, on);
            // MSAA is a forward-renderer feature in Unreal; the deferred path's nearest is FXAA (on) or none (off).
            if (UnityBridge.HasHost) SystemLibrary.ExecuteConsoleCommand(on ? "r.AntiAliasingMethod 1" : "r.AntiAliasingMethod 0");
        }

        public override void SetPostProcessing(Camera camera, PostProcessLook look) => Apply(camera, look);

        static void Apply(Camera camera, PostProcessLook look)
        {
            var cam = camera?.Unreal;
            if (cam == null) return;
            var pp = cam.PostProcessSettings;
            pp.Override_ToneCurveAmount = true;
            pp.ToneCurveAmount = 0f;
            pp.Override_ExpandGamut = true;
            pp.ExpandGamut = 0f;
            pp.Override_AutoExposureMethod = true;
            pp.MeteringMode = EAutoExposureMethod.AEM_Manual;
            pp.Override_AutoExposureBias = true;
            pp.ExposureCompensation = look.Enabled ? look.ExposureEv : 0f;
            pp.Override_AutoExposureApplyPhysicalCameraExposure = true;
            pp.ApplyPhysicalCameraExposure = false;
            pp.Override_BloomIntensity = true;
            pp.BloomIntensity = look.Enabled ? look.BloomIntensity : 0f;
            pp.Override_BloomThreshold = true;
            pp.BloomThreshold = look.Enabled ? look.BloomThreshold : -1f;
            pp.Override_MotionBlurAmount = true;
            pp.Amount = 0f;
            pp.Override_ColorContrast = true;
            float contrast = look.Enabled ? 1f + look.Contrast / 100f : 1f;
            pp.ColorContrast = new FVector4(contrast, contrast, contrast, 1);
            pp.Override_ColorSaturation = true;
            float saturation = look.Enabled ? 1f + look.Saturation / 100f : 1f;
            pp.ColorSaturation = new FVector4(saturation, saturation, saturation, 1);
            cam.PostProcessSettings = pp;
            cam.PostProcessBlendWeight = 1f;
        }
    }
}
