#pragma once
// Port of Unity.Shared/Runtime/Core/Profiles/ProfileStore.cs (ProfileStore, Profile).

#include "Cs/Cs.h"
#include "Shared/Json/MiniJson.h"
#include "Shared/Profiles/ProfileSettings.h"

namespace SpaceGod::Shared
{
	class Profile;

	/// <summary>
	/// The player profiles under one Profiles folder (normally <c>&lt;persistentDataPath&gt;/Profiles</c>):
	/// a folder per profile holding its <c>settings.json</c> and <c>Saves/</c>, a <c>current.txt</c> naming the one
	/// in use, and <c>.trash/</c> for deleted ones. Plain files, the same on every platform. One instance
	/// hands out one <see cref="Profile"/> object per profile, so settings changed through it are seen everywhere.
	/// Use it from one thread (Unity's main thread).
	/// (Always made with cs::New: a Profile holds its store through shared_from_this.)
	/// </summary>
	class SPACEGODCORE_API ProfileStore final : public std::enable_shared_from_this<ProfileStore>
	{
	public:
		/// <summary>The profile made when there is none.</summary>
		static constexpr const char* DefaultName = "Default";

		/// <summary>The folder under a data path that holds the profiles.</summary>
		static constexpr const char* FolderName = "Profiles";

		/// <summary>The file naming the profile in use.</summary>
		static constexpr const char* CurrentFileName = "current.txt";

		/// <summary>Where deleted profiles go (HOUSE-LAW-2: nothing is destroyed, only moved aside).</summary>
		static constexpr const char* TrashFolderName = ".trash";

		/// <summary>The longest profile name.</summary>
		static constexpr int MaxNameLength = 64;

	private:
		static const cs::List<char>& ForbiddenChars();

		static const cs::HashSet<cs::String, cs::OrdinalIgnoreCaseComparer>& ReservedNames();

		cs::Dictionary<cs::String, cs::Ref<Profile>, cs::OrdinalIgnoreCaseComparer> open;

	public:
		/// <summary>A store over the Profiles folder <paramref name="root"/> (the override tests and automation use).</summary>
		explicit ProfileStore(const cs::String& root, cs::Ref<Json::IJsonCodec> codec = nullptr);

		/// <summary>The store under <c>&lt;dataPath&gt;/Profiles</c>, e.g. Unity's <c>Application.persistentDataPath</c>.</summary>
		static cs::Ref<ProfileStore> ForDataPath(const cs::String& dataPath, cs::Ref<Json::IJsonCodec> codec = nullptr);

		/// <summary>The Profiles folder.</summary>
		const cs::String Root;

		/// <summary>How settings files are read and written.</summary>
		const cs::Ref<Json::IJsonCodec> Codec;

		/// <summary>The path of <c>current.txt</c>.</summary>
		cs::String CurrentFile() const;

		/// <summary>The folder deleted profiles are moved into.</summary>
		cs::String TrashFolder() const;

		/// <summary>
		/// A name that makes one folder on every platform: not blank, at most 64 characters, no leading or
		/// trailing spaces, not starting with '.' or ending with '.', no path or wildcard characters
		/// (<c>/ \ : * ? " &lt; &gt; |</c>) and no control characters, and not a Windows device name (CON, NUL, COM1...).
		/// </summary>
		static bool IsValidName(const cs::String& name);

		/// <summary>Every profile's name, sorted ignoring case.</summary>
		cs::List<cs::String> List() const;

		/// <summary>Whether a profile called <paramref name="name"/> exists (ignoring case: two names differing only in case would share a folder on Windows).</summary>
		bool Exists(const cs::String& name) const;

		/// <summary>The profile called <paramref name="name"/> (ignoring case), or null when there is none.</summary>
		cs::Ref<Profile> Get(const cs::String& name);

		/// <summary>
		/// The profile in use: the one <c>current.txt</c> names; if it names none that exists, the first profile
		/// by name; if there are none, a new <see cref="DefaultName"/> profile. Whichever it is, it is then recorded
		/// in <c>current.txt</c>.
		/// </summary>
		cs::Ref<Profile> Current();

		/// <summary>Makes <paramref name="name"/> the profile in use. Throws when there is no such profile.</summary>
		cs::Ref<Profile> Select(const cs::String& name);

		/// <summary>
		/// Makes a new profile: its folder, its <c>Saves/</c> folder and a default <c>settings.json</c>. Throws
		/// <see cref="ArgumentException"/> for a bad name and <see cref="InvalidOperationException"/> when it exists.
		/// It does not become current; call <see cref="Select"/> for that.
		/// </summary>
		cs::Ref<Profile> Create(const cs::String& name);

		/// <summary>
		/// Renames a profile's folder (a change of case alone is allowed). The <see cref="Profile"/> object keeps
		/// working under its new name, and <c>current.txt</c> follows it if it was the profile in use.
		/// </summary>
		cs::Ref<Profile> Rename(const cs::String& name, const cs::String& newName);

		/// <summary>
		/// Deletes a profile by moving its folder to <c>.trash/&lt;name&gt;~&lt;UTC timestamp&gt;</c>, never by erasing
		/// it. If it was in use, <c>current.txt</c> is cleared and <see cref="Current"/> picks another next time.
		/// Returns where the folder went.
		/// </summary>
		cs::String Delete(const cs::String& name);

	private:
		cs::String ReadCurrentName() const; // empty for none (C# null)

		cs::String FolderNameOf(const cs::String& name) const; // empty for none (C# null)

		static void RequireValid(const cs::String& name, const cs::String& param);
	};

	/// <summary>
	/// One player profile: a folder with <c>settings.json</c> (see <see cref="Settings"/>) and <c>Saves/</c>
	/// for the game's own save files. Get it from <see cref="ProfileStore"/> or <see cref="Profiles.Current"/>.
	/// </summary>
	class SPACEGODCORE_API Profile final
	{
		const cs::Ref<ProfileStore> store;
		cs::Ref<ProfileSettings> settings;

	public:
		// internal in C#.
		Profile(cs::Ref<ProfileStore> store, const cs::String& name);

		/// <summary>The profile's name, which is also its folder's name.</summary>
		cs::String Name; // { get; private set; }

		/// <summary>The profile's folder.</summary>
		cs::String Folder() const;

		/// <summary>The path of its settings.json.</summary>
		cs::String SettingsPath() const;

		/// <summary>The folder for the game's own save files (made by <see cref="ProfileStore.Create"/>; call <see cref="EnsureSavesFolder"/> to be sure).</summary>
		cs::String SavesFolder() const;

		/// <summary>Makes <see cref="SavesFolder"/> if it is missing and returns it.</summary>
		cs::String EnsureSavesFolder();

		/// <summary>
		/// The profile's settings, read from disk the first time they are asked for (never throws: a missing or
		/// damaged file gives defaults, see <see cref="ProfileSettings.Status"/>). Changes stay in memory until
		/// <see cref="SaveSettings"/>.
		/// </summary>
		cs::Ref<ProfileSettings> Settings();

		/// <summary>Writes <see cref="Settings"/> to settings.json atomically. Throws on an I/O failure; the old file is then untouched.</summary>
		void SaveSettings();

		/// <summary>As <see cref="SaveSettings"/>, but returns false instead of throwing when the disk refuses.</summary>
		bool TrySaveSettings();

		/// <summary>Drops unsaved changes and reads settings.json again.</summary>
		cs::Ref<ProfileSettings> ReloadSettings();

		void Renamed(const cs::String& newName) { Name = newName; } // internal in C#

		/// <inheritdoc />
		cs::String ToString() const { return Name; }
	};
}
