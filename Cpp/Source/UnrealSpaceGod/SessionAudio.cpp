#include "SessionAudio.h"
#include "UnityCompat/UnityTypes.h"
#include "Shared/Profiles/Profiles.h"

namespace SpaceGod
{
	bool SessionAudio::Muted() { return Shared::Profiles::SessionMuted; }

	void SessionAudio::OnLoad()
	{
		if (!Muted()) return;
		AudioListener::volume = 0.0f;
		Debug::Log("[Unity.SpaceGod] Audio muted for this session (-muted / MINDATTIC_CLI_MUTED=1).");
	}

	float SessionAudio::ListenerVolume(double percent) { return Muted() ? 0.0f : Mathf::Clamp01(static_cast<float>(percent / 100)); }
}
