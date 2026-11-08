// UnitySpaceApp.Tutorial.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Tutorial.cpp.
//
// <summary>
// US-T254, the guided tutorial and the first-time hints. A profile's first New Game from the main menu offers the
// tutorial (once: the profile remembers it was offered); the main menu's and the Help screen's Tutorial button start it
// any time. It teaches by doing: each step is a short card with the key it teaches, waits for the player to do it (the
// game's own state says so: Core::Tutorial's rules), and outlines the HUD element involved. It never takes the keys: the
// player plays as ever, and Shift+Enter skips a step, Shift+Esc ends it. Afterwards a short card says what to do the first
// time the player meets a battle, a hyperlane, a jump gate, a void stop and a fight aboard, once each per profile, while
// the Tutorial hints setting (Controls) is on. Local to this machine, like the Help screen; never in a scripted run's own
// play (the audit drives it on purpose).
// </summary>

public:
	/// <summary>Starts the guided tutorial over the game in play (the step cards, from the first).</summary>
	void StartTutorial();

	/// <summary>Ends it part way (the Shift+Esc key, the End button).</summary>
	void StopTutorial();

	/// <summary>The tutorial's progress (the audit reads it); null before one was started this game.</summary>
	const Core::Tutorial* TutorialProgress() const { return tutorial.get(); }

	/// <summary>Whether the welcome card offering the tutorial is up.</summary>
	bool TutorialOffered() const { return tutorialOffer; }

	/// <summary>The audit's stand-in for clicking the NAVIGATOR's title bar (the tutorial's navigator step).</summary>
	void SetNavigatorOpen(bool open);

	/// <summary>The first-time hints, on or off (Settings > Controls); remembered per profile like every setting.</summary>
	static const cs::Ref<Core::ToggleSetting>& TutorialHintsSetting();

private:
	/// <summary>Once a frame, before the commands: the tutorial's own keys (Shift+Enter, Shift+Esc), whose other meanings
	/// (the FTL drive, the Esc menu) are taken out of this frame's input while a card is up.</summary>
	void TutorialKeys();

	/// <summary>Once a frame, after the commands: the offer due at a game's start, the step's action looked for, and the
	/// first-time hints.</summary>
	void UpdateTutorial(float dt);

	/// <summary>The welcome card, the step card and its outline, the done card or a hint, over the HUD.</summary>
	void DrawTutorial();

	/// <summary>The cards' textures go with the app (U.SPC-T366).</summary>
	void DestroyTutorialTextures();

	/// <summary>The game as the tutorial reads it this frame.</summary>
	Core::TutorialSignals TutorialSignalsNow(float dt);

	/// <summary>The first-time hints: whether one may be shown now, and the next one due (-1 none).</summary>
	void UpdateTutorialHints(float dt);
	void DismissTutorialHint();

	cs::Ref<Core::Tutorial> tutorial;
	bool tutorialAsked = false;
	int tutorialAsk = 0, tutorialDueFrames = -1;
	bool tutorialOffer = false;
	/// <summary>Real seconds the "tutorial complete" card has been up (-1: it is not).</summary>
	float tutorialDoneAge = -1;
	Target tutorialLastSelected = Target::None();
	bool tutorialMissileHeld = false;
	/// <summary>The hint on screen (a Core::TutorialHint, -1 none) and how long it has been up; the profile's hints seen.</summary>
	int tutorialHint = -1;
	float tutorialHintAge = 0;
	std::set<cs::String> tutorialHintsSeen;
	bool tutorialHintsLoaded = false;

	bool tutorialStylesReady = false;
	cs::Ref<Texture2D> tutorialPanelTexture, tutorialButtonTexture, tutorialButtonOnTexture, tutorialBarTexture;
	cs::Ref<GUIStyle> tutorialPanel, tutorialTitle, tutorialHeading, tutorialText, tutorialSmall, tutorialSmallRight, tutorialKeys, tutorialButton;
