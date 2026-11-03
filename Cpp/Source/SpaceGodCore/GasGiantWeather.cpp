// Port of Unity.SpaceGod Core/GasGiantWeather.cs (see docs/MIGRATION.md).

#include "GasGiantWeather.h"
#include "BodyDefinition.h"
#include "BodyOrientation.h"

namespace SpaceGod::Core
{
	const GasGiantWeather::Profile& GasGiantWeather::Jupiter()
	{
		static const Profile value =
		{
			{ -90, 0 }, { -78, 0 }, { -72, 25 }, { -68, -10 }, { -64, 30 }, { -60, -15 }, { -56, 35 }, { -52, -15 },
			{ -48, 35 }, { -44, -20 }, { -40, 40 }, { -36, 30 }, { -32.5, -25 }, { -29, 10 }, { -26.5, 45 },
			{ -23, 0 }, { -19.5, -60 }, { -15, 0 }, { -10, 60 }, { -7, 140 }, { -3, 105 }, { 0, 95 }, { 3, 105 },
			{ 7, 130 }, { 12, 25 }, { 17, -25 }, { 20.5, 20 }, { 23.7, 165 }, { 27, -10 }, { 31, 30 }, { 35.5, -20 },
			{ 39, 35 }, { 43, -15 }, { 48, 25 }, { 53, -10 }, { 56, 30 }, { 60, -10 }, { 66, 20 }, { 78, 0 }, { 90, 0 },
		};
		return value;
	}

	const GasGiantWeather::Profile& GasGiantWeather::Saturn()
	{
		static const Profile value =
		{
			{ -90, 0 }, { -80, 20 }, { -74, 100 }, { -67, -10 }, { -61, 110 }, { -53, -10 }, { -48, 70 }, { -46, 0 },
			{ -41, 90 }, { -36, -20 }, { -30, 60 }, { -20, 220 }, { -10, 360 }, { 0, 400 }, { 10, 360 }, { 20, 220 },
			{ 30, 60 }, { 36, -20 }, { 41, 90 }, { 46, 0 }, { 48, 70 }, { 53, -10 }, { 61, 110 }, { 67, -10 },
			{ 74, 100 }, { 80, 20 }, { 90, 0 },
		};
		return value;
	}

	const GasGiantWeather::Profile& GasGiantWeather::Uranus()
	{
		static const Profile value =
		{
			{ -90, 0 }, { -75, 150 }, { -60, 250 }, { -40, 150 }, { -20, 0 }, { 0, -60 }, { 20, 0 }, { 40, 150 },
			{ 60, 250 }, { 75, 150 }, { 90, 0 },
		};
		return value;
	}

	const GasGiantWeather::Profile& GasGiantWeather::Neptune()
	{
		static const Profile value =
		{
			{ -90, 0 }, { -80, 150 }, { -70, 250 }, { -50, 0 }, { -35, -150 }, { -20, -300 }, { 0, -400 },
			{ 20, -300 }, { 35, -150 }, { 50, 0 }, { 70, 250 }, { 80, 150 }, { 90, 0 },
		};
		return value;
	}

	const GasGiantWeather::Profile* GasGiantWeather::ProfileFor(const cs::String& bodyName)
	{
		if (bodyName == "Jupiter") return &Jupiter();
		if (bodyName == "Saturn") return &Saturn();
		if (bodyName == "Uranus") return &Uranus();
		if (bodyName == "Neptune") return &Neptune();
		return nullptr;
	}

	double GasGiantWeather::WindMs(const Profile* profile, double latDeg)
	{
		const Profile& p = *profile;
		int n = p.Count();
		if (latDeg <= p[0][0]) return p[0][1];
		for (int i = 1; i < n; i++)
		{
			if (latDeg <= p[i][0])
			{
				double t = (latDeg - p[i - 1][0]) / (p[i][0] - p[i - 1][0]);
				return p[i - 1][1] + (p[i][1] - p[i - 1][1]) * t;
			}
		}
		return p[n - 1][1];
	}

	double GasGiantWeather::DriftDegPerDay(const cs::Ref<BodyDefinition>& body, const Profile* profile, double latDeg)
	{
		double u = WindMs(profile, latDeg);
		double circumferenceKm = Astro::TwoPi * body->RadiusKm * cs::Math::Max(cs::Math::Cos(latDeg * Astro::Deg2Rad), 0.05);
		double degPerDay = u / 1000.0 * Astro::SecondsPerDay / circumferenceKm * 360.0;
		return cs::Math::Sign(BodyOrientation::RateDegPerDay(body)) >= 0 ? degPerDay : -degPerDay;
	}

	void GasGiantWeather::SampleDrift(const cs::Ref<BodyDefinition>& body, const Profile* profile, cs::List<float>& output)
	{
		int n = output.Count();
		for (int i = 0; i < n; i++)
		{
			double lat = -90.0 + 180.0 * i / cs::Math::Max(1, n - 1);
			output[i] = static_cast<float>(DriftDegPerDay(body, profile, lat));
		}
	}
}
