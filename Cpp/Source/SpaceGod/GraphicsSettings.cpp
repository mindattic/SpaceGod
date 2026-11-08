#include "GraphicsSettings.h"

#include "CoreGlobals.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Scalability.h"

#include "MindAtticHeadless.h"
#include "RenderLod.h"
#include "ShipLod.h"
#include "Rendering/Pipeline/RenderBackend.h"
#include "Settings/GameSetting.h"
#include "Settings/SettingsRegistry.h"
#include "UnityCompat/UnityTypes.h"
#include "Views/BattleFx.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		// The rows' values as last put into effect. The Applying callbacks write these and the engine side reads only
		// these, never the accessors, so a row's first apply (inside its own function-local static's construction) never
		// reaches back into a static still being built.
		int qualityIndex = GraphicsSettings::Epic;
		int shadows = 3, textures = 3, effects = 3, postProcess = 3, viewDistance = 3;
		int aaIndex = 0, upscalerIndex = 0;
		double resolutionScale = 100;
		float flownFov = 60.0f;
		bool screenPercentageSet = false;

		/// > 0 while the preset pushes its values into the knobs, or a profile is being loaded: neither is the player
		/// touching a knob.
		int quiet = 0;

		// The anti-aliasing row's options and the r.AntiAliasingMethod each stands for (MSAA first: the original's 4x MSAA
		// on the forward renderer stays the default, US-T55).
		const cs::List<cs::String>& AaOptions()
		{
			static const cs::List<cs::String> value{ "MSAA 4x", "TSR", "TAA", "FXAA", "Off" };
			return value;
		}
		constexpr int AaMethods[] = { 3, 4, 2, 1, 0 };
		constexpr int AaOff = 4;
		constexpr int UpscalerTsr = 1;

		// What each preset puts the game's own budgets at (Epic: their defaults, as the game always drew).
		constexpr double PresetSimplifyPx[] = { 140, 96, 64, 48 };
		constexpr double PresetShipIconPx[] = { 12, 8, 6, 5 };
		constexpr double PresetEffects[] = { 1500, 2000, 3000, 4000 };

		const cs::List<cs::String>& Levels()
		{
			static const cs::List<cs::String> value{ "Low", "Medium", "High", "Epic" };
			return value;
		}

		void SetCVar(const TCHAR* name, float value)
		{
			if (IConsoleVariable* v = IConsoleManager::Get().FindConsoleVariable(name)) v->Set(value, ECVF_SetByCode);
		}

		/// Unreal's scalability groups: the five with rows of their own, and the rest following the preset (left where
		/// they were under Custom). SetQualityLevels does nothing when nothing changed.
		void ApplyScalability()
		{
			Scalability::FQualityLevels levels = Scalability::GetQualityLevels();
			levels.ShadowQuality = shadows;
			levels.TextureQuality = textures;
			levels.EffectsQuality = effects;
			levels.PostProcessQuality = postProcess;
			levels.ViewDistanceQuality = viewDistance;
			if (qualityIndex != GraphicsSettings::Custom)
			{
				levels.AntiAliasingQuality = levels.GlobalIlluminationQuality = levels.ReflectionQuality = qualityIndex;
				levels.FoliageQuality = levels.ShadingQuality = levels.LandscapeQuality = qualityIndex;
			}
			Scalability::SetQualityLevels(levels);
		}

		void ApplyResolution()
		{
			// Left to the engine (r.ScreenPercentage 0: its own default, native here) until the player first moves it.
			if (resolutionScale < 100 || screenPercentageSet)
			{
				screenPercentageSet = true;
				SetCVar(TEXT("r.ScreenPercentage"), static_cast<float>(resolutionScale));
			}
			GraphicsSettings::ApplyAntiAliasing(); // TSR takes over while it upscales
		}

		/// The player moved a knob away from what the preset puts it at: the preset is Custom now.
		void Touched(bool matchesPreset)
		{
			if (quiet > 0 || qualityIndex == GraphicsSettings::Custom || matchesPreset) return;
			GraphicsSettings::Quality()->Set(GraphicsSettings::Custom);
		}

		cs::Ref<ChoiceSetting> Level(const char* key, const char* label, const char* description, const char* keywords, int* into)
		{
			auto s = cs::New<ChoiceSetting>(key, SettingGroups::Graphics, label, Levels(), GraphicsSettings::Epic, description, keywords)
				->Applying([into](int i) { *into = i; ApplyScalability(); });
			s->Changed += [into](cs::Ref<GameSetting>) { Touched(*into == qualityIndex); };
			return s;
		}

		/// A scripted run (smoke, shots, audits, the demo, a capture, headless) never benchmarks: it keeps the defaults.
		bool ScriptedRun()
		{
			if (FMindAtticHeadless::IsActive() || GIsAutomationTesting || GIsEditor || IsRunningCommandlet() || !FApp::CanEverRender()) return true;
			const TCHAR* cursor = FCommandLine::Get();
			FString token;
			while (FParse::Token(cursor, token, false))
				if (token.StartsWith(TEXT("-SpaceGod"), ESearchCase::IgnoreCase) || token.Equals(TEXT("-demo"), ESearchCase::IgnoreCase)
					|| token.Equals(TEXT("-capture"), ESearchCase::IgnoreCase))
					return true;
			return false;
		}

		constexpr const char* DetectedKey = "graphics.autoDetected";
	}

	GraphicsSettings::LoadScope::LoadScope() { quiet++; }
	GraphicsSettings::LoadScope::~LoadScope() { quiet--; }

	const cs::Ref<ChoiceSetting>& GraphicsSettings::Quality()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("graphics.quality", SettingGroups::Graphics,
			"Quality preset", cs::List<cs::String>{ "Low", "Medium", "High", "Epic", "Custom" }, Epic,
			"Sets shadows, textures, effects, post-processing, view distance and the LOD budgets together. Changing any of them makes it Custom. Picked from a hardware benchmark on a profile's first launch.",
			"graphics quality preset scalability performance low medium high epic custom")->Applying([](int i)
			{
				qualityIndex = i;
				if (i == Custom) { ApplyScalability(); return; }
				quiet++;
				Shadows()->Set(i); Textures()->Set(i); Effects()->Set(i); PostProcessing()->Set(i); ViewDistance()->Set(i);
				RenderLod::SimplifyBelowPx()->Set(PresetSimplifyPx[i]);
				ShipLod::IconBelowPx()->Set(PresetShipIconPx[i]);
				Views::BattleFx::EffectsBudget()->Set(PresetEffects[i]);
				quiet--;
				ApplyScalability();
			});
		return value;
	}

	const cs::Ref<ChoiceSetting>& GraphicsSettings::Shadows()
	{
		static const cs::Ref<ChoiceSetting> value = Level("graphics.shadows", "Shadows",
			"Shadow resolution, distance and softness (Unreal's shadow scalability).", "shadow quality scalability performance", &shadows);
		return value;
	}

	const cs::Ref<ChoiceSetting>& GraphicsSettings::Textures()
	{
		static const cs::Ref<ChoiceSetting> value = Level("graphics.textures", "Textures",
			"Texture detail and the memory kept for it; lower it on a GPU short of video memory.", "texture quality vram memory mip scalability", &textures);
		return value;
	}

	const cs::Ref<ChoiceSetting>& GraphicsSettings::Effects()
	{
		static const cs::Ref<ChoiceSetting> value = Level("graphics.effects", "Effects",
			"The quality of particles, translucency and other effects drawn by the engine.", "effects particles translucency quality scalability", &effects);
		return value;
	}

	const cs::Ref<ChoiceSetting>& GraphicsSettings::PostProcessing()
	{
		static const cs::Ref<ChoiceSetting> value = Level("graphics.postProcessing", "Post-processing",
			"The quality of bloom, depth of field, motion blur and the other screen effects.", "post processing bloom blur quality scalability", &postProcess);
		return value;
	}

	const cs::Ref<ChoiceSetting>& GraphicsSettings::ViewDistance()
	{
		static const cs::Ref<ChoiceSetting> value = Level("graphics.viewDistance", "View distance",
			"How far away the engine keeps drawing detail.", "view draw distance lod quality scalability", &viewDistance);
		return value;
	}

	const cs::Ref<ChoiceSetting>& GraphicsSettings::AntiAliasing()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("graphics.antiAliasing", SettingGroups::Graphics,
			"Anti-aliasing", AaOptions(), 0,
			"How the edges of hulls, rings and orbit lines are smoothed. MSAA 4x keeps the one-pixel stars sharp; TSR and TAA smooth more but soften fine detail; FXAA is the cheapest.",
			"antialiasing jaggies edges msaa multisample tsr taa fxaa temporal quality")->Applying([](int i)
			{
				aaIndex = i;
				ApplyAntiAliasing();
			});
		return value;
	}

	const cs::Ref<RangeSetting>& GraphicsSettings::ResolutionScale()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("graphics.resolutionScale", SettingGroups::Graphics,
			"Resolution scale", 100, 50, 100, 5,
			"Draws the 3D scene at this share of the screen's resolution and enlarges it (the HUD stays sharp). Lower is faster.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), "%"); }), "resolution scale screen percentage render upscale performance")
			->Applying([](double v)
			{
				resolutionScale = v;
				ApplyResolution();
			});
		return value;
	}

	const cs::Ref<ChoiceSetting>& GraphicsSettings::Upscaler()
	{
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("graphics.upscaler", SettingGroups::Graphics,
			"Upscaler", cs::List<cs::String>{ "Spatial", "TSR" }, 0,
			"How a scene drawn below full resolution is enlarged: Spatial stretches each frame (sharp, cheap); TSR rebuilds detail from past frames and becomes the anti-aliasing while the scale is below 100%.",
			"upscaler upscaling tsr temporal super resolution spatial resolution scale")->Applying([](int i)
			{
				upscalerIndex = i;
				ApplyAntiAliasing();
			});
		return value;
	}

	const cs::Ref<RangeSetting>& GraphicsSettings::FieldOfView()
	{
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("graphics.fieldOfView", SettingGroups::Graphics,
			"Field of view", 60, 50, 100, 1,
			"The vertical field of view behind the helm and in a cockpit; speed and the cruise drive still widen it. The maps keep their own.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), "°"); }), "fov field of view camera chase cockpit wide zoom")
			->Applying([](double v) { flownFov = static_cast<float>(v); });
		return value;
	}

	void GraphicsSettings::Declare()
	{
		Quality(); Shadows(); Textures(); Effects(); PostProcessing(); ViewDistance();
		AntiAliasing(); ResolutionScale(); Upscaler(); FieldOfView();
		static bool watching = false;
		if (watching) return;
		watching = true;
		// The game's own budgets sit on their LOD page; moving one off the preset's value makes the preset Custom too.
		RenderLod::SimplifyBelowPx()->Changed += [](cs::Ref<GameSetting>)
		{
			Touched(qualityIndex < Custom && RenderLod::SimplifyBelowPx()->Value == PresetSimplifyPx[qualityIndex]);
		};
		ShipLod::IconBelowPx()->Changed += [](cs::Ref<GameSetting>)
		{
			Touched(qualityIndex < Custom && ShipLod::IconBelowPx()->Value == PresetShipIconPx[qualityIndex]);
		};
		Views::BattleFx::EffectsBudget()->Changed += [](cs::Ref<GameSetting>)
		{
			Touched(qualityIndex < Custom && Views::BattleFx::EffectsBudget()->Value == PresetEffects[qualityIndex]);
		};
	}

	int GraphicsSettings::AntiAliasingMethod()
	{
		if (upscalerIndex == UpscalerTsr && resolutionScale < 100) return 4;
		return AaMethods[cs::Math::Max(0, cs::Math::Min(AaOff, aaIndex))];
	}

	void GraphicsSettings::ApplyAntiAliasing()
	{
		int method = AntiAliasingMethod();
		for (Camera* cam : Camera::allCameras()) Rendering::RenderBackend::Active()->SetAntiAliasing(cam, method);
	}

	float GraphicsSettings::FlownFov() { return flownFov; }

	void GraphicsSettings::AfterBind(const cs::Ref<ISettingStore>& store)
	{
		if (store == nullptr) return;
		try
		{
			// render.msaa (US-T55) became the anti-aliasing row: a profile that had it off keeps it off.
			if (!store->ReadSetting(AntiAliasing()->Key).has_value())
			{
				auto msaa = store->ReadSetting("render.msaa");
				if (msaa.has_value() && cs::Strings::EqualsOrdinalIgnoreCase(cs::Strings::Trim(*msaa), "false")) AntiAliasing()->Set(AaOff);
			}

			if (store->ReadSetting(DetectedKey).has_value()) return;
			if (store->ReadSetting(Quality()->Key).has_value()) // chosen before there was a benchmark: keep it
			{
				store->WriteSetting(DetectedKey, "true");
				return;
			}
			if (ScriptedRun()) return; // a real launch of this profile still benchmarks

			Scalability::FQualityLevels measured = Scalability::BenchmarkQualityLevels();
			int level = cs::Math::Max(static_cast<int>(Low), cs::Math::Min(static_cast<int>(Epic), static_cast<int>(measured.GetMinQualityLevel())));
			Quality()->Set(level);
			store->WriteSetting(DetectedKey, "true");
			Debug::Log(cs::Cat("[Unity.SpaceGod] Graphics: the hardware benchmark picked the ", Quality()->Selected(), " preset."));
		}
		catch (const cs::IOException& e)
		{
			Debug::LogWarning(cs::Cat("[Unity.SpaceGod] Graphics: the profile's settings could not be read or written: ", e.what()));
		}
	}
}
