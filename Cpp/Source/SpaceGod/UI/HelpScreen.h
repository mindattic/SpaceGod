#pragma once

// HelpScreen: the C++ port of Unity.SpaceGod's Runtime/UI/HelpScreen.cs.

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

namespace SpaceGod::Core
{
	class ManualChapter;
	class HelpSection;
	struct HelpLine;
}

namespace SpaceGod::UI
{
	class ShipCatalogScreen;

	/// The Help screen (U.SPC-T494, U.SPC-T497): the in-game manual, the whole screen. A contents list of the
	/// GameManual chapters down the left; the chapter picked on the right, its topics and then the
	/// keys for it from ControlsHelp, scrolling when it runs past the bottom (the last chapter is every
	/// key). Up / Down (or Previous / Next) step through the chapters. The main menu opens it from Help, the game
	/// from H / F1 and the Esc menu's Help. Its Ship catalog button (US-T193) swaps the manual for the full ship
	/// catalog (ShipCatalogScreen), and Manual swaps it back.
	class HelpScreen final
	{
	public:
		HelpScreen();
		~HelpScreen();

		/// Draws the screen; true once the player has clicked Back (Esc and the keys that open it are the owner's to handle).
		bool Draw(const cs::String& closeHint);

		/// The screen's own textures go with whatever owns it (U.SPC-T366): DontSave outlives the scene otherwise.
		void DestroyOwnedTextures();

	private:
		static constexpr float Margin = 40, Gutter = 28, KeyW = 150, LineGap = 3, TopicGap = 14, HeadingH = 24, ContentsRowH = 30;

		bool stylesReady = false;
		cs::Ref<GUIStyle> panel, title, chapterTitle, heading, keys, text, small, button, entry, entrySelected;
		cs::Ref<Texture2D> panelTexture, buttonTexture, buttonActiveTexture;
		Vector2 scroll, contentsScroll;
		int chapter = 0;

		/// The ship catalog page (US-T193), and whether it has the screen.
		cs::Ref<ShipCatalogScreen> catalog;
		bool showCatalog = false;

		void HandleKeys(int count);
		void Select(int index);
		void DrawContents(Rect area, const cs::List<cs::Ref<Core::ManualChapter>>& chapters);
		void DrawChapter(const cs::Ref<Core::ManualChapter>& c, Rect view);
		float ChapterHeight(const cs::Ref<Core::ManualChapter>& c, float w);
		float LineHeight(const Core::HelpLine& line, float w);
		float DrawSection(const cs::Ref<Core::HelpSection>& section, float y, float w);
		void EnsureStyles();
	};
}
