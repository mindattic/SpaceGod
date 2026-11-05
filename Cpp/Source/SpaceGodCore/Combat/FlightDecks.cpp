#include "Combat/FlightDecks.h"
#include "Combat/Battle.h"
#include "Combat/BoardingShuttles.h"
#include "Combat/CarrierWings.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Fighters.h"
#include "Combat/StrandedWings.h"
#include "Settings/GameSetting.h"
#include "RenderLod.h"
#include "Astro.h"
#include <unordered_map>

namespace SpaceGod::Core
{
	using cs::Math::Max;
	using cs::Math::Min;

	const char* ToString(DeckStage v)
	{
		switch (v)
		{
			case DeckStage::Inbound: return "Inbound";
			case DeckStage::Final: return "Final";
			case DeckStage::OnDeck: return "OnDeck";
			case DeckStage::Launching: return "Launching";
		}
		return "";
	}

	// ---------------------------------------------------------------- FlightDeck

	FlightDeck::FlightDeck(cs::Ref<BattleUnit> carrier, cs::Ref<Subsystem> bay, cs::String name)
	{
		Carrier = std::move(carrier);
		Bay = std::move(bay);
		Name = std::move(name);
		auto d = Bay->Def;
		MiddleLocal = DVec3(d->X, d->Y, d->Z);
		double length = Max(Carrier->LengthKm, 1e-6);
		double half = Min(FlightDecks::HalfLengthShipLengths, FlightDecks::MaxHalfLengthKm / length);
		SternAlong = (Max(-0.5, d->Z - half) - d->Z) * length;
		BowAlong = (Min(0.5, d->Z + half) - d->Z) * length;
		if (BowAlong - SternAlong < 0.05) BowAlong = SternAlong + 0.05;
	}

	bool FlightDeck::Open() const { return !Bay->Destroyed() && StrandedWings::CanLandOn(Carrier); }

	DVec3 FlightDeck::Middle() const { return Carrier->PointLocal(MiddleLocal); }

	DVec3 FlightDeck::Stern() const { return At(SternAlong, DVec3::Zero); }
	DVec3 FlightDeck::Bow() const { return At(BowAlong, DVec3::Zero); }

	DVec3 FlightDeck::Gate() const { return At(SternAlong - FlightDecks::ApproachKm, DVec3::Zero); }

	DVec3 FlightDeck::At(double along, DVec3 offset) const
	{
		DVec3 right, up;
		Carrier->Basis(right, up);
		return Middle() + Carrier->Forward * along + right * offset.X + up * offset.Y;
	}

	void FlightDeck::Frame(DVec3 pos, double& along, DVec3& offset) const
	{
		DVec3 right, up;
		Carrier->Basis(right, up);
		auto d = pos - Middle();
		along = DVec3::Dot(d, Carrier->Forward);
		offset = DVec3(DVec3::Dot(d, right), DVec3::Dot(d, up), 0);
	}

	double FlightDeck::SpotAlong(int i) const { return (i - (FlightDecks::Spots - 1) * 0.5) * FlightDecks::SpotSpacingKm; }

	bool FlightDeck::Busy(const cs::Ref<BattleUnit>& craft)
	{
		// A craft cleared and since shot down, recalled or waved off frees the glide path.
		if (Final != nullptr && Final != craft)
		{
			auto visit = Final->Fighter ? Final->Fighter->Deck : nullptr;
			if (Final->Dead || (visit ? visit->Deck.get() : nullptr) != this || Final->Fighter->Deck->Stage != DeckStage::Final) Final = nullptr;
		}
		return (Final != nullptr && Final != craft) || Aboard.Count() >= FlightDecks::Spots;
	}

	// ---------------------------------------------------------------- FlightDecks: settings

	const cs::Ref<RangeSetting>& FlightDecks::ServiceSeconds()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("dogfight.deckService", SettingGroups::Dogfighting,
			"Flight deck turnaround", 30, 5, 180, 5,
			cs::Cat("How long (seconds) a craft down on a carrier's flight deck takes to be refuelled, rearmed and repaired before it is ",
				"launched again out of the bow."), cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v, "0"), " s"); }),
			"flight deck carrier land landing rearm refuel repair turnaround hangar");
		return setting;
	}

	const cs::Ref<ToggleSetting>& FlightDecks::AiRearms()
	{
		static const cs::Ref<ToggleSetting> setting = cs::New<ToggleSetting>("dogfight.deckRearm", SettingGroups::Dogfighting,
			"Pilots land to rearm", true,
			cs::Cat("Carrier pilots who are badly hurt or out of torpedoes and missiles fly home mid-fight, land on the flight deck, ",
				"rearm and repair, and launch again. Off, they land only when recalled."),
			"flight deck carrier land rearm repair pilot ai");
		return setting;
	}

	const cs::Ref<RangeSetting>& FlightDecks::ServiceHull()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("dogfight.deckHull", SettingGroups::Dogfighting,
			"Pilots land for repairs below", 0.4, 0.1, 0.9, 0.05,
			"How much of its hull a carrier craft has left before its pilot flies home to land and be repaired.",
			cs::Func<cs::String, double>([](double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }), "flight deck carrier land repair hull damage pilot");
		return setting;
	}

	const cs::Ref<LodClass>& FlightDecks::Lod()
	{
		static const cs::Ref<LodClass> lod = RenderLod::Register(cs::New<LodClass>("deck", "Flight deck", cs::Func<double>([] { return 24.0; }), LodFar::None, false));
		return lod;
	}

	// ---------------------------------------------------------------- FlightDecks: the decks of a hull

	namespace
	{
		// C#'s ConditionalWeakTable<BattleUnit, FlightDeck[]>: a hull's decks are built once and live as long as the hull.
		// Each deck holds its carrier, so an entry is dropped once nothing but its own decks holds the hull any more (the
		// ephemeron a ConditionalWeakTable gives); the purge runs when a new entry goes in.
		struct DeckTableEntry
		{
			std::weak_ptr<BattleUnit> Key;
			cs::Ref<cs::List<cs::Ref<FlightDeck>>> Decks;
		};

		std::unordered_map<const BattleUnit*, DeckTableEntry>& DeckTable()
		{
			static std::unordered_map<const BattleUnit*, DeckTableEntry> table;
			return table;
		}

		void PurgeDeckTable()
		{
			auto& table = DeckTable();
			for (auto it = table.begin(); it != table.end();)
			{
				long held = it->second.Key.use_count();
				if (held == 0 || held <= static_cast<long>(it->second.Decks->Count())) it = table.erase(it);
				else ++it;
			}
		}
	}

	const cs::List<cs::Ref<FlightDeck>>& FlightDecks::None()
	{
		static const cs::List<cs::Ref<FlightDeck>> none;
		return none;
	}

	const cs::List<cs::Ref<FlightDeck>>& FlightDecks::Of(const cs::Ref<BattleUnit>& carrier)
	{
		if (carrier == nullptr || carrier->Kind != UnitKind::Ship || carrier->Combat == nullptr) return None();
		auto& table = DeckTable();
		auto it = table.find(carrier.get());
		if (it != table.end() && it->second.Key.lock() == carrier) return *it->second.Decks;
		auto built = Build(carrier);
		PurgeDeckTable();
		auto& entry = table[carrier.get()];
		entry.Key = carrier;
		entry.Decks = built;
		return *entry.Decks;
	}

	void FlightDecks::Adopt(const cs::Ref<FlightDeck>& deck)
	{
		if (deck == nullptr || deck->Bay == nullptr) return;
		const auto& carrier = deck->Carrier;
		// Of(carrier) finds the entry, or makes it in Build's order (one deck per hangar, in subsystem order).
		if (&Of(carrier) == &None()) return;
		auto& decks = *DeckTable()[carrier.get()].Decks;
		for (int i = 0; i < decks.Count(); i++)
		{
			if (decks[i]->Bay != deck->Bay) continue;
			decks[i] = deck;
			return;
		}
	}

	cs::Ref<cs::List<cs::Ref<FlightDeck>>> FlightDecks::Build(const cs::Ref<BattleUnit>& carrier)
	{
		auto list = cs::New<cs::List<cs::Ref<FlightDeck>>>();
		for (const auto& s : carrier->Combat->Subsystems)
		{
			if (s->Kind() != SubsystemKind::Hangar) continue;
			double x = s->Def->X;
			cs::String side = x > 0.02 ? "starboard" : x < -0.02 ? "port" : "centreline";
			list->Add(cs::New<FlightDeck>(carrier, s, cs::Cat(side, " deck")));
		}
		return list;
	}

	bool FlightDecks::HasOpenDeck(const cs::Ref<BattleUnit>& carrier)
	{
		for (const auto& d : Of(carrier))
			if (d->Open()) return true;
		return false;
	}

	cs::Ref<DeckVisit> FlightDecks::VisitOf(const cs::Ref<BattleUnit>& u)
	{
		auto f = u ? u->Fighter : nullptr;
		return f ? f->Deck : nullptr;
	}

	bool FlightDecks::OnDeck(const cs::Ref<BattleUnit>& u)
	{
		auto v = VisitOf(u);
		return v != nullptr && v->Stage == DeckStage::OnDeck;
	}

	bool FlightDecks::TurningRound(const cs::Ref<BattleUnit>& u)
	{
		auto v = VisitOf(u);
		return v != nullptr && v->Stage == DeckStage::OnDeck && v->Deck->Open() && !v->Deck->Carrier->Fleeing && !BoardingShuttles::Is(u);
	}

	bool FlightDecks::Away(const cs::Ref<BattleUnit>& u)
	{
		return (u ? u->Fighter : nullptr) != nullptr && ((u->Fighter->Deck != nullptr && u->Fighter->Deck->Stage != DeckStage::Launching) || u->Fighter->Phase == FighterPhase::Return);
	}

	// ---------------------------------------------------------------- the pilot's decision

	bool FlightDecks::WantsService(const cs::Ref<Battle>& /*b*/, const cs::Ref<BattleUnit>& u)
	{
		auto f = u->Fighter;
		if (f == nullptr || !AiRearms()->Value || BoardingShuttles::Is(u)) return false;
		auto home = f->LandsOn();
		if (home == nullptr || !home->Present() || home->Captured || home->Fleeing || home->Surrendering || !HasOpenDeck(home)) return false;
		auto c = u->Combat;
		if (c->HullFraction() < ServiceHull()->Value) return true;
		return OutOfOrdnance(c);
	}

	bool FlightDecks::OutOfOrdnance(const cs::Ref<ShipCombat>& c)
	{
		bool carries = false;
		for (const auto& s : c->Subsystems)
		{
			auto w = s->Def->Weapon;
			if (w == nullptr || !w->Missile || w->Ammo <= 0 || s->Kind() == SubsystemKind::Countermeasures) continue;
			carries = true;
			if (!s->Destroyed() && s->Ammo > 0) return false;
		}
		return carries;
	}

	// ---------------------------------------------------------------- inbound

	cs::Ref<FlightDeck> FlightDecks::Assign(const cs::Ref<BattleUnit>& u)
	{
		auto f = u->Fighter;
		auto home = f ? f->LandsOn() : nullptr;
		if (home == nullptr) return nullptr;
		auto v = f->Deck;
		if (v != nullptr && v->Deck->Carrier == home && v->Deck->Open()) return v->Deck;
		cs::Ref<FlightDeck> best = nullptr;
		double bestScore = cs::Double::MaxValue;
		for (const auto& d : Of(home))
		{
			if (!d->Open()) continue;
			double score = d->Aboard.Count() * 2 + (d->Final != nullptr ? 1 : 0) + (d->Gate() - u->Pos).Length() / 5;
			if (score < bestScore)
			{
				bestScore = score;
				best = d;
			}
		}
		if (best == nullptr)
		{
			Abort(u);
			return nullptr;
		}
		if (v != nullptr && v->Deck->Final == u) v->Deck->Final = nullptr;
		auto visit = cs::New<DeckVisit>();
		visit->Deck = best;
		visit->Stage = DeckStage::Inbound;
		f->Deck = visit;
		return best;
	}

	DVec3 FlightDecks::InboundGoal(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<FlightDeck>& deck)
	{
		auto c = deck->Carrier;
		DVec3 right, up;
		c->Basis(right, up);
		// Coming in from ahead of the gate it swings round the hull's flank to come up astern, never through the hull.
		auto local = c->ToLocal(u->Pos) * c->LengthKm;
		double gateZ = deck->MiddleLocal.Z * c->LengthKm + deck->SternAlong - ApproachKm;
		if (local.Z > gateZ + GateCaptureKm)
		{
			double beam = c->LengthKm * BeamShipLengths + RoundHullKm;
			double side = local.X != 0 ? cs::Math::Sign(local.X) : deck->MiddleLocal.X >= 0 ? 1 : -1;
			return c->Pos + right * (side * beam) + up * (local.Y * 0.5) + c->Forward * gateZ;
		}
		if (!deck->Busy(u)) return deck->Gate();
		double a = b->Seconds * 0.12 + (u->Fighter ? u->Fighter->Seed : 0);
		return deck->Gate() - c->Forward * HoldAsternKm + (right * cs::Math::Cos(a) + up * cs::Math::Sin(a)) * HoldRadiusKm;
	}

	bool FlightDecks::TryFinal(const cs::Ref<Battle>& /*b*/, const cs::Ref<BattleUnit>& u, const cs::Ref<FlightDeck>& deck, double dt)
	{
		auto v = VisitOf(u);
		if (v == nullptr || v->Deck != deck || v->Stage != DeckStage::Inbound || deck->Busy(u)) return false;
		double rel = (u->Vel - deck->Carrier->Vel).Length();
		if ((deck->Gate() - u->Pos).Length() > Max(GateCaptureKm, rel * dt * 1.5)) return false;
		ClearToLand(u, deck);
		return true;
	}

	void FlightDecks::ClearToLand(const cs::Ref<BattleUnit>& u, const cs::Ref<FlightDeck>& deck)
	{
		auto v = VisitOf(u);
		v->Stage = DeckStage::Final;
		deck->Frame(u->Pos, v->Along, v->Offset);
		v->SpotAlong = deck->SpotAlong(deck->Aboard.Count());
		// Never cleared from inside the bay: it comes in through the stern mouth.
		if (v->Along > deck->SternAlong) v->Along = deck->SternAlong;
		deck->Final = u;
	}

	void FlightDecks::Abort(const cs::Ref<BattleUnit>& u)
	{
		auto v = VisitOf(u);
		if (v == nullptr) return;
		if (v->Deck->Final == u) v->Deck->Final = nullptr;
		v->Deck->Aboard.Remove(u);
		u->Fighter->Deck = nullptr;
	}

	// ---------------------------------------------------------------- the deck flies it

	bool FlightDecks::Fly(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, double dt)
	{
		auto v = VisitOf(u);
		if (v == nullptr || (v->Stage != DeckStage::Final && v->Stage != DeckStage::Launching)) return false;
		auto deck = v->Deck;
		auto c = deck->Carrier;
		if (!deck->Open())
		{
			Abort(u);
			return false;
		}
		auto spec = u->Combat->Spec;
		if (v->Stage == DeckStage::Final)
		{
			double k = 1 - cs::Math::Exp(-dt / LineUpSeconds);
			v->Offset = v->Offset - v->Offset * k;
			double close = Min(FinalSpeedKmS, Max(MinCloseKmS, (v->SpotAlong - v->Along) * 0.2));
			v->Along = Min(v->SpotAlong, v->Along + close * dt);
			u->Pos = deck->At(v->Along, v->Offset);
			u->Vel = c->Vel + c->Forward * (v->Along < v->SpotAlong ? close : 0);
			Battle::TurnToward(u, c->Forward, Max(spec->TurnRateDegS, 30.0), dt);
			if (v->Along >= v->SpotAlong - 1e-6) Touchdown(b, u, v);
			return true;
		}
		// Launching: thrown down the deck at the craft's own thrust, nose along the hull's.
		double top = Max(spec->MaxSpeed, MinCloseKmS);
		v->Speed = Min(top, v->Speed + Max(spec->Accel, 0.02) * dt);
		v->Along += v->Speed * dt;
		v->Offset = v->Offset - v->Offset * (1 - cs::Math::Exp(-dt / LineUpSeconds));
		u->Pos = deck->At(v->Along, v->Offset);
		u->Vel = c->Vel + c->Forward * v->Speed;
		u->Forward = c->Forward;
		if (v->Along >= deck->BowAlong + ExitKm) ClearOfBow(b, u, v);
		return true;
	}

	void FlightDecks::Touchdown(const cs::Ref<Battle>& /*b*/, const cs::Ref<BattleUnit>& u, const cs::Ref<DeckVisit>& v)
	{
		auto deck = v->Deck;
		if (deck->Final == u) deck->Final = nullptr;
		v->Stage = DeckStage::OnDeck;
		v->Service = 0;
		v->Offset = DVec3::Zero;
		if (!deck->Aboard.Contains(u)) deck->Aboard.Add(u);
		u->Docked = true;
		u->Vel = deck->Carrier->Vel;
		u->Forward = deck->Carrier->Forward;
		FighterAI::TakeAboard(deck->Carrier, u);
	}

	void FlightDecks::ClearOfBow(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<DeckVisit>& v)
	{
		auto f = u->Fighter;
		f->Deck = nullptr;
		f->Phase = v->After;
		f->PhaseTime = 0;
		f->RunClosest = cs::Double::MaxValue;
		f->RunEntry = DVec3::Zero;
		// A relaunched pilot rejoins its squadron's vee behind whoever leads it now (it leads if nobody does); a squadron
		// launching together at the start of a fight keeps the leads it was launched with.
		if (f->Squadron >= 0 && v->Relaunch && !b->Flown.ContainsKey(u))
		{
			cs::Ref<BattleUnit> lead = nullptr;
			for (const auto& o : b->Units)
			{
				if (o == u || !o->Present() || o->Kind != UnitKind::Fighter || o->Fighter == nullptr || o->Fighter->Carrier != f->Carrier
					|| o->Fighter->Squadron != f->Squadron || o->Fighter->Lead != nullptr || Away(o)) continue;
				if (lead == nullptr || o->Fighter->Slot < lead->Fighter->Slot) lead = o;
			}
			f->Lead = lead;
		}
		cs::Ref<FighterControl> stick;
		if (b->Flown.TryGetValue(u, stick))
		{
			stick->Aim = u->Forward;
			stick->Up = DVec3::Zero;
			stick->AngularVelocity = DVec3::Zero;
			stick->Throttle = Max(stick->Throttle, 0.6);
		}
	}

	// ---------------------------------------------------------------- on deck

	void FlightDecks::Step(const cs::Ref<Battle>& b, double dt)
	{
		for (int i = 0; i < b->Units.Count(); i++)
		{
			auto u = b->Units[i];
			if (u->Kind != UnitKind::Fighter || u->Dead) continue;
			auto v = VisitOf(u);
			if (v == nullptr || v->Stage != DeckStage::OnDeck) continue;
			auto deck = v->Deck;
			auto c = deck->Carrier;
			if (c->Dead) continue;
			int spot = cs::Math::Max(0, deck->Aboard.IndexOf(u));
			u->Pos = deck->At(deck->SpotAlong(spot), DVec3::Zero);
			u->Vel = c->Vel;
			u->Forward = c->Forward;
			if (!deck->Open() || !c->Present()) continue;
			v->Service = Min(1.0, v->Service + dt / Max(1.0, ServiceSeconds()->Value));
			Service(u, v->Service);
			bool player = b->Flown.ContainsKey(u);
			// Home for good (the shooting over, its hull running, a shuttle back with its team, or its berth lost with a bay):
			// struck down off the deck into the hangar, so the spot is free for the next one in.
			if (BoardingShuttles::Is(u) || c->Fleeing || c->Surrendering || (b->Mopping && !player) || !Berthed(c, u))
			{
				v->Stowing += dt;
				if (v->Stowing >= StowSeconds)
				{
					deck->Aboard.Remove(u);
					u->Fighter->Deck = nullptr;
				}
				continue;
			}
			v->Stowing = 0;
			if (v->Service < 1 || b->Seconds < deck->NextLaunch) continue;
			if (!TakeOff(c, u)) continue;
			deck->NextLaunch = b->Seconds + LaunchIntervalSeconds;
			deck->Aboard.Remove(u);
			u->Docked = false;
			v->Stage = DeckStage::Launching;
			v->Along = deck->SpotAlong(spot);
			v->Speed = 0;
			v->After = FighterPhase::Launch;
			v->Relaunch = true;
		}
	}

	void FlightDecks::Service(const cs::Ref<BattleUnit>& u, double share)
	{
		auto c = u->Combat;
		c->Hull = Max(c->Hull, c->Spec->Hull * share);
		c->BoostFraction = Max(c->BoostFraction, share);
		if (share < 1) return;
		c->Hull = c->Spec->Hull;
		c->BoostFraction = 1;
		for (const auto& s : c->Subsystems)
		{
			s->Hp = s->Def->MaxHp;
			s->Restoring = 0;
			s->Cooldown = 0;
			if (s->Def->Weapon != nullptr) s->Ammo = s->Def->Weapon->Ammo;
			if (s->Kind() == SubsystemKind::ShieldGenerator) s->Shield = s->MaxShield;
		}
		u->Heat = 0;
		u->Overheated = false;
		u->Stunned = 0;
	}

	bool FlightDecks::Berthed(const cs::Ref<BattleUnit>& home, const cs::Ref<BattleUnit>& craft)
	{
		return craft->CraftHull != nullptr ? home->Combat->Hangar->Contains(craft->CraftHull) : home->Combat->Fighters() > 0;
	}

	bool FlightDecks::TakeOff(const cs::Ref<BattleUnit>& home, const cs::Ref<BattleUnit>& craft)
	{
		if ((home ? home->Combat : nullptr) == nullptr || BoardingShuttles::Is(craft)) return false;
		if (craft->CraftHull != nullptr) return CarrierWings::Undock(home->Combat, craft->CraftHull);
		if (home->Combat->Fighters() <= 0) return false;
		// C#: home.Combat.Fighters-- (a bodied property with a setter).
		home->Combat->SetFighters(home->Combat->Fighters() - 1);
		return true;
	}

	bool FlightDecks::Catapult(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& carrier, const cs::Ref<BattleUnit>& craft)
	{
		if ((craft ? craft->Fighter : nullptr) == nullptr) return false;
		cs::Ref<FlightDeck> best = nullptr;
		int bestCount = cs::Int::MaxValue;
		cs::Dictionary<cs::Ref<FlightDeck>, int> counts;
		for (const auto& o : b->Units)
		{
			auto ov = VisitOf(o);
			if (o == craft || ov == nullptr || ov->Stage != DeckStage::Launching || ov->Deck->Carrier != carrier) continue;
			int n = 0;
			counts[ov->Deck] = (counts.TryGetValue(ov->Deck, n) ? n : 0) + 1;
		}
		for (const auto& d : Of(carrier))
		{
			if (!d->Open()) continue;
			int m = 0;
			int n = counts.TryGetValue(d, m) ? m : 0;
			if (n < bestCount)
			{
				bestCount = n;
				best = d;
			}
		}
		if (best == nullptr) return false;
		// In column down the deck from its middle, one spacing apart (a long one backs out of the stern mouth).
		auto v = cs::New<DeckVisit>();
		v->Deck = best;
		v->Stage = DeckStage::Launching;
		v->After = craft->Fighter->Phase;
		v->Along = -bestCount * LaunchSpacingKm;
		craft->Fighter->Deck = v;
		craft->Pos = best->At(v->Along, DVec3::Zero);
		craft->Vel = carrier->Vel;
		craft->Forward = carrier->Forward;
		return true;
	}

	// ---------------------------------------------------------------- the player

	bool FlightDecks::FlyManned(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<FighterControl>& stick, double dt)
	{
		auto v = VisitOf(u);
		if (v != nullptr && v->Stage == DeckStage::Final && stick->Boost)
		{
			Abort(u);
			return false;
		}
		if (v != nullptr && v->Stage == DeckStage::Inbound) Abort(u);
		if (Fly(b, u, dt)) return true;
		auto deck = Capturing(b, u);
		if (deck == nullptr) return false;
		auto f = u->Fighter;
		if (deck->Carrier != f->Carrier) f->Home = deck->Carrier;
		auto visit = cs::New<DeckVisit>();
		visit->Deck = deck;
		visit->Stage = DeckStage::Inbound;
		f->Deck = visit;
		ClearToLand(u, deck);
		return Fly(b, u, dt);
	}

	cs::Ref<FlightDeck> FlightDecks::Capturing(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		for (const auto& o : b->Units)
		{
			if (o->Side != u->Side || o->Kind != UnitKind::Ship || o->Fleeing || o->Surrendering) continue;
			for (const auto& d : Of(o))
			{
				if (!d->Open() || d->Busy(u)) continue;
				if (o != u->Fighter->Carrier && o != u->Fighter->Home && StrandedWings::Room(b, o) <= 0) continue;
				double along = 0;
				DVec3 off;
				d->Frame(u->Pos, along, off);
				if (along < d->SternAlong - ApproachKm - GateCaptureKm || along > d->SternAlong) continue;
				if (off.Length() > CaptureOffAxisKm) continue;
				if (DVec3::Dot(u->Forward, o->Forward) < cs::Math::Cos(CaptureAlignDeg * Astro::Deg2Rad)) continue;
				if ((u->Vel - o->Vel).Length() > CaptureSpeedKmS) continue;
				return d;
			}
		}
		return nullptr;
	}

	cs::Ref<FlightDeck> FlightDecks::GuideDeck(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		auto v = VisitOf(u);
		if (v != nullptr) return v->Deck;
		cs::Ref<FlightDeck> best = nullptr;
		double bestD = GuideKm;
		for (const auto& o : b->Units)
		{
			if (o->Side != u->Side || o->Kind != UnitKind::Ship || !o->Present() || o->Captured) continue;
			for (const auto& d : Of(o))
			{
				if (!d->Open()) continue;
				double dist = (d->Gate() - u->Pos).Length() - (o == (u->Fighter ? u->Fighter->LandsOn() : nullptr) ? 2 : 0);
				if (dist < bestD)
				{
					bestD = dist;
					best = d;
				}
			}
		}
		return best;
	}

	cs::String FlightDecks::Guidance(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		auto deck = GuideDeck(b, u);
		if (deck == nullptr) return cs::String();
		auto v = VisitOf(u);
		cs::String where = cs::Cat(deck->Carrier->Name(), " ", deck->Name);
		if (v != nullptr)
		{
			switch (v->Stage)
			{
				case DeckStage::Final: return cs::Cat("AUTO-LAND ", where, " ", Metres(v->SpotAlong - v->Along), " to the spot (boost aborts)");
				case DeckStage::OnDeck:
					return deck->Open() && !deck->Carrier->Fleeing
						? cs::Cat("ON DECK ", where, ": refuel, rearm, repair ", Pct(v->Service), (v->Service >= 1 ? ", launching" : ""))
						: cs::Cat("ON DECK ", where);
				case DeckStage::Launching: return cs::Cat("LAUNCHING out of the ", where);
				default: break;
			}
		}
		double along = 0;
		DVec3 off;
		deck->Frame(u->Pos, along, off);
		double range = (deck->Gate() - u->Pos).Length();
		double rel = (u->Vel - deck->Carrier->Vel).Length();
		double nose = cs::Math::Acos(Max(-1.0, Min(1.0, DVec3::Dot(u->Forward, deck->Carrier->Forward)))) * Astro::Rad2Deg;
		cs::StringBuilder sb;
		sb.Append("DECK ").Append(where).Append(' ');
		sb.Append(cs::Fmt(range, "0.0")).Append(" km");
		if (along > deck->SternAlong) sb.Append(": come round astern");
		else
		{
			sb.Append(": ").Append(Metres(cs::Math::Abs(off.X))).Append(off.X > 0 ? " right" : " left");
			sb.Append(' ').Append(Metres(cs::Math::Abs(off.Y))).Append(off.Y > 0 ? " high" : " low");
			sb.Append(", nose ").Append(cs::Fmt(nose, "0")).Append("\xC2\xB0");
		}
		sb.Append(", closing ").Append(cs::Fmt(rel, "0.00")).Append(" km/s");
		if (rel > CaptureSpeedKmS) sb.Append(" (slow to ").Append(cs::Fmt(CaptureSpeedKmS, "0.0")).Append(')');
		if (deck->Busy(u)) sb.Append(", deck busy: hold");
		return sb.ToString();
	}

	cs::String FlightDecks::Metres(double km) { return cs::Cat(cs::Fmt(km * 1000, "0"), " m"); }
	cs::String FlightDecks::Pct(double f) { return cs::Cat(cs::Fmt(cs::Math::Round(100 * Max(0.0, Min(1.0, f))), "0"), "%"); }
}
