// US-T194: freighters set upon by raiders call for help, and the navy patrols near enough answer (see Combat/RaidDistress.h).

#include "Combat/RaidDistress.h"

#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Combat/RaidDoctrine.h"
#include "Combat/War.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	namespace
	{
		bool Flying(const cs::Ref<Ship>& s) { return s != nullptr && !s->Destroyed && !s->GateDeparted && !s->Derelict; }

		/// The present guns of side <paramref name="side"/> of <paramref name="b"/>: what is still in the fight.
		double SideStrength(const cs::Ref<Battle>& b, int side)
		{
			double sum = 0;
			for (const auto& u : b->Units)
				if (u->Side == side && u->Present() && !u->Captured && u->Combat != nullptr && !u->Combat->Disabled()) sum += u->Combat->Strength();
			return sum;
		}

		cs::String Seconds(double s) { return cs::Cat(cs::Fmt(s, "0"), " s"); }
	}

	cs::Ref<RaidDistress> RaidDistress::Find(const cs::Ref<World>& world)
	{
		return world != nullptr && world->War != nullptr ? world->War->Distress : nullptr;
	}

	cs::Ref<RaidDistress> RaidDistress::Of(const cs::Ref<World>& world)
	{
		if (world == nullptr || world->War == nullptr) return nullptr;
		if (world->War->Distress == nullptr) world->War->Distress = cs::New<RaidDistress>();
		return world->War->Distress;
	}

	cs::Ref<RaidDistressCall> RaidDistress::CallIn(const cs::Ref<World>& world, const cs::Ref<Battle>& b)
	{
		auto book = Find(world);
		if (book == nullptr || b == nullptr) return nullptr;
		for (const auto& c : book->Calls)
			if (c->Fight == b) return c;
		return nullptr;
	}

	double RaidDistress::ResponseSeconds(const cs::Ref<World>& world, const cs::Ref<Ship>& leader, const cs::Ref<Battle>& b, double jd)
	{
		if (world == nullptr || leader == nullptr || b == nullptr || leader->Orbit == nullptr || leader->Phase != ShipPhase::Parked) return -1;
		int here = leader->Orbit->Body;
		double travel = -1;
		if (b->Site != nullptr && b->Site->Hop.Lane != nullptr)
		{
			// Pulled out of a lane: the patrols at either end ride it out to the fight.
			const auto& hop = b->Site->Hop;
			double p = cs::Math::Max(0.0, cs::Math::Min(1.0, b->Site->Progress));
			if (here == hop.From) travel = p * LaneRideS;
			else if (here == hop.To) travel = (1 - p) * LaneRideS;
		}
		else if (here == b->Body)
			// A fight at the body it is parked at: burn across to it.
			travel = (leader->Orbit->OffsetAt(jd) - b->Anchor).Length() / BurnKmS;
		if (travel < 0) return -1;
		double total = ScrambleS + travel;
		return total <= MaxResponseS ? total : -1;
	}

	void RaidDistress::Advance(const cs::Ref<World>& world, double jd)
	{
		if (world == nullptr || world->War == nullptr || world->Fleet == nullptr) return;
		auto war = world->War;
		auto diplomacy = world->Diplomacy();

		// Calls of fights that are over (resolved and gone from the war's list) are forgotten.
		if (auto book = Find(world))
			for (int i = book->Calls.Count() - 1; i >= 0; i--)
			{
				auto c = book->Calls[i];
				if (c->Fight == nullptr || c->Fight->Over || !war->Battles.Contains(c->Fight)) book->Calls.RemoveAt(i);
			}

		for (const auto& b : war->Battles)
		{
			if (b->Over) continue;
			auto call = CallIn(world, b);
			if (call == nullptr)
			{
				// Raiders on one side, a hull they mean to rob on the other: it calls for help as soon as it is set upon.
				for (int raiderSide = 0; raiderSide < 2 && call == nullptr; raiderSide++)
				{
					auto raiders = b->SideLeaders[raiderSide];
					if (raiders == nullptr || !raiders->IsPirate) continue;
					for (const auto& u : b->Units)
					{
						if (u->Side != 1 - raiderSide || u->Kind != UnitKind::Ship || u->Ship == nullptr || !u->Present() || u->Captured) continue;
						if (!RaidDoctrine::Robs(raiders, u->Ship)) continue;
						call = cs::New<RaidDistressCall>();
						call->Fight = b;
						call->Caller = u->Ship;
						call->Raiders = raiders;
						call->Side = u->Side;
						call->Jd = jd;
						break;
					}
				}
				if (call == nullptr) continue;
				Of(world)->Calls.Add(call);

				// Who answers: armed navy groups parked near enough, of any power not at war with the caller, the soonest there first.
				auto busy = cs::HashSet<cs::Ref<Ship>>();
				for (const auto& other : Find(world)->Calls)
					for (const auto& r : other->Responses)
						if (!r->Arrived) busy.Add(r->Leader);
				cs::List<std::pair<double, cs::Ref<Ship>>> candidates;
				for (const auto& s : world->Fleet->Ships())
				{
					if (!Flying(s) || s->IsEscort() || s->Battle != nullptr || s == world->PilotedShip || busy.Contains(s)) continue;
					if (s->Faction == nullptr || s->Faction->IsPirate || s->Runner || s->Convoy || s->PicketGate != nullptr) continue;
					if (!s->Spec->IsMilitary || s->Combat == nullptr || !s->Combat->Armed() || s->Combat->HullFraction() < 0.5) continue;
					if (diplomacy != nullptr && (!diplomacy->Hostile(s->Faction, call->Raiders) || diplomacy->Hostile(s->Faction, call->Caller->Faction))) continue;
					if (war->Withdrawing(s, jd)) continue;
					double t = ResponseSeconds(world, s, b, jd);
					if (t < 0) continue;
					// (A stable insertion: equal times keep fleet order.)
					int at = candidates.Count();
					while (at > 0 && candidates[at - 1].first > t) at--;
					candidates.Insert(at, std::make_pair(t, s));
				}
				cs::String answered;
				for (int i = 0; i < candidates.Count() && i < MaxResponders; i++)
				{
					auto r = cs::New<RaidDistressResponse>();
					r->Leader = candidates[i].second;
					r->ArriveAtS = b->Seconds + candidates[i].first;
					call->Responses.Add(r);
					answered += cs::Cat(i == 0 ? "" : "; ", "the ", r->Leader->Faction->Name, " ", r->Leader->Name, " (", Seconds(candidates[i].first), " out)");
				}
				cs::String text = cs::Cat(b->Name, ": the ", call->Caller->Name, " calls for help against ", call->Raiders->Name, " raiders");
				text += call->Responses.Count() > 0 ? cs::Cat(". Answering: ", answered) : cs::String(", but no patrol is near enough to answer");
				world->Log(jd, text, WorldEventKind::Battle, war->Shown(b));
				if (world->PilotedShip != nullptr && b->UnitOf(world->PilotedShip) != nullptr)
					war->pilotNotices.Add(call->Responses.Count() > 0
						? cs::Cat("DISTRESS: the ", call->Caller->Name, " calls for help. ", call->Responses[0]->Leader->Name, " answering, ", Seconds(candidates[0].first), " out")
						: cs::Cat("DISTRESS: the ", call->Caller->Name, " calls for help, but no patrol is near enough to answer"));
			}

			// Patrols whose time has come join the fight on the caller's side.
			for (const auto& r : call->Responses)
			{
				if (r->Arrived || b->Seconds < r->ArriveAtS) continue;
				r->Arrived = true;
				auto leader = r->Leader;
				if (!Flying(leader) || leader->Battle != nullptr || leader == world->PilotedShip) continue;
				int side = call->Side;
				if (b->SideLeaders[side] != nullptr && diplomacy != nullptr && diplomacy->Hostile(leader->Faction, b->SideLeaders[side])) continue;
				war->Join(b, leader, side, true);
				if (leader->Battle != b) continue;
				world->Log(jd, cs::Cat(b->Name, ": the ", leader->Faction->Name, " ", leader->Name, " arrives answering the ", call->Caller->Name, "'s call"), WorldEventKind::Battle, war->Shown(b));
				if (world->PilotedShip != nullptr && b->UnitOf(world->PilotedShip) != nullptr)
					war->pilotNotices.Add(cs::Cat("The ", leader->Faction->Name, " ", leader->Name, " arrives answering the distress call"));

				// Raiders still short of their loot weigh the guns now against them: more than their nerve, and they run.
				int raiderSide = 1 - side;
				if (raiderSide < 0 || raiderSide > 1 || b->Robbed[raiderSide]) continue;
				double band = SideStrength(b, raiderSide);
				if (band <= 0 || SideStrength(b, side) <= band * RaidDoctrine::BaseNerve) continue;
				bool broke = false;
				for (const auto& u : b->Units)
					if (u->Side == raiderSide && u->Present() && !u->PlayerControlled() && !u->Fleeing && !u->Captured && !u->Surrendering)
					{
						u->Fleeing = true;
						broke = true;
					}
				if (broke)
					world->Log(jd, cs::Cat(b->Name, ": the ", call->Raiders->Name, " raiders break off and run before the ", call->Caller->Name, " can be robbed"), WorldEventKind::Battle, war->Shown(b));
			}
		}
	}

	// ================================================================ ObjectGraph registrations (docs/PERSISTENCE.md)

	SG_SERIALIZE(RaidDistressResponse, "SpaceGod.Core.RaidDistressResponse",
		SG_F(Leader), SG_F(ArriveAtS), SG_F(Arrived))

	SG_SERIALIZE(RaidDistressCall, "SpaceGod.Core.RaidDistressCall",
		SG_F(Fight), SG_F(Caller), SG_F(Raiders), SG_F(Side), SG_F(Jd), SG_F(Responses))

	SG_SERIALIZE(RaidDistress, "SpaceGod.Core.RaidDistress",
		SG_F(Calls))
}
