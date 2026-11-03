#pragma once

// The display's real modes, for both tracks' Unity Screen facades (US-T56): Unity's Screen.resolutions lists every mode of
// the monitor with its exact refresh rate (refreshRateRatio, DXGI's rational), and Screen.SetResolution(w, h,
// ExclusiveFullScreen, rate) switches the monitor to that rate. Unreal's own list (UKismetSystemLibrary::
// GetSupportedFullscreenResolutions) drops the rates, and its swap chain goes fullscreen at "any" rate (0/0), so the
// helpers below ask DXGI directly and set the rate on the game's swap chain. The C++ track calls them from
// UI/DisplayBackend.cpp; the C# track through console commands its primary module registers (sg.QueryDisplayModes,
// sg.SetFullscreenRefresh).

#include "CoreMinimal.h"

/// One mode of a display: its size and refresh rate as DXGI gives it (Numerator / Denominator Hz).
struct FSpaceGodDisplayMode
{
	int32 Width = 0;
	int32 Height = 0;
	uint32 RefreshNumerator = 0;
	uint32 RefreshDenominator = 0;

	double RefreshHz() const { return RefreshDenominator == 0 ? 0.0 : static_cast<double>(RefreshNumerator) / RefreshDenominator; }
	bool operator==(const FSpaceGodDisplayMode& o) const
	{
		return Width == o.Width && Height == o.Height && RefreshNumerator == o.RefreshNumerator && RefreshDenominator == o.RefreshDenominator;
	}
};

namespace SpaceGodDisplay
{
	/// Every mode of the monitor NativeWindow (an HWND) is on, or of the primary monitor when it is null, as Unity's
	/// Screen.resolutions: DXGI's mode list for R8G8B8A8_UNORM, each (size, rate) once, smallest first. Empty off Windows
	/// or when DXGI cannot answer (a remote desktop).
	SPACEGODRENDER_API TArray<FSpaceGodDisplayMode> Modes(void* NativeWindow);

	/// The monitor's current desktop mode (Screen.currentResolution), its rate as Windows reports it (whole Hz).
	SPACEGODRENDER_API bool CurrentMode(void* NativeWindow, FSpaceGodDisplayMode& Out);

	/// The mode of Modes(NativeWindow) of that size whose rate is nearest RefreshHz (false when the size has none).
	SPACEGODRENDER_API bool FindMode(void* NativeWindow, int32 Width, int32 Height, double RefreshHz, FSpaceGodDisplayMode& Out);

	/// Switches an exclusive-fullscreen swap chain (an IDXGISwapChain, FRHIViewport::GetNativeSwapChain) to Mode's size
	/// and rate (IDXGISwapChain::ResizeTarget), as Unity's Screen.SetResolution does with a refresh rate.
	SPACEGODRENDER_API bool SetFullscreenRefresh(void* NativeSwapChain, const FSpaceGodDisplayMode& Mode);

	/// "W H NUM DEN" per mode, ';' between (the text the C# track's sg.DisplayModes console variable carries).
	SPACEGODRENDER_API FString Format(const TArray<FSpaceGodDisplayMode>& Modes);
}
