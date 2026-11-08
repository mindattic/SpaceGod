// FeatureAudit's display probes (US-T55, US-T56), line for line the C# track's Runtime/Audit/DisplayAudit.cs.
#include "Smoke/FeatureAuditDisplay.h"

#include "GraphicsSettings.h"
#include "Settings/GameSetting.h"
#include "UI/DisplayOptions.h"
#include "UnityCompat/ShaderBinding.h"

#include "HAL/IConsoleManager.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace SpaceGod::Smoke::FeatureAudit
{
	using namespace SpaceGod::Core;
	using namespace SpaceGod::UI;

	namespace
	{
		int CVar(const TCHAR* name)
		{
			IConsoleVariable* v = IConsoleManager::Get().FindConsoleVariable(name);
			return v ? v->GetInt() : -1;
		}

		cs::String Text(const FString& s) { return cs::String(TCHAR_TO_UTF8(*s)); }

		/// Windows' own list for the primary display (whole Hz), the yardstick DXGI's rates are checked against.
		std::map<std::pair<int, int>, int> WindowsBestRates()
		{
			std::map<std::pair<int, int>, int> best;
#if PLATFORM_WINDOWS
			DEVMODEW dm{};
			dm.dmSize = sizeof(dm);
			for (DWORD i = 0; ::EnumDisplaySettingsW(nullptr, i, &dm); i++)
			{
				auto key = std::make_pair(static_cast<int>(dm.dmPelsWidth), static_cast<int>(dm.dmPelsHeight));
				auto it = best.find(key);
				if (it == best.end() || static_cast<int>(dm.dmDisplayFrequency) > it->second) best[key] = static_cast<int>(dm.dmDisplayFrequency);
			}
#endif
			return best;
		}
	}

	void MsaaProbes(const DisplayReport& result, const std::function<void(int)>& frames)
	{
		// US-T251: render.msaa's toggle is Settings > Graphics > Anti-aliasing now ("MSAA 4x" first, "Off" last).
		const auto& aa = GraphicsSettings::AntiAliasing();
		int before = aa->Index;
		bool wasDefault = aa->IsDefault();
		int forward = CVar(TEXT("r.ForwardShading"));
		aa->Set(aa->Options.IndexOf("Off"));
		frames(1);
		int offMethod = CVar(TEXT("r.AntiAliasingMethod")), offCount = CVar(TEXT("r.MSAACount"));
		float offWidth = UnityCompat::ShaderBinding::LineWidthPx();
		aa->Set(aa->Options.IndexOf("MSAA 4x"));
		frames(1);
		int onMethod = CVar(TEXT("r.AntiAliasingMethod")), onCount = CVar(TEXT("r.MSAACount"));
		float onWidth = UnityCompat::ShaderBinding::LineWidthPx();
		if (wasDefault) aa->Reset(); else aa->Set(before);
		frames(1);
		result("render.msaa", forward == 1 && offMethod == 0 && offCount == 1 && offWidth == 1.0f && onMethod == 3 && onCount == 4 && onWidth == 1.4f,
			Text(FString::Printf(TEXT("forward shading %d; off: AA method %d, %d sample, lines %.1f px; on: AA method %d, %d samples, lines %.1f px"),
				forward, offMethod, offCount, offWidth, onMethod, onCount, onWidth)));
	}

	void DisplayModeProbes(const DisplayReport& result, const std::function<void(int)>& frames)
	{
		cs::List<DisplayBackend::Resolution> raw = DisplayBackend::Resolutions();
		cs::List<DisplayResolution> page = DisplayOptions::Resolutions();
		auto windows = WindowsBestRates();
		std::set<std::pair<int, int>> sizes;
		std::set<double> rateSet;
		for (int i = 0; i < raw.Count(); i++)
		{
			sizes.insert({ raw[i].width, raw[i].height });
			rateSet.insert(std::round(raw[i].refreshRateHz));
		}
		std::vector<DisplayResolution> real;
		for (int i = 0; i < page.Count(); i++)
			if (sizes.count({ page[i].Width, page[i].Height })) real.push_back(page[i]);
		int matching = 0;
		for (const auto& r : real)
		{
			auto it = windows.find({ r.Width, r.Height });
			if (it != windows.end() && std::abs(r.RefreshHz - it->second) < 1.0) matching++;
		}
		FString rates;
		for (double v : rateSet) rates += FString::Printf(TEXT("%s%.0f"), rates.IsEmpty() ? TEXT("") : TEXT(", "), v);
		DisplayResolution largest(0, 0, 0);
		for (const auto& r : real)
			if (static_cast<long long>(r.Width) * r.Height >= static_cast<long long>(largest.Width) * largest.Height) largest = r;
		DisplayBackend::Resolution exact;
		for (int i = 0; i < raw.Count(); i++)
			if (raw[i].width == largest.Width && raw[i].height == largest.Height && raw[i].refreshRateHz >= exact.refreshRateHz) exact = raw[i];
		result("display.modes", !real.empty() && matching == static_cast<int>(real.size()),
			Text(FString::Printf(TEXT("%d modes, %d sizes on the page, %d of them at Windows' best rate; rates %s Hz; largest %s (%u/%u)"),
				raw.Count(), static_cast<int>(real.size()), matching, *rates, UTF8_TO_TCHAR(largest.ToString().c_str()),
				exact.refreshNumerator, exact.refreshDenominator)));

		DisplaySettings was = DisplayOptions::Current;
		DisplayOptions::Apply(DisplaySettings(largest.Width, largest.Height, DisplayMode::Exclusive, was.VSync));
		frames(1);
		DisplayBackend::Resolution asked = DisplayBackend::RequestedRefresh();
		DisplayOptions::Apply(was);
		frames(1);
		double after = DisplayBackend::RequestedRefresh().refreshRateHz;
		result("display.exclusive-rate", largest.Width > 0 && asked.refreshNumerator == exact.refreshNumerator && asked.refreshDenominator == exact.refreshDenominator && after == 0,
			Text(FString::Printf(TEXT("Exclusive %d x %d: asked %u/%u Hz (%.3f); back to %s: %.3f Hz asked"),
				largest.Width, largest.Height, asked.refreshNumerator, asked.refreshDenominator, asked.refreshRateHz,
				UTF8_TO_TCHAR(was.ToString().c_str()), after)));
	}
}
