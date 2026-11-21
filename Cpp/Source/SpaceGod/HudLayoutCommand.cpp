// US-T438/US-T439: sg.HudLayout, the HUD's drawers and its two modes seen. Takes the controls of a practice fighter (so the
// reactor's and the sensors' drawers can show) and screenshots: the minimal HUD (the sensors' edge tab glowing red after a
// ping), HUD mode with every edge's tabs, a drawer floated off its edge, two drawers docked into a tabbed group (and the
// bottom drawers' group pulled out), a collapsed and a pinned window in HUD mode, then the minimal HUD again with only the
// pinned window left. Checks the layout saves and loads back the same, and puts the layout and the mode back as they were.
// US-T440 (every panel a drawer): then, the layout reset and Earth selected, HUD mode with every default drawer (the top bar,
// the navigator, the selection card, the status line...), and each modal-like drawer opened by its flag as its key does:
// the market, the cargo hold, the fleet and the port (docked at Earth's port), and the market again in the minimal HUD (a
// drawer opened by its key shows there too); checks each shows, where it should, and closes with its flag.
// US-T441 (layout presets): then each built-in preset (Default, Combat, Commerce, Piracy, Exploration) in HUD mode and in the
// minimal HUD, a preset saved, a built-in saved over and reset, Layout 1, the Layouts drawer pulled out (and kept out as another
// preset is used from it), a preset renamed and deleted, and "switch layout with the situation" docked at Earth (Commerce) and
// undocked (the chosen one back); the profile's presets and the option are put back as they were.
//   sg.HudLayout [shot <dir>] [quit]   (screenshots: <dir>/hudlayout-*.png; without a dir, Saved/Screenshots)

#include "UnitySpaceApp.h"

#include "GameLaunch.h"
#include "PlayerOptions.h"
#include "Settings/GameSetting.h"
#include "SpaceGodLog.h"
#include "UI/Hud.h"
#include "UI/HudDrawers.h"
#include "UnitySpaceAppActor.h"
#include "UnityCompat/UnityRuntime.h"
#include "UnityCompat/UnityTypes.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/CombatState.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Ships.h"
#include "Shared/Json/JsonObject.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

using SpaceGod::UnitySpaceApp;
using SpaceGod::UI::Hud;
using SpaceGod::UI::HudDrawers;
namespace HudDock = SpaceGod::UI::HudDock;

namespace
{
	UnitySpaceApp* App()
	{
		if (!GEngine) return nullptr;
		for (const FWorldContext& c : GEngine->GetWorldContexts())
		{
			if (c.WorldType != EWorldType::Game && c.WorldType != EWorldType::PIE) continue;
			auto* actor = AUnitySpaceAppActor::Find(c.World());
			if (actor != nullptr && actor->GetApp() != nullptr) return actor->GetApp();
		}
		return nullptr;
	}

	FString T(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

	HudDrawers* Drawers(UnitySpaceApp& app) { return app.CoopHud() != nullptr ? &app.CoopHud()->Drawers() : nullptr; }

	struct Run
	{
		using Step = std::function<bool(UnitySpaceApp&, double)>;
		std::vector<std::pair<FString, Step>> steps;
		size_t at = 0;
		double stepStart = -1, started = FPlatformTime::Seconds();
		bool askedNewGame = false, quit = false;
		/// The layout and the mode before the run, put back at the end.
		cs::String savedLayout;
		bool savedMode = false, saved = false;
		/// US-T441: the profile's presets, the preset shown and the situation option before the run, put back at the end.
		cs::String savedPresets, savedShown;
		bool savedSituation = false, savedPresetsSet = false;
		int failures = 0;
		size_t setUp = static_cast<size_t>(-1);
		bool First()
		{
			if (setUp == at) return false;
			setUp = at;
			return true;
		}

		bool Tick()
		{
			auto* app = App();
			double now = FPlatformTime::Seconds();
			if (app == nullptr)
			{
				if (!askedNewGame && now - started > 2.0)
				{
					askedNewGame = true;
					SpaceGod::GameLaunch::NewGame();
				}
				return now - started > 180.0 ? Finish(TEXT("no game came up")) : true;
			}
			if (at >= steps.size()) return Finish(nullptr);
			if (stepStart < 0) stepStart = now;
			if (steps[at].second(*app, now - stepStart))
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.HudLayout: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 60.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		void Restore()
		{
			auto* app = App();
			if (!saved || app == nullptr) return;
			saved = false;
			app->HudMode = savedMode;
			if (savedPresetsSet && app->CoopHud() != nullptr)
			{
				savedPresetsSet = false;
				SpaceGod::PlayerOptions::LayoutWithSituation()->Set(savedSituation);
				app->CoopHud()->RestoreLayoutsJson(savedPresets);
				app->CoopHud()->ApplyLayout(savedShown, false);
			}
			if (auto* d = Drawers(*app)) d->Load(SpaceGod::Shared::Json::JsonObject::Parse(savedLayout));
		}

		bool Finish(const TCHAR* failure)
		{
			Restore();
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.HudLayout: %s"), failure); }
			else if (failures > 0) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.HudLayout: done, %d check(s) failed"), failures); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.HudLayout: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}

		void Expect(bool ok, const TCHAR* what)
		{
			if (!ok) failures++;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.HudLayout: %s %s"), ok ? TEXT("ok  ") : TEXT("FAIL"), what);
		}
	};

	void Start(const TArray<FString>& args)
	{
		FString dir;
		bool quit = false;
		for (int i = 0; i < args.Num(); i++)
		{
			if (args[i].Equals(TEXT("shot"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) dir = args[++i];
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
		}
		if (dir.IsEmpty()) dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"));
		auto run = std::make_shared<Run>();
		run->quit = quit;
		auto push = [&run](FString name, Run::Step step) { run->steps.push_back({ name, std::move(step) }); };
		// The sensors lit, as after a ping, so their drawer raises its urgent alert (held while the shots are taken).
		auto lit = [](UnitySpaceApp& app)
		{
			if (app.Piloted.Ship != nullptr && app.Piloted.Ship->Combat != nullptr) app.Piloted.Ship->Combat->PingLitS = 20;
		};
		auto shot = [&](const TCHAR* tag)
		{
			FString file = FPaths::Combine(dir, FString::Printf(TEXT("hudlayout-%s.png"), tag));
			push(FString::Printf(TEXT("shot %s"), *file), [file, lit](UnitySpaceApp& app, double)
			{
				lit(app);
				FScreenshotRequest::RequestScreenshot(file, true, false);
				return true;
			});
			push(TEXT("written"), [lit](UnitySpaceApp& app, double t) { lit(app); return t >= 1.0; });
		};
		auto log = [](UnitySpaceApp& app, const TCHAR* what)
		{
			auto* d = Drawers(app);
			if (d == nullptr) return;
			for (const auto& w : d->Windows())
			{
				FString members;
				for (const auto& m : w.members) members += (members.IsEmpty() ? TEXT("") : TEXT("+")) + T(m);
				UE_LOG(LogSpaceGod, Display, TEXT("sg.HudLayout: [%s] %s: %s%s%s%s at %.0f,%.0f %.0fx%.0f%s"), what, *members,
					w.shown ? TEXT("shown") : TEXT("closed"), w.collapsed ? TEXT(", collapsed") : TEXT(""), w.pinned ? TEXT(", pinned") : TEXT(""),
					w.docked ? *FString::Printf(TEXT(", docked %s #%d"), UTF8_TO_TCHAR(HudDock::EdgeName(w.edge)), w.order) : TEXT(", floating"),
					w.rect.x, w.rect.y, w.rect.width, w.rect.height, w.drawn ? TEXT("") : TEXT(" (not drawn)"));
			}
		};

		push(TEXT("game up"), [](UnitySpaceApp&, double t) { return t >= 6.0; });
		push(TEXT("at a practice fighter's helm, the layout reset, the minimal HUD"), [run](UnitySpaceApp& app, double t)
		{
			auto* d = Drawers(app);
			if (d == nullptr) return false;
			if (run->First())
			{
				if (!run->saved)
				{
					run->savedLayout = d->Save()->ToJson();
					run->savedMode = app.HudMode;
					run->saved = true;
				}
				d->ResetLayout();
				app.HudMode = false;
				if (!app.IsPiloting()) app.SpawnPracticeFighter();
			}
			return app.IsPiloting() && t >= 3.0;
		});
		push(TEXT("the layout saves and loads back the same"), [run](UnitySpaceApp& app, double)
		{
			auto* d = Drawers(app);
			cs::String a = d->Save()->ToJson();
			bool loaded = d->Load(SpaceGod::Shared::Json::JsonObject::Parse(a));
			cs::String b = d->Save()->ToJson();
			run->Expect(loaded && a == b, TEXT("save -> load -> save gives the same JSON"));
			run->Expect(!d->Load(SpaceGod::Shared::Json::JsonObject::Parse("{\"nothing\":1}")), TEXT("a JSON with no layout is refused"));
			UE_LOG(LogSpaceGod, Display, TEXT("sg.HudLayout: layout %s"), *T(a));
			return true;
		});
		push(TEXT("minimal: the sensors lit"), [lit](UnitySpaceApp& app, double t) { lit(app); return t >= 1.5; });
		push(TEXT("minimal checked"), [run, log](UnitySpaceApp& app, double)
		{
			auto* d = Drawers(app);
			log(app, TEXT("minimal"));
			run->Expect(!d->Visible(Hud::DrawerReactorId) && !d->Visible(Hud::DrawerSensorsId), TEXT("minimal: the reactor and the sensors hidden"));
			return true;
		});
		shot(TEXT("minimal"));
		push(TEXT("HUD mode on"), [run, lit](UnitySpaceApp& app, double t)
		{
			if (run->First()) app.HudMode = true;
			lit(app);
			return t >= 1.5;
		});
		push(TEXT("HUD mode checked"), [run, log](UnitySpaceApp& app, double)
		{
			auto* d = Drawers(app);
			log(app, TEXT("hud mode"));
			run->Expect(d->Visible(Hud::DrawerReactorId) && d->Visible(Hud::DrawerSensorsId), TEXT("HUD mode: the reactor and the sensors shown"));
			run->Expect(!d->Visible(Hud::DrawerNavigationId), TEXT("HUD mode: the bottom drawers closed to their tabs"));
			return true;
		});
		shot(TEXT("hudmode"));
		push(TEXT("the sensors dragged off the right edge to float mid-screen"), [run, lit](UnitySpaceApp& app, double t)
		{
			auto* d = Drawers(app);
			if (run->First())
			{
				float sw = static_cast<float>(SpaceGod::Screen::width), sh = static_cast<float>(SpaceGod::Screen::height);
				d->Float(Hud::DrawerSensorsId, SpaceGod::Rect(sw * 0.5f - 160, sh * 0.42f, 320, 96));
			}
			lit(app);
			return t >= 1.0;
		});
		push(TEXT("floating checked"), [run, log](UnitySpaceApp& app, double)
		{
			auto* d = Drawers(app);
			log(app, TEXT("floating"));
			int w = d->WindowOf(Hud::DrawerSensorsId);
			run->Expect(w >= 0 && !d->Windows()[w].docked && d->Visible(Hud::DrawerSensorsId), TEXT("floating: the sensors float and show"));
			return true;
		});
		shot(TEXT("floating"));
		push(TEXT("the sensors docked onto the reactor (a tabbed group), the bottom drawers pulled out"), [run, lit](UnitySpaceApp& app, double t)
		{
			auto* d = Drawers(app);
			if (run->First())
			{
				d->Join(Hud::DrawerSensorsId, Hud::DrawerReactorId);
				d->Open(Hud::DrawerShipsId);
			}
			lit(app);
			return t >= 1.0;
		});
		push(TEXT("group checked"), [run, log](UnitySpaceApp& app, double)
		{
			auto* d = Drawers(app);
			log(app, TEXT("group"));
			int a = d->WindowOf(Hud::DrawerSensorsId), b = d->WindowOf(Hud::DrawerReactorId);
			run->Expect(a >= 0 && a == b && d->Windows()[a].members.size() == 2 && d->Windows()[a].tabbed, TEXT("group: the reactor and the sensors one window of tabs"));
			run->Expect(d->Visible(Hud::DrawerSensorsId) && !d->Visible(Hud::DrawerReactorId), TEXT("group: the sensors' tab shown, the reactor's behind it"));
			run->Expect(d->Visible(Hud::DrawerShipsId), TEXT("group: the bottom group open on Ships"));
			return true;
		});
		shot(TEXT("group"));
		push(TEXT("the reactor's group collapsed, the bottom group pinned"), [run, lit](UnitySpaceApp& app, double t)
		{
			auto* d = Drawers(app);
			if (run->First())
			{
				d->SetCollapsed(Hud::DrawerReactorId, true);
				d->SetPinned(Hud::DrawerShipsId, true);
				d->Open(Hud::DrawerNoticesId);
			}
			lit(app);
			return t >= 1.0;
		});
		push(TEXT("collapsed and pinned checked"), [run, log](UnitySpaceApp& app, double)
		{
			auto* d = Drawers(app);
			log(app, TEXT("collapsed+pinned"));
			int w = d->WindowOf(Hud::DrawerReactorId);
			run->Expect(w >= 0 && d->Windows()[w].collapsed && d->Windows()[w].drawn && d->Windows()[w].rect.height <= HudDrawers::TitleH + 0.5f,
				TEXT("collapsed: the reactor's window is its title bar only"));
			run->Expect(d->Pinned(Hud::DrawerNoticesId) && d->Visible(Hud::DrawerNoticesId), TEXT("pinned: the bottom group pinned, on Notices"));
			return true;
		});
		shot(TEXT("collapsed-pinned"));
		push(TEXT("back to the minimal HUD: only the pinned window stays"), [run, lit](UnitySpaceApp& app, double t)
		{
			if (run->First()) app.HudMode = false;
			lit(app);
			return t >= 1.5;
		});
		push(TEXT("pinned in minimal checked"), [run, log](UnitySpaceApp& app, double)
		{
			auto* d = Drawers(app);
			log(app, TEXT("minimal pinned"));
			run->Expect(d->Visible(Hud::DrawerNoticesId), TEXT("minimal: the pinned window shows"));
			int w = d->WindowOf(Hud::DrawerReactorId);
			run->Expect(w >= 0 && !d->Windows()[w].drawn, TEXT("minimal: the unpinned window is gone"));
			return true;
		});
		shot(TEXT("minimal-pinned"));
		// ---- US-T440: every panel a drawer
		auto earth = [](UnitySpaceApp& app) -> int
		{
			auto body = (*app.Catalog())["Earth"];
			return body != nullptr ? body->Index : 0;
		};
		push(TEXT("US-T440: the layout reset, Earth selected, HUD mode with every default drawer"), [run, lit, earth](UnitySpaceApp& app, double t)
		{
			auto* d = Drawers(app);
			if (run->First())
			{
				d->ResetLayout();
				app.HudMode = true;
				app.Select(SpaceGod::Target::OfBody(earth(app)));
			}
			lit(app);
			return t >= 2.0;
		});
		push(TEXT("every default drawer checked"), [run, log](UnitySpaceApp& app, double)
		{
			auto* d = Drawers(app);
			log(app, TEXT("all drawers"));
			auto docked = [d](const char* id, HudDock::Edge edge)
			{
				int w = d->WindowOf(id);
				return w >= 0 && d->Windows()[w].docked && d->Windows()[w].edge == edge && d->Visible(id);
			};
			run->Expect(docked(Hud::DrawerTopBarId, HudDock::Edge::Top), TEXT("all: the top bar at the top edge"));
			run->Expect(docked(Hud::DrawerNavigatorId, HudDock::Edge::Left), TEXT("all: the navigator at the left edge"));
			run->Expect(docked(Hud::DrawerStatusId, HudDock::Edge::Bottom), TEXT("all: the status line at the bottom edge"));
			run->Expect(docked(Hud::DrawerSelectionId, HudDock::Edge::Right), TEXT("all: the selection card at the right edge"));
			run->Expect(docked(Hud::DrawerReactorId, HudDock::Edge::Right) && docked(Hud::DrawerSensorsId, HudDock::Edge::Right), TEXT("all: the reactor and the sensors at the right edge"));
			auto top = d->RectOf(Hud::DrawerTopBarId), nav = d->RectOf(Hud::DrawerNavigatorId), status = d->RectOf(Hud::DrawerStatusId);
			float sw = static_cast<float>(SpaceGod::Screen::width), sh = static_cast<float>(SpaceGod::Screen::height);
			run->Expect(top.y < 20 && SpaceGod::Mathf::Abs(top.x + top.width * 0.5f - sw * 0.5f) < 2, TEXT("all: the top bar centred at the top, as it was"));
			run->Expect(nav.x < 40 && nav.y < 20, TEXT("all: the navigator in the top left corner, as it was"));
			run->Expect(status.yMax() > sh - 20, TEXT("all: the status line on the screen's foot, as it was"));
			auto sel = d->RectOf(Hud::DrawerSelectionId), react = d->RectOf(Hud::DrawerReactorId);
			run->Expect(sel.yMax() <= react.y + 0.5f, TEXT("all: the selection card stands over the reactor, as it did"));
			run->Expect(!d->Visible(Hud::DrawerMarketId) && !d->Visible(Hud::DrawerCargoId), TEXT("all: the market and the cargo hold closed until their keys"));
			return true;
		});
		shot(TEXT("all-drawers"));
		struct Modal { const TCHAR* tag; const char* id; bool UnitySpaceApp::* flag; };
		const Modal modals[] = {
			{ TEXT("market"), Hud::DrawerMarketId, &UnitySpaceApp::ShowMarket },
			{ TEXT("cargo"), Hud::DrawerCargoId, &UnitySpaceApp::ShowCargo },
			{ TEXT("fleet"), Hud::DrawerFleetId, &UnitySpaceApp::ShowFleet },
		};
		for (const auto& m : modals)
		{
			push(FString::Printf(TEXT("US-T440: the %s opened by its flag"), m.tag), [run, lit, m](UnitySpaceApp& app, double t)
			{
				if (run->First()) app.*(m.flag) = true;
				lit(app);
				return t >= 1.5;
			});
			push(FString::Printf(TEXT("%s checked"), m.tag), [run, log, m](UnitySpaceApp& app, double)
			{
				auto* d = Drawers(app);
				log(app, m.tag);
				run->Expect(d->Visible(m.id), *FString::Printf(TEXT("%s: its drawer shows, opened by its flag"), m.tag));
				if (FCString::Strcmp(m.tag, TEXT("market")) == 0)
					run->Expect(!d->Visible(Hud::DrawerOpportunitiesId), TEXT("market: it takes the opportunities' place"));
				if (FCString::Strcmp(m.tag, TEXT("fleet")) == 0)
				{
					auto r = d->RectOf(m.id);
					int w = d->WindowOf(m.id);
					run->Expect(w >= 0 && !d->Windows()[w].docked && SpaceGod::Mathf::Abs(r.x + r.width * 0.5f - SpaceGod::Screen::width * 0.5f) < 2,
						TEXT("fleet: floating, centred across the screen, as it was"));
				}
				return true;
			});
			shot(m.tag);
			push(FString::Printf(TEXT("the %s closed by its flag"), m.tag), [run, m](UnitySpaceApp& app, double t)
			{
				if (run->First()) app.*(m.flag) = false;
				return t >= 0.5;
			});
			push(FString::Printf(TEXT("%s closed checked"), m.tag), [run, m](UnitySpaceApp& app, double)
			{
				run->Expect(!Drawers(app)->Visible(m.id), *FString::Printf(TEXT("%s: closed with its flag"), m.tag));
				return true;
			});
		}
		push(TEXT("US-T440: docked at Earth's port (the port screen opens)"), [run, lit, earth](UnitySpaceApp& app, double t)
		{
			if (run->First())
				for (const auto& s : app.Economy()->At(earth(app)))
					if (s->Discovered() && app.Dock(s)) break;
			lit(app);
			return t >= 2.0;
		});
		push(TEXT("port checked"), [run, log](UnitySpaceApp& app, double)
		{
			auto* d = Drawers(app);
			log(app, TEXT("port"));
			run->Expect(app.DockedAt != nullptr && app.ShowPort, TEXT("port: docked, the port screen's flag up"));
			run->Expect(d->Visible(Hud::DrawerPortId), TEXT("port: its drawer shows"));
			return true;
		});
		shot(TEXT("port"));
		push(TEXT("undocked, the market opened and the minimal HUD"), [run, lit](UnitySpaceApp& app, double t)
		{
			if (run->First())
			{
				app.Undock(cs::String());
				app.ShowMarket = true;
				app.HudMode = false;
			}
			lit(app);
			return t >= 1.5;
		});
		push(TEXT("minimal with the market checked"), [run, log](UnitySpaceApp& app, double)
		{
			auto* d = Drawers(app);
			log(app, TEXT("minimal market"));
			run->Expect(!app.ShowPort && !d->Visible(Hud::DrawerPortId), TEXT("undocked: the port's drawer gone with its flag"));
			run->Expect(d->Visible(Hud::DrawerMarketId), TEXT("minimal: the market, opened by its key, shows"));
			run->Expect(!d->Visible(Hud::DrawerTopBarId) && !d->Visible(Hud::DrawerNavigatorId) && !d->Visible(Hud::DrawerStatusId),
				TEXT("minimal: the top bar, the navigator and the status line hidden"));
			return true;
		});
		shot(TEXT("minimal-market"));
		// ---- US-T441: the layout presets
		push(TEXT("US-T441: the presets kept aside, undocked, HUD mode"), [run](UnitySpaceApp& app, double t)
		{
			auto* hud = app.CoopHud();
			if (hud == nullptr) return false;
			if (run->First())
			{
				run->savedPresets = hud->LayoutsJson();
				run->savedShown = hud->ShownLayout();
				run->savedSituation = SpaceGod::PlayerOptions::LayoutWithSituation()->Value;
				run->savedPresetsSet = true;
				SpaceGod::PlayerOptions::LayoutWithSituation()->Set(false);
				app.ShowMarket = false;
				app.HudMode = true;
			}
			return t >= 0.5;
		});
		for (const auto& name : Hud::BuiltinLayouts())
		{
			FString tag = T(name).ToLower();
			push(FString::Printf(TEXT("US-T441: the %s preset on screen"), *T(name)), [run, lit, name](UnitySpaceApp& app, double t)
			{
				if (run->First())
				{
					app.HudMode = true;
					app.CoopHud()->ApplyLayout(name, false);
				}
				lit(app);
				return t >= 1.5;
			});
			push(FString::Printf(TEXT("%s checked"), *tag), [run, log, name, tag](UnitySpaceApp& app, double)
			{
				auto* d = Drawers(app);
				log(app, *tag);
				run->Expect(app.CoopHud()->ShownLayout() == name, *FString::Printf(TEXT("%s: the preset shown"), *tag));
				auto leftDocked = [d](const char* id)
				{
					int w = d->WindowOf(id);
					return w >= 0 && d->Windows()[w].docked && d->Windows()[w].edge == HudDock::Edge::Left && d->Windows()[w].members.size() == 1;
				};
				if (name == "Default") run->Expect(!d->Pinned(Hud::DrawerReactorId) && !d->Pinned(Hud::DrawerNavigatorId), TEXT("default: nothing pinned"));
				if (name == "Combat")
					run->Expect(d->Pinned(Hud::DrawerReactorId) && d->Pinned(Hud::DrawerSensorsId) && d->Pinned(Hud::DrawerTargetId) && d->Pinned(Hud::DrawerHelmId) && !d->Shown(Hud::DrawerOpportunitiesId),
						TEXT("combat: the reactor, the sensors, the target and the helm pinned, the opportunities closed"));
				if (name == "Commerce")
					run->Expect(d->Pinned(Hud::DrawerNavigatorId) && d->Pinned(Hud::DrawerOpportunitiesId) && d->Pinned(Hud::DrawerTopBarId), TEXT("commerce: the navigator, the opportunities and the clock pinned"));
				if (name == "Piracy")
					run->Expect(d->Pinned(Hud::DrawerSensorsId) && d->Pinned(Hud::DrawerHuntersId) && leftDocked(Hud::DrawerShipsId) && d->Visible(Hud::DrawerShipsId),
						TEXT("piracy: the sensors and the hunters pinned, the ships in the area down the left edge"));
				if (name == "Exploration")
					run->Expect(d->Pinned(Hud::DrawerNavigatorId) && leftDocked(Hud::DrawerNavigationId) && d->Visible(Hud::DrawerNavigationId),
						TEXT("exploration: the navigator pinned, the Navigation list down the left edge"));
				run->Expect(!d->Visible(Hud::DrawerMarketId), *FString::Printf(TEXT("%s: the market (opened by its key) left closed"), *tag));
				return true;
			});
			shot(*FString::Printf(TEXT("preset-%s"), *tag));
			push(FString::Printf(TEXT("the %s preset in the minimal HUD"), *T(name)), [run, lit](UnitySpaceApp& app, double t)
			{
				if (run->First()) app.HudMode = false;
				lit(app);
				return t >= 1.5;
			});
			shot(*FString::Printf(TEXT("preset-%s-minimal"), *tag));
		}
		push(TEXT("US-T441: a preset saved, a built-in saved over and reset, Layout 1"), [run](UnitySpaceApp& app, double)
		{
			auto* hud = app.CoopHud();
			auto* d = Drawers(app);
			hud->ApplyLayout("Piracy", false);
			d->SetPinned(Hud::DrawerNavigatorId, true);
			run->Expect(hud->SaveLayout("  My hunt  "), TEXT("presets: saved as My hunt"));
			auto names = hud->LayoutNames();
			run->Expect(std::find(names.begin(), names.end(), cs::String("My hunt")) != names.end() && hud->ActiveLayout() == "My hunt", TEXT("presets: My hunt listed (trimmed) and chosen"));
			hud->ApplyLayout("Default", false);
			run->Expect(hud->ApplyLayout("My hunt", false) && d->Pinned(Hud::DrawerNavigatorId) && d->Pinned(Hud::DrawerHuntersId), TEXT("presets: My hunt comes back as saved"));
			run->Expect(!hud->RenameLayout("My hunt", "Combat") && !hud->DeleteLayout("Combat"), TEXT("presets: a built-in's name is not taken, a built-in not deleted"));
			hud->ApplyLayout("Combat", false);
			d->SetPinned(Hud::DrawerNavigatorId, true);
			run->Expect(hud->SaveLayout("Combat") && hud->LayoutOverridden("Combat"), TEXT("presets: Combat saved over (the player's copy)"));
			run->Expect(hud->ResetLayoutPreset("Combat") && !hud->LayoutOverridden("Combat") && !d->Pinned(Hud::DrawerNavigatorId), TEXT("presets: Combat reset to the built-in (on screen too)"));
			hud->SetLayoutSlot(0, "My hunt");
			run->Expect(hud->LayoutInSlot(0) == "My hunt" && hud->LayoutInSlot(1) == "Combat", TEXT("presets: My hunt on Layout 1, Combat still on 2"));
			hud->LayoutKey(0);
			run->Expect(hud->ShownLayout() == "My hunt", TEXT("presets: Layout 1 puts My hunt on screen"));
			return true;
		});
		push(TEXT("US-T441: the Layouts drawer pulled out"), [run, lit](UnitySpaceApp& app, double t)
		{
			if (run->First())
			{
				app.HudMode = true;
				Drawers(app)->Open(Hud::DrawerLayoutsId);
			}
			lit(app);
			return t >= 1.5;
		});
		push(TEXT("layouts drawer checked"), [run, log](UnitySpaceApp& app, double)
		{
			auto* d = Drawers(app);
			log(app, TEXT("layouts"));
			auto r = d->RectOf(Hud::DrawerLayoutsId);
			run->Expect(d->Visible(Hud::DrawerLayoutsId) && r.x < 60, TEXT("layouts: the drawer shows, on the left edge"));
			return true;
		});
		shot(TEXT("layouts"));
		push(TEXT("US-T441: another preset used from the drawer keeps it open"), [run, lit](UnitySpaceApp& app, double t)
		{
			if (run->First()) app.CoopHud()->ApplyLayout("Exploration");
			lit(app);
			return t >= 1.5;
		});
		push(TEXT("layouts kept checked"), [run](UnitySpaceApp& app, double)
		{
			run->Expect(Drawers(app)->Visible(Hud::DrawerLayoutsId) && app.CoopHud()->ActiveLayout() == "Exploration", TEXT("layouts: Exploration chosen, the drawer still out"));
			return true;
		});
		shot(TEXT("layouts-exploration"));
		push(TEXT("US-T441: My hunt renamed, then deleted"), [run](UnitySpaceApp& app, double)
		{
			auto* hud = app.CoopHud();
			run->Expect(hud->RenameLayout("My hunt", "Night hunt") && hud->LayoutInSlot(0) == "Night hunt", TEXT("presets: renamed, its slot follows"));
			run->Expect(hud->DeleteLayout("Night hunt") && hud->LayoutInSlot(0).empty(), TEXT("presets: deleted, its slot free"));
			auto names = hud->LayoutNames();
			run->Expect(names.size() == Hud::BuiltinLayouts().size(), TEXT("presets: only the built-ins left"));
			return true;
		});
		push(TEXT("US-T441: switch with the situation on, docked at Earth"), [run, lit, earth](UnitySpaceApp& app, double t)
		{
			if (run->First())
			{
				Drawers(app)->Close(Hud::DrawerLayoutsId);
				app.CoopHud()->ApplyLayout("Default");
				SpaceGod::PlayerOptions::LayoutWithSituation()->Set(true);
				for (const auto& s : app.Economy()->At(earth(app)))
					if (s->Discovered() && app.Dock(s)) break;
			}
			lit(app);
			return t >= 2.5;
		});
		push(TEXT("situation docked checked"), [run, log](UnitySpaceApp& app, double)
		{
			log(app, TEXT("situation docked"));
			run->Expect(app.DockedAt != nullptr && app.CoopHud()->ShownLayout() == "Commerce" && app.CoopHud()->ActiveLayout() == "Default",
				TEXT("situation: docked, Commerce shown, Default still the chosen one"));
			return true;
		});
		shot(TEXT("situation-docked"));
		push(TEXT("US-T441: undocked, the chosen layout back"), [run, lit](UnitySpaceApp& app, double t)
		{
			if (run->First()) app.Undock(cs::String());
			lit(app);
			return t >= 2.5;
		});
		push(TEXT("situation undocked checked"), [run](UnitySpaceApp& app, double)
		{
			run->Expect(app.DockedAt == nullptr && app.CoopHud()->ShownLayout() == "Default", TEXT("situation: undocked, Default back"));
			SpaceGod::PlayerOptions::LayoutWithSituation()->Set(false);
			return true;
		});
		push(TEXT("the layout and the mode put back"), [run](UnitySpaceApp& app, double)
		{
			app.ShowMarket = false;
			if (app.Piloted.Ship != nullptr && app.Piloted.Ship->Combat != nullptr) app.Piloted.Ship->Combat->PingLitS = 0;
			run->Restore();
			return true;
		});
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	FAutoConsoleCommand GHudLayoutCommand(
		TEXT("sg.HudLayout"),
		TEXT("US-T438/US-T439/US-T440: sg.HudLayout [shot <dir>] [quit] takes a practice fighter's helm and screenshots the HUD's drawers: the minimal HUD, "
			"HUD mode with its edge tabs, a drawer floated, two drawers docked into a tabbed group, a collapsed and a pinned window (and the minimal "
			"HUD with only the pinned one), HUD mode with every default drawer, the market, the cargo hold, the fleet and the port opened (and the "
			"market in the minimal HUD); US-T441: each built-in layout preset (HUD mode and minimal), the Layouts drawer, a preset saved, "
			"renamed and deleted, a built-in saved over and reset, Layout 1, and the layout following the situation (docked: Commerce); "
			"checks the layout saves and loads back the same; puts the layout, the mode and the profile's presets back after."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
