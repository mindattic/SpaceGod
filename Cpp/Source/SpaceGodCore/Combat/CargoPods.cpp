// Not a C# file (US-T95): cargo pods and the tractor beam (see CargoPods.h).
#include "Combat/CargoPods.h"

#include "BodyCatalog.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Ships.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Salvage.h"
#include "Comms/Comms.h"
#include "Comms/Pilots.h"
#include "Economy/CargoHold.h"
#include "Economy/Goods.h"

#include "Persistence/ObjectGraph.h"

#include <cstdint>

namespace SpaceGod::Core
{
	namespace
	{
		uint32_t Mix(uint32_t x)
		{
			x ^= x >> 16; x *= 0x7feb352du;
			x ^= x >> 15; x *= 0x846ca68bu;
			x ^= x >> 16;
			return x;
		}

		double Unit(uint32_t h) { return (h & 0xFFFF) / 65535.0; }

		/// <summary>How many pods a spill of <paramref name="tonnes"/> of a good goes out as: about two crates a pod.</summary>
		int PodsFor(Good good, double tonnes)
		{
			double crates = tonnes / cs::Math::Max(1e-9, CargoFootprint::Of(good).Tonnes);
			return cs::ToInt(cs::Math::Clamp(cs::Math::Ceiling(crates / 2), 1.0, static_cast<double>(CargoPods::MaxPodsPerStack)));
		}
	}

	// ---------------------------------------------------------------- CargoPod

	double CargoPod::DriftKm(double jd) const
	{
		if (SpreadKm <= 0 || DriftKmS <= 0) return 0;
		double seconds = cs::Math::Max(0.0, jd - SpilledJd) * 86400.0;
		double tau = SpreadKm / DriftKmS;
		return SpreadKm * (1 - cs::Math::Exp(-seconds / tau));
	}

	DVec3 CargoPod::DriftedAt(double jd) const { return Local + DriftDir * DriftKm(jd); }

	DVec3 CargoPod::Helio(const cs::Ref<World>& world, double jd) const
	{
		auto lanes = world->Fleet != nullptr ? world->Fleet->Lanes : nullptr;
		if (Site == nullptr || lanes == nullptr) return world->Ephemeris->HelioAt(Body, jd) + Anchor + LocalAt(jd);
		auto hop = Site->Hop;
		auto a = world->Ephemeris->HelioAt(hop.From, jd) + Site->FromOffset;
		auto b = world->Ephemeris->HelioAt(hop.To, jd) + Site->ToOffset;
		return lanes->CurveKm(hop.Lane, a, b, Site->Progress, jd) + Site->Side + LocalAt(jd);
	}

	// ---------------------------------------------------------------- CargoPods

	cs::Ref<CargoPod> CargoPods::Make(Good good, double tonnes, int salt)
	{
		auto p = cs::New<CargoPod>();
		p->Id = nextId++;
		p->What = good;
		p->Tonnes = tonnes;
		uint32_t h = Mix(static_cast<uint32_t>(p->Id) * 7919u + static_cast<uint32_t>(salt) * 104729u + 0x95u);
		double z = 2 * Unit(h) - 1, a = 2 * cs::Math::PI * Unit(h >> 16), r = cs::Math::Sqrt(1 - z * z);
		p->DriftDir = DVec3(r * cs::Math::Cos(a), r * cs::Math::Sin(a), z);
		uint32_t g = Mix(h);
		// 4-16 m/s off the hull, each pod its own share of the spread.
		p->DriftKmS = 0.004 + 0.012 * Unit(g);
		p->SpreadKm = 0.45 + 0.55 * Unit(g >> 16);
		return p;
	}

	cs::List<cs::Ref<CargoPod>> CargoPods::Spill(const cs::Ref<Wreck>& wreck, double jd)
	{
		cs::List<cs::Ref<CargoPod>> made;
		if (wreck == nullptr) return made;
		// A few hull lengths out at most, never more than a few km: close enough to sweep with a beam from beside the wreck.
		double spread = cs::Math::Clamp(3 * wreck->LengthKm + 0.4, 0.5, 6.0);
		double life = cs::Math::Max(0.0, wreck->LifetimeDays - (jd - wreck->CreatedJd));
		for (int i = 0; i < wreck->Salvage.Count(); i++)
		{
			auto stack = wreck->Salvage[i];
			double spill = stack.Tonnes * SpillShare;
			if (spill < CargoFootprint::Of(stack.Good).Tonnes * 0.5) continue;
			int n = PodsFor(stack.Good, spill);
			for (int k = 0; k < n; k++)
			{
				auto p = Make(stack.Good, spill / n, wreck->Id * 64 + i * 8 + k);
				p->From = wreck->Name;
				p->Owners = wreck->Faction;
				p->Body = wreck->Body;
				p->Anchor = wreck->Anchor;
				p->Local = wreck->Local;
				p->Site = wreck->Site;
				p->SpreadKm *= spread;
				p->SpilledJd = jd;
				p->LifetimeDays = life;
				p->WreckId = wreck->Id;
				Items.Add(p);
				made.Add(p);
			}
			wreck->Salvage[i] = SalvageStack(stack.Good, stack.Tonnes - spill);
		}
		return made;
	}

	double CargoPods::Jettison(const cs::Ref<World>& world, const CargoDrop& drop)
	{
		if (world == nullptr || drop.Tonnes <= 0) return 0;
		auto cat = world->Catalog();
		int best = 0;
		double bestD = cs::Double::PositiveInfinity;
		for (int b = 0; b < cat->Count(); b++)
		{
			double d = (world->Ephemeris->HelioAt(b, drop.Jd) - drop.HelioKm).Length();
			if (d < bestD) { bestD = d; best = b; }
		}
		double length = drop.From != nullptr && drop.From->Combat != nullptr ? drop.From->LengthKm() : 0.1;
		double spread = cs::Math::Clamp(2 * length + 0.3, 0.4, 3.0);
		int n = PodsFor(drop.What, drop.Tonnes);
		for (int k = 0; k < n; k++)
		{
			auto p = Make(drop.What, drop.Tonnes / n, (drop.From != nullptr ? drop.From->Id : 0) * 64 + k + 7);
			p->From = drop.From != nullptr ? drop.From->Name : cs::String("a jettison");
			p->Owners = drop.From != nullptr ? drop.From->Faction : nullptr;
			p->Stolen = drop.Stolen;
			p->Body = best;
			p->Anchor = drop.HelioKm - world->Ephemeris->HelioAt(best, drop.Jd);
			p->Local = DVec3::Zero;
			p->SpreadKm *= spread;
			p->SpilledJd = drop.Jd;
			p->LifetimeDays = JettisonLifetimeDays;
			Items.Add(p);
		}
		return drop.Tonnes;
	}

	double CargoPods::Haul(const cs::Ref<Wreck>& wreck, Good good, double tonnes)
	{
		if (wreck == nullptr || tonnes <= 0) return 0;
		double left = tonnes;
		for (const auto& p : Items)
		{
			if (left <= 0) break;
			if (p->WreckId != wreck->Id || p->What != good || p->Tonnes <= 0) continue;
			double take = cs::Math::Min(left, p->Tonnes);
			p->Tonnes -= take;
			left -= take;
		}
		Items.RemoveAll([](const cs::Ref<CargoPod>& p) { return p->Tonnes <= 1e-9; });
		for (int i = 0; i < wreck->Salvage.Count() && left > 0; i++)
		{
			if (wreck->Salvage[i].Good != good) continue;
			double take = cs::Math::Min(left, wreck->Salvage[i].Tonnes);
			wreck->Salvage[i] = SalvageStack(good, wreck->Salvage[i].Tonnes - take);
			left -= take;
		}
		return tonnes - left;
	}

	double CargoPods::SpilledFrom(const cs::Ref<Wreck>& wreck, Good good) const
	{
		double t = 0;
		if (wreck == nullptr) return t;
		for (const auto& p : Items)
			if (p->WreckId == wreck->Id && p->What == good) t += p->Tonnes;
		return t;
	}

	cs::Ref<CargoPod> CargoPods::Nearest(const cs::Ref<World>& world, DVec3 helioKm, double jd, double rangeKm, double& distanceKm,
		const cs::Ref<CargoPod>& skip) const
	{
		cs::Ref<CargoPod> best;
		distanceKm = cs::Double::PositiveInfinity;
		for (const auto& p : Items)
		{
			if (p == skip || p->Expired(jd)) continue;
			double d = (p->Helio(world, jd) - helioKm).Length();
			if (d > rangeKm || d >= distanceKm) continue;
			distanceKm = d;
			best = p;
		}
		return best;
	}

	void CargoPods::AdvanceTo(double jd)
	{
		Items.RemoveAll([jd](const cs::Ref<CargoPod>& p) { return p->Expired(jd); });
	}

	// ---------------------------------------------------------------- the beam

	TractorSpec TractorSpec::Of(ShipClass c)
	{
		switch (c)
		{
			// Strike craft: a small emitter in the nose, a few km and a gentle tug.
			case ShipClass::Fighter:
			case ShipClass::Interceptor:
			case ShipClass::Recon:
			case ShipClass::Bomber:
			case ShipClass::Gunship:
			case ShipClass::AssaultShuttle:
				return TractorSpec(3.0, 0.02, 0.15, 25);
			// Light hulls and the trade and work hulls that live by hauling.
			case ShipClass::Courier:
			case ShipClass::Explorer:
			case ShipClass::Corvette:
			case ShipClass::Frigate:
			case ShipClass::GatePicket:
			case ShipClass::LaneTender:
				return TractorSpec(5.0, 0.03, 0.25, 80);
			case ShipClass::Freighter:
			case ShipClass::Miner:
				return TractorSpec(8.0, 0.04, 0.3, 400);
			case ShipClass::Destroyer:
				return TractorSpec(7.0, 0.04, 0.3, 200);
			// Capital ships: big emitters, long reach.
			case ShipClass::Cruiser:
			case ShipClass::CommandCruiser:
			case ShipClass::Battleship:
			case ShipClass::GateBreaker:
			case ShipClass::Carrier:
				return TractorSpec(12.0, 0.05, 0.4, 600);
			// The tugs are built for it.
			case ShipClass::OreTug:
			case ShipClass::CaptureTug:
				return TractorSpec(15.0, 0.08, 0.6, 3000);
			default:
				return TractorSpec(5.0, 0.03, 0.25, 80);
		}
	}

	void TractorBeam::Pull(const cs::Ref<CargoPod>& pod, const TractorSpec& spec, DVec3 toShipKm, double dt, double& speedKmS)
	{
		if (pod == nullptr || dt <= 0) return;
		double d = toShipKm.Length();
		if (d <= 1e-9) return;
		// A heavy pod comes in slower: the same pull on more mass.
		double load = cs::Math::Clamp(spec.RatedTonnes / cs::Math::Max(1e-6, pod->Tonnes), 1.0 / 3.0, 1.0);
		double accel = spec.PullKmS2 * load;
		speedKmS = cs::Math::Min(spec.MaxSpeedKmS * load, speedKmS + accel * dt);
		// Eased in at the end so it settles alongside instead of slamming into the hull.
		speedKmS = cs::Math::Min(speedKmS, cs::Math::Sqrt(2 * accel * d) + 0.002);
		double step = cs::Math::Min(d, speedKmS * dt);
		pod->TowKm = pod->TowKm + toShipKm * (step / d);
	}

	TractorCatch TractorBeam::Catch(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, const cs::Ref<CargoPod>& pod, double jd)
	{
		TractorCatch r;
		if (ship == nullptr || pod == nullptr || pod->Tonnes <= 0) return r;
		SalvageStack stack(pod->What, pod->Tonnes);
		double before = stack.Tonnes;
		double crate = CargoFootprint::Of(pod->What).Tonnes;
		r.Crates = SalvageSession::LoadCrates(ship->PersonalHold(), stack, pod->Stolen, r.HoldFull);
		// Less than half a crate is not worth the space (as latch-and-salvage leaves it): the pod is empty.
		pod->Tonnes = stack.Tonnes < crate * 0.5 ? 0 : stack.Tonnes;
		r.Tonnes = cs::Math::Min(before, r.Crates * crate);
		if (r.Crates <= 0 || world == nullptr) return r;
		// On the record of the pilot flying the hull that took it, and nobody else's.
		auto chars = Characters::Of(world);
		auto pilot = chars->Find(ship);
		if (pilot == nullptr && ship == world->PilotedShip) pilot = chars->Embodied(world, ship, jd);
		if (pilot != nullptr)
		{
			pilot->PodsSalvaged++;
			pilot->TonnesSalvaged += r.Tonnes;
			chars->Act(pilot, LifeEventKind::Salvaged, jd, 0.25,
				{ "victim", pod->From, "good", cs::Strings::ToLowerInvariant(GoodInfo::Of(pod->What)->Name), "tonnes", cs::Fmt(r.Tonnes, "N0") });
		}
		return r;
	}
}

SG_SERIALIZE(SpaceGod::Core::CargoPod, "SpaceGod.Core.CargoPod",
	SG_F(Anchor), SG_F(Body), SG_F(DriftDir), SG_F(DriftKmS), SG_F(From), SG_F(Id), SG_F(LifetimeDays), SG_F(Local), SG_F(Owners),
	SG_F(Site), SG_F(SpilledJd), SG_F(SpreadKm), SG_F(Stolen), SG_F(Tonnes), SG_F(TowKm), SG_F(What), SG_F(WreckId))

SG_SERIALIZE(SpaceGod::Core::CargoPods, "SpaceGod.Core.CargoPods",
	SG_F(Items), SG_F(nextId))
