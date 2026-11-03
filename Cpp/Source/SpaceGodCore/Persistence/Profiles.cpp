// Port of Unity.SpaceGod Core/Persistence/Profiles.cs (US-T20).

#include "Persistence/Profiles.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include "Shared/Json/JsonObject.h"
#include "Shared/Json/MiniJson.h"
#include "Shared/Profiles/ProfileSettings.h"
#include "Shared/Profiles/ProfileStore.h"

namespace SpaceGod::Core
{
	namespace
	{
		namespace fs = std::filesystem;

		fs::path ToPath(const cs::String& s) { return fs::path(std::u8string(s.begin(), s.end())); }

		cs::String FromPath(const fs::path& p)
		{
			auto u = p.u8string();
			return cs::String(u.begin(), u.end());
		}

		/// File.ReadAllText(file, Encoding.UTF8): the text, a UTF-8 byte order mark dropped.
		cs::String ReadAllText(const fs::path& file)
		{
			std::ifstream f(file, std::ios::binary);
			if (!f) throw cs::IOException(cs::Cat("Could not read ", FromPath(file)));
			cs::String text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) text.erase(0, 3);
			return text;
		}

		/// bool.TryParse: "true" / "false" in any case, surrounding white space allowed.
		bool TryParseBool(const cs::String& text, bool& on)
		{
			cs::String t = cs::Strings::Trim(text);
			if (cs::Strings::EqualsOrdinalIgnoreCase(t, "true")) { on = true; return true; }
			if (cs::Strings::EqualsOrdinalIgnoreCase(t, "false")) { on = false; return true; }
			return false;
		}

		bool Exists(const fs::path& p)
		{
			std::error_code ec;
			return fs::exists(p, ec);
		}
	}

	// ================================================================ GameSettings

	cs::String GameSettings::Setting(const cs::Ref<Shared::Profile>& profile, const cs::String& key) { return profile->Settings()->Game()->GetString(key); }

	void GameSettings::SetSetting(const cs::Ref<Shared::Profile>& profile, const cs::String& key, const cs::String& value)
	{
		profile->Settings()->Game()->Set(key, value); // (`value ?? ""`: null is empty already)
		profile->SaveSettings();
	}

	bool GameSettings::Flag(const cs::Ref<Shared::Profile>& profile, const cs::String& key, bool fallback) { return profile->Settings()->Game()->GetBool(key, fallback); }

	void GameSettings::SetFlag(const cs::Ref<Shared::Profile>& profile, const cs::String& key, bool on)
	{
		profile->Settings()->Game()->Set(key, on);
		profile->SaveSettings();
	}

	// ================================================================ LegacyProfileSettings

	cs::List<cs::String> LegacyProfileSettings::MigrateAll(const cs::Ref<Shared::ProfileStore>& store)
	{
		cs::List<cs::String> migrated;
		for (const auto& name : store->List())
		{
			auto profile = store->Get(name);
			if (profile != nullptr && Migrate(profile)) migrated.Add(profile->Name);
		}
		return migrated;
	}

	bool LegacyProfileSettings::Migrate(const cs::Ref<Shared::Profile>& profile)
	{
		fs::path folder = ToPath(profile->Folder()) / ToPath(FolderName);
		std::error_code ec;
		if (!fs::is_directory(folder, ec)) return false;
		try
		{
			auto game = profile->Settings()->Game();
			// Directory.GetFiles(folder, "*.txt")
			for (const auto& entry : fs::directory_iterator(folder))
			{
				if (!entry.is_regular_file()) continue;
				if (!cs::Strings::EqualsOrdinalIgnoreCase(FromPath(entry.path().extension()), ".txt")) continue;
				cs::String key = KeyFor(FromPath(entry.path().stem()));
				if (key.empty() || game->ContainsKey(key)) continue;
				cs::String text = cs::Strings::Trim(ReadAllText(entry.path()));
				bool on = false;
				if (TryParseBool(text, on)) game->Set(key, on);
				else game->Set(key, text);
			}
			profile->SaveSettings();
			fs::rename(folder, ToPath(KeptAsidePath(profile->Folder())));
			return true;
		}
		catch (const cs::IOException&) { return false; }
		catch (const fs::filesystem_error&) { return false; } // (IOException / UnauthorizedAccessException)
	}

	cs::String LegacyProfileSettings::KeyFor(const cs::String& fileName)
	{
		cs::StringBuilder key;
		bool upper = false;
		for (char c : cs::Strings::Trim(fileName))
		{
			if (c == '-' || c == '_' || c == ' ')
			{
				upper = key.Length() > 0;
				continue;
			}
			key.Append(cs::String(1, upper && c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : c));
			upper = false;
		}
		return key.ToString();
	}

	cs::String LegacyProfileSettings::KeptAsidePath(const cs::String& profileFolder)
	{
		fs::path target = ToPath(profileFolder) / ToPath(KeptAsideName);
		if (!Exists(target)) return FromPath(target);
		cs::String stamp = cs::DateTime::UtcNow().ToString("yyyyMMdd'T'HHmmss'Z'");
		target = ToPath(profileFolder) / ToPath(cs::Cat(KeptAsideName, "-", stamp));
		for (int i = 2; Exists(target); i++)
			target = ToPath(profileFolder) / ToPath(cs::Cat(KeptAsideName, "-", stamp, "-", i));
		return FromPath(target);
	}

	// ================================================================ ProfileSettingStore

	ProfileSettingStore::ProfileSettingStore(cs::Ref<Shared::Profile> profile)
	{
		if (!profile) throw cs::ArgumentNullException("profile");
		this->profile = std::move(profile);
	}

	cs::Nullable<cs::String> ProfileSettingStore::ReadSetting(const cs::String& key)
	{
		Shared::Json::JsonValue v = (*profile->Settings()->Game())[key];
		if (auto s = v.AsString()) return *s;
		if (auto b = v.AsBool()) return cs::String(*b ? "true" : "false");
		if (auto n = v.AsNumber()) return n->Raw;
		return std::nullopt;
	}

	void ProfileSettingStore::WriteSetting(const cs::String& key, const cs::String& value)
	{
		auto game = profile->Settings()->Game();
		if (value == "true" || value == "false") game->Set(key, value == "true");
		else if (Shared::Json::MiniJson::IsNumber(value)) game->Set(key, Shared::Json::JsonNumber::FromRaw(value));
		else game->Set(key, value); // (`value ?? ""`)
		profile->SaveSettings();
	}

	void ProfileSettingStore::ForgetSetting(const cs::String& key)
	{
		if (profile->Settings()->Game()->Remove(key)) profile->SaveSettings();
	}
}
