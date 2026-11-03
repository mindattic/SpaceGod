// Port of Unity.SpaceGod Core/BodyDefinition.cs (see docs/MIGRATION.md).

#include "BodyDefinition.h"

namespace SpaceGod::Core
{
	const char* ToString(BodyKind v)
	{
		switch (v)
		{
		case BodyKind::Star: return "Star";
		case BodyKind::Planet: return "Planet";
		case BodyKind::DwarfPlanet: return "DwarfPlanet";
		case BodyKind::Moon: return "Moon";
		case BodyKind::Barycentre: return "Barycentre";
		case BodyKind::Station: return "Station";
		case BodyKind::Asteroid: return "Asteroid";
		}
		return "";
	}

	const char* ToString(BodyStatus v)
	{
		switch (v)
		{
		case BodyStatus::Confirmed: return "Confirmed";
		case BodyStatus::Candidate: return "Candidate";
		case BodyStatus::Disputed: return "Disputed";
		case BodyStatus::Fictional: return "Fictional";
		}
		return "";
	}

	const char* ToString(OrbitPlane v)
	{
		switch (v)
		{
		case OrbitPlane::Ecliptic: return "Ecliptic";
		case OrbitPlane::ParentEquator: return "ParentEquator";
		}
		return "";
	}
}
