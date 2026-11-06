#pragma once
// US-T140: ordering a squad through its leader, out of a fight (the helm orders of US-T106, Combat/HelmOrders.cpp, are the
// ones given in one). Embody a raider band's captain while the band is closing on a mark, or lying in wait for one, and
// order it to stand down: whether the crews comply is theirs to decide, by the SpaceGod's Influence and by the captain's
// own hold over them (rank, reputation, loyalty and history, from US-T138's named pilot). They obey, hesitate, argue, or
// mutiny and put someone else at the head of the band.
//
// Everything here is world state, changed only by the simulation and by the helm's command stream (a HelmAbility in the
// control line, so co-op's CommandCodec carries it), with every draw from the world (Characters::Roll over ids and the
// date), so each lockstep peer reaches the same answer. Nothing here runs unless a close-in or an order exists, so the
// Bench never sees it.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Ship;
	class World;

	/// <summary>How a squad answered its leader's order (US-T140).</summary>
	enum class SquadAnswer : int
	{
		None,
		Obey,
		Hesitate,
		Argue,
		Mutiny,
	};
	SPACEGODCORE_API const char* ToString(SquadAnswer v);

	/// <summary>
	/// A raider band closing on a mark, not engaged yet (US-T140): no battle exists, so its hulls are not marked in one and
	/// the SpaceGod may take any of them. At <see cref="StrikeJd"/> it falls on the mark (<see cref="War.StrikeAt"/>) unless
	/// it has been stood down, the mark is gone or either side is already fighting.
	/// </summary>
	class SPACEGODCORE_API CloseIn final
	{
	public:
		/// <summary>The band (<see cref="Ship.Band"/>) and the mark it is after.</summary>
		int Band = -1;
		cs::Ref<Ship> Mark;
		/// <summary>Where the strike falls (the mark's frame body) and when.</summary>
		int Body = 0;
		double StartJd = 0, StrikeJd = 0;
		/// <summary>Orders to stand down the crews have argued with: each makes the next harder to give.</summary>
		int Defied = 0;
	};

	/// <summary>What came of an order (US-T140): the answer and the line the HUD toasts.</summary>
	struct SPACEGODCORE_API SquadOrderResult
	{
		SquadAnswer Answer = SquadAnswer::None;
		cs::String Toast;
		/// <summary>The odds as weighed (0..1-ish), and the two halves of them, for the HUD and the manual's example.</summary>
		double Compliance = 0, InfluencePart = 0, CaptainPart = 0;
	};

	/// <summary>The squads' book (US-T140): the bands closing on marks and those stood down. Held on <see cref="War.Squads"/>.</summary>
	class SPACEGODCORE_API SquadOrders final
	{
	public:
		cs::List<cs::Ref<CloseIn>> Closing;

		/// <summary>Bands stood down (<see cref="Ship.Band"/>), until when: no ambush, no strike meanwhile.</summary>
		cs::Dictionary<int, double> StoodDown;

		/// <summary>How long a band that obeys keeps off, days.</summary>
		static constexpr double StandDownDays = 0.5;

		/// <summary>The world's book (made the first time it is asked for: older saves have none); null without a war.</summary>
		static cs::Ref<SquadOrders> Of(const cs::Ref<World>& world);

		/// <summary>The book if it exists, never making it.</summary>
		static cs::Ref<SquadOrders> Find(const cs::Ref<World>& world);

		/// <summary>Every other hull of <paramref name="ship"/>'s band, still flying (its escorts and its fellow escorts).</summary>
		static cs::List<cs::Ref<Ship>> SquadOf(const cs::Ref<World>& world, const cs::Ref<Ship>& ship);

		/// <summary>Puts <paramref name="leader"/>'s band closing on <paramref name="mark"/>, striking in <paramref name="seconds"/>.</summary>
		static cs::Ref<CloseIn> Begin(const cs::Ref<World>& world, const cs::Ref<Ship>& leader, const cs::Ref<Ship>& mark, double seconds, double jd);

		/// <summary>The close-in <paramref name="ship"/>'s band is on, or null.</summary>
		static cs::Ref<CloseIn> ClosingFor(const cs::Ref<World>& world, const cs::Ref<Ship>& ship);

		/// <summary>Whether <paramref name="ship"/>'s band is stood down at <paramref name="jd"/>.</summary>
		static bool IsStoodDown(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, double jd);

		/// <summary>
		/// What a stand-down order from <paramref name="ship"/>'s helm would be about, or why there is nothing to order:
		/// empty <paramref name="why"/> when it can be given. <paramref name="state"/> is the bar's line ("closing on the
		/// Ilse, contact in 42 s", "lying in wait", "engaged").
		/// </summary>
		static bool CanOrder(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, double jd, cs::String& state, cs::String& why);

		/// <summary>
		/// <paramref name="ship"/>'s captain orders the band to stand down (the SpaceGod at its helm). The crews weigh the
		/// SpaceGod's Influence and the captain's hold over them, then obey (the close-in is called off and the band keeps
		/// off for <see cref="StandDownDays"/>), hesitate (the strike held back a while; order again), argue (it goes on,
		/// and the next order is harder) or mutiny (the band's strongest captain deposes this one and strikes at once).
		/// </summary>
		static SquadOrderResult StandDown(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, double jd);

		/// <summary>The close-ins, advanced to <paramref name="jd"/> (War.Update): due ones strike, stale ones go.</summary>
		static void Advance(const cs::Ref<World>& world, double jd);

		SG_SERIALIZABLE(SquadOrders);
	};
}
