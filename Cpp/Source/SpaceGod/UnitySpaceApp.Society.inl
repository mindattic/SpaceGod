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

	/// <summary>Before the commands each frame: tells the news about what is watched (<see cref="UpdateSocietyWatch"/>), and
	/// while the screen's search is being typed in it has the keys (true: the owner hands the input over; US-T298).</summary>
	bool UpdateSocietyKeys();

	/// <summary>US-T304: the houses, people, organisations, businesses, worlds and states the player watches, local to this
	/// machine and this galaxy (forgotten when another galaxy's people take over, as on a load or a new game).</summary>
	std::vector<CivPick> SocietyWatched;
	bool IsWatched(const CivPick& p) const;
	void ToggleWatch(const CivPick& p);

	/// <summary>US-T308: one world's mood on one day, sampled every <see cref="SocietyTrendDays"/> game days while the game runs
	/// (local to this machine and this galaxy, never saved: a load or a new game starts the trends over).</summary>
	struct SocietyTrendSample
	{
		double Jd = 0, Approval = 0, Unrest = 0, Crime = 0, Infected = 0, Weariness = 0;
	};
	static constexpr double SocietyTrendDays = 30;
	static constexpr int SocietyTrendMax = 24;
	/// <summary>The samples of the world called <paramref name="world"/>, oldest first (null: none taken yet).</summary>
	const std::vector<SocietyTrendSample>* SocietyTrendOf(const cs::String& world) const;

private:
	cs::Ref<UI::SocietyScreen> societyScreen;
	/// <summary>US-T304: the civilization the watch list is about, the last event looked at, and the toast held back meanwhile
	/// (the newest watched event, and how many more came with it) so a fast warp does not flicker one over another.</summary>
	const void* watchCiv = nullptr;
	int watchSeenId = 0, watchHeld = 0;
	cs::String watchToast;
	double watchToastAt = -1e9;
	/// <summary>US-T304: once a frame, any new event about something watched becomes a toast (at most one every few seconds).</summary>
	void UpdateSocietyWatch();
	/// <summary>US-T308: the worlds' trends, the civilization they are about, and the day the next sample is due.</summary>
	std::map<cs::String, std::vector<SocietyTrendSample>> societyTrends;
	const void* trendCiv = nullptr;
	double trendNextJd = -1e18;
	/// <summary>US-T308: once a frame, every world's mood is sampled when a sample is due (and the future dropped after a rewind).</summary>
	void UpdateSocietyTrends();
	/// <summary>A "civ" entry carried out on this peer (UnitySpaceApp.Coop.cpp): the asker's line, applied here.</summary>
	void CoopCivHand(Coop::CoopPeer& peer, int seat, const cs::String& line);
