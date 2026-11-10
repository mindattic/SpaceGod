// US-T272: the scan card and the Encyclopedia, each leading to the other (declared in UnitySpaceApp.Encyclopedia.inl). The
// card's keyword links are UI::Hud's (Hud.ScanLinks.cpp); here, which article is about what the card shows, and the readings
// an entity's entry shows: the card's own while the thing is in sight (UI::Hud::ScanReadings, the same code as the card),
// else the last the card took and how old they are, else the discovery log's. Display only: nothing here changes the
// simulation, and the readings remembered are this session's.

#include "UnitySpaceApp.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Fleet.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Comms/Pilots.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "GameLore.h"
#include "Infrastructure/Infrastructure.h"
#include "Law/Law.h"
#include "Lore/Encyclopedia.h"
#include "Lore/LoreKeys.h"
#include "Lore/LoreLibrary.h"
#include "Lore/ScanTabs.h"
#include "Systems/DiscoveryLog.h"
#include "Systems/Galaxy.h"
#include "UI/Hud.h"

namespace SpaceGod
{
	using namespace Core;

	namespace
	{
		/// How long ago <paramref name="days"/> was, in words ("3 hours", "12 days").
		cs::String ScanAgo(double days)
		{
			if (days < 1.0 / 24) return cs::Cat(cs::Math::Max(1, static_cast<int>(days * 1440)), " min");
			if (days < 2) return cs::Cat(cs::Fmt(days * 24, "0"), " hours");
			if (days < 730) return cs::Cat(cs::Fmt(days, "0"), " days");
			return cs::Cat(cs::Fmt(days / 365.25, "0.0"), " years");
		}

		cs::String ScanDate(double jd)
		{
			return jd <= 0 ? cs::String("date unknown") : Astro::ToDateTime(jd).ToString("yyyy-MM-dd");
		}
	}

	cs::String UnitySpaceApp::EntityArticleId(const cs::String& logId)
	{
		if (logId.empty() || Galaxy == nullptr) return cs::String();
		const auto& book = EncyclopediaBook();
		if (entityArticlesBuilds != encyclopediaBuilds)
		{
			entityArticlesBuilds = encyclopediaBuilds;
			entityArticles.Clear();
			for (const auto& a : book.Articles())
			{
				if (a == nullptr) continue;
				if (!a->EntityId.empty()) entityArticles.TryAdd(a->EntityId, a->Id);
				// A star visited is written in beside its system with no find of its own: keyed by its Prose key.
				else if (a->Category == EncyclopediaCategory::Astronomy && !a->LoreKey.empty()) entityArticles.TryAdd(a->LoreKey, a->Id);
			}
		}
		cs::String id;
		entityArticles.TryGetValue(logId, id);
		return id;
	}

	cs::String UnitySpaceApp::TargetEntityKey(const Target& t, int subtab)
	{
		if (Galaxy == nullptr || ActiveSystem == nullptr) return cs::String();
		const auto& log = Discoveries();
		const cs::String& sys = ActiveSystem->Name;
		if (t.IsBody())
		{
			auto cat = Catalog();
			if (cat == nullptr || t.Body < 0 || t.Body >= cat->Count()) return cs::String();
			const auto& b = (*cat)[t.Body];
			if (subtab > 0)
			{
				// The card's subtabs: the body, then each settlement found on it, in the economy's order.
				int i = 0;
				for (const auto& s : Economy()->At(t.Body))
				{
					if (s == nullptr || !s->Discovered()) continue;
					if (++i < subtab) continue;
					cs::String id = cs::Cat(s->Kind == SettlementKind::Haven ? "haven" : "settlement", "|", sys, "|", s->Name);
					return log.Has(id) ? id : cs::String();
				}
				return cs::String();
			}
			if (b->IsStar() && cat->Root() == b) return LoreKeys::Body(b->Kind, sys, b->Name);
			cs::String body = cs::Cat("body|", sys, "|", b->Name), sight = cs::Cat("sight|", sys, "|", b->Name);
			return log.Has(body) ? body : log.Has(sight) ? sight : cs::String();
		}
		if (t.IsShip() && t.Ship != nullptr && subtab > 0)
		{
			auto world = World();
			auto pilots = world != nullptr && world->Law != nullptr ? world->Law->Pilots : nullptr;
			auto p = pilots != nullptr ? pilots->Find(t.Ship) : nullptr;
			if (p == nullptr) return cs::String();
			cs::String id = cs::Cat("pilot|", sys, "|", p->Id, " ", p->Name);
			return log.Has(id) ? id : cs::String();
		}
		return cs::String();
	}

	cs::String UnitySpaceApp::TargetArticleId(const Target& t)
	{
		if (Galaxy == nullptr || t.IsNone() || t.IsUnit()) return cs::String();
		cs::String id = EntityArticleId(TargetEntityKey(t, 0));
		if (!id.empty()) return id;
		if (t.IsShip() && t.Ship != nullptr)
		{
			id = EntityArticleId(TargetEntityKey(t, 1)); // its pilot, once met
			if (id.empty() && t.Ship->Spec != nullptr) id = Encyclopedia::HullId(t.Ship->Spec->Class);
		}
		else if (ActiveSystem != nullptr)
		{
			cs::Ref<Settlement> served = t.IsStructure() && t.Structure != nullptr ? t.Structure->Settlement : t.IsRing() && t.Ring != nullptr ? t.Ring->Settlement : nullptr;
			if (served != nullptr)
				id = EntityArticleId(cs::Cat(served->Kind == SettlementKind::Haven ? "haven" : "settlement", "|", ActiveSystem->Name, "|", served->Name));
		}
		if (id.empty()) return cs::String();
		const auto& book = EncyclopediaBook();
		auto a = book.Find(id);
		return a != nullptr ? id : cs::String();
	}

	void UnitySpaceApp::RememberReadings(const cs::String& logId, const cs::List<cs::String>& lines)
	{
		if (logId.empty() || lines.Count() == 0 || Clock == nullptr) return;
		lastReadings.Set(logId, lines);
		lastReadingsJd.Set(logId, Clock->Jd);
	}

	cs::List<cs::String> UnitySpaceApp::ArticleScanStats(const EncyclopediaArticle& a)
	{
		articleStatsLive = false;
		if (Galaxy == nullptr || Clock == nullptr) return {};
		cs::String key = a.EntityId;
		if (key.empty() && a.Category == EncyclopediaCategory::Astronomy) key = a.LoreKey; // a star visited
		if (key.empty()) return {};

		// In sight: the scan card's own readings of it, as the simulation stands now.
		Target target = Target::None();
		int subtab = 0;
		auto e = a.EntityId.empty() ? nullptr : Discoveries().Find(a.EntityId);
		if (e != nullptr && e->Kind == DiscoveryKind::Star)
		{
			// A system's readings are the charts' (no scan card is of a whole system): the log's, as they stand.
			articleStatsLive = true;
			return ArticleLiveStats(a);
		}
		if (ActiveSystem != nullptr && hud != nullptr)
		{
			if (e != nullptr && (e->Kind == DiscoveryKind::Settlement || e->Kind == DiscoveryKind::Haven))
			{
				if (e->System == ActiveSystem->Name && e->Body >= 0)
				{
					int i = 0;
					for (const auto& s : Economy()->At(e->Body))
					{
						if (s == nullptr || !s->Discovered()) continue;
						i++;
						if (s->Name == e->Name) { target = Target::OfBody(e->Body); subtab = i; break; }
					}
				}
			}
			else if (e != nullptr && DiscoveryTarget(*e, target)) subtab = e->Kind == DiscoveryKind::Pilot ? 1 : 0;
			else if (e == nullptr)
			{
				auto cat = Catalog();
				auto root = cat != nullptr && cat->Count() > 0 ? cat->Root() : nullptr;
				if (root != nullptr && LoreKeys::Body(root->Kind, ActiveSystem->Name, root->Name) == key) target = Target::OfBody(root->Index);
			}
		}
		if (!target.IsNone() && TargetEntityKey(target, subtab) == key)
		{
			auto lines = hud->ScanReadings(target, subtab);
			if (lines.Count() > 0)
			{
				RememberReadings(key, lines);
				articleStatsLive = true;
				return lines;
			}
		}

		// Out of sight: what the card last read of it, and how long ago.
		cs::List<cs::String> last;
		double jd = 0;
		if (lastReadings.TryGetValue(key, last) && lastReadingsJd.TryGetValue(key, jd))
		{
			cs::List<cs::String> lines;
			lines.Add(cs::Cat("Out of sight: last read ", ScanAgo(Clock->Jd - jd), " ago (", ScanDate(jd), ")"));
			for (const auto& l : last) lines.Add(l);
			return lines;
		}

		// Never on the scan card this session: the log's readings of it.
		auto logged = ArticleLiveStats(a);
		if (logged.Count() == 0) return logged;
		cs::List<cs::String> lines;
		lines.Add(e != nullptr ? cs::Cat("Never scanned this session: the discovery log's readings (found ", ScanDate(e->Jd), ")") : cs::String("Never scanned this session: the discovery log's readings"));
		for (const auto& l : logged) lines.Add(l);
		return lines;
	}

	cs::List<EncyclopediaSection> UnitySpaceApp::ArticleScanTabs(const EncyclopediaArticle& a)
	{
		cs::List<EncyclopediaSection> sections;
		if (a.LoreKey.empty() || (a.EntityId.empty() && a.Category != EncyclopediaCategory::Astronomy)) return sections;
		const auto& lore = GameLore();
		cs::List<cs::String> seen;
		// Every subject's tabs but the overview (the entry's own description, its "Record"): history, culture, industry...
		for (int s = 0; s <= static_cast<int>(ScanSubject::Pilot); s++)
			for (const auto& tab : ScanTabs::Of(static_cast<ScanSubject>(s)))
			{
				cs::String id = tab.Id;
				if (id == ScanTabs::Overview || seen.Contains(id)) continue;
				seen.Add(id);
				cs::String text = lore.Long(ScanTabs::Key(a.LoreKey, id), "");
				if (!text.empty()) sections.Add(EncyclopediaSection{ tab.Label, text });
			}
		return sections;
	}
}
