// Port of Unity.SpaceGod Runtime/Rendering/Pipeline/RenderBackend.cs, RenderPipelineSelection.cs (RenderPipelineSettings)
// and Urp/UrpPostProcessSettings.cs, with UrpRenderBackend.cs's look on Unreal's post-process volume (US-T30).
#include "Rendering/Pipeline/RenderBackend.h"
#include "Rendering/RenderPipelineProfiles.h"
#include "Settings/GameSetting.h"
#include "UnityCompat/UnityScene.h"
#include "UnityCompat/ShaderBinding.h"

#include "Engine/PostProcessVolume.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"

#include <cmath>

using namespace SpaceGod::Core;

namespace SpaceGod::Rendering
{
	// ---------------------------------------------------------------- RenderBackend

	namespace
	{
		cs::Dictionary<cs::String, cs::Func<cs::Ref<IRenderBackend>>>& Factories()
		{
			static cs::Dictionary<cs::String, cs::Func<cs::Ref<IRenderBackend>>> factories;
			return factories;
		}

		cs::Ref<IRenderBackend>& ActiveSlot()
		{
			static cs::Ref<IRenderBackend> active;
			return active;
		}

		UnrealRenderBackend*& Live()
		{
			static UnrealRenderBackend* live = nullptr;
			return live;
		}
	}

	void RenderBackend::Register(const cs::String& profileId, const cs::Func<cs::Ref<IRenderBackend>>& make)
	{
		if (profileId.empty()) throw cs::ArgumentException("A backend registers under a pipeline id.");
		if (!make) throw cs::ArgumentNullException("make");
		Factories()[profileId] = make;
		auto& active = ActiveSlot();
		if (active && active->Profile()->Id == profileId) active = nullptr;
	}

	const cs::Ref<IRenderBackend>& RenderBackend::Active()
	{
		auto& active = ActiveSlot();
		if (!active) active = Resolve();
		return active;
	}

	void RenderBackend::Reset() { ActiveSlot() = nullptr; }

	cs::Ref<IRenderBackend> RenderBackend::Resolve()
	{
		// Unity matches the pipeline asset it runs; Unreal runs one renderer, drawn as the default profile (URP's).
		auto profile = RenderPipelineProfiles::Default();
		cs::Func<cs::Ref<IRenderBackend>> make;
		if (Factories().TryGetValue(profile->Id, make)) return make();
		return cs::New<UnrealRenderBackend>();
	}

	// ---------------------------------------------------------------- NeutralRenderBackend

	NeutralRenderBackend::NeutralRenderBackend(const cs::Ref<RenderPipelineProfile>& profile_) : profile(profile_)
	{
		if (!profile) throw cs::ArgumentNullException("profile");
	}

	cs::Ref<Shader> NeutralRenderBackend::LoadShader(const cs::String& name)
	{
		// The shader is SpaceGodRender's material of that name (ShaderBinding); a name it lacks draws the placeholder.
		return Shader::Find("Unity.SpaceGod/" + name);
	}

	void NeutralRenderBackend::SetupCamera(Camera*, CameraRole)
	{
		// The hand-written shaders draw straight to the display's range: Unreal's camera always renders HDR and the
		// look below (or none) brings it back to that, so there is no camera flag to set.
	}

	void NeutralRenderBackend::SetMsaa(Camera*, bool)
	{
		// Unreal's deferred renderer takes its anti-aliasing from r.AntiAliasingMethod, not per camera.
	}

	cs::Ref<Light> NeutralRenderBackend::AddKeyLight(const cs::Ref<Transform>& parent, const cs::String&, Quaternion rotation, float intensity, int cullingMask,
		HideFlags)
	{
		auto light = cs::New<Light>();
		light->type = LightType::Directional;
		light->direction = rotation * Vector3::forward();
		light->intensity = intensity;
		// US-T53: a light under that transform, lighting only `cullingMask`, kept by the scene host (UnityScene::KeyLightOf:
		// the model viewer's key light under its camera, the C# UnityBridge.KeyLightOf).
		light->Parent = parent;
		light->localRotation = rotation;
		light->cullingMask = cullingMask;
		UnityCompat::UnityScene::AddLight(light);
		return light;
	}

	StarLighting NeutralRenderBackend::Lighting() const
	{
		StarLighting l;
		l.SunPosition = Shader::GetGlobalVector(RenderGlobals::SunPosition);
		l.SunColor = Shader::GetGlobalVector(RenderGlobals::SunColor);
		l.Sun2Position = Shader::GetGlobalVector(RenderGlobals::Sun2Position);
		l.Sun2Color = Shader::GetGlobalVector(RenderGlobals::Sun2Color);
		return l;
	}

	void NeutralRenderBackend::SetLighting(const StarLighting& value)
	{
		Shader::SetGlobalVector(RenderGlobals::SunPosition, value.SunPosition);
		Shader::SetGlobalVector(RenderGlobals::SunColor, value.SunColor);
		Shader::SetGlobalVector(RenderGlobals::Sun2Position, value.Sun2Position);
		Shader::SetGlobalVector(RenderGlobals::Sun2Color, value.Sun2Color);
	}

	// ---------------------------------------------------------------- UnrealRenderBackend

	UnrealRenderBackend::UnrealRenderBackend() : NeutralRenderBackend(RenderPipelineProfiles::Universal())
	{
		Live() = this;
		look = UrpPostProcessSettings::Current();
		Shader::SetGlobalFloat(RenderGlobals::SunBrightness, StarGlare());
	}

	UnrealRenderBackend::~UnrealRenderBackend()
	{
		if (Live() == this) Live() = nullptr;
	}

	UnrealRenderBackend* UnrealRenderBackend::Existing() { return Live(); }

	float UnrealRenderBackend::StarGlare() const { return look.Enabled ? static_cast<float>(cs::Math::Max(1.0, look.SunBrightness)) : 1.0f; }

	namespace
	{
		void SetCVar(const TCHAR* name, int value)
		{
			if (IConsoleVariable* v = IConsoleManager::Get().FindConsoleVariable(name)) v->Set(value, ECVF_SetByCode);
		}
	}

	void UnrealRenderBackend::SetupCamera(Camera* camera, CameraRole role)
	{
		NeutralRenderBackend::SetupCamera(camera, role);
		// US-T45/T55, the C# track's UnrealRenderBackend.SetCvars: the original's 4x MSAA (the forward renderer's), not the
		// engine's default TSR, which smeared the one-pixel stars into soft blobs; the rest of the engine-wide look the C#
		// track sets is the post-process volume's below.
		static bool cvarsSet = false;
		if (!cvarsSet)
		{
			cvarsSet = true;
			SetCVar(TEXT("r.MSAACount"), 4);
			SetCVar(TEXT("r.AntiAliasingMethod"), 3);
		}
		if (role != CameraRole::Main) return;
		mainSetUp = true;
		Apply();
	}

	void UnrealRenderBackend::SetMsaa(Camera* camera, bool on)
	{
		NeutralRenderBackend::SetMsaa(camera, on);
		// US-T55: as the original's URP asset (m_MSAA: 4) with the camera's allowMSAA: 4x MSAA on, none off (MSAA is the
		// forward renderer's: Config/DefaultEngine.ini r.ForwardShading). Unity's hardware lines are 1.4-pixel quads under
		// MSAA, one pixel without (ShaderBinding::SetLineWidthPx); its points stay 1x1.
		SetCVar(TEXT("r.MSAACount"), on ? 4 : 1);
		SetCVar(TEXT("r.AntiAliasingMethod"), on ? 3 : 0);
		UnityCompat::ShaderBinding::SetLineWidthPx(on ? 1.4f : 1.0f);
	}

	void UnrealRenderBackend::SetPostProcessing(Camera* camera, const PostProcessLook& value)
	{
		if (!camera) return;
		game = value;
		Apply();
	}

	void UnrealRenderBackend::SetLook(const CameraLook& value)
	{
		look = value;
		Shader::SetGlobalFloat(RenderGlobals::SunBrightness, StarGlare());
		Apply();
	}

	APostProcessVolume* UnrealRenderBackend::Volume()
	{
		static TWeakObjectPtr<APostProcessVolume> volume;
		UWorld* world = UnityCompat::UnityScene::World();
		if (!world) return nullptr;
		if (volume.IsValid() && volume->GetWorld() == world) return volume.Get();
		FActorSpawnParameters spawn;
		spawn.Name = TEXT("PostProcessVolume");
		spawn.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
		spawn.ObjectFlags |= RF_Transient;
		auto* v = world->SpawnActor<APostProcessVolume>(spawn);
		if (!v) return nullptr;
		v->bUnbound = true;
		v->Priority = 100;
		volume = v;
		return v;
	}

	void UnrealRenderBackend::Apply()
	{
		APostProcessVolume* volume = Volume();
		if (!volume) return;
		FPostProcessSettings& s = volume->Settings;
		bool stack = mainSetUp && look.Enabled;

		// Always: the shaders' own exposure (SG_Expose cancels the eye adaptation), so a fixed, neutral exposure.
		s.bOverride_AutoExposureMethod = true;
		s.AutoExposureMethod = AEM_Manual;
		s.bOverride_AutoExposureBias = true;
		s.AutoExposureBias = 0;
		s.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		s.AutoExposureApplyPhysicalCameraExposure = false;
		s.bOverride_ExpandGamut = true;
		s.ExpandGamut = 0;

		float gameEv = game.Enabled ? game.ExposureEv : 0.0f;
		float contrast = (game.Enabled ? game.Contrast : 0.0f) + (stack ? static_cast<float>(look.Contrast) : 0.0f);
		float saturation = (game.Enabled ? game.Saturation : 0.0f) + (stack ? static_cast<float>(look.Saturation) : 0.0f);
		float bloomIntensity = stack ? static_cast<float>(look.Bloom) + (game.Enabled ? game.BloomIntensity : 0.0f) : game.BloomIntensity;
		float bloomThreshold = stack ? static_cast<float>(look.BloomThreshold) : game.BloomThreshold;
		// Adapting meters cannot read Unreal's eye adaptation (cancelled by the shaders): their compensation stands.
		double stops = stack ? (look.Adapts() ? look.Compensation : CameraExposure::FixedStops(look)) : 0.0;

		// URP's post-exposure is a gain in linear light before the tone curve.
		float gain = static_cast<float>(std::pow(2.0, stops + gameEv));
		s.bOverride_ColorGain = true;
		s.ColorGain = FVector4(gain, gain, gain, 1);
		s.bOverride_ColorContrast = true;
		float c = 1.0f + FMath::Clamp(contrast, -100.0f, 100.0f) / 100.0f;
		s.ColorContrast = FVector4(c, c, c, 1);
		s.bOverride_ColorSaturation = true;
		float sat = 1.0f + FMath::Clamp(saturation, -100.0f, 100.0f) / 100.0f;
		s.ColorSaturation = FVector4(sat, sat, sat, 1);
		s.bOverride_WhiteTemp = true;
		// URP's temperature runs -100 (cool) to 100 (warm); Unreal's white point is in kelvin (warmer = lower).
		s.WhiteTemp = 6500.0f - 30.0f * (stack ? FMath::Clamp(static_cast<float>(look.Temperature), -100.0f, 100.0f) : 0.0f);
		s.bOverride_BloomIntensity = true;
		s.BloomIntensity = FMath::Max(0.0f, bloomIntensity);
		s.bOverride_BloomThreshold = true;
		s.BloomThreshold = FMath::Max(0.0f, bloomThreshold);
		// None draws the image as the shaders do (clipping above white); Neutral and ACES take Unreal's filmic curve.
		s.bOverride_ToneCurveAmount = true;
		s.ToneCurveAmount = stack && look.Tonemapping != ToneCurve::None ? 1.0f : 0.0f;
	}

	cs::String UnrealRenderBackend::Describe()
	{
		APostProcessVolume* volume = Volume();
		if (!volume) return "no post-process";
		const FPostProcessSettings& s = volume->Settings;
		return cs::Cat("gain ", cs::Fmt(s.ColorGain.X, "0.###"), ", contrast ", cs::Fmt(s.ColorContrast.X, "0.###"),
			", saturation ", cs::Fmt(s.ColorSaturation.X, "0.###"), ", white ", cs::Fmt(s.WhiteTemp, "0.#"), " K, bloom ",
			cs::Fmt(s.BloomIntensity, "0.###"), " over ", cs::Fmt(s.BloomThreshold, "0.###"), ", tone curve ", cs::Fmt(s.ToneCurveAmount, "0.#"),
			", glare ", cs::Fmt(Shader::GetGlobalFloat(RenderGlobals::SunBrightness), "0.#"));
	}

	// ---------------------------------------------------------------- RenderPipelineSettings

	namespace
	{
		cs::List<cs::String> PipelineNames()
		{
			cs::List<cs::String> names;
			for (const auto& p : RenderPipelineProfiles::All()) names.Add(p->MenuName);
			return names;
		}

		int IndexOfProfile(const cs::Ref<RenderPipelineProfile>& profile)
		{
			return cs::Math::Max(0, PipelineNames().IndexOf(profile->MenuName));
		}

		cs::String SignedEv(double v) { return cs::Cat(v > 0 ? "+" : "", cs::Fmt(v, "0.##"), " EV"); }
	}

	bool& RenderPipelineSettings::Declared()
	{
		static bool declared = false;
		return declared;
	}

	const cs::Ref<ChoiceSetting>& RenderPipelineSettings::Pipeline()
	{
		static cs::Ref<ChoiceSetting> s = cs::New<ChoiceSetting>(PipelineKey, SettingGroups::RenderPipeline,
			"Render pipeline", PipelineNames(), IndexOfProfile(RenderPipelineProfiles::Default()),
			"URP is the default and the lighter choice. HDRP is the photoreal path: a physically based sky and volumetric light in a "
			"planet's atmosphere, exposure metered for the star's light, and optional ray tracing; it needs a GPU with compute shaders. "
			"Takes effect the next time the game starts (the game restarts into its HDRP build, installed beside it).",
			"hdrp urp photoreal high definition universal restart quality");
		return s;
	}

	const cs::Ref<ToggleSetting>& RenderPipelineSettings::PhysicalSky()
	{
		static cs::Ref<ToggleSetting> s = cs::New<ToggleSetting>("render.hdrp.sky", SettingGroups::RenderPipeline,
			"Physically based sky (HDRP)", PhotorealOptions::Default().PhysicalSky,
			"While the ship you fly or follow comes down through a docking ring, sits at its city or climbs back, HDRP draws that "
			"world's atmosphere as light really scatters in it: blue overhead, red at sunset, the haze below. Everywhere else the "
			"planets keep their own atmosphere rims. Ignored on URP.", "atmosphere scattering sky rayleigh docking ring descent entry hdrp")
			->Applying([](bool) { Apply(); });
		return s;
	}

	const cs::Ref<ToggleSetting>& RenderPipelineSettings::Volumetrics()
	{
		static cs::Ref<ToggleSetting> s = cs::New<ToggleSetting>("render.hdrp.volumetrics", SettingGroups::RenderPipeline,
			"Volumetric light (HDRP)", PhotorealOptions::Default().Volumetrics,
			"During a docking-ring descent the air itself catches the star's light, in shafts and haze. Costs GPU time. Ignored on URP.",
			"fog god rays sunbeams haze volumetric hdrp")->Applying([](bool) { Apply(); });
		return s;
	}

	const cs::Ref<ChoiceSetting>& RenderPipelineSettings::Exposure()
	{
		static cs::Ref<ChoiceSetting> s = cs::New<ChoiceSetting>("render.hdrp.exposure", SettingGroups::RenderPipeline,
			"Sun exposure (HDRP)", cs::List<cs::String>{ "Fixed for sunlight", "Adaptive (eye)" }, static_cast<int>(PhotorealOptions::Default().Exposure),
			"Fixed meters the image once for the star's light (EV 15), so it looks as drawn. Adaptive meters what is in view like an "
			"eye: the star's glare in frame stops the picture down, and it opens up again in shadow. Ignored on URP.",
			"exposure eye adaptation auto exposure ev metering glare hdrp")->Applying([](int) { Apply(); });
		return s;
	}

	const cs::Ref<RangeSetting>& RenderPipelineSettings::ExposureCompensation()
	{
		static cs::Ref<RangeSetting> s = cs::New<RangeSetting>("render.hdrp.exposureCompensation", SettingGroups::RenderPipeline,
			"Exposure compensation (HDRP)", PhotorealOptions::Default().ExposureCompensation, -3, 3, 0.25,
			"Brightens (+) or darkens (-) the whole image by stops on top of the metering. Ignored on URP.",
			[](double v) { return SignedEv(v); }, "brightness exposure ev hdrp")->Applying([](double) { Apply(); });
		return s;
	}

	const cs::Ref<RangeSetting>& RenderPipelineSettings::SunBrightness()
	{
		static cs::Ref<RangeSetting> s = cs::New<RangeSetting>("render.hdrp.sunBrightness", SettingGroups::RenderPipeline,
			"Star glare (HDRP)", PhotorealOptions::Default().SunBrightness, 1, 64, 1,
			"How much brighter than a sunlit surface the star's face is drawn, so it blooms and, with adaptive exposure, dazzles. Ignored on URP.",
			[](double v) { return cs::Cat(cs::Fmt(v, "0"), "x"); }, "sun glare bloom brightness hdrp")->Applying([](double) { Apply(); });
		return s;
	}

	const cs::Ref<ToggleSetting>& RenderPipelineSettings::RayTracing()
	{
		static cs::Ref<ToggleSetting> s = cs::New<ToggleSetting>("render.hdrp.rayTracing", SettingGroups::RenderPipeline,
			"Ray tracing (HDRP)", PhotorealOptions::Default().RayTracing,
			"Ray-traced shadows and reflections from the star, on a GPU and graphics API (DirectX 12) that support it; "
			"otherwise it stays off. Costs a lot of GPU time. Ignored on URP.", "raytracing rtx dxr reflections shadows hdrp")
			->Applying([](bool) { Apply(); });
		Declared() = true;
		return s;
	}

	PhotorealOptions RenderPipelineSettings::Options()
	{
		PhotorealOptions o;
		o.PhysicalSky = PhysicalSky()->Value;
		o.Volumetrics = Volumetrics()->Value;
		o.Exposure = static_cast<SunExposure>(Exposure()->Index);
		o.ExposureCompensation = ExposureCompensation()->Value;
		o.SunBrightness = SunBrightness()->Value;
		o.RayTracing = RayTracing()->Value;
		return o;
	}

	cs::Ref<RenderPipelineProfile> RenderPipelineSettings::Chosen()
	{
		auto p = RenderPipelineProfiles::ForMenuName(Pipeline()->Selected());
		return p ? p : RenderPipelineProfiles::Default();
	}

	bool RenderPipelineSettings::RestartPending() { return Chosen()->Id != RenderBackend::Active()->Profile()->Id; }

	void RenderPipelineSettings::Apply()
	{
		// Each declaration applies on construction, before those after it exist: wait for the last.
		if (!Declared()) return;
		RenderBackend::Active()->SetPhotoreal(Options());
	}

	// ---------------------------------------------------------------- UrpPostProcessSettings

	namespace
	{
		const cs::List<cs::String>& MeteringNames()
		{
			static cs::List<cs::String> n{ "Fixed", "Automatic (eye)", "Histogram (eye)", "Physical camera" };
			return n;
		}

		const cs::List<cs::String>& CurveNames()
		{
			static cs::List<cs::String> n{ "None (as drawn)", "Neutral", "ACES (filmic)" };
			return n;
		}

		const cs::List<double>& IsoStops()
		{
			static cs::List<double> s{ 50, 100, 200, 400, 800, 1600, 3200, 6400 };
			return s;
		}

		const cs::List<double>& ShutterStops()
		{
			static cs::List<double> s{ 30, 60, 125, 250, 500, 1000, 2000, 4000 };
			return s;
		}

		const cs::List<double>& ApertureStops()
		{
			static cs::List<double> s{ 1.4, 2, 2.8, 4, 5.6, 8, 11, 16, 22 };
			return s;
		}

		cs::List<cs::String> Names(const cs::List<double>& stops, const cs::Func<cs::String, double>& name)
		{
			cs::List<cs::String> names;
			for (double v : stops) names.Add(name(v));
			return names;
		}

		cs::String Number(double v) { return cs::Fmt(v, "0.#"); }

		int IndexOf(const cs::List<double>& stops, double value)
		{
			int best = 0;
			for (int i = 1; i < stops.Count(); i++)
				if (cs::Math::Abs(stops[i] - value) < cs::Math::Abs(stops[best] - value)) best = i;
			return best;
		}

		cs::String Signed(double v, const cs::String& unit) { return cs::Cat(v > 0 ? "+" : "", cs::Fmt(v, "0.##"), unit); }

		constexpr const char* Group = SettingGroups::RenderPipeline;
	}

	bool& UrpPostProcessSettings::Declared()
	{
		static bool declared = false;
		return declared;
	}

	const cs::Ref<ToggleSetting>& UrpPostProcessSettings::Enabled()
	{
		static cs::Ref<ToggleSetting> s = cs::New<ToggleSetting>("render.urp.post", Group,
			"Post-processing (URP)", CameraLook::Default().Enabled,
			"URP's own Volume stack for the game's view: HDR, so the star's face glares above white, with exposure, bloom, a tone "
			"curve and colour grading below. Off draws the image exactly as the shaders do, with no post-processing. Ignored on HDRP.",
			"post processing volume hdr urp photoreal")->Applying([](bool) { Apply(); });
		return s;
	}

	const cs::Ref<ChoiceSetting>& UrpPostProcessSettings::Metering()
	{
		static cs::Ref<ChoiceSetting> s = cs::New<ChoiceSetting>("render.urp.exposure", Group,
			"Exposure (URP)", MeteringNames(), static_cast<int>(CameraLook::Default().Metering),
			"Fixed shows the image as drawn, moved only by the compensation below. Automatic meters the average of everything in "
			"view and adapts like an eye; Histogram does the same but throws out black space and the star's face first, so it "
			"meters what is lit. Physical camera exposes by the ISO, shutter and aperture below, against sunny 16. Ignored on HDRP.",
			"exposure eye adaptation auto exposure histogram metering physical camera ev urp")->Applying([](int) { Apply(); });
		return s;
	}

	const cs::Ref<RangeSetting>& UrpPostProcessSettings::Compensation()
	{
		static cs::Ref<RangeSetting> s = cs::New<RangeSetting>("render.urp.exposureCompensation", Group,
			"Exposure compensation (URP)", CameraLook::Default().Compensation, -3, 3, 0.25,
			"Brightens (+) or darkens (-) the image by stops on top of the exposure. Ignored on HDRP.",
			[](double v) { return Signed(v, " EV"); }, "brightness exposure ev compensation urp")->Applying([](double) { Apply(); });
		return s;
	}

	const cs::Ref<ChoiceSetting>& UrpPostProcessSettings::Iso()
	{
		static cs::Ref<ChoiceSetting> s = cs::New<ChoiceSetting>("render.urp.camera.iso", Group,
			"Physical camera ISO (URP)", Names(IsoStops(), [](double v) { return cs::Cat("ISO ", Number(v)); }), IndexOf(IsoStops(), CameraLook::Default().Iso),
			"The sensor's sensitivity when Exposure is Physical camera: each doubling brightens the image a stop. Ignored on HDRP.",
			"iso sensitivity physical camera urp")->Applying([](int) { Apply(); });
		return s;
	}

	const cs::Ref<ChoiceSetting>& UrpPostProcessSettings::Shutter()
	{
		static cs::Ref<ChoiceSetting> s = cs::New<ChoiceSetting>("render.urp.camera.shutter", Group,
			"Physical camera shutter (URP)", Names(ShutterStops(), [](double v) { return cs::Cat("1/", Number(v), " s"); }),
			IndexOf(ShutterStops(), 1 / CameraLook::Default().ShutterSeconds),
			"The shutter time when Exposure is Physical camera: each halving darkens the image a stop. Ignored on HDRP.",
			"shutter speed physical camera urp")->Applying([](int) { Apply(); });
		return s;
	}

	const cs::Ref<ChoiceSetting>& UrpPostProcessSettings::Aperture()
	{
		static cs::Ref<ChoiceSetting> s = cs::New<ChoiceSetting>("render.urp.camera.aperture", Group,
			"Physical camera aperture (URP)", Names(ApertureStops(), [](double v) { return cs::Cat("f/", Number(v)); }), IndexOf(ApertureStops(), CameraLook::Default().Aperture),
			"The lens's f-number when Exposure is Physical camera: each full stop wider (f/16 to f/11) brightens the image a stop; "
			"f/16 at 1/125 s and ISO 100 is sunny 16, the image as drawn. Ignored on HDRP.",
			"aperture f-stop f number lens physical camera urp")->Applying([](int) { Apply(); });
		return s;
	}

	const cs::Ref<RangeSetting>& UrpPostProcessSettings::Bloom()
	{
		static cs::Ref<RangeSetting> s = cs::New<RangeSetting>("render.urp.bloom", Group,
			"Bloom (URP)", CameraLook::Default().Bloom, 0, 2, 0.05,
			"How strongly what is brighter than the threshold below (the star's face, engine plumes at full burn) bleeds light "
			"around itself. 0 turns bloom off. Ignored on HDRP.", nullptr, "bloom glow glare urp")->Applying([](double) { Apply(); });
		return s;
	}

	const cs::Ref<RangeSetting>& UrpPostProcessSettings::BloomThreshold()
	{
		static cs::Ref<RangeSetting> s = cs::New<RangeSetting>("render.urp.bloomThreshold", Group,
			"Bloom threshold (URP)", CameraLook::Default().BloomThreshold, 0.5, 4, 0.1,
			"The brightness bloom starts at, where 1 is the display's white: lower lets more of the image glow. Ignored on HDRP.",
			nullptr, "bloom threshold glow urp")->Applying([](double) { Apply(); });
		return s;
	}

	const cs::Ref<RangeSetting>& UrpPostProcessSettings::SunBrightness()
	{
		static cs::Ref<RangeSetting> s = cs::New<RangeSetting>("render.urp.sunBrightness", Group,
			"Star glare (URP)", CameraLook::Default().SunBrightness, 1, 32, 1,
			"How much brighter than a sunlit surface the star's face is drawn, so it blooms and stops an adapting exposure down. "
			"Ignored on HDRP (it has its own) and with post-processing off.",
			[](double v) { return cs::Cat(Number(v), "x"); }, "sun star glare bloom brightness urp")->Applying([](double) { Apply(); });
		return s;
	}

	const cs::Ref<ChoiceSetting>& UrpPostProcessSettings::Tonemapping()
	{
		static cs::Ref<ChoiceSetting> s = cs::New<ChoiceSetting>("render.urp.tonemapping", Group,
			"Tone curve (URP)", CurveNames(), static_cast<int>(CameraLook::Default().Tonemapping),
			"How the image's range is fitted to the display. None clips what is above white, as the game drew it; Neutral rolls the "
			"highlights off gently; ACES is film-like, with more contrast. Ignored on HDRP (always ACES).",
			"tonemapping tone mapping aces neutral filmic hdr urp")->Applying([](int) { Apply(); });
		return s;
	}

	const cs::Ref<RangeSetting>& UrpPostProcessSettings::Contrast()
	{
		static cs::Ref<RangeSetting> s = cs::New<RangeSetting>("render.urp.contrast", Group,
			"Contrast (URP)", CameraLook::Default().Contrast, -50, 50, 1,
			"Colour grading: spreads (+) or squeezes (-) the image's tones. Ignored on HDRP.", [](double v) { return Signed(v, ""); },
			"contrast colour color grading urp")->Applying([](double) { Apply(); });
		return s;
	}

	const cs::Ref<RangeSetting>& UrpPostProcessSettings::Saturation()
	{
		static cs::Ref<RangeSetting> s = cs::New<RangeSetting>("render.urp.saturation", Group,
			"Saturation (URP)", CameraLook::Default().Saturation, -50, 50, 1,
			"Colour grading: richer (+) or more muted (-) colour. Ignored on HDRP.", [](double v) { return Signed(v, ""); },
			"saturation colour color grading urp")->Applying([](double) { Apply(); });
		return s;
	}

	const cs::Ref<RangeSetting>& UrpPostProcessSettings::Temperature()
	{
		static cs::Ref<RangeSetting> s = cs::New<RangeSetting>("render.urp.temperature", Group,
			"White balance (URP)", CameraLook::Default().Temperature, -50, 50, 1,
			"Colour grading: warmer (+) or cooler (-) light. Ignored on HDRP.", [](double v) { return Signed(v, ""); },
			"white balance temperature warm cool colour color grading urp")->Applying([](double) { Apply(); });
		Declared() = true;
		return s;
	}

	CameraLook UrpPostProcessSettings::Current()
	{
		if (!Declared()) return CameraLook::Default();
		CameraLook l;
		l.Enabled = Enabled()->Value;
		l.Metering = static_cast<CameraMetering>(Metering()->Index);
		l.Compensation = Compensation()->Value;
		l.Iso = IsoStops()[Iso()->Index];
		l.ShutterSeconds = 1 / ShutterStops()[Shutter()->Index];
		l.Aperture = ApertureStops()[Aperture()->Index];
		l.Bloom = Bloom()->Value;
		l.BloomThreshold = BloomThreshold()->Value;
		l.Tonemapping = static_cast<ToneCurve>(Tonemapping()->Index);
		l.Contrast = Contrast()->Value;
		l.Saturation = Saturation()->Value;
		l.Temperature = Temperature()->Value;
		l.SunBrightness = SunBrightness()->Value;
		return l;
	}

	void UrpPostProcessSettings::Apply()
	{
		if (!Declared()) return;
		if (auto* b = UnrealRenderBackend::Existing()) b->SetLook(Current());
	}
}
