// Port of Unity.SpaceGod Core/Embodiment/EmbodimentSession.cs (US-T21, see docs/MIGRATION.md).

#include "Embodiment/EmbodimentSession.h"

namespace SpaceGod::Core
{
	bool EmbodimentSession::Switch(cs::Ref<IEmbodiable> next)
	{
		if (next != nullptr && EmbodiableEquals(next, Current) && Current->Embodied()) return true;
		Release();
		EndedBecause = cs::String();
		if (next == nullptr || !next->CanEmbody()) return false;
		next->TakeCharge();
		Current = next;
		return true;
	}

	void EmbodimentSession::Release()
	{
		auto was = Current;
		Current = nullptr;
		if (was != nullptr && was->Embodied()) was->HandBack();
	}

	bool EmbodimentSession::MoveToHost()
	{
		// C#: Current?.Host != null && Switch(Current.Host)
		return Current != nullptr && Current->Host() != nullptr && Switch(Current->Host());
	}

	bool EmbodimentSession::Step(const EmbodimentInput& input, double dt)
	{
		if (Current == nullptr) return false;
		if (!Current->CanEmbody())
		{
			EndedBecause = Current->Blocker();
			Release();
			return false;
		}
		Current->Control(input, dt);
		return true;
	}
}
