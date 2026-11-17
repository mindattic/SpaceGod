#pragma once

// SocietyScreen (US-T293, the Civilization epic's P10): the galaxy's people, and the SpaceGod's hand in their affairs.

#include "Cs/Cs.h"
#include "CivHand.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

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

	private:
		static constexpr float Margin = 40, Gutter = 24, HeadingH = 24, TabRowH = 30, RowH = 46, LineH = 20, SectionGap = 12;

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

		UnitySpaceApp* app;
		bool stylesReady = false;
		cs::Ref<GUIStyle> panel, title, entryTitle, heading, text, small, button, entry, entrySelected, rowTitle, rowLine, link, good, bad;
		cs::Ref<Texture2D> panelTexture, buttonTexture, buttonActiveTexture;
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
		void Select(const Row& r);
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

		/// The module group of a kind (an index into the group names), and the names.
		static int GroupOf(int kind);
		static int GroupCount();
		static const char* GroupName(int g);
	};
}
