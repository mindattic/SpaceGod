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
//     fading out; and a pulse round the screen's edge when its shields go down and when its hull goes critical.

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
