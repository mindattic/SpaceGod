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
		/// <summary>Real seconds after a rewind before the next one (US-T410: the designer's 10, was 60). With the window
		/// it means: you can always go back 10 seconds, never into a time already rewound through (see FloorS).</summary>
		static constexpr double CooldownS = 10;
		/// <summary>US-T410: seconds of the past played back per real second at the press (the ease starts slow)...</summary>
		static constexpr double StartRate = 0.25;
		/// <summary>...rising smoothly over this many real seconds held...</summary>
		static constexpr double RampS = 0.8;
		/// <summary>...to this constant top rate (3: three seconds of the past a second).</summary>
		static constexpr double TopRate = 3.0;
		/// <summary>US-T410: within this many seconds of the oldest moment reachable the playback slows to settle on it...</summary>
		static constexpr double BrakeS = 0.6;
		/// <summary>...down to this rate at the edge itself.</summary>
		static constexpr double BrakeMinRate = 0.1;
		/// <summary>US-T410: real seconds the playback eases to a stop after the key is let go, before play goes on (0: at
		/// once). Short, so control never feels late.</summary>
		static constexpr double ReleaseEaseS = 0.12;
		/// <summary>US-T410: true: a held-back booking of the world (a hull lost, struck or taken) waits until its moment is
		/// older than the window, so a hold can bring the hull back; false: booked at once, a barrier as in US-T409.</summary>
		static constexpr bool DeferBookings = true;
		/// <summary>US-T417: true: the player's OWN hull lost, taken or struck, and any surrender's terms, wait out the window
		/// like everyone else's, so a hold can undo them too. Meanwhile the helm is held but dead in the player's hands (the
		/// view follows the wreck, the stick and triggers do nothing) and the readout says there is still time to rewind;
		/// once the moment is older than the window the loss is final exactly as before. False: booked at once, a barrier.</summary>
		static constexpr bool DeferOwnFate = true;
		/// <summary>US-T419: true: the fight's end (a win, a defeat, both sides breaking off) waits out the window too, the
		/// fight a mop-up meanwhile with its outcome shown, so a hold can undo it; the war settles it (loot, the ships let
		/// go) once the moment is older than the window. False: it ends at once, a barrier.</summary>
		static constexpr bool DeferFightEnd = true;
		/// <summary>US-T410: true: while held, the whole system is drawn as it was (the world clock set back for the drawing
		/// only: planets, ships in orbit and on the lanes, gates), and put back to now on letting go.</summary>
		static constexpr bool ViewSystemBack = true;
		/// <summary>How often a capital ship's pose is kept (docs/research/rewind-short-track.md: 5 Hz).</summary>
		static constexpr double CapitalHz = 5;
		/// <summary>How often a craft's, missile's, round's or decoy's pose is kept (20 Hz: their turns set the rate).</summary>
		static constexpr double CraftHz = 20;
		/// <summary>The hull the player holds is kept every frame.</summary>
		static constexpr bool PlayerEveryFrame = true;
		/// <summary>A resume point (the whole fight's state, by value) every this many real seconds: letting go plays on from
		/// the newest one at or before the moment reached.</summary>
		static constexpr double AnchorS = 0.5;
		/// <summary>Poses kept at most, all tracks together (about 64 bytes each), before projectiles stop being kept.</summary>
		static constexpr size_t MaxSamples = 600000;
		/// <summary>A hold shorter than this (real seconds) plays on from where it began: a tap does nothing.</summary>
		static constexpr double MinHoldS = 0.15;
	};

	/// <summary>US-T409: the hold rewind's readout (OnGUI): REWIND ready, the cooldown, or how far back a hold has gone.</summary>
	void DrawHoldRewind();

	/// <summary>US-T409: true while a hold rewind is playing the past back (the simulation stands still meanwhile).</summary>
	bool HoldRewindActive() const;

	/// <summary>US-T410: true for the frame a hold rewind put a past state back (watchers of the fight take stock then
	/// instead of reporting what changed).</summary>
	bool HoldRewindJustResumed() const;

	/// <summary>US-T421: the clock the effects views age their effects by (Time::unscaledTime, <paramref name="now"/>):
	/// while a hold plays the past back it runs backward with it, so an explosion contracts to its birth and is gone
	/// before it; otherwise <paramref name="now"/>.</summary>
	float HoldRewindFxClock(float now) const;
	/// <summary>US-T421: once per hold ended (<paramref name="seen"/> the caller's count): true, with the effects' clock of
	/// the moment play went on from. Effects born after it never happened; the rest go on from it, shifted to now.</summary>
	bool HoldRewindFxEnded(int& seen, float& resumeFx) const;

	/// <summary>US-T418: whether the ship at fleet index <paramref name="index"/> is drawn: while a hold plays the past back,
	/// a ship that went out through a gate after the moment reached is drawn again, and one that appeared after it is
	/// hidden; otherwise <paramref name="shownNow"/>.</summary>
	bool HoldRewindShipShown(int index, bool shownNow) const;

	/// <summary>US-T410: the self-test (sg.HoldRewind selftest): watches <paramref name="fight"/>, then drives the hold by
	/// itself and checks the resumed state, the floor, the cooldown and the ease. Each frame's <see cref="TickHoldRewindSelfTest"/>
	/// says whether it is still running; the summary line is "HoldRewind selftest: PASS|FAIL (n checks, m failed)".</summary>
	void BeginHoldRewindSelfTest(const cs::Ref<Core::Battle>& fight);
	bool TickHoldRewindSelfTest();
	/// <summary>US-T410: the self-test's last verdict (true: every check passed).</summary>
	bool HoldRewindSelfTestPassed() const;
	/// <summary>US-T420: whether the self-test wants the hold key held now. With <see cref="SetHoldRewindSelfTestRealKeys"/>
	/// the caller presses and lets go of the real keys (Shift+R through Unreal's input) to match, and the hold reads only
	/// the keys, exactly as when a player holds them.</summary>
	bool HoldRewindSelfTestWantsKey() const;
	void SetHoldRewindSelfTestRealKeys(bool real);
	/// <summary>US-T420: the hold key as the game read it this frame (InputState: Shift+R held).</summary>
	bool HoldRewindKeyHeld() const { return Input.HoldRewind; }

	/// <summary>US-T410: a hash of the fight's whole state (its clock and random draws to come, every unit's flags, pose,
	/// hull and mounts, every missile, round, mine and decoy), for the self-test.</summary>
	static unsigned long long HoldRewindFightDigest(const cs::Ref<Core::Battle>& fight);

	/// <summary>US-T423: lived seconds the fight in view has been recorded (-1: none in view), for the co-op smoke.</summary>
	double HoldRewindRecordedS() const;
	/// <summary>US-T423: how far back a hold has gone (0 when none plays).</summary>
	double HoldRewindBackS() const;
	/// <summary>US-T423: in co-op, who holds the rewind key and who it waits for ("REWIND: P1 Host holds Shift+R -
	/// waiting for P2 Client"), or empty (nobody holds it, or everyone does).</summary>
	cs::String CoopHoldRewindWaiting() const;
	/// <summary>US-T423: a co-op sync: the recording starts afresh on every peer (a hold under way back to its press; with
	/// <paramref name="book"/> the held-back bookings made first, so the sync's save holds them), its frame clock, cooldown
	/// and key edge from zero on every peer alike.</summary>
	void ResetHoldRewindForSync(bool book);

private:
	/// <summary>US-T423: co-op. The hold plays only while every player holds the key: the host ANDs its own key with each
	/// player's latest and writes it (with the fight it keeps and who holds) into each frame as "hr &lt;all&gt; &lt;fight&gt;
	/// &lt;mask&gt;" (left out when idle); a frame whose clock does not step carries "ho". Every peer runs the hold on the
	/// frame's values.</summary>
	cs::String coopHrEntry;
	bool coopHrAll = false, coopHrSent = false;
	int coopHrMask = 0, coopHrLogged = 0;
	long long coopHrStepFrame = -1;
	cs::Ref<Core::Battle> coopHrFight;
	bool coopHrMismatchWarned = false;

	/// <summary>US-T423: the hold's own step on this frame's key (UpdateHoldRewind's body).</summary>
	bool StepHoldRewind(float dt, bool down);
	/// <summary>US-T423: the frame's hold key in co-op: the host's AND of every player's (and the entry it writes), a
	/// client's from the frame replayed (its own key sent to the host when it changes).</summary>
	bool CoopHoldRewindKey(Coop::CoopPeer& peer);
	/// <summary>US-T423: an Update that ran the hold's step but no clock step: on the host it is a frame of its own
	/// ("ho"); on a client a hold frame replayed is consumed.</summary>
	void CoopHoldRewindFrame(bool held);
	/// <summary>US-T423: the seats every player holds and the helm, hashed: in co-op any change is a barrier (a resume puts
	/// no seat back).</summary>
	unsigned long long CoopHoldRewindSeats();
	long long CoopHoldRewindFrameNo(Coop::CoopPeer& peer) const;
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

	/// <summary>US-T409: forgets every track and resume point (a load, a keyframe rewind, co-op). US-T410: the bookings held
	/// back are made first (<paramref name="book"/>), unless the world they belong to is being thrown away (a load).</summary>
	void ClearHoldRewind(bool book = true);

	/// <summary>US-T410: wraps the fight's hooks so the world's bookings wait out the window (or unwraps them, booking
	/// what waited, for null).</summary>
	void HookHoldRewindBookings(const cs::Ref<Core::Battle>& fight);

	/// <summary>US-T410: makes the bookings held back whose moment is at or before <paramref name="uptoS"/> (lived
	/// seconds; all for the highest double). A resume never goes back past the newest made.</summary>
	void BookHoldRewindUpTo(double uptoS);

	/// <summary>US-T410: before any save or keyframe: the held-back bookings are made so the save holds them.</summary>
	void BookHoldRewindBeforeSave();

	/// <summary>US-T410: true while bookings wait and a due keyframe has waited less than the window: US-T99's keyframe
	/// waits with them (once, at most the window and a second), so its save does not book them early.</summary>
	bool HoldRewindDelaysKeyframe();

	/// <summary>US-T409: the fight the hold rewind keeps (the hull flown's, else the one watched), or null.</summary>
	cs::Ref<Core::Battle> HoldRewindBattle();

	/// <summary>US-T418: notes, per fleet index, when a ship went out through a gate and when it first appeared (lived
	/// seconds <paramref name="t"/>), for HoldRewindShipShown.</summary>
	void TrackHoldRewindShips(double t);

	/// <summary>US-T409: why a hold cannot start now (empty: it can).</summary>
	cs::String HoldRewindRefusal();

	/// <summary>US-T409: writes the recorded past at the moment the hold has reached into the fight's hulls, missiles,
	/// rounds and decoys and into the hull flown, for the views to draw (the moment the hold ends puts a whole state
	/// back over all of it).</summary>
	void PlayHoldRewind();

	/// <summary>US-T409: the key let go: the resume point at or before the moment reached is put back and play goes on from
	/// there (no further back than the last change that cannot be undone yet; a tap changes nothing).</summary>
	void EndHoldRewind();

	/// <summary>US-T417: called by UpdatePiloting before the stick is read: true while the hull flown is dead or taken in its
	/// fight but that loss is still held back (DeferOwnFate). The helm then follows the wreck and reads nothing; the caller
	/// skips the rest of the frame's piloting.</summary>
	bool HoldRewindOwnFateHeld(const cs::Ref<Core::Ship>& ship);

	/// <summary>US-T417: the seats a resume point was taken with put back (the helm, or a gun, a craft or a mech on the same
	/// body): so a resume to before a seat change puts the player back where they sat, not only the fight.</summary>
	void RestoreHoldRewindSeat(const cs::Ref<Core::IEmbodiable>& seat, bool piloting, const cs::Ref<Core::Ship>& pilotShip);
