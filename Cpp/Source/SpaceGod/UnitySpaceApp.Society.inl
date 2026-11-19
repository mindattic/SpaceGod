// UnitySpaceApp.Society.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Society.cpp.
//
// <summary>
// US-T293 (the Civilization epic's P10): the galaxy's people as the player meets them. The Society screen (Shift+G,
// UI/SocietyScreen) reads the Civilization layer (Core's Systems/Civilization) and is local to this machine, like the Help
// screen; the SpaceGod's hand it offers goes through <see cref="CivHandAsk"/>, the same road sg.Civ's verbs take, so a click and
// the console do exactly the same thing, and in co-op both go to every peer as a "civ" entry carried out on the same frame.
// </summary>

public:
	/// <summary>The Society screen (Shift+G). Esc closes it.</summary>
	bool ShowSociety = false;

	/// <summary>
	/// The SpaceGod's hand in the galaxy's people: <paramref name="line"/> is an sg.Civ hand verb and its words ("feud 12 40",
	/// "riot New Shanghai", "slant 7 against"). In single player it is applied now and its outcome toasted; in co-op it goes
	/// into the frames as a "civ" entry (CivHandCommands.cpp) and every peer applies the same call on the frame it lands on.
	/// Returns why it could not be done ("" when it was done or sent); <paramref name="sent"/> says it went to the party, and
	/// <paramref name="done"/> is the line said when it was done here.
	/// </summary>
	cs::String CivHandAsk(const cs::String& line, bool& sent, cs::String& done);

	/// <summary>Draws the Society screen over the whole view (the Hud calls it while <see cref="ShowSociety"/>).</summary>
	void DrawSociety();

	/// <summary>The screen's own textures go with the Hud's (U.SPC-T366).</summary>
	void DestroySocietyTextures();

	/// <summary>Before the commands each frame: while the screen's search is being typed in it has the keys (true: the owner
	/// hands the input over; US-T298).</summary>
	bool UpdateSocietyKeys();

private:
	cs::Ref<UI::SocietyScreen> societyScreen;
	/// <summary>A "civ" entry carried out on this peer (UnitySpaceApp.Coop.cpp): the asker's line, applied here.</summary>
	void CoopCivHand(Coop::CoopPeer& peer, int seat, const cs::String& line);
