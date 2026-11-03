// Port of Unity.Shared/Runtime/Core/Profiles/Profiles.cs.

#include "Shared/Profiles/Profiles.h"
#include "Shared/Profiles/SharedIo.h"

namespace SpaceGod::Shared
{
	using Json::IJsonCodec;

	cs::Ref<ProfileStore> Profiles::store;
	bool Profiles::SessionMuted = false;
	cs::List<cs::String> Profiles::CommandLineArgs;

	bool Profiles::MutedOverride()
	{
		cs::String env = Io::EnvironmentVariable(MutedEnvironmentVariable);
		if (!cs::Strings::IsNullOrWhiteSpace(env))
		{
			cs::String v = cs::Strings::ToLowerInvariant(cs::Strings::Trim(env));
			if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
		}
		for (const cs::String& a : CommandLineArgs)
			if (cs::Strings::EqualsOrdinalIgnoreCase(a, MutedCommandLineSwitch)) return true;
		return false;
	}

	bool Profiles::ApplyMutedOverride()
	{
		if (MutedOverride()) SessionMuted = true;
		return SessionMuted;
	}

	bool Profiles::IsConfigured() { return store != nullptr; }

	cs::Ref<ProfileStore> Profiles::Store()
	{
		if (store) return store;
		cs::String overrideRoot = OverrideRoot();
		if (!overrideRoot.empty()) return store = cs::New<ProfileStore>(overrideRoot);
		throw cs::InvalidOperationException(cs::Cat(
			"Profiles has no folder yet. In Unity the Unity.Shared assembly sets it; elsewhere call Profiles.UseRoot(path) or Profiles.UseDataPath(path), or set ", RootEnvironmentVariable, "."));
	}

	cs::Ref<Profile> Profiles::Current() { return Store()->Current(); }

	cs::Ref<ProfileStore> Profiles::UseDataPath(const cs::String& dataPath, cs::Ref<IJsonCodec> codec)
	{
		cs::String overrideRoot = OverrideRoot();
		store = !overrideRoot.empty() ? cs::New<ProfileStore>(overrideRoot, codec) : ProfileStore::ForDataPath(dataPath, codec);
		return store;
	}

	cs::Ref<ProfileStore> Profiles::UseRoot(const cs::String& profilesRoot, cs::Ref<IJsonCodec> codec) { return store = cs::New<ProfileStore>(profilesRoot, std::move(codec)); }

	cs::Ref<ProfileStore> Profiles::Use(cs::Ref<ProfileStore> profileStore)
	{
		if (!profileStore) throw cs::ArgumentNullException("profileStore");
		return store = std::move(profileStore);
	}

	void Profiles::Reset() { store = nullptr; }

	cs::String Profiles::OverrideRoot()
	{
		cs::String env = Io::EnvironmentVariable(RootEnvironmentVariable);
		if (!cs::Strings::IsNullOrWhiteSpace(env)) return cs::Strings::Trim(env);
		const cs::List<cs::String>& args = CommandLineArgs;
		const cs::String prefix = cs::Cat(RootCommandLineSwitch, "=");
		for (int i = 0; i < args.Count(); i++)
		{
			const cs::String& a = args[i];
			cs::String value;
			if (cs::Strings::EqualsOrdinalIgnoreCase(a, RootCommandLineSwitch) && i + 1 < args.Count())
				value = args[i + 1];
			else if (a.size() >= prefix.size() && cs::Strings::EqualsOrdinalIgnoreCase(a.substr(0, prefix.size()), prefix))
				value = cs::Strings::Substring(a, static_cast<int>(prefix.size()));
			// A blank value ("-profiles-root=") is no override, as for the environment variable; passing it
			// on would make ProfileStore throw from the Unity bootstrap.
			if (!cs::Strings::IsNullOrWhiteSpace(value)) return cs::Strings::Trim(value);
		}
		return cs::String();
	}
}
