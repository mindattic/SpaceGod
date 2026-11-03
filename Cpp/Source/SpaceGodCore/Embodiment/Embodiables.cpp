// Port of Unity.SpaceGod Core/Embodiment/Embodiables.cs (US-T21, see docs/MIGRATION.md).

#include "Embodiment/Embodiables.h"
#include "Embodiment/FighterSeat.h"
#include "Embodiment/MountedTurret.h"
#include "Embodiment/ShipHelm.h"
#include "Embodiment/StationSeat.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ships.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Systems/JumpGate.h"

namespace SpaceGod::Core
{
	cs::Ref<ShipHelm> Embodiables::HelmOf(const cs::Ref<World>& world, const cs::Ref<Ship>& ship) { return cs::New<ShipHelm>(world, ship); }

	cs::Ref<FighterSeat> Embodiables::FighterOf(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& unit) { return cs::New<FighterSeat>(battle, unit); }

	cs::List<cs::Ref<MountedTurret>> Embodiables::TurretsOn(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& unit, cs::Ref<IEmbodiable> host)
	{
		cs::List<cs::Ref<MountedTurret>> list;
		if (battle == nullptr || unit == nullptr) return list;
		// A station's guns are mounted on the station (U.SPC-T447): a gun seat there moves up to the whole of it.
		if (!host) host = StationOf(nullptr, battle, unit);
		for (const auto& s : unit->Combat->Subsystems)
		{
			auto t = cs::New<MountedTurret>(battle, unit, s, host);
			if (t->CanEmbody()) list.Add(t);
		}
		return list;
	}

	cs::List<cs::Ref<MountedTurret>> Embodiables::TurretsOn(const cs::Ref<World>& world, const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr || ship->Battle == nullptr) return cs::List<cs::Ref<MountedTurret>>();
		// (C# evaluates the arguments left to right: UnitOf before HelmOf.)
		auto unit = ship->Battle->UnitOf(ship);
		return TurretsOn(ship->Battle, unit, HelmOf(world, ship));
	}

	cs::List<cs::Ref<MountedTurret>> Embodiables::TurretsOn(const cs::Ref<World>& world, const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& unit)
	{
		cs::Ref<IEmbodiable> host = unit != nullptr && unit->Ship != nullptr && world != nullptr
			? cs::Ref<IEmbodiable>(HelmOf(world, unit->Ship))
			: cs::Ref<IEmbodiable>(StationOf(world, battle, unit));
		return TurretsOn(battle, unit, host);
	}

	// ------------------------------------------------------------ stations (U.SPC-T447)

	cs::Ref<StationSeat> Embodiables::StationOf(const cs::Ref<World>& world, const cs::Ref<Settlement>& settlement, const cs::Ref<ShipCombat>& platform)
	{
		return cs::New<StationSeat>(world, settlement, nullptr, platform);
	}

	cs::Ref<StationSeat> Embodiables::FortOf(const cs::Ref<World>& world, const cs::Ref<JumpGate>& gate, const cs::Ref<ShipCombat>& fort)
	{
		return cs::New<StationSeat>(world, nullptr, gate, fort);
	}

	cs::Ref<StationSeat> Embodiables::StationOf(const cs::Ref<World>& world, const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& unit)
	{
		if (unit == nullptr || unit->Kind != UnitKind::Platform || (unit->Platform == nullptr && unit->Fort == nullptr)) return nullptr;
		return cs::New<StationSeat>(world, unit->Platform, unit->Fort, unit->Combat, battle);
	}

	cs::List<cs::Ref<StationSeat>> Embodiables::StationsAt(const cs::Ref<World>& world, int body)
	{
		cs::List<cs::Ref<StationSeat>> list;
		if (world == nullptr) return list;
		for (const auto& st : world->Economy->At(body))
			for (const auto& p : st->Platforms)
				Add(list, StationOf(world, st, p));
		if (body == world->Catalog()->Root()->Index)
			for (const auto& g : world->Gates)
				for (const auto& f : g->Forts)
					Add(list, FortOf(world, g, f));
		return list;
	}

	cs::List<cs::Ref<StationSeat>> Embodiables::StationsIn(const cs::Ref<World>& world)
	{
		cs::List<cs::Ref<StationSeat>> list;
		if (world == nullptr) return list;
		for (const auto& st : world->Economy->Settlements)
			for (const auto& p : st->Platforms)
				Add(list, StationOf(world, st, p));
		for (const auto& g : world->Gates)
			for (const auto& f : g->Forts)
				Add(list, FortOf(world, g, f));
		return list;
	}

	void Embodiables::Add(cs::List<cs::Ref<StationSeat>>& list, const cs::Ref<StationSeat>& s)
	{
		if (s->CanEmbody()) list.Add(s);
	}

	cs::List<cs::Ref<IEmbodiable>> Embodiables::SeatsIn(const cs::Ref<World>& world, const cs::Ref<Battle>& battle)
	{
		cs::List<cs::Ref<IEmbodiable>> list;
		if (battle == nullptr || battle->Over) return list;
		for (const auto& u : battle->Units)
		{
			auto seat = SeatOf(world, battle, u);
			if (seat != nullptr && seat->CanEmbody()) list.Add(seat);
		}
		return list;
	}

	cs::Ref<IEmbodiable> Embodiables::Cycle(const cs::List<cs::Ref<IEmbodiable>>& seats, const cs::Ref<IEmbodiable>& current, int step)
	{
		if (seats.Count() == 0) return nullptr;
		int at = current == nullptr ? -1 : EmbodiableIndexOf(seats, current);
		if (at < 0) return step >= 0 ? seats[0] : seats[seats.Count() - 1];
		int n = seats.Count();
		return seats[((at + step) % n + n) % n];
	}

	cs::List<cs::Ref<IEmbodiable>> Embodiables::Pickable(const cs::Ref<World>& world, const cs::Ref<Battle>& battle, cs::Ref<BattleUnit> onUnit,
		const cs::Ref<Ship>& onShip, const cs::List<cs::Ref<Ship>>& nearby)
	{
		cs::List<cs::Ref<IEmbodiable>> list;
		auto Add = [&](const cs::Ref<IEmbodiable>& e)
		{
			// C#: list.Contains(e), by the seats' Equals.
			if (e != nullptr && e->CanEmbody() && EmbodiableIndexOf(list, e) < 0) list.Add(e);
		};

		if (onUnit == nullptr && onShip != nullptr && onShip->Battle != nullptr) onUnit = onShip->Battle->UnitOf(onShip);
		if (onUnit != nullptr)
		{
			// C#: onUnit.Ship?.Battle ?? battle
			auto unitBattle = onUnit->Ship != nullptr && onUnit->Ship->Battle != nullptr ? onUnit->Ship->Battle : battle;
			if (unitBattle != nullptr && !unitBattle->Over)
			{
				Add(SeatOf(world, unitBattle, onUnit));
				for (const auto& gun : TurretsOn(world, unitBattle, onUnit)) Add(gun);
			}
		}
		else if (onShip != nullptr && world != nullptr) Add(HelmOf(world, onShip));
		for (const auto& s : SeatsIn(world, battle)) Add(s);
		if (world != nullptr)
			for (const auto& ship : nearby)
				if (ship != nullptr) Add(HelmOf(world, ship));
		return list;
	}

	cs::Ref<IEmbodiable> Embodiables::SeatOf(const cs::Ref<World>& world, const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& unit)
	{
		if (unit == nullptr) return nullptr;
		if (unit->Kind == UnitKind::Fighter) return FighterOf(battle, unit);
		if (unit->Ship != nullptr && world != nullptr) return HelmOf(world, unit->Ship);
		return nullptr;
	}
}
