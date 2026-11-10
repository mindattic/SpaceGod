// US-T285: Culture (see Systems/Civilization/Culture.h).

#include "Systems/Civilization/Culture.h"

#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Persistence/ObjectGraph.h"
#include "Ships.h"
#include "Systems/Civilization/CivHash.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Civilization/Politics.h"
#include "Systems/Galaxy.h"
#include "Systems/GalaxyLife.h"
#include "Systems/Polities.h"
#include "Systems/Society.h"
#include "Systems/Statecraft.h"
#include "Systems/Territory.h"
#include "World.h"

namespace SpaceGod::Core
{
	using namespace Civ;

	namespace
	{
		const char* const TrendNames[] = { "chrome-thread jackets", "zero-g dance", "retro Earth music", "hull-tattoos", "algae cuisine", "holo-masks",
			"vintage flight suits", "crystal jewellery", "slow tea", "neon hair", "pilot boots", "ringside poetry" };

		bool PirateHeld(const cs::Ref<StarSystem>& sys)
		{
			if (sys == nullptr || sys->World == nullptr || sys->World->Economy == nullptr) return false;
			for (const auto& s : sys->World->Economy->Settlements)
				if (s->Kind == SettlementKind::Haven && !s->Fallen) return true;
			return false;
		}

		double Ly(const cs::Ref<StarSystem>& a, const cs::Ref<StarSystem>& b)
		{
			if (a == nullptr || b == nullptr) return 0;
			return (a->PositionLy - b->PositionLy).Length();
		}

		/// The worlds people travelled between last month (from, to), so faith and fashion can follow them.
		cs::List<cs::Ref<Trip>> RecentTrips(const Society& society, double jd)
		{
			cs::List<cs::Ref<Trip>> r;
			for (const auto& t : society.Trips)
				if ((t->Stage == TripStage::There || t->Stage == TripStage::Homeward || t->Stage == TripStage::Done) && t->ArriveJd > jd - 30 && t->From != nullptr && t->To != nullptr) r.Add(t);
			return r;
		}
	}

	const char* Culture::TrendName(int i) { return TrendNames[static_cast<std::size_t>(i < 0 ? 0 : i) % (sizeof(TrendNames) / sizeof(TrendNames[0]))]; }

	void Culture::Day(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		(void)society;
		React(civ, galaxy, jd);
	}

	void Culture::Month(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		Faiths(civ, galaxy, society, jd);
		Festivals(civ, galaxy, society, jd);
		League(civ, galaxy, society, jd);
		Trends(civ, galaxy, society, jd);
		Drift(civ, galaxy, jd);
		Talk(civ, galaxy, society, jd);
	}

	// ---------------------------------------------------------------- faiths

	void Culture::Faiths(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		auto faiths = civ.OrgsOf(OrgKind::Faith);
		// The pious who want faith above all found new ones.
		if (faiths.Count() < MaxFaiths)
			for (const auto& c : Civilization::Adults(society, jd))
			{
				if (!Civilization::Has(c, Trait::Pious) || c->Goal != Ambition::Faith || c->Faith != nullptr || c->House == nullptr || c->House->Home == nullptr) continue;
				if (!Roll(H(static_cast<std::uint32_t>(c->Id), DayOf(jd), 0xFA17u), 0.01)) continue;
				Preach(civ, galaxy, c, jd);
				faiths = civ.OrgsOf(OrgKind::Faith);
				break;
			}
		if (faiths.Count() == 0) return;

		// Faith travels: a world a pilgrim, a merchant or a visitor of another faith came from catches a little of it.
		for (const auto& t : RecentTrips(society, jd))
		{
			auto from = civ.CivicsOf(t->From), to = civ.CivicsOf(t->To);
			if (from == nullptr || to == nullptr || from->Faith == nullptr || !from->Faith->Active()) continue;
			if (to->Faith == nullptr || !to->Faith->Active()) { to->Faith = from->Faith; to->FaithShare = 0.03; }
			else if (to->Faith == from->Faith) to->FaithShare = cs::Math::Min(1.0, to->FaithShare + 0.01);
			else if (Roll(H(static_cast<std::uint32_t>(t->Id), 0xFA18u), from->FaithShare * 0.05) && to->FaithShare < 0.2) { to->Faith = from->Faith; to->FaithShare = 0.05; }
		}
		// And within a system, a faith strong on one world reaches the others.
		for (const auto& c : civ.Worlds)
		{
			if (c->Faith == nullptr || !c->Faith->Active())
			{
				c->Faith = nullptr;
				c->FaithShare = 0;
				for (const auto& d : civ.Worlds)
					if (d != c && d->System == c->System && d->Faith != nullptr && d->Faith->Active() && d->FaithShare > 0.4) { c->Faith = d->Faith; c->FaithShare = 0.02; break; }
				continue;
			}
			double before = c->FaithShare;
			c->FaithShare = cs::Math::Min(0.95, c->FaithShare + 0.02 * (0.5 + c->Faith->Strength) * (1 - c->FaithShare) * (c->Learning > 0.6 ? 0.5 : 1));
			if (before < 0.5 && c->FaithShare >= 0.5)
			{
				auto e = Civilization::Make(SocialKind::Conversion, jd, c->Where, c->System);
				e->Org = c->Faith;
				civ.Publish(galaxy, e);
			}
			// The people of the houses there come to it.
			for (const auto& h : society.HousesOf(c->Where))
				for (const auto& m : h->Members)
					if (m->Alive() && m->Faith != c->Faith && Roll(H(static_cast<std::uint32_t>(m->Id), DayOf(jd), 0xFA19u), c->FaithShare * 0.1 + (Civilization::Has(m, Trait::Pious) ? 0.05 : 0)))
					{
						if (m->Faith != nullptr) m->Faith->Members.Remove(m);
						m->Faith = c->Faith;
						c->Faith->Members.Add(m);
					}
		}
		// A faith's strength: the share of the galaxy's people it holds; its money: its believers' tithes.
		double total = 0;
		for (const auto& c : civ.Worlds) total += c->Where->Population;
		for (const auto& f : faiths)
		{
			double held = 0;
			int worlds = 0;
			cs::Ref<Civics> farthest;
			for (const auto& c : civ.Worlds)
				if (c->Faith == f)
				{
					held += c->Where->Population * c->FaithShare;
					if (c->FaithShare > 0.3) worlds++;
					if (c->Where != f->Seat && (farthest == nullptr || Ly(c->System, f->System) > Ly(farthest->System, f->System))) farthest = c;
				}
			f->Strength = total > 0 ? cs::Math::Min(1.0, held / total * 3) : 0;
			f->Funds += held * 0.01;
			if (held <= 0 && jd - f->FoundedJd > 720) { f->EndedJd = jd; continue; }
			std::uint32_t h = H(static_cast<std::uint32_t>(f->Id), DayOf(jd), 0x5C15u);
			// Schism: a faith spread wide splits; the far worlds go their own way.
			if (worlds >= 3 && farthest != nullptr && civ.OrgsOf(OrgKind::Faith).Count() < MaxFaiths && Roll(h, 0.02))
			{
				cs::Ref<Citizen> heretic;
				for (const auto& hh : society.HousesOf(farthest->Where))
					for (const auto& m : hh->Members)
						if (m->Alive() && m->Faith == f && m->AgeYears(jd) > 25 && heretic == nullptr) heretic = m;
				auto split = civ.Found(OrgKind::Faith, farthest->Where, farthest->System, nullptr, heretic, f->Funds * 0.2, jd, h);
				f->Funds *= 0.8;
				split->Parent = f;
				split->Rival = f;
				f->Rival = split;
				split->Stance = -f->Stance;
				farthest->Faith = split;
				auto e = Civilization::Make(SocialKind::Schism, jd, farthest->Where, farthest->System);
				e->Org = f;
				e->OtherOrg = split;
				e->Who = heretic;
				civ.Publish(galaxy, e);
			}
			// Pilgrims travel to the seat.
			if (f->Seat != nullptr && Roll(h >> 6, 0.4))
				for (const auto& m : f->Members)
					if (m->Alive() && Civilization::Has(m, Trait::Pious) && m->House != nullptr && m->House->Home != f->Seat)
					{
						if (society.Book(galaxy, m, f->Seat, TripPurpose::Pilgrimage, f->Leader, jd) == nullptr) continue;
						auto e = Civilization::Make(SocialKind::Pilgrimage, jd, f->Seat, f->System);
						e->Org = f;
						e->Who = m;
						civ.Publish(galaxy, e);
						break;
					}
			// Holy war: a state of this faith against a state keeping its rival.
			if (f->Rival == nullptr || !f->Rival->Active() || galaxy.Polities == nullptr || galaxy.Polities->Statecraft == nullptr || !Roll(h >> 11, 0.03)) continue;
			cs::Ref<Faction> faithful, infidel;
			for (const auto& c : civ.Worlds)
			{
				if (c->FaithShare < 0.5) continue;
				if (c->Faith == f && faithful == nullptr) faithful = c->Where->Owner;
				if (c->Faith == f->Rival && infidel == nullptr) infidel = c->Where->Owner;
			}
			if (faithful == nullptr || infidel == nullptr || faithful == infidel) continue;
			auto sc = galaxy.Polities->Statecraft;
			sc->Sway(faithful, infidel, -10);
			auto e = Civilization::Make(SocialKind::HolyWar, jd, f->Seat, f->System);
			e->Org = f;
			e->Faction = faithful;
			e->Rival = infidel;
			civ.Publish(galaxy, e);
			auto regime = civ.Politics != nullptr ? civ.Politics->RegimeOf(faithful) : nullptr;
			if (regime != nullptr && regime->Hawk > 0.3 && !Civilization::Hostile(galaxy, faithful, infidel)) sc->DeclareWar(faithful, infidel, jd);
		}
	}

	// ---------------------------------------------------------------- festivals

	void Culture::Festival(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Settlement>& s, double jd, bool steered)
	{
		auto c = civ.CivicsOf(s);
		if (c == nullptr) return;
		for (const auto& b : society.Businesses)
			if (b->Trading() && b->Home == s && (b->Kind == BusinessKind::Hospitality || b->Kind == BusinessKind::TradingHouse)) b->Capital *= 1.03;
		c->Approval = cs::Math::Min(1.0, c->Approval + 0.05);
		auto e = Civilization::Make(SocialKind::Festival, jd, s, c->System);
		e->Steered = steered;
		civ.Publish(galaxy, e);
		// Visitors from other worlds of the same flag.
		int booked = 0;
		for (const auto& h : society.HousesOf(nullptr))
		{
			if (booked >= 3 || h->Home == s || h->Home == nullptr || h->Home->Owner != s->Owner) continue;
			for (const auto& m : h->Members)
				if (m->Alive() && m->AgeYears(jd) >= 18 && Roll(H(static_cast<std::uint32_t>(m->Id), DayOf(jd), 0xFE57u), 0.1) && society.Book(galaxy, m, s, TripPurpose::Festival, nullptr, jd) != nullptr) { booked++; break; }
		}
		// Raiders where pirates lurk.
		if (PirateHeld(c->System) && Roll(H(HashText(s->Name), DayOf(jd), 0xFE58u), 0.2)) civ.Stir(s, 0.03, -0.05);
	}

	void Culture::Festivals(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		double year = 365.25;
		for (const auto& c : civ.Worlds)
		{
			double day = static_cast<double>(H(HashText(c->Where->Name), 0xFE5u) % 365u);
			double now = cs::Math::Floor(jd - cs::Math::Floor(jd / year) * year);
			double since = now - day;
			if (since < 0) since += year;
			if (since < Civilization::MonthDays) Festival(civ, galaxy, society, c->Where, jd, false);
		}
	}

	// ---------------------------------------------------------------- the league

	void Culture::League(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		auto teams = civ.OrgsOf(OrgKind::Team);
		// The big worlds field a team, backed by their richest house.
		if (teams.Count() < MaxTeams)
			for (const auto& c : civ.Worlds)
			{
				if (c->Where->Population < 5e5) continue;
				bool has = false;
				for (const auto& t : teams)
					if (t->Seat == c->Where) has = true;
				if (has || !Roll(H(HashText(c->Where->Name), DayOf(jd), 0x7EA3u), 0.05)) continue;
				cs::Ref<Household> backer;
				for (const auto& h : society.HousesOf(c->Where))
					if (backer == nullptr || h->Wealth > backer->Wealth) backer = h;
				auto t = civ.Found(OrgKind::Team, c->Where, c->System, c->Where->Owner, backer != nullptr ? backer->Head() : nullptr, 1e6, jd, H(HashText(c->Where->Name), 0x7EA4u));
				t->Strength = 0.3 + 0.4 * Unit(H(static_cast<std::uint32_t>(t->Id), 0x7EA5u));
				teams.Add(t);
				break;
			}
		if (teams.Count() < 2) return;
		// A round: teams paired by the round's hash.
		Round++;
		std::uint32_t r = H(static_cast<std::uint32_t>(Season), static_cast<std::uint32_t>(Round), 0x7EA6u);
		int shift = 1 + static_cast<int>(r % static_cast<std::uint32_t>(teams.Count() - 1));
		for (int i = 0; i < teams.Count(); i += 2)
		{
			auto a = teams[i], b = teams[(i + shift) % teams.Count()];
			if (a == b) continue;
			std::uint32_t m = H(r, static_cast<std::uint32_t>(a->Id), static_cast<std::uint32_t>(b->Id));
			bool rigged = false;
			// A syndicate on a team's world fixes the game now and then.
			for (const auto& syn : civ.OrgsOf(OrgKind::Syndicate))
				if ((syn->Seat == a->Seat || syn->Seat == b->Seat) && Roll(H(m, static_cast<std::uint32_t>(syn->Id)), 0.05 * syn->Strength * 4)) { rigged = true; syn->Funds += 2e6; }
			bool aWins = rigged ? Roll(m >> 3, 0.5) : Unit(m) * (a->Strength + b->Strength) < a->Strength;
			auto w = aWins ? a : b, l = aWins ? b : a;
			w->Score++;
			w->Strength = cs::Math::Min(1.0, w->Strength + 0.01);
			l->Strength = cs::Math::Max(0.1, l->Strength - 0.005);
			auto e = Civilization::Make(rigged ? SocialKind::MatchRigged : SocialKind::MatchPlayed, jd, w->Seat, w->System);
			e->Org = w;
			e->OtherOrg = l;
			if (rigged || Roll(m >> 7, 0.25)) civ.Publish(galaxy, e);
		}
		// The season ends after a year: the champion's world celebrates, fans fly in for the final.
		if (Round < 12) return;
		cs::Ref<Org> champ;
		for (const auto& t : teams)
			if (champ == nullptr || t->Score > champ->Score) champ = t;
		auto e = Civilization::Make(SocialKind::Championship, jd, champ->Seat, champ->System);
		e->Org = champ;
		e->Amount = Season;
		civ.Publish(galaxy, e);
		if (auto c = civ.CivicsOf(champ->Seat)) c->Approval = cs::Math::Min(1.0, c->Approval + 0.08);
		if (champ->Owner != nullptr) champ->Owner->Wealth += 5e6;
		int fans = 0;
		for (const auto& h : society.HousesOf(nullptr))
		{
			if (fans >= 3 || h->Home == champ->Seat || h->Home == nullptr || h->Home->Owner != champ->Faction) continue;
			for (const auto& m : h->Members)
				if (m->Alive() && m->AgeYears(jd) >= 16 && Roll(H(static_cast<std::uint32_t>(m->Id), DayOf(jd), 0x7EA7u), 0.15) && society.Book(galaxy, m, champ->Seat, TripPurpose::Match, nullptr, jd) != nullptr) { fans++; break; }
		}
		for (const auto& t : teams) t->Score = 0;
		Season++;
		Round = 0;
	}

	// ---------------------------------------------------------------- fashion

	void Culture::Trends(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		for (const auto& c : civ.Worlds)
		{
			c->TrendHeat = cs::Math::Max(0.0, c->TrendHeat - 0.05);
			std::uint32_t h = H(HashText(c->Where->Name), DayOf(jd), 0x72E1u);
			if (c->TrendHeat < 0.1 && Roll(h, 0.04))
			{
				c->Trend = static_cast<int>((h >> 4) % (sizeof(TrendNames) / sizeof(TrendNames[0])));
				c->TrendHeat = 0.8;
				auto e = Civilization::Make(SocialKind::TrendSweeps, jd, c->Where, c->System);
				e->Amount = c->Trend;
				civ.Publish(galaxy, e);
			}
			if (c->TrendHeat > 0.5)
				for (const auto& b : society.Businesses)
					if (b->Trading() && b->Home == c->Where && (b->Kind == BusinessKind::TradingHouse || b->Kind == BusinessKind::Hospitality || b->Kind == BusinessKind::Media)) b->Capital *= 1.01;
		}
		// The hot fashions follow the travellers.
		for (const auto& t : RecentTrips(society, jd))
		{
			auto from = civ.CivicsOf(t->From), to = civ.CivicsOf(t->To);
			if (from == nullptr || to == nullptr || from->TrendHeat < 0.5 || to->TrendHeat > 0.3) continue;
			if (!Roll(H(static_cast<std::uint32_t>(t->Id), 0x72E2u), 0.3)) continue;
			to->Trend = from->Trend;
			to->TrendHeat = from->TrendHeat * 0.8;
		}
	}

	// ---------------------------------------------------------------- drift

	void Culture::Drift(Civilization& civ, Galaxy& galaxy, double jd)
	{
		if (galaxy.Polities == nullptr) return;
		auto life = galaxy.Territory != nullptr ? galaxy.Territory->Life : nullptr;
		for (const auto& c : civ.Worlds)
		{
			auto capital = galaxy.Polities->CapitalOf(c->Where->Owner);
			if (capital == nullptr) continue;
			auto home = civ.CivicsOf(capital);
			double ly = Ly(c->System, home != nullptr ? home->System : Civilization::SystemOf(galaxy, capital));
			bool otherFaith = home != nullptr && c->Faith != nullptr && c->FaithShare > 0.5 && c->Faith != home->Faith;
			double before = c->Drift;
			c->Drift = cs::Math::Clamp(c->Drift + ly * 0.0005 + (otherFaith ? 0.01 : 0) - (c->Where == capital ? 0.05 : 0.002) - (c->Approval > 0.4 ? 0.005 : 0), 0.0, 1.0);
			if (before < 0.5 && c->Drift >= 0.5)
			{
				auto e = Civilization::Make(SocialKind::CulturalDrift, jd, c->Where, c->System);
				civ.Publish(galaxy, e);
			}
			// A drifting world yearns to go its own way (GalaxyLife secedes it at 1).
			if (life != nullptr && c->Drift > 0.5)
				if (auto sl = life->LifeOf(c->Where)) sl->Yearning = cs::Math::Min(1.0, sl->Yearning + (c->Drift - 0.5) * 0.004);
		}
	}

	// ---------------------------------------------------------------- talk: rumours and propaganda

	void Culture::Talk(Civilization& civ, Galaxy& galaxy, Society& society, double jd)
	{
		std::uint32_t h = H(DayOf(jd), 0x2A1Eu);
		// A rumour about someone famous.
		cs::List<cs::Ref<Citizen>> famous;
		for (const auto& c : Civilization::Adults(society, jd))
			if (c->Reputation > 0.4) famous.Add(c);
		if (famous.Count() > 0 && Roll(h, 0.6)) Whisper(civ, galaxy, Pick(famous, h >> 3), jd);
		// Propaganda from states at war and from strongmen.
		if (civ.Politics == nullptr) return;
		for (const auto& r : civ.Politics->Regimes)
		{
			auto enemy = Civilization::EnemyOf(galaxy, r->Faction);
			if (enemy == nullptr && r->Liberty > -0.2) continue;
			if (!Roll(H(h, HashText(r->Faction->Name)), 0.2)) continue;
			auto capital = galaxy.Polities != nullptr ? galaxy.Polities->CapitalOf(r->Faction) : nullptr;
			auto e = Civilization::Make(SocialKind::Propaganda, jd, capital, Civilization::SystemOf(galaxy, capital));
			e->Faction = r->Faction;
			e->Rival = enemy;
			e->Unverified = true;
			civ.Publish(galaxy, e);
			for (const auto& c : civ.Worlds)
				if (c->Where->Owner == r->Faction)
				{
					double believed = c->PressTrust;
					c->Approval = cs::Math::Min(1.0, c->Approval + 0.03 * believed);
					c->Weariness = cs::Math::Max(0.0, c->Weariness - 0.02 * believed);
				}
		}
	}

	// ---------------------------------------------------------------- history

	void Culture::React(Civilization& civ, Galaxy& galaxy, double jd)
	{
		for (const auto& e : civ.Events.ToArray())
		{
			if (e->Id <= seenEvent) continue;
			seenEvent = e->Id;
			switch (e->Kind)
			{
				case SocialKind::Coup: case SocialKind::PeaceSummit: case SocialKind::Uprising: case SocialKind::HolyWar: case SocialKind::Schism:
				case SocialKind::FaithFounded: case SocialKind::Championship: case SocialKind::Disaster: case SocialKind::Outbreak: case SocialKind::ElectionWon:
				case SocialKind::Assassination: case SocialKind::BankRun: case SocialKind::PeaceForced: case SocialKind::Famine:
				{
					Chronicle.Add(e);
					if (Chronicle.Count() > MaxChronicle) Chronicle.RemoveAt(0);
					std::uint32_t h = H(static_cast<std::uint32_t>(e->Id), 0xC4A0u);
					// The greatest are carved in stone.
					bool great = e->Kind == SocialKind::Coup || e->Kind == SocialKind::PeaceSummit || e->Kind == SocialKind::Uprising || e->Kind == SocialKind::Championship || e->Kind == SocialKind::Disaster;
					if (great && e->Where != nullptr && Roll(h, 0.3))
						if (auto c = civ.CivicsOf(e->Where); c != nullptr && c->Monument.empty())
						{
							c->Monument = ToString(e->Kind);
							auto m = Civilization::Make(SocialKind::MonumentRaised, jd, e->Where, e->System);
							m->Org = e->Org;
							m->Faction = e->Faction;
							civ.Publish(galaxy, m);
						}
					if (Roll(h >> 7, 0.15))
					{
						auto ch = Civilization::Make(SocialKind::Chronicle, jd, e->Where, e->System);
						civ.Publish(galaxy, ch);
					}
					break;
				}
				default: break;
			}
		}
	}

	// ---------------------------------------------------------------- the SpaceGod's hand

	cs::String Culture::Preach(Civilization& civ, Galaxy& galaxy, const cs::Ref<Citizen>& prophet, double jd)
	{
		if (prophet == nullptr || !prophet->Alive() || prophet->House == nullptr || prophet->House->Home == nullptr) return "no such person living";
		auto home = prophet->House->Home;
		auto old = prophet->Faith;
		if (old != nullptr) old->Members.Remove(prophet);
		auto f = civ.Found(OrgKind::Faith, home, prophet->House->System, nullptr, prophet, 1e6, jd, H(static_cast<std::uint32_t>(prophet->Id), DayOf(jd), 0xFA1Au));
		f->Stance = Unit(H(static_cast<std::uint32_t>(f->Id), 0xFA1Bu)) * 2 - 1;
		f->Strength = 0.1;
		if (auto c = civ.CivicsOf(home)) { c->Faith = f; c->FaithShare = 0.1; }
		prophet->Reputation = cs::Math::Min(1.0, prophet->Reputation + 0.2);
		auto e = Civilization::Make(SocialKind::FaithFounded, jd, home, prophet->House->System);
		e->Org = f;
		e->Who = prophet;
		civ.Publish(galaxy, e);
		return "";
	}

	cs::String Culture::Celebrate(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Settlement>& s, double jd)
	{
		if (civ.CivicsOf(s) == nullptr) return "nobody watches that world yet";
		Festival(civ, galaxy, society, s, jd, true);
		return "";
	}

	cs::String Culture::Rig(Civilization& civ, Galaxy& galaxy, const cs::Ref<Org>& team, double jd)
	{
		if (team == nullptr || team->Kind != OrgKind::Team || !team->Active()) return "no such team";
		int top = 0;
		for (const auto& t : civ.OrgsOf(OrgKind::Team)) top = cs::Math::Max(top, t->Score);
		team->Score = top + 3;
		auto e = Civilization::Make(SocialKind::MatchRigged, jd, team->Seat, team->System);
		e->Org = team;
		e->Steered = true;
		civ.Publish(galaxy, e);
		return "";
	}

	cs::String Culture::Whisper(Civilization& civ, Galaxy& galaxy, const cs::Ref<Citizen>& about, double jd)
	{
		if (about == nullptr || !about->Alive()) return "no such person living";
		about->Reputation = cs::Math::Max(-1.0, about->Reputation - 0.05);
		auto e = Civilization::Make(SocialKind::Rumour, jd, about->House != nullptr ? about->House->Home : nullptr, about->House != nullptr ? about->House->System : nullptr);
		e->Who = about;
		e->House = about->House;
		e->Faction = about->House != nullptr && about->House->Home != nullptr ? about->House->Home->Owner : nullptr;
		e->Unverified = true;
		civ.Publish(galaxy, e);
		return "";
	}

	// ================================================================ ObjectGraph registrations

	SG_SERIALIZE(Culture, "SpaceGod.Core.Culture",
		SG_F(Chronicle), SG_F(Season), SG_F(Round), SG_F(seenEvent))
}
