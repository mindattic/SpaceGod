// US-T437: sg.Bindings, the controls page seen. Opens the pause menu on Settings > Controls > Key bindings and screenshots
// it at the top (Move, Flight), at the Combat group (the pad's triggers beside the keys), at the View group with the HUD
// mode's slots holding a key, a mouse side button and a pad stick on double-tap (and the clash that key makes), and with a
// slot listening. The HUD mode's own binding is put back as it was before the run ends.
//   sg.Bindings [quit]   (screenshots: Saved/Screenshots/bindings-*.png)

#include "UnitySpaceApp.h"

#include "GameLaunch.h"
#include "Input/KeyBindings.h"
#include "SpaceGodLog.h"
#include "UI/PauseMenu.h"
#include "UnitySpaceAppActor.h"

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
namespace SgIn = ::SpaceGod::Input;

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

	struct Run
	{
		using Step = std::function<bool(UnitySpaceApp&, double)>;
		std::vector<std::pair<FString, Step>> steps;
		size_t at = 0;
		double stepStart = -1, started = FPlatformTime::Seconds();
		bool askedNewGame = false, quit = false;
		/// The HUD mode's slots before the run (put back at the end) and whether they were its defaults.
		SgIn::InputBinding saved[SgIn::KeyBindingSetting::Slots];
		bool savedDefault = true, changed = false;
		/// The step whose setup has run (a step's lambda is called every frame until it is done: its setup runs once).
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
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Bindings: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 60.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		void Restore()
		{
			using namespace ::SpaceGod::Input;
			if (!changed) return;
			changed = false;
			const auto& b = KeyBindings::Binding(KeyAction::HudMode);
			if (savedDefault) b->Reset();
			else b->Assign(saved);
		}

		bool Finish(const TCHAR* failure)
		{
			Restore();
			if (auto* app = App()) app->PauseMenuForChecks()->Close();
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Bindings: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Bindings: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	void Start(const TArray<FString>& args)
	{
		using namespace ::SpaceGod::Input;
		bool quit = false;
		for (const FString& a : args)
			if (a.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
		auto run = std::make_shared<Run>();
		run->quit = quit;
		FString dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"));
		auto push = [&run](FString name, Run::Step step) { run->steps.push_back({ name, std::move(step) }); };
		auto shot = [&](const TCHAR* tag)
		{
			FString file = FPaths::Combine(dir, FString::Printf(TEXT("bindings-%s.png"), tag));
			push(FString::Printf(TEXT("shot %s"), *file), [file](UnitySpaceApp&, double) { FScreenshotRequest::RequestScreenshot(file, true, false); return true; });
			push(TEXT("written"), [](UnitySpaceApp&, double t) { return t >= 1.0; });
		};

		push(TEXT("game up"), [](UnitySpaceApp&, double t) { return t >= 6.0; });
		push(TEXT("Settings > Controls > Key bindings opened"), [run](UnitySpaceApp& app, double t)
		{
			if (run->First()) app.PauseMenuForChecks()->ShowKeyBindings();
			return t >= 1.5; // the page eases in
		});
		shot(TEXT("top"));
		push(TEXT("scrolled to Combat"), [run](UnitySpaceApp& app, double t)
		{
			if (run->First()) app.PauseMenuForChecks()->ShowKeyBindings("Combat");
			return t >= 1.0;
		});
		shot(TEXT("combat"));
		push(TEXT("the HUD mode bound to Z, Mouse 4 and a double-tapped stick"), [run](UnitySpaceApp& app, double t)
		{
			if (run->First() && !run->changed)
			{
				const auto& b = KeyBindings::Binding(KeyAction::HudMode);
				for (int i = 0; i < KeyBindingSetting::Slots; i++) run->saved[i] = b->Value[i];
				run->savedDefault = b->IsDefault();
				InputBinding custom[KeyBindingSetting::Slots] = {
					InputBinding(Key::Z), InputBinding::OfMouse(MouseButton::Back), InputBinding::OfStick(StickDir::LeftUp).With(Trigger::DoubleTap) };
				b->Assign(custom);
				run->changed = true;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Bindings: HUD mode now \"%s\" (shown \"%s\"); %d clash(es) for Z"), *T(b->Format()), *T(b->ValueText()),
					KeyBindings::Conflicts(KeyAction::HudMode, InputBinding(Key::Z)).Count());
				app.PauseMenuForChecks()->ShowKeyBindings("HUD mode");
			}
			return t >= 1.0;
		});
		shot(TEXT("view-custom"));
		push(TEXT("the HUD mode's secondary slot listening"), [run](UnitySpaceApp& app, double t)
		{
			if (run->First()) app.PauseMenuForChecks()->ListenOn(static_cast<int>(KeyAction::HudMode), 1);
			return t >= 0.6;
		});
		shot(TEXT("listening"));
		push(TEXT("the HUD mode put back"), [run](UnitySpaceApp& app, double)
		{
			run->Restore();
			app.PauseMenuForChecks()->ShowKeyBindings("HUD mode"); // stops the listening
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Bindings: HUD mode back to \"%s\""), *T(KeyBindings::Binding(KeyAction::HudMode)->Format()));
			return true;
		});
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	FAutoConsoleCommand GBindingsCommand(
		TEXT("sg.Bindings"),
		TEXT("US-T437: sg.Bindings [quit] opens Settings > Controls > Key bindings and screenshots it (the top, Combat, a HUD mode bound to a key, "
			"a mouse side button and a double-tapped stick, a slot listening; Saved/Screenshots/bindings-*.png); the binding is put back after."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
