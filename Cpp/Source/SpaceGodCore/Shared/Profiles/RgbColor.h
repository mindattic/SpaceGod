#pragma once
// Port of Unity.Shared/Runtime/Core/Profiles/RgbColor.cs.

#include "Cs/Cs.h"

namespace SpaceGod::Shared
{
	/// <summary>
	/// An opaque 24-bit colour, engine-free so Core code and dotnet tests can use it. Stored in settings as
	/// <c>"#RRGGBB"</c>. The Unity assembly converts it to and from <c>UnityEngine.Color</c>
	/// (<c>ToUnityColor()</c> / <c>ToRgbColor()</c>).
	/// </summary>
	struct SPACEGODCORE_API RgbColor
	{
		/// <summary>Red, 0-255.</summary>
		uint8_t R = 0;

		/// <summary>Green, 0-255.</summary>
		uint8_t G = 0;

		/// <summary>Blue, 0-255.</summary>
		uint8_t B = 0;

		RgbColor() = default;

		/// <summary>A colour from its three channels.</summary>
		RgbColor(uint8_t r, uint8_t g, uint8_t b) : R(r), G(g), B(b) {}

		/// <summary>A colour from 0-1 channels (as Unity's Color uses), clamped and rounded.</summary>
		static RgbColor FromFloats(float r, float g, float b) { return RgbColor(ToByte(r), ToByte(g), ToByte(b)); }

		/// <summary>Red as 0-1.</summary>
		float RedFloat() const { return R / 255.0f; }

		/// <summary>Green as 0-1.</summary>
		float GreenFloat() const { return G / 255.0f; }

		/// <summary>Blue as 0-1.</summary>
		float BlueFloat() const { return B / 255.0f; }

		/// <summary>The colour as <c>#RRGGBB</c> (upper-case hex).</summary>
		cs::String ToHex() const;

		/// <summary>Reads <c>#RRGGBB</c> or <c>RRGGBB</c> (any case); false for anything else.</summary>
		static bool TryParse(const cs::String& text, RgbColor& color);

		/// <summary>Reads <c>#RRGGBB</c>; throws <see cref="FormatException"/> for anything else.</summary>
		static RgbColor Parse(const cs::String& text);

		/// <summary>
		/// Eight player colours that stay distinct from one another and readable on dark and light backgrounds;
		/// a profile without a chosen colour gets one of these, picked by <see cref="ForName"/>.
		/// </summary>
		static const cs::List<RgbColor>& Palette();

		/// <summary>
		/// The same <see cref="Palette"/> colour for the same name on every run and platform (FNV-1a of the
		/// lower-cased name), so a player who never picks a colour still keeps one.
		/// </summary>
		static RgbColor ForName(const cs::String& name);

		/// <inheritdoc />
		bool Equals(const RgbColor& other) const { return R == other.R && G == other.G && B == other.B; }

		/// <inheritdoc />
		size_t HashCode() const { return static_cast<size_t>((R << 16) | (G << 8) | B); }

		/// <summary>Same colour.</summary>
		bool operator==(const RgbColor& b) const { return Equals(b); }

		/// <summary>Different colour.</summary>
		bool operator!=(const RgbColor& b) const { return !Equals(b); }

		/// <inheritdoc />
		cs::String ToString() const { return ToHex(); }

	private:
		static uint8_t ToByte(float v);
	};
}
