// US-T145: the sky's layers from the console and the dev menu: sg.SkyMode, sg.SkyVolume, sg.SkyCompare, sg.EdgeRun,
// sg.WarpTunnel and sg.WarpRun (each a step list run once a frame on the core ticker, as sg.Skybox).

#include "UnitySpaceApp.h"

#include "Astro.h"
#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "GameLaunch.h"
#include "Rendering/SystemSkies.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Systems/DeepSpace.h"
#include "Systems/Galaxy.h"
#include "Systems/HelmFtl.h"
#include "Target.h"
#include "UI/CheatMenu.h"
#include "UnitySpaceAppActor.h"
#include "Views/SkyLayers.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "UnrealClient.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <vector>

using namespace SpaceGod::Core;
using SpaceGod::Views::SkyLayers;
using SpaceGod::Views::SkyMode;

namespace
{
	SpaceGod::UnitySpaceApp* RunningApp()
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

	/// One run, stepped once a frame on the core ticker (as sg.Skybox): each step returns true when done.
	struct Run
	{
		using Step = std::function<bool(SpaceGod::UnitySpaceApp&, double)>;
		const TCHAR* name = TEXT("sg.Sky");
		std::vector<std::pair<FString, Step>> steps;
		size_t at = 0;
		double stepStart = -1;
		double started = FPlatformTime::Seconds();
		double timeout = 240.0;
		bool askedNewGame = false;
		bool quit = false;
		std::function<void(SpaceGod::UnitySpaceApp*)> onFinish;

		bool Tick()
		{
			auto* app = RunningApp();
			double now = FPlatformTime::Seconds();
			if (app == nullptr)
			{
				if (!askedNewGame && now - started > 2.0)
				{
					askedNewGame = true;
					UE_LOG(LogSpaceGod, Display, TEXT("%s: no game running; starting a new one"), name);
					SpaceGod::GameLaunch::NewGame();
				}
				if (now - started > 180.0) return Finish(TEXT("no game came up"));
				return true;
			}
			if (at >= steps.size()) return Finish(nullptr);
			if (stepStart < 0) stepStart = now;
			if (steps[at].second(*app, now - stepStart))
			{
				UE_LOG(LogSpaceGod, Display, TEXT("%s: %s"), name, *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > timeout) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("%s: %s"), name, failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("%s: done"), name); }
			if (onFinish) onFinish(RunningApp());
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}

		void Go(std::shared_ptr<Run> self)
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([self](float)
			{
				try { return self->Tick(); }
				catch (const std::exception& e) { return self->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
			}));
		}
	};

	Run::Step Wait(double seconds) { return [seconds](SpaceGod::UnitySpaceApp&, double t) { return t >= seconds; }; }

	Run::Step Shot(const FString& dir, const FString& file)
	{
		return [dir, file](SpaceGod::UnitySpaceApp&, double)
		{
			if (dir.IsEmpty()) return true;
			FString path = FPaths::Combine(dir, file);
			FScreenshotRequest::RequestScreenshot(path, false, false);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Sky: shot %s"), *path);
			return true;
		};
	}

	/// The common opening: a game, the hero system reached, the scale asked for, real time.
	void ToHero(Run& run, bool realScale)
	{
		run.steps.push_back({ TEXT("game up"), Wait(2.0) });
		auto system = std::make_shared<cs::Ref<StarSystem>>();
		run.steps.push_back({ TEXT("travelling to the hero system"), [system](SpaceGod::UnitySpaceApp& app, double)
		{
			cs::String hero = app.SkyHeroSystem();
			if (hero.empty()) throw std::runtime_error("no system beyond Sol in this galaxy");
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Sky: the hero system is %s"), UTF8_TO_TCHAR(hero.c_str()));
			SpaceGod::Rendering::SystemSkies::Prefetch(hero);
			*system = app.Galaxy->Realize(hero, app.Clock->Jd);
			if (*system == nullptr) throw std::runtime_error("hero system not found");
			if (app.ActiveSystem != *system)
			{
				app.SkyLeaveHelm();
				app.TravelTo(*system);
			}
			return true;
		} });
		run.steps.push_back({ TEXT("arrived"), [system](SpaceGod::UnitySpaceApp& app, double t)
		{
			if (app.ActiveSystem != *system && !app.Travel->Active && t > 5.0) app.TravelTo(*system);
			return app.ActiveSystem == *system && !app.Travel->Active;
		} });
		run.steps.push_back({ realScale ? TEXT("realistic scale") : TEXT("game scale"), [realScale](SpaceGod::UnitySpaceApp& app, double)
		{
			double goal = realScale ? 0.0 : 1.0;
			if (app.ScaleGoal != goal) app.ToggleScale();
			return !app.ScaleAnimating();
		} });
	}

	/// The way out of the hero system for the edge run: through the painting's bright core, a little above it (so the
	/// star is not in the way looking back): flying out, the brightest gas thins and streams past, and from outside
	/// it is the near side of the cloud round the star.
	DVec3 HeroOutward()
	{
		DVec3 heart = SkyLayers::HeroHeart();
		return (heart + DVec3(0, 0, 0.3)).Normalized();
	}
	/// The way out opposite the core, through the painting's darker teal walls (sg.WarpTunnel: the jump's axis).
	DVec3 HeroBackward()
	{
		DVec3 heart = SkyLayers::HeroHeart();
		return (-heart + DVec3(0, 0, 0.25)).Normalized();
	}

	/// Frame timing over a window, for the cost of each sky.
	struct Timing
	{
		std::vector<double> frameMs, gpuMs;
		void Sample()
		{
			frameMs.push_back(FApp::GetDeltaTime() * 1000.0);
			gpuMs.push_back(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
		}
		static double Median(std::vector<double> v)
		{
			if (v.empty()) return 0;
			std::sort(v.begin(), v.end());
			return v[v.size() / 2];
		}
		FString Text() const
		{
			return FString::Printf(TEXT("frame %.2f ms median (%.1f fps), GPU %.2f ms median over %d frames"),
				Median(frameMs), Median(frameMs) > 0 ? 1000.0 / Median(frameMs) : 0.0, Median(gpuMs), static_cast<int>(frameMs.size()));
		}
	};

	// ------------------------------------------------------------------------------------------------ sg.SkyMode

	void SkyModeCommand(const TArray<FString>& args)
	{
		if (args.Num() == 0)
		{
			UE_LOG(LogSpaceGod, Display, TEXT("sg.SkyMode: %s (cube | sphere | volume)"), UTF8_TO_TCHAR(SkyLayers::ModeName(SkyLayers::Mode())));
			return;
		}
		SkyMode mode;
		if (args[0].Equals(TEXT("next"), ESearchCase::IgnoreCase)) mode = static_cast<SkyMode>((static_cast<int>(SkyLayers::Mode()) + 1) % 3);
		else if (!SkyLayers::ParseMode(TCHAR_TO_UTF8(*args[0]), mode))
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.SkyMode: cube | sphere | volume | next"));
			return;
		}
		SkyLayers::SetMode(mode);
		if (auto* app = RunningApp())
		{
			static const char* say[] = { "SKY: CUBE (the system's painted skybox at infinity)", "SKY: SPHERE (the hero painting, Alien Space Nebula 1 by Jungle Jim, CC BY 4.0)",
				"SKY: VOLUME (the Milky Way behind the system's painting as a cloud the size of its rim)" };
			app->ShowToast(say[static_cast<int>(mode)]);
		}
		UE_LOG(LogSpaceGod, Display, TEXT("sg.SkyMode: %s"), UTF8_TO_TCHAR(SkyLayers::ModeName(mode)));
	}

	// ------------------------------------------------------------------------------------------------ sg.SkyVolume

	void SkyVolumeCommand(const TArray<FString>& args)
	{
		for (const auto& a : args)
		{
			FString k, v;
			if (!a.Split(TEXT("="), &k, &v)) continue;
			float f = FCString::Atof(*v);
			if (k.Equals(TEXT("density"), ESearchCase::IgnoreCase)) SkyLayers::VolumeDensity() = FMath::Max(0.0f, f);
			else if (k.Equals(TEXT("brightness"), ESearchCase::IgnoreCase)) SkyLayers::VolumeBrightness() = FMath::Max(0.0f, f);
			else if (k.Equals(TEXT("steps"), ESearchCase::IgnoreCase)) SkyLayers::VolumeSteps() = FMath::Clamp(f, 8.0f, 96.0f);
		}
		UE_LOG(LogSpaceGod, Display, TEXT("sg.SkyVolume: density=%.2f brightness=%.2f steps=%.0f"), SkyLayers::VolumeDensity(), SkyLayers::VolumeBrightness(), SkyLayers::VolumeSteps());
	}

	// ------------------------------------------------------------------------------------------------ sg.SkyCompare

	/// <c>sg.SkyCompare [real] [shots &lt;dir&gt;] [quit]</c>: the hero system, the helm taken halfway to the rim facing the
	/// painting's core, then each mode in place (cube, sphere, volume), timed for 3 s and shot.
	void SkyCompareCommand(const TArray<FString>& args)
	{
		FString shots;
		bool quit = false, real = false;
		for (int i = 0; i < args.Num(); i++)
		{
			if (args[i].Equals(TEXT("shots"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) shots = args[++i];
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
			else if (args[i].Equals(TEXT("real"), ESearchCase::IgnoreCase)) real = true;
		}
		auto run = std::make_shared<Run>();
		run->name = TEXT("sg.SkyCompare");
		run->quit = quit;
		ToHero(*run, real);
		run->steps.push_back({ TEXT("helm taken"), [](SpaceGod::UnitySpaceApp& app, double)
		{
			if (!app.SkyTakeHelm(false)) throw std::runtime_error("no hull to take here");
			app.Clock->WarpIndex = 0;
			return true;
		} });
		// Halfway out, nose on the core: the ship in front of the painting's heart.
		run->steps.push_back({ TEXT("placed halfway to the rim"), [](SpaceGod::UnitySpaceApp& app, double)
		{
			return app.SkyEdgePlace(0.5, (-SkyLayers::HeroHeart() + DVec3(0.25, 0.35, 0.15)).Normalized(), true);
		} });
		const char* modes[] = { "cube", "sphere", "volume" };
		auto results = std::make_shared<std::vector<FString>>();
		for (int m = 0; m < 3; m++)
		{
			SkyMode mode = static_cast<SkyMode>(m);
			FString name = UTF8_TO_TCHAR(modes[m]);
			run->steps.push_back({ FString::Printf(TEXT("mode %s"), *name), [mode](SpaceGod::UnitySpaceApp&, double) { SkyLayers::SetMode(mode); return true; } });
			run->steps.push_back({ TEXT("sky up"), [mode](SpaceGod::UnitySpaceApp& app, double t)
			{
				bool up = mode != SkyMode::Volume || (app.SkyLayersView != nullptr && app.SkyLayersView->VolumeShown());
				return t >= 4.0 && (up || t > 30.0);
			} });
			auto timing = std::make_shared<Timing>();
			run->steps.push_back({ TEXT("timed"), [timing, results, name](SpaceGod::UnitySpaceApp&, double t)
			{
				timing->Sample();
				if (t < 3.0) return false;
				results->push_back(FString::Printf(TEXT("%s: %s"), *name, *timing->Text()));
				UE_LOG(LogSpaceGod, Display, TEXT("sg.SkyCompare: cost %s"), *results->back());
				return true;
			} });
			run->steps.push_back({ TEXT("shot"), Shot(shots, FString::Printf(TEXT("compare-%d-%s.png"), m + 1, *name)) });
			run->steps.push_back({ TEXT("shot written"), Wait(1.0) });
		}
		run->onFinish = [results](SpaceGod::UnitySpaceApp*)
		{
			for (const auto& r : *results) UE_LOG(LogSpaceGod, Display, TEXT("sg.SkyCompare: %s"), *r);
		};
		run->Go(run);
	}

	// ------------------------------------------------------------------------------------------------ sg.EdgeRun

	/// <c>sg.EdgeRun [real] [mode] [shots &lt;dir&gt;] [quit]</c>: a hull in the hero system flown from well inside out
	/// through the rim into open space, turned, and flown back in, the chase camera behind it; with shots, pictures at
	/// each point (inside, through the wall, at the rim, outside, looking back from outside, back in).
	void EdgeRunCommand(const TArray<FString>& args)
	{
		FString shots;
		bool quit = false, real = false;
		SkyMode mode = SkyMode::Volume;
		for (int i = 0; i < args.Num(); i++)
		{
			SkyMode m;
			if (args[i].Equals(TEXT("shots"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) shots = args[++i];
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
			else if (args[i].Equals(TEXT("real"), ESearchCase::IgnoreCase)) real = true;
			else if (SkyLayers::ParseMode(TCHAR_TO_UTF8(*args[i]), m)) mode = m;
		}
		auto run = std::make_shared<Run>();
		run->name = TEXT("sg.EdgeRun");
		run->quit = quit;
		ToHero(*run, real);
		run->steps.push_back({ TEXT("helm taken"), [mode](SpaceGod::UnitySpaceApp& app, double)
		{
			SkyLayers::SetMode(mode);
			if (!app.SkyTakeHelm(false)) throw std::runtime_error("no hull to take here");
			app.Clock->WarpIndex = 0;
			app.ShowToast("EDGE RUN: out through the rim into open space and back");
			return true;
		} });
		DVec3 out = HeroOutward();
		run->steps.push_back({ TEXT("placed inside"), [out](SpaceGod::UnitySpaceApp& app, double) { return app.SkyEdgePlace(0.3, out, false); } });
		run->steps.push_back({ TEXT("sky up"), [](SpaceGod::UnitySpaceApp& app, double t)
		{
			return t >= 4.0 && (SkyLayers::Mode() != SkyMode::Volume || app.SkyLayersView->VolumeShown() || t > 30.0);
		} });

		// A leg: the hull flown from one fraction of the rim to another over some seconds, eased, a shot at each mark.
		struct Mark { double At; const TCHAR* File; };
		auto leg = [run, out, shots](double from, double to, double seconds, bool facingIn, std::vector<Mark> marks)
		{
			auto next = std::make_shared<size_t>(0);
			auto shotAt = std::make_shared<double>(-1);
			run->steps.push_back({ FString::Printf(TEXT("flown %.2f -> %.2f of the rim"), from, to), [=](SpaceGod::UnitySpaceApp& app, double t)
			{
				// Held still a moment at each mark for its picture, then on.
				if (*shotAt >= 0)
				{
					if (t - *shotAt < 1.2) return false;
					*shotAt = -1;
					(*next)++;
				}
				double held = 1.2 * static_cast<double>(*next);
				double u = FMath::Clamp((t - held) / seconds, 0.0, 1.0);
				double e = u * u * (3 - 2 * u);
				double x = from + (to - from) * e;
				if (*next < marks.size())
				{
					const Mark& m = marks[*next];
					bool reached = to > from ? x >= m.At : x <= m.At;
					if (reached)
					{
						x = m.At;
						app.SkyEdgePlace(x, out, facingIn);
						if (!shots.IsEmpty()) FScreenshotRequest::RequestScreenshot(FPaths::Combine(shots, m.File), false, false);
						UE_LOG(LogSpaceGod, Display, TEXT("sg.EdgeRun: %.2f of the rim (%s)%s"), x, facingIn ? TEXT("facing in") : TEXT("facing out"),
							shots.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", shot %s"), m.File));
						*shotAt = t;
						return false;
					}
				}
				app.SkyEdgePlace(x, out, facingIn);
				return u >= 1.0 && *next >= marks.size();
			} });
		};
		leg(0.3, 2.2, 22.0, false, { { 0.45, TEXT("edge-1-inside.png") }, { 0.8, TEXT("edge-2-in-the-wall.png") }, { 1.0, TEXT("edge-3-at-the-rim.png") },
			{ 1.35, TEXT("edge-4-outside.png") } });
		run->steps.push_back({ TEXT("turned to look back"), [out](SpaceGod::UnitySpaceApp& app, double t)
		{
			app.SkyEdgePlace(2.2, out, true);
			return t >= 2.5;
		} });
		run->steps.push_back({ TEXT("shot looking back"), Shot(shots, TEXT("edge-5-looking-back.png")) });
		run->steps.push_back({ TEXT("held"), Wait(1.2) });
		leg(2.2, 0.4, 18.0, true, { { 0.9, TEXT("edge-6-back-inside.png") } });
		run->Go(run);
	}

	// ------------------------------------------------------------------------------------------------ sg.WarpTunnel

	/// <c>sg.WarpTunnel [0..1 | off] [milkyway] [demo | shots &lt;dir&gt; [quit]]</c>: the jump's tunnel held round the view (a
	/// preview of what the drive does), with milkyway the sky between the stars. With demo or shots: the Milky Way sky, the tunnel
	/// building, at full strength, and unwinding.
	void WarpTunnelCommand(const TArray<FString>& args)
	{
		FString shots;
		bool quit = false, milky = false, off = false, demo = false;
		float warp = -1;
		for (int i = 0; i < args.Num(); i++)
		{
			if (args[i].Equals(TEXT("shots"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) shots = args[++i];
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
			else if (args[i].Equals(TEXT("milkyway"), ESearchCase::IgnoreCase)) milky = true;
			else if (args[i].Equals(TEXT("off"), ESearchCase::IgnoreCase)) off = true;
			else if (args[i].Equals(TEXT("demo"), ESearchCase::IgnoreCase)) demo = true;
			else if (args[i].IsNumeric()) warp = FMath::Clamp(FCString::Atof(*args[i]), 0.0f, 1.0f);
		}
		if (shots.IsEmpty() && !demo)
		{
			if (auto* app = RunningApp()) app->SkyPreview(off ? -1.0f : warp < 0 ? 0.6f : warp, milky);
			return;
		}
		auto run = std::make_shared<Run>();
		run->name = TEXT("sg.WarpTunnel");
		run->quit = quit;
		ToHero(*run, false);
		run->steps.push_back({ TEXT("helm taken"), [](SpaceGod::UnitySpaceApp& app, double)
		{
			SkyLayers::SetMode(SkyMode::Cube);
			if (!app.SkyTakeHelm(false)) throw std::runtime_error("no hull to take here");
			app.Clock->WarpIndex = 0;
			// Beyond the rim, nose on the way the hull would jump (toward the galactic centre: the bulge ahead).
			return app.SkyEdgePlace(1.25, HeroBackward(), false);
		} });
		run->steps.push_back({ TEXT("settled"), Wait(4.0) });
		run->steps.push_back({ TEXT("system sky shot"), Shot(shots, TEXT("warp-0-system-sky.png")) });
		run->steps.push_back({ TEXT("held"), Wait(1.0) });
		struct Beat { float Warp; bool Milky; double Hold; const TCHAR* File; };
		std::vector<Beat> beats = { { 0.35f, false, 2.0, TEXT("warp-1-spooling.png") }, { 0.75f, false, 2.0, TEXT("warp-2-charged.png") },
			{ 1.0f, false, 1.5, TEXT("warp-3-full-strength.png") }, { 1.0f, true, 1.5, TEXT("warp-4-swapped-under-streaks.png") },
			{ 0.28f, true, 3.0, TEXT("warp-5-crossing.png") }, { 0.0f, true, 3.0, TEXT("milkyway-void-stop.png") } };
		for (const auto& b : beats)
		{
			run->steps.push_back({ FString::Printf(TEXT("tunnel %.2f%s"), b.Warp, b.Milky ? TEXT(" (Milky Way)") : TEXT("")), [b](SpaceGod::UnitySpaceApp& app, double t)
			{
				app.SkyPreview(b.Warp, b.Milky);
				return t >= b.Hold;
			} });
			run->steps.push_back({ TEXT("shot"), Shot(shots, b.File) });
			run->steps.push_back({ TEXT("held"), Wait(1.0) });
		}
		// The Milky Way turned toward the galactic centre, the bulge in its true direction.
		run->steps.push_back({ TEXT("looking at the galactic centre"), [](SpaceGod::UnitySpaceApp& app, double t)
		{
			app.SkyPreview(0.0f, true);
			// Galactic centre: RA 266.405, Dec -28.936 (J2000); the ecliptic direction by the usual obliquity.
			double ra = 266.405 * cs::Math::PI / 180, dec = -28.936 * cs::Math::PI / 180, eps = 23.4392911 * cs::Math::PI / 180;
			DVec3 eq(cs::Math::Cos(dec) * cs::Math::Cos(ra), cs::Math::Cos(dec) * cs::Math::Sin(ra), cs::Math::Sin(dec));
			DVec3 ecl(eq.X, eq.Y * cs::Math::Cos(eps) + eq.Z * cs::Math::Sin(eps), -eq.Y * cs::Math::Sin(eps) + eq.Z * cs::Math::Cos(eps));
			app.SkyEdgePlace(1.25, -ecl, true);
			return t >= 3.0;
		} });
		run->steps.push_back({ TEXT("shot"), Shot(shots, TEXT("milkyway-galactic-centre.png")) });
		run->steps.push_back({ TEXT("held"), Wait(1.0) });
		run->onFinish = [](SpaceGod::UnitySpaceApp* app)
		{
			if (app) app->SkyPreview(-1, false);
			SkyLayers::SetMode(SkyMode::Volume);
		};
		run->Go(run);
	}

	// ------------------------------------------------------------------------------------------------ sg.WarpRun

	/// <c>sg.WarpRun [shots &lt;dir&gt;] [quit]</c>: the real thing. A hull with a jump drive, put beyond the rim with its
	/// nose on the nearest star, the FTL autopilot engaged; shots as the drive spools, at the peak, through the crossing
	/// (the Milky Way) and at the drop-out at the far end.
	void WarpRunCommand(const TArray<FString>& args)
	{
		FString shots;
		bool quit = false;
		for (int i = 0; i < args.Num(); i++)
		{
			if (args[i].Equals(TEXT("shots"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) shots = args[++i];
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
		}
		auto run = std::make_shared<Run>();
		run->name = TEXT("sg.WarpRun");
		run->quit = quit;
		run->timeout = 400;
		run->steps.push_back({ TEXT("game up"), Wait(3.0) });
		run->steps.push_back({ TEXT("a drive hull at the helm, beyond the rim, nose on the nearest star"), [](SpaceGod::UnitySpaceApp& app, double)
		{
			if (!app.SkyTakeHelm(true)) throw std::runtime_error("no hull with a jump drive in this system");
			cs::Ref<FtlTarget> best;
			double bestKm = cs::Double::PositiveInfinity;
			for (const auto& t : FtlTargets::All(app.Galaxy, app.ActiveSystem))
			{
				double km;
				HelmFtl::Bearing(t, app.ActiveSystem, DVec3::Zero, km);
				if (t->System != nullptr && km < bestKm) { bestKm = km; best = t; }
			}
			if (best == nullptr) throw std::runtime_error("no star to jump to");
			double km;
			DVec3 way = HelmFtl::Bearing(best, app.ActiveSystem, DVec3::Zero, km);
			app.SkyEdgePlace(1.3, way, false);
			if (!app.PlotFtlCourse(best, true)) throw std::runtime_error("no course to the nearest star");
			if (!app.EngageFtlAutopilot()) throw std::runtime_error("the FTL autopilot would not take the helm");
			UE_LOG(LogSpaceGod, Display, TEXT("sg.WarpRun: for %s, %.2f ly"), UTF8_TO_TCHAR(best->Name.c_str()), km / Astro::LightYearKm);
			return true;
		} });
		auto beat = [run, shots](const TCHAR* what, std::function<bool(SpaceGod::UnitySpaceApp&)> when, const TCHAR* file)
		{
			run->steps.push_back({ what, [when](SpaceGod::UnitySpaceApp& app, double) { return when(app); } });
			run->steps.push_back({ TEXT("shot"), Shot(shots, file) });
		};
		beat(TEXT("spooling, streaks building"), [](SpaceGod::UnitySpaceApp& app) { return app.Ftl->Stage == FtlStage::Spooling && app.SkyWarp() >= 0.4f; }, TEXT("warprun-1-spooling.png"));
		beat(TEXT("near full strength"), [](SpaceGod::UnitySpaceApp& app) { return app.SkyWarp() >= 0.92f || app.InFtlCruise(); }, TEXT("warprun-2-peak.png"));
		auto engaged = std::make_shared<double>(-1);
		beat(TEXT("engaged: the crossing under the Milky Way"), [engaged](SpaceGod::UnitySpaceApp& app)
		{
			if (!app.InFtlCruise()) return false;
			double now = FPlatformTime::Seconds();
			if (*engaged < 0) *engaged = now;
			return now - *engaged > 2.5;
		}, TEXT("warprun-3-crossing.png"));
		auto arrived = std::make_shared<double>(-1);
		beat(TEXT("dropped out"), [arrived](SpaceGod::UnitySpaceApp& app)
		{
			if (app.InFtlCruise()) return false;
			double now = FPlatformTime::Seconds();
			if (*arrived < 0) *arrived = now;
			return now - *arrived > 0.15;
		}, TEXT("warprun-4-drop-out.png"));
		run->steps.push_back({ TEXT("arrived"), Wait(3.5) });
		run->steps.push_back({ TEXT("shot"), Shot(shots, TEXT("warprun-5-arrived.png")) });
		run->steps.push_back({ TEXT("written"), Wait(1.0) });
		run->Go(run);
	}

	FAutoConsoleCommand GSkyModeCommand(TEXT("sg.SkyMode"),
		TEXT("US-T145: how systems' skies are drawn: sg.SkyMode cube | sphere | volume | next (no argument: says which)"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& a) { SkyModeCommand(a); }));
	FAutoConsoleCommand GSkyVolumeCommand(TEXT("sg.SkyVolume"),
		TEXT("US-T145: tune the painted volume: sg.SkyVolume [density=14] [brightness=1] [steps=40]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& a) { SkyVolumeCommand(a); }));
	FAutoConsoleCommand GSkyCompareCommand(TEXT("sg.SkyCompare"),
		TEXT("US-T145: the three skies in place in the hero system, timed: sg.SkyCompare [real] [shots <dir>] [quit]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& a) { SkyCompareCommand(a); }));
	FAutoConsoleCommand GEdgeRunCommand(TEXT("sg.EdgeRun"),
		TEXT("US-T145: fly a hull out through the system's rim into open space and back: sg.EdgeRun [real] [cube|sphere|volume] [shots <dir>] [quit]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& a) { EdgeRunCommand(a); }));
	FAutoConsoleCommand GWarpTunnelCommand(TEXT("sg.WarpTunnel"),
		TEXT("US-T145: preview the jump's tunnel: sg.WarpTunnel [0..1 | off] [milkyway]; sg.WarpTunnel demo plays it building and the Milky Way, sg.WarpTunnel shots <dir> [quit] shoots each beat"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& a) { WarpTunnelCommand(a); }));
	FAutoConsoleCommand GWarpRunCommand(TEXT("sg.WarpRun"),
		TEXT("US-T145: a real jump from beyond the rim to the nearest star on the FTL autopilot: sg.WarpRun [shots <dir>] [quit]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& a) { WarpRunCommand(a); }));

	/// The dev menu (US-T126): Travel > the sky's modes and its runs.
	void RegisterSkyCheats()
	{
		using SpaceGod::UI::CheatEntry;
		using SpaceGod::UI::CheatRegistry;
		CheatRegistry::Add("Travel", CheatEntry::Console("Sky: cube", "the system's painted skybox at infinity (US-T145)", "sg.SkyMode cube").At(60));
		CheatRegistry::Add("Travel", CheatEntry::Console("Sky: sphere", "the hero sky sphere, Alien Space Nebula 1 (US-T145)", "sg.SkyMode sphere").At(61));
		CheatRegistry::Add("Travel", CheatEntry::Console("Sky: volume", "the Milky Way behind the system's painting as a cloud the size of its rim (US-T145)", "sg.SkyMode volume").At(62));
		CheatRegistry::Add("Travel", CheatEntry::Console("Sky: compare in the hero system", "cube, sphere and volume in place, timed (US-T145)", "sg.SkyCompare").At(63));
		CheatRegistry::Add("Travel", CheatEntry::Console("Edge run", "a hull out through the rim into open space and back (US-T145)", "sg.EdgeRun").At(64));
		CheatRegistry::Add("Travel", CheatEntry::Console("Jump tunnel preview", "the jump drive's streaks held round the view (sg.WarpTunnel off ends it) (US-T145)", "sg.WarpTunnel 0.8").At(65));
		CheatRegistry::Add("Travel", CheatEntry::Console("Milky Way sky", "the sky between the stars, band and bulge where they are (sg.WarpTunnel off ends it) (US-T145)", "sg.WarpTunnel 0 milkyway").At(66));
		CheatRegistry::Add("Travel", CheatEntry::Console("Real jump to the nearest star", "a drive hull beyond the rim on the FTL autopilot (US-T145)", "sg.WarpRun").At(67));
	}
	SpaceGod::UI::CheatRegistration GSkyCheats(&RegisterSkyCheats);
}
