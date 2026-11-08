// US-T105: objectives with lasting consequences (see Systems/LastingConsequences.h).

#include "Systems/LastingConsequences.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/War.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Embodiment/Influence.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarWars.h"
#include "Systems/Polities.h"
#include "Systems/Territory.h"
#include "World.h"

#include <unordered_map>

namespace SpaceGod::Core
{
	namespace
	{
		/// The most reviews one update catches up on: a longer jump is reviewed once at its end.
		constexpr int MaxCatchUp = 13;

		/// A settlement that stands and governs: not fallen, not a raider haven, with people in it.
		bool Stands(const cs::Ref<Settlement>& s) { return s != nullptr && !s->Fallen && s->Kind != SettlementKind::Haven && s->Population > 0; }

		cs::String NameOf(const cs::Ref<Faction>& f) { return f != nullptr ? cs::Cat("the ", f->Name) : cs::String("nobody"); }

		cs::Ref<StarSystem> SystemOf(const Galaxy& galaxy, const cs::Ref<World>& w)
		{
			if (w == nullptr) return nullptr;
			for (const auto& s : galaxy.Systems)
				if (s->World == w) return s;
			return nullptr;
		}

		cs::Ref<World> WorldOf(const Galaxy& galaxy, const cs::Ref<Settlement>& s)
		{
			return galaxy.Polities != nullptr && s != nullptr ? galaxy.Polities->WorldOf(s) : nullptr;
		}

		/// "Eris Base (Eris) in Sol".
		cs::String PlaceOf(const Galaxy& galaxy, const cs::Ref<Settlement>& s)
		{
			auto w = WorldOf(galaxy, s);
			auto sys = SystemOf(galaxy, w);
			cs::String body;
			if (w != nullptr && w->Catalog() != nullptr && s->Body >= 0 && s->Body < w->Catalog()->Count()) body = (*w->Catalog())[s->Body]->Name;
			cs::String text = body.empty() || body == s->Name ? s->Name : cs::Cat(s->Name, " (", body, ")");
			return sys != nullptr ? cs::Cat(text, " in ", sys->Name) : text;
		}

		/// Logs <paramref name="text"/> in the world <paramref name="s"/> is in, at its body.
		void LogAt(const Galaxy& galaxy, const cs::Ref<Settlement>& s, double jd, const cs::String& text, WorldEventKind kind)
		{
			auto w = WorldOf(galaxy, s);
			if (w != nullptr) w->Log(jd, text, kind, s->Body);
		}

		/// Whether <paramref name="a"/> and <paramref name="b"/> are at war, or made their peace within the last two days
		/// (a settlement ceded at the treaty changes hands the day the war ends).
		bool Enemies(const Galaxy& galaxy, const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, double jd)
		{
			if (galaxy.Wars == nullptr || a == nullptr || b == nullptr || a == b) return false;
			if (galaxy.Wars->Between(a, b) != nullptr) return true;
			for (const auto& w : galaxy.Wars->Ended())
				if (w->Involves(a) && w->Involves(b) && !(w->EndedJd < jd - 2)) return true;
			return false;
		}

		/// How far a garrison holds its settlement, 0..1 (1 for one never mustered: nobody has fought there).
		double Hold(const cs::Ref<Settlement>& s)
		{
			if (cs::Double::IsNaN(s->Garrison)) return 1;
			double full = InterstellarWars::FullGarrison(s);
			return full > 0 ? cs::Math::Min(1.0, s->Garrison / full) : 1;
		}
	}

	const char* ToString(GodObjectiveKind v)
	{
		switch (v)
		{
			case GodObjectiveKind::Take: return "Take";
			case GodObjectiveKind::Hold: return "Hold";
		}
		return "";
	}

	const char* ToString(GodObjectiveStatus v)
	{
		switch (v)
		{
			case GodObjectiveStatus::Offered: return "Offered";
			case GodObjectiveStatus::Accepted: return "Accepted";
			case GodObjectiveStatus::Declined: return "Declined";
			case GodObjectiveStatus::Completed: return "Completed";
			case GodObjectiveStatus::Failed: return "Failed";
			case GodObjectiveStatus::Lapsed: return "Lapsed";
		}
		return "";
	}

	const char* ToString(TribeStage v)
	{
		switch (v)
		{
			case TribeStage::Fleeing: return "Fleeing";
			case TribeStage::Resettled: return "Resettled";
			case TribeStage::Returned: return "Returned";
			case TribeStage::Assimilated: return "Assimilated";
			case TribeStage::Scattered: return "Scattered";
		}
		return "";
	}

	const char* ToString(PlotKind v)
	{
		switch (v)
		{
			case PlotKind::Revenge: return "Revenge";
			case PlotKind::Assassination: return "Assassination";
			case PlotKind::AssassinateSpaceGod: return "AssassinateSpaceGod";
		}
		return "";
	}

	cs::Ref<Grudge> RefugeeTribe::NextGrudge() const
	{
		for (const auto& g : Grudges)
			if (g->Owed() && !g->SpaceGod) return g;
		for (const auto& g : Grudges)
			if (g->Owed()) return g;
		return nullptr;
	}

	// ---------------------------------------------------------------- the galaxy's consequences

	cs::Ref<LastingConsequences> LastingConsequences::Of(Galaxy& galaxy)
	{
		auto t = Territory::Of(galaxy);
		if (t->Consequences == nullptr) t->Consequences = cs::New<LastingConsequences>();
		return t->Consequences;
	}

	cs::Ref<LastingConsequences> LastingConsequences::Find(const Galaxy& galaxy)
	{
		return galaxy.Territory != nullptr ? galaxy.Territory->Consequences : nullptr;
	}

	cs::Ref<GodObjective> LastingConsequences::ObjectiveById(int id) const
	{
		for (const auto& o : Objectives)
			if (o->Id == id) return o;
		return nullptr;
	}

	cs::List<cs::Ref<GodObjective>> LastingConsequences::OpenObjectives() const
	{
		auto list = cs::List<cs::Ref<GodObjective>>();
		for (const auto& o : Objectives)
			if (o->Open()) list.Add(o);
		return list;
	}

	cs::List<cs::Ref<RefugeeTribe>> LastingConsequences::ActiveTribes() const
	{
		auto list = cs::List<cs::Ref<RefugeeTribe>>();
		for (const auto& t : Tribes)
			if (t->Active()) list.Add(t);
		return list;
	}

	bool LastingConsequences::SidedWith(const cs::Ref<Faction>& side, const cs::Ref<Faction>& against) const
	{
		for (const auto& o : Objectives)
			if (o->Side == side && o->Against == against
				&& (o->Status == GodObjectiveStatus::Accepted || o->Status == GodObjectiveStatus::Completed || o->Status == GodObjectiveStatus::Failed))
				return true;
		return false;
	}

	cs::String LastingConsequences::Accept(Galaxy& galaxy, const cs::Ref<GodObjective>& o, double jd)
	{
		if (o == nullptr) return "no such objective";
		if (o->Status != GodObjectiveStatus::Offered) return cs::Cat("it is ", ToString(o->Status), ", not offered");
		if (o->War == nullptr || o->War->Over()) return "that war is over";
		for (const auto& other : Objectives)
			if (other != o && other->War == o->War && other->Status != GodObjectiveStatus::Offered && other->Status != GodObjectiveStatus::Declined
				&& other->Status != GodObjectiveStatus::Lapsed)
				return cs::Cat("the SpaceGod has already sided with ", NameOf(other->Side), " in that war");
		o->Status = GodObjectiveStatus::Accepted;
		o->AcceptedJd = jd;
		for (const auto& other : Objectives)
			if (other != o && other->War == o->War && other->Status == GodObjectiveStatus::Offered)
			{
				other->Status = GodObjectiveStatus::Declined;
				other->ResolvedJd = jd;
			}
		LogAt(galaxy, o->Target, jd, cs::Cat("The SpaceGod sides with ", NameOf(o->Side), " against ", NameOf(o->Against), ": ", Describe(o)), WorldEventKind::Opportunity);
		return cs::String();
	}

	cs::String LastingConsequences::Decline(const cs::Ref<GodObjective>& o, double jd)
	{
		if (o == nullptr) return "no such objective";
		if (o->Status != GodObjectiveStatus::Offered) return cs::Cat("it is ", ToString(o->Status), ", not offered");
		o->Status = GodObjectiveStatus::Declined;
		o->ResolvedJd = jd;
		return cs::String();
	}

	cs::String LastingConsequences::Describe(const cs::Ref<GodObjective>& o)
	{
		if (o == nullptr) return cs::String();
		cs::String side = o->Side != nullptr ? o->Side->Name : cs::String("nobody");
		cs::String place = !o->Place.empty() ? o->Place : o->Target != nullptr ? o->Target->Name : cs::String("?");
		return o->Kind == GodObjectiveKind::Take
			? cs::Cat("Help the ", side, " take ", place, " from ", NameOf(o->Against))
			: cs::Cat("Help the ", side, " hold ", place, " against ", NameOf(o->Against));
	}

	cs::String LastingConsequences::Describe(const cs::Ref<RefugeeTribe>& t)
	{
		if (t == nullptr) return cs::String();
		cs::String where;
		switch (t->Stage)
		{
			case TribeStage::Fleeing: where = "on the road"; break;
			case TribeStage::Resettled: where = t->Refuge != nullptr ? cs::Cat("settled at ", t->Refuge->Name) : cs::String("settled"); break;
			case TribeStage::Returned: where = t->Home != nullptr ? cs::Cat("home again at ", t->Home->Name) : cs::String("home again"); break;
			case TribeStage::Assimilated: where = "one with the people who took them in"; break;
			case TribeStage::Scattered: where = "scattered"; break;
		}
		int owed = 0;
		bool god = false;
		for (const auto& g : t->Grudges)
			if (g->Owed())
			{
				owed++;
				if (g->SpaceGod) god = true;
			}
		return cs::Cat(t->Name, " (", NameOf(t->Origin), ", ", cs::Fmt(t->People, "N0"), " people): driven from ",
			t->Home != nullptr ? t->Home->Name : cs::String("their home"), " by ", NameOf(t->Victor), ", ", where,
			owed > 0 ? cs::Cat(", ", cs::Fmt(owed, "0"), " grudge", owed == 1 ? "" : "s", " owed", god ? " (the SpaceGod's among them)" : "") : cs::String(),
			", ", cs::Fmt(t->Plots, "0"), " plot", t->Plots == 1 ? "" : "s", " staged");
	}

	double LastingConsequences::Roll(int tribe, int n, double jd, std::uint32_t salt)
	{
		// FNV-1a over the tribe, the draw and the whole day: the same roll on every peer and every replay.
		std::uint32_t h = 2166136261u ^ salt;
		auto mix = [&h](std::uint32_t v)
		{
			for (int i = 0; i < 4; i++)
			{
				h ^= (v >> (i * 8)) & 0xFFu;
				h *= 16777619u;
			}
		};
		mix(static_cast<std::uint32_t>(tribe));
		mix(static_cast<std::uint32_t>(n));
		mix(static_cast<std::uint32_t>(static_cast<long long>(cs::Math::Floor(jd))));
		h ^= h >> 15;
		h *= 0x2C1B3C6Du;
		h ^= h >> 12;
		return static_cast<double>(h) / 4294967296.0;
	}

	// ---------------------------------------------------------------- the day

	void LastingConsequences::Update(Galaxy& galaxy, double jd)
	{
		if (cs::Double::IsNaN(jd) || galaxy.Polities == nullptr) return;
		if (cs::Double::IsNaN(nextDailyJd))
		{
			// The first look only takes stock of who holds what: nothing that changed hands before it makes refugees.
			Watch(galaxy, jd, true);
			nextDailyJd = cs::Math::Floor(jd) + 1;
			return;
		}
		if (jd < nextDailyJd) return;
		if (jd - nextDailyJd > MaxCatchUp) nextDailyJd = cs::Math::Floor(jd);
		while (nextDailyJd <= jd)
		{
			double day = nextDailyJd;
			Watch(galaxy, day, false);
			Judge(galaxy, day);
			Offer(galaxy, day);
			ReviewTribes(galaxy, day);
			nextDailyJd = day + 1;
		}
	}

	// ---------------------------------------------------------------- objectives

	void LastingConsequences::Offer(Galaxy& galaxy, double jd)
	{
		if (galaxy.Wars == nullptr) return;
		for (const auto& w : galaxy.Wars->Active())
		{
			if (w->Over() || w->Aggressor == nullptr || w->Defender == nullptr) continue;
			bool offered = false;
			for (const auto& o : Objectives)
				if (o->War == w)
				{
					offered = true;
					break;
				}
			if (offered) continue;
			auto a = w->Aggressor, d = w->Defender;

			// The aggressor's first goal still held: the defender's most populous settlement there.
			cs::Ref<Settlement> target;
			cs::Ref<StarSystem> where;
			for (const auto& g : w->Goals)
			{
				if (g == nullptr || g->World == nullptr) continue;
				for (const auto& s : g->World->Economy->Settlements)
					if (s->Owner == d && Stands(s) && (target == nullptr || s->Population > target->Population))
					{
						target = s;
						where = g;
					}
				if (target != nullptr) break;
			}
			auto taker = a, holder = d;
			if (target == nullptr)
			{
				// Every goal taken: the defender would take back the most populous it has lost.
				for (const auto& sys : galaxy.Systems)
					for (const auto& s : sys->World->Economy->Settlements)
						if (s->Owner == a && s->OccupiedFrom == d && Stands(s) && (target == nullptr || s->Population > target->Population))
						{
							target = s;
							where = sys;
						}
				taker = d;
				holder = a;
			}
			if (target == nullptr) continue;

			cs::String place = PlaceOf(galaxy, target);
			auto take = cs::New<GodObjective>();
			take->Id = nextObjectiveId++;
			take->War = w;
			take->Side = taker;
			take->Against = holder;
			take->Kind = GodObjectiveKind::Take;
			take->Target = target;
			take->System = where;
			take->Place = place;
			take->OfferedJd = jd;
			Objectives.Add(take);

			auto hold = cs::New<GodObjective>();
			hold->Id = nextObjectiveId++;
			hold->War = w;
			hold->Side = holder;
			hold->Against = taker;
			hold->Kind = GodObjectiveKind::Hold;
			hold->Target = target;
			hold->System = where;
			hold->Place = place;
			hold->OfferedJd = jd;
			Objectives.Add(hold);

			LogAt(galaxy, target, jd, cs::Cat("The SpaceGod is called on over ", place, ": ", Describe(take), " (objective ", cs::Fmt(take->Id, "0"),
				"); or ", Describe(hold), " (objective ", cs::Fmt(hold->Id, "0"), ")"),
				WorldEventKind::Opportunity);
		}
	}

	void LastingConsequences::Judge(Galaxy& galaxy, double jd)
	{
		for (const auto& o : Objectives)
		{
			if (!o->Open()) continue;
			auto s = o->Target;
			bool over = o->War == nullptr || o->War->Over();
			if (s == nullptr || s->Fallen)
			{
				Resolve(galaxy, o, GodObjectiveStatus::Failed, jd);
				continue;
			}
			if (o->Kind == GodObjectiveKind::Take)
			{
				if (s->Owner == o->Side) Resolve(galaxy, o, GodObjectiveStatus::Completed, jd);
				else if (over) Resolve(galaxy, o, GodObjectiveStatus::Failed, jd);
			}
			else
			{
				if (s->Owner != o->Side) Resolve(galaxy, o, GodObjectiveStatus::Failed, jd);
				else if (over) Resolve(galaxy, o, GodObjectiveStatus::Completed, jd);
			}
		}
	}

	void LastingConsequences::Resolve(Galaxy& galaxy, const cs::Ref<GodObjective>& o, GodObjectiveStatus status, double jd)
	{
		o->ResolvedJd = jd;
		if (o->Status == GodObjectiveStatus::Offered)
		{
			// Never taken up: whatever came of it was none of the SpaceGod's doing.
			o->Status = GodObjectiveStatus::Lapsed;
			return;
		}
		o->Status = status;
		auto w = WorldOf(galaxy, o->Target);
		if (status == GodObjectiveStatus::Completed)
		{
			cs::String why = cs::Cat("objective carried through: ", Describe(o));
			if (w != nullptr) SpaceGodInfluence::Grant(w, InfluenceSource::Mission, ObjectiveXp, why);
			LogAt(galaxy, o->Target, jd, cs::Cat("The SpaceGod's objective is carried through: ", Describe(o)), WorldEventKind::Opportunity);
		}
		else LogAt(galaxy, o->Target, jd, cs::Cat("The SpaceGod's objective has failed: ", Describe(o)), WorldEventKind::Opportunity);
	}

	// ---------------------------------------------------------------- refugees

	void LastingConsequences::Watch(Galaxy& galaxy, double jd, bool quiet)
	{
		auto now = cs::List<cs::Ref<OwnerMark>>();
		std::unordered_map<const Settlement*, cs::Ref<OwnerMark>> before;
		for (const auto& m : marks)
			if (m->Settlement != nullptr) before[m->Settlement.get()] = m;
		for (const auto& sys : galaxy.Systems)
		{
			if (sys->World == nullptr || sys->World->Economy == nullptr) continue;
			for (const auto& s : sys->World->Economy->Settlements)
			{
				auto found = before.find(s.get());
				cs::Ref<OwnerMark> was = found != before.end() ? found->second : nullptr;
				// Taken by an enemy (or ceded to one at the peace), not taken back by its own: its people are driven out.
				if (!quiet && was != nullptr && was->Owner != nullptr && s->Owner != nullptr && s->Owner != was->Owner && was->OccupiedFrom != s->Owner
					&& Stands(s) && Enemies(galaxy, was->Owner, s->Owner, jd))
					Flee(galaxy, s, was->Owner, s->Owner, jd);
				auto m = cs::New<OwnerMark>();
				m->Settlement = s;
				m->Owner = s->Owner;
				m->OccupiedFrom = s->OccupiedFrom;
				now.Add(m);
			}
		}
		marks = now;
	}

	void LastingConsequences::Flee(Galaxy& galaxy, const cs::Ref<Settlement>& s, const cs::Ref<Faction>& loser, const cs::Ref<Faction>& victor, double jd)
	{
		double people = cs::Math::Floor(s->Population * FleeShare);
		if (people < MinTribe) return;
		s->Population -= people;
		auto t = cs::New<RefugeeTribe>();
		t->Id = nextTribeId++;
		t->Name = cs::Cat("the ", s->Name, " exiles");
		t->Origin = loser;
		t->Home = s;
		t->HomeSystem = SystemOf(galaxy, WorldOf(galaxy, s));
		t->Victor = victor;
		t->People = people;
		t->FledJd = jd;
		auto g = cs::New<Grudge>();
		g->Against = victor;
		g->SinceJd = jd;
		t->Grudges.Add(g);
		bool god = SidedWith(victor, loser);
		if (god)
		{
			auto gg = cs::New<Grudge>();
			gg->SpaceGod = true;
			gg->SinceJd = jd;
			t->Grudges.Add(gg);
		}
		Tribes.Add(t);
		LogAt(galaxy, s, jd, cs::Cat(cs::Fmt(people, "N0"), " of ", s->Name, "'s people will not live under ", NameOf(victor), ": ", t->Name,
			" flee, swearing to remember who beat them", god ? cs::String(", and the SpaceGod who helped") : cs::String()), WorldEventKind::Loss);
	}

	void LastingConsequences::ReviewTribes(Galaxy& galaxy, double jd)
	{
		for (const auto& t : Tribes)
		{
			if (!t->Active()) continue;
			if (t->Stage == TribeStage::Fleeing)
			{
				if (jd - t->FledJd >= FleeDays) Resettle(galaxy, t, jd);
				continue;
			}
			// Their refuge fell under them: they take to the road again.
			if (!Stands(t->Refuge))
			{
				t->Stage = TribeStage::Fleeing;
				t->FledJd = jd;
				t->Refuge = nullptr;
				t->RefugeSystem = nullptr;
				continue;
			}
			if (!cs::Double::IsNaN(t->NextPlotJd) && jd >= t->NextPlotJd) StagePlot(galaxy, t, jd);
		}
	}

	bool LastingConsequences::Resettle(Galaxy& galaxy, const cs::Ref<RefugeeTribe>& t, double jd)
	{
		// Among their own first, nearest first, the biggest breaking a tie; else anyone neither the victor nor at war with them.
		cs::Ref<Settlement> best;
		cs::Ref<StarSystem> bestSystem;
		int bestTier = 2;
		double bestLy = 0;
		for (const auto& sys : galaxy.Systems)
		{
			if (sys->World == nullptr || sys->World->Economy == nullptr) continue;
			double ly = t->HomeSystem != nullptr ? Galaxy::DistanceLy(t->HomeSystem, sys) : 0;
			for (const auto& s : sys->World->Economy->Settlements)
			{
				if (!Stands(s) || s == t->Home || s->Owner == nullptr || s->Owner == t->Victor || s->Owner->IsPirate) continue;
				if (s->Owner != t->Origin && galaxy.Wars != nullptr && galaxy.Wars->Between(s->Owner, t->Origin) != nullptr) continue;
				int tier = s->Owner == t->Origin ? 0 : 1;
				bool better = best == nullptr || tier < bestTier
					|| (tier == bestTier && (ly < bestLy - 1e-9 || (cs::Math::Abs(ly - bestLy) <= 1e-9 && s->Population > best->Population)));
				if (!better) continue;
				best = s;
				bestSystem = sys;
				bestTier = tier;
				bestLy = ly;
			}
		}
		if (best == nullptr)
		{
			t->Stage = TribeStage::Scattered;
			t->EndedJd = jd;
			LogAt(galaxy, t->Home, jd, cs::Cat("Nowhere will take ", t->Name, " in: they scatter"), WorldEventKind::Loss);
			return false;
		}
		best->Population += t->People;
		t->Refuge = best;
		t->RefugeSystem = bestSystem;
		t->Stage = TribeStage::Resettled;
		t->ResettledJd = jd;
		t->NextPlotJd = jd + BrewDays + Roll(t->Id, t->Plots, jd, 0x5EED) * PlotJitterDays;
		LogAt(galaxy, best, jd, cs::Cat(t->Name, " (", cs::Fmt(t->People, "N0"), " people driven out by ", NameOf(t->Victor), ") settle at ", PlaceOf(galaxy, best),
			best->Owner == t->Origin ? cs::String(" among their own") : cs::Cat(", taken in by ", NameOf(best->Owner))), WorldEventKind::Info);
		return true;
	}

	void LastingConsequences::StagePlot(Galaxy& galaxy, const cs::Ref<RefugeeTribe>& t, double jd)
	{
		auto grudge = t->NextGrudge();
		auto finish = [&]
		{
			t->NextPlotJd = jd + PlotEveryDays + Roll(t->Id, t->Plots, jd, 0xA11E) * PlotJitterDays;
			if (t->Stage != TribeStage::Resettled) return;
			if (t->People < MinTribe / 5)
			{
				t->Stage = TribeStage::Scattered;
				t->EndedJd = jd;
				LogAt(galaxy, t->Refuge, jd, cs::Cat(t->Name, " are too few now to go on: the last of them scatter"), WorldEventKind::Info);
			}
			else if (t->NextGrudge() == nullptr || t->Plots >= MaxPlots)
			{
				t->Stage = TribeStage::Assimilated;
				t->EndedJd = jd;
				LogAt(galaxy, t->Refuge, jd, cs::Cat(t->Name, " put their grudges down and become one with the people of ", t->Refuge->Name), WorldEventKind::Info);
			}
		};
		if (grudge == nullptr)
		{
			finish();
			return;
		}

		auto plot = cs::New<RevengePlot>();
		plot->Tribe = t;
		plot->Jd = jd;
		// Their numbers make the plot: a few hundred zealots at worst, a whole people's network at best.
		double strength = cs::Math::Clamp(0.2 + 0.1 * cs::Math::Log10(cs::Math::Max(1.0, t->People / MinTribe)), 0.15, 0.6);

		if (grudge->SpaceGod)
		{
			plot->Kind = PlotKind::AssassinateSpaceGod;
			auto w = WorldOf(galaxy, t->Refuge);
			auto inf = SpaceGodInfluence::Of(w);
			int level = inf != nullptr ? inf->Level() : 1;
			plot->Odds = strength * (1 - static_cast<double>(level) / (SpaceGodInfluence::MaxLevel + 2));
			plot->Succeeded = Roll(t->Id, t->Plots, jd, 0xDEAD) < plot->Odds;
			if (plot->Succeeded)
			{
				if (inf != nullptr) inf->Xp = cs::Math::Max(SpaceGodInfluence::XpFor(level), inf->Xp - AssassinationXpLoss);
				grudge->SettledJd = jd;
				plot->Outcome = cs::Cat(t->Name, " strike at the SpaceGod: the body it last wore is cut down, and its reach falters");
			}
			else plot->Outcome = cs::Cat("An attempt on the SpaceGod by ", t->Name, " is foiled");
			if (w != nullptr) w->Log(jd, plot->Outcome, WorldEventKind::Law, t->Refuge->Body);
		}
		else
		{
			auto f = grudge->Against;
			bool homeHeld = Stands(t->Home) && t->Home->Owner == f;
			cs::Ref<Settlement> where = homeHeld ? t->Home : galaxy.Polities->CapitalOf(f);
			if (!Stands(where))
			{
				// Nothing of the victor's left to strike at: the grudge dies with them.
				grudge->SettledJd = jd;
				finish();
				return;
			}
			plot->Target = f;
			plot->Where = where;
			plot->Kind = homeHeld && t->Plots % 2 == 0 ? PlotKind::Revenge : PlotKind::Assassination;
			plot->Odds = strength * (1.2 - 0.6 * Hold(where));
			plot->Succeeded = Roll(t->Id, t->Plots, jd, 0xBEEF) < plot->Odds;
			auto w = WorldOf(galaxy, where);
			if (plot->Kind == PlotKind::Revenge)
			{
				if (plot->Succeeded)
				{
					where->Unrest = cs::Math::Min(1.0, cs::Math::Max(where->Unrest, 0.5) + 0.25);
					if (!cs::Double::IsNaN(where->Garrison)) where->Garrison *= 0.6;
					plot->Outcome = cs::Cat(t->Name, " stir ", where->Name, " up against ", NameOf(f), ": sabotage and riots, the garrison bled");
					// The garrison thin and the people up in arms: the old home rises and goes back to its own.
					bool ownAlive = t->Origin != nullptr && galaxy.Polities->SettlementsOf(t->Origin).Count() > 0;
					if (ownAlive && w != nullptr && where->Unrest >= 0.75 && Hold(where) < 0.3)
					{
						w->Economy->Capture(where, t->Origin);
						where->OccupiedFrom = nullptr;
						where->Garrison = InterstellarWars::FullGarrison(where) * 0.3;
						where->Unrest = 0.3;
						where->Population += t->People;
						if (w->War != nullptr) w->War->RevokeBookings();
						for (const auto& m : marks)
							if (m->Settlement == where)
							{
								m->Owner = where->Owner;
								m->OccupiedFrom = nullptr;
							}
						t->Stage = TribeStage::Returned;
						t->EndedJd = jd;
						t->Refuge = nullptr;
						t->RefugeSystem = nullptr;
						for (const auto& g : t->Grudges)
							if (g->Owed() && !g->SpaceGod) g->SettledJd = jd;
						plot->Outcome = cs::Cat(where->Name, " rises against ", NameOf(f), ": ", t->Name, " come home, and it declares for ", NameOf(t->Origin), " again");
					}
					else grudge->SettledJd = jd;
				}
				else
				{
					t->People = cs::Math::Floor(t->People * 0.9);
					where->Unrest = cs::Math::Min(1.0, where->Unrest + 0.05);
					plot->Outcome = cs::Cat(t->Name, "'s plot against ", NameOf(f), " at ", where->Name, " is broken up, its plotters hunted down");
				}
				if (w != nullptr) w->Log(jd, plot->Outcome, t->Stage == TribeStage::Returned ? WorldEventKind::Capture : WorldEventKind::Law, where->Body);
			}
			else
			{
				if (plot->Succeeded)
				{
					where->Unrest = cs::Math::Min(1.0, where->Unrest + 0.3);
					if (!cs::Double::IsNaN(where->Garrison)) where->Garrison *= 0.8;
					grudge->SettledJd = jd;
					plot->Outcome = cs::Cat("The governor ", NameOf(f), " set over ", where->Name, " is assassinated by ", t->Name);
				}
				else
				{
					t->People = cs::Math::Floor(t->People * 0.95);
					plot->Outcome = cs::Cat("An attempt by ", t->Name, " on the governor ", NameOf(f), " set over ", where->Name, " is foiled");
				}
				if (w != nullptr) w->Log(jd, plot->Outcome, WorldEventKind::Law, where->Body);
			}
			// Told where they live too, when that is another system.
			auto rw = WorldOf(galaxy, t->Refuge);
			if (rw != nullptr && rw != w) rw->Log(jd, plot->Outcome, WorldEventKind::Law, t->Refuge->Body);
		}
		t->Plots++;
		if (plot->Succeeded) t->PlotsSucceeded++;
		Plots.Add(plot);
		finish();
	}

	SG_ENUM(GodObjectiveKind, "SpaceGod.Core.GodObjectiveKind")
	SG_ENUM(GodObjectiveStatus, "SpaceGod.Core.GodObjectiveStatus")
	SG_ENUM(TribeStage, "SpaceGod.Core.TribeStage")
	SG_ENUM(PlotKind, "SpaceGod.Core.PlotKind")

	SG_SERIALIZE(GodObjective, "SpaceGod.Core.GodObjective",
		SG_F(Id), SG_F(War), SG_F(Side), SG_F(Against), SG_F(Kind), SG_F(Target), SG_F(System), SG_F(Place), SG_F(Status),
		SG_F(OfferedJd), SG_F(AcceptedJd), SG_F(ResolvedJd))

	SG_SERIALIZE(Grudge, "SpaceGod.Core.Grudge",
		SG_F(Against), SG_F(SpaceGod), SG_F(SinceJd), SG_F(SettledJd))

	SG_SERIALIZE(RefugeeTribe, "SpaceGod.Core.RefugeeTribe",
		SG_F(Id), SG_F(Name), SG_F(Origin), SG_F(Home), SG_F(HomeSystem), SG_F(Victor), SG_F(People), SG_F(Stage), SG_F(FledJd),
		SG_F(ResettledJd), SG_F(EndedJd), SG_F(Refuge), SG_F(RefugeSystem), SG_F(Grudges), SG_F(NextPlotJd), SG_F(Plots), SG_F(PlotsSucceeded))

	SG_SERIALIZE(RevengePlot, "SpaceGod.Core.RevengePlot",
		SG_F(Tribe), SG_F(Kind), SG_F(Target), SG_F(Where), SG_F(Jd), SG_F(Odds), SG_F(Succeeded), SG_F(Outcome))

	SG_SERIALIZE(OwnerMark, "SpaceGod.Core.OwnerMark",
		SG_F(Settlement), SG_F(Owner), SG_F(OccupiedFrom))

	SG_SERIALIZE(LastingConsequences, "SpaceGod.Core.LastingConsequences",
		SG_F(Objectives), SG_F(Tribes), SG_F(Plots), SG_F(marks), SG_F(nextDailyJd), SG_F(nextObjectiveId), SG_F(nextTribeId))
}
