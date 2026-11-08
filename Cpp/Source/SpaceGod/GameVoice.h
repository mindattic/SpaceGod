#pragma once
// US-T139: the BYOKey voice in the game (Core's StoryVoice over the player's own LLM key).

namespace SpaceGod
{
	/// <summary>
	/// Reads the player's own key and persona and, when a key is there, installs the LLM voice (Core::StoryVoice) so named
	/// pilots' stories and stranded ships' calls are reworded by the player's model; with no key nothing changes and the
	/// pregenerated pools speak. Called once at start (UnitySpaceApp::Awake); sg.Voice shows and changes it.
	///
	/// Where it looks, first found wins:
	///   key      SPACEGOD_LLM_KEY, else Saved/Voice/api-key.txt
	///   persona  the file named by SPACEGOD_PERSONA, else Saved/Voice/persona.txt (optional first line "name: ...")
	///   model    SPACEGOD_LLM_MODEL, else Saved/Voice/model.txt, else claude-opus-5-5
	///   endpoint SPACEGOD_LLM_URL, else the Anthropic Messages API
	/// The key never leaves this machine except to that endpoint, and is never logged or saved by the game.
	/// </summary>
	void StartGameVoice();
}
