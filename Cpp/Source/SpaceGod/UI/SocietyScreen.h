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
	/// the latest events about it, each one a link to the event.
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

	private:
		static constexpr float Margin = 40, Gutter = 24, HeadingH = 24, TabRowH = 30, RowH = 46, LineH = 20, SectionGap = 12;
		static constexpr const char* SearchControl = "SocietySearch";
		/// The most events the detail lists about the thing picked (US-T299), and the most picks Back remembers (US-T298).
		static constexpr int RecentMax = 12, HistoryMax = 64;

		/// The pages down the left: the five lists, then the module views (CivHands::Views()).
		enum Page : int { Feed, Houses, People, Orgs, Worlds, FirstView };

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
		Vector2 listScroll, detailScroll, pageScroll;

		int page = Feed;
		/// The feed's filters: a module group (-1 all), a kind (0 all), the world picked only, the SpaceGod's doings only.
		int group = -1, kind = 0;
		bool worldOnly = false, steeredOnly = false;
		/// The organisations' kind (-1 all).
		int orgKind = -1;

		CivPick selected, marked;
		int selectedEvent = 0;
		cs::String result;
		bool resultBad = false;

		std::vector<Row> rows;
		cs::String rowsKey;
		double rowsAt = -1;

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

		/// US-T299: whether <paramref name="e"/> is about <paramref name="p"/>, and the latest such events.
		static bool About(const Core::SocialEvent& e, const CivPick& p);
		void BuildRecent(const CivPick& p, Core::Civilization& civ);

		/// The module group of a kind (an index into the group names), and the names.
		static int GroupOf(int kind);
		static int GroupCount();
		static const char* GroupName(int g);
	};
}
