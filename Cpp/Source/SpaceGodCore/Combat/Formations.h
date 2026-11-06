#pragma once
#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	enum class FighterRole : int;

	/// <summary>The shape a squadron flies in behind its lead (US-T198).</summary>
	enum class FormationShape : int
	{
		/// <summary>A wedge (the vee): wingmen in pairs, one each side, each pair further back.</summary>
		Wedge,

		/// <summary>Line abreast: wingmen out to either side of the lead, almost level with it.</summary>
		Line,

		/// <summary>An echelon: every wingman on the one side, stepped back in a single diagonal.</summary>
		Echelon,
	};
	SPACEGODCORE_API const char* ToString(FormationShape v);

	/// <summary>
	/// Squadron formations (US-T198). A squadron flies in its class's shape behind its lead (<see cref="ShapeOf"/>):
	/// interceptors and heavy fighters a wedge, space superiority fighters an echelon (to the right for an even-numbered
	/// squadron, the left for an odd one), bombers and recon flights line abreast, so their torpedoes go in on one front.
	/// The slots hang in the lead's own level frame, so the shape turns with it and holds through its turns and while it
	/// cruises. When the squadron engages (an enemy fighter within <see cref="EngageKm"/> of its lead, or one jumping a
	/// wingman within <see cref="JumpedKm"/>) the wingmen break and each takes on the enemy fighter nearest it
	/// (<see cref="BreakTarget"/>); once the furball is over (nothing left within <see cref="ReformKm"/> of the lead) they
	/// form up on it again. Bombers and recon craft never break: they hold their line for the run.
	/// Everything here is a pure function of the battle's state (no draws), and in battle-local km, which is the same
	/// at both scales (<see cref="ScaleModel.GameShipScale"/> scales all hardware alike).
	/// The one hook into <see cref="FighterAI"/> is its wingman step: the slot it flies to and whether it breaks.
	/// </summary>
	class SPACEGODCORE_API Formations
	{
	public:
		/// <summary>How near the lead an enemy fighter must come for the squadron to break and engage it, km.</summary>
		static constexpr double EngageKm = 5;

		/// <summary>How near the lead the fight must stay for a broken wingman to keep at it, km (wider than
		/// <see cref="EngageKm"/>, so a squadron does not flicker between breaking and forming up at the edge).</summary>
		static constexpr double ReformKm = 9;

		/// <summary>How near a wingman an armed enemy fighter must be for it to break on its own, km.</summary>
		static constexpr double JumpedKm = 1.5;

		/// <summary>US-T225: how near its capital target (beyond the hull's own radius) a lead on an attack run must be before
		/// its wingmen split off onto strafing runs of their own, km; further out the squadron flies in formation.</summary>
		static constexpr double StrafeBreakKm = 6;

		/// <summary>The length the slot spacings are drawn for (an interceptor's), km: a longer lead's slots open out with
		/// it, up to twice as wide.</summary>
		static constexpr double SpacingLengthKm = 0.016;

		/// <summary>The shape a squadron of <paramref name="role"/>'s craft flies.</summary>
		static FormationShape ShapeOf(FighterRole role);

		/// <summary>The shape <paramref name="lead"/>'s squadron flies (Wedge for a unit that is not a launched craft).</summary>
		static FormationShape ShapeOf(const cs::Ref<BattleUnit>& lead);

		/// <summary>Whether squadron <paramref name="squadron"/>'s echelon steps out to the left rather than the right.</summary>
		static bool Mirrored(int squadron) { return (squadron & 1) == 1; }

		/// <summary>
		/// Slot <paramref name="slot"/>'s place behind the lead (slot 0, the lead itself, is the origin), km in the lead's
		/// level frame: right, up, forward. <paramref name="leadLengthKm"/> opens the spacing out for longer craft.
		/// </summary>
		static DVec3 Slot(FormationShape shape, int slot, bool mirrored, double leadLengthKm = SpacingLengthKm);

		/// <summary>Wingman <paramref name="u"/>'s slot behind its lead, in its squadron's shape.</summary>
		static DVec3 SlotOf(const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// The enemy fighter wingman <paramref name="u"/> (flying on its lead under <see cref="WingOrder.FormUp"/>) breaks
		/// formation to engage, or null while it holds its slot. Keeps the one it is already on while that is still in the
		/// fight; otherwise the nearest to it.
		/// </summary>
		static cs::Ref<BattleUnit> BreakTarget(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>Whether <paramref name="u"/> has broken formation and is fighting on its own (its own phase a dogfight).</summary>
		static bool Broken(const cs::Ref<BattleUnit>& u);

		/// <summary><paramref name="u"/> breaks onto <paramref name="foe"/>: into its own dogfight, a fresh one if it was in the vee.</summary>
		static void Break(const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& foe);

		/// <summary><paramref name="u"/>'s fight is over: it forms up on its lead again, its own phase back to the approach.</summary>
		static void Rejoin(const cs::Ref<BattleUnit>& u);

	private:
		/// <summary>An armed enemy fighter still in the fight: hostile to <paramref name="u"/>, present, not struck or taken.</summary>
		static bool Foe(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<BattleUnit>& o);
	};
}
