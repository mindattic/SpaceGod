// US-T239: encounters as Prose conversations (Lore/Encounters.h, docs/PROSE.md).

#include "Lore/Encounters.h"

#include "Comms/Pilots.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"
#include "Lore/LoreKeys.h"
#include "Lore/LoreLibrary.h"
#include "Shared/Json/JsonObject.h"
#include "Combat/Battle.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	namespace
	{
		using Shared::Json::JsonArray;
		using Shared::Json::JsonObject;
		using Shared::Json::JsonValue;

		struct TagVerb
		{
			const char* Tag;
			CommsVerb Verb;
		};

		// Every verb a player can choose on the comms panel, in the panel's order (Comms::Verbs). The hail is how an encounter
		// starts, so it is no choice. Keep in step with Data/Prose/prose.config.json's "tags" and docs/PROSE.md.
		const TagVerb TagVerbs[] = {
			{ "rumours", CommsVerb::AskRumours },
			{ "advice", CommsVerb::AskAdvice },
			{ "news", CommsVerb::AskNews },
			{ "trade", CommsVerb::AskTrade },
			{ "buy", CommsVerb::Buy },
			{ "story", CommsVerb::AskStory },
			{ "threaten", CommsVerb::Threaten },
			{ "accept-surrender", CommsVerb::AcceptSurrender },
			{ "plunder", CommsVerb::Plunder },
			{ "let-go", CommsVerb::LetGo },
			{ "refuse-surrender", CommsVerb::RefuseSurrender },
			{ "sign-off", CommsVerb::SignOff },
		};

		cs::String GoodWords(Good g) { return cs::Strings::ToLowerInvariant(GoodInfo::Of(g)->Name); }

		cs::String Hold(const cs::Ref<Ship>& s)
		{
			if (s == nullptr || !s->HasCargo || s->CargoTonnes <= 0.5) return "nothing";
			return cs::Cat(cs::Fmt(s->CargoTonnes, "N0"), " t of ", GoodWords(s->CargoGood));
		}

		cs::String Lower(const char* s) { return cs::Strings::ToLowerInvariant(cs::String(s)); }
	}

	const cs::List<cs::String>& Encounters::Tags()
	{
		static const cs::List<cs::String> tags = []
		{
			cs::List<cs::String> t;
			for (const auto& tv : TagVerbs) t.Add(tv.Tag);
			return t;
		}();
		return tags;
	}

	bool Encounters::VerbOfTag(const cs::String& tag, CommsVerb& verb)
	{
		for (const auto& tv : TagVerbs)
			if (tag == tv.Tag)
			{
				verb = tv.Verb;
				return true;
			}
		return false;
	}

	const char* Encounters::TagOf(CommsVerb v)
	{
		for (const auto& tv : TagVerbs)
			if (tv.Verb == v) return tv.Tag;
		return "";
	}

	const cs::List<cs::String>& Encounters::Placeholders()
	{
		static const cs::List<cs::String> names{ "ship", "pilot", "callsign", "faction", "station", "system", "stance", "range", "cargo",
			"my-ship", "my-pilot", "my-faction", "my-cargo", "credits" };
		return names;
	}

	const char* Encounters::Name(EncounterKind k)
	{
		switch (k)
		{
		case EncounterKind::Hail: return "hail";
		case EncounterKind::Trader: return "trader";
		case EncounterKind::Patrol: return "patrol";
		case EncounterKind::Pirate: return "pirate";
		case EncounterKind::Distress: return "distress";
		case EncounterKind::Surrender: return "surrender";
		case EncounterKind::Station: return "station";
		}
		return "hail";
	}

	EncounterKind Encounters::KindOf(const CommsContext& c, const CommsParty& to)
	{
		if (to.IsStation()) return EncounterKind::Station;
		auto ship = to.Ship;
		if (ship == nullptr) return EncounterKind::Hail;
		auto hailing = Comms::SurrenderHail(c);
		if (hailing != nullptr && hailing->Ship == ship) return EncounterKind::Surrender;
		bool otherFight = ship->Battle != nullptr && (c.Speaker == nullptr || c.Speaker->Battle != ship->Battle);
		if (ship->OutOfPropellant || ship->Phase == ShipPhase::Adrift || otherFight) return EncounterKind::Distress;
		if (ship->Faction != nullptr && ship->Faction->IsPirate) return EncounterKind::Pirate;
		if (ship->Spec != nullptr && ship->Spec->IsMilitary) return EncounterKind::Patrol;
		return EncounterKind::Trader;
	}

	cs::String Encounters::TextName(EncounterKind k) { return cs::Cat("encounter-", Name(k)); }

	cs::List<cs::String> Encounters::Keys(EncounterKind k, const cs::String& pilotId, const cs::String& factionId)
	{
		cs::List<cs::String> keys;
		const cs::String name = TextName(k);
		if (!pilotId.empty()) keys.Add(LoreKeys::Named(pilotId, name));
		if (!factionId.empty()) keys.Add(LoreKeys::Named(factionId, name));
		keys.Add(LoreKeys::Named(LoreKeys::Universe, name));
		if (k != EncounterKind::Hail) keys.Add(LoreKeys::Named(LoreKeys::Universe, TextName(EncounterKind::Hail)));
		return keys;
	}

	const LoreText* Encounters::Find(const LoreLibrary& lib, const cs::List<cs::String>& keys)
	{
		for (const auto& key : keys)
		{
			const LoreText* t = lib.Find(key);
			if (t != nullptr && t->Kind == "conversation" && t->Payload != nullptr) return t;
		}
		return nullptr;
	}

	cs::String Encounters::Start(const LoreText& text) { return text.Payload != nullptr ? text.Payload->GetString("start") : cs::String(); }

	bool Encounters::Node(const LoreText& text, const cs::String& id, EncounterNode& node)
	{
		if (text.Payload == nullptr || id.empty()) return false;
		auto nodes = text.Payload->GetArray("nodes");
		if (!nodes) return false;
		for (const JsonValue& v : *nodes)
		{
			auto o = v.AsObject();
			if (!o || o->GetString("id") != id) continue;
			node = EncounterNode();
			node.Id = id;
			node.Speaker = o->GetString("speaker");
			node.Line = o->GetString("line");
			node.Next = o->GetString("next");
			if (auto choices = o->GetArray("choices"))
				for (const JsonValue& cv : *choices)
				{
					auto co = cv.AsObject();
					if (!co) continue;
					EncounterChoice ch;
					ch.Text = co->GetString("text");
					ch.Next = co->GetString("next");
					if (auto tags = co->GetArray("tags"))
						for (const JsonValue& tv : *tags)
							if (auto s = tv.AsString())
							{
								ch.Tags.Add(*s);
								CommsVerb verb;
								if (!ch.HasAction && VerbOfTag(*s, verb))
								{
									ch.HasAction = true;
									ch.Action = verb;
								}
							}
					if (!ch.Text.empty()) node.Choices.Add(std::move(ch));
				}
			return true;
		}
		return false;
	}

	cs::List<int> Encounters::Offered(const EncounterNode& node, const cs::List<CommsVerb>& verbs)
	{
		cs::List<int> out;
		for (int i = 0; i < node.Choices.Count(); i++)
		{
			const auto& ch = node.Choices[i];
			if (ch.HasAction && !verbs.Contains(ch.Action)) continue;
			out.Add(i);
		}
		return out;
	}

	const char* Encounters::OutcomeNode(CommsOutcome o)
	{
		switch (o)
		{
		case CommsOutcome::None: return "";
		case CommsOutcome::Talked: return "talked";
		case CommsOutcome::Complied: return "complied";
		case CommsOutcome::Ran: return "ran";
		case CommsOutcome::Fought: return "fought";
		case CommsOutcome::Refused: return "refused";
		case CommsOutcome::Traded: return "traded";
		case CommsOutcome::SurrenderTaken: return "surrender-taken";
		case CommsOutcome::NoQuarter: return "no-quarter";
		case CommsOutcome::Plundered: return "plundered";
		case CommsOutcome::LetGo: return "let-go";
		}
		return "";
	}

	bool Encounters::IsPlayerSpeaker(const cs::String& speaker, const cs::String& playerId)
	{
		if (speaker == "$player" || speaker == "$me" || speaker == "$hero") return true;
		return !playerId.empty() && speaker == playerId;
	}

	EncounterValues Encounters::Values(const CommsContext& c, const CommsParty& to)
	{
		EncounterValues v;
		for (const auto& name : Placeholders()) v[name] = cs::String();
		auto chars = c.World != nullptr ? Characters::Of(c.World) : nullptr;
		if (to.Ship != nullptr)
		{
			v["ship"] = to.Ship->Name;
			auto p = chars != nullptr ? chars->Find(to.Ship) : nullptr;
			v["pilot"] = p != nullptr ? p->Styled() : cs::Cat("the captain of the ", to.Ship->Name);
			if (p != nullptr) v["callsign"] = p->Callsign;
			v["cargo"] = Hold(to.Ship);
		}
		if (to.Station != nullptr) v["station"] = to.Station->Name;
		if (auto f = to.Faction()) v["faction"] = f->Name;
		if (!to.IsNone() && c.World != nullptr)
		{
			v["stance"] = Lower(ToString(Comms::StanceOf(c, to)));
			v["range"] = c.Ruler.Range(c.HelioKm, Comms::HelioOf(c, to));
		}
		if (c.Speaker != nullptr)
		{
			v["my-ship"] = c.Speaker->Name;
			auto me = chars != nullptr ? chars->Find(c.Speaker) : nullptr;
			v["my-pilot"] = me != nullptr ? me->Styled() : cs::Cat("the captain of the ", c.Speaker->Name);
			v["my-cargo"] = Hold(c.Speaker);
			v["credits"] = cs::Fmt(c.Speaker->Credits, "N0");
		}
		else if (c.SpeakerStation != nullptr)
		{
			v["my-ship"] = c.SpeakerStation->Name;
			v["my-pilot"] = cs::Cat(c.SpeakerStation->Name, " control");
		}
		if (c.SpeakerFaction != nullptr) v["my-faction"] = c.SpeakerFaction->Name;
		return v;
	}

	cs::String Encounters::EntityWords(const cs::String& id)
	{
		// Drop the type prefix (up to the first hyphen), then capitalise each remaining word.
		auto dash = id.find('-');
		cs::String rest = dash == cs::String::npos ? id : id.substr(dash + 1);
		cs::String out;
		bool start = true;
		for (char ch : rest)
		{
			if (ch == '-')
			{
				out += ' ';
				start = true;
				continue;
			}
			out += start && ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch;
			start = false;
		}
		return out;
	}

	cs::String Encounters::Fill(const cs::String& line, const EncounterValues& values, const std::function<cs::String(const cs::String&)>& entityName)
	{
		cs::String out;
		out.reserve(line.size());
		size_t i = 0;
		while (i < line.size())
		{
			size_t open = line.find('{', i);
			if (open == cs::String::npos)
			{
				out.append(line, i, cs::String::npos);
				break;
			}
			size_t close = line.find('}', open + 1);
			if (close == cs::String::npos)
			{
				out.append(line, i, cs::String::npos);
				break;
			}
			out.append(line, i, open - i);
			cs::String name = line.substr(open + 1, close - open - 1);
			const cs::String entityPrefix = "entity:";
			if (name.compare(0, entityPrefix.size(), entityPrefix) == 0)
			{
				cs::String id = name.substr(entityPrefix.size());
				cs::String said = entityName ? entityName(id) : cs::String();
				out += said.empty() ? EntityWords(id) : said;
			}
			else
			{
				auto it = values.find(name);
				if (it != values.end()) out += it->second;
				else out.append(line, open, close - open + 1);
			}
			i = close + 1;
		}
		return out;
	}
}
