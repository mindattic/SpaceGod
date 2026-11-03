// Port of Unity.SpaceGod Assets/UnitySpace/Core/Rendering/Photoreal.cs (see docs/MIGRATION.md).

#include "Rendering/Photoreal.h"

#include "Infrastructure/Infrastructure.h"
#include "Rendering/RenderingDetail.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	const char* ToString(SunExposure v)
	{
		switch (v)
		{
			case SunExposure::Fixed: return "Fixed";
			case SunExposure::Adaptive: return "Adaptive";
		}
		return "";
	}

	PhotorealOptions PhotorealOptions::Default()
	{
		PhotorealOptions o;
		o.PhysicalSky = true; o.Volumetrics = true; o.Exposure = SunExposure::Fixed; o.ExposureCompensation = 0; o.SunBrightness = 16; o.RayTracing = false;
		return o;
	}

	bool PhotorealOptions::Equals(const PhotorealOptions& o) const
	{
		using RenderingDetail::DoubleEquals;
		return PhysicalSky == o.PhysicalSky && Volumetrics == o.Volumetrics && Exposure == o.Exposure
			&& DoubleEquals(ExposureCompensation, o.ExposureCompensation) && DoubleEquals(SunBrightness, o.SunBrightness) && RayTracing == o.RayTracing;
	}

	size_t PhotorealOptions::HashCode() const
	{
		using RenderingDetail::DoubleHash;
		int flags = (PhysicalSky ? 1 : 0) | (Volumetrics ? 2 : 0) | (static_cast<int>(Exposure) << 2) | (RayTracing ? 16 : 0);
		// unchecked: (flags * 397 ^ a) * 397 ^ b, wrapping.
		int h = cs::WrapMul(cs::WrapMul(flags, 397) ^ DoubleHash(ExposureCompensation), 397) ^ DoubleHash(SunBrightness);
		return static_cast<size_t>(static_cast<uint32_t>(h));
	}

	double PlanetAtmospheres::Closeness(double distanceToCentre, double radius, float atmosphereStrength, bool isStar)
	{
		if (isStar || !(atmosphereStrength > 0) || !(radius > 0) || cs::Double::IsNaN(distanceToCentre)) return cs::Double::PositiveInfinity;
		double altitude = (distanceToCentre - radius) / radius;
		if (altitude > ReachRadii) return cs::Double::PositiveInfinity;
		return cs::Math::Max(altitude, 0.0);
	}

	AtmosphereShell PlanetAtmospheres::Describe(int body, double radius, double distanceToCentre, float r, float g, float b, float strength)
	{
		if (body < 0 || !(radius > 0) || !(strength > 0)) return AtmosphereShell::None();
		double s = Clamp01(strength);
		// C#: Math.Max(1e-3, Math.Max(r, Math.Max(g, b))) -- the inner two in float, the outer in double.
		double maxChannel = cs::Math::Max(1e-3, static_cast<double>(cs::Math::Max(r, cs::Math::Max(g, b))));
		auto Opacity = [&](float c)
		{
			double t = Clamp01(static_cast<float>(c / maxChannel));
			return 1 - cs::Math::Exp(-AirOpacityPerStrength * s * t * t);
		};

		double thickness = radius * ThicknessFraction(strength);
		double altitude = distanceToCentre - radius;
		AtmosphereShell shell;
		shell.Body = body; shell.Radius = radius; shell.Thickness = thickness; shell.Altitude = altitude;
		shell.AirR = Opacity(r); shell.AirG = Opacity(g); shell.AirB = Opacity(b);
		// Haze: Earth's aerosols reach about a seventh of the way up its air; thicker air holds more.
		shell.Haze = 1 - cs::Math::Exp(-0.02 * s * 4); shell.HazeThickness = thickness * 0.15;
		shell.FogMeanFreePath = FogMeanFreePath(shell, s);
		return shell;
	}

	double PlanetAtmospheres::FogMeanFreePath(const AtmosphereShell& shell, double strength)
	{
		if (shell.IsNone() || !(strength > 0) || shell.Altitude >= shell.Thickness) return cs::Double::PositiveInfinity;
		double scaleHeight = shell.Thickness / 6.0;
		double density = cs::Math::Exp(-cs::Math::Max(shell.Altitude, 0.0) / scaleHeight);
		return shell.Thickness / (Clamp01(static_cast<float>(strength)) * density);
	}

	int PlanetAtmospheres::EntryBody(const cs::Ref<Ship>& ridden)
	{
		if (ridden == nullptr || ridden->Destroyed) return -1;
		auto c = ridden->Controller();
		return c != nullptr && !c->Destroyed && c->Ring != nullptr && c->Planetside() ? c->Ring->Body : -1;
	}
}
