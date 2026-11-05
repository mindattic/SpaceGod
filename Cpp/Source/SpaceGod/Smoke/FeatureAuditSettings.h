#pragma once

// FeatureAudit's settings probes (US-T50, US-T51): the master volume and the CLI mute reaching Unreal's audio device, and
// every Render pipeline / Post-processing setting declared, kept in the profile and put into effect on the post-process
// Unreal draws with. The C# track's Runtime/Audit/SettingsAudit.cs runs the same probes with the same ids and text.

#include "Cs/Cs.h"

#include <functional>

namespace SpaceGod::Smoke::FeatureAudit
{
	using Report = std::function<void(const cs::String& id, bool ok, const cs::String& detail)>;

	void AudioProbes(const Report& result, const std::function<void(int)>& frames);
	void RenderProbes(const Report& result, const std::function<void(int)>& frames);
}
