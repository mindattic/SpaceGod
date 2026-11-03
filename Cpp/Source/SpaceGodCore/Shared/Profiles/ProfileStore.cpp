// Port of Unity.Shared/Runtime/Core/Profiles/ProfileStore.cs (ProfileStore, Profile).

#include "Shared/Profiles/ProfileStore.h"
#include "Shared/Profiles/AtomicFile.h"
#include "Shared/Profiles/SharedIo.h"

namespace SpaceGod::Shared
{
	using Json::IJsonCodec;
	using Json::MiniJsonCodec;

	namespace
	{
		// string.Equals(a, b, StringComparison.OrdinalIgnoreCase)
		bool EqualsIgnoreCase(const cs::String& a, const cs::String& b) { return cs::Strings::EqualsOrdinalIgnoreCase(a, b); }

		// StringComparer.OrdinalIgnoreCase.Compare: upper-cases both, then compares ordinally.
		int CompareIgnoreCase(const cs::String& a, const cs::String& b)
		{
			return cs::Strings::CompareOrdinal(cs::Strings::ToUpperInvariant(a), cs::Strings::ToUpperInvariant(b));
		}
	}

	const cs::List<char>& ProfileStore::ForbiddenChars()
	{
		static const cs::List<char> chars{ '/', '\\', ':', '*', '?', '"', '<', '>', '|' };
		return chars;
	}

	const cs::HashSet<cs::String, cs::OrdinalIgnoreCaseComparer>& ProfileStore::ReservedNames()
	{
		static const cs::HashSet<cs::String, cs::OrdinalIgnoreCaseComparer> names{
			"CON", "PRN", "AUX", "NUL",
			"COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
			"LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
		};
		return names;
	}

	namespace
	{
		cs::String CheckedFullPath(const cs::String& root)
		{
			if (cs::Strings::IsNullOrWhiteSpace(root)) throw cs::ArgumentException("A profiles folder is needed. (Parameter 'root')");
			return Io::GetFullPath(root);
		}
	}

	ProfileStore::ProfileStore(const cs::String& root, cs::Ref<IJsonCodec> codec)
		: Root(CheckedFullPath(root)),
		  Codec(codec ? codec : cs::Ref<IJsonCodec>(MiniJsonCodec::Instance()))
	{
	}

	cs::Ref<ProfileStore> ProfileStore::ForDataPath(const cs::String& dataPath, cs::Ref<IJsonCodec> codec)
	{
		return cs::New<ProfileStore>(Io::Combine(dataPath, FolderName), std::move(codec));
	}

	cs::String ProfileStore::CurrentFile() const { return Io::Combine(Root, CurrentFileName); }

	cs::String ProfileStore::TrashFolder() const { return Io::Combine(Root, TrashFolderName); }

	bool ProfileStore::IsValidName(const cs::String& name)
	{
		if (cs::Strings::IsNullOrWhiteSpace(name) || Io::Utf16Length(name) > MaxNameLength || name != cs::Strings::Trim(name)) return false;
		if (cs::Strings::StartsWith(name, ".") || cs::Strings::EndsWith(name, ".")) return false;
		// name.IndexOfAny(ForbiddenChars) / Path.GetInvalidFileNameChars() (Windows: those plus 0-31, which the
		// control-character test below covers too).
		for (char c : name)
			if (ForbiddenChars().Contains(c) || static_cast<unsigned char>(c) < 0x20) return false;
		// char.IsControl: U+0000-001F and U+007F-009F.
		for (uint16_t c : Io::Utf16Units(name))
			if (c < 0x20 || (c >= 0x7F && c <= 0x9F)) return false;
		int dot = cs::Strings::IndexOf(name, '.');
		cs::String stem = dot < 0 ? name : cs::Strings::Substring(name, 0, dot);
		return !ReservedNames().Contains(cs::Strings::TrimEnd(stem));
	}

	cs::List<cs::String> ProfileStore::List() const
	{
		cs::List<cs::String> names;
		if (!Io::DirectoryExists(Root)) return names;
		for (const auto& dir : Io::GetDirectories(Root))
		{
			cs::String n = Io::GetFileName(dir);
			if (IsValidName(n)) names.Add(n);
		}
		// OrderBy(n => n, StringComparer.OrdinalIgnoreCase): a stable sort.
		std::stable_sort(names.Raw().begin(), names.Raw().end(), [](const cs::String& a, const cs::String& b) { return CompareIgnoreCase(a, b) < 0; });
		return names;
	}

	bool ProfileStore::Exists(const cs::String& name) const { return !FolderNameOf(name).empty(); }

	cs::Ref<Profile> ProfileStore::Get(const cs::String& name)
	{
		cs::String actual = FolderNameOf(name);
		if (actual.empty()) return nullptr;
		cs::Ref<Profile> profile;
		if (open.TryGetValue(actual, profile) && profile->Name == actual) return profile;
		profile = cs::New<Profile>(shared_from_this(), actual);
		open[actual] = profile;
		return profile;
	}

	cs::Ref<Profile> ProfileStore::Current()
	{
		cs::String name = ReadCurrentName();
		auto profile = IsValidName(name) ? Get(name) : nullptr;
		if (profile) return profile;
		auto names = List();
		return Select(names.Count() > 0 ? names[0] : Create(DefaultName)->Name);
	}

	cs::Ref<Profile> ProfileStore::Select(const cs::String& name)
	{
		auto profile = Get(name);
		if (!profile) throw cs::ArgumentException(cs::Cat("There is no profile called ", name, ". (Parameter 'name')"));
		AtomicFile::WriteAllText(CurrentFile(), profile->Name + "\n");
		return profile;
	}

	cs::Ref<Profile> ProfileStore::Create(const cs::String& name)
	{
		RequireValid(name, "name");
		if (Exists(name)) throw cs::InvalidOperationException(cs::Cat("A profile called ", name, " already exists."));
		auto profile = cs::New<Profile>(shared_from_this(), name);
		Io::CreateFolder(profile->SavesFolder());
		profile->SaveSettings();
		open[name] = profile;
		return profile;
	}

	cs::Ref<Profile> ProfileStore::Rename(const cs::String& name, const cs::String& newName)
	{
		RequireValid(newName, "newName");
		auto profile = Get(name);
		if (!profile) throw cs::ArgumentException(cs::Cat("There is no profile called ", name, ". (Parameter 'name')"));
		if (profile->Name == newName) return profile;
		bool caseOnly = EqualsIgnoreCase(profile->Name, newName);
		if (!caseOnly && Exists(newName)) throw cs::InvalidOperationException(cs::Cat("A profile called ", newName, " already exists."));

		bool wasCurrent = EqualsIgnoreCase(ReadCurrentName(), profile->Name);
		cs::String from = profile->Folder();
		cs::String to = Io::Combine(Root, newName);
		if (caseOnly)
		{
			// A case-only move is a no-op or an error on case-insensitive file systems: go through a spare name.
			cs::String spare = Io::Combine(Root, ".rename-" + Io::NewGuidN());
			Io::MoveFolder(from, spare);
			try
			{
				Io::MoveFolder(spare, to);
			}
			catch (...)
			{
				// Left under the hidden spare name the profile would vanish from List(): put it back.
				Io::MoveFolder(spare, from);
				throw;
			}
		}
		else
		{
			Io::MoveFolder(from, to);
		}

		open.Remove(profile->Name);
		profile->Renamed(newName);
		open[newName] = profile;
		if (wasCurrent) AtomicFile::WriteAllText(CurrentFile(), newName + "\n");
		return profile;
	}

	cs::String ProfileStore::Delete(const cs::String& name)
	{
		auto profile = Get(name);
		if (!profile) throw cs::ArgumentException(cs::Cat("There is no profile called ", name, ". (Parameter 'name')"));
		bool wasCurrent = EqualsIgnoreCase(ReadCurrentName(), profile->Name);
		Io::CreateFolder(TrashFolder());
		cs::String stamp = cs::DateTime::UtcNow().ToString("yyyyMMdd'T'HHmmss'Z'");
		cs::String target = Io::Combine(TrashFolder(), profile->Name + "~" + stamp);
		for (int i = 2; Io::DirectoryExists(target) || Io::FileExists(target); i++)
			target = Io::Combine(TrashFolder(), cs::Cat(profile->Name, "~", stamp, "-", i));
		Io::MoveFolder(profile->Folder(), target);
		open.Remove(profile->Name);
		if (wasCurrent && Io::FileExists(CurrentFile())) Io::RemoveFile(CurrentFile());
		return target;
	}

	cs::String ProfileStore::ReadCurrentName() const
	{
		try
		{
			return Io::FileExists(CurrentFile()) ? cs::Strings::Trim(Io::ReadAllText(CurrentFile())) : cs::String();
		}
		catch (const cs::IOException&)
		{
			return cs::String();
		}
	}

	cs::String ProfileStore::FolderNameOf(const cs::String& name) const
	{
		if (!IsValidName(name) || !Io::DirectoryExists(Root)) return cs::String();
		for (const auto& existing : List())
			if (EqualsIgnoreCase(existing, name)) return existing;
		return cs::String();
	}

	void ProfileStore::RequireValid(const cs::String& name, const cs::String& param)
	{
		if (!IsValidName(name)) throw cs::ArgumentException(cs::Cat("Not a valid profile name: \"", name, "\". (Parameter '", param, "')"));
	}

	// ------------------------------------------------------------------ Profile
	Profile::Profile(cs::Ref<ProfileStore> storeIn, const cs::String& name) : store(std::move(storeIn)), Name(name) {}

	cs::String Profile::Folder() const { return Io::Combine(store->Root, Name); }

	cs::String Profile::SettingsPath() const { return Io::Combine(Folder(), ProfileSettings::FileName); }

	cs::String Profile::SavesFolder() const { return Io::Combine(Folder(), "Saves"); }

	cs::String Profile::EnsureSavesFolder()
	{
		Io::CreateFolder(SavesFolder());
		return SavesFolder();
	}

	cs::Ref<ProfileSettings> Profile::Settings()
	{
		// `() => Name`: the settings follow a rename. They hold this profile by pointer (C# holds the closure);
		// keep the Profile alive as long as its settings are in use.
		if (!settings) settings = ProfileSettings::Load(SettingsPath(), store->Codec, cs::Func<cs::String>([this]() { return Name; }));
		return settings;
	}

	void Profile::SaveSettings() { Settings()->Save(SettingsPath(), store->Codec); }

	bool Profile::TrySaveSettings()
	{
		try
		{
			SaveSettings();
			return true;
		}
		catch (const cs::IOException&)
		{
			return false;
		}
	}

	cs::Ref<ProfileSettings> Profile::ReloadSettings()
	{
		settings = nullptr;
		return Settings();
	}
}
