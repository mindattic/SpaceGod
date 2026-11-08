#pragma once

// EncyclopediaScreen (US-T271): every article in one place, to browse by category or search with an open-text filter.

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

#include <vector>

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class Encyclopedia;
	class EncyclopediaArticle;
	class EncyclopediaKnowledge;
}

namespace SpaceGod::UI
{
	/// The Encyclopedia (Shift+I), in the Help screen's look (UiStyle): the categories down the left (Controls, Guides, Lore,
	/// Astronomy, Commerce, Factions, Ships, Stations, Settlements, Systems, Pilots) with how many articles of each are known,
	/// the articles of the one picked in the middle, and the article picked on the right: its sections, the live readings of
	/// the thing it is about (UnitySpaceApp::ArticleLiveStats), and a "see also" of the known articles its text names, each a
	/// link. The filter at the top searches every known article's title, body and keywords as it is typed (title matches
	/// first, the match marked); the categories then count the matches. Back and Forward (or Alt+Left / Alt+Right) step
	/// through the articles read. While the filter is being typed in it has the keys; Esc closes the screen, Enter or a click
	/// elsewhere leaves the filter, Up / Down pick an article, Left / Right a category.
	class EncyclopediaScreen final
	{
	public:
		explicit EncyclopediaScreen(UnitySpaceApp* app);
		~EncyclopediaScreen();

		/// Draws the screen; true once it should close (Back, or Esc while typing in the filter: the game's own Esc closes it
		/// otherwise). The key that opens it is the owner's to handle.
		bool Draw(const cs::String& closeHint);

		/// The screen has just been opened: the filter takes the keys.
		void Opened();
		/// The screen has been closed: the filter lets the keys go.
		void Closed();
		/// Shows the article <paramref name="id"/> (the filter cleared, its category picked), as a step of the history.
		void Show(const cs::String& id);
		/// Puts <paramref name="query"/> in the filter.
		void Search(const cs::String& query);
		/// Whether the filter has the keys (the owner hands the game's input over meanwhile).
		bool Typing() const { return typing; }

		/// The screen's own textures go with whatever owns it (U.SPC-T366).
		void DestroyOwnedTextures();

	private:
		static constexpr float Margin = 40, Gutter = 24, HeadingH = 24, CategoryRowH = 32, RowH = 46, KeyW = 170, LineGap = 3, SectionGap = 12;
		static constexpr const char* FilterControl = "EncyclopediaFilter";

		struct Row
		{
			cs::Ref<Core::EncyclopediaArticle> Article;
			cs::String Line;
		};

		UnitySpaceApp* app;
		bool stylesReady = false;
		cs::Ref<GUIStyle> panel, title, entryTitle, heading, keys, text, small, button, entry, entrySelected, field, rowTitle, rowLine, link;
		cs::Ref<Texture2D> panelTexture, buttonTexture, buttonActiveTexture, fieldTexture;
		Vector2 scroll, listScroll;

		cs::String filter;
		bool focusFilter = true, typing = false, closeAsked = false;
		int category = 1; // Guides: the first page a newcomer reads
		/// While filtering, the category the matches are narrowed to (-1: all of them).
		int hitCategory = -1;
		cs::String current;
		cs::List<cs::String> back, forward;
		cs::String pending; // a link clicked this event, followed once the drawing is done
		bool pendingStep = true;

		/// The rows shown, rebuilt when the Encyclopedia, the filter or the category changes.
		std::vector<Row> rows;
		std::vector<int> counts;
		int matches = 0;
		cs::List<cs::String> words;
		int shownBuilds = -1, shownCategory = -1, shownHitCategory = -2;
		cs::String shownFilter;
		bool shownFiltering = false;

		/// The article's "see also", rebuilt when it or the Encyclopedia changes.
		cs::List<cs::Ref<Core::EncyclopediaArticle>> links;
		cs::String linksFor;
		int linksBuilds = -1;

		void Refresh(const Core::Encyclopedia& book, const Core::EncyclopediaKnowledge* k);
		void HandleKeys();
		/// Shows <paramref name="id"/>; with <paramref name="step"/>, as a step of the history (Back returns to the one before).
		void Go(const cs::String& id, bool step);
		void StepBack();
		void StepForward();
		void PickCategory(int c);
		int SelectedRow() const;
		void DrawFilter(Rect r);
		void DrawCategories(Rect area);
		void DrawList(Rect area);
		void DrawArticle(const Core::Encyclopedia& book, const Core::EncyclopediaKnowledge* k, Rect view);
		/// One line of text with the filter's words marked behind it.
		void DrawMarked(Rect r, const cs::String& s, const cs::Ref<GUIStyle>& style);
		float SectionHeight(const cs::String& head, const cs::String& body, bool controls, float w) const;
		float LinksHeight(float w) const;
		void EnsureStyles();
	};
}
