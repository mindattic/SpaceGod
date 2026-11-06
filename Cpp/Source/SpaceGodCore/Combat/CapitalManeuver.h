#pragma once
// US-T187: dynamic capital-ship combat -- the hull's six shield facings, the heavy roll that turns a fresh one to the
// enemy, and the close-then-cross run that brings the broadside to bear at the guns' range.

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class ShipCombat;

	/// <summary>One of a capital hull's six faces (US-T187), in its own frame: bow (+Z), stern, port (-X), starboard (+X),
	/// dorsal (+Y) and ventral.</summary>
	enum class ShieldFacing : int { Fore, Aft, Port, Starboard, Dorsal, Ventral };
	SPACEGODCORE_API const char* ToString(ShieldFacing v);

	/// <summary>
	/// What is left of a hull's shields on each of its six faces (US-T187): the mean live field (0 down or holed, 1 full,
	/// <see cref="ShieldEmitters.PatchStrength"/>) over points spread across that face of the real capsule hull. The emitter
	/// patches (U.SPC-T501) stay the truth a shot drains; this is how a captain reads them by face.
	/// </summary>
	struct SPACEGODCORE_API FacingShields
	{
		double Strength[6] = { 1, 1, 1, 1, 1, 1 };

		double Of(ShieldFacing f) const { return Strength[static_cast<int>(f)]; }

		/// <summary>The face with the most field left (the first, in facing order, among equals).</summary>
		ShieldFacing Strongest() const;

		/// <summary>The face with the least field left.</summary>
		ShieldFacing Weakest() const;

		/// <summary>The field a direction (unit, hull frame) meets: each face weighted by how squarely it looks that way.</summary>
		double Toward(DVec3 local) const;
	};

	/// <summary>
	/// Dynamic capital-ship combat (US-T187), layered on the crossing (US-T164, <see cref="Choreography.Crossing"/>) and the
	/// weak-patch roll (U.SPC-T502, <see cref="WeakPatches"/>) rather than replacing them. A capital closes on its target
	/// until its guns reach, then steams across its line broadside-on at its preferred range (<see cref="Closing"/>); it
	/// reads its shields by face (<see cref="Facings"/>), rolls its strongest face toward the main threat while its battery
	/// still bears (<see cref="PresentWeight"/>, weighed in WeakPatches' choice of attitude), and comes about to rotate its
	/// fresh broadside in when the one it has been fighting with is depleted (<see cref="BroadsideWorn"/>). Its rolls are
	/// slow and heavy (<see cref="Roll"/>): they spin up and brake at the hull's own roll authority, to the roll rate the
	/// ship catalog gives its class (US-T193) over its battle turn rate. All AI only, deterministic, no state but the roll's
	/// swing (BroadsideCommand::RollSpin, absent from older saves and then at rest).
	/// </summary>
	class SPACEGODCORE_API CapitalManeuver final
	{
	public:
		/// <summary>A face is depleted under this much field; the other broadside must be this much fresher to come about for.</summary>
		static constexpr double DepletedAt = 0.4; static constexpr double FreshMargin = 0.2;

		/// <summary>How much of its battery's share a captain gives for showing its strongest face to the threat rather than its
		/// weakest (on top of the weak-patch exposure): enough to pick the fresher of two worn faces, far under a holed one.</summary>
		static constexpr double PresentWeight = 0.12;

		/// <summary>The fewest and most seconds a capital's roll takes to spin up to its full rate (and as long to brake):
		/// the hull's own roll authority within these, so even the handiest capital reads heavy and none hangs forever.</summary>
		static constexpr double MinSpinUpS = 2.5; static constexpr double MaxSpinUpS = 8;

		/// <summary>The roll rate over the turn rate for a class the catalog has no row for (the catalog's own default).</summary>
		static constexpr double DefaultRollFactor = 1.5;

		/// <summary>From this many times its preferred range a capital drives straight in; at it or nearer it crosses.</summary>
		static constexpr double CloseFromShare = 1.8;

		/// <summary>Whether the battle flies <paramref name="u"/> as a maneuvering capital: a capital-sized ship the battle's
		/// AI captains (not the player's hull).</summary>
		static bool Flies(const cs::Ref<BattleUnit>& u);

		// ------------------------------------------------------------ the facings

		/// <summary>The face a direction (hull frame) points out of: whichever axis it lies most along.</summary>
		static ShieldFacing FacingOf(DVec3 local);

		/// <summary>The outward axis of face <paramref name="f"/>, hull frame.</summary>
		static DVec3 NormalOf(ShieldFacing f);

		/// <summary>What is left of <paramref name="c"/>'s shields on each face (a hull with no emitters reads full everywhere).</summary>
		static FacingShields Facings(const cs::Ref<ShipCombat>& c);

		/// <summary>The way the weight of the fire on <paramref name="u"/> comes from (unit, battle axes; zero with none), from
		/// WeakPatches::IncomingFireOn.</summary>
		static DVec3 ThreatDir(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>The face of <paramref name="u"/>, as it lies now, that a battle-axes direction meets.</summary>
		static ShieldFacing FacingToward(const cs::Ref<BattleUnit>& u, DVec3 dir);

		/// <summary>
		/// Whether the broadside <paramref name="u"/> shows the threat is depleted (under <see cref="DepletedAt"/>) while its
		/// other broadside has at least <see cref="FreshMargin"/> more: the crossing comes about to rotate the fresh one in.
		/// </summary>
		static bool BroadsideWorn(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u);

		/// <summary>The bonus WeakPatches' choice gives an attitude (nose <paramref name="forward"/>, roof <paramref name="up"/>)
		/// for the field of the faces it shows <paramref name="threat"/> (battle axes), <see cref="PresentWeight"/> at most.</summary>
		static double PresentBonus(const FacingShields& faces, DVec3 threat, DVec3 forward, DVec3 up);

		// ------------------------------------------------------------ the run

		/// <summary>How much of a capital's wanted velocity is a straight run in at <paramref name="distKm"/> from its target
		/// with its guns reaching <paramref name="rangeKm"/>: 0 within range (it crosses), 1 from <see cref="CloseFromShare"/>
		/// times it, smooth between.</summary>
		static double Closing(double distKm, double rangeKm);

		// ------------------------------------------------------------ the roll

		/// <summary>The fastest <paramref name="u"/> rolls in battle, deg/s: its battle turn rate times its class's roll over
		/// turn in the ship catalog.</summary>
		static double RollRateDegS(const cs::Ref<BattleUnit>& u);

		/// <summary>How fast its roll spins up and brakes, rad/s²: the hull's roll authority (thrust over mass,
		/// HullDynamics), held to <see cref="MinSpinUpS"/>..<see cref="MaxSpinUpS"/> to full rate.</summary>
		static double RollAlpha(const cs::Ref<BattleUnit>& u);

		/// <summary>
		/// Rolls <paramref name="u"/> about its nose toward <paramref name="roof"/> the heavy way: the swing builds at
		/// <see cref="RollAlpha"/> to at most <see cref="RollRateDegS"/> and brakes to stop on the roll, not past it. Returns
		/// false (nothing done) for a hull that is not <see cref="Flies"/>, which rolls as it did.
		/// </summary>
		static bool Roll(const cs::Ref<BattleUnit>& u, DVec3 roof, double dt);
	};
}
