#pragma once

// DiscoveriesScreen (US-T259): the discovery log, everything the player has found, to look back on and plan from.

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

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
	/// buttons to select it, focus the camera on it, or plot a course to where it is. Up / Down pick a find, Left / Right a kind.
	class DiscoveriesScreen final
	{
	public:
		explicit DiscoveriesScreen(UnitySpaceApp* app);
		~DiscoveriesScreen();

		/// Draws the screen; true once it should close (Back, or a button that went to the find). Esc and the key that opens
		/// it are the owner's to handle.
		bool Draw(const cs::String& closeHint);

		/// The screen's own textures go with whatever owns it (U.SPC-T366).
		void DestroyOwnedTextures();

	private:
		static constexpr float Margin = 40, Gutter = 24, HeadingH = 24, KindRowH = 34, RowH = 44;

		UnitySpaceApp* app;
		bool stylesReady = false;
		cs::Ref<GUIStyle> panel, title, entryTitle, heading, text, small, button, entry, entrySelected;
		cs::Ref<Texture2D> panelTexture, buttonTexture, buttonActiveTexture;
		Vector2 scroll, listScroll;
		int kind = 0, selected = 0;

		/// The finds of the kind picked, newest first, rebuilt when the log or the kind changes.
		cs::List<cs::Ref<Core::DiscoveryEntry>> shown;
		int shownVersion = -1, shownKind = -1;
		const Core::DiscoveryLog* shownLog = nullptr;

		void Refresh(const Core::DiscoveryLog& log);
		void HandleKeys(int count);
		void Pick(int index);
		void PickKind(int k);
		void DrawKinds(Rect area, const Core::DiscoveryLog& log);
		void DrawList(Rect area);
		/// The find's card; true when one of its buttons went to it (the screen closes).
		bool DrawEntry(const Core::DiscoveryEntry& e, Rect view, float buttonsY);
		void EnsureStyles();

		static cs::String Date(double jd);
		/// What finds a kind, for an empty list.
		static const char* HowFound(int kind);
	};
}
