#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Events/EventSources.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

#include <cstdint>

namespace SpaceGod::Core
{
	class EscapePod;
	class Faction;
	class Lane;
	class Ship;
	class World;
	class Wreck;
	enum class PodState : int;

	/// <summary>
	/// What <see cref="EventDirector"/> reads from the sim to decide what adventures exist: the ships,
	/// the wrecks, how dangerous each lane is, and where things are. <see cref="Of"/> wires it to a live
	/// <see cref="World"/>; tests fill it in by hand, so the spawning rules are checked without an
	/// ephemeris or a whole fleet.
	/// </summary>
	// (The C# IReadOnlyLists alias the world's own lists; here they are copies taken when Of runs. The director
	// only reads them during one Update, and nothing it does changes which items it would act on.)
	class SPACEGODCORE_API EventSources final
	{
	public:
		cs::List<cs::Ref<Ship>> Ships;
		cs::List<cs::Ref<Wreck>> Wrecks;

		/// <summary>Escape pods drifting and broadcasting (see <see cref="Crews"/>).</summary>
		cs::List<cs::Ref<EscapePod>> Pods;

		/// <summary>Where a pod cluster is now, heliocentric km.</summary>
		cs::Func<DVec3, cs::Ref<EscapePod>> PodHelio = [](const cs::Ref<EscapePod>&) { return DVec3::Zero; };

		/// <summary>The player's ship takes the pods aboard; returns what became of their people (see <see cref="World.PickUpPods"/>).</summary>
		cs::Func<PodState, cs::Ref<EscapePod>, cs::Ref<Ship>> PickUpPod = &DefaultPickUpPod;

		/// <summary>Where a ship is now, heliocentric km.</summary>
		cs::Func<DVec3, cs::Ref<Ship>> ShipHelio = [](const cs::Ref<Ship>&) { return DVec3::Zero; };

		/// <summary>Where a wreck is now, heliocentric km.</summary>
		cs::Func<DVec3, cs::Ref<Wreck>> WreckHelio = [](const cs::Ref<Wreck>&) { return DVec3::Zero; };

		/// <summary>Where a body is now, heliocentric km.</summary>
		cs::Func<DVec3, int> BodyHelio = [](int) { return DVec3::Zero; };

		/// <summary>The body whose neighbourhood a point is in (anomalies are anchored in its frame).</summary>
		cs::Func<int, DVec3> NearestBody = [](DVec3) { return 0; };

		/// <summary>A body's name, for the text hooks.</summary>
		cs::Func<cs::String, int> BodyName = [](int b) { return cs::Cat("body ", b); };

		/// <summary>How dangerous a lane is: its own ambush record (<see cref="Hyperlanes.Danger"/>) plus
		/// the fighting at either end (<see cref="War.Danger"/>).</summary>
		cs::Func<double, cs::Ref<Lane>> LaneDanger = [](const cs::Ref<Lane>&) { return 0.0; };

		/// <summary>A ship that carries freight for a living (the ones that ask for escorts).</summary>
		cs::Func<bool, cs::Ref<Ship>> IsMerchant = &DefaultIsMerchant;

		/// <summary>Who pays for pirates dealt with (the Belt Consortium in Sol), or null for nobody.</summary>
		cs::Ref<Faction> PirateHunters;

		/// <summary>Whether a patron will offer the player its contracts (standing and bounties, see <see cref="Law.OffersContracts"/>).</summary>
		cs::Func<bool, cs::Ref<Faction>> WillHire = [](const cs::Ref<Faction>&) { return true; };

		/// <summary>The bounties the law has posted on a ship's captain for what they have actually done, credits.</summary>
		cs::Func<double, cs::Ref<Ship>> PostedBounty = [](const cs::Ref<Ship>&) { return 0.0; };

		/// <summary>How much of the fighting at a lane's two ends counts toward its danger.</summary>
		static constexpr double EndDangerWeight = 0.25;

		/// <summary>Everything a live <see cref="World"/> offers the director at <paramref name="jd"/>.</summary>
		static cs::Ref<EventSources> Of(const cs::Ref<World>& world, double jd);

	private:
		// The C# field initialisers' lambdas with bodies that need the full types (EventSources.cpp).
		static PodState DefaultPickUpPod(cs::Ref<EscapePod> p, cs::Ref<Ship> s);
		static bool DefaultIsMerchant(cs::Ref<Ship> s);
	};

	/// <summary>The names raider captains go by, fixed per hull so a bounty always names the same person.</summary>
	class SPACEGODCORE_API PirateCaptains
	{
		static const cs::List<cs::String>& First();

		static const cs::List<cs::String>& Last();

		static const cs::List<cs::String>& Epithet();

	public:
		/// <summary>The captain of <paramref name="ship"/>: the same name every time it is asked.</summary>
		static cs::String NameFor(const cs::Ref<Ship>& ship);

	private:
		static int StableHash(const cs::String& s);

		static uint32_t Hash(uint32_t x);
	};
}
