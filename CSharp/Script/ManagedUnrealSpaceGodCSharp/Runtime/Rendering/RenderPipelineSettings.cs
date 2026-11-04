using System;
using System.Globalization;
using Unity.SpaceGod.Core;

namespace Unity.SpaceGod.Rendering
{
    // Port of Unity.SpaceGod's Runtime/Rendering/Pipeline/RenderPipelineSelection.cs (US-T51), the settings half as it is.
    // RenderPipelineSelection (restarting into the other pipeline's player, or switching quality levels in the editor) has
    // nothing to do in Unreal, which runs one renderer: as on the C++ track (Rendering/Pipeline/RenderBackend.cpp), the
    // pipeline choice is kept and reported (Chosen, RestartPending) but restarts nothing, and the photoreal choices go to
    // RenderBackend.Active, which keeps them (HDRP's extras are "Ignored on URP", and Unreal runs URP's profile).

    /// <summary>
    /// The render-pipeline settings (U.SPC-T430): which pipeline the game runs on (URP, the default and the lighter
    /// one, or HDRP, the photoreal path), applied the next time the game starts, and the photoreal choices a pipeline
    /// that offers them puts into effect at once (a physically based sky, volumetric light, how the eye is metered for
    /// the star's light, and ray tracing). Each is remembered per profile like every other setting.
    /// </summary>
    [SettingsSource]
    public static class RenderPipelineSettings
    {
        /// <summary>The key the pipeline choice is kept under (read at start-up, before the settings are bound).</summary>
        public const string PipelineKey = "render.pipeline";

        static readonly string[] PipelineNames = MenuNames();

        static string[] MenuNames()
        {
            var all = RenderPipelineProfiles.All;
            var names = new string[all.Count];
            for (int i = 0; i < names.Length; i++) names[i] = all[i].MenuName;
            return names;
        }

        static int IndexOf(RenderPipelineProfile profile) => Math.Max(0, Array.IndexOf(PipelineNames, profile.MenuName));

        public static readonly ChoiceSetting Pipeline = new ChoiceSetting(PipelineKey, SettingGroups.RenderPipeline,
            "Render pipeline", PipelineNames, IndexOf(RenderPipelineProfiles.Default),
            "URP is the default and the lighter choice. HDRP is the photoreal path: a physically based sky and volumetric light in a " +
            "planet's atmosphere, exposure metered for the star's light, and optional ray tracing; it needs a GPU with compute shaders. " +
            "Takes effect the next time the game starts (the game restarts into its HDRP build, installed beside it).",
            "hdrp urp photoreal high definition universal restart quality");

        public static readonly ToggleSetting PhysicalSky = new ToggleSetting("render.hdrp.sky", SettingGroups.RenderPipeline,
            "Physically based sky (HDRP)", PhotorealOptions.Default.PhysicalSky,
            "While the ship you fly or follow comes down through a docking ring, sits at its city or climbs back, HDRP draws that " +
            "world's atmosphere as light really scatters in it: blue overhead, red at sunset, the haze below. Everywhere else the " +
            "planets keep their own atmosphere rims. Ignored on URP.", "atmosphere scattering sky rayleigh docking ring descent entry hdrp").Applying(_ => Apply());

        public static readonly ToggleSetting Volumetrics = new ToggleSetting("render.hdrp.volumetrics", SettingGroups.RenderPipeline,
            "Volumetric light (HDRP)", PhotorealOptions.Default.Volumetrics,
            "During a docking-ring descent the air itself catches the star's light, in shafts and haze. Costs GPU time. Ignored on URP.",
            "fog god rays sunbeams haze volumetric hdrp").Applying(_ => Apply());

        public static readonly ChoiceSetting Exposure = new ChoiceSetting("render.hdrp.exposure", SettingGroups.RenderPipeline,
            "Sun exposure (HDRP)", new[] { "Fixed for sunlight", "Adaptive (eye)" }, (int)PhotorealOptions.Default.Exposure,
            "Fixed meters the image once for the star's light (EV 15), so it looks as drawn. Adaptive meters what is in view like an " +
            "eye: the star's glare in frame stops the picture down, and it opens up again in shadow. Ignored on URP.",
            "exposure eye adaptation auto exposure ev metering glare hdrp").Applying(_ => Apply());

        public static readonly RangeSetting ExposureCompensation = new RangeSetting("render.hdrp.exposureCompensation", SettingGroups.RenderPipeline,
            "Exposure compensation (HDRP)", PhotorealOptions.Default.ExposureCompensation, -3, 3, 0.25,
            "Brightens (+) or darkens (-) the whole image by stops on top of the metering. Ignored on URP.",
            v => (v > 0 ? "+" : "") + v.ToString("0.##", CultureInfo.InvariantCulture) + " EV", "brightness exposure ev hdrp").Applying(_ => Apply());

        public static readonly RangeSetting SunBrightness = new RangeSetting("render.hdrp.sunBrightness", SettingGroups.RenderPipeline,
            "Star glare (HDRP)", PhotorealOptions.Default.SunBrightness, 1, 64, 1,
            "How much brighter than a sunlit surface the star's face is drawn, so it blooms and, with adaptive exposure, dazzles. Ignored on URP.",
            v => v.ToString("0", CultureInfo.InvariantCulture) + "x", "sun glare bloom brightness hdrp").Applying(_ => Apply());

        public static readonly ToggleSetting RayTracing = new ToggleSetting("render.hdrp.rayTracing", SettingGroups.RenderPipeline,
            "Ray tracing (HDRP)", PhotorealOptions.Default.RayTracing,
            "Ray-traced shadows and reflections from the star, on a GPU and graphics API (DirectX 12) that support it; " +
            "otherwise it stays off. Costs a lot of GPU time. Ignored on URP.", "raytracing rtx dxr reflections shadows hdrp").Applying(_ => Apply());

        /// <summary>The photoreal choices as they stand.</summary>
        public static PhotorealOptions Options => new PhotorealOptions
        {
            PhysicalSky = PhysicalSky, Volumetrics = Volumetrics, Exposure = (SunExposure)Exposure.Index,
            ExposureCompensation = ExposureCompensation.Value, SunBrightness = SunBrightness.Value, RayTracing = RayTracing,
        };

        /// <summary>The pipeline chosen in the menu (the one the game starts on next time).</summary>
        public static RenderPipelineProfile Chosen => RenderPipelineProfiles.ForMenuName(Pipeline.Selected) ?? RenderPipelineProfiles.Default;

        /// <summary>Whether the pipeline chosen is not the one running, so a restart is waiting to apply it.</summary>
        public static bool RestartPending => Chosen.Id != RenderBackend.Active.Profile.Id;

        // Each declaration applies on construction, before those after it exist: wait for the last.
        static void Apply()
        {
            if (PhysicalSky == null || Volumetrics == null || Exposure == null || ExposureCompensation == null || SunBrightness == null || RayTracing == null)
                return;
            RenderBackend.Active.Photoreal = Options;
        }
    }
}
