// UnitySpaceApp.Influence.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Influence.cpp.
//
// <summary>
// US-T140, embody limits and Influence as the player meets them: the HUD's Influence meter (the SpaceGod's level, its title
// and the way to the next), the refusal banner when the embody gate says no (in battle, or not Influence enough), the squad
// a band captain held is closing with (and how the order to stand down went), and the dev menu's SpaceGod entries (grant
// Influence, lift the limits, the stand-down scenario).
// </summary>

public:
	/// <summary>Each frame (US-T140): hands the session's embody gate this frame's world (the same on every peer).</summary>
	void UpdateInfluence();

	/// <summary>The Influence meter and, while one is fresh, the refusal banner and the squad's line (US-T140).</summary>
	void DrawInfluence();

	/// <summary>The embody gate said no (US-T140): the banner shows it for a few seconds, and a toast says it.</summary>
	void NoteEmbodyRefusal(const cs::String& what, const Core::EmbodyVerdict& verdict);

	/// <summary>
	/// The stand-down scenario (US-T140, the dev menu and sg.StandDown): an unescorted freighter ahead and a pirate band (a
	/// corvette and two fighters) beside it, closing, contact in <paramref name="seconds"/>; you at the band captain's helm
	/// (a dev path, past the gate). Shift+1 (the bar's squad order) tells them to stand down. False if it could not be laid out.
	/// </summary>
	bool StartStandDownScenario(double seconds = 75);

	/// <summary>At a band captain's helm, orders the squad to stand down through the helm's own path (as its Shift+n key);
	/// returns what came of it (the toast).</summary>
	cs::String OrderSquadStandDown();

	/// <summary>The dev menu's SpaceGod entries (US-T140): registered from this feature's own file.</summary>
	static void RegisterInfluenceCheats();

private:
	cs::String embodyRefusal;
	int embodyRefusalRule = 0; // a Core::EmbodyRefusal
	int embodyRefusalNeed = 0, embodyRefusalHave = 0;
	float embodyRefusalUntil = -1;
	int influenceGainSeq = 0;
	float influenceGainUntil = -1;
	cs::Ref<Texture2D> influenceBack, influenceFill, influenceTrack, influenceRefuseBack;
	cs::Ref<GUIStyle> influencePanel, influenceRefusePanel, influenceTitle, influenceSmall, influenceRefuse;
