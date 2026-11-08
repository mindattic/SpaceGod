#pragma once
// US-T239: pilot encounters as Prose conversations (docs/PROSE.md, "Encounters").
//
// When the unit the SpaceGod holds hails someone (or a beaten hull hails it for terms), the game works out what kind of
// encounter it is from the simulation alone (KindOf) and looks for a Prose `conversation` written for it: the hailed pilot's
// own, else their faction's, else the generic one of that kind. With none, the comms panel runs exactly as before.
//
// The text only talks. Every choice that does something carries a tag from Tags(), and a tag is one of the comms verbs the
// game already has (Comms::Verbs): the panel offers a tagged choice only while that verb is on offer, and choosing it says
// the verb through the same path the plain buttons use (Comms::Apply), so the simulation, not the text, decides what happens
// and every peer and replay sees the same outcome. A choice's next may be "$outcome": the game then goes to the node named
// after what the verb came to (OutcomeNode), so the text can react to an outcome it never chose. Numbers the text shows
// (credits, cargo, ranges) come from the game through {placeholders} (Placeholders, Fill).
//
// Nothing here changes the simulation: KindOf and Values only read it. Display only, like every other Lore surface.

#include "Cs/Cs.h"
#include "Comms/Comms.h"

#include <functional>
#include <map>

namespace SpaceGod::Core
{
	class LoreLibrary;
	struct LoreText;

	/// <summary>What an encounter is, from the simulation (KindOf). Its name keys the conversation (Name).</summary>
	enum class EncounterKind : int
	{
		/// <summary>Anyone else: the generic hail, and the last fallback of every kind.</summary>
		Hail,
		/// <summary>A civilian hull: a merchant, courier, miner or independent.</summary>
		Trader,
		/// <summary>A lawful warship (any faction's navy that is not a pirate clan).</summary>
		Patrol,
		/// <summary>A pirate clan's hull.</summary>
		Pirate,
		/// <summary>A hull in trouble: dry tanks, pulled out of a lane and adrift, or fighting a battle the speaker is not in.</summary>
		Distress,
		/// <summary>A hull that has struck its colours to the speaker's side and hails for terms.</summary>
		Surrender,
		/// <summary>A settlement's traffic control.</summary>
		Station,
	};

	/// <summary>One choice of a node, as the game reads it.</summary>
	struct SPACEGODCORE_API EncounterChoice
	{
		cs::String Text;
		/// <summary>A node id, "$outcome" (the game picks the node by what the action came to), or "" / "$end" (the talk ends).</summary>
		cs::String Next;
		cs::List<cs::String> Tags;
		/// <summary>The comms verb its first game tag names (HasAction), else a choice that only moves the talk on.</summary>
		bool HasAction = false;
		CommsVerb Action = CommsVerb::Hail;
	};

	/// <summary>One node of an encounter conversation.</summary>
	struct SPACEGODCORE_API EncounterNode
	{
		cs::String Id, Speaker, Line, Next;
		cs::List<EncounterChoice> Choices;
	};

	/// <summary>The game-filled values a line may show, by placeholder name (without braces).</summary>
	using EncounterValues = std::map<cs::String, cs::String>;

	class SPACEGODCORE_API Encounters final
	{
	public:
		/// <summary>The choice tags the game executes, in Data/Prose/prose.config.json's order: one per comms verb a player can
		/// choose (the hail itself is how an encounter starts, so it has none).</summary>
		static const cs::List<cs::String>& Tags();

		/// <summary>The verb a tag names; false for a tag the game does not know (the choice then only moves the talk on).</summary>
		static bool VerbOfTag(const cs::String& tag, CommsVerb& verb);

		/// <summary>A verb's tag ("" for the hail).</summary>
		static const char* TagOf(CommsVerb v);

		/// <summary>The placeholder names Fill knows, in docs/PROSE.md's order.</summary>
		static const cs::List<cs::String>& Placeholders();

		/// <summary>"hail", "trader", "patrol", "pirate", "distress", "surrender", "station".</summary>
		static const char* Name(EncounterKind k);

		/// <summary>What talking to <paramref name="to"/> from <paramref name="c"/> is, read from the simulation only.</summary>
		static EncounterKind KindOf(const CommsContext& c, const CommsParty& to);

		/// <summary>The text name of an encounter conversation: encounter-&lt;kind&gt;.</summary>
		static cs::String TextName(EncounterKind k);

		/// <summary>
		/// The keys a conversation for this encounter is looked up by, most specific first: &lt;pilot&gt;/encounter-&lt;kind&gt;,
		/// &lt;faction&gt;/encounter-&lt;kind&gt;, spacegod/encounter-&lt;kind&gt;, then spacegod/encounter-hail (a kind other than
		/// the hail). An empty pilot or faction id is skipped (a station has no pilot; a pilot not yet met has no id).
		/// </summary>
		static cs::List<cs::String> Keys(EncounterKind k, const cs::String& pilotId, const cs::String& factionId);

		/// <summary>The first of <paramref name="keys"/> that is a conversation in <paramref name="lib"/>, or null.</summary>
		static const LoreText* Find(const LoreLibrary& lib, const cs::List<cs::String>& keys);

		/// <summary>The conversation's start node id ("" when it has none).</summary>
		static cs::String Start(const LoreText& text);

		/// <summary>Reads node <paramref name="id"/> of a conversation; false when it has no such node.</summary>
		static bool Node(const LoreText& text, const cs::String& id, EncounterNode& node);

		/// <summary>
		/// The indices of the choices the panel offers now, in the text's order: a choice with a game tag only while its verb is
		/// in <paramref name="verbs"/> (Comms::Verbs: what can be said to them now), any other choice always.
		/// </summary>
		static cs::List<int> Offered(const EncounterNode& node, const cs::List<CommsVerb>& verbs);

		/// <summary>The node "$outcome" goes to for an action that came to <paramref name="o"/>: "complied", "ran", "fought",
		/// "refused", "traded", "talked", "surrender-taken", "no-quarter", "plundered", "let-go" ("" for none).</summary>
		static const char* OutcomeNode(CommsOutcome o);

		/// <summary>Whether a speaker of a node is the unit the SpaceGod holds ($player, $me, $hero, or the speaker's own id).</summary>
		static bool IsPlayerSpeaker(const cs::String& speaker, const cs::String& playerId);

		/// <summary>
		/// The values of every placeholder for this encounter, read from the simulation (see Placeholders). Never changes it.
		/// </summary>
		static EncounterValues Values(const CommsContext& c, const CommsParty& to);

		/// <summary>
		/// <paramref name="line"/> with each {name} of <paramref name="values"/> filled in, and each {entity:&lt;id&gt;} named
		/// by <paramref name="entityName"/> (null, or an empty answer: the id's own words, "planet-sol-earth" -> "Earth"...
		/// see EntityWords). An unknown {name} is left as written, so a typo shows on screen rather than vanishing.
		/// </summary>
		static cs::String Fill(const cs::String& line, const EncounterValues& values,
			const std::function<cs::String(const cs::String&)>& entityName = nullptr);

		/// <summary>An entity id's last words, capitalised: "pilot-tomas-brennan" -> "Tomas Brennan", "planet-sol-earth" -> "Sol Earth"
		/// (the type prefix dropped). The fallback name of an {entity:&lt;id&gt;} the game cannot resolve.</summary>
		static cs::String EntityWords(const cs::String& id);
	};
}
