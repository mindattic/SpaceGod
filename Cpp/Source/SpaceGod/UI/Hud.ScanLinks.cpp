// US-T272: the scan card's keyword links into the Encyclopedia (declared in UI/Hud.Panels.inl). What the card shows (the
// thing itself, its system, star and world, its flag, hull and pilot, the goods and industries of a port, and its notable
// features) is a row of links, each opening its article; scanning unlocks the faction, hull, good and industry articles it
// names. Display only: nothing here changes the simulation.

#include "UI/Hud.h"

#include "UnitySpaceApp.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ships.h"
#include "World.h"
#include "Comms/Pilots.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Industry.h"
#include "Economy/Settlement.h"
#include "Infrastructure/Infrastructure.h"
#include "Lore/Encyclopedia.h"
#include "Lore/LoreKeys.h"
#include "Systems/Galaxy.h"

#include <algorithm>
#include <vector>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		/// The most links one card shows (two or three rows).
		constexpr int MaxScanLinks = 10;

		/// The word a body's kind is known by (a feature keyword).
		const char* BodyKindWord(BodyKind k)
		{
			switch (k)
			{
				case BodyKind::Star: return "star";
				case BodyKind::Planet: return "planet";
				case BodyKind::DwarfPlanet: return "dwarf planet";
				case BodyKind::Moon: return "moon";
				case BodyKind::Station: return "station";
				case BodyKind::Asteroid: return "asteroid";
				default: return "";
			}
		}

		const char* SettlementKindWord(SettlementKind k)
		{
			switch (k)
			{
				case SettlementKind::Colony: return "colony";
				case SettlementKind::Station: return "station";
				case SettlementKind::MiningOutpost: return "mining";
				case SettlementKind::Haven: return "haven";
			}
			return "";
		}

		const char* StructureKindWord(StructureKind k)
		{
			switch (k)
			{
				case StructureKind::Shipyard: return "shipyard";
				case StructureKind::Factory: return "factory";
				case StructureKind::Habitat: return "habitat";
				case StructureKind::Telescope: return "telescope";
				case StructureKind::Depot: return "propellant";
				case StructureKind::Relay: return "relay";
				default: return "";
			}
		}

		cs::String SettlementLogId(const cs::String& system, const Settlement& s)
		{
			return cs::Cat(s.Kind == SettlementKind::Haven ? "haven" : "settlement", "|", system, "|", s.Name);
		}
	}

	void Hud::RefreshScanLinks(const Target& t, int subtab, const SelectionCard& card)
	{
		// Every couple of seconds (a port's goods move on), else only when the card shows something else.
		if (t == scanLinksFor && subtab == scanLinksSubtab && ++scanLinksFrames < 120) return;
		scanLinksFor = t;
		scanLinksSubtab = subtab;
		scanLinksFrames = 0;
		scanLinks.Clear();
		if (t.IsNone() || t.IsUnit() || app->Galaxy == nullptr || app->ActiveSystem == nullptr || app->Catalog() == nullptr) return;

		// The card's readings, kept for the thing's entry once it is out of sight.
		cs::String key = app->TargetEntityKey(t, subtab);
		if (!key.empty()) app->RememberReadings(key, ReadingsOf(card.lines));

		const auto& book = app->EncyclopediaBook();
		auto& known = app->Knowledge();
		const cs::String sys = app->ActiveSystem->Name;
		auto& cat = *app->Catalog();
		cs::List<cs::String> ids;
		cs::List<cs::String> words; // feature keywords: an article whose title holds the word
		auto add = [&](const cs::String& id) { if (!id.empty() && !ids.Contains(id)) ids.Add(id); };
		auto addLog = [&](const cs::String& logId) { add(app->EntityArticleId(logId)); };
		auto addBody = [&](int index)
		{
			if (index < 0 || index >= cat.Count()) return;
			const auto& b = cat[index];
			add(Encyclopedia::MakeId(EncyclopediaCategory::Astronomy, LoreKeys::Body(b->Kind, sys, b->Name)));
		};
		auto addFaction = [&](const cs::Ref<Faction>& f) { if (f != nullptr) add(Encyclopedia::FactionId(f->Name)); };
		auto addSettlement = [&](const cs::Ref<Settlement>& s)
		{
			if (s == nullptr) return;
			addLog(SettlementLogId(sys, *s));
			addFaction(s->Owner);
		};
		auto addMarket = [&](const Settlement& s)
		{
			// The goods it holds most of, and what its industry makes.
			std::vector<std::pair<double, int>> stock;
			for (int g = 0; g < GoodInfo::Count && g < s.Stock.Count(); g++)
				if (s.Stock[g] >= 1) stock.emplace_back(s.Stock[g], g);
			std::sort(stock.begin(), stock.end(), [](const auto& a, const auto& b) { return a.first > b.first || (a.first == b.first && a.second < b.second); });
			for (size_t i = 0; i < stock.size() && i < 3; i++) add(Encyclopedia::GoodId(static_cast<Good>(stock[i].second)));
			int industries = 0;
			for (const auto& f : s.Facilities)
				if (f != nullptr && f->Recipe != nullptr && industries < 2)
				{
					cs::String id = Encyclopedia::IndustryId(f->Recipe->Kind);
					if (!ids.Contains(id)) { add(id); industries++; }
				}
			if (s.HasShipyard()) words.Add("shipyard");
			words.Add(SettlementKindWord(s.Kind));
		};
		cs::String selfId;

		if (t.IsBody())
		{
			if (t.Body < 0 || t.Body >= cat.Count()) return;
			const auto& b = cat[t.Body];
			if (subtab > 0 && subtab < card.subtabs.Count() && card.subtabs[subtab].settlement != nullptr)
			{
				// A port on this world: the port, its flag, its world and system, its market.
				const auto& s = card.subtabs[subtab].settlement;
				addSettlement(s);
				addBody(t.Body);
				addMarket(*s);
			}
			else
			{
				addBody(t.Body);
				if (b->ParentIndex > 0) addBody(b->ParentIndex);
				for (int i = 1; i < card.subtabs.Count(); i++) addSettlement(card.subtabs[i].settlement);
				words.Add(BodyKindWord(b->Kind));
				if (b->RingOuterKm > 0) words.Add("ring");
			}
			if (ids.Count() > 0) selfId = ids[0];
			if (cat.Count() > 0 && cat.Root() != b) addBody(cat.Root()->Index);
		}
		else if (t.IsShip() && t.Ship != nullptr)
		{
			const auto& ship = t.Ship;
			if (subtab > 0 && card.pilot != nullptr)
			{
				// The pilot's page: the pilot, their side, the hull they fly.
				addLog(app->TargetEntityKey(t, subtab));
				if (ids.Count() > 0) selfId = ids[0];
				addFaction(card.pilot->Faction);
			}
			if (ship->Spec != nullptr) add(Encyclopedia::HullId(ship->Spec->Class));
			addFaction(ship->Faction);
			if (subtab == 0 && card.pilot != nullptr) addLog(app->TargetEntityKey(t, 1));
			if (ship->Spec != nullptr && ship->Spec->IsMilitary) words.Add("warship");
		}
		else if (t.IsStructure() && t.Structure != nullptr)
		{
			const auto& s = t.Structure;
			addFaction(s->Owner());
			addSettlement(s->Settlement);
			if (s->Kind == StructureKind::Factory) add(Encyclopedia::IndustryId(s->Industry));
			addBody(s->Body);
			words.Add(StructureKindWord(s->Kind));
		}
		else if (t.IsRing() && t.Ring != nullptr)
		{
			const auto& r = t.Ring;
			addSettlement(r->Settlement);
			addFaction(r->Owner());
			addBody(r->Body);
			words.Add("docking ring");
		}
		addLog(cs::Cat("star|", sys, "|", sys));

		// Notable features: an article named for the word (a guide, a good, a hull...), the first by title.
		for (const auto& w : words)
		{
			if (w.empty()) continue;
			cs::String low = cs::Strings::ToLowerInvariant(w);
			for (const auto& id : book.ArticlesFor(w, &known))
			{
				auto a = book.Find(id);
				if (a == nullptr || a->Category == EncyclopediaCategory::Lore || Encyclopedia::FindNoCase(a->Title, low) == cs::String::npos) continue;
				add(id);
				break;
			}
		}

		// Scanning unlocks what the card names (a Prose text stays the books' and the log's to unlock).
		for (const auto& id : ids)
		{
			if (scanLinks.Count() >= MaxScanLinks) break;
			auto a = book.Find(id);
			if (a == nullptr) continue;
			if (!Encyclopedia::IsKnown(*a, &known))
			{
				if (a->Category == EncyclopediaCategory::Lore) continue;
				known.Unlock(id);
			}
			ScanLink link;
			link.label = a->Title;
			link.id = id;
			link.self = id == selfId;
			scanLinks.Add(link);
		}
	}

	cs::List<Rect> Hud::ScanLinkRects(float w)
	{
		cs::List<Rect> rects;
		float x = 0, y = 0;
		for (const auto& l : scanLinks)
		{
			float bw = Mathf::Min(w, button->CalcSize(GUIContent(l.label)).x + 12);
			if (x > 0 && x + bw > w)
			{
				x = 0;
				y += 24;
			}
			rects.Add(Rect(x, y, bw, 22));
			x += bw + 4;
		}
		return rects;
	}
}
