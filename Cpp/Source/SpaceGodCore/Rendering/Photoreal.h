#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Rendering/Photoreal.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Ship;

	/// <summary>
	/// Photographic exposure as HDRP meters it (U.SPC-T430): an exposure value at ISO 100 (EV100) shows the luminance
	/// 1.2 × 2^EV as white (the saturation-based relation, K = 12.5 and q = 0.65), so each step up halves what reaches
	/// the image. Sunlit scenes sit near EV 15 ("sunny 16").
	/// </summary>
	class SPACEGODCORE_API ExposureMath
	{
	public:
		/// <summary>The luminance (cd/m²) that exposure <paramref name="ev100"/> shows as white.</summary>
		static double LuminanceAtEv100(double ev100) { return 1.2 * cs::Math::Pow(2, ev100); }

		/// <summary>The exposure (EV100) that shows <paramref name="luminance"/> (cd/m², above 0) as white.</summary>
		static double Ev100ForLuminance(double luminance) { return cs::Math::Log(luminance / 1.2, 2); }

		/// <summary>What HDRP multiplies scene light by at exposure <paramref name="ev100"/> (1 / white luminance).</summary>
		static double Multiplier(double ev100) { return 1.0 / LuminanceAtEv100(ev100); }
	};

	/// <summary>How the eye of the camera is metered on a pipeline with physical exposure (U.SPC-T430).</summary>
	enum class SunExposure : int
	{
		/// <summary>Metered once for sunlight (the pipeline's reference exposure): the scene looks as it is drawn.</summary>
		Fixed,

		/// <summary>Adapts to what is in view, like an eye: the star's glare in frame stops the image down, and it opens again in shadow.</summary>
		Adaptive,
	};
	SPACEGODCORE_API const char* ToString(SunExposure v);

	/// <summary>
	/// The photoreal choices the settings menu makes (U.SPC-T430), in pipeline-neutral terms: each backend puts into
	/// effect what its pipeline offers (<see cref="RenderPipelineProfile.Features"/>) and ignores the rest, so URP,
	/// which offers none of them, draws the game exactly as before.
	/// </summary>
	struct SPACEGODCORE_API PhotorealOptions
	{
		/// <summary>A physically based sky and atmosphere for the planet a docking-ring descent the camera rides is coming down to (U.SPC-T452).</summary>
		bool PhysicalSky = false;

		/// <summary>Volumetric light (sunbeams, haze lit by the star) inside a planet's atmosphere.</summary>
		bool Volumetrics = false;

		SunExposure Exposure = SunExposure::Fixed;

		/// <summary>Exposure compensation in stops on top of the metering (positive brightens).</summary>
		double ExposureCompensation = 0;

		/// <summary>How much brighter than a lit surface's white the star's photosphere is drawn, so it glares (1 = no brighter).</summary>
		double SunBrightness = 0;

		/// <summary>Hardware ray tracing, where the GPU and graphics API support it.</summary>
		bool RayTracing = false;

		/// <summary>What a fresh profile gets: the sky, volumetrics and fixed sunlight exposure on, ray tracing off.</summary>
		static PhotorealOptions Default();

		/// <summary>The exposure (EV100) to meter at, on a pipeline whose reference exposure is <paramref name="referenceEv100"/>.</summary>
		double MeteredEv100(double referenceEv100) const { return referenceEv100 - ExposureCompensation; }

		bool Equals(const PhotorealOptions& o) const;

		bool operator==(const PhotorealOptions& o) const { return Equals(o); }
		bool operator!=(const PhotorealOptions& o) const { return !Equals(o); }

		/// <summary>C#'s GetHashCode, with .NET's double.GetHashCode for the terms.</summary>
		size_t HashCode() const;
	};

	/// <summary>
	/// A planet's atmosphere as a physically based sky draws it (U.SPC-T430), in the scene's own units: the planet's
	/// radius, the shell of air above it, and how opaque that air is looking straight up in red, green and blue (the
	/// zenith opacity HDRP's sky takes), with the haze lower down and the fog the camera sits in.
	/// </summary>
	struct AtmosphereShell
	{
		/// <summary>The body whose atmosphere this is (its catalogue index), or -1 for none.</summary>
		int Body = 0;

		double Radius = 0;

		/// <summary>How high the air reaches above the surface.</summary>
		double Thickness = 0;

		/// <summary>The camera's height above the surface (negative inside the planet, which never draws).</summary>
		double Altitude = 0;

		/// <summary>The air's zenith opacity per channel, 0 to 1.</summary>
		double AirR = 0, AirG = 0, AirB = 0;

		/// <summary>The haze's (aerosols') zenith opacity, and how high it reaches.</summary>
		double Haze = 0, HazeThickness = 0;

		/// <summary>The distance light travels through the air at the camera before half of it is scattered; infinite above the air.</summary>
		double FogMeanFreePath = 0;

		bool IsNone() const { return Body < 0; }

		/// <summary>Whether the camera is inside the air.</summary>
		bool CameraInside() const { return Body >= 0 && Altitude < Thickness; }

		static AtmosphereShell None()
		{
			AtmosphereShell s;
			s.Body = -1;
			s.FogMeanFreePath = cs::Double::PositiveInfinity;
			return s;
		}
	};

	/// <summary>
	/// Which planet's atmosphere the physically based sky draws, and how (U.SPC-T430). A sky models one planet, so it
	/// follows the atmosphere the camera is in or closest to (by height over the planet's size), within
	/// <see cref="ReachRadii"/> of the surface; farther out the hand-drawn atmosphere rims carry it alone. It is drawn
	/// only during docking-ring atmospheric entry (<see cref="EntryBody"/>, U.SPC-T452): a narrow experiment behind
	/// the pipeline abstraction, not a renderer migration.
	/// </summary>
	class SPACEGODCORE_API PlanetAtmospheres
	{
	public:
		/// <summary>How far above a surface (in the planet's radii) its sky still draws.</summary>
		static constexpr double ReachRadii = 4.0;

		/// <summary>
		/// Earth's air, as HDRP's physically based sky defaults have it: zenith opacity 0.233 in blue from Rayleigh
		/// scattering of 33.1e-6 per metre over an 8 km scale height. With Earth's atmosphere strength (0.65) and blue
		/// tint this constant gives that back, and red and green follow the tint squared (Rayleigh falls off steeply).
		/// </summary>
		static constexpr double AirOpacityPerStrength = 0.408;

		/// <summary>
		/// How eligible a body is for the sky: its altitude over its radius, or +∞ when it has no atmosphere, is a
		/// star, or is farther than <see cref="ReachRadii"/>. The smallest wins.
		/// </summary>
		static double Closeness(double distanceToCentre, double radius, float atmosphereStrength, bool isStar);

		/// <summary>How high the air reaches, as a share of the planet's radius: 2% for a thin one up to 6% for a thick one.</summary>
		static double ThicknessFraction(float strength) { return 0.02 + 0.04 * Clamp01(strength); }

		/// <summary>
		/// The sky for body <paramref name="body"/>, of radius <paramref name="radius"/>, with the camera
		/// <paramref name="distanceToCentre"/> from its centre, an atmosphere of colour (<paramref name="r"/>,
		/// <paramref name="g"/>, <paramref name="b"/>) and strength <paramref name="strength"/> (as the planet shader takes them).
		/// </summary>
		static AtmosphereShell Describe(int body, double radius, double distanceToCentre, float r, float g, float b, float strength);

		/// <summary>
		/// The fog at the camera: none above the air; inside it, the air thins exponentially with height (a scale height of
		/// a sixth of the shell), and near the surface of a strong atmosphere light goes about the shell's thickness.
		/// </summary>
		static double FogMeanFreePath(const AtmosphereShell& shell, double strength);

	private:
		// C#'s `v < 0 ? 0 : v > 1 ? 1 : v` is a float expression, widened to the double it returns.
		static double Clamp01(float v) { return v < 0 ? 0.0f : v > 1 ? 1.0f : v; }

	public:
		/// <summary>
		/// The one planet whose physically based sky may draw (U.SPC-T452), or -1 for none: the ringed world of the ship
		/// the camera rides (<paramref name="ridden"/>, followed or flown) while that ship, or the ship it escorts, is
		/// coming down through a docking ring, landed at its city, or climbing back to the ring. That descent is the one
		/// sequence in which the player sits inside a single atmosphere, which is what HDRP's sky models; everywhere
		/// else (orbit, open space, other planets seen from afar) the shaders' own per-planet atmosphere rims carry the
		/// air, so the experiment never touches the rest of the renderer (docs/RENDER_PIPELINES_SPIKE.md).
		/// </summary>
		static int EntryBody(const cs::Ref<Ship>& ridden);
	};
}
