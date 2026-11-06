#pragma once
// US-T98: stoppable escapes. A hull the battle flies that breaks off jumps out of the fight only once its FTL drive has
// spooled up, in plain sight, and enough damage to its drive or hull while it charges cancels the jump.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class ShipCombat;

	/// <summary>
	/// The escape jump (US-T98). A hull its crew has broken off (<see cref="BattleUnit.Fleeing"/>) does not simply slip away:
	/// it spools its FTL drive, for <see cref="SpoolSecondsFor"/> (longer the heavier the hull), and jumps out when the charge
	/// is full. The charge shows in the battle (<see cref="FxKind.FtlSpool"/>, a ring of light round the hull that swells and
	/// brightens as it fills), and so does its end: a flash and a streak when it jumps (<see cref="FxKind.FtlJump"/>), a
	/// collapsing flicker when it is broken (<see cref="FxKind.FtlAbort"/>). Hits on the hull while it charges, and above
	/// all on its drive (a bomber's torpedoes put into its engines), count against the charge: once they pass
	/// <see cref="HullShare"/> of the hull or <see cref="DriveShare"/> of the drive (a share of each adds up) the jump is
	/// cancelled, and the drive needs <see cref="RestartS"/> before it can begin to spool again, from nothing. So does a stun
	/// (an interdiction pulse, an EMP). A hull that is disabled, boarded, latched to, struck or taken loses its charge. The
	/// gunners of a hull with a spooling enemy in their sights go for its drive (<see cref="DriveAimWeight"/>).
	/// A hull the player flies leaves the fight the player's own way, and never spools here.
	/// </summary>
	class SPACEGODCORE_API EscapeJumps
	{
	public:
		/// <summary>How many times the in-system cruise drive's spool (<see cref="SystemCruise.SpoolSecondsFor"/>) the FTL charge
		/// for an escape takes: about 10 s for a fighter-sized hull, about half a minute for a capital.</summary>
		static constexpr double SpoolFactor = 4;

		/// <summary>The share of its full hull a hull may lose while it charges before the jump is cancelled.</summary>
		static constexpr double HullShare = 0.05;

		/// <summary>The share of its drive's (every engine's) hit points it may lose while it charges before the jump is cancelled.</summary>
		static constexpr double DriveShare = 0.2;

		/// <summary>Seconds a cancelled drive takes before it can begin to spool again.</summary>
		static constexpr double RestartS = 5;

		/// <summary>How often a spooling hull's charge ring pulses, seconds.</summary>
		static constexpr double PulseS = 0.8;

		/// <summary>How much more a spooling hull's engines are worth to the gunners aiming at it than a mount's usual worth.</summary>
		static constexpr double DriveAimWeight = 6;

		/// <summary>How long <paramref name="u"/>'s escape charge takes, seconds.</summary>
		static double SpoolSecondsFor(const cs::Ref<BattleUnit>& u);

		/// <summary>Whether <paramref name="u"/>'s drive is spooling to jump out now.</summary>
		static bool Spooling(const cs::Ref<BattleUnit>& u);

		/// <summary>How full <paramref name="u"/>'s charge is, 0 to 1 (0 when it is not spooling).</summary>
		static double Fraction(const cs::Ref<BattleUnit>& u);

		/// <summary>The damage taken since the charge began, as a share of what cancels it: 1 or more cancels it.</summary>
		static double Disruption(const cs::Ref<BattleUnit>& u);

		/// <summary>The hit points left in <paramref name="c"/>'s engines, all of them.</summary>
		static double DriveHp(const cs::Ref<ShipCombat>& c);

		/// <summary>
		/// One battle step of <paramref name="u"/>'s escape, for a hull free to run (not disabled, boarded, latched, struck or
		/// taken; the battle calls <see cref="Reset"/> for one that is not): begins the charge once it breaks off, fills it,
		/// cancels it on enough damage or a stun, and returns true the step it jumps out.
		/// </summary>
		static bool Step(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt);

		/// <summary>Drops any charge <paramref name="u"/> holds, quietly (it no longer runs, or no longer can).</summary>
		static void Reset(const cs::Ref<BattleUnit>& u);

		/// <summary>Cancels <paramref name="u"/>'s charge where all can see it: the drive must rest <see cref="RestartS"/>
		/// before it can begin again.</summary>
		static void Abort(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);
	};
}
