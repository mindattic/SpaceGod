#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "RenderCore.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GenericPlatform/GenericWindow.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "SpaceGodDisplayModes.h"

// The primary game module: the game itself is C# (see Script/ManagedUnrealSpaceGodCSharp).
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, UnrealSpaceGodCSharp, "UnrealSpaceGodCSharp");

DEFINE_LOG_CATEGORY_STATIC(LogSpaceGod, Log, All);

namespace
{
	// US-T45: the engine's r.SetNearClipPlane clamps to 1 cm, and one Unity unit is one Unreal unit in this track, so
	// the original's dynamic near plane (nearest surface x 0.25: about 0.2 units beside Earth at the start) became 1 unit
	// and cut away Earth, its rings and every line near the camera. This sets the global near plane as given (the C++
	// track sets GNearClippingPlane directly), with the C++ track's floor of a ten-thousandth of a scene unit.
	void SetSpaceGodNearClipPlane(const TArray<FString>& Args)
	{
		if (Args.Num() == 0) return;
		const float Near = FMath::Max(FCString::Atof(*Args[0]), 1e-4f);
		SetNearClipPlaneGlobals(Near);
	}

	FAutoConsoleCommand GSpaceGodNearClipPlaneCmd(
		TEXT("sg.SetNearClipPlane"),
		TEXT("Sets the global near clipping plane in Unreal units, without r.SetNearClipPlane's 1-unit floor (US-T45)"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SetSpaceGodNearClipPlane));

	// ---------------------------------------------------------------- US-T56: display modes and refresh rates

	/// The game window's HWND (null headless: the primary monitor is asked then).
	void* GameWindow()
	{
		UGameViewportClient* Client = GEngine ? GEngine->GameViewport : nullptr;
		TSharedPtr<SWindow> Window = Client ? Client->GetWindow() : nullptr;
		TSharedPtr<FGenericWindow> Native = Window.IsValid() ? Window->GetNativeWindow() : nullptr;
		return Native.IsValid() ? Native->GetOSWindowHandle() : nullptr;
	}

	/// The game window's RHI viewport (its swap chain), which Slate's renderer owns.
	FRHIViewport* GameViewportRHI()
	{
		UGameViewportClient* Client = GEngine ? GEngine->GameViewport : nullptr;
		TSharedPtr<SWindow> Window = Client ? Client->GetWindow() : nullptr;
		if (!Window.IsValid() || !FSlateApplication::IsInitialized()) return nullptr;
		FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
		return Renderer ? static_cast<FRHIViewport*>(Renderer->GetViewportResource(*Window)) : nullptr;
	}

	FString GDisplayModes, GCurrentDisplayMode;
	FAutoConsoleVariableRef GDisplayModesVar(TEXT("sg.DisplayModes"), GDisplayModes,
		TEXT("The monitor's modes as sg.QueryDisplayModes last found them: \"W H NUM DEN\" per mode, ';' between (US-T56)"));
	FAutoConsoleVariableRef GCurrentDisplayModeVar(TEXT("sg.CurrentDisplayMode"), GCurrentDisplayMode,
		TEXT("The monitor's desktop mode as sg.QueryDisplayModes last found it: \"W H NUM DEN\" (US-T56)"));

	/// Screen.resolutions / Screen.currentResolution for the C# facade (UnityCompat/Imgui/Screen.cs): fills
	/// sg.DisplayModes and sg.CurrentDisplayMode from DXGI (SpaceGodDisplayModes.h).
	void QueryDisplayModes()
	{
		void* Window = GameWindow();
		GDisplayModes = SpaceGodDisplay::Format(SpaceGodDisplay::Modes(Window));
		FSpaceGodDisplayMode Current;
		GCurrentDisplayMode = SpaceGodDisplay::CurrentMode(Window, Current) ? SpaceGodDisplay::Format({ Current }) : FString();
	}

	FAutoConsoleCommand GQueryDisplayModesCmd(TEXT("sg.QueryDisplayModes"),
		TEXT("Fills sg.DisplayModes and sg.CurrentDisplayMode with the monitor's modes and refresh rates (US-T56)"),
		FConsoleCommandDelegate::CreateStatic(&QueryDisplayModes));

	/// The rate Screen.SetResolution(w, h, ExclusiveFullScreen, rate) asked for, set once the window is exclusive
	/// fullscreen at that size (Unreal goes fullscreen at "any" rate, a frame or more after ApplyResolutionSettings).
	TOptional<FSpaceGodDisplayMode> GPendingRefresh;
	int32 GPendingFrames = 0;
	FTSTicker::FDelegateHandle GRefreshTicker;

	bool TickFullscreenRefresh(float)
	{
		if (!GPendingRefresh.IsSet()) return true;
		const FSpaceGodDisplayMode Mode = GPendingRefresh.GetValue();
		UGameViewportClient* Client = GEngine ? GEngine->GameViewport : nullptr;
		FViewport* Viewport = Client ? Client->Viewport : nullptr;
		const bool bReady = Viewport && Viewport->GetWindowMode() == EWindowMode::Fullscreen
			&& Viewport->GetSizeXY() == FIntPoint(Mode.Width, Mode.Height) && GameViewportRHI() != nullptr;
		if (!bReady)
		{
			if (++GPendingFrames > 600)
			{
				UE_LOG(LogSpaceGod, Display, TEXT("Display: %dx%d at %.3f Hz not applied: the window never went exclusive fullscreen at that size"),
					Mode.Width, Mode.Height, Mode.RefreshHz());
				GPendingRefresh.Reset();
			}
			return true;
		}
		GPendingRefresh.Reset();
		const ERHIInterfaceType Rhi = RHIGetInterfaceType();
		if (Rhi != ERHIInterfaceType::D3D12 && Rhi != ERHIInterfaceType::D3D11) return true; // no DXGI swap chain to ask
		FlushRenderingCommands();
		const bool bOk = SpaceGodDisplay::SetFullscreenRefresh(GameViewportRHI()->GetNativeSwapChain(), Mode);
		FSpaceGodDisplayMode Now;
		const bool bNow = SpaceGodDisplay::CurrentMode(GameWindow(), Now);
		UE_LOG(LogSpaceGod, Display, TEXT("Display: exclusive fullscreen %dx%d at %u/%u Hz (%.3f): %s; the monitor now runs %dx%d at %u Hz"),
			Mode.Width, Mode.Height, Mode.RefreshNumerator, Mode.RefreshDenominator, Mode.RefreshHz(), bOk ? TEXT("set") : TEXT("refused"),
			bNow ? Now.Width : 0, bNow ? Now.Height : 0, bNow ? Now.RefreshNumerator : 0);
		return true;
	}

	/// sg.SetFullscreenRefresh W H NUM DEN: the rate to put the exclusive-fullscreen window at; W = 0 (or no rate) cancels.
	void SetFullscreenRefresh(const TArray<FString>& Args)
	{
		GPendingRefresh.Reset();
		GPendingFrames = 0;
		if (Args.Num() < 4) return;
		FSpaceGodDisplayMode Mode;
		Mode.Width = FCString::Atoi(*Args[0]);
		Mode.Height = FCString::Atoi(*Args[1]);
		Mode.RefreshNumerator = static_cast<uint32>(FCString::Strtoui64(*Args[2], nullptr, 10));
		Mode.RefreshDenominator = static_cast<uint32>(FCString::Strtoui64(*Args[3], nullptr, 10));
		if (Mode.Width <= 0 || Mode.Height <= 0 || Mode.RefreshDenominator == 0 || Mode.RefreshNumerator == 0) return;
		GPendingRefresh = Mode;
		if (!GRefreshTicker.IsValid()) GRefreshTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickFullscreenRefresh));
		UE_LOG(LogSpaceGod, Display, TEXT("Display: exclusive fullscreen %dx%d asked at %u/%u Hz (%.3f)"),
			Mode.Width, Mode.Height, Mode.RefreshNumerator, Mode.RefreshDenominator, Mode.RefreshHz());
	}

	FAutoConsoleCommand GSetFullscreenRefreshCmd(TEXT("sg.SetFullscreenRefresh"),
		TEXT("sg.SetFullscreenRefresh W H NUM DEN: once the window is exclusive fullscreen at WxH, switch the monitor to NUM/DEN Hz (US-T56)"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SetFullscreenRefresh));

	// ---------------------------------------------------------------- US-T55: MSAA's line width

	/// sg.SetLineWidthPx W: every live instance of SpaceGodRender's M_Line draws its ribbons W pixels wide (the C# facade's
	/// ShaderBinding gives new ones the same). Unity's lines are hardware lines: under MSAA Direct3D rasterises them as
	/// 1.4-pixel-wide quads, without it one pixel per step.
	void SetLineWidthPx(const TArray<FString>& Args)
	{
		if (Args.Num() == 0) return;
		const float Width = FCString::Atof(*Args[0]);
		UMaterialInterface* Line = LoadObject<UMaterialInterface>(nullptr, TEXT("/SpaceGodRender/Materials/M_Line.M_Line"));
		if (!Line) return;
		static const FName Param(TEXT("_LineWidthPx"));
		for (TObjectIterator<UMaterialInstanceDynamic> It; It; ++It)
			if (It->Parent == Line) It->SetScalarParameterValue(Param, Width);
	}

	FAutoConsoleCommand GSetLineWidthPxCmd(TEXT("sg.SetLineWidthPx"),
		TEXT("sg.SetLineWidthPx W: the width of every live M_Line instance's ribbons in pixels (US-T55)"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SetLineWidthPx));
}
