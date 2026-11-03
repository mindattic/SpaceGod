#include "ActiveProfile.h"
#include "GameSettingsHost.h"
#include "UnityCompat/UnityTypes.h"
#include "UI/DisplayOptions.h"
#include "Shared/Profiles/Profiles.h"
#include "Shared/Profiles/ProfileStore.h"
#include "Persistence/Profiles.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	cs::Ref<Shared::ProfileStore> ActiveProfile::Store()
	{
		if (!Shared::Profiles::IsConfigured()) Shared::Profiles::UseDataPath(Application::persistentDataPath());
		auto store = Shared::Profiles::Store();
		if (!migrated)
		{
			migrated = true;
			for (const auto& name : LegacyProfileSettings::MigrateAll(store))
				Debug::Log(cs::Cat("[Unity.SpaceGod] Moved profile ", name, "'s settings into settings.json (the old files are in Settings.old)."));
		}
		return store;
	}

	cs::Ref<Shared::Profile> ActiveProfile::Current()
	{
		if (!current) current = Store()->Current();
		return current;
	}

	cs::Ref<Shared::Profile> ActiveProfile::Switch(const cs::String& name)
	{
		auto store = Store();
		if (!store->Exists(name)) store->Create(name);
		current = store->Select(name);
		GameSettingsHost::Bind(); // the new profile's settings (U.SPC-T431)
		UI::DisplayOptions::ApplySaved();
		return current;
	}
}
