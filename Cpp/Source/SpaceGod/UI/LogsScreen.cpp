#include "UI/LogsScreen.h"

#include "Lore/LoreBooks.h"
#include "Lore/LoreLibrary.h"
#include "UI/UiStyle.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	LogsScreen::LogsScreen() = default;

	LogsScreen::~LogsScreen() = default;

	bool LogsScreen::Draw(const cs::List<cs::String>& keys, const LoreLibrary& lore, const cs::String& closeHint)
	{
		EnsureStyles();

		// Newest find first; a key whose text has since been retired drops out (no book, no item).
		cs::List<LoreBook> books;
		for (int i = keys.Count() - 1; i >= 0; i--)
		{
			LoreBook b;
			if (LoreBooks::Read(lore, keys[i], b)) books.Add(b);
		}
		if (!openKey.empty())
			for (int i = 0; i < books.Count(); i++)
				if (books[i].Key == openKey && i != selected) { selected = i; break; }
		selected = books.Count() == 0 ? 0 : Mathf::Clamp(selected, 0, books.Count() - 1);
		int pages = books.Count() == 0 ? 0 : books[selected].Pages.Count();
		page = pages == 0 ? 0 : Mathf::Clamp(page, 0, pages - 1);
		HandleKeys(books.Count(), pages);
		openKey = books.Count() == 0 ? cs::String() : books[selected].Key;

		auto screen = Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height));
		GUI::Box(screen, GUIContent::none(), panel);
		GUI::Label(Rect(Margin, 22, screen.width - 2 * Margin, 30), "RECOVERED LOGS", title);
		GUI::Label(Rect(Margin, 54, screen.width - 2 * Margin, 18),
			cs::Cat("Ship logs and station records you have found  \xC2\xB7  Up / Down for the next log, Left / Right to turn the page  \xC2\xB7  ", closeHint), small);

		float top = 86, bottom = screen.height - 64;
		if (books.Count() == 0)
		{
			auto r = Rect(Margin, top, screen.width - 2 * Margin, bottom - top);
			GUI::Label(Rect(r.x, r.y, r.width, HeadingH - 4), "NO LOGS YET", heading);
			GUI::Label(Rect(r.x, r.y + HeadingH, r.width, 80),
				"Logs are found where people left them: salvage a derelict or a wreck once nobody hostile is aboard, take a prize in a fight, "
				"or dock at a port to read its records. Not every hull or port has kept one.", text);
			return GUI::Button(Rect(screen.width - Margin - 120, screen.height - 50, 120, 32), "Back", button);
		}

		float listW = Mathf::Clamp(screen.width * 0.26f, 200.0f, 340.0f);
		DrawList(Rect(Margin, top, listW, bottom - top), books);

		float x = Margin + listW + Gutter;
		DrawBook(books[selected], Rect(x, top, screen.width - Margin - x, bottom - top));
		pages = books[selected].Pages.Count(); // a click on the list may have picked another log this event

		if (page > 0 && GUI::Button(Rect(x, screen.height - 50, 120, 32), "Previous", button)) Turn(page - 1);
		GUI::Label(Rect(x + 132, screen.height - 44, 120, 20), cs::Cat("Page ", page + 1, " of ", pages), small);
		if (page < pages - 1 && GUI::Button(Rect(x + 264, screen.height - 50, 120, 32), "Next", button)) Turn(page + 1);
		return GUI::Button(Rect(screen.width - Margin - 120, screen.height - 50, 120, 32), "Back", button);
	}

	void LogsScreen::HandleKeys(int count, int pages)
	{
		Event& e = Event::current();
		if (e.type != EventType::KeyDown) return;
		if (e.keyCode == KeyCode::DownArrow && selected < count - 1) { Select(selected + 1); e.Use(); }
		else if (e.keyCode == KeyCode::UpArrow && selected > 0) { Select(selected - 1); e.Use(); }
		else if (e.keyCode == KeyCode::RightArrow && page < pages - 1) { Turn(page + 1); e.Use(); }
		else if (e.keyCode == KeyCode::LeftArrow && page > 0) { Turn(page - 1); e.Use(); }
	}

	void LogsScreen::Select(int index)
	{
		if (index == selected) return;
		selected = index;
		openKey = cs::String(); // the list's index now leads; Draw takes the key again
		page = 0;
		scroll = Vector2::zero();
	}

	void LogsScreen::Turn(int to)
	{
		if (to == page) return;
		page = to;
		scroll = Vector2::zero();
	}

	void LogsScreen::DrawList(Rect area, const cs::List<LoreBook>& books)
	{
		GUI::Label(Rect(area.x, area.y, area.width, HeadingH - 4), cs::Cat("LOGS  (", books.Count(), ")"), heading);
		GUI::DrawTexture(Rect(area.x, area.y + HeadingH - 5, area.width, 1), buttonActiveTexture);
		auto list = Rect(area.x, area.y + HeadingH, area.width, area.height - HeadingH);
		float contentH = books.Count() * RowH;
		listScroll = GUI::BeginScrollView(list, listScroll, Rect(0, 0, list.width - 16, contentH));
		for (int i = 0; i < books.Count(); i++)
		{
			auto row = Rect(0, i * RowH, list.width - 16, RowH - 4);
			cs::String label = books[i].Author.empty() ? books[i].Title : cs::Cat(books[i].Title, "\n", books[i].Author);
			if (GUI::Button(row, label, i == selected ? entrySelected : entry)) Select(i);
		}
		GUI::EndScrollView();
	}

	void LogsScreen::DrawBook(const LoreBook& book, Rect view)
	{
		float w = view.width - 16;
		const cs::String& body = book.Pages[page];
		float bh = text->CalcHeight(GUIContent(body), w);
		float contentH = 28 + (book.Author.empty() ? 0.0f : 22.0f) + PageGap + bh;
		scroll = GUI::BeginScrollView(view, scroll, Rect(0, 0, w, contentH));
		float y = 0;
		GUI::Label(Rect(0, y, w, 28), cs::Strings::ToUpperInvariant(book.Title), bookTitle);
		y += 28;
		if (!book.Author.empty())
		{
			GUI::Label(Rect(0, y, w, 20), cs::Cat("by ", book.Author), small);
			y += 22;
		}
		GUI::DrawTexture(Rect(0, y + PageGap / 2, w, 1), buttonActiveTexture);
		y += PageGap;
		GUI::Label(Rect(0, y, w, bh), body, text);
		GUI::EndScrollView();
	}

	void LogsScreen::EnsureStyles()
	{
		if (stylesReady) return;
		stylesReady = true;
		// The Help screen's look (HelpScreen::EnsureStyles, UiStyle.h) at full-screen depth.
		namespace S = UiStyle;
		namespace T = UiStyle::Type;
		panelTexture = S::PanelFrame(S::Depth::Screen);
		buttonTexture = S::ControlTexture();
		buttonActiveTexture = S::ControlOnTexture();
		panel = S::PanelStyle(panelTexture);
		title = S::TitleStyle(T::Screen);
		bookTitle = S::TitleStyle(T::Chapter);
		heading = S::TextStyle(T::Heading, S::Accent, FontStyle::Bold);
		text = S::TextStyle(T::Body);
		text->wordWrap = true;
		small = S::CaptionStyle(T::Body);
		small->fontStyle = FontStyle::Italic;
		button = S::ButtonStyle(T::Heading, buttonTexture, buttonActiveTexture);
		entry = S::EntryStyle(T::Body, buttonTexture, buttonActiveTexture);
		entry->wordWrap = true;
		entrySelected = S::EntrySelectedStyle(entry, buttonActiveTexture);
	}

	void LogsScreen::DestroyOwnedTextures()
	{
		panelTexture = nullptr;
		buttonTexture = nullptr;
		buttonActiveTexture = nullptr;
		stylesReady = false;
	}
}
