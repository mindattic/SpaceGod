// Hud.Hits.inl -- included INSIDE class Hud (see UI/Hud.h).
// US-T257: hit and damage feedback for the unit embodied; bodies in UI/Hud.Hits.cpp, what it needs in UI/Hud.Hits.deps.h.
//
// Read from the battle's effects (Battle::Fx, tagged with whose shot each is by CombatFx::By) and the held hull's own
// shield and hull, never written back: nothing here enters the simulation. In every seat that fires (a helm, a launched
// craft, a gun mount, a station):
//   - a hit marker on the crosshair when the unit's own round, beam or missile lands, styled by what it struck: the
//     shield (blue), the hull (white) or an exposed weak point (gold, larger);
//   - a kill marker and a line in a small combat feed when a hull it hit is destroyed, disabled or strikes its colours;
//   - arcs round the reticle pointing where the held hull is taking fire from, blue on the shield and red on the hull,
//     fading out; and a pulse round the screen's edge when its shields go down and when its hull goes critical;
//   - US-T300: a threat warning for warheads homing on it and hostiles locking it (DrawThreatWarning);
//   - US-T303: an after-action report when its fight ends or it leaves the fight (DrawAfterAction);
//   - US-T309: a LOST / RESTORED line in the feed for each of its own subsystems shot away or repaired (TrackOwnDamage);
//   - US-T312: leading a squadron, its wingmen's radio calls in the feed (TrackWing).

private:
	enum class HitMark : int { Shield, Hull, WeakPoint };

	/// One arc of incoming fire: the way it came from (battle km, from the held hull), when, and on what it fell.
	struct HitArc
	{
		Core::DVec3 From;
		float Born = 0;
		bool Shield = false;
		float Weight = 1;
	};

	/// A line of the combat feed.
	struct HitFeedLine
	{
		cs::String Text;
		Color Tint;
		float Born = 0;
	};

	/// US-T303: what the held unit did and took in one fight, for the after-action report.
	struct HitTally
	{
		int Shield = 0, Hull = 0, Weak = 0, Destroyed = 0, Disabled = 0, Struck = 0, TakenShield = 0, TakenHull = 0;
		/// US-T309: the held hull's own subsystems shot away, and brought back by damage control, in the fight.
		int SystemsLost = 0, SystemsRestored = 0;
		double SecondsFrom = 0, SecondsTo = 0, HullFrom = 1, HullTo = 1;
		cs::String Where, Outcome;
		Color Tint;
	};

	static constexpr float AfterActionSeconds = 12.0f;
	static constexpr float HitMarkSeconds = 0.22f;
	static constexpr float KillMarkSeconds = 0.75f;
	static constexpr float HitArcSeconds = 1.4f;
	static constexpr float HitFeedSeconds = 5.0f;
	static constexpr int HitFeedMax = 5;
	static constexpr int HitArcMax = 12;
	/// How long after the unit's last hit on a hull its fall is still the unit's (a kill by fire already started).
	static constexpr float HitCreditSeconds = 15.0f;
	/// The held hull's hull share under which it is critical (and back over which, with a margin, it is not).
	static constexpr float HullCritical = 0.25f;

	cs::Ref<Core::Battle> hitBattle;
	cs::Ref<Core::BattleUnit> hitUnit;
	int hitSeq = 0;
	float hitSeenAt = -100.0f;
	float hitMarkAt = -100.0f;
	HitMark hitMark = HitMark::Hull;
	float killMarkAt = -100.0f;
	float shieldDownAt = -100.0f;
	float hullCriticalAt = -100.0f;
	bool hadShield = false;
	bool hullWasCritical = false;
	cs::List<HitArc> hitArcs;
	cs::List<HitFeedLine> hitFeed;
	/// The hulls the unit has hit, and when it last did: whose fall it may claim.
	cs::Dictionary<cs::Ref<Core::BattleUnit>, float> hitStruck;
	/// The hulls already reported disabled (or found so when first hit), so each is reported once.
	cs::Dictionary<cs::Ref<Core::BattleUnit>, float> hitDisabled;
	/// US-T303: the fight running, whether it has been reported, and the last report and when it was made.
	HitTally hitTally, hitReport;
	bool hitTallyDone = true;
	float hitReportAt = -100.0f;
	/// US-T309: which of the held hull's subsystems were down at the last look, by index.
	cs::List<bool> hitSubDown;

	/// US-T312: the wingmen of the craft held, and whether each was away (flying home to its deck or down on one) at the
	/// last look; the one its order had it on (the lock it attacks, the threat it covers against), the last one called,
	/// and when.
	cs::Dictionary<cs::Ref<Core::BattleUnit>, bool> hitWing;
	cs::Ref<Core::BattleUnit> hitWingCalled;
	float hitWingCalledAt = -100.0f;

	/// US-T312: the squadron's radio calls while <paramref name="me"/> leads it from the cockpit: a feed line when a
	/// wingman is shot down (with how many are left), heads home to rearm or comes back into the fight, and, under Attack my
	/// target or Cover me, when the wing goes onto a hull, splashes it or finds the lead clear. The first look after a new
	/// fight or seat only takes stock.
	void TrackWing(const cs::Ref<Core::Battle>& battle, const cs::Ref<Core::BattleUnit>& me, float now, bool takeStock);

	/// A line of the combat feed, newest last.
	void AddHitFeed(const cs::String& what, Color tint, float now);

	/// US-T309: the held hull's own damage report: a feed line naming each of its subsystems shot away since the last look
	/// (and each that damage control has brought back), counted for the after-action card. The first look after a new
	/// fight or seat only takes stock, so what was down before is not reported as new.
	void TrackOwnDamage(const cs::Ref<Core::BattleUnit>& me, float now, bool takeStock);

	/// US-T303: ends the tally of <paramref name="me"/>'s fight in <paramref name="battle"/> (over, or left), making the
	/// report if the unit fired or was fired on.
	void CloseHitTally(const cs::Ref<Core::Battle>& battle, const cs::Ref<Core::BattleUnit>& me, float now);

	/// US-T303: the after-action card, top middle, for a while after a fight is reported.
	void DrawAfterAction(float now);

	/// The frame's feedback: reads the new effects, then draws the markers, arcs, feed and edge pulses. Repaint only.
	void DrawHitFeedback();

	/// Reads the battle's effects newer than the last read, as the held unit <paramref name="me"/> (firing from
	/// <paramref name="mount"/> alone when a gun mount is held).
	void TakeHitFx(const cs::Ref<Core::Battle>& battle, const cs::Ref<Core::BattleUnit>& me, const cs::Ref<Core::Subsystem>& mount, float now);

	/// A hull the unit had a hand in has fallen (or struck, or is crippled): the kill marker and a feed line.
	void ConfirmKill(const cs::Ref<Core::BattleUnit>& unit, const cs::String& how, Color tint, float now);

	/// Where the reticle in use is on the screen: the seat's sight, the helm's crosshair or else its nose.
	Vector2 HitReticle();

	/// The screen bearing (a unit GUI vector, y down) of a battle-km direction from the held hull.
	Vector2 HitBearing(Core::DVec3 direction);

	/// A band of light round the screen's edge, <paramref name="alpha"/> at its outer rim.
	void DrawEdgePulse(Color tint, float alpha);

	/// US-T300: the threat warning for the held hull, round the reticle at <paramref name="at"/>: a chevron the way each
	/// hostile warhead homing on it comes from (the nearer its impact, the brighter), a ring and time to impact on the
	/// round itself when it is on screen, and a banner over the reticle (MISSILE / TORPEDO / EMP with the count and the
	/// soonest impact, else LOCK while a hostile holds its nose on the hull). Read only, as the warning tones are.
	void DrawThreatWarning(const cs::Ref<Core::Battle>& battle, const cs::Ref<Core::BattleUnit>& me, Vector2 at, float now);
