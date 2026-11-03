// Port of Unity.Shared/Runtime/Core/Profiles/RgbColor.cs.

#include "Shared/Profiles/RgbColor.h"
#include "Shared/Profiles/SharedIo.h"

namespace SpaceGod::Shared
{
	uint8_t RgbColor::ToByte(float v)
	{
		// (byte)Math.Round(Math.Max(0f, Math.Min(1f, float.IsNaN(v) ? 0f : v)) * 255f): the product is a float,
		// then Math.Round(double) rounds it to even.
		float clamped = cs::Math::Max(0.0f, cs::Math::Min(1.0f, cs::Single::IsNaN(v) ? 0.0f : v));
		float scaled = clamped * 255.0f;
		return static_cast<uint8_t>(cs::Math::Round(static_cast<double>(scaled)));
	}

	cs::String RgbColor::ToHex() const
	{
		return cs::Cat("#", cs::Fmt(static_cast<int>(R), "X2"), cs::Fmt(static_cast<int>(G), "X2"), cs::Fmt(static_cast<int>(B), "X2"));
	}

	bool RgbColor::TryParse(const cs::String& textIn, RgbColor& color)
	{
		color = RgbColor();
		// (C#: `if (text == null) return false;` -- an empty string fails the length test below.)
		cs::String text = cs::Strings::Trim(textIn);
		if (cs::Strings::StartsWith(text, "#")) text = cs::Strings::Substring(text, 1);
		if (Io::Utf16Length(text) != 6) return false;
		// int.TryParse(text, NumberStyles.AllowHexSpecifier, ...): hex digits only.
		int v = 0;
		for (char c : text)
		{
			int d;
			if (c >= '0' && c <= '9') d = c - '0';
			else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
			else return false;
			v = v * 16 + d;
		}
		color = RgbColor(static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v));
		return true;
	}

	RgbColor RgbColor::Parse(const cs::String& text)
	{
		RgbColor c;
		if (TryParse(text, c)) return c;
		throw cs::FormatException(cs::Cat("Not a #RRGGBB colour: ", text));
	}

	const cs::List<RgbColor>& RgbColor::Palette()
	{
		static const cs::List<RgbColor> palette{
			RgbColor(0xE6, 0x4B, 0x3C), // red
			RgbColor(0x34, 0x98, 0xDB), // blue
			RgbColor(0x2E, 0xCC, 0x71), // green
			RgbColor(0xF1, 0xC4, 0x0F), // yellow
			RgbColor(0x9B, 0x59, 0xB6), // purple
			RgbColor(0xE6, 0x7E, 0x22), // orange
			RgbColor(0x1A, 0xBC, 0x9C), // teal
			RgbColor(0xE8, 0x43, 0x93), // pink
		};
		return palette;
	}

	RgbColor RgbColor::ForName(const cs::String& name)
	{
		uint32_t hash = 2166136261u;
		// foreach (char c in (name ?? "").ToLowerInvariant()): FNV-1a over the UTF-16 units. (ToLowerInvariant
		// here folds ASCII only; a non-ASCII upper-case letter in a profile name would hash differently.)
		for (uint16_t c : Io::Utf16Units(cs::Strings::ToLowerInvariant(name)))
		{
			hash ^= c;
			hash *= 16777619u;
		}
		return Palette()[static_cast<int>(hash % static_cast<uint32_t>(Palette().Count()))];
	}
}
