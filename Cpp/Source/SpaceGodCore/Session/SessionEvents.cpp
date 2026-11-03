// Port of Unity.SpaceGod Core/Session/SessionEvents.cs (US-T21, see docs/MIGRATION.md).

#include "Session/SessionEvents.h"
#include "Session/Commands.h"

namespace SpaceGod::Core
{
	PlayerId CommandRefused::Player() const { return Command ? Command->Player() : PlayerId::None(); }
}
