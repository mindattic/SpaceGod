// Port of Unity.SpaceGod Core/World.MiningConvoys.cs (US-T8): part of the partial class World (World.h).

#include "World.h"

#include "Fleet.h"
#include "Economy/BeltMining.h"
#include "Economy/Economy.h"

namespace SpaceGod::Core
{
	namespace
	{
		using FleetT = ::SpaceGod::Core::Fleet;
		using EconomyT = ::SpaceGod::Core::Economy;
	}

	// ---------------------------------------------------------------- the raw-material convoys' freighters (U.SPC-T523)

	const cs::List<cs::String>& World::WorksFreighterNames()
	{
		static const cs::List<cs::String> names = { "Ore Wain", "Slag Queen", "Belt Drayman", "Iron Carter", "Lodestone", "Bulk Venture", "Smelter's Due", "Heavy Mercy" };
		return names;
	}

	const cs::List<ShipClass>& World::WorksEscortClasses()
	{
		static const cs::List<ShipClass> classes = { ShipClass::Gunship, ShipClass::Fighter };
		return classes;
	}

	cs::List<cs::Ref<Ship>> World::RaiseFreighters(cs::Ref<MiningOperation> op, double jd)
	{
		if (!op->Freighters) op->Freighters = cs::New<cs::List<cs::Ref<Ship>>>();
		cs::List<cs::Ref<Ship>> made;
		int home = op->RefineryBody, to = op->Destination ? op->Destination->Body : op->RefineryBody;
		while (op->Freighters->Count() < op->ConvoyFreighters)
		{
			cs::String name = WorksFreighterNames()[worksFreightersNamed % WorksFreighterNames().Count()] +
				(worksFreightersNamed >= WorksFreighterNames().Count() ? cs::Cat(" ", worksFreightersNamed / WorksFreighterNames().Count() + 1) : cs::String());
			worksFreightersNamed++;
			auto ship = Fleet->AddShip(name, ShipClass::Freighter, op->Owner, ShipRole::Hauler);
			ship->Ports = home == to ? cs::List<int>{ home } : cs::List<int>{ home, to };
			ship->CapacityTonnes = EconomyT::CapacityOf(ShipClass::Freighter);
			ship->Cargo = "Empty, waiting on the separators";
			Fleet->Station(ship, home, jd);
			for (int i = 0; i < op->EscortsPerFreighter(); i++)
			{
				auto e = Fleet->AddShip(cs::Cat(name, " escort ", i + 1), WorksEscortClasses()[i % WorksEscortClasses().Count()], op->Owner, ShipRole::Escort);
				Fleet->AddEscort(ship, e, FleetT::SlotFor(i));
			}
			op->Freighters->Add(ship);
			made.Add(ship);
		}
		op->ConvoyFreighters = op->Freighters->Count();
		return made;
	}

	int World::ReplaceWorksEscorts(double /*jd*/)
	{
		if (Mining == nullptr) return 0;
		int built = 0;
		for (const auto& op : Mining->Operations)
		{
			if (op->Freighters == nullptr || op->EscortsPerFreighter() <= 0) continue;
			for (const auto& f : *op->Freighters)
			{
				if (f->Destroyed || f->Derelict || f->HasCargo || f->Phase != ShipPhase::Parked || f->Orbit == nullptr || f->Orbit->Body != op->RefineryBody) continue;
				if (!FreighterFree(f)) continue;
				int have = 0;
				for (const auto& s : Fleet->Ships())
					if (s->Leader == f && !s->Destroyed && !s->Derelict && s->Faction == op->Owner) have++;
				for (; have < op->EscortsPerFreighter(); have++)
				{
					auto e = Fleet->AddShip(cs::Cat(f->Name, " escort ", ++worksEscortsRaised), WorksEscortClasses()[have % WorksEscortClasses().Count()], op->Owner, ShipRole::Escort);
					Fleet->AddEscort(f, e, FleetT::SlotFor(have));
					built++;
				}
			}
		}
		return built;
	}

	bool World::FreighterFree(cs::Ref<Ship> freighter) { return freighter != PilotedShip && freighter->Battle == nullptr && (!Fleet->IsHeld || !Fleet->IsHeld(freighter)); }

	void World::FreighterSailed(cs::Ref<MaterialConvoy> convoy, double jd)
	{
		auto f = convoy->Freighter;
		double now = cs::Double::IsNaN(Jd) ? jd : cs::Math::Max(jd, Jd);
		f->Cargo = cs::Cat(cs::Fmt(convoy->Tonnes(), "N0"), " t of separated elements for the ", convoy->To->Name);
		Fleet->ScheduleNext(f, now);
	}

	void World::FreighterArrived(cs::Ref<Ship> freighter, double jd)
	{
		if (Mining) Mining->FreighterArrived(freighter, jd);
	}

	void World::FreighterDelivered(cs::Ref<MaterialConvoy> convoy, double jd)
	{
		auto f = convoy->Freighter;
		f->Cargo = "Empty, back to the refinery";
		Log(jd, cs::Cat("The freighter ", f->Name, " brings the ", convoy->Name, " in to the ", convoy->To->Name, ": ", cs::Fmt(convoy->Tonnes(), "N0"), " t of raw materials"),
			WorldEventKind::Economy, convoy->To->Body);
	}
}
