#include "Combat/PatrolWings.h"
#include "Combat/Corvettes.h"
#include "Combat/Facilities.h"
#include "Combat/Outfitting.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Systems/Polities.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"

// Ship.Route is a C# int[] that may be null: held here as a cs::List<int> by value, where null and empty read the same
// (NextOnBeat and OnBeat answer alike for both).

namespace SpaceGod::Core
{
	namespace
	{
		// double.CompareTo: NaN sorts below every number.
		int CompareDouble(double a, double b)
		{
			if (a < b) return -1;
			if (a > b) return 1;
			if (a == b) return 0;
			if (cs::Double::IsNaN(a)) return cs::Double::IsNaN(b) ? 0 : -1;
			return 1;
		}
	}

	const cs::List<cs::List<ShipClass>>& PatrolWings::Compositions()
	{
		static const cs::List<cs::List<ShipClass>> compositions =
		{
			{ ShipClass::Fighter, ShipClass::Fighter, ShipClass::Fighter },
			{ ShipClass::Gunship, ShipClass::Fighter, ShipClass::Fighter },
			{ ShipClass::Destroyer, ShipClass::Destroyer, ShipClass::Fighter, ShipClass::Fighter },
			{ ShipClass::Destroyer, ShipClass::Gunship, ShipClass::Fighter, ShipClass::Fighter },
		};
		return compositions;
	}

	const cs::List<cs::List<PatrolWings::Fitting>>& PatrolWings::Fittings()
	{
		static const cs::List<cs::List<Fitting>> fittings =
		{
			cs::List<Fitting>(),
			cs::List<Fitting>(),
			cs::List<Fitting>{ Fitting(1, FacilityKinds::FieldProjector) },
			cs::List<Fitting>(),
		};
		return fittings;
	}

	const cs::List<cs::String>& PatrolWings::CallSigns()
	{
		static const cs::List<cs::String> callSigns =
		{
			"Lamplighter", "Nighthawk", "Vigilant", "Redstart", "Sentinel", "Outrider", "Lookout", "Harfang", "Tripwire", "Nightwatch",
			"Gyrfalcon", "Warder", "Ranger", "Skirmisher", "Lanner", "Kingfisher", "Osprey", "Shrike", "Peregrine", "Hobby",
			"Goshawk", "Kite", "Sparrowhawk", "Falconet",
		};
		return callSigns;
	}

	const cs::List<cs::String>& PatrolWings::Wingmen()
	{
		static const cs::List<cs::String> wingmen = { "Two", "Three", "Four", "Five" };
		return wingmen;
	}

	const cs::List<cs::List<ShipClass>>& PatrolWings::ConvoyEscorts()
	{
		static const cs::List<cs::List<ShipClass>> escorts =
		{
			{ ShipClass::Fighter, ShipClass::Fighter },
			{ ShipClass::Gunship, ShipClass::Fighter },
		};
		return escorts;
	}

	const cs::List<cs::String>& PatrolWings::ConvoyNames()
	{
		static const cs::List<cs::String> names =
		{
			"Provender", "Quartermaster", "Victualler", "Commissary", "Replenisher", "Sutler", "Purveyor", "Storekeeper",
			"Bounty", "Larder", "Forager", "Steward",
		};
		return names;
	}

	int PatrolWings::ConvoysFor(int ports) { return ports < 2 ? 0 : cs::Math::Min(MaxConvoysPerFaction, ports / 2); }

	cs::List<ShipClass> PatrolWings::ConvoyEscort(int index) { return ConvoyEscorts()[index % ConvoyEscorts().Count()]; }

	int PatrolWings::WingsFor(int ports) { return ports < 2 ? 0 : cs::Math::Min(MaxWingsPerFaction, 1 + ports / 2); }

	cs::List<ShipClass> PatrolWings::Composition(int index) { return Compositions()[index % Compositions().Count()]; }

	const cs::List<PatrolWings::Fitting>& PatrolWings::FittingsOf(int index) { return Fittings()[index % Fittings().Count()]; }

	void PatrolWings::Fit(const cs::Ref<Ship>& ship, int index, int member)
	{
		for (const auto& [m, facility] : FittingsOf(index))
			if (m == member)
			{
				if (auto kind = FacilityKinds::Of(facility)) Outfitting::Install(ship, kind);
			}
	}

	cs::List<int> PatrolWings::Ports(const cs::Ref<World>& w, const cs::Ref<Faction>& f)
	{
		cs::List<cs::Ref<Settlement>> list;
		for (const auto& s : w->Economy->Settlements)
			if (s->Owner == f && !s->Fallen && s->Kind != SettlementKind::Haven) list.Add(s);
		list.Sort([](const cs::Ref<Settlement>& a, const cs::Ref<Settlement>& b) { return CompareDouble(b->Population, a->Population); });
		cs::List<int> bodies;
		for (const auto& s : list)
			if (!bodies.Contains(s->Body)) bodies.Add(s->Body);
		return bodies;
	}

	cs::List<int> PatrolWings::Beat(const cs::List<int>& ports, int index, int wings) { return Run(ports, index, wings, BeatLength); }

	cs::List<int> PatrolWings::ConvoyRun(const cs::List<int>& ports, int index, int convoys) { return Run(ports, index, convoys, RunLength); }

	cs::List<int> PatrolWings::Run(const cs::List<int>& ports, int index, int wings, int maxLength)
	{
		int n = ports.Count();
		int length = cs::Math::Min(maxLength, n);
		int start = static_cast<int>(static_cast<long long>(index) * n / cs::Math::Max(1, wings)) % n;
		auto beat = cs::NewArray<int>(length);
		for (int j = 0; j < length; j++) beat[j] = ports[(start + j) % n];
		return beat;
	}

	cs::List<cs::Ref<Ship>> PatrolWings::Raise(const cs::Ref<World>& w, double jd)
	{
		cs::List<cs::Ref<Ship>> leaders;
		cs::List<cs::Ref<Faction>> factions;
		factions.AddRange(w->Fleet->Factions());
		for (const auto& f : factions)
		{
			if (!f->IsMilitary || f->IsPirate) continue;
			auto ports = Ports(w, f);
			int wings = WingsFor(ports.Count());
			// Call signs run on across every system the navy holds, so no two of its wings share one.
			int sign = 0;
			for (const auto& s : w->Polities->Navy(f))
				if (s->PatrolWing && !s->IsEscort() && !s->Convoy && !Corvettes::Stationed(s)) sign++;
			for (int i = 0; i < wings; i++)
			{
				auto classes = Composition(i);
				cs::String call = cs::Cat(CallSigns()[sign % CallSigns().Count()], (sign >= CallSigns().Count() ? cs::Cat(" ", sign / CallSigns().Count() + 1) : cs::String()));
				sign++;
				auto leader = w->Fleet->AddShip(call, classes[0], f, ShipRole::Patrol);
				leader->PatrolWing = true;
				leader->Route = Beat(ports, i, wings);
				leader->RouteIndex = 0;
				Fit(leader, i, 0);
				for (int k = 1; k < classes.Count(); k++)
				{
					auto e = w->Fleet->AddShip(cs::Cat(call, " ", Wingmen()[(k - 1) % Wingmen().Count()]), classes[k], f, ShipRole::Escort);
					e->PatrolWing = true;
					Fit(e, i, k);
					w->Fleet->AddEscort(leader, e, Fleet::SlotFor(k - 1));
				}
				w->Fleet->Station(leader, leader->Route[0], jd);
				leaders.Add(leader);
			}
			// Its supply convoys: one of its freighters with naval stores round a run of its ports, under escort (U.SPC-T375).
			int convoys = ConvoysFor(ports.Count());
			int named = w->Polities->ConvoyCount(f);
			for (int i = 0; i < convoys; i++)
			{
				cs::String call = cs::Cat(ConvoyNames()[named % ConvoyNames().Count()], (named >= ConvoyNames().Count() ? cs::Cat(" ", named / ConvoyNames().Count() + 1) : cs::String()));
				named++;
				auto leader = w->Fleet->AddShip(call, ShipClass::Freighter, f, ShipRole::Patrol);
				leader->PatrolWing = true;
				leader->Convoy = true;
				leader->Route = ConvoyRun(ports, i, convoys);
				leader->RouteIndex = 0;
				leader->Cargo = "Naval stores";
				auto escort = ConvoyEscort(i);
				for (int k = 0; k < escort.Count(); k++)
				{
					auto e = w->Fleet->AddShip(cs::Cat(call, " ", Wingmen()[k % Wingmen().Count()]), escort[k], f, ShipRole::Escort);
					e->PatrolWing = true;
					w->Fleet->AddEscort(leader, e, Fleet::SlotFor(k));
				}
				w->Fleet->Station(leader, leader->Route[0], jd);
				leaders.Add(leader);
			}
		}
		// And its corvettes, stationed for good at its ports to police the lanes of the system (U.SPC-T476).
		Corvettes::Raise(w, jd);
		return leaders;
	}

	int PatrolWings::NextOnBeat(const cs::Ref<Ship>& leader, int here)
	{
		const auto& beat = leader->Route;
		if (beat.Count() == 0) return here;
		int i = beat.IndexOf(here);
		return i < 0 ? beat[0] : beat[(i + 1) % beat.Count()];
	}

	bool PatrolWings::OnBeat(const cs::Ref<Ship>& leader, int body) { return leader->Route.IndexOf(body) >= 0; }
}
