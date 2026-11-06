// Port of Unity.SpaceGod Core/Law/Outlaws.cs (US-T20).

#include "Law/Outlaws.h"

#include <utility>
#include <vector>

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Battle.h"
#include "Combat/Boarding.h"
#include "Combat/CombatState.h"
#include "Combat/Crews.h"
#include "Combat/Diplomacy.h"
#include "Combat/Morale.h"
#include "Combat/Outfitting.h"
#include "Combat/War.h"
#include "Combat/Wreck.h"
#include "Economy/Contracts.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Lagrange.h"
#include "Law/Law.h"
#include "Persistence/ObjectGraph.h"
#include "Sensors/Sensors.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	const char* ToString(OutlawFate v)
	{
		switch (v)
		{
			case OutlawFate::AtLarge: return "AtLarge";
			case OutlawFate::Killed: return "Killed";
			case OutlawFate::Captured: return "Captured";
			case OutlawFate::Vanished: return "Vanished";
			case OutlawFate::Held: return "Held";
		}
		return "";
	}

	// ================================================================ Outlaw

	Outlaw::Outlaw(cs::Ref<LawRecord> record, cs::Ref<::SpaceGod::Core::Ship> ship, cs::Ref<Settlement> haven, double sinceJd)
	{
		Record = std::move(record);
		Ship = std::move(ship);
		Haven = std::move(haven);
		SinceJd = sinceJd;
	}

	cs::String Outlaw::Name() const { return Record->Name; }

	cs::String Outlaw::Deeds() const
	{
		cs::List<cs::String> parts;
		if (ShipsRobbed > 0) parts.Add(cs::Cat(ShipsRobbed, ShipsRobbed == 1 ? " ship" : " ships", " robbed"));
		if (Kills > 0) parts.Add(cs::Cat(Kills, " destroyed"));
		if (Ransoms > 0) parts.Add(cs::Cat(Ransoms, Ransoms == 1 ? " cargo" : " cargoes", " ransomed"));
		if (parts.Count() == 0) parts.Add(CrimeCount > 0 ? cs::Cat(CrimeCount, CrimeCount == 1 ? " crime" : " crimes", " on record") : cs::String("no crimes yet on record"));
		return cs::Strings::Join(", ", parts);
	}

	// ================================================================ BountyHunter

	BountyHunter::BountyHunter(cs::Ref<::SpaceGod::Core::Ship> ship, double sinceJd, cs::Ref<World> world)
	{
		Ship = ship;
		Faction = ship->Faction;
		SinceJd = sinceJd;
		this->world = std::move(world);
	}

	cs::String BountyHunter::Name() const { return Ship->Name; }

	bool BountyHunter::Alive() const
	{
		return !Retired && !Ship->Destroyed && !Ship->Derelict && !Ship->GateDeparted && Ship->Faction == Faction && world->Fleet->IsPresent(Ship);
	}

	// ================================================================ BountyPosting

	bool BountyPosting::IsPlayer() const { return Player; }

	// ================================================================ Outlaws

	const cs::List<cs::String>& Outlaws::HunterNames()
	{
		static const cs::List<cs::String> names = {
			"Nemesis", "Writ of Seizure", "Long Arm", "Due Process", "Warrant", "Reckoning", "Bail Bond", "Quarry",
			"Manhunter", "Summons", "Last Word", "Price on Your Head",
		};
		return names;
	}

	Outlaws::Outlaws(cs::Ref<World> world, int seed) : rng(seed)
	{
		this->world = std::move(world);
	}

	cs::Ref<::SpaceGod::Core::Law> Outlaws::Law() const { return world->Law; }

	cs::Ref<Faction> Outlaws::Guild() const { return world->Independents; }

	// ---------------------------------------------------------------- lookups

	cs::Ref<Outlaw> Outlaws::Of(const cs::Ref<Ship>& ship)
	{
		cs::Ref<Outlaw> o;
		return ship != nullptr && byShip.TryGetValue(ship->Controller(), o) ? o : nullptr;
	}

	cs::Ref<Outlaw> Outlaws::Of(const cs::Ref<LawRecord>& r)
	{
		cs::Ref<Outlaw> o;
		return r != nullptr && byRecord.TryGetValue(r, o) ? o : nullptr;
	}

	cs::Ref<BountyHunter> Outlaws::HunterOf(const cs::Ref<Ship>& ship)
	{
		cs::Ref<BountyHunter> h;
		return ship != nullptr && byHunter.TryGetValue(ship->Controller(), h) && !h->Retired && h->Ship->Faction == Guild() ? h : nullptr;
	}

	cs::Ref<Sighting> Outlaws::SeenLast(const cs::Ref<LawRecord>& r)
	{
		cs::Ref<Sighting> s;
		return r != nullptr && LastSeen.TryGetValue(r, s) ? s : nullptr;
	}

	// ---------------------------------------------------------------- the clock

	void Outlaws::Update(double jd)
	{
		if (cs::Double::IsNaN(nextDailyJd))
		{
			Sync(jd);
			nextDailyJd = jd + 1;
			return;
		}
		while (jd >= nextDailyJd)
		{
			Daily(nextDailyJd);
			nextDailyJd += 1;
		}
	}

	void Outlaws::Daily(double jd)
	{
		Sync(jd);
		SettleFates(jd);
		Sightings(jd);
		Fear(jd);
		ReviewContracts(jd);
		Commission(jd);
		RepairHunters();
		SettleCaptives(jd);
		LapseClaims(jd);
	}

	void Outlaws::Sync(double jd)
	{
		if (world->War == nullptr || Law() == nullptr) return;
		for (const auto& s : world->Fleet->Ships())
		{
			if (s->Destroyed || s->IsEscort() || s->Derelict || s->GateDeparted || s->Faction == nullptr || !s->Faction->IsPirate || s == world->PilotedShip) continue;
			if (byShip.ContainsKey(s)) continue;
			// A captain back with the raiders takes the next band that sails -- not one whose own captain the law
			// already has on its books for what the band has done (that captain is the one wanted for it).
			auto ashore = All.Find([](const cs::Ref<Outlaw>& x) { return x->AtLarge() && x->Ashore; });
			auto own = Law()->FindRecord(s);
			bool known = own != nullptr && (own->Crimes.Count() > 0 || own->Wanted() || own->Notoriety > 0);
			if (ashore != nullptr && !known)
			{
				Rebind(ashore, s, jd);
				continue;
			}
			auto record = Law()->RecordOf(s);
			auto o = cs::New<Outlaw>(record, s, world->War->HomeHaven(s), jd);
			All.Add(o);
			byShip[s] = o;
			byRecord[o->Record] = o;
		}
	}

	void Outlaws::SettleFates(double jd)
	{
		for (const auto& o : All)
		{
			cs::Ref<Outlaw> back;
			if (o->AtLarge() && o->Ashore && o->FlagshipAway && o->Ship != nullptr && !o->Ship->GateDeparted && !o->Ship->Destroyed && !o->Ship->Derelict
				&& o->Ship->Faction != nullptr && o->Ship->Faction->IsPirate && world->Fleet->IsPresent(o->Ship)
				&& byShip.TryGetValue(o->Ship, back) && back == o)
			{
				o->Ashore = false;
				o->FlagshipAway = false;
			}
			if (!o->Aboard()) continue;
			if (o->Ship->Faction == nullptr || !o->Ship->Faction->IsPirate) End(o, OutlawFate::Captured, jd, o->Ship->Faction ? o->Ship->Faction->Name : cs::String());
			else if (o->Ship->GateDeparted || !world->Fleet->IsPresent(o->Ship))
			{
				o->Ashore = true;
				o->FlagshipAway = true;
			}
			else if (o->Ship->Destroyed || o->Ship->Derelict) End(o, OutlawFate::Vanished, jd, cs::String());
		}
	}

	void Outlaws::End(const cs::Ref<Outlaw>& o, OutlawFate fate, double jd, const cs::String& by)
	{
		if (!o->AtLarge() && o->Fate != OutlawFate::Held) return;
		Release(o);
		o->Pod = nullptr;
		o->Ashore = false;
		o->Fate = fate;
		o->FateJd = jd;
		o->FateBy = by;
		cs::String what = fate == OutlawFate::Killed ? "is dead" : fate == OutlawFate::Captured ? "is taken alive" : "has vanished";
		world->Log(jd, cs::Cat(Cap(o->Title()), " ", what, !by.empty() ? cs::Cat(" (", by, ")") : cs::String(""), " after ", cs::Fmt(o->DaysAtLarge(jd), "0"), " days at large: ", o->Deeds()),
			WorldEventKind::Law, -1);
	}

	// ---------------------------------------------------------------- sightings

	void Outlaws::Sightings(double jd)
	{
		for (const auto& o : All)
		{
			if (!o->Aboard() || o->Ship->Battle != nullptr) continue;
			auto pos = world->Fleet->StateOf(o->Ship, jd).HelioKm;
			// Seen by what the hull itself gives off (a raider lying dark is hard to make out), not a battle's glare.
			cs::String witness = world->Sensors != nullptr ? Law()->FindWitness(pos, jd, nullptr, world->Sensors->SignatureOf(o->Ship, jd)) : Law()->FindWitness(pos, jd);
			if (witness.empty()) continue;
			// A flagship merged into another band goes where that band's leader goes.
			Sight(o->Record, Locate(o->Ship->Controller()), jd, witness);
		}
		// The pilot the player is embodied in (US-T148): a wanted hull is seen where the helm last reported it.
		auto p = Law()->PlayerRecord();
		if (p != nullptr && p->Wanted() && world->PilotedShip != nullptr && pilotBody >= 0 && !cs::Double::IsNaN(pilotJd) && jd - pilotJd < 1)
		{
			double sig = world->Sensors != nullptr && world->Sensors->PilotSignature() > 0 ? world->Sensors->PilotSignature() : Signatures::Battle;
			cs::String witness = Law()->FindWitness(pilotHelio, jd, nullptr, sig);
			if (!witness.empty()) Sight(p, Hide(pilotBody, cs::Cat("near ", BodyName(pilotBody))), jd, witness);
		}
	}

	Outlaws::Place Outlaws::Locate(const cs::Ref<Ship>& s)
	{
		int body;
		cs::String where;
		switch (s->Phase)
		{
			case ShipPhase::Transit:
				body = s->DestinationBody;
				where = cs::Cat("bound for ", BodyName(body));
				break;
			case ShipPhase::Lane:
			case ShipPhase::Lurk:
			case ShipPhase::Adrift:
				body = s->LaneLeg.From;
				where = cs::Cat(s->Phase == ShipPhase::Lane ? "riding the " : s->Stalled ? "stalled off the " : "lurking off the ", s->LaneLeg.Lane->Name, " lane");
				break;
			default:
				body = s->Orbit ? s->Orbit->Body : -1;
				where = cs::Cat("at ", BodyName(body));
				break;
		}
		return Hide(body, where);
	}

	Outlaws::Place Outlaws::Hide(int body, const cs::String& where)
	{
		if (body < 0 || body >= world->Catalog()->Count()) return Place{ -1, where };
		for (const auto& st : world->Economy->At(body))
			if (!st->Discovered())
			{
				auto rock = (*world->Catalog())[body];
				return Place{ -1, rock->TrojanPoint != LagrangePoint::None ? cs::String("somewhere in the Trojans")
					: rock->Kind == BodyKind::Asteroid ? cs::String("somewhere in the Belt") : cs::String("somewhere in the dark") };
			}
		return Place{ body, where };
	}

	void Outlaws::Sight(const cs::Ref<LawRecord>& r, const Place& at, double jd, const cs::String& by)
	{
		cs::Ref<Sighting> s;
		if (!LastSeen.TryGetValue(r, s))
		{
			s = cs::New<Sighting>();
			LastSeen[r] = s;
		}
		s->Body = at.body;
		s->Where = at.where;
		s->Jd = jd;
		s->By = by;
	}

	cs::String Outlaws::BodyName(int body) { return body >= 0 && body < world->Catalog()->Count() ? (*world->Catalog())[body]->Name : cs::String("deep space"); }

	// ---------------------------------------------------------------- crimes and battles

	void Outlaws::OnCrime(const cs::Ref<Crime>& c)
	{
		if (c == nullptr || !c->Recorded()) return;
		auto o = Of(c->Offender);
		if (o != nullptr)
		{
			o->CrimeCount++;
			if (o->Feared) AddBounty(c->Offender, c, c->Bounty * (FearedBountyFactor - 1));
		}
		auto at = Hide(c->Body, c->Body >= 0 ? cs::Cat("near ", BodyName(c->Body)) : !c->Detail.empty() ? cs::Cat("at the ", c->Detail) : cs::String("somewhere unseen"));
		Sight(c->Offender, at, c->Jd, c->Witness);
	}

	void Outlaws::AddBounty(const cs::Ref<LawRecord>& r, const cs::Ref<Crime>& c, double extra)
	{
		if (extra <= 0 || c->PostedBy == nullptr) return;
		double posted = r->BountyBy(c->PostedBy) + extra;
		r->Bounty[c->PostedBy] = posted;
		r->BountyUntil[c->PostedBy] = c->Jd + ::SpaceGod::Core::Law::BountyDays;
		c->Bounty += extra;
	}

	void Outlaws::OnBattleStarted(const cs::Ref<Battle>& b)
	{
		if (b == nullptr) return;
		for (int side = 0; side < 2; side++)
		{
			for (const auto& u : b->Units)
			{
				if (u->Side != 1 - side || u->Ship == nullptr) continue;
				auto o = Of(u->Ship);
				if (o != nullptr && o->AtLarge() && o->Feared)
				{
					b->Dread[side] = FearedResolve;
					break;
				}
			}
		}
	}

	void Outlaws::OnBattleEnded(const cs::Ref<Battle>& b, double jd)
	{
		if (b == nullptr || b->Units.Count() == 0) return;
		cs::HashSet<cs::Ref<Outlaw>> seen;
		for (const auto& u : b->Units)
		{
			if (u->Ship == nullptr || u->Side < 0 || u->Side > 1) continue;
			auto o = Of(u->Ship);
			// (One taken alive in this very fight is in a brig already: what the band did still counts.)
			if (o == nullptr || !(o->AtLarge() || (o->Fate == OutlawFate::Held && o->FateJd == jd)) || !seen.Add(o)) continue;
			// Sides as fought: a hull taken in the fight has moved to its captor's side since.
			int side = u->FoughtOn();
			int kills = 0, robbed = 0, ransoms = 0;
			for (const auto& v : b->Units)
			{
				if (v->FoughtOn() != 1 - side || v->Kind == UnitKind::Fighter || v->Faction == nullptr || v->Faction->IsPirate) continue;
				if (v->Dead) kills++;
				else if (v->Captured || v->SurrenderOutcome == SurrenderTerms::TakeShip) robbed++;
				else if (v->SurrenderOutcome == SurrenderTerms::RansomCargo)
				{
					robbed++;
					ransoms++;
				}
			}
			o->Battles++;
			o->Kills += kills;
			o->ShipsRobbed += robbed;
			o->Ransoms += ransoms;
			// The crime the law booked for this fight, if anyone saw it: the deeds go on its bounty.
			auto rec = o->Record;
			cs::Ref<Crime> crime = rec->Crimes.Count() > 0 ? rec->Crimes[rec->Crimes.Count() - 1] : nullptr;
			// Unless the law has already settled every price on them in this fight (paid a hunter, or let them lapse on
			// a captain dead with the ship): a captain still wanted -- at large, in the pods or in a brig -- answers for it.
			if (crime != nullptr && crime->Jd == jd && crime->Detail == b->Name && crime->Recorded() && rec->Wanted())
			{
				double extra = kills * KillBounty + robbed * RobbedBounty + ransoms * RansomBounty;
				if (o->Feared) extra *= FearedBountyFactor;
				if (extra > 0)
				{
					AddBounty(rec, crime, extra);
					world->Log(jd, cs::Cat(crime->PostedBy->Name, " raises the price on ", o->Title(), " to ", Cr(rec->BountyBy(crime->PostedBy)), " cr: ", Deeds(kills, robbed, ransoms), " at ", b->Name),
						WorldEventKind::Law, -1);
				}
				if (b->Site != nullptr) Sight(rec, Hide(b->Site->Hop.From, cs::Cat("on the ", b->Site->Hop.Lane->Name, " lane")), jd, crime->Witness);
			}
		}
		// Outlaws whose flagship went down or was taken in it are finished -- unless they got away in its pods,
		// or were taken alive into a brig.
		for (const auto& u : b->Units)
		{
			if (u->Ship == nullptr || !(u->Dead || u->Captured)) continue;
			cs::Ref<Outlaw> o;
			if (!byShip.TryGetValue(u->Ship, o) || !o->AtLarge() || o->Ship != u->Ship) continue;
			if (u->Dead && o->Pod != nullptr) continue;
			if (u->Captured && Seize(b, u, o, jd)) continue;
			End(o, u->Dead ? OutlawFate::Killed : OutlawFate::Captured, jd, Victor(b, 1 - u->FoughtOn()));
		}
		// Captains in the brigs of ships that fought here: a breached brig lets them go.
		auto all = All.ToArray();
		for (const auto& o : all)
			if (o->Fate == OutlawFate::Held && o->HeldBy != nullptr)
				if (auto hu = b->UnitOf(o->HeldBy)) Breach(o, hu, jd);
	}

	cs::String Outlaws::Deeds(int kills, int robbed, int ransoms)
	{
		cs::List<cs::String> parts;
		if (kills > 0) parts.Add(cs::Cat(kills, kills == 1 ? " ship destroyed" : " ships destroyed"));
		if (robbed > 0) parts.Add(cs::Cat(robbed, robbed == 1 ? " ship robbed" : " ships robbed"));
		if (ransoms > 0) parts.Add(cs::Cat(ransoms, ransoms == 1 ? " cargo ransomed" : " cargoes ransomed"));
		return cs::Strings::Join(", ", parts);
	}

	cs::String Outlaws::Victor(const cs::Ref<Battle>& b, int side)
	{
		for (const auto& u : b->Units)
			if (u->Side == side && u->Ship != nullptr && IsHunter(u->Ship)) return cs::Cat("the bounty hunter ", HunterOf(u->Ship)->Name());
		return side >= 0 && side < 2 && b->SideLeaders[side] != nullptr ? b->SideLeaders[side]->Name : cs::String();
	}

	bool Outlaws::ClaimByHunter(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<LawRecord>& rec, double jd)
	{
		if (b == nullptr || u == nullptr || rec == nullptr || !rec->Wanted()) return false;
		cs::Ref<BountyHunter> hunter;
		for (const auto& o : b->Units)
			// Only a hunter still there to scan the wreck or take the prize collects: one lost in the fight claims nothing.
			if (o->Side == 1 - u->FoughtOn() && !o->Dead && !o->Captured && o->Ship != nullptr)
				if (auto h = HunterOf(o->Ship)) hunter = h;
		if (hunter == nullptr) return false;
		double total = Pay(rec, u->Dead ? DeadShare : 1, hunter->Ship);
		auto outlaw = Of(rec);
		// (The hull the player flew in that fight: the helm may have let go of it since.)
		bool player = u->PlayerFlown;
		cs::String name = outlaw != nullptr ? outlaw->Title() : player ? cs::Cat("the ", u->Ship ? u->Ship->Name : cs::String()) : rec->Name;
		world->Log(jd, cs::Cat("The bounty hunter ", hunter->Name(), " collects ", Cr(total), " cr on ", name, ", ", (u->Dead ? "dead (the wreck scanned for proof)" : "taken"), " at ", b->Name), WorldEventKind::Law, -1);
		if (player) Law()->Notice(cs::Cat("The bounty hunter ", hunter->Name(), " has collected the ", Cr(total), " cr on your head"));
		return true;
	}

	// ---------------------------------------------------------------- alive or dead (U.SPC-T63)

	double Outlaws::DeadRate(const cs::Ref<LawRecord>& r) { return r == nullptr ? 0 : r->TotalBounty() * DeadShare; }

	double Outlaws::RansomFor(const cs::Ref<Outlaw>& o) { return o == nullptr ? 0 : cs::Math::Max(MinRansom, RansomShare * o->Record->TotalBounty()); }

	cs::List<cs::Ref<Outlaw>> Outlaws::HeldAboard(const cs::Ref<Ship>& ship)
	{
		cs::List<cs::Ref<Outlaw>> list;
		if (ship != nullptr)
			for (const auto& o : All)
				if (o->Fate == OutlawFate::Held && o->HeldBy == ship) list.Add(o);
		return list;
	}

	cs::Ref<KillClaim> Outlaws::ClaimOn(const cs::Ref<Outlaw>& o) { return Claims.Find([&o](const cs::Ref<KillClaim>& c) { return c->Outlaw == o; }); }

	bool Outlaws::ClaimInBattle(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<LawRecord>& rec, double jd)
	{
		if (b == nullptr || u == nullptr || rec == nullptr) return false;
		auto o = Of(rec);
		if (o == nullptr || o->Ship != u->Ship || !o->AtLarge()) return ClaimByHunter(b, u, rec, jd);
		if (u->Captured) return Seize(b, u, o, jd) || ClaimByHunter(b, u, rec, jd);
		// A captain ashore is not aboard the hull that died: nothing of theirs is lost with it (U.SPC-T579).
		if (o->Ashore) return u->Dead;
		if (!u->Dead) return false;
		if (o->Pod != nullptr) return true;
		if (ClaimByHunter(b, u, rec, jd)) return true;
		if (!rec->Wanted() || !PlayerFoughtAgainst(b, u)) return false;
		cs::Ref<Wreck> wreck = world->War ? world->War->Wrecks->Of(u->Ship) : nullptr;
		auto claim = cs::New<KillClaim>();
		claim->Outlaw = o;
		claim->Wreck = wreck;
		claim->Jd = jd;
		claim->Battle = b->Name;
		Claims.Add(claim);
		Law()->Notice(cs::Cat(Cap(o->Title()), " died with the ", u->Ship->Name, ". Scan the wreck to prove the kill for ", Cr(DeadRate(rec)), " cr (alive would have paid ", Cr(rec->TotalBounty()), ")"));
		return true;
	}

	bool Outlaws::PlayerFoughtAgainst(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u)
	{
		for (const auto& v : b->Units)
			if (v->PlayerFlown && v->Side == 1 - u->FoughtOn() && !v->Dead && !v->Captured && v->Ship != nullptr && v->Ship == world->PilotedShip) return true;
		return false;
	}

	bool Outlaws::Seize(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<Outlaw>& o, double jd)
	{
		if (o->Fate == OutlawFate::Held) return true;
		cs::Ref<Faction> captor = u->Combat ? u->Combat->Interior->Boarder : nullptr;
		cs::Ref<Ship> best;
		int bestRank = cs::Int::MaxValue;
		for (const auto& v : b->Units)
		{
			if (v->Side != 1 - u->FoughtOn() || v->Ship == nullptr || v->Dead || v->Captured || v->Ship == u->Ship || v->Ship->Destroyed) continue;
			if (v->Faction == nullptr || v->Faction->IsPirate || (captor != nullptr && v->Faction != captor)) continue;
			// A brig already holed (see Breach) holds nobody: the captain would be out of it in the same breath.
			if (Outfitting::BrigRoom(v->Ship) < 1 || (v->Combat != nullptr && v->Combat->HullFraction() < BrigBreachHull)) continue;
			int rank = v->Ship == world->PilotedShip ? 0 : IsHunter(v->Ship) ? 1 : 2;
			if (rank < bestRank)
			{
				bestRank = rank;
				best = v->Ship;
			}
		}
		return best != nullptr && Hold(o, best, jd, cs::Cat("taken alive with the ", u->Ship->Name, " at ", b->Name));
	}

	bool Outlaws::Hold(const cs::Ref<Outlaw>& o, const cs::Ref<Ship>& ship, double jd, const cs::String& how)
	{
		if (o == nullptr || ship == nullptr || ship->Destroyed || Outfitting::BrigRoom(ship) < 1) return false;
		if (o->Fate != OutlawFate::AtLarge && o->Fate != OutlawFate::Held) return false;
		Release(o);
		Unbind(o);
		o->Fate = OutlawFate::Held;
		o->FateJd = jd;
		o->FateBy = cs::Cat("the ", ship->Name);
		o->HeldBy = ship;
		o->Pod = nullptr;
		o->Ashore = false;
		ship->CaptainsHeld++;
		auto rec = o->Record;
		world->Log(jd, cs::Cat(Cap(o->Title()), " is ", how, " and locked in the brig of the ", ship->Name,
			rec->Wanted() ? cs::Cat(": ", Cr(rec->TotalBounty()), " cr on their head, paid in full when handed over") : cs::String("")), WorldEventKind::Law, -1);
		if (ship == world->PilotedShip)
			Law()->Notice(cs::Cat(Cap(o->Title()), " is in your brig. Hand them over at a port of ", Posters(rec), " for ", Cr(rec->TotalBounty()), " cr, or ransom them at a haven for ", Cr(RansomFor(o)), " cr"));
		return true;
	}

	cs::String Outlaws::Posters(const cs::Ref<LawRecord>& rec)
	{
		cs::List<cs::String> names;
		for (const auto& f : rec->Bounty.Keys()) names.Add(f->Name);
		return names.Count() == 0 ? cs::String("anyone") : cs::Strings::Join(" or ", names);
	}

	void Outlaws::Unbind(const cs::Ref<Outlaw>& o)
	{
		auto old = o->Ship;
		if (old == nullptr) return;
		cs::Ref<Outlaw> was;
		if (byShip.TryGetValue(old, was) && was == o) byShip.Remove(old);
		cs::Ref<LawRecord> r;
		if (Law()->Captains.TryGetValue(old, r) && r == o->Record) Law()->Captains.Remove(old);
	}

	void Outlaws::Release(const cs::Ref<Outlaw>& o)
	{
		if (o->HeldBy != nullptr) o->HeldBy->CaptainsHeld = cs::Math::Max(0, o->HeldBy->CaptainsHeld - 1);
		o->HeldBy = nullptr;
	}

	void Outlaws::Free(const cs::Ref<Outlaw>& o, double jd, const cs::String& how)
	{
		Release(o);
		Unbind(o);
		o->Fate = OutlawFate::AtLarge;
		o->Pod = nullptr;
		o->Ashore = true;
		o->FlagshipAway = false;
		o->FateBy = cs::String();
		world->Log(jd, cs::Cat(Cap(o->Title()), " ", how, ": free, and back with the raiders"), WorldEventKind::Law, -1);
	}

	void Outlaws::Rebind(const cs::Ref<Outlaw>& o, const cs::Ref<Ship>& ship, double jd)
	{
		auto old = o->Ship;
		cs::Ref<Outlaw> was;
		if (old != nullptr && byShip.TryGetValue(old, was) && was == o) byShip.Remove(old);
		cs::Ref<LawRecord> r;
		if (old != nullptr && Law()->Captains.TryGetValue(old, r) && r == o->Record) Law()->Captains.Remove(old);
		o->Record->Captain = ship;
		Law()->Captains[ship] = o->Record;
		o->Ship = ship;
		o->Haven = world->War->HomeHaven(ship);
		o->Ashore = false;
		o->FlagshipAway = false;
		byShip[ship] = o;
		world->Log(jd, cs::Cat(Cap(o->Title()), " is back in command, of the raider ", ship->Name), WorldEventKind::Law, -1);
	}

	double Outlaws::Pay(const cs::Ref<LawRecord>& rec, double share, const cs::Ref<Ship>& payee)
	{
		double total = 0;
		std::vector<std::pair<cs::Ref<Faction>, double>> bounties;
		for (auto [f, v] : rec->Bounty) bounties.push_back({ f, v });
		for (const auto& kv : bounties)
		{
			double amount = kv.second * share;
			kv.first->Treasury -= amount;
			total += amount;
			::SpaceGod::Core::Law::ClearBounty(rec, kv.first);
		}
		auto hunter = HunterOf(payee);
		if (hunter != nullptr)
		{
			hunter->Collected += total;
			hunter->Takes++;
			hunter->Ship->Credits += total;
			if (Guild() != nullptr) Guild()->Treasury += total;
			if ((hunter->Contract ? hunter->Contract->Target : nullptr) == rec) hunter->Contract = nullptr;
		}
		else if ((payee ? payee->Faction : nullptr) != nullptr) payee->Faction->Treasury += total;
		return total;
	}

	void Outlaws::Lapse(const cs::Ref<LawRecord>& rec, double jd, const cs::String& why)
	{
		if (!rec->Wanted()) return;
		for (const auto& f : rec->Bounty.Keys()) ::SpaceGod::Core::Law::ClearBounty(rec, f);
		world->Log(jd, cs::Cat("The bounties on ", rec->Name, " lapse: ", why), WorldEventKind::Law, -1);
	}

	double Outlaws::Deliver(const cs::Ref<Outlaw>& o, const cs::Ref<Settlement>& port, double jd)
	{
		if (o == nullptr || o->Fate != OutlawFate::Held || o->HeldBy == nullptr) return 0;
		if (port == nullptr || port->Fallen || port->Owner == nullptr || port->Owner->IsPirate) return 0;
		auto holder = o->HeldBy;
		auto rec = o->Record;
		if (rec->Wanted() && !rec->WantedBy(port->Owner)) return 0;
		if (world->Economy != nullptr && !world->Economy->CanTrade(holder, port)) return 0;
		double paid = Pay(rec, 1, holder);
		End(o, OutlawFate::Captured, jd, cs::Cat("the ", holder->Name));
		world->Log(jd, cs::Cat("The ", holder->Name, " hands ", o->Name(), " over to the ", port->Owner->Name, " at ", port->Name, paid > 0 ? cs::Cat(" for ", Cr(paid), " cr, the full price alive") : cs::String("")),
			WorldEventKind::Law, port->Body);
		if (holder == world->PilotedShip) Law()->Notice(cs::Cat(Cap(o->Name()), " is handed over at ", port->Name, paid > 0 ? cs::Cat(": ", Cr(paid), " cr, the full price alive") : cs::String("")));
		return paid;
	}

	double Outlaws::Ransom(const cs::Ref<Outlaw>& o, const cs::Ref<Settlement>& haven, double jd)
	{
		if (o == nullptr || o->Fate != OutlawFate::Held || o->HeldBy == nullptr) return 0;
		if (haven == nullptr || haven->Fallen || haven->Owner == nullptr || !haven->Owner->IsPirate) return 0;
		auto holder = o->HeldBy;
		double ransom = RansomFor(o);
		if (haven->Owner->Treasury < ransom || holder->Faction == nullptr) return 0;
		haven->Owner->Treasury -= ransom;
		holder->Faction->Treasury += ransom;
		Free(o, jd, cs::Cat("is ransomed back to the ", haven->Owner->Name, " at ", haven->Name, " by the ", holder->Name, " for ", Cr(ransom), " cr"));
		return ransom;
	}

	void Outlaws::OnHullLost(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<EscapePod>& pod, bool reactor, double jd)
	{
		double roll = rng.NextDouble();
		OnHullLost(b, u, pod, reactor, jd, roll);
	}

	void Outlaws::OnHullLost(const cs::Ref<Battle>& b, const cs::Ref<BattleUnit>& u, const cs::Ref<EscapePod>& pod, bool reactor, double jd, double roll)
	{
		(void)b;
		if ((u ? u->Ship : nullptr) == nullptr) return;
		cs::Ref<Outlaw> o;
		if (pod != nullptr && pod->Captain == nullptr && byShip.TryGetValue(u->Ship, o) && o->Aboard() && o->Ship == u->Ship &&
			roll < (reactor ? EjectChanceReactor : EjectChance))
		{
			o->Pod = pod;
			pod->Captain = o;
			world->Log(jd, cs::Cat(Cap(o->Title()), " ejects from the dying ", u->Ship->Name, " in its escape pods",
				o->Record->Wanted() ? cs::Cat(", ", Cr(o->Record->TotalBounty()), " cr on their head") : cs::String("")), WorldEventKind::Law, -1);
		}
		for (const auto& c : All)
		{
			if (c->Fate != OutlawFate::Held || c->HeldBy != u->Ship || pod == nullptr || pod->Captain != nullptr) continue;
			Release(c);
			c->Fate = OutlawFate::AtLarge;
			c->Pod = pod;
			pod->Captain = c;
			world->Log(jd, cs::Cat(Cap(c->Title()), " breaks out of the brig of the dying ", u->Ship->Name, " and into its escape pods"), WorldEventKind::Law, -1);
		}
	}

	void Outlaws::OnPodResolved(const cs::Ref<EscapePod>& pod, const cs::Ref<Ship>& ship)
	{
		auto o = pod ? pod->Captain : nullptr;
		if (o == nullptr || o->Pod != pod) return;
		o->Pod = nullptr;
		double jd = pod->ResolvedJd;
		if (pod->State == PodState::Lost)
		{
			End(o, OutlawFate::Killed, jd, "their pods' air ran out");
			Lapse(o->Record, jd, "they died in the pods");
			return;
		}
		auto by = pod->TakenBy;
		if (by != nullptr && by->IsPirate)
		{
			Free(o, jd, cs::Cat("is hauled in from the pods by the ", ship != nullptr ? ship->Name : by->Name));
			return;
		}
		if (ship != nullptr && Hold(o, ship, jd, "tractored in from the pods")) return;
		End(o, OutlawFate::Captured, jd, by ? by->Name : cs::String());
		if (by != nullptr && o->Record->WantedBy(by)) ::SpaceGod::Core::Law::ClearBounty(o->Record, by);
		if (ship != nullptr && ship == world->PilotedShip) Law()->Notice(cs::Cat("No berth free in your brig: ", o->Name(), " is handed to the ", by ? by->Name : cs::String(), " as their prisoner"));
	}

	void Outlaws::Breach(const cs::Ref<Outlaw>& o, const cs::Ref<BattleUnit>& hu, double jd)
	{
		auto holder = o->HeldBy;
		if (hu->Dead)
		{
			End(o, OutlawFate::Killed, jd, cs::Cat("died in the brig of the ", holder->Name));
			Lapse(o->Record, jd, "they died in a brig");
		}
		else if (hu->Captured)
		{
			cs::Ref<Faction> captor = hu->Combat ? hu->Combat->Interior->Boarder : nullptr;
			if (captor == nullptr || captor->IsPirate) Free(o, jd, cs::Cat("is freed from the brig of the ", holder->Name, " when the raiders take it"));
			else
			{
				// Lawful boarders find the captain in the brig: theirs, and so is the price on them, in full.
				double total = 0;
				std::vector<std::pair<cs::Ref<Faction>, double>> bounties;
				for (auto [f, v] : o->Record->Bounty) bounties.push_back({ f, v });
				for (const auto& kv : bounties)
				{
					kv.first->Treasury -= kv.second;
					total += kv.second;
					::SpaceGod::Core::Law::ClearBounty(o->Record, kv.first);
				}
				captor->Treasury += total;
				End(o, OutlawFate::Captured, jd, captor->Name);
				if (total > 0) world->Log(jd, cs::Cat(captor->Name, " collects ", Cr(total), " cr on ", o->Title(), ", found in the brig of the ", holder->Name), WorldEventKind::Law, -1);
			}
		}
		else if (hu->Combat != nullptr && hu->Combat->HullFraction() < BrigBreachHull)
		{
			Free(o, jd, cs::Cat("slips out through the breached brig of the ", holder->Name));
			if (holder == world->PilotedShip) Law()->Notice(cs::Cat("Your brig is breached: ", o->Name(), " has got away"));
		}
	}

	void Outlaws::SettleCaptives(double jd)
	{
		auto all = All.ToArray();
		for (const auto& o : all)
		{
			if (o->Fate != OutlawFate::Held || o->HeldBy == nullptr) continue;
			auto h = o->HeldBy;
			// The holder itself through a gate or under its own cruise drive (U.SPC-T453) is the same live hull,
			// never flagged GateDeparted; it is simply no longer in this world's own fleet (Fleet.IsPresent).
			if (h->GateDeparted || !world->Fleet->IsPresent(h))
			{
				End(o, OutlawFate::Captured, jd, cs::Cat("carried out of the system aboard the ", h->Name));
				continue;
			}
			if (h->Destroyed)
			{
				End(o, OutlawFate::Killed, jd, cs::Cat("lost with the ", h->Name));
				Lapse(o->Record, jd, "they were lost in a brig");
				continue;
			}
			if (h == world->PilotedShip || h->Battle != nullptr) continue;
			auto c = h->Controller();
			if (c->Phase != ShipPhase::Parked || c->Orbit == nullptr) continue;
			for (const auto& st : world->Economy->At(c->Orbit->Body))
				if (Deliver(o, st, jd) > 0 || o->Fate != OutlawFate::Held) break;
		}
	}

	int Outlaws::NearestPortFor(const cs::Ref<LawRecord>& rec, const cs::Ref<Ship>& ship, int here, double jd)
	{
		int best = -1;
		double bestKm = cs::Double::MaxValue;
		for (const auto& st : world->Economy->Settlements)
		{
			if (st->Fallen || st->Owner == nullptr || st->Owner->IsPirate || (rec->Wanted() && !rec->WantedBy(st->Owner))) continue;
			if (!world->Economy->CanTrade(ship, st)) continue;
			double km = st->Body == here ? 0 : (world->Ephemeris->HelioAt(st->Body, jd) - world->Ephemeris->HelioAt(here, jd)).Length();
			if (km < bestKm)
			{
				bestKm = km;
				best = st->Body;
			}
		}
		return best;
	}

	void Outlaws::ProveKills(const cs::Ref<Ship>& ship, double jd)
	{
		if (Claims.Count() == 0 || world->War == nullptr) return;
		for (int i = Claims.Count() - 1; i >= 0; i--)
		{
			auto c = Claims[i];
			if (c->Wreck == nullptr || c->Wreck->Expired(jd) || !world->War->Wrecks->Items.Contains(c->Wreck)) continue;
			if ((world->WreckHelio(c->Wreck, jd) - pilotHelio).Length() > ProofScanKm) continue;
			Claims.RemoveAt(i);
			auto rec = c->Outlaw->Record;
			double paid = Pay(rec, DeadShare, ship);
			world->Log(jd, cs::Cat("The ", ship->Name, " scans the wreck of the ", c->Wreck->Name, ": proof that ", c->Outlaw->Name(), " is dead, and ", Cr(paid), " cr in bounties paid"), WorldEventKind::Law, -1);
			Law()->Notice(cs::Cat("Wreck scanned: the kill of ", c->Outlaw->Name(), " is proven. ", Cr(paid), " cr paid, the dead rate"));
		}
	}

	void Outlaws::LapseClaims(double jd)
	{
		for (int i = Claims.Count() - 1; i >= 0; i--)
		{
			auto c = Claims[i];
			if (c->Wreck != nullptr && !c->Wreck->Expired(jd) && world->War != nullptr && world->War->Wrecks->Items.Contains(c->Wreck)) continue;
			Claims.RemoveAt(i);
			Lapse(c->Outlaw->Record, jd, "nobody scanned the wreck to prove the kill");
			Law()->Notice(cs::Cat("The wreck of ", c->Outlaw->Name(), "'s flagship is gone unscanned: no proof, no bounty"));
		}
	}

	bool Outlaws::WantsAlive(const cs::Ref<Ship>& hunter, const cs::Ref<Ship>& target)
	{
		if (!IsHunter(hunter) || target == nullptr) return false;
		cs::Ref<Outlaw> o;
		return byShip.TryGetValue(target, o) && o->Aboard() && o->Ship == target && o->Record->Wanted();
	}

	cs::Ref<LawRecord> Outlaws::RecordCommanding(const cs::Ref<Ship>& ship)
	{
		cs::Ref<Outlaw> o;
		return ship != nullptr && byShip.TryGetValue(ship, o) && o->Ship == ship ? o->Record : nullptr;
	}

	// ---------------------------------------------------------------- feared names

	void Outlaws::Fear(double jd)
	{
		for (const auto& o : All)
		{
			if (!o->AtLarge() || o->Feared || o->DaysAtLarge(jd) < FearedDays) continue;
			if (o->CrimeCount < FearedCrimes && o->Kills < FearedKills) continue;
			o->Feared = true;
			o->FearedJd = jd;
			o->Record->Notoriety += 25;
			for (const auto& f : o->Record->Bounty.Keys()) o->Record->Bounty[f] *= FearedBountyFactor;
			world->Log(jd, cs::Cat(o->Name(), " has run free for ", cs::Fmt(o->DaysAtLarge(jd), "0"), " days (", o->Deeds(), "): the name is feared on every lane, and the price on it rises to ", Cr(o->Record->TotalBounty()), " cr"),
				WorldEventKind::Law, -1);
		}
	}

	// ---------------------------------------------------------------- the boards

	cs::List<cs::Ref<BountyPosting>> Outlaws::BoardAt(const cs::Ref<Settlement>& port)
	{
		cs::List<cs::Ref<BountyPosting>> list;
		if (port == nullptr || port->Owner == nullptr || port->Owner->IsPirate || port->Fallen || Law() == nullptr) return list;
		for (const auto& p : Postings())
			if (p->Target->WantedBy(port->Owner))
			{
				p->PostedBy = port->Owner;
				p->Bounty = p->Target->BountyBy(port->Owner);
				list.Add(p);
			}
		list.Sort([](const cs::Ref<BountyPosting>& a, const cs::Ref<BountyPosting>& b) { return cs::Compare(b->Bounty, a->Bounty); });
		return list;
	}

	cs::List<cs::Ref<BountyPosting>> Outlaws::Postings()
	{
		cs::List<cs::Ref<BountyPosting>> list;
		if (Law() == nullptr) return list;
		auto add = [&](const cs::Ref<LawRecord>& r)
		{
			if (!r->Wanted()) return;
			auto o = Of(r);
			if (o != nullptr && !o->AtLarge()) return;
			// (A captain adrift in the pods or ashore is wanted all the same.)
			bool player = Law()->IsPlayer(r);
			if (!player && (o == nullptr || o->Aboard()) && (r->Captain == nullptr || r->Captain->Destroyed)) return;
			auto p = cs::New<BountyPosting>();
			p->Target = r;
			p->Player = player;
			p->Outlaw = o;
			p->Bounty = r->TotalBounty();
			p->TotalBounty = r->TotalBounty();
			p->LastSeen = SeenLast(r);
			p->HuntedBy = HunterOn(r);
			list.Add(p);
		};
		// (The player's is among the captains' now: the record of the hull they are embodied in, US-T148.)
		Law()->PlayerRecord(); // an older save's record taken up by that hull first
		for (const auto& r : Law()->Captains.Values()) add(r);
		list.Sort([](const cs::Ref<BountyPosting>& a, const cs::Ref<BountyPosting>& b) { return cs::Compare(b->TotalBounty, a->TotalBounty); });
		return list;
	}

	cs::Ref<BountyHunter> Outlaws::HunterOn(const cs::Ref<LawRecord>& r)
	{
		for (const auto& h : Hunters)
			if (h->Alive() && (h->Contract ? h->Contract->Target : nullptr) == r) return h;
		return nullptr;
	}

	cs::String Outlaws::Describe(const cs::Ref<BountyPosting>& p, double jd)
	{
		static const char* const Dot = "  \xC2\xB7  "; // "  ·  "
		auto o = p->Outlaw;
		cs::String who = o != nullptr ? o->Title() : p->IsPlayer() ? cs::String("YOU") : p->Target->Name;
		cs::String text = cs::Cat(who, Dot, Cr(p->Bounty), " cr");
		if (o != nullptr) text += cs::Cat(Dot, o->Deeds(), ", ", cs::Fmt(o->DaysAtLarge(jd), "0"), " d at large");
		else text += cs::Cat(Dot, p->Target->Crimes.Count(), p->Target->Crimes.Count() == 1 ? " crime" : " crimes", " on file");
		auto s = p->LastSeen;
		text += s != nullptr ? cs::Cat(Dot, "last seen ", s->Where, ", ", Ago(s->DaysAgo(jd)), !s->By.empty() ? cs::Cat(" by ", s->By) : cs::String("")) : cs::Cat(Dot, "not seen");
		if (p->HuntedBy != nullptr) text += cs::Cat(Dot, "hunted by the ", p->HuntedBy->Name());
		return text;
	}

	cs::String Outlaws::Ago(double days)
	{
		return days < 1 ? cs::String("today") : days < 2 ? cs::String("yesterday") : cs::Cat(cs::Fmt(days, "0"), " days ago");
	}

	// ---------------------------------------------------------------- hunters

	void Outlaws::Commission(double jd)
	{
		if (Guild() == nullptr || world->HunterCap <= 0) return;
		int alive = 0;
		// Every hunter still under its faction counts, wherever it is: one away on a hunt abroad (through a gate
		// or under cruise) is still a group, so the guild does not buy a second to stand beside it (U.SPC-T586).
		for (const auto& h : Hunters)
			if (!h->Retired && (!h->Ship->Destroyed || h->Ship->GateDeparted) && !h->Ship->Derelict && h->Ship->Faction == h->Faction) alive++;
		if (alive >= world->HunterCap) return;
		bool unhunted = false;
		for (const auto& p : Postings())
			if (p->TotalBounty >= MinContract && p->HuntedBy == nullptr && Huntable(p->Target)) unhunted = true;
		if (!unhunted) return;
		double cost = Economy::HullPrice(ShipClass::Cruiser) + Economy::HullPrice(ShipClass::Destroyer);
		if (Guild()->Treasury < cost + GuildReserve || rng.NextDouble() > CommissionChance) return;
		cs::Ref<Settlement> yard;
		for (const auto& st : world->Economy->Settlements)
		{
			if (!st->HasShipyard() || st->Fallen || st->Owner == nullptr || st->Owner->IsPirate || !world->Diplomacy()->CanDock(Guild(), st->Owner)) continue;
			if (yard == nullptr || st->Population > yard->Population) yard = st;
		}
		if (yard == nullptr) return;
		// Bought from the yard like any other hull: its owner is paid for them.
		Guild()->Treasury -= cost;
		if (yard->Owner != Guild()) yard->Owner->Treasury += cost;
		auto hunter = Commission(yard, jd);
		world->Log(jd, cs::Cat(yard->Name, " fits out the bounty hunter ", hunter->Name(), " for the ", Guild()->Name, ": there is money on the boards"), WorldEventKind::Launch, yard->Body);
	}

	cs::Ref<BountyHunter> Outlaws::Commission(const cs::Ref<Settlement>& yard, double jd)
	{
		auto fleet = world->Fleet;
		auto next = [this]()
		{
			const auto& names = HunterNames();
			int n = hunterNames++;
			return cs::Cat(names[n % names.Count()], n >= names.Count() ? cs::Cat(" ", n / names.Count() + 1) : cs::String(""));
		};
		auto lead = fleet->AddShip(next(), ShipClass::Cruiser, Guild(), ShipRole::Patrol);
		lead->Route = world->PortBodies();
		// A brig, to bring captains in alive for the full price (U.SPC-T63).
		Outfitting::Install(lead, ModuleKind::Brig);
		auto escort = fleet->AddShip(next(), ShipClass::Destroyer, Guild(), ShipRole::Escort);
		fleet->AddEscort(lead, escort, Fleet::SlotFor(0));
		// Out into a parking orbit of its own (not the yard's slip, where a new merchant may be sitting).
		fleet->Station(lead, yard->Body, jd);
		auto h = cs::New<BountyHunter>(lead, jd, world);
		Hunters.Add(h);
		byHunter[lead] = h;
		world->NotifyAdded(lead);
		return h;
	}

	void Outlaws::SetAway(const cs::Ref<Ship>& flagship, bool away)
	{
		cs::Ref<BountyHunter> h;
		if (flagship != nullptr && byHunter.TryGetValue(flagship, h)) h->Away = away;
	}

	void Outlaws::OnSuccession(const cs::Ref<Ship>& dead, const cs::Ref<Ship>& heir)
	{
		cs::Ref<BountyHunter> h;
		if (dead == nullptr || heir == nullptr || !byHunter.TryGetValue(dead, h)) return;
		auto next = cs::New<BountyHunter>(heir, h->SinceJd, world);
		next->Contract = h->Contract;
		next->Collected = h->Collected;
		next->Takes = h->Takes;
		byHunter.Remove(dead);
		Hunters.Add(next);
		byHunter[heir] = next;
		h->Contract = nullptr;
		h->Retired = true;
	}

	void Outlaws::ReviewContracts(double jd)
	{
		for (const auto& h : Hunters)
		{
			auto c = h->Contract;
			if (c == nullptr) continue;
			if (!h->Alive() || !Huntable(c->Target) || jd > c->UntilJd) h->Contract = nullptr;
		}
		PruneHunters();
	}

	void Outlaws::PruneHunters()
	{
		for (int i = Hunters.Count() - 1; i >= 0; i--)
		{
			auto h = Hunters[i];
			if (!h->Retired && (!h->Ship->Destroyed || h->Ship->GateDeparted || h->Away)) continue;
			Hunters.RemoveAt(i);
			cs::Ref<BountyHunter> cur;
			if (byHunter.TryGetValue(h->Ship, cur) && cur == h) byHunter.Remove(h->Ship);
		}
	}

	bool Outlaws::Huntable(const cs::Ref<LawRecord>& r)
	{
		if (r == nullptr || !r->Wanted() || r->TotalBounty() < MinContract * 0.5) return false;
		// The hull the player is embodied in (US-T148): its pilot is fair game while the player holds it.
		if (Law()->IsPlayer(r)) return !world->PilotedShip->Destroyed;
		auto o = Of(r);
		return o != nullptr && o->Aboard() && !o->Ship->Destroyed;
	}

	void Outlaws::RepairHunters()
	{
		for (const auto& s : world->Fleet->Ships())
		{
			if (s->Destroyed || s->Battle != nullptr || !IsHunter(s) || s->Combat->Damage() <= 0.001) continue;
			auto c = s->Controller();
			if (c->Phase != ShipPhase::Parked) continue;
			for (const auto& st : world->Economy->At(c->Orbit->Body))
			{
				if (!st->HasShipyard() || st->Fallen || st->Owner == nullptr || st->Owner->IsPirate || !world->Diplomacy()->CanDock(Guild(), st->Owner)) continue;
				double share = cs::Math::Min(0.2, s->Combat->Damage());
				s->Combat->Repair(share);
				double bill = Economy::HullPrice(s->Spec->Class) * share * 0.3;
				Guild()->Treasury -= bill;
				st->Owner->Treasury += bill;
				break;
			}
		}
	}

	int Outlaws::ChooseNext(const cs::Ref<Ship>& leader, int here, double jd)
	{
		auto h = HunterOf(leader);
		if (h == nullptr) return -1;
		// A captain in the brig goes to the nearest port that will pay for them first.
		auto held = All.Find([&leader](const cs::Ref<Outlaw>& o) { return o->Fate == OutlawFate::Held && o->HeldBy != nullptr && o->HeldBy->Controller() == leader; });
		if (held != nullptr)
		{
			int port = NearestPortFor(held->Record, leader, here, jd);
			if (port >= 0) return port;
		}
		if (leader->Combat->HullDamage() > RepairDamage || !leader->Combat->Armed())
		{
			int yard = NearestYard(here, jd);
			if (yard >= 0) return yard;
		}
		if (h->Contract != nullptr && (!Huntable(h->Contract->Target) || Outmatched(h, SeenLast(h->Contract->Target)))) h->Contract = nullptr;
		if (h->Contract == nullptr) h->Contract = TakeContract(h, here, jd);
		if (h->Contract != nullptr)
		{
			auto seen = SeenLast(h->Contract->Target);
			if (seen != nullptr && seen->Body >= 0) return seen->Body;
		}
		// Nothing to chase: the guns' jobs on the contracts boards, a patrol or an escort (U.SPC-T101).
		int job = world->Contracts != nullptr ? world->Contracts->ChooseForHunter(leader, here, jd) : -1;
		if (job >= 0) return job;
		const auto& ports = leader->Route;
		if (ports.Count() == 0) return here;
		return ports[rng.Next(ports.Count())];
	}

	bool Outlaws::Outmatched(const cs::Ref<BountyHunter>& h, const cs::Ref<Sighting>& seen)
	{
		if (seen == nullptr || seen->Body < 0) return false;
		// (C# evaluates the defence first, then the hunter's strength.)
		double defence = world->War->DefenceAt(seen->Body, h->Ship->Faction);
		double strength = world->War->GroupStrength(h->Ship);
		return defence > strength * 1.2;
	}

	cs::Ref<BountyContract> Outlaws::TakeContract(const cs::Ref<BountyHunter>& h, int here, double jd)
	{
		cs::Ref<BountyContract> best;
		double bestValue = 0;
		double strength = world->War->GroupStrength(h->Ship);
		for (const auto& p : Postings())
		{
			if (p->TotalBounty < MinContract || (p->HuntedBy != nullptr && p->HuntedBy != h) || !Huntable(p->Target)) continue;
			auto seen = p->LastSeen;
			if (seen == nullptr || seen->Body < 0 || jd - seen->Jd > StaleSightingDays) continue;
			if (p->Outlaw != nullptr)
			{
				// Its band, and whatever else would fight it where it was seen (a haven's guns, other raiders).
				double need = world->War->GroupStrength(p->Outlaw->Ship) * (p->Outlaw->Feared ? 1 : MatchShare);
				need = cs::Math::Max(need, world->War->DefenceAt(seen->Body, h->Ship->Faction) * (p->Outlaw->Feared ? 1 : MatchShare));
				if (strength < need) continue;
			}
			double days = seen->Body == here ? 0 : world->Fleet->EstimateTravelDays(h->Ship, here, seen->Body, jd);
			double value = p->TotalBounty / (days + 3);
			if (value > bestValue)
			{
				bestValue = value;
				best = cs::New<BountyContract>();
				best->Target = p->Target;
				best->TakenJd = jd;
				best->UntilJd = jd + ContractDays;
			}
		}
		if (best != nullptr)
		{
			auto o = Of(best->Target);
			bool player = Law()->IsPlayer(best->Target);
			cs::String who = o != nullptr ? o->Title() : player ? cs::String("the player's ship") : best->Target->Name;
			world->Log(jd, cs::Cat("The bounty hunter ", h->Name(), " takes the contract on ", who, " (", Cr(best->Target->TotalBounty()), " cr)"), WorldEventKind::Law, -1);
			if (player) Law()->Notice(cs::Cat("The bounty hunter ", h->Name(), " has taken the contract on you: ", Cr(best->Target->TotalBounty()), " cr on your head"));
		}
		return best;
	}

	int Outlaws::NearestYard(int here, double jd)
	{
		int best = -1;
		double bestKm = cs::Double::MaxValue;
		for (const auto& st : world->Economy->Settlements)
		{
			if (!st->HasShipyard() || st->Fallen || st->Owner == nullptr || st->Owner->IsPirate || !world->Diplomacy()->CanDock(Guild(), st->Owner)) continue;
			double km = st->Body == here ? 0 : (world->Ephemeris->HelioAt(st->Body, jd) - world->Ephemeris->HelioAt(here, jd)).Length();
			if (km < bestKm)
			{
				bestKm = km;
				best = st->Body;
			}
		}
		return best;
	}

	cs::Ref<Battle> Outlaws::UpdatePilot(const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, DVec3 forward, double jd)
	{
		if (ship == nullptr || ship->Destroyed || world->War == nullptr) return nullptr;
		pilotBody = body;
		pilotHelio = world->Ephemeris->HelioAt(body, jd) + offsetKm;
		pilotJd = jd;
		ProveKills(ship, jd);
		// The pilot of the hull the player flies (US-T148): a hunter's contract is on them, not on the SpaceGod.
		auto rec = Law()->FindRecord(ship);
		if (ship->Battle != nullptr || rec == nullptr || !rec->Wanted()) return nullptr;
		for (const auto& h : Hunters)
		{
			// A hunter off abroad through a gate or its own cruise drive (U.SPC-T453) reads as not Alive here
			// (Fleet.IsPresent): nothing here gives a position for it to engage from.
			if (!h->Alive() || (h->Contract ? h->Contract->Target : nullptr) != rec || h->Ship->Battle != nullptr || h->Ship == ship) continue;
			if ((world->Fleet->StateOf(h->Ship, jd).HelioKm - pilotHelio).Length() > EngageKm) continue;
			auto b = world->War->TryStartHunt(ship, h->Ship, body, offsetKm, forward, jd);
			if (b == nullptr) continue;
			Law()->Notice(cs::Cat("Bounty hunter ", h->Name(), ": \"", ship->Name, ", there's ", Cr(rec->TotalBounty()), " on your head. Nothing personal.\""));
			return b;
		}
		return nullptr;
	}

	cs::String Outlaws::Cap(const cs::String& s) { return s.empty() ? s : cs::Cat(cs::Strings::ToUpperInvariant(s.substr(0, 1)), cs::Strings::Substring(s, 1)); }

	cs::String Outlaws::Cr(double v) { return cs::Fmt(v, "N0"); }

	// ================================================================ ObjectGraph registrations (docs/PERSISTENCE.md)

	SG_ENUM(OutlawFate, "Unity.SpaceGod.Core.OutlawFate")

	SG_SERIALIZE(Sighting, "Unity.SpaceGod.Core.Sighting",
		SG_F(Body), SG_F(Jd), SG_F(By), SG_F(Where))

	SG_SERIALIZE(Outlaw, "Unity.SpaceGod.Core.Outlaw",
		SG_F(Record), SG_P(Ship), SG_P(Haven), SG_F(Pod), SG_F(Ashore), SG_F(FlagshipAway), SG_F(HeldBy), SG_F(SinceJd),
		SG_F(ShipsRobbed), SG_F(Kills), SG_F(Ransoms), SG_F(Battles), SG_F(CrimeCount), SG_F(Fate), SG_F(FateJd), SG_F(FateBy),
		SG_F(Feared), SG_F(FearedJd))

	SG_SERIALIZE(BountyHunter, "Unity.SpaceGod.Core.BountyHunter",
		SG_F(Ship), SG_F(SinceJd), SG_F(Contract), SG_F(Collected), SG_F(Takes), SG_F(world), SG_F(Retired), SG_F(Away), SG_F(Faction))

	SG_SERIALIZE(BountyContract, "Unity.SpaceGod.Core.BountyContract",
		SG_F(Target), SG_F(TakenJd), SG_F(UntilJd))

	SG_SERIALIZE(KillClaim, "Unity.SpaceGod.Core.KillClaim",
		SG_F(Outlaw), SG_F(Wreck), SG_F(Jd), SG_F(Battle))

	SG_SERIALIZE(BountyPosting, "Unity.SpaceGod.Core.BountyPosting",
		SG_F(Target), SG_F(Outlaw), SG_F(PostedBy), SG_F(Bounty), SG_F(TotalBounty), SG_F(LastSeen), SG_F(HuntedBy),
		SG_OPT(Player)) // US-T148: absent from older saves

	SG_SERIALIZE(Outlaws, "Unity.SpaceGod.Core.Outlaws",
		SG_F(world), SG_F(rng), SG_F(All), SG_F(Hunters), SG_F(LastSeen), SG_F(byShip), SG_F(byRecord), SG_F(byHunter),
		SG_F(nextDailyJd), SG_F(hunterNames), SG_F(pilotBody), SG_F(pilotHelio), SG_F(pilotJd), SG_F(Claims))
}
