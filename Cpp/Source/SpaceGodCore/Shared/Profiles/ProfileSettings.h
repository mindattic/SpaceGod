#pragma once
// Port of Unity.Shared/Runtime/Core/Profiles/ProfileSettings.cs.

#include "Cs/Cs.h"
#include "Shared/Json/JsonObject.h"
#include "Shared/Json/MiniJson.h"
#include "Shared/Profiles/CommonSettings.h"

namespace SpaceGod::Shared
{
	/// <summary>How a settings file load went.</summary>
	enum class SettingsLoadStatus : int
	{
		/// <summary>The file was read.</summary>
		Loaded,

		/// <summary>There was no file; defaults are in use.</summary>
		Missing,

		/// <summary>The file could not be read as settings; it was moved aside (see <see cref="ProfileSettings.MovedAsidePath"/>) and defaults are in use.</summary>
		Damaged,
	};
	SPACEGODCORE_API const char* ToString(SettingsLoadStatus v);

	/// <summary>
	/// One profile's settings.json (format <c>mindattic.settings</c>, version 1):
	/// <c>{"format":"mindattic.settings","version":1,"common":{...},"game":{...}}</c>.
	/// <see cref="Common"/> is the block every MindAttic game shares; <see cref="Game"/> belongs to the game.
	/// Unknown fields anywhere in the file are kept and written back.
	/// </summary>
	class SPACEGODCORE_API ProfileSettings final
	{
	public:
		/// <summary>The value of the file's <c>"format"</c> field.</summary>
		static constexpr const char* Format = "mindattic.settings";

		/// <summary>The format version this code writes.</summary>
		static constexpr int CurrentVersion = 1;

		/// <summary>The settings file's name inside a profile folder.</summary>
		static constexpr const char* FileName = "settings.json";

		/// <summary>A damaged file is renamed to its name plus this plus a UTC timestamp.</summary>
		static constexpr const char* DamagedSuffix = ".bad-";

	private:
		const cs::Func<cs::String> profileName;

	public:
		// Private in C#; public so cs::New can call it.
		ProfileSettings(cs::Ref<Json::JsonObject> root, cs::Func<cs::String> profileName);

		/// <summary>The whole file as JSON, unknown top-level fields included.</summary>
		const cs::Ref<Json::JsonObject> Root;

		/// <summary>The shared block: volumes, mutes, reduce motion, language, display name, colour.</summary>
		const cs::Ref<CommonSettings> Common;

		/// <summary>
		/// The game's own block, free-form. Read and write it with <see cref="JsonObject"/>'s getters and
		/// <see cref="JsonObject.Set"/>, e.g. <c>Game.GetFloat("panSpeed", 0.05f)</c>, <c>Game.Set("panSpeed", 0.1f)</c>.
		/// </summary>
		cs::Ref<Json::JsonObject> Game() const;

		/// <summary>The file's format version (a newer file keeps its number when saved by this code).</summary>
		int Version() const;

		/// <summary>How the last load went.</summary>
		SettingsLoadStatus Status = SettingsLoadStatus::Missing;

		/// <summary>Where a damaged file was moved to, when <see cref="Status"/> is <see cref="SettingsLoadStatus.Damaged"/>.</summary>
		/// (Empty when there is none.)
		cs::String MovedAsidePath;

		/// <summary>Fresh default settings for a profile called <paramref name="profileName"/>.</summary>
		static cs::Ref<ProfileSettings> CreateDefault(const cs::String& profileName = cs::String());

		static cs::Ref<ProfileSettings> CreateDefault(cs::Func<cs::String> profileName); // internal in C#

		/// <summary>Settings read from JSON text; throws when the text is not a settings file.</summary>
		static cs::Ref<ProfileSettings> FromJson(const cs::String& json, cs::Ref<Json::IJsonCodec> codec = nullptr, const cs::String& profileName = cs::String());

		/// <summary>The settings as JSON text.</summary>
		cs::String ToJson(cs::Ref<Json::IJsonCodec> codec = nullptr) const;

		/// <summary>
		/// Reads the settings at <paramref name="path"/>. Never throws for a bad file: a missing one gives
		/// defaults; one that cannot be read or parsed is moved aside to
		/// <c>settings.json.bad-&lt;yyyyMMddTHHmmssfffZ&gt;</c> and defaults are used.
		/// </summary>
		static cs::Ref<ProfileSettings> Load(const cs::String& path, cs::Ref<Json::IJsonCodec> codec = nullptr, const cs::String& profileName = cs::String());

		static cs::Ref<ProfileSettings> Load(const cs::String& path, cs::Ref<Json::IJsonCodec> codec, cs::Func<cs::String> profileName); // internal in C#

		/// <summary>Writes the settings to <paramref name="path"/> atomically (temp file, then replace).</summary>
		void Save(const cs::String& path, cs::Ref<Json::IJsonCodec> codec = nullptr) const;

	private:
		static cs::Ref<ProfileSettings> FromJson(const cs::String& json, cs::Ref<Json::IJsonCodec> codec, cs::Func<cs::String> profileName);

		static cs::String MoveAside(const cs::String& path);
	};
}
