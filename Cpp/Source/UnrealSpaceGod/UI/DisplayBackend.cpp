// DisplayBackend (declared in DisplayOptions.h): Unity's Screen display modes and QualitySettings.vSyncCount on
// Unreal's UGameUserSettings and the adapter's fullscreen modes (US-T31), as the C# track's Screen facade does
// (US-T28, UnityCompat/Imgui/Screen.cs): Unreal reports no refresh rates, so every mode is listed at 60 Hz.

#include "UI/DisplayOptions.h"

#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"

namespace SpaceGod::UI::DisplayBackend
{
	namespace
	{
		int vSync = 1;

		UGameUserSettings* Settings() { return GEngine ? GEngine->GetGameUserSettings() : nullptr; }

		EWindowMode::Type ToUnreal(FullScreenMode mode)
		{
			return mode == FullScreenMode::ExclusiveFullScreen ? EWindowMode::Fullscreen
				: mode == FullScreenMode::FullScreenWindow ? EWindowMode::WindowedFullscreen
				: EWindowMode::Windowed;
		}
	}

	void SetResolution(int width, int height, FullScreenMode mode, double)
	{
		UGameUserSettings* s = Settings();
		if (s == nullptr) return;
		s->SetScreenResolution(FIntPoint(width, height));
		s->SetFullscreenMode(ToUnreal(mode));
		s->ApplyResolutionSettings(false);
	}

	cs::List<Resolution> Resolutions()
	{
		cs::List<Resolution> list;
		TArray<FIntPoint> modes;
		if (UKismetSystemLibrary::GetSupportedFullscreenResolutions(modes))
			for (const FIntPoint& m : modes)
			{
				Resolution r;
				r.width = m.X;
				r.height = m.Y;
				r.refreshRateHz = 60;
				list.Add(r);
			}
		return list;
	}

	Resolution CurrentResolution()
	{
		Resolution r;
		r.refreshRateHz = 60;
		if (UGameUserSettings* s = Settings())
		{
			FIntPoint d = s->GetDesktopResolution();
			r.width = d.X;
			r.height = d.Y;
		}
		else
		{
			r.width = Screen::width;
			r.height = Screen::height;
		}
		return r;
	}

	FullScreenMode CurrentMode()
	{
		UGameUserSettings* s = Settings();
		if (s == nullptr) return FullScreenMode::Windowed;
		switch (s->GetFullscreenMode())
		{
			case EWindowMode::Fullscreen: return FullScreenMode::ExclusiveFullScreen;
			case EWindowMode::WindowedFullscreen: return FullScreenMode::FullScreenWindow;
			default: return FullScreenMode::Windowed;
		}
	}

	int VSyncCount() { return vSync; }

	void SetVSyncCount(int count)
	{
		vSync = count;
		if (UGameUserSettings* s = Settings())
		{
			s->SetVSyncEnabled(count > 0);
			s->ApplyNonResolutionSettings();
		}
	}
}
