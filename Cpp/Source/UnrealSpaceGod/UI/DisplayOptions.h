#pragma once

// DisplayOptions: the C++ port of Unity.SpaceGod's Runtime/UI/DisplayOptions.cs (US-T31), a static class.
// What it needs from Unity's Screen and QualitySettings (SetResolution, resolutions, currentResolution, fullScreenMode,
// vSyncCount) is not in UnityRuntime.h: it goes through DisplayBackend below, declared here in Unity's shapes and
// implemented by the host (US-T31's lead).

#include "Cs/Cs.h"
#include "DisplaySettings.h"
#include "UnityCompat/UnityTypes.h"

// ---------------------------------------------------------------- seam: the screen, as Unity's Screen / QualitySettings

namespace SpaceGod::UI::DisplayBackend
{
	/// UnityEngine.FullScreenMode (Unity's values).
	enum class FullScreenMode : int
	{
		ExclusiveFullScreen = 0,
		FullScreenWindow = 1,
		MaximizedWindow = 2,
		Windowed = 3,
	};

	/// UnityEngine.Resolution: a mode the screen offers (refreshRateRatio.value as a plain number of Hz).
	struct Resolution
	{
		int width = 0;
		int height = 0;
		double refreshRateHz = 0;
		/// refreshRateRatio itself (US-T56): the rate exactly as the display reports it (0/0 when unknown).
		unsigned int refreshNumerator = 0, refreshDenominator = 0;
	};

	/// Screen.SetResolution(width, height, mode[, refreshRate]): `refreshHz` 0 = the default for that size. In exclusive
	/// fullscreen the monitor is switched to the size's mode nearest that rate once the window is fullscreen (US-T56).
	void SetResolution(int width, int height, FullScreenMode mode, double refreshHz = 0);

	/// The mode (and exact rate) the last exclusive-fullscreen SetResolution asked for; refreshRateHz 0 when the last
	/// call asked for none (the audit's display.exclusive-rate, US-T56).
	Resolution RequestedRefresh();

	/// Screen.resolutions: every mode the screen offers, each at each of its real refresh rates (US-T56).
	cs::List<Resolution> Resolutions();

	/// Screen.currentResolution: the screen's own (desktop) mode.
	Resolution CurrentResolution();

	/// Screen.fullScreenMode.
	FullScreenMode CurrentMode();

	/// QualitySettings.vSyncCount (0 off, 1 every refresh).
	int VSyncCount();

	/// QualitySettings.vSyncCount = count.
	void SetVSyncCount(int count);
}

namespace SpaceGod::UI
{
	/// Puts <see cref="DisplaySettings"/> on the screen and remembers them between runs (U.SPC-T276): the size,
	/// windowed / borderless windowed / exclusive fullscreen, and VSync. The pause menu's Display page edits a
	/// copy and only comes here on Apply, Keep or Revert.
	class DisplayOptions
	{
	public:
		/// What is on the screen now, as last applied: <see cref="Screen.width"/> and the mode only catch up
		/// with <see cref="Screen.SetResolution(int, int, FullScreenMode)"/> at the end of the frame.
		/// C#: { get; private set; }
		static inline Core::DisplaySettings Current;

		/// At startup: the settings the player last kept, or (none kept yet) the screen as the player opened it with
		/// VSync on. A run whose screen is set on the command line (a capture, the scripted tour, -screen-width...)
		/// keeps that screen and takes only the remembered VSync.
		static void ApplySaved();

		/// Puts <paramref name="settings"/> on the screen (exclusive fullscreen at the size's best refresh rate).
		static void Apply(Core::DisplaySettings settings);

		/// Remembers <see cref="Current"/> in the player's profile for the next run (the player kept it).
		static void Save();

		/// The sizes the screen offers, once each at its best refresh rate, and the usual widescreen and ultrawide
		/// sizes that fit on it (U.SPC-T284), smallest first; at least the size the game is drawn at now (the editor's
		/// game view lists nothing).
		static cs::List<Core::DisplayResolution> Resolutions();

	private:
		static bool TryFindMode(int width, int height, DisplayBackend::Resolution& best);

		static Core::DisplaySettings FromScreen();

		static DisplayBackend::FullScreenMode ToUnity(Core::DisplayMode mode);

		static Core::DisplayMode FromUnity(DisplayBackend::FullScreenMode mode);

		static const cs::List<cs::String>& ScreenFlags();

		static bool ScreenFixedByCommandLine();
	};
}
