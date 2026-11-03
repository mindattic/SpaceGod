#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Rendering/RenderPipelineProfiles.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	/// <summary>The photoreal pieces a render pipeline can offer beyond drawing the game's own shaders (U.SPC-T430).</summary>
	// [Flags]
	enum class PipelineFeatures : int
	{
		None = 0,

		/// <summary>A physically based sky: a planet's atmosphere scattering the star's light, seen from inside or from orbit.</summary>
		PhysicalSky = 1,

		/// <summary>Volumetric light (shafts of sunlight) in a planet's atmosphere.</summary>
		Volumetrics = 2,

		/// <summary>Physical exposure: the scene is metered for the star's light, fixed or adapting like an eye.</summary>
		Exposure = 4,

		/// <summary>Hardware ray tracing (shadows, reflections), where the GPU and graphics API support it.</summary>
		RayTracing = 8,

		All = PhysicalSky | Volumetrics | Exposure | RayTracing,
	};
	CS_FLAGS(PipelineFeatures)
	/// <summary>C#'s [Flags] enum ToString: the member's name, or the set flags' names joined by ", ".</summary>
	SPACEGODCORE_API cs::String ToString(PipelineFeatures v);

	/// <summary>
	/// What the game knows about one render pipeline, as data (U.SPC-T429): which pipeline asset selects it, the
	/// <c>RenderPipeline</c> tag its hand-written shaders' SubShaders carry, the Resources folder they load from, and
	/// how the camera flags behave under it. Game code never names a pipeline: it asks the active backend
	/// (<c>Unity.SpaceGod.Rendering.RenderBackend</c>) for a shader, a camera, a light or a post-process look, and the
	/// backend registered for the active profile supplies it. A second pipeline is one more profile here and one more
	/// backend registered for it, never an edit to the code that draws the game.
	/// </summary>
	class SPACEGODCORE_API RenderPipelineProfile final
	{
	public:
		/// <summary>The key a backend registers under and a setting would store ("urp").</summary>
		cs::String Id;

		cs::String DisplayName;

		/// <summary>The type name of the pipeline asset that selects this pipeline (matched against the active one).</summary>
		cs::String AssetTypeName;

		/// <summary>The <c>"RenderPipeline"</c> SubShader tag every hand-written shader for this pipeline carries.</summary>
		cs::String ShaderTag;

		/// <summary>The Resources folder this pipeline's versions of the game's shaders load from.</summary>
		cs::String ShaderFolder;

		/// <summary>Whether the camera's own HDR flag decides HDR (false where the pipeline always renders HDR).</summary>
		bool HonoursCameraHdr = false;

		/// <summary>Whether the camera's own MSAA flag decides anti-aliasing (false where the pipeline sets it elsewhere).</summary>
		bool HonoursCameraMsaa = false;

		/// <summary>The renderer features the game expects this pipeline's renderer to carry (none, today).</summary>
		cs::List<cs::String> RenderFeatures;

		/// <summary>How the settings menu names this pipeline (and how the choice is remembered in a profile).</summary>
		cs::String MenuName;

		/// <summary>
		/// The exposure (EV100) the game's display-referred shaders are metered for under this pipeline (U.SPC-T430):
		/// at it a shader's 1.0 is the display's white, as on a pipeline with no exposure. NaN where the pipeline has no
		/// physical exposure and draws the shaders' colours as they are (URP).
		/// </summary>
		double ReferenceEv100 = 0;

		/// <summary>The photoreal pieces this pipeline offers.</summary>
		PipelineFeatures Features = PipelineFeatures::None;

		/// <summary>Whether the pipeline runs only on a GPU with compute shaders (the game stays on the default without them).</summary>
		bool RequiresComputeShaders = false;

		/// <summary>
		/// Where this pipeline's player sits beside the default one (U.SPC-T430): Unity builds a player for one pipeline
		/// only, so each pipeline but the default is built into a folder of this name inside the default player's
		/// folder ("" for the default itself), and the game restarts into the other player to switch.
		/// </summary>
		cs::String PlayerFolder;

		// (C#'s null strings and list are the empty string and list here; the constructor treats both alike.)
		RenderPipelineProfile(const cs::String& id, const cs::String& displayName, const cs::String& assetTypeName, const cs::String& shaderTag, const cs::String& shaderFolder,
			bool honoursCameraHdr = true, bool honoursCameraMsaa = true, const cs::List<cs::String>& renderFeatures = cs::List<cs::String>(),
			const cs::String& menuName = cs::String(), double referenceEv100 = cs::Double::NaN, PipelineFeatures features = PipelineFeatures::None,
			bool requiresComputeShaders = false, const cs::String& playerFolder = "");

		bool Offers(PipelineFeatures feature) const { return feature != PipelineFeatures::None && (Features & feature) == feature; }

		/// <summary>Whether the pipeline meters the scene with a physical exposure (<see cref="ReferenceEv100"/>).</summary>
		bool HasExposure() const { return !cs::Double::IsNaN(ReferenceEv100) && !cs::Double::IsInfinity(ReferenceEv100); }

		/// <summary>
		/// The luminance (cd/m²) a shader's 1.0 stands for: what exposure <see cref="ReferenceEv100"/> shows as white
		/// (<see cref="ExposureMath.LuminanceAtEv100"/>), and 1 on a pipeline with no exposure.
		/// </summary>
		double ReferenceLuminance() const;

		/// <summary>
		/// The illuminance (lux) of a light of intensity 1 as the game's shaders mean it: a white Lambert surface it
		/// lights head on shows as white at the reference exposure (π × <see cref="ReferenceLuminance"/>; 1 without exposure).
		/// </summary>
		double LuxPerLightUnit() const { return HasExposure() ? cs::Math::PI * ReferenceLuminance() : 1.0; }

		/// <summary>The Resources path of <paramref name="shader"/> (one of <see cref="RenderShaders"/>) under this pipeline.</summary>
		cs::String ShaderPath(const cs::String& shader) const { return cs::Cat(ShaderFolder, "/", shader); }

		cs::String ToString() const { return DisplayName; }
	};

	/// <summary>
	/// The game's hand-written shaders by name, independent of any pipeline (U.SPC-T429). Each pipeline supplies its
	/// own version of every one (<see cref="RenderPipelineProfile.ShaderPath"/>); the shading math they share lives in
	/// the pipeline-neutral <c>SolarCommon.hlsl</c>, and the one pipeline-specific include in <c>PipelineCore.hlsl</c>.
	/// </summary>
	class SPACEGODCORE_API RenderShaders
	{
	public:
		static constexpr const char* Planet = "Planet", * Sun = "Sun", * Glow = "Glow", * Ring = "Ring", * Ship = "Ship", * Line = "Line",
			* Stars = "Stars", * GasGiant = "GasGiant", * Asteroids = "Asteroids", * Fx = "Fx", * Points = "Points", * Sky = "Sky";

		static const cs::List<cs::String>& All();
	};

	/// <summary>
	/// The shader globals the game lights everything by (U.SPC-T429): the nearest star and a companion star, in
	/// camera-relative world space. Hand-written shaders read these instead of Unity lights, on every pipeline.
	/// </summary>
	class RenderGlobals
	{
	public:
		static constexpr const char* SunPosition = "_SunPositionWS", * SunColor = "_SunColor", * Sun2Position = "_Sun2PositionWS", * Sun2Color = "_Sun2Color";

		/// <summary>
		/// On a pipeline with physical exposure (U.SPC-T430): the luminance a shader's 1.0 stands for
		/// (<see cref="RenderPipelineProfile.ReferenceLuminance"/>), and how much brighter than that the star's face is
		/// drawn. <c>PipelineCore.hlsl</c> reads them; a pipeline without exposure never sets them.
		/// </summary>
		static constexpr const char* SceneLuminance = "_UnitySpaceSceneLuminance", * SunBrightness = "_UnitySpaceSunBrightness";
	};

	/// <summary>
	/// The render pipelines the game knows (U.SPC-T429). URP is the first and the default; another registers its
	/// profile here and its backend with <c>RenderBackend.Register</c>.
	/// </summary>
	class SPACEGODCORE_API RenderPipelineProfiles
	{
	public:
		/// <summary>
		/// URP 17: the shaders carry <c>"UniversalPipeline"</c> and load from <c>Resources/Shaders</c>, the camera's HDR
		/// and MSAA flags are honoured, and the renderer carries no custom features.
		/// </summary>
		static const cs::Ref<RenderPipelineProfile>& Universal();

		/// <summary>
		/// HDRP 17, the photoreal path (U.SPC-T430): the same shader files (each carries a SubShader tagged
		/// <c>"HDRenderPipeline"</c> beside its URP one), HDR always on and anti-aliasing set per camera rather than by
		/// the camera's MSAA flag, the scene metered for sunlight (EV100 15), and a physically based sky, volumetric
		/// light and ray tracing on offer. It runs from a quality level of its own, so it is chosen at start-up.
		/// </summary>
		static const cs::Ref<RenderPipelineProfile>& HighDefinition();

	private:
		static cs::List<cs::Ref<RenderPipelineProfile>>& registered();

	public:
		static const cs::List<cs::Ref<RenderPipelineProfile>>& All() { return registered(); }

		/// <summary>The pipeline used when the active one is none the game knows.</summary>
		static cs::Ref<RenderPipelineProfile> Default() { return Universal(); }

		/// <summary>Adds <paramref name="profile"/>, replacing one already registered under its id.</summary>
		static void Register(const cs::Ref<RenderPipelineProfile>& profile);

		/// <summary>The profile registered under <paramref name="id"/>, or null.</summary>
		static cs::Ref<RenderPipelineProfile> Find(const cs::String& id);

		/// <summary>The profile the settings menu names <paramref name="menuName"/> (or whose id it is), or null.</summary>
		static cs::Ref<RenderPipelineProfile> ForMenuName(const cs::String& menuName);

		/// <summary>
		/// The player to restart into for <paramref name="wanted"/> (U.SPC-T430), from the player running now
		/// (<paramref name="runningExe"/>, on pipeline <paramref name="running"/>): the default pipeline's player sits at
		/// the top, each other pipeline's in its <see cref="RenderPipelineProfile.PlayerFolder"/> beside it, under the same
		/// file name. Null when there is nothing to switch (the same pipeline, or no path).
		/// </summary>
		// (A null C# string is the empty string here.)
		static cs::String PlayerExecutable(const cs::String& runningExe, const cs::Ref<RenderPipelineProfile>& running, const cs::Ref<RenderPipelineProfile>& wanted);

		/// <summary>
		/// The quality level to run <paramref name="wanted"/> from (U.SPC-T430). Each level names the pipeline asset it
		/// runs in <paramref name="levelAssetTypes"/> (the asset's type name, or null for one that falls back to the
		/// project's default asset, of type <paramref name="defaultAssetType"/>). Returns the <paramref name="current"/>
		/// level when it already runs that pipeline, otherwise the highest level that does, and -1 when none does (the
		/// pipeline is not set up in this build, so the game stays on the one it has).
		/// </summary>
		// (C#'s null levelAssetTypes is passed as a null pointer.)
		static int QualityLevelFor(const cs::Ref<RenderPipelineProfile>& wanted, const cs::List<cs::String>* levelAssetTypes, const cs::String& defaultAssetType, int current);

		/// <summary>
		/// The profile whose pipeline asset is of type <paramref name="assetTypeName"/>, or null when none is (no
		/// pipeline asset at all, or one the game has no backend for).
		/// </summary>
		static cs::Ref<RenderPipelineProfile> ForAsset(const cs::String& assetTypeName);
	};
}
