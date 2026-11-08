// UnitySpaceApp.Comms.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Comms.cpp.
//
// US-T137: the comms panel. Shift+H opens it at any controls: pick who to hail (every hull the held unit's sensors hold in
// hailing range, and the stations near it), then what to say (hail, ask about the area, the news, to trade, buy a crate,
// ask about them, threaten), and read the answer. A hull that strikes its colours to the held unit's side hails on its own
// and is answered here (accept, plunder, let go, no quarter). Everything is said by the unit held (Core::CommsContext's
// speaker); the answers are Core::Comms's, from the simulation as it stands. While open the panel has the keys and the
// mouse (the cheat menu's pattern, US-T126) and the craft holds its heading.
//
// US-T201: hailing as a conversation. HailShip opens the panel straight on one hull's channel and says the hail (Shift+H does
// it for the hull locked or selected, US-T200's ships list for a clicked row), and the options now include asking about
// rumours, each voice telling its own trade (see Core::Comms::Rumours): who answers, and how, is their faction, role and
// stance toward the unit held.
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

	/// <summary>
	/// US-T201's entry point: hail <paramref name="ship"/> and open the conversation with it (the comms panel on that channel,
	/// the hail said, its options offered: rumours, the area, news, trade, threaten...). What a "ships in the area" list
	/// (US-T200) calls when a row is clicked; Core::Comms::CanHail tells it beforehand which rows can be. False, with a toast
	/// saying why, when it cannot be hailed (out of range, off the sensors, no unit held). Shift+H hails the hull locked at
	/// the helm or selected the same way, when it can be hailed.
	/// </summary>
	bool HailShip(const cs::Ref<Core::Ship>& ship);

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

	/// <summary>US-T237: the Prose speaker id of a party: a station's settlement, a hull's named pilot once met; "" for none.</summary>
	cs::String CommsLoreSpeaker(const Core::CommsParty& p);

	/// <summary>US-T237: a hail's answer as the panel shows it: the speaker's (or the hull's) approved hail bark, else <paramref name="generated"/>.</summary>
	cs::String HailBark(const Core::CommsParty& p, const cs::String& generated);
