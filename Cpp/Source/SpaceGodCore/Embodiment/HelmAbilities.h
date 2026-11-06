#pragma once
// US-T106: a hull's own abilities at its helm.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class IEmbodiable;
	class Ship;
	class World;
	enum class FighterRole : int;

	/// <summary>
	/// What the player can order from a ship's helm beyond flying and fighting it (US-T106, <see cref="HelmAbilities"/>). The
	/// numbers are the wire's (<see cref="EmbodimentInput.HelmAbility"/>, five bits of a control line's flags): append only.
	/// </summary>
	enum class HelmAbility : int
	{
		None = 0,
		LaunchInterceptors = 1,
		LaunchFighters = 2,
		LaunchBombers = 3,
		LaunchHeavyFighters = 4,
		LaunchRecon = 5,
		RecallWing = 6,

		/// <summary>The wing's call (<see cref="BattleUnit.WingCall"/>): onto the helm's locked hull, guard the carrier, or
		/// their own fights again.</summary>
		WingAttack = 7,
		WingGuard = 8,
		WingFree = 9,

		SendShuttle = 10,
		FireBreacher = 11,
		FirePods = 12,

		/// <summary>US-T140: a band's captain orders the squad to stand down (out of a fight, <see cref="SquadOrders.StandDown"/>).
		/// Last on the bar, so the slots before it keep their keys.</summary>
		StandDown = 13,
	};

	/// <summary>One entry of a helm's ability bar: what it does, and how it stands now (craft aboard, reloading, ready).</summary>
	struct HelmAbilityEntry
	{
		HelmAbility Ability = HelmAbility::None;
		cs::String Label, State;
		bool Ready = false;
	};

	/// <summary>
	/// A hull's abilities at its helm (US-T106): built from what it really carries, never invented. A carrier launches its
	/// squadrons one class at a time (one entry for each class its hangar holds), recalls its wing, and calls it onto the
	/// hull the helm has locked or to guard the carrier; a hull with a shuttle bay sends a boarding shuttle, one with a
	/// Breacher bay fires a Breacher round, one with boarding pods fires them, each at the locked hull. A hull with none of
	/// these has an empty bar. Orders are the player's only, from the helm (<see cref="ShipHelm.Control"/>, through the
	/// helm's command stream in co-op); the AI never gives them. Every order but the wing's call needs a fight: out of one
	/// it is refused, saying why.
	/// </summary>
	class SPACEGODCORE_API HelmAbilities
	{
	public:
		/// <summary>The bar for <paramref name="ship"/>'s helm, in key order (Shift+1 for the first, Shift+0 the tenth). With
		/// <paramref name="world"/>, a band's captain also has the squad's order last (US-T140); every caller that maps keys
		/// to slots passes the same world, so the HUD's bar and the keys agree.</summary>
		static cs::List<HelmAbilityEntry> Of(const cs::Ref<Ship>& ship, const cs::Ref<World>& world = nullptr);

		/// <summary>The bar's heading: what kind of hull it is ("CARRIER", "ASSAULT SHIP"...), from what it carries.</summary>
		static cs::String Heading(const cs::Ref<Ship>& ship);

		/// <summary>What the entry at <paramref name="slot"/> (1-based) of <paramref name="ship"/>'s bar orders, or none.</summary>
		static HelmAbility AtSlot(const cs::Ref<Ship>& ship, int slot, const cs::Ref<World>& world = nullptr);

		/// <summary>The craft class a launch ability puts up, and whether it is one.</summary>
		static bool LaunchRole(HelmAbility a, FighterRole& role);

		/// <summary>The result of the last order given (for the HUD's toast and the jump into a launched craft).</summary>
		struct Result
		{
			int Seq = 0;
			cs::String Toast;
			cs::Ref<Ship> Carrier;
			cs::List<cs::Ref<BattleUnit>> Launched;
		};

		static const Result& Last();

		/// <summary>
		/// Carries out <paramref name="a"/> from <paramref name="ship"/>'s helm and records what came of it in <see cref="Last"/>
		/// (every peer in co-op, so each toasts it). Returns whether anything happened.
		/// </summary>
		static bool Use(const cs::Ref<Ship>& ship, HelmAbility a, const cs::Ref<World>& world = nullptr);

		/// <summary>An ability carried in a seat's command (<see cref="SeatAuthority"/>): used if <paramref name="seat"/> is a helm.</summary>
		static void Command(const cs::Ref<IEmbodiable>& seat, int ability);

		/// <summary>
		/// The craft of <paramref name="carrier"/> aloft in <paramref name="battle"/> the player can jump into, those just
		/// launched first: the units in <paramref name="recent"/> (newest first) that are still the carrier's and flying, then
		/// the rest of its craft aloft, the latest out first.
		/// </summary>
		static cs::List<cs::Ref<BattleUnit>> Aloft(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& carrier, const cs::List<cs::Ref<BattleUnit>>& recent);

	private:
		static Result& Mutable();
		static cs::String Order(const cs::Ref<Ship>& ship, HelmAbility a, cs::List<cs::Ref<BattleUnit>>& launched, const cs::Ref<World>& world);
	};
}
