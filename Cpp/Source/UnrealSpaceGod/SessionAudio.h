#pragma once

namespace SpaceGod
{
	/// The CLI mute (U.SPC-T536): launched with <c>-muted</c> or <c>MINDATTIC_CLI_MUTED=1</c>, Unity.Shared's bootstrap
	/// turns on Profiles.SessionMuted before the first scene loads. Everything this game plays goes through
	/// the one AudioListener, whose volume the profile's master volume setting
	/// (EngineSettings::MasterVolume) would otherwise put back whenever a profile is loaded, so that setting
	/// asks ListenerVolume here. The mute lasts this run only: AudioListener.volume is never saved and the
	/// stored master volume is left as the player set it.
	class SessionAudio
	{
	public:
		/// Whether this run is muted by the CLI mute.
		static bool Muted();

		/// [RuntimeInitializeOnLoadMethod(BeforeSceneLoad)]: called by the host actor before the app wakes.
		static void OnLoad();

		/// The listener volume, 0-1, for a master volume of <paramref name="percent"/>: 0 while the session is muted.
		static float ListenerVolume(double percent);
	};
}
