// US-T44: CoopSettings.cs (US-T43) in C++; see CoopSettings.h.

#include "Coop/CoopSettings.h"

#include "Coop/CoopPeer.h"
#include "Coop/CoopWire.h"

#include "Settings/GameSetting.h"
#include "Settings/SettingsRegistry.h"

#include <exception>
#include <map>
#include <set>

using namespace SpaceGod::Core;

namespace SpaceGod::Coop::CoopSettings
{
	namespace
	{
		bool hooked = false, applying = false;
		int applied = 0;
		std::map<cs::String, cs::String> shared;
		std::set<cs::String> watched;

		const std::set<cs::String>& SharedGroups()
		{
			static const std::set<cs::String> groups = {
				SettingGroups::Gameplay /* US-T253: the host's difficulty is everyone's */, SettingGroups::Streaming, SettingGroups::Simulation, SettingGroups::Ftl, SettingGroups::Civilizations, SettingGroups::Ai,
				SettingGroups::Dogfighting, SettingGroups::Broadsides, SettingGroups::Weapons, SettingGroups::Economy,
			};
			return groups;
		}

		void Set(const cs::Ref<GameSetting>& s, const cs::String& value)
		{
			if (auto t = cs::As<ToggleSetting>(s)) t->Set(value == "1");
			else if (auto r = cs::As<RangeSetting>(s)) r->Set(Wire::ParseD(value));
			else if (auto c = cs::As<ChoiceSetting>(s)) c->Set(Wire::ParseI(value));
		}

		void Changed(const cs::Ref<GameSetting>& s)
		{
			if (applying) return;
			CoopPeer* peer = CoopPeer::Current();
			cs::String value = ValueOf(s);
			if (peer == nullptr)
			{
				shared[s->Key] = value;
				return;
			}
			if (peer->IsHost())
			{
				shared[s->Key] = value;
				peer->QueueEntry(cs::Cat("set ", s->Key, " ", value));
				return;
			}
			// A client's own change would run here ahead of everyone: put back, and asked of the host.
			peer->RequestSetting(s->Key, value);
			auto was = shared.find(s->Key);
			if (was != shared.end() && was->second != value)
			{
				applying = true;
				try { Set(s, was->second); }
				catch (...) { applying = false; throw; }
				applying = false;
			}
		}
	}

	int Applied() { return applied; }

	bool IsShared(const cs::Ref<GameSetting>& s) { return s != nullptr && SharedGroups().count(s->Group) > 0; }

	cs::String ValueOf(const cs::Ref<GameSetting>& s)
	{
		if (auto t = cs::As<ToggleSetting>(s)) return t->Value ? "1" : "0";
		if (auto r = cs::As<RangeSetting>(s)) return Wire::D(r->Value);
		if (auto c = cs::As<ChoiceSetting>(s)) return Wire::I(c->Index);
		return cs::String();
	}

	void Hook()
	{
		if (!hooked)
		{
			hooked = true;
			SettingsRegistry::DiscoverLoadedDeclarations();
		}
		for (const auto& s : SettingsRegistry::Default()->All())
		{
			if (!IsShared(s) || !watched.insert(s->Key).second) continue;
			shared[s->Key] = ValueOf(s);
			s->Changed += [](cs::Ref<GameSetting> g) { Changed(g); };
		}
	}

	void Apply(const cs::String& key, const cs::String& value)
	{
		auto s = SettingsRegistry::Default()->Find(key);
		if (s == nullptr || !IsShared(s))
		{
			CoopPeer::Warn(cs::Cat("setting '", key, "' is not a shared one here"));
			return;
		}
		shared[key] = value;
		if (ValueOf(s) == value) return;
		applying = true;
		try { Set(s, value); }
		catch (const std::exception& e) { CoopPeer::Warn(cs::Cat("could not set '", key, "' to ", value, ": ", e.what())); }
		applying = false;
		applied++;
	}

	cs::String Snapshot()
	{
		Hook();
		cs::String sb;
		for (const auto& s : SettingsRegistry::Default()->All())
		{
			if (!IsShared(s)) continue;
			if (!sb.empty()) sb += '\n';
			sb += cs::Cat(s->Key, " ", ValueOf(s));
		}
		return sb;
	}

	int ApplySnapshot(const std::vector<cs::String>& lines)
	{
		Hook();
		int before = applied;
		for (const auto& l : lines)
		{
			size_t sp = l.find(' ');
			if (sp != cs::String::npos && sp > 0) Apply(l.substr(0, sp), l.substr(sp + 1));
		}
		return applied - before;
	}

	bool IsSharedKey(const cs::String& key) { return IsShared(SettingsRegistry::Default()->Find(key)); }
}
