// Port of Unity.SpaceGod Core/DisplaySettings.cs (see docs/MIGRATION.md).

#include "DisplaySettings.h"

#include <limits>
#include <utility>

namespace SpaceGod::Core
{
	const char* ToString(DisplayMode v)
	{
		switch (v)
		{
		case DisplayMode::Windowed: return "Windowed";
		case DisplayMode::Borderless: return "Borderless";
		case DisplayMode::Exclusive: return "Exclusive";
		}
		return "";
	}

	namespace
	{
		// int.TryParse(s, NumberStyles.None, Invariant): ASCII digits only, no sign, no white space.
		bool TryParseDigits(const cs::String& s, int& out)
		{
			if (s.empty()) return false;
			for (char c : s) if (c < '0' || c > '9') return false;
			return cs::Int::TryParse(s, out);
		}

		// Enum.TryParse(text, ignoreCase: false, out DisplayMode): a member name or an integer, white space trimmed,
		// comma-separated parts OR'd together, as .NET does for any enum.
		bool TryParseDisplayMode(const cs::String& text, DisplayMode& mode)
		{
			int value = 0;
			auto parts = cs::Strings::Split(text, ',');
			for (const auto& raw : parts)
			{
				auto p = cs::Strings::Trim(raw);
				int v;
				if (p == "Windowed") v = static_cast<int>(DisplayMode::Windowed);
				else if (p == "Borderless") v = static_cast<int>(DisplayMode::Borderless);
				else if (p == "Exclusive") v = static_cast<int>(DisplayMode::Exclusive);
				else if (!p.empty() && (p[0] == '-' || p[0] == '+' || (p[0] >= '0' && p[0] <= '9')) && cs::Int::TryParse(p, v)) {}
				else return false;
				value |= v;
			}
			mode = static_cast<DisplayMode>(value);
			return true;
		}

		bool IsDefined(DisplayMode m) { int v = static_cast<int>(m); return v >= 0 && v <= 2; }

		int CompareTo(int a, int b) { return a < b ? -1 : (a > b ? 1 : 0); }

		struct PairHash
		{
			size_t operator()(const std::pair<int, int>& k) const { return cs::HashCombine(std::hash<int>{}(k.first), std::hash<int>{}(k.second)); }
		};
	}

	// ---------------------------------------------------------------- DisplayResolution

	size_t DisplayResolution::HashCode() const
	{
		return (static_cast<size_t>(cs::WrapMul(Width, 397)) ^ static_cast<size_t>(Height)) ^ std::hash<double>{}(RefreshHz);
	}

	cs::String DisplayResolution::ToString() const
	{
		return cs::Cat(cs::Str(Width), " x ", cs::Str(Height),
			RefreshHz > 0 ? cs::Cat("  @ ", cs::Str(cs::Math::Round(RefreshHz)), " Hz") : cs::String());
	}

	// ---------------------------------------------------------------- DisplaySettings

	cs::String DisplaySettings::ModeName(DisplayMode mode)
	{
		return mode == DisplayMode::Borderless ? "Borderless windowed" : mode == DisplayMode::Exclusive ? "Exclusive fullscreen" : "Windowed";
	}

	cs::String DisplaySettings::Format() const
	{
		return cs::Cat(cs::Str(Width), "x", cs::Str(Height), ";", Mode, ";", VSync ? "vsync" : "novsync");
	}

	bool DisplaySettings::TryParse(const cs::String& text, DisplaySettings& settings)
	{
		settings = DisplaySettings();
		if (cs::Strings::IsNullOrEmpty(text)) return false;
		auto parts = cs::Strings::Split(text, ';');
		if (parts.Count() != 3) return false;
		auto size = cs::Strings::Split(parts[0], 'x');
		int w = 0, h = 0;
		if (size.Count() != 2
			|| !TryParseDigits(size[0], w)
			|| !TryParseDigits(size[1], h)
			|| w < MinWidth || h < MinHeight)
			return false;
		DisplayMode mode{};
		if (!TryParseDisplayMode(parts[1], mode) || !IsDefined(mode)) return false;
		bool vSync;
		if (parts[2] == "vsync") vSync = true;
		else if (parts[2] == "novsync") vSync = false;
		else return false;
		settings = DisplaySettings(w, h, mode, vSync);
		return true;
	}

	cs::List<DisplayResolution> DisplaySettings::Distinct(const cs::List<DisplayResolution>& modes)
	{
		cs::Dictionary<std::pair<int, int>, DisplayResolution, PairHash> best;
		for (const auto& m : modes)
		{
			if (m.Width < MinWidth || m.Height < MinHeight) continue;
			auto key = std::make_pair(m.Width, m.Height);
			DisplayResolution have;
			if (!best.TryGetValue(key, have) || m.RefreshHz > have.RefreshHz) best[key] = m;
		}
		auto list = best.Values();
		list.Sort([](const DisplayResolution& a, const DisplayResolution& b) { return a.Width != b.Width ? CompareTo(a.Width, b.Width) : CompareTo(a.Height, b.Height); });
		return list;
	}

	const cs::List<cs::String>& DisplaySettings::Aspects()
	{
		static const cs::List<cs::String> value = { "4:3", "5:4", "16:10", "16:9", "21:9", "32:9" };
		return value;
	}

	const cs::List<double>& DisplaySettings::AspectValues()
	{
		static const cs::List<double> value = { 4.0 / 3, 5.0 / 4, 16.0 / 10, 16.0 / 9, 21.0 / 9, 32.0 / 9 };
		return value;
	}

	cs::String DisplaySettings::AspectOf(int width, int height)
	{
		if (width <= 0 || height <= 0) return "Other";
		double r = static_cast<double>(width) / height;
		int best = -1;
		double bestGap = 0.06;
		for (int i = 0; i < AspectValues().Count(); i++)
		{
			double gap = cs::Math::Abs(r / AspectValues()[i] - 1);
			if (gap < bestGap)
			{
				bestGap = gap;
				best = i;
			}
		}
		return best >= 0 ? Aspects()[best] : cs::String("Other");
	}

	const cs::List<DisplaySettings::Size>& DisplaySettings::StandardSizes()
	{
		static const cs::List<Size> value =
		{
			{ 1024, 768 }, { 1280, 960 }, { 1600, 1200 },
			{ 1280, 1024 },
			{ 1280, 800 }, { 1440, 900 }, { 1680, 1050 }, { 1920, 1200 }, { 2560, 1600 },
			{ 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3200, 1800 }, { 3840, 2160 }, { 5120, 2880 },
			{ 2560, 1080 }, { 3440, 1440 }, { 3840, 1600 }, { 5120, 2160 },
			{ 3840, 1080 }, { 5120, 1440 }, { 7680, 2160 },
		};
		return value;
	}

	cs::List<DisplayResolution> DisplaySettings::WithStandardSizes(const cs::List<DisplayResolution>& listed, int maxWidth, int maxHeight)
	{
		cs::List<DisplayResolution> all(listed.begin(), listed.end());
		for (const auto& [w, h] : StandardSizes())
			if (w <= maxWidth && h <= maxHeight) all.Add(DisplayResolution(w, h, 0));
		return Distinct(all);
	}

	cs::List<DisplayResolution> DisplaySettings::OfAspect(const cs::List<DisplayResolution>& list, const cs::String& aspect)
	{
		cs::List<DisplayResolution> result;
		for (const auto& r : list)
			if (cs::Strings::IsNullOrEmpty(aspect) || aspect == "All" || AspectOf(r.Width, r.Height) == aspect) result.Add(r);
		return result;
	}

	int DisplaySettings::IndexOfNearest(const cs::List<DisplayResolution>& list, int width, int height)
	{
		int best = -1;
		long long bestGap = std::numeric_limits<long long>::max();
		long long want = static_cast<long long>(width) * height;
		for (int i = 0; i < list.Count(); i++)
		{
			if (list[i].SameSize(width, height)) return i;
			long long gap = cs::Math::Abs(static_cast<long long>(list[i].Width) * list[i].Height - want);
			if (gap < bestGap)
			{
				bestGap = gap;
				best = i;
			}
		}
		return best;
	}

	size_t DisplaySettings::HashCode() const
	{
		int h = cs::WrapAdd(cs::WrapMul(cs::WrapMul(Width, 397) ^ Height, 397) ^ static_cast<int>(Mode), 0);
		return static_cast<size_t>(cs::WrapAdd(cs::WrapMul(h, 2), VSync ? 1 : 0));
	}

	cs::String DisplaySettings::ToString() const
	{
		return cs::Cat(Width, " x ", Height, ", ", ModeName(Mode), ", VSync ", VSync ? "on" : "off");
	}
}
