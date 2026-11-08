#pragma once

namespace SpaceGod { class UnitySpaceApp; }

namespace SpaceGod::Audio
{
	/// The interface's feedback sounds (US-T244), all on the Interface bus (so Settings > Audio > Interface volume sets
	/// them) and all local to this machine; silent wherever GameAudio is (no device, -muted, capture and other scripted
	/// runs unless -GameAudio).
	///
	/// - Every GUI::Button and GUI::Toggle on every screen (the main menu, pause menu, HUD, boards, galaxy map, dev menu;
	///   tabs are buttons too) hovers and presses through the GUI's control-sound hook, which this installs at start-up.
	/// - Poll, once a frame after the app's Update (the actor's Step, on every co-op peer), hears what changed: a panel
	///   opened or closed, a toast, a news report, an incoming hail, a comms line, a contract taken, completed or failed,
	///   credits gained or spent.
	/// - SaveDone is called where a save finishes (UnitySpaceApp::SaveTo and a toasted autosave).
	namespace InterfaceSounds
	{
		void Poll(UnitySpaceApp* app);
		void SaveDone();
	}
}
