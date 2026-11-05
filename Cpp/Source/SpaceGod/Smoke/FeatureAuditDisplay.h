#pragma once

// FeatureAudit's display probes (US-T55, US-T56): render.msaa as the original's 4x MSAA on the forward renderer (and
// Direct3D's MSAA line width), the Display page's modes at their real refresh rates, and the exclusive-fullscreen rate
// asked for as the original's DisplayOptions.Apply does. The C# track's Runtime/Audit/DisplayAudit.cs runs the same
// probes with the same ids and detail text.

#include "Cs/Cs.h"

#include <functional>

namespace SpaceGod::Smoke::FeatureAudit
{
	using DisplayReport = std::function<void(const cs::String& id, bool ok, const cs::String& detail)>;

	void MsaaProbes(const DisplayReport& result, const std::function<void(int)>& frames);
	void DisplayModeProbes(const DisplayReport& result, const std::function<void(int)>& frames);
}
