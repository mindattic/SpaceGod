#pragma once
// Port of Unity.SpaceGod Runtime/Views/EmbeddedPods.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class Battle;
	class Embedded;
	class Missile;
	class Faction;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// Boarding pods and Breacher rounds (U.SPC-T466) stuck in hulls. Each is parented to the model of the ship (or platform) it holed,
	/// drill buried and tail standing out of the plating, so it rides along as the hull manoeuvres,
	/// and it stays there after the fight until the ship is lost or the battle is long over. Breacher rounds still in flight,
	/// or tumbling adrift after a miss, are drawn here too, as their own round model (U.SPC-T517): a Breacher is a fired
	/// round, never a ship, so it has no ship model of its own.
	/// </summary>
	class EmbeddedPods final
	{
		UnitySpaceApp* app;
		cs::Ref<Transform> root;
		cs::Dictionary<cs::Ref<Core::Embedded>, cs::Ref<GameObject>> pods;
		cs::Dictionary<cs::Ref<Core::Missile>, cs::Ref<GameObject>> rounds;

	public:
		EmbeddedPods(UnitySpaceApp* app, const cs::Ref<Transform>& root);

		void Update();

	private:
		void Track(const cs::Ref<Core::Battle>& b, cs::HashSet<cs::Ref<Core::Embedded>>& live);

		/// <summary>Every Breacher round of <paramref name="b"/> in space: its model at the round, nose along its track, or
		/// tumbling end over end once it has gone adrift (U.SPC-T519).</summary>
		void TrackRounds(const cs::Ref<Core::Battle>& b, cs::HashSet<cs::Ref<Core::Missile>>& flying);

		cs::Ref<GameObject> CreateRound(const cs::Ref<Core::Faction>& owner);

		cs::Ref<GameObject> Create(const cs::Ref<Core::Embedded>& e, const cs::Ref<Transform>& parent, const cs::Ref<Core::Battle>& b);
	};
}
