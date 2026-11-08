#pragma once
// US-T139: BYOKey voices. A player who brings their own LLM API key and a persona profile hears the characters (a named
// pilot's life told, a stranded ship's call) reworded by that model; everyone else, and anyone offline or past the latency
// budget, gets the pregenerated pools (StoryTemplates.cpp, Events/StoryHooks.cpp), which are always the fallback.
//
// An API key brings a model's intelligence but no personality or memory: calls are stateless. So each request carries the
// player's persona profile (the voice to tell things in), the character sheet and the local facts, and asks for nothing
// the facts don't say. Replies are presentation only and are recorded (VoiceLog) under a key made from the sim's own state,
// so a rewind replays the recorded words instead of asking again, and a co-op seat that generated a reply can hand the
// same entry to its peers (VoiceLog::Record). Calls are asynchronous: the line shows from the pools until the reply lands.

#include "Cs/Cs.h"
#include "Comms/Pilots.h"

#include <functional>
#include <utility>

namespace SpaceGod::Core
{
	class Opportunity;

	/// <summary>The voice the player wants characters told in (US-T139): text they write or export from elsewhere.</summary>
	struct SPACEGODCORE_API PersonaProfile
	{
		/// <summary>What the player calls it ("Noir narrator"); from a first line "name: ...", else empty.</summary>
		cs::String Name;
		/// <summary>The profile itself, as sent with every request.</summary>
		cs::String Text;

		bool Empty() const { return Text.empty(); }

		/// <summary>A profile from a file's text: an optional first line "name: ..." names it, the rest is the profile
		/// (trimmed, capped at <see cref="MaxChars"/> so a pasted export cannot run up the player's bill).</summary>
		static PersonaProfile Parse(const cs::String& text);

		static constexpr size_t MaxChars = 4000;
	};

	/// <summary>One request to a BYOKey model (US-T139): what is asked, and under what key the reply is recorded.</summary>
	struct SPACEGODCORE_API VoiceRequest
	{
		cs::String Key;
		/// <summary>The standing instructions: the persona, the character sheet and the rules (keep every fact, add none).</summary>
		cs::String System;
		/// <summary>The line to reword and the facts it rests on.</summary>
		cs::String Prompt;
		/// <summary>How long the call may take before the pools stand in for good, seconds.</summary>
		double BudgetSeconds = 12;
		int MaxTokens = 1024;
	};

	/// <summary>
	/// A model the player brings their own key for (US-T139). The game's runtime implements it over HTTP; Core only asks.
	/// <see cref="Ask"/> must not block: it answers later, once, through <paramref name="done"/>, on the game thread, with the
	/// reply or empty (no key, offline, refused, past <see cref="VoiceRequest.BudgetSeconds"/>).
	/// </summary>
	class SPACEGODCORE_API ILlmClient
	{
	public:
		virtual ~ILlmClient() = default;
		virtual cs::String Name() const = 0;
		virtual bool Ready() const = 0;
		virtual void Ask(const VoiceRequest& request, std::function<void(const cs::String&)> done) = 0;
	};

	/// <summary>
	/// Every reply a BYOKey model has given (US-T139), by request key. A line once voiced reads the same from then on, after a
	/// rewind as before it; a key whose call failed is not asked again this session (the pools keep it).
	/// </summary>
	class SPACEGODCORE_API VoiceLog final
	{
	public:
		/// <summary>The recorded reply for <paramref name="key"/>, or empty.</summary>
		cs::String Find(const cs::String& key) const;
		bool Has(const cs::String& key) const;
		bool Waiting(const cs::String& key) const;
		bool GaveUp(const cs::String& key) const;

		/// <summary>A reply for <paramref name="key"/>: from this seat's model, or handed over by the co-op seat that asked.</summary>
		void Record(const cs::String& key, const cs::String& text);
		void Asked(const cs::String& key);
		void Failed(const cs::String& key);

		int Count() const { return replies.Count(); }
		int WaitingCount() const { return waiting.Count(); }
		/// <summary>Every recorded reply, in key order (to save, show or hand to a peer).</summary>
		cs::List<std::pair<cs::String, cs::String>> Entries() const;
		void Clear();

	private:
		cs::Dictionary<cs::String, cs::String> replies;
		cs::Dictionary<cs::String, bool> waiting, failed;
	};

	/// <summary>The BYOKey voice (US-T139): what is installed, how requests are built, and the one way a surface asks.</summary>
	class SPACEGODCORE_API StoryVoice final
	{
	public:
		/// <summary>Installs the player's model (and the LLM story teller over it); null puts the pools back alone.</summary>
		static void SetClient(cs::Ref<ILlmClient> client);
		static const cs::Ref<ILlmClient>& Client();
		static void SetPersona(const PersonaProfile& persona);
		static const PersonaProfile& Persona();
		static VoiceLog& Log();

		/// <summary>A model is installed and ready to be asked.</summary>
		static bool Live();

		/// <summary>The recorded reply to <paramref name="request"/>, or empty: asking the model for it when it is live and
		/// the key has never been asked (the reply lands later, and shows from the next look on).</summary>
		static cs::String Voice(const VoiceRequest& request);

		/// <summary>A stranded ship's call (or any opportunity's hook) as the HUD shows it: the voiced reply, else the pools'
		/// call (<see cref="StoryHooks.Tell"/>), else the director's hook.</summary>
		static cs::String Hook(const cs::Ref<Opportunity>& op);

		static cs::String BeatKey(const StoryBeat& beat);
		static cs::String HookKey(const Opportunity& op);
		static VoiceRequest ForBeat(const StoryBeat& beat, const cs::String& plain);
		static VoiceRequest ForHook(const Opportunity& op, const cs::String& plain);

		/// <summary>A reply made fit to show: trimmed, one paragraph, wrapping quotes dropped, capped in length; empty when
		/// nothing usable is left.</summary>
		static cs::String Clean(const cs::String& reply);

		static constexpr size_t MaxReplyChars = 600;

	private:
		static cs::String Rules();
		static cs::String PersonaBlock();
	};

	/// <summary>
	/// The story teller over a BYOKey model (US-T139, set by <see cref="StoryVoice.SetClient"/>): a beat is first told by the
	/// template teller, and that sentence (with the pilot's sheet) is what the model rewords. Until the reply lands, or when
	/// there is none, it answers empty and <see cref="Characters.Tell"/> falls back on the templates.
	/// </summary>
	class SPACEGODCORE_API LlmStoryTeller final : public IStoryTeller
	{
	public:
		cs::String Name() const override { return "llm"; }
		cs::String Tell(const StoryBeat& beat) override;
		cs::String Motive(const cs::Ref<NamedPilot>& pilot, const cs::Ref<LifeEvent>& act) override;

	private:
		TemplateStoryTeller templates;
	};
}
