// Port of Unity.SpaceGod Assets/UnitySpace/Core/Information/News.cs (see docs/MIGRATION.md).

#include "Information/News.h"

#include "BodyDefinition.h"
#include "Combat/BioMass.h" // US-T262
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Combat/War.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Information/LightSpeed.h"
#include "Ships.h"
#include "Systems/DeepSpace.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/JumpGate.h"
#include "Systems/Polities.h"

namespace SpaceGod::Core
{
	const char* ToString(NewsCarrier v)
	{
		switch (v)
		{
			case NewsCarrier::Relay: return "Relay";
			case NewsCarrier::Ship: return "Ship";
			case NewsCarrier::Charts: return "Charts";
			case NewsCarrier::DeepSpaceRelay: return "DeepSpaceRelay";
		}
		return "";
	}

	bool GateState::Down() const { return 1 - Damage < JumpGate::OperationalIntegrity; }

	// ---------------------------------------------------------------- SystemReport

	bool SystemReport::AtWar(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) const
	{
		if (a == nullptr || b == nullptr || a == b) return false;
		for (const auto& w : Wars)
			if (w.Involves(a) && w.Involves(b)) return true;
		return false;
	}

	cs::Ref<Settlement> SystemReport::Market(const cs::String& name) const
	{
		for (const auto& s : Markets)
			if (s->Name == name) return s;
		return nullptr;
	}

	cs::Ref<SystemReport> SystemReport::Delivered(double receivedJd, NewsCarrier carrier, const cs::String& carriedBy) const
	{
		auto r = cs::New<SystemReport>();
		r->Source = Source;
		r->AsOfJd = AsOfJd;
		r->ReceivedJd = receivedJd;
		r->Carrier = carrier;
		r->CarriedBy = carriedBy;
		r->Markets.AddRange(Markets);
		r->Headlines.AddRange(Headlines);
		r->Holder = Holder;
		r->Gates.AddRange(Gates);
		r->PortThreats.AddRange(PortThreats);
		r->Wars.AddRange(Wars);
		r->PirateShips = PirateShips;
		r->Havens = Havens;
		r->BioGlobules = BioGlobules;
		return r;
	}

	cs::Ref<SystemReport> SystemReport::Take(const cs::Ref<StarSystem>& s, double jd)
	{
		auto r = cs::New<SystemReport>();
		r->Source = s;
		r->AsOfJd = jd;
		r->ReceivedJd = jd;
		for (const auto& st : s->World->Economy->Settlements)
			if (!st->Hidden && st->Kind != SettlementKind::Haven) r->Markets.Add(st->Snapshot());
		const auto& events = s->World->Events;
		for (int i = cs::Math::Max(0, events.Count() - 40); i < events.Count(); i++)
		{
			const auto& e = events[i];
			if (e.Jd <= jd && Notable(e.Kind) && !cs::Strings::StartsWith(e.Text, ForeignPrefix)) r->Headlines.Add(e);
		}
		TakeSituation(r, s, jd);
		return r;
	}

	void SystemReport::TakeSituation(const cs::Ref<SystemReport>& r, const cs::Ref<StarSystem>& s, double /*jd*/)
	{
		auto world = s->World;
		r->Holder = world->Holder();
		for (const auto& g : s->Gates())
		{
			GateState state;
			state.Gate = g;
			state.Owner = g->Owner;
			state.BlockadeUntilJd = g->BlockadeUntilJd;
			state.Damage = 1 - g->Integrity;
			r->Gates.Add(state);
		}
		for (const auto& st : world->Economy->Settlements)
		{
			if (st->Kind == SettlementKind::Haven)
			{
				if (!st->Hidden && !st->Fallen) r->Havens++;
				continue;
			}
			if (st->Hidden || st->Fallen) continue;
			auto blockader = world->Diplomacy()->BlockadedBy(st);
			auto besieger = world->War ? world->War->Besieger(st) : nullptr;
			if (blockader != nullptr || besieger != nullptr)
			{
				PortThreat threat;
				threat.Port = st->Name;
				threat.Owner = st->Owner;
				threat.BlockadedBy = blockader;
				threat.BesiegedBy = besieger;
				r->PortThreats.Add(threat);
			}
		}
		for (const auto& ship : world->Fleet->Ships())
			if (!ship->Destroyed && !ship->IsEscort() && !ship->Derelict && ship->Faction != nullptr && ship->Faction->IsPirate && ship->Combat != nullptr && ship->Combat->Armed())
				r->PirateShips++;
		if (world->BioMass != nullptr) // US-T262: the blooms loose there, for the galaxy map's danger mode
			for (const auto& g : world->BioMass->Globules)
				if (g->Alive() && cs::Double::IsNaN(g->ExitedJd)) r->BioGlobules++;
		const auto& factions = world->Polities->Factions();
		for (int i = 0; i < factions.Count(); i++)
			for (int j = i + 1; j < factions.Count(); j++)
			{
				auto a = factions[i];
				auto b = factions[j];
				if (!a->IsPirate && !b->IsPirate && world->Diplomacy()->Get(a, b) == Relation::War) r->Wars.Add(FactionPair(a, b));
			}
	}

	// ---------------------------------------------------------------- Newswire

	const cs::Ref<Faction>& Newswire::RelayTraffic()
	{
		static const cs::Ref<Faction> value = cs::New<Faction>("Gate relay", "RLY", Rgb());
		return value;
	}

	Newswire::Newswire(cs::Ref<Galaxy> galaxy, double jd)
	{
		this->galaxy = galaxy;
		nextRelayJd = jd + RelayIntervalDays;
		// Each system starts knowing the others as the last relay left them.
		for (const auto& from : galaxy->Systems)
		{
			if (!from->Settled()) continue;
			auto report = SystemReport::Take(from, jd);
			for (const auto& to : galaxy->Systems)
			{
				if (to == from) continue;
				double lag = RelayDays(from, to, jd);
				if (cs::Double::IsInfinity(lag)) continue;
				auto r = report->Delivered(jd, NewsCarrier::Charts, cs::String());
				r->AsOfJd = jd - lag;
				Book(to)[from] = r;
			}
		}
	}

	cs::Dictionary<cs::Ref<StarSystem>, cs::Ref<SystemReport>>& Newswire::Book(const cs::Ref<StarSystem>& at)
	{
		// C#: if (!known.TryGetValue(at, out var book)) known[at] = book = new Dictionary<...>(); the inner book is held by value.
		return known[at];
	}

	cs::Ref<SystemReport> Newswire::Known(const cs::Ref<StarSystem>& at, const cs::Ref<StarSystem>& about) const
	{
		if (at == nullptr || about == nullptr || at == about) return nullptr;
		if (!known.ContainsKey(at)) return nullptr;
		cs::Ref<SystemReport> r;
		return known[at].TryGetValue(about, r) ? r : nullptr;
	}

	cs::List<cs::Ref<SystemReport>> Newswire::KnownAt(const cs::Ref<StarSystem>& at) const
	{
		return known.ContainsKey(at) ? known[at].Values() : cs::List<cs::Ref<SystemReport>>();
	}

	double Newswire::InnerLightDays(const cs::Ref<StarSystem>& s)
	{
		double nearest = cs::Double::PositiveInfinity;
		for (const auto& st : s->World->Economy->Settlements)
		{
			if (st->Hidden || st->Kind == SettlementKind::Haven) continue;
			nearest = cs::Math::Min(nearest, s->Ephemeris->Helio(st->Body).Length());
		}
		return cs::Double::IsInfinity(nearest) ? 0 : LightSpeed::DelayDays(nearest);
	}

	double Newswire::RelayDays(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, double jd) const
	{
		bool discard = false;
		return RelayDays(from, to, jd, discard);
	}

	double Newswire::RelayDays(const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, double jd, bool& deepSpace) const
	{
		auto route = galaxy->Network ? galaxy->Network->Route(from, to, RelayTraffic(), jd) : nullptr;
		double gates = route == nullptr ? cs::Double::PositiveInfinity : GateNetwork::RouteDays(route);
		double relay = galaxy->DeepSpace ? galaxy->DeepSpace->RelayLagDays(from, to) : cs::Double::PositiveInfinity;
		deepSpace = relay < gates;
		double way = cs::Math::Min(gates, relay);
		if (cs::Double::IsInfinity(way)) return cs::Double::PositiveInfinity;
		return InnerLightDays(from) + way + InnerLightDays(to);
	}

	void Newswire::Update(double jd)
	{
		if (jd >= nextRelayJd)
		{
			// A long jump of the clock sends once, from now: the boards are as they are now, not as they were.
			Relay(jd);
			nextRelayJd = cs::Math::Max(nextRelayJd + RelayIntervalDays, jd + RelayIntervalDays * 0.5);
		}
		if (inFlight.Count() == 0) return;
		inFlight.Sort([](const std::tuple<cs::Ref<SystemReport>, cs::Ref<StarSystem>, double>& a, const std::tuple<cs::Ref<SystemReport>, cs::Ref<StarSystem>, double>& b)
		{
			return cs::CompareTo(std::get<2>(a), std::get<2>(b));
		});
		int done = 0;
		while (done < inFlight.Count() && std::get<2>(inFlight[done]) <= jd)
		{
			auto [r, to, at] = inFlight[done];
			Deliver(to, r, at);
			done++;
		}
		if (done > 0) inFlight.RemoveRange(0, done);
	}

	void Newswire::Relay(double jd)
	{
		for (const auto& from : galaxy->Systems)
		{
			if (!from->Settled()) continue;
			auto report = SystemReport::Take(from, jd);
			for (const auto& to : galaxy->Systems)
			{
				if (to == from) continue;
				bool deepSpace = false;
				double lag = RelayDays(from, to, jd, deepSpace);
				if (cs::Double::IsInfinity(lag)) continue;
				Send(report, to, jd + lag, deepSpace ? NewsCarrier::DeepSpaceRelay : NewsCarrier::Relay, cs::String());
			}
		}
	}

	void Newswire::Send(const cs::Ref<SystemReport>& report, const cs::Ref<StarSystem>& to, double arriveJd, NewsCarrier carrier, const cs::String& by)
	{
		inFlight.Add(std::make_tuple(report->Delivered(arriveJd, carrier, by), to, arriveJd));
	}

	cs::List<cs::Ref<SystemReport>> Newswire::Bundle(const cs::Ref<StarSystem>& from, double jd)
	{
		cs::List<cs::Ref<SystemReport>> bundle;
		if (from->Settled()) bundle.Add(SystemReport::Take(from, jd));
		bundle.AddRange(KnownAt(from));
		return bundle;
	}

	void Newswire::Carried(const cs::Ref<StarSystem>& to, const cs::List<cs::Ref<SystemReport>>& bundle, const cs::String& ship, double jd)
	{
		double arrive = jd + InnerLightDays(to);
		for (const auto& r : bundle)
			if (r->Source != to) Send(r, to, arrive, NewsCarrier::Ship, ship);
	}

	void Newswire::Deliver(const cs::Ref<StarSystem>& to, const cs::Ref<SystemReport>& r, double jd)
	{
		auto& book = Book(to);
		cs::Ref<SystemReport> had;
		book.TryGetValue(r->Source, had);
		if (had != nullptr && had->AsOfJd >= r->AsOfJd) return;
		book[r->Source] = r;
		Delivered++;
		// The newest few headlines since the last report, logged oldest first so the feed reads in order.
		double since = had ? had->AsOfJd : cs::Double::NegativeInfinity;
		int first = r->Headlines.Count();
		while (first > 0 && r->Headlines.Count() - first < HeadlinesPerReport && r->Headlines[first - 1].Jd > since) first--;
		for (int i = first; i < r->Headlines.Count(); i++)
		{
			const auto& e = r->Headlines[i];
			to->World->Log(jd, cs::Cat(SystemReport::ForeignPrefix, r->Source->Name, " (", LightSpeed::Describe(jd - e.Jd), " old, by ", CarrierText(r), "): ", e.Text), e.Kind, -1);
		}
		Received.Invoke(to, r);
	}

	cs::String Newswire::CarrierText(const cs::Ref<SystemReport>& r)
	{
		switch (r->Carrier)
		{
			// r.CarriedBy ?? "ship": a null C# string is the empty string here.
			case NewsCarrier::Ship: return !r->CarriedBy.empty() ? r->CarriedBy : cs::String("ship");
			case NewsCarrier::Charts: return "the charts";
			case NewsCarrier::DeepSpaceRelay: return "deep-space relay";
			default: return "gate relay";
		}
	}

	cs::String Newswire::Describe(const cs::Ref<StarSystem>& at, const cs::Ref<StarSystem>& about, double jd) const
	{
		auto r = Known(at, about);
		if (r == nullptr) return "no news";
		return cs::Cat("news ", LightSpeed::Describe(r->AgeDays(jd)), " old, by ", CarrierText(r),
		               (r->Markets.Count() > 0 ? cs::Cat(", ", cs::Str(r->Markets.Count()), " markets") : cs::String("")));
	}
}
