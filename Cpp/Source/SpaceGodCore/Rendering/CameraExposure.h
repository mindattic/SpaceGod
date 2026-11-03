#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Rendering/CameraExposure.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	/// <summary>
	/// How the game's camera is exposed on a pipeline whose own Volume stack does the post-processing (U.SPC-T451, URP).
	/// Every mode ends in a number of stops added to the drawn image before tonemapping (0 leaves it as the shaders draw).
	/// </summary>
	enum class CameraMetering : int
	{
		/// <summary>Exposure compensation only: the image as drawn, brightened or darkened by a fixed number of stops.</summary>
		Fixed,

		/// <summary>Metered like an eye on the log average of everything in view, adapting over time.</summary>
		Automatic,

		/// <summary>Metered on a histogram of what is in view with its darkest and brightest shares thrown out (black space and the star's face), adapting over time.</summary>
		Histogram,

		/// <summary>Set by the camera's own ISO, shutter and aperture, as on a real camera, against "sunny 16" (EV100 15).</summary>
		PhysicalCamera,
	};
	SPACEGODCORE_API const char* ToString(CameraMetering v);

	/// <summary>The tone curve that maps the scene's range (the star's glare above white) onto the display (U.SPC-T451).</summary>
	enum class ToneCurve : int
	{
		/// <summary>None: values above white clip, as the game drew before.</summary>
		None,

		/// <summary>A neutral curve that rolls highlights off with little change to colour.</summary>
		Neutral,

		/// <summary>The film-like ACES curve: more contrast, saturated highlights desaturating toward white.</summary>
		Aces,
	};
	SPACEGODCORE_API const char* ToString(ToneCurve v);

	/// <summary>
	/// The post-processing choices of the settings menu for a pipeline that runs its own Volume stack (U.SPC-T451, URP):
	/// exposure metering, the physical camera, bloom and colour grading, in pipeline-neutral terms. With
	/// <see cref="Enabled"/> off the game draws exactly as before (no HDR, no post-processing).
	/// </summary>
	struct SPACEGODCORE_API CameraLook
	{
		bool Enabled = false;

		CameraMetering Metering = CameraMetering::Fixed;

		/// <summary>Stops added on top of the metering (positive brightens).</summary>
		double Compensation = 0;

		/// <summary>The physical camera's sensitivity (ISO), shutter time (seconds) and aperture (f-number).</summary>
		double Iso = 0, ShutterSeconds = 0, Aperture = 0;

		/// <summary>Bloom strength (0 for none) and the brightness it starts at (1 is the display's white).</summary>
		double Bloom = 0, BloomThreshold = 0;

		ToneCurve Tonemapping = ToneCurve::None;

		/// <summary>Colour grading: contrast and saturation, -100 to 100, and white balance temperature, -100 (cool) to 100 (warm); 0 leaves the image as it is.</summary>
		double Contrast = 0, Saturation = 0, Temperature = 0;

		/// <summary>How much brighter than a lit surface's white the star's face is drawn, so it glares and blooms (1 = no brighter).</summary>
		double SunBrightness = 0;

		/// <summary>
		/// What a fresh profile gets: the stack on with the image exposed as drawn (fixed, no compensation, no tone
		/// curve, no grading), so the only change from the game without it is the star glaring and blooming; the
		/// adapting meters, the tone curves and grading are the player's to turn on. The physical camera starts at
		/// "sunny 16" (f/16, 1/125 s, ISO 100), which also shows the image as drawn.
		/// </summary>
		static CameraLook Default();

		/// <summary>Whether the exposure adapts to what is in view (and so needs the image metered each frame).</summary>
		bool Adapts() const { return Enabled && (Metering == CameraMetering::Automatic || Metering == CameraMetering::Histogram); }

		bool Equals(const CameraLook& o) const;

		bool operator==(const CameraLook& o) const { return Equals(o); }
		bool operator!=(const CameraLook& o) const { return !Equals(o); }

		/// <summary>C#'s GetHashCode, with .NET's double.GetHashCode for the terms.</summary>
		size_t HashCode() const;
	};

	/// <summary>
	/// The exposure maths of <see cref="CameraLook"/> (U.SPC-T451), all in stops added to the drawn image. The shaders
	/// draw display-referred colour (1.0 is white), metered as if for sunlight at EV100 <see cref="ReferenceEv100"/>, so
	/// a physical camera set to that exposure shows the image as drawn and each EV darker opens it a stop. The adapting
	/// modes meter the scene's luminance (before exposure) against middle grey, inside <see cref="MinStops"/> to
	/// <see cref="MaxStops"/> so deep space never turns grey and the star never blacks the image out, and move toward it
	/// over time as an eye does: quickly when the view brightens, slowly when it darkens.
	/// </summary>
	class SPACEGODCORE_API CameraExposure
	{
	public:
		/// <summary>The exposure the drawn image stands for: sunlight, "sunny 16" (f/16 at 1/125 s and ISO 100 is EV100 14.97).</summary>
		static constexpr double ReferenceEv100 = 15;

		/// <summary>The scene luminance an adapting exposure brings to the display's middle grey.</summary>
		static constexpr double MiddleGrey = 0.18;

		/// <summary>How far an adapting exposure may stop down (the star in frame) and open up (a dim view), in stops.</summary>
		static constexpr double MinStops = -4;
		static constexpr double MaxStops = 1.5;

		/// <summary>The darkest luminance metered (black space counts as this, not as zero), and the brightest.</summary>
		static constexpr double MinLuminance = 1.0 / 1024;
		static constexpr double MaxLuminance = 1024;

		/// <summary>The shares of the histogram's darkest and brightest pixels thrown out before averaging.</summary>
		static constexpr double HistogramLow = 0.45;
		static constexpr double HistogramHigh = 0.98;

		/// <summary>How fast an adapting exposure follows: per second, toward a brighter view and toward a darker one.</summary>
		static constexpr double SpeedToBright = 3.0;
		static constexpr double SpeedToDark = 1.0;

	private:
		static constexpr int HistogramBins = 64;

	public:
		/// <summary>The exposure value at ISO 100 of a camera at f-number <paramref name="aperture"/>, shutter <paramref name="shutterSeconds"/> and sensitivity <paramref name="iso"/>.</summary>
		static double Ev100(double aperture, double shutterSeconds, double iso);

		/// <summary>Stops a physical camera adds to the drawn image: one per EV it sits below the reference.</summary>
		static double PhysicalStops(double aperture, double shutterSeconds, double iso) { return ReferenceEv100 - Ev100(aperture, shutterSeconds, iso); }

		/// <summary>The Rec. 709 luminance of a linear colour.</summary>
		static double Luminance(double r, double g, double b) { return 0.2126 * r + 0.7152 * g + 0.0722 * b; }

	private:
		static double ClampLuminance(double l);

	public:
		/// <summary>The log-average (geometric mean) of <paramref name="luminances"/>, each clamped to the metered range; middle grey for none.</summary>
		// (C# null is the empty list here: middle grey either way.)
		static double LogAverage(const cs::List<double>& luminances);

		/// <summary>
		/// The average luminance of <paramref name="luminances"/> from a histogram in log2 bins, with the darkest
		/// <paramref name="low"/> and brightest share above <paramref name="high"/> of the pixels thrown out; middle grey for none.
		/// </summary>
		static double HistogramAverage(const cs::List<double>& luminances, double low = HistogramLow, double high = HistogramHigh);

		/// <summary>The stops that bring <paramref name="averageLuminance"/> to middle grey, plus <paramref name="compensation"/>, inside the adaptation limits.</summary>
		static double TargetStops(double averageLuminance, double compensation);

		/// <summary>
		/// Moves exposure <paramref name="current"/> toward <paramref name="target"/> over <paramref name="dt"/> seconds:
		/// fast toward a brighter view (fewer stops) and slower toward a darker one, never past the target.
		/// </summary>
		static double Adapt(double current, double target, double dt);

		/// <summary>
		/// The stops a non-adapting <paramref name="look"/> adds: its compensation, plus the physical camera's exposure
		/// in that mode. For an adapting look, its compensation alone (the metering adds the rest); 0 when off.
		/// </summary>
		static double FixedStops(const CameraLook& look);
	};
}
