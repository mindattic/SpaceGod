#pragma once
// Port of Unity.SpaceGod Runtime/Rendering/Pipeline/RenderBackend.cs (US-T30), plus the Unreal backend that stands in
// for URP's (UrpRenderBackend.cs): Unreal has one renderer, so the game always runs on the profile Unity.SpaceGod
// ships as its default (URP's) and that pipeline's look (UrpPostProcessSettings) is put into effect on an unbound
// post-process volume. HDRP's photoreal extras (physical sky, volumetrics, ray tracing) are kept as choices only.

#include "UnityCompat/UnityRenderingTypes.h"
#include "Rendering/Photoreal.h"
#include "Rendering/CameraExposure.h"

#include <optional>

namespace SpaceGod::Core
{
	class RenderPipelineProfile;
	class ChoiceSetting;
	class RangeSetting;
	class ToggleSetting;
}

class APostProcessVolume;

namespace SpaceGod::Rendering
{
	/// <summary>What a camera is for, so the pipeline can set it up as that pipeline needs (U.SPC-T429).</summary>
	enum class CameraRole : int
	{
		/// <summary>The game's own camera, drawing the universe to the screen.</summary>
		Main,

		/// <summary>A camera drawn over the game's own on the screen (the model viewer).</summary>
		Overlay,

		/// <summary>A camera rendered by hand into a texture (ship thumbnails), never part of the frame.</summary>
		Offscreen,
	};

	/// <summary>
	/// The light the hand-written shaders read (U.SPC-T429): the nearest star and a companion, in camera-relative world
	/// space (a position's w is unused; a colour's w is its strength). Set through <see cref="IRenderBackend.Lighting"/>.
	/// </summary>
	struct StarLighting
	{
		Vector4 SunPosition, SunColor, Sun2Position, Sun2Color;
	};

	/// <summary>
	/// US-T132: the light of a place beyond Sol, graded to its painted sky (Rendering::SystemSkies::GradeFor). The star's
	/// light is tinted, a fill comes from the sky's bright heart (as the companion light, when there is no companion star),
	/// and the frame gets the place's fog and dust: a coloured lift in the shadows and a wash. Inactive (Sol) changes nothing.
	/// </summary>
	struct SystemGrade
	{
		bool Active = false;
		/// <summary>Multiplies the star's light (rgb).</summary>
		Vector4 KeyTint = Vector4(1, 1, 1, 1);
		/// <summary>Toward the sky's heart (unit, the game's world axes) and the fill's colour times its strength (rgb).</summary>
		Vector4 FillDirection = Vector4(0, 1, 0, 0), FillColor = Vector4(0, 0, 0, 1);
		/// <summary>The fog and dust: added to the shadows (rgb), and the frame's colour wash (rgb, 1 for none).</summary>
		Vector4 Haze = Vector4(0, 0, 0, 0), Tint = Vector4(1, 1, 1, 1);
	};

	/// <summary>
	/// A post-process look in pipeline-neutral terms (U.SPC-T429): each pipeline turns it into its own volume and
	/// overrides. <see cref="None"/> (the default) means no post-processing at all, which is how the game draws today.
	/// </summary>
	struct PostProcessLook
	{
		bool Enabled = false;

		/// <summary>Exposure compensation, in stops.</summary>
		float ExposureEv = 0;

		/// <summary>Bloom strength (0 for none) and the brightness it starts at.</summary>
		float BloomIntensity = 0, BloomThreshold = 0;

		/// <summary>Contrast and saturation, -100 to 100 (0 leaves the image as it is).</summary>
		float Contrast = 0, Saturation = 0;

		static PostProcessLook None() { return PostProcessLook(); }
	};

	/// <summary>
	/// Every pipeline-specific piece of rendering, behind one interface (U.SPC-T429): the game's shaders, its cameras,
	/// the lights of preview scenes, the star lighting its shaders read, post-processing and renderer features. Game
	/// code reaches the pipeline only through <see cref="RenderBackend.Active"/>, so it never assumes URP.
	/// </summary>
	class IRenderBackend
	{
	public:
		virtual ~IRenderBackend() = default;

		virtual cs::Ref<Core::RenderPipelineProfile> Profile() const = 0;

		/// <summary>This pipeline's version of the game's shader <paramref name="name"/> (one of <see cref="RenderShaders"/>).</summary>
		virtual cs::Ref<Shader> LoadShader(const cs::String& name) = 0;

		/// <summary>Readies <paramref name="camera"/> for its <paramref name="role"/> under this pipeline.</summary>
		virtual void SetupCamera(Camera* camera, CameraRole role) = 0;

		/// <summary>Turns multisample anti-aliasing on or off for <paramref name="camera"/>.</summary>
		virtual void SetMsaa(Camera* camera, bool on) = 0;

		/// <summary>Adds a directional key light under <paramref name="parent"/> that lights only <paramref name="cullingMask"/>.</summary>
		virtual cs::Ref<Light> AddKeyLight(const cs::Ref<Transform>& parent, const cs::String& name, Quaternion rotation, float intensity, int cullingMask,
			HideFlags hideFlags = HideFlags::None) = 0;

		/// <summary>The star light the game's shaders read, for every camera.</summary>
		virtual StarLighting Lighting() const = 0;
		virtual void SetLighting(const StarLighting& value) = 0;

		/// <summary>
		/// US-T116: a fixed light that wins over <see cref="SetLighting"/> until cleared (an empty value), for snapshots of
		/// the game's own hulls (the ship thumbnails, the US-T110 lineup) lit from a known direction whatever the game's
		/// star does meanwhile. The game's lighting set while it holds is kept and comes back when it is cleared.
		/// </summary>
		virtual void OverrideLighting(const std::optional<StarLighting>& value) = 0;
		virtual std::optional<StarLighting> LightingOverride() const = 0;

		/// <summary>US-T132: grades the game's light (not an override's) to the place in view; an inactive grade for Sol.</summary>
		virtual void SetSystemGrade(const SystemGrade& value) = 0;
		virtual SystemGrade Grade() const = 0;

		/// <summary>Applies <paramref name="look"/> to what <paramref name="camera"/> draws (<see cref="PostProcessLook.None"/> turns it off).</summary>
		virtual void SetPostProcessing(Camera* camera, const PostProcessLook& look) = 0;

		/// <summary>The renderer features the active renderer carries, by name.</summary>
		virtual cs::List<cs::String> RenderFeatures() const = 0;

		/// <summary>
		/// The photoreal choices of the settings menu (U.SPC-T430). A backend puts into effect those its pipeline
		/// offers (<see cref="RenderPipelineProfile.Features"/>) and keeps the rest only to report them back.
		/// </summary>
		virtual Core::PhotorealOptions Photoreal() const = 0;
		virtual void SetPhotoreal(const Core::PhotorealOptions& value) = 0;

		/// <summary>
		/// The planet atmosphere the game's own camera is at, for a physically based sky (U.SPC-T430). A pipeline without
		/// such a sky ignores it (the shaders' own atmosphere rims draw it there).
		/// </summary>
		virtual void SetAtmosphere(Camera* camera, const Core::AtmosphereShell& shell, Vector3 centre) = 0;
	};

	/// <summary>
	/// The active render pipeline's backend (U.SPC-T429). In Unity the pipeline asset Unity runs picks the profile; Unreal
	/// has one renderer, so it is always <see cref="RenderPipelineProfiles.Default"/>, served by the backend registered for
	/// it (the Unreal backend below unless something registers another).
	/// </summary>
	class RenderBackend
	{
	public:
		/// <summary>Registers how to make the backend for the pipeline <paramref name="profileId"/> (a later one replaces it).</summary>
		static void Register(const cs::String& profileId, const cs::Func<cs::Ref<IRenderBackend>>& make);

		static const cs::Ref<IRenderBackend>& Active();

		/// <summary>Forgets the active backend, so the next use matches the pipeline afresh.</summary>
		static void Reset();

	private:
		static cs::Ref<IRenderBackend> Resolve();
	};

	/// <summary>
	/// What every pipeline does the same way (U.SPC-T429): shaders by name, camera flags, a plain directional light, the
	/// star lighting as shader globals, and no post-processing or renderer features.
	/// </summary>
	class NeutralRenderBackend : public IRenderBackend
	{
	public:
		explicit NeutralRenderBackend(const cs::Ref<Core::RenderPipelineProfile>& profile);

		cs::Ref<Core::RenderPipelineProfile> Profile() const override { return profile; }
		cs::Ref<Shader> LoadShader(const cs::String& name) override;
		void SetupCamera(Camera* camera, CameraRole role) override;
		void SetMsaa(Camera* camera, bool on) override;
		cs::Ref<Light> AddKeyLight(const cs::Ref<Transform>& parent, const cs::String& name, Quaternion rotation, float intensity, int cullingMask,
			HideFlags hideFlags = HideFlags::None) override;
		StarLighting Lighting() const override;
		void SetLighting(const StarLighting& value) override;
		void OverrideLighting(const std::optional<StarLighting>& value) override;
		std::optional<StarLighting> LightingOverride() const override { return lightingOverride; }
		void SetSystemGrade(const SystemGrade& value) override;
		SystemGrade Grade() const override { return grade; }
		void SetPostProcessing(Camera* camera, const PostProcessLook& look) override {}
		cs::List<cs::String> RenderFeatures() const override { return cs::List<cs::String>(); }
		Core::PhotorealOptions Photoreal() const override { return photoreal; }
		void SetPhotoreal(const Core::PhotorealOptions& value) override { photoreal = value; }
		void SetAtmosphere(Camera* camera, const Core::AtmosphereShell& shell, Vector3 centre) override {}

	protected:
		cs::Ref<Core::RenderPipelineProfile> profile;
		Core::PhotorealOptions photoreal = Core::PhotorealOptions::Default();
		SystemGrade grade;

	private:
		void ApplyLighting(const StarLighting& value);
		/// The game's light with the place's grade on it.
		StarLighting Graded(const StarLighting& value) const;
		std::optional<StarLighting> lightingOverride;
		/// What the game last asked for (ungraded).
		StarLighting gameLighting;
		bool gameLightingSet = false;
	};

	/// <summary>
	/// URP's backend (UrpRenderBackend.cs) on Unreal: the game's own camera sees by one unbound post-process volume that
	/// carries the settings menu's look (<see cref="UrpPostProcessSettings"/>) plus what the game asks of it
	/// (<see cref="SetPostProcessing"/>), as URP's Volume stack does. With the look off the image is drawn exactly as
	/// the shaders draw it (SpaceGodRender's README): no tone curve, no gamut expansion, no bloom, a fixed exposure.
	/// Exposure stops are a colour gain (URP's post-exposure): the shaders cancel Unreal's own eye adaptation
	/// (SG_Expose), so its metering cannot be used, and the adapting meters keep their compensation only.
	/// </summary>
	class UnrealRenderBackend final : public NeutralRenderBackend
	{
	public:
		UnrealRenderBackend();
		~UnrealRenderBackend() override;

		void SetupCamera(Camera* camera, CameraRole role) override;
		void SetPostProcessing(Camera* camera, const PostProcessLook& look) override;
		void SetSystemGrade(const SystemGrade& value) override;
		/// The C# track's UnrealRenderBackend.SetMsaa (US-T55): 4x MSAA on the forward renderer, or none, with Unity's
		/// MSAA line width.
		void SetMsaa(Camera* camera, bool on) override;

		/// <summary>Puts <paramref name="value"/> into effect on the game's own camera (the settings menu's choice).</summary>
		void SetLook(const Core::CameraLook& value);

		/// <summary>The Unreal backend made last (null before the first), which the settings menu's choices reach.</summary>
		static UnrealRenderBackend* Existing();

		/// <summary>The post-process Unreal draws the game's camera with, read back from the volume, and the star glare
		/// the shaders read (the audit's render probes, US-T51; the C# track's UnrealRenderBackend.Describe, same text).</summary>
		cs::String Describe();

	private:
		Core::CameraLook look;
		PostProcessLook game;
		bool mainSetUp = false;
		float StarGlare() const;
		void Apply();
		APostProcessVolume* Volume();
	};

	/// <summary>
	/// The render-pipeline settings (RenderPipelineSelection.cs, U.SPC-T430). Unreal has one renderer: the pipeline choice
	/// is kept (and reported) but never restarts anything; the photoreal choices go to <see cref="RenderBackend.Active"/>.
	/// [SettingsSource]: the settings registry registers these.
	/// </summary>
	class RenderPipelineSettings
	{
	public:
		/// <summary>The key the pipeline choice is kept under (read at start-up, before the settings are bound).</summary>
		static constexpr const char* PipelineKey = "render.pipeline";

		static const cs::Ref<Core::ChoiceSetting>& Pipeline();
		static const cs::Ref<Core::ToggleSetting>& PhysicalSky();
		static const cs::Ref<Core::ToggleSetting>& Volumetrics();
		static const cs::Ref<Core::ChoiceSetting>& Exposure();
		static const cs::Ref<Core::RangeSetting>& ExposureCompensation();
		static const cs::Ref<Core::RangeSetting>& SunBrightness();
		static const cs::Ref<Core::ToggleSetting>& RayTracing();

		/// <summary>The photoreal choices as they stand.</summary>
		static Core::PhotorealOptions Options();

		/// <summary>The pipeline chosen in the menu (the one the game starts on next time).</summary>
		static cs::Ref<Core::RenderPipelineProfile> Chosen();

		/// <summary>Whether the pipeline chosen is not the one running, so a restart is waiting to apply it.</summary>
		static bool RestartPending();

	private:
		static void Apply();
		static bool& Declared();
	};

	/// <summary>
	/// URP's post-processing choices (UrpPostProcessSettings.cs, U.SPC-T451), put into effect by
	/// <see cref="UnrealRenderBackend"/>. [SettingsSource]: the settings registry registers these.
	/// </summary>
	class UrpPostProcessSettings
	{
	public:
		static const cs::Ref<Core::ToggleSetting>& Enabled();
		static const cs::Ref<Core::ChoiceSetting>& Metering();
		static const cs::Ref<Core::RangeSetting>& Compensation();
		static const cs::Ref<Core::ChoiceSetting>& Iso();
		static const cs::Ref<Core::ChoiceSetting>& Shutter();
		static const cs::Ref<Core::ChoiceSetting>& Aperture();
		static const cs::Ref<Core::RangeSetting>& Bloom();
		static const cs::Ref<Core::RangeSetting>& BloomThreshold();
		static const cs::Ref<Core::RangeSetting>& SunBrightness();
		static const cs::Ref<Core::ChoiceSetting>& Tonemapping();
		static const cs::Ref<Core::RangeSetting>& Contrast();
		static const cs::Ref<Core::RangeSetting>& Saturation();
		static const cs::Ref<Core::RangeSetting>& Temperature();

		/// <summary>The choices as they stand (the defaults while the settings are still being declared).</summary>
		static Core::CameraLook Current();

	private:
		static void Apply();
		static bool& Declared();
	};
}
