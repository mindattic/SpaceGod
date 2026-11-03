// Port of Unity.SpaceGod Core/Systems/Statecraft.cs (US-T15, see docs/MIGRATION.md).

#include "Systems/Statecraft.h"

#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Combat/War.h"
#include "Economy/Settlement.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarWars.h"
#include "Systems/Polities.h"
#include "Systems/StrategicCommand.h"
#include "World.h"

namespace SpaceGod::Core
{
	// ==================================================================== PactKind

	struct PactKind::Statics
	{
		cs::Ref<PactKind> NonAggression, Trade, Alliance, Tribute, Vassalage;
	};

	cs::List<cs::Ref<PactKind>>& PactKind::all()
	{
		static cs::List<cs::Ref<PactKind>> list = cs::List<cs::Ref<PactKind>>();
		return list;
	}

	const PactKind::Statics& PactKind::statics()
	{
		all();
		static const Statics s{
			Add("Non-aggression pact", "Neither goes to war with the other for five years",
				false, true, false, 5 * 365.25, 10, nullptr),
			Add("Trade pact", "Tariffs lifted between them: both treasuries gain from the trade every day",
				false, false, false, cs::Double::NaN, 15, [](Statecraft* s, cs::Ref<Pact> p, double jd) { return s->KeepTrade(p); }),
			Add("Alliance", "At peace with each other, and each joins the wars the other is attacked in",
				false, true, true, cs::Double::NaN, 35, nullptr),
			Add("Tribute", "One pays the other a sum every month for two years",
				true, true, false, 2 * 365.25, -5, [](Statecraft* s, cs::Ref<Pact> p, double jd) { return s->KeepTribute(p, jd); }),
			Add("Vassalage", "The vassal pays a share of its treasury to its overlord, fights its wars and is defended in its own",
				true, true, true, cs::Double::NaN, 5, [](Statecraft* s, cs::Ref<Pact> p, double jd) { return s->KeepVassalage(p, jd); }),
		};
		return s;
	}

	const cs::List<cs::Ref<PactKind>>& PactKind::All() { statics(); return all(); }

	PactKind::PactKind(cs::String name, cs::String description, bool directed, bool keepsPeace, bool callsToArms, double days, double warmth,
		cs::Func<bool, Statecraft*, cs::Ref<Pact>, double> daily)
	{
		Name = name;
		Description = description;
		Directed = directed;
		KeepsPeace = keepsPeace;
		CallsToArms = callsToArms;
		Days = days;
		Warmth = warmth;
		Daily = daily;
	}

	cs::Ref<PactKind> PactKind::Add(cs::String name, cs::String description, bool directed, bool keepsPeace, bool callsToArms, double days,
		double warmth, cs::Func<bool, Statecraft*, cs::Ref<Pact>, double> daily)
	{
		auto k = cs::Ref<PactKind>(new PactKind(name, description, directed, keepsPeace, callsToArms, days, warmth, daily));
		all().Add(k);
		return k;
	}

	cs::Ref<PactKind> PactKind::Register(cs::String name, cs::String description, bool directed, bool keepsPeace, bool callsToArms, double days,
		double warmth, cs::Func<bool, Statecraft*, cs::Ref<Pact>, double> daily)
	{
		statics();
		return Add(name, description, directed, keepsPeace, callsToArms, days, warmth, daily);
	}

	cs::Ref<PactKind> PactKind::Find(const cs::String& name)
	{
		statics();
		return all().Find([&](const cs::Ref<PactKind>& k) { return k->Name == name; });
	}

	const cs::Ref<PactKind>& PactKind::NonAggression() { return statics().NonAggression; }
	const cs::Ref<PactKind>& PactKind::Trade() { return statics().Trade; }
	const cs::Ref<PactKind>& PactKind::Alliance() { return statics().Alliance; }
	const cs::Ref<PactKind>& PactKind::Tribute() { return statics().Tribute; }
	const cs::Ref<PactKind>& PactKind::Vassalage() { return statics().Vassalage; }

	// ==================================================================== Pact

	cs::String Pact::Describe() const
	{
		if (Kind == PactKind::Tribute()) return cs::Cat("the ", A->Name, " pays the ", B->Name, " ", cs::Fmt(Amount, "N0"), " cr a month");
		if (Kind == PactKind::Vassalage()) return cs::Cat("the ", A->Name, " is the ", B->Name, "'s vassal");
		return cs::Cat(cs::Strings::ToLowerInvariant(Kind->Name), " between the ", A->Name, " and the ", B->Name);
	}

	// ==================================================================== Proposal

	cs::String Proposal::Describe() const { return Deal->Describe(*this); }

	// ==================================================================== Appraisal

	Appraisal* Appraisal::Add(double weight, const cs::String& why)
	{
		Score += weight;
		if (!why.empty() && cs::Math::Abs(weight) >= 0.5) Reasons.Add(Reason{ weight, why });
		return this;
	}

	cs::String Appraisal::Main() const
	{
		cs::String best;
		double w = 0;
		for (const auto& [weight, why] : Reasons)
			if ((Accepted() ? weight > w : weight < w))
			{
				w = weight;
				best = why;
			}
		return !best.empty() ? best : cs::String(Accepted() ? "it suits them" : "it does not suit them");
	}

	// ==================================================================== Deal

	struct Deal::Statics
	{
		cs::Ref<Deal> Peace, NonAggression, TradePact, Alliance, OfferTribute, DemandTribute, OfferFealty, DemandFealty;
	};

	cs::List<cs::Ref<Deal>>& Deal::all()
	{
		static cs::List<cs::Ref<Deal>> list = cs::List<cs::Ref<Deal>>();
		return list;
	}

	cs::String Deal::Cr(double v) { return cs::Cat(cs::Fmt(v, "N0"), " cr"); }

	const Deal::Statics& Deal::statics()
	{
		all();
		static const Statics s{
			Add("Peace", "End the war between you: white peace, all occupied ground given back unless a treaty cedes it", false,
				[](Statecraft* s, cs::Ref<Faction> a, cs::Ref<Faction> b, double jd) { return s->Polities->Diplomacy->Get(a, b) == Relation::Peace ? cs::String("You are not at war") : cs::String(); },
				[](Statecraft* s, cs::Ref<Proposal> p, double jd) { return s->AppraisePeace(p->To, p->From, jd); },
				[](Statecraft* s, cs::Ref<Proposal> p, double jd) { s->MakePeace(p->From, p->To, jd, "terms agreed through their envoys"); },
				[](const Proposal& p) { return cs::Cat("the ", p.From->Name, " offers the ", p.To->Name, " peace"); }),

			Add("Non-aggression pact", PactKind::NonAggression()->Description, false,
				[](Statecraft* s, cs::Ref<Faction> a, cs::Ref<Faction> b, double jd)
				{
					auto r = s->NotAtPeace(a, b);
					if (!r.empty()) return r;
					return s->PactBetween(a, b, PactKind::NonAggression()) != nullptr || s->PactBetween(a, b, PactKind::Alliance()) != nullptr ? cs::String("Already bound not to fight") : cs::String();
				},
				[](Statecraft* s, cs::Ref<Proposal> p, double jd) { return s->AppraiseNonAggression(p->To, p->From); },
				[](Statecraft* s, cs::Ref<Proposal> p, double jd) { s->Sign(PactKind::NonAggression(), p->From, p->To, jd); },
				nullptr),

			Add("Trade pact", PactKind::Trade()->Description, false,
				[](Statecraft* s, cs::Ref<Faction> a, cs::Ref<Faction> b, double jd)
				{
					auto r = s->NotAtPeace(a, b);
					if (!r.empty()) return r;
					return s->PactBetween(a, b, PactKind::Trade()) != nullptr ? cs::String("Already trading under a pact")
						: s->Polities->Diplomacy->IsEmbargoed(a, b) || s->Polities->Diplomacy->IsEmbargoed(b, a) ? cs::String("An embargo stands between you") : cs::String();
				},
				[](Statecraft* s, cs::Ref<Proposal> p, double jd) { return s->AppraiseTrade(p->To, p->From); },
				[](Statecraft* s, cs::Ref<Proposal> p, double jd) { s->Sign(PactKind::Trade(), p->From, p->To, jd); },
				nullptr),

			Add("Alliance", PactKind::Alliance()->Description, false,
				[](Statecraft* s, cs::Ref<Faction> a, cs::Ref<Faction> b, double jd)
				{
					auto r = s->NotAtPeace(a, b);
					if (!r.empty()) return r;
					return s->PactBetween(a, b, PactKind::Alliance()) != nullptr ? cs::String("Already allies") : s->Liege(a, b) ? cs::String("A vassal and its overlord") : cs::String();
				},
				[](Statecraft* s, cs::Ref<Proposal> p, double jd) { return s->AppraiseAlliance(p->To, p->From); },
				[](Statecraft* s, cs::Ref<Proposal> p, double jd) { s->Sign(PactKind::Alliance(), p->From, p->To, jd); },
				nullptr),

			Add("Offer tribute", "Pay them a sum every month for two years: goodwill, and peace bought", true,
				[](Statecraft* s, cs::Ref<Faction> a, cs::Ref<Faction> b, double jd)
				{
					auto r = s->NotAtPeace(a, b);
					if (!r.empty()) return r;
					return s->TributeBetween(a, b) != nullptr ? cs::String("Tribute already flows between you") : cs::String();
				},
				[](Statecraft* s, cs::Ref<Proposal> p, double jd) { return s->AppraiseTributeReceived(p->To, p->From, p->Amount); },
				[](Statecraft* s, cs::Ref<Proposal> p, double jd) { s->Sign(PactKind::Tribute(), p->From, p->To, jd, p->Amount); },
				[](const Proposal& p) { return cs::Cat("the ", p.From->Name, " offers the ", p.To->Name, " tribute of ", Cr(p.Amount), " a month"); }),

			Add("Demand tribute", "They pay you a sum every month for two years, or face your navy", true,
				[](Statecraft* s, cs::Ref<Faction> a, cs::Ref<Faction> b, double jd)
				{
					auto r = s->NotAtPeace(a, b);
					if (!r.empty()) return r;
					return s->TributeBetween(a, b) != nullptr ? cs::String("Tribute already flows between you") : cs::String();
				},
				[](Statecraft* s, cs::Ref<Proposal> p, double jd) { return s->AppraiseTributePaid(p->To, p->From, p->Amount); },
				[](Statecraft* s, cs::Ref<Proposal> p, double jd) { s->Sign(PactKind::Tribute(), p->To, p->From, jd, p->Amount); },
				[](const Proposal& p) { return cs::Cat("the ", p.From->Name, " demands tribute of ", Cr(p.Amount), " a month from the ", p.To->Name); }),

			Add("Offer fealty", "Become their vassal: their protection, for a share of your treasury and your navy in their wars", false,
				[](Statecraft* s, cs::Ref<Faction> a, cs::Ref<Faction> b, double jd)
				{
					auto r = s->NotAtPeace(a, b);
					if (!r.empty()) return r;
					return s->OverlordOf(a) != nullptr ? cs::String("Already a vassal") : s->OverlordOf(b) == a ? cs::String("They are your vassal") : cs::String();
				},
				[](Statecraft* s, cs::Ref<Proposal> p, double jd) { return s->AppraiseVassalTaken(p->To, p->From); },
				[](Statecraft* s, cs::Ref<Proposal> p, double jd) { s->Sign(PactKind::Vassalage(), p->From, p->To, jd); },
				[](const Proposal& p) { return cs::Cat("the ", p.From->Name, " offers its fealty to the ", p.To->Name); }),

			Add("Demand fealty", "They become your vassal (a war between you ends with it)", false,
				[](Statecraft* s, cs::Ref<Faction> a, cs::Ref<Faction> b, double jd)
				{
					return s->OverlordOf(b) != nullptr ? cs::String("They already serve an overlord") : s->OverlordOf(a) == b ? cs::String("You are their vassal") : s->OverlordOf(a) != nullptr ? cs::String("A vassal takes no vassals") : cs::String();
				},
				[](Statecraft* s, cs::Ref<Proposal> p, double jd) { return s->AppraiseFealty(p->To, p->From, jd); },
				[](Statecraft* s, cs::Ref<Proposal> p, double jd)
				{
					if (s->Polities->Diplomacy->Get(p->From, p->To) != Relation::Peace) s->MakePeace(p->From, p->To, jd, cs::Cat("the ", p->To->Name, " submits to the ", p->From->Name));
					s->Sign(PactKind::Vassalage(), p->To, p->From, jd);
				},
				[](const Proposal& p) { return cs::Cat("the ", p.From->Name, " demands the ", p.To->Name, "'s fealty"); }),
		};
		return s;
	}

	const cs::List<cs::Ref<Deal>>& Deal::All() { statics(); return all(); }

	Deal::Deal(cs::String name, cs::String description, bool hasAmount, cs::Func<cs::String, Statecraft*, cs::Ref<Faction>, cs::Ref<Faction>, double> unavailable,
		cs::Func<cs::Ref<Appraisal>, Statecraft*, cs::Ref<Proposal>, double> appraise, cs::Action<Statecraft*, cs::Ref<Proposal>, double> enact,
		cs::Func<cs::String, const Proposal&> words)
	{
		Name = name;
		Description = description;
		HasAmount = hasAmount;
		Unavailable = unavailable;
		Appraise = appraise;
		Enact = enact;
		Words = words;
	}

	cs::Ref<Deal> Deal::Add(cs::String name, cs::String description, bool hasAmount, cs::Func<cs::String, Statecraft*, cs::Ref<Faction>, cs::Ref<Faction>, double> unavailable,
		cs::Func<cs::Ref<Appraisal>, Statecraft*, cs::Ref<Proposal>, double> appraise, cs::Action<Statecraft*, cs::Ref<Proposal>, double> enact,
		cs::Func<cs::String, const Proposal&> words)
	{
		auto d = cs::Ref<Deal>(new Deal(name, description, hasAmount, unavailable, appraise, enact, words));
		all().Add(d);
		return d;
	}

	cs::Ref<Deal> Deal::Register(cs::String name, cs::String description, bool hasAmount, cs::Func<cs::String, Statecraft*, cs::Ref<Faction>, cs::Ref<Faction>, double> unavailable,
		cs::Func<cs::Ref<Appraisal>, Statecraft*, cs::Ref<Proposal>, double> appraise, cs::Action<Statecraft*, cs::Ref<Proposal>, double> enact,
		cs::Func<cs::String, const Proposal&> words)
	{
		statics();
		return Add(name, description, hasAmount, unavailable, appraise, enact, words);
	}

	cs::Ref<Deal> Deal::Find(const cs::String& name)
	{
		statics();
		return all().Find([&](const cs::Ref<Deal>& d) { return d->Name == name; });
	}

	cs::String Deal::Describe(const Proposal& p) const
	{
		return Words ? Words(p) : cs::Cat("the ", p.From->Name, " proposes ", cs::Strings::ToLowerInvariant(Name), " to the ", p.To->Name);
	}

	const cs::Ref<Deal>& Deal::Peace() { return statics().Peace; }
	const cs::Ref<Deal>& Deal::NonAggression() { return statics().NonAggression; }
	const cs::Ref<Deal>& Deal::TradePact() { return statics().TradePact; }
	const cs::Ref<Deal>& Deal::Alliance() { return statics().Alliance; }
	const cs::Ref<Deal>& Deal::OfferTribute() { return statics().OfferTribute; }
	const cs::Ref<Deal>& Deal::DemandTribute() { return statics().DemandTribute; }
	const cs::Ref<Deal>& Deal::OfferFealty() { return statics().OfferFealty; }
	const cs::Ref<Deal>& Deal::DemandFealty() { return statics().DemandFealty; }

	// ==================================================================== Statecraft

	const cs::Ref<ToggleSetting>& Statecraft::AiDiplomacy()
	{
		static const cs::Ref<ToggleSetting> s = cs::New<ToggleSetting>("diplomacy.ai", SettingGroups::Civilizations, "AI diplomacy", true,
			"Whether the polities put pacts, tribute, fealty and peace to each other (and to you) of their own accord.", "diplomacy treaty alliance pact ai");
		return s;
	}

	const cs::Ref<RangeSetting>& Statecraft::OpinionPace()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("diplomacy.opinionPace", SettingGroups::Civilizations, "Diplomatic memory", 1, 0.25, 4, 0.25,
			"How fast the polities' opinions of each other move toward what their pacts, wars and rivalries make them (higher forgets faster).",
			[](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }, "diplomacy opinion relations");
		return s;
	}

	const cs::Ref<RangeSetting>& Statecraft::TradePactIncome()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("diplomacy.tradeIncome", SettingGroups::Civilizations, "Trade pact income", 1, 0, 5, 0.25,
			"How much a trade pact pays each side a day, as a multiple of the standard.",
			[](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }, "diplomacy trade pact treasury");
		return s;
	}

	const cs::Ref<RangeSetting>& Statecraft::VassalLevy()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("diplomacy.vassalLevy", SettingGroups::Civilizations, "Vassal levy", 0.1, 0, 0.5, 0.05,
			"The share of its treasury a vassal pays its overlord each month.",
			[](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }, "diplomacy vassal levy tribute");
		return s;
	}

	cs::Ref<Faction> Statecraft::Player() const { return Polities->Command->Player; }

	bool Statecraft::Party(const cs::Ref<Faction>& f) { return f != nullptr && !f->IsPirate && f->Kind == PolityKind::State; }

	cs::List<cs::Ref<Faction>> Statecraft::Parties() const
	{
		auto list = cs::List<cs::Ref<Faction>>();
		for (const auto& f : Polities->Factions())
			if (Party(f)) list.Add(f);
		return list;
	}

	// ---------------------------------------------------------------- opinion

	double Statecraft::Opinion(const cs::Ref<Faction>& of, const cs::Ref<Faction>& toward) const
	{
		double v = 0;
		return of == nullptr || toward == nullptr || of == toward ? 100 : opinions.TryGetValue(FactionPair{ of, toward }, v) ? v : 0;
	}

	void Statecraft::Sway(const cs::Ref<Faction>& of, const cs::Ref<Faction>& toward, double by)
	{
		if (of == nullptr || toward == nullptr || of == toward) return;
		double v = cs::Math::Max(-100.0, cs::Math::Min(100.0, Opinion(of, toward) + by));
		opinions[FactionPair{ of, toward }] = v;
	}

	double Statecraft::Baseline(const cs::Ref<Faction>& of, const cs::Ref<Faction>& toward)
	{
		double b = 0;
		auto dip = Polities->Diplomacy;
		if (dip->Get(of, toward) == Relation::War) b -= 50;
		else if (dip->Get(of, toward) == Relation::Ceasefire) b -= 20;
		for (const auto& p : pacts)
			if (p->Between(of, toward)) b += p->Kind == PactKind::Tribute() && p->A == of ? -15 : p->Kind->Warmth;
		// A common enemy draws two together; a friend's enemy is not loved.
		for (const auto& g : Polities->Factions())
		{
			if (g == of || g == toward || !Party(g)) continue;
			bool ofFights = dip->Get(of, g) == Relation::War, theyFight = dip->Get(toward, g) == Relation::War;
			if (ofFights && theyFight) b += 20;
		}
		// Ground it lost to them still rankles.
		for (const auto& s : Polities->SettlementsOf(toward))
			if (s->OccupiedFrom == of)
			{
				b -= 25;
				break;
			}
		// A navy that could swallow it is a worry.
		if (Power(toward) > 3 * Power(of)) b -= 10;
		if (dip->IsEmbargoed(toward, of)) b -= 15;
		return cs::Math::Max(-100.0, cs::Math::Min(100.0, b));
	}

	double Statecraft::Power(const cs::Ref<Faction>& f)
	{
		double total = 1;
		if (f == nullptr) return total;
		// Within a day's reckoning each navy is counted once: a count walks every ship in the galaxy.
		double known = 0;
		if (powerCache != nullptr && powerCache->TryGetValue(f, known)) return known;
		for (const auto& s : Polities->Navy(f)) total += s->Combat->Strength();
		if (powerCache != nullptr) (*powerCache)[f] = total;
		return total;
	}

	// ---------------------------------------------------------------- pacts

	cs::Ref<Pact> Statecraft::PactBetween(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, const cs::Ref<PactKind>& kind) const
	{
		for (const auto& p : pacts)
			if (p->Kind == kind && p->Between(a, b)) return p;
		return nullptr;
	}

	cs::List<cs::Ref<Pact>> Statecraft::PactsOf(const cs::Ref<Faction>& f) const
	{
		auto list = cs::List<cs::Ref<Pact>>();
		for (const auto& p : pacts)
			if (p->Involves(f)) list.Add(p);
		return list;
	}

	cs::Ref<Faction> Statecraft::OverlordOf(const cs::Ref<Faction>& vassal) const
	{
		for (const auto& p : pacts)
			if (p->Kind == PactKind::Vassalage() && p->A == vassal) return p->B;
		return nullptr;
	}

	bool Statecraft::BoundToPeace(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) const
	{
		for (const auto& p : pacts)
			if (p->Kind->KeepsPeace && p->Between(a, b)) return true;
		return false;
	}

	cs::Ref<Pact> Statecraft::Sign(const cs::Ref<PactKind>& kind, const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, double jd, double amount)
	{
		if (kind == nullptr || a == nullptr || b == nullptr || a == b) return nullptr;
		auto p = cs::New<Pact>();
		p->Kind = kind;
		p->A = a;
		p->B = b;
		p->SinceJd = jd;
		p->UntilJd = cs::Double::IsNaN(kind->Days) ? cs::Double::NaN : jd + kind->Days;
		p->Amount = amount;
		pacts.Add(p);
		if (kind == PactKind::Trade())
		{
			// Trade under a pact: neither keeps the other's ships out.
			auto embargoes = cs::List<cs::Ref<Embargo>>(Polities->Diplomacy->Embargoes());
			for (const auto& e : embargoes)
				if ((e->By == a && e->Against == b) || (e->By == b && e->Against == a)) Polities->Diplomacy->LiftEmbargo(e);
		}
		Sway(a, b, 10);
		Sway(b, a, 10);
		Tell(jd, cs::Cat(Capitalise(p->Describe()), ": ", (kind == PactKind::Tribute() || kind == PactKind::Vassalage() ? cs::String("signed") : cs::Cat("signed, ", cs::Strings::ToLowerInvariant(kind->Description)))), a, b);
		return p;
	}

	bool Statecraft::Break(const cs::Ref<Pact>& p, const cs::Ref<Faction>& by, double jd, const cs::String& why)
	{
		if (p == nullptr || !pacts.Remove(p)) return false;
		auto other = p->Other(by);
		if (other != nullptr)
		{
			double hurt = p->Kind == PactKind::Vassalage() && p->A == by ? 60 : p->Kind->KeepsPeace || p->Kind->CallsToArms ? 35 : 15;
			Sway(other, by, -hurt);
			if (p->Kind->KeepsPeace)
				for (const auto& g : Parties())
					if (g != by && g != other) Sway(g, by, -8);
		}
		cs::String what = p->Kind == PactKind::Vassalage() && p->A == by ? cs::Cat("The ", by->Name, " throws off the ", p->B->Name, "'s rule and declares its independence")
			: cs::Cat("The ", (by ? by->Name : cs::String("one side")), " breaks the ", Lower(p->Describe()));
		Tell(jd, cs::Cat(what, (!why.empty() ? cs::Cat(": ", why) : cs::String())), p->A, p->B);
		return true;
	}

	void Statecraft::Expire(const cs::Ref<Pact>& p, double jd)
	{
		pacts.Remove(p);
		Tell(jd, cs::Cat(Capitalise(p->Describe()), " has run its course"), p->A, p->B);
	}

	bool Statecraft::KeepTrade(const cs::Ref<Pact>& p)
	{
		double each = TradePactIncome()->Value * TradePerDay * cs::Math::Sqrt(cs::Math::Max(0.0, cs::Math::Min(People(p->A), People(p->B))) / 1e6);
		p->A->Treasury += each;
		p->B->Treasury += each;
		return true;
	}

	bool Statecraft::KeepTribute(const cs::Ref<Pact>& p, double jd)
	{
		double due = p->Amount / 30;
		if (p->A->Treasury < due) return false;
		p->A->Treasury -= due;
		p->B->Treasury += due;
		Stir(p->A);
		return true;
	}

	bool Statecraft::KeepVassalage(const cs::Ref<Pact>& p, double jd)
	{
		double levy = cs::Math::Max(0.0, p->A->Treasury) * VassalLevy()->Value / 30;
		p->A->Treasury -= levy;
		p->B->Treasury += levy;
		Stir(p->A);
		return true;
	}

	void Statecraft::Stir(const cs::Ref<Faction>& payer)
	{
		for (const auto& s : Polities->SettlementsOf(payer))
			if (s->Unrest < TributeUnrestCap) s->Unrest = cs::Math::Min(TributeUnrestCap, s->Unrest + TributeUnrestPerDay);
	}

	double Statecraft::People(const cs::Ref<Faction>& f) const
	{
		double n = 0;
		for (const auto& s : Polities->SettlementsOf(f)) n += s->Population;
		return n;
	}

	// ---------------------------------------------------------------- proposals

	cs::String Statecraft::Unavailable(const cs::Ref<Deal>& deal, const cs::Ref<Faction>& from, const cs::Ref<Faction>& to, double jd)
	{
		if (deal == nullptr || from == nullptr || to == nullptr || from == to) return "Nobody to put it to";
		if (!Party(from) || !Party(to)) return "Only states treat with each other";
		return deal->Unavailable ? deal->Unavailable(this, from, to, jd) : cs::String();
	}

	cs::Ref<Appraisal> Statecraft::Weigh(const cs::Ref<Deal>& deal, const cs::Ref<Faction>& from, const cs::Ref<Faction>& to, double jd, double amount)
	{
		auto p = cs::New<Proposal>();
		p->Deal = deal;
		p->From = from;
		p->To = to;
		p->Amount = amount;
		p->Jd = jd;
		return deal->Appraise(this, p, jd);
	}

	cs::String Statecraft::Propose(const cs::Ref<Deal>& deal, const cs::Ref<Faction>& from, const cs::Ref<Faction>& to, double jd, cs::Ref<Appraisal>& answer, double amount)
	{
		answer = nullptr;
		auto why = Unavailable(deal, from, to, jd);
		if (!why.empty()) return why;
		auto p = cs::New<Proposal>();
		p->Deal = deal;
		p->From = from;
		p->To = to;
		p->Amount = amount;
		p->Jd = jd;
		if (to == Player() && from != Player())
		{
			if (inbox.Exists([&](const cs::Ref<Proposal>& x) { return x->From == from && x->To == to && x->Deal == deal; })) return "Already put to them";
			inbox.Add(p);
			Tell(jd, cs::Cat(Capitalise(p->Describe()), ", and awaits an answer"), from, to);
			return cs::String();
		}
		answer = deal->Appraise(this, p, jd);
		if (answer->Accepted())
		{
			deal->Enact(this, p, jd);
			if (deal != Deal::Peace() && deal != Deal::DemandFealty()) record.Add(cs::Cat(Capitalise(p->Describe()), ": agreed"));
		}
		else
		{
			Sway(from, to, deal == Deal::DemandTribute() || deal == Deal::DemandFealty() ? -2 : -1);
			Sway(to, from, deal == Deal::DemandTribute() || deal == Deal::DemandFealty() ? -10 : 0);
			Tell(jd, cs::Cat(Capitalise(p->Describe()), ": refused, ", answer->Main()), from, to);
		}
		return cs::String();
	}

	bool Statecraft::Answer(const cs::Ref<Proposal>& p, bool accept, double jd)
	{
		if (p == nullptr || !inbox.Remove(p)) return false;
		if (!Unavailable(p->Deal, p->From, p->To, jd).empty()) return false;
		if (accept)
		{
			p->Deal->Enact(this, p, jd);
			record.Add(cs::Cat(Capitalise(p->Describe()), ": agreed"));
		}
		else
		{
			Sway(p->From, p->To, p->Deal == Deal::DemandTribute() || p->Deal == Deal::DemandFealty() ? -15 : -5);
			Tell(jd, cs::Cat(Capitalise(p->Describe()), ": refused"), p->From, p->To);
			if (p->Deal == Deal::DemandTribute() || p->Deal == Deal::DemandFealty()) Spurned(p->From, p->To, jd);
		}
		return true;
	}

	// ---------------------------------------------------------------- acts

	cs::String Statecraft::DeclareWar(const cs::Ref<Faction>& by, const cs::Ref<Faction>& against, double jd)
	{
		if (by == nullptr || against == nullptr || by == against) return "Nobody to declare war on";
		if (!Party(by) || !Party(against)) return "Only states declare war on each other";
		if (Polities->Diplomacy->Get(by, against) == Relation::War) return "Already at war";
		auto liege = OverlordOf(by);
		if (liege != nullptr && liege != against) return cs::Cat("A vassal makes no war of its own: its overlord, the ", liege->Name, ", decides");
		auto snapshot = cs::List<cs::Ref<Pact>>(pacts);
		for (const auto& p : snapshot)
			if (p->Between(by, against)) Break(p, by, jd, "war declared");
		cs::Ref<Conflict> c = nullptr;
		if (Galaxy && Galaxy->Wars != nullptr && !Polities->Diplomacy->InTruce(by, against, jd)) c = Galaxy->Wars->Declare(by, against, jd);
		if (c == nullptr)
		{
			Polities->Diplomacy->Set(by, against, Relation::War);
			for (const auto& w : Polities->Worlds())
				if (w->War) w->War->RevokeBookings();
			Tell(jd, cs::Cat("The ", by->Name, " declares war on the ", against->Name), by, against);
		}
		else record.Add(cs::Cat("The ", by->Name, " declares war on the ", against->Name));
		// Breaking a truce or just going to war on someone is noticed by everyone.
		for (const auto& g : Parties())
			if (g != by && g != against) Sway(g, by, Polities->Diplomacy->InTruce(by, against, jd) ? -15 : -5);
		Sway(against, by, -40);
		Began(by, against, jd);
		CallToArms(jd);
		return cs::String();
	}

	void Statecraft::MakePeace(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, double jd, const cs::String& why)
	{
		auto dip = Polities->Diplomacy;
		if (dip->Get(a, b) == Relation::Peace) return;
		dip->Set(a, b, Relation::Peace);
		if (!(Galaxy && Galaxy->Wars && Galaxy->Wars->Between(a, b) != nullptr))
		{
			auto t = cs::New<Treaty>();
			t->A = a;
			t->B = b;
			t->Jd = jd;
			t->TruceUntilJd = jd + InterstellarWars::TruceDays;
			t->Terms = cs::Cat("Peace between the ", a->Name, " and the ", b->Name, ": ", why);
			dip->Sign(t);
		}
		warsBegun.Remove(Key(a, b));
		coBelligerents.RemoveAll([&](const cs::Ref<CoBelligerence>& x) { return (x->Ally == a && x->Enemy == b) || (x->Ally == b && x->Enemy == a); });
		Sway(a, b, 15);
		Sway(b, a, 15);
		Tell(jd, cs::Cat("Peace between the ", a->Name, " and the ", b->Name, ": ", why), a, b);
		for (const auto& w : Polities->Worlds())
			if (w->War) w->War->RevokeBookings();
	}

	cs::String Statecraft::Gift(const cs::Ref<Faction>& by, const cs::Ref<Faction>& to, double amount, double jd)
	{
		if (by == nullptr || to == nullptr || by == to || amount <= 0) return "Nothing to give";
		if (by->Treasury < amount) return "The treasury cannot spare it";
		by->Treasury -= amount;
		to->Treasury += amount;
		Sway(to, by, cs::Math::Min(30.0, 40 * amount / cs::Math::Max(1e6, cs::Math::Abs(to->Treasury))));
		Tell(jd, cs::Cat("The ", by->Name, " sends the ", to->Name, " a gift of ", cs::Fmt(amount, "N0"), " cr"), by, to);
		return cs::String();
	}

	double Statecraft::SuggestedTribute(const cs::Ref<Faction>& payer)
	{
		return cs::Math::Max(1e5, cs::Math::Round(cs::Math::Max(0.0, payer ? payer->Treasury : 0.0) * 0.02 / 1e4) * 1e4);
	}

	double Statecraft::SuggestedGift(const cs::Ref<Faction>& giver)
	{
		return cs::Math::Max(1e5, cs::Math::Round(cs::Math::Max(0.0, giver ? giver->Treasury : 0.0) * 0.01 / 1e4) * 1e4);
	}

	// ---------------------------------------------------------------- the envoy

	cs::List<cs::Ref<EnvoyOption>> Statecraft::Envoy(const cs::Ref<Faction>& from, const cs::Ref<Faction>& to, double jd)
	{
		auto list = cs::List<cs::Ref<EnvoyOption>>();
		for (const auto& deal : Deal::All())
		{
			double amount = deal == Deal::OfferTribute() ? SuggestedTribute(from) : deal == Deal::DemandTribute() ? SuggestedTribute(to) : 0;
			auto why = Unavailable(deal, from, to, jd);
			auto o = cs::New<EnvoyOption>();
			o->Deal = deal;
			o->Amount = amount;
			o->Unavailable = why;
			o->Read = why.empty() ? Weigh(deal, from, to, jd, amount) : nullptr;
			list.Add(o);
		}
		return list;
	}

	cs::String Statecraft::Mood(double opinion)
	{
		return opinion <= -60 ? "hostile" : opinion <= -25 ? "cold" : opinion < -5 ? "wary" : opinion < 15 ? "indifferent" : opinion < 45 ? "cordial" : opinion < 75 ? "friendly" : "warm";
	}

	cs::String Statecraft::Hail(const cs::Ref<Faction>& from, const cs::Ref<Faction>& to, double jd)
	{
		if (from == nullptr || to == nullptr || from == to) return "Nobody answers.";
		if (!Party(to)) return cs::Cat("The ", to->Name, " does not treat with states.");
		if (!Party(from)) return cs::Cat("The ", to->Name, " treats only with states: speak for one (the galaxy map's DIPLOMACY panel).");
		double op = Opinion(to, from);
		auto dip = Polities->Diplomacy;
		cs::String say;
		if (dip->Get(from, to) == Relation::War)
			say = op <= -60 ? cs::Cat("\"We have nothing to say to the ", from->Name, ", save with our guns.\"")
				: AppraisePeace(to, from, jd)->Accepted() ? cs::String("\"We would hear your terms for peace.\"") : cs::String("\"Peace? Not while the war goes our way.\"");
		else if (OverlordOf(to) == from) say = "\"Our liege's word is heard.\"";
		else if (OverlordOf(from) == to) say = cs::Cat("\"The ", to->Name, " hears its vassal.\"");
		else if (PactBetween(from, to, PactKind::Alliance()) != nullptr) say = "\"Well met, ally.\"";
		else
			say = op <= -25 ? "\"State your business and be gone.\"" : op < 15 ? "\"We are listening.\"" : "\"Always glad to hear from friends.\"";
		int waiting = 0;
		for (const auto& p : inbox)
			if (p->From == to && p->To == from) waiting++;
		return cs::Cat("The ", to->Name, " (", Mood(op), ", ", Standing(from, to), "): ", say,
			(waiting > 0 ? cs::Cat(" ", waiting, " proposal", (waiting == 1 ? "" : "s"), " of theirs await", (waiting == 1 ? "s" : ""), " your answer.") : cs::String()));
	}

	// ---------------------------------------------------------------- weighing

	cs::String Statecraft::NotAtPeace(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) const
	{
		return Polities->Diplomacy->Get(a, b) != Relation::Peace ? cs::String("Make peace first") : cs::String();
	}

	double Statecraft::WarDays(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, double jd) const
	{
		auto c = Galaxy && Galaxy->Wars ? Galaxy->Wars->Between(a, b) : nullptr;
		if (c != nullptr) return jd - c->StartJd;
		WarBegun w;
		return warsBegun.TryGetValue(Key(a, b), w) ? jd - w.Since : 0;
	}

	Statecraft::FactionPair Statecraft::Key(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b)
	{
		return cs::Strings::CompareOrdinal(a->Name, b->Name) <= 0 ? FactionPair{ a, b } : FactionPair{ b, a };
	}

	void Statecraft::Began(const cs::Ref<Faction>& by, const cs::Ref<Faction>& against, double jd)
	{
		if (!warsBegun.ContainsKey(Key(by, against))) warsBegun[Key(by, against)] = WarBegun{ by, jd };
	}

	bool Statecraft::CommonEnemy(const cs::Ref<Faction>& f, const cs::Ref<Faction>& g) const
	{
		auto dip = Polities->Diplomacy;
		for (const auto& h : Parties())
		{
			if (h == f || h == g) continue;
			bool fHates = dip->Get(f, h) == Relation::War || Opinion(f, h) < -40;
			bool gHates = dip->Get(g, h) == Relation::War || Opinion(g, h) < -40;
			if (fHates && gHates) return true;
		}
		return false;
	}

	cs::Ref<Appraisal> Statecraft::AppraisePeace(const cs::Ref<Faction>& me, const cs::Ref<Faction>& them, double jd)
	{
		auto a = cs::New<Appraisal>();
		double ratio = Ratio(them, me);
		a->Add(-10, "they would rather fight on");
		a->Add(0.3 * Opinion(me, them), Opinion(me, them) >= 0 ? "they bear you little ill will" : "they hate you");
		a->Add(25 * Clamp(ratio - 1, -1, 2), ratio > 1 ? "your navy outweighs theirs" : "they think they are winning");
		double days = WarDays(me, them, jd);
		a->Add(30 * cs::Math::Min(1.0, days / 365), days > 90 ? cs::String("they are weary of the war") : cs::String());
		int lost = 0;
		for (const auto& s : Polities->SettlementsOf(them))
			if (s->OccupiedFrom == me) lost++;
		if (lost > 0) a->Add(-10 * lost, "you hold ground taken from them");
		if (me->CrewMorale < 0.7) a->Add(15, "their crews have lost heart");
		return a;
	}

	cs::Ref<Appraisal> Statecraft::AppraiseNonAggression(const cs::Ref<Faction>& me, const cs::Ref<Faction>& them)
	{
		auto a = cs::New<Appraisal>();
		a->Add(5, "peace costs them nothing");
		a->Add(0.5 * Opinion(me, them), Opinion(me, them) >= 0 ? "they think well of you" : "they distrust you");
		double ratio = Ratio(them, me);
		a->Add(20 * Clamp(ratio - 1, 0, 2), "your navy worries them");
		if (Ratio(me, them) >= InterstellarWars::DeclareRatio) a->Add(-25, "they covet what you hold");
		return a;
	}

	cs::Ref<Appraisal> Statecraft::AppraiseTrade(const cs::Ref<Faction>& me, const cs::Ref<Faction>& them)
	{
		auto a = cs::New<Appraisal>();
		a->Add(10, "trade pays them too");
		a->Add(0.6 * Opinion(me, them), Opinion(me, them) >= 0 ? "they think well of you" : "they distrust you");
		return a;
	}

	cs::Ref<Appraisal> Statecraft::AppraiseAlliance(const cs::Ref<Faction>& me, const cs::Ref<Faction>& them)
	{
		auto a = cs::New<Appraisal>();
		a->Add(-30, "an alliance drags them into your wars");
		a->Add(0.8 * Opinion(me, them), Opinion(me, them) >= 0 ? "they count you a friend" : "they distrust you");
		if (CommonEnemy(me, them)) a->Add(30, "you share an enemy");
		if (PactBetween(me, them, PactKind::NonAggression()) != nullptr) a->Add(5, "your pact has held");
		if (PactBetween(me, them, PactKind::Trade()) != nullptr) a->Add(5, "your trade binds you");
		if (Polities->AtWar(them) && !CommonEnemy(me, them)) a->Add(-20, "you are at war with someone they have no quarrel with");
		return a;
	}

	cs::Ref<Appraisal> Statecraft::AppraiseTributeReceived(const cs::Ref<Faction>& me, const cs::Ref<Faction>& them, double amount)
	{
		auto a = cs::New<Appraisal>();
		a->Add(amount > 0 ? 50 : -10, amount > 0 ? "money is money" : "an empty offer");
		return a;
	}

	cs::Ref<Appraisal> Statecraft::AppraiseTributePaid(const cs::Ref<Faction>& me, const cs::Ref<Faction>& them, double amount)
	{
		auto a = cs::New<Appraisal>();
		double ratio = Ratio(them, me);
		a->Add(30 * (ratio - 2.5), ratio > 2.5 ? "your navy could crush theirs" : "they do not fear you");
		a->Add(0.2 * Opinion(me, them), cs::String());
		double year = amount * 12;
		a->Add(-40 * cs::Math::Min(2.0, year / cs::Math::Max(1e6, me->Treasury)), "the sum would ruin them");
		if (me->Treasury < amount) a->Add(-100, "they cannot pay it");
		return a;
	}

	cs::Ref<Appraisal> Statecraft::AppraiseVassalTaken(const cs::Ref<Faction>& me, const cs::Ref<Faction>& them)
	{
		auto a = cs::New<Appraisal>();
		a->Add(20, "a vassal's levy and its ships");
		a->Add(0.3 * Opinion(me, them), cs::String());
		for (const auto& g : Parties())
			if (g != me && g != them && Polities->Diplomacy->Get(them, g) == Relation::War && Power(g) > Power(me))
			{
				a->Add(-30, "your enemies are stronger than they are");
				break;
			}
		return a;
	}

	cs::Ref<Appraisal> Statecraft::AppraiseFealty(const cs::Ref<Faction>& me, const cs::Ref<Faction>& them, double jd)
	{
		auto a = cs::New<Appraisal>();
		double ratio = Ratio(them, me);
		a->Add(25 * (ratio - 4), ratio > 4 ? "they cannot stand against you" : "they will not bend the knee");
		a->Add(0.2 * Opinion(me, them), cs::String());
		if (Polities->Diplomacy->Get(me, them) == Relation::War)
		{
			int lost = 0;
			for (const auto& s : Polities->SettlementsOf(them))
				if (s->OccupiedFrom == me) lost++;
			if (lost > 0) a->Add(15 * lost, "you hold their ground");
			if (ratio > 2) a->Add(20, "they are losing the war");
		}
		return a;
	}

	// ---------------------------------------------------------------- the day

	void Statecraft::Update(double jd)
	{
		if (cs::Double::IsNaN(nextDailyJd)) nextDailyJd = jd;
		if (cs::Double::IsNaN(nextRoundJd)) nextRoundJd = jd + 15;
		while (jd >= nextDailyJd)
		{
			powerCache = cs::New<cs::Dictionary<cs::Ref<Faction>, double>>();
			// try { Daily } finally { powerCache = null; }
			try
			{
				Daily(nextDailyJd);
			}
			catch (...)
			{
				powerCache = nullptr;
				throw;
			}
			powerCache = nullptr;
			nextDailyJd += 1;
		}
	}

	void Statecraft::Daily(double jd)
	{
		auto parties = Parties();
		// Opinions drift toward what pacts, wars and rivalries make them.
		double pace = 0.02 * OpinionPace()->Value;
		for (const auto& f : parties)
			for (const auto& g : parties)
				if (f != g)
				{
					double now = Opinion(f, g);
					double to = Baseline(f, g);
					if (cs::Math::Abs(to - now) > 1e-6) opinions[FactionPair{ f, g }] = now + (to - now) * pace;
				}
		// Pacts: run out, are kept up, or lapse.
		auto pactsNow = cs::List<cs::Ref<Pact>>(pacts);
		for (const auto& p : pactsNow)
		{
			if (!pacts.Contains(p)) continue;
			if (!Party(p->A) || !Party(p->B) || !Alive(p->A) || !Alive(p->B))
			{
				pacts.Remove(p);
				continue;
			}
			if (!cs::Double::IsNaN(p->UntilJd) && jd >= p->UntilJd)
			{
				Expire(p, jd);
				continue;
			}
			if (p->Kind->Daily && !p->Kind->Daily(this, p, jd))
			{
				pacts.Remove(p);
				Sway(p->B, p->A, -30);
				Tell(jd, cs::Cat("The ", p->A->Name, " cannot pay and defaults on the ", Lower(p->Describe())), p->A, p->B);
				Spurned(p->B, p->A, jd);
			}
		}
		// Wars noticed (a declaration made anywhere), friends called to arms, and friends' wars ended.
		for (const auto& f : parties)
			for (const auto& g : parties)
				if (f != g && Polities->Diplomacy->Get(f, g) == Relation::War && !warsBegun.ContainsKey(Key(f, g)))
				{
					auto c = Galaxy && Galaxy->Wars ? Galaxy->Wars->Between(f, g) : nullptr;
					Began(c ? c->Aggressor : f, c ? c->Defender : g, c ? c->StartJd : jd);
				}
		for (const auto& key : warsBegun.Keys())
			if (Polities->Diplomacy->Get(key.Item1, key.Item2) != Relation::War) warsBegun.Remove(key);
		StandDown(jd);
		CallToArms(jd);
		// Proposals to the player left unanswered are taken as declined.
		auto inboxNow = cs::List<cs::Ref<Proposal>>(inbox);
		for (const auto& p : inboxNow)
			if (jd - p->Jd > InboxDays || p->To != Player()) Answer(p, false, jd);
		if (jd >= nextRoundJd)
		{
			nextRoundJd = jd + RoundDays;
			if (AutoDiplomacy && AiDiplomacy()->Value) Round(jd);
		}
	}

	bool Statecraft::Alive(const cs::Ref<Faction>& f) const
	{
		for (const auto& _ : Polities->SettlementsOf(f))
		{
			(void)_;
			return true;
		}
		return Polities->Navy(f).Count() > 0;
	}

	void Statecraft::CallToArms(double jd)
	{
		auto dip = Polities->Diplomacy;
		auto pactsNow = cs::List<cs::Ref<Pact>>(pacts);
		for (const auto& p : pactsNow)
		{
			if (!p->Kind->CallsToArms || !pacts.Contains(p)) continue;
			for (const auto& friend_ : cs::List<cs::Ref<Faction>>{ p->A, p->B })
			{
				auto ally = p->Other(friend_);
				auto begun = cs::List<std::pair<FactionPair, WarBegun>>();
				for (auto [k, v] : warsBegun) begun.Add({ k, v });
				for (const auto& kv : begun)
				{
					auto x = kv.first.Item1;
					auto y = kv.first.Item2;
					if (x != friend_ && y != friend_) continue;
					auto enemy = x == friend_ ? y : x;
					if (enemy == ally || dip->Get(friend_, enemy) != Relation::War || dip->Get(ally, enemy) == Relation::War) continue;
					// An ally answers a friend who was attacked; a vassal follows its overlord into any war.
					bool attacked = kv.second.By == enemy;
					bool serves = p->Kind == PactKind::Vassalage() && p->A == ally;
					bool protects = p->Kind == PactKind::Vassalage() && p->B == ally && attacked;
					if (!(serves || protects || (p->Kind != PactKind::Vassalage() && attacked))) continue;
					if (BoundToPeace(ally, enemy) || (OverlordOf(ally) != nullptr && OverlordOf(ally) != friend_))
						continue;
					if (!serves && Opinion(ally, friend_) < -20 && ally != Player())
					{
						Break(p, ally, jd, cs::Cat("it will not fight the ", enemy->Name, " for the ", friend_->Name));
						goto nextPact;
					}
					dip->Set(ally, enemy, Relation::War);
					// The ally declared it: its own friends are not called against an enemy that never attacked them.
					Began(ally, enemy, jd);
					{
						auto cb = cs::New<CoBelligerence>();
						cb->Ally = ally;
						cb->Friend = friend_;
						cb->Enemy = enemy;
						cb->SinceJd = jd;
						coBelligerents.Add(cb);
					}
					for (const auto& w : Polities->Worlds())
						if (w->War) w->War->RevokeBookings();
					Tell(jd, serves ? cs::Cat("The ", ally->Name, " follows its overlord, the ", friend_->Name, ", to war with the ", enemy->Name)
						: cs::Cat("The ", ally->Name, " honours its ", (p->Kind == PactKind::Vassalage() ? "duty to its vassal" : "alliance"), " with the ", friend_->Name, " and declares war on the ", enemy->Name),
						ally, enemy);
				}
			}
		nextPact:;
		}
	}

	void Statecraft::StandDown(double jd)
	{
		auto snapshot = cs::List<cs::Ref<CoBelligerence>>(coBelligerents);
		for (const auto& c : snapshot)
		{
			if (Polities->Diplomacy->Get(c->Friend, c->Enemy) == Relation::War) continue;
			coBelligerents.Remove(c);
			if (Polities->Diplomacy->Get(c->Ally, c->Enemy) == Relation::War)
				MakePeace(c->Ally, c->Enemy, jd, cs::Cat("the ", c->Friend->Name, "'s war over"));
		}
	}

	void Statecraft::Spurned(const cs::Ref<Faction>& by, const cs::Ref<Faction>& refuser, double jd)
	{
		if (by == Player() || !AutoDiplomacy || !AiDiplomacy()->Value || !(Galaxy && Galaxy->Wars) || !Galaxy->Wars->AutoDeclare) return;
		if (Polities->AtWar(by) || BoundToPeace(by, refuser) || OverlordOf(by) != nullptr) return;
		if (Ratio(by, refuser) < 2.5 || rng.NextDouble() > 0.5) return;
		DeclareWar(by, refuser, jd);
	}

	void Statecraft::Round(double jd)
	{
		auto parties = Parties();
		auto dip = Polities->Diplomacy;
		for (const auto& f : parties)
		{
			if (f == Player()) continue;
			auto liege = OverlordOf(f);
			if (liege != nullptr && Power(f) > 1.2 * Power(liege) && Opinion(f, liege) < -20)
			{
				Break(PactBetween(f, liege, PactKind::Vassalage()), f, jd);
				continue;
			}
			for (const auto& g : parties)
			{
				if (g == f || rng.NextDouble() > 0.5) continue;
				double op = Opinion(f, g);
				cs::Ref<Deal> deal = nullptr;
				double amount = 0;
				if (dip->Get(f, g) == Relation::War)
				{
					if (AppraisePeace(f, g, jd)->Score > 0) deal = Deal::Peace();
					else if (Ratio(f, g) > 4) deal = Deal::DemandFealty();
				}
				else if (dip->Get(f, g) == Relation::Ceasefire) continue;
				else if (op > 40 && CommonEnemy(f, g) && PactBetween(f, g, PactKind::Alliance()) == nullptr) deal = Deal::Alliance();
				else if (Ratio(g, f) > 1.5 && op > -30 && !BoundToPeace(f, g)) deal = Deal::NonAggression();
				else if (op > 10 && PactBetween(f, g, PactKind::Trade()) == nullptr) deal = Deal::TradePact();
				else if (Ratio(f, g) > 3 && op < -10 && TributeBetween(f, g) == nullptr && liege == nullptr)
				{
					deal = Deal::DemandTribute();
					amount = SuggestedTribute(g);
				}
				if (deal == nullptr || !Unavailable(deal, f, g, jd).empty()) continue;
				cs::Ref<Appraisal> answer;
				Propose(deal, f, g, jd, answer, amount);
				if (answer != nullptr && !answer->Accepted() && deal == Deal::DemandTribute()) Spurned(f, g, jd);
			}
		}
	}

	// ---------------------------------------------------------------- the news

	void Statecraft::Tell(double jd, const cs::String& text, const cs::Ref<Faction>& a, const cs::Ref<Faction>& b)
	{
		record.Add(text);
		if (record.Count() > 200) record.RemoveRange(0, record.Count() - 200);
		auto told = cs::HashSet<cs::Ref<World>>();
		for (const auto& f : cs::List<cs::Ref<Faction>>{ a, b })
		{
			auto w = Polities->CapitalSystemOf(f);
			if (!w) w = Polities->HomeOf(f);
			if (w != nullptr && told.Add(w)) w->Log(jd, text, WorldEventKind::Diplomacy);
		}
	}

	cs::String Statecraft::Capitalise(const cs::String& s)
	{
		return cs::Strings::IsNullOrEmpty(s) ? s : cs::Cat(cs::Strings::ToUpperInvariant(cs::Strings::Substring(s, 0, 1)), cs::Strings::Substring(s, 1));
	}

	cs::String Statecraft::Lower(const cs::String& s)
	{
		return cs::Strings::IsNullOrEmpty(s) ? s : cs::Strings::StartsWith(s, "the ") ? s : cs::Cat(cs::Strings::ToLowerInvariant(cs::Strings::Substring(s, 0, 1)), cs::Strings::Substring(s, 1));
	}

	cs::String Statecraft::Standing(const cs::Ref<Faction>& f, const cs::Ref<Faction>& g) const
	{
		auto r = Polities->Diplomacy->Get(f, g);
		auto parts = cs::List<cs::String>{ r == Relation::War ? "at war" : r == Relation::Ceasefire ? "in a ceasefire" : "at peace" };
		for (const auto& p : pacts)
			if (p->Between(f, g))
				parts.Add(p->Kind == PactKind::Vassalage() ? cs::String(p->A == f ? "their vassal" : "your vassal")
					: p->Kind == PactKind::Tribute() ? cs::String(p->A == f ? "paying them tribute" : "taking their tribute") : cs::Strings::ToLowerInvariant(p->Kind->Name));
		return cs::Strings::Join(", ", parts);
	}
}
