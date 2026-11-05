#pragma once

// FeatureAudit's facade probes (US-T52, US-T53): GUI.TextField as Unity's TextEditor (the caret, the selection by keys
// and mouse, the clipboard, word moves and deletes) on a probe field run through the GUI host's scripted frames, and
// the model viewer's camera drawing the screen on its own (depth, culling mask, its backdrop, its key light).

#include "Cs/Cs.h"

#include <functional>

namespace SpaceGod::Smoke::FeatureAudit
{
	using EditingReport = std::function<void(const cs::String& id, bool ok, const cs::String& detail)>;

	void TextEditProbes(const EditingReport& result);
	void ModelViewerProbes(const EditingReport& result, const std::function<void(int)>& frames);
}
