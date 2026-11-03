// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/MarketShocks.cs (US-T19).

#include "Economy/MarketShocks.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Diplomacy.h"
#include "Combat/War.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Industry.h"
#include "Economy/Settlement.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	const char* ToString(ShockKind v)
	{
		switch (v)
		{
			case ShockKind::MineAccident: return "MineAccident";
			case ShockKind::Depletion: return "Depletion";
			case ShockKind::Blight: return "Blight";
			case ShockKind::Strike: return "Strike";
			case ShockKind::Rationing: return "Rationing";
			case ShockKind::Blockade: return "Blockade";
			case ShockKind::Embargo: return "Embargo";
		}
		return "";
	}

	MarketShocks::MarketShocks(cs::Ref<World> world, int seed)
		: world(world), rng(seed)
	{
		// C# `world.Economy.Sold += OnSold;` on a plain delegate field: combine, the earlier handler first.
		auto previous = world->Economy->Sold;
		world->Economy->Sold = [previous, this](cs::Ref<Ship> ship, cs::Ref<Settlement> port, Good good, double tonnes)
		{
			if (previous) previous(ship, port, good, tonnes);
			OnSold(ship, port, good, tonnes);
		};
	}

	cs::Ref<::SpaceGod::Core::Economy> MarketShocks::Economy() const { return world->Economy; }
	cs::Ref<::SpaceGod::Core::Diplomacy> MarketShocks::Diplomacy() const { return world->Diplomacy(); }

	cs::List<cs::Ref<EconomicShock>> MarketShocks::At(const cs::Ref<Settlement>& s) const
	{
		cs::List<cs::Ref<EconomicShock>> result;
		for (const auto& e : Active)
			if (e->Settlement == s || ((e->Kind == ShockKind::Rationing || e->Kind == ShockKind::Embargo) && e->Faction == s->Owner)) result.Add(e);
		return result;
	}

	void MarketShocks::Update(double jd)
	{
		if (world->War == nullptr) return;
		if (cs::Double::IsNaN(nextDailyJd)) nextDailyJd = firstJd = jd;
		while (jd >= nextDailyJd)
		{
			Daily(nextDailyJd);
			nextDailyJd += 1;
		}
	}

	void MarketShocks::Daily(double jd)
	{
		EndExpired(jd);
		for (const auto& s : Economy()->Settlements)
		{
			if (s->Fallen || s->Kind == SettlementKind::Haven || s->Owner->IsPirate) continue;
			for (const auto& f : s->Facilities)
			{
				if (f->Recipe->Kind == IndustryKind::Mine) Mine(s, f, jd);
				else if (f->Recipe->Kind == IndustryKind::Farm) Farm(s, f, jd);
			}
			Wages(s, jd);
		}
		Rationing(jd);
		Blockades(jd);
		Embargoes(jd);
		PriceSpikes(jd);
	}

	// ---------------------------------------------------------------- mines

	bool MarketShocks::IsOre(const cs::Ref<::SpaceGod::Core::Facility>& f) { return GoodInfo::Of(std::get<0>(f->Recipe->Outputs[0]))->Category == GoodCategory::Mineral; }

	void MarketShocks::Mine(const cs::Ref<Settlement>& s, const cs::Ref<::SpaceGod::Core::Facility>& f, double jd)
	{
		auto ore = std::get<0>(f->Recipe->Outputs[0]);
		double perDay = std::get<1>(f->Recipe->Outputs[0]);
		if (IsOre(f))
		{
			// A seam holds a few years of output at the mine's base rate (the ice of a moon, its gas, never runs out).
			if (f->Reserve <= 0)
			{
				surveyedLevel[f] = f->BaseLevel;
				f->Reserve = perDay * f->BaseLevel * (900 + rng.NextDouble() * 2700);
				f->Extracted = 0;
			}
			if (f->Extracted >= f->Reserve) Deplete(s, f, ore, jd);
		}

		if (f->Impairment < 1) return;
		// Pushed past its base level and short of machinery for upkeep, a mine gets dangerous.
		double push = cs::Math::Max(0.5, f->Level / cs::Math::Max(f->BaseLevel, 1e-9));
		double machinery = s->Target(Good::Machinery) > 0 ? cs::Math::Min(1.0, s->Stock[static_cast<int>(Good::Machinery)] / s->Target(Good::Machinery)) : 1;
		double risk = AccidentPerDay * cs::Math::Min(push, 2.5) * cs::Math::Max(0.1, f->Utilisation) * (1 + 3 * (1 - machinery));
		if (rng.NextDouble() >= risk) return;
		double days = 7 + rng.NextDouble() * 21;
		f->Impairment = 0.2;
		cs::String why = machinery < 0.5 ? ", short of machinery for upkeep" : push > 2 ? ", worked far past its design" : "";
		auto e = cs::New<EconomicShock>();
		e->Kind = ShockKind::MineAccident; e->Settlement = s; e->Faction = s->Owner; e->Facility = f; e->Good = ore; e->StartJd = jd; e->EndJd = jd + days;
		e->Label = cs::Cat("Mine accident (", Name(ore), ")");
		e->Text = cs::Cat("Accident in the ", cs::Strings::ToLowerInvariant(Name(ore)), " workings at ", Where(s), why, ": extraction cut to a fifth for about ", Days(days));
		Start(e);
	}

	void MarketShocks::Deplete(const cs::Ref<Settlement>& s, const cs::Ref<::SpaceGod::Core::Facility>& f, ::SpaceGod::Core::Good ore, double jd)
	{
		double perDay = std::get<1>(f->Recipe->Outputs[0]);
		double o = 0;
		double original = surveyedLevel.TryGetValue(f, o) ? o : f->BaseLevel;
		cs::String text;
		if (f->BaseLevel * 0.6 < original * 0.4)
		{
			// Worked down to little: the prospectors go out and find another seam.
			double level = original * (0.8 + rng.NextDouble() * 0.4);
			f->Level *= level / f->BaseLevel;
			f->BaseLevel = level;
			text = cs::Cat("Prospectors at ", Where(s), " strike a new ", cs::Strings::ToLowerInvariant(Name(ore)), " seam: extraction is back to ", Percent(level / original), " of what it was");
		}
		else
		{
			f->BaseLevel *= 0.6;
			f->Level *= 0.6;
			text = cs::Cat("The ", cs::Strings::ToLowerInvariant(Name(ore)), " seams at ", Where(s), " are playing out: extraction falls to ", Percent(f->BaseLevel / original), " of what it was");
		}
		f->Extracted = 0;
		f->Reserve = perDay * f->BaseLevel * (700 + rng.NextDouble() * 2000);
		auto e = cs::New<EconomicShock>();
		e->Kind = ShockKind::Depletion; e->Settlement = s; e->Faction = s->Owner; e->Facility = f; e->Good = ore; e->StartJd = jd; e->EndJd = jd;
		e->Label = cs::Cat("Seam worked out (", Name(ore), ")"); e->Text = text;
		Announce(e);
	}

	// ---------------------------------------------------------------- farms

	void MarketShocks::Farm(const cs::Ref<Settlement>& s, const cs::Ref<::SpaceGod::Core::Facility>& f, double jd)
	{
		if (f->Impairment < 1) return;
		// Crops short of nitrogen are weak; crowded, intensively run farms spread it.
		double target = s->Target(Good::Nitrogen);
		double nitrogen = target > 0 ? cs::Math::Min(1.0, s->Stock[static_cast<int>(Good::Nitrogen)] / target) : 1;
		double push = cs::Math::Max(0.5, f->Level / cs::Math::Max(f->BaseLevel, 1e-9));
		double risk = BlightPerDay * push * (1 + 4 * (1 - nitrogen));
		if (rng.NextDouble() >= risk) return;
		double days = 15 + rng.NextDouble() * 30;
		f->Impairment = 0.35;
		int food = static_cast<int>(Good::Food);
		double lost = s->Stock[food] * 0.2;
		s->Stock[food] -= lost;
		s->RefreshPrices();
		auto e = cs::New<EconomicShock>();
		e->Kind = ShockKind::Blight; e->Settlement = s; e->Faction = s->Owner; e->Facility = f; e->Good = Good::Food; e->StartJd = jd; e->EndJd = jd + days;
		e->Label = "Hydroponics blight";
		e->Text = cs::Cat("Blight in the hydroponic farms at ", Where(s), (nitrogen < 0.5 ? ", short of nitrogen" : ""), ": ", Tonnes(lost), " of food spoiled, harvests down to a third for about ", Days(days));
		Start(e);
	}

	// ---------------------------------------------------------------- wages and strikes

	void MarketShocks::Wages(const cs::Ref<Settlement>& s, double jd)
	{
		double living = s->CostOfLiving();
		if (s->WageIndex <= 0) s->WageIndex = living;
		// Wages follow the cost of living, slowly; they hold up better than they rise.
		double lag = living > s->WageIndex ? WageLagDays : WageLagDays * 2;
		s->WageIndex += (living - s->WageIndex) * (1 - cs::Math::Exp(-1 / lag));

		// The markets settle for a month after the start before anyone is counting what wages buy.
		if (s->IndustryFactor < 1 || s->Population < 20000 || jd - firstJd < 30) return;
		double last = 0;
		if (lastStrikeEndJd.TryGetValue(s, last) && jd - last < 45) return;
		double gap = living / s->WageIndex;
		if (gap < StrikeThreshold) return;
		if (rng.NextDouble() >= cs::Math::Min(0.1, (gap - StrikeThreshold) * 0.25 + 0.01)) return;
		double days = 5 + rng.NextDouble() * 15;
		s->IndustryFactor = 0.15;
		auto dearest = DearestNeed(s);
		auto e = cs::New<EconomicShock>();
		e->Kind = ShockKind::Strike; e->Settlement = s; e->Faction = s->Owner; e->Good = dearest; e->StartJd = jd; e->EndJd = jd + days; e->Severity = gap;
		e->Label = "Strike";
		e->Text = cs::Cat("Workers at ", Where(s), " strike: wages buy ", Percent(1 - 1 / gap), " less than they did, ", cs::Strings::ToLowerInvariant(Name(dearest)), " at ", Ratio(s, dearest), " normal; industry stops, life support runs");
		Start(e);
	}

	::SpaceGod::Core::Good MarketShocks::DearestNeed(const cs::Ref<Settlement>& s)
	{
		auto best = Good::Food;
		double top = 0;
		for (const auto& [g, _] : s->PersonNeeds())
		{
			double r = s->Price[static_cast<int>(g)] / GoodInfo::Of(g)->BasePrice;
			if (r > top)
			{
				top = r;
				best = g;
			}
		}
		return best;
	}

	// ---------------------------------------------------------------- war rationing

	void MarketShocks::Rationing(double jd)
	{
		for (const auto& f : world->Fleet->Factions())
		{
			if (!f->IsMilitary || f->IsPirate) continue;
			auto current = Find(ShockKind::Rationing, f);
			bool atWar = AtWar(f);
			double essentials = 0, munitions = 1;
			int n = 0, m = 0;
			double munitionSum = 0;
			for (const auto& s : Economy()->Settlements)
			{
				if (s->Owner != f || s->Fallen || s->Kind == SettlementKind::Haven) continue;
				double cover = 1.5;
				for (auto g : { Good::Food, Good::Water, Good::MedicalSupplies })
				{
					double t = s->Target(g);
					if (s->Demand[static_cast<int>(g)] > 0 && t > 0) cover = cs::Math::Min(cover, s->Stock[static_cast<int>(g)] / t);
				}
				essentials += cs::Math::Min(1.5, cover);
				n++;
				double mt = s->Target(Good::Munitions);
				if (s->HasShipyard() && mt > 0)
				{
					munitionSum += cs::Math::Min(1.5, s->Stock[static_cast<int>(Good::Munitions)] / mt);
					m++;
				}
			}
			if (n == 0)
			{
				// No ports left to ration: what it had in force goes with them, rather than standing on
				// forever and landing on the first port it takes back.
				if (current != nullptr)
				{
					current->EndJd = jd;
					End(current, jd, cs::Cat("The ", f->Name, " ends war rationing: it has no ports left to ration"));
				}
				continue;
			}
			essentials /= n;
			if (m > 0) munitions = munitionSum / m;

			if (current == nullptr)
			{
				if (!atWar || (essentials >= 0.5 && munitions >= 0.25)) continue;
				bool food = essentials < 0.5;
				auto shock = cs::New<EconomicShock>();
				shock->Kind = ShockKind::Rationing; shock->Faction = f; shock->Good = food ? Good::Food : Good::Munitions; shock->StartJd = jd; shock->EndJd = cs::Double::PositiveInfinity;
				shock->Label = "War rationing";
				shock->Text = cs::Cat("The ", f->Name, " brings in war rationing: ",
					(food ? cs::Cat("stores of essentials are down to ", Percent(essentials), " of what its ports want") : cs::Cat("munitions are down to ", Percent(munitions), " of what its yards want")),
					", so its people get four-fifths of their food, water and medicine");
				Start(shock);
			}
			else if (jd - current->StartJd >= 30 && (!atWar || (essentials >= 0.8 && munitions >= 0.6)))
			{
				current->EndJd = jd;
				End(current, jd, !atWar ? cs::Cat("The ", f->Name, " ends war rationing now the fighting has stopped") : cs::Cat("The ", f->Name, " ends war rationing: its stores have recovered"));
			}
		}
		// Every port follows its owner's rationing (one taken in a siege goes on its captor's ration).
		for (const auto& s : Economy()->Settlements) s->Ration = Find(ShockKind::Rationing, s->Owner) != nullptr ? 0.8 : 1;
	}

	bool MarketShocks::AtWar(const cs::Ref<::SpaceGod::Core::Faction>& f) const
	{
		for (const auto& o : world->Fleet->Factions())
			if (o != f && !o->IsPirate && Diplomacy()->Get(f, o) == Relation::War) return true;
		return false;
	}

	// ---------------------------------------------------------------- blockades

	void MarketShocks::Blockades(double jd)
	{
		bool changed = false;
		for (const auto& s : Economy()->Settlements)
		{
			if (s->Kind == SettlementKind::Haven) continue;
			auto by = BesiegerOf ? BesiegerOf(s) : world->War->Besieger(s);
			auto held = Diplomacy()->BlockadedBy(s);
			// A blockade holds through a day or two of its ships coming and going; it lifts when they have gone.
			int absent = 0;
			besiegerAbsentDays.TryGetValue(s, absent);
			absent = held != nullptr && by != held ? absent + 1 : 0;
			besiegerAbsentDays[s] = absent;
			if (held != nullptr && by != held && (absent >= 3 || s->Owner == held || by != nullptr))
			{
				Diplomacy()->LiftBlockade(s);
				auto shock = Find(ShockKind::Blockade, s);
				cs::String why = s->Owner == held ? cs::String("the port is theirs now") : by != nullptr ? cs::Cat("the ", by->Name, " holds the space over it now") : cs::String("its ships have gone");
				if (shock != nullptr)
				{
					shock->EndJd = jd;
					End(shock, jd, cs::Cat("The ", held->Name, " lifts its blockade of ", Where(s), ": ", why));
				}
				held = nullptr;
				changed = true;
			}
			if (by != nullptr && held == nullptr && by != s->Owner && Diplomacy()->DeclareBlockade(s, by))
			{
				auto e = cs::New<EconomicShock>();
				e->Kind = ShockKind::Blockade; e->Settlement = s; e->Faction = by; e->StartJd = jd; e->EndJd = cs::Double::PositiveInfinity;
				e->Label = cs::Cat("Blockade by the ", by->Name);
				e->Text = cs::Cat("The ", by->Name, " declares a blockade of ", Where(s), ": no ship may dock there until it is lifted, and the ", s->Owner->Name, " port lives on what it has");
				Start(e);
				changed = true;
			}
		}
		if (changed) world->War->RevokeBookings();
	}

	// ---------------------------------------------------------------- embargoes

	double MarketShocks::WarWeight(::SpaceGod::Core::Good g) { return g == Good::Munitions ? 1 : g == Good::Electronics ? 0.5 : g == Good::Alloys ? 0.1 : 0; }

	void MarketShocks::OnSold(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port, ::SpaceGod::Core::Good good, double tonnes)
	{
		double weight = WarWeight(good);
		if (weight <= 0 || ship->Faction == port->Owner || ship->Faction->IsPirate || port->Owner->IsPirate) return;
		for (const auto& by : world->Fleet->Factions())
		{
			if (!by->IsMilitary || by->IsPirate || by == ship->Faction || by == port->Owner) continue;
			// A navy minds who arms its enemy, if the seller is not its enemy already.
			if (Diplomacy()->Get(by, port->Owner) != Relation::War || Diplomacy()->Hostile(by, ship->Faction)) continue;
			auto key = std::make_tuple(by, ship->Faction);
			double sum = 0;
			contraband.TryGetValue(key, sum);
			contraband[key] = sum + tonnes * weight;
			contrabandFor[key] = port->Owner;
		}
	}

	double MarketShocks::Contraband(const cs::Ref<::SpaceGod::Core::Faction>& by, const cs::Ref<::SpaceGod::Core::Faction>& against) const
	{
		double t = 0;
		return contraband.TryGetValue(std::make_tuple(by, against), t) ? t : 0;
	}

	void MarketShocks::Embargoes(double jd)
	{
		// The navies' memory of who ran what fades.
		for (const auto& key : contraband.Keys()) contraband[key] *= cs::Math::Exp(-1 / ContrabandMemoryDays);

		bool changed = false;
		for (auto [key, value] : contraband)
		{
			if (value < EmbargoThreshold) continue;
			const auto& [by, against] = key;
			auto enemy = contrabandFor.At(key);
			if (Diplomacy()->Get(by, enemy) != Relation::War) continue;
			cs::String reason = cs::Cat("running war goods to the ", enemy->Name);
			auto e = Diplomacy()->DeclareEmbargo(by, against, jd, jd + 90 + rng.NextDouble() * 60, reason);
			if (e == nullptr) continue;
			auto shock = cs::New<EconomicShock>();
			shock->Kind = ShockKind::Embargo; shock->Faction = by; shock->Embargo = e; shock->Good = Good::Munitions; shock->StartJd = jd; shock->EndJd = e->UntilJd;
			shock->Label = cs::Cat("Embargo on ", MerchantsOf(against));
			shock->Text = cs::Cat("The ", by->Name, " embargoes ", MerchantsOf(against), " for ", reason, ": its ports are closed to their ships");
			Start(shock);
			changed = true;
		}
		cs::List<cs::Ref<::SpaceGod::Core::Embargo>> embargoes = Diplomacy()->Embargoes();
		for (const auto& e : embargoes)
		{
			// Every system shares the one diplomacy table, but each keeps its own book of who ran war goods: only
			// the system that declared an embargo can judge when it is over, or the others would lift it the next day.
			auto shock = Active.Find([&](const cs::Ref<EconomicShock>& x) { return x->Embargo == e; });
			if (shock == nullptr) continue;
			auto key = std::make_tuple(e->By, e->Against);
			cs::Ref<::SpaceGod::Core::Faction> enemy;
			bool warOver = !contrabandFor.TryGetValue(key, enemy) || Diplomacy()->Get(e->By, enemy) != Relation::War;
			if (jd < e->UntilJd && !warOver) continue;
			Diplomacy()->LiftEmbargo(e);
			contraband.Remove(key);
			shock->EndJd = jd;
			End(shock, jd, cs::Cat("The ", e->By->Name, " lifts its embargo on ", MerchantsOf(e->Against), (warOver ? ": the war it was for has stopped" : "")));
			changed = true;
		}
		if (changed) world->War->RevokeBookings();
	}

	cs::String MarketShocks::MerchantsOf(const cs::Ref<::SpaceGod::Core::Faction>& f)
	{
		return f->IsMilitary || f->IsPirate ? cs::Cat("the ", f->Name) : cs::Cat(cs::Strings::ToLowerInvariant(f->Name), " merchants");
	}

	// ---------------------------------------------------------------- price spikes

	void MarketShocks::PriceSpikes(double jd)
	{
		double k = 1 - cs::Math::Exp(-1 / SpikeSmoothingDays);
		for (const auto& s : Economy()->Settlements)
		{
			if (s->Fallen || !s->Discovered() || s->Kind == SettlementKind::Haven) continue;
			for (int g = 0; g < GoodInfo::Count; g++)
			{
				auto key = std::make_tuple(s, g);
				double now = s->Price[g] / GoodInfo::Of(static_cast<Good>(g))->BasePrice;
				double was = 0;
				double ratio = smoothed.TryGetValue(key, was) ? was + (now - was) * k : now;
				smoothed[key] = ratio;
				if (spiking.Contains(key))
				{
					if (ratio < SpikeClear)
					{
						spiking.Remove(key);
						spikeClearedJd[key] = jd;
					}
					continue;
				}
				if (ratio < SpikeRatio || s->Demand[g] <= 0) continue;
				spiking.Add(key);
				double cleared = 0;
				if (spikeClearedJd.TryGetValue(key, cleared) && jd - cleared < 30) continue; // the same flare-up, back again
				cs::String cause = CauseAt(s, static_cast<Good>(g));
				world->Log(jd, cs::Cat(Name(static_cast<Good>(g)), " at ", Where(s), " jumps to ", cs::Fmt(now, "0.0"), "x normal, ",
					cs::Fmt(s->Price[g], "N0"), " cr/t", (!cause.empty() ? cs::Cat(" (", cause, ")") : cs::String())), WorldEventKind::Market, s->Body);
			}
		}
		for (int g = 0; g < GoodInfo::Count; g++)
		{
			double now = SystemIndex(static_cast<Good>(g));
			double index = systemIndex[g] = systemIndex[g] > 0 ? systemIndex[g] + (now - systemIndex[g]) * (1 - cs::Math::Exp(-1 / 5.0)) : now;
			cs::String name = cs::Strings::ToLowerInvariant(Name(static_cast<Good>(g)));
			if (systemSpiking[g])
			{
				if (index < SystemSpikeClear)
				{
					systemSpiking[g] = false;
					world->Log(jd, cs::Cat("Across the system, ", name, " eases back to ", cs::Fmt(index, "0.0"), "x normal on average"), WorldEventKind::Market);
				}
			}
			else if (index >= SystemSpikeRatio)
			{
				systemSpiking[g] = true;
				world->Log(jd, cs::Cat("Across the system, ", name, " now costs ", cs::Fmt(index, "0.0"), "x normal on average"), WorldEventKind::Market);
			}
		}
	}

	double MarketShocks::SystemIndex(::SpaceGod::Core::Good g) const
	{
		int i = static_cast<int>(g);
		double sum = 0, weight = 0;
		for (const auto& s : Economy()->Settlements)
		{
			if (s->Fallen || s->Kind == SettlementKind::Haven) continue;
			double use = s->Demand[i] - s->ExtraDemand[i];
			if (use <= 0) continue;
			sum += use * s->Price[i] / GoodInfo::Of(g)->BasePrice;
			weight += use;
		}
		return weight > 0 ? sum / weight : 1;
	}

	cs::String MarketShocks::CauseAt(const cs::Ref<Settlement>& s, ::SpaceGod::Core::Good g) const
	{
		for (const auto& e : At(s))
		{
			switch (e->Kind)
			{
				case ShockKind::Blockade: return cs::Cat("blockaded by the ", e->Faction->Name);
				case ShockKind::Strike: return "strike";
				case ShockKind::Blight: if (g == Good::Food) return "blight"; break;
				case ShockKind::MineAccident: if (e->Good == g) return "mine accident"; break;
				default: break;
			}
		}
		return cs::String();
	}

	// ---------------------------------------------------------------- bookkeeping

	cs::Ref<EconomicShock> MarketShocks::Find(ShockKind kind, const cs::Ref<::SpaceGod::Core::Faction>& f) const
	{
		for (const auto& e : Active)
			if (e->Kind == kind && e->Faction == f) return e;
		return nullptr;
	}

	cs::Ref<EconomicShock> MarketShocks::Find(ShockKind kind, const cs::Ref<Settlement>& s) const
	{
		for (const auto& e : Active)
			if (e->Kind == kind && e->Settlement == s) return e;
		return nullptr;
	}

	void MarketShocks::Start(const cs::Ref<EconomicShock>& e)
	{
		Active.Add(e);
		Announce(e);
	}

	void MarketShocks::Announce(const cs::Ref<EconomicShock>& e)
	{
		world->Log(e->StartJd, e->Text, e->Kind == ShockKind::Embargo ? WorldEventKind::Diplomacy : WorldEventKind::Economy, e->Settlement ? e->Settlement->Body : -1);
		Started.Invoke(e);
	}

	void MarketShocks::EndExpired(double jd)
	{
		for (int i = Active.Count() - 1; i >= 0; i--)
		{
			auto e = Active[i];
			if (jd < e->EndJd) continue;
			switch (e->Kind)
			{
				case ShockKind::MineAccident:
					e->Facility->Impairment = 1;
					End(e, jd, cs::Cat("The ", cs::Strings::ToLowerInvariant(Name(*e->Good)), " workings at ", Where(e->Settlement), " are back at work"));
					break;
				case ShockKind::Blight:
					e->Facility->Impairment = 1;
					End(e, jd, cs::Cat("The hydroponic farms at ", Where(e->Settlement), " are clear of the blight"));
					break;
				case ShockKind::Strike:
				{
					auto s = e->Settlement;
					s->IndustryFactor = 1;
					double living = s->CostOfLiving();
					double before = s->WageIndex;
					// The settlement closes most of the gap the strike was called over (or what is left of it now).
					s->WageIndex = cs::Math::Max(s->WageIndex * (1 + (e->Severity - 1) * 0.6), s->WageIndex + cs::Math::Max(0.0, living - s->WageIndex) * 0.8);
					lastStrikeEndJd[s] = jd;
					End(e, jd, cs::Cat("The strike at ", Where(s), " ends: the workers win a ", Percent(s->WageIndex / before - 1), " raise"));
					break;
				}
				default:
					// Rationing, blockades and embargoes end when their cause does, in their own passes.
					break;
			}
		}
	}

	void MarketShocks::End(const cs::Ref<EconomicShock>& e, double jd, const cs::String& text)
	{
		Active.Remove(e);
		world->Log(jd, text, e->Kind == ShockKind::Embargo ? WorldEventKind::Diplomacy : WorldEventKind::Economy, e->Settlement ? e->Settlement->Body : -1);
	}

	// ---------------------------------------------------------------- text

	cs::String MarketShocks::Where(const cs::Ref<Settlement>& s) const { return cs::Cat(s->Name, " (", (*world->Catalog())[s->Body]->Name, ")"); }
	cs::String MarketShocks::Name(::SpaceGod::Core::Good g) { return GoodInfo::Of(g)->Name; }
	cs::String MarketShocks::Ratio(const cs::Ref<Settlement>& s, ::SpaceGod::Core::Good g) { return cs::Cat(cs::Fmt(s->Price[static_cast<int>(g)] / GoodInfo::Of(g)->BasePrice, "0.0"), "x"); }
	cs::String MarketShocks::Percent(double f) { return cs::Cat(cs::Fmt(cs::Math::Max(0.0, f) * 100, "0"), "%"); }
	cs::String MarketShocks::Tonnes(double t) { return cs::Cat(cs::Fmt(t, "N0"), " t"); }
	cs::String MarketShocks::Days(double d) { return d >= 14 ? cs::Cat(cs::Str(cs::Math::Round(d / 7)), " weeks") : cs::Cat(cs::Str(cs::Math::Round(d)), " days"); }
}
