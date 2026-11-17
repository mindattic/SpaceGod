// US-T292: Witness (see Systems/Civilization/Witness.h).
//
// Steered (the SpaceGod's hand), as far as Core can tell it:
//  - a battle with a unit a player flew (BattleUnit.PlayerFlown), and whatever else that system saw the same day: a world
//    taken, fallen or devastated, a haven stormed;
//  - a world taken by, or a war declared by, the polity the SpaceGod commands from the galaxy map (StrategicCommand.Player);
//  - a pact signed by that polity (only it decides for itself), a blockade or embargo its navy declares.
// A peace, a broken pact and the other shocks are left unsteered: Core ends wars by treaty on its own terms, does not say
// who broke a pact, and rations or strikes of its own accord. Influence keeps no record of what an intervention did, and
// the Law's records are the pilot's crimes rather than the SpaceGod's, so neither is read.

#include "Systems/Civilization/Witness.h"

#include "Combat/Battle.h"
#include "Combat/Diplomacy.h"
#include "Combat/War.h"
#include "Economy/Economy.h"
#include "Economy/MarketShocks.h"
#include "Economy/Settlement.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "Systems/Civilization/CivHash.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarWars.h"
#include "Systems/Polities.h"
#include "Systems/Statecraft.h"
#include "Systems/StrategicCommand.h"
#include "World.h"

#include <unordered_map>

namespace SpaceGod::Core
{
	using namespace Civ;

	namespace
	{
		bool Haven(const cs::Ref<Settlement>& s) { return s->Kind == SettlementKind::Haven; }

		bool StandsNow(const cs::Ref<Settlement>& s) { return s->Owner != nullptr && !s->Fallen && s->Stage != ColonyStage::Ruin; }

		cs::Ref<Settlement> CapitalOf(Galaxy& galaxy, const cs::Ref<Faction>& f)
		{
			return f != nullptr && galaxy.Polities != nullptr ? galaxy.Polities->CapitalOf(f) : nullptr;
		}

		/// The most populous standing world of a system (no haven), one of <paramref name="prefer"/>'s if it has one.
		cs::Ref<Settlement> MainWorld(const cs::Ref<StarSystem>& sys, const cs::Ref<Faction>& prefer)
		{
			if (sys == nullptr || sys->World == nullptr || sys->World->Economy == nullptr) return nullptr;
			cs::Ref<Settlement> best, mine;
			for (const auto& s : sys->World->Economy->Settlements)
			{
				if (Haven(s) || !StandsNow(s)) continue;
				if (best == nullptr || s->Population > best->Population) best = s;
				if (prefer != nullptr && s->Owner == prefer && (mine == nullptr || s->Population > mine->Population)) mine = s;
			}
			return mine != nullptr ? mine : best;
		}

		/// Whether another module already told the people of this (a summit's peace, an uprising's secession) in the last days.
		bool Told(const Civilization& civ, SocialKind a, SocialKind b, const cs::Ref<Faction>& f, const cs::Ref<Faction>& g, const cs::Ref<Settlement>& where, double jd)
		{
			for (int i = civ.Events.Count() - 1, n = 0; i >= 0 && n < 120; i--, n++)
			{
				const auto& e = civ.Events[i];
				if (e->Jd < jd - 3) break;
				if (e->Kind != a && e->Kind != b) continue;
				if (where != nullptr && e->Where == where) return true;
				if (f != nullptr && g != nullptr && ((e->Faction == f && e->Rival == g) || (e->Faction == g && e->Rival == f))) return true;
			}
			return false;
		}

		int ShockKey(const EconomicShock& sh)
		{
			std::uint32_t h = H(static_cast<std::uint32_t>(sh.Kind) + 1u, sh.Settlement != nullptr ? HashText(sh.Settlement->Name) : 0u,
				sh.Faction != nullptr ? HashText(sh.Faction->Name) : 0u, static_cast<std::uint32_t>(static_cast<long long>(cs::Math::Floor(sh.StartJd * 24))));
			return static_cast<int>(h & 0x7fffffffu);
		}

		/// What one finished battle came to, read off its units.
		struct Fight
		{
			cs::Ref<Faction> Faction, Rival;
			cs::Ref<Settlement> Port;
			int Lost = 0;
			bool Raid = false, Hand = false;
		};
	}

	cs::Ref<Faction> Witness::Commanded(const Galaxy& galaxy)
	{
		return galaxy.Polities != nullptr && galaxy.Polities->Command != nullptr ? galaxy.Polities->Command->Player : nullptr;
	}

	bool Witness::IsWitnessed(SocialKind k) { return k >= SocialKind::WorldConquered && k < SocialKind::Count_; }

	void Witness::Tell(Civilization& civ, Galaxy& galaxy, const cs::Ref<SocialEvent>& e)
	{
		civ.Publish(galaxy, e);
		Witnessed++;
	}

	void Witness::Day(Civilization& civ, Galaxy& galaxy, double jd)
	{
		bool quiet = !Primed;
		cs::List<cs::Ref<StarSystem>> fresh, flown;
		Battles(civ, galaxy, jd, quiet, fresh, flown);
		Settlements(civ, galaxy, jd, quiet, fresh, flown);
		WarsAndPacts(civ, galaxy, jd, quiet);
		Markets(civ, galaxy, jd, quiet, fresh);
		States(civ, galaxy, jd, quiet);
		Primed = true;
	}

	// ---------------------------------------------------------------- battles (and the systems seen)

	void Witness::Battles(Civilization& civ, Galaxy& galaxy, double jd, bool quiet, cs::List<cs::Ref<StarSystem>>& fresh, cs::List<cs::Ref<StarSystem>>& flown)
	{
		cs::List<cs::Ref<WitnessedSystem>> next;
		for (const auto& sys : galaxy.Systems)
		{
			if (sys->World == nullptr) continue;
			cs::Ref<WitnessedSystem> ws;
			for (const auto& w : Systems)
				if (w->System == sys) { ws = w; break; }
			if (ws == nullptr)
			{
				ws = cs::New<WitnessedSystem>();
				ws->System = sys;
				fresh.Add(sys);
			}
			next.Add(ws);
			auto war = sys->World->War;
			if (war == nullptr) { ws->Battles.Clear(); continue; }
			cs::List<int> ids;
			Fight raid, battle;
			for (const auto& b : war->Ended)
			{
				ids.Add(b->Id);
				if (quiet || fresh.Contains(sys) || ws->Battles.Contains(b->Id)) continue;
				int lost[2] = { 0, 0 };
				bool works = false, hand = false;
				cs::Ref<Settlement> port;
				for (const auto& u : b->Units)
				{
					int side = u->Side == 1 ? 1 : 0;
					if (u->Dead || u->Captured) lost[side]++;
					if (u->Platform != nullptr) { works = true; port = u->Platform; }
					if (u->Fort != nullptr) works = true;
					if (u->PlayerFlown) hand = true;
				}
				if (hand && !flown.Contains(sys)) flown.Add(sys);
				cs::Ref<Faction> l0 = b->SideLeaders.Count() > 0 ? b->SideLeaders[0] : nullptr, l1 = b->SideLeaders.Count() > 1 ? b->SideLeaders[1] : nullptr;
				bool p0 = l0 != nullptr && l0->IsPirate, p1 = l1 != nullptr && l1->IsPirate;
				Fight f;
				f.Port = port;
				f.Hand = hand;
				if (p0 != p1)
				{
					int ps = p0 ? 0 : 1, vs = 1 - ps;
					auto clan = ps == 0 ? l0 : l1, victim = ps == 0 ? l1 : l0;
					if (lost[vs] > 0)
					{
						// Raiders took or broke hulls: a raid on the victims' shipping.
						f.Faction = victim;
						f.Rival = clan;
						f.Lost = lost[vs];
						f.Raid = true;
						if (raid.Faction == nullptr || f.Lost > raid.Lost || (f.Hand && !raid.Hand)) raid = f;
						continue;
					}
					if (lost[ps] < 3) continue;
					// A band beaten off hard enough to be a battle.
					f.Faction = victim;
					f.Rival = clan;
					f.Lost = lost[ps];
				}
				else
				{
					int total = lost[0] + lost[1];
					bool navies = l0 != nullptr && l1 != nullptr && l0->IsMilitary && l1->IsMilitary;
					if (!(total >= 3 || (works && total >= 1) || (navies && total >= 2))) continue;
					// The side that held the field leads the story; else whoever started it.
					int win = b->Winner == 1 ? 1 : b->Winner == 0 ? 0 : b->AggressorSide == 1 ? 1 : 0;
					f.Faction = win == 0 ? l0 : l1;
					f.Rival = win == 0 ? l1 : l0;
					f.Lost = total;
				}
				if (battle.Faction == nullptr || f.Lost > battle.Lost || (f.Hand && !battle.Hand)) battle = f;
			}
			ws->Battles = ids;
			for (const auto* f : { &raid, &battle })
			{
				if (f->Faction == nullptr) continue;
				auto where = f->Port != nullptr ? f->Port : MainWorld(sys, f->Raid ? f->Faction : f->Rival);
				auto e = Civilization::Make(f->Raid ? SocialKind::PirateRaid : SocialKind::BattleFought, jd, where, sys);
				e->Faction = f->Faction;
				e->Rival = f->Rival;
				e->Amount = f->Lost;
				e->Steered = f->Hand;
				Tell(civ, galaxy, e);
			}
		}
		Systems = next;
	}

	// ---------------------------------------------------------------- settlements: owners, ruins, havens, people

	void Witness::Settlements(Civilization& civ, Galaxy& galaxy, double jd, bool quiet, const cs::List<cs::Ref<StarSystem>>& fresh, const cs::List<cs::Ref<StarSystem>>& flown)
	{
		std::unordered_map<const Settlement*, cs::Ref<WitnessedWorld>> seen;
		for (const auto& w : Worlds)
			if (w->Where != nullptr) seen[w->Where.get()] = w;
		auto commanded = Commanded(galaxy);
		cs::List<cs::Ref<WitnessedWorld>> next;
		for (const auto& sys : galaxy.Systems)
		{
			if (sys->World == nullptr || sys->World->Economy == nullptr) continue;
			bool hush = quiet || fresh.Contains(sys);
			bool hand = flown.Contains(sys);
			for (const auto& s : sys->World->Economy->Settlements)
			{
				bool stood = StandsNow(s);
				auto it = seen.find(s.get());
				auto was = it != seen.end() ? it->second : nullptr;
				if (!hush)
				{
					if (Haven(s))
					{
						bool before = was != nullptr && was->Stood;
						if (before && !stood)
						{
							// A haven taken or burned: the system's people breathe again.
							auto e = Civilization::Make(SocialKind::HavenStormed, jd, s, sys);
							auto main = MainWorld(sys, nullptr);
							e->Faction = main != nullptr ? main->Owner : nullptr;
							e->Rival = was->Owner != nullptr ? was->Owner : s->Owner;
							e->Steered = hand;
							Tell(civ, galaxy, e);
						}
						else if (!before && stood && s->Discovered())
						{
							// A new nest in a system already known, or an old one rebuilt (one still hidden is nobody's news yet).
							auto e = Civilization::Make(SocialKind::PirateHold, jd, s, sys);
							e->Rival = MainWorld(sys, nullptr) != nullptr ? MainWorld(sys, nullptr)->Owner : nullptr;
							Tell(civ, galaxy, e);
						}
					}
					else if (was != nullptr && was->Stood && !stood)
					{
						auto e = Civilization::Make(SocialKind::WorldFallen, jd, s, sys);
						e->Faction = was->Owner;
						e->Amount = cs::Math::Floor(was->Population);
						e->Steered = hand;
						Tell(civ, galaxy, e);
						lostBy.Add(was->Owner);
						lostTo.Add(nullptr);
						lostWhere.Add(s);
						lostSteered.Add(hand ? 1 : 0);
					}
					else if (was != nullptr && was->Stood && stood)
					{
						if (was->Owner != s->Owner)
						{
							bool steered = hand || (commanded != nullptr && s->Owner == commanded);
							SocialKind kind = s->Owner == was->OccupiedFrom ? SocialKind::WorldLiberated
								: !Standing.Contains(s->Owner) && !s->Owner->IsPirate ? SocialKind::WorldSeceded : SocialKind::WorldConquered;
							// An uprising Strife already told of breaks the world away; the people know.
							if (!(kind == SocialKind::WorldSeceded && Told(civ, SocialKind::Uprising, SocialKind::Uprising, nullptr, nullptr, s, jd)))
							{
								auto e = Civilization::Make(kind, jd, s, sys);
								e->Rival = was->Owner;
								e->Amount = cs::Math::Floor(s->Population);
								e->Steered = steered;
								Tell(civ, galaxy, e);
							}
							lostBy.Add(was->Owner);
							lostTo.Add(s->Owner);
							lostWhere.Add(s);
							lostSteered.Add(steered ? 1 : 0);
						}
						else if (was->OccupiedFrom != nullptr && s->OccupiedFrom == nullptr && s->Owner != was->OccupiedFrom)
						{
							// The peace made an occupation for good.
							auto e = Civilization::Make(SocialKind::WorldCeded, jd, s, sys);
							e->Rival = was->OccupiedFrom;
							e->Amount = cs::Math::Floor(s->Population);
							e->Steered = commanded != nullptr && s->Owner == commanded;
							Tell(civ, galaxy, e);
						}
						double lost = was->Population - s->Population;
						if (lost >= DevastatedPeople && lost >= was->Population * DevastatedShare)
						{
							auto e = Civilization::Make(SocialKind::WorldDevastated, jd, s, sys);
							e->Amount = cs::Math::Floor(lost);
							e->Steered = hand;
							Tell(civ, galaxy, e);
						}
					}
				}
				auto now = was != nullptr ? was : cs::New<WitnessedWorld>();
				now->Where = s;
				now->System = sys;
				now->Owner = s->Owner;
				now->OccupiedFrom = s->OccupiedFrom;
				now->Stood = stood;
				now->Population = s->Population;
				next.Add(now);
			}
		}
		Worlds = next;
	}

	// ---------------------------------------------------------------- wars and pacts

	void Witness::WarsAndPacts(Civilization& civ, Galaxy& galaxy, double jd, bool quiet)
	{
		auto commanded = Commanded(galaxy);
		if (galaxy.Wars != nullptr)
		{
			const auto& active = galaxy.Wars->Active();
			if (!quiet)
			{
				for (const auto& w : active)
				{
					if (Wars.Contains(w) || w->Aggressor == nullptr || w->Defender == nullptr) continue;
					auto where = CapitalOf(galaxy, w->Defender);
					auto e = Civilization::Make(SocialKind::WarDeclared, jd, where, Civilization::SystemOf(galaxy, where));
					e->Faction = w->Aggressor;
					e->Rival = w->Defender;
					e->Steered = commanded != nullptr && w->Aggressor == commanded;
					Tell(civ, galaxy, e);
				}
				for (const auto& w : Wars)
				{
					if (active.Contains(w) || w->Aggressor == nullptr || w->Defender == nullptr) continue;
					// A summit's peace, or one forced by a weary people, is news already.
					if (Told(civ, SocialKind::PeaceSummit, SocialKind::PeaceForced, w->Aggressor, w->Defender, nullptr, jd)) continue;
					auto where = CapitalOf(galaxy, w->Aggressor);
					auto e = Civilization::Make(SocialKind::PeaceMade, jd, where, Civilization::SystemOf(galaxy, where));
					e->Faction = w->Aggressor;
					e->Rival = w->Defender;
					e->Amount = cs::Math::Max(1.0, cs::Math::Floor(jd - w->StartJd));
					Tell(civ, galaxy, e);
				}
			}
			Wars.Clear();
			for (const auto& w : active) Wars.Add(w);
		}
		auto sc = galaxy.Polities != nullptr ? galaxy.Polities->Statecraft : nullptr;
		if (sc == nullptr) return;
		const auto& pacts = sc->Pacts();
		if (!quiet)
		{
			for (const auto& p : pacts)
			{
				if (Pacts.Contains(p) || p->A == nullptr || p->B == nullptr) continue;
				auto where = CapitalOf(galaxy, p->A);
				auto e = Civilization::Make(SocialKind::PactSigned, jd, where, Civilization::SystemOf(galaxy, where));
				e->Faction = p->A;
				e->Rival = p->B;
				e->Amount = p->Amount;
				e->Steered = p->Involves(commanded);
				Tell(civ, galaxy, e);
			}
			for (const auto& p : Pacts)
			{
				if (pacts.Contains(p) || p->A == nullptr || p->B == nullptr) continue;
				if (!cs::Double::IsNaN(p->UntilJd) && jd >= p->UntilJd - 1) continue; // it ran out
				auto where = CapitalOf(galaxy, p->A);
				auto e = Civilization::Make(SocialKind::PactBroken, jd, where, Civilization::SystemOf(galaxy, where));
				e->Faction = p->A;
				e->Rival = p->B;
				Tell(civ, galaxy, e);
			}
		}
		Pacts.Clear();
		for (const auto& p : pacts) Pacts.Add(p);
	}

	// ---------------------------------------------------------------- the markets' shocks

	void Witness::Markets(Civilization& civ, Galaxy& galaxy, double jd, bool quiet, const cs::List<cs::Ref<StarSystem>>& fresh)
	{
		auto commanded = Commanded(galaxy);
		cs::List<int> now;
		for (const auto& sys : galaxy.Systems)
		{
			if (sys->World == nullptr || sys->World->Shocks == nullptr) continue;
			for (const auto& sh : sys->World->Shocks->Active)
			{
				int key = ShockKey(*sh);
				if (now.Contains(key)) continue; // a state's rationing or embargo, shown in each of its systems
				now.Add(key);
				if (quiet || fresh.Contains(sys) || Shocks.Contains(key)) continue;
				SocialKind kind = SocialKind::None;
				switch (sh->Kind)
				{
					case ShockKind::MineAccident: kind = SocialKind::MineAccident; break;
					case ShockKind::Blight: kind = SocialKind::Blight; break;
					case ShockKind::Strike: kind = SocialKind::StrikeCalled; break;
					case ShockKind::Rationing: kind = SocialKind::Rationing; break;
					case ShockKind::Blockade: kind = SocialKind::Blockade; break;
					case ShockKind::Embargo: kind = SocialKind::Embargo; break;
					default: break; // a worked-out seam is the mine's news, not the people's
				}
				if (kind == SocialKind::None) continue;
				auto where = sh->Settlement != nullptr ? sh->Settlement : CapitalOf(galaxy, sh->Faction);
				if (where == nullptr) continue;
				auto e = Civilization::Make(kind, jd, where, sh->Settlement != nullptr ? sys : Civilization::SystemOf(galaxy, where));
				e->Amount = cs::Math::Max(0.0, cs::Math::Floor(sh->EndJd - sh->StartJd));
				switch (kind)
				{
					case SocialKind::Blockade:
						e->Rival = sh->Faction;
						e->Steered = commanded != nullptr && sh->Faction == commanded;
						break;
					case SocialKind::Embargo:
						e->Faction = sh->Faction;
						e->Rival = sh->Embargo != nullptr ? sh->Embargo->Against : nullptr;
						e->Steered = commanded != nullptr && sh->Faction == commanded;
						break;
					case SocialKind::Rationing:
						if (sh->Faction != nullptr) e->Faction = sh->Faction;
						break;
					default: break;
				}
				Tell(civ, galaxy, e);
			}
		}
		Shocks = now;
	}

	// ---------------------------------------------------------------- states that fell

	void Witness::States(Civilization& civ, Galaxy& galaxy, double jd, bool quiet)
	{
		cs::List<cs::Ref<Faction>> now;
		for (const auto& w : Worlds)
			if (w->Stood && w->Owner != nullptr && !w->Owner->IsPirate && w->Where != nullptr && !Haven(w->Where) && !now.Contains(w->Owner)) now.Add(w->Owner);
		if (!quiet)
			for (const auto& f : Standing)
			{
				if (now.Contains(f)) continue;
				int i = lostBy.Count() - 1;
				while (i >= 0 && lostBy[i] != f) i--;
				if (i < 0) continue; // its worlds went out of sight, not out of its hands
				auto e = Civilization::Make(SocialKind::FactionFallen, jd, lostWhere[i], Civilization::SystemOf(galaxy, lostWhere[i]));
				e->Faction = f;
				e->Rival = lostTo[i];
				e->Steered = lostSteered[i] != 0;
				Tell(civ, galaxy, e);
			}
		Standing = now;
		lostBy.Clear();
		lostTo.Clear();
		lostWhere.Clear();
		lostSteered.Clear();
	}

	// ================================================================ ObjectGraph registrations

	SG_SERIALIZE(WitnessedWorld, "SpaceGod.Core.WitnessedWorld",
		SG_F(Where), SG_F(System), SG_F(Owner), SG_F(OccupiedFrom), SG_F(Stood), SG_F(Population))

	SG_SERIALIZE(WitnessedSystem, "SpaceGod.Core.WitnessedSystem",
		SG_F(System), SG_F(Battles))

	SG_SERIALIZE(Witness, "SpaceGod.Core.Witness",
		SG_F(Primed), SG_F(Worlds), SG_F(Systems), SG_F(Wars), SG_F(Pacts), SG_F(Standing), SG_F(Shocks), SG_F(Witnessed))
}
