// Port of Unity.SpaceGod Core/Settings/SettingsRegistry.cs (US-T21, see docs/MIGRATION.md). DiscoverLoadedDeclarations,
// the explicit stand-in for the C# reflection discovery, is in Settings/SettingsSources.cpp.

#include "Settings/SettingsRegistry.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	const cs::Ref<SettingsRegistry>& SettingsRegistry::Default()
	{
		static const cs::Ref<SettingsRegistry> value = cs::New<SettingsRegistry>();
		return value;
	}

	cs::List<cs::Ref<GameSetting>> SettingsRegistry::All()
	{
		std::lock_guard<std::recursive_mutex> lock(gate);
		Adopt();
		return settings.ToArray();
	}

	void SettingsRegistry::Register(const cs::Ref<GameSetting>& setting)
	{
		if (setting == nullptr) throw cs::ArgumentNullException("setting");
		{
			std::lock_guard<std::recursive_mutex> lock(gate);
			Adopt();
			if (byKey.ContainsKey(setting->Key)) throw cs::InvalidOperationException(cs::Cat("Two settings declare the key '", setting->Key, "'."));
			byKey.Add(setting->Key, setting);
			settings.Add(setting);
		}
		setting->Attach(shared_from_this());
		// Declared after a profile was bound (a class first touched later): take the profile's value now.
		auto s = store;
		if (s != nullptr) Load(*setting, *s);
	}

	void SettingsRegistry::RegisterDeclared(GameSetting* setting)
	{
		if (setting == nullptr) throw cs::ArgumentNullException("setting");
		// The setting's constructor is still running: no shared_ptr owns it yet, so it goes in as a non-owning alias
		// (aliasing constructor, empty owner) and is adopted by the next read of the registry.
		cs::Ref<GameSetting> alias(cs::Ref<GameSetting>(), setting);
		{
			std::lock_guard<std::recursive_mutex> lock(gate);
			Adopt();
			if (byKey.ContainsKey(setting->Key)) throw cs::InvalidOperationException(cs::Cat("Two settings declare the key '", setting->Key, "'."));
			byKey.Add(setting->Key, alias);
			settings.Add(alias);
			pending.Add(setting);
		}
		setting->Attach(shared_from_this());
		// Declared after a profile was bound (a class first touched later): take the profile's value now.
		auto s = store;
		if (s != nullptr) Load(*setting, *s);
	}

	void SettingsRegistry::ForgetDeclared(GameSetting* setting)
	{
		std::lock_guard<std::recursive_mutex> lock(gate);
		int i = pending.IndexOf(setting);
		if (i < 0) return;
		pending.RemoveAt(i);
		settings.RemoveAll([&](const cs::Ref<GameSetting>& s) { return s.get() == setting; });
		cs::Ref<GameSetting> known;
		if (byKey.TryGetValue(setting->Key, known) && known.get() == setting) byKey.Remove(setting->Key);
	}

	void SettingsRegistry::Adopt()
	{
		for (int i = pending.Count() - 1; i >= 0; i--)
		{
			GameSetting* p = pending[i];
			auto owned = p->weak_from_this().lock();
			if (!owned) continue; // still inside its constructor (or not made by cs::New): try again next time
			for (int k = 0; k < settings.Count(); k++)
				if (settings[k].get() == p) settings[k] = owned;
			byKey[p->Key] = owned;
			pending.RemoveAt(i);
		}
	}

	cs::Ref<GameSetting> SettingsRegistry::Find(const cs::String& key)
	{
		// C#: if (key == null) return null; -- no null string here.
		{
			std::lock_guard<std::recursive_mutex> lock(gate);
			Adopt();
			cs::Ref<GameSetting> s;
			if (byKey.TryGetValue(key, s)) return s;
		}
		if (this == Default().get() && discovered.exchange(1) == 0)
		{
			DiscoverLoadedDeclarations();
			std::lock_guard<std::recursive_mutex> lock(gate);
			Adopt();
			cs::Ref<GameSetting> s;
			if (byKey.TryGetValue(key, s)) return s;
		}
		return nullptr;
	}

	cs::List<cs::String> SettingsRegistry::Groups()
	{
		// C#: new HashSet<string>(All.Select(s => s.Group))
		cs::HashSet<cs::String> present;
		for (const auto& s : All()) present.Add(s->Group);
		// C#: SettingGroups.Order.Where(present.Contains).ToList()
		cs::List<cs::String> list;
		for (const auto& g : SettingGroups::Order())
			if (present.Contains(g)) list.Add(g);
		// C#: present.Where(g => Array.IndexOf(SettingGroups.Order, g) < 0).OrderBy(g => g, StringComparer.OrdinalIgnoreCase)
		cs::List<cs::String> rest;
		for (const auto& g : present)
			if (SettingGroups::Order().IndexOf(g) < 0) rest.Add(g);
		std::stable_sort(rest.Raw().begin(), rest.Raw().end(), [](const cs::String& a, const cs::String& b)
		{
			return cs::Strings::CompareOrdinal(cs::Strings::ToUpperInvariant(a), cs::Strings::ToUpperInvariant(b)) < 0;
		});
		list.AddRange(rest);
		return list;
	}

	cs::List<cs::Ref<GameSetting>> SettingsRegistry::InGroup(const cs::String& group)
	{
		cs::List<cs::Ref<GameSetting>> list;
		for (const auto& s : All())
			if (s->Group == group) list.Add(s);
		return list;
	}

	cs::List<cs::Ref<GameSetting>> SettingsRegistry::Search(const cs::String& query)
	{
		auto groups = Groups();
		// C#: All.Where(s => s.Matches(query)).OrderBy(s => groups.IndexOf(s.Group)).ToList()
		cs::List<cs::Ref<GameSetting>> matched;
		for (const auto& s : All())
			if (s->Matches(query)) matched.Add(s);
		return cs::OrderBy(matched, [&](const cs::Ref<GameSetting>& s) { return groups.IndexOf(s->Group); });
	}

	void SettingsRegistry::Bind(const cs::Ref<ISettingStore>& newStore)
	{
		store = newStore;
		if (newStore == nullptr) return;
		for (const auto& s : All()) Load(*s, *newStore);
	}

	void SettingsRegistry::Load(GameSetting& s, ISettingStore& from)
	{
		s.SetToDefault();
		cs::Nullable<cs::String> text;
		// C#: catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) { }
		try { text = from.ReadSetting(s.Key); }
		catch (const cs::IOException&) {}
		if (text.has_value()) s.TryLoad(cs::Strings::Trim(*text));
		s.RaiseChanged();
	}

	void SettingsRegistry::Remember(GameSetting& s)
	{
		auto to = store;
		if (to == nullptr) return;
		try
		{
			if (s.IsDefault()) to->ForgetSetting(s.Key);
			else to->WriteSetting(s.Key, s.Format());
		}
		catch (const cs::IOException& e)
		{
			auto ref = s.weak_from_this().lock();
			StoreFailed.Invoke(ref ? ref : cs::Ref<GameSetting>(cs::Ref<GameSetting>(), &s), e);
		}
	}

	void SettingsRegistry::Reset(const cs::Ref<GameSetting>& s)
	{
		if (s == nullptr) return;
		bool changed = !s->IsDefault();
		s->SetToDefault();
		auto to = store;
		if (to != nullptr)
		{
			try { to->ForgetSetting(s->Key); }
			catch (const cs::IOException& e) { StoreFailed.Invoke(s, e); }
		}
		if (changed) s->RaiseChanged();
	}

	void SettingsRegistry::ResetGroup(const cs::String& group)
	{
		for (const auto& s : InGroup(group)) Reset(s);
	}

	void SettingsRegistry::ResetAll()
	{
		for (const auto& s : All()) Reset(s);
	}

	cs::List<cs::Action<>>& SettingsRegistry::ExtraSources()
	{
		static cs::List<cs::Action<>> value;
		return value;
	}

	void SettingsRegistry::AddDeclarationSource(cs::Action<> touch)
	{
		if (touch) ExtraSources().Add(std::move(touch));
	}
}
