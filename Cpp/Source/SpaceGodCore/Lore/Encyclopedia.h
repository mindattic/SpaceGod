#pragma once
// US-T271: the Encyclopedia, everything the game can explain written down in one place: the controls (from the live key
// bindings, the gamepad map and the Help card), the manual's guides, the Prose lore, and an article for each thing the
// player has learned of (bodies and stars, goods, industries and markets, factions, hulls, stations, settlements, systems
// and pilots), with an open-text search over the titles, bodies and keywords.
//
// Model. An Encyclopedia is a list of articles, each with a stable id "<category>/<slug>" (the slug is the thing's Prose
// key, Core::LoreKeys, where it has one: astronomy/planet-sol-earth, pilots/pilot-tomas-brennan), a title, a line under it,
// its sections, and keywords. It is built from Core's own tables and the simulation as it stands (the Runtime adds the
// control articles from its key bindings, which Core cannot see) and rebuilt whenever what it is built from moves on.
//
// Knowledge. The guides and controls are always known. An article about something in the game is shown only once the
// player has come to know it: the discovery log's finds (Systems/DiscoveryLog) are their own articles, and everything else
// (a faction, a good, an industry, a hull, a Prose text) is unlocked into EncyclopediaKnowledge, saved with the Galaxy
// (optional: absent from older saves) by EncyclopediaKnowledge::Sweep from what the player has found. Locked articles stay
// hidden: they never list, never match a search and are never linked to.
//
// Links and search. Articles link by keyword: an article's "see also" are the known articles whose title its own text
// names. The keyword index (ArticlesFor) answers which articles a word leads to, for the scan card's links (US-T272).
// Search (Search) is case-insensitive over title, body and keywords, every word of the query having to match, title
// matches ranked first, then keyword matches, then the body.
//
// Display only: nothing here changes the simulation, and no simulation code reads the Encyclopedia or the knowledge, so
// the Bench digests never see them. Every co-op peer sweeps the same shared state (the log, the systems visited) and so
// keeps the same knowledge.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

#include <unordered_map>
#include <unordered_set>

namespace SpaceGod::Core
{
	class DiscoveryEntry;
	class DiscoveryLog;
	class Galaxy;
	class LoreLibrary;
	enum class Good : int;
	enum class IndustryKind : int;
	enum class ShipClass : int;

	/// <summary>The Encyclopedia's categories (US-T271), in the screen's order.</summary>
	enum class EncyclopediaCategory : int
	{
		Controls,
		Guides,
		Lore,
		Astronomy,
		Commerce,
		Factions,
		Ships,
		Stations,
		Settlements,
		Systems,
		Pilots,
	};
	SPACEGODCORE_API const char* ToString(EncyclopediaCategory v);

	/// <summary>One section of an article: a heading ("" for none) and its text (lines split by '\n').</summary>
	struct EncyclopediaSection
	{
		cs::String Heading;
		cs::String Text;
	};

	/// <summary>One article of the Encyclopedia (US-T271).</summary>
	class SPACEGODCORE_API EncyclopediaArticle final
	{
	public:
		/// <summary>The stable id: "&lt;category&gt;/&lt;slug&gt;" (<see cref="Encyclopedia::MakeId"/>).</summary>
		cs::String Id;
		EncyclopediaCategory Category = EncyclopediaCategory::Guides;
		cs::String Title;
		/// <summary>The line under the title (what it is, where); "" for none.</summary>
		cs::String Subtitle;
		cs::List<EncyclopediaSection> Sections;
		/// <summary>Words it is also found by (searched and indexed, never shown as text of its own).</summary>
		cs::List<cs::String> Keywords;
		/// <summary>Known from the start (the guides and controls; a find of the discovery log, which exists only once found);
		/// otherwise only once <see cref="EncyclopediaKnowledge"/> holds its id.</summary>
		bool AlwaysKnown = false;
		/// <summary>US-T272's hook for live stats: the discovery log's id of the thing it is about ("" for none). A view asks
		/// the game for that thing's readings as the simulation stands now and shows them in the article.</summary>
		cs::String EntityId;
		/// <summary>The Prose key of the thing it is about (Core::LoreKeys), "" for none.</summary>
		cs::String LoreKey;

		/// <summary>The heading of the section holding an entity's readings as they stood when the article was built: a view
		/// with live stats for the article (<see cref="EntityId"/>) shows those in its place.</summary>
		static constexpr const char* LiveSection = "Readings";

		/// <summary>Every section's heading and text, as one block (what search reads as the body).</summary>
		cs::String Body() const;

		void AddSection(const cs::String& heading, const cs::String& text);
		/// <summary>Adds each word of <paramref name="words"/> (split on spaces) not already a keyword.</summary>
		void AddKeywords(const cs::String& words);
	};

	/// <summary>One search result: the article, how it matched (0 title, 1 keyword, 2 body) and a line of its body around the
	/// first match ("" when the body does not hold one).</summary>
	struct EncyclopediaHit
	{
		cs::Ref<EncyclopediaArticle> Article;
		int Rank = 2;
		cs::String Snippet;
	};

	/// <summary>
	/// What the player has come to know (US-T271): the ids of the articles unlocked beyond the always-known ones, in the
	/// order unlocked. Saved with the Galaxy (Galaxy::Knowledge, optional). See the file's head.
	/// </summary>
	class SPACEGODCORE_API EncyclopediaKnowledge final
	{
	public:
		/// <summary>The ids unlocked, in the order they were.</summary>
		cs::List<cs::String> Known;

		/// <summary>Counts every change, so a view knows to rebuild (not saved).</summary>
		int Version = 0;

		bool Has(const cs::String& id) const;
		/// <summary>Unlocks <paramref name="id"/>; true when it was new.</summary>
		bool Unlock(const cs::String& id);

		/// <summary>
		/// Unlocks what the player has come to know from shared state: in every system the log says was visited, the
		/// factions holding its found settlements, the goods in their markets, their industries and the hulls in its space;
		/// the factions of the pilots met; the Prose books recovered (<paramref name="recoveredBooks"/>); and the Prose texts
		/// about the universe a find of the log is keyed to. Reads only; returns how many it unlocked.
		/// </summary>
		int Sweep(const Galaxy& galaxy, const DiscoveryLog* log, const cs::List<cs::String>& recoveredBooks);

	private:
		mutable std::unordered_set<std::string> ids;
		mutable int indexed = -1;
		void Index() const;

		SG_SERIALIZABLE(EncyclopediaKnowledge);
	};

	/// <summary>The Encyclopedia (US-T271): every article, the keyword index and the search. See the file's head.</summary>
	class SPACEGODCORE_API Encyclopedia final
	{
	public:
		static constexpr int CategoryCount = static_cast<int>(EncyclopediaCategory::Pilots) + 1;

		/// <summary>The category's id prefix ("astronomy").</summary>
		static const char* Slug(EncyclopediaCategory c);
		/// <summary>The category's heading on the screen ("Astronomy").</summary>
		static const char* Heading(EncyclopediaCategory c);
		/// <summary>What unlocks articles of the category, for an empty list.</summary>
		static const char* HowLearned(EncyclopediaCategory c);

		/// <summary>An article id: "&lt;category&gt;/&lt;slug of name&gt;" (a name already a Prose key keeps its form).</summary>
		static cs::String MakeId(EncyclopediaCategory c, const cs::String& name);

		/// <summary>The ids of the articles about a faction, a good, an industry, a hull class and a Prose text.</summary>
		static cs::String FactionId(const cs::String& factionName);
		static cs::String GoodId(Good g);
		static cs::String IndustryId(IndustryKind k);
		static cs::String HullId(ShipClass c);
		static cs::String LoreId(const cs::String& loreKey);
		/// <summary>The category of the article a find of the discovery log makes.</summary>
		static EncyclopediaCategory CategoryOf(const DiscoveryEntry& e);
		/// <summary>The id of the article a find of the discovery log makes (before any clash is numbered).</summary>
		static cs::String DiscoveryId(const DiscoveryEntry& e);

		// ---------------------------------------------------------------- the articles

		/// <summary>Adds <paramref name="a"/> (one already there with its id is replaced) and returns it.</summary>
		cs::Ref<EncyclopediaArticle> Add(const cs::Ref<EncyclopediaArticle>& a);
		const cs::List<cs::Ref<EncyclopediaArticle>>& Articles() const { return articles; }
		int Count() const { return articles.Count(); }
		/// <summary>The article with this id, or null.</summary>
		cs::Ref<EncyclopediaArticle> Find(const cs::String& id) const;
		void Clear();

		/// <summary>Whether the player knows <paramref name="a"/> (always known, or unlocked in <paramref name="k"/>).</summary>
		static bool IsKnown(const EncyclopediaArticle& a, const EncyclopediaKnowledge* k);
		/// <summary>The known articles of a category, by title.</summary>
		cs::List<cs::Ref<EncyclopediaArticle>> Known(EncyclopediaCategory c, const EncyclopediaKnowledge* k) const;
		int CountKnown(EncyclopediaCategory c, const EncyclopediaKnowledge* k) const;

		// ---------------------------------------------------------------- search, keywords and links

		/// <summary>
		/// Every known article holding each word of <paramref name="query"/> (case-insensitive) in its title, body or
		/// keywords: title matches first, then keyword matches, then body matches, each by title. Empty for an empty query.
		/// </summary>
		cs::List<EncyclopediaHit> Search(const cs::String& query, const EncyclopediaKnowledge* k) const;

		/// <summary>The ids of the articles a word leads to (a title or a keyword equal to it, case-insensitive), by title;
		/// known ones only when <paramref name="k"/> is given.</summary>
		cs::List<cs::String> ArticlesFor(const cs::String& word, const EncyclopediaKnowledge* k = nullptr) const;

		/// <summary>The known articles <paramref name="a"/>'s text names by title (whole words, case-insensitive), at most
		/// <paramref name="max"/>, in the order they are first named.</summary>
		cs::List<cs::Ref<EncyclopediaArticle>> LinksOf(const EncyclopediaArticle& a, const EncyclopediaKnowledge* k, int max = 16) const;

		/// <summary>Where <paramref name="needle"/> (already lowercase) first occurs in <paramref name="text"/>, ignoring ASCII
		/// case; npos when it does not.</summary>
		static size_t FindNoCase(const cs::String& text, const cs::String& needle, size_t from = 0);
		/// <summary>The query's words, lowercase, split on spaces.</summary>
		static cs::List<cs::String> Words(const cs::String& query);
		/// <summary>About <paramref name="width"/> bytes of <paramref name="text"/> around its first match of <paramref name="word"/>,
		/// cut at spaces, on one line, with "..." where it was cut; "" when it does not match.</summary>
		static cs::String Snippet(const cs::String& text, const cs::String& word, int width = 110);

		// ---------------------------------------------------------------- builders (no UI dependency)

		/// <summary>A controls article of <paramref name="title"/> (always known): one section per line, keys as the heading.</summary>
		cs::Ref<EncyclopediaArticle> AddControls(const cs::String& slug, const cs::String& title, const cs::String& subtitle,
			const cs::List<EncyclopediaSection>& lines, const cs::String& keywords);
		/// <summary>The Help card's groups (ControlsHelp), one controls article each.</summary>
		void AddControlCard();
		/// <summary>The manual's chapters (GameManual), one guide each, with the keys each prints.</summary>
		void AddGuides();
		/// <summary>The Prose books recovered and the texts about the universe as a whole.</summary>
		void AddLore(const LoreLibrary& lore, const cs::List<cs::String>& recoveredBooks);
		/// <summary>Every good and industry, and the markets of the settlements found.</summary>
		void AddCommerce(const Galaxy& galaxy, const DiscoveryLog* log);
		/// <summary>Every hull class of the ship catalog.</summary>
		void AddShips();
		/// <summary>Every faction.</summary>
		void AddFactions(const Galaxy& galaxy, const LoreLibrary& lore);
		/// <summary>An article for every find of the log (bodies and stars, settlements and stations, systems, pilots,
		/// sightings), with its readings as they stand at <paramref name="jd"/> and its Prose description.</summary>
		void AddDiscoveries(const Galaxy& galaxy, const DiscoveryLog& log, const LoreLibrary& lore, double jd);

		/// <summary>Builds the keyword index; call once every article is in.</summary>
		void Finish();

	private:
		cs::List<cs::Ref<EncyclopediaArticle>> articles;
		std::unordered_map<std::string, int> byId;
		/// Lowercase keyword or title -> article indices.
		std::unordered_map<std::string, std::vector<int>> keywordIndex;
		/// Per article (same index): lowercase title, keywords joined, body.
		std::vector<std::string> lowTitle, lowKeywords, lowBody;
		bool finished = false;
	};
}
