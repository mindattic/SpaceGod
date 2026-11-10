#pragma once
// US-T274/US-T275: words for the Civilization layer's events (docs/PROSE.md, "Social narration").
//
// Prose owns every word a player reads as story. Each SocialKind has a bark pool keyed spacegod/social-<slug> (the slug is
// the kind in kebab case: FeudDeclared -> spacegod/social-feud-declared); each organisation kind a name pool
// spacegod/names-<kind>. The game hands the Narrator its Prose library (SetSource, GameLore's Lines); a pool with no
// approved text falls back to the templates here. A line is picked by the event's seed, so every peer and every replay
// reads the same words, and newly approved text rewords old events too (the events are saved, the words never are).
//
// Placeholders a line may use: {who} {whom} {house} {otherhouse} {business} {org} {otherorg} {world} {system} {faction}
// {rival} {money} {amount} {pct} {outlet}. One a given event lacks reads as a plain noun ("someone", "a world", ...).
//
// Display only, with one exception: a notable event is logged on its system (so the relays carry it) in the template's
// words, never Prose's, so the simulation's state never depends on what Prose has written.

#include "Cs/Cs.h"

#include <cstdint>
#include <functional>

namespace SpaceGod::Core
{
	class Org;
	class SocialEvent;
	enum class SocialKind : int;
	enum class OrgKind : int;
	enum class FeedKind : int;

	class SPACEGODCORE_API Narrator final
	{
	public:
		/// <summary>The lines of a Prose text by key (a bark's lines), empty when none is approved.</summary>
		using LineSource = std::function<cs::List<cs::String>(const cs::String& key)>;

		/// <summary>The game's Prose library (GameLore); unset (a Core-only run, a test) reads the templates only.</summary>
		static void SetSource(LineSource source);
		static bool HasSource();

		/// <summary>The kind's slug (feud-declared) and its Prose key (spacegod/social-feud-declared).</summary>
		static cs::String Slug(SocialKind k);
		static cs::String Key(SocialKind k);
		/// <summary>The name pool key of an organisation kind (spacegod/names-faith).</summary>
		static cs::String NamesKey(OrgKind k);

		/// <summary>Where the kind shows in the feed (news, an ad, spam, a message).</summary>
		static FeedKind FeedOf(SocialKind k);
		/// <summary>Whether the kind is news beyond its own system (logged as WorldEventKind::Society, relayed).</summary>
		static bool Notable(SocialKind k);
		/// <summary>The built-in lines of a kind (what Prose replaces).</summary>
		static cs::List<cs::String> Templates(SocialKind k);

		/// <summary>The event in words: Prose's pool when <paramref name="prose"/> and approved, else the template.</summary>
		static cs::String Render(const SocialEvent& e, bool prose = true);
		/// <summary>An organisation's name: from Prose's name pool by its seed when approved, else the one it was founded with.</summary>
		static cs::String OrgName(const Org& o);
		/// <summary>A founding name from the built-in pools (what the simulation keeps), for <paramref name="place"/>.</summary>
		static cs::String FallbackName(OrgKind k, std::uint32_t seed, const cs::String& place, const cs::String& surname);

		/// <summary>Fills the placeholders of <paramref name="line"/> from the event.</summary>
		static cs::String Fill(const cs::String& line, const SocialEvent& e, bool prose);

		static cs::String Money(double credits);
	};
}
