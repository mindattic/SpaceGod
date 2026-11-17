#include "UI/SocietyScreen.h"

#include "UnitySpaceApp.h"
#include "UI/UiMotion.h"
#include "UI/UiStyle.h"

#include "Astro.h"
#include "SimClock.h"
#include "Ships.h"
#include "Economy/Settlement.h"
#include "Systems/Civilization/Civilization.h"
#include "Systems/Civilization/Culture.h"
#include "Systems/Civilization/Kinship.h"
#include "Systems/Civilization/Narrator.h"
#include "Systems/Civilization/Politics.h"
#include "Systems/Civilization/Strife.h"
#include "Systems/Galaxy.h"
#include "Systems/Polities.h"
#include "Systems/Society.h"

#include "HAL/PlatformTime.h"

#include <algorithm>
#include <cmath>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		const char* const SocietyPageNames[] = { "Feed", "Houses", "People", "Organisations", "Worlds" };

		/// The module views' names on the screen (CivHands::Views(), in its order).
		const char* SocietyViewName(const cs::String& v)
		{
			if (v == "feuds") return "Feuds";
			if (v == "regimes") return "Regimes";
			if (v == "trials") return "Trials";
			if (v == "admirals") return "Admirals & strikes";
			if (v == "crime") return "Crime";
			if (v == "money") return "Money";
			if (v == "chronicle") return "Chronicle";
			return "View";
		}

		/// The module groups of the feed, by the first kind of each (Civilization.h's SocialKind, in its order). A kind past
		/// the last group's (one a later module adds) falls in "Other", so it still shows.
		struct SocietyGroup
		{
			const char* Name;
			SocialKind First;
		};
		const SocietyGroup SocietyGroups[] = {
			{ "Society", SocialKind::Birth }, { "Persona", SocialKind::Friendship }, { "Kinship", SocialKind::FeudDeclared },
			{ "Politics", SocialKind::IdeologyShift }, { "Strife", SocialKind::Protest }, { "Underworld", SocialKind::SyndicateRises },
			{ "Commerce", SocialKind::UnionFormed }, { "Culture", SocialKind::FaithFounded }, { "Health", SocialKind::Outbreak },
			{ "Press", SocialKind::OutletFounded },
		};
		constexpr int SocietyGroupsKnown = static_cast<int>(sizeof(SocietyGroups) / sizeof(SocietyGroups[0]));

		cs::String SocietyPct(double v) { return cs::Cat(cs::Fmt(v * 100, "0"), "%"); }
		cs::String SocietySigned(double v) { return cs::Cat(v >= 0 ? "+" : "", cs::Fmt(v, "0.00")); }
		cs::String SocietyDate(double jd) { return jd <= 0 ? cs::String("?") : Astro::ToDateTime(jd).ToString("yyyy-MM-dd"); }
	}

	int SocietyScreen::GroupOf(int k)
	{
		if (k > static_cast<int>(SocialKind::Donation)) return SocietyGroupsKnown; // a kind added after the Press module's
		int g = 0;
		for (int i = 0; i < SocietyGroupsKnown; i++)
			if (static_cast<int>(SocietyGroups[i].First) <= k) g = i;
		return g;
	}

	int SocietyScreen::GroupCount() { return SocietyGroupsKnown + 1; }

	const char* SocietyScreen::GroupName(int g) { return g >= 0 && g < SocietyGroupsKnown ? SocietyGroups[g].Name : "Other"; }

	SocietyScreen::SocietyScreen(UnitySpaceApp* app) : app(app) {}

	SocietyScreen::~SocietyScreen() = default;

	// ---------------------------------------------------------------- the rows

	cs::String SocietyScreen::RowsKey() const
	{
		return cs::Cat(page, "|", group, "|", kind, "|", worldOnly ? 1 : 0, "|", steeredOnly ? 1 : 0, "|", orgKind, "|",
			worldOnly && selected.Kind == CivPickKind::World ? selected.Name : cs::String());
	}

	void SocietyScreen::Rebuild(Galaxy& galaxy, Civilization* civ, double jd)
	{
		// Built afresh when what is shown changes, else twice a second (the galaxy moves on under it).
		cs::String key = RowsKey();
		double now = FPlatformTime::Seconds();
		if (key == rowsKey && now - rowsAt < 0.5) return;
		if (key != rowsKey) listScroll = Vector2::zero();
		rowsKey = key;
		rowsAt = now;
		rows.clear();
		if (civ == nullptr) return;
		auto society = Society::Find(galaxy);

		if (page == Feed)
		{
			cs::String world = worldOnly && selected.Kind == CivPickKind::World ? selected.Name : cs::String();
			for (int i = civ->Events.Count() - 1; i >= 0; i--)
			{
				const auto& e = civ->Events[i];
				if (e == nullptr) continue;
				int k = static_cast<int>(e->Kind);
				if (kind != 0 && k != kind) continue;
				if (kind == 0 && group >= 0 && GroupOf(k) != group) continue;
				if (steeredOnly && !e->Steered) continue;
				if (!world.empty() && (e->Where == nullptr || e->Where->Name != world)) continue;
				cs::String head = cs::Cat(SocietyDate(e->Jd), "  \xC2\xB7  ", Core::ToString(e->Kind), e->Where != nullptr ? cs::Cat("  \xC2\xB7  ", e->Where->Name) : cs::String(),
					e->Steered ? cs::String("  \xC2\xB7  STEERED") : cs::String(), e->Unverified ? cs::String("  \xC2\xB7  unverified") : cs::String());
				rows.push_back(Row{ head, cs::String(), CivPick(), e });
			}
			return;
		}
		if (page == Houses && society != nullptr)
		{
			for (const auto& h : society->Houses)
			{
				if (h == nullptr || h->Scattered) continue;
				rows.push_back(Row{ cs::Cat("House ", h->Surname), cs::Cat(h->Home != nullptr ? h->Home->Name : cs::String("?"), "  \xC2\xB7  ", h->Living(), " living  \xC2\xB7  ",
					Narrator::Money(h->Wealth), h->Business != nullptr ? cs::Cat("  \xC2\xB7  ", h->Business->Name) : cs::String()), CivPick::House(h->Id), nullptr });
			}
			return;
		}
		if (page == People && society != nullptr)
		{
			auto adults = Civilization::Adults(*society, jd);
			std::vector<cs::Ref<Citizen>> people(adults.begin(), adults.end());
			// The best and worst known first: renown either way.
			std::stable_sort(people.begin(), people.end(), [](const cs::Ref<Citizen>& a, const cs::Ref<Citizen>& b) { return std::abs(a->Reputation) > std::abs(b->Reputation); });
			if (people.size() > 400) people.resize(400);
			for (const auto& c : people)
				rows.push_back(Row{ c->FullName(), cs::Cat(static_cast<int>(c->AgeYears(jd)), ", ", Core::ToString(c->Role), c->House != nullptr && c->House->Home != nullptr ? cs::Cat(" of ", c->House->Home->Name) : cs::String(),
					"  \xC2\xB7  reputation ", SocietySigned(c->Reputation)), CivPick::Person(c->Id), nullptr });
			return;
		}
		if (page == Orgs)
		{
			for (const auto& o : civ->Orgs)
			{
				if (o == nullptr || !o->Active()) continue;
				if (orgKind >= 0 && static_cast<int>(o->Kind) != orgKind) continue;
				rows.push_back(Row{ cs::Cat(Narrator::OrgName(*o), "   ", Core::ToString(o->Kind)),
					cs::Cat(o->Seat != nullptr ? o->Seat->Name : cs::String("?"), "  \xC2\xB7  strength ", SocietyPct(o->Strength), "  \xC2\xB7  ", Narrator::Money(o->Funds), "  \xC2\xB7  ", o->Members.Count(), " members"),
					CivPick::Org(o->Id), nullptr });
			}
			return;
		}
		if (page == Worlds)
		{
			for (const auto& c : civ->Worlds)
			{
				if (c == nullptr || c->Where == nullptr) continue;
				rows.push_back(Row{ cs::Cat(c->Where->Name, c->Where->Owner != nullptr ? cs::Cat("   ", c->Where->Owner->Name) : cs::String(), c->Quarantined(jd) ? cs::String("   QUARANTINED") : cs::String()),
					cs::Cat("approval ", SocietySigned(c->Approval), "  \xC2\xB7  crime ", SocietyPct(c->Crime), "  \xC2\xB7  sick ", SocietyPct(c->Infected), "  \xC2\xB7  weary ", SocietyPct(c->Weariness),
						c->Faith != nullptr ? cs::Cat("  \xC2\xB7  ", Narrator::OrgName(*c->Faith)) : cs::String()),
					CivPick::World(c->Where->Name), nullptr });
			}
			return;
		}
		int view = page - FirstView;
		const auto& views = CivHands::Views();
		if (view >= 0 && view < static_cast<int>(views.size()))
		{
			cs::List<CivRow> lines;
			CivHands::View(views[view], galaxy, *civ, jd, lines);
			for (const auto& l : lines) rows.push_back(Row{ l.Text, cs::String(), l.Pick, nullptr });
		}
	}

	void SocietyScreen::Select(const Row& r)
	{
		if (r.Event != nullptr)
		{
			selectedEvent = r.Event->Id;
			selected = CivPick();
		}
		else if (!r.Pick.IsNone())
		{
			selected = r.Pick;
			selectedEvent = 0;
		}
		detailScroll = Vector2::zero();
		result.clear();
	}

	void SocietyScreen::HandleKeys()
	{
		Event& e = Event::current();
		if (e.type != EventType::KeyDown || rows.empty()) return;
		int at = -1;
		for (int i = 0; i < static_cast<int>(rows.size()); i++)
			if ((rows[i].Event != nullptr && rows[i].Event->Id == selectedEvent) || (rows[i].Event == nullptr && !rows[i].Pick.IsNone() && rows[i].Pick == selected)) { at = i; break; }
		int n = static_cast<int>(rows.size());
		if (e.keyCode == KeyCode::DownArrow) { Select(rows[at < 0 ? 0 : Mathf::Min(at + 1, n - 1)]); e.Use(); }
		else if (e.keyCode == KeyCode::UpArrow) { Select(rows[at < 0 ? 0 : Mathf::Max(at - 1, 0)]); e.Use(); }
	}

	void SocietyScreen::Ask(const cs::String& line)
	{
		bool sent = false;
		cs::String done;
		cs::String why = app->CivHandAsk(line, sent, done);
		resultBad = !why.empty();
		result = !why.empty() ? why : sent ? cs::Cat("Sent to the party (", line, "): every peer lays the same hand on the same day") : done;
		rowsAt = -1; // the feed shows it at once
	}

	// ---------------------------------------------------------------- drawing

	bool SocietyScreen::Draw(const cs::String& closeHint)
	{
		EnsureStyles();
		UiMotion::Scope motion("society", 0); // US-T255: the whole screen fades in
		auto screen = Rect(0, 0, static_cast<float>(Screen::width), static_cast<float>(Screen::height));
		GUI::Box(screen, GUIContent::none(), panel);
		GUI::Label(Rect(Margin, 22, screen.width - 2 * Margin, 30), "SOCIETY", title);
		float by = screen.height - 50;
		if (app->Galaxy == nullptr || app->Clock == nullptr) return GUI::Button(Rect(screen.width - Margin - 120, by, 120, 32), "Close", button);
		auto& galaxy = *app->Galaxy;
		double jd = app->Clock->Jd;
		auto civ = Civilization::Find(galaxy);

		cs::String tallies = civ == nullptr ? cs::String("The galaxy's people have not woken yet")
			: cs::Cat(civ->Worlds.Count(), " worlds watched  \xC2\xB7  ", civ->OrgsOf(OrgKind::Party).Count() + civ->OrgsOf(OrgKind::Faith).Count(), " parties and faiths  \xC2\xB7  ",
				civ->Events.Count(), " events kept  \xC2\xB7  Prose narration ", Narrator::HasSource() ? "on" : "off");
		GUI::Label(Rect(Margin, 56, screen.width - 2 * Margin, 18),
			cs::Cat(tallies, "  \xC2\xB7  Up / Down to pick  \xC2\xB7  ", closeHint), small);

		Rebuild(galaxy, civ.get(), jd);
		HandleKeys();

		float top = 88, bottom = screen.height - 64;
		float pagesW = Mathf::Clamp(screen.width * 0.13f, 160.0f, 210.0f);
		DrawPages(Rect(Margin, top, pagesW, bottom - top));
		float listX = Margin + pagesW + Gutter;
		float listW = Mathf::Clamp(screen.width * 0.36f, 300.0f, 640.0f);
		if (civ == nullptr)
		{
			GUI::Label(Rect(listX, top, screen.width - Margin - listX, 80),
				"The houses, their people and the organisations they found come to life once the game has run a day: let time pass and look again.", text);
			return GUI::Button(Rect(screen.width - Margin - 120, by, 120, 32), "Close", button);
		}
		float listTop = top;
		if (page == Feed || page == Orgs)
		{
			DrawFilters(Rect(listX, top, listW, 26), *civ);
			listTop += 32;
		}
		DrawList(Rect(listX, listTop, listW, bottom - listTop), civ.get());
		float x = listX + listW + Gutter;
		DrawDetail(Rect(x, top, screen.width - Margin - x, bottom - top), galaxy, *civ, jd);

		if (!result.empty())
			GUI::Label(Rect(x, by + 6, screen.width - Margin - 132 - x, 22), result, resultBad ? bad : good);
		return GUI::Button(Rect(screen.width - Margin - 120, by, 120, 32), "Close", button);
	}

	void SocietyScreen::DrawPages(Rect area)
	{
		const auto& views = CivHands::Views();
		int count = FirstView + static_cast<int>(views.size());
		float contentH = HeadingH + count * TabRowH + HeadingH;
		pageScroll = GUI::BeginScrollView(area, pageScroll, Rect(0, 0, area.width - 16, contentH));
		float w = area.width - 16, y = 0;
		GUI::Label(Rect(0, y, w, HeadingH - 4), "THE PEOPLE", heading);
		y += HeadingH;
		for (int p = 0; p < count; p++)
		{
			if (p == FirstView)
			{
				GUI::Label(Rect(0, y + 4, w, HeadingH - 4), "THE MODULES", heading);
				y += HeadingH;
			}
			cs::String name = p < FirstView ? cs::String(SocietyPageNames[p]) : cs::String(SocietyViewName(views[p - FirstView]));
			if (GUI::Button(Rect(0, y, w, TabRowH - 4), name, p == page ? entrySelected : entry) && p != page)
			{
				page = p;
				result.clear();
			}
			y += TabRowH;
		}
		GUI::EndScrollView();
	}

	void SocietyScreen::DrawFilters(Rect r, Civilization& civ)
	{
		float x = r.x;
		if (page == Orgs)
		{
			// The kinds there are (a kind a later module adds shows too).
			std::vector<int> kinds;
			for (const auto& o : civ.Orgs)
				if (o != nullptr && o->Active() && std::find(kinds.begin(), kinds.end(), static_cast<int>(o->Kind)) == kinds.end()) kinds.push_back(static_cast<int>(o->Kind));
			std::sort(kinds.begin(), kinds.end());
			auto at = std::find(kinds.begin(), kinds.end(), orgKind);
			int i = at == kinds.end() ? -1 : static_cast<int>(at - kinds.begin());
			int n = static_cast<int>(kinds.size());
			if (GUI::Button(Rect(x, r.y, 26, r.height), "<", button)) orgKind = i <= 0 ? (i == 0 ? -1 : (n > 0 ? kinds[n - 1] : -1)) : kinds[i - 1];
			GUI::Label(Rect(x + 30, r.y + 3, 200, r.height), cs::Cat("Kind: ", orgKind < 0 ? cs::String("all") : cs::String(Core::ToString(static_cast<OrgKind>(orgKind)))), text);
			if (GUI::Button(Rect(x + 234, r.y, 26, r.height), ">", button)) orgKind = i + 1 < n ? kinds[i + 1] : -1;
			return;
		}
		// The module group: All, each group, Other.
		if (GUI::Button(Rect(x, r.y, 22, r.height), "<", button)) { group = group <= -1 ? GroupCount() - 1 : group - 1; kind = 0; }
		GUI::Label(Rect(x + 26, r.y + 3, 100, r.height), group < 0 ? cs::String("All modules") : cs::String(GroupName(group)), text);
		if (GUI::Button(Rect(x + 128, r.y, 22, r.height), ">", button)) { group = group + 1 >= GroupCount() ? -1 : group + 1; kind = 0; }
		x += 158;
		// The kind: every SocialKind that has happened in the group picked, in the enum's order (ToString names it).
		std::vector<int> kinds;
		for (int k = 1; k < static_cast<int>(SocialKind::Count_); k++)
			if (civ.Count(static_cast<SocialKind>(k)) > 0 && (group < 0 || GroupOf(k) == group)) kinds.push_back(k);
		auto at = std::find(kinds.begin(), kinds.end(), kind);
		int i = at == kinds.end() ? -1 : static_cast<int>(at - kinds.begin());
		int n = static_cast<int>(kinds.size());
		if (GUI::Button(Rect(x, r.y, 22, r.height), "<", button)) kind = i < 0 ? (n > 0 ? kinds[n - 1] : 0) : i == 0 ? 0 : kinds[i - 1];
		cs::String kindName = kind == 0 ? cs::String("All kinds") : cs::Cat(Core::ToString(static_cast<SocialKind>(kind)), " (", civ.Count(static_cast<SocialKind>(kind)), ")");
		GUI::Label(Rect(x + 26, r.y + 3, 150, r.height), kindName, text);
		if (GUI::Button(Rect(x + 178, r.y, 22, r.height), ">", button)) kind = i + 1 < n ? kinds[i + 1] : 0;
		x += 208;
		float rest = r.xMax() - x;
		float half = Mathf::Max(60.0f, (rest - 6) / 2);
		if (GUI::Button(Rect(x, r.y, half, r.height), steeredOnly ? "Steered only" : "All doings", steeredOnly ? entrySelected : entry)) steeredOnly = !steeredOnly;
		bool canWorld = selected.Kind == CivPickKind::World;
		bool enabled = GUI::enabled;
		GUI::enabled = enabled && (canWorld || worldOnly);
		if (GUI::Button(Rect(x + half + 6, r.y, half, r.height), worldOnly && canWorld ? cs::Cat("At ", selected.Name) : cs::String("Every world"), worldOnly && canWorld ? entrySelected : entry))
			worldOnly = !worldOnly;
		GUI::enabled = enabled;
	}

	void SocietyScreen::DrawList(Rect area, Civilization* civ)
	{
		(void)civ;
		cs::String head = page < FirstView ? cs::Strings::ToUpperInvariant(cs::String(SocietyPageNames[page])) : cs::Strings::ToUpperInvariant(cs::String(SocietyViewName(CivHands::Views()[page - FirstView])));
		GUI::Label(Rect(area.x, area.y, area.width, HeadingH - 4), cs::Cat(head, "  (", static_cast<int>(rows.size()), ")"), heading);
		GUI::DrawTexture(Rect(area.x, area.y + HeadingH - 5, area.width, 1), buttonActiveTexture);
		auto list = Rect(area.x, area.y + HeadingH, area.width, area.height - HeadingH);
		int n = static_cast<int>(rows.size());
		if (n == 0)
		{
			GUI::Label(Rect(list.x, list.y + 4, list.width, 80), page == Feed ? "Nothing of that kind has happened yet." : "Nobody here yet: that part of the civilization wakes as the days pass.", text);
			return;
		}
		float w = list.width - 16;
		listScroll = GUI::BeginScrollView(list, listScroll, Rect(0, 0, w, n * RowH));
		// Only the rows in view are drawn (and only their events worded): the feed keeps hundreds.
		int first = Mathf::Max(0, static_cast<int>(listScroll.y / RowH) - 1);
		int last = Mathf::Min(n - 1, static_cast<int>((listScroll.y + list.height) / RowH) + 1);
		for (int i = first; i <= last; i++)
		{
			auto& r = rows[i];
			bool on = r.Event != nullptr ? r.Event->Id == selectedEvent : (!r.Pick.IsNone() && r.Pick == selected);
			auto row = Rect(0, i * RowH, w, RowH - 4);
			if (GUI::Button(row, GUIContent::none(), on ? entrySelected : entry)) Select(r);
			if (r.Event != nullptr && r.Line.empty()) r.Line = Narrator::Render(*r.Event);
			if (r.Line.empty())
				GUI::Label(Rect(row.x + 8, row.y + 2, row.width - 14, row.height - 4), r.Title, small);
			else
			{
				GUI::Label(Rect(row.x + 8, row.y + 3, row.width - 14, 20), r.Event != nullptr ? r.Line : r.Title, rowTitle);
				GUI::Label(Rect(row.x + 8, row.y + 22, row.width - 14, 18), r.Event != nullptr ? r.Title : r.Line, rowLine);
			}
		}
		GUI::EndScrollView();
	}

	cs::String SocietyScreen::NameOf(const CivPick& p, Galaxy& galaxy, Civilization& civ) const
	{
		auto society = Society::Find(galaxy);
		switch (p.Kind)
		{
		case CivPickKind::House: { auto h = society != nullptr ? society->HouseById(p.Id) : nullptr; return h != nullptr ? cs::Cat("House ", h->Surname) : cs::Cat("house ", p.Id); }
		case CivPickKind::Person: { auto c = society != nullptr ? society->CitizenById(p.Id) : nullptr; return c != nullptr ? c->FullName() : cs::Cat("person ", p.Id); }
		case CivPickKind::Business: { auto b = society != nullptr ? society->BusinessById(p.Id) : nullptr; return b != nullptr ? b->Name : cs::Cat("business ", p.Id); }
		case CivPickKind::Org: { auto o = civ.OrgById(p.Id); return o != nullptr ? Narrator::OrgName(*o) : cs::Cat("organisation ", p.Id); }
		case CivPickKind::Trial: return cs::Cat("trial ", p.Id);
		case CivPickKind::World: case CivPickKind::Faction: return p.Name;
		default: return cs::String();
		}
	}

	void SocietyScreen::DescribeEvent(const SocialEvent& e, cs::String& name, cs::List<cs::String>& lines, std::vector<std::pair<cs::String, CivPick>>& links)
	{
		name = Core::ToString(e.Kind);
		lines.Add(Narrator::Render(e));
		lines.Add(cs::Cat(SocietyDate(e.Jd), e.Steered ? "  \xC2\xB7  brought about by the SpaceGod's hand" : "", e.Unverified ? "  \xC2\xB7  told, not true" : "",
			e.RunBy != nullptr ? cs::Cat("  \xC2\xB7  run ", e.Slant > 0 ? "for" : "against", " by ", Narrator::OrgName(*e.RunBy)) : cs::String()));
		if (Narrator::HasSource()) lines.Add(cs::Cat("The built-in words: ", Narrator::Render(e, false)));
		auto person = [&](const cs::Ref<Citizen>& c) { if (c != nullptr) links.emplace_back(c->FullName(), CivPick::Person(c->Id)); };
		auto house = [&](const cs::Ref<Household>& h) { if (h != nullptr) links.emplace_back(cs::Cat("House ", h->Surname), CivPick::House(h->Id)); };
		auto org = [&](const cs::Ref<Org>& o) { if (o != nullptr) links.emplace_back(Narrator::OrgName(*o), CivPick::Org(o->Id)); };
		person(e.Who);
		person(e.Whom);
		house(e.House);
		house(e.OtherHouse);
		if (e.Business != nullptr) links.emplace_back(e.Business->Name, CivPick::Business(e.Business->Id));
		org(e.Org);
		org(e.OtherOrg);
		if (e.Where != nullptr) links.emplace_back(e.Where->Name, CivPick::World(e.Where->Name));
		if (e.Faction != nullptr) links.emplace_back(e.Faction->Name, CivPick::Faction(e.Faction->Name));
		if (e.Rival != nullptr) links.emplace_back(e.Rival->Name, CivPick::Faction(e.Rival->Name));
	}

	void SocietyScreen::Describe(const CivPick& p, Galaxy& galaxy, Civilization& civ, double jd, cs::String& name, cs::List<cs::String>& lines,
		std::vector<std::pair<cs::String, CivPick>>& links, std::vector<Hand>& hands)
	{
		auto society = Society::Find(galaxy);
		name = NameOf(p, galaxy, civ);
		cs::String arg = p.Arg();
		// A hand that takes a second thing: offered with the one marked, else a hint to mark one.
		auto pair = [&](CivPickKind want, const char* verb, const char* label, const char* hint, bool markFirst = false)
		{
			if (marked.Kind == want && marked != p)
				hands.push_back(Hand{ cs::Cat(label, " ", NameOf(marked, galaxy, civ)), markFirst ? cs::Cat(verb, " ", marked.Arg(), " ", arg) : cs::Cat(verb, " ", arg, " ", marked.Arg()) });
			else hands.push_back(Hand{ cs::Cat(label, "...  (", hint, ")"), cs::String() });
		};
		auto one = [&](const char* verb, const char* label) { hands.push_back(Hand{ label, cs::Cat(verb, " ", arg) }); };

		switch (p.Kind)
		{
		case CivPickKind::House:
		{
			auto h = society != nullptr ? society->HouseById(p.Id) : nullptr;
			if (h == nullptr) break;
			lines.Add(cs::Cat("Of ", h->Home != nullptr ? h->Home->Name : cs::String("nowhere"), ", founded ", SocietyDate(h->FoundedJd), h->Scattered ? ", scattered" : ""));
			lines.Add(cs::Cat(h->Living(), " living  \xC2\xB7  wealth ", Narrator::Money(h->Wealth)));
			if (h->Business != nullptr)
			{
				lines.Add(Society::Describe(h->Business));
				links.emplace_back(h->Business->Name, CivPick::Business(h->Business->Id));
			}
			if (civ.Kinship != nullptr)
				if (auto f = civ.Kinship->FeudOf(h); f != nullptr && f->Active())
				{
					auto other = f->A == h ? f->B : f->A;
					lines.Add(cs::Cat("At feud with House ", other->Surname, " over ", f->Cause, ", heat ", cs::Fmt(f->Heat, "0.00")));
					links.emplace_back(cs::Cat("House ", other->Surname), CivPick::House(other->Id));
				}
			if (h->Home != nullptr) links.emplace_back(h->Home->Name, CivPick::World(h->Home->Name));
			for (const auto& m : h->Members)
				if (m != nullptr && m->Alive()) links.emplace_back(m->FullName(), CivPick::Person(m->Id));
			one("donate", "Donate (a great gift)");
			pair(CivPickKind::House, "feud", "Swear a feud with", "mark another house");
			pair(CivPickKind::House, "reconcile", "Reconcile with", "mark another house");
			pair(CivPickKind::Business, "takeover", "Take over", "mark a business");
			break;
		}
		case CivPickKind::Person:
		{
			auto c = society != nullptr ? society->CitizenById(p.Id) : nullptr;
			if (c == nullptr) break;
			cs::String traits;
			for (int bit = 1; bit <= (1 << 11); bit <<= 1)
				if (c->Traits & bit) traits = cs::Cat(traits, traits.empty() ? "" : ", ", Core::ToString(static_cast<Trait>(bit)));
			lines.Add(cs::Cat(c->Alive() ? cs::Cat(static_cast<int>(c->AgeYears(jd)), " years old") : cs::Cat("died ", SocietyDate(c->DiedJd)), ", ", Core::ToString(c->Role)));
			lines.Add(cs::Cat("Traits: ", traits.empty() ? cs::String("none yet") : traits, "  \xC2\xB7  wants ", Core::ToString(c->Goal)));
			lines.Add(cs::Cat("Reputation ", SocietySigned(c->Reputation), "  \xC2\xB7  skill ", cs::Fmt(c->Skill, "0.00"), c->Faith != nullptr ? cs::Cat("  \xC2\xB7  keeps ", Narrator::OrgName(*c->Faith)) : cs::String()));
			for (const auto& t : c->Ties)
				if (t != nullptr && t->Other != nullptr)
					lines.Add(cs::Cat("  ", Core::ToString(t->Kind), " of ", t->Other->FullName(), ", ", cs::Fmt(t->Strength, "0.00"), t->Other->Alive() ? "" : ", dead"));
			if (c->House != nullptr) links.emplace_back(cs::Cat("House ", c->House->Surname), CivPick::House(c->House->Id));
			if (c->Spouse != nullptr) links.emplace_back(c->Spouse->FullName(), CivPick::Person(c->Spouse->Id));
			for (const auto& t : c->Ties)
				if (t != nullptr && t->Other != nullptr) links.emplace_back(t->Other->FullName(), CivPick::Person(t->Other->Id));
			for (const auto& o : c->Orgs)
				if (o != nullptr && o->Active()) links.emplace_back(Narrator::OrgName(*o), CivPick::Org(o->Id));
			if (c->Faith != nullptr) links.emplace_back(Narrator::OrgName(*c->Faith), CivPick::Org(c->Faith->Id));
			one("fame", "Make them famous");
			one("coup", "Back their coup");
			one("hit", "Put a price on their head");
			one("preach", "Have them preach a new faith");
			one("rumour", "Spread a rumour about them");
			pair(CivPickKind::Person, "seduce", "Begin an affair with", "mark another person");
			break;
		}
		case CivPickKind::Org:
		{
			auto o = civ.OrgById(p.Id);
			if (o == nullptr) break;
			lines.Add(cs::Cat(Core::ToString(o->Kind), o->Active() ? cs::Cat(", founded ", SocietyDate(o->FoundedJd)) : cs::Cat(", ended ", SocietyDate(o->EndedJd))));
			lines.Add(cs::Cat("Strength ", SocietyPct(o->Strength), "  \xC2\xB7  funds ", Narrator::Money(o->Funds), "  \xC2\xB7  ", o->Members.Count(), " members  \xC2\xB7  stance ", SocietySigned(o->Stance),
				o->Score != 0 ? cs::Cat("  \xC2\xB7  score ", o->Score) : cs::String()));
			if (o->Seat != nullptr) links.emplace_back(o->Seat->Name, CivPick::World(o->Seat->Name));
			if (o->Leader != nullptr) links.emplace_back(cs::Cat("Led by ", o->Leader->FullName()), CivPick::Person(o->Leader->Id));
			if (o->Owner != nullptr) links.emplace_back(cs::Cat("House ", o->Owner->Surname), CivPick::House(o->Owner->Id));
			if (o->Faction != nullptr) links.emplace_back(o->Faction->Name, CivPick::Faction(o->Faction->Name));
			if (o->Rival != nullptr) links.emplace_back(cs::Cat("Rival: ", Narrator::OrgName(*o->Rival)), CivPick::Org(o->Rival->Id));
			if (o->Parent != nullptr) links.emplace_back(cs::Cat("Split from ", Narrator::OrgName(*o->Parent)), CivPick::Org(o->Parent->Id));
			switch (o->Kind)
			{
			case OrgKind::Party: one("elect", "Swing the next election to them"); break;
			case OrgKind::Bank: one("bankrun", "Start a run on the bank"); break;
			case OrgKind::Team: one("rig", "Rig their next match"); break;
			case OrgKind::Mercenaries: pair(CivPickKind::Faction, "hire", "Hire them out to", "mark a state"); break;
			case OrgKind::Outlet:
				one("investigate", "Set the reporters digging");
				hands.push_back(Hand{ "Slant it for its flag", cs::Cat("slant ", arg, " for") });
				hands.push_back(Hand{ "Slant it against its flag", cs::Cat("slant ", arg, " against") });
				hands.push_back(Hand{ "Have it run straight", cs::Cat("slant ", arg, " straight") });
				break;
			default: break;
			}
			break;
		}
		case CivPickKind::World:
		{
			cs::Ref<Civics> c;
			for (const auto& w : civ.Worlds)
				if (w != nullptr && w->Where != nullptr && w->Where->Name == p.Name) { c = w; break; }
			if (c == nullptr) break;
			auto s = c->Where;
			lines.Add(cs::Cat("Approval of its flag ", SocietySigned(c->Approval), "  \xC2\xB7  unrest ", SocietyPct(s->Unrest)));
			lines.Add(cs::Cat("Politics: hawk ", SocietySigned(c->Hawk), ", liberty ", SocietySigned(c->Liberty), "  \xC2\xB7  trust in the press ", SocietyPct(c->PressTrust)));
			lines.Add(cs::Cat("Crime ", SocietyPct(c->Crime), "  \xC2\xB7  sick ", SocietyPct(c->Infected), ", immune ", SocietyPct(c->Immune), "  \xC2\xB7  war weariness ", SocietyPct(c->Weariness),
				c->WarDays > 0 ? cs::Cat(" (", static_cast<int>(c->WarDays), " days at war)") : cs::String()));
			lines.Add(cs::Cat("Hospitals ", SocietyPct(c->Care), ", schools ", SocietyPct(c->Learning), "  \xC2\xB7  drift ", SocietyPct(c->Drift)));
			lines.Add(cs::Cat("Fashion: ", Culture::TrendName(c->Trend), " (", SocietyPct(c->TrendHeat), " hot)",
				c->Faith != nullptr ? cs::Cat("  \xC2\xB7  faith: ", Narrator::OrgName(*c->Faith), " ", SocietyPct(c->FaithShare)) : cs::String()));
			if (c->Quarantined(jd)) lines.Add(cs::Cat("QUARANTINED until ", SocietyDate(c->QuarantineUntil)));
			if (!c->Landmark.empty()) lines.Add(cs::Cat("Landmark: ", c->Landmark));
			if (!c->Monument.empty()) lines.Add(cs::Cat("Monument: ", c->Monument));
			if (s->Owner != nullptr) links.emplace_back(s->Owner->Name, CivPick::Faction(s->Owner->Name));
			if (c->Faith != nullptr) links.emplace_back(Narrator::OrgName(*c->Faith), CivPick::Org(c->Faith->Id));
			if (society != nullptr)
				for (const auto& h : society->HousesOf(s))
					if (h != nullptr) links.emplace_back(cs::Cat("House ", h->Surname), CivPick::House(h->Id));
			for (const auto& o : civ.Orgs)
				if (o != nullptr && o->Active() && o->Seat == s) links.emplace_back(Narrator::OrgName(*o), CivPick::Org(o->Id));
			one("riot", "Stir a riot");
			one("arm", "Arm the rebels");
			one("racket", "Let a syndicate take the streets");
			one("crackdown", "Send the police in");
			one("festival", "Throw a festival");
			one("landmark", "Reveal a landmark");
			one("plague", "Loose a plague");
			one("cure", "Cure its sickness");
			one("disaster", "Strike it with disaster");
			pair(CivPickKind::Faction, "aid", "Have relief sent by", "mark a state", true);
			break;
		}
		case CivPickKind::Trial:
		{
			auto t = civ.Politics != nullptr ? civ.Politics->TrialById(p.Id) : nullptr;
			if (t == nullptr || t->Accused == nullptr) break;
			name = cs::Cat("The trial of ", t->Accused->FullName());
			lines.Add(cs::Cat("For ", Core::ToString(t->Charge), t->Where != nullptr ? cs::Cat(", at ", t->Where->Name) : cs::String()));
			lines.Add(t->Decided ? (t->Bought ? cs::String("Acquitted (the judge was bought)") : t->Guilty ? cs::String("Found guilty") : cs::String("Acquitted")) : cs::String("Pending"));
			links.emplace_back(t->Accused->FullName(), CivPick::Person(t->Accused->Id));
			if (t->Where != nullptr) links.emplace_back(t->Where->Name, CivPick::World(t->Where->Name));
			one("acquit", "Have the court acquit");
			break;
		}
		case CivPickKind::Business:
		{
			auto b = society != nullptr ? society->BusinessById(p.Id) : nullptr;
			if (b == nullptr) break;
			lines.Add(Society::Describe(b));
			if (b->Owner != nullptr) links.emplace_back(cs::Cat("House ", b->Owner->Surname), CivPick::House(b->Owner->Id));
			if (b->Home != nullptr) links.emplace_back(b->Home->Name, CivPick::World(b->Home->Name));
			if (marked.Kind == CivPickKind::House)
				hands.push_back(Hand{ cs::Cat("Have it taken over by ", NameOf(marked, galaxy, civ)), cs::Cat("takeover ", marked.Arg(), " ", arg) });
			else hands.push_back(Hand{ "Have it taken over by...  (mark a house)", cs::String() });
			break;
		}
		case CivPickKind::Faction:
		{
			cs::Ref<Faction> f = galaxy.Polities != nullptr ? galaxy.Polities->Find(p.Name) : nullptr;
			if (f == nullptr) break;
			if (civ.Politics != nullptr)
				for (const auto& r : civ.Politics->Regimes)
					if (r != nullptr && r->Faction == f)
					{
						lines.Add(cs::Cat("Ruled by ", r->Ruler != nullptr ? r->Ruler->FullName() : cs::String("nobody (crisis)"), r->Ruling != nullptr ? cs::Cat(" of ", Narrator::OrgName(*r->Ruling)) : cs::String()));
						lines.Add(cs::Cat("Hawk ", SocietySigned(r->Hawk), ", liberty ", SocietySigned(r->Liberty), ", approval ", SocietySigned(r->Approval), ", ", r->Coups, " coups",
							cs::Double::IsNaN(r->NextElectionJd) ? cs::String(", no elections") : cs::Cat(", votes in ", static_cast<int>(r->NextElectionJd - jd), " days")));
						if (r->Ruler != nullptr) links.emplace_back(r->Ruler->FullName(), CivPick::Person(r->Ruler->Id));
						if (r->Ruling != nullptr) links.emplace_back(Narrator::OrgName(*r->Ruling), CivPick::Org(r->Ruling->Id));
					}
			if (civ.Strife != nullptr)
				if (auto a = civ.Strife->AdmiralOf(f); a != nullptr && a->Who != nullptr) links.emplace_back(cs::Cat("Admiral ", a->Who->FullName()), CivPick::Person(a->Who->Id));
			for (const auto& w : civ.Worlds)
				if (w != nullptr && w->Where != nullptr && w->Where->Owner == f) links.emplace_back(w->Where->Name, CivPick::World(w->Where->Name));
			pair(CivPickKind::Faction, "summit", "Call a peace summit with", "mark another state");
			pair(CivPickKind::Faction, "spy", "Set its spies on", "mark another state");
			if (marked.Kind == CivPickKind::Org) hands.push_back(Hand{ cs::Cat("Hire ", NameOf(marked, galaxy, civ), " for it"), cs::Cat("hire ", marked.Arg(), " ", arg) });
			if (marked.Kind == CivPickKind::World) hands.push_back(Hand{ cs::Cat("Have it send relief to ", marked.Name), cs::Cat("aid ", arg, " ", marked.Arg()) });
			break;
		}
		default: break;
		}
	}

	void SocietyScreen::DrawDetail(Rect area, Galaxy& galaxy, Civilization& civ, double jd)
	{
		cs::String name;
		cs::List<cs::String> lines;
		std::vector<std::pair<cs::String, CivPick>> links;
		std::vector<Hand> hands;
		cs::Ref<SocialEvent> event;
		if (selectedEvent != 0)
			for (int i = civ.Events.Count() - 1; i >= 0; i--)
				if (civ.Events[i] != nullptr && civ.Events[i]->Id == selectedEvent) { event = civ.Events[i]; break; }
		if (event != nullptr) DescribeEvent(*event, name, lines, links);
		else if (!selected.IsNone()) Describe(selected, galaxy, civ, jd, name, lines, links, hands);
		if (name.empty())
		{
			GUI::Label(Rect(area.x, area.y, area.width, HeadingH - 4), "NOTHING PICKED", heading);
			GUI::Label(Rect(area.x, area.y + HeadingH, area.width, 120),
				"Pick an event, a house, a person, an organisation or a world on the left. What it is shows here, with the people and "
				"places about it to follow, and the SpaceGod's hand in it: the same verbs as the console's sg.Civ, so in co-op every "
				"player's galaxy feels it on the same day.", text);
			return;
		}

		float w = area.width - 16;
		float linkH = 0;
		{
			float x = 0;
			linkH = links.empty() ? 0.0f : 28.0f;
			for (const auto& l : links)
			{
				float bw = Mathf::Min(w, link->CalcSize(GUIContent(l.first)).x + 16);
				if (x > 0 && x + bw > w) { x = 0; linkH += 28; }
				x += bw + 6;
			}
		}
		float textH = 0;
		for (const auto& l : lines) textH += text->CalcHeight(GUIContent(l), w) + 2;
		float contentH = 30 + 22 + 12 + textH + SectionGap + (links.empty() ? 0.0f : HeadingH + linkH + SectionGap)
			+ (selected.IsNone() || event != nullptr ? 0.0f : HeadingH + 30 + static_cast<float>(hands.size()) * 30.0f + SectionGap);
		detailScroll = GUI::BeginScrollView(area, detailScroll, Rect(0, 0, w, contentH));
		float y = 0;
		GUI::Label(Rect(0, y, w, 30), cs::Strings::ToUpperInvariant(name), entryTitle);
		y += 30;
		static const char* const KindNames[] = { "", "House", "Person", "Organisation", "World", "Trial", "Business", "State" };
		GUI::Label(Rect(0, y, w, 20), event != nullptr ? cs::String("Event") : cs::String(KindNames[static_cast<int>(selected.Kind)]), small);
		y += 22;
		GUI::DrawTexture(Rect(0, y + 4, w, 1), buttonActiveTexture);
		y += 12;
		for (const auto& l : lines)
		{
			float h = text->CalcHeight(GUIContent(l), w);
			GUI::Label(Rect(0, y, w, h), l, text);
			y += h + 2;
		}
		y += SectionGap;
		CivPick follow;
		if (!links.empty())
		{
			GUI::Label(Rect(0, y, w, HeadingH - 4), "SEE ALSO", heading);
			y += HeadingH;
			float x = 0;
			for (const auto& l : links)
			{
				float bw = Mathf::Min(w, link->CalcSize(GUIContent(l.first)).x + 16);
				if (x > 0 && x + bw > w) { x = 0; y += 28; }
				if (GUI::Button(Rect(x, y, bw, 24), l.first, link)) follow = l.second;
				x += bw + 6;
			}
			y += 28 + SectionGap;
		}
		if (event == nullptr && !selected.IsNone())
		{
			GUI::Label(Rect(0, y, w, HeadingH - 4), "THE SPACEGOD'S HAND", heading);
			GUI::DrawTexture(Rect(0, y + HeadingH - 5, w, 1), buttonActiveTexture);
			y += HeadingH;
			// The mark: the second thing a two-sided hand (a feud, an affair, a summit) takes.
			bool isMarked = marked == selected;
			if (GUI::Button(Rect(0, y, 200, 24), isMarked ? "Marked (click to clear)" : "Mark as the second", isMarked ? entrySelected : entry)) marked = isMarked ? CivPick() : selected;
			if (!marked.IsNone() && !isMarked) GUI::Label(Rect(208, y + 3, w - 208, 20), cs::Cat("Marked: ", NameOf(marked, galaxy, civ)), small);
			y += 30;
			bool enabled = GUI::enabled;
			for (const auto& h : hands)
			{
				GUI::enabled = enabled && !h.Line.empty();
				if (GUI::Button(Rect(0, y, Mathf::Min(w, 420.0f), 26), h.Label, button)) Ask(h.Line);
				if (!h.Line.empty()) GUI::Label(Rect(Mathf::Min(w, 420.0f) + 10, y + 4, Mathf::Max(0.0f, w - 430), 20), cs::Cat("sg.Civ ", h.Line), small);
				y += 30;
			}
			GUI::enabled = enabled;
			if (hands.empty()) GUI::Label(Rect(0, y, w, 20), "No hand of the SpaceGod's reaches this.", small);
		}
		GUI::EndScrollView();
		if (!follow.IsNone())
		{
			selected = follow;
			selectedEvent = 0;
			detailScroll = Vector2::zero();
			result.clear();
		}
	}

	void SocietyScreen::EnsureStyles()
	{
		if (stylesReady) return;
		stylesReady = true;
		// The Help screen's look (HelpScreen::EnsureStyles, UiStyle.h) at full-screen depth, as the Encyclopedia's.
		namespace S = UiStyle;
		namespace T = UiStyle::Type;
		panelTexture = S::PanelFrame(S::Depth::Screen);
		buttonTexture = S::ControlTexture();
		buttonActiveTexture = S::ControlOnTexture();
		panel = S::PanelStyle(panelTexture);
		title = S::TitleStyle(T::Screen);
		entryTitle = S::TitleStyle(T::Chapter);
		heading = S::TextStyle(T::Heading, S::Accent, FontStyle::Bold);
		text = S::TextStyle(T::Body);
		text->wordWrap = true;
		small = S::CaptionStyle(T::Body);
		small->fontStyle = FontStyle::Italic;
		small->wordWrap = true;
		button = S::ButtonStyle(T::Body, buttonTexture, buttonActiveTexture);
		link = S::ButtonStyle(T::Body, buttonTexture, buttonActiveTexture);
		entry = S::EntryStyle(T::Body, buttonTexture, buttonActiveTexture);
		entrySelected = S::EntrySelectedStyle(entry, buttonActiveTexture);
		rowTitle = S::TextStyle(T::Body, S::Heading, FontStyle::Bold);
		rowTitle->clipping = TextClipping::Clip;
		rowLine = S::CaptionStyle(T::Caption);
		rowLine->clipping = TextClipping::Clip;
		good = S::TextStyle(T::Body, S::Good);
		good->clipping = TextClipping::Clip;
		bad = S::TextStyle(T::Body, S::Warning);
		bad->clipping = TextClipping::Clip;
	}

	void SocietyScreen::DestroyOwnedTextures()
	{
		panelTexture = nullptr;
		buttonTexture = nullptr;
		buttonActiveTexture = nullptr;
		stylesReady = false;
	}
}
