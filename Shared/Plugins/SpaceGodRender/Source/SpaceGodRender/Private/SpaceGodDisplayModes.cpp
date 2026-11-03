// SpaceGodDisplayModes.h (US-T56): the monitor's modes and their exact refresh rates from DXGI, and the rate set on an
// exclusive-fullscreen swap chain. dxgi.dll is loaded at run time (no import library, no RHI dependency here).

#include "SpaceGodDisplayModes.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <dxgi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace SpaceGodDisplay
{
#if PLATFORM_WINDOWS
	namespace
	{
		template <typename T>
		struct TCom
		{
			T* P = nullptr;
			~TCom() { if (P) P->Release(); }
			T** operator&() { return &P; }
			T* operator->() const { return P; }
			explicit operator bool() const { return P != nullptr; }
		};

		HMONITOR MonitorOf(void* NativeWindow)
		{
			if (NativeWindow) return ::MonitorFromWindow(static_cast<HWND>(NativeWindow), MONITOR_DEFAULTTOPRIMARY);
			return ::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
		}

		using FCreateFactory = HRESULT(WINAPI*)(REFIID, void**);

		bool CreateFactory(TCom<IDXGIFactory1>& Factory)
		{
			HMODULE Dxgi = ::GetModuleHandleW(L"dxgi.dll");
			if (!Dxgi) Dxgi = ::LoadLibraryW(L"dxgi.dll");
			if (!Dxgi) return false;
			auto Create = reinterpret_cast<FCreateFactory>(::GetProcAddress(Dxgi, "CreateDXGIFactory1"));
			return Create && SUCCEEDED(Create(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&Factory)));
		}

		/// The DXGI output driving Monitor (any adapter: the monitor may hang off another GPU than Unreal's).
		bool OutputFor(IDXGIFactory1* Factory, HMONITOR Monitor, TCom<IDXGIOutput>& Out)
		{
			for (UINT a = 0;; a++)
			{
				TCom<IDXGIAdapter1> Adapter;
				if (Factory->EnumAdapters1(a, &Adapter) == DXGI_ERROR_NOT_FOUND) return false;
				if (!Adapter) continue;
				for (UINT o = 0;; o++)
				{
					TCom<IDXGIOutput> Output;
					if (Adapter->EnumOutputs(o, &Output) == DXGI_ERROR_NOT_FOUND) break;
					if (!Output) continue;
					DXGI_OUTPUT_DESC Desc{};
					if (SUCCEEDED(Output->GetDesc(&Desc)) && Desc.Monitor == Monitor)
					{
						Out.P = Output.P;
						Output.P = nullptr;
						return true;
					}
				}
			}
		}
	}

	TArray<FSpaceGodDisplayMode> Modes(void* NativeWindow)
	{
		TArray<FSpaceGodDisplayMode> Result;
		TCom<IDXGIFactory1> Factory;
		if (!CreateFactory(Factory)) return Result;
		TCom<IDXGIOutput> Output;
		if (!OutputFor(Factory.P, MonitorOf(NativeWindow), Output)) return Result;

		// The format Unreal's own list uses (D3D12RHI GetAvailableResolutionsForOutput), no interlaced or stretched extras.
		const DXGI_FORMAT Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		TArray<DXGI_MODE_DESC> List;
		HRESULT Hr;
		do
		{
			UINT Count = 0;
			if (FAILED(Output->GetDisplayModeList(Format, 0, &Count, nullptr)) || Count == 0) return Result;
			List.SetNum(Count);
			Hr = Output->GetDisplayModeList(Format, 0, &Count, List.GetData());
			List.SetNum(Count);
		} while (Hr == DXGI_ERROR_MORE_DATA);
		if (FAILED(Hr)) return Result;

		for (const DXGI_MODE_DESC& d : List)
		{
			FSpaceGodDisplayMode m;
			m.Width = static_cast<int32>(d.Width);
			m.Height = static_cast<int32>(d.Height);
			m.RefreshNumerator = d.RefreshRate.Numerator;
			m.RefreshDenominator = d.RefreshRate.Denominator;
			Result.AddUnique(m); // the same mode comes once per scaling (centred, stretched)
		}
		Result.Sort([](const FSpaceGodDisplayMode& a, const FSpaceGodDisplayMode& b)
		{
			if (a.Width != b.Width) return a.Width < b.Width;
			if (a.Height != b.Height) return a.Height < b.Height;
			return a.RefreshHz() < b.RefreshHz();
		});
		return Result;
	}

	bool CurrentMode(void* NativeWindow, FSpaceGodDisplayMode& Out)
	{
		MONITORINFOEXW Info{};
		Info.cbSize = sizeof(Info);
		if (!::GetMonitorInfoW(MonitorOf(NativeWindow), &Info)) return false;
		DEVMODEW Dm{};
		Dm.dmSize = sizeof(Dm);
		if (!::EnumDisplaySettingsW(Info.szDevice, ENUM_CURRENT_SETTINGS, &Dm)) return false;
		Out.Width = static_cast<int32>(Dm.dmPelsWidth);
		Out.Height = static_cast<int32>(Dm.dmPelsHeight);
		Out.RefreshNumerator = Dm.dmDisplayFrequency > 1 ? Dm.dmDisplayFrequency : 0; // 0 and 1 mean "the hardware default"
		Out.RefreshDenominator = 1;
		return true;
	}

	bool SetFullscreenRefresh(void* NativeSwapChain, const FSpaceGodDisplayMode& Mode)
	{
		if (!NativeSwapChain) return false;
		TCom<IDXGISwapChain> SwapChain;
		if (FAILED(static_cast<IUnknown*>(NativeSwapChain)->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&SwapChain)))) return false;
		DXGI_SWAP_CHAIN_DESC Desc{};
		if (FAILED(SwapChain->GetDesc(&Desc))) return false;
		DXGI_MODE_DESC Target{};
		Target.Width = static_cast<UINT>(Mode.Width);
		Target.Height = static_cast<UINT>(Mode.Height);
		Target.RefreshRate.Numerator = Mode.RefreshNumerator;
		Target.RefreshRate.Denominator = Mode.RefreshDenominator;
		Target.Format = Desc.BufferDesc.Format;
		return SUCCEEDED(SwapChain->ResizeTarget(&Target));
	}
#else
	TArray<FSpaceGodDisplayMode> Modes(void*) { return {}; }
	bool CurrentMode(void*, FSpaceGodDisplayMode&) { return false; }
	bool SetFullscreenRefresh(void*, const FSpaceGodDisplayMode&) { return false; }
#endif

	bool FindMode(void* NativeWindow, int32 Width, int32 Height, double RefreshHz, FSpaceGodDisplayMode& Out)
	{
		bool Found = false;
		for (const FSpaceGodDisplayMode& m : Modes(NativeWindow))
			if (m.Width == Width && m.Height == Height && (!Found || FMath::Abs(m.RefreshHz() - RefreshHz) < FMath::Abs(Out.RefreshHz() - RefreshHz)))
			{
				Out = m;
				Found = true;
			}
		return Found;
	}

	FString Format(const TArray<FSpaceGodDisplayMode>& Modes)
	{
		FString Text;
		for (const FSpaceGodDisplayMode& m : Modes)
		{
			if (!Text.IsEmpty()) Text += TEXT(";");
			Text += FString::Printf(TEXT("%d %d %u %u"), m.Width, m.Height, m.RefreshNumerator, m.RefreshDenominator);
		}
		return Text;
	}
}
