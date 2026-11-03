#pragma once
// Port of Unity.SpaceGod Core/Persistence/Profiles.cs (US-T20): GameSettings, LegacyProfileSettings, ProfileSettingStore.
//
// C++ notes:
// - GameSettings' C# extension methods on Profile (`profile.Setting(key)`) are static members taking the profile
//   first: `GameSettings::Setting(profile, key)`.
// - A C# null string is an empty cs::String (Setting's "never set"); ISettingStore.ReadSetting keeps null apart from ""
//   with cs::Nullable, as Settings/SettingsRegistry.h declares it.
// - LegacyProfileSettings.KeyFor upper-cases ASCII letters only (C#'s char.ToUpperInvariant also folds other
//   letters; the legacy file names were ASCII).

#include "Cs/Cs.h"
#include "Settings/SettingsRegistry.h" // ISettingStore (base class)

namespace SpaceGod::Shared
{
	class Profile;
	class ProfileStore;
}

namespace SpaceGod::Core
{
	/// <summary>
	/// Unity.SpaceGod's own settings, kept in the <c>game</c> block of the profile's one <c>settings.json</c>
	/// (the shared mindattic.settings v1 format from the Unity.Shared package, U.SPC-T325). The profiles
	/// themselves (current.txt, one folder each, Saves/) are the package's <see cref="ProfileStore"/>.
	/// </summary>
	class SPACEGODCORE_API GameSettings
	{
	public:
		/// <summary>The window size, mode and VSync, as <c>DisplaySettings.Format()</c> writes them.</summary>
		static constexpr const char* Display = "display";

		/// <summary>Whether the view swings level with each system's plane.</summary>
		static constexpr const char* AutoReorient = "autoReorient";

		/// <summary>The text of <paramref name="key"/>, or null when it was never set or is not text.</summary>
		static cs::String Setting(const cs::Ref<Shared::Profile>& profile, const cs::String& key);

		/// <summary>Sets <paramref name="key"/> and writes settings.json (throws when the disk refuses; the old file is then intact).</summary>
		static void SetSetting(const cs::Ref<Shared::Profile>& profile, const cs::String& key, const cs::String& value);

		/// <summary>An on/off setting, or <paramref name="fallback"/> when it was never set or is not one.</summary>
		static bool Flag(const cs::Ref<Shared::Profile>& profile, const cs::String& key, bool fallback);

		/// <summary>Sets an on/off setting and writes settings.json.</summary>
		static void SetFlag(const cs::Ref<Shared::Profile>& profile, const cs::String& key, bool on);
	};

	/// <summary>
	/// Moves profiles written before U.SPC-T325, which kept each setting in a file of its own
	/// (<c>Profiles/&lt;Name&gt;/Settings/display.txt</c>, <c>auto-reorient.txt</c>...), onto the single
	/// <c>Profiles/&lt;Name&gt;/settings.json</c>: every <c>&lt;setting&gt;.txt</c> goes into the <c>game</c> block under
	/// its camelCase name (<c>true</c>/<c>false</c> as a JSON bool, anything else as text), a value already in
	/// settings.json wins, and the old <c>Settings</c> folder is then kept aside as <c>Settings.old</c>, never erased.
	/// current.txt and Saves/ are not touched.
	/// </summary>
	class SPACEGODCORE_API LegacyProfileSettings
	{
	public:
		/// <summary>The per-setting folder profiles had before settings.json.</summary>
		static constexpr const char* FolderName = "Settings";

		/// <summary>What the old folder is renamed to once its settings are in settings.json.</summary>
		static constexpr const char* KeptAsideName = "Settings.old";

		/// <summary>Migrates every profile in <paramref name="store"/> that still has a Settings folder; returns the names of those migrated.</summary>
		static cs::List<cs::String> MigrateAll(const cs::Ref<Shared::ProfileStore>& store);

		/// <summary>
		/// Migrates one profile. Returns true when it had a Settings folder and it was moved into settings.json and
		/// kept aside; false when there was nothing to do or the disk refused (the old files are then left as they
		/// were, to be tried again next run).
		/// </summary>
		static bool Migrate(const cs::Ref<Shared::Profile>& profile);

		/// <summary>A setting file's name as a settings.json key: "auto-reorient" is "autoReorient".</summary>
		static cs::String KeyFor(const cs::String& fileName);

	private:
		static cs::String KeptAsidePath(const cs::String& profileFolder);
	};

	/// <summary>
	/// The settings registry's store (U.SPC-T431) over a profile's settings.json: each declared setting is a field
	/// of the <c>game</c> block under its key, an on/off one as a JSON bool and a number as a JSON number (so the
	/// auto-reorient flag reads and writes exactly as it always has), anything else as text. Each change is
	/// written to settings.json at once; one back at its default is removed.
	/// </summary>
	class SPACEGODCORE_API ProfileSettingStore final : public ISettingStore
	{
	public:
		explicit ProfileSettingStore(cs::Ref<Shared::Profile> profile);

		cs::Nullable<cs::String> ReadSetting(const cs::String& key) override;

		void WriteSetting(const cs::String& key, const cs::String& value) override;

		void ForgetSetting(const cs::String& key) override;

	private:
		cs::Ref<Shared::Profile> profile;
	};
}
