#pragma once

// US-T48 faithfulness audit (-SpaceGodAudit, tools/play.ps1 -Track Cpp -Mode audit): after the smoke, presses the
// original's bindings (Core/ControlsHelp.cs) on the scripted device one feature at a time and checks the app state the
// original's code path changes, logging `AUDIT <id> PASS|FAIL: detail`. The C# track's Runtime/Audit/FeatureAudit.cs
// runs the same probes in the same order with the same ids.

#include <functional>

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Smoke
{
	struct ScriptedDevice;

	namespace FeatureAudit
	{
		void Run(UnitySpaceApp& app, ScriptedDevice& script, const std::function<void(int)>& frames);
	}
}
