#pragma once

// US-T44: the settings the Core reads, shared in co-op: the C# track's CSharp/.../Runtime/Coop/CoopSettings.cs (US-T43) in
// C++. The groups the simulation reads are the host's: every sync carries its values and every peer takes them before it
// loads the save; a change the host makes goes out as a frame entry ("set <key> <value>"); a client's own change is put
// back at once and asked of the host. The view's own settings (graphics, LOD, controls, audio) stay each machine's.

#include "Cs/Cs.h"

#include <vector>

namespace SpaceGod::Core { class GameSetting; }

namespace SpaceGod::Coop::CoopSettings
{
	int Applied();
	bool IsShared(const cs::Ref<Core::GameSetting>& s);
	/// A setting's value as the wire carries it (exact: a range as its IEEE bits, a choice as its index).
	cs::String ValueOf(const cs::Ref<Core::GameSetting>& s);
	/// Watches every shared setting on this machine.
	void Hook();
	/// A "set" entry from a frame (or a sync's list): applied without echoing it back.
	void Apply(const cs::String& key, const cs::String& value);
	/// The host's values of every shared setting, one "key value" a line.
	cs::String Snapshot();
	/// Takes the host's values; returns how many differed here.
	int ApplySnapshot(const std::vector<cs::String>& lines);
	bool IsSharedKey(const cs::String& key);
}
