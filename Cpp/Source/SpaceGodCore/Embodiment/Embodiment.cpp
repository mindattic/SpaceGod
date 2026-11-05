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
		case EmbodimentKind::RepairWorker: return "Worker"; // US-T135
		case EmbodimentKind::Mech: return "Mech";
		case EmbodimentKind::Marine: return "Marine";
		}
		return "";
	}

	const char* ToString(EmbodimentInputProfile v)
	{
		switch (v)
		{
		case EmbodimentInputProfile::Helm: return "Helm";
		case EmbodimentInputProfile::Cockpit: return "Cockpit";
		case EmbodimentInputProfile::Gunner: return "Gunner";
		case EmbodimentInputProfile::Station: return "Station";
		case EmbodimentInputProfile::Satellite: return "Satellite";
		case EmbodimentInputProfile::HullWalker: return "HullWalker";
		case EmbodimentInputProfile::Walker: return "Walker";
		}
		return "";
	}
}
