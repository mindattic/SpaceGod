// UnitySpaceApp.Rewind.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Rewind.cpp.
//
// US-T99 (docs/REWIND.md, phase 2): time reversal's keyframe ring in the game. While the session runs, a keyframe of the
// whole world (the save format) is taken now and then, thinned by game age under a fixed memory budget; Shift+F8 loads
// the one before now in place (pressed again within a few seconds, the one before that), and play going on from there
// replaces the future that was ahead. sg.Rewind lists the ring and rewinds by game time. A rewind lands on a keyframe:
// replaying the intervention log to any moment between them is the next phase.

public:
	/// <summary>The session's keyframes (US-T99).</summary>
	Core::RewindRing Rewind;

	/// <summary>Game seconds since the session (or the save it loaded) began, along this timeline: the ring's key.</summary>
	double RewindSessionS() const { return rewindSessionS; }

	/// <summary>Loads the newest keyframe at least <paramref name="backS"/> game seconds back (else the session start) in
	/// place; false (with a toast saying why) when it cannot.</summary>
	bool RewindBack(double backS);

	/// <summary>Loads keyframe <paramref name="id"/> in place; false (with a toast saying why) when it cannot.</summary>
	bool RewindTo(int id);

	/// <summary>One line per keyframe, oldest first, and a total (sg.Rewind list).</summary>
	cs::String DescribeRewind() const;

	/// <summary>RewindBack / RewindTo at the start of the next frame (asked from outside the frame, as sg.Rewind does).</summary>
	void RequestRewindBack(double backS) { pendingRewindBackS = backS; }
	void RequestRewindTo(int id) { pendingRewindId = id; }

private:
	double pendingRewindBackS = -1;
	int pendingRewindId = -1;

	double rewindSessionS = 0;
	double rewindLastJd = std::numeric_limits<double>::quiet_NaN();

	/// <summary>Off for the scripted tour, captures and -norewind (a keyframe is a short hitch).</summary>
	bool rewindEnabled = true;
	bool rewindInit = false;

	/// <summary>Set while a keyframe is being loaded, so ApplyLoaded keeps the ring (any other load starts a new one).</summary>
	bool rewindRestoring = false;

	/// <summary>Pressed again within this many real seconds of a rewind, Shift+F8 steps back from the moment rewound to.</summary>
	static constexpr double RewindStepWindowS = 6;

	/// <summary>Each frame, from HandleSaveCommands: the session's game time, a keyframe when one is due, and Shift+F8.</summary>
	void UpdateRewind();

	/// <summary>From ApplyLoaded: a save loaded (not a keyframe) starts a new timeline rooted at it.</summary>
	void RewindAfterLoad();

	/// <summary>A battle in the watched system, or a craft held: the dense keyframe tier applies.</summary>
	bool RewindBattle() const;
