// US-T137: comms (see Comms/Comms.h).

#include "Comms/Comms.h"

#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Combat/Morale.h"
#include "Combat/War.h"
#include "Comms/Pilots.h"
#include "Economy/CargoHold.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Information/Ranges.h" // US-T156
#include "Law/Law.h"
#include "Sensors/Sensors.h"
#include "Ships.h"
#include "World.h"

#include <vector>

namespace SpaceGod::Core
{
	const char* ToString(CommsVerb v)
	{
		switch (v)
		{
		case CommsVerb::Hail: return "Hail";
		case CommsVerb::AskAdvice: return "AskAdvice";
		case CommsVerb::AskNews: return "AskNews";
		case CommsVerb::AskTrade: return "AskTrade";
		case CommsVerb::Buy: return "Buy";
		case CommsVerb::AskStory: return "AskStory";
		case CommsVerb::Threaten: return "Threaten";
		case CommsVerb::AcceptSurrender: return "AcceptSurrender";
		case CommsVerb::RefuseSurrender: return "RefuseSurrender";
		case CommsVerb::Plunder: return "Plunder";
		case CommsVerb::LetGo: return "LetGo";
		case CommsVerb::SignOff: return "SignOff";
		}
		return "?";
	}

	cs::String VerbLabel(CommsVerb v)
	{
		switch (v)
		{
		case CommsVerb::Hail: return "Hail";
		case CommsVerb::AskAdvice: return "Ask about the area";
		case CommsVerb::AskNews: return "Ask the news";
		case CommsVerb::AskTrade: return "Ask to trade";
		case CommsVerb::Buy: return "Buy a crate";
		case CommsVerb::AskStory: return "Ask about them";
		case CommsVerb::Threaten: return "Threaten: cargo or your life";
		case CommsVerb::AcceptSurrender: return "Accept their surrender";
		case CommsVerb::RefuseSurrender: return "Refuse: no quarter";
		case CommsVerb::Plunder: return "Plunder: board and strip it";
		case CommsVerb::LetGo: return "Let them go";
		case CommsVerb::SignOff: return "Sign off";
		}
		return "?";
	}

	const char* ToString(CommsStance v)
	{
		switch (v)
		{
		case CommsStance::Friendly: return "Friendly";
		case CommsStance::Neutral: return "Neutral";
		case CommsStance::Wary: return "Wary";
		case CommsStance::Hostile: return "Hostile";
		}
		return "?";
	}

	const char* ToString(CommsOutcome v)
	{
		switch (v)
		{
		case CommsOutcome::None: return "None";
		case CommsOutcome::Talked: return "Talked";
		case CommsOutcome::Complied: return "Complied";
		case CommsOutcome::Ran: return "Ran";
		case CommsOutcome::Fought: return "Fought";
		case CommsOutcome::Refused: return "Refused";
		case CommsOutcome::Traded: return "Traded";
		case CommsOutcome::SurrenderTaken: return "SurrenderTaken";
		case CommsOutcome::NoQuarter: return "NoQuarter";
		case CommsOutcome::Plundered: return "Plundered";
		case CommsOutcome::LetGo: return "LetGo";
		}
		return "?";
	}

	// ---------------------------------------------------------------- ranges and bearings

	cs::String CommsRuler::Words(double km) { return Ranges::Spoken(km); }

	cs::String CommsRuler::Range(DVec3 fromHelio, DVec3 toHelio) const { return Ranges::Spoken(Ranges::Km(fromHelio, toHelio)); }

	cs::String Comms::Bearing(DVec3 fromHelio, DVec3 forward, DVec3 up, DVec3 toHelio)
	{
		auto d = toHelio - fromHelio;
		auto f = forward.LengthSquared() > 1e-18 ? forward.Normalized() : DVec3::UnitX;
		auto u = up - f * DVec3::Dot(up, f);
		if (u.LengthSquared() < 1e-18) u = cs::Math::Abs(f.Z) < 0.9 ? DVec3::UnitZ - f * f.Z : DVec3::UnitY - f * f.Y;
		u = u.Normalized();
		auto right = DVec3::Cross(f, u);
		double x = DVec3::Dot(d, f), y = DVec3::Dot(d, right), z = DVec3::Dot(d, u);
		double bearing = cs::Math::Atan2(y, x) * 180 / cs::Math::PI;
		double mark = cs::Math::Atan2(z, cs::Math::Sqrt(x * x + y * y)) * 180 / cs::Math::PI;
		int b = (static_cast<int>(cs::Math::Round(bearing)) + 360) % 360;
		int m = (static_cast<int>(cs::Math::Round(mark)) + 360) % 360;
		return cs::Cat("bearing ", b, " mark ", m);
	}

	// ---------------------------------------------------------------- parties

	cs::Ref<Faction> CommsParty::Faction() const
	{
		if (Ship != nullptr) return Ship->Faction;
		return Station != nullptr ? Station->Owner : nullptr;
	}

	cs::String CommsParty::Name() const
	{
		if (Ship != nullptr) return cs::Cat("the ", Ship->Name);
		return Station != nullptr ? cs::Cat(Station->Name, " traffic control") : cs::String("nobody");
	}

	int CommsParty::Key() const
	{
		if (Ship != nullptr) return Ship->Id;
		return Station != nullptr ? -(Station->Id + 1) : cs::Int::MinValue;
	}

	CommsParty Comms::Resolve(const cs::Ref<World>& world, int key)
	{
		if (world == nullptr) return CommsParty();
		if (key >= 0)
		{
			const auto& ships = world->Fleet->Ships();
			if (key < ships.Count() && ships[key] != nullptr && ships[key]->Id == key) return CommsParty::OfShip(ships[key]);
			for (const auto& s : ships)
				if (s != nullptr && s->Id == key) return CommsParty::OfShip(s);
			return CommsParty();
		}
		if (key == cs::Int::MinValue) return CommsParty();
		int id = -key - 1;
		for (const auto& s : world->Economy->Settlements)
			if (s != nullptr && s->Id == id) return CommsParty::OfStation(s);
		return CommsParty();
	}

	CommsReply Comms::Apply(const CommsContext& c, int targetKey, int verb)
	{
		if (verb < 0 || verb > static_cast<int>(CommsVerb::SignOff))
		{
			CommsReply r;
			r.Lines.Add({ "comms", "Garbled transmission.", false });
			return r;
		}
		return Say(c, Resolve(c.World, targetKey), static_cast<CommsVerb>(verb));
	}

	std::function<bool(const cs::Ref<Ship>&)>& Comms::AnonymousBroadcast()
	{
		static std::function<bool(const cs::Ref<Ship>&)> hook;
		return hook;
	}

	std::function<double(const CargoDrop&)>& Comms::Tractor()
	{
		static std::function<double(const CargoDrop&)> hook;
		return hook;
	}

	DVec3 Comms::HelioOf(const CommsContext& c, const CommsParty& p)
	{
		if (c.World == nullptr) return DVec3();
		if (p.Ship != nullptr) return p.Ship == c.Speaker ? c.HelioKm : c.World->Sensors->HelioOf(p.Ship, c.Jd);
		if (p.Station != nullptr) return c.World->Ephemeris->HelioAt(p.Station->Body, c.Jd);
		return DVec3();
	}

	namespace
	{
		bool Gone(const cs::Ref<Ship>& s) { return s == nullptr || s->Destroyed || s->GateDeparted || s->Scrapped; }

		bool HostileTo(const cs::Ref<World>& w, const cs::Ref<Faction>& a, const cs::Ref<Faction>& b)
		{
			if (a == nullptr || b == nullptr || a == b) return false;
			if (a->IsPirate != b->IsPirate) return true;
			if (a->IsPirate && b->IsPirate) return false;
			return w->Diplomacy() != nullptr && w->Diplomacy()->Hostile(a, b);
		}

		cs::Ref<ContactBook> SpeakerPicture(const CommsContext& c)
		{
			if (c.Speaker != nullptr && c.Speaker == c.World->PilotedShip) return c.World->Sensors->Player;
			return c.SpeakerFaction != nullptr ? c.World->Sensors->PictureOf(c.SpeakerFaction) : nullptr;
		}

		/// A pick from a list of lines by a seed (the same seed, the same line).
		const char* Pick(const std::vector<const char*>& lines, uint32_t seed)
		{
			return lines[Characters::Mix(seed) % lines.size()];
		}

		cs::String Fill(cs::String text, const cs::String& key, const cs::String& value) { return cs::Strings::Replace(text, cs::Cat("{", key, "}"), value); }

		cs::String GoodName(Good g) { return cs::Strings::ToLowerInvariant(GoodInfo::Of(g)->Name); }

		cs::String Who(const CommsParty& p, const cs::Ref<NamedPilot>& pilot)
		{
			if (pilot != nullptr) return pilot->Name;
			return p.IsStation() ? cs::Cat(p.Station->Name, " Control") : p.Name();
		}

		cs::String SpeakerName(const CommsContext& c)
		{
			if (c.Speaker != nullptr)
			{
				auto p = Characters::Of(c.World)->Find(c.Speaker);
				return p != nullptr ? p->Name : c.Speaker->Name;
			}
			return c.SpeakerStation != nullptr ? cs::Cat(c.SpeakerStation->Name, " Control") : cs::String("you");
		}
	}

	ContactLevel Comms::Seen(const CommsContext& c, const cs::Ref<Ship>& s)
	{
		if (c.World == nullptr || s == nullptr) return ContactLevel::None;
		// Its own side it knows as they are; anyone else as well as its picture holds them or its own scanners make them out now.
		if (c.SpeakerFaction != nullptr && s->Faction == c.SpeakerFaction) return ContactLevel::Identified;
		auto book = SpeakerPicture(c);
		auto level = book != nullptr ? book->LevelOf(s) : ContactLevel::None;
		if (c.Speaker != nullptr)
		{
			auto now = c.World->Sensors->Detect(c.Speaker, s, c.Jd);
			if (static_cast<int>(now) > static_cast<int>(level)) level = now;
		}
		return level;
	}

	cs::Ref<NamedPilot> Comms::PilotOf(const CommsContext& c, const CommsParty& p)
	{
		if (p.Ship == nullptr) return nullptr;
		return Characters::Of(c.World)->Meet(c.World, p.Ship, MeetKind::Hailed, c.Jd);
	}

	cs::List<CommsParty> Comms::Contacts(const CommsContext& c, int max)
	{
		cs::List<CommsParty> list;
		if (c.World == nullptr) return list;
		struct Entry { CommsParty P; double Km; int Key; };
		std::vector<Entry> found;
		for (const auto& s : c.World->Fleet->Ships())
		{
			if (Gone(s) || s == c.Speaker || s->Planetside() || s->Derelict) continue;
			double km = (c.World->Sensors->HelioOf(s, c.Jd) - c.HelioKm).Length();
			if (km > HailRangeKm || Seen(c, s) == ContactLevel::None) continue;
			found.push_back({ CommsParty::OfShip(s), km, s->Id });
		}
		for (const auto& s : c.World->Economy->Settlements)
		{
			if (s == nullptr || s->Fallen || s == c.SpeakerStation) continue;
			if (s->Hidden && !(c.SpeakerFaction != nullptr && s->KnownBy(c.SpeakerFaction))) continue;
			double km = (c.World->Ephemeris->HelioAt(s->Body, c.Jd) - c.HelioKm).Length();
			if (km > HailRangeKm && s->Body != c.FrameBody) continue;
			found.push_back({ CommsParty::OfStation(s), km, -(s->Id + 1) });
		}
		std::stable_sort(found.begin(), found.end(), [](const Entry& a, const Entry& b) { return a.Km != b.Km ? a.Km < b.Km : a.Key < b.Key; });
		for (const auto& e : found)
		{
			if (list.Count() >= max) break;
			list.Add(e.P);
		}
		return list;
	}

	CommsStance Comms::StanceOf(const CommsContext& c, const CommsParty& to)
	{
		auto them = to.Faction();
		auto us = c.SpeakerFaction;
		if (c.World == nullptr || them == nullptr) return CommsStance::Neutral;
		if (them == us) return CommsStance::Friendly;
		if (HostileTo(c.World, them, us)) return CommsStance::Hostile;
		int score = 0;
		// The law: a hull its faction has a bounty on, by the speaker's own pilot's record.
		cs::Ref<LawRecord> rec;
		if (c.Speaker != nullptr && c.World->Law->Captains.TryGetValue(c.Speaker->Controller(), rec) && rec != nullptr)
		{
			if (rec->WantedBy(them)) score += them->IsMilitary || to.IsStation() ? 3 : 2;
			score += rec->StandingWith(them) < -10 ? 1 : 0;
		}
		// What the speaker's faction has done to theirs, through comms.
		double harm = Characters::Of(c.World)->GrievanceOf(them, us);
		score += harm > 5e5 ? 2 : harm > 5e4 ? 1 : 0;
		// The pilot's own grudges.
		auto p = to.Ship != nullptr ? Characters::Of(c.World)->Find(to.Ship) : nullptr;
		if (p != nullptr)
		{
			auto me = SpeakerName(c);
			if (p->Grudges.Contains(me)) score += 3;
			if (us != nullptr && p->Grudges.Contains(us->Name)) score += 1;
			if (p->Warmth > 0.7) score -= 1;
		}
		if (score >= 3) return CommsStance::Hostile;
		if (score >= 1) return CommsStance::Wary;
		return p != nullptr && p->Warmth > 0.75 ? CommsStance::Friendly : CommsStance::Neutral;
	}

	cs::Ref<BattleUnit> Comms::SurrenderHail(const CommsContext& c)
	{
		if (c.Speaker == nullptr || c.Speaker->Battle == nullptr) return nullptr;
		auto battle = c.Speaker->Battle;
		auto me = battle->UnitOf(c.Speaker);
		return me != nullptr ? battle->AwaitingTermsFrom(me->Side) : nullptr;
	}

	cs::List<CommsVerb> Comms::Verbs(const CommsContext& c, const CommsParty& to)
	{
		cs::List<CommsVerb> v;
		if (!c.Piloted || to.IsNone()) return v;
		auto hailing = SurrenderHail(c);
		if (hailing != nullptr && to.Ship != nullptr && hailing->Ship == to.Ship)
		{
			v.Add(CommsVerb::AcceptSurrender);
			v.Add(CommsVerb::Plunder);
			v.Add(CommsVerb::LetGo);
			v.Add(CommsVerb::RefuseSurrender);
			v.Add(CommsVerb::SignOff);
			return v;
		}
		v.Add(CommsVerb::Hail);
		v.Add(CommsVerb::AskAdvice);
		v.Add(CommsVerb::AskNews);
		v.Add(CommsVerb::AskTrade);
		if (to.Ship != nullptr && to.Ship->HasCargo && to.Ship->CargoTonnes > 0.5 && c.Speaker != nullptr) v.Add(CommsVerb::Buy);
		if (to.Ship != nullptr) v.Add(CommsVerb::AskStory);
		if (to.Ship != nullptr && c.Speaker != nullptr) v.Add(CommsVerb::Threaten);
		v.Add(CommsVerb::SignOff);
		return v;
	}

	// ---------------------------------------------------------------- what is said

	cs::String Comms::Greeting(const CommsContext& c, const CommsParty& to, CommsStance stance, const cs::Ref<NamedPilot>& p)
	{
		uint32_t seed = static_cast<uint32_t>(p != nullptr ? p->Seed + p->TimesHailed * 31 : to.Key() * 131 + static_cast<int>(c.Jd));
		if (to.IsStation())
		{
			static const std::vector<const char*> friendly{ "{name}, this is {station}. Good to hear you. Lanes are open.", "{station} to {name}: welcome in. Say what you need.",
				"{station} here. Always a berth for {faction} ships." };
			static const std::vector<const char*> neutral{ "{station} traffic control. State your business, {name}.", "This is {station}. Go ahead, {name}.",
				"{station} control, reading you. Keep to the approach lanes." };
			static const std::vector<const char*> wary{ "{station} to {name}: we have your record on file. Keep your distance.", "{station}. We're watching you, {name}.",
				"{name}, {station}. Mind yourself on our approach." };
			static const std::vector<const char*> hostile{ "{station} to {name}: you are not welcome here. Turn about.", "{station}: our guns are tracking you, {name}.",
				"{name}, {station} will open fire if you close. This is your only warning." };
			const auto& pool = stance == CommsStance::Friendly ? friendly : stance == CommsStance::Neutral ? neutral : stance == CommsStance::Wary ? wary : hostile;
			cs::String line = Pick(pool, seed);
			line = Fill(line, "station", to.Station->Name);
			line = Fill(line, "name", c.Speaker != nullptr ? c.Speaker->Name : SpeakerName(c));
			line = Fill(line, "faction", c.SpeakerFaction != nullptr ? c.SpeakerFaction->Name : cs::String("free"));
			return line;
		}
		bool pirate = to.Ship->Faction != nullptr && to.Ship->Faction->IsPirate;
		bool chatty = p != nullptr && p->Chatter > 0.55;
		cs::String intro = p == nullptr ? cs::String()
			: p->TimesHailed <= 1 ? cs::Cat("This is ", p->Styled(), " of the ", to.Ship->Name, ". ")
			: cs::Cat(p->Name.substr(0, p->Name.find(' ')), " here. ");
		static const std::vector<const char*> friendlyTerse{ "Go ahead.", "Reading you. What do you need?", "Good to hear you." };
		static const std::vector<const char*> friendlyChat{ "Good to hear a friendly voice out here. What can I do for you?", "Well, hello! Not many ships say hello these days. Go on.",
			"Always glad of company on a long burn. What's on your mind?" };
		static const std::vector<const char*> neutralTerse{ "Go ahead.", "State your business.", "Reading you." };
		static const std::vector<const char*> neutralChat{ "Reading you fine. What's this about?", "Go ahead. Make it quick, I've a schedule to keep.",
			"Hello yourself. What can I do for you?" };
		static const std::vector<const char*> wary{ "I know who you are. What do you want?", "Keep your distance and keep it short.", "Your reputation's ahead of you. Speak." };
		static const std::vector<const char*> hostile{ "You've got some nerve hailing me.", "Say your piece before my gunners lose patience.", "We've nothing to say to you." };
		static const std::vector<const char*> pirateLines{ "Speak fast, or speak to my guns.", "Well, well. Who's this, then?", "You're on our lanes. Talk.",
			"Make it worth my while." };
		const std::vector<const char*>* pool = &neutralTerse;
		if (pirate && stance != CommsStance::Friendly) pool = &pirateLines;
		else if (stance == CommsStance::Friendly) pool = chatty ? &friendlyChat : &friendlyTerse;
		else if (stance == CommsStance::Neutral) pool = chatty ? &neutralChat : &neutralTerse;
		else if (stance == CommsStance::Wary) pool = &wary;
		else pool = &hostile;
		return cs::Cat(intro, Pick(*pool, seed));
	}

	cs::String Comms::Advice(const CommsContext& c, const CommsParty& to, CommsStance stance, const cs::Ref<NamedPilot>& p)
	{
		auto them = to.Faction();
		bool pirate = them != nullptr && them->IsPirate;
		if (stance == CommsStance::Hostile)
			return pirate ? "Advice? Here's some: turn around." : "Find your own way. We don't help your kind.";
		if (them == nullptr) return "Nothing on our scopes worth telling.";
		auto book = c.World->Sensors->PictureOf(them);
		// The tracks their side holds of anything that would go for the speaker: raiders and the speaker's enemies.
		struct Threat { cs::Ref<Contact> T; double Km; int Group; };
		std::vector<Threat> threats;
		cs::Dictionary<cs::Ref<Ship>, int> groups;
		for (const auto& t : book->Contacts())
		{
			auto s = t->Ship;
			if (Gone(s) || s == c.Speaker || s == to.Ship || s->Planetside() || s->Derelict) continue;
			if (c.Jd - t->LastSeenJd > 0.25) continue; // a cold track nobody would vouch for
			if (!HostileTo(c.World, s->Faction, c.SpeakerFaction)) continue;
			auto lead = s->Controller();
			int n = 0;
			groups.TryGetValue(lead, n);
			groups[lead] = n + 1;
			if (n > 0) continue;
			threats.push_back({ t, (t->LastHelioKm - c.HelioKm).Length(), s->Id });
		}
		std::stable_sort(threats.begin(), threats.end(), [](const Threat& a, const Threat& b) { return a.Km != b.Km ? a.Km < b.Km : a.Group < b.Group; });
		cs::String said;
		int told = 0;
		for (const auto& th : threats)
		{
			if (told >= 2 || th.Km > 5 * HailRangeKm) break;
			auto s = th.T->Ship;
			int n = 0;
			groups.TryGetValue(s->Controller(), n);
			cs::String what;
			bool raider = s->Faction != nullptr && s->Faction->IsPirate;
			if (th.T->Level == ContactLevel::Identified)
				what = raider ? (n > 1 ? cs::Cat(n, " pirate hulls under the ", s->Name) : cs::Cat("pirates, the ", s->Name))
					: (n > 1 ? cs::Cat(n, " ", s->Faction->Name, " warships") : cs::Cat("a ", s->Faction->Name, " ", cs::Strings::ToLowerInvariant(s->Spec->DisplayName)));
			else if (th.T->Level == ContactLevel::Class) what = cs::Cat("an unidentified ", cs::Strings::ToLowerInvariant(s->Spec->DisplayName), raider ? " running without a transponder" : "");
			else what = "something running dark";
			auto line = cs::Cat("Watch out for ", what, " ", c.Ruler.Range(c.HelioKm, th.T->LastHelioKm), " out at ",
				Bearing(c.HelioKm, c.Forward, c.Up, th.T->LastHelioKm), ".");
			said = said.empty() ? line : cs::Cat(said, " ", line);
			told++;
		}
		// A fight under way nearby, wherever it is.
		cs::Ref<Battle> nearest;
		double best = HailRangeKm * 2;
		DVec3 at;
		for (const auto& b : c.World->War->Battles)
		{
			if (b == nullptr || (c.Speaker != nullptr && b == c.Speaker->Battle)) continue;
			auto h = c.World->Law->BattleHelio(b, c.Jd);
			double km = (h - c.HelioKm).Length();
			if (km < best)
			{
				best = km;
				nearest = b;
				at = h;
			}
		}
		if (nearest != nullptr)
		{
			auto line = cs::Cat("There's a fight on, ", nearest->Name, ", ", c.Ruler.Range(c.HelioKm, at), " out at ", Bearing(c.HelioKm, c.Forward, c.Up, at), ".");
			said = said.empty() ? line : cs::Cat(said, " ", line);
		}
		if (said.empty())
		{
			// Nothing hostile: name the nearest friendly patrol, if the speaker is on good terms with it.
			cs::Ref<Contact> patrol;
			double pk = HailRangeKm * 2;
			for (const auto& t : book->Contacts())
			{
				auto s = t->Ship;
				if (Gone(s) || s == c.Speaker || s->Spec == nullptr || !s->Spec->IsMilitary || s->Faction == nullptr || s->Faction->IsPirate) continue;
				if (HostileTo(c.World, s->Faction, c.SpeakerFaction) || c.Jd - t->LastSeenJd > 0.25) continue;
				double km = (t->LastHelioKm - c.HelioKm).Length();
				if (km < pk)
				{
					pk = km;
					patrol = t;
				}
			}
			said = "Scopes are clean. Quiet as I've seen it.";
			if (patrol != nullptr)
				said = cs::Cat(said, " There's a ", patrol->Ship->Faction->Name, " patrol ", c.Ruler.Range(c.HelioKm, patrol->LastHelioKm), " out at ",
					Bearing(c.HelioKm, c.Forward, c.Up, patrol->LastHelioKm), " if you need it.");
		}
		if (p != nullptr && p->Chatter > 0.6 && stance == CommsStance::Friendly) said = cs::Cat(said, " Fly safe out there.");
		return said;
	}

	cs::String Comms::News(const CommsContext& c, const CommsParty& to)
	{
		int want = to.IsStation() ? 3 : 2;
		cs::String said;
		int told = 0;
		for (int i = c.World->Events.Count() - 1; i >= 0 && told < want; i--)
		{
			const auto& ev = c.World->Events[i];
			if (c.Jd - ev.Jd > 7) break;
			if (ev.Kind == WorldEventKind::Info || ev.Kind == WorldEventKind::Launch || ev.Kind == WorldEventKind::Opportunity) continue;
			said = said.empty() ? cs::Cat("Word on the lanes: ", ev.Text, ".") : cs::Cat(said, " And ", ev.Text, ".");
			told++;
		}
		return said.empty() ? cs::String("Nothing worth repeating. Quiet week.") : said;
	}

	cs::String Comms::TradeOffer(const CommsContext&, const CommsParty& to, CommsStance stance, const cs::Ref<NamedPilot>& p, double& pricePerTonne)
	{
		pricePerTonne = 0;
		if (stance == CommsStance::Hostile) return "Trade? With you? No.";
		if (to.IsStation())
		{
			auto s = to.Station;
			int sell = -1, buy = -1;
			double sellRatio = 1e9, buyRatio = 0;
			for (int g = 0; g < GoodInfo::Count; g++)
			{
				double base = GoodInfo::Of(static_cast<Good>(g))->BasePrice;
				if (base <= 0) continue;
				double r = s->Price[g] / base;
				if (s->Stock[g] > 1 && r < sellRatio) { sellRatio = r; sell = g; }
				if (r > buyRatio) { buyRatio = r; buy = g; }
			}
			cs::String line = cs::Cat("Dock at ", s->Name, " to trade.");
			if (sell >= 0) line = cs::Cat(line, " We're long on ", GoodName(static_cast<Good>(sell)), " at ", cs::Fmt(s->Price[sell], "N0"), " a tonne.");
			if (buy >= 0 && buy != sell) line = cs::Cat(line, " Paying ", cs::Fmt(s->Price[buy], "N0"), " a tonne for ", GoodName(static_cast<Good>(buy)), ".");
			return line;
		}
		auto ship = to.Ship;
		if (ship->Spec != nullptr && ship->Spec->IsMilitary && !(ship->Faction != nullptr && ship->Faction->IsPirate))
			return "This is a warship, not a market stall.";
		if (!ship->HasCargo || ship->CargoTonnes <= 0.5)
		{
			if (ship->PlunderTonnes() > 0.5) return "Nothing I'd sell to a stranger.";
			return "Hold's empty. Try the stations.";
		}
		double base = GoodInfo::Of(ship->CargoGood)->BasePrice;
		double factor = stance == CommsStance::Friendly ? 0.95 : stance == CommsStance::Neutral ? 1.1 : 1.35;
		if (p != nullptr) factor += 0.3 * p->Greed - 0.1 * p->Warmth;
		pricePerTonne = cs::Math::Round(base * factor);
		return cs::Cat("I'm carrying ", cs::Fmt(ship->CargoTonnes, "N0"), " t of ", GoodName(ship->CargoGood), ". I'll let a crate go at ",
			cs::Fmt(pricePerTonne, "N0"), " a tonne.");
	}

	CommsReply Comms::Buy(const CommsContext& c, const CommsParty& to, CommsStance stance, const cs::Ref<NamedPilot>& p)
	{
		CommsReply r;
		double price = 0;
		auto offer = TradeOffer(c, to, stance, p, price);
		auto who = Who(to, p);
		if (price <= 0 || c.Speaker == nullptr)
		{
			r.Lines.Add({ who, offer, false });
			r.Outcome = CommsOutcome::Refused;
			return r;
		}
		auto ship = to.Ship;
		auto good = ship->CargoGood;
		double tonnes = cs::Math::Min(CargoFootprint::Of(good).Tonnes, ship->CargoTonnes);
		double cost = cs::Math::Round(price * tonnes);
		if (c.Speaker->Credits < cost)
		{
			r.Lines.Add({ who, cs::Cat("That's ", cs::Fmt(cost, "N0"), " credits. Come back when you have them."), false });
			r.Outcome = CommsOutcome::Refused;
			return r;
		}
		auto crate = c.Speaker->PersonalHold() != nullptr ? c.Speaker->PersonalHold()->AddFirstFit(good) : nullptr;
		if (crate == nullptr)
		{
			r.Lines.Add({ who, "You've no room for it. Clear your hold first.", false });
			r.Outcome = CommsOutcome::Refused;
			return r;
		}
		c.Speaker->Credits -= cost;
		ship->Credits += cost;
		double before = ship->CargoTonnes;
		ship->CargoTonnes -= tonnes;
		if (before > 0) ship->CargoCost *= ship->CargoTonnes / before;
		if (ship->CargoTonnes <= 1e-6)
		{
			c.World->Economy->Release(ship);
			ship->HasCargo = false;
			ship->CargoTonnes = 0;
			ship->CargoDestination = nullptr;
			ship->Cargo = cs::String();
		}
		auto chars = Characters::Of(c.World);
		auto me = chars->Find(c.Speaker);
		chars->Act(me, LifeEventKind::Traded, c.Jd, 0.2, { "good", GoodName(good), "victim", ship->Name });
		chars->Act(p, LifeEventKind::Traded, c.Jd, 0.1, { "good", GoodName(good), "victim", c.Speaker->Name });
		r.Lines.Add({ who, cs::Cat("Done. One crate of ", GoodName(good), ", ", cs::Fmt(tonnes, "0.#"), " t, coming across. Pleasure."), false });
		r.Outcome = CommsOutcome::Traded;
		r.Toast = cs::Cat("Bought ", cs::Fmt(tonnes, "0.#"), " t of ", GoodName(good), " from the ", ship->Name, " for ", cs::Fmt(cost, "N0"), " cr");
		return r;
	}

	double Comms::GroupStrength(const cs::Ref<World>& world, const cs::Ref<Ship>& s)
	{
		if (s == nullptr) return 0;
		auto lead = s->Controller();
		double total = 0;
		for (const auto& o : world->Fleet->Ships())
			if (!Gone(o) && o->Combat != nullptr && (o == lead || o->Leader == lead)) total += o->Combat->Strength();
		if (total <= 0 && s->Combat != nullptr) total = s->Combat->Strength();
		return total;
	}

	void Comms::BookPiracy(const CommsContext& c, const cs::Ref<Ship>& victim, const cs::String& detail)
	{
		auto law = c.World->Law;
		auto chars = Characters::Of(c.World);
		auto me = chars->Find(c.Speaker);
		// The pilot's own record, by the hull they command: never a record of the SpaceGod's. It stays theirs when it leaves.
		auto captain = c.Speaker->Controller();
		cs::Ref<LawRecord> rec;
		if (!law->Captains.TryGetValue(captain, rec) || rec == nullptr)
		{
			rec = cs::New<LawRecord>(me != nullptr ? me->Name : cs::Cat("the master of the ", captain->Name), captain);
			law->Captains[captain] = rec;
		}
		// Seen: the victim names the hull, unless a broadcast array hid it (US-T96) and its own sensors could not make it out.
		cs::String witness = victim->Name;
		auto& anon = AnonymousBroadcast();
		if (anon && anon(c.Speaker) && c.World->Sensors->Detect(victim, c.Speaker, c.Jd) != ContactLevel::Identified) witness = cs::String();
		if (victim->Faction != nullptr && victim->Faction->IsPirate) witness = cs::String(); // pirates report nothing
		law->Commit(rec, CrimeKind::Piracy, law->JurisdictionAt(c.FrameBody), victim->Faction, c.FrameBody, c.Jd, witness, detail);
	}

	double Comms::Robbery(const CommsContext& c, const cs::Ref<Ship>& victim, Good& good)
	{
		double tonnes = 0;
		if (victim->HasCargo && victim->CargoTonnes > 0)
		{
			good = victim->CargoGood;
			tonnes = victim->CargoTonnes;
			c.World->Economy->Release(victim);
			victim->HasCargo = false;
			victim->CargoTonnes = 0;
			victim->CargoDestination = nullptr;
			victim->Cargo = cs::String();
		}
		else if (victim->PlunderTonnes() > 0)
		{
			int best = 0;
			for (int g = 1; g < victim->Plunder.Count(); g++)
				if (victim->Plunder[g] > victim->Plunder[best]) best = g;
			good = static_cast<Good>(best);
			tonnes = victim->Plunder[best];
			victim->Plunder[best] = 0;
		}
		if (tonnes <= 0) return 0;
		// Dropped: US-T95's tractor takes it as canisters if it is there; what it leaves comes aboard as stolen crates.
		CargoDrop drop;
		drop.From = victim;
		drop.To = c.Speaker;
		drop.What = good;
		drop.Tonnes = tonnes;
		drop.HelioKm = c.World->Sensors->HelioOf(victim, c.Jd);
		drop.Jd = c.Jd;
		double left = tonnes;
		auto& tractor = Tractor();
		if (tractor) left -= cs::Math::Max(0.0, cs::Math::Min(left, tractor(drop)));
		auto hold = c.Speaker->PersonalHold();
		double crate = CargoFootprint::Of(good).Tonnes;
		while (hold != nullptr && left >= crate * 0.5)
		{
			auto item = hold->AddFirstFit(good);
			if (item == nullptr) break;
			item->Stolen = true;
			left -= crate;
		}
		return tonnes;
	}

	CommsReply Comms::Threaten(const CommsContext& c, const CommsParty& to, CommsStance stance, const cs::Ref<NamedPilot>& p)
	{
		CommsReply r;
		auto victim = to.Ship;
		auto chars = Characters::Of(c.World);
		auto me = chars->Find(c.Speaker);
		auto who = Who(to, p);
		r.Lines.Add({ SpeakerName(c), cs::Cat("This is the ", c.Speaker->Name, ". Drop your cargo and power down, or we open fire."), true });
		chars->Act(me, LifeEventKind::Threatened, c.Jd, 0.5, { "victim", victim->Name });
		if (victim->Battle != nullptr && victim->Battle == c.Speaker->Battle)
		{
			r.Lines.Add({ who, "We're already shooting at each other. Strike or be struck.", false });
			r.Outcome = CommsOutcome::Refused;
			return r;
		}
		// What decides it: their pilot's nerve and greed, what they carry, the two groups' real strength, who they are.
		double mine = GroupStrength(c.World, c.Speaker), theirs = GroupStrength(c.World, victim);
		double odds = mine / cs::Math::Max(1e-9, mine + theirs); // 1: hopeless for them
		bool hasCargo = (victim->HasCargo && victim->CargoTonnes > 0) || victim->PlunderTonnes() > 0;
		double worth = victim->HasCargo ? victim->CargoTonnes * GoodInfo::Of(victim->CargoGood)->BasePrice : 0;
		bool warship = victim->Spec != nullptr && victim->Spec->IsMilitary;
		bool pirate = victim->Faction != nullptr && victim->Faction->IsPirate;
		double courage = p != nullptr ? p->Courage : 0.5, greed = p != nullptr ? p->Greed : 0.5, spite = p != nullptr ? p->Spite : 0.5;
		double fast = c.Speaker->Spec != nullptr && victim->Spec != nullptr && victim->Spec->AccelG > c.Speaker->Spec->AccelG * 1.2 ? 1 : 0;
		int salt = p != nullptr ? p->Story.Count() : 0;
		int seed = p != nullptr ? p->Seed : victim->Id;
		double fight = 0.6 * courage + 0.9 * (1 - odds) + (warship ? 0.5 : 0) + (pirate ? 0.35 : 0) + 0.2 * spite + (stance == CommsStance::Hostile ? 0.2 : 0)
			+ 0.15 * Characters::Roll(seed, 900 + salt);
		double comply = hasCargo ? 0.5 * (1 - courage) + 0.9 * odds + 0.25 * (1 - greed) - (worth > 5e6 ? 0.15 : 0) + 0.15 * Characters::Roll(seed, 901 + salt) : -1;
		double run = 0.45 * (1 - courage) + 0.45 * fast + 0.2 * greed + 0.15 * Characters::Roll(seed, 902 + salt) - (warship ? 0.4 : 0);
		cs::String meName = me != nullptr ? me->Name : c.Speaker->Name;
		bool anonymous = AnonymousBroadcast() && AnonymousBroadcast()(c.Speaker);
		cs::String culprit = anonymous ? cs::String("a masked voice") : meName;

		if (comply >= fight && comply >= run)
		{
			Good good{};
			double tonnes = Robbery(c, victim, good);
			static const std::vector<const char*> yes{ "Alright! Alright. Cargo's going out the lock. Don't shoot.", "Dumping it now. You'll answer for this.",
				"Take it, take it. It's not worth dying over.", "Fine. It's insured anyway. Go to hell." };
			r.Lines.Add({ who, Pick(yes, static_cast<uint32_t>(seed + salt)), false });
			r.Outcome = CommsOutcome::Complied;
			if (tonnes > 0)
			{
				auto gname = GoodName(good);
				chars->Act(me, LifeEventKind::Robbed, c.Jd, 2, { "victim", victim->Name, "good", gname, "tonnes", cs::Fmt(tonnes, "N0") });
				if (me != nullptr)
				{
					me->Robberies++;
					// A lawful pilot who robs has crossed a line: the pirate ladder from here (the career the act chose).
					if (me->Track != CareerTrack::Pirate)
					{
						me->Track = CareerTrack::Pirate;
						me->Rank = 1;
						me->RankJd = c.Jd;
						chars->Act(me, LifeEventKind::TurnedPirate, c.Jd, 0, { "faction", c.SpeakerFaction != nullptr ? c.SpeakerFaction->Name : cs::String("nobody"), "place", me->Birthplace });
					}
				}
				if (p != nullptr)
				{
					p->TimesRobbed++;
					chars->Act(p, LifeEventKind::WasRobbed, c.Jd, 0, { "rival", culprit, "good", gname });
					chars->Grudge(p, culprit, c.Jd, "robbed");
				}
				chars->AddGrievance(victim->Faction, c.SpeakerFaction, tonnes * GoodInfo::Of(good)->BasePrice);
				BookPiracy(c, victim, cs::Cat(cs::Fmt(tonnes, "N0"), " t of ", gname, " taken from the ", victim->Name, " at gunpoint"));
				c.World->Log(c.Jd, cs::Cat("The ", victim->Name, " jettisons ", cs::Fmt(tonnes, "N0"), " t of ", gname, " under threat from ",
					anonymous ? cs::String("an unidentified ship") : cs::Cat("the ", c.Speaker->Name)), WorldEventKind::Law, c.FrameBody);
				r.Toast = cs::Cat("The ", victim->Name, " dumps ", cs::Fmt(tonnes, "N0"), " t of ", gname, " to you. Piracy: the ", c.Speaker->Name, "'s pilot answers for it");
			}
			else r.Toast = cs::Cat("The ", victim->Name, " had nothing to give");
		}
		else if (run >= fight)
		{
			static const std::vector<const char*> flee{ "Not today! All power to the drive!", "You'll have to catch us first.", "[the channel closes; the drive flares]",
				"Like hell. Burning for home." };
			r.Lines.Add({ who, Pick(flee, static_cast<uint32_t>(seed + salt + 1)), false });
			r.Outcome = CommsOutcome::Ran;
			if (p != nullptr) chars->Grudge(p, culprit, c.Jd, "threatened");
			if (c.Speaker == c.World->PilotedShip)
			{
				r.Battle = c.World->War->TryStartPlayerAttack(c.Speaker, victim, c.FrameBody, c.OffsetKm, c.Forward, c.Jd);
				auto u = r.Battle != nullptr ? r.Battle->UnitOf(victim) : nullptr;
				if (u != nullptr) u->Fleeing = true;
			}
			r.Toast = r.Battle != nullptr ? cs::Cat("The ", victim->Name, " runs: run it down") : cs::Cat("The ", victim->Name, " burns away");
		}
		else
		{
			static const std::vector<const char*> no{ "Come and take it.", "Wrong ship, friend. Weapons free!", "You picked the wrong hull to threaten.",
				"I've buried better pirates than you." };
			r.Lines.Add({ who, Pick(no, static_cast<uint32_t>(seed + salt + 2)), false });
			r.Outcome = CommsOutcome::Fought;
			if (p != nullptr) chars->Grudge(p, culprit, c.Jd, "threatened");
			chars->AddGrievance(victim->Faction, c.SpeakerFaction, 1e4);
			if (c.Speaker == c.World->PilotedShip) r.Battle = c.World->War->TryStartPlayerAttack(c.Speaker, victim, c.FrameBody, c.OffsetKm, c.Forward, c.Jd);
			r.Toast = r.Battle != nullptr ? cs::Cat("The ", victim->Name, " turns to fight") : cs::Cat("The ", victim->Name, " refuses: weapons hot");
		}
		return r;
	}

	cs::String Comms::SurrenderOffer(const CommsContext& c, const cs::Ref<BattleUnit>& u)
	{
		if (u == nullptr) return cs::String();
		auto ship = u->Ship;
		if (ship == nullptr) return cs::Cat("This is ", u->Name(), ". We yield. Hold your fire.");
		auto p = Characters::Of(c.World)->Meet(c.World, ship, MeetKind::Fought, c.Jd);
		bool pirate = ship->Faction != nullptr && ship->Faction->IsPirate;
		uint32_t seed = static_cast<uint32_t>(p->Seed + p->Surrenders);
		if (ship->HasCargo && ship->CargoTonnes > 0)
		{
			static const std::vector<const char*> cargo{ "We strike our colours! Take the {good}, all {tonnes} t of it. Just let us go.",
				"Enough! The {good}'s yours. Spare the crew.", "We yield. {tonnes} t of {good} for our lives. Deal?" };
			cs::String line = Pick(cargo, seed);
			line = Fill(line, "good", GoodName(ship->CargoGood));
			line = Fill(line, "tonnes", cs::Fmt(ship->CargoTonnes, "N0"));
			return line;
		}
		if (pirate)
		{
			static const std::vector<const char*> pir{ "Alright, alright! We yield! Take the loot, take the ship, just don't space us.",
				"You win. We're powering down. I'll come quietly.", "Hold your fire! Hold it! We're done." };
			return Pick(pir, seed);
		}
		static const std::vector<const char*> ship_{ "We strike our colours. The ship is yours. Spare the crew.", "This is the {ship}. We surrender. Please, there are wounded aboard.",
			"Cease fire! We yield the ship." };
		return Fill(Pick(ship_, seed), "ship", ship->Name);
	}

	CommsReply Comms::AnswerSurrender(const CommsContext& c, CommsVerb verb)
	{
		CommsReply r;
		auto u = SurrenderHail(c);
		if (u == nullptr)
		{
			r.Lines.Add({ "comms", "Nobody is hailing for terms.", false });
			return r;
		}
		auto battle = c.Speaker->Battle;
		auto ship = u->Ship;
		auto chars = Characters::Of(c.World);
		auto me = chars->Find(c.Speaker);
		auto p = ship != nullptr ? chars->Meet(c.World, ship, MeetKind::Fought, c.Jd) : nullptr;
		auto who = p != nullptr ? p->Name : u->Name();
		auto victimName = ship != nullptr ? ship->Name : u->Name();
		cs::String meName = me != nullptr ? me->Name : c.Speaker->Name;
		bool pirate = u->Faction != nullptr && u->Faction->IsPirate;
		bool lawfulCaptor = c.SpeakerFaction != nullptr && !c.SpeakerFaction->IsPirate;
		auto detain = [&]()
		{
			// A pirate taken alive by a lawful hull is turned in: the reform ladder from the cells up (US-T138).
			if (p == nullptr || !pirate || !lawfulCaptor) return;
			cs::String at = "a detention centre";
			for (const auto& s : c.World->Economy->Settlements)
				if (s != nullptr && s->Owner == c.SpeakerFaction && !s->Hidden && !s->Fallen)
				{
					at = s->Name;
					if (s->Body == c.FrameBody) break;
				}
			chars->Detain(p, at, c.SpeakerFaction, c.Jd);
			r.Toast = cs::Cat(p->Name, " is taken alive and turned in at ", at);
		};
		switch (verb)
		{
		case CommsVerb::AcceptSurrender:
		{
			bool cargo = ship != nullptr && ship->HasCargo;
			bool done = battle->OfferSurrenderTerms(u, cargo ? SurrenderTerms::RansomCargo : SurrenderTerms::TakeShip)
				|| battle->OfferSurrenderTerms(u, SurrenderTerms::RansomCargo) || battle->OfferSurrenderTerms(u, SurrenderTerms::LetGo);
			r.Lines.Add({ meName, "We accept. Power down and stand by.", true });
			r.Lines.Add({ who, cargo ? "Cargo's going out. Thank you." : "Understood. We're standing by for your boarders.", false });
			r.Outcome = done ? CommsOutcome::SurrenderTaken : CommsOutcome::Refused;
			if (p != nullptr)
			{
				p->Surrenders++;
				chars->Act(p, LifeEventKind::Surrendered, c.Jd, 0, { "rival", meName });
			}
			chars->Act(me, LifeEventKind::Spared, c.Jd, 1, { "victim", victimName });
			if (!cargo) detain();
			if (r.Toast.empty()) r.Toast = cs::Cat("The ", victimName, cargo ? " hands over its cargo" : " is yours");
			break;
		}
		case CommsVerb::Plunder:
		{
			bool done = battle->OfferSurrenderTerms(u, SurrenderTerms::TakeShip) || battle->OfferSurrenderTerms(u, SurrenderTerms::RansomCargo);
			r.Lines.Add({ meName, "Boarders are coming across. Everything aboard is ours.", true });
			r.Lines.Add({ who, "You'll strip us bare? Damn you.", false });
			r.Outcome = done ? CommsOutcome::Plundered : CommsOutcome::Refused;
			if (p != nullptr)
			{
				p->Surrenders++;
				chars->Act(p, LifeEventKind::Surrendered, c.Jd, 0, { "rival", meName });
				chars->Grudge(p, meName, c.Jd, "plundered");
			}
			chars->Act(me, LifeEventKind::Robbed, c.Jd, 2, { "victim", victimName, "good", ship != nullptr && ship->HasCargo ? GoodName(ship->CargoGood) : cs::String("everything") });
			if (ship != nullptr && !pirate && u->Faction != nullptr && !HostileTo(c.World, u->Faction, c.SpeakerFaction))
				BookPiracy(c, ship, cs::Cat("the ", ship->Name, " plundered after it struck"));
			chars->AddGrievance(u->Faction, c.SpeakerFaction, 2e5);
			detain();
			if (r.Toast.empty()) r.Toast = done ? cs::Cat("Boarders away to strip the ", victimName) : cs::Cat("Nobody on this side can board the ", victimName);
			break;
		}
		case CommsVerb::LetGo:
		{
			battle->OfferSurrenderTerms(u, SurrenderTerms::LetGo);
			r.Lines.Add({ meName, "Go. Don't let me see you again.", true });
			r.Lines.Add({ who, "We won't forget this.", false });
			r.Outcome = CommsOutcome::LetGo;
			chars->Act(me, LifeEventKind::Spared, c.Jd, 1, { "victim", victimName });
			if (p != nullptr)
			{
				p->Surrenders++;
				chars->Act(p, LifeEventKind::Surrendered, c.Jd, 0, { "rival", meName });
				p->Grudges.Remove(meName);
			}
			r.Toast = cs::Cat("The ", victimName, " is let go");
			break;
		}
		default:
		{
			// No quarter: it powers its guns up again, desperate.
			u->Surrendering = false;
			u->SurrenderTimer = 0;
			r.Lines.Add({ meName, "No quarter.", true });
			r.Lines.Add({ who, "Then we'll take you with us!", false });
			r.Outcome = CommsOutcome::NoQuarter;
			chars->Act(me, LifeEventKind::OwnedAct, c.Jd, 1, { "victim", victimName, "act", "refused quarter" });
			if (p != nullptr) chars->Grudge(p, meName, c.Jd, "no quarter");
			chars->AddGrievance(u->Faction, c.SpeakerFaction, 5e4);
			r.Toast = cs::Cat("Quarter refused: the ", victimName, " fights on");
			break;
		}
		}
		return r;
	}

	CommsReply Comms::Say(const CommsContext& c, const CommsParty& to, CommsVerb verb)
	{
		CommsReply r;
		if (c.World == nullptr || !c.Piloted)
		{
			r.Lines.Add({ "comms", "Nobody aboard to speak for you: take a ship or a station first.", false });
			return r;
		}
		if (verb == CommsVerb::AcceptSurrender || verb == CommsVerb::RefuseSurrender || verb == CommsVerb::Plunder || verb == CommsVerb::LetGo)
			return AnswerSurrender(c, verb);
		if (to.IsNone() || (to.Ship != nullptr && Gone(to.Ship)))
		{
			r.Lines.Add({ "comms", "No carrier. The channel is dead.", false });
			return r;
		}
		auto chars = Characters::Of(c.World);
		// The speaker's own pilot is a named character from the first word (US-T138), and so is whoever answers.
		if (c.Speaker != nullptr) chars->Meet(c.World, c.Speaker, MeetKind::Embodied, c.Jd);
		auto stance = StanceOf(c, to);
		auto p = PilotOf(c, to);
		auto who = Who(to, p);
		r.Outcome = CommsOutcome::Talked;
		// A raider lying dark makes no answer to a hail it would rather not hear.
		if (to.Ship != nullptr && verb != CommsVerb::Threaten)
		{
			bool unseen = Seen(c, to.Ship) == ContactLevel::Blip;
			if (unseen && stance == CommsStance::Hostile)
			{
				r.Lines.Add({ "comms", "Only static answers.", false });
				return r;
			}
		}
		switch (verb)
		{
		case CommsVerb::Hail:
			r.Lines.Add({ who, Greeting(c, to, stance, p), false });
			break;
		case CommsVerb::AskAdvice:
			r.Lines.Add({ SpeakerName(c), "What's it like out there?", true });
			r.Lines.Add({ who, Advice(c, to, stance, p), false });
			break;
		case CommsVerb::AskNews:
			r.Lines.Add({ SpeakerName(c), "Any news?", true });
			r.Lines.Add({ who, stance == CommsStance::Hostile ? cs::String("Read it in your obituary.") : News(c, to), false });
			break;
		case CommsVerb::AskTrade:
		{
			double price = 0;
			r.Lines.Add({ SpeakerName(c), "Looking to trade?", true });
			r.Lines.Add({ who, TradeOffer(c, to, stance, p, price), false });
			break;
		}
		case CommsVerb::Buy:
			return Buy(c, to, stance, p);
		case CommsVerb::AskStory:
		{
			r.Lines.Add({ SpeakerName(c), "Who am I talking to?", true });
			if (p == nullptr || stance == CommsStance::Hostile)
			{
				r.Lines.Add({ who, "None of your business.", false });
				break;
			}
			// They tell it themselves, in their own voice; a cagey pilot tells less.
			int max = 3 + static_cast<int>(p->Chatter * 5);
			auto story = Characters::LifeStory(p, c.Jd, true, max);
			cs::String said = cs::Cat("Name's ", p->Name, p->Callsign.empty() ? cs::String() : cs::Cat(", they call me ", p->Callsign), ". ",
				Careers::Title(p->Track, p->Rank).empty() ? cs::String() : cs::Cat("I'm a ", Careers::Title(p->Track, p->Rank), "."));
			for (const auto& line : story) said = cs::Cat(said, " ", line);
			r.Lines.Add({ who, said, false });
			break;
		}
		case CommsVerb::Threaten:
			return Threaten(c, to, stance, p);
		default:
			r.Lines.Add({ SpeakerName(c), "Signing off.", true });
			r.Lines.Add({ who, stance == CommsStance::Hostile ? cs::String("Good riddance.") : cs::String("Fly safe."), false });
			break;
		}
		return r;
	}
}
