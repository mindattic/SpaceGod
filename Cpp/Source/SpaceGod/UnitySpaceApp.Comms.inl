// UnitySpaceApp.Comms.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Comms.cpp.
//
// US-T137: the comms panel. Shift+H opens it at any controls: pick who to hail (every hull the held unit's sensors hold in
// hailing range, and the stations near it), then what to say (hail, ask about the area, the news, to trade, buy a crate,
// ask about them, threaten), and read the answer. A hull that strikes its colours to the held unit's side hails on its own
// and is answered here (accept, plunder, let go, no quarter). Everything is said by the unit held (Core::CommsContext's
// speaker); the answers are Core::Comms's, from the simulation as it stands. While open the panel has the keys and the
// mouse (the cheat menu's pattern, US-T126) and the craft holds its heading.
//
// US-T138: the named pilots. Every frame the unit held is watched: taking a hull meets its pilot (Characters::Embodied),
// leaving it hands the pilot every act done meanwhile as their own (Characters::Released, the rationalization rule), a
// fight the held hull comes out of is one they survived, and turning on one's own side in it is a betrayal.

public:
	/// <summary>The comms panel is open (it has the keys and the mouse).</summary>
	bool CommsOpen() const { return commsOpen; }

	/// <summary>Opens the panel (on <paramref name="target"/> when it is given and in range), or says why it cannot.</summary>
	void OpenComms(const Core::CommsParty& target = Core::CommsParty());
	void CloseComms();

	/// <summary>Who is speaking now and from where: the unit held, its hull, faction and pose, the scale's ruler.</summary>
	Core::CommsContext CommsHere();

	/// <summary>Says <paramref name="verb"/> to the panel's target, as the unit held, and logs the answer (the smoke's way in too).</summary>
	void CommsSay(Core::CommsVerb verb);

	const cs::List<Core::CommsLine>& CommsLog() const { return commsLog; }

	/// <summary>sg.Comms's showcase (CommsCommand.cpp), one step a call, true when the step is done: 0 takes the helm of the
	/// Sol-side hull with the most to hail near it, 1 opens the panel on its nearest hull and hails it, asks about the area and
	/// about the pilot. False with <paramref name="failed"/> set when there is nothing to show.</summary>
	bool CommsShowcase(int step, cs::String& failed);

private:
	bool commsOpen = false;
	cs::List<Core::CommsParty> commsContacts;
	Core::CommsParty commsTarget;
	cs::List<Core::CommsLine> commsLog;
	/// <summary>The row picked in the contacts list, and whether the verbs (true) or the contacts have the keys.</summary>
	int commsRow = 0;
	bool commsTalking = false;
	int commsClickedContact = -1, commsClickedVerb = -1;
	float commsRefresh = 0;

	/// <summary>What was held last frame, the fight it was in, and the surrender hail already announced.</summary>
	cs::Ref<Core::Ship> commsHeldShip;
	cs::Ref<Core::Battle> commsHeldBattle;
	bool commsBetrayalNoted = false;
	cs::Ref<Core::BattleUnit> commsHailNoted;

	bool commsStylesReady = false;
	cs::Ref<GUIStyle> commsPanel, commsTitle, commsText, commsSmall, commsRowStyle, commsRowPicked, commsButton, commsThem, commsUs, commsHint;
	cs::Ref<Texture2D> commsPanelTex, commsRowTex, commsRowPickedTex, commsButtonTex, commsButtonHotTex, commsHintTex;

	/// <summary>Once a frame, after the dev menu: Shift+H opens or closes the panel; open, it has the keys (true).</summary>
	bool UpdateComms();

	/// <summary>The held unit's pilot, watched every frame (US-T138): met, held, released, battles and betrayals.</summary>
	void TrackEmbodiedPilot();

	void DrawComms();
	void EnsureCommsStyles();
	void DestroyCommsTextures();

	/// <summary>A contact as the held unit's sensors make it out ("the Kestrel", "unidentified destroyer", "unknown contact").</summary>
	cs::String CommsLabel(const Core::CommsContext& c, const Core::CommsParty& p);
