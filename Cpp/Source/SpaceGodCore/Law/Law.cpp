// Port of Unity.SpaceGod Core/Law/Law.cs (US-T20).

#include "Law/Law.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Battle.h"
#include "Combat/Diplomacy.h"
#include "Combat/Morale.h"
#include "Combat/War.h"
#include "Economy/CargoHold.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "Events/EventSources.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Law/Outlaws.h"
#include "Persistence/ObjectGraph.h"
#include "ScaleModel.h"
#include "Sensors/Sensors.h"
#include "Ships.h"
#include "Systems/JumpGate.h"
#include "World.h"

namespace SpaceGod::Core
{
	const char* ToString(CrimeKind v)
	{
		switch (v)
		{
			case CrimeKind::Assault: return "Assault";
			case CrimeKind::Piracy: return "Piracy";
			case CrimeKind::Smuggling: return "Smuggling";
			case CrimeKind::Trespass: return "Trespass";
			case CrimeKind::FleeingScan: return "FleeingScan";
		}
		return "";
	}

	// ================================================================ Crime

	cs::String Crime::ToString() const { return cs::Cat(Law::Describe(Kind), !Detail.empty() ? cs::Cat(" (", Detail, ")") : cs::String("")); }

	// ================================================================ LawRecord

	LawRecord::LawRecord(cs::String name, cs::Ref<Ship> captain, cs::Ref<cs::Dictionary<cs::Ref<Faction>, double>> standing)
	{
		Name = std::move(name);
		Captain = std::move(captain);
		Standing = standing ? standing : cs::New<cs::Dictionary<cs::Ref<Faction>, double>>();
	}

	double LawRecord::StandingWith(const cs::Ref<Faction>& f) const
	{
		double s = 0;
		return f != nullptr && Standing->TryGetValue(f, s) ? s : 0;
	}

	double LawRecord::BountyBy(const cs::Ref<Faction>& f) const
	{
		double b = 0;
		return f != nullptr && Bounty.TryGetValue(f, b) ? b : 0;
	}

	double LawRecord::TotalBounty() const
	{
		double t = 0;
		for (auto [f, v] : Bounty) t += v;
		return t;
	}

	// ================================================================ PatrolHail

	double PatrolHail::SecondsLeft(double jd) const { return cs::Math::Max(0, (DueJd - jd) * Astro::SecondsPerDay); }

	// ================================================================ Law

	Law::Law(cs::Ref<World> world, cs::Ref<cs::Dictionary<cs::Ref<Faction>, double>> playerStanding)
	{
		this->world = std::move(world);
		Player = cs::New<LawRecord>("you", nullptr, playerStanding);
	}

	std::tuple<double, double, double> Law::Severity(CrimeKind k)
	{
		switch (k)
		{
			case CrimeKind::Assault: return std::make_tuple(10.0, 15.0, 50000.0);
			case CrimeKind::Piracy: return std::make_tuple(20.0, 25.0, 150000.0);
			case CrimeKind::Smuggling: return std::make_tuple(5.0, 8.0, 20000.0);
			case CrimeKind::Trespass: return std::make_tuple(3.0, 5.0, 10000.0);
			default: return std::make_tuple(6.0, 10.0, 30000.0);
		}
	}

	cs::String Law::Describe(CrimeKind k)
	{
		switch (k)
		{
			case CrimeKind::Assault: return "assault on a lawful ship";
			case CrimeKind::Piracy: return "piracy";
			case CrimeKind::Smuggling: return "smuggling contraband";
			case CrimeKind::Trespass: return "trespass";
			default: return "fleeing a scan";
		}
	}

	// ---------------------------------------------------------------- records and standing

	cs::Ref<LawRecord> Law::RecordOf(const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr) return nullptr;
		if (ship == (world ? world->PilotedShip : nullptr)) return Player;
		auto captain = ship->Controller();
		cs::Ref<LawRecord> r;
		if (!Captains.TryGetValue(captain, r))
		{
			r = cs::New<LawRecord>(CaptainName(captain), captain);
			Captains[captain] = r;
		}
		return r;
	}

	cs::Ref<LawRecord> Law::FindRecord(const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr) return nullptr;
		if (ship == (world ? world->PilotedShip : nullptr)) return Player;
		cs::Ref<LawRecord> r;
		return Captains.TryGetValue(ship->Controller(), r) ? r : nullptr;
	}

	cs::String Law::CaptainName(const cs::Ref<Ship>& s) { return s->Faction != nullptr && s->Faction->IsPirate ? PirateCaptains::NameFor(s) : cs::Cat("the master of the ", s->Name); }

	double Law::BountyOn(const cs::Ref<Ship>& ship)
	{
		auto r = FindRecord(ship);
		return r ? r->TotalBounty() : 0;
	}

	bool Law::CanDock(const cs::Ref<LawRecord>& r, const cs::Ref<Settlement>& s)
	{
		if (r == nullptr || (s ? s->Owner : nullptr) == nullptr || s->Owner->IsPirate) return true;
		return r->StandingWith(s->Owner) >= DockMinStanding && r->BountyBy(s->Owner) < DockRefusedBounty;
	}

	bool Law::AllowsDocking(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s) { return CanDock(FindRecord(ship), s); }

	double Law::PriceFactor(const cs::Ref<LawRecord>& r, const cs::Ref<Faction>& owner)
	{
		if (r == nullptr || owner == nullptr || owner->IsPirate) return 1;
		double s = r->StandingWith(owner);
		return s >= 0 ? 1 - 0.1 * cs::Math::Min(1, s / 50) : 1 + 0.3 * cs::Math::Min(1, -s / 50);
	}

	double Law::PriceFactorFor(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& s) { return PriceFactor(FindRecord(ship), s ? s->Owner : nullptr); }

	bool Law::OffersContracts(const cs::Ref<LawRecord>& r, const cs::Ref<Faction>& patron)
	{
		if (r == nullptr || patron == nullptr) return true;
		return !r->WantedBy(patron) && r->StandingWith(patron) >= ContractMinStanding;
	}

	void Law::Adjust(const cs::Ref<LawRecord>& r, const cs::Ref<Faction>& f, double delta)
	{
		if (f == nullptr || f->IsPirate || delta == 0) return;
		double s = r->StandingWith(f) + delta;
		(*r->Standing)[f] = s;
	}

	// ---------------------------------------------------------------- jurisdiction

	cs::Ref<Faction> Law::JurisdictionAt(int body)
	{
		auto port = PortAuthority(body);
		if (port != nullptr) return port;
		auto lanes = world->Fleet->Lanes;
		return lanes != nullptr && body >= 0 ? world->LaneKeeper(lanes->SystemOf(body)) : nullptr;
	}

	cs::Ref<Faction> Law::PortAuthority(int body)
	{
		cs::Ref<Settlement> best;
		for (auto& s : world->Economy->At(body))
			if (!s->Fallen && s->Owner != nullptr && !s->Owner->IsPirate && (best == nullptr || s->Population > best->Population)) best = s;
		return best ? best->Owner : nullptr;
	}

	cs::Ref<Faction> Law::JurisdictionOf(const LaneHop& hop)
	{
		auto from = world->LaneKeeper(hop.From);
		return from ? from : world->LaneKeeper(hop.To);
	}

	// ---------------------------------------------------------------- witnesses

	cs::String Law::FindWitness(DVec3 helioKm, double jd, cs::Func<bool, cs::Ref<Ship>> exclude, double signature)
	{
		auto sensors = world->Sensors ? world->Sensors : cs::New<Sensors>(world);
		return sensors->FindWitness(helioKm, jd, signature, exclude);
	}

	// ---------------------------------------------------------------- crimes

	cs::Ref<Crime> Law::Commit(const cs::Ref<LawRecord>& offender, CrimeKind kind, cs::Ref<Faction> jurisdiction, const cs::Ref<Faction>& victim, int body, double jd, const cs::String& witness, const cs::String& detail)
	{
		if (jurisdiction != nullptr && jurisdiction->IsPirate) jurisdiction = nullptr;
		auto crime = cs::New<Crime>();
		crime->Kind = kind;
		crime->Offender = offender;
		crime->Jurisdiction = jurisdiction;
		crime->Victim = victim;
		crime->Body = body;
		crime->Jd = jd;
		crime->Witness = witness;
		crime->Detail = detail;
		crime->PostedBy = jurisdiction ? jurisdiction : (victim != nullptr && !victim->IsPirate ? victim : nullptr);
		if (!crime->Recorded())
		{
			offender->Unwitnessed++;
			Committed.Invoke(crime);
			return crime;
		}
		auto [notoriety, standing, bounty] = Severity(kind);
		auto poster = crime->PostedBy;
		offender->Notoriety += notoriety;
		Adjust(offender, poster, -standing);
		if (victim != nullptr && victim != poster) Adjust(offender, victim, -standing * VictimStandingShare);
		double posted = offender->BountyBy(poster) + bounty;
		offender->Bounty[poster] = posted;
		offender->BountyUntil[poster] = jd + BountyDays;
		crime->Bounty = bounty;
		offender->Crimes.Add(crime);
		if (offender->Crimes.Count() > 40) offender->Crimes.RemoveAt(0);
		Recent.Add(crime);
		if (Recent.Count() > 100) Recent.RemoveAt(0);
		cs::String where = body >= 0 && body < world->Catalog()->Count() ? cs::Cat(" near ", (*world->Catalog())[body]->Name) : cs::String("");
		world->Log(jd, cs::Cat(poster->Name, " posts ", Cr(bounty), " cr on ", offender->Name, " for ", Describe(kind), where, ", seen by ", witness), WorldEventKind::Law, body);
		if (offender->IsPlayer())
			Notice(cs::Cat("CRIME: ", Describe(kind), ", seen by ", witness, ". ", poster->Name, " posts a bounty of ", Cr(bounty), " cr (now ", Cr(offender->BountyBy(poster)), ")"));
		Committed.Invoke(crime);
		return crime;
	}

	bool Law::PayFine(const cs::Ref<Ship>& ship, const cs::Ref<Faction>& f, double jd, double& paid)
	{
		paid = Player->BountyBy(f);
		if ((ship ? ship->Faction : nullptr) == nullptr || paid <= 0 || ship->Faction->Treasury < paid)
		{
			paid = 0;
			return false;
		}
		ship->Faction->Treasury -= paid;
		if (f != ship->Faction) f->Treasury += paid; // a fine to one's own faction is burned as court costs (U.SPC-T389)
		ClearBounty(Player, f);
		if (Player->StandingWith(f) < DockMinStanding) (*Player->Standing)[f] = DockMinStanding;
		if (Hail != nullptr && Hail->By == f) Hail = nullptr;
		scannedUntil[f] = jd + ScanCooldownDays;
		world->Log(jd, cs::Cat("The ", ship->Name, " pays ", Cr(paid), " cr in fines to ", f->Name), WorldEventKind::Law, ship->Orbit ? ship->Orbit->Body : -1);
		return true;
	}

	void Law::ClearBounty(const cs::Ref<LawRecord>& r, const cs::Ref<Faction>& f)
	{
		r->Bounty.Remove(f);
		r->BountyUntil.Remove(f);
	}

	void Law::Update(double jd)
	{
		if (cs::Double::IsNaN(lastJd))
		{
			lastJd = jd;
			return;
		}
		double dt = jd - lastJd;
		if (dt <= 0) return;
		lastJd = jd;
		double fade = cs::Math::Pow(0.5, dt / NotorietyHalfLifeDays);
		Fade(Player, fade, jd);
		for (const auto& r : Captains.Values()) Fade(r, fade, jd);
		PruneCaptains();
	}

	void Law::PruneCaptains()
	{
		cs::List<cs::Ref<Ship>> stale;
		for (auto [ship, r] : Captains)
		{
			if (r->Bounty.Count() > 0 || r->Notoriety > 0.01) continue;
			if (ship != nullptr && !ship->Destroyed && !ship->GateDeparted) continue;
			stale.Add(ship);
		}
		for (const auto& s : stale) Captains.Remove(s);
	}

	void Law::Fade(const cs::Ref<LawRecord>& r, double fade, double jd)
	{
		r->Notoriety *= fade;
		if (r->BountyUntil.Count() == 0) return;
		cs::List<cs::Ref<Faction>> lapsed;
		for (auto [f, until] : r->BountyUntil)
			if (jd >= until) lapsed.Add(f);
		for (const auto& f : lapsed) ClearBounty(r, f);
	}

	// ---------------------------------------------------------------- battles

	void Law::OnBattleEnded(const cs::Ref<Battle>& b, double jd)
	{
		if (b == nullptr || b->Units.Count() == 0) return;
		auto where = BattleHelio(b, jd);
		auto juris = b->Site != nullptr ? JurisdictionOf(b->Site->Hop) : JurisdictionAt(b->Body);
		// News of it names no body for a lane fight, nor for one over a haven nobody has found (it would give the rock away).
		int shownBody = b->Site != nullptr || HidesHaven(b->Body) ? -1 : b->Body;
		cs::HashSet<cs::Ref<Ship>> inBattle;
		for (const auto& u : b->Units)
			if (u->Ship != nullptr) inBattle.Add(u->Ship);

		for (int side = 0; side < 2; side++)
		{
			if (side == b->EnforcingSide) continue;
			// Each commander once: the player (keyed by null) for a hull they flew, else the group's captain.
			cs::Dictionary<OffenderKey, cs::Ref<Faction>> offenders;
			for (const auto& u : b->Units)
			{
				if (u->Side != side || u->Kind != UnitKind::Ship || u->Ship == nullptr) continue;
				// A prize taken by this side is not one of its fighters: its own captain (taken with it) did nothing here.
				if (u->Captured && u->FoughtOn() != side) continue;
				OffenderKey key = u->PlayerFlown ? OffenderKey{ Player, nullptr } : OffenderKey{ nullptr, u->Ship->Controller() };
				if (!offenders.ContainsKey(key)) offenders[key] = u->Faction;
			}
			for (auto [key, byRef] : offenders)
			{
				auto by = byRef;
				auto victims = LawfulVictims(b, 1 - side, by, juris);
				if (victims.Count() == 0) continue;
				bool harmed = false, robbed = false;
				for (const auto& v : victims)
				{
					// A captured hull now sits on its captor's side: taken by this side, it was robbed; taken by
					// a third party fighting alongside it (pirates who took a merchant the offenders were
					// defending), it is not this side's doing.
					bool takenHere = v->Captured && v->Side == side;
					harmed |= v->Dead || takenHere;
					robbed |= takenHere || v->SurrenderOutcome == SurrenderTerms::RansomCargo
						|| (v->SurrenderOutcome == SurrenderTerms::TakeShip && (!v->Captured || takenHere));
				}
				bool pirate = by != nullptr && by->IsPirate;
				// A wanted ship stopped by the law answers only for hurting the patrol; a lawful ship that was
				// attacked and fought back did nothing wrong (unless it turned robber).
				// Nor did a third party that only fought alongside the side that started it (a bounty hunter joining
				// a navy against raiders, with that navy's enemies on the raiders' side), unless anyone was hurt.
				if (b->EnforcingSide == 1 - side) { if (!harmed) continue; }
				else if ((side != b->AggressorSide || (by != b->SideLeaders[side] && !harmed)) && !pirate && !robbed) continue;
				auto rec = key.Record ? key.Record : RecordOfCaptain(key.Ship);
				// The one they attacked who lived to tell it, else anyone close enough to see.
				cs::String witness; // (null is empty)
				for (const auto& v : victims)
					if (!v->Dead && !v->Captured)
					{
						witness = v->Ship != nullptr ? cs::Cat("the ", v->Ship->Name) : v->Platform != nullptr ? v->Platform->Name : v->Fort != nullptr ? v->Fort->Name : cs::String();
						if (!witness.empty()) break;
					}
				if (witness.empty()) witness = FindWitness(where, jd, [&inBattle](cs::Ref<Ship> s) { return inBattle.Contains(s); });
				// The side's leader, if its own ships were among those wronged (not a power the offender is at war with, fighting beside them).
				auto leader = b->SideLeaders[1 - side];
				auto victim = leader != nullptr && !leader->IsPirate && victims.Exists([&leader](const cs::Ref<BattleUnit>& v) { return v->Faction == leader; }) ? leader : victims[0]->Faction;
				auto kind = pirate || robbed ? CrimeKind::Piracy : CrimeKind::Assault;
				Commit(rec, kind, juris, victim, shownBody, jd, witness, b->Name);
			}
		}

		// Bounties paid in full: a wanted commander killed or taken by those who posted them.
		for (const auto& u : b->Units)
		{
			if (u->Kind != UnitKind::Ship || u->Ship == nullptr || !(u->Dead || u->Captured)) continue;
			bool player = u->PlayerFlown;
			// An escort answers only for a wanted captain of its own aboard (a band merged into another group).
			cs::Ref<LawRecord> rec = player ? Player
				: u->Ship->IsEscort() ? (world && world->Outlaws ? world->Outlaws->RecordCommanding(u->Ship) : nullptr)
				: FindRecord(u->Ship);
			if (rec == nullptr || !rec->Wanted()) continue;
			// Alive or dead (see Outlaws.ClaimInBattle): a captain taken goes in a brig to be handed over for the
			// full price, one who got away in the pods is still wanted, and a kill pays hunters (or, on a scanned
			// wreck, the player) the dead rate.
			if (world && world->Outlaws != nullptr && world->Outlaws->ClaimInBattle(b, u, rec, jd)) continue;
			if (!player && u->Dead)
			{
				auto claimed = rec->Bounty.Keys();
				for (const auto& f : claimed) ClearBounty(rec, f);
				world->Log(jd, cs::Cat(rec->Name, " dies with the ", u->Ship->Name, ": the bounties on them lapse"), WorldEventKind::Law, shownBody);
				continue;
			}
			cs::HashSet<cs::Ref<Faction>> takers;
			for (const auto& o : b->Units)
				if (o->Side == 1 - u->FoughtOn() && o->Faction != nullptr) takers.Add(o->Faction);
			for (const auto& f : takers)
			{
				if (!rec->WantedBy(f)) continue;
				ClearBounty(rec, f);
				world->Log(jd, cs::Cat(f->Name, " collects its bounty on ", rec->Name), WorldEventKind::Law, shownBody);
				if (player) Notice(cs::Cat(f->Name, " has collected its bounty on you with the ", u->Ship->Name));
			}
		}
	}

	bool Law::HidesHaven(int body)
	{
		for (const auto& s : world->Economy->At(body))
			if (!s->Discovered()) return true;
		return false;
	}

	cs::Ref<LawRecord> Law::RecordOfCaptain(const cs::Ref<Ship>& ship)
	{
		auto captain = ship->Controller();
		cs::Ref<LawRecord> r;
		if (!Captains.TryGetValue(captain, r))
		{
			r = cs::New<LawRecord>(CaptainName(captain), captain);
			Captains[captain] = r;
		}
		return r;
	}

	cs::List<cs::Ref<BattleUnit>> Law::LawfulVictims(const cs::Ref<Battle>& b, int side, const cs::Ref<Faction>& by, const cs::Ref<Faction>& juris)
	{
		cs::List<cs::Ref<BattleUnit>> list;
		for (const auto& v : b->Units)
		{
			// By the side it fought on: a hull taken by boarders has moved to its captor's side.
			if (v->FoughtOn() != side || v->Kind == UnitKind::Fighter || v->Faction == nullptr || v->Faction->IsPirate || v->Faction == by) continue;
			if (by != nullptr && world->Diplomacy()->Get(by, v->Faction) == Relation::War) continue;
			cs::Ref<LawRecord> rec = v->PlayerFlown ? Player : v->Ship != nullptr ? FindRecord(v->Ship) : nullptr;
			if (rec != nullptr && (juris != nullptr ? rec->WantedBy(juris) : rec->Wanted())) continue;
			list.Add(v);
		}
		return list;
	}

	DVec3 Law::BattleHelio(const cs::Ref<Battle>& b, double jd)
	{
		auto lanes = world->Fleet->Lanes;
		if (b->Site == nullptr || lanes == nullptr) return world->Ephemeris->HelioAt(b->Body, jd) + b->Anchor;
		const auto& hop = b->Site->Hop;
		auto a = world->Ephemeris->HelioAt(hop.From, jd) + b->Site->FromOffset;
		auto c = world->Ephemeris->HelioAt(hop.To, jd) + b->Site->ToOffset;
		return lanes->CurveKm(hop.Lane, a, c, b->Site->Progress, jd) + b->Site->Side;
	}

	// ---------------------------------------------------------------- the player's ship and the patrols

	cs::List<cs::Ref<CargoItem>> Law::ContrabandIn(const cs::Ref<Ship>& ship, const cs::Ref<Faction>& f)
	{
		cs::List<cs::Ref<CargoItem>> found;
		if (ship == nullptr) return found;
		for (const auto& item : ship->PersonalHold()->Items())
			if (item->Stolen || (f != nullptr && f->Contraband.Contains(item->Good))) found.Add(item);
		return found;
	}

	void Law::UpdatePilot(const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, DVec3 forward, double jd)
	{
		lastJd = cs::Double::IsNaN(lastJd) ? jd : cs::Math::Max(lastJd, jd);
		if (ship == nullptr || ship->Destroyed || ship->Battle != nullptr)
		{
			Hail = nullptr;
			return;
		}
		auto helio = world->Ephemeris->HelioAt(body, jd) + offsetKm;
		CheckTrespass(ship, body, offsetKm, jd);
		if (Hail != nullptr)
		{
			AnswerHail(ship, body, offsetKm, forward, helio, jd);
			return;
		}
		for (const auto& p : world->Fleet->Ships())
		{
			if (!IsPatrol(p, ship)) continue;
			auto f = p->Faction;
			double until = 0;
			if (scannedUntil.TryGetValue(f, until) && jd < until) continue;
			bool wanted = Player->WantedBy(f);
			// Scans are routine only in a patrol's own space; and a navy does not stop its own warships.
			if (!wanted && (JurisdictionAt(body) != f || (ship->Faction == f && ship->Spec->IsMilitary))) continue;
			double range = (world->Fleet->StateOf(p, jd).HelioKm - helio).Length();
			if (range > PatrolHailKm) continue;
			// It has to make the ship out first: a hull running silent can slip past a patrol that only holds a blip.
			if (PatrolSees(p, ship, range, jd) < ContactLevel::Class) continue;
			auto hail = cs::New<PatrolHail>();
			hail->Patrol = p;
			hail->By = f;
			hail->Interdiction = wanted;
			hail->StartJd = jd;
			hail->Fine = Player->BountyBy(f);
			hail->DueJd = jd + (wanted ? HailSeconds : ScanSeconds) / Astro::SecondsPerDay;
			Hail = hail;
			Notice(wanted
				? cs::Cat(f->Name, " patrol ", p->Name, ": \"", ship->Name, ", you are wanted. Heave to and pay ", Cr(Hail->Fine), " cr, or we open fire.\"")
				: cs::Cat(f->Name, " patrol ", p->Name, ": \"", ship->Name, ", hold your position for a cargo scan.\""));
			return;
		}
	}

	ContactLevel Law::PatrolSees(const cs::Ref<Ship>& p, const cs::Ref<Ship>& ship, double rangeKm, double jd)
	{
		auto sensors = world->Sensors;
		if (sensors == nullptr) return ContactLevel::Identified;
		bool burning = ship == world->PilotedShip ? sensors->PilotBurning() : sensors->Burning(ship, jd);
		return Sensors::Resolve(sensors->GroupSuite(p), Signatures::Of(ship, burning), rangeKm, Signatures::Transponder(ship));
	}

	bool Law::IsPatrol(const cs::Ref<Ship>& p, const cs::Ref<Ship>& player)
	{
		return p != player && !p->Destroyed && !p->GateDeparted && !p->IsEscort() && !p->Derelict && p->Battle == nullptr && !p->Planetside()
			&& p->Spec->IsMilitary && p->Faction != nullptr && p->Faction->IsMilitary && !p->Faction->IsPirate && p->Combat != nullptr && p->Combat->Armed()
			&& !world->Diplomacy()->Hostile(p->Faction, player->Faction);
	}

	void Law::AnswerHail(const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, DVec3 forward, DVec3 helio, double jd)
	{
		auto h = Hail;
		auto p = h->Patrol;
		auto f = h->By;
		if (p->Destroyed || p->Battle != nullptr || world->Diplomacy()->Hostile(f, ship->Faction))
		{
			Hail = nullptr;
			return;
		}
		if (h->Interdiction && !Player->WantedBy(f))
		{
			Hail = nullptr;
			return;
		}
		double d = (world->Fleet->StateOf(p, jd).HelioKm - helio).Length();
		// Run out of range, or gone dark under its nose: either way the patrol saw it go.
		if (d > PatrolHoldKm || PatrolSees(p, ship, d, jd) == ContactLevel::None)
		{
			Hail = nullptr;
			scannedUntil[f] = jd + ScanCooldownDays;
			Commit(Player, CrimeKind::FleeingScan, f, f, body, jd, cs::Cat("the ", p->Name), h->Interdiction ? "ran from an interdiction" : "ran from a scan");
			return;
		}
		if (jd < h->DueJd) return;
		Hail = nullptr;
		scannedUntil[f] = jd + ScanCooldownDays;
		if (h->Interdiction)
		{
			cs::Ref<Battle> b = world->War ? world->War->TryStartInterdiction(ship, p, body, offsetKm, forward, jd) : nullptr;
			Notice(b != nullptr ? cs::Cat(f->Name, " patrol ", p->Name, " moves to interdict you") : cs::Cat(f->Name, " patrol ", p->Name, " lets you go for now"));
			return;
		}
		auto contraband = ContrabandIn(ship, f);
		if (contraband.Count() == 0)
		{
			Notice(cs::Cat(f->Name, " patrol ", p->Name, ": \"Scan clean. Safe travels, ", ship->Name, ".\""));
			return;
		}
		cs::HashSet<cs::String> goods;
		for (const auto& item : contraband)
		{
			ship->PersonalHold()->Remove(item);
			goods.Add(cs::Strings::ToLowerInvariant(GoodInfo::Of(item->Good)->Name));
		}
		Notice(cs::Cat(f->Name, " patrol ", p->Name, " finds contraband and confiscates ", contraband.Count(), " crate", (contraband.Count() == 1 ? "" : "s"), " (", cs::Strings::Join(", ", goods), ")"));
		auto here = JurisdictionAt(body);
		Commit(Player, CrimeKind::Smuggling, here ? here : f, f, body, jd, cs::Cat("the ", p->Name), cs::Strings::Join(", ", goods));
	}

	void Law::CheckTrespass(const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, double jd)
	{
		if (body < 0 || body >= world->Catalog()->Count() || offsetKm.Length() > ScaleModel::NeighbourhoodKm((*world->Catalog())[body])) return;
		cs::Ref<Settlement> port;
		for (const auto& s : world->Economy->At(body))
			if (!s->Fallen && s->Owner != nullptr && !s->Owner->IsPirate && (port == nullptr || s->Population > port->Population)) port = s;
		if (port == nullptr) return;
		auto f = port->Owner;
		if (world->Diplomacy()->Hostile(f, ship->Faction) || Player->StandingWith(f) >= DockMinStanding) return;
		double until = 0;
		if (trespassUntil.TryGetValue(f, until) && jd < until) return;
		trespassUntil[f] = jd + TrespassCooldownDays;
		Commit(Player, CrimeKind::Trespass, f, f, body, jd, port->Name, cs::Cat("barred from ", port->Name));
	}

	void Law::Notice(const cs::String& text)
	{
		notices.Add(text);
		if (notices.Count() > 20) notices.RemoveAt(0);
	}

	cs::List<cs::String> Law::TakeNotices()
	{
		cs::List<cs::String> list = notices;
		notices.Clear();
		return list;
	}

	cs::String Law::Cr(double v) { return cs::Fmt(v, "N0"); }

	// ================================================================ ObjectGraph registrations (docs/PERSISTENCE.md)
	// Every instance field of the C# classes, by .NET name (any order: the registry sorts them).

	SG_ENUM(CrimeKind, "Unity.SpaceGod.Core.CrimeKind")

	SG_SERIALIZE(Crime, "Unity.SpaceGod.Core.Crime",
		SG_F(Kind), SG_F(Offender), SG_F(Jurisdiction), SG_F(Victim), SG_F(PostedBy), SG_F(Body), SG_F(Jd),
		SG_F(Witness), SG_F(Detail), SG_F(Bounty))

	SG_SERIALIZE(LawRecord, "Unity.SpaceGod.Core.LawRecord",
		SG_F(Name), SG_P(Captain), SG_F(Standing), SG_F(Bounty), SG_F(BountyUntil), SG_F(Crimes), SG_F(Notoriety), SG_F(Unwitnessed))

	SG_SERIALIZE(PatrolHail, "Unity.SpaceGod.Core.PatrolHail",
		SG_F(Patrol), SG_F(By), SG_F(Interdiction), SG_F(StartJd), SG_F(DueJd), SG_F(Fine))

	SG_SERIALIZE(Law, "Unity.SpaceGod.Core.Law",
		SG_F(world), SG_F(Player), SG_F(Captains), SG_F(Recent), SG_F(notices), SG_P(Hail), SG_F(Committed),
		SG_F(PatrolHailKm), SG_F(PatrolHoldKm), SG_F(ScanSeconds), SG_F(HailSeconds), SG_F(ScanCooldownDays), SG_F(TrespassCooldownDays),
		// (Committed's subscriber, Outlaws.OnCrime, is World's wiring: World's SG_LOADED hook makes it again.)
		SG_F(lastJd), SG_F(scannedUntil), SG_F(trespassUntil))
}
