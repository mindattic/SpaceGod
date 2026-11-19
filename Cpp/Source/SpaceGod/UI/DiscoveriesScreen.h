#pragma once

// DiscoveriesScreen (US-T259): the discovery log, everything the player has found, to look back on and plan from.

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
	class DiscoveryEntry;
	class DiscoveryLog;
}

namespace SpaceGod::UI
{
	/// The discoveries screen (Shift+O), in the Help screen's look (UiStyle): the kinds of find down the left (stars charted,
	/// bodies surveyed, settlements, pirate havens, graveyards, derelicts, void sights, sightings, pilots met) with how many of
	/// each, the finds of the kind picked in the middle, newest first with the day each was found, and the one picked on the
	/// right: its scan readings as the simulation stands now, its Prose description ("No record on file." without one), and
	/// buttons to select it, focus the camera on it, or plot a course to where it is. Up / Down pick a find, Left / Right a kind,
	/// Page Up / Page Down / Home / End jump down the list, which scrolls to keep the pick in view (US-T301). The search field
	/// (US-T301) narrows every kind's finds to those whose name, system, finder or note hold each word typed.
	class DiscoveriesScreen final
	{
	public:
		explicit DiscoveriesScreen(UnitySpaceApp* app);
		~DiscoveriesScreen();

		/// Draws the screen; true once it should close (Back, or a button that went to the find). Esc and the key that opens
		/// it are the owner's to handle, except while the search is typed in (Esc then leaves the field).
		bool Draw(const cs::String& closeHint);

		/// Whether the search field has the keys (the owner hands the game's input over meanwhile).
		bool Typing() const { return typing; }
		/// The screen has been closed: the search lets the keys go.
		void Closed();

		/// The screen's own textures go with whatever owns it (U.SPC-T366).
		void DestroyOwnedTextures();

	private:
		static constexpr float Margin = 40, Gutter = 24, HeadingH = 24, KindRowH = 34, RowH = 44;
		static constexpr const char* FilterControl = "DiscoveriesFilter";

		UnitySpaceApp* app;
		bool stylesReady = false;
		cs::Ref<GUIStyle> panel, title, entryTitle, heading, text, small, button, entry, entrySelected, field;
		cs::Ref<Texture2D> panelTexture, buttonTexture, buttonActiveTexture, fieldTexture;
		Vector2 scroll, listScroll;
		int kind = 0, selected = 0;
		/// The list scrolls to the pick on the next draw (a key moved it, not a click).
		bool follow = false;
		/// Rows the list showed last draw: how far Page Up / Page Down go.
		int pageRows = 8;

		cs::String filter;
		bool typing = false, focusFilter = false;

		/// The finds of the kind picked that match the search, newest first, rebuilt when the log, the kind or the search
		/// changes; and every kind's matches.
		cs::List<cs::Ref<Core::DiscoveryEntry>> shown;
		std::vector<int> counts;
		int shownVersion = -1, shownKind = -1;
		cs::String shownFilter;
		const Core::DiscoveryLog* shownLog = nullptr;

		void Refresh(const Core::DiscoveryLog& log);
		void HandleKeys(int count);
		void Pick(int index);
		void PickKind(int k);
		void DrawFilter(Rect r);
		void DrawKinds(Rect area, const Core::DiscoveryLog& log);
		void DrawList(Rect area);
		/// Whether <paramref name="e"/> holds every one of <paramref name="words"/> (lower case) in its name, system, finder or note.
		static bool Matches(const Core::DiscoveryEntry& e, const cs::List<cs::String>& words);
		/// The find's card; true when one of its buttons went to it (the screen closes).
		bool DrawEntry(const Core::DiscoveryEntry& e, Rect view, float buttonsY);
		void EnsureStyles();

		static cs::String Date(double jd);
		/// What finds a kind, for an empty list.
		static const char* HowFound(int kind);
	};
}
