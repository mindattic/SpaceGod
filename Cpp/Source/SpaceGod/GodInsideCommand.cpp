// US-T442: sg.GodInside, the god view inside things. The free camera (no collision) is put just above Earth's surface, just
// under it, halfway in, by the core looking out at the Moon, inside the Sun, inside a capital ship and inside a small craft,
// and each is screenshotted: from inside, the body or hull is drawn see-through with its inside as a wireframe.
//   sg.GodInside [shot <dir>] [quit]

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "GameLaunch.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Target.h"
#include "UnitySpaceAppActor.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
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

	SpaceGod::Vector3 V(DVec3 v) { return SpaceGod::Vector3(static_cast<float>(v.X), static_cast<float>(v.Y), static_cast<float>(v.Z)); }

	struct Run
	{
		using Step = std::function<bool(UnitySpaceApp&, double)>;
		std::vector<std::pair<FString, Step>> steps;
		size_t at = 0;
		double stepStart = -1, started = FPlatformTime::Seconds();
		bool askedNewGame = false, quit = false;
		int failures = 0;

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
				UE_LOG(LogSpaceGod, Display, TEXT("sg.GodInside: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 60.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (auto* app = App()) app->Clock->SetPaused(false);
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.GodInside: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.GodInside: done, %d check(s) failed"), failures); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	int BodyNamed(UnitySpaceApp& app, const char* name)
	{
		for (int i = 0; i < app.Catalog()->Count(); i++)
			if ((*app.Catalog())[i]->Name == name) return i;
		return -1;
	}

	struct Stage
	{
		int Earth = -1, Moon = -1, Sun = 0;
		cs::Ref<Ship> Capital, Craft;
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
		auto run = std::make_shared<Run>();
		run->quit = quit;
		auto s = std::make_shared<Stage>();
		auto hold = [s](UnitySpaceApp& app)
		{
			for (const auto& ship : { s->Capital, s->Craft })
				if (ship) { ship->Phase = ShipPhase::Parked; ship->PhaseEndJd = app.Clock->Jd + 1; }
		};
		auto push = [&run](FString name, Run::Step step) { run->steps.push_back({ name, std::move(step) }); };

		push(TEXT("game up"), [](UnitySpaceApp&, double t) { return t >= 6.0; });
		push(TEXT("staged by Earth"), [s](UnitySpaceApp& app, double)
		{
			app.SkyLeaveHelm();
			if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
			app.Clock->SetPaused(true);
			s->Earth = BodyNamed(app, "Earth");
			s->Moon = BodyNamed(app, "Moon");
			s->Sun = app.Catalog()->Root()->Index;
			if (s->Earth < 0 || s->Moon < 0) throw std::runtime_error("no Earth or Moon in this system");
			// A capital and a fighter parked off Earth's day side, far enough apart to be looked at alone, Earth behind them.
			auto earth = (*app.Catalog())[s->Earth];
			DVec3 off(earth->RadiusKm * 2.2, earth->RadiusKm * 0.4, 0);
			s->Capital = app.Fleet()->SpawnAt("Inside Check Battleship", ShipClass::Battleship, app.World()->Sol, s->Earth, off, DVec3::UnitY, app.Clock->Jd);
			s->Craft = app.Fleet()->SpawnAt("Inside Check Fighter", ShipClass::Fighter, app.World()->Sol, s->Earth, off + DVec3(0, 0, 3000), DVec3::UnitY, app.Clock->Jd);
			if (!s->Capital || !s->Craft) throw std::runtime_error("a hull would not spawn");
			return true;
		});

		struct BodyShot { const TCHAR* Tag; bool Sun; double Radii; double Elevation; int Look; }; // Look: 0 the Moon, 1 the centre, 2 across the limb
		// Radii from the centre along the line to the Moon (offset by Elevation, turned off it, so the limb is in view).
		static const BodyShot bodyShots[] = {
			{ TEXT("earth-outside"), false, 1.06, 0.35, 2 },
			{ TEXT("earth-just-inside"), false, 0.985, 0.35, 2 },
			{ TEXT("earth-mantle"), false, 0.7, 0.6, 1 },
			{ TEXT("earth-core"), false, 0.1, 0.2, 0 },
			{ TEXT("sun-inside"), true, 0.45, 0.4, 1 },
		};
		for (const auto& b : bodyShots)
		{
			FString tag = b.Tag;
			push(FString::Printf(TEXT("%s placed"), b.Tag), [s, hold, b](UnitySpaceApp& app, double t)
			{
				hold(app);
				int body = b.Sun ? s->Sun : s->Earth;
				auto centre = app.BodyScene(body);
				double r = app.TargetRadius(Target::OfBody(body));
				auto toMoon = (app.BodyScene(s->Moon) - centre).Normalized();
				auto up = DVec3::UnitY;
				auto side = DVec3::Cross(up, toMoon).Normalized();
				// From the Moon's line, raised off it (above the ecliptic) and swung aside, so the eye looks across the body.
				auto dir = (toMoon * (1 - b.Elevation) + up * b.Elevation + side * (b.Elevation * 0.5)).Normalized();
				auto eye = centre + dir * (r * b.Radii);
				// Across the limb: towards a point on the far side of the body, so the skin's curve, the shells within and the
				// sky beyond are all in view.
				DVec3 look = b.Look == 0 ? app.BodyScene(s->Moon) - eye : b.Look == 1 ? centre - eye + up * (r * 0.15)
					: centre + side * (r * 0.9) - dir * (r * 0.3) - eye;
				if (t < 0.05 || app.SpaceCamera->Mode != SpaceGod::Cameras::SpaceCamera::CameraMode::Free)
					app.SpaceCamera->SetPose(eye, SpaceGod::Quaternion::LookRotation(V(look.Normalized()), SpaceGod::Vector3::up()));
				return t > 3.0; // the maps stream in, the fade settles
			});
			FString file = dir.IsEmpty() ? FString() : FPaths::Combine(dir, FString::Printf(TEXT("godinside-%s.png"), b.Tag));
			push(TEXT("shot"), [run, s, file, b](UnitySpaceApp& app, double)
			{
				int body = b.Sun ? s->Sun : s->Earth;
				double r = app.TargetRadius(Target::OfBody(body));
				double at = (app.SpaceCamera->ScenePosition - app.BodyScene(body)).Length() / r;
				// No collision: the camera stays where it was put (once pushed back out onto 1.003 radii).
				bool held = cs::Math::Abs(at - b.Radii) < 0.01;
				if (!held) run->failures++;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.GodInside: %s at %.4f radii (put at %.4f) %s; speed %.4g, clip gap %.4g, radius %.4g"), b.Tag, at, b.Radii,
					held ? TEXT("PASS") : TEXT("FAIL"), app.SpaceCamera->CurrentSpeed, app.ClipDistance(app.SpaceCamera->ScenePosition), r);
				if (!file.IsEmpty()) FScreenshotRequest::RequestScreenshot(file, false, false);
				return true;
			});
			push(TEXT("written"), [hold](UnitySpaceApp& app, double t) { hold(app); return t >= 0.8; });
		}

		struct HullShot { const TCHAR* Tag; bool Capital; float Along; };
		static const HullShot hullShots[] = {
			{ TEXT("capital-inside"), true, -0.25f },
			{ TEXT("capital-bow"), true, 0.3f },
			{ TEXT("craft-inside"), false, -0.15f },
		};
		for (const auto& h : hullShots)
		{
			push(FString::Printf(TEXT("%s placed"), h.Tag), [s, hold, h](UnitySpaceApp& app, double t)
			{
				hold(app);
				auto ship = h.Capital ? s->Capital : s->Craft;
				double length = app.ShipDisplayLength(ship);
				auto rot = app.TargetRotation(Target::OfShip(ship));
				auto centre = app.FocusScene(Target::OfShip(ship));
				if (length <= 0) return t > 20;
				// Inside the hull, a little aft (or forward) of its middle and above its keel, looking forward along it and down.
				auto fwd = rot * SpaceGod::Vector3::forward();
				auto up = rot * SpaceGod::Vector3::up();
				auto right = rot * SpaceGod::Vector3::right();
				auto offset = fwd * h.Along + up * 0.012f + right * 0.01f;
				auto eye = centre + DVec3(offset.x, offset.y, offset.z) * length;
				auto lookDir = (fwd + up * -0.12f + right * -0.18f).normalized();
				if (t < 0.05 || app.SpaceCamera->Mode != SpaceGod::Cameras::SpaceCamera::CameraMode::Free || t < 2.0)
					app.SpaceCamera->SetPose(eye, SpaceGod::Quaternion::LookRotation(lookDir, up));
				return t > 3.0;
			});
			FString file = dir.IsEmpty() ? FString() : FPaths::Combine(dir, FString::Printf(TEXT("godinside-%s.png"), h.Tag));
			push(TEXT("shot"), [s, file, h](UnitySpaceApp& app, double)
			{
				auto ship = h.Capital ? s->Capital : s->Craft;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.GodInside: %s inside %s (%s), drawn %.4g long; speed %.4g, clip gap %.4g"), h.Tag, *T(ship->Name),
					*T(cs::Str(ship->Spec->Class)), app.ShipDisplayLength(ship), app.SpaceCamera->CurrentSpeed, app.ClipDistance(app.SpaceCamera->ScenePosition));
				if (!file.IsEmpty()) FScreenshotRequest::RequestScreenshot(file, false, false);
				return true;
			});
			push(TEXT("written"), [hold](UnitySpaceApp& app, double t) { hold(app); return t >= 0.8; });
		}

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	FAutoConsoleCommand GGodInsideCommand(
		TEXT("sg.GodInside"),
		TEXT("US-T442: sg.GodInside [shot <dir>] [quit] puts the free (god) camera above Earth's surface, just under it, in its mantle, by "
			"its core looking out at the Moon, inside the Sun, inside a capital ship and inside a fighter, screenshotting each into <dir>."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
