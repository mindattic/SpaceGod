// UnitySpaceApp.Encyclopedia.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Encyclopedia.cpp.
//
// <summary>
// US-T271, the Encyclopedia as the player meets it: every article in one place (the controls as bound now, the manual's
// guides, the Prose lore, and everything the player has learned of), unlocked as the game goes on and searched with an
// open-text filter. The game sweeps what the player has come to know into the Galaxy's knowledge (Core::EncyclopediaKnowledge,
// saved) whenever the discovery log is swept; the screen (Shift+I, UI/EncyclopediaScreen) is local to this machine, like
// the Help screen, and reads the Encyclopedia built here (Core::Encyclopedia), rebuilt when what it is built from moves on.
// </summary>

public:
	/// <summary>The Encyclopedia screen (Shift+I). Esc closes it.</summary>
	bool ShowEncyclopedia = false;

	/// <summary>The Galaxy's knowledge (the Encyclopedia's unlocks), made the first time it is asked for.</summary>
	Core::EncyclopediaKnowledge& Knowledge();

	/// <summary>Unlocks what the player has come to know (Core::EncyclopediaKnowledge::Sweep) when the discovery log or the
	/// books recovered have moved on, else every few seconds.</summary>
	void UpdateKnowledge();

	/// <summary>The Encyclopedia as it stands, rebuilt when the log, the knowledge or the books recovered have moved on, or the
	/// screen has just been opened (so a rebind shows).</summary>
	const Core::Encyclopedia& EncyclopediaBook();

	/// <summary>Counts the rebuilds of <see cref="EncyclopediaBook"/>, so a view of it knows to refresh.</summary>
	int EncyclopediaBuilds() const { return encyclopediaBuilds; }

	/// <summary>
	/// US-T272's entry point: opens the Encyclopedia on the article <paramref name="id"/> ("astronomy/planet-sol-earth"), its
	/// filter cleared; on the search for <paramref name="id"/>'s words when there is no such known article. False then.
	/// </summary>
	bool OpenArticle(const cs::String& id);

	/// <summary>
	/// US-T272's hook for live stats: the readings of the thing an article is about as the simulation stands now (a body's
	/// gravity, a settlement's people, a pilot's hull), a line each; empty for an article about no such thing.
	/// </summary>
	cs::List<cs::String> ArticleLiveStats(const Core::EncyclopediaArticle& a);

	/// <summary>Before the commands each frame: while the screen's filter is being typed in it has the keys (true: the
	/// owner hands the input over).</summary>
	bool UpdateEncyclopediaKeys();

	/// <summary>Draws the Encyclopedia over the whole view (the Hud calls it while <see cref="ShowEncyclopedia"/>).</summary>
	void DrawEncyclopedia();

	/// <summary>The screen's own textures go with the Hud's (U.SPC-T366).</summary>
	void DestroyEncyclopediaTextures();

private:
	cs::Ref<UI::EncyclopediaScreen> encyclopediaScreen;
	cs::Ref<Core::Encyclopedia> encyclopedia;
	int encyclopediaBuilds = 0;
	int encyclopediaLog = -1, encyclopediaKnown = -1, encyclopediaBooks = -1;
	const void* encyclopediaGalaxy = nullptr;
	bool encyclopediaWasShown = false, encyclopediaStale = true;
	int knowledgeFrames = 0, knowledgeLog = -1, knowledgeBooks = -1;
	const void* knowledgeGalaxy = nullptr;
	/// <summary>Adds the controls articles from the key bindings as they stand and the gamepad's map.</summary>
	void AddControlArticles(Core::Encyclopedia& book);
