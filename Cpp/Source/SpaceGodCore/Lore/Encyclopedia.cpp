// US-T271: the Encyclopedia (see Encyclopedia.h).

#include "Lore/Encyclopedia.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "ControlsHelp.h"
#include "Fleet.h"
#include "GameManual.h"
#include "Ships.h"
#include "World.h"
#include "Combat/Construction.h"
#include "Combat/ShipCatalog.h"
#include "Comms/Pilots.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Industry.h"
#include "Economy/Settlement.h"
#include "Law/Law.h"
#include "Lore/LoreBooks.h"
#include "Lore/LoreKeys.h"
#include "Lore/LoreLibrary.h"
#include "Persistence/ObjectGraph.h"
#include "Systems/DiscoveryLog.h"
#include "Systems/Galaxy.h"
#include "Systems/Polities.h"

#include <algorithm>

namespace SpaceGod::Core
{
	// Named, not anonymous: a unity build puts several files' helpers in one translation unit.
	namespace EncyclopediaDetail
	{
		char EncLower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

		std::string EncLow(const cs::String& s)
		{
			std::string r = s;
			for (auto& c : r) c = EncLower(c);
			return r;
		}

		bool EncAlnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || static_cast<unsigned char>(c) >= 0x80; }

		/// Whether the lowercase <paramref name="word"/> occurs in lowercase <paramref name="text"/> as a whole word.
		size_t EncFindWord(const std::string& text, const std::string& word)
		{
			if (word.empty()) return std::string::npos;
			for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1))
			{
				bool startOk = at == 0 || !EncAlnum(text[at - 1]);
				size_t end = at + word.size();
				bool endOk = end >= text.size() || !EncAlnum(text[end]);
				if (startOk && endOk) return at;
			}
			return std::string::npos;
		}

		/// "CAMERA AND GOD VIEW" -> "Camera and god view".
		cs::String EncSentence(const cs::String& upper)
		{
			cs::String r = cs::Strings::ToLowerInvariant(upper);
			if (!r.empty() && r[0] >= 'a' && r[0] <= 'z') r[0] = static_cast<char>(r[0] - 'a' + 'A');
			return r;
		}

		cs::String EncPlural(int n, const char* one, const char* many) { return cs::Cat(n, " ", n == 1 ? one : many); }

		cs::String EncTonnes(double t)
		{
			return t >= 1e6 ? cs::Cat(cs::Fmt(t / 1e6, "0.0"), " Mt") : t >= 1e3 ? cs::Cat(cs::Fmt(t / 1e3, "0.0"), " kt") : cs::Cat(cs::Fmt(t, "0"), " t");
		}

		/// The systems the player has been in: the log's star finds that say so (DiscoveryLog's SweepHere writes "visited").
		bool EncVisited(const DiscoveryLog* log, const cs::String& system)
		{
			if (log == nullptr) return false;
			auto star = log->Find(cs::Cat("star|", system, "|", system));
			return star != nullptr && star->Note.find("isited") != cs::String::npos;
		}

		cs::Ref<Settlement> EncFindSettlement(const Galaxy& galaxy, const cs::String& system, const cs::String& name)
		{
			auto sys = galaxy.Find(system);
			if (sys == nullptr || sys->World == nullptr || sys->World->Economy == nullptr) return nullptr;
			for (const auto& s : sys->World->Economy->Settlements)
				if (s != nullptr && s->Name == name) return s;
			return nullptr;
		}

		/// The goods a market holds most of, "Food 12.0 kt, Water 9.1 kt, Alloys 800 t".
		cs::String EncTopStock(const Settlement& s, int count)
		{
			std::vector<std::pair<double, int>> stock;
			for (int g = 0; g < GoodInfo::Count && g < s.Stock.Count(); g++)
				if (s.Stock[g] >= 1) stock.push_back({ s.Stock[g], g });
			std::stable_sort(stock.begin(), stock.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
			cs::String line;
			for (int i = 0; i < count && i < static_cast<int>(stock.size()); i++)
				line = cs::Cat(line, i == 0 ? "" : ", ", GoodInfo::Of(static_cast<Good>(stock[i].second))->Name, " ", EncTonnes(stock[i].first));
			return line;
		}

		cs::String EncGoodsLine(const cs::List<std::tuple<Good, double>>& goods)
		{
			cs::String line;
			for (const auto& [g, t] : goods)
				line = cs::Cat(line, line.empty() ? "" : ", ", GoodInfo::Of(g)->Name, " ", cs::Fmt(t, "0.##"), " t a day");
			return line.empty() ? cs::String("nothing") : line;
		}
	}

	using namespace EncyclopediaDetail;

	const char* ToString(EncyclopediaCategory v)
	{
		return Encyclopedia::Heading(v);
	}

	// ================================================================ the article

	cs::String EncyclopediaArticle::Body() const
	{
		cs::String body;
		for (const auto& s : Sections)
		{
			if (!body.empty()) body += "\n";
			if (!s.Heading.empty()) body = cs::Cat(body, s.Heading, "\n");
			body += s.Text;
		}
		return body;
	}

	void EncyclopediaArticle::AddSection(const cs::String& heading, const cs::String& text)
	{
		if (heading.empty() && text.empty()) return;
		Sections.Add(EncyclopediaSection{ heading, text });
	}

	void EncyclopediaArticle::AddKeywords(const cs::String& words)
	{
		for (const auto& w : cs::Strings::Split(words, ' '))
		{
			auto k = cs::Strings::ToLowerInvariant(cs::Strings::Trim(w));
			if (k.empty() || Keywords.Contains(k)) continue;
			Keywords.Add(k);
		}
	}

	// ================================================================ knowledge

	void EncyclopediaKnowledge::Index() const
	{
		if (indexed == Known.Count()) return;
		ids.clear();
		for (const auto& id : Known) ids.insert(id);
		indexed = Known.Count();
	}

	bool EncyclopediaKnowledge::Has(const cs::String& id) const
	{
		Index();
		return ids.count(id) > 0;
	}

	bool EncyclopediaKnowledge::Unlock(const cs::String& id)
	{
		if (id.empty() || Has(id)) return false;
		Known.Add(id);
		ids.insert(id);
		indexed = Known.Count();
		Version++;
		return true;
	}

	int EncyclopediaKnowledge::Sweep(const Galaxy& galaxy, const DiscoveryLog* log, const cs::List<cs::String>& recoveredBooks)
	{
		int before = Known.Count();
		for (const auto& sys : galaxy.Systems)
		{
			if (sys == nullptr || sys->World == nullptr || !EncVisited(log, sys->Name)) continue;
			const auto& world = sys->World;
			if (world->Economy != nullptr)
				for (const auto& s : world->Economy->Settlements)
				{
					if (s == nullptr || !s->Discovered()) continue;
					if (s->Owner != nullptr) Unlock(Encyclopedia::FactionId(s->Owner->Name));
					for (int g = 0; g < GoodInfo::Count && g < s->Stock.Count(); g++)
						if (s->Stock[g] >= 1) Unlock(Encyclopedia::GoodId(static_cast<Good>(g)));
					for (const auto& f : s->Facilities)
						if (f != nullptr && f->Recipe != nullptr) Unlock(Encyclopedia::IndustryId(f->Recipe->Kind));
				}
			if (world->Fleet != nullptr)
				for (const auto& ship : world->Fleet->Ships())
				{
					if (ship == nullptr || ship->Destroyed || ship->Spec == nullptr) continue;
					Unlock(Encyclopedia::HullId(ship->Spec->Class));
					if (ship->Faction != nullptr) Unlock(Encyclopedia::FactionId(ship->Faction->Name));
				}
		}
		// The pilots met, wherever (hailed across a gate, rescued out of a pod), name their side.
		for (const auto& sys : galaxy.Systems)
		{
			if (sys == nullptr || sys->World == nullptr || sys->World->Law == nullptr || sys->World->Law->Pilots == nullptr) continue;
			for (const auto& p : sys->World->Law->Pilots->All)
				if (p != nullptr && p->FirstMet != MeetKind::Mentioned && p->Faction != nullptr && log != nullptr
					&& log->Has(cs::Cat("pilot|", sys->Name, "|", p->Id, " ", p->Name)))
					Unlock(Encyclopedia::FactionId(p->Faction->Name));
		}
		for (const auto& key : recoveredBooks) Unlock(Encyclopedia::LoreId(key));
		if (log != nullptr)
			for (const auto& e : log->Entries)
				if (e != nullptr && e->LoreKey.rfind(LoreKeys::Universe, 0) == 0) Unlock(Encyclopedia::LoreId(e->LoreKey));
		return Known.Count() - before;
	}

	// ================================================================ ids and categories

	const char* Encyclopedia::Slug(EncyclopediaCategory c)
	{
		switch (c)
		{
			case EncyclopediaCategory::Controls: return "controls";
			case EncyclopediaCategory::Guides: return "guides";
			case EncyclopediaCategory::Lore: return "lore";
			case EncyclopediaCategory::Astronomy: return "astronomy";
			case EncyclopediaCategory::Commerce: return "commerce";
			case EncyclopediaCategory::Factions: return "factions";
			case EncyclopediaCategory::Ships: return "ships";
			case EncyclopediaCategory::Stations: return "stations";
			case EncyclopediaCategory::Settlements: return "settlements";
			case EncyclopediaCategory::Systems: return "systems";
			case EncyclopediaCategory::Pilots: return "pilots";
		}
		return "guides";
	}

	const char* Encyclopedia::Heading(EncyclopediaCategory c)
	{
		switch (c)
		{
			case EncyclopediaCategory::Controls: return "Controls";
			case EncyclopediaCategory::Guides: return "Guides";
			case EncyclopediaCategory::Lore: return "Lore";
			case EncyclopediaCategory::Astronomy: return "Astronomy";
			case EncyclopediaCategory::Commerce: return "Commerce";
			case EncyclopediaCategory::Factions: return "Factions";
			case EncyclopediaCategory::Ships: return "Ships";
			case EncyclopediaCategory::Stations: return "Stations";
			case EncyclopediaCategory::Settlements: return "Settlements";
			case EncyclopediaCategory::Systems: return "Systems";
			case EncyclopediaCategory::Pilots: return "Pilots";
		}
		return "";
	}

	const char* Encyclopedia::HowLearned(EncyclopediaCategory c)
	{
		switch (c)
		{
			case EncyclopediaCategory::Controls: return "Every key and button, as they are bound now.";
			case EncyclopediaCategory::Guides: return "The manual, chapter by chapter.";
			case EncyclopediaCategory::Lore: return "Ship logs and station records you recover, the Bio Mass, Void Raider strikes and signs of the Watcher you come across, and what Prose has written of the universe they belong to.";
			case EncyclopediaCategory::Astronomy: return "A body is written in when you open it on the scan card (select it) or bring a helm in close; a star when you visit its system; a void sight when you stop at it.";
			case EncyclopediaCategory::Commerce: return "Goods and industries are learned in the markets of the settlements you find; the markets themselves as you visit them.";
			case EncyclopediaCategory::Factions: return "A faction is learned when you visit a system it holds settlements or ships in, or meet one of its pilots.";
			case EncyclopediaCategory::Ships: return "A hull class is learned when you are in a system with one in its space.";
			case EncyclopediaCategory::Stations: return "Station ports, derelict stations and graveyards are written in as you find them.";
			case EncyclopediaCategory::Settlements: return "The colonies, outposts and pirate havens of each system are written in as you find them.";
			case EncyclopediaCategory::Systems: return "A system is written in when a lookout charts its star or you visit it.";
			case EncyclopediaCategory::Pilots: return "A pilot is written in when you hail them, fly their hull, fight them or pull them out of a wreck.";
		}
		return "";
	}

	cs::String Encyclopedia::MakeId(EncyclopediaCategory c, const cs::String& name)
	{
		return cs::Cat(Slug(c), "/", LoreKeys::Slug(name));
	}

	cs::String Encyclopedia::FactionId(const cs::String& factionName) { return MakeId(EncyclopediaCategory::Factions, LoreKeys::Faction(factionName)); }

	cs::String Encyclopedia::GoodId(Good g) { return MakeId(EncyclopediaCategory::Commerce, cs::Cat("good-", GoodInfo::Of(g)->Name)); }

	cs::String Encyclopedia::IndustryId(IndustryKind k) { return MakeId(EncyclopediaCategory::Commerce, cs::Cat("industry-", Core::ToString(k))); }

	cs::String Encyclopedia::HullId(ShipClass c) { return MakeId(EncyclopediaCategory::Ships, cs::Cat("hull-", Core::ToString(c))); }

	cs::String Encyclopedia::LoreId(const cs::String& loreKey) { return MakeId(EncyclopediaCategory::Lore, loreKey); }

	EncyclopediaCategory Encyclopedia::CategoryOf(const DiscoveryEntry& e)
	{
		switch (e.Kind)
		{
			case DiscoveryKind::Star: return EncyclopediaCategory::Systems;
			case DiscoveryKind::Body: return EncyclopediaCategory::Astronomy;
			case DiscoveryKind::VoidSight: return EncyclopediaCategory::Astronomy;
			case DiscoveryKind::Settlement: return e.Note.find("station port") != cs::String::npos ? EncyclopediaCategory::Stations : EncyclopediaCategory::Settlements;
			case DiscoveryKind::Haven: return EncyclopediaCategory::Settlements;
			case DiscoveryKind::Graveyard: return EncyclopediaCategory::Stations;
			case DiscoveryKind::Derelict: return EncyclopediaCategory::Stations;
			case DiscoveryKind::Sighting: return EncyclopediaCategory::Lore;
			case DiscoveryKind::Pilot: return EncyclopediaCategory::Pilots;
		}
		return EncyclopediaCategory::Systems;
	}

	cs::String Encyclopedia::DiscoveryId(const DiscoveryEntry& e)
	{
		// A sighting's Prose key is the universe's text about what was seen (shared by every sighting of it): its own id.
		bool ownKey = !e.LoreKey.empty() && e.Kind != DiscoveryKind::Sighting;
		return MakeId(CategoryOf(e), ownKey ? e.LoreKey : e.Id);
	}

	// ================================================================ the articles

	cs::Ref<EncyclopediaArticle> Encyclopedia::Add(const cs::Ref<EncyclopediaArticle>& a)
	{
		finished = false;
		auto it = byId.find(a->Id);
		if (it != byId.end())
		{
			articles[it->second] = a;
			return a;
		}
		byId[a->Id] = articles.Count();
		articles.Add(a);
		return a;
	}

	cs::Ref<EncyclopediaArticle> Encyclopedia::Find(const cs::String& id) const
	{
		auto it = byId.find(id);
		return it == byId.end() ? nullptr : articles[it->second];
	}

	void Encyclopedia::Clear()
	{
		articles.Clear();
		byId.clear();
		keywordIndex.clear();
		lowTitle.clear();
		lowKeywords.clear();
		lowBody.clear();
		finished = false;
	}

	bool Encyclopedia::IsKnown(const EncyclopediaArticle& a, const EncyclopediaKnowledge* k)
	{
		return a.AlwaysKnown || (k != nullptr && k->Has(a.Id));
	}

	cs::List<cs::Ref<EncyclopediaArticle>> Encyclopedia::Known(EncyclopediaCategory c, const EncyclopediaKnowledge* k) const
	{
		cs::List<cs::Ref<EncyclopediaArticle>> list;
		for (const auto& a : articles)
			if (a->Category == c && IsKnown(*a, k)) list.Add(a);
		// Controls and guides keep the order they were written in (the bindings page's, the manual's); the rest by title.
		if (c != EncyclopediaCategory::Controls && c != EncyclopediaCategory::Guides)
			std::stable_sort(list.begin(), list.end(), [](const auto& x, const auto& y) { return EncLow(x->Title) < EncLow(y->Title); });
		return list;
	}

	int Encyclopedia::CountKnown(EncyclopediaCategory c, const EncyclopediaKnowledge* k) const
	{
		int n = 0;
		for (const auto& a : articles)
			if (a->Category == c && IsKnown(*a, k)) n++;
		return n;
	}

	// ================================================================ search, keywords and links

	size_t Encyclopedia::FindNoCase(const cs::String& text, const cs::String& needle, size_t from)
	{
		if (needle.empty() || needle.size() > text.size()) return cs::String::npos;
		for (size_t i = from; i + needle.size() <= text.size(); i++)
		{
			size_t j = 0;
			while (j < needle.size() && EncLower(text[i + j]) == needle[j]) j++;
			if (j == needle.size()) return i;
		}
		return cs::String::npos;
	}

	cs::List<cs::String> Encyclopedia::Words(const cs::String& query)
	{
		cs::List<cs::String> words;
		for (const auto& w : cs::Strings::Split(EncLow(query), ' '))
			if (!w.empty() && !words.Contains(w)) words.Add(w);
		return words;
	}

	cs::String Encyclopedia::Snippet(const cs::String& text, const cs::String& word, int width)
	{
		size_t at = FindNoCase(text, word);
		if (at == cs::String::npos) return "";
		size_t half = static_cast<size_t>(std::max(20, width / 2));
		size_t start = at > half ? at - half : 0;
		size_t end = std::min(text.size(), at + word.size() + half);
		// Cut at spaces so no word is split (nor a UTF-8 character).
		if (start > 0)
		{
			size_t sp = text.find(' ', start);
			if (sp != cs::String::npos && sp < at) start = sp + 1;
		}
		if (end < text.size())
		{
			size_t sp = text.rfind(' ', end);
			if (sp != cs::String::npos && sp > at + word.size()) end = sp;
		}
		cs::String s = text.substr(start, end - start);
		for (auto& c : s)
			if (c == '\n' || c == '\r' || c == '\t') c = ' ';
		return cs::Cat(start > 0 ? "..." : "", s, end < text.size() ? "..." : "");
	}

	void Encyclopedia::Finish()
	{
		keywordIndex.clear();
		lowTitle.assign(articles.Count(), std::string());
		lowKeywords.assign(articles.Count(), std::string());
		lowBody.assign(articles.Count(), std::string());
		for (int i = 0; i < articles.Count(); i++)
		{
			const auto& a = *articles[i];
			lowTitle[i] = EncLow(a.Title);
			std::string kw;
			for (const auto& k : a.Keywords)
			{
				kw += EncLow(k);
				kw += ' ';
				keywordIndex[EncLow(k)].push_back(i);
			}
			lowKeywords[i] = kw;
			lowBody[i] = EncLow(cs::Cat(a.Subtitle, "\n", a.Body()));
			auto& titled = keywordIndex[lowTitle[i]];
			if (std::find(titled.begin(), titled.end(), i) == titled.end()) titled.push_back(i);
		}
		finished = true;
	}

	cs::List<EncyclopediaHit> Encyclopedia::Search(const cs::String& query, const EncyclopediaKnowledge* k) const
	{
		cs::List<EncyclopediaHit> hits;
		auto words = Words(query);
		if (words.Count() == 0) return hits;
		if (!finished) const_cast<Encyclopedia*>(this)->Finish();
		std::string whole = EncLow(cs::Strings::Trim(query));
		for (int i = 0; i < articles.Count(); i++)
		{
			const auto& a = articles[i];
			if (!IsKnown(*a, k)) continue;
			bool all = true, allInTitle = true, allInTitleOrKeywords = true;
			for (const auto& w : words)
			{
				bool t = lowTitle[i].find(w) != std::string::npos;
				bool kw = lowKeywords[i].find(w) != std::string::npos;
				bool b = lowBody[i].find(w) != std::string::npos;
				if (!t && !kw && !b) { all = false; break; }
				allInTitle = allInTitle && t;
				allInTitleOrKeywords = allInTitleOrKeywords && (t || kw);
			}
			if (!all) continue;
			EncyclopediaHit hit;
			hit.Article = a;
			hit.Rank = lowTitle[i].find(whole) != std::string::npos || allInTitle ? 0 : allInTitleOrKeywords ? 1 : 2;
			// The line of the body around the first word that it holds.
			cs::String body = a->Body();
			for (const auto& w : words)
			{
				hit.Snippet = Snippet(body, w);
				if (!hit.Snippet.empty()) break;
			}
			hits.Add(hit);
		}
		std::stable_sort(hits.begin(), hits.end(), [](const EncyclopediaHit& x, const EncyclopediaHit& y)
		{
			if (x.Rank != y.Rank) return x.Rank < y.Rank;
			return EncLow(x.Article->Title) < EncLow(y.Article->Title);
		});
		return hits;
	}

	cs::List<cs::String> Encyclopedia::ArticlesFor(const cs::String& word, const EncyclopediaKnowledge* k) const
	{
		cs::List<cs::String> ids;
		if (!finished) const_cast<Encyclopedia*>(this)->Finish();
		auto it = keywordIndex.find(EncLow(cs::Strings::Trim(word)));
		if (it == keywordIndex.end()) return ids;
		std::vector<int> found = it->second;
		std::stable_sort(found.begin(), found.end(), [this](int x, int y) { return lowTitle[x] < lowTitle[y]; });
		for (int i : found)
			if (k == nullptr || IsKnown(*articles[i], k))
				if (!ids.Contains(articles[i]->Id)) ids.Add(articles[i]->Id);
		return ids;
	}

	cs::List<cs::Ref<EncyclopediaArticle>> Encyclopedia::LinksOf(const EncyclopediaArticle& a, const EncyclopediaKnowledge* k, int max) const
	{
		cs::List<cs::Ref<EncyclopediaArticle>> links;
		if (!finished) const_cast<Encyclopedia*>(this)->Finish();
		std::string body = EncLow(cs::Cat(a.Subtitle, "\n", a.Body()));
		std::vector<std::pair<size_t, int>> named;
		for (int i = 0; i < articles.Count(); i++)
		{
			const auto& b = articles[i];
			if (b->Id == a.Id || lowTitle[i].size() < 4 || !IsKnown(*b, k)) continue;
			size_t at = EncFindWord(body, lowTitle[i]);
			if (at != std::string::npos) named.push_back({ at, i });
		}
		std::stable_sort(named.begin(), named.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
		for (const auto& [at, i] : named)
		{
			if (links.Count() >= max) break;
			links.Add(articles[i]);
		}
		return links;
	}

	// ================================================================ builders

	cs::Ref<EncyclopediaArticle> Encyclopedia::AddControls(const cs::String& slug, const cs::String& title, const cs::String& subtitle,
		const cs::List<EncyclopediaSection>& lines, const cs::String& keywords)
	{
		auto a = cs::New<EncyclopediaArticle>();
		a->Id = MakeId(EncyclopediaCategory::Controls, slug);
		a->Category = EncyclopediaCategory::Controls;
		a->Title = title;
		a->Subtitle = subtitle;
		a->Sections = lines;
		a->AlwaysKnown = true;
		a->AddKeywords("controls control keys key keyboard binding bindings");
		a->AddKeywords(keywords);
		return Add(a);
	}

	void Encyclopedia::AddControlCard()
	{
		for (const auto& s : ControlsHelp::Sections())
		{
			cs::List<EncyclopediaSection> lines;
			for (const auto& l : s->Lines) lines.Add(EncyclopediaSection{ l.Keys, l.Text });
			AddControls(cs::Cat("card-", s->Title), cs::Cat(EncSentence(s->Title), ": reference card"),
				"The Help screen's card for it: mouse, keys and what they do", lines, cs::Cat("reference card help mouse ", s->Title));
		}
	}

	void Encyclopedia::AddGuides()
	{
		for (const auto& c : GameManual::Chapters())
		{
			if (c->Title == GameManual::EveryKeyTitle) continue; // the Controls category is the whole card
			auto a = cs::New<EncyclopediaArticle>();
			a->Id = MakeId(EncyclopediaCategory::Guides, c->Title);
			a->Category = EncyclopediaCategory::Guides;
			a->Title = c->Title;
			a->Subtitle = c->Summary;
			a->AlwaysKnown = true;
			a->AddKeywords("guide manual help how");
			a->AddKeywords(c->Title);
			for (const auto& t : c->Topics)
			{
				a->AddSection(t.Heading, t.Text);
				a->AddKeywords(t.Heading);
			}
			for (const auto& s : c->Controls())
			{
				cs::String keys;
				for (const auto& l : s->Lines) keys = cs::Cat(keys, keys.empty() ? "" : "\n", l.Keys.empty() ? cs::String("   ") : cs::Cat(l.Keys, "  -  "), l.Text);
				a->AddSection(cs::Cat("Keys: ", EncSentence(s->Title)), keys);
			}
			Add(a);
		}
	}

	void Encyclopedia::AddLore(const LoreLibrary& lore, const cs::List<cs::String>& recoveredBooks)
	{
		for (const auto& key : recoveredBooks)
		{
			LoreBook book;
			if (!LoreBooks::Read(lore, key, book)) continue;
			auto a = cs::New<EncyclopediaArticle>();
			a->Id = LoreId(key);
			a->Category = EncyclopediaCategory::Lore;
			a->Title = book.Title;
			bool records = key.size() > 8 && key.compare(key.size() - 8, 8, "/records") == 0;
			a->Subtitle = cs::Cat(records ? "Station records" : "Ship's log", book.Author.empty() ? cs::String() : cs::Cat(", by ", book.Author), ", recovered");
			a->LoreKey = key;
			a->AddKeywords(records ? "records station book recovered" : "log ship book recovered");
			for (int i = 0; i < book.Pages.Count(); i++) a->AddSection(book.Pages.Count() > 1 ? cs::Cat("Page ", i + 1) : cs::String(), book.Pages[i]);
			Add(a);
		}
		// What Prose has written of the universe as a whole: spacegod (always) and spacegod/<name> (once a find names it).
		for (const auto& key : lore.Keys())
		{
			if (key != LoreKeys::Universe && key.rfind(cs::Cat(LoreKeys::Universe, "/"), 0) != 0) continue;
			cs::String text = lore.Long(key, "");
			if (text.empty()) continue;
			auto a = cs::New<EncyclopediaArticle>();
			a->Id = LoreId(key);
			a->Category = EncyclopediaCategory::Lore;
			cs::String name = key == LoreKeys::Universe ? cs::String("The SpaceGod universe") : key.substr(key.find('/') + 1);
			for (auto& c : name)
				if (c == '-') c = ' ';
			a->Title = EncSentence(name);
			a->Subtitle = lore.Short(key, "");
			if (a->Subtitle == text) a->Subtitle.clear();
			a->LoreKey = key;
			a->AlwaysKnown = key == LoreKeys::Universe;
			a->AddKeywords("lore history universe");
			a->AddKeywords(name);
			a->AddSection("", text);
			Add(a);
		}
	}

	void Encyclopedia::AddCommerce(const Galaxy& galaxy, const DiscoveryLog* log)
	{
		cs::List<cs::Ref<Recipe>> recipes;
		for (int k = 0; k <= static_cast<int>(IndustryKind::Shipyard); k++)
			if (static_cast<IndustryKind>(k) != IndustryKind::Mine) recipes.Add(Recipe::Of(static_cast<IndustryKind>(k)));
		cs::List<cs::Ref<Faction>> factions;
		if (galaxy.Polities != nullptr) factions = galaxy.Polities->Factions();

		for (int g = 0; g < GoodInfo::Count; g++)
		{
			auto good = static_cast<Good>(g);
			const auto& info = GoodInfo::Of(good);
			auto a = cs::New<EncyclopediaArticle>();
			a->Id = GoodId(good);
			a->Category = EncyclopediaCategory::Commerce;
			a->Title = info->Name;
			cs::String category = cs::Strings::ToLowerInvariant(cs::String(Core::ToString(info->Category)));
			a->Subtitle = cs::Cat("A traded good (", category, "), about ", cs::Fmt(info->BasePrice, "N0"), " cr a tonne at base");
			a->AddKeywords(cs::Cat("good goods commodity trade market cargo price ", category));
			a->AddKeywords(info->Name);
			cs::String made, used;
			for (const auto& r : recipes)
			{
				if (r == nullptr) continue;
				for (const auto& [og, t] : r->Outputs)
					if (og == good) made = cs::Cat(made, made.empty() ? "" : ", ", r->Name);
				for (const auto& [ig, t] : r->Inputs)
					if (ig == good) used = cs::Cat(used, used.empty() ? "" : ", ", r->Name);
			}
			bool ore = info->Category == GoodCategory::Mineral || info->Category == GoodCategory::Volatile || good == Good::Water;
			if (ore) made = cs::Cat(made, made.empty() ? "" : ", ", "Mines (extraction from the site)");
			a->AddSection("Made by", made.empty() ? cs::String("No industry in the known worlds makes it.") : made);
			a->AddSection("Used by", used.empty() ? cs::String("Consumed by the people of the settlements that buy it.") : used);
			cs::String banned;
			for (const auto& f : factions)
				if (f != nullptr && f->Contraband.Contains(good)) banned = cs::Cat(banned, banned.empty() ? "" : ", ", f->Name);
			if (!banned.empty()) a->AddSection("Contraband", cs::Cat("Banned by ", banned, ": a patrol's scan that finds it in a hold confiscates it."));
			Add(a);
		}

		for (int k = 0; k <= static_cast<int>(IndustryKind::Shipyard); k++)
		{
			auto kind = static_cast<IndustryKind>(k);
			auto a = cs::New<EncyclopediaArticle>();
			a->Id = IndustryId(kind);
			a->Category = EncyclopediaCategory::Commerce;
			a->AddKeywords("industry industries facility production economy");
			if (kind == IndustryKind::Mine)
			{
				a->Title = "Mines";
				a->Subtitle = "An industry: ore, ice and gas cut from the site";
				a->AddSection("", "A mine takes its output from the body or belt it is on: ice for water, ores for nickel-iron, copper, zinc, "
					"titanium and platinum, rare earths, helium-3 sifted from regolith, hydrocarbons and nitrogen. It burns a little machinery "
					"and helium-3 as it works, and needs miners by the tonne: bulk ice and ore little labour, concentrated metals ten times as "
					"much, the trace products a thousand times.");
			}
			else
			{
				auto r = Recipe::Of(kind);
				if (r == nullptr) continue;
				a->Title = r->Name;
				a->Subtitle = "An industry of the settlements";
				a->AddSection("Takes in", EncGoodsLine(r->Inputs));
				a->AddSection("Makes", EncGoodsLine(r->Outputs));
				if (r->WorkersPerLevel > 0) a->AddSection("Workers", cs::Cat(cs::Fmt(r->WorkersPerLevel, "N0"), " to run one level at full capacity. A shortage of a bulk feedstock caps its output outright; one of a consumable only cuts its efficiency."));
			}
			a->AddKeywords(cs::String(Core::ToString(kind)));
			Add(a);
		}

		// The markets of the settlements found, as they stand now.
		auto markets = cs::New<EncyclopediaArticle>();
		markets->Id = MakeId(EncyclopediaCategory::Commerce, "markets");
		markets->Category = EncyclopediaCategory::Commerce;
		markets->Title = "Markets";
		markets->Subtitle = "The markets of the settlements you have found, and what they hold most of now";
		markets->AddKeywords("market markets trade price stock buy sell");
		markets->AlwaysKnown = true;
		markets->AddSection("", "Every settlement keeps a market: stock it holds goes cheap, a good it is short of dear. M opens the market of the port in view, "
			"\\ the trade desk with the runs your price sheets add up to.");
		if (log != nullptr)
			for (const auto& e : log->Entries)
			{
				if (e == nullptr || (e->Kind != DiscoveryKind::Settlement && e->Kind != DiscoveryKind::Haven)) continue;
				auto s = EncFindSettlement(galaxy, e->System, e->Name);
				if (s == nullptr) continue;
				cs::String top = EncTopStock(*s, 4);
				markets->AddSection(cs::Cat(e->Name, "  (", e->System, ")"), top.empty() ? cs::String("Empty shelves.") : cs::Cat("Holds most of: ", top));
			}
		Add(markets);
	}

	void Encyclopedia::AddShips()
	{
		for (const auto& e : ShipCatalog::Entries())
		{
			if (e == nullptr) continue;
			auto a = cs::New<EncyclopediaArticle>();
			a->Id = HullId(e->Class);
			a->Category = EncyclopediaCategory::Ships;
			a->Title = e->Name;
			a->Subtitle = e->Role;
			cs::String category = cs::Strings::ToLowerInvariant(cs::String(Core::ToString(e->Category)));
			a->AddKeywords(cs::Cat("ship ships hull class ", category, e->SmallCraft ? " small craft fighter" : " capital"));
			a->AddKeywords(cs::String(Core::ToString(e->Class)));
			const auto& h = e->Handling;
			a->AddSection("Handling", cs::Cat("Top speed ", cs::Fmt(h.TopSpeedKmS, "0.##"), " km/s, turn ", cs::Fmt(h.TurnRateDegS, "0.#"), " deg/s, roll ",
				cs::Fmt(h.RollRateDegS, "0.#"), " deg/s; boost x", cs::Fmt(h.BoostSpeedMultiplier, "0.##"), " for ", cs::Fmt(h.BoostDurationS, "0.#"),
				" s, cruise x", cs::Fmt(h.CruiseSpeedMultiplier, "0.##"), "."));
			cs::String designs;
			for (const auto& d : e->Designs)
				designs = cs::Cat(designs, designs.empty() ? "" : "\n", d.Name, ": ", cs::Fmt(d.LengthKm * 1000, "N0"), " m, ", cs::Fmt(d.MassKt, "N1"), " kt; hull ",
					cs::Fmt(d.Hull, "N0"), ", shields ", cs::Fmt(d.Shield, "N0"), ", armour ", cs::Fmt(d.Armor, "N0"));
			a->AddSection("Designs", designs);
			cs::String fitted;
			if (e->CargoTonnes > 0) fitted = cs::Cat(fitted, "Hold ", EncTonnes(e->CargoTonnes), ". ");
			if (e->HangarCraft > 0) fitted = cs::Cat(fitted, "Hangar for ", EncPlural(e->HangarCraft, "craft", "craft"), ". ");
			if (e->Crew > 0) fitted = cs::Cat(fitted, "Crew ", cs::Fmt(e->Crew, "N0"), e->Marines > 0 ? cs::Cat(" and ", cs::Fmt(e->Marines, "N0"), " marines") : cs::String(), ". ");
			if (e->ReactorMw > 0) fitted = cs::Cat(fitted, "Reactor spare ", cs::Fmt(e->ReactorMw, "N0"), " MW. ");
			a->AddSection("Aboard", cs::Strings::Trim(fitted));
			Add(a);
		}
	}

	void Encyclopedia::AddFactions(const Galaxy& galaxy, const LoreLibrary& lore)
	{
		if (galaxy.Polities == nullptr) return;
		for (const auto& f : galaxy.Polities->Factions())
		{
			if (f == nullptr) continue;
			auto a = cs::New<EncyclopediaArticle>();
			a->Id = FactionId(f->Name);
			a->Category = EncyclopediaCategory::Factions;
			a->Title = f->Name;
			cs::String kind = cs::Strings::ToLowerInvariant(cs::String(Core::ToString(f->Kind)));
			a->Subtitle = f->IsHouse ? cs::String("Your own house") : f->IsPirate ? cs::String("A pirate clan, hostile to everyone")
				: cs::Cat("A ", kind, f->IsMilitary ? " with a navy" : "");
			a->AlwaysKnown = f->IsHouse;
			a->LoreKey = LoreKeys::Faction(f->Name);
			a->AddKeywords(cs::Cat("faction factions polity power ", kind, f->IsPirate ? " pirate pirates clan" : "", f->IsMilitary ? " navy military" : ""));
			a->AddKeywords(f->Name);
			cs::String record = lore.Long(a->LoreKey, "");
			if (!record.empty()) a->AddSection("", record);
			int settlements = 0, ships = 0;
			for (const auto& sys : galaxy.Systems)
			{
				if (sys == nullptr || sys->World == nullptr) continue;
				if (sys->World->Economy != nullptr)
					for (const auto& s : sys->World->Economy->Settlements)
						if (s != nullptr && s->Owner == f) settlements++;
				if (sys->World->Fleet != nullptr)
					for (const auto& s : sys->World->Fleet->Ships())
						if (s != nullptr && !s->Destroyed && s->Faction == f) ships++;
			}
			cs::String standing = cs::Cat(EncPlural(settlements, "settlement", "settlements"), " and ", EncPlural(ships, "ship", "ships"), " in the systems running now.");
			auto capital = f->Capital != nullptr ? f->Capital->Name : cs::String();
			if (!capital.empty()) standing = cs::Cat(standing, " Its seat is ", capital, ".");
			a->AddSection("Holdings", standing);
			if (!f->IsHouse) a->AddSection("Its crews", cs::Cat("Marines ", cs::Fmt(f->MarineQuality, "0.00"), "x the average, morale ", cs::Fmt(f->CrewMorale, "0.00"),
				", ", cs::Fmt(f->CrewLost, "N0"), " spacers lost and ", cs::Fmt(f->CrewRescued, "N0"), " brought home."));
			cs::String banned;
			for (int g = 0; g < GoodInfo::Count; g++)
				if (f->Contraband.Contains(static_cast<Good>(g))) banned = cs::Cat(banned, banned.empty() ? "" : ", ", GoodInfo::Of(static_cast<Good>(g))->Name);
			if (!banned.empty()) a->AddSection("Contraband", cs::Cat("Its law bans ", banned, "."));
			Add(a);
		}
	}

	void Encyclopedia::AddDiscoveries(const Galaxy& galaxy, const DiscoveryLog& log, const LoreLibrary& lore, double jd)
	{
		for (const auto& e : log.Entries)
		{
			if (e == nullptr) continue;
			auto a = cs::New<EncyclopediaArticle>();
			a->Category = CategoryOf(*e);
			a->Id = DiscoveryId(*e);
			// Two finds slugging alike (a pilot's namesake, a sighting seen twice) are numbered in log order, which is saved.
			if (Find(a->Id) != nullptr)
			{
				cs::String base = a->Id;
				for (int n = 2; Find(a->Id) != nullptr; n++) a->Id = cs::Cat(base, "-", n);
			}
			a->Title = e->Name;
			a->Subtitle = cs::Cat(DiscoveryLog::Heading(e->Kind), e->System.empty() || e->System == e->Name ? cs::String() : cs::Cat("  \xC2\xB7  ", e->System));
			a->AlwaysKnown = true; // a find exists only once found
			a->EntityId = e->Id;
			a->LoreKey = e->Kind == DiscoveryKind::Sighting ? cs::String() : e->LoreKey;
			a->AddKeywords(cs::Cat("discovery found ", DiscoveryLog::Heading(e->Kind)));
			a->AddKeywords(e->System);
			if (!e->By.empty()) a->AddKeywords(e->By);
			if (!e->Note.empty()) a->AddSection("", e->Note);
			auto readings = DiscoveryLog::Readings(galaxy, *e, jd);
			cs::String lines;
			for (const auto& l : readings) lines = cs::Cat(lines, lines.empty() ? "" : "\n", l);
			a->AddSection(EncyclopediaArticle::LiveSection, lines);
			if (e->Kind == DiscoveryKind::Settlement || e->Kind == DiscoveryKind::Haven)
				if (auto s = EncFindSettlement(galaxy, e->System, e->Name))
				{
					if (s->Owner != nullptr) a->AddKeywords(s->Owner->Name);
					cs::String top = EncTopStock(*s, 5);
					if (!top.empty()) a->AddSection("Market", cs::Cat("Holds most of: ", top));
				}
			cs::String record = e->LoreKey.empty() ? cs::String() : lore.Long(e->LoreKey, "");
			if (!record.empty()) a->AddSection("Record", record);
			Add(a);

			// A star visited: the star itself, under Astronomy, beside its system's article.
			if (e->Kind == DiscoveryKind::Star && e->Note.find("isited") != cs::String::npos)
			{
				auto sys = galaxy.Find(e->System);
				auto cat = sys != nullptr ? sys->Catalog : nullptr;
				auto root = cat != nullptr && cat->Count() > 0 ? cat->Root() : nullptr;
				if (root == nullptr) continue;
				auto star = cs::New<EncyclopediaArticle>();
				star->Category = EncyclopediaCategory::Astronomy;
				star->Id = MakeId(EncyclopediaCategory::Astronomy, LoreKeys::Body(root->Kind, e->System, root->Name));
				if (Find(star->Id) != nullptr) continue;
				star->Title = root->Name;
				star->Subtitle = cs::Cat(root->Kind == BodyKind::Barycentre ? "The centre of a multiple star" : "A star", "  \xC2\xB7  ", e->System);
				star->AlwaysKnown = true;
				star->LoreKey = LoreKeys::Body(root->Kind, e->System, root->Name);
				star->AddKeywords("star stars astronomy sun");
				star->AddKeywords(e->System);
				if (!root->Summary.empty()) star->AddSection("", root->Summary);
				if (root->RadiusKm > 0) star->AddSection("Size", cs::Cat("Radius ", cs::Fmt(root->RadiusKm, "N0"), " km."));
				int bodies = cat->Count() - 1;
				star->AddSection("Its system", cs::Cat(EncPlural(bodies, "body", "bodies"), " known around it in ", e->System, "."));
				cs::String starRecord = lore.Long(star->LoreKey, "");
				if (!starRecord.empty()) star->AddSection("Record", starRecord);
				Add(star);
			}
		}
	}

	// ================================================================ ObjectGraph registrations (docs/PERSISTENCE.md)

	SG_SERIALIZE(EncyclopediaKnowledge, "SpaceGod.Core.EncyclopediaKnowledge",
		SG_F(Known))
}
