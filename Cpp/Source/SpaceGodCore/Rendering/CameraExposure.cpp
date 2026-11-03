// Port of Unity.SpaceGod Assets/UnitySpace/Core/Rendering/CameraExposure.cs (see docs/MIGRATION.md).

#include "Rendering/CameraExposure.h"

#include "Rendering/RenderingDetail.h"

namespace SpaceGod::Core
{
	const char* ToString(CameraMetering v)
	{
		switch (v)
		{
			case CameraMetering::Fixed: return "Fixed";
			case CameraMetering::Automatic: return "Automatic";
			case CameraMetering::Histogram: return "Histogram";
			case CameraMetering::PhysicalCamera: return "PhysicalCamera";
		}
		return "";
	}

	const char* ToString(ToneCurve v)
	{
		switch (v)
		{
			case ToneCurve::None: return "None";
			case ToneCurve::Neutral: return "Neutral";
			case ToneCurve::Aces: return "Aces";
		}
		return "";
	}

	CameraLook CameraLook::Default()
	{
		CameraLook l;
		l.Enabled = true; l.Metering = CameraMetering::Fixed; l.Compensation = 0;
		l.Iso = 100; l.ShutterSeconds = 1.0 / 125; l.Aperture = 16;
		l.Bloom = 0.35; l.BloomThreshold = 1.0; l.Tonemapping = ToneCurve::None;
		l.Contrast = 0; l.Saturation = 0; l.Temperature = 0; l.SunBrightness = 8;
		return l;
	}

	bool CameraLook::Equals(const CameraLook& o) const
	{
		using RenderingDetail::DoubleEquals;
		return Enabled == o.Enabled && Metering == o.Metering && DoubleEquals(Compensation, o.Compensation)
			&& DoubleEquals(Iso, o.Iso) && DoubleEquals(ShutterSeconds, o.ShutterSeconds) && DoubleEquals(Aperture, o.Aperture)
			&& DoubleEquals(Bloom, o.Bloom) && DoubleEquals(BloomThreshold, o.BloomThreshold) && Tonemapping == o.Tonemapping
			&& DoubleEquals(Contrast, o.Contrast) && DoubleEquals(Saturation, o.Saturation) && DoubleEquals(Temperature, o.Temperature)
			&& DoubleEquals(SunBrightness, o.SunBrightness);
	}

	size_t CameraLook::HashCode() const
	{
		using RenderingDetail::DoubleHash;
		// unchecked int arithmetic, as C#'s: h * 397 wraps.
		int h = (Enabled ? 1 : 0) | (static_cast<int>(Metering) << 1) | (static_cast<int>(Tonemapping) << 4);
		h = cs::WrapMul(h, 397) ^ DoubleHash(Compensation);
		h = cs::WrapMul(h, 397) ^ DoubleHash(Iso);
		h = cs::WrapMul(h, 397) ^ DoubleHash(ShutterSeconds);
		h = cs::WrapMul(h, 397) ^ DoubleHash(Aperture);
		h = cs::WrapMul(h, 397) ^ DoubleHash(Bloom);
		h = cs::WrapMul(h, 397) ^ DoubleHash(Contrast);
		h = cs::WrapMul(h, 397) ^ DoubleHash(Saturation);
		return static_cast<size_t>(static_cast<uint32_t>(cs::WrapMul(h, 397) ^ DoubleHash(SunBrightness)));
	}

	double CameraExposure::Ev100(double aperture, double shutterSeconds, double iso)
	{
		if (!(aperture > 0) || !(shutterSeconds > 0) || !(iso > 0)) return ReferenceEv100;
		return cs::Math::Log(aperture * aperture / shutterSeconds, 2) - cs::Math::Log(iso / 100.0, 2);
	}

	double CameraExposure::ClampLuminance(double l)
	{
		return cs::Double::IsNaN(l) ? MinLuminance : cs::Math::Max(MinLuminance, cs::Math::Min(MaxLuminance, l));
	}

	double CameraExposure::LogAverage(const cs::List<double>& luminances)
	{
		if (luminances.Count() == 0) return MiddleGrey;
		double sum = 0;
		for (int i = 0; i < luminances.Count(); i++) sum += cs::Math::Log(ClampLuminance(luminances[i]), 2);
		return cs::Math::Pow(2, sum / luminances.Count());
	}

	double CameraExposure::HistogramAverage(const cs::List<double>& luminances, double low, double high)
	{
		if (luminances.Count() == 0) return MiddleGrey;
		low = cs::Math::Max(0.0, cs::Math::Min(1.0, low));
		high = cs::Math::Max(low, cs::Math::Min(1.0, high));
		double minLog = cs::Math::Log(MinLuminance, 2), maxLog = cs::Math::Log(MaxLuminance, 2), width = (maxLog - minLog) / HistogramBins;
		auto bins = cs::NewArray<int>(HistogramBins);
		for (int i = 0; i < luminances.Count(); i++)
		{
			int b = cs::ToInt((cs::Math::Log(ClampLuminance(luminances[i]), 2) - minLog) / width);
			bins[cs::Math::Max(0, cs::Math::Min(HistogramBins - 1, b))]++;
		}
		double total = luminances.Count(), from = low * total, to = high * total, seen = 0, weight = 0, logSum = 0;
		for (int b = 0; b < HistogramBins; b++)
		{
			double start = seen, end = seen + bins[b];
			seen = end;
			double kept = cs::Math::Min(end, to) - cs::Math::Max(start, from);
			if (kept <= 0) continue;
			logSum += kept * (minLog + (b + 0.5) * width);
			weight += kept;
		}
		// Everything thrown out (a range too narrow to keep a pixel): meter the plain average instead.
		return weight > 0 ? cs::Math::Pow(2, logSum / weight) : LogAverage(luminances);
	}

	double CameraExposure::TargetStops(double averageLuminance, double compensation)
	{
		double stops = cs::Math::Log(MiddleGrey / ClampLuminance(averageLuminance), 2);
		return cs::Math::Max(MinStops, cs::Math::Min(MaxStops, stops)) + compensation;
	}

	double CameraExposure::Adapt(double current, double target, double dt)
	{
		if (cs::Double::IsNaN(current) || cs::Double::IsInfinity(current)) return target;
		if (!(dt > 0)) return current;
		double speed = target < current ? SpeedToBright : SpeedToDark;
		return current + (target - current) * (1 - cs::Math::Exp(-dt * speed));
	}

	double CameraExposure::FixedStops(const CameraLook& look)
	{
		if (!look.Enabled) return 0;
		return look.Metering == CameraMetering::PhysicalCamera
			? PhysicalStops(look.Aperture, look.ShutterSeconds, look.Iso) + look.Compensation
			: look.Compensation;
	}
}
