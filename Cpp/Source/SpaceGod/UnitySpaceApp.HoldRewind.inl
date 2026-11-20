// UnitySpaceApp.HoldRewind.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h). Bodies in UnitySpaceApp.HoldRewind.cpp.
//
// US-T409 (docs/REWIND.md, "Hold to rewind"): the designer's short rewind. Holding the key (Shift+R) drops time to real
// speed and plays the last seconds of the fight in view (and the hull flown) backward, up to HoldRewindTuning::WindowS
// real seconds; letting go plays on from the moment reached. Free, then a cooldown. The recording is a read-only
// observer of the game layer (nothing in Core calls it, it draws no random numbers), so the Bench never meets it.

public:
	/// <summary>US-T409: every number the hold rewind is tuned by, in one place.</summary>
	struct HoldRewindTuning
	{
		/// <summary>The most real seconds a hold goes back (the designer's 10).</summary>
		static constexpr double WindowS = 10;
		/// <summary>Real seconds after a rewind before the next one (the designer's 60).</summary>
		static constexpr double CooldownS = 60;
		/// <summary>How often a capital ship's pose is kept (docs/research/rewind-short-track.md: 5 Hz).</summary>
		static constexpr double CapitalHz = 5;
		/// <summary>How often a craft's, missile's, round's or decoy's pose is kept (20 Hz: their turns set the rate).</summary>
		static constexpr double CraftHz = 20;
		/// <summary>The hull the player holds is kept every frame.</summary>
		static constexpr bool PlayerEveryFrame = true;
		/// <summary>A resume point (the whole fight's state, by value) every this many real seconds: letting go plays on from
		/// the newest one at or before the moment reached.</summary>
		static constexpr double AnchorS = 0.5;
		/// <summary>Real seconds of the past played back per real second held (1: as it was lived, backward).</summary>
		static constexpr double PlaybackSpeed = 1;
		/// <summary>Poses kept at most, all tracks together (about 64 bytes each), before projectiles stop being kept.</summary>
		static constexpr size_t MaxSamples = 600000;
		/// <summary>A hold shorter than this (real seconds) plays on from where it began: a tap does nothing.</summary>
		static constexpr double MinHoldS = 0.15;
	};

	/// <summary>US-T409: the hold rewind's readout (OnGUI): REWIND ready, the cooldown, or how far back a hold has gone.</summary>
	void DrawHoldRewind();

	/// <summary>US-T409: true while a hold rewind is playing the past back (the simulation stands still meanwhile).</summary>
	bool HoldRewindActive() const;

private:
	/// <summary>US-T409: the tracks, the resume points and the playback (UnitySpaceApp.HoldRewind.cpp).</summary>
	struct HoldRewindState;
	std::shared_ptr<HoldRewindState> holdRewind;

	/// <summary>US-T409: called each frame before the commands: starts a hold on the key's press, plays the past back while
	/// it is held, and plays on (or comes back to now) when it is let go. True while the rewind has the frame: the caller
	/// then runs only the drawing, never the simulation.</summary>
	bool UpdateHoldRewind(float dt);

	/// <summary>US-T409: at the end of a frame that ran the simulation: keeps this frame's poses and, when one is due, a
	/// resume point. Read only.</summary>
	void RecordHoldRewind(float dt);

	/// <summary>US-T409: forgets every track and resume point (a load, a keyframe rewind, co-op).</summary>
	void ClearHoldRewind();

	/// <summary>US-T409: the fight the hold rewind keeps (the hull flown's, else the one watched), or null.</summary>
	cs::Ref<Core::Battle> HoldRewindBattle();

	/// <summary>US-T409: why a hold cannot start now (empty: it can).</summary>
	cs::String HoldRewindRefusal();

	/// <summary>US-T409: writes the recorded past at the moment the hold has reached into the fight's hulls, missiles,
	/// rounds and decoys and into the hull flown, for the views to draw (the moment the hold ends puts a whole state
	/// back over all of it).</summary>
	void PlayHoldRewind();

	/// <summary>US-T409: the key let go: the resume point at or before the moment reached is put back and play goes on from
	/// there (no further back than the last change that cannot be undone yet; a tap changes nothing).</summary>
	void EndHoldRewind();
