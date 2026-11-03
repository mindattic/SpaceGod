// Port of Unity.SpaceGod Assets/UnitySpace/Core/Events/EventDirector.cs (see docs/MIGRATION.md).

#include "Events/EventDirector.h"

#include "Combat/Battle.h"
#include "Combat/Boarding.h"
#include "Combat/CombatState.h"
#include "Combat/Crews.h"
#include "Combat/Salvage.h"
#include "Combat/Wreck.h"
#include "Economy/CargoHold.h"
#include "Economy/Goods.h"
#include "Economy/Propulsion.h"
#include "Events/EventSources.h"
#include "Hyperlanes.h"
#include "Law/Law.h"
#include "Law/Outlaws.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	EventDirector::EventDirector(int seed) : rng(seed) {}

	double EventDirector::ReputationWith(const cs::Ref<Faction>& f) const
	{
		double r = 0;
		return f != nullptr && Reputation->TryGetValue(f, r) ? r : 0;
	}

	void EventDirector::Update(double jd, const cs::Ref<EventSources>& src, DVec3 playerHelio, const cs::Ref<Ship>& playerShip)
	{
		for (int i = Active.Count() - 1; i >= 0; i--)
		{
			auto op = Active[i];
			Refresh(op, src);
			// Arriving counts only from another ship: taking the helm of the very ship it is about isn't
			// seeing it through (its spot is the player's own, so it would read as arrived at once).
			// Nor from a hull of the very raiders a bounty or ambush is on (one of their own escorts, taken
			// over in formation): nobody collects on their own band. That is the specific flagship-and-
			// escorts group the opportunity is about (Ship.Band), not the whole pirate clan -- a raider
			// from an unrelated band of the same clan still collects (U.SPC-T363). Band, not Leader/
			// Controller, survives EnterPiloting clearing an escort's Leader the moment it is taken.
			bool sameBand = op->Ship != nullptr && playerShip != nullptr && op->Ship->Band() == playerShip->Band()
				&& (op->Kind == OpportunityKind::Bounty || op->Kind == OpportunityKind::PirateAmbush);
			if (playerShip != nullptr && !playerShip->Destroyed && op->Ship != playerShip && !sameBand && (op->HelioKm - playerHelio).Length() <= ArriveKm)
			{
				op->Engaged = true;
				op->EngagedBy = playerShip;
				op->EngagedFaction = playerShip->Faction;
			}
			Settle(op, jd, src, playerShip, playerHelio);
		}
		if (jd < nextScanJd) return;
		nextScanJd = jd + ScanIntervalDays;
		Scan(jd, src, playerHelio, playerShip);
	}

	// ---------------------------------------------------------------- spawning

	void EventDirector::Scan(double jd, const cs::Ref<EventSources>& src, DVec3 player, const cs::Ref<Ship>& playerShip)
	{
		PruneRest(jd);
		cs::List<std::tuple<cs::Ref<Opportunity>, double>> found;
		bool bountyOpen = Active.Exists([](const cs::Ref<Opportunity>& o) { return o->Kind == OpportunityKind::Bounty; });
		bool anomalyOpen = Active.Exists([](const cs::Ref<Opportunity>& o) { return o->Kind == OpportunityKind::Anomaly; });

		for (const auto& s : src->Ships)
		{
			if (s == nullptr || s->Destroyed || s->GateDeparted || s == playerShip || s->IsEscort() || s->Faction == nullptr) continue;
			auto pos = src->ShipHelio(s);
			double d = (pos - player).Length();
			cs::Ref<Opportunity> op = nullptr;
			if (s->Faction->IsPirate)
			{
				cs::Ref<Lane> lurkLane;
				if (d <= SpawnRadiusKm && s->Ambushing() && !s->Derelict && (lurkLane = LaneOf(s)) != nullptr && src->LaneDanger(lurkLane) >= AmbushDanger)
					op = Ambush(s, lurkLane, jd, src);
				else if (!bountyOpen && d <= BountyRadiusKm && !s->Derelict && s->Spec->IsMilitary)
					op = BountyOn(s, jd, src);
			}
			else if (d <= SpawnRadiusKm)
			{
				cs::Ref<Lane> lane;
				if (s->Derelict) op = DerelictOf(s, jd);
				else if (s->Battle != nullptr && !s->Battle->Over && !s->Spec->IsMilitary) op = DistressUnderAttack(s, jd, src);
				else if (s->OutOfPropellant && s->Battle == nullptr) op = DistressDry(s, jd, src);
				else if (s->Phase == ShipPhase::Adrift && s->Battle == nullptr) op = DistressAdrift(s, jd, src);
				else if (s->Phase == ShipPhase::Lane && s->Battle == nullptr && src->IsMerchant(s) && (lane = s->LaneLeg.Lane) != nullptr
				         && src->LaneDanger(lane) >= EscortDanger && s->PhaseEndJd - jd >= MinEscortDays)
					op = Escort(s, lane, jd, src);
			}
			if (op == nullptr) continue;
			op->HelioKm = pos;
			found.Add(std::make_tuple(op, d));
		}

		for (const auto& w : src->Wrecks)
		{
			if (w == nullptr || w->Ship == nullptr || w->Expired(jd) || w->CreatedJd + w->LifetimeDays - jd < 0.25) continue;
			auto pos = src->WreckHelio(w);
			double d = (pos - player).Length();
			if (d > SpawnRadiusKm) continue;
			auto op = WreckOf(w, jd);
			op->HelioKm = pos;
			found.Add(std::make_tuple(op, d));
		}

		for (const auto& pod : src->Pods)
		{
			if (pod == nullptr || !pod->Drifting() || pod->AirLeftDays(jd) < 0.1) continue;
			auto pos = src->PodHelio(pod);
			double d = (pos - player).Length();
			if (d > SpawnRadiusKm) continue;
			auto op = PodsOf(pod, jd, src);
			op->HelioKm = pos;
			found.Add(std::make_tuple(op, d));
		}

		if (!anomalyOpen && AnomalyRatePerDay > 0)
		{
			double dt = cs::Double::IsNaN(lastAnomalyRollJd) ? ScanIntervalDays : cs::Math::Min(1.0, cs::Math::Max(0.0, jd - lastAnomalyRollJd));
			if (rng.NextDouble() < 1 - cs::Math::Exp(-AnomalyRatePerDay * dt))
			{
				auto op = AnomalyNear(player, jd, src);
				found.Add(std::make_tuple(op, (op->HelioKm - player).Length()));
			}
		}
		lastAnomalyRollJd = jd;

		// Lives at stake first, then fights, then salvage; nearest first within each.
		found.Sort([](const std::tuple<cs::Ref<Opportunity>, double>& a, const std::tuple<cs::Ref<Opportunity>, double>& b)
		{
			int c = cs::CompareTo(Priority(std::get<0>(a)->Kind), Priority(std::get<0>(b)->Kind));
			return c != 0 ? c : cs::CompareTo(std::get<1>(a), std::get<1>(b));
		});
		for (const auto& entry : found)
		{
			const auto& op = std::get<0>(entry);
			if (Active.Count() >= MaxActive) break;
			if (IsOpen(op->SourceKey) || IsResting(op->SourceKey, jd)) continue;
			// A faction that wants the player, or no longer trusts them, offers them no work.
			if (op->Patron != nullptr && !src->WillHire(op->Patron)) continue;
			if (op->Kind == OpportunityKind::Bounty && Active.Exists([](const cs::Ref<Opportunity>& o) { return o->Kind == OpportunityKind::Bounty; })) continue;
			op->Id = nextId++;
			op->SpawnJd = jd;
			op->State = OpportunityState::Active;
			Active.Add(op);
			Spawned.Invoke(op);
		}
	}

	int EventDirector::Priority(OpportunityKind k)
	{
		switch (k)
		{
			case OpportunityKind::Distress: return 0;
			case OpportunityKind::EscapePods: return 0;
			case OpportunityKind::ConvoyEscort: return 1;
			case OpportunityKind::PirateAmbush: return 2;
			case OpportunityKind::Bounty: return 3;
			case OpportunityKind::Derelict: return 4;
			case OpportunityKind::Wreck: return 5;
			default: return 6;
		}
	}

	bool EventDirector::IsOpen(const cs::String& key) const
	{
		return Active.Exists([&](const cs::Ref<Opportunity>& o) { return o->SourceKey == key; });
	}

	bool EventDirector::IsResting(const cs::String& key, double jd) const
	{
		double until = 0;
		return restUntil.TryGetValue(key, until) && jd < until;
	}

	void EventDirector::PruneRest(double jd)
	{
		cs::Ref<cs::List<cs::String>> done = nullptr;
		for (auto kv : restUntil)
			if (jd >= kv.second) { if (!done) done = cs::New<cs::List<cs::String>>(); done->Add(kv.first); }
		if (done != nullptr) for (const auto& k : *done) restUntil.Remove(k);
	}

	double EventDirector::RestDays(OpportunityKind k)
	{
		switch (k)
		{
			case OpportunityKind::Distress: return 1;
			case OpportunityKind::ConvoyEscort: return 2;
			case OpportunityKind::PirateAmbush: return 4;
			case OpportunityKind::Bounty: return 30;
			default: return 1000; // a derelict or a wreck is only worth one look
		}
	}

	cs::Ref<Lane> EventDirector::LaneOf(const cs::Ref<Ship>& s)
	{
		return s->LaneLeg.Lane ? s->LaneLeg.Lane : s->LurkLane;
	}

	cs::String EventDirector::Key(const cs::String& kind, const cs::Ref<Ship>& s) { return cs::Cat(kind, ":ship:", s->Id); }

	double EventDirector::CargoValue(const cs::Ref<Ship>& s)
	{
		return s->HasCargo && s->CargoTonnes > 0 ? s->CargoTonnes * GoodInfo::Of(s->CargoGood)->BasePrice : 0;
	}

	double EventDirector::RoundThousand(double v) { return cs::Math::Round(v / 1000) * 1000; }

	cs::String EventDirector::Cr(double v) { return cs::Fmt(v, "N0"); }

	cs::Ref<Opportunity> EventDirector::DistressUnderAttack(const cs::Ref<Ship>& s, double jd, const cs::Ref<EventSources>& src)
	{
		cs::String where = src->BodyName(s->Battle->Body);
		auto op = cs::New<Opportunity>();
		op->Kind = OpportunityKind::Distress;
		op->SourceKey = Key("distress", s);
		op->Title = cs::Cat("Distress: ", s->Name);
		op->Hook = cs::Cat("Mayday, mayday: ", s->Name, " under attack ", (s->Battle->Site != nullptr ? cs::String("beside the lane") : cs::Cat("off ", where)), ", shields failing. Any ship, please respond!");
		op->ExpiresJd = jd + 0.25;
		op->Ship = s;
		op->Battle = s->Battle;
		op->UnderAttack = true;
		op->Patron = s->Faction;
		OpportunityReward reward;
		reward.Credits = RoundThousand(100000 + 0.02 * CargoValue(s));
		reward.ReputationWith = s->Faction;
		reward.Reputation = 6;
		op->Reward = reward;
		return op;
	}

	cs::Ref<Opportunity> EventDirector::DistressAdrift(const cs::Ref<Ship>& s, double jd, const cs::Ref<EventSources>& /*src*/)
	{
		cs::String lane = s->LaneLeg.Lane != nullptr ? cs::Cat(" off the ", s->LaneLeg.Lane->Name, " lane") : cs::String("");
		auto op = cs::New<Opportunity>();
		op->Kind = OpportunityKind::Distress;
		op->SourceKey = Key("distress", s);
		op->Title = cs::Cat("Adrift: ", s->Name);
		op->Hook = cs::Cat(s->Name, " here, pulled out of the lane", lane, " by an interdiction pulse. Drives are dead and we're drifting. Requesting assistance.");
		op->ExpiresJd = jd + 1;
		op->Ship = s;
		op->Patron = s->Faction;
		OpportunityReward reward;
		reward.Credits = 60000;
		reward.ReputationWith = s->Faction;
		reward.Reputation = 3;
		op->Reward = reward;
		return op;
	}

	cs::Ref<Opportunity> EventDirector::DistressDry(const cs::Ref<Ship>& s, double jd, const cs::Ref<EventSources>& src)
	{
		cs::String nearBody = s->Orbit != nullptr ? cs::Cat(" off ", src->BodyName(s->Orbit->Body)) : cs::String("");
		auto op = cs::New<Opportunity>();
		op->Kind = OpportunityKind::Distress;
		op->SourceKey = Key("distress", s);
		op->Title = cs::Cat("Dry tanks: ", s->Name);
		op->Hook = cs::Cat(s->Name, " here, drifting", nearBody, " with our tanks run dry. The tanker is still a way out. Can anyone spare reaction mass?");
		op->ExpiresJd = cs::Math::Max(jd + 0.1, s->PhaseEndJd);
		op->Ship = s;
		op->Patron = s->Faction;
		op->DryTanks = true;
		OpportunityReward reward;
		reward.Credits = 50000;
		reward.ReputationWith = s->Faction;
		reward.Reputation = 3;
		op->Reward = reward;
		return op;
	}

	cs::Ref<Opportunity> EventDirector::DerelictOf(const cs::Ref<Ship>& s, double jd)
	{
		// What it pays for is reporting and working the hulk (the salvage registry's award); what is in
		// its hold the player takes for themselves once latched on (see SalvageSession).
		OpportunityReward reward;
		reward.Credits = s->Spec->IsMilitary ? 180000 : 60000;
		cs::String aboard = s->Combat != nullptr && s->Combat->Interior->Survivors() + s->Combat->Interior->Marines() > 0.5 ? cs::String(" Sensors read movement aboard.") : cs::String("");
		auto op = cs::New<Opportunity>();
		op->Kind = OpportunityKind::Derelict;
		op->SourceKey = Key("derelict", s);
		op->Title = cs::Cat("Derelict: ", s->Name);
		op->Hook = cs::Cat("No transponder answer from ", s->Name, ". Hull dark, airlocks open: boarded and left to drift. ", HoldReads(s->Salvage), aboard,
		                   " Match velocity and latch on before it breaks up.");
		op->ExpiresJd = cs::Math::Min(jd + 5, s->DerelictSinceJd + 25);
		op->Ship = s;
		op->Reward = reward;
		return op;
	}

	cs::Ref<Opportunity> EventDirector::WreckOf(const cs::Ref<Wreck>& w, double jd)
	{
		auto op = cs::New<Opportunity>();
		op->Kind = OpportunityKind::Wreck;
		op->SourceKey = cs::Cat("wreck:", w->Id);
		op->Title = cs::Cat("Wreck: ", w->Name);
		op->Hook = cs::Cat("The wreck of ", w->Name, " is still tumbling where it died. ", HoldReads(w->Salvage), " Latch on and strip it.");
		op->ExpiresJd = cs::Math::Min(jd + 6, w->CreatedJd + w->LifetimeDays);
		op->Wreck = w;
		OpportunityReward reward;
		reward.Credits = 25000;
		op->Reward = reward;
		return op;
	}

	cs::Ref<Opportunity> EventDirector::PodsOf(const cs::Ref<EscapePod>& pod, double jd, const cs::Ref<EventSources>& src)
	{
		cs::String where = pod->Site != nullptr ? cs::Cat("beside the ", pod->Site->Hop.Lane->Name, " lane") : cs::Cat("off ", src->BodyName(pod->Body));
		cs::String pods = pod->Pods == 1 ? cs::String("one escape pod") : cs::Cat(pod->Pods, " escape pods");
		cs::String captainAboard = pod->Captain != nullptr
			? cs::Cat(" ", pod->Captain->Name(), " is aboard", (pod->Captain->Record->Wanted() ? cs::Cat(", ", Cr(pod->Captain->Record->TotalBounty()), " cr on their head, paid in full alive: tractor them into your brig.") : cs::String(".")))
			: cs::String("");
		cs::String hook = pod->Owners->IsPirate
			? cs::Cat(pods, " from the raider ", pod->Name, " drifting ", where, ", ", Cr(pod->People), " aboard. The pirate hunters pay for raiders handed over.")
			: cs::Cat("Beacon: ", pods, " from the ", pod->Name, " drifting ", where, " with ", Cr(pod->People), " ", pod->Owners->Name, " crew aboard and ",
			          cs::Fmt(pod->AirLeftDays(jd), "0"), " days of air. Any ship, please pick us up.");
		OpportunityReward reward;
		if (pod->Owners->IsPirate)
		{
			reward.Credits = RoundThousand(pod->People * Crews::PrisonerBountyPerHead);
			reward.ReputationWith = src->PirateHunters;
			reward.Reputation = 3;
		}
		else
		{
			reward.Credits = RoundThousand(pod->People * PodRescueCredits);
			reward.ReputationWith = pod->Owners;
			reward.Reputation = PodRescueReputation;
		}
		auto op = cs::New<Opportunity>();
		op->Kind = OpportunityKind::EscapePods;
		op->SourceKey = cs::Cat("pods:", pod->Id);
		op->Title = cs::Cat("Escape pods: ", pod->Name);
		op->Hook = cs::Cat(hook, captainAboard);
		op->ExpiresJd = pod->LaunchedJd + pod->LifeSupportDays;
		op->Pod = pod;
		op->Patron = pod->Owners->IsPirate ? src->PirateHunters : pod->Owners;
		op->Reward = reward;
		return op;
	}

	cs::String EventDirector::HoldReads(const cs::List<SalvageStack>& salvage)
	{
		for (const auto& stack : salvage)
			if (stack.Tonnes > 0) return cs::Cat("Its hold still reads ", cs::Strings::ToLowerInvariant(GoodInfo::Of(stack.Good)->Name), ".");
		return "Scrap, at least.";
	}

	cs::Ref<Opportunity> EventDirector::Ambush(const cs::Ref<Ship>& s, const cs::Ref<Lane>& lane, double jd, const cs::Ref<EventSources>& src)
	{
		double until = s->PhaseEndJd > jd ? cs::Math::Min(jd + 2, s->PhaseEndJd) : jd + 2;
		auto op = cs::New<Opportunity>();
		op->Kind = OpportunityKind::PirateAmbush;
		op->SourceKey = cs::Cat("ambush:lane:", lane->Id);
		op->Title = cs::Cat("Ambush: ", lane->Name, " lane");
		op->Hook = cs::Cat("Traffic warning: ", s->Faction->Name, " raiders are lying in wait off the ", lane->Name, " lane. Break them before they spring it.");
		op->ExpiresJd = until;
		op->Ship = s;
		op->Lane = lane;
		op->Patron = src->PirateHunters;
		OpportunityReward reward;
		reward.Credits = 300000;
		reward.ReputationWith = src->PirateHunters;
		reward.Reputation = 8;
		op->Reward = reward;
		return op;
	}

	cs::Ref<Opportunity> EventDirector::Escort(const cs::Ref<Ship>& s, const cs::Ref<Lane>& lane, double /*jd*/, const cs::Ref<EventSources>& /*src*/)
	{
		auto op = cs::New<Opportunity>();
		op->Kind = OpportunityKind::ConvoyEscort;
		op->SourceKey = Key("escort", s);
		op->Title = cs::Cat("Escort: ", s->Name);
		op->Hook = cs::Cat(s->Name, " is crossing the ", lane->Name, " lane", (s->HasCargo ? cs::Cat(" with ", cs::Strings::ToLowerInvariant(GoodInfo::Of(s->CargoGood)->Name)) : cs::String("")),
		                   " and there have been ambushes. Paying for an escort to the far ramp.");
		op->ExpiresJd = s->PhaseEndJd + 0.05;
		op->Ship = s;
		op->Lane = lane;
		op->Patron = s->Faction;
		OpportunityReward reward;
		reward.Credits = RoundThousand(80000 + 0.03 * CargoValue(s));
		reward.ReputationWith = s->Faction;
		reward.Reputation = 5;
		op->Reward = reward;
		return op;
	}

	cs::Ref<Opportunity> EventDirector::BountyOn(const cs::Ref<Ship>& s, double jd, const cs::Ref<EventSources>& src)
	{
		cs::String captain = PirateCaptains::NameFor(s);
		cs::String payer = src->PirateHunters != nullptr ? src->PirateHunters->Name : cs::String("Every port in the system");
		// The hunters' standing price on a raider captain. Whatever the law has posted for what they have actually
		// done is paid by the law on top (U.SPC-T63): in full for the captain alive, half on a scanned wreck.
		double credits = 650000;
		double posted = RoundThousand(src->PostedBounty(s));
		auto op = cs::New<Opportunity>();
		op->Kind = OpportunityKind::Bounty;
		op->SourceKey = Key("bounty", s);
		op->Title = cs::Cat("Bounty: ", captain);
		op->Hook = cs::Cat(payer, " posts ", Cr(credits), " cr for ", captain, ", commanding the raider ", s->Name, ". Dead or taken.",
			(posted > 0 ? cs::Cat(" The law's ", Cr(posted), " cr on their head pays in full alive (board the disabled flagship), half dead on a scanned wreck.") : cs::String("")));
		op->ExpiresJd = jd + 12;
		op->Ship = s;
		op->Captain = captain;
		op->Patron = src->PirateHunters;
		OpportunityReward reward;
		reward.Credits = credits;
		reward.ReputationWith = src->PirateHunters;
		reward.Reputation = 10;
		op->Reward = reward;
		return op;
	}

	const cs::List<std::tuple<cs::String, cs::String, double, cs::Nullable<Good>>>& EventDirector::Anomalies()
	{
		static const cs::List<std::tuple<cs::String, cs::String, double, cs::Nullable<Good>>> value
		{
			{ "Ghost transponder", "A transponder code nobody has registered in forty years, pinging from empty space near {0}.", 120000, std::nullopt },
			{ "Venting ice fragment", "A comet fragment is venting near {0}; the spectra say volatiles worth scooping.", 60000, Good::HeliumThree },
			{ "Lensing ripple", "Something is bending starlight near {0}. Survey offices pay well for readings like this.", 220000, std::nullopt },
			{ "Silent pod", "A sealed cargo pod is drifting near {0}, no markings, no owner on record.", 40000, Good::Platinum },
			{ "Old probe", "A pre-lane survey probe is still tumbling near {0}, its data core intact.", 150000, Good::Electronics },
		};
		return value;
	}

	cs::Ref<Opportunity> EventDirector::AnomalyNear(DVec3 player, double jd, const cs::Ref<EventSources>& src)
	{
		const auto& a = Anomalies()[rng.Next(Anomalies().Count())];
		int body = src->NearestBody(player);
		// Somewhere a short trip from the player: a random direction, a tenth to half the spawn radius away.
		double z = 2 * rng.NextDouble() - 1, t = 2 * cs::Math::PI * rng.NextDouble(), r = cs::Math::Sqrt(1 - z * z);
		auto dir = DVec3(r * cs::Math::Cos(t), r * cs::Math::Sin(t), z);
		auto spot = player + dir * (SpawnRadiusKm * (0.1 + 0.4 * rng.NextDouble()));
		auto bodyHelio = src->BodyHelio(body);
		OpportunityReward reward;
		reward.Credits = std::get<2>(a);
		if (std::get<3>(a).has_value()) { reward.CargoGood = *std::get<3>(a); reward.CargoCrates = 1; }
		auto op = cs::New<Opportunity>();
		op->Kind = OpportunityKind::Anomaly;
		op->SourceKey = cs::Cat("anomaly:", nextId, ":", cs::Fmt(jd, "0.000"));
		op->Title = std::get<0>(a);
		op->Hook = cs::Format(std::get<1>(a), src->BodyName(body));
		op->ExpiresJd = jd + 3;
		op->Body = body;
		op->OffsetKm = spot - bodyHelio;
		op->HelioKm = spot;
		op->Reward = reward;
		return op;
	}

	// ---------------------------------------------------------------- following and settling

	void EventDirector::Refresh(const cs::Ref<Opportunity>& op, const cs::Ref<EventSources>& src)
	{
		if (op->Ship != nullptr && !op->Ship->Destroyed) op->HelioKm = src->ShipHelio(op->Ship);
		else if (op->Wreck != nullptr) op->HelioKm = src->WreckHelio(op->Wreck);
		else if (op->Pod != nullptr) op->HelioKm = src->PodHelio(op->Pod);
		else if (op->Body >= 0) op->HelioKm = src->BodyHelio(op->Body) + op->OffsetKm;
	}

	void EventDirector::Settle(const cs::Ref<Opportunity>& op, double jd, const cs::Ref<EventSources>& src, const cs::Ref<Ship>& player, DVec3 playerHelio)
	{
		auto s = op->Ship;
		switch (op->Kind)
		{
			case OpportunityKind::Distress:
				if (s->Destroyed) { Fail(op, jd, cs::Cat(s->Name, " was destroyed.")); return; }
				if (s->Derelict || s->Faction != op->Patron) { Fail(op, jd, cs::Cat(s->Name, " was taken.")); return; }
				if (op->UnderAttack)
				{
					if (s->Battle != op->Battle || op->Battle->Over)
					{
						if (op->Engaged) Complete(op, jd, player, cs::Cat(s->Name, " made it out with your help."));
						else Fail(op, jd, cs::Cat(s->Name, " fought clear without you."));
						return;
					}
				}
				else if (op->DryTanks)
				{
					if (op->Engaged && player != nullptr && !player->Destroyed && s->OutOfPropellant)
					{
						double moved = Propulsion::Transfer(player, s, s->PropellantCapacity() * DryTanksTransfer);
						if (moved > 0)
						{
							// Enough to get going on: it does not wait for the tanker.
							if (s->PropellantFraction() >= 0.25) s->PhaseEndJd = cs::Math::Min(s->PhaseEndJd, jd);
							Complete(op, jd, player, cs::Cat("You pumped ", Cr(moved), " t of reaction mass across to ", s->Name, "."));
							return;
						}
					}
					if (!s->OutOfPropellant) { Fail(op, jd, cs::Cat("A tanker reached ", s->Name, " first.")); return; }
				}
				else
				{
					if (op->Engaged) { Complete(op, jd, player, cs::Cat("You got ", s->Name, " underway again.")); return; }
					if (s->Phase != ShipPhase::Adrift) { Fail(op, jd, cs::Cat(s->Name, " got its drives back and went on without you.")); return; }
				}
				break;

			// Coming within sight of a hulk is not seeing it through: that takes latching on and working
			// it (HulkWorked), so these only fail here.
			case OpportunityKind::Derelict:
				if (s->Destroyed || !s->Derelict) { Fail(op, jd, cs::Cat(s->Name, " broke up before anyone reached it.")); return; }
				break;

			case OpportunityKind::Wreck:
				if (!Contains(src->Wrecks, op->Wreck) || op->Wreck->Expired(jd)) { Fail(op, jd, cs::Cat("The wreck of ", op->Wreck->Name, " broke up.")); return; }
				break;

			case OpportunityKind::PirateAmbush:
				if (s->Destroyed || s->Derelict || !s->Faction->IsPirate)
				{
					if (op->Engaged) Complete(op, jd, player, cs::Cat("The raiders off the ", op->Lane->Name, " lane are broken."));
					else Fail(op, jd, cs::Cat("Someone else dealt with the raiders off the ", op->Lane->Name, " lane."));
					return;
				}
				if (!op->Engaged && !s->Ambushing()) { Fail(op, jd, cs::Cat("The raiders off the ", op->Lane->Name, " lane moved on.")); return; }
				break;

			case OpportunityKind::ConvoyEscort:
				if (s->Destroyed) { Fail(op, jd, cs::Cat(s->Name, " was destroyed on the lane.")); return; }
				if (s->Derelict || s->Faction != op->Patron) { Fail(op, jd, cs::Cat(s->Name, " was taken on the lane.")); return; }
				if (s->Phase != ShipPhase::Lane && s->Phase != ShipPhase::Adrift && s->Battle == nullptr)
				{
					if (op->Engaged) Complete(op, jd, player, cs::Cat("You saw ", s->Name, " safely across the ", op->Lane->Name, " lane."));
					else Fail(op, jd, cs::Cat(s->Name, " made the crossing without you."));
					return;
				}
				break;

			case OpportunityKind::Bounty:
				if (s->Destroyed || s->Derelict || !s->Faction->IsPirate)
				{
					if (op->Engaged) Complete(op, jd, player, cs::Cat(op->Captain, " is finished. The bounty is yours."));
					else Fail(op, jd, cs::Cat(op->Captain, " fell to someone else."));
					return;
				}
				break;

			case OpportunityKind::EscapePods:
			{
				auto pod = op->Pod;
				if (!pod->Drifting())
				{
					cs::String by = pod->TakenBy != nullptr ? cs::Cat("the ", pod->TakenBy->Name) : cs::String("someone");
					cs::String how;
					switch (pod->State)
					{
						case PodState::Lost: how = cs::Cat("The pods from the ", pod->Name, " fell silent before anyone reached them."); break;
						case PodState::Hostage: how = cs::Cat("Raiders hauled in the pods from the ", pod->Name, " first."); break;
						case PodState::Prisoner: how = cs::Cat(Cap(by), " took the crew of the ", pod->Name, " prisoner first."); break;
						default: how = cs::Cat(Cap(by), " picked up the pods from the ", pod->Name, " first."); break;
					}
					Fail(op, jd, how);
					return;
				}
				if (op->Engaged && player != nullptr && !player->Destroyed && (op->HelioKm - playerHelio).Length() <= PodPickupKm)
				{
					double people = pod->People;
					auto fate = src->PickUpPod(pod, player);
					cs::String aboard = cs::Cat(Cr(people), " ", (pod->Owners->IsPirate ? cs::String("raiders") : cs::Cat(pod->Owners->Name, " crew")));
					if (fate == PodState::Rescued && !pod->Owners->IsPirate)
						Complete(op, jd, player, cs::Cat("You took ", aboard, " aboard from the ", pod->Name, "'s pods."));
					else if (fate == PodState::Prisoner && pod->Owners->IsPirate)
						Complete(op, jd, player, cs::Cat("You hauled in ", aboard, " from the ", pod->Name, "'s pods for the pirate hunters."));
					else
					{
						// Raiders picking up their own, or crews taken as hostages or prisoners: nobody pays a
						// rescuer for that (a ransom, if any, comes later).
						op->Reward = OpportunityReward();
						cs::String what = fate == PodState::Rescued ? cs::Cat("You took ", aboard, " aboard")
							: fate == PodState::Hostage ? cs::Cat("You hold ", aboard, " for ransom")
							: cs::Cat("You took ", aboard, " prisoner");
						Complete(op, jd, player, cs::Cat(what, " from the ", pod->Name, "'s pods."));
					}
					return;
				}
				break;
			}

			case OpportunityKind::Anomaly:
				if (op->Engaged) { Complete(op, jd, player, cs::Cat("You logged the ", cs::Strings::ToLowerInvariant(op->Title), ".")); return; }
				break;
		}
		if (jd >= op->ExpiresJd) Fail(op, jd, cs::Cat("Expired: ", op->Title, "."));
	}

	cs::String EventDirector::Cap(const cs::String& s)
	{
		// char.ToUpperInvariant(s[0]) + s.Substring(1) (faction names are ASCII).
		return cs::Strings::IsNullOrEmpty(s) ? s : cs::Cat(cs::Strings::ToUpperInvariant(cs::Strings::Substring(s, 0, 1)), cs::Strings::Substring(s, 1));
	}

	bool EventDirector::Contains(const cs::List<cs::Ref<Wreck>>& list, const cs::Ref<Wreck>& w)
	{
		for (int i = 0; i < list.Count(); i++) if (list[i] == w) return true;
		return false;
	}

	cs::Ref<Opportunity> EventDirector::HulkWorked(const cs::Ref<SalvageSession>& session, double jd)
	{
		if (session == nullptr || !session->Worked()) return nullptr;
		auto op = Active.Find([&](const cs::Ref<Opportunity>& o)
		{
			return session->Derelict != nullptr
				? o->Kind == OpportunityKind::Derelict && o->Ship == session->Derelict
				: o->Kind == OpportunityKind::Wreck && o->Wreck == session->Wreck;
		});
		if (op == nullptr) return nullptr;
		auto player = session->Player;
		op->Engaged = true;
		op->EngagedBy = player;
		op->EngagedFaction = player->Faction;
		// Whoever the hull belonged to when the clamps took hold (a claimed one has changed hands since).
		auto owners = session->Owners;
		if (session->SurvivorsRescued >= 0.5 && owners != nullptr && !owners->IsPirate && owners != player->Faction)
		{
			double thanks = RoundThousand(session->SurvivorsRescued * SurvivorRescueCredits);
			op->Reward.Credits += thanks;
			// Only their owners' thanks come out of the owners' treasury; the registry's award is new money.
			op->Reward.PatronShare = thanks;
			op->Reward.ReputationWith = owners;
			op->Reward.Reputation += SurvivorRescueReputation;
		}
		Complete(op, jd, player, cs::Cat("You ", session->Summary(), " aboard ", session->Name(), "."));
		return op;
	}

	void EventDirector::Complete(const cs::Ref<Opportunity>& op, double jd, const cs::Ref<Ship>& player, const cs::String& how)
	{
		Pay(op, player);
		End(op, OpportunityState::Completed, jd, cs::Cat(how, " Reward: ", op->Reward.ToString(), "."));
	}

	void EventDirector::Fail(const cs::Ref<Opportunity>& op, double jd, const cs::String& how) { End(op, OpportunityState::Failed, jd, how); }

	void EventDirector::End(const cs::Ref<Opportunity>& op, OpportunityState state, double jd, const cs::String& how)
	{
		op->State = state;
		op->ResolvedJd = jd;
		op->Outcome = how;
		Active.Remove(op);
		History.Add(op);
		if (History.Count() > HistoryLength) History.RemoveRange(0, History.Count() - HistoryLength);
		restUntil[op->SourceKey] = jd + RestDays(op->Kind);
		Resolved.Invoke(op);
	}

	void EventDirector::Pay(const cs::Ref<Opportunity>& op, cs::Ref<Ship> player)
	{
		OpportunityReward r = op->Reward; // a struct: a copy, as C#'s `var r = op.Reward`
		// Paid to whoever saw it through, as they were when they came: not whoever holds the helm
		// when it resolves (another ship, another faction), nor the captor of a ship since lost.
		auto purse = op->EngagedFaction ? op->EngagedFaction : (player ? player->Faction : nullptr);
		// A patron pays from its own treasury, capped at what it holds (as Crews' pod rescue does); the
		// reward is rewritten to what actually moved. Patronless rewards stay new money (U.SPC-T391).
		auto patron = r.ReputationWith;
		if (patron != nullptr && patron != purse && r.Credits > 0)
		{
			double owed = cs::Math::Min(r.Credits, r.PatronShare.value_or(r.Credits));
			double paid = cs::Math::Max(0.0, cs::Math::Min(owed, patron->Treasury));
			patron->Treasury -= paid;
			r.Credits = op->Reward.Credits = r.Credits - owed + paid;
		}
		if (purse != nullptr) purse->Treasury += r.Credits;
		// Cargo into the hold of the ship that came, while it still exists and is still theirs; else
		// into the ship they fly now, if it is their own.
		cs::Ref<Ship> hold = op->EngagedBy != nullptr && !op->EngagedBy->Destroyed && op->EngagedBy->Faction == purse ? op->EngagedBy
			: player != nullptr && !player->Destroyed && player->Faction == purse ? player : nullptr;
		if (hold == nullptr) op->Reward.CargoCrates = 0;
		else if (r.CargoCrates > 0)
		{
			player = hold;
			int stored = 0;
			for (int i = 0; i < r.CargoCrates; i++)
				if (player->PersonalHold()->AddFirstFit(r.CargoGood) != nullptr) stored++;
			op->Reward.CargoCrates = stored;
		}
		if (r.ReputationWith != nullptr && r.Reputation != 0)
			(*Reputation)[r.ReputationWith] = ReputationWith(r.ReputationWith) + r.Reputation;
	}
}
