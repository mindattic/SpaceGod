// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/BeltMining.cs (US-T18).

#include "Economy/BeltMining.h"

#include "Astro.h"
#include "Combat/CombatSpecs.h"
#include "Economy/Goods.h"
#include "Hyperlanes.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	namespace
	{
		// BeltMining has a method named Ship: inside its member definitions the type goes by this name.
		using ShipRef = cs::Ref<::SpaceGod::Core::Ship>;
		using YieldList = cs::List<std::tuple<Good, double>>;
	}

	// ---------------------------------------------------------------- AsteroidClass

	AsteroidClass::AsteroidClass(const cs::String& name, double share, double density, cs::List<std::tuple<Good, double>> yield)
	{
		Name = name;
		Share = share;
		Density = density;
		Yield = yield;
	}

	const cs::Ref<AsteroidClass>& AsteroidClass::Carbonaceous()
	{
		static const cs::Ref<AsteroidClass> k = cs::New<AsteroidClass>("C-type", 0.6, 1.4, YieldList{
			{ Good::Water, 0.12 }, { Good::Hydrocarbons, 0.04 }, { Good::Nitrogen, 0.002 }, { Good::NickelIron, 0.12 },
			{ Good::Titanium, 0.001 }, { Good::Copper, 0.0001 }, { Good::Zinc, 0.0001 }, { Good::RareEarths, 0.00002 }, { Good::Platinum, 0.000002 } });
		return k;
	}

	const cs::Ref<AsteroidClass>& AsteroidClass::Stony()
	{
		static const cs::Ref<AsteroidClass> k = cs::New<AsteroidClass>("S-type", 0.3, 2.7, YieldList{
			{ Good::NickelIron, 0.20 }, { Good::Titanium, 0.004 }, { Good::Copper, 0.0002 }, { Good::Zinc, 0.0002 },
			{ Good::RareEarths, 0.00005 }, { Good::Platinum, 0.000005 } });
		return k;
	}

	const cs::Ref<AsteroidClass>& AsteroidClass::Metallic()
	{
		static const cs::Ref<AsteroidClass> k = cs::New<AsteroidClass>("M-type", 0.1, 5.0, YieldList{
			{ Good::NickelIron, 0.88 }, { Good::Copper, 0.0005 }, { Good::Zinc, 0.0003 }, { Good::RareEarths, 0.00001 }, { Good::Platinum, 0.00003 } });
		return k;
	}

	const cs::Ref<AsteroidClass>& AsteroidClass::CometIce()
	{
		static const cs::Ref<AsteroidClass> k = cs::New<AsteroidClass>("Comet ice", 0, 0.6, YieldList{
			{ Good::Water, 0.8 }, { Good::Hydrocarbons, 0.06 }, { Good::Nitrogen, 0.03 } });
		return k;
	}

	cs::List<cs::Ref<AsteroidClass>>& AsteroidClass::registered()
	{
		static cs::List<cs::Ref<AsteroidClass>> list = { Carbonaceous(), Stony(), Metallic() };
		return list;
	}

	const cs::List<cs::Ref<AsteroidClass>>& AsteroidClass::All() { return registered(); }

	void AsteroidClass::Register(const cs::Ref<AsteroidClass>& kind)
	{
		if (kind != nullptr && !registered().Contains(kind)) registered().Add(kind);
	}

	cs::Ref<AsteroidClass> AsteroidClass::Pick(cs::Random& rng)
	{
		double total = 0;
		for (const auto& k : registered()) total += k->Share;
		double r = rng.NextDouble() * total;
		for (const auto& k : registered())
		{
			r -= k->Share;
			if (r < 0) return k;
		}
		return registered()[registered().Count() - 1];
	}

	// ---------------------------------------------------------------- the records

	double MaterialWarehouse::StoredTonnes() const
	{
		double t = 0;
		for (auto s : Stock) t += s;
		return t;
	}

	const char* ToString(ConvoyState v)
	{
		switch (v)
		{
			case ConvoyState::EnRoute: return "EnRoute";
			case ConvoyState::Delivered: return "Delivered";
			case ConvoyState::Stolen: return "Stolen";
			case ConvoyState::Lost: return "Lost";
		}
		return "";
	}

	double MaterialConvoy::Tonnes() const
	{
		double t = 0;
		for (auto c : Cargo) t += c;
		return t;
	}

	int MiningOperation::EscortsPerFreighter() const
	{
		return cs::ToInt(cs::Math::Round(cs::Math::Max(0.0, cs::Math::Min(1.0, EscortShare)) * MaxEscortsPerFreighter));
	}

	double MiningOperation::WaitingTonnes() const
	{
		double t = 0;
		for (auto s : Separated) t += s;
		return t;
	}

	// ---------------------------------------------------------------- MegastructureSpec

	MegastructureSpec::MegastructureSpec(const cs::String& name, double minDays, cs::List<std::tuple<Good, double>> bill)
	{
		Name = name;
		MinDays = minDays;
		Bill = cs::NewArray<double>(GoodInfo::Count);
		for (const auto& [g, t] : bill) Bill[static_cast<int>(g)] += t;
	}

	double MegastructureSpec::BillTonnes() const
	{
		double t = 0;
		for (auto b : Bill) t += b;
		return t;
	}

	const cs::Ref<MegastructureSpec>& MegastructureSpec::Shipyard()
	{
		static const cs::Ref<MegastructureSpec> s = []
		{
			auto x = cs::New<MegastructureSpec>("Shipyard", 120, YieldList{
				{ Good::NickelIron, 400e3 }, { Good::Titanium, 3e3 }, { Good::Copper, 300.0 } });
			x->Completed = [](const cs::Ref<BeltMining>& mining, const cs::Ref<MegastructureProject>& p, double jd)
			{
				if (mining->YardBuilt) mining->YardBuilt(p, jd);
			};
			return x;
		}();
		return s;
	}

	const cs::Ref<MegastructureSpec>& MegastructureSpec::SpaceElevator()
	{
		static const cs::Ref<MegastructureSpec> s = cs::New<MegastructureSpec>("Space elevator", 365, YieldList{
			{ Good::NickelIron, 1.5e6 }, { Good::Hydrocarbons, 400e3 }, { Good::Titanium, 10e3 }, { Good::Copper, 2e3 } });
		return s;
	}

	const cs::Ref<MegastructureSpec>& MegastructureSpec::DysonRing()
	{
		static const cs::Ref<MegastructureSpec> s = []
		{
			auto x = cs::New<MegastructureSpec>("Dyson swarm ring", 730, YieldList{
				{ Good::NickelIron, 20e6 }, { Good::Titanium, 50e3 }, { Good::Copper, 30e3 }, { Good::RareEarths, 500.0 }, { Good::Platinum, 20.0 } });
			x->Completed = [](const cs::Ref<BeltMining>& mining, const cs::Ref<MegastructureProject>& p, double jd)
			{
				mining->Begin(DysonRing(), p->Builder, p->SiteBody, p->BaseName, jd, p->Stage + 1);
			};
			return x;
		}();
		return s;
	}

	cs::List<cs::Ref<MegastructureSpec>>& MegastructureSpec::registered()
	{
		static cs::List<cs::Ref<MegastructureSpec>> list = { Shipyard(), SpaceElevator(), DysonRing() };
		return list;
	}

	const cs::List<cs::Ref<MegastructureSpec>>& MegastructureSpec::All() { return registered(); }

	void MegastructureSpec::Register(const cs::Ref<MegastructureSpec>& spec)
	{
		if (spec != nullptr && !registered().Contains(spec)) registered().Add(spec);
	}

	cs::Ref<MegastructureSpec> MegastructureSpec::Find(const cs::String& name)
	{
		for (const auto& s : registered())
			if (s->Name == name) return s;
		return nullptr;
	}

	double MegastructureProject::Progress() const
	{
		double p = 1;
		for (int g = 0; g < GoodInfo::Count; g++)
			if (Spec->Bill[g] > 0) p = cs::Math::Min(p, Used[g] / Spec->Bill[g]);
		return p;
	}

	// ---------------------------------------------------------------- BeltMining

	double BeltMining::RockTonnes(double diameterM, double density) { return cs::Math::PI / 6 * diameterM * diameterM * diameterM * density; }

	cs::Ref<TugHaul> BeltMining::HaulOf(const ShipRef& tug) const
	{
		if (tug == nullptr) return nullptr;
		for (const auto& h : Hauls)
			if (h->Tug == tug && h->State == ConvoyState::EnRoute) return h;
		if (Captures != nullptr)
			for (const auto& h : *Captures)
				if (h->Tug == tug && h->State == ConvoyState::EnRoute) return h;
		return nullptr;
	}

	cs::Ref<MiningOperation> BeltMining::OperationOf(const ShipRef& tug) const
	{
		if (tug == nullptr) return nullptr;
		for (const auto& op : Operations)
			if ((op->Tugs != nullptr && op->Tugs->Contains(tug)) || (op->CaptureHulls != nullptr && op->CaptureHulls->Contains(tug))) return op;
		return nullptr;
	}

	int BeltMining::TugNext(const ShipRef& tug, int here)
	{
		auto h = HaulOf(tug);
		if (h != nullptr)
		{
			if (h->Latched) return h->ToBody;
			tug->Wander = true;
			return here;
		}
		auto op = OperationOf(tug);
		if (op == nullptr) return -1;
		return op->CaptureHulls != nullptr && op->CaptureHulls->Contains(tug) ? op->RefineryBody : op->TugHome();
	}

	bool BeltMining::TugArrived(const ShipRef& tug, double jd)
	{
		auto h = HaulOf(tug);
		if (h == nullptr) return false;
		if (!h->Latched)
		{
			if (tug->Orbit == nullptr || tug->Orbit->Body != h->FromBody) return false;
			h->Latched = true;
			tug->Tow = h->Load;
			tug->Homing = tug->Orbit->Body == h->ToBody;
			return true;
		}
		if (tug->Orbit == nullptr || tug->Orbit->Body != h->ToBody || tug->Homing) return false;
		h->ArriveJd = h->FreeJd = jd;
		tug->Tow = nullptr;
		Deliver(h, jd);
		return true;
	}

	bool BeltMining::Gone(const cs::Ref<MiningOperation>& op, const ShipRef& tug)
	{
		return tug == nullptr || tug->Destroyed || tug->Derelict || (op != nullptr && tug->Faction != op->Owner);
	}

	double BeltMining::TugMassTonnes()
	{
		auto spec = CombatSpec::Of(ShipClass::OreTug);
		return spec->DryMassTonnes + spec->PropellantTonnes;
	}

	double BeltMining::TugAccel(double loadTonnes)
	{
		double tug = TugMassTonnes();
		return ShipClassSpec::Of(ShipClass::OreTug)->AccelG * 9.80665 * tug / (tug + cs::Math::Max(0.0, loadTonnes));
	}

	double BeltMining::PushDays(double km, double loadTonnes)
	{
		if (km <= 0) return 0;
		return 2 * cs::Math::Sqrt(km * 1000 / TugAccel(loadTonnes)) / Astro::SecondsPerDay;
	}

	double BeltMining::CaptureTugMassTonnes()
	{
		auto spec = CombatSpec::Of(ShipClass::CaptureTug);
		return spec->DryMassTonnes + spec->PropellantTonnes;
	}

	double BeltMining::CaptureTugAccel(double loadTonnes)
	{
		double tug = CaptureTugMassTonnes();
		return ShipClassSpec::Of(ShipClass::CaptureTug)->AccelG * 9.80665 * tug / (tug + cs::Math::Max(0.0, loadTonnes));
	}

	double BeltMining::CaptureDaysFor(double loadTonnes)
	{
		double m = TugLocalHaulKm * 1000;
		double outS = 2 * cs::Math::Sqrt(m / CaptureTugAccel(0)), inS = 2 * cs::Math::Sqrt(m / CaptureTugAccel(loadTonnes));
		return (outS + inS) / Astro::SecondsPerDay;
	}

	double BeltMining::RockDiameter(double tonnes, double density) { return cs::Math::Pow(6 * tonnes / (cs::Math::PI * density), 1.0 / 3); }

	BeltMining::BeltMining(int seed)
		: rng(seed)
	{
	}

	// ---------------------------------------------------------------- setting up

	cs::Ref<MaterialWarehouse> BeltMining::AddWarehouse(const cs::String& name, const cs::Ref<Faction>& owner, int body, double capacityTonnes)
	{
		auto w = cs::New<MaterialWarehouse>();
		w->Name = name;
		w->Owner = owner;
		w->Body = body;
		w->CapacityTonnes = capacityTonnes;
		Warehouses.Add(w);
		return w;
	}

	cs::Ref<MiningOperation> BeltMining::AddOperation(const cs::String& name, const cs::Ref<Faction>& owner, int refineryBody, const cs::Ref<MaterialWarehouse>& destination)
	{
		auto op = cs::New<MiningOperation>();
		op->Name = name;
		op->Owner = owner;
		op->RefineryBody = refineryBody;
		op->Destination = destination;
		Operations.Add(op);
		return op;
	}

	cs::Ref<MegastructureProject> BeltMining::Begin(const cs::Ref<MegastructureSpec>& spec, const cs::Ref<Faction>& builder, int siteBody, const cs::String& name, double jd, int stage)
	{
		auto p = cs::New<MegastructureProject>();
		p->Spec = spec;
		p->Builder = builder;
		p->SiteBody = siteBody;
		p->BaseName = name;
		p->Stage = stage;
		p->StartJd = jd;
		p->Name = stage > 1 ? cs::Cat(name, " (", cs::Strings::ToLowerInvariant(spec->Name), " ", stage, ")") : name;
		Projects.Add(p);
		return p;
	}

	// ---------------------------------------------------------------- running

	void BeltMining::Update(double jd)
	{
		if (cs::Double::IsNaN(nextDayJd)) nextDayJd = jd;
		while (nextDayJd <= jd)
		{
			Day(nextDayJd);
			nextDayJd += 1;
		}
	}

	void BeltMining::Day(double jd)
	{
		double pressure = cs::Math::Max(0.0, cs::Math::Min(1.0, PiratePressure ? PiratePressure() : 0));
		for (const auto& op : Operations)
		{
			Capture(op, jd);
			Grind(op);
			Separate(op);
			Ship(op, jd);
			SendTugs(op, jd);
		}
		MoveConvoys(jd, pressure);
		MoveHauls(jd, pressure);
		RaidWarehouses(jd, pressure);
		Build(jd);
	}

	void BeltMining::Capture(const cs::Ref<MiningOperation>& op, double jd)
	{
		if (op->CaptureHulls != nullptr)
		{
			SendCaptureHulls(op, jd);
			return;
		}
		if (op->TugReadyJd.Count() != op->CaptureTugs)
		{
			op->TugReadyJd = cs::NewArray<double>(op->CaptureTugs);
			// The tugs are spread out over their first trip rather than all bringing a rock in on the same day.
			for (int i = 0; i < op->CaptureTugs; i++) op->TugReadyJd[i] = jd + op->CaptureDays * (i + 1.0) / op->CaptureTugs;
		}
		for (int i = 0; i < op->TugReadyJd.Count(); i++)
		{
			if (jd < op->TugReadyJd[i] || op->Backlog.Count() >= op->MaxBacklog) continue;
			auto kind = AsteroidClass::Pick(rng);
			double d = op->MinRockM + (op->MaxRockM - op->MinRockM) * rng.NextDouble();
			double t = RockTonnes(d, kind->Density);
			auto rock = cs::New<CapturedRock>();
			rock->Designation = cs::Cat(op->Name, " rock ", ++rockSerial);
			rock->Class = kind;
			rock->DiameterM = d;
			rock->Tonnes = t;
			rock->Remaining = t;
			rock->CapturedJd = jd;
			op->Backlog.Add(rock);
			op->RocksCaptured++;
			op->TonnesCaptured += t;
			op->TugReadyJd[i] = jd + op->CaptureDays * (0.6 + 0.8 * rng.NextDouble());
		}
	}

	void BeltMining::SendCaptureHulls(const cs::Ref<MiningOperation>& op, double jd)
	{
		if (!Captures) Captures = cs::New<cs::List<cs::Ref<TugHaul>>>();
		for (int i = op->CaptureHulls->Count() - 1; i >= 0; i--)
		{
			auto tug = (*op->CaptureHulls)[i];
			if (!Gone(op, tug) || HaulOf(tug) != nullptr) continue;
			op->CaptureHulls->RemoveAt(i);
			op->CaptureTugsLost++;
		}
		op->CaptureTugs = op->CaptureHulls->Count();
		int inbound = 0;
		for (const auto& h : Hauls)
			if (h->Operation == op && h->State == ConvoyState::EnRoute && h->ToDepot == nullptr) inbound++;
		for (const auto& h : *Captures)
			if (h->Operation == op && h->State == ConvoyState::EnRoute) inbound++;
		auto hulls = cs::List<ShipRef>(*op->CaptureHulls);
		for (const auto& tug : hulls)
		{
			if (op->Backlog.Count() + inbound >= op->MaxBacklog) return;
			if (tug->Tow != nullptr || tug->Phase != ShipPhase::Parked || tug->Orbit == nullptr || tug->Orbit->Body != op->RefineryBody) continue;
			if (HaulOf(tug) != nullptr || (TugFree != nullptr && !TugFree(tug))) continue;
			auto kind = AsteroidClass::Pick(rng);
			double d = op->MinRockM + (op->MaxRockM - op->MinRockM) * rng.NextDouble();
			double t = RockTonnes(d, kind->Density);
			auto haul = cs::New<TugHaul>();
			haul->Name = cs::Cat(op->Name, " capture ", ++haulSerial);
			haul->Operation = op;
			haul->FromBody = op->RefineryBody;
			haul->ToBody = op->RefineryBody;
			haul->Capture = true;
			haul->Tug = tug;
			haul->AccelMs2 = CaptureTugAccel(t);
			haul->DepartJd = jd;
			haul->ArriveJd = jd + cs::Math::Max(1.0, CaptureDaysFor(t));
			// Its hull, not a timetable, says when it is free again.
			haul->FreeJd = cs::Double::PositiveInfinity;
			auto load = cs::New<CapturedRock>();
			load->Designation = cs::Cat(op->Name, " rock ", ++rockSerial);
			load->Class = kind;
			load->DiameterM = d;
			load->Tonnes = t;
			load->Remaining = t;
			haul->Load = load;
			Captures->Add(haul);
			inbound++;
			// The rock lies out in the field round the refinery: the tug flies out to it with its claws open first.
			if (HaulSent) HaulSent(haul, jd);
		}
	}

	void BeltMining::Grind(const cs::Ref<MiningOperation>& op)
	{
		double left = op->GrindTonnesPerDay;
		while (left > 0 && op->Backlog.Count() > 0)
		{
			auto rock = op->Backlog[0];
			double t = cs::Math::Min(left, rock->Remaining);
			rock->Remaining -= t;
			left -= t;
			op->Dust += t;
			op->TonnesGround += t;
			for (const auto& [g, f] : rock->Class->Yield) op->DustContent[static_cast<int>(g)] += t * f;
			if (rock->Remaining <= 1e-6) op->Backlog.RemoveAt(0);
		}
	}

	void BeltMining::Separate(const cs::Ref<MiningOperation>& op)
	{
		if (op->Dust <= 0) return;
		double t = cs::Math::Min(op->SeparateTonnesPerDay, op->Dust);
		double share = t / op->Dust;
		double recovered = 0;
		for (int g = 0; g < GoodInfo::Count; g++)
		{
			double content = op->DustContent[g] * share;
			op->DustContent[g] -= content;
			double got = content * op->SeparationEfficiency;
			op->Separated[g] += got;
			recovered += got;
		}
		op->Dust -= t;
		if (op->Dust < 1e-6)
		{
			op->Dust = 0;
			for (auto& x : op->DustContent) x = 0;
		}
		op->TonnesSeparated += recovered;
		op->Slag += t - recovered;
	}

	void BeltMining::Ship(const cs::Ref<MiningOperation>& op, double jd)
	{
		if (op->Freighters != nullptr)
		{
			SailFreighters(op, jd);
			return;
		}
		NewConvoy(op, jd, op->ConvoyTonnes);
	}

	cs::Ref<MaterialConvoy> BeltMining::NewConvoy(const cs::Ref<MiningOperation>& op, double jd, double most)
	{
		auto to = op->Destination;
		if (to == nullptr) return nullptr;
		if (cs::Double::IsNaN(op->LastConvoyJd)) op->LastConvoyJd = jd;
		double waiting = op->WaitingTonnes();
		if (waiting <= 0) return nullptr;
		if (waiting < most && jd - op->LastConvoyJd < op->MaxHoldDays) return nullptr;
		// Nothing sails for a warehouse with no room, nor more than it can still take (counting what is already on the way to it).
		double room = to->CapacityTonnes - to->StoredTonnes();
		for (const auto& c : Convoys)
			if (c->State == ConvoyState::EnRoute && c->To == to) room -= c->Tonnes();
		double load = cs::Math::Min(cs::Math::Min(waiting, most), room);
		if (load <= 0) return nullptr;

		auto convoy = cs::New<MaterialConvoy>();
		convoy->Name = cs::Cat(op->Name, " convoy ", ++convoySerial);
		convoy->From = op;
		convoy->To = to;
		convoy->DepartJd = jd;
		double share = load / waiting;
		for (int g = 0; g < GoodInfo::Count; g++)
		{
			double t = op->Separated[g] * share;
			convoy->Cargo[g] = t;
			op->Separated[g] -= t;
		}
		convoy->ArriveJd = jd + TransitDays(op->RefineryBody, to->Body, jd);
		Convoys.Add(convoy);
		op->ConvoysSent++;
		op->LastConvoyJd = jd;
		return convoy;
	}

	void BeltMining::SailFreighters(const cs::Ref<MiningOperation>& op, double jd)
	{
		for (int i = op->Freighters->Count() - 1; i >= 0; i--)
		{
			auto f = (*op->Freighters)[i];
			if (!Gone(op, f) || ConvoyOf(f) != nullptr) continue;
			op->Freighters->RemoveAt(i);
			op->FreightersLost++;
		}
		op->ConvoyFreighters = op->Freighters->Count();
		auto freighters = cs::List<ShipRef>(*op->Freighters);
		for (const auto& f : freighters)
		{
			if (f->HasCargo || f->Phase != ShipPhase::Parked || f->Orbit == nullptr || f->Orbit->Body != op->RefineryBody) continue;
			if (ConvoyOf(f) != nullptr || (FreighterFree != nullptr && !FreighterFree(f))) continue;
			auto convoy = NewConvoy(op, jd, f->CapacityTonnes > 0 ? f->CapacityTonnes : op->ConvoyTonnes);
			if (convoy == nullptr) return;
			convoy->Freighter = f;
			// The load goes in its hold: its heaviest element names it, the whole tonnage weighs it down (and draws raiders).
			int main = 0;
			for (int g = 1; g < GoodInfo::Count; g++)
				if (convoy->Cargo[g] > convoy->Cargo[main]) main = g;
			f->HasCargo = true;
			f->CargoGood = static_cast<Good>(main);
			f->CargoTonnes = convoy->Tonnes();
			f->CargoCost = 0;
			f->CargoDestination = nullptr;
			// Its hull, not a timetable, says when it is in.
			if (ConvoySailed) ConvoySailed(convoy, jd);
		}
	}

	double BeltMining::TransitDays(int from, int to, double jd) const
	{
		if (Position == nullptr || from == to) return MinTransitDays;
		double au = (Position(to, jd) - Position(from, jd)).Length() / Astro::AuKm;
		return cs::Math::Max(MinTransitDays, au / ConvoyAuPerDay);
	}

	void BeltMining::MoveConvoys(double jd, double pressure)
	{
		// A list, not a foreach: a freighter lost here may set its world planning afresh.
		for (int k = 0; k < Convoys.Count(); k++)
		{
			auto c = Convoys[k];
			if (c->State != ConvoyState::EnRoute) continue;
			if (c->Freighter != nullptr)
			{
				MoveFreighter(c, jd);
				continue;
			}
			double risk = ConvoyTheftPerDay * pressure * (1 - EscortCut * Clamp01(c->From ? c->From->EscortShare : 0));
			if (risk > 0 && rng.NextDouble() < risk)
			{
				c->State = ConvoyState::Stolen;
				c->TakenBy = Raiders ? Raiders() : nullptr;
				for (int g = 0; g < GoodInfo::Count; g++) Plundered[g] += c->Cargo[g];
				ConvoysStolen++;
				if (Log) Log(jd, cs::Cat("Raiders", By(c->TakenBy), " took the ", c->Name, " on its way to the ", c->To->Name, ": ", cs::Fmt(c->Tonnes(), "N0"), " t of raw materials gone"), c->To->Body);
				ConvoyStolen.Invoke(c);
				continue;
			}
			if (jd < c->ArriveJd) continue;
			c->State = ConvoyState::Delivered;
			for (int g = 0; g < GoodInfo::Count; g++) c->To->Stock[g] += c->Cargo[g];
			ConvoysDelivered++;
		}
		// Keep the history bounded: drop the oldest finished convoys.
		int finished = 0;
		for (const auto& c : Convoys)
			if (c->State != ConvoyState::EnRoute) finished++;
		for (int i = 0; i < Convoys.Count() && finished > 300; i++)
		{
			if (Convoys[i]->State == ConvoyState::EnRoute) continue;
			Convoys.RemoveAt(i--);
			finished--;
		}
	}

	// ---------------------------------------------------------------- the convoys' own freighters (U.SPC-T523)

	cs::Ref<MaterialConvoy> BeltMining::ConvoyOf(const ShipRef& freighter) const
	{
		if (freighter == nullptr) return nullptr;
		for (const auto& c : Convoys)
			if (c->Freighter == freighter && c->State == ConvoyState::EnRoute) return c;
		return nullptr;
	}

	cs::Ref<MiningOperation> BeltMining::WorksOf(const ShipRef& freighter) const
	{
		if (freighter == nullptr) return nullptr;
		for (const auto& op : Operations)
			if (op->Freighters != nullptr && op->Freighters->Contains(freighter)) return op;
		return nullptr;
	}

	int BeltMining::FreighterNext(const ShipRef& freighter, int /*here*/) const
	{
		auto c = ConvoyOf(freighter);
		if (c != nullptr) return c->To->Body;
		auto op = WorksOf(freighter);
		return op != nullptr ? op->RefineryBody : -1;
	}

	bool BeltMining::FreighterArrived(const ShipRef& freighter, double jd)
	{
		auto c = ConvoyOf(freighter);
		if (c == nullptr || freighter->Orbit == nullptr || freighter->Orbit->Body != c->To->Body) return false;
		c->State = ConvoyState::Delivered;
		c->ArriveJd = jd;
		for (int g = 0; g < GoodInfo::Count; g++) c->To->Stock[g] += c->Cargo[g];
		ConvoysDelivered++;
		freighter->HasCargo = false;
		freighter->CargoTonnes = 0;
		freighter->CargoDestination = nullptr;
		if (ConvoyDelivered) ConvoyDelivered(c, jd);
		return true;
	}

	void BeltMining::MoveFreighter(const cs::Ref<MaterialConvoy>& c, double jd)
	{
		auto op = c->From;
		auto f = c->Freighter;
		if (!Gone(op, f)) return;
		if ((op ? op->Freighters : nullptr) != nullptr && op->Freighters->Remove(f))
		{
			op->FreightersLost++;
			op->ConvoyFreighters = op->Freighters->Count();
		}
		bool boarded = f->Derelict || (op != nullptr && f->Faction != op->Owner) || !f->HasCargo;
		if (!boarded)
		{
			// Blown apart with the load still aboard: nobody the richer.
			c->State = ConvoyState::Lost;
			ConvoysLost++;
			if (Log) Log(jd, cs::Cat("The freighter ", f->Name, " of the ", (op ? op->Name : cs::String()), " is lost with the ", c->Name, "'s ", cs::Fmt(c->Tonnes(), "N0"), " t of raw materials"), c->To->Body);
			ConvoyLost.Invoke(c);
			return;
		}
		c->State = ConvoyState::Stolen;
		if (f->Derelict) c->TakenBy = f->DerelictBy ? f->DerelictBy : (Raiders ? Raiders() : nullptr);
		else c->TakenBy = op != nullptr && f->Faction != op->Owner ? f->Faction : (Raiders ? Raiders() : nullptr);
		for (int g = 0; g < GoodInfo::Count; g++) Plundered[g] += c->Cargo[g];
		ConvoysStolen++;
		if (Log) Log(jd, cs::Cat("Raiders", By(c->TakenBy), " boarded the freighter ", f->Name, " and took the ", c->Name, " on its way to the ", c->To->Name, ": ", cs::Fmt(c->Tonnes(), "N0"), " t of raw materials gone"), f->Orbit ? f->Orbit->Body : c->To->Body);
		ConvoyStolen.Invoke(c);
	}

	// ---------------------------------------------------------------- ore tugs (U.SPC-T483)

	double BeltMining::OpenKm(int from, int to, double jd) const
	{
		double km = TugLocalHaulKm;
		if (Position != nullptr && from != to) km += (Position(to, jd) - Position(from, jd)).Length();
		return km;
	}

	double BeltMining::HaulDays(int from, int to, double tonnes, double jd, cs::Nullable<HeavyLanePath>& heavy) const
	{
		heavy.reset();
		double open = tonnes <= OpenSpaceMaxTonnes ? PushDays(OpenKm(from, to, jd), tonnes) : cs::Double::PositiveInfinity;
		cs::Nullable<HeavyLanePath> lane = from == to ? cs::Nullable<HeavyLanePath>() : (HeavyLane ? HeavyLane(from, to, jd) : cs::Nullable<HeavyLanePath>());
		if (lane.has_value() && tonnes <= Hyperlanes::HeavyLaneMaxTonnes)
		{
			const HeavyLanePath& h = *lane;
			double byLane = PushDays(TugLocalHaulKm, tonnes) + 2 * PushDays(h.LegKm / 2, tonnes) + Hyperlanes::HeavyRideDays(h.LaneKm);
			if (byLane <= open)
			{
				heavy = h;
				return byLane;
			}
		}
		return open;
	}

	double BeltMining::MaxLoadTonnes(int from, int to, double jd) const
	{
		return from != to && HeavyLane && HeavyLane(from, to, jd).has_value() ? Hyperlanes::HeavyLaneMaxTonnes : OpenSpaceMaxTonnes;
	}

	void BeltMining::SendTugs(const cs::Ref<MiningOperation>& op, double jd)
	{
		if (op->Tugs != nullptr)
		{
			SendHulls(op, jd);
			return;
		}
		if (op->OreTugs <= 0) return;
		int busy = 0, inbound = 0;
		for (const auto& h : Hauls)
		{
			if (h->Operation != op || h->State == ConvoyState::Stolen || h->State == ConvoyState::Lost) continue;
			if (jd < h->FreeJd) busy++;
			if (h->State == ConvoyState::EnRoute && h->ToDepot == nullptr) inbound++;
		}
		for (; busy < op->OreTugs; busy++)
			if (NewHaul(op, jd, inbound) == nullptr) return;
	}

	void BeltMining::SendHulls(const cs::Ref<MiningOperation>& op, double jd)
	{
		for (int i = op->Tugs->Count() - 1; i >= 0; i--)
		{
			auto tug = (*op->Tugs)[i];
			if (!Gone(op, tug) || HaulOf(tug) != nullptr) continue;
			op->Tugs->RemoveAt(i);
			op->TugsLost++;
		}
		op->OreTugs = op->Tugs->Count();
		int inbound = 0;
		for (const auto& h : Hauls)
			if (h->Operation == op && h->State == ConvoyState::EnRoute && h->ToDepot == nullptr) inbound++;
		// The capture tugs' rocks on the way in fill the same grinders' yard (U.SPC-T522).
		if (Captures != nullptr)
			for (const auto& h : *Captures)
				if (h->Operation == op && h->State == ConvoyState::EnRoute) inbound++;
		auto tugs = cs::List<ShipRef>(*op->Tugs);
		for (const auto& tug : tugs)
		{
			if (tug->Tow != nullptr || tug->Phase != ShipPhase::Parked || tug->Orbit == nullptr || tug->Orbit->Body != op->TugHome()) continue;
			if (HaulOf(tug) != nullptr || (TugFree != nullptr && !TugFree(tug))) continue;
			auto haul = NewHaul(op, jd, inbound);
			if (haul == nullptr) return;
			haul->Tug = tug;
			// A load for anywhere else is latched on where it lies, in the field the tug is at; one for this very body
			// lies out in its field, and the tug flies out empty to it first.
			haul->Latched = haul->FromBody != haul->ToBody;
			if (haul->Latched) tug->Tow = haul->Load;
			// Its hull, not a timetable, says when it is free again.
			haul->FreeJd = cs::Double::PositiveInfinity;
			if (HaulSent) HaulSent(haul, jd);
		}
	}

	cs::Ref<TugHaul> BeltMining::NewHaul(const cs::Ref<MiningOperation>& op, double jd, int& inbound)
	{
		bool ice = op->TugIceShare > 0 && rng.NextDouble() < op->TugIceShare;
		// Melted ice is never drawn down by a build that needs none: a full ice depot sends the tug for ore instead of idling it.
		if (ice && op->IceDepot != nullptr && op->IceDepot->StoredTonnes() >= op->IceDepot->CapacityTonnes) ice = false;
		auto kind = ice ? AsteroidClass::CometIce() : AsteroidClass::Pick(rng);
		auto depot = ice ? op->IceDepot : nullptr;
		if (depot == nullptr)
		{
			// The grinders' yard is full (counting what is on the way): the tugs stand down.
			if (op->Backlog.Count() + inbound >= op->MaxBacklog) return nullptr;
		}
		else if (depot->StoredTonnes() >= depot->CapacityTonnes) return nullptr;

		int from = op->TugHome();
		int to = depot ? depot->Body : op->RefineryBody;
		double d = op->TugMinLoadM + (op->TugMaxLoadM - op->TugMinLoadM) * rng.NextDouble();
		double t = RockTonnes(d, kind->Density), cap = MaxLoadTonnes(from, to, jd);
		if (t > cap)
		{
			// Too big for any way in: the tug cuts (or picks) one that is not.
			t = cap;
			d = RockDiameter(t, kind->Density);
		}
		cs::Nullable<HeavyLanePath> heavy;
		double days = HaulDays(from, to, t, jd, heavy);
		if (cs::Double::IsInfinity(days)) return nullptr;
		// Home again empty, the same way, at its unladen acceleration.
		cs::Nullable<HeavyLanePath> discard;
		double back = HaulDays(from, to, 0, jd, discard);
		auto haul = cs::New<TugHaul>();
		haul->Name = cs::Cat(op->Name, " tug haul ", ++haulSerial);
		haul->Operation = op;
		haul->FromBody = from;
		haul->ToBody = to;
		haul->ToDepot = depot;
		haul->HeavyLane = heavy ? heavy->Name : cs::String();
		haul->AccelMs2 = TugAccel(t);
		haul->DepartJd = jd;
		haul->ArriveJd = jd + cs::Math::Max(1.0, days);
		auto load = cs::New<CapturedRock>();
		load->Designation = ice ? cs::Cat(op->Name, " ice block ", haulSerial) : cs::Cat(op->Name, " rock ", ++rockSerial);
		load->Class = kind;
		load->DiameterM = d;
		load->Tonnes = t;
		load->Remaining = t;
		haul->Load = load;
		haul->FreeJd = haul->ArriveJd + cs::Math::Max(1.0, back);
		Hauls.Add(haul);
		op->TugHaulsSent++;
		if (depot == nullptr) inbound++;
		return haul;
	}

	void BeltMining::MoveHauls(double jd, double pressure)
	{
		// A list, not a foreach: a hull handed over to raiders may set its world planning afresh.
		for (int k = 0; k < Hauls.Count(); k++)
		{
			auto h = Hauls[k];
			if (h->State != ConvoyState::EnRoute) continue;
			if (h->Tug != nullptr)
			{
				MoveHull(h, jd);
				continue;
			}
			auto op = h->Operation;
			double risk = TugTheftPerDay * pressure * (1 - EscortCut * Clamp01(op ? op->EscortShare : 0));
			if (risk > 0 && rng.NextDouble() < risk)
			{
				Seize(h, Raiders ? Raiders() : nullptr, h->ToBody, jd);
				continue;
			}
			if (jd < h->ArriveJd) continue;
			Deliver(h, jd);
		}
		// Keep the history bounded: drop the oldest finished hauls whose tugs are home (or gone).
		int finished = 0;
		for (const auto& h : Hauls)
			if (Finished(h, jd)) finished++;
		for (int i = 0; i < Hauls.Count() && finished > 300; i++)
		{
			if (!Finished(Hauls[i], jd)) continue;
			Hauls.RemoveAt(i--);
			finished--;
		}
		MoveCaptures(jd);
	}

	void BeltMining::MoveCaptures(double jd)
	{
		if (Captures == nullptr) return;
		for (int k = 0; k < Captures->Count(); k++)
		{
			auto h = (*Captures)[k];
			if (h->State == ConvoyState::EnRoute) MoveHull(h, jd);
		}
		int finished = 0;
		for (const auto& h : *Captures)
			if (Finished(h, jd)) finished++;
		for (int i = 0; i < Captures->Count() && finished > 300; i++)
		{
			if (!Finished((*Captures)[i], jd)) continue;
			Captures->RemoveAt(i--);
			finished--;
		}
	}

	bool BeltMining::Finished(const cs::Ref<TugHaul>& h, double jd)
	{
		return h->State == ConvoyState::Stolen || h->State == ConvoyState::Lost || (h->State == ConvoyState::Delivered && jd >= h->FreeJd);
	}

	void BeltMining::MoveHull(const cs::Ref<TugHaul>& h, double jd)
	{
		auto op = h->Operation;
		auto tug = h->Tug;
		if (Gone(op, tug))
		{
			if (tug->Destroyed && !tug->Derelict && (op == nullptr || tug->Faction == op->Owner))
			{
				// Blown apart with its load: nobody the richer.
				h->State = ConvoyState::Lost;
				LoseHull(h);
				if (h->Capture) CaptureTugsDestroyed++;
				else TugsDestroyed++;
				if (Log) Log(jd, cs::Cat("The ", h->TugKind(), " ", tug->Name, " of the ", (op ? op->Name : cs::String()), " is lost with the ", cs::Fmt(h->Load->Tonnes, "N0"), " t of ", cs::Strings::ToLowerInvariant(h->Load->Class->Name), " it was pushing"), h->ToBody);
				return;
			}
			// Boarded in a fight: left adrift by its captors or flying their colours, its load is theirs.
			cs::Ref<Faction> by;
			if (tug->Derelict) by = tug->DerelictBy ? tug->DerelictBy : (Raiders ? Raiders() : nullptr);
			else by = tug->Faction;
			Seize(h, by, tug->Orbit ? tug->Orbit->Body : h->ToBody, jd, /*handOver:*/ false);
			return;
		}
		if (tug->Battle != nullptr) return;
		auto danger = TugThreat ? TugThreat(h, jd) : TugDanger();
		double risk = TugTheftPerDay * Clamp01(danger.Pressure) * (1 - EscortCut * Clamp01(op ? op->EscortShare : 0));
		if (risk > 0 && rng.NextDouble() < risk) Seize(h, danger.Raiders ? danger.Raiders : (Raiders ? Raiders() : nullptr), danger.Body >= 0 ? danger.Body : h->ToBody, jd);
	}

	void BeltMining::LoseHull(const cs::Ref<TugHaul>& h)
	{
		auto op = h->Operation;
		if (h->Capture)
		{
			// A capture tug's hull (U.SPC-T522): gone from its works' capture tugs.
			if (h->Tug != nullptr)
			{
				h->Tug->Tow = nullptr;
				h->Tug->Homing = false;
				if (op && op->CaptureHulls) op->CaptureHulls->Remove(h->Tug);
			}
			if (op == nullptr) return;
			op->CaptureTugs = op->CaptureHulls != nullptr ? op->CaptureHulls->Count() : cs::Math::Max(0, op->CaptureTugs - 1);
			op->CaptureTugsLost++;
			return;
		}
		if (h->Tug != nullptr)
		{
			h->Tug->Tow = nullptr;
			h->Tug->Homing = false;
			if (op && op->Tugs) op->Tugs->Remove(h->Tug);
		}
		if (op == nullptr) return;
		op->OreTugs = op->Tugs != nullptr ? op->Tugs->Count() : cs::Math::Max(0, op->OreTugs - 1);
		op->TugsLost++;
	}

	void BeltMining::Seize(const cs::Ref<TugHaul>& h, const cs::Ref<Faction>& by, int body, double jd, bool handOver)
	{
		auto op = h->Operation;
		h->State = ConvoyState::Stolen;
		h->TakenBy = by;
		h->FreeJd = jd;
		for (const auto& [g, f] : h->Load->Class->Yield) Plundered[static_cast<int>(g)] += h->Load->Tonnes * f;
		LoseHull(h);
		if (h->Capture) CaptureTugsTaken++;
		else TugsTaken++;
		cs::String tug = h->Tug != nullptr ? cs::Cat("the ", h->TugKind(), " ", h->Tug->Name) : h->Capture ? cs::String("a capture tug") : cs::String("an ore tug");
		if (Log) Log(jd, cs::Cat("Raiders", By(h->TakenBy), " seized ", tug, " of the ", (op ? op->Name : cs::String()), " and the ", cs::Fmt(h->Load->Tonnes, "N0"), " t of ", cs::Strings::ToLowerInvariant(h->Load->Class->Name), " it was pushing"), body);
		if (handOver && h->Tug != nullptr && TugSeized) TugSeized(h, jd);
		TugTaken.Invoke(h);
	}

	void BeltMining::Deliver(const cs::Ref<TugHaul>& h, double jd)
	{
		auto op = h->Operation;
		h->State = ConvoyState::Delivered;
		if (h->Capture)
		{
			// A capture tug's catch (U.SPC-T522): the rock is parked at the refinery for the grinders.
			if (op == nullptr) return;
			h->Load->CapturedJd = jd;
			op->Backlog.Add(h->Load);
			op->RocksCaptured++;
			op->TonnesCaptured += h->Load->Tonnes;
			return;
		}
		TugHaulsDelivered++;
		if (op == nullptr) return;
		op->TugHaulsDelivered++;
		op->TugTonnesDelivered += h->Load->Tonnes;
		if (h->ToDepot != nullptr)
		{
			// Ice is melted at the depot: its water and volatiles go into stock, the grit is dumped. Never past its room.
			auto w = h->ToDepot;
			double room = cs::Math::Max(0.0, w->CapacityTonnes - w->StoredTonnes()), got = 0;
			for (const auto& [_, f] : h->Load->Class->Yield) got += h->Load->Tonnes * f * op->SeparationEfficiency;
			double share = got > room ? room / got : 1;
			for (const auto& [g, f] : h->Load->Class->Yield) w->Stock[static_cast<int>(g)] += h->Load->Tonnes * f * op->SeparationEfficiency * share;
		}
		else
		{
			// Parked at the refinery for the grinders, like a capture tug's catch.
			h->Load->CapturedJd = jd;
			op->Backlog.Add(h->Load);
			op->RocksCaptured++;
			op->TonnesCaptured += h->Load->Tonnes;
		}
	}

	void BeltMining::RaidWarehouses(double jd, double pressure)
	{
		for (const auto& w : Warehouses)
		{
			if (w->StoredTonnes() <= 0) continue;
			bool guarded = Guarded ? Guarded(w) : false;
			double risk = WarehouseRaidPerDay * pressure * (guarded ? 1 - GuardedRaidCut : 1);
			if (risk <= 0 || rng.NextDouble() >= risk) continue;
			double taken = 0;
			for (int g = 0; g < GoodInfo::Count; g++)
			{
				double t = w->Stock[g] * RaidShare;
				w->Stock[g] -= t;
				Plundered[g] += t;
				taken += t;
			}
			w->Raids++;
			w->TonnesLostToRaids += taken;
			WarehouseRaids++;
			// (C#'s `Log?.Invoke(...)` evaluates its arguments, Raiders among them, only when Log is set.)
			if (Log) Log(jd, cs::Cat("Raiders", By(Raiders ? Raiders() : nullptr), " broke into the ", w->Name, " and carried off ", cs::Fmt(taken, "N0"), " t of raw materials"), w->Body);
			WarehouseRaided.Invoke(w, taken);
		}
	}

	void BeltMining::Build(double jd)
	{
		// A list, not a foreach: a finished project may lay down the next.
		for (int i = 0; i < Projects.Count(); i++)
		{
			auto p = Projects[i];
			if (p->Done()) continue;
			for (int g = 0; g < GoodInfo::Count; g++)
			{
				double need = p->Spec->Bill[g] - p->Used[g];
				if (need <= 1e-9) continue;
				double want = cs::Math::Min(need, p->Spec->Bill[g] / cs::Math::Max(1.0, p->Spec->MinDays));
				for (const auto& w : Warehouses)
				{
					if (want <= 0) break;
					if (w->Owner != p->Builder) continue;
					double t = cs::Math::Min(want, w->Stock[g]);
					w->Stock[g] -= t;
					p->Used[g] += t;
					want -= t;
				}
			}
			if (p->Progress() < 1 - 1e-9) continue;
			p->DoneJd = jd;
			if (Log) Log(jd, cs::Cat((p->Builder ? p->Builder->Name : cs::String("Someone")), " finished the ", p->Name), p->SiteBody);
			if (p->Spec->Completed) p->Spec->Completed(shared_from_this(), p, jd);
			ProjectCompleted.Invoke(p);
		}
	}

	double BeltMining::Clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

	cs::String BeltMining::By(const cs::Ref<Faction>& f) { return f == nullptr ? cs::String() : cs::Cat(" of the ", f->Name); }
}
