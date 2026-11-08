// US-T262: the galaxy map's modes. Political is the view the map always had (GalaxyMapScreen.cpp); Trade, Exploration
// and Danger are worked out here from what the player could know, and drawn in its place. The view alone: nothing here
// changes the game or goes to a co-op peer.

#include "UI/GalaxyMapScreen.h"

#include "ActiveProfile.h"
#include "UnitySpaceApp.h"
#include "UI/Icons.h"

#include "Ships.h"
#include "World.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/MarketIntel.h"
#include "Economy/Settlement.h"
#include "Economy/TradeDesk.h"
#include "Information/GalaxyMap.h"
#include "Information/News.h"
#include "Persistence/Profiles.h"
#include "Settings/GameSetting.h"
#include "Systems/DeepSpace.h"
#include "Systems/Galaxy.h"
#include "Systems/StarDiscovery.h"
#include "Systems/SystemSources.h"
#include "Systems/VoidStations.h"
#include "Systems/VoidWatcher.h"

#include <algorithm>
#include <cmath>
#include <exception>

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		/// How long a Void Raider strike or a Watcher sign stays on the Danger map, days (the window traffic is counted over).
		constexpr double VoidMarkDays = GalaxyMap::FlowWindowDays;

		/// Word older than this is drawn at its faintest in the Trade mode, days.
		constexpr double StaleDays = 60;

		float Clamp01(double v) { return Mathf::Clamp01(static_cast<float>(v)); }

		cs::String Credits(double v) { return cs::Cat(cs::Fmt(v, "N0"), " cr/t"); }
	}

	// ---------------------------------------------------------------- the mode

	cs::String GalaxyMapScreen::ModeName(MapMode m)
	{
		switch (m)
		{
		case MapMode::Trade: return "TRADE";
		case MapMode::Exploration: return "EXPLORATION";
		case MapMode::Danger: return "DANGER";
		default: return "POLITICAL";
		}
	}

	void GalaxyMapScreen::SetMode(MapMode m)
	{
		modeLoaded = true;
		if (mode == m) return;
		mode = m;
		SaveMode();
	}

	void GalaxyMapScreen::LoadMode()
	{
		modeLoaded = true;
		try
		{
			auto profile = ActiveProfile::Current();
			if (profile == nullptr) return;
			cs::String saved = GameSettings::Setting(profile, ModeSetting);
			for (int i = 0; i <= static_cast<int>(MapMode::Danger); i++)
				if (saved == cs::Strings::ToLowerInvariant(ModeName(static_cast<MapMode>(i)))) mode = static_cast<MapMode>(i);
			cs::String good = GameSettings::Setting(profile, GoodSetting);
			for (int g = 0; g < GoodInfo::Count; g++)
				if (good == GoodInfo::Of(static_cast<Good>(g))->Name) tradeGood = g;
		}
		catch (const std::exception& e) { Debug::LogError(cs::Cat("[SpaceGod] Reading the galaxy map's mode failed: ", e.what())); }
	}

	void GalaxyMapScreen::SaveMode()
	{
		try
		{
			auto profile = ActiveProfile::Current();
			if (profile == nullptr) return;
			GameSettings::SetSetting(profile, ModeSetting, cs::Strings::ToLowerInvariant(ModeName(mode)));
			GameSettings::SetSetting(profile, GoodSetting, GoodInfo::Of(static_cast<Good>(tradeGood))->Name);
		}
		catch (const std::exception& e) { Debug::LogError(cs::Cat("[SpaceGod] Saving the galaxy map's mode failed: ", e.what())); }
	}

	// ---------------------------------------------------------------- what each mode knows

	const GalaxyMapScreen::ModeLook* GalaxyMapScreen::LookOf(const cs::Ref<StarSystem>& s) const
	{
		auto it = looks.find(s.get());
		return it == looks.end() ? nullptr : &it->second;
	}

	void GalaxyMapScreen::BuildLooks(double jd)
	{
		looks.clear();
		voidMarks.clear();
		if (model == nullptr) return;
		auto here = app->ActiveSystem;
		if (here != nullptr) visited.insert(here.get());

		for (const auto& m : model->Systems)
		{
			ModeLook look;
			auto s = m->System;
			// Trade: the house's price sheets of the system's markets (MarketIntel), but only those word of could have come
			// here (no newer than the newest news held of the system); the news' own boards for any market no sheet has.
			// The system the player is in is seen as it is.
			double knownTo = m->Here ? jd : m->Known() ? m->Report->AsOfJd : -cs::Double::MaxValue;
			cs::List<cs::String> priced;
			auto consider = [&](const cs::String& market, double price, bool spare, bool wanted, double asOf)
			{
				double age = cs::Math::Max(0.0, jd - asOf);
				if (spare && (cs::Double::IsNaN(look.Buy) || price < look.Buy)) { look.Buy = price; look.BuyAt = market; look.BuyAge = age; }
				if (wanted && (cs::Double::IsNaN(look.Sell) || price > look.Sell)) { look.Sell = price; look.SellAt = market; look.SellAge = age; }
			};
			auto desk = s->World != nullptr ? s->World->Trade : nullptr;
			if (desk != nullptr && desk->Intel != nullptr)
				for (const auto& sheet : desk->Intel->Sheets())
				{
					if (sheet->Market == nullptr || sheet->Jd > knownTo) continue;
					if (sheet->Source == "seen in port") visited.insert(s.get()); // the player was there to see it
					consider(sheet->Market->Name, sheet->Price[tradeGood], sheet->Available[tradeGood] >= 1, sheet->Buys[tradeGood], sheet->Jd);
					priced.Add(sheet->Market->Name);
				}
			if (m->Known() && !m->Here)
				for (const auto& board : m->Report->Markets)
				{
					if (board->Fallen || board->Hidden || priced.Contains(board->Name)) continue;
					consider(board->Name, board->Price[tradeGood], board->Available(static_cast<Good>(tradeGood)) >= 1, board->Demand[tradeGood] > 0, m->Report->AsOfJd);
					priced.Add(board->Name);
				}
			else if (m->Here && s->World != nullptr && s->World->Economy != nullptr)
				for (const auto& st : s->World->Economy->Settlements)
				{
					if (st->Fallen || st->Hidden || priced.Contains(st->Name)) continue;
					consider(st->Name, st->Price[tradeGood], st->Available(static_cast<Good>(tradeGood)) >= 1, st->Demand[tradeGood] > 0, jd);
					priced.Add(st->Name);
				}
			look.Sheets = priced.Count();
			look.Visited = visited.count(s.get()) > 0;
			looks[s.get()] = look;
		}

		// Danger: the Void Raiders' strikes and the Watcher's signs logged out between the stars lately. Word of one comes
		// in by the nearest system: known here once the news held of that system is as new as it (at once near here).
		if (app->Galaxy->DeepSpace != nullptr)
			for (const auto& e : app->Galaxy->DeepSpace->Events())
			{
				if (e->Kind != DeepSpaceEvent::VoidRaid && e->Kind != DeepSpaceEvent::WatcherSign) continue;
				if (jd - e->Jd > VoidMarkDays || e->Jd > jd) continue;
				cs::Ref<MapSystem> nearest = nullptr;
				double best = cs::Double::MaxValue;
				for (const auto& m : model->Systems)
				{
					double d = m->System->Origin.DistanceLy(e->Where);
					if (d < best) { best = d; nearest = m; }
				}
				if (nearest == nullptr) continue;
				bool known = nearest->Here || (nearest->Known() && nearest->Report->AsOfJd >= e->Jd);
				if (!known) continue;
				auto& look = looks[nearest->System.get()];
				if (e->Kind == DeepSpaceEvent::VoidRaid) look.VoidRaids++;
				else look.WatcherSigns++;
				voidMarks.push_back(VoidMark{ e->Where.Ly(), e->Kind, e->Jd, e->Text });
			}

		for (const auto& m : model->Systems)
		{
			auto& look = looks[m->System.get()];
			if (!m->Known()) continue;
			double score = m->Pirates == PirateActivity::High ? 5 : m->Pirates == PirateActivity::Low ? 2 : 0;
			score += 1.5 * cs::Math::Min(m->BioGlobules, 4);
			score += 2.0 * look.VoidRaids + 1.0 * look.WatcherSigns;
			if (m->Contested || m->Threats.Count() > 0) score += 2;
			look.Danger = score;
		}

		if (mode == MapMode::Exploration) BuildFrontier();
	}

	void GalaxyMapScreen::BuildFrontier()
	{
		// Rebuilt only when the charts change (a star discovered, or the stars on them streamed in).
		int key = app->Galaxy->Discoveries.Count() * 7919 + app->Galaxy->NearbyStars.Count() * 31 + app->Galaxy->Systems.Count();
		if (key == frontierFor) return;
		frontierFor = key;
		frontier.clear();
		if (voidStationNames.empty())
			for (const auto& v : VoidStations::Stations()) voidStationNames.insert(v->Name);
		// Detected, not found: an undiscovered star within twice the discovery range of anything on the charts shows as a
		// nameless contact, the edge the next voyage out would find.
		double reach = 2 * StarDiscovery::Range()->Value;
		cs::List<DVec3> charted;
		for (const auto& s : app->Galaxy->Systems) charted.Add(s->PositionLy);
		for (const auto& n : app->Galaxy->NearbyStars) charted.Add(n.PositionLy);
		double reach2 = reach * reach;
		for (const auto& u : app->Galaxy->Undiscovered())
			for (const auto& c : charted)
				if ((u.PositionLy - c).LengthSquared() <= reach2)
				{
					frontier.push_back(u.PositionLy);
					break;
				}
	}

	Color GalaxyMapScreen::ModeTint(const cs::Ref<MapSystem>& m, bool& draw) const
	{
		draw = false;
		auto look = LookOf(m->System);
		switch (mode)
		{
		case MapMode::Trade:
		{
			if (look == nullptr) return Dim;
			double price = tradeSell ? look->Sell : look->Buy;
			if (cs::Double::IsNaN(price)) return Dim;
			double basis = GoodInfo::Of(static_cast<Good>(tradeGood))->BasePrice;
			// Cheap is green and dear red where buying; the other way round where selling.
			float t = Clamp01((price / cs::Math::Max(basis, 0.01) - 0.6) / 1.0);
			if (tradeSell) t = 1 - t;
			draw = true;
			return Color::Lerp(Color(0.3f, 0.95f, 0.45f), Color(1.0f, 0.35f, 0.3f), t);
		}
		case MapMode::Exploration:
			if (m->Here) { draw = true; return Accent; }
			if (look != nullptr && look->Visited) { draw = true; return VisitedColor; }
			if (m->System->IsVoidBase) { draw = true; return VoidColor; }
			if (m->Known()) { draw = true; return HeardColor; }
			return Unknown;
		case MapMode::Danger:
		{
			if (!m->Known() || look == nullptr) return Unknown;
			if (look->Danger <= 0) { draw = true; return Color(0.45f, 0.8f, 0.6f); }
			draw = true;
			return Color::Lerp(Color(1.0f, 0.85f, 0.3f), Color(1.0f, 0.22f, 0.2f), Clamp01(look->Danger / 10));
		}
		default:
			return Dim;
		}
	}

	cs::String GalaxyMapScreen::ModeSub(const cs::Ref<MapSystem>& m) const
	{
		auto look = LookOf(m->System);
		switch (mode)
		{
		case MapMode::Trade:
		{
			if (look == nullptr || look->Sheets == 0) return m->Known() || m->Here ? "no prices for it" : "no news";
			double price = tradeSell ? look->Sell : look->Buy;
			if (cs::Double::IsNaN(price)) return tradeSell ? "nobody buys it" : "none to spare";
			double age = tradeSell ? look->SellAge : look->BuyAge;
			return cs::Cat(tradeSell ? "sells " : "buy ", Credits(price), "  ·  ", MarketIntel::AgeText(age));
		}
		case MapMode::Exploration:
			if (m->Here) return "you are here";
			if (look != nullptr && look->Visited) return m->System->IsVoidBase ? "visited  ·  void station" : "visited";
			if (m->System->IsVoidBase) return m->Known() ? "void station  ·  heard from" : "void station";
			return m->Known() ? "heard from, never visited" : "charted, never heard from";
		case MapMode::Danger:
		{
			if (!m->Known() || look == nullptr) return "no news";
			if (look->Danger <= 0) return "quiet, as last heard";
			cs::List<cs::String> parts;
			if (m->Pirates != PirateActivity::None) parts.Add(m->Pirates == PirateActivity::High ? "raiders, heavy" : "raiders");
			if (m->BioGlobules > 0) parts.Add(cs::Cat("Bio Mass ", m->BioGlobules));
			if (look->VoidRaids > 0) parts.Add(cs::Cat(look->VoidRaids, " void strike", look->VoidRaids == 1 ? "" : "s"));
			if (look->WatcherSigns > 0) parts.Add(cs::Cat(look->WatcherSigns, " dread sign", look->WatcherSigns == 1 ? "" : "s"));
			if (m->Contested || m->Threats.Count() > 0) parts.Add("war");
			return cs::Strings::Join("  ·  ", parts);
		}
		default:
			return cs::String();
		}
	}

	// ---------------------------------------------------------------- drawing

	void GalaxyMapScreen::DrawModeTabs(Rect r)
	{
		float x = r.x + 150;
		for (int i = 0; i <= static_cast<int>(MapMode::Danger); i++)
		{
			auto m = static_cast<MapMode>(i);
			float w = m == MapMode::Exploration ? 108.0f : 88.0f;
			if (GUI::Button(Rect(x, r.y + 5, w, 20), ModeName(m), mode == m ? buttonOn : button)) SetMode(m);
			x += w + 4;
		}
	}

	void GalaxyMapScreen::DrawModeUnderlay(Rect local, double jd)
	{
		// A wash round each system in the mode's colour, as faint as the word of it is old (Trade).
		for (const auto& m : model->Systems)
		{
			bool draw = false;
			auto c = ModeTint(m, draw);
			if (!draw) continue;
			auto p = Project(local, m->System->PositionLy);
			float alpha = 0.32f;
			if (mode == MapMode::Trade)
			{
				auto look = LookOf(m->System);
				double age = look == nullptr ? StaleDays : tradeSell ? look->SellAge : look->BuyAge;
				alpha = 0.42f * Mathf::Lerp(1.0f, 0.3f, Clamp01(age / StaleDays));
			}
			else if (mode == MapMode::Exploration && !m->Here && !(LookOf(m->System) && LookOf(m->System)->Visited)) alpha = 0.16f;
			Disc(p, mode == MapMode::Danger ? 30 + 2.5f * Mathf::Min(static_cast<float>(LookOf(m->System) ? LookOf(m->System)->Danger : 0), 10.0f) : 34, Color(c.r, c.g, c.b, alpha));
			if (mode == MapMode::Exploration && m->System->IsVoidBase) Ring(p, 18, VoidColor, 2.0f);
			if (mode == MapMode::Danger && m->BioGlobules > 0)
			{
				float pulse = 0.5f + 0.5f * Mathf::Sin(Time::unscaledTime * 3);
				for (int i = 0; i < cs::Math::Min(m->BioGlobules, 4); i++)
					Disc(p + Vector2(14 + 7.0f * i, 14), 3.2f, Color(BioColor.r, BioColor.g, BioColor.b, 0.6f + 0.35f * pulse));
			}
		}

		if (mode == MapMode::Exploration)
		{
			// Void stations among the plain stars on the charts, and the undiscovered frontier just past them.
			for (const auto& n : chartStars)
				if (voidStationNames.count(n.Name) > 0)
				{
					auto p = Project(local, n.PositionLy);
					if (local.Contains(p)) Ring(p, 8, VoidColor, 1.5f);
				}
			float pulse = 0.5f + 0.5f * Mathf::Sin(Time::unscaledTime * 2);
			for (const auto& f : frontier)
			{
				auto p = Project(local, f);
				if (!local.Contains(p)) continue;
				Ring(p, 6 + 2 * pulse, Color(FrontierColor.r, FrontierColor.g, FrontierColor.b, 0.35f + 0.3f * pulse), 1.2f);
				Label(p + Vector2(7, -9), "?", Color(FrontierColor.r, FrontierColor.g, FrontierColor.b, 0.8f), true);
			}
		}

		if (mode == MapMode::Danger)
			for (const auto& v : voidMarks)
			{
				auto p = Project(local, v.Ly);
				if (!local.Contains(p)) continue;
				float fresh = 1.0f - Clamp01((jd - v.Jd) / VoidMarkDays);
				if (v.Kind == DeepSpaceEvent::VoidRaid)
				{
					// A cross where the ghosts struck.
					auto c = Color(FrontColor.r, FrontColor.g, FrontColor.b, 0.35f + 0.6f * fresh);
					Line(p + Vector2(-6, -6), p + Vector2(6, 6), c, 2.5f);
					Line(p + Vector2(-6, 6), p + Vector2(6, -6), c, 2.5f);
				}
				else
				{
					// A ring where it looked.
					auto c = Color(WatcherColor.r, WatcherColor.g, WatcherColor.b, 0.3f + 0.55f * fresh);
					Ring(p, 7, c, 1.5f);
					Disc(p, 2.2f, c);
				}
			}
	}

	void GalaxyMapScreen::LegendHelp(Rect r, float x, float w)
	{
		auto old = GUI::contentColor;
		GUI::contentColor = Dim;
		GUI::Label(Rect(x, r.yMax() - 92, w, 84),
			"Click a star to inspect it, double-click to travel.\nAt the helm: plot an FTL course to any star.\nDrag to turn, shift-drag to pan, wheel to zoom.\n[ or Esc closes the map.", small);
		GUI::contentColor = old;
	}

	void GalaxyMapScreen::DrawModeLegend(Rect r, double jd)
	{
		GUI::Box(r, GUIContent::none(), Panel());
		float y = r.y + 10;
		float x = r.x + 12, w = r.width - 24;
		switch (mode)
		{
		case MapMode::Trade:
		{
			Heading(y, x, w, "GOOD");
			if (GUI::Button(Rect(x, y, 26, 22), "<", button)) { tradeGood = (tradeGood + GoodInfo::Count - 1) % GoodInfo::Count; builtAt = -10.0f; SaveMode(); }
			if (GUI::Button(Rect(x + w - 26, y, 26, 22), ">", button)) { tradeGood = (tradeGood + 1) % GoodInfo::Count; builtAt = -10.0f; SaveMode(); }
			const auto& info = GoodInfo::Of(static_cast<Good>(tradeGood));
			GUI::Label(Rect(x + 34, y + 2, w - 68, 22), cs::Strings::ToUpperInvariant(info->Name), title);
			y += 26;
			Line(y, x, w, cs::Cat("base price ", Credits(info->BasePrice)), Dim);
			if (GUI::Button(Rect(x, y, w * 0.5f - 2, 22), "WHERE TO BUY", tradeSell ? button : buttonOn)) tradeSell = false;
			if (GUI::Button(Rect(x + w * 0.5f + 2, y, w * 0.5f - 2, 22), "WHERE TO SELL", tradeSell ? buttonOn : button)) tradeSell = true;
			y += 28;
			Heading(y, x, w, "KEY");
			Swatch(y, x, w, Color(0.3f, 0.95f, 0.45f), tradeSell ? "pays well over the base price" : "cheap, with some to spare", false);
			Swatch(y, x, w, Color(1.0f, 0.35f, 0.3f), tradeSell ? "pays under the base price" : "dear", false);
			Swatch(y, x, w, Color(0.6f, 0.6f, 0.6f, 0.4f), "fainter: older word of the prices", false);
			y += 6;
			Heading(y, x, w, tradeSell ? "BEST KNOWN SALES" : "BEST KNOWN BUYS");
			// The five best systems, as the player knows them.
			std::vector<std::pair<double, cs::Ref<MapSystem>>> best;
			for (const auto& m : model->Systems)
			{
				auto look = LookOf(m->System);
				if (look == nullptr) continue;
				double price = tradeSell ? look->Sell : look->Buy;
				if (!cs::Double::IsNaN(price)) best.emplace_back(tradeSell ? -price : price, m);
			}
			std::sort(best.begin(), best.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
			if (best.empty()) Line(y, x, w, "no prices known for it anywhere", Dim);
			for (size_t i = 0; i < best.size() && i < 6; i++)
			{
				auto look = LookOf(best[i].second->System);
				Line(y, x, w, cs::Cat(best[i].second->System->Name, ": ", Credits(std::abs(best[i].first)), " at ", tradeSell ? look->SellAt : look->BuyAt,
					"  ·  ", MarketIntel::AgeText(tradeSell ? look->SellAge : look->BuyAge)), Color::white());
			}
			y += 6;
			Line(y, x, w, "Prices are the house's sheets (seen in port, the captains, brokers) and the boards the news brings, no newer than word from each system.", Dim);
			break;
		}
		case MapMode::Exploration:
		{
			int visitedN = 0, heard = 0, voids = 0;
			for (const auto& m : model->Systems)
			{
				auto look = LookOf(m->System);
				if (m->Here || (look != nullptr && look->Visited)) visitedN++;
				if (m->Known()) heard++;
				if (m->System->IsVoidBase) voids++;
			}
			Heading(y, x, w, "THE CHARTS");
			Line(y, x, w, cs::Cat(model->Systems.Count(), " systems on the gates, ", visitedN, " visited, ", heard, " heard from"), Color::white());
			Line(y, x, w, cs::Cat(app->Galaxy->NearbyStars.Count(), " plain stars charted, ", app->Galaxy->Discoveries.Count(), " discovered since the start"), Color::white());
			Line(y, x, w, cs::Cat(frontier.size(), " undiscovered contacts at the edge of the charts"), FrontierColor);
			if (voids > 0) Line(y, x, w, cs::Cat(voids, voids == 1 ? " void station" : " void stations", " on the gates"), VoidColor);
			y += 6;
			Heading(y, x, w, "KEY");
			Swatch(y, x, w, Accent, "you are here", false);
			Swatch(y, x, w, VisitedColor, "visited", false);
			Swatch(y, x, w, HeardColor, "heard from, never visited", false);
			Swatch(y, x, w, Unknown, "charted, never heard from", false);
			Swatch(y, x, w, VoidColor, "void station (ringed)", false);
			Swatch(y, x, w, FrontierColor, "detected, undiscovered (?)", true);
			y += 6;
			Line(y, x, w, cs::Cat("A star is discovered when a lookout comes within ", cs::Fmt(StarDiscovery::Range()->Value, "0.#"),
				" ly of it; the contacts shown are within twice that of the charts."), Dim);
			break;
		}
		case MapMode::Danger:
		{
			Heading(y, x, w, "KEY");
			Swatch(y, x, w, Color(0.45f, 0.8f, 0.6f), "quiet, as last heard", false);
			Swatch(y, x, w, Color(1.0f, 0.85f, 0.3f), "some danger", false);
			Swatch(y, x, w, Color(1.0f, 0.22f, 0.2f), "heavy danger", false);
			Swatch(y, x, w, PirateColor, "raiders about (the hull icon)", false);
			Swatch(y, x, w, BioColor, "Bio Mass loose (the green dots)", false);
			Swatch(y, x, w, FrontColor, "Void Raider strike in the void (cross)", false);
			Swatch(y, x, w, WatcherColor, "a sign of the Watcher (ring)", false);
			Swatch(y, x, w, FrontColor, "war front or shut gate", true);
			y += 6;
			int raids = 0, signs = 0;
			for (const auto& v : voidMarks) (v.Kind == DeepSpaceEvent::VoidRaid ? raids : signs)++;
			Heading(y, x, w, cs::Cat("THE VOID, LAST ", cs::Fmt(VoidMarkDays, "0"), " DAYS"));
			Line(y, x, w, cs::Cat(raids, raids == 1 ? " Void Raider strike" : " Void Raider strikes", " heard of"), raids > 0 ? FrontColor : Dim);
			Line(y, x, w, cs::Cat(signs, signs == 1 ? " sign" : " signs", " of the Watcher heard of"), signs > 0 ? WatcherColor : Dim);
			auto ship = app->Piloted.Ship;
			if (ship != nullptr)
			{
				double dread = VoidWatcher::DreadAt(*ship, jd);
				int stage = VoidWatcher::StageOf(dread);
				Line(y, x, w, cs::Cat("Your crew's dread: ", VoidWatcher::StageName(stage), " (", cs::Fmt(dread * 100, "0"), "%)"), stage > 0 ? WatcherColor : Dim);
			}
			y += 6;
			Line(y, x, w, "Raiders, Bio Mass and war are as the newest news of each system had them; a strike or sign in the void is known once word from its nearest system is as new.", Dim);
			break;
		}
		default:
			break;
		}
		LegendHelp(r, x, w);
	}

	void GalaxyMapScreen::DrawModeDetail(float& y, float x, float w, const cs::Ref<MapSystem>& m, double jd)
	{
		auto look = LookOf(m->System);
		switch (mode)
		{
		case MapMode::Trade:
		{
			const auto& info = GoodInfo::Of(static_cast<Good>(tradeGood));
			Heading(y, x, w, cs::Cat("PRICES OF ", cs::Strings::ToUpperInvariant(info->Name)));
			if (look == nullptr || look->Sheets == 0) Line(y, x, w, m->Known() || m->Here ? "No prices known here." : "Nothing heard from it: no prices.", Dim);
			else
			{
				if (!cs::Double::IsNaN(look->Buy))
					Line(y, x, w, cs::Cat("Buy at ", look->BuyAt, ": ", Credits(look->Buy), "  ·  ", MarketIntel::AgeText(look->BuyAge)), Color(0.3f, 0.95f, 0.45f));
				else Line(y, x, w, "None to spare at any market known.", Dim);
				if (!cs::Double::IsNaN(look->Sell))
					Line(y, x, w, cs::Cat("Sell at ", look->SellAt, ": ", Credits(look->Sell), "  ·  ", MarketIntel::AgeText(look->SellAge)), Color(1.0f, 0.82f, 0.4f));
				else Line(y, x, w, "No market known here buys it.", Dim);
				Line(y, x, w, cs::Cat(look->Sheets, look->Sheets == 1 ? " market" : " markets", " priced; base price ", Credits(info->BasePrice)), Dim);
			}
			y += 6;
			break;
		}
		case MapMode::Exploration:
		{
			Heading(y, x, w, "EXPLORATION");
			bool been = m->Here || (look != nullptr && look->Visited);
			Line(y, x, w, been ? "Visited." : m->Known() ? "Heard from, never visited." : "Charted, never heard from.", been ? VisitedColor : m->Known() ? HeardColor : Unknown);
			if (m->System->IsVoidBase) Line(y, x, w, "A void station: a relay in the dark, no state's law.", VoidColor);
			if (!cs::Double::IsNaN(m->System->OpenedJd)) Line(y, x, w, cs::Cat("Opened to settlement ", Days(jd - m->System->OpenedJd), " ago."), Dim);
			int near = 0;
			for (const auto& f : frontier)
				if ((f - m->System->PositionLy).Length() <= 2 * StarDiscovery::Range()->Value) near++;
			if (near > 0) Line(y, x, w, cs::Cat(near, near == 1 ? " undiscovered contact" : " undiscovered contacts", " nearby."), FrontierColor);
			y += 6;
			break;
		}
		case MapMode::Danger:
		{
			Heading(y, x, w, "DANGER");
			if (!m->Known()) { Line(y, x, w, "Nothing heard from it.", Dim); y += 6; break; }
			if (look == nullptr || look->Danger <= 0) Line(y, x, w, "Quiet, as last heard.", Color(0.45f, 0.8f, 0.6f));
			if (m->Pirates != PirateActivity::None)
				Line(y, x, w, cs::Cat("Raiders: ", m->PirateShips, " armed, ", m->Havens, m->Havens == 1 ? " haven, " : " havens, ", m->Raids, m->Raids == 1 ? " raid" : " raids"), PirateColor);
			if (m->BioGlobules > 0) Line(y, x, w, cs::Cat("Bio Mass: ", m->BioGlobules, m->BioGlobules == 1 ? " bloom" : " blooms", " loose"), BioColor);
			if (look != nullptr && look->VoidRaids > 0) Line(y, x, w, cs::Cat("Void Raiders: ", look->VoidRaids, look->VoidRaids == 1 ? " strike" : " strikes", " in the void nearby"), FrontColor);
			if (look != nullptr && look->WatcherSigns > 0) Line(y, x, w, cs::Cat("The Watcher: ", look->WatcherSigns, look->WatcherSigns == 1 ? " sign" : " signs", " in the void nearby"), WatcherColor);
			if (m->Contested || m->Threats.Count() > 0) Line(y, x, w, "War: fought over, or ports under siege or blockade", FrontColor);
			Line(y, x, w, cs::Cat("As of ", Age(m->AgeDays)), Dim);
			y += 6;
			break;
		}
		default:
			break;
		}
	}
}
