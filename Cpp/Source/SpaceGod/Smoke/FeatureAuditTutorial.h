#pragma once

// FeatureAudit's tutorial probe (US-T254): starts the guided tutorial and plays it headless to its end on the scripted
// device, one step at a time by the step's own keys (the navigator's title bar by its stand-in, the one click the scripted
// pointer cannot aim), logging `AUDIT tutorial.<step> PASS|FAIL` for each step and `tutorial.finish` at the end.

#include "Cs/Cs.h"

#include <functional>

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Smoke
{
	struct ScriptedDevice;
}

namespace SpaceGod::Smoke::FeatureAudit
{
	using TutorialReport = std::function<void(const cs::String& id, bool ok, const cs::String& detail)>;

	void TutorialProbes(UnitySpaceApp& app, ScriptedDevice& kb, const TutorialReport& result, const std::function<void(int)>& frames);
}
