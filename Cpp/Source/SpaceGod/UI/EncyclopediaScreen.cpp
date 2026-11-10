#include "UI/EncyclopediaScreen.h"

#include "UnitySpaceApp.h"
#include "Lore/Encyclopedia.h"
#include "UI/UiMotion.h"
#include "UI/UiStyle.h"

#include <algorithm>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		/// The mark behind a matched word: the accent, see-through.
		constexpr Color EncyclopediaMark{0.45f, 0.85f, 1.0f, 0.38f};

		float EncyclopediaTextWidth(const GUIStyle& style, const cs::String& s)
		{
			if (s.empty()) return 0;
			return Mathf::Max(0.0f, style.CalcSize(GUIContent(s)).x - style.padding.horizontal());
		}

		/// The link buttons laid out in rows across <paramref name="w"/>: each one's rect.
		std::vector<Rect> EncyclopediaLinkRects(const cs::List<cs::Ref<EncyclopediaArticle>>& links, const GUIStyle& style, float w)
		{
			std::vector<Rect> rects;
			float x = 0, y = 0;
			for (const auto& l : links)
			{
				float bw = Mathf::Min(w, style.CalcSize(GUIContent(l->Title)).x + 16);
				if (x > 0 && x + bw > w)
				{
					x = 0;
					y += 30;
				}
				rects.push_back(Rect(x, y, bw, 26));
				x += bw + 8;
			}
			return rects;
		}
	}

	EncyclopediaScreen::EncyclopediaScreen(UnitySpaceApp* app) : app(app) {}

	EncyclopediaScreen::~EncyclopediaScreen() = default;

	void EncyclopediaScreen::Opened()
	{
		focusFilter = true;
		closeAsked = false;
	}

	void EncyclopediaScreen::Closed()
	{
		if (typing) GUIUtility::keyboardControl = 0;
		typing = false;
		focusFilter = false;
	}

	void EncyclopediaScreen::Show(const cs::String& id)
	{
		filter.clear();
		hitCategory = -1;
		Go(id, true);
	}

	void EncyclopediaScreen::Search(const cs::String& query)
	{
		filter = query;
		hitCategory = -1;
		focusFilter = true;
	}

	// ---------------------------------------------------------------- the rows and the history

	void EncyclopediaScreen::Refresh(const Encyclopedia& book, const EncyclopediaKnowledge* k)
	{
		int builds = app->EncyclopediaBuilds();
		if (builds == shownBuilds && filter == shownFilter && category == shownCategory && hitCategory == shownHitCategory) return;
		bool filterChanged = filter != shownFilter;
		bool categoryChanged = category != shownCategory || hitCategory != shownHitCategory;
		shownBuilds = builds;
		shownFilter = filter;
		shownCategory = category;
		shownHitCategory = hitCategory;

		rows.clear();
		counts.assign(Encyclopedia::CategoryCount, 0);
		words = Encyclopedia::Words(filter);
		bool filtering = words.Count() > 0;
		if (!filtering)
		{
			for (int c = 0; c < Encyclopedia::CategoryCount; c++) counts[c] = book.CountKnown(static_cast<EncyclopediaCategory>(c), k);
			for (const auto& a : book.Known(static_cast<EncyclopediaCategory>(category), k)) rows.push_back(Row{ a, a->Subtitle });
		}
		else
		{
			auto hits = book.Search(filter, k);
			matches = hits.Count();
			for (const auto& h : hits)
			{
				int c = static_cast<int>(h.Article->Category);
				counts[c]++;
				if (hitCategory < 0 || c == hitCategory) rows.push_back(Row{ h.Article, h.Snippet.empty() ? h.Article->Subtitle : h.Snippet });
			}
		}
		// A new search, or another category picked, reads its first article (not a step of the history: typing would fill it).
		bool restart = (filterChanged && filtering) || (categoryChanged && !filterChanged) || (filterChanged && !filtering && shownFiltering);
		shownFiltering = filtering;
		if ((restart || current.empty()) && !rows.empty() && SelectedRow() < 0) Go(rows[0].Article->Id, false);
		listScroll = filterChanged || categoryChanged ? Vector2::zero() : listScroll;
	}

	int EncyclopediaScreen::SelectedRow() const
	{
		for (int i = 0; i < static_cast<int>(rows.size()); i++)
			if (rows[i].Article->Id == current) return i;
		return -1;
	}

	void EncyclopediaScreen::Go(const cs::String& id, bool step)
	{
		if (id.empty() || id == current) return;
		if (step && !current.empty())
		{
			back.Add(current);
			if (back.Count() > 64) back.RemoveAt(0);
			forward.Clear();
		}
		current = id;
		scroll = Vector2::zero();
		// Unfiltered, the list follows the article to its category (a link, or a step back, may lead to another).
		if (words.Count() == 0)
		{
			size_t slash = id.find('/');
			cs::String slug = slash == cs::String::npos ? id : id.substr(0, slash);
			for (int c = 0; c < Encyclopedia::CategoryCount; c++)
				if (slug == Encyclopedia::Slug(static_cast<EncyclopediaCategory>(c))) category = c;
		}
	}

	void EncyclopediaScreen::StepBack()
	{
		if (back.Count() == 0) return;
		forward.Add(current);
		cs::String to = back[back.Count() - 1];
		back.RemoveAt(back.Count() - 1);
		current.clear();
		Go(to, false);
	}

	void EncyclopediaScreen::StepForward()
	{
		if (forward.Count() == 0) return;
		back.Add(current);
		cs::String to = forward[forward.Count() - 1];
		forward.RemoveAt(forward.Count() - 1);
		current.clear();
		Go(to, false);
	}

	void EncyclopediaScreen::PickCategory(int c)
	{
		if (words.Count() > 0)
		{
			hitCategory = hitCategory == c ? -1 : c;
			return;
		}
		if (c == category) return;
		category = c;
	}

	// ---------------------------------------------------------------- drawing

	bool EncyclopediaScreen::Draw(const cs::String& closeHint)
	{
		EnsureStyles();
		UiMotion::Scope motion("encyclopedia", 0); // US-T255: the whole screen fades in
		const auto& book = app->EncyclopediaBook();
		const EncyclopediaKnowledge* k = &app->Knowledge();
		Refresh(book, k);
		HandleKeys();
		Refresh(book, k);

		auto screen = Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height));
		GUI::Box(screen, GUIContent::none(), panel);
		GUI::Label(Rect(Margin, 22, 300, 30), "ENCYCLOPEDIA", title);
		float fieldW = Mathf::Clamp(screen.width * 0.34f, 260.0f, 460.0f);
		DrawFilter(Rect(screen.width - Margin - fieldW, 22, fieldW, 30));
		GUI::Label(Rect(Margin, 56, screen.width - 2 * Margin, 18),
			cs::Cat("Everything written down, gathered as you go  \xC2\xB7  type to search titles, text and keywords  \xC2\xB7  Up / Down an article, Left / Right a category, Alt+Left / Alt+Right back and forward  \xC2\xB7  ", closeHint), small);

		float top = 88, bottom = screen.height - 64;
		float catW = Mathf::Clamp(screen.width * 0.15f, 170.0f, 220.0f);
		DrawCategories(Rect(Margin, top, catW, bottom - top));
		float listX = Margin + catW + Gutter;
		float listW = Mathf::Clamp(screen.width * 0.25f, 230.0f, 380.0f);
		DrawList(Rect(listX, top, listW, bottom - top));
		float x = listX + listW + Gutter;
		DrawArticle(book, k, Rect(x, top, screen.width - Margin - x, bottom - top));

		// Back and forward through what was read.
		float by = screen.height - 50;
		bool enabled = GUI::enabled;
		GUI::enabled = enabled && back.Count() > 0;
		if (GUI::Button(Rect(x, by, 110, 32), "<  Back", button)) StepBack();
		GUI::enabled = enabled && forward.Count() > 0;
		if (GUI::Button(Rect(x + 122, by, 120, 32), "Forward  >", button)) StepForward();
		GUI::enabled = enabled;

		if (!pending.empty())
		{
			cs::String to = pending;
			pending.clear();
			Go(to, pendingStep);
		}
		bool close = GUI::Button(Rect(screen.width - Margin - 120, by, 120, 32), "Close", button) || closeAsked;
		closeAsked = false;
		return close;
	}

	void EncyclopediaScreen::HandleKeys()
	{
		Event& e = Event::current();
		if (e.type != EventType::KeyDown) return;
		int sel = SelectedRow();
		int n = static_cast<int>(rows.size());
		if (e.keyCode == KeyCode::Escape && typing) { closeAsked = true; e.Use(); }
		else if ((e.keyCode == KeyCode::Return || e.keyCode == KeyCode::KeypadEnter) && typing) { GUIUtility::keyboardControl = 0; typing = false; e.Use(); }
		else if (e.keyCode == KeyCode::DownArrow && n > 0) { Go(rows[sel < 0 ? 0 : Mathf::Min(sel + 1, n - 1)].Article->Id, false); e.Use(); }
		else if (e.keyCode == KeyCode::UpArrow && n > 0) { Go(rows[sel < 0 ? 0 : Mathf::Max(sel - 1, 0)].Article->Id, false); e.Use(); }
		else if (e.keyCode == KeyCode::LeftArrow && e.alt) { StepBack(); e.Use(); }
		else if (e.keyCode == KeyCode::RightArrow && e.alt) { StepForward(); e.Use(); }
		else if (!typing && e.keyCode == KeyCode::RightArrow)
		{
			int c = words.Count() > 0 ? hitCategory : category;
			if (c < Encyclopedia::CategoryCount - 1) PickCategory(c + 1);
			e.Use();
		}
		else if (!typing && e.keyCode == KeyCode::LeftArrow)
		{
			int c = words.Count() > 0 ? hitCategory : category;
			if (c > (words.Count() > 0 ? -1 : 0))
			{
				if (words.Count() > 0) hitCategory = c - 1;
				else PickCategory(c - 1);
			}
			e.Use();
		}
	}

	void EncyclopediaScreen::DrawFilter(Rect r)
	{
		GUI::SetNextControlName(FilterControl);
		filter = GUI::TextField(r, filter, 60, field);
		if (focusFilter)
		{
			GUI::FocusControl(FilterControl);
			focusFilter = GUI::GetNameOfFocusedControl() != FilterControl;
		}
		typing = GUI::GetNameOfFocusedControl() == FilterControl;
		if (filter.empty())
			GUI::Label(Rect(r.x + 10, r.y + 6, r.width - 20, 18), typing ? "type a word: pilot, mars, cargo, lock..." : "click to search every article", small);
		else if (GUI::Button(Rect(r.x + r.width - 28, r.y + 3, 24, 24), "X", button))
		{
			filter.clear();
			hitCategory = -1;
			focusFilter = true;
		}
	}

	void EncyclopediaScreen::DrawCategories(Rect area)
	{
		bool filtering = words.Count() > 0;
		float y = area.y;
		int total = 0;
		for (int c : counts) total += c;
		GUI::Label(Rect(area.x, y, area.width, HeadingH - 4), filtering ? cs::Cat("MATCHES  (", matches, ")") : cs::Cat("KNOWN  (", total, ")"), heading);
		GUI::DrawTexture(Rect(area.x, y + HeadingH - 5, area.width, 1), buttonActiveTexture);
		y += HeadingH;
		if (filtering)
		{
			if (GUI::Button(Rect(area.x, y, area.width, CategoryRowH - 4), cs::Cat("All  (", matches, ")"), hitCategory < 0 ? entrySelected : entry)) hitCategory = -1;
			y += CategoryRowH;
		}
		for (int c = 0; c < Encyclopedia::CategoryCount; c++)
		{
			bool on = filtering ? hitCategory == c : category == c;
			cs::String label = cs::Cat(Encyclopedia::Heading(static_cast<EncyclopediaCategory>(c)), "  (", counts.empty() ? 0 : counts[c], ")");
			if (GUI::Button(Rect(area.x, y, area.width, CategoryRowH - 4), label, on ? entrySelected : entry)) PickCategory(c);
			y += CategoryRowH;
		}
	}

	void EncyclopediaScreen::DrawList(Rect area)
	{
		bool filtering = words.Count() > 0;
		cs::String head = filtering ? cs::Cat("FOR \"", cs::Strings::ToUpperInvariant(cs::Strings::Trim(filter)), "\"")
			: cs::Strings::ToUpperInvariant(cs::String(Encyclopedia::Heading(static_cast<EncyclopediaCategory>(category))));
		GUI::Label(Rect(area.x, area.y, area.width, HeadingH - 4), head, heading);
		GUI::DrawTexture(Rect(area.x, area.y + HeadingH - 5, area.width, 1), buttonActiveTexture);
		auto list = Rect(area.x, area.y + HeadingH, area.width, area.height - HeadingH);
		int n = static_cast<int>(rows.size());
		if (n == 0)
		{
			cs::String why = filtering ? cs::Cat("No article you know holds \"", cs::Strings::Trim(filter), "\". More are written in as you find things.")
				: cs::String(Encyclopedia::HowLearned(static_cast<EncyclopediaCategory>(category)));
			GUI::Label(Rect(list.x, list.y + 4, list.width, 120), why, text);
			return;
		}
		float w = list.width - 16;
		listScroll = GUI::BeginScrollView(list, listScroll, Rect(0, 0, w, n * RowH));
		int sel = SelectedRow();
		// Only the rows in view are drawn: a long career meets hundreds of pilots.
		int first = Mathf::Max(0, static_cast<int>(listScroll.y / RowH) - 1);
		int last = Mathf::Min(n - 1, static_cast<int>((listScroll.y + list.height) / RowH) + 1);
		for (int i = first; i <= last; i++)
		{
			const auto& r = rows[i];
			auto row = Rect(0, i * RowH, w, RowH - 4);
			if (GUI::Button(row, GUIContent::none(), i == sel ? entrySelected : entry))
			{
				pending = r.Article->Id;
				pendingStep = true;
			}
			cs::String name = filtering ? cs::Cat(r.Article->Title, "   ", Encyclopedia::Heading(r.Article->Category)) : r.Article->Title;
			DrawMarked(Rect(row.x + 8, row.y + 3, row.width - 14, 20), name, rowTitle);
			DrawMarked(Rect(row.x + 8, row.y + 22, row.width - 14, 18), r.Line, rowLine);
		}
		GUI::EndScrollView();
	}

	void EncyclopediaScreen::DrawMarked(Rect r, const cs::String& s, const cs::Ref<GUIStyle>& style)
	{
		if (Event::current().type == EventType::Repaint)
			for (const auto& w : words)
			{
				size_t at = Encyclopedia::FindNoCase(s, w);
				if (at == cs::String::npos) continue;
				float x = r.x + style->padding.left + EncyclopediaTextWidth(*style, s.substr(0, at));
				float mw = EncyclopediaTextWidth(*style, s.substr(at, w.size()));
				float right = Mathf::Min(x + mw, r.xMax());
				if (right <= x) continue;
				Color keep = GUI::color;
				GUI::color = EncyclopediaMark;
				GUI::DrawTexture(Rect(x - 1, r.y + 1, right - x + 2, r.height - 2), Texture2D::whiteTexture());
				GUI::color = keep;
			}
		GUI::Label(r, s, style);
	}

	float EncyclopediaScreen::SectionHeight(const cs::String& head, const cs::String& body, bool controls, float w) const
	{
		if (controls)
		{
			float hk = head.empty() ? 0.0f : keys->CalcHeight(GUIContent(head), KeyW - 8);
			return Mathf::Max(hk, text->CalcHeight(GUIContent(body), w - KeyW)) + LineGap;
		}
		float h = head.empty() ? 0.0f : HeadingH;
		return h + (body.empty() ? 0.0f : text->CalcHeight(GUIContent(body), w)) + SectionGap;
	}

	float EncyclopediaScreen::LinksHeight(float w) const
	{
		if (links.Count() == 0) return 0;
		auto rects = EncyclopediaLinkRects(links, *link, w);
		return HeadingH + rects.back().yMax() + SectionGap;
	}

	void EncyclopediaScreen::DrawArticle(const Encyclopedia& book, const EncyclopediaKnowledge* k, Rect view)
	{
		auto a = current.empty() ? nullptr : book.Find(current);
		if (a == nullptr || !Encyclopedia::IsKnown(*a, k))
		{
			GUI::Label(Rect(view.x, view.y, view.width, HeadingH - 4), "NOTHING OPEN", heading);
			GUI::Label(Rect(view.x, view.y + HeadingH, view.width, 60), "Pick an article on the left, or type in the search above.", text);
			return;
		}
		if (linksFor != a->Id || linksBuilds != app->EncyclopediaBuilds())
		{
			links = book.LinksOf(*a, k);
			linksFor = a->Id;
			linksBuilds = app->EncyclopediaBuilds();
		}
		// US-T272: the thing the article is about, as the scan card reads it now (the same code as the card), or its last
		// known readings and how old they are, in place of the readings taken when it was built; and the Prose of the card's
		// other tabs (its history, culture...) after its own sections.
		auto live = app->ArticleScanStats(*a);
		cs::String liveText;
		for (const auto& l : live) liveText = cs::Cat(liveText, liveText.empty() ? "" : "\n", l);
		const cs::String liveHeading = app->ArticleStatsLive() ? "READINGS (LIVE)" : "READINGS (LAST KNOWN)";
		bool controls = a->Category == EncyclopediaCategory::Controls;
		cs::List<EncyclopediaSection> sections = a->Sections;
		for (const auto& s : app->ArticleScanTabs(*a)) sections.Add(s);

		float w = view.width - 16;
		float contentH = 30 + 22 + 12;
		for (const auto& s : sections)
		{
			bool isLive = !liveText.empty() && s.Heading == EncyclopediaArticle::LiveSection;
			contentH += SectionHeight(isLive ? liveHeading : s.Heading, isLive ? liveText : s.Text, controls, w);
		}
		if (controls) contentH += SectionGap;
		contentH += LinksHeight(w);
		float cardH = view.height - 44; // the buttons' row below

		scroll = GUI::BeginScrollView(Rect(view.x, view.y, view.width, cardH), scroll, Rect(0, 0, w, contentH));
		float y = 0;
		DrawMarked(Rect(0, y, w, 30), cs::Strings::ToUpperInvariant(a->Title), entryTitle);
		y += 30;
		GUI::Label(Rect(0, y, w, 20), cs::Cat(Encyclopedia::Heading(a->Category), a->Subtitle.empty() ? cs::String() : cs::Cat("  \xC2\xB7  ", a->Subtitle)), small);
		y += 22;
		GUI::DrawTexture(Rect(0, y + 4, w, 1), buttonActiveTexture);
		y += 12;
		for (const auto& s : sections)
		{
			bool isLive = !liveText.empty() && s.Heading == EncyclopediaArticle::LiveSection;
			const cs::String& body = isLive ? liveText : s.Text;
			if (controls)
			{
				float h = SectionHeight(s.Heading, body, true, w) - LineGap;
				if (!s.Heading.empty()) GUI::Label(Rect(0, y, KeyW - 8, h), s.Heading, keys);
				GUI::Label(Rect(KeyW, y, w - KeyW, h), body, s.Heading.empty() ? small : text);
				y += h + LineGap;
				continue;
			}
			if (!s.Heading.empty())
			{
				GUI::Label(Rect(0, y, w, HeadingH - 4), isLive ? liveHeading : cs::Strings::ToUpperInvariant(s.Heading), heading);
				y += HeadingH;
			}
			if (!body.empty())
			{
				float h = text->CalcHeight(GUIContent(body), w);
				GUI::Label(Rect(0, y, w, h), body, text);
				y += h;
			}
			y += SectionGap;
		}
		if (controls) y += SectionGap;
		if (links.Count() > 0)
		{
			GUI::Label(Rect(0, y, w, HeadingH - 4), "SEE ALSO", heading);
			GUI::DrawTexture(Rect(0, y + HeadingH - 5, w, 1), buttonActiveTexture);
			y += HeadingH;
			auto rects = EncyclopediaLinkRects(links, *link, w);
			for (int i = 0; i < links.Count(); i++)
			{
				auto r = rects[i];
				r.y += y;
				if (GUI::Button(r, links[i]->Title, link))
				{
					pending = links[i]->Id;
					pendingStep = true;
				}
			}
		}
		GUI::EndScrollView();
	}

	void EncyclopediaScreen::EnsureStyles()
	{
		if (stylesReady) return;
		stylesReady = true;
		// The Help screen's look (HelpScreen::EnsureStyles, UiStyle.h) at full-screen depth.
		namespace S = UiStyle;
		namespace T = UiStyle::Type;
		panelTexture = S::PanelFrame(S::Depth::Screen);
		buttonTexture = S::ControlTexture();
		buttonActiveTexture = S::ControlOnTexture();
		fieldTexture = S::FieldTexture();
		panel = S::PanelStyle(panelTexture);
		title = S::TitleStyle(T::Screen);
		entryTitle = S::TitleStyle(T::Chapter);
		heading = S::TextStyle(T::Heading, S::Accent, FontStyle::Bold);
		keys = S::TextStyle(T::Body, S::Heading, FontStyle::Bold);
		keys->wordWrap = true;
		text = S::TextStyle(T::Body);
		text->wordWrap = true;
		small = S::CaptionStyle(T::Body);
		small->fontStyle = FontStyle::Italic;
		button = S::ButtonStyle(T::Heading, buttonTexture, buttonActiveTexture);
		link = S::ButtonStyle(T::Body, buttonTexture, buttonActiveTexture);
		entry = S::EntryStyle(T::Body, buttonTexture, buttonActiveTexture);
		entrySelected = S::EntrySelectedStyle(entry, buttonActiveTexture);
		field = S::FieldStyle(T::Body, fieldTexture);
		rowTitle = S::TextStyle(T::Body, S::Heading, FontStyle::Bold);
		rowTitle->clipping = TextClipping::Clip;
		rowLine = S::CaptionStyle(T::Caption);
		rowLine->clipping = TextClipping::Clip;
	}

	void EncyclopediaScreen::DestroyOwnedTextures()
	{
		panelTexture = nullptr;
		buttonTexture = nullptr;
		buttonActiveTexture = nullptr;
		fieldTexture = nullptr;
		stylesReady = false;
	}
}
