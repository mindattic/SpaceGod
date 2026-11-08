// US-T139: BYOKey voices (Comms/StoryVoice.h).

#include "Comms/StoryVoice.h"

#include "Events/Opportunity.h"
#include "Events/StoryHooks.h"
#include "Ships.h"

#include <algorithm>

namespace SpaceGod::Core
{
	namespace
	{
		struct VoiceState
		{
			cs::Ref<ILlmClient> Client;
			PersonaProfile Persona;
			VoiceLog Log;
		};

		VoiceState& State()
		{
			static VoiceState state;
			return state;
		}

		bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

		cs::String TrimText(const cs::String& s)
		{
			size_t a = 0, b = s.size();
			while (a < b && IsSpace(s[a])) a++;
			while (b > a && IsSpace(s[b - 1])) b--;
			return s.substr(a, b - a);
		}

		cs::String Trait(const char* name, double v)
		{
			return cs::Cat(name, " ", v >= 0.75 ? "high" : v >= 0.55 ? "fairly high" : v > 0.45 ? "middling" : v > 0.25 ? "fairly low" : "low");
		}
	}

	// ---------------------------------------------------------------- persona

	PersonaProfile PersonaProfile::Parse(const cs::String& text)
	{
		PersonaProfile p;
		cs::String body = TrimText(text);
		size_t eol = body.find('\n');
		cs::String first = TrimText(body.substr(0, eol));
		if (first.size() > 5 && cs::Strings::ToLowerInvariant(first.substr(0, 5)) == "name:")
		{
			p.Name = TrimText(first.substr(5));
			body = eol == cs::String::npos ? cs::String() : TrimText(body.substr(eol + 1));
		}
		if (body.size() > MaxChars) body = body.substr(0, MaxChars);
		p.Text = body;
		return p;
	}

	// ---------------------------------------------------------------- the log

	cs::String VoiceLog::Find(const cs::String& key) const { return replies.GetValueOrDefault(key, cs::String()); }
	bool VoiceLog::Has(const cs::String& key) const { return replies.ContainsKey(key); }
	bool VoiceLog::Waiting(const cs::String& key) const { return waiting.ContainsKey(key); }
	bool VoiceLog::GaveUp(const cs::String& key) const { return failed.ContainsKey(key); }

	void VoiceLog::Record(const cs::String& key, const cs::String& text)
	{
		waiting.Remove(key);
		if (text.empty()) return;
		replies[key] = text;
		failed.Remove(key);
	}

	void VoiceLog::Asked(const cs::String& key) { waiting[key] = true; }

	void VoiceLog::Failed(const cs::String& key)
	{
		waiting.Remove(key);
		if (!replies.ContainsKey(key)) failed[key] = true;
	}

	cs::List<std::pair<cs::String, cs::String>> VoiceLog::Entries() const
	{
		cs::List<std::pair<cs::String, cs::String>> all;
		for (auto kv : replies) all.Add(std::make_pair(kv.first, kv.second));
		all.Sort([](const std::pair<cs::String, cs::String>& a, const std::pair<cs::String, cs::String>& b) { return a.first.compare(b.first); });
		return all;
	}

	void VoiceLog::Clear()
	{
		replies.Clear();
		waiting.Clear();
		failed.Clear();
	}

	// ---------------------------------------------------------------- the voice

	void StoryVoice::SetClient(cs::Ref<ILlmClient> client)
	{
		State().Client = client;
		Characters::SetTeller(client != nullptr ? cs::Ref<IStoryTeller>(cs::New<LlmStoryTeller>()) : nullptr);
	}

	const cs::Ref<ILlmClient>& StoryVoice::Client() { return State().Client; }
	void StoryVoice::SetPersona(const PersonaProfile& persona) { State().Persona = persona; }
	const PersonaProfile& StoryVoice::Persona() { return State().Persona; }
	VoiceLog& StoryVoice::Log() { return State().Log; }

	bool StoryVoice::Live()
	{
		const auto& c = State().Client;
		return c != nullptr && c->Ready();
	}

	cs::String StoryVoice::Voice(const VoiceRequest& request)
	{
		auto& log = Log();
		if (log.Has(request.Key)) return log.Find(request.Key);
		if (!Live() || log.Waiting(request.Key) || log.GaveUp(request.Key)) return cs::String();
		log.Asked(request.Key);
		cs::String key = request.Key;
		try
		{
			State().Client->Ask(request, [key](const cs::String& reply)
			{
				auto clean = Clean(reply);
				if (clean.empty()) Log().Failed(key);
				else Log().Record(key, clean);
			});
		}
		catch (...) { log.Failed(key); }
		// A client that answered at once (a cache, a test double) has recorded it already.
		return log.Find(key);
	}

	cs::String StoryVoice::Hook(const cs::Ref<Opportunity>& op)
	{
		if (op == nullptr) return cs::String();
		cs::String plain = StoryHooks::Tell(op);
		// Only a caller speaks: a call for help, or pods' beacon. A sprung lure is the raiders, who say nothing.
		if (op->Kind != OpportunityKind::Distress || op->Caller.empty()) return plain;
		auto voiced = Voice(ForHook(*op, plain));
		return voiced.empty() ? plain : voiced;
	}

	cs::String StoryVoice::BeatKey(const StoryBeat& beat)
	{
		if (beat.Pilot == nullptr || beat.Event == nullptr) return cs::String();
		const auto& e = beat.Event;
		// Owned acts read differently once their pilot is let go (the motive joins them): the key says which.
		bool owned = e->Owned && !beat.Pilot->Held && !e->Slot("motive").empty();
		return cs::Cat("beat:", beat.Pilot->Id, ":", ToString(e->Kind), ":", e->Seed, ":", cs::Fmt(e->Jd, "0.000000"), ":",
			beat.FirstPerson ? "1" : "3", owned ? ":owned" : "");
	}

	cs::String StoryVoice::HookKey(const Opportunity& op)
	{
		return cs::Cat("hook:", op.SourceKey, ":", op.Id, ":", cs::Fmt(op.SpawnJd, "0.000000"));
	}

	cs::String StoryVoice::Rules()
	{
		return "You voice characters in SpaceGod, a space game. You are given one line the game has already written and the facts it rests on. "
			"Reword that line in the character's own voice, as the persona below asks. Keep every name, place, number and fact in it, and add no "
			"new facts, names, events or promises: the game owns what happened. One to three sentences, plain text, no quotation marks around the "
			"whole reply, no stage directions, nothing about being an AI or a game.";
	}

	cs::String StoryVoice::PersonaBlock()
	{
		const auto& p = Persona();
		if (p.Empty()) return "Persona: none given. Use a natural, understated voice that suits the character.";
		return cs::Cat("Persona", p.Name.empty() ? cs::String() : cs::Cat(" (", p.Name, ")"), ":\n", p.Text);
	}

	VoiceRequest StoryVoice::ForBeat(const StoryBeat& beat, const cs::String& plain)
	{
		VoiceRequest r;
		r.Key = BeatKey(beat);
		const auto& p = beat.Pilot;
		auto t = p->Traits();
		cs::String sheet = cs::Cat(
			"Character sheet:\n",
			"Name: ", p->Name, p->Callsign.empty() ? cs::String() : cs::Cat(" (callsign ", p->Callsign, ")"), "\n",
			"Rank: ", p->Styled(), "\n",
			"Faction: ", p->Faction != nullptr ? p->Faction->Name : cs::String("none"), "\n",
			"Age: ", p->AgeAt(beat.NowJd), "; ", p->Pronoun == 0 ? "she/her" : "he/him", "; born on ", p->Birthplace.empty() ? cs::String("unknown") : p->Birthplace, "\n",
			"Ship: ", p->Ship != nullptr ? p->Ship->Name : (p->ShipName.empty() ? cs::String("none") : p->ShipName), "\n",
			"Temperament: ", Trait("courage", t.Courage), ", ", Trait("greed", t.Greed), ", ", Trait("honour", t.Honour), ", ",
			Trait("warmth", t.Warmth), ", ", Trait("spite", t.Spite), ", ", t.Chatter >= 0.5 ? "talks loosely and at length" : "talks in clipped, formal phrases", "\n",
			"Battles survived: ", p->BattlesSurvived, "; robberies: ", p->Robberies,
			p->Grudges.Count() > 0 ? cs::Cat("; holds a grudge against ", p->Grudges[p->Grudges.Count() - 1]) : cs::String());
		r.System = cs::Cat(Rules(), "\n\n", PersonaBlock(), "\n\n", sheet);
		r.Prompt = cs::Cat(beat.FirstPerson ? "Spoken by the character, in the first person, over comms:\n" : "Told of the character, in the third person:\n",
			plain);
		return r;
	}

	VoiceRequest StoryVoice::ForHook(const Opportunity& op, const cs::String& plain)
	{
		// Only what the call itself says: the caller, where, and what is wrong. (A lure's request is a stranded ship's,
		// word for word: the model cannot give away what it is not told.)
		VoiceRequest r;
		r.Key = HookKey(op);
		cs::String plight = op.UnderAttack ? "under attack right now" : op.DryTanks ? "drifting with empty propellant tanks" : "adrift after an interdiction pulse knocked out the drives";
		r.System = cs::Cat(Rules(), "\n\n", PersonaBlock(), "\n\nCharacter: whoever is on the comms aboard the ship ", op.Caller, ", ", plight,
			op.Place.empty() ? cs::String() : cs::Cat(", ", op.Place), ".");
		r.Prompt = cs::Cat("A distress call broadcast on the open band, in the first person:\n", plain);
		return r;
	}

	cs::String StoryVoice::Clean(const cs::String& reply)
	{
		cs::String s = TrimText(reply);
		for (auto& c : s) if (c == '\r' || c == '\n' || c == '\t') c = ' ';
		cs::String out;
		for (char c : s) if (!(c == ' ' && !out.empty() && out.back() == ' ')) out += c;
		if (out.size() >= 2 && out.front() == '"' && out.back() == '"') out = TrimText(out.substr(1, out.size() - 2));
		if (out.size() > MaxReplyChars)
		{
			// Cut at the last sentence end that fits, else at a word.
			size_t cut = out.find_last_of(".!?", MaxReplyChars);
			if (cut == cs::String::npos || cut < MaxReplyChars / 3) cut = out.find_last_of(' ', MaxReplyChars);
			out = TrimText(out.substr(0, cut == cs::String::npos ? MaxReplyChars : cut + 1));
		}
		return out;
	}

	// ---------------------------------------------------------------- the teller

	cs::String LlmStoryTeller::Tell(const StoryBeat& beat)
	{
		if (beat.Pilot == nullptr || beat.Event == nullptr) return cs::String();
		cs::String plain = templates.Tell(beat);
		if (plain.empty()) return plain;
		return StoryVoice::Voice(StoryVoice::ForBeat(beat, plain));
	}

	cs::String LlmStoryTeller::Motive(const cs::Ref<NamedPilot>& pilot, const cs::Ref<LifeEvent>& act)
	{
		// The motive is part of the sentence Tell rewords; on its own it stays the templates'.
		return templates.Motive(pilot, act);
	}
}
