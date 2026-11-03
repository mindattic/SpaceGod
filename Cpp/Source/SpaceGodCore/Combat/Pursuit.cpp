#include "Combat/Pursuit.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Combat/War.h"
#include "Sensors/Sensors.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	const char* ToString(PursuitEvent v)
	{
		switch (v)
		{
			case PursuitEvent::Contact: return "Contact";
			case PursuitEvent::EmpCharging: return "EmpCharging";
			case PursuitEvent::EmpHit: return "EmpHit";
			case PursuitEvent::EmpMissed: return "EmpMissed";
			case PursuitEvent::Engage: return "Engage";
			case PursuitEvent::GaveUp: return "GaveUp";
		}
		return "";
	}

	cs::Ref<Faction> Pursuer::Faction() const { return Leader->Faction; }

	Pursuit::Pursuit(cs::Ref<World> world, int seed)
		: world(std::move(world)), rng(seed)
	{
	}

	cs::Ref<Pursuer> Pursuit::Of(const cs::Ref<Ship>& s) const
	{
		for (const auto& g : Groups)
			if (g->Ships.Contains(s)) return g;
		return nullptr;
	}

	DVec3 Pursuit::OffsetOf(const cs::Ref<Pursuer>& g, const cs::Ref<Ship>& s) { return s == g->Leader ? g->Pos : g->Pos + s->FormationSlot * FormationKm; }

	cs::Nullable<DVec3> Pursuit::HelioOf(const cs::Ref<Ship>& s, double jd) const
	{
		auto g = Of(s);
		if (g == nullptr || Body < 0) return std::nullopt;
		return world->Ephemeris->HelioAt(Body, jd) + OffsetOf(g, s);
	}

	cs::List<cs::Ref<Pursuer>> Pursuit::Consider(const cs::Ref<Ship>& pilot, int body, DVec3 pilotOffset, DVec3 pilotVel, double jd)
	{
		cs::List<cs::Ref<Pursuer>> found;
		if (pilot == nullptr || pilot->Destroyed || pilot->Battle != nullptr || Groups.Count() >= MaxGroups) return found;
		if (Body != body) ReleaseAll(jd);
		Body = body;
		PruneLastChased(jd);
		auto sensors = world->Sensors;
		auto pilotHelio = world->Ephemeris->HelioAt(body, jd) + pilotOffset;
		double signature = Signatures::Of(pilot, sensors ? sensors->PilotBurning() : true);
		bool transponder = Signatures::Transponder(pilot);
		for (const auto& s : world->Fleet->Ships())
		{
			// Begin adds each new group to Groups as well as found: count it once.
			if (Groups.Count() >= MaxGroups) break;
			if (!MayChase(s, pilot, body, jd)) continue;
			auto helio = world->Fleet->StateOf(s, jd).HelioKm;
			auto to = helio - pilotHelio;
			double d = to.Length();
			if (d > NoticeKm) continue;
			auto suite = sensors != nullptr ? sensors->GroupSuite(s) : SensorSuite::Military;
			if (Sensors::Resolve(suite, signature, d, transponder) < ContactLevel::Class) continue;
			if (rng.NextDouble() > ChaseChance(s)) continue;
			auto g = Begin(s, pilotOffset + (d > 1e-6 ? to / d : DVec3::UnitX) * ContactKm, pilotVel, jd);
			found.Add(g);
		}
		return found;
	}

	double Pursuit::ChaseChance(const cs::Ref<Ship>& s) { return s->Faction->IsPirate ? 0.35 : 0.25; }

	bool Pursuit::MayChase(const cs::Ref<Ship>& s, const cs::Ref<Ship>& pilot, int body, double jd) const
	{
		if (s == pilot || s->Destroyed || s->IsEscort() || s->Derelict || s->Battle != nullptr || s->Planetside() || s->GateDeparted) return false;
		if (s->Voyage != nullptr || s->Phase == ShipPhase::Lane || Of(s) != nullptr) return false;
		if (!(s->Faction->IsPirate || s->Spec->IsMilitary) || !world->Diplomacy()->Hostile(s->Faction, pilot->Faction)) return false;
		if (!s->Combat->Armed() || s->Combat->HullFraction() < 0.5) return false;
		if (world->War != nullptr && !world->War->Commanded(s, body)) return false;
		double when = 0;
		return !lastChased.TryGetValue(s, when) || jd - when >= RechaseDays;
	}

	void Pursuit::PruneLastChased(double jd)
	{
		cs::Ref<cs::List<cs::Ref<Ship>>> gone = nullptr;
		for (auto [key, value] : lastChased)
			if (key->Destroyed || key->GateDeparted || jd - value >= RechaseDays)
			{
				if (!gone) gone = cs::New<cs::List<cs::Ref<Ship>>>();
				gone->Add(key);
			}
		if (gone != nullptr) for (const auto& s : *gone) lastChased.Remove(s);
	}

	cs::Ref<Pursuer> Pursuit::Begin(const cs::Ref<Ship>& leader, DVec3 at, DVec3 pilotVel, double jd)
	{
		auto g = cs::New<Pursuer>();
		g->Leader = leader;
		g->Pos = at;
		g->Vel = pilotVel;
		g->Ships.Add(leader);
		for (const auto& e : world->Fleet->Ships())
			if (e->Leader == leader && !e->Destroyed && e->Battle == nullptr) g->Ships.Add(e);
		Groups.Add(g);
		lastChased[leader] = jd;
		return g;
	}

	cs::List<std::pair<cs::Ref<Pursuer>, PursuitEvent>> Pursuit::Step(const cs::Ref<Ship>& pilot, int body, DVec3 pilotOffset, DVec3 pilotVel, double dt, double jd)
	{
		cs::List<std::pair<cs::Ref<Pursuer>, PursuitEvent>> events;
		if (dt <= 0) return events;
		if (Body != body)
		{
			ReleaseAll(jd);
			Body = body;
			return events;
		}
		for (int i = Groups.Count() - 1; i >= 0; i--)
		{
			auto g = Groups[i];
			g->Ships.RemoveAll([](const cs::Ref<Ship>& s) { return s->Destroyed || s->Battle != nullptr; });
			// The leader lost or pulled into another fight (U.SPC-T314): the chase is over, and whoever is left of
			// the group is pinned where the chase had flown it rather than snapping back to a stale schedule spot.
			if (g->Ships.Count() == 0 || g->Leader->Destroyed || g->Leader->Battle != nullptr)
			{
				bool anyLeft = g->Ships.Count() > 0;
				Release(g, jd);
				if (anyLeft && !g->Leader->Destroyed) events.Add({ g, PursuitEvent::GaveUp });
				continue;
			}
			g->Seconds += dt;
			auto rel = pilotOffset - g->Pos;
			double d = rel.Length();
			if (d > GiveUpKm || g->Seconds > MaxChaseSeconds)
			{
				Release(g, jd);
				events.Add({ g, PursuitEvent::GaveUp });
				continue;
			}
			// Close as fast as it can and still stop alongside: v² = 2·a·s, over and above the pilot's own velocity.
			double closing = cs::Math::Min(TopSpeedKmS, cs::Math::Sqrt(2 * AccelKmS2 * cs::Math::Max(0.0, d - EngageKm * 0.5)));
			auto want = pilotVel + (d > 1e-9 ? rel / d : DVec3::Zero) * closing;
			if (want.Length() > TopSpeedKmS) want = want.Normalized() * TopSpeedKmS;
			auto dv = want - g->Vel;
			double maxDv = AccelKmS2 * dt;
			g->Vel = g->Vel + (dv.Length() > maxDv ? dv.Normalized() * maxDv : dv);
			g->Pos = g->Pos + g->Vel * dt;
			d = (pilotOffset - g->Pos).Length();

			g->EmpReadyIn = cs::Math::Max(0.0, g->EmpReadyIn - dt);
			if (g->EmpCharge >= 0)
			{
				g->EmpCharge += dt;
				if (g->EmpCharge >= EmpChargeSeconds)
				{
					g->EmpCharge = -1;
					g->EmpReadyIn = EmpCooldownSeconds;
					events.Add({ g, d <= EmpRangeKm * 1.25 ? PursuitEvent::EmpHit : PursuitEvent::EmpMissed });
				}
			}
			else if (d <= EmpRangeKm && g->EmpReadyIn <= 0)
			{
				g->EmpCharge = 0;
				events.Add({ g, PursuitEvent::EmpCharging });
			}
			if (d <= EngageKm) events.Add({ g, PursuitEvent::Engage });
		}
		return events;
	}

	void Pursuit::Engaged(const cs::Ref<Pursuer>& g) { Groups.Remove(g); }

	void Pursuit::Release(const cs::Ref<Pursuer>& g, double jd)
	{
		Groups.Remove(g);
		auto facing = g->Vel.LengthSquared() > 1e-12 ? g->Vel.Normalized() : DVec3::UnitX;
		for (const auto& s : g->Ships)
		{
			if (s->Destroyed || s->Battle != nullptr) continue;
			Fleet::PinAt(s, Body, OffsetOf(g, s), facing, jd);
			if (!s->IsEscort()) world->Fleet->ScheduleNext(s, jd);
		}
	}

	void Pursuit::ReleaseAll(double jd)
	{
		for (int i = Groups.Count() - 1; i >= 0; i--) Release(Groups[i], jd);
	}
}
