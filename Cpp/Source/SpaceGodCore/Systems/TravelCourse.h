#pragma once
// Port of Assets/UnitySpace/Core/Systems/TravelCourse.cs (US-T17).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class JumpGate;
	class StarSystem;
	class GateNetwork;
	class Faction;

	/// <summary>
	/// A planned trip over the jump gates (U.SPC-T516): the gates to take in order (<see cref="GateNetwork.Route"/>), one per
	/// leg, never a jump straight to the far system. Each leg is flown across the system the ship is in to that
	/// system's gate for the next link, then through it; <see cref="Passed"/> moves on to the next leg (or replans if the
	/// ship went through some other gate) and <see cref="Next"/> is the gate the HUD and map point at.
	/// </summary>
	class TravelCourse final
	{
		// The planner's own list (GateNetwork.Route's), held by reference as in the C#.
		cs::Ref<cs::List<cs::Ref<JumpGate>>> gates;
		int index = 0;

	public:
		cs::Ref<StarSystem> From;
		cs::Ref<StarSystem> Destination;

		/// <summary>Every gate on the course, in order, the ones already taken included.</summary>
		const cs::List<cs::Ref<JumpGate>>& Gates() const { return *gates; }

		/// <summary>How many gates have been taken so far.</summary>
		int Taken() const { return index; }

		int Legs() const { return gates->Count(); }
		bool Done() const { return index >= gates->Count(); }

		/// <summary>The gate to fly to and go through next, or null when the course is run.</summary>
		cs::Ref<JumpGate> Next() const { return index < gates->Count() ? (*gates)[index] : nullptr; }

		/// <summary>The system the next gate is in: the one the ship should be crossing now.</summary>
		cs::Ref<StarSystem> Here() const;

		/// <summary>The system the next gate leads to.</summary>
		cs::Ref<StarSystem> NextSystem() const;

		// private in C#; public so cs::New can make one (only Plan does).
		TravelCourse(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& destination, const cs::Ref<cs::List<cs::Ref<JumpGate>>>& route);

		/// <summary>
		/// Plans the quickest way from <paramref name="from"/> to <paramref name="to"/> for <paramref name="traveller"/>
		/// (null: any working gate, the view's own way). Null when there is none or the two are the same system.
		/// </summary>
		static cs::Ref<TravelCourse> Plan(const cs::Ref<GateNetwork>& network, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, const cs::Ref<Faction>& traveller = nullptr, double jd = cs::Double::NegativeInfinity);

		/// <summary>
		/// Records that the ship has gone through <paramref name="gate"/>. On course it moves to the next leg; through any
		/// other gate the course is replanned from where the ship now is (<paramref name="network"/>). Returns false when the
		/// course is over (arrived, or no way on from here).
		/// </summary>
		bool Passed(const cs::Ref<JumpGate>& gate, const cs::Ref<GateNetwork>& network, const cs::Ref<Faction>& traveller, double jd);

		/// <summary>Brings the course up to a hull that has taken <paramref name="taken"/> gates of it (a crew flying it by its voyage).</summary>
		void SyncTo(int taken) { index = cs::Math::Max(index, cs::Math::Min(taken, gates->Count())); }

		/// <summary>The course in a line: the systems in order, the next leg marked.</summary>
		cs::String Describe() const;
	};
}
