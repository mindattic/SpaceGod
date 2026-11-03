// Port of Unity.Shared/Runtime/Core/Profiles/CommonSettings.cs.

#include "Shared/Profiles/CommonSettings.h"
#include "Shared/Profiles/Profiles.h"
#include "Shared/Profiles/SharedIo.h"

namespace SpaceGod::Shared
{
	using Json::JsonObject;

	CommonSettings::CommonSettings(cs::Ref<JsonObject> data, cs::Func<cs::String> profileName)
		: profileName(std::move(profileName)), Data(std::move(data))
	{
	}

	float CommonSettings::MusicVolume() const { return Profiles::SessionMuted ? 0.0f : Clamp01(Data->GetFloat(Keys::MusicVolume, DefaultMusicVolume)); }
	void CommonSettings::SetMusicVolume(float value) { Data->Set(Keys::MusicVolume, Clamp01(value)); }

	float CommonSettings::SfxVolume() const { return Profiles::SessionMuted ? 0.0f : Clamp01(Data->GetFloat(Keys::SfxVolume, DefaultSfxVolume)); }
	void CommonSettings::SetSfxVolume(float value) { Data->Set(Keys::SfxVolume, Clamp01(value)); }

	bool CommonSettings::MuteMusic() const { return Profiles::SessionMuted || Data->GetBool(Keys::MuteMusic, false); }
	void CommonSettings::SetMuteMusic(bool value) { Data->Set(Keys::MuteMusic, value); }

	bool CommonSettings::MuteSfx() const { return Profiles::SessionMuted || Data->GetBool(Keys::MuteSfx, false); }
	void CommonSettings::SetMuteSfx(bool value) { Data->Set(Keys::MuteSfx, value); }

	bool CommonSettings::ReduceMotion() const { return Data->GetBool(Keys::ReduceMotion, false); }
	void CommonSettings::SetReduceMotion(bool value) { Data->Set(Keys::ReduceMotion, value); }

	cs::String CommonSettings::Language() const
	{
		auto s = Data->GetString(Keys::Language);
		return cs::Strings::IsNullOrWhiteSpace(s) ? cs::String(DefaultLanguage) : cs::Strings::Trim(s);
	}

	void CommonSettings::SetLanguage(const cs::String& value)
	{
		if (cs::Strings::IsNullOrWhiteSpace(value)) Data->Remove(Keys::Language);
		else Data->Set(Keys::Language, cs::Strings::Trim(value));
	}

	cs::String CommonSettings::DisplayName() const
	{
		auto s = Data->GetString(Keys::DisplayName);
		return cs::Strings::IsNullOrWhiteSpace(s) ? FallbackName() : cs::Strings::Trim(s);
	}

	void CommonSettings::SetDisplayName(const cs::String& valueIn)
	{
		if (cs::Strings::IsNullOrWhiteSpace(valueIn)) { Data->Remove(Keys::DisplayName); return; }
		cs::String value = cs::Strings::Trim(valueIn);
		if (Io::Utf16Length(value) > MaxDisplayNameLength)
		{
			// Don't cut between the halves of a surrogate pair (an emoji): a lone half is written as U+FFFD.
			// (Utf16Prefix never splits a code point, which is the same cut.)
			value = cs::Strings::TrimEnd(Io::Utf16Prefix(value, MaxDisplayNameLength));
		}
		Data->Set(Keys::DisplayName, value);
	}

	bool CommonSettings::HasDisplayName() const { return !cs::Strings::IsNullOrWhiteSpace(Data->GetString(Keys::DisplayName)); }

	RgbColor CommonSettings::Color() const
	{
		RgbColor c;
		return RgbColor::TryParse(Data->GetString(Keys::Color), c) ? c : RgbColor::ForName(FallbackName());
	}

	void CommonSettings::SetColor(const RgbColor& value) { Data->Set(Keys::Color, value.ToHex()); }

	bool CommonSettings::HasColor() const
	{
		RgbColor unused;
		return RgbColor::TryParse(Data->GetString(Keys::Color), unused);
	}

	void CommonSettings::ClearColor() { Data->Remove(Keys::Color); }

	cs::String CommonSettings::FallbackName() const
	{
		cs::String n = profileName ? profileName() : cs::String();
		return cs::Strings::IsNullOrWhiteSpace(n) ? cs::String("Player") : n;
	}

	void CommonSettings::FillDefaults()
	{
		if (!(*Data)[Keys::MusicVolume].IsNumber()) SetMusicVolume(DefaultMusicVolume);
		if (!(*Data)[Keys::SfxVolume].IsNumber()) SetSfxVolume(DefaultSfxVolume);
		if (!(*Data)[Keys::MuteMusic].IsBool()) SetMuteMusic(false);
		if (!(*Data)[Keys::MuteSfx].IsBool()) SetMuteSfx(false);
		if (!(*Data)[Keys::ReduceMotion].IsBool()) SetReduceMotion(false);
		if (!(*Data)[Keys::Language].IsString()) SetLanguage(DefaultLanguage);
	}

	float CommonSettings::Clamp01(float v) { return cs::Single::IsNaN(v) ? 0.0f : cs::Math::Max(0.0f, cs::Math::Min(1.0f, v)); }
}
