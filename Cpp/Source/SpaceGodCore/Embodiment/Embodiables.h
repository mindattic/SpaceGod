#pragma once
// Port of Unity.SpaceGod Core/Embodiment/Embodiables.cs (US-T21, see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Embodiment/Embodiment.h"

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class FighterSeat;
	class JumpGate;
	class MountedTurret;
	class Settlement;
	class Ship;
	class ShipCombat;
	class ShipHelm;
	class StationSeat;
	class World;

	/// <summary>
	/// Where the things the player can embody are found (U.SPC-T18): a picker lists from here rather than
	/// building its own lists, so a new kind registers once and every picker sees it.
	/// </summary>
	class SPACEGODCORE_API Embodiables
	{
	public:
		/// <summary>A ship's helm.</summary>
		static cs::Ref<ShipHelm> HelmOf(const cs::Ref<World>& world, const cs::Ref<Ship>& ship);

		/// <summary>The cockpit of a launched fighter or bomber (U.SPC-T422).</summary>
		static cs::Ref<FighterSeat> FighterOf(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& unit);

		/// <summary>
		/// Every gun mount on <paramref name="unit"/> the player could take right now (intact, a direct-fire
		/// gun), each with <paramref name="host"/> as what it is mounted on.
		/// </summary>
		static cs::List<cs::Ref<MountedTurret>> TurretsOn(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& unit, cs::Ref<IEmbodiable> host = nullptr);

		/// <summary>Every gun mount on a ship in a fight, its helm as their host.</summary>
		static cs::List<cs::Ref<MountedTurret>> TurretsOn(const cs::Ref<World>& world, const cs::Ref<Ship>& ship);

		/// <summary>
		/// Every gun mount on any battle unit (a ship's battery, a defence platform's, a fort's), hosted by the ship's
		/// helm when it is a ship and <paramref name="world"/> is given, and by the station when it is a platform or a fort.
		/// </summary>
		static cs::List<cs::Ref<MountedTurret>> TurretsOn(const cs::Ref<World>& world, const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& unit);

		// ------------------------------------------------------------ stations (U.SPC-T447)

		/// <summary>One of <paramref name="settlement"/>'s defence platforms, as a station the player can take.</summary>
		static cs::Ref<StationSeat> StationOf(const cs::Ref<World>& world, const cs::Ref<Settlement>& settlement, const cs::Ref<ShipCombat>& platform);

		/// <summary>One of the forts guarding <paramref name="gate"/>, as a station the player can take.</summary>
		static cs::Ref<StationSeat> FortOf(const cs::Ref<World>& world, const cs::Ref<JumpGate>& gate, const cs::Ref<ShipCombat>& fort);

		/// <summary>The station a platform or fort in a fight is, or null for any other unit.</summary>
		static cs::Ref<StationSeat> StationOf(const cs::Ref<World>& world, const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& unit);

		/// <summary>
		/// Every station at <paramref name="body"/> the player could take right now, in or out of a fight: each settlement's
		/// defence platforms there, and out at the system's root the forts of its jump gates.
		/// </summary>
		static cs::List<cs::Ref<StationSeat>> StationsAt(const cs::Ref<World>& world, int body);

		/// <summary>Every station in <paramref name="world"/> the player could take right now: every settlement's defence
		/// platforms, then every gate's forts.</summary>
		static cs::List<cs::Ref<StationSeat>> StationsIn(const cs::Ref<World>& world);

	private:
		static void Add(cs::List<cs::Ref<StationSeat>>& list, const cs::Ref<StationSeat>& s);

	public:
		/// <summary>
		/// Every seat in <paramref name="battle"/> the player could take right now, in the battle's own order (U.SPC-T422):
		/// the helm of each ship still fighting, and the cockpit of each fighter and bomber aloft. Gun mounts, many to a
		/// hull, are reached from their hull (<see cref="TurretsOn(World, Battle, BattleUnit)"/>), not listed here.
		/// </summary>
		static cs::List<cs::Ref<IEmbodiable>> SeatsIn(const cs::Ref<World>& world, const cs::Ref<Battle>& battle);

		/// <summary>
		/// The seat <paramref name="step"/> places on from <paramref name="current"/> in <paramref name="seats"/>, wrapping
		/// round (the first, or the last going back, when <paramref name="current"/> is not among them); null if there are none.
		/// </summary>
		/// (C#'s seats.IndexOf uses the seats' Equals: EmbodiableIndexOf. A null list is an empty one here.)
		static cs::Ref<IEmbodiable> Cycle(const cs::List<cs::Ref<IEmbodiable>>& seats, const cs::Ref<IEmbodiable>& current, int step);

		/// <summary>
		/// The picker's list (U.SPC-T446): every seat the player could take now on the thing in hand and around the camera,
		/// each once, in this order: the seat of <paramref name="onUnit"/> (or the helm of <paramref name="onShip"/> when it
		/// is not fighting) and every gun mount on it, then every other seat in <paramref name="battle"/> (the fight in view),
		/// then the helm of each of <paramref name="nearby"/> (ships near the camera, nearest first) not already listed. Built
		/// only from the finders above, so a new kind registered there (a station, a satellite) shows up here too.
		/// </summary>
		/// (C#'s null <paramref name="nearby"/> is an empty list here: the same, nothing to add.)
		static cs::List<cs::Ref<IEmbodiable>> Pickable(const cs::Ref<World>& world, const cs::Ref<Battle>& battle, cs::Ref<BattleUnit> onUnit = nullptr,
			const cs::Ref<Ship>& onShip = nullptr, const cs::List<cs::Ref<Ship>>& nearby = cs::List<cs::Ref<Ship>>());

		/// <summary>The seat that flies <paramref name="unit"/> itself (a ship's helm, a fighter's cockpit), or null for a
		/// unit nobody flies (a defence platform, a fort: their guns are reached through <see cref="TurretsOn(World, Battle, BattleUnit)"/>,
		/// and the station itself from any of them, or through <see cref="StationOf(World, Battle, BattleUnit)"/>).</summary>
		static cs::Ref<IEmbodiable> SeatOf(const cs::Ref<World>& world, const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& unit);
	};
}
