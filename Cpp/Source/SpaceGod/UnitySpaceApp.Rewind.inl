// UnitySpaceApp.Rewind.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.Rewind.cpp.
//
// US-T99 (docs/REWIND.md, phase 2): time reversal's keyframe ring in the game. While the session runs, a keyframe of the
// whole world (the save format) is taken now and then, thinned by game age under a fixed memory budget; Shift+F8 loads
// the one before now in place (pressed again within a few seconds, the one before that), and play going on from there
// replaces the future that was ahead. sg.Rewind lists the ring and rewinds by game time.
//
// Phase 3: every frame's dt and clock (and whether the player touched anything) is logged, so a seek to any moment
// restores the keyframe before it and replays the logged frames up to it, stopping early at the first frame the player
// acted on (that input is not replayed yet). The timeline bar shows the session, its keyframes and the replayable
// stretch; a click on it seeks.

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

	/// <summary>Phase 3: goes to session game second <paramref name="targetS"/>: the keyframe at or before it, then the
	/// logged frames replayed up to it (or to the first one the player acted on); false (with a toast) when it cannot.</summary>
	bool RewindSeek(double targetS);
	void RequestRewindSeek(double targetS) { pendingRewindSeekS = targetS; }

	/// <summary>Shows the timeline bar for <paramref name="seconds"/> real seconds (it also shows after Shift+F8 and while
	/// a seek replays).</summary>
	void ShowRewindBar(double seconds);

	/// <summary>Called first thing each frame, with the frame's dt: notes the frame for the log, and while a seek replays,
	/// swaps in the logged dt and clock. True when the replay has the frame (the caller hands the input over).</summary>
	bool RewindFrameStart(float& dt);

	/// <summary>The timeline bar (OnGUI).</summary>
	void DrawRewindBar();

private:
	double pendingRewindBackS = -1;
	int pendingRewindId = -1;
	double pendingRewindSeekS = -1;

	/// <summary>One frame of play as the log keeps it: when it began on the timeline, the real seconds it stepped, the
	/// clock just before it advanced and just after, and whether the player touched a key, button, wheel or pad.</summary>
	struct RewindFrame
	{
		long long Serial = 0;
		double SessionS = 0;
		float Dt = 0;
		Coop::ClockState Before;
		double JdAfter = 0;
		bool Intervened = false;
		/// <summary>A hash of the world just after the frame (CoopDigest), on every RewindCheckEvery-th frame (0: none).</summary>
		unsigned long long Check = 0;
		/// <summary>US-T99: whether the frame ran the world's adventures, and round which point (the camera's, PlayerHelioKm):
		/// a replay runs them round the logged point, as a co-op client runs them round the host's (FrameRecord), so a
		/// restored view that stands elsewhere spawns nothing different.</summary>
		bool Adventures = false;
		Core::DVec3 AdventurePoint;
	};
	/// <summary>US-T99: the frame whose adventures this frame's CoopUpdateAdventures notes (live) or reads (replay); -1: none.</summary>
	long long rewindAdventureSerial = -1;
	bool rewindAdventureFromLog = false;
	/// <summary>US-T99: single player's adventures for this frame: round the camera live (noted in the log), round the logged
	/// point while a seek replays. Called from CoopUpdateAdventures in place of its single-player line.</summary>
	void RewindAdventures(bool wanted);

	/// <summary>Every this many logged frames the world's digest is kept, so a replay can tell whether it is re-living the
	/// play exactly (about once a second at 60 fps).</summary>
	static constexpr long long RewindCheckEvery = 60;
	/// <summary>The hash of CoopDigest() now (never 0).</summary>
	unsigned long long RewindWorldCheck();
	/// <summary>The replay's checks: compared, differing, and the session second of the first that differed (-1: none).</summary>
	int rewindReplayChecks = 0, rewindReplayMismatches = 0;
	double rewindReplayFirstMismatchS = -1;
	/// <summary>The last replay's verdict, for sg.Rewind list.</summary>
	cs::String rewindLastVerdict;

	/// <summary>The frame log, oldest first, serials consecutive (single player only).</summary>
	std::deque<RewindFrame> rewindFrames;
	/// <summary>The serial the next frame logged gets; a rewind sets it back, and the old future is dropped as play goes on.</summary>
	long long rewindNextSerial = 0;
	/// <summary>Each keyframe's first frame after it (the frame its world steps into).</summary>
	std::unordered_map<int, long long> rewindKeySerial;
	/// <summary>About 10 minutes of frames at 60 fps (a frame is under 112 bytes).</summary>
	static constexpr size_t RewindMaxFrames = 40000;
	/// <summary>A seek replays at most this many real seconds of play (replay runs at the speed it was played).</summary>
	static constexpr double RewindMaxReplayS = 90;

	bool rewindFrameStarted = false;
	float rewindFrameDt = 0;
	Coop::ClockState rewindFrameClock;
	double rewindFrameSessionS = 0;
	bool rewindFrameIntervened = false;

	bool rewindReplaying = false;
	long long rewindReplayNext = 0, rewindReplayStart = 0, rewindReplayStop = 0;
	double rewindReplayTargetS = 0;
	int rewindReplayDrift = 0;
	cs::String rewindReplayWhy;

	double rewindBarUntil = 0;
	cs::Ref<GUIStyle> rewindBarPanel, rewindBarText;
	cs::Ref<Texture2D> rewindBarBack, rewindBarLog, rewindBarKey, rewindBarNow, rewindBarHover;

	/// <summary>The logged frame with this serial, or null.</summary>
	const RewindFrame* RewindFrameAt(long long serial) const;
	/// <summary>The replay is over (it arrived, or Esc or a gap stopped it): play goes on from here.</summary>
	void FinishRewindReplay(bool stopped);
	/// <summary>Logs the frame that just ran (from UpdateRewind).</summary>
	void LogRewindFrame();

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
