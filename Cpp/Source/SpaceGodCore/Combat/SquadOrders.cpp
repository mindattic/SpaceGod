// US-T140: ordering a squad through its leader (see Combat/SquadOrders.h).

#include "Combat/SquadOrders.h"

#include "Astro.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/War.h"
#include "Comms/Pilots.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/Influence.h"
#include "Embodiment/ShipHelm.h"
#include "Fleet.h"
#include "Law/Outlaws.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	const char* ToString(SquadAnswer v)
	{
		switch (v)
		{
		case SquadAnswer::None: return "None";
		case SquadAnswer::Obey: return "Obey";
		case SquadAnswer::Hesitate: return "Hesitate";
		case SquadAnswer::Argue: return "Argue";
		case SquadAnswer::Mutiny: return "Mutiny";
		}
		return "?";
	}

	namespace
	{
		bool Flying(const cs::Ref<Ship>& s) { return s != nullptr && !s->Destroyed && !s->GateDeparted && !s->Derelict; }

		/// Every hull of band <paramref name="band"/> still flying, in fleet order.
		cs::List<cs::Ref<Ship>> BandShips(const cs::Ref<World>& world, int band)
		{
			cs::List<cs::Ref<Ship>> list;
			if (world == nullptr || world->Fleet == nullptr || band < 0) return list;
			for (const auto& s : world->Fleet->Ships())
				if (Flying(s) && s->Band() == band) list.Add(s);
			return list;
		}

		bool Engaged(const cs::Ref<Ship>& s) { return s != nullptr && s->Battle != nullptr && !s->Battle->Over; }

		double Hull(const cs::Ref<Ship>& s) { return s->Combat != nullptr && s->Combat->Spec != nullptr ? s->Combat->Spec->Hull : 0; }

		/// The band's leader now: the hull of it nobody leads (the war's heir once its captain was taken), else the strongest.
		cs::Ref<Ship> LeaderOf(const cs::List<cs::Ref<Ship>>& squad)
		{
			cs::Ref<Ship> best;
			for (const auto& s : squad)
				if (!s->IsEscort() && (best == nullptr || Hull(s) > Hull(best))) best = s;
			if (best != nullptr) return best;
			for (const auto& s : squad)
				if (best == nullptr || Hull(s) > Hull(best)) best = s;
			return best;
		}

		/// What the crews say back when they argue, picked by the draw.
		const cs::String& ArgueLine(double r)
		{
			static const cs::List<cs::String> lines
			{
				"\"Stand down? She's unescorted and riding low. We're taking her.\"",
				"\"With respect, Captain, the crews didn't sign on to watch prizes fly past.\"",
				"\"You've gone soft. We're going in.\"",
				"\"Orders like that get captains spaced. We'll pretend we didn't hear it.\"",
				"\"Say that again on the open channel and see who's still listening.\"",
			};
			return lines[cs::Math::Min(lines.Count() - 1, static_cast<int>(r * lines.Count()))];
		}
	}

	cs::Ref<SquadOrders> SquadOrders::Find(const cs::Ref<World>& world)
	{
		return world != nullptr && world->War != nullptr ? world->War->Squads : nullptr;
	}

	cs::Ref<SquadOrders> SquadOrders::Of(const cs::Ref<World>& world)
	{
		if (world == nullptr || world->War == nullptr) return nullptr;
		if (world->War->Squads == nullptr) world->War->Squads = cs::New<SquadOrders>();
		return world->War->Squads;
	}

	cs::List<cs::Ref<Ship>> SquadOrders::SquadOf(const cs::Ref<World>& world, const cs::Ref<Ship>& ship)
	{
		cs::List<cs::Ref<Ship>> list;
		if (world == nullptr || world->Fleet == nullptr || ship == nullptr) return list;
		int band = ship->Band();
		for (const auto& s : world->Fleet->Ships())
			if (s != ship && Flying(s) && s->Faction == ship->Faction && (s->Band() == band || s->Leader == ship)) list.Add(s);
		return list;
	}

	cs::Ref<CloseIn> SquadOrders::Begin(const cs::Ref<World>& world, const cs::Ref<Ship>& leader, const cs::Ref<Ship>& mark, double seconds, double jd)
	{
		auto book = Of(world);
		if (book == nullptr || leader == nullptr || mark == nullptr) return nullptr;
		auto c = cs::New<CloseIn>();
		c->Band = leader->Band();
		c->Mark = mark;
		c->Body = std::get<0>(ShipHelm::FrameOf(world, mark, jd));
		c->StartJd = jd;
		c->StrikeJd = jd + seconds / Astro::SecondsPerDay;
		book->StoodDown.Remove(c->Band);
		book->Closing.Add(c);
		return c;
	}

	cs::Ref<CloseIn> SquadOrders::ClosingFor(const cs::Ref<World>& world, const cs::Ref<Ship>& ship)
	{
		auto book = Find(world);
		if (book == nullptr || ship == nullptr) return nullptr;
		int band = ship->Band();
		for (const auto& c : book->Closing)
			if (c->Band == band) return c;
		return nullptr;
	}

	bool SquadOrders::IsStoodDown(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, double jd)
	{
		auto book = Find(world);
		if (book == nullptr || ship == nullptr) return false;
		double until = 0;
		return book->StoodDown.TryGetValue(ship->Band(), until) && jd < until;
	}

	bool SquadOrders::CanOrder(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, double jd, cs::String& state, cs::String& why)
	{
		state = why = cs::String();
		if (!Flying(ship)) { why = "the hull is gone"; return false; }
		auto squad = SquadOf(world, ship);
		if (squad.Count() == 0)
		{
			why = cs::Cat("no squad answers to the ", ship->Name, ": its captain flies alone");
			state = "no squad";
			return false;
		}
		for (const auto& s : squad)
			if (Engaged(s))
			{
				why = cs::Cat("the squad is engaged (", s->Battle->Name, "): too late to call it off");
				state = "engaged";
				return false;
			}
		if (Engaged(ship))
		{
			why = "you are in the fight yourself: squad orders are given before it";
			state = "engaged";
			return false;
		}
		if (IsStoodDown(world, ship, jd))
		{
			double until = 0;
			Find(world)->StoodDown.TryGetValue(ship->Band(), until);
			state = cs::Cat("standing down, ", cs::Fmt((until - jd) * 24, "0.0"), " h left");
			why = "the squad is already standing down";
			return false;
		}
		if (auto c = ClosingFor(world, ship))
		{
			double s = cs::Math::Max(0.0, (c->StrikeJd - jd) * Astro::SecondsPerDay);
			state = cs::Cat(squad.Count(), " hull", squad.Count() == 1 ? "" : "s", " closing on the ", c->Mark != nullptr ? c->Mark->Name : cs::String("mark"),
				", contact in ", cs::Fmt(s, "0"), " s");
			return true;
		}
		for (const auto& s : squad)
			if (!s->IsEscort() && (s->Ambushing() || s->StakeoutKm.has_value()))
			{
				state = cs::Cat(squad.Count(), " hull", squad.Count() == 1 ? "" : "s", " lying in wait");
				return true;
			}
		state = cs::Cat(squad.Count(), " hull", squad.Count() == 1 ? "" : "s", ", nothing in their sights");
		why = "the squad is not closing on anything: nothing to stand down from";
		return false;
	}

	SquadOrderResult SquadOrders::StandDown(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, double jd)
	{
		SquadOrderResult r;
		cs::String state, why;
		if (!CanOrder(world, ship, jd, state, why))
		{
			r.Toast = cs::Cat("Stand down: ", why);
			return r;
		}
		auto book = Of(world);
		auto squad = SquadOf(world, ship);
		auto closing = ClosingFor(world, ship);
		auto chars = Characters::Of(world);
		auto captain = chars != nullptr ? chars->Meet(world, ship, MeetKind::Embodied, jd) : nullptr;
		cs::String captainName = captain != nullptr ? captain->Styled() : cs::Cat("the captain of the ", ship->Name);

		// The SpaceGod's half: its Influence, 0 at the first level to 1 at the top.
		auto inf = SpaceGodInfluence::Of(world);
		int level = inf != nullptr ? inf->Level() : 1;
		double influence = (level - 1) / static_cast<double>(SpaceGodInfluence::MaxLevel - 1);

		// The captain's half: rank, reputation, loyalty and history over these crews (US-T138's named pilot).
		double hold = 0;
		if (captain != nullptr)
		{
			double rank = captain->Rank / cs::Math::Max(1.0, static_cast<double>(Careers::TopRank(captain->Track)));
			double reputation = cs::Math::Min(1.0, captain->Renown / 30.0);
			double loyalty = 0.4 * captain->Honour + 0.3 * captain->Warmth + 0.3 * (1 - captain->Spite) - (captain->Betrayed ? 0.15 : 0.0);
			double history = cs::Math::Min(0.2, captain->BattlesSurvived * 0.03) - 0.05 * captain->Surrenders;
			hold = 0.35 * rank + 0.25 * reputation + 0.4 * loyalty + history;
		}
		else hold = 0.3;
		// The band's own captain (its flagship) gives orders; an escort's captain giving them to the rest is pushing it.
		hold += ship->Id == ship->Band() ? 0.15 : -0.15;
		if (world->Outlaws != nullptr)
			if (auto o = world->Outlaws->Of(ship); o != nullptr && o->Feared && o->Ship == ship) hold += 0.2;

		// What pulls the crews the other way: the prize in their sights, and every order they have already argued down.
		double pull = 0.5;
		if (closing != nullptr && closing->Mark != nullptr)
		{
			auto mark = closing->Mark;
			bool alone = SquadOf(world, mark).Count() == 0 && (mark->Spec == nullptr || !mark->Spec->IsMilitary);
			pull = alone ? (mark->HasCargo || mark->CargoTonnes > 0 ? 1.0 : 0.8) : 0.6;
		}
		double resistance = 0.2 + 0.3 * pull + 0.15 * (closing != nullptr ? closing->Defied : 0);

		r.InfluencePart = 0.45 * influence;
		r.CaptainPart = 0.5 * hold;
		r.Compliance = 0.4 + r.InfluencePart + r.CaptainPart - resistance;
		// The crews' mood on the day: one draw from the hull, the band, the date (to ten seconds) and the orders argued, the
		// same on every peer.
		uint32_t seed = Characters::Mix(static_cast<uint32_t>(ship->Id) * 7919u ^ static_cast<uint32_t>(ship->Band()) * 104729u
			^ static_cast<uint32_t>(static_cast<long long>(cs::Math::Floor(jd * 8640.0)) & 0x7fffffff));
		double draw = Characters::Roll(static_cast<int>(seed & 0x7fffffff), 0x5D0u + static_cast<uint32_t>(closing != nullptr ? closing->Defied : 0));
		double v = r.Compliance + (draw - 0.5) * 0.5;
		r.Answer = v >= 0.55 ? SquadAnswer::Obey : v >= 0.35 ? SquadAnswer::Hesitate : v >= 0.15 ? SquadAnswer::Argue : SquadAnswer::Mutiny;

		cs::String markName = closing != nullptr && closing->Mark != nullptr ? cs::Cat("the ", closing->Mark->Name) : cs::String("the lane's traffic");
		auto leader = LeaderOf(squad);
		cs::String odds = cs::Cat("  [Influence ", level, " +", cs::Fmt(r.InfluencePart * 100, "0"), ", captain +", cs::Fmt(r.CaptainPart * 100, "0"),
			", the prize -", cs::Fmt(resistance * 100, "0"), "]");
		switch (r.Answer)
		{
		case SquadAnswer::Obey:
		{
			book->StoodDown[ship->Band()] = jd + StandDownDays;
			for (int i = book->Closing.Count() - 1; i >= 0; i--)
				if (book->Closing[i]->Band == ship->Band()) book->Closing.RemoveAt(i);
			// The captain spared the mark: their act (owned once the SpaceGod leaves, the rationalization rule).
			if (captain != nullptr && chars != nullptr) chars->Act(captain, LifeEventKind::Spared, jd, 1, { "victim", closing != nullptr && closing->Mark != nullptr ? closing->Mark->Name : cs::String("lane's traffic") });
			SpaceGodInfluence::Grant(world, InfluenceSource::Intervention, 25, cs::Cat("talked a squad down off ", markName));
			world->Log(jd, cs::Cat("The ", ship->Name, "'s raiders break off ", markName, " on their captain's order"), WorldEventKind::Info, -1);
			r.Toast = cs::Cat("OBEY: \"Aye, Captain.\" ", squad.Count(), " hull", squad.Count() == 1 ? "" : "s", " break off ", markName,
				" and stand down for ", cs::Fmt(StandDownDays * 24, "0"), " h", odds);
			break;
		}
		case SquadAnswer::Hesitate:
			if (closing != nullptr) closing->StrikeJd = cs::Math::Max(closing->StrikeJd, jd) + 45.0 / Astro::SecondsPerDay;
			r.Toast = cs::Cat("HESITATE: the crews hold their fire and look to ", leader != nullptr ? cs::Cat("the ", leader->Name) : cs::String("each other"),
				closing != nullptr ? cs::String(": contact held back 45 s. Order again") : cs::String(": order again"), odds);
			break;
		case SquadAnswer::Argue:
			if (closing != nullptr) closing->Defied++;
			if (captain != nullptr) captain->Renown = cs::Math::Max(0.0, captain->Renown - 1);
			r.Toast = cs::Cat("ARGUE: ", ArgueLine(Characters::Roll(static_cast<int>(seed & 0x7fffffff), 0x5E1u)), " The squad keeps closing; the next order is harder", odds);
			break;
		case SquadAnswer::Mutiny:
		{
			// The crews put someone else at the band's head: the hull leading it now (the war's heir), or its strongest.
			if (leader == nullptr)
			{
				r.Answer = SquadAnswer::Argue;
				r.Toast = cs::Cat("ARGUE: the crews shout the order down", odds);
				break;
			}
			int oldBand = ship->Band(), newBand = leader->Id;
			for (const auto& s : squad) s->BandId = newBand;
			ship->BandId = -1; // cut loose: the deposed captain flies alone now
			for (const auto& s : squad)
				if (s->Leader == ship) s->Leader = nullptr;
			for (int i = book->Closing.Count() - 1; i >= 0; i--)
			{
				auto c = book->Closing[i];
				if (c->Band != oldBand && c != closing) continue;
				c->Band = newBand;
				c->StrikeJd = jd + 5.0 / Astro::SecondsPerDay; // they go in at once
			}
			cs::String usurper = cs::Cat("the captain of the ", leader->Name);
			if (chars != nullptr)
			{
				if (captain != nullptr)
				{
					captain->Rank = cs::Math::Max(0, captain->Rank - 1);
					chars->Act(captain, LifeEventKind::Demoted, jd, 0, { "title", Careers::Title(captain->Track, captain->Rank) });
				}
				auto heir = chars->Meet(world, leader, MeetKind::Mentioned, jd);
				if (heir != nullptr)
				{
					usurper = heir->Styled();
					chars->Betrayal(heir, captainName, jd);
					chars->Advance(world, heir, jd);
				}
			}
			world->Log(jd, cs::Cat("Mutiny in the ", ship->Name, "'s band: the crews depose ", captainName, " and follow ", usurper, " (the ", leader->Name, ")"),
				WorldEventKind::Info, -1);
			r.Toast = cs::Cat("MUTINY: the crews turn on ", captainName, ". ", usurper, " takes the band", closing != nullptr ? cs::Cat(" and goes for ", markName, " now") : cs::String(), odds);
			break;
		}
		default: break;
		}
		return r;
	}

	void SquadOrders::Advance(const cs::Ref<World>& world, double jd)
	{
		auto book = Find(world);
		if (book == nullptr) return;
		// Stand-downs run out.
		cs::List<int> over;
		for (auto [band, until] : book->StoodDown)
			if (jd >= until) over.Add(band);
		for (int band : over) book->StoodDown.Remove(band);
		for (int i = book->Closing.Count() - 1; i >= 0; i--)
		{
			auto c = book->Closing[i];
			auto band = BandShips(world, c->Band);
			bool stale = !Flying(c->Mark) || c->Mark->Planetside() || band.Count() == 0 || Engaged(c->Mark);
			for (const auto& s : band)
				if (Engaged(s)) stale = true;
			if (stale)
			{
				book->Closing.RemoveAt(i);
				continue;
			}
			if (jd < c->StrikeJd) continue;
			book->Closing.RemoveAt(i);
			if (world->War != nullptr) world->War->StrikeAt(band, c->Mark, jd);
		}
	}

	// ================================================================ ObjectGraph registrations (docs/PERSISTENCE.md)

	SG_SERIALIZE(CloseIn, "SpaceGod.Core.CloseIn",
		SG_F(Band), SG_F(Mark), SG_F(Body), SG_F(StartJd), SG_F(StrikeJd), SG_F(Defied))

	SG_SERIALIZE(SquadOrders, "SpaceGod.Core.SquadOrders",
		SG_F(Closing), SG_F(StoodDown))
}
