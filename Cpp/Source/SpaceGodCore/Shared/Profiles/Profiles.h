#pragma once
// Port of Unity.Shared/Runtime/Core/Profiles/Profiles.cs.

#include "Cs/Cs.h"
#include "Shared/Json/MiniJson.h"
#include "Shared/Profiles/ProfileStore.h"

namespace SpaceGod::Shared
{
	/// <summary>
	/// The one-line way in: <c>Profiles.Current.Settings.Common.DisplayName</c>. In Unity the package's runtime
	/// assembly points this at <c>Application.persistentDataPath</c> before the first scene loads; anywhere else
	/// (dotnet tests, tools) call <see cref="UseRoot"/> or <see cref="UseDataPath"/> first, or set the
	/// <see cref="RootEnvironmentVariable"/> environment variable.
	/// </summary>
	class SPACEGODCORE_API Profiles
	{
	public:
		/// <summary>An environment variable naming a Profiles folder to use instead of the data path's (tests, automation, CI).</summary>
		static constexpr const char* RootEnvironmentVariable = "MINDATTIC_PROFILES_ROOT";

		/// <summary>A command-line switch naming a Profiles folder, as <c>-profiles-root &lt;path&gt;</c> or <c>-profiles-root=&lt;path&gt;</c>.</summary>
		static constexpr const char* RootCommandLineSwitch = "-profiles-root";

		/// <summary>An environment variable that, set to <c>1</c> (or <c>true</c>/<c>yes</c>/<c>on</c>), mutes all audio for this session (see <see cref="SessionMuted"/>).</summary>
		static constexpr const char* MutedEnvironmentVariable = "MINDATTIC_CLI_MUTED";

		/// <summary>A command-line switch that mutes all audio for this session, as <c>-muted</c> (see <see cref="SessionMuted"/>).</summary>
		static constexpr const char* MutedCommandLineSwitch = "-muted";

		/// <summary>
		/// A session-only audio mute: while true, every profile's <see cref="CommonSettings"/> reports music and
		/// sound effects muted at volume 0, but the stored values are untouched, so nothing reaches settings.json
		/// (saving writes the player's real values). The Unity bootstrap turns it on from
		/// <see cref="MutedEnvironmentVariable"/> or <see cref="MutedCommandLineSwitch"/> before the first scene
		/// loads, so a CLI or agent launching a game does not play sound.
		/// </summary>
		static bool SessionMuted; // { get; set; }

		/// <summary>
		/// C++ only: Environment.GetCommandLineArgs(). Standard C++ cannot read the process's command line, so
		/// the host sets this at startup (the Bench from main's argv; the UE game from FCommandLine), element 0
		/// being the program, as in .NET. Left empty, the command-line overrides simply never match.
		/// </summary>
		static cs::List<cs::String> CommandLineArgs;

		/// <summary>Whether the environment variable or command line asks for a muted session.</summary>
		static bool MutedOverride();

		/// <summary>Turns <see cref="SessionMuted"/> on if <see cref="MutedOverride"/> asks for it (never turns it off). Returns <see cref="SessionMuted"/>.</summary>
		static bool ApplyMutedOverride();

		/// <summary>Whether a store has been set (by the Unity bootstrap, <see cref="UseRoot"/>, <see cref="UseDataPath"/> or the override).</summary>
		static bool IsConfigured();

		/// <summary>
		/// The store in use. If none was set but the environment variable or command-line override names a
		/// folder, that is used; otherwise this throws <see cref="InvalidOperationException"/>.
		/// </summary>
		static cs::Ref<ProfileStore> Store();

		/// <summary>The profile in use (see <see cref="ProfileStore.Current"/>).</summary>
		static cs::Ref<Profile> Current();

		/// <summary>
		/// Uses <c>&lt;dataPath&gt;/Profiles</c>, unless the environment variable or command-line override names
		/// another folder, which wins. Returns the store.
		/// </summary>
		static cs::Ref<ProfileStore> UseDataPath(const cs::String& dataPath, cs::Ref<Json::IJsonCodec> codec = nullptr);

		/// <summary>Uses exactly <paramref name="profilesRoot"/> as the Profiles folder (no override applies). Returns the store.</summary>
		static cs::Ref<ProfileStore> UseRoot(const cs::String& profilesRoot, cs::Ref<Json::IJsonCodec> codec = nullptr);

		/// <summary>Uses a store you built. Returns it.</summary>
		static cs::Ref<ProfileStore> Use(cs::Ref<ProfileStore> profileStore);

		/// <summary>Forgets the store, so the next use needs one set again (tests).</summary>
		static void Reset();

		/// <summary>The Profiles folder the environment variable or command line names, or null.</summary>
		/// (Empty for none.)
		static cs::String OverrideRoot();

	private:
		static cs::Ref<ProfileStore> store;
	};
}
