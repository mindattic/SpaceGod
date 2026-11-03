// Port of Unity.Shared/Runtime/Core/Profiles/ProfileSettings.cs.

#include "Shared/Profiles/ProfileSettings.h"
#include "Shared/Profiles/AtomicFile.h"
#include "Shared/Profiles/SharedIo.h"

#include <new>

namespace SpaceGod::Shared
{
	using Json::IJsonCodec;
	using Json::JsonFormatException;
	using Json::JsonObject;
	using Json::MiniJsonCodec;

	const char* ToString(SettingsLoadStatus v)
	{
		switch (v)
		{
		case SettingsLoadStatus::Loaded: return "Loaded";
		case SettingsLoadStatus::Missing: return "Missing";
		case SettingsLoadStatus::Damaged: return "Damaged";
		}
		return "?";
	}

	namespace
	{
		// The first lines of the C# constructor, which run before Common is made from the "common" block.
		const cs::Ref<JsonObject>& Prepare(const cs::Ref<JsonObject>& root)
		{
			if (!(*root)["format"].IsString()) root->Set("format", ProfileSettings::Format);
			if (!(*root)["version"].IsNumber()) root->Set("version", ProfileSettings::CurrentVersion);
			if (!(*root)["common"].IsObject()) root->Set("common", cs::New<JsonObject>());
			if (!(*root)["game"].IsObject()) root->Set("game", cs::New<JsonObject>());
			return root;
		}

		cs::Ref<IJsonCodec> OrDefault(const cs::Ref<IJsonCodec>& codec) { return codec ? codec : cs::Ref<IJsonCodec>(MiniJsonCodec::Instance()); }
	}

	ProfileSettings::ProfileSettings(cs::Ref<JsonObject> root, cs::Func<cs::String> profileNameIn)
		: profileName(std::move(profileNameIn)),
		  Root(Prepare(root)),
		  Common(cs::New<CommonSettings>(Root->GetObject("common"), profileName))
	{
		Common->FillDefaults();
	}

	cs::Ref<JsonObject> ProfileSettings::Game() const { return Root->GetOrAddObject("game"); }

	int ProfileSettings::Version() const { return Root->GetInt("version", CurrentVersion); }

	cs::Ref<ProfileSettings> ProfileSettings::CreateDefault(const cs::String& profileName)
	{
		return cs::New<ProfileSettings>(cs::New<JsonObject>(), [profileName]() { return profileName; });
	}

	cs::Ref<ProfileSettings> ProfileSettings::CreateDefault(cs::Func<cs::String> profileName)
	{
		return cs::New<ProfileSettings>(cs::New<JsonObject>(), std::move(profileName));
	}

	cs::Ref<ProfileSettings> ProfileSettings::FromJson(const cs::String& json, cs::Ref<IJsonCodec> codec, const cs::String& profileName)
	{
		return FromJson(json, std::move(codec), cs::Func<cs::String>([profileName]() { return profileName; }));
	}

	cs::Ref<ProfileSettings> ProfileSettings::FromJson(const cs::String& json, cs::Ref<IJsonCodec> codec, cs::Func<cs::String> profileName)
	{
		auto root = OrDefault(codec)->Parse(json);
		if (!root) throw JsonFormatException("The codec returned no object.", 0);
		auto format = (*root)["format"];
		auto s = format.AsString();
		if (!format.IsNull() && !(s && *s == Format))
			throw JsonFormatException(cs::Cat("Not a ", Format, " file (format is ", format.ToString(), ")."), 0);
		return cs::New<ProfileSettings>(root, std::move(profileName));
	}

	cs::String ProfileSettings::ToJson(cs::Ref<IJsonCodec> codec) const { return OrDefault(codec)->Write(Root); }

	cs::Ref<ProfileSettings> ProfileSettings::Load(const cs::String& path, cs::Ref<IJsonCodec> codec, const cs::String& profileName)
	{
		return Load(path, std::move(codec), cs::Func<cs::String>([profileName]() { return profileName; }));
	}

	cs::Ref<ProfileSettings> ProfileSettings::Load(const cs::String& path, cs::Ref<IJsonCodec> codec, cs::Func<cs::String> profileName)
	{
		if (!Io::FileExists(path)) return CreateDefault(profileName);
		try
		{
			auto settings = FromJson(Io::ReadAllText(path), codec, profileName);
			settings->Status = SettingsLoadStatus::Loaded;
			return settings;
		}
		catch (const std::bad_alloc&)
		{
			throw; // catch (Exception e) when (!(e is OutOfMemoryException))
		}
		catch (...)
		{
			auto fresh = CreateDefault(profileName);
			fresh->Status = SettingsLoadStatus::Damaged;
			fresh->MovedAsidePath = MoveAside(path);
			return fresh;
		}
	}

	void ProfileSettings::Save(const cs::String& path, cs::Ref<IJsonCodec> codec) const { AtomicFile::WriteAllText(path, ToJson(std::move(codec))); }

	cs::String ProfileSettings::MoveAside(const cs::String& path)
	{
		try
		{
			cs::String stamp = cs::DateTime::UtcNow().ToString("yyyyMMdd'T'HHmmssfff'Z'");
			cs::String target = path + DamagedSuffix + stamp;
			for (int i = 2; Io::FileExists(target); i++) target = cs::Cat(path, DamagedSuffix, stamp, "-", i);
			Io::MoveFileTo(path, target);
			return target;
		}
		catch (const cs::IOException&)
		{
			return cs::String(); // could not move it; defaults are still used and the next save replaces it
		}
	}
}
