// Port of Unity.SpaceGod Core/Embodiment/Embodiment.cs (US-T21, see docs/MIGRATION.md).

#include "Embodiment/Embodiment.h"

namespace SpaceGod::Core
{
	const char* ToString(EmbodimentKind v)
	{
		switch (v)
		{
		case EmbodimentKind::Ship: return "Ship";
		case EmbodimentKind::Fighter: return "Fighter";
		case EmbodimentKind::Turret: return "Turret";
		case EmbodimentKind::Station: return "Station";
		case EmbodimentKind::Satellite: return "Satellite";
		}
		return "";
	}
}
