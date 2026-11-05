// Port of Unity.SpaceGod Assets/UnitySpace/Core/Sensors/Sensors.cs (see docs/MIGRATION.md).

#include "Sensors/Sensors.h"
#include "Sensors/SensorPosture.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"

#include "Astro.h"
#include "Combat/CombatState.h"
#include "Combat/ElectronicWarfare.h"
#include "Combat/Facilities.h"
#include "Combat/FleetCoordination.h"
#include "Combat/Outfitting.h"
#include "Combat/Pursuit.h"
#include "Combat/War.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Information/LightSpeed.h"
#include "Ships.h"
#include "World.h"
#include "Systems/SpaceFeatures.h"

namespace SpaceGod::Core
{
	const char* ToString(ContactLevel v)
	{
		switch (v)
		{
			case ContactLevel::None: return "None";
			case ContactLevel::Blip: return "Blip";
			case ContactLevel::Class: return "Class";
			case ContactLevel::Identified: return "Identified";
		}
		return "";
	}

	// ---------------------------------------------------------------- Signatures

	bool Signatures::Silent(const cs::Ref<Ship>& s)
	{
		return s != nullptr && (s->SilentRunning || (s->Faction != nullptr && s->Faction->IsPirate && s->Controller()->Ambushing()));
	}

	bool Signatures::Transponder(const cs::Ref<Ship>& s)
	{
		// A hull powered down (US-T93, only ever the player's) squawks nothing.
		return s != nullptr && s->Faction != nullptr && !s->Faction->IsPirate && !s->Derelict && !Silent(s) && !SensorPosture::Offline(s->Combat);
	}

	double Signatures::Of(const cs::Ref<Ship>& s, bool burning, cs::Nullable<bool> firing)
	{
		if (s == nullptr || s->Destroyed || s->GateDeparted || s->Controller()->Planetside()) return 0;
		double mass = cs::Math::Max(1.0, s->DryMassTonnes());
		double ratio = mass / ReferenceMassTonnes;
		double hull = cs::Math::Pow(ratio, 2.0 / 3.0);
		if (s->Derelict) return hull * HulkFactor;
		bool silent = Silent(s);
		double em = silent ? 0 : s->Spec->IsMilitary && (s->Combat == nullptr || s->Combat->Armed()) ? MilitaryEm : CivilEm;
		// US-T93, the player's power-down and ping (a crew's hull never has either, so its sum is the original's):
		// dark, the hull's residual heat alone, no EM, no drive, no guns; restarting, the heat and EM climbing back.
		const ShipCombat* c = s->Combat.get();
		double ping = SensorPosture::Lit(c) ? SensorPosture::PingSignature : 0;
		if (SensorPosture::Offline(c))
		{
			bool restarting = c->PowerDown == static_cast<int>(PowerState::Restarting);
			return hull * c->ResidualHeat + (restarting ? em * c->ResidualHeat : 0) + ping;
		}
		if (silent) hull *= SilentFactor;
		double drive = burning ? DriveFactor * ratio * s->Spec->AccelG / 0.1 : 0;
		double weapons = (firing.has_value() ? *firing : s->Battle != nullptr) ? WeaponsFire : 0;
		double sum = hull + em + drive + weapons;
		if (ping > 0) sum += ping;
		return sum;
	}

	// ---------------------------------------------------------------- SensorSuite

	cs::Dictionary<ShipClass, SensorSuite>& SensorSuite::baseByClass()
	{
		static cs::Dictionary<ShipClass, SensorSuite> value = []
		{
			cs::Dictionary<ShipClass, SensorSuite> d;
			d[ShipClass::GatePicket] = Picket;
			d[ShipClass::Corvette] = Patrol;
			return d;
		}();
		return value;
	}

	void SensorSuite::RegisterBase(ShipClass cls, SensorSuite suite) { baseByClass()[cls] = suite; }

	SensorSuite SensorSuite::BaseOf(ShipClass cls, bool military)
	{
		SensorSuite b;
		return baseByClass().TryGetValue(cls, b) ? b : military ? Military : Civil;
	}

	SensorSuite SensorSuite::Of(const cs::Ref<Ship>& s)
	{
		if (s == nullptr) return Civil;
		auto b = s->Spec == nullptr ? Civil : BaseOf(s->Spec->Class, s->Spec->IsMilitary);
		double n = (s->Loadout ? s->Loadout->Count(ModuleKind::Scanner) : 0) + ArrayScanners * FleetCoordination::Count(s, FacilityKinds::SensorArray)
		           + ElectronicWarfare::SensorScanners * ElectronicWarfare::ReconAboard(s);
		return SensorSuite(b.RangeKm * (1 + ScannerRangeShare * n), b.Resolution + ScannerResolution * n);
	}

	// ---------------------------------------------------------------- ContactSighting / Contact

	double ContactSighting::LagSeconds() const { return (ObservedJd - SeenJd) * Astro::SecondsPerDay; }

	double ContactSighting::ArrivesAt(DVec3 where) const { return ObservedJd + LightSpeed::DelayDays(ObserverHelioKm, where); }

	double Contact::LagSeconds() const { return (LastSeenJd - SeenJd) * Astro::SecondsPerDay; }

	cs::String Contact::Describe(const cs::Ref<SpaceGod::Core::Ship>& s, ContactLevel level)
	{
		switch (level)
		{
			case ContactLevel::Identified: return s->Name;
			case ContactLevel::Class: return cs::Cat("unidentified ", cs::Strings::ToLowerInvariant(s->Spec->DisplayName));
			case ContactLevel::Blip: return "unknown contact";
			default: return "nothing";
		}
	}

	// ---------------------------------------------------------------- ContactBook

	cs::Ref<Contact> ContactBook::Of(const cs::Ref<Ship>& s) const
	{
		cs::Ref<Contact> c;
		return s != nullptr && contacts.TryGetValue(s, c) ? c : nullptr;
	}

	cs::Nullable<ContactSighting> ContactBook::KnownAt(const cs::Ref<Ship>& s, DVec3 where, double jd) const
	{
		auto c = Of(s);
		if (c == nullptr) return std::nullopt;
		for (int i = c->Sightings.Count() - 1; i >= 0; i--)
			if (c->Sightings[i].ArrivesAt(where) <= jd) return c->Sightings[i];
		return std::nullopt;
	}

	ContactLevel ContactBook::LevelOf(const cs::Ref<Ship>& s) const
	{
		auto c = Of(s);
		return c ? c->Level : ContactLevel::None;
	}

	int ContactBook::CountLive(ContactLevel level) const
	{
		int n = 0;
		for (const auto& c : contacts.Values())
			if (!c->Cold() && c->Level == level) n++;
		return n;
	}

	void ContactBook::Observe(const cs::Ref<Ship>& s, ContactLevel level, DVec3 helio, double signature, double jd)
	{
		Observe(s, level, helio, signature, jd, helio, jd, -1);
	}

	void ContactBook::Observe(const cs::Ref<Ship>& s, ContactLevel level, DVec3 helio, double signature, double jd, DVec3 observer, double seenJd, int body)
	{
		cs::Ref<Contact> c;
		contacts.TryGetValue(s, c);
		if (level == ContactLevel::None)
		{
			if (c != nullptr) c->Now = ContactLevel::None;
			return;
		}
		if (c == nullptr)
		{
			c = cs::New<Contact>();
			c->Ship = s;
			c->FirstSeenJd = jd;
			contacts[s] = c;
		}
		c->Now = level;
		if (level > c->Level) c->Level = level;
		c->LastSeenJd = jd;
		c->SeenJd = seenJd;
		c->LastHelioKm = helio;
		c->Signature = signature;
		if (c->Sightings.Count() >= SightingsKept) c->Sightings.RemoveAt(0);
		c->Sightings.Add(ContactSighting(jd, seenJd, helio, observer, body, level));
	}

	cs::List<cs::Ref<Ship>> ContactBook::Expire(double jd, double holdDays)
	{
		cs::List<cs::Ref<Ship>> lost;
		for (auto kv : contacts)
			// (A ship gone through a jump gate is never swept again, so its track would never go cold.)
			if (kv.first->Destroyed || kv.first->GateDeparted || (kv.second->Cold() && jd - kv.second->LastSeenJd > holdDays)) lost.Add(kv.first);
		for (const auto& s : lost) contacts.Remove(s);
		return lost;
	}

	// ---------------------------------------------------------------- Sensors

	cs::Ref<ContactBook> Sensors::PictureOf(const cs::Ref<Faction>& f)
	{
		if (f == nullptr) return nullptr;
		cs::Ref<ContactBook> book;
		if (!pictures.TryGetValue(f, book))
		{
			book = cs::New<ContactBook>();
			pictures[f] = book;
		}
		return book;
	}

	ContactLevel Sensors::Resolve(SensorSuite suite, double signature, double distanceKm, bool transponder)
	{
		double reach = suite.ReachKm(signature);
		if (reach <= 0 || distanceKm > reach) return ContactLevel::None;
		if (transponder) return ContactLevel::Identified;
		double q = reach / cs::Math::Max(distanceKm, 1e-3);
		double res = cs::Math::Max(suite.Resolution, 1e-6);
		if (q >= IdentifyQuality / res) return ContactLevel::Identified;
		if (q >= ClassQuality / res) return ContactLevel::Class;
		return ContactLevel::Blip;
	}

	bool Sensors::Burning(const cs::Ref<Ship>& s, double jd)
	{
		DVec3 discard;
		return Burning(s, jd, discard);
	}

	bool Sensors::Burning(const cs::Ref<Ship>& s, double jd, DVec3& helio)
	{
		if (s == pilotShip && s == world->PilotedShip)
		{
			helio = pilotHelio;
			return pilotBurning;
		}
		// A group running the player down is where the chase has it, its drive lit (U.SPC-T273).
		cs::Nullable<DVec3> chasing = world->Pursuit != nullptr && world->Pursuit->Active() ? world->Pursuit->HelioOf(s, jd) : std::nullopt;
		if (chasing.has_value())
		{
			helio = *chasing;
			return true;
		}
		auto st = world->Fleet->StateOf(s, jd);
		helio = st.HelioKm;
		auto c = s->Controller();
		return st.Burning && c->Phase == ShipPhase::Transit;
	}

	DVec3 Sensors::HelioOf(const cs::Ref<Ship>& s, double jd)
	{
		DVec3 helio;
		Burning(s, jd, helio);
		return helio;
	}

	void Sensors::Record(const cs::Ref<ContactBook>& book, const cs::Ref<Ship>& s, ContactLevel level, DVec3 helioNow, DVec3 observer, double signature, double jd)
	{
		if (level == ContactLevel::None)
		{
			book->Observe(s, level, helioNow, signature, jd);
			return;
		}
		double lag = LightSpeed::DelayDays(helioNow, observer);
		double seen = jd - lag;
		auto helio = helioNow;
		auto c = s->Controller();
		if (lag > 0 && !(c == pilotShip && c == world->PilotedShip)) helio = world->Fleet->StateOf(c, seen).HelioKm;
		book->Observe(s, level, helio, signature, jd, observer, seen, ParkedAt(c));
	}

	int Sensors::ParkedAt(const cs::Ref<Ship>& s)
	{
		return (s->Phase == ShipPhase::Parked || s->Phase == ShipPhase::Landed) && s->Orbit != nullptr ? s->Orbit->Body : -1;
	}

	cs::Nullable<ContactSighting> Sensors::KnownAt(const cs::Ref<Faction>& f, const cs::Ref<Ship>& target, DVec3 where, double jd)
	{
		if (f == nullptr || target == nullptr) return std::nullopt;
		if (target->Faction == f)
		{
			auto h = HelioOf(target, jd);
			return ContactSighting(jd, jd, h, h, ParkedAt(target->Controller()), ContactLevel::Identified);
		}
		return PictureOf(f)->KnownAt(target, where, jd);
	}

	double Sensors::SignatureOf(const cs::Ref<Ship>& s, double jd) { return Signatures::Of(s, Burning(s, jd)); }

	ContactLevel Sensors::Detect(const cs::Ref<Ship>& observer, const cs::Ref<Ship>& target, double jd)
	{
		if (observer == nullptr || target == nullptr || observer == target) return ContactLevel::None;
		// The C# operands in their left-to-right order (each look may step a pursuit or the fleet's caches).
		auto from = HelioOf(observer, jd);
		auto to = HelioOf(target, jd);
		double d = (from - to).Length();
		auto suite = GroupSuite(observer);
		double sig = SignatureOf(target, jd);
		auto level = Resolve(suite, sig, d, Signatures::Transponder(target));
		// Line of sight (US-T93), only where the player is one end of it.
		if (level != ContactLevel::None && (IsPilot(observer) || IsPilot(target)) && PilotOccluded(IsPilot(observer) ? to : from, jd)) return ContactLevel::None;
		return level;
	}

	SensorSuite Sensors::GroupSuite(const cs::Ref<Ship>& s)
	{
		auto leader = s->Controller();
		auto suite = SensorSuite::Of(leader);
		for (const auto& e : world->Fleet->Ships())
			if (e->Leader == leader && !e->Destroyed) suite = SensorSuite::Best(suite, SensorSuite::Of(e));
		return suite;
	}

	ContactLevel Sensors::Knows(const cs::Ref<Faction>& f, const cs::Ref<Ship>& target, double jd)
	{
		if (f == nullptr || target == nullptr || target->Destroyed) return ContactLevel::None;
		if (target->Faction == f) return ContactLevel::Identified;
		auto book = PictureOf(f);
		auto c = book->Of(target);
		if (c != nullptr && !c->Cold() && c->LastSeenJd >= lastSweepJd && jd - c->LastSeenJd <= SweepDays * 1.5) return c->Level;
		DVec3 helio;
		bool burning = Burning(target, jd, helio);
		double sig = Signatures::Of(target, burning);
		bool squawk = Signatures::Transponder(target);
		auto best = ContactLevel::None;
		auto by = helio;
		double byKm2 = cs::Double::MaxValue;
		bool pilot = IsPilot(target);
		for (const auto& o : Observers(jd))
		{
			if (o.Faction != f) continue;
			double d2 = (o.Helio - helio).LengthSquared();
			auto level = Resolve(o.Suite, sig, cs::Math::Sqrt(d2), squawk);
			// Line of sight to the player's hull (US-T93): a body between hides it.
			if (pilot && level != ContactLevel::None && PilotOccluded(o.Helio, jd)) continue;
			// The best look, and of those the nearest: the one with the least light delay.
			if (level != ContactLevel::None && (level > best || (level == best && d2 < byKm2)))
			{
				best = level;
				by = o.Helio;
				byKm2 = d2;
			}
		}
		if (best != ContactLevel::None) Record(book, target, best, helio, by, sig, jd);
		return book->LevelOf(target);
	}

	// ---------------------------------------------------------------- sweeps

	cs::List<Sensors::Observer> Sensors::Observers(double jd)
	{
		cs::List<Observer> list;
		cs::Dictionary<cs::Ref<Ship>, SensorSuite> suites;
		for (const auto& s : world->Fleet->Ships())
		{
			if (!Looks(s)) continue;
			auto c = s->Controller();
			auto suite = SensorSuite::Of(s);
			SensorSuite had;
			auto merged = suites.TryGetValue(c, had) ? SensorSuite::Best(had, suite) : suite;
			suites[c] = merged;
		}
		for (auto kv : suites)
			if (Looks(kv.first)) list.Add(Observer(kv.first->Faction, HelioOf(kv.first, jd), kv.second));
		for (const auto& st : world->Economy->Settlements)
			if (!st->Fallen && st->Owner != nullptr && st->Population > 0)
				list.Add(Observer(st->Owner, world->Ephemeris->HelioAt(st->Body, jd), SensorSuite::Station));
		return list;
	}

	bool Sensors::Looks(const cs::Ref<Ship>& s)
	{
		return !s->Destroyed && !s->GateDeparted && !s->Derelict && s->Faction != nullptr && !s->Controller()->Planetside();
	}

	void Sensors::Update(double jd)
	{
		if (jd < nextSweepJd) return;
		Sweep(jd);
		nextSweepJd = jd + SweepDays;
	}

	void Sensors::Sweep(double jd)
	{
		lastSweepJd = jd;
		auto observers = Observers(jd);
		cs::HashSet<cs::Ref<Faction>> factions;
		// Each faction's own lookers, so a target is tried against them alone and not every faction's.
		cs::Dictionary<cs::Ref<Faction>, cs::List<Observer>> lookers;
		for (const auto& o : observers)
		{
			factions.Add(o.Faction);
			lookers[o.Faction].Add(o); // C#: TryGetValue, else a new list stored under the key; then Add.
		}
		// A faction left with nobody looking still lets its old tracks go cold and be lost.
		factions.UnionWith(pictures.Keys());
		cs::List<Target> targets;
		// (helio, burning) of each group's leader
		cs::Dictionary<cs::Ref<Ship>, std::tuple<DVec3, bool>> stateOfLeader;
		for (const auto& s : world->Fleet->Ships())
		{
			if (s->Destroyed || s->GateDeparted || s->Faction == nullptr) continue;
			auto c = s->Controller();
			std::tuple<DVec3, bool> st;
			if (!stateOfLeader.TryGetValue(c, st))
			{
				DVec3 h;
				bool b = Burning(c, jd, h);
				st = std::make_tuple(h, b);
				stateOfLeader[c] = st;
			}
			double sig = Signatures::Of(s, std::get<1>(st));
			targets.Add(Target(s, std::get<0>(st), sig, Signatures::Transponder(s)));
		}
		for (const auto& f : factions)
		{
			auto book = PictureOf(f);
			const cs::List<Observer>* mineList = lookers.ContainsKey(f) ? &lookers.At(f) : nullptr;
			for (const auto& t : targets)
			{
				if (t.Ship->Faction == f) continue;
				auto best = ContactLevel::None;
				auto by = t.Helio;
				double byKm2 = cs::Double::MaxValue;
				bool pilot = IsPilot(t.Ship);
				if (t.Signature > 0 && mineList != nullptr)
					for (const auto& o : *mineList)
					{
						double reach = o.Suite.RangeKm * t.Root;
						double d2 = (o.Helio - t.Helio).LengthSquared();
						if (d2 > reach * reach) continue;
						// Line of sight to the player's hull (US-T93): a body between hides it. No crew's look is changed.
						if (pilot && PilotOccluded(o.Helio, jd)) continue;
						auto level = Resolve(o.Suite, t.Signature, cs::Math::Sqrt(d2), t.Transponder);
						// The best look, and of those the nearest: the one with the least light delay.
						if (level != ContactLevel::None && (level > best || (level == best && d2 < byKm2)))
						{
							best = level;
							by = o.Helio;
							byKm2 = d2;
						}
					}
				Record(book, t.Ship, best, t.Helio, by, t.Signature, jd);
			}
			auto lost = book->Expire(jd, FactionHoldDays);
			if (lost.Count() > 0 && Lost.HasHandlers()) for (const auto& s : lost) Lost.Invoke(f, s);
		}
		TrackHome(jd);
	}

	void Sensors::TrackHome(double jd)
	{
		auto war = world->War;
		if (war == nullptr) return;
		cs::Dictionary<int, cs::Ref<Settlement>> hidden;
		for (const auto& st : world->Economy->Settlements)
			if (st->Hidden && !st->Fallen) hidden[st->Body] = st;
		if (hidden.Count() == 0) return;
		for (auto kv : pictures)
		{
			auto f = kv.first;
			if (!f->IsMilitary || f->IsPirate) continue;
			for (const auto& c : kv.second->Contacts())
			{
				if (c->Cold() || c->Level < ContactLevel::Class) continue;
				auto raider = c->Ship->Controller();
				if (raider->Faction == nullptr || !raider->Faction->IsPirate) continue;
				int body = raider->Phase == ShipPhase::Parked ? raider->Orbit->Body : raider->Phase == ShipPhase::Transit ? raider->DestinationBody : -1;
				cs::Ref<Settlement> haven;
				if (body < 0 || !hidden.TryGetValue(body, haven) || haven->KnownTo.Contains(f)) continue;
				cs::String who = c->Level == ContactLevel::Identified ? cs::Cat("the ", raider->Name) : cs::Cat("an unidentified ", cs::Strings::ToLowerInvariant(raider->Spec->DisplayName));
				war->Discover(haven, f, jd, raider->Phase == ShipPhase::Parked
					? cs::Cat("its sensors hold ", who, " parked there")
					: cs::Cat("its sensors track ", who, " burning in to it"));
			}
		}
	}

	void Sensors::UpdatePilot(const cs::Ref<Ship>& ship, DVec3 helioKm, bool burning, double jd)
	{
		if (ship == nullptr || ship->Destroyed)
		{
			pilotShip = nullptr;
			Player->Clear();
			return;
		}
		if (ship != pilotShip) Player->Clear();
		pilotShip = ship;
		pilotHelio = helioKm;
		pilotBurning = burning;
		if (jd < nextPlayerSweepJd && jd >= nextPlayerSweepJd - PlayerSweepSeconds * 2 / Astro::SecondsPerDay) return;
		nextPlayerSweepJd = jd + PlayerSweepSeconds / Astro::SecondsPerDay;
		// Passive by default; half its reach dark or restarting, the ping's burst just after one (US-T93).
		auto suite = PilotSuite(ship);
		// US-T130: a nebula swallows sensors: inside one the helm's reach (passive, and a ping's burst alike) falls short,
		// and a contact inside one is as hard to make out from anywhere.
		const cs::String root = world->Catalog()->Root()->Name;
		suite = SensorSuite(suite.RangeKm * SpaceFeatures::SensorMask(root, helioKm), suite.Resolution);
		// (helio, burning) of each group's leader
		cs::Dictionary<cs::Ref<Ship>, std::tuple<DVec3, bool>> seen;
		for (const auto& s : world->Fleet->Ships())
		{
			if (s == ship || s->Destroyed || s->GateDeparted || s->Faction == nullptr) continue;
			auto c = s->Controller();
			std::tuple<DVec3, bool> st;
			if (!seen.TryGetValue(c, st))
			{
				DVec3 h;
				bool b = Burning(c, jd, h);
				st = std::make_tuple(h, b);
				seen[c] = st;
			}
			double sig = Signatures::Of(s, std::get<1>(st));
			double hidden = SpaceFeatures::SensorMask(root, std::get<0>(st));
			auto level = Resolve(suite, sig, (std::get<0>(st) - helioKm).Length() / hidden, Signatures::Transponder(s));
			// Line of sight (US-T93): a body or rock between the hull and the contact hides it.
			if (level != ContactLevel::None && PilotOccluded(std::get<0>(st), jd)) level = ContactLevel::None;
			Record(Player, s, level, std::get<0>(st), helioKm, sig, jd);
		}
		auto lost = Player->Expire(jd, PlayerHoldSeconds / Astro::SecondsPerDay);
		if (lost.Count() > 0 && Lost.HasHandlers()) for (const auto& s : lost) Lost.Invoke(nullptr, s);
	}

	double Sensors::PilotSignature() const { return pilotShip != nullptr ? Signatures::Of(pilotShip, pilotBurning) : 0; }

	cs::String Sensors::FindWitness(DVec3 helioKm, double jd, double signature, const cs::Func<bool, cs::Ref<Ship>>& exclude)
	{
		double root = cs::Math::Sqrt(cs::Math::Max(0.0, signature));
		// A deed at the player's own hull (US-T93): a witness has to have it in line of sight.
		bool atPilot = pilotShip != nullptr && pilotShip == world->PilotedShip && (helioKm - pilotHelio).LengthSquared() < WitnessAtPilotKm * WitnessAtPilotKm;
		for (const auto& s : world->Economy->Settlements)
		{
			if (s->Fallen || s->Owner == nullptr || s->Owner->IsPirate || s->Population <= 0) continue;
			auto at = world->Ephemeris->HelioAt(s->Body, jd);
			if ((at - helioKm).Length() <= SensorSuite::Station.RangeKm * root && !(atPilot && PilotOccluded(at, jd))) return s->Name;
		}
		for (const auto& ship : world->Fleet->Ships())
		{
			if (ship->Destroyed || ship->GateDeparted || ship->Derelict || ship->Faction == nullptr || ship->Faction->IsPirate || ship->Controller()->Planetside()) continue;
			if (ship == world->PilotedShip || (exclude != nullptr && exclude(ship))) continue;
			auto at = world->Fleet->StateOf(ship, jd).HelioKm;
			if ((at - helioKm).Length() <= SensorSuite::Of(ship).RangeKm * root && !(atPilot && PilotOccluded(at, jd))) return cs::Cat("the ", ship->Name);
		}
		return cs::String();
	}

	// ---------------------------------------------------------------- the player's ping and line of sight (US-T93)

	bool Sensors::IsPilot(const cs::Ref<Ship>& target) const
	{
		return pilotShip != nullptr && target == pilotShip && target == world->PilotedShip;
	}

	SensorSuite Sensors::PilotSuite(const cs::Ref<Ship>& ship)
	{
		auto suite = SensorSuite::Of(ship);
		const ShipCombat* c = ship != nullptr ? ship->Combat.get() : nullptr;
		if (SensorPosture::Offline(c)) return SensorSuite(suite.RangeKm * SensorPosture::DarkSensorShare, suite.Resolution);
		if (SensorPosture::Bursting(c)) return SensorSuite(suite.RangeKm * SensorPosture::PingRangeFactor, suite.Resolution * SensorPosture::PingResolutionFactor);
		return suite;
	}

	void Sensors::PilotPinged(const cs::Ref<Ship>& ship, double jd)
	{
		if (!IsPilot(ship)) return;
		// Its own picture: swept at the next report, with the burst.
		nextPlayerSweepJd = cs::Double::NegativeInfinity;
		// Everyone else's: the flash is heard now by every faction with a ship or station in reach and in sight of it.
		double sig = Signatures::Of(ship, pilotBurning);
		bool squawk = Signatures::Transponder(ship);
		cs::Dictionary<cs::Ref<Faction>, std::tuple<ContactLevel, DVec3, double>> best;
		for (const auto& o : Observers(jd))
		{
			if (o.Faction == nullptr || o.Faction == ship->Faction) continue;
			double d2 = (o.Helio - pilotHelio).LengthSquared();
			auto level = Resolve(o.Suite, sig, cs::Math::Sqrt(d2), squawk);
			if (level == ContactLevel::None || PilotOccluded(o.Helio, jd)) continue;
			std::tuple<ContactLevel, DVec3, double> had;
			bool has = best.TryGetValue(o.Faction, had);
			if (!has || level > std::get<0>(had) || (level == std::get<0>(had) && d2 < std::get<2>(had))) best[o.Faction] = std::make_tuple(level, o.Helio, d2);
		}
		for (auto kv : best) Record(PictureOf(kv.first), ship, std::get<0>(kv.second), pilotHelio, std::get<1>(kv.second), sig, jd);
	}

	void Sensors::ReportPilotRocks(const cs::List<Occluder>& rocks, double jd)
	{
		pilotRocks = rocks;
		pilotRocksJd = jd;
	}

	bool Sensors::Blocks(DVec3 a, DVec3 b, const Occluder& o)
	{
		double r2 = o.RadiusKm * o.RadiusKm;
		DVec3 ac = o.HelioKm - a;
		// An end inside the body (a settlement at its world's centre, a hull down in it) is not hidden by it.
		if (ac.LengthSquared() <= r2 || (o.HelioKm - b).LengthSquared() <= r2) return false;
		DVec3 ab = b - a;
		double len2 = ab.LengthSquared();
		if (len2 <= 0) return false;
		double t = DVec3::Dot(ac, ab) / len2;
		if (t <= 0 || t >= 1) return false;
		return (ac - ab * t).LengthSquared() < r2;
	}

	bool Sensors::PilotOccluded(DVec3 other, double jd)
	{
		if (pilotShip == nullptr) return false;
		auto catalog = world->Catalog();
		if (catalog == nullptr || world->Ephemeris == nullptr) return false;
		// The catalogue's bodies, placed once a date (a sweep asks about many lines at the same moment).
		if (!(jd == bodyOccludersJd))
		{
			bodyOccluders.Clear();
			for (int i = 0; i < catalog->Count(); i++)
			{
				auto b = (*catalog)[i];
				if (b == nullptr || b->RadiusKm < MinOccluderRadiusKm) continue;
				if (b->Kind != BodyKind::Star && b->Kind != BodyKind::Planet && b->Kind != BodyKind::DwarfPlanet && b->Kind != BodyKind::Moon && b->Kind != BodyKind::Asteroid) continue;
				bodyOccluders.Add(Occluder(world->Ephemeris->HelioAt(i, jd), b->RadiusKm));
			}
			bodyOccludersJd = jd;
		}
		for (const auto& o : bodyOccluders)
			if (Blocks(pilotHelio, other, o)) return true;
		// The belt's rocks round the hull, as the helm last reported them, while the report is fresh.
		if (cs::Math::Abs(jd - pilotRocksJd) * Astro::SecondsPerDay <= RockHoldSeconds)
			for (const auto& o : pilotRocks)
				if (Blocks(pilotHelio, other, o)) return true;
		return false;
	}

	double Sensors::HavenReachKm(const cs::Ref<Ship>& s) { return SensorSuite::Of(s).ReachKm(Signatures::Haven); }
}
