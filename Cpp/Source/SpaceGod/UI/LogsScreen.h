#pragma once

// LogsScreen (US-T261): the Prose books the player has recovered, to read.

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityGui.h"

namespace SpaceGod::Core
{
	class LoreLibrary;
	struct LoreBook;
}

namespace SpaceGod::UI
{
	/// The logs screen (Shift+L), in the Help screen's look (UiStyle): the ship logs and station records recovered from
	/// derelicts, wrecks, prizes and ports (Core::LoreBooks), listed down the left by title and author, newest first; the one
	/// picked is read on the right a page at a time. Up / Down pick a log, Left / Right turn its pages. Only books Prose wrote
	/// are ever listed: with none recovered it says how logs are found.
	class LogsScreen final
	{
	public:
		LogsScreen();
		~LogsScreen();

		/// Draws the screen over <paramref name="keys"/> (recovered book keys, oldest first); true once the player has clicked
		/// Back (Esc and the key that opens it are the owner's to handle).
		bool Draw(const cs::List<cs::String>& keys, const Core::LoreLibrary& lore, const cs::String& closeHint);

		/// The screen's own textures go with whatever owns it (U.SPC-T366).
		void DestroyOwnedTextures();

	private:
		static constexpr float Margin = 40, Gutter = 28, HeadingH = 24, RowH = 44, PageGap = 14;

		bool stylesReady = false;
		cs::Ref<GUIStyle> panel, title, bookTitle, heading, text, small, button, entry, entrySelected;
		cs::Ref<Texture2D> panelTexture, buttonTexture, buttonActiveTexture;
		Vector2 scroll, listScroll;
		int selected = 0, page = 0;
		/// The key last read, so a new find (listed first) does not move the reader off the book open.
		cs::String openKey;

		void HandleKeys(int count, int pages);
		void Select(int index);
		void Turn(int to);
		void DrawList(Rect area, const cs::List<Core::LoreBook>& books);
		void DrawBook(const Core::LoreBook& book, Rect view);
		void EnsureStyles();
	};
}
