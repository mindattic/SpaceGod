#pragma once
// Port of Unity.Shared/Runtime/Core/Profiles/CommonSettings.cs.

#include "Cs/Cs.h"
#include "Shared/Json/JsonObject.h"
#include "Shared/Profiles/RgbColor.h"

namespace SpaceGod::Shared
{
	/// <summary>
	/// The <c>"common"</c> block of settings.json: what every MindAttic game reads the same way. Each property
	/// reads straight from, and writes straight into, the underlying <see cref="Data"/> object, so fields this
	/// version does not know survive a load and save. A missing or wrong-typed field reads as its default.
	/// Change values, then call <see cref="Profile.SaveSettings"/> to write them.
	/// </summary>
	class SPACEGODCORE_API CommonSettings final
	{
	public:
		/// <summary>JSON field names, as written in settings.json.</summary>
		class Keys
		{
		public:
			static constexpr const char* MusicVolume = "musicVolume";
			static constexpr const char* SfxVolume = "sfxVolume";
			static constexpr const char* MuteMusic = "muteMusic";
			static constexpr const char* MuteSfx = "muteSfx";
			static constexpr const char* ReduceMotion = "reduceMotion";
			static constexpr const char* Language = "language";
			static constexpr const char* DisplayName = "displayName";
			static constexpr const char* Color = "color";
		};

		/// <summary>Music volume before the player changes it (GridGame2026's default).</summary>
		static constexpr float DefaultMusicVolume = 0.6f;

		/// <summary>Sound-effect volume before the player changes it (GridGame2026's default).</summary>
		static constexpr float DefaultSfxVolume = 0.85f;

		/// <summary>Language before the player changes it: an IETF tag.</summary>
		static constexpr const char* DefaultLanguage = "en";

		/// <summary>The longest display name kept; a longer one is cut to this.</summary>
		static constexpr int MaxDisplayNameLength = 32;

	private:
		const cs::Func<cs::String> profileName;

	public:
		// internal in C#.
		CommonSettings(cs::Ref<Json::JsonObject> data, cs::Func<cs::String> profileName);

		/// <summary>The raw <c>"common"</c> object, unknown fields and all.</summary>
		const cs::Ref<Json::JsonObject> Data;

		/// <summary>
		/// Music volume, 0-1 (stored values outside that are clamped on read and write). Reads 0 while
		/// <see cref="Profiles.SessionMuted"/> is on; the stored value is kept and still saved.
		/// </summary>
		float MusicVolume() const;
		void SetMusicVolume(float value);

		/// <summary>Sound-effect volume, 0-1. Reads 0 while <see cref="Profiles.SessionMuted"/> is on.</summary>
		float SfxVolume() const;
		void SetSfxVolume(float value);

		/// <summary>Music muted. Reads true while <see cref="Profiles.SessionMuted"/> is on.</summary>
		bool MuteMusic() const;
		void SetMuteMusic(bool value);

		/// <summary>Sound effects muted. Reads true while <see cref="Profiles.SessionMuted"/> is on.</summary>
		bool MuteSfx() const;
		void SetMuteSfx(bool value);

		/// <summary>The player asked for less motion (no screen shake, softer transitions).</summary>
		bool ReduceMotion() const;
		void SetReduceMotion(bool value);

		/// <summary>UI language as an IETF tag (<c>"en"</c>, <c>"fr-CA"</c>); null or blank resets it to <see cref="DefaultLanguage"/>.</summary>
		cs::String Language() const;
		void SetLanguage(const cs::String& value);

		/// <summary>
		/// The name other players see (multiplayer lobbies, scoreboards). Until the player sets one it is the
		/// profile's name, and it follows a profile rename. Setting null or blank clears it back to that;
		/// anything else is trimmed and cut to <see cref="MaxDisplayNameLength"/> characters.
		/// </summary>
		cs::String DisplayName() const;
		void SetDisplayName(const cs::String& value);

		/// <summary>Whether the player chose a <see cref="DisplayName"/> (false when it is the profile name by default).</summary>
		bool HasDisplayName() const;

		/// <summary>
		/// The player's colour (cursor, avatar tint, lobby badge). Until the player picks one it is a
		/// <see cref="RgbColor.Palette"/> colour chosen from the profile name, the same on every run.
		/// Stored as <c>"#RRGGBB"</c>. In Unity: <c>Common.GetUnityColor()</c> / <c>Common.SetUnityColor(c)</c>.
		/// </summary>
		RgbColor Color() const;
		void SetColor(const RgbColor& value);

		/// <summary>Whether the player chose a <see cref="Color"/>.</summary>
		bool HasColor() const;

		/// <summary>Forgets the chosen colour, back to the one picked from the profile name.</summary>
		void ClearColor();

		/// <summary>Writes every standard field that is missing, with its default (displayName and color stay optional).</summary>
		void FillDefaults(); // internal in C#

	private:
		cs::String FallbackName() const;

		static float Clamp01(float v);
	};
}
