// US-T438/US-T439: sg.HudLayout, the HUD's drawers and its two modes seen. Takes the controls of a practice fighter (so the
// reactor's and the sensors' drawers can show) and screenshots: the minimal HUD (the sensors' edge tab glowing red after a
// ping), HUD mode with every edge's tabs, a drawer floated off its edge, two drawers docked into a tabbed group (and the
// bottom drawers' group pulled out), a collapsed and a pinned window in HUD mode, then the minimal HUD again with only the
// pinned window left. Checks the layout saves and loads back the same, and puts the layout and the mode back as they were.
//   sg.HudLayout [shot <dir>] [quit]   (screenshots: <dir>/hudlayout-*.png; without a dir, Saved/Screenshots)

#include "UnitySpaceApp.h"

#include "GameLaunch.h"
#include "SpaceGodLog.h"
#include "UI/Hud.h"
#include "UI/HudDrawers.h"
#include "UnitySpaceAppActor.h"
#include "UnityCompat/UnityRuntime.h"
#include "UnityCompat/UnityTypes.h"

#include "Combat/CombatState.h"
#include "Ships.h"
#include "Shared/Json/JsonObject.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

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
		push(TEXT("the layout and the mode put back"), [run](UnitySpaceApp& app, double)
		{
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
		TEXT("US-T438/US-T439: sg.HudLayout [shot <dir>] [quit] takes a practice fighter's helm and screenshots the HUD's drawers: the minimal HUD, "
			"HUD mode with its edge tabs, a drawer floated, two drawers docked into a tabbed group, a collapsed and a pinned window (and the minimal "
			"HUD with only the pinned one); checks the layout saves and loads back the same; puts the layout and the mode back after."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
