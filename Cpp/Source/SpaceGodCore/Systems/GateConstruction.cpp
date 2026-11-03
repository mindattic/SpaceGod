// Port of Assets/UnitySpace/Core/Systems/GateConstruction.cs (see docs/MIGRATION.md).

#include "Systems/GateConstruction.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "DVec3.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"
#include "Combat/Diplomacy.h"
#include "Combat/GatePickets.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Information/GalaxyMap.h"
#include "Systems/FrontierSystems.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/JumpGate.h"
#include "Systems/Polities.h"
#include "Systems/StarSystemSpec.h"

namespace SpaceGod::Core
{
	const char* ToString(GateJob v)
	{
		switch (v)
		{
		case GateJob::NewLink: return "NewLink";
		case GateJob::Repair: return "Repair";
		}
		return "";
	}

	const char* ToString(GateStage v)
	{
		switch (v)
		{
		case GateStage::Survey: return "Survey";
		case GateStage::Building: return "Building";
		case GateStage::Done: return "Done";
		case GateStage::Abandoned: return "Abandoned";
		}
		return "";
	}

	// ================================================================ GateProject

	double GateProject::Progress() const { return WorkDays <= 0 ? 1 : cs::Math::Min(1.0, WorkDone / WorkDays); }

	bool GateProject::Active() const { return Stage == GateStage::Survey || Stage == GateStage::Building; }

	DVec3 GateProject::TargetLy() const { return To != nullptr ? To->PositionLy : Frontier->PositionLy(); }

	DVec3 GateProject::SiteKm() const
	{
		return Gate != nullptr ? Gate->PositionKm : (TargetLy() - From->PositionLy).Normalized() * Galaxy::GateDistanceKm(From);
	}

	bool GateProject::Joins(cs::Ref<StarSystem> a, const cs::String& b) const
	{
		return (From == a && TargetName == b) || ((From != nullptr && From->Name == b) && TargetName == a->Name);
	}

	cs::String GateProject::ToString() const
	{
		return cs::Cat(Work == GateJob::Repair ? "Rebuilding the " : "", Gate != nullptr ? Gate->Name : cs::Cat(TargetName, " Gate"), " at ", From->Name);
	}

	// ================================================================ GateConstruction

	const cs::List<cs::Ref<GateProject>>& GateConstruction::Projects() const { return projects; }

	const cs::List<Good>& GateConstruction::Materials()
	{
		static const cs::List<Good> materials{ Good::Alloys, Good::Electronics, Good::Machinery };
		return materials;
	}

	cs::List<double> GateConstruction::Cost(double ly) { return cs::List<double>{ 40000 + 6000 * ly, 5000 + 800 * ly, 8000 + 1200 * ly }; }

	double GateConstruction::CostCredits(double ly)
	{
		auto c = Cost(ly);
		double total = 0;
		for (int i = 0; i < Materials().Count(); i++) total += c[i] * GoodInfo::Of(Materials()[i])->BasePrice;
		return total;
	}

	double GateConstruction::BuildDays(double ly) { return 365.25 * (2 + 0.2 * ly); }

	double GateConstruction::SurveyDays(double ly) { return 60 + 15 * ly; }

	GateConstruction::GateConstruction(Galaxy* galaxy, double jd, int seed)
		: galaxy(galaxy), rng(seed)
	{
		nextDailyJd = jd + 1;
		nextPlanJd = jd + 1;
	}

	cs::List<cs::Ref<GateProject>> GateConstruction::Active() const
	{
		cs::List<cs::Ref<GateProject>> active;
		for (const auto& p : projects)
			if (p->Active()) active.Add(p);
		return active;
	}

	cs::List<cs::Ref<GateWork>> GateConstruction::Works()
	{
		cs::List<cs::Ref<GateWork>> works;
		for (const auto& p : projects)
			if (p->Active() && p->Work == GateJob::NewLink && p->Stage == GateStage::Building && p->To != nullptr)
			{
				auto w = cs::New<GateWork>();
				w->A = p->From; w->B = p->To; w->Progress = p->Progress(); w->Builder = p->Builder;
				works.Add(w);
			}
		return works;
	}

	void GateConstruction::Update(double jd)
	{
		while (jd >= nextDailyJd)
		{
			Daily(nextDailyJd);
			nextDailyJd += 1;
		}
	}

	void GateConstruction::Daily(double jd)
	{
		for (const auto& p : projects)
			if (p->Active()) Step(p, jd, 1);
		Sabotage(jd);
		if (jd >= nextPlanJd)
		{
			Plan(jd);
			nextPlanJd = jd + PlanIntervalDays;
		}
	}

	// ---------------------------------------------------------------- the work

	void GateConstruction::Step(cs::Ref<GateProject> p, double jd, double days)
	{
		if (!p->Active()) return;
		if (Fallen(p->Builder))
		{
			Abandon(p, jd, cs::Cat(p->Builder->Name, " is no more"));
			return;
		}
		if (p->Stage == GateStage::Survey)
		{
			if (jd - p->StartJd < p->SurveyDays) return;
			p->Stage = GateStage::Building;
			p->BuildStartJd = jd;
			SendConstructors(p, jd);
			Log(p, jd, cs::Cat("The survey of the ", SiteName(p), " site is done: ", p->Builder->Name, " constructors begin the gate to ", p->TargetName, ", ",
				cs::Fmt(p->Ly, "0.0"), " ly, ", cs::Fmt(p->WorkDays / 365.25, "0.0"), " years of work"));
			return;
		}
		if (p->Work == GateJob::NewLink && LinkedAlready(p))
		{
			Abandon(p, jd, "the way is already built");
			return;
		}

		double crew = Crew(p, jd);
		if (crew <= 0) return;
		// The day's share of the bill, at the crew's pace; short of any one material, the work goes at that one's pace.
		double rate = crew * days;
		const auto& materials = Materials();
		auto want = cs::NewArray<double>(materials.Count());
		double have = 1;
		for (int i = 0; i < materials.Count(); i++)
		{
			want[i] = cs::Math::Min(p->Need[i] - p->Used[i], p->Need[i] / p->WorkDays * rate);
			if (want[i] <= 1e-9) continue;
			double stock = 0;
			for (const auto& s : Sources(p))
			{
				stock += s->Stock[static_cast<int>(materials[i])];
				s->ExtraDemand[static_cast<int>(materials[i])] += want[i] / days;
			}
			have = cs::Math::Min(have, stock / want[i]);
		}
		// ... and as far as the builder can pay for it.
		double bill = 0;
		for (int i = 0; i < materials.Count(); i++) bill += want[i] * PriceOf(p, materials[i]);
		if (bill > 0) have = cs::Math::Min(have, cs::Math::Max(0.0, p->Builder->Treasury) / bill);
		if (have <= 0) return;
		for (int i = 0; i < materials.Count(); i++) Buy(p, i, want[i] * have);
		double before = p->WorkDone;
		p->WorkDone = cs::Math::Min(p->WorkDays, p->WorkDone + rate * have);
		// The day's work mends its share of the damage it was sized for, on top of whatever is left of the ring: damage done
		// to it meanwhile stands (U.SPC-T299).
		if (p->Work == GateJob::Repair) p->Gate->Integrity = cs::Math::Min(1.0, p->Gate->Integrity + (1 - p->FromIntegrity) * (p->WorkDone - before) / p->WorkDays);
		if (p->WorkDone >= p->WorkDays - 1e-9) Complete(p, jd);
	}

	double GateConstruction::Crew(cs::Ref<GateProject> p, double jd)
	{
		int working = 0;
		for (const auto& c : p->Constructors)
			if (!c->Destroyed && !c->Derelict && c->Faction == p->Builder) working++;
		if (working > 0)
		{
			p->CrewLostJd = cs::Double::NaN;
			return cs::Math::Min(1.0, working / static_cast<double>(ConstructorsPerProject));
		}
		if (cs::Double::IsNaN(p->CrewLostJd))
		{
			p->CrewLostJd = jd;
			Log(p, jd, cs::Cat("Work stops on the ", SiteName(p), ": no ", p->Builder->Name, " constructor is left at the site"));
		}
		if (jd - p->CrewLostJd >= ReplaceCrewDays)
		{
			p->Constructors.Clear();
			SendConstructors(p, jd);
			p->CrewLostJd = cs::Double::NaN;
		}
		return 0;
	}

	void GateConstruction::SendConstructors(cs::Ref<GateProject> p, double jd)
	{
		auto fleet = p->From->World->Fleet;
		fleet->AddFaction(p->Builder);
		auto site = p->SiteKm();
		auto outward = site.Normalized();
		auto side = DVec3::Cross(cs::Math::Abs(outward.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX, outward).Normalized();
		int root = p->From->Catalog->Root()->Index;
		for (int i = p->Constructors.Count(); i < ConstructorsPerProject; i++)
		{
			// Either side of the ring, clear of it and of each other.
			auto at = site + side * (i % 2 == 0 ? 60 : -60) - outward * 20;
			auto ship = fleet->SpawnAt(cs::Cat("Constructor ", p->TargetName, " ", i + 1), ShipClass::Freighter, p->Builder, root, at, -outward, jd);
			ship->Cargo = cs::Cat("Gate segments for the ", SiteName(p));
			// Held at the site: nothing is scheduled for it until the gate is done.
			ship->PhaseEndJd = jd + 1e6;
			p->Constructors.Add(ship);
		}
	}

	cs::List<cs::Ref<Settlement>> GateConstruction::Sources(cs::Ref<GateProject> p)
	{
		cs::List<cs::Ref<Settlement>> list;
		auto add = [&list](const cs::Ref<World>& w)
		{
			if (w == nullptr) return;
			for (const auto& s : w->Economy->Settlements)
				if (!s->Fallen && !s->Hidden && s->Kind != SettlementKind::Haven && s->Owner != nullptr && !s->Owner->IsPirate && !list.Contains(s)) list.Add(s);
		};
		add(p->From->World);
		add(galaxy->Polities->CapitalSystemOf(p->Builder));
		return list;
	}

	double GateConstruction::PriceOf(cs::Ref<GateProject> p, Good g)
	{
		double price = 0;
		for (const auto& s : Sources(p))
			if (s->Stock[static_cast<int>(g)] > 0) price = cs::Math::Max(price, s->Price[static_cast<int>(g)]);
		return price > 0 ? price : GoodInfo::Of(g)->BasePrice;
	}

	void GateConstruction::Buy(cs::Ref<GateProject> p, int i, double tonnes)
	{
		if (tonnes <= 0) return;
		auto g = Materials()[i];
		auto sources = Sources(p);
		sources.Sort([g](const cs::Ref<Settlement>& a, const cs::Ref<Settlement>& b) { return cs::CompareTo(b->Stock[static_cast<int>(g)], a->Stock[static_cast<int>(g)]); });
		double left = tonnes;
		for (const auto& s : sources)
		{
			if (left <= 1e-9) break;
			double take = cs::Math::Min(left, s->Stock[static_cast<int>(g)]);
			if (take <= 0) continue;
			s->Stock[static_cast<int>(g)] -= take;
			double cost = take * (s->Price[static_cast<int>(g)] > 0 ? s->Price[static_cast<int>(g)] : GoodInfo::Of(g)->BasePrice);
			p->Builder->Treasury -= cost;
			p->Spent += cost;
			if (s->Owner != nullptr && s->Owner != p->Builder) s->Owner->Treasury += cost * Economy::PortTax;
			left -= take;
		}
		p->Used[i] += tonnes - left;
	}

	void GateConstruction::Complete(cs::Ref<GateProject> p, double jd)
	{
		p->Stage = GateStage::Done;
		p->DoneJd = jd;
		for (const auto& c : p->Constructors)
		{
			// Back to the yards as ordinary hulls: their next move is their own.
			if (c->Destroyed || c->Phase != ShipPhase::Parked) continue;
			c->Cargo = cs::String();
			c->PhaseEndJd = jd + 0.5 + rng.NextDouble();
		}
		if (p->Work == GateJob::Repair)
		{
			// Whole, unless it was damaged again while the work went on: that is left for the next repair (U.SPC-T299).
			p->Gate->Integrity = cs::Math::Min(1.0, p->Gate->Integrity + (1 - p->FromIntegrity) * cs::Math::Max(0.0, 1 - p->Progress()) + 1e-9);
			if (p->Gate->Integrity > 1 - 1e-6) p->Gate->Integrity = 1;
			// The ring is rebuilt with its forts (U.SPC-T294), not left open to the next war fleet.
			GateNetwork::Fortify(p->Gate);
			Repaired++;
			auto destination = p->Gate->Destination();
			Log(p, jd, cs::Cat(p->Builder->Name, " has rebuilt the ", p->Gate->Name, ": the way to ", destination ? destination->Name : cs::String(), " is open again"));
			Completed.Invoke(p);
			return;
		}

		bool opened = false;
		if (p->To == nullptr)
		{
			p->To = galaxy->Find(p->Frontier->Name);
			if (p->To == nullptr)
			{
				p->To = StarSystem::Create(p->Frontier, jd, nullptr, /*polities:*/ galaxy->Polities);
				galaxy->AddSystem(p->To, jd);
			}
		}
		if (!p->To->Settled() && !p->To->OpenToSettlement()) opened = true;
		auto link = galaxy->Network->Link(p->From, p->To);
		link->AtA->Owner = p->Builder;
		// The far end is the far system's own, if anyone holds it; an empty system's is the builder's.
		auto holder = p->To->Holder();
		link->AtB->Owner = holder ? holder : (p->To->Settled() ? p->To->World->Sol : p->Builder);
		// Both ends armed like every other held gate (U.SPC-T92, U.SPC-T294).
		GateNetwork::Fortify(link->AtA);
		GateNetwork::Fortify(link->AtB);
		p->Gate = link->AtA;
		p->To->World->Fleet->AddFaction(p->Builder);
		// And watched from the day it opens (U.SPC-T480).
		GatePickets::PostFull(link->AtA, jd);
		GatePickets::PostFull(link->AtB, jd);
		Built++;
		Log(p, jd, cs::Cat("The ", link->AtA->Name, " is finished: ", p->Builder->Name, " opens the way from ", p->From->Name, " to ", p->To->Name, ", ", cs::Fmt(p->Ly, "0.0"), " ly, ",
			"after ", cs::Fmt((jd - p->StartJd) / 365.25, "0.0"), " years and ", cs::Fmt(p->Spent / 1e6, "0"), " million credits"));
		if (opened)
		{
			Opened++;
			p->To->OpenedJd = jd;
			p->To->World->Log(jd, cs::Cat("The first gate into ", p->To->Name, ", from ", p->From->Name, ", is open: the system is open to settlement"), WorldEventKind::Diplomacy, p->To->Catalog->Root()->Index);
			galaxy->RaiseOpened(p->To, p->Builder, jd);
		}
		Completed.Invoke(p);
	}

	void GateConstruction::Abandon(cs::Ref<GateProject> p, double jd, const cs::String& why)
	{
		p->Stage = GateStage::Abandoned;
		p->DoneJd = jd;
		for (const auto& c : p->Constructors)
			if (!c->Destroyed && c->Phase == ShipPhase::Parked) c->PhaseEndJd = jd + 0.5;
		Log(p, jd, cs::Cat("Work on the ", SiteName(p), " is given up: ", why));
	}

	bool GateConstruction::LinkedAlready(cs::Ref<GateProject> p)
	{
		auto to = p->To ? p->To : galaxy->Find(p->TargetName);
		return to != nullptr && galaxy->Network->Between(p->From, to) != nullptr;
	}

	bool GateConstruction::Fallen(cs::Ref<Faction> f)
	{
		// C#: foreach (var _ in SettlementsOf(f)) return false; (an eager list here, so: is it empty)
		return galaxy->Polities->SettlementsOf(f).Count() == 0;
	}

	cs::String GateConstruction::SiteName(cs::Ref<GateProject> p) { return p->Gate != nullptr ? p->Gate->Name : GateNetworkName(p->TargetName); }

	cs::String GateConstruction::GateNetworkName(const cs::String& to)
	{
		return cs::Cat(to == "Sol System" ? cs::String("Sol") : to == "Alpha Centauri" ? cs::String("Centauri") : to, " Gate");
	}

	void GateConstruction::Log(cs::Ref<GateProject> p, double jd, const cs::String& text)
	{
		p->From->World->Log(jd, text, WorldEventKind::Info, p->From->Catalog->Root()->Index);
	}

	// ---------------------------------------------------------------- starting work

	cs::Ref<GateProject> GateConstruction::Start(cs::Ref<Faction> builder, cs::Ref<StarSystem> from, cs::Ref<StarSystem> to, cs::Ref<SystemSpec> frontier, double jd)
	{
		double ly = ((to != nullptr ? to->PositionLy : frontier->PositionLy()) - from->PositionLy).Length();
		auto p = cs::New<GateProject>();
		p->Work = GateJob::NewLink; p->Stage = GateStage::Survey; p->Builder = builder; p->From = from; p->To = to; p->Frontier = to == nullptr ? frontier : nullptr;
		p->TargetName = to != nullptr ? to->Name : frontier->Name; p->Ly = ly; p->StartJd = jd; p->SurveyDays = SurveyDays(ly); p->WorkDays = BuildDays(ly);
		auto cost = Cost(ly);
		for (int i = 0; i < p->Need.Count(); i++) p->Need[i] = cost[i]; // Array.Copy(Cost(ly), p.Need, p.Need.Length)
		projects.Add(p);
		Log(p, jd, cs::Cat(builder->Name, " begins surveying a gate site at ", from->Name, " for a way to ", p->TargetName, ", ", cs::Fmt(ly, "0.0"), " ly"));
		return p;
	}

	cs::Ref<GateProject> GateConstruction::StartRepair(cs::Ref<JumpGate> gate, cs::Ref<Faction> builder, double jd)
	{
		double damage = 1 - gate->Integrity;
		double share = RepairShare * damage;
		auto destination = gate->Destination();
		double linkLy = gate->Link != nullptr ? gate->Link->Ly : 0;
		auto p = cs::New<GateProject>();
		p->Work = GateJob::Repair; p->Stage = GateStage::Building; p->Builder = builder; p->From = gate->Home; p->To = destination; p->Gate = gate;
		p->FromIntegrity = gate->Integrity; p->TargetName = destination ? destination->Name : gate->Name; p->Ly = linkLy;
		p->StartJd = jd; p->BuildStartJd = jd; p->WorkDays = cs::Math::Max(30.0, BuildDays(linkLy) * share);
		auto c = Cost(p->Ly);
		for (int i = 0; i < c.Count(); i++) p->Need[i] = c[i] * share;
		projects.Add(p);
		SendConstructors(p, jd);
		Log(p, jd, cs::Cat(builder->Name, " sends constructors to rebuild the ", gate->Name, ", ", cs::Fmt(damage * 100, "0"), "% wrecked"));
		return p;
	}

	void GateConstruction::Damage(cs::Ref<JumpGate> gate, double amount, double jd, const cs::String& by)
	{
		if (gate == nullptr || amount <= 0) return;
		bool down = gate->Damage(amount);
		cs::String what = gate->Wrecked() ? "wrecked" : down ? "knocked out of action" : "damaged";
		gate->Home->World->Log(jd, cs::Cat("The ", gate->Name, " is ", what, !by.empty() ? cs::Cat(" by ", by) : cs::String(), " (", cs::Fmt(gate->Integrity * 100, "0"), "% whole)"),
			WorldEventKind::Battle, gate->Home->Catalog->Root()->Index);
	}

	cs::Ref<GateProject> GateConstruction::RepairOf(cs::Ref<JumpGate> gate)
	{
		for (const auto& p : projects)
			if (p->Active() && p->Work == GateJob::Repair && p->Gate == gate) return p;
		return nullptr;
	}

	void GateConstruction::Plan(double jd)
	{
		for (const auto& link : galaxy->Network->Links())
			for (const auto& gate : cs::List<cs::Ref<JumpGate>>{ link->AtA, link->AtB })
			{
				if (gate->Integrity >= 1 || RepairOf(gate) != nullptr) continue;
				auto builder = gate->Owner != nullptr && !gate->Owner->IsPirate && !Fallen(gate->Owner) ? gate->Owner : gate->Home->Holder();
				if (builder == nullptr || builder->IsPirate) continue;
				// A ring on the front line is not rebuilt while the enemy holds the far end.
				cs::Ref<Faction> farHolder = gate->Partner != nullptr && gate->Partner->Home != nullptr ? gate->Partner->Home->Holder() : nullptr;
				if (Hostile(builder, farHolder)) continue;
				StartRepair(gate, builder, jd);
			}

		for (const auto& f : galaxy->Polities->Factions())
		{
			if (f->Kind != PolityKind::State || f->IsPirate) continue;
			int running = 0;
			for (const auto& p : projects)
				if (p->Active() && p->Work == GateJob::NewLink && p->Builder == f) running++;
			if (running >= (f->Treasury >= RichTreasury ? 2 : 1)) continue;
			cs::Ref<StarSystem> from, to;
			cs::Ref<SystemSpec> frontier;
			double ly = 0;
			if (BestProject(f, from, to, frontier, ly) && f->Treasury >= ReserveShare * CostCredits(ly))
				Start(f, from, to, frontier, jd);
		}
	}

	bool GateConstruction::Hostile(cs::Ref<Faction> a, cs::Ref<Faction> b)
	{
		return a != nullptr && b != nullptr && a != b && galaxy->Polities->Diplomacy->Get(a, b) == Relation::War;
	}

	bool GateConstruction::BestProject(cs::Ref<Faction> f, cs::Ref<StarSystem>& from, cs::Ref<StarSystem>& to, cs::Ref<SystemSpec>& frontier, double& ly)
	{
		from = to = nullptr;
		frontier = nullptr;
		ly = 0;
		double best = 0;
		for (const auto& s : BasesOf(f))
		{
			for (const auto& t : galaxy->Systems)
			{
				// A Void Base is a relay for the network's gaps (U.SPC-T372), no place for a state to reach out to: nothing there to
				// open or hold, and a gate it built in would hand the lawless relay to the builder.
				if (t == s || t->IsVoidBase || galaxy->Network->Between(s, t) != nullptr || Targeted(s, t->Name)) continue;
				double d = Galaxy::DistanceLy(s, t);
				if (d > MaxLinkLy || !SiteClear(s, t->PositionLy) || !SiteClear(t, s->PositionLy)) continue;
				auto round = galaxy->Network->Route(s, t);
				double saved = (round == nullptr ? 30 : GateNetwork::RouteDays(round)) - GateNetwork::TransitDays(d);
				if (saved < MinSavedDays) continue;
				double score = saved / d;
				if (score > best)
				{
					best = score;
					from = s;
					to = t;
					frontier = nullptr;
					ly = d;
				}
			}
			for (const auto& spec : FrontierSystems::All())
			{
				// Nobody surveys a gate to a star nobody has discovered (U.SPC-T456).
				if (galaxy->Find(spec->Name) != nullptr || galaxy->IsUndiscovered(!spec->NeighbourName.empty() ? spec->NeighbourName : spec->Name) || Targeted(nullptr, spec->Name)) continue;
				double d = (spec->PositionLy() - s->PositionLy).Length();
				if (d > MaxLinkLy || !SiteClear(s, spec->PositionLy())) continue;
				double score = FrontierWorthDays / d;
				if (score > best)
				{
					best = score;
					from = s;
					to = nullptr;
					frontier = spec;
					ly = d;
				}
			}
		}
		return from != nullptr;
	}

	bool GateConstruction::Targeted(cs::Ref<StarSystem> a, const cs::String& b)
	{
		for (const auto& p : projects)
		{
			if (!p->Active() || p->Work != GateJob::NewLink) continue;
			if (a == nullptr ? p->TargetName == b : p->Joins(a, b)) return true;
		}
		return false;
	}

	bool GateConstruction::SiteClear(cs::Ref<StarSystem> s, DVec3 towardLy)
	{
		auto site = (towardLy - s->PositionLy).Normalized() * Galaxy::GateDistanceKm(s);
		for (const auto& g : s->Gates())
			if ((g->PositionKm - site).Length() < JumpGate::ActivationRangeKm * 3) return false;
		for (const auto& p : projects)
			if (p->Active() && p->Work == GateJob::NewLink && p->From == s && (p->SiteKm() - site).Length() < JumpGate::ActivationRangeKm * 3) return false;
		return true;
	}

	cs::List<cs::Ref<StarSystem>> GateConstruction::BasesOf(cs::Ref<Faction> f)
	{
		cs::List<cs::Ref<StarSystem>> bases;
		for (const auto& held : galaxy->Polities->SystemsHeldBy(f))
		{
			auto s = SystemOf(held);
			if (s != nullptr) bases.Add(s);
		}
		for (const auto& s : galaxy->Systems)
		{
			if (s->Settled() || s->IsVoidBase || bases.Contains(s)) continue;
			for (const auto& gate : s->Gates())
				if (gate->Owner == f && gate->Working())
				{
					bases.Add(s);
					break;
				}
		}
		return bases;
	}

	cs::Ref<StarSystem> GateConstruction::SystemOf(cs::Ref<World> w)
	{
		for (const auto& s : galaxy->Systems)
			if (s->World == w) return s;
		return nullptr;
	}

	// ---------------------------------------------------------------- war

	void GateConstruction::Sabotage(double jd)
	{
		for (const auto& link : galaxy->Network->Links())
			for (const auto& gate : cs::List<cs::Ref<JumpGate>>{ link->AtA, link->AtB })
			{
				if (gate->Wrecked() || gate->Owner == nullptr) continue;
				cs::Ref<Faction> enemy = gate->Partner != nullptr && gate->Partner->Home != nullptr ? gate->Partner->Home->Holder() : nullptr;
				if (!Hostile(gate->Owner, enemy) || rng.NextDouble() > SabotageChancePerDay) continue;
				Damage(gate, 0.35 + 0.65 * rng.NextDouble(), jd, cs::Cat(enemy->Name, " saboteurs"));
			}
	}
}
