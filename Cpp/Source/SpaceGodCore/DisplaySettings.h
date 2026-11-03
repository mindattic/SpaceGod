#pragma once
// Port of Unity.SpaceGod Core/DisplaySettings.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	/// <summary>How the game's window sits on the screen (U.SPC-T276).</summary>
	enum class DisplayMode : int
	{
		/// <summary>An ordinary window with a frame, of the chosen size.</summary>
		Windowed,

		/// <summary>A borderless window covering the whole screen, drawn at the chosen size and scaled to fit.</summary>
		Borderless,

		/// <summary>The screen handed over to the game outright, switched to the chosen size and refresh rate.</summary>
		Exclusive,
	};
	SPACEGODCORE_API const char* ToString(DisplayMode v);

	/// <summary>One size the screen can be set to, at the highest refresh rate it offers that size.</summary>
	struct SPACEGODCORE_API DisplayResolution
	{
		int Width = 0, Height = 0;
		double RefreshHz = 0;

		DisplayResolution() = default;
		DisplayResolution(int width, int height, double refreshHz)
		{
			Width = width;
			Height = height;
			RefreshHz = refreshHz;
		}

		bool SameSize(int width, int height) const { return Width == width && Height == height; }

		// C#: RefreshHz.Equals(other.RefreshHz) -- double.Equals counts NaN equal to NaN.
		bool Equals(const DisplayResolution& other) const
		{
			return Width == other.Width && Height == other.Height
				&& (RefreshHz == other.RefreshHz || (cs::Double::IsNaN(RefreshHz) && cs::Double::IsNaN(other.RefreshHz)));
		}
		// C# DisplayResolution defines no == operator; this is Equals, for cs::List.IndexOf and dictionary keys.
		bool operator==(const DisplayResolution& other) const { return Equals(other); }
		size_t HashCode() const;

		cs::String ToString() const;
	};

	/// <summary>
	/// The player's display settings (U.SPC-T276): the size the game is drawn at, how its window sits on the
	/// screen, and whether frames wait for the screen's refresh. Kept free of the engine, so the pause menu's
	/// choices and what is remembered between runs can be checked without it.
	/// </summary>
	struct SPACEGODCORE_API DisplaySettings
	{
		int Width = 0, Height = 0;
		DisplayMode Mode{};
		bool VSync = false;

		DisplaySettings() = default;
		DisplaySettings(int width, int height, DisplayMode mode, bool vSync)
		{
			Width = width;
			Height = height;
			Mode = mode;
			VSync = vSync;
		}

		DisplaySettings WithSize(int width, int height) const { return DisplaySettings(width, height, Mode, VSync); }
		DisplaySettings WithMode(DisplayMode mode) const { return DisplaySettings(Width, Height, mode, VSync); }
		DisplaySettings WithVSync(bool vSync) const { return DisplaySettings(Width, Height, Mode, vSync); }

		/// <summary>What the menu calls each mode.</summary>
		static cs::String ModeName(DisplayMode mode);

		/// <summary>How the settings are remembered between runs: "1920x1080;Borderless;vsync" (or "novsync").</summary>
		cs::String Format() const;

		/// <summary>Reads back what <see cref="Format"/> wrote; false (and nothing applied) for anything else, such as an
		/// empty or damaged preference, or a size too small to play in.</summary>
		static bool TryParse(const cs::String& text, DisplaySettings& settings);

		/// <summary>The smallest size worth restoring: anything below it is a damaged preference, not a choice.</summary>
		static constexpr int MinWidth = 640, MinHeight = 360;

		/// <summary>
		/// The sizes the menu offers: each width x height once, at the highest refresh rate the screen gives it,
		/// smallest first (by width, then height). Sizes too small to play in are left out.
		/// </summary>
		static cs::List<DisplayResolution> Distinct(const cs::List<DisplayResolution>& modes);

		/// <summary>The aspect ratios the Display page offers, narrowest first (U.SPC-T284): ultrawide 21:9 and super-ultrawide
		/// 32:9 among them.</summary>
		static const cs::List<cs::String>& Aspects();

	private:
		static const cs::List<double>& AspectValues();

	public:
		/// <summary>
		/// The aspect ratio a size is sold as: the nearest of <see cref="Aspects"/> within 6%, so 2560 x 1080 and
		/// 3440 x 1440 (really 64:27 and 43:18) are 21:9 and 5120 x 1440 is 32:9; "Other" for anything further off.
		/// </summary>
		static cs::String AspectOf(int width, int height);

	private:
		/// <summary>The usual sizes of each aspect, so a widescreen or ultrawide one can be picked even where the screen
		/// does not list it (a window or a borderless window can be any size that fits the screen).</summary>
		struct Size { int w = 0, h = 0; };
		static const cs::List<Size>& StandardSizes();

	public:
		/// <summary>
		/// <paramref name="listed"/> (what the screen offers) with the usual sizes of every aspect added that fit within
		/// <paramref name="maxWidth"/> x <paramref name="maxHeight"/> (the screen's own size); each size once, smallest
		/// first. An added size has no refresh rate of its own.
		/// </summary>
		static cs::List<DisplayResolution> WithStandardSizes(const cs::List<DisplayResolution>& listed, int maxWidth, int maxHeight);

		/// <summary>The sizes of <paramref name="list"/> of aspect <paramref name="aspect"/> (null or "All": every one).</summary>
		static cs::List<DisplayResolution> OfAspect(const cs::List<DisplayResolution>& list, const cs::String& aspect);

		/// <summary>Which of <paramref name="list"/> is <paramref name="width"/> x <paramref name="height"/>, or failing
		/// that the one closest to it in pixel count (-1 for an empty list): what the menu starts with picked.</summary>
		static int IndexOfNearest(const cs::List<DisplayResolution>& list, int width, int height);

		bool Equals(const DisplaySettings& other) const { return Width == other.Width && Height == other.Height && Mode == other.Mode && VSync == other.VSync; }
		size_t HashCode() const;
		bool operator==(const DisplaySettings& b) const { return Equals(b); }
		bool operator!=(const DisplaySettings& b) const { return !Equals(b); }

		cs::String ToString() const;
	};

	/// <summary>
	/// The "Keep these display settings?" countdown after Apply (U.SPC-T276): the new settings stand only if the
	/// player says so within <see cref="Seconds"/> of real time; otherwise (or on Revert) the previous ones come back.
	/// </summary>
	class DisplayConfirmation final
	{
	public:
		/// <summary>How long the player has to keep new settings before they are taken back.</summary>
		static constexpr double Seconds = 10;

		/// <summary>What was on the screen before Apply: what a revert goes back to.</summary>
		DisplaySettings Previous;

		/// <summary>What Apply put on the screen: what Keep remembers.</summary>
		DisplaySettings Applied;

		/// <summary>Real seconds left before the settings revert by themselves.</summary>
		double Remaining = 0;

		DisplayConfirmation(DisplaySettings previous, DisplaySettings applied, double seconds = Seconds)
		{
			Previous = previous;
			Applied = applied;
			Remaining = seconds;
		}

		/// <summary>The whole seconds shown in "Reverting in N": 10 the moment it starts, 1 in its last second.</summary>
		int SecondsShown() const { return cs::Math::Max(0, cs::ToInt(cs::Math::Ceiling(Remaining))); }

		/// <summary>Counts down by <paramref name="realSeconds"/> (unscaled: a paused or slowed game still counts);
		/// true once the time is up and the previous settings must come back.</summary>
		bool Tick(double realSeconds)
		{
			if (realSeconds > 0) Remaining = cs::Math::Max(0.0, Remaining - realSeconds);
			return Remaining <= 0;
		}
	};
}
