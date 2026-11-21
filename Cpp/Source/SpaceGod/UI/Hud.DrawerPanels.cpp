// US-T440: every HUD panel a drawer (UI/HudDrawers.h, docs/HUD.md). Each Draw* that placed itself (a fixed rect, a HudLayout
// region) now draws into the content rect its drawer's window is handed (Hud::Place), and its drawer is registered here with
// the edge it starts on, so the screen starts as it always looked:
//   - Top:     the top bar, the rewind bar (an alert), the cruise drive's readout, the news feed (X), the news ticker;
//   - Left:    the navigator, the Influence meter, and the cargo hold (its key) at the foot;
//   - Right:   at the top the hold rewind's line (an alert), the opportunities or the market (its key), the gas works, the
//              HUNTED list; at the foot the reactor and the sensors (US-T438), the target card and the selection card over them;
//   - Bottom:  from the foot up the status line, the seat line, the helm's abilities, the tractor beam's readout, a lane
//              ride's off-ramps and the interdiction line (alerts), the FTL drive, and the bottom drawer's four (US-T200);
//   - Floating, centred, opened by their keys: the bounty and contracts boards, the fleet, outfitting (and its mechlab view),
//              the trade desk, the port, the seat picker, the saves; the comms panel and the tutorial's card (shown
//              whenever they are up).
// A panel opened by a key or a flag (ShowMarket, ShowCargo, ShowPort...) keeps it: the flag opens and closes its drawer, and
// the drawer's edge tab and close button set the flag (SyncPanelFlags), so the keys, the console and the co-op paths that set
// those flags work as before.

#include "UI/Hud.h"

#include "UnitySpaceApp.h"
#include "Camera/StarTravel.h"
#include "Systems/CruiseDrive.h"

#include "World.h"

namespace SpaceGod::UI
{
	Rect Hud::Place(float w, float h)
	{
		// Drawn outside its drawer (nothing is): where it asks, centred, framed as a panel.
		if (placeId.empty()) return Panel(Rect((Screen::width - w) * 0.5f, (Screen::height - h) * 0.5f, w, h));
		drawers.Want(placeId, Vector2(w, h));
		return placeRect;
	}

	float Hud::PlaceRoom() const
	{
		return placeId.empty() ? static_cast<float>(Screen::height) : drawers.Room(placeId);
	}

	void Hud::AddPanel(DrawerSpec s, std::function<void()> body, bool* flag)
	{
		s.fit = true;
		cs::String id = s.id;
		s.draw = [this, id, body](Rect r)
		{
			cs::String was = placeId;
			Rect wasRect = placeRect;
			placeId = id;
			placeRect = r;
			body();
			placeId = was;
			placeRect = wasRect;
		};
		if (flag != nullptr)
		{
			s.keyed = true;
			s.shown = false;
			FlagLink link;
			link.id = id;
			link.flag = flag;
			flagLinks.push_back(link);
		}
		drawers.Register(std::move(s));
	}

	void Hud::SyncPanelFlags()
	{
		if (flagsSyncedFrame == Time::frameCount) return;
		flagsSyncedFrame = Time::frameCount;
		for (auto& l : flagLinks)
		{
			bool flag = *l.flag;
			bool shown = drawers.Shown(l.id);
			// The flag moved (a key, the console, co-op): its drawer follows. Else the drawer moved (its edge tab, its close
			// button): the flag follows.
			if (flag != l.lastFlag)
			{
				if (flag) drawers.Open(l.id);
				else drawers.Close(l.id);
			}
			else if (shown != l.lastShown) *l.flag = shown;
			l.lastFlag = *l.flag;
			l.lastShown = drawers.Shown(l.id);
		}
	}

	void Hud::PublishColumn()
	{
		if (Event::current().type != EventType::Repaint) return;
		auto r = drawers.LastEdgeExtent(HudDock::Edge::Right);
		HudLayout::PublishColumnLeft(r.Empty() ? static_cast<float>(Screen::width) : r.x);
	}

	void Hud::RegisterPanelDrawers()
	{
		using HudDock::Edge;
		// Whether a panel can show at all: the screen not cleared, no full screen over the view (the HUD hidden by U leaves the
		// alerts and what a key opened: the frame's bare pass sees to that).
		auto screenFree = [this]
		{
			return !app->ScreenCleared && !app->ShowSpawn && !app->ShowHelp && !app->ShowLogs && !app->ShowDiscoveries && !app->ShowEncyclopedia &&
				!app->ShowSociety && !(app->ShowGalaxyMap && !app->Travel->Active) && !(app->ShowSystemMap && !app->Travel->Active);
		};
		auto spec = [&](const char* id, const char* title, const char* shortTitle, Edge edge, Vector2 size)
		{
			DrawerSpec s;
			s.id = id;
			s.title = title;
			s.shortTitle = shortTitle;
			s.edge = edge;
			s.size = size;
			s.available = screenFree;
			return s;
		};
		auto floating = [&](const char* id, const char* title, const char* shortTitle, float y, Vector2 size, bool fromBottom = false)
		{
			auto s = spec(id, title, shortTitle, Edge::Top, size);
			s.floatY = y;
			s.floatBottom = fromBottom;
			return s;
		};

		// ---- the top edge, down from the top
		AddPanel(spec(DrawerTopBarId, "CLOCK & SCALE", "CLOCK", Edge::Top, Vector2(648, 80)), [this] { DrawTopBar(); });
		{
			auto s = spec(DrawerRewindBarId, "REWIND TIMELINE", "TIMELINE", Edge::Top, Vector2(908, 82));
			s.always = true; // an alert: after Shift+F8, while a seek replays
			s.bare = true;
			AddPanel(s, [this] { app->DrawRewindBar(); });
		}
		{
			auto s = spec(DrawerCruiseId, "CRUISE DRIVE", "CRUISE", Edge::Top, Vector2(768, 76));
			s.available = [this, screenFree] { return screenFree() && app->IsPiloting() && app->SystemCruiseActive(); };
			AddPanel(s, [this] { app->DrawSystemCruiseReadout(); });
		}
		AddPanel(spec(DrawerNewsId, "NEWS", "NEWS", Edge::Top, Vector2(768, 448)), [this] { DrawNews(); }, &app->ShowNews);
		{
			auto s = spec(DrawerTickerId, "HEADLINES", "NEWS", Edge::Top, Vector2(648, 106));
			s.available = [this, screenFree] { return screenFree() && !app->ShowNews; };
			AddPanel(s, [this] { DrawTicker(); });
		}

		// ---- the left edge, down from the top (the cargo hold up from the foot)
		AddPanel(spec(DrawerNavigatorId, "NAVIGATOR", "NAV", Edge::Left, Vector2(276, 62)), [this] { DrawNavigator(); });
		{
			auto s = spec(DrawerInfluenceId, "INFLUENCE", "INFL", Edge::Left, Vector2(276, 74));
			s.available = [this, screenFree] { return screenFree() && app->World() != nullptr; };
			AddPanel(s, [this] { app->DrawInfluenceMeter(); });
		}
		{
			auto s = spec(DrawerCargoId, "CARGO", "CARGO", Edge::Left, Vector2(268, 62));
			s.far = true; // at the foot of the left edge, where it always opened
			AddPanel(s, [this] { DrawCargo(); }, &app->ShowCargo);
		}

		// ---- the right edge: its top stack, down from the top (the foot stack: the reactor and the sensors, then the cards)
		{
			auto s = spec(DrawerHoldRewindId, "REWIND", "REWIND", Edge::Right, Vector2(500, 20));
			s.far = true;
			s.always = true; // REWIND ready / its cooldown: shown in the minimal HUD as it always was
			s.bare = true;
			s.frameless = true;
			AddPanel(s, [this] { app->DrawHoldRewindReadout(); });
		}
		{
			auto s = spec(DrawerOpportunitiesId, "OPPORTUNITIES", "JOBS", Edge::Right, Vector2(388, 120));
			s.far = true;
			s.available = [this, screenFree] { return screenFree() && !app->ShowMarket; }; // the market takes its place while open
			AddPanel(s, [this] { DrawOpportunityBoard(); });
		}
		{
			auto s = spec(DrawerMarketId, "MARKET", "MARKET", Edge::Right, Vector2(968, 480));
			s.far = true;
			AddPanel(s, [this] { DrawMarket(); }, &app->ShowMarket);
		}
		{
			auto s = spec(DrawerHarvestId, "GAS WORKS", "GAS", Edge::Right, Vector2(400, 96));
			s.far = true;
			AddPanel(s, [this] { DrawHarvest(true); });
		}
		{
			auto s = spec(DrawerHuntersId, "HUNTED", "HUNTED", Edge::Right, Vector2(388, 68));
			s.far = true;
			s.available = [this, screenFree] { return screenFree() && app->IsPiloting(); };
			AddPanel(s, [this] { DrawHunters(); });
		}
		// The foot stack, over the reactor and the sensors (registered before these): the locked hull's card, then the selection.
		AddPanel(spec(DrawerTargetId, "TARGET", "TGT", Edge::Right, Vector2(248, 300)), [this] { DrawTargetCard(); });
		{
			auto s = spec(DrawerSelectionId, "SELECTION", "SCAN", Edge::Right, Vector2(418, 300));
			s.yields = true; // it takes what the right edge leaves (its lines scroll in that)
			s.available = [this, screenFree] { return screenFree() && !app->Selected.IsNone(); };
			AddPanel(s, [this] { DrawSelection(); });
		}

		// ---- the bottom edge, up from the foot (the bottom drawer's four go last, over these)
		AddPanel(spec(DrawerStatusId, "STATUS", "STATUS", Edge::Bottom, Vector2(900, 58)), [this] { DrawStatus(); });
		AddPanel(spec(DrawerSeatId, "SEAT", "SEAT", Edge::Bottom, Vector2(500, 62)), [this] { DrawSeat(); });
		{
			auto s = spec(DrawerHelmId, "HELM", "HELM", Edge::Bottom, Vector2(900, 88));
			s.available = [this, screenFree] { return screenFree() && app->IsPiloting(); };
			AddPanel(s, [this] { DrawHelmAbilities(); });
		}
		{
			auto s = spec(DrawerTractorId, "TRACTOR BEAM", "TRACTOR", Edge::Bottom, Vector2(768, 74));
			s.available = [this, screenFree] { return screenFree() && app->IsPiloting(); };
			AddPanel(s, [this] { app->DrawTractorReadout(); });
		}
		{
			auto s = spec(DrawerLaneRideId, "HYPERLANE", "LANE", Edge::Bottom, Vector2(800, 78));
			s.always = true; // the off-ramps: an alert
			s.bare = true;
			s.available = [this, screenFree] { return screenFree() && app->RidingLane(); };
			AddPanel(s, [this] { DrawLaneRide(); });
		}
		{
			auto s = spec(DrawerInterdictionId, "INTERDICTION", "ALERT", Edge::Bottom, Vector2(800, 78));
			s.always = true; // an interdiction field ahead, an EMP, the lane beside: an alert
			s.bare = true;
			s.available = [this, screenFree] { return screenFree() && app->IsPiloting(); };
			AddPanel(s, [this] { DrawInterdiction(); });
		}
		{
			auto s = spec(DrawerFtlId, "FTL DRIVE", "FTL", Edge::Bottom, Vector2(800, 96));
			s.available = [this, screenFree] { return screenFree() && app->IsPiloting() && !app->RidingLane(); };
			AddPanel(s, [this] { DrawFtl(); });
		}

		// ---- floating, centred: the boards and screens opened by their keys
		AddPanel(floating(DrawerBountiesId, "BOUNTIES", "BOUNTY", 120, Vector2(768, 300)), [this] { DrawBounties(); }, &app->ShowBounties);
		AddPanel(floating(DrawerContractsId, "CONTRACTS", "JOBS", 120, Vector2(868, 300)), [this] { DrawContracts(); }, &app->ShowContracts);
		AddPanel(floating(DrawerFleetId, "FLEET", "FLEET", 110, Vector2(828, 520)), [this] { DrawFleet(); }, &app->ShowFleet);
		AddPanel(floating(DrawerOutfitId, "OUTFITTING", "OUTFIT", 190, Vector2(608, 400)), [this] { DrawOutfit(); }, &app->ShowOutfit);
		AddPanel(floating(DrawerTradeId, "TRADE DESK", "TRADE", 100, Vector2(868, 440)), [this] { DrawTradeDesk(); }, &app->ShowTrade);
		{
			// Its foot clear of the status line's window and the bottom edge's tabs (it sat 48 up, on the bare status line).
			auto s = floating(DrawerPortId, "PORT", "PORT", 104, Vector2(828, 140), true);
			s.available = [this, screenFree] { return screenFree() && app->DockedAt != nullptr; }; // docked only (no tab otherwise)
			AddPanel(s, [this] { DrawPort(); }, &app->ShowPort);
		}
		AddPanel(floating(DrawerPickerId, "TAKE CHARGE", "SEATS", 110, Vector2(708, 300)), [this] { DrawPicker(); }, &app->ShowPicker);
		AddPanel(floating(DrawerSavesId, "SAVE & LOAD", "SAVES", 170, Vector2(768, 300)), [this] { DrawSaves(); }, &app->ShowSaves);
		{
			auto s = floating(DrawerCommsId, "COMMS", "COMMS", 104, Vector2(988, 498), true);
			s.always = true; // shown whenever the comms panel is up (Esc or its key closes it)
			s.available = [this] { return app->CommsShown(); };
			AddPanel(s, [this] { app->DrawComms(); });
		}
		{
			auto s = floating(DrawerTutorialId, "TUTORIAL", "TUTORIAL", 104, Vector2(588, 178), true);
			s.always = true; // the guided tutorial's card, its end and the first-time hints
			s.available = [this] { return app->TutorialCardShown(); };
			AddPanel(s, [this] { app->DrawTutorialCard(); });
		}
	}
}
