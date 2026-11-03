#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Propulsion.cs (US-T19).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class Ship;
	enum class Good : int;

	/// <summary>
	/// What a trip is expected to take, for a trader pricing it: days under way (queues and landing
	/// included), the delta-v the ship's own drive must supply (the burns to, between and from the
	/// hyperlane ramps, or the one burn of a local hop) and how many lanes it rides (each one tolled).
	/// </summary>
	struct TripEstimate
	{
		double Days = 0;
		double DeltaVKmS = 0;
		int LaneHops = 0;

		TripEstimate() = default;
		TripEstimate(double days, double deltaVKmS, int laneHops)
		{
			Days = days;
			DeltaVKmS = deltaVKmS;
			LaneHops = laneHops;
		}
	};

	/// <summary>
	/// Reaction mass. Every hull carries a tank of propellant (its design's <see cref="CombatSpec.PropellantTonnes"/>)
	/// and a fusion drive that throws it out at <see cref="CombatSpec.ExhaustVelocityKmS"/>: the rocket
	/// equation (Tsiolkovsky) then fixes what every manoeuvre costs, heavier for a full hold than an empty
	/// one. Reaction mass is water or hydrocarbons (whichever the port sells cheaper), heated by a D–He-3
	/// burn that takes <see cref="HeliumThreePerTonne"/> of helium-3 with every tonne. Hyperlane rides cost
	/// no propellant (the lane carries the ship) but are tolled.
	/// </summary>
	class Propulsion
	{
	public:
		/// <summary>Helium-3 the fusion burn takes per tonne of reaction mass thrown out (0.5 kg).</summary>
		static constexpr double HeliumThreePerTonne = 0.0005;

		/// <summary>A planner keeps this much more than a trip needs in the tanks before it sets out.</summary>
		static constexpr double PlanningReserve = 0.25;

		/// <summary>A ship tops up when its tanks are below this share of full (not for every last tonne).</summary>
		static constexpr double TopUpBelow = 0.97;

		/// <summary>The goods a drive can use as reaction mass.</summary>
		static const cs::List<Good>& ReactionMass();

		/// <summary>Days an emergency tanker takes to reach a ship that has run dry (plus up to as much again).</summary>
		static constexpr double TankerDays = 1.5;

		/// <summary>m0 / mf for a burn of <paramref name="deltaVKmS"/> at exhaust velocity <paramref name="exhaustKmS"/>.</summary>
		static double MassRatio(double deltaVKmS, double exhaustKmS);

		/// <summary>The delta-v (km/s) a drive gets from burning a ship of <paramref name="wetTonnes"/> down to <paramref name="dryTonnes"/>.</summary>
		static double DeltaV(double exhaustKmS, double wetTonnes, double dryTonnes);

		/// <summary>Propellant (t) a burn of <paramref name="deltaVKmS"/> throws out of a ship that starts it at <paramref name="startTonnes"/>.</summary>
		static double PropellantBurned(double deltaVKmS, double exhaustKmS, double startTonnes);

		/// <summary>
		/// The delta-v (km/s) of a flip-and-burn over <paramref name="distanceKm"/> from rest to rest at a
		/// constant <paramref name="accelKmS2"/>: accelerate half way, brake the other half, 2·√(a·d).
		/// </summary>
		static double BrachistochroneDeltaV(double accelKmS2, double distanceKm);

		/// <summary>
		/// The delta-v (km/s) of a planned transit: the flip-and-burn (its acceleration for the whole flight)
		/// plus the steady push that takes the ship from its departure velocity to its arrival velocity and
		/// what holding its line against gravity costs; or, on a transfer orbit, its escape and capture burns.
		/// </summary>
		static double TransitDeltaV(const cs::Ref<Ship>& s);

		/// <summary>Propellant (t) <paramref name="ship"/> as it is now (hold and tanks) burns for <paramref name="deltaVKmS"/>.</summary>
		static double PropellantFor(const cs::Ref<Ship>& ship, double deltaVKmS);

		/// <summary>
		/// Propellant (t) a trip of <paramref name="deltaVKmS"/> takes with <paramref name="cargoTonnes"/> aboard,
		/// setting out with <paramref name="propellantTonnes"/> in the tanks.
		/// </summary>
		static double PropellantFor(const cs::Ref<Ship>& ship, double deltaVKmS, double cargoTonnes, double propellantTonnes);

		/// <summary>
		/// Flies a manoeuvre on the ship's own tanks: burns what the change of velocity
		/// <paramref name="deltaVKmS"/> costs, or as much of it as is left. Returns the share of the
		/// manoeuvre the propellant covered (1 for all of it, 0 for a dry ship).
		/// </summary>
		static double Spend(const cs::Ref<Ship>& ship, double deltaVKmS);

		/// <summary>
		/// One frame of hand-flown thrust on the ship's tanks: the pilot's controls moved the velocity from
		/// <paramref name="velBefore"/> to <paramref name="vel"/> (km/s) and the position on from
		/// <paramref name="posBefore"/> over <paramref name="dt"/> seconds. The change is paid for in
		/// propellant; a ship that cannot pay for it (dry, or running dry part way) gets only what its tanks
		/// covered and coasts on with the rest, so a dry hull drifts on the velocity it had.
		/// </summary>
		static void ApplyManoeuvre(const cs::Ref<Ship>& ship, DVec3& pos, DVec3& vel, DVec3 posBefore, DVec3 velBefore, double dt);

		/// <summary>What an emergency tanker brings a ship that has run dry: enough to fill its tanks.</summary>
		static double TankerLoad(const cs::Ref<Ship>& ship);

		/// <summary>What the tanker charges for coming out with <paramref name="tonnes"/>: a call-out fee, then three times the going rate.</summary>
		static double TankerFee(double tonnes);

		/// <summary>Base price of a tonne of propellant: a tonne of water and its helium-3.</summary>
		static double PropellantBasePrice();

		/// <summary>
		/// Gives <paramref name="to"/> up to <paramref name="tonnes"/> from <paramref name="from"/>'s tanks
		/// (a pilot answering a dry ship's call), as much as both can spare and hold. Returns what moved.
		/// </summary>
		static double Transfer(const cs::Ref<Ship>& from, const cs::Ref<Ship>& to, double tonnes);
	};
}
