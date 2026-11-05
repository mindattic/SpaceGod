// US-T136: sg.Shine, the co-op/PvP shine tried solo: a fake remote holder on a nearby hull (and its screenshots at both
// scales), plus its dev-menu entries.

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "GameLaunch.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Target.h"
#include "UI/CheatMenu.h"
#include "UnitySpaceAppActor.h"
#include "Views/EmbodiedShine.h"

#include "Combat/Battle.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace SpaceGod::Core;
using SpaceGod::Target;
using SpaceGod::UnitySpaceApp;
using SpaceGod::Views::EmbodiedShine;

namespace
{
	UnitySpaceApp* RunningApp()
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

	FString Utf(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

	/// <summary>The hull to fake: what is selected (a ship, or a unit in a fight), else the ship nearest the camera that this
	/// machine's player is not flying.</summary>
	Target Pick(UnitySpaceApp& app)
	{
		if (app.Selected.IsShip() && app.Selected.Ship != nullptr && !app.Selected.Ship->Destroyed) return app.Selected;
		if (app.Selected.IsUnit() && app.Selected.Unit != nullptr && app.Selected.Unit->Present()) return app.Selected;
		auto world = app.World();
		if (world == nullptr) return Target::None();
		auto eye = app.SpaceCamera->ScenePosition;
		Target best = Target::None();
		double bestD = cs::Double::PositiveInfinity;
		for (const auto& s : world->Fleet->Ships())
		{
			if (s->Destroyed || !app.Fleet()->IsPresent(s) || s->Controller()->Phase == ShipPhase::Landed) continue;
			if (!app.Piloted.IsNone() && app.Piloted.Ship == s) continue;
			double d = (app.TargetScene(Target::OfShip(s)) - eye).Length();
			if (d < bestD)
			{
				bestD = d;
				best = Target::OfShip(s);
			}
		}
		return best;
	}

	/// <summary>A big hull for the pictures: a parked warship, else whatever <see cref="Pick"/> finds.</summary>
	Target Showpiece(UnitySpaceApp& app)
	{
		auto world = app.World();
		if (world != nullptr)
			for (const auto& s : world->Fleet->Ships())
				if (!s->Destroyed && app.Fleet()->IsPresent(s) && s->Phase == ShipPhase::Parked && s->Spec != nullptr && s->Spec->IsMilitary
					&& (app.Piloted.IsNone() || app.Piloted.Ship != s))
					return Target::OfShip(s);
		return Pick(app);
	}

	cs::String NameOf(const Target& t)
	{
		if (t.IsShip() && t.Ship != nullptr) return t.Ship->Name;
		if (t.IsUnit() && t.Unit != nullptr) return t.Unit->Name();
		return "nothing";
	}

	/// <summary>Adds (or moves) the test holder of <paramref name="seat"/> to <paramref name="t"/>.</summary>
	void Fake(UnitySpaceApp& app, const Target& t, int seat, bool dark)
	{
		for (int i = app.FakeHolders.Count() - 1; i >= 0; i--)
			if (app.FakeHolders[i].Seat == seat) app.FakeHolders.RemoveAt(i);
		UnitySpaceApp::FakeHolder f;
		f.Seat = seat;
		f.What = t;
		f.Dark = dark;
		app.FakeHolders.Add(f);
	}

	void Report(UnitySpaceApp& app, const TCHAR* when)
	{
		for (const auto& s : app.ShiningNow())
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Shine %s: P%d on %s%s, %s%s"), when, s.Player.Seat + 1, *Utf(NameOf(s.What)), s.Fake ? TEXT(" (fake)") : TEXT(""),
				s.Dark ? TEXT("dark") : TEXT("lit"), s.Shows() ? TEXT(", shines") : TEXT(", beyond visual range: no shine"));
		if (app.embodiedShine)
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Shine %s: %d near, %d far, %d hidden"), when, app.embodiedShine->NearCount, app.embodiedShine->FarCount,
				app.embodiedShine->HiddenCount);
	}

	/// <summary>One run of the screenshot script, stepped once a frame (each step returns true when done), as sg.BioMass's.</summary>
	struct Run
	{
		using Step = std::function<bool(UnitySpaceApp&, double)>;
		std::vector<std::pair<FString, Step>> steps;
		size_t at = 0;
		double stepStart = -1;
		double started = FPlatformTime::Seconds();
		bool askedNewGame = false;
		bool quit = false;

		bool Tick()
		{
			auto* app = RunningApp();
			double now = FPlatformTime::Seconds();
			if (app == nullptr)
			{
				if (!askedNewGame && now - started > 2.0)
				{
					askedNewGame = true;
					UE_LOG(LogSpaceGod, Display, TEXT("sg.Shine: no game running; starting a new one"));
					SpaceGod::GameLaunch::NewGame();
				}
				if (now - started > 180.0) return Finish(TEXT("no game came up"));
				return true;
			}
			if (at >= steps.size()) return Finish(nullptr);
			if (stepStart < 0) stepStart = now;
			if (steps[at].second(*app, now - stepStart))
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Shine: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 240.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			EmbodiedShine::FreezePhase = -1.0f;
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Shine: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Shine: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	void Shots(const FString& dir, bool quit)
	{
		auto run = std::make_shared<Run>();
		run->quit = quit;
		auto target = std::make_shared<Target>(Target::None());
		auto wait = [](double seconds) { return [seconds](UnitySpaceApp&, double t) { return t >= seconds; }; };
		auto scale = [](bool game)
		{
			return [game](UnitySpaceApp& app, double)
			{
				double goal = game ? 1.0 : 0.0;
				if (app.ScaleGoal != goal) app.ToggleScale();
				return !app.ScaleAnimating();
			};
		};
		auto focus = [target](double radii)
		{
			return [target, radii](UnitySpaceApp& app, double)
			{
				app.SpaceCamera->FocusOn(*target, radii, 12.0f);
				return true;
			};
		};
		auto shot = [dir](const TCHAR* file)
		{
			return [dir, file](UnitySpaceApp& app, double)
			{
				Report(app, file);
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(dir, file), false, false);
				return true;
			};
		};
		auto dark = [](bool on)
		{
			return [on](UnitySpaceApp& app, double)
			{
				for (auto& f : app.FakeHolders) f.Dark = on;
				return true;
			};
		};
		run->steps.push_back({ TEXT("game up"), wait(3.0) });
		run->steps.push_back({ TEXT("game scale"), scale(true) });
		run->steps.push_back({ TEXT("hull picked, faked as P2's, real time"), [target](UnitySpaceApp& app, double)
		{
			*target = Showpiece(app);
			if (target->IsNone()) throw std::runtime_error("no hull in this system to shine");
			app.Clock->WarpIndex = 0;
			Fake(app, *target, 1, false);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Shine: P2 holds %s (fake)"), *Utf(NameOf(*target)));
			return true;
		} });
		// The flare's crest in every picture (a frozen beat), up close and from far enough that the hull is a few pixels.
		auto phases = [](float p) { return [p](UnitySpaceApp&, double) { EmbodiedShine::FreezePhase = p; return true; }; };
		const double Near = 3.2, Far = 900, Beyond = 9000;
		for (int pass = 0; pass < 2; pass++)
		{
			bool game = pass == 0;
			const TCHAR* tag = game ? TEXT("game") : TEXT("real");
			if (!game) run->steps.push_back({ TEXT("realistic scale"), scale(false) });
			run->steps.push_back({ FString::Printf(TEXT("%s: close"), tag), focus(Near) });
			run->steps.push_back({ TEXT("settled"), wait(4.0) });
			run->steps.push_back({ TEXT("flare frozen"), phases(0.25f) });
			run->steps.push_back({ TEXT("held"), wait(0.4) });
			run->steps.push_back({ TEXT("shot close"), shot(game ? TEXT("shine-game-close.png") : TEXT("shine-real-close.png")) });
			run->steps.push_back({ TEXT("written"), wait(1.0) });
			run->steps.push_back({ TEXT("sweep mid-hull"), phases(0.12f) });
			run->steps.push_back({ TEXT("held"), wait(0.4) });
			run->steps.push_back({ TEXT("shot sweep"), shot(game ? TEXT("shine-game-sweep.png") : TEXT("shine-real-sweep.png")) });
			run->steps.push_back({ TEXT("written"), wait(1.0) });
			run->steps.push_back({ TEXT("running again"), phases(-1.0f) });
			run->steps.push_back({ FString::Printf(TEXT("%s: far"), tag), focus(Far) });
			run->steps.push_back({ TEXT("settled"), wait(4.5) });
			run->steps.push_back({ TEXT("flare frozen"), phases(0.25f) });
			run->steps.push_back({ TEXT("held"), wait(0.4) });
			run->steps.push_back({ TEXT("shot far"), shot(game ? TEXT("shine-game-far.png") : TEXT("shine-real-far.png")) });
			run->steps.push_back({ TEXT("written"), wait(1.0) });
			// Running dark this far out (450 lengths) it is still inside visual range (about 3,400 lengths): it still shines.
			run->steps.push_back({ TEXT("dark"), dark(true) });
			run->steps.push_back({ TEXT("held"), wait(0.4) });
			run->steps.push_back({ TEXT("shot far, dark, in visual range"), shot(game ? TEXT("shine-game-far-dark.png") : TEXT("shine-real-far-dark.png")) });
			run->steps.push_back({ TEXT("written"), wait(1.0) });
			run->steps.push_back({ TEXT("lit"), dark(false) });
			// Past visual range (4,500 lengths): lit, the glint still shows; dark, nothing does.
			run->steps.push_back({ FString::Printf(TEXT("%s: beyond visual range"), tag), focus(Beyond) });
			run->steps.push_back({ TEXT("settled"), wait(4.5) });
			run->steps.push_back({ TEXT("shot beyond, lit"), shot(game ? TEXT("shine-game-beyond-lit.png") : TEXT("shine-real-beyond-lit.png")) });
			run->steps.push_back({ TEXT("written"), wait(1.0) });
			run->steps.push_back({ TEXT("dark"), dark(true) });
			run->steps.push_back({ TEXT("held"), wait(0.4) });
			run->steps.push_back({ TEXT("shot beyond, dark"), shot(game ? TEXT("shine-game-beyond-dark.png") : TEXT("shine-real-beyond-dark.png")) });
			run->steps.push_back({ TEXT("written"), wait(1.0) });
			run->steps.push_back({ TEXT("lit"), dark(false) });
			run->steps.push_back({ TEXT("running again"), phases(-1.0f) });
		}
		run->steps.push_back({ TEXT("fake cleared"), [](UnitySpaceApp& app, double)
		{
			app.FakeHolders.Clear();
			return true;
		} });

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	void Start(const TArray<FString>& args)
	{
		FString verb = args.Num() > 0 ? args[0].ToLower() : FString(TEXT("fake"));
		if (verb == TEXT("shots"))
		{
			if (args.Num() < 2)
			{
				UE_LOG(LogSpaceGod, Warning, TEXT("sg.Shine shots <dir> [quit]"));
				return;
			}
			bool quit = false;
			for (int i = 2; i < args.Num(); i++)
				if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
			Shots(args[1], quit);
			return;
		}
		auto* app = RunningApp();
		if (app == nullptr || app->World() == nullptr)
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Shine: no game running"));
			return;
		}
		if (verb == TEXT("clear"))
		{
			app->FakeHolders.Clear();
			app->ShowToast("Test shine cleared");
			return;
		}
		if (verb == TEXT("list"))
		{
			Report(*app, TEXT("list"));
			return;
		}
		// fake [seat=N] [dark]
		int seat = 1;
		bool dark = false;
		for (int i = 0; i < args.Num(); i++)
		{
			if (args[i].StartsWith(TEXT("seat="), ESearchCase::IgnoreCase)) seat = FMath::Clamp(FCString::Atoi(*args[i].Mid(5)) - 1, 0, 3);
			else if (args[i].Equals(TEXT("dark"), ESearchCase::IgnoreCase)) dark = true;
		}
		if (seat == app->LocalPlayerId().Seat) seat = (seat + 1) % 4; // your own seat never shines for you
		auto t = Pick(*app);
		if (t.IsNone())
		{
			app->ShowToast("Nothing near to shine: select a ship or a unit in a fight");
			return;
		}
		Fake(*app, t, seat, dark);
		app->ShowToast(cs::Cat("Test shine: the ", NameOf(t), " shines as P", seat + 1, "'s", dark ? " (running dark: only inside visual range)" : "",
			"  --  sg.Shine clear to stop"));
	}

	/// US-T136: <c>sg.Shine [fake] [seat=N] [dark]</c> fakes a remote player holding the hull selected (or the ship nearest the
	/// camera), so its shine can be seen without a second machine; <c>sg.Shine clear</c>, <c>sg.Shine list</c>;
	/// <c>sg.Shine shots &lt;dir&gt; [quit]</c> takes shine-{game,real}-{close,sweep,far,far-dark,beyond-lit,beyond-dark}.png into dir.
	FAutoConsoleCommand GShineCommand(
		TEXT("sg.Shine"),
		TEXT("US-T136: the shine on a hull another player embodies, tried solo: sg.Shine [fake] [seat=N] [dark] fakes a remote holder on the "
			"selected or nearest hull; sg.Shine clear | list; sg.Shine shots <dir> [quit] screenshots it close and far at both scales."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));

	void RegisterShineCheats()
	{
		using SpaceGod::UI::CheatEntry;
		using SpaceGod::UI::CheatRegistry;
		CheatRegistry::Add("Helm", CheatEntry::Console("Fake a remote holder", "the hull selected or nearest shines as P2's, as another player's would (US-T136)", "sg.Shine fake"));
		CheatRegistry::Add("Helm", CheatEntry::Console("Fake a remote holder, dark", "the same, running dark: it shines only inside visual range", "sg.Shine fake dark"));
		CheatRegistry::Add("Helm", CheatEntry::Console("Clear fake holders", "stop the test shine", "sg.Shine clear"));
	}

	SpaceGod::UI::CheatRegistration GShineCheats(&RegisterShineCheats);
}
