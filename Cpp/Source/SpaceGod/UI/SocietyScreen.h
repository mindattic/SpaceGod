#pragma once

// SocietyScreen (US-T293, the Civilization epic's P10): the galaxy's people, and the SpaceGod's hand in their affairs.

#include "Cs/Cs.h"
#include "CivHand.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

#include <unordered_map>
#include <vector>

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class Civilization;
	class Galaxy;
	class Society;
	class SocialEvent;
}

namespace SpaceGod::UI
{
	/// <summary>
	/// The Society screen (Shift+G; also the pause menu and the bottom drawer), in the Help screen's look (UiStyle), reading the
	/// Civilization layer (Core's Systems/Civilization) as it runs. Down the left: the Feed (every social event, newest first,
	/// in the Narrator's words: Prose's when approved, else the built-in lines), Houses, People (the living by renown),
	/// Organisations (by kind), Worlds (each one's Civics: approval, politics, crime, sickness, weariness, faith, fashion,
	/// quarantine) and the module views sg.Civ prints (feuds, regimes, trials, admirals and strikes, crime, money, the
	/// chronicle). The feed filters by module, by kind (every SocialKind, so new kinds show without a change here), by the
	/// world picked and to the SpaceGod's own doings (Steered). On the right: the thing picked (a house, a person, an
	/// organisation, a world, a trial, a business, a state), with links to the people, houses and worlds about it, and the
	/// SpaceGod's hand verbs that apply to it, each the same sg.Civ line the console would run (CivHand.h), so in co-op it
	/// goes to every peer. A verb that takes two (a feud, an affair, a summit) takes the second from the one marked.
	/// US-T298: a search over whatever list is shown (Ctrl+F; the feed's events by their words too), and Back / Forward
	/// through what was picked (Alt+Left / Alt+Right, or the arrows by the detail's title). US-T299: the thing picked lists
	/// the latest events about it, each one a link to the event. US-T304: anything with events can be watched; the Watched page
	/// lists what is, the feed narrows to it (Watched only), and while playing a toast tells the newest event about it.
	/// US-T344: the mark (C, or the Mark button) also compares: with a person, house, organisation or business marked, another of
	/// the same kind picked shows beside it row by row, the better value of each highlighted.
	/// </summary>
	class SocietyScreen final
	{
	public:
		explicit SocietyScreen(UnitySpaceApp* app);
		~SocietyScreen();

		/// Draws the screen; true once it should close (Close; Esc and the key that opens it are the owner's to handle).
		bool Draw(const cs::String& closeHint);

		/// The screen's own textures go with whatever owns it (U.SPC-T366).
		void DestroyOwnedTextures();

		/// Whether the search has the keys (the owner hands the game's input over meanwhile; US-T298).
		bool Typing() const { return typing; }

		/// US-T313: picks <paramref name="p"/> (remembered for Back), on the page that lists it.
		void Show(const CivPick& p);

		/// US-T299: whether <paramref name="e"/> is about <paramref name="p"/> (a house's people's doings are the house's).
		/// Public for the watch list's news (US-T304, UnitySpaceApp::UpdateSocietyWatch).
		static bool About(const Core::SocialEvent& e, const CivPick& p);

	private:
		static constexpr float Margin = 40, Gutter = 24, HeadingH = 24, TabRowH = 30, RowH = 46, LineH = 20, SectionGap = 12;
		static constexpr const char* SearchControl = "SocietySearch";
		/// The most events the detail lists about the thing picked (US-T299), and the most picks Back remembers (US-T298).
		static constexpr int RecentMax = 12, HistoryMax = 64;

		/// The pages down the left: the lists (Watched: what the player watches, US-T304; YourHand: what the SpaceGod's hand
		/// brought about, US-T325), then the module views (CivHands::Views()).
		/// US-T337: Month, the last 30 days' events by kind.
		enum Page : int { Feed, Houses, People, Orgs, Worlds, Watched, YourHand, Month, FirstView };
		/// US-T325: how long after a steered event its aftermath is looked for, days, and the most it lists.
		static constexpr double AftermathDays = 365;
		/// US-T337: how far back the This month page looks, days.
		static constexpr double MonthDays = 30;
		static constexpr int AftermathMax = 12;
		/// US-T327: the most lines a person's LIFE lists (born, the events about them oldest first, died).
		static constexpr int LifeMax = 30;
		/// US-T348: the most years the TIMELINE strip shows (the latest), and the most events a year opened under it lists.
		static constexpr int TimelineYearsMax = 40;
		static constexpr int TimelineListMax = 20;
		/// US-T375: the most events a house's CHRONICLE tells (the earliest; the rest are counted), and the most names a decade links.
		static constexpr int ChronicleMax = 60;
		static constexpr int ChronicleNamesMax = 12;
		/// US-T368: the most RIPE rows Your hand opens with (the most urgent first).
		static constexpr int RipeMax = 8;
		/// US-T378: the most houses, firms and organisations each the world's POWER section ranks.
		static constexpr int PowerMax = 8;

		/// The feed's doings filter (US-T304; was the Steered toggle): every event, the SpaceGod's doings, or those about what is watched.
		enum Doings : int { AllDoings, SteeredDoings, WatchedDoings, DoingsCount };

		struct Row
		{
			cs::String Title, Line;
			CivPick Pick;
			/// The feed's event (its words are rendered only while in view).
			cs::Ref<Core::SocialEvent> Event;
		};

		/// A button the detail panel offers: a link to another thing, or a hand (an sg.Civ line), or a hint (no line, disabled).
		struct Hand
		{
			cs::String Label, Line;
			/// US-T371: what it will do if used now (sure, or refused and why), shown under its button.
			cs::String Outlook;
		};

		/// One place Back and Forward return to: a thing, or an event (US-T298).
		struct Visit
		{
			CivPick Pick;
			int Event = 0;
		};

		/// The latest events about the thing picked, worded (US-T299): rebuilt when the pick or the feed changes.
		struct Recent
		{
			int Event = 0;
			cs::String Line;
		};

		UnitySpaceApp* app;
		bool stylesReady = false;
		cs::Ref<GUIStyle> panel, title, entryTitle, heading, text, small, button, entry, entrySelected, rowTitle, rowLine, link, good, bad, field;
		cs::Ref<Texture2D> panelTexture, buttonTexture, buttonActiveTexture, fieldTexture;

		/// The search (US-T298): its words, lower-cased, and whether it has the keys.
		cs::String search;
		bool typing = false, focusSearch = false, closeAsked = false;
		/// Every event's words, by id, as the search reads them (worded once: an event never changes after it is published).
		std::unordered_map<int, cs::String> eventWords;

		std::vector<Visit> back, forward;

		std::vector<Recent> recent;
		int recentTotal = 0;
		cs::String recentKey;
		/// US-T325: what followed the steered event picked (oldest first), how many in all, and what it was built for.
		std::vector<Recent> aftermath;
		int aftermathTotal = 0;
		cs::String aftermathKey;
		/// US-T327: the person picked's life in order (a birth or death line has no event), the events left out, and its key.
		std::vector<Recent> life;
		int lifeMore = 0;
		cs::String lifeKey;
		/// US-T358: whether a person's FAMILY TREE section is drawn open (UI-side, kept from person to person).
		bool familyTreeOpen = false;
		/// US-T348: one year of the TIMELINE strip: how many events about the thing picked, and how many the hand steered.
		struct TimelineYear
		{
			int Year = 0;
			int Count = 0;
			int Steered = 0;
		};
		/// US-T348: the strip's years (oldest first, every year from the first kept to now), the events before its first year,
		/// all its events, the year opened under it (0 none) with that year's events (oldest first), and what each was built for.
		std::vector<TimelineYear> timeline;
		int timelineEarlier = 0;
		int timelineTotal = 0;
		int timelineOpen = 0;
		std::vector<Recent> timelineRows;
		cs::String timelineKey, timelinePick;
		/// US-T375: one paragraph of a house's CHRONICLE: its decade (0: how the house stands now), its prose, and the people,
		/// houses, organisations and businesses it names (to follow).
		struct ChronicleDecade
		{
			int Decade = 0;
			cs::String Prose;
			std::vector<CivPick> Names;
		};
		/// US-T375: whether a house's CHRONICLE is drawn open (UI-side, kept from house to house), its paragraphs, every event
		/// about the house, those past ChronicleMax left untold, and what it was built for.
		bool chronicleOpen = false;
		std::vector<ChronicleDecade> chronicle;
		int chronicleTotal = 0;
		int chronicleMore = 0;
		cs::String chronicleKey;
		Vector2 listScroll, detailScroll, pageScroll;

		int page = Feed;
		/// The feed's filters: a module group (-1 all), a kind (0 all), the world picked only, whose doings (Doings).
		int group = -1, kind = 0, doings = AllDoings;
		bool worldOnly = false;
		/// US-T383: the world the feed is held to while worldOnly, kept apart from the pick so picking an event keeps it.
		cs::String worldFilter;
		/// The organisations' kind (-1 all).
		int orgKind = -1;
		/// US-T354: each list page's sort (0 its own order: founding, renown...; SortName names each), kept per page.
		int sortBy[FirstView] = {};

		CivPick selected, marked;
		int selectedEvent = 0;
		cs::String result;
		bool resultBad = false;

		std::vector<Row> rows;
		cs::String rowsKey;
		double rowsAt = -1;
		/// US-T351: how many events about what is watched came since each was last looked at (the Watched page's button), and when counted.
		int watchedNew = 0;
		double watchedNewAt = -1e9;

		void Rebuild(Core::Galaxy& galaxy, Core::Civilization* civ, double jd);
		cs::String RowsKey() const;
		void HandleKeys();
		/// Picks a row's thing; <paramref name="remember"/> keeps where it was for Back (a click does, Up / Down don't).
		void Select(const Row& r, bool remember = true);
		void DrawPages(Rect area);
		void DrawFilters(Rect r, Core::Civilization& civ);
		void DrawList(Rect area, Core::Civilization* civ);
		void DrawDetail(Rect area, Core::Galaxy& galaxy, Core::Civilization& civ, double jd);
		/// The lines (heading-free text) and links about <paramref name="p"/>, and the hands that apply to it.
		void Describe(const CivPick& p, Core::Galaxy& galaxy, Core::Civilization& civ, double jd, cs::String& name, cs::List<cs::String>& lines,
			std::vector<std::pair<cs::String, CivPick>>& links, std::vector<Hand>& hands);
		void DescribeEvent(const Core::SocialEvent& e, cs::String& name, cs::List<cs::String>& lines, std::vector<std::pair<cs::String, CivPick>>& links);
		/// The name of <paramref name="p"/> as the screen shows it.
		cs::String NameOf(const CivPick& p, Core::Galaxy& galaxy, Core::Civilization& civ) const;
		void Ask(const cs::String& line);
		void EnsureStyles();

		/// US-T298: goes to <paramref name="to"/>, remembering where it was for Back (and forgetting Forward).
		void Go(const Visit& to);
		void StepBack();
		void StepForward();
		Visit Here() const { return Visit{ selected, selectedEvent }; }
		void DrawSearch(Rect r);
		/// Whether a row's words (and its event's, worded) hold the search.
		bool Matches(const cs::String& a, const cs::String& b = cs::String()) const;
		const cs::String& WordsOf(const Core::SocialEvent& e);

		/// US-T299: the latest events about <paramref name="p"/>.
		void BuildRecent(const CivPick& p, Core::Civilization& civ);
		/// US-T325: the people, houses, organisations, businesses and (failing those) world an event is about.
		static std::vector<CivPick> SubjectsOf(const Core::SocialEvent& e);
		/// US-T325: whether <paramref name="later"/> followed <paramref name="steered"/> (after it, within AftermathDays, about one of its subjects).
		static bool Follows(const Core::SocialEvent& later, const Core::SocialEvent& steered, const std::vector<CivPick>& subjects);
		/// US-T325: what came of the steered event <paramref name="e"/>.
		void BuildAftermath(const Core::SocialEvent& e, Core::Civilization& civ);
		/// US-T327: the life of the person <paramref name="personId"/>, in order.
		void BuildLife(int personId, Core::Galaxy& galaxy, Core::Civilization& civ);
		/// US-T348: the TIMELINE of <paramref name="p"/> (its events by year to the year of <paramref name="jd"/>), and the year opened.
		void BuildTimeline(const CivPick& p, Core::Civilization& civ, double jd);
		/// <summary>US-T348: the TIMELINE section at <paramref name="y"/> (a bar a year, the steered share bright; a click opens the
		/// year's events below it); returns its height, draws only when <paramref name="draw"/>, and sets
		/// <paramref name="followEvent"/> to the event a row was clicked for.</summary>
		float DrawTimeline(float y, float w, bool draw, int& followEvent);
		/// <summary>US-T308: the TRENDS section of the world called <paramref name="world"/> at <paramref name="y"/> (its mood over the
		/// samples the game took, a bar chart a row); returns its height, and draws only when <paramref name="draw"/>.</summary>
		float DrawTrends(float y, float w, const cs::String& world, Core::Civilization& civ, double jd, bool draw);
		/// <summary>US-T378: the POWER section of the world called <paramref name="world"/> at <paramref name="y"/>: the houses, firms
		/// and organisations based there, each kind ranked by its wealth, capital or strength with its share of the kind's total as a
		/// bar (at most PowerMax each); returns its height, draws only when <paramref name="draw"/>, and sets <paramref name="follow"/>
		/// to the thing a row was clicked for.</summary>
		float DrawPower(float y, float w, const cs::String& world, Core::Galaxy& galaxy, Core::Civilization& civ, bool draw, CivPick& follow);
		/// <summary>US-T315: the FAMILY section of the house <paramref name="houseId"/> at <paramref name="y"/> (its members by
		/// generation, who each married, the dead greyed); returns its height, draws only when <paramref name="draw"/>, and sets
		/// <paramref name="follow"/> to the person a row was clicked for.</summary>
		float DrawFamily(float y, float w, int houseId, Core::Galaxy& galaxy, double jd, bool draw, CivPick& follow);
		/// <summary>US-T358: the FAMILY TREE section of the person <paramref name="personId"/> at <paramref name="y"/>: a Tree button
		/// that opens a drawn tree of their house's people two generations up and down (citizens keep no parents, so a generation
		/// is a 25-year band of births, as in FAMILY), the spouse beside each and the dead greyed; returns its height, draws only
		/// when <paramref name="draw"/>, and sets <paramref name="follow"/> to the person a box was clicked for.</summary>
		float DrawFamilyTree(float y, float w, int personId, Core::Galaxy& galaxy, double jd, bool draw, CivPick& follow);
		/// US-T375: the CHRONICLE of the house <paramref name="houseId"/>: its founding, then its events in order (each in the
		/// narrator's words), a paragraph a decade, and how it stands now.
		void BuildChronicle(int houseId, Core::Galaxy& galaxy, Core::Civilization& civ);
		/// <summary>US-T375: the CHRONICLE section at <paramref name="y"/>: a Chronicle button that opens the house's history as
		/// prose, the names each paragraph mentions as links under it; returns its height, draws only when <paramref name="draw"/>,
		/// and sets <paramref name="follow"/> to the thing a name was clicked for.</summary>
		float DrawChronicle(float y, float w, Core::Galaxy& galaxy, Core::Civilization& civ, bool draw, CivPick& follow);

		/// US-T344: one row of the side-by-side compare: what it is, the picked one's value and the marked one's, and which is
		/// better (0 neither or no better way, 1 the picked, 2 the marked).
		struct CompareRow
		{
			cs::String Label, A, B;
			int Better = 0;
		};
		/// US-T344: whether the thing picked and the one marked compare (two different people, houses, organisations or businesses).
		bool Comparable() const;
		/// US-T344: the rows comparing the thing picked with the one marked (empty when they don't compare or one is gone).
		void BuildCompare(Core::Galaxy& galaxy, Core::Civilization& civ, double jd, std::vector<CompareRow>& out) const;
		/// <summary>US-T344: the COMPARE section at <paramref name="y"/> (the picked and the marked side by side, the better value of
		/// each row highlighted); returns its height, draws only when <paramref name="draw"/>, and sets <paramref name="follow"/> to
		/// the marked one when its name is clicked.</summary>
		float DrawCompare(float y, float w, Core::Galaxy& galaxy, Core::Civilization& civ, double jd, bool draw, CivPick& follow);
		/// US-T344: marks the thing picked (C or the Mark button), or clears the mark when it is the one marked.
		void ToggleMark();

		/// US-T354: how many sorts the page <paramref name="p"/> offers (1: none to choose), and the name of sort <paramref name="s"/> there.
		static int SortCount(int p);
		static const char* SortName(int p, int s);
		/// US-T354: the next sort of the page shown (S, or the Sort button by the list's heading).
		void StepSort();

		/// The module group of a kind (an index into the group names), and the names.
		static int GroupOf(int kind);
		static int GroupCount();
		static const char* GroupName(int g);
	};
}
