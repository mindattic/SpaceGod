// US-T218: three staged checks for features best judged in the real renderer, each a step list run once a frame on the
// core ticker (as sg.WarpTunnel), logging the numbers that say whether the feature works and, with shot, screenshots:
//   sg.Ghost [shot <file>] [quit]        the occluder ghost (US-T185 / US-T206): a hull between the camera and a focused one
//   sg.LaneRide [shot <dir>] [quit]      steering inside a space lane's tube and the speed bled off past its wall (US-T196)
//   sg.CapitalRoll [shot <dir>] [quit]   a capital rolling its strongest shield face toward its main threat (US-T187)
// and US-T224's two more, the same way:
//   sg.CruiseRun [shot <dir>] [quit]     the cruise drive at a lone helm: spooled, steered at cruise, dropped (US-T127, US-T197)
//   sg.Squadron [shot <dir>] [quit]      a carrier squadron in formation, breaking on enemy fighters, re-formed (US-T198, US-T188)

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "GameLaunch.h"
#include "SceneTour.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Smoke/ScriptedDevice.h"
#include "Target.h"
#include "UnityCompat/ShaderBinding.h"
#include "UnitySpaceAppActor.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Ships.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/Broadsides.h"
#include "Combat/CapitalManeuver.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/DogfightManeuvers.h"
#include "Combat/Dogfighting.h"
#include "Combat/DriveRegime.h"
#include "Combat/Fighters.h"
#include "Combat/FlightDecks.h"
#include "Combat/Formations.h"
#include "Combat/LaneFlight.h"
#include "Combat/SystemCruise.h"
#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Combat/WeakPatches.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/ShipHelm.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
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
	AUnitySpaceAppActor* StageActor()
	{
		if (!GEngine) return nullptr;
		for (const FWorldContext& c : GEngine->GetWorldContexts())
		{
			if (c.WorldType != EWorldType::Game && c.WorldType != EWorldType::PIE) continue;
			auto* actor = AUnitySpaceAppActor::Find(c.World());
			if (actor != nullptr && actor->GetApp() != nullptr) return actor;
		}
		return nullptr;
	}

	UnitySpaceApp* StageApp()
	{
		auto* a = StageActor();
		return a != nullptr ? a->GetApp() : nullptr;
	}

	FString T(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

	/// One staged run, stepped once a frame on the core ticker: each step returns true when done. While Keys is in use the
	/// app reads the run's keyboard rather than the real one (the smoke's ScriptedDevice).
	struct Stage
	{
		using Step = std::function<bool(UnitySpaceApp&, double)>;
		const TCHAR* name = TEXT("sg.Stage");
		std::vector<std::pair<FString, Step>> steps;
		size_t at = 0;
		double stepStart = -1;
		double started = FPlatformTime::Seconds();
		double timeout = 120.0;
		bool askedNewGame = false, quit = false, scripted = false;
		SpaceGod::Smoke::ScriptedDevice Keys;
		std::function<void(UnitySpaceApp*)> onFinish;

		void Script()
		{
			auto* a = StageActor();
			if (a == nullptr) return;
			if (!a->bScriptedInput) SpaceGod::Input::InputDevice::current().Clear();
			a->bScriptedInput = true;
			scripted = true;
		}

		bool Tick()
		{
			auto* app = StageApp();
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
			// The keys the steps hold, read by the app's next frame.
			if (scripted)
			{
				Keys.Write();
				Keys.EndFrame();
			}
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("%s: %s"), name, failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("%s: done"), name); }
			if (scripted)
			{
				SpaceGod::Input::InputDevice::current().Clear();
				if (auto* a = StageActor()) a->bScriptedInput = false;
			}
			if (onFinish) onFinish(StageApp());
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}

		void Go(std::shared_ptr<Stage> self)
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([self](float)
			{
				try { return self->Tick(); }
				catch (const std::exception& e) { return self->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
			}));
		}
	};

	Stage::Step Wait(double seconds) { return [seconds](UnitySpaceApp&, double t) { return t >= seconds; }; }

	void Shoot(const TCHAR* who, const FString& path, bool ui = false)
	{
		if (path.IsEmpty()) return;
		FScreenshotRequest::RequestScreenshot(path, ui, false);
		UE_LOG(LogSpaceGod, Display, TEXT("%s: screenshot %s"), who, *path);
	}

	/// The common args: shot &lt;path&gt; and quit.
	void ParseArgs(const TArray<FString>& args, FString& shot, bool& quit)
	{
		for (int i = 0; i < args.Num(); i++)
		{
			if (args[i].Equals(TEXT("shot"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) shot = args[++i];
			else if (args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
		}
	}

	void ToCommandView(UnitySpaceApp& app)
	{
		app.SkyLeaveHelm();
		if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
	}

	// ------------------------------------------------------------------------------------------------ sg.Ghost

	/// What a model's renderers are drawn with now: each material's name, its shader and the Unreal material behind it.
	FString MaterialsOf(UnitySpaceApp& app, const cs::Ref<Ship>& ship)
	{
		using SpaceGod::UnityCompat::ShaderBinding;
		auto renderers = app.ModelRenderersOf(Target::OfShip(ship));
		FString out = FString::Printf(TEXT("%d renderers"), renderers.Count());
		TMap<FString, int> seen;
		for (const auto& r : renderers)
		{
			if (!r) continue;
			for (const auto& m : r->sharedMaterials())
			{
				FString key;
				if (!m) key = TEXT("(none)");
				else
				{
					UMaterialInterface* parent = m->shader ? ShaderBinding::Parent(m->shader.get()) : nullptr;
					key = FString::Printf(TEXT("'%s' shader %s -> %s%s"), *T(m->name), m->shader ? *T(ShaderBinding::ShaderName(m->shader.get())) : TEXT("(none)"),
						parent ? *parent->GetPathName() : TEXT("(no Unreal material)"),
						m->shader && !ShaderBinding::IsReal(m->shader.get()) ? TEXT(" [PLACEHOLDER]") : TEXT(""));
					if (m->HasProperty("_GhostColor"))
					{
						auto c = m->Properties.GetVector("_GhostColor");
						key += FString::Printf(TEXT(" _GhostColor (%.2f, %.2f, %.2f, %.2f)"), c.x, c.y, c.z, c.w);
					}
				}
				seen.FindOrAdd(key)++;
			}
		}
		for (const auto& kv : seen) out += FString::Printf(TEXT("; %d x %s"), kv.Value, *kv.Key);
		return out;
	}

	/// <c>sg.Ghost [shot &lt;file&gt;] [quit]</c>: a destroyer and, a little nearer the camera on the same line, a cruiser, held
	/// still in open space; the camera focused on the destroyer from beyond the cruiser, so the cruiser hides it and
	/// UpdateOccluderGhost draws it as the ghost. Logs what each hull's renderers carry, shoots the view, then lets the
	/// focus go (the cruiser's own materials back) and shoots the same pose again as &lt;file&gt;-opaque.png.
	void GhostCommand(const TArray<FString>& args)
	{
		FString shot;
		bool quit = false;
		ParseArgs(args, shot, quit);
		auto run = std::make_shared<Stage>();
		run->name = TEXT("sg.Ghost");
		run->quit = quit;
		struct Pair { cs::Ref<Ship> Far, Near; int Body = 0; DVec3 At; DVec3 Along = DVec3::UnitX; double Km = 4; };
		auto p = std::make_shared<Pair>();
		auto hold = [p](UnitySpaceApp& app)
		{
			// Nothing takes them anywhere while they are looked at.
			for (const auto& s : { p->Far, p->Near })
				if (s) { s->Phase = ShipPhase::Parked; s->PhaseEndJd = app.Clock->Jd + 1; }
		};
		run->steps.push_back({ TEXT("game up"), Wait(6.0) });
		run->steps.push_back({ TEXT("two hulls staged in open space"), [p](UnitySpaceApp& app, double)
		{
			ToCommandView(app);
			app.Clock->SetPaused(true);
			// Well clear of every world: 1.3 AU from the Sun, off the ecliptic a little.
			p->Body = app.Catalog()->Root()->Index;
			p->At = DVec3(0.35, 1.25, 0.04) * Astro::AuKm;
			p->Far = app.Fleet()->SpawnAt("Ghost Check Far", ShipClass::Destroyer, app.World()->Sol, p->Body, p->At, DVec3::UnitY, app.Clock->Jd);
			p->Near = app.Fleet()->SpawnAt("Ghost Check Near", ShipClass::Cruiser, app.World()->Sol, p->Body, p->At + p->Along * p->Km, DVec3::UnitY, app.Clock->Jd);
			if (!p->Far || !p->Near) throw std::runtime_error("the hulls would not spawn");
			return true;
		} });
		// Spaced so the near hull is clear of the far one as they are drawn (the scale's own hull size, not the true one).
		run->steps.push_back({ TEXT("spaced as drawn"), [p, hold](UnitySpaceApp& app, double t)
		{
			hold(app);
			if (t < 0.5) return false;
			double sep = (app.ShipScene(p->Near) - app.ShipScene(p->Far)).Length();
			double lf = app.ShipDisplayLength(p->Far), ln = app.ShipDisplayLength(p->Near);
			double want = lf * 1.3 + ln * 0.8;
			if (sep <= 0 || want <= 0) return t > 5;
			// Only ever pushed further apart: hulls drawn close are kept apart by the drawing itself, whatever their true km.
			if (sep >= want * 0.9 || t > 5)
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Ghost: far %s (drawn %.4g), near %s (drawn %.4g), %.4g apart in the scene (%.2f km true)"),
					*T(p->Far->Name), lf, *T(p->Near->Name), ln, sep, p->Km);
				return true;
			}
			p->Km *= want / sep;
			p->Near->Orbit->Center = p->At + p->Along * p->Km;
			return false;
		} });
		run->steps.push_back({ TEXT("both drawn as models"), [p, hold](UnitySpaceApp& app, double t)
		{
			hold(app);
			// From beyond the near hull, on the line through both, looking at the far one.
			auto far = app.FocusScene(Target::OfShip(p->Far)), near = app.FocusScene(Target::OfShip(p->Near));
			auto out = (near - far).Normalized();
			double back = (near - far).Length() + app.ShipDisplayLength(p->Near) * 2.2;
			auto eye = far + out * back;
			auto d = (far - eye).Normalized();
			app.SpaceCamera->SetPose(eye, SpaceGod::Quaternion::LookRotation(SpaceGod::Vector3(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z)), SpaceGod::Vector3::up()));
			bool drawn = app.ModelRenderersOf(Target::OfShip(p->Far)).Count() > 0 && app.ModelRenderersOf(Target::OfShip(p->Near)).Count() > 0;
			return (drawn && t > 1.0) || t > 20;
		} });
		run->steps.push_back({ TEXT("focused on the far hull from behind the near one"), [p, hold](UnitySpaceApp& app, double)
		{
			hold(app);
			auto far = app.FocusScene(Target::OfShip(p->Far)), near = app.FocusScene(Target::OfShip(p->Near));
			double back = (near - far).Length() + app.ShipDisplayLength(p->Near) * 2.2;
			auto t = Target::OfShip(p->Far);
			// No pitch given: the focus comes in from the side the camera is already on, the near hull's.
			app.SpaceCamera->FocusOn(t, back / cs::Math::Max(app.TargetRadius(t), 1e-9));
			return true;
		} });
		run->steps.push_back({ TEXT("settled in focus"), [p, hold](UnitySpaceApp& app, double t)
		{
			hold(app);
			return (app.SpaceCamera->Mode == SpaceGod::Cameras::SpaceCamera::CameraMode::Focus && t > 3.0) || t > 15;
		} });
		run->steps.push_back({ TEXT("ghost read"), [p, hold](UnitySpaceApp& app, double)
		{
			hold(app);
			auto cam = app.SpaceCamera->ScenePosition;
			auto far = app.FocusScene(Target::OfShip(p->Far)), near = app.FocusScene(Target::OfShip(p->Near));
			auto dir = (far - cam).Normalized();
			double off = ((near - cam) - dir * DVec3::Dot(near - cam, dir)).Length();
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Ghost: camera %s on %s, %.4g from it; the near hull %.4g from the camera, %.4g off the line of sight (drawn length %.4g)"),
				UTF8_TO_TCHAR(SpaceGod::Cameras::ToString(app.SpaceCamera->Mode)), *T(p->Far->Name), (far - cam).Length(), (near - cam).Length(), off, app.ShipDisplayLength(p->Near));
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Ghost: occluder %s while ghosted: %s"), *T(p->Near->Name), *MaterialsOf(app, p->Near));
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Ghost: focused %s: %s"), *T(p->Far->Name), *MaterialsOf(app, p->Far));
			return true;
		} });
		run->steps.push_back({ TEXT("shot ghosted"), [shot](UnitySpaceApp&, double) { Shoot(TEXT("sg.Ghost"), shot); return true; } });
		run->steps.push_back({ TEXT("written"), Wait(1.5) });
		// The same pose with the focus let go: nothing is ghosted, the cruiser drawn as it is.
		run->steps.push_back({ TEXT("focus let go"), [p, hold](UnitySpaceApp& app, double t)
		{
			hold(app);
			if (t == 0 || app.SpaceCamera->Mode != SpaceGod::Cameras::SpaceCamera::CameraMode::Free) app.SpaceCamera->ExitFocus();
			if (t < 1.0) return false;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Ghost: occluder %s with the focus let go: %s"), *T(p->Near->Name), *MaterialsOf(app, p->Near));
			return true;
		} });
		run->steps.push_back({ TEXT("shot opaque"), [shot](UnitySpaceApp&, double)
		{
			if (!shot.IsEmpty()) Shoot(TEXT("sg.Ghost"), FPaths::Combine(FPaths::GetPath(shot), FPaths::GetBaseFilename(shot) + TEXT("-opaque.png")));
			return true;
		} });
		run->steps.push_back({ TEXT("written"), Wait(1.5) });
		// And with the near hull moved off the line: what the far one looks like with nothing in front of it, to compare.
		run->steps.push_back({ TEXT("near hull moved aside"), [p, hold](UnitySpaceApp& app, double t)
		{
			hold(app);
			if (t == 0 || p->Near->Orbit->Center.Z < p->At.Z + 1000) p->Near->Orbit->Center = p->At + DVec3(0, 0, 5000);
			return t >= 1.0;
		} });
		run->steps.push_back({ TEXT("shot far hull alone"), [shot](UnitySpaceApp&, double)
		{
			if (!shot.IsEmpty()) Shoot(TEXT("sg.Ghost"), FPaths::Combine(FPaths::GetPath(shot), FPaths::GetBaseFilename(shot) + TEXT("-far-alone.png")));
			return true;
		} });
		run->steps.push_back({ TEXT("written"), Wait(1.5) });
		run->onFinish = [](UnitySpaceApp* app) { if (app) app->Clock->SetPaused(false); };
		run->Go(run);
	}

	// ------------------------------------------------------------------------------------------------ sg.LaneRide

	/// <c>sg.LaneRide [shot &lt;dir&gt;] [quit]</c>: a fighter at the helm just off a lane's on-ramp, flown in on W; inside the
	/// tube D, A and E nudge it across (its offset off the centre line and its speed logged every half second), then once the
	/// lane has it near its pace D is held until it is out past the wall, and its speed is logged every quarter second as it
	/// bleeds down to its own. Shots: lane-1-centre.png, lane-2-offset.png (in the tube), lane-3-exit.png, lane-4-after.png.
	void LaneRideCommand(const TArray<FString>& args)
	{
		FString dir;
		bool quit = false;
		ParseArgs(args, dir, quit);
		auto run = std::make_shared<Stage>();
		run->name = TEXT("sg.LaneRide");
		run->quit = quit;
		run->timeout = 90;
		struct Ride { cs::Ref<Ship> Ship; cs::Ref<Lane> Lane; double NextLog = 0; double OwnKmS = 0; double ExitedAt = -1; };
		auto r = std::make_shared<Ride>();
		auto shotIn = [dir](const TCHAR* file) { if (!dir.IsEmpty()) Shoot(TEXT("sg.LaneRide"), FPaths::Combine(dir, file)); };
		auto log = [r](UnitySpaceApp& app, const TCHAR* what, double t)
		{
			const auto& f = app.RideFlight();
			const auto& w = app.LaneWakeNow();
			UE_LOG(LogSpaceGod, Display, TEXT("sg.LaneRide: %s t=%.2f riding %d: offset right %.0f km up %.0f km (%.0f of %.0f km), across %.0f / %.0f km/s; speed %.1f km/s (own top %.1f), ride pace %.2f; wake %s %.2f/%.2f s %.1f -> %.1f km/s"),
				what, t, app.RidingLane() ? 1 : 0, f.RightKm, f.UpKm, f.OffsetKm(), LaneFlight::TubeRadiusKm(), f.RightKmS, f.UpKmS,
				app.PilotSpeedKmS(), r->OwnKmS, app.LaneRideSpeed, w.Active() ? TEXT("on") : TEXT("off"), w.ElapsedS, w.DurationS, w.FromKmS, w.ToKmS);
		};
		run->steps.push_back({ TEXT("game up"), Wait(6.0) });
		run->steps.push_back({ TEXT("a fighter at the helm just off an on-ramp"), [run, r](UnitySpaceApp& app, double)
		{
			ToCommandView(app);
			app.Clock->SetPaused(false);
			const auto& lanes = app.Fleet()->Lanes;
			if (lanes == nullptr) throw std::runtime_error("no lanes in this system");
			auto busy = [&app](int body)
			{
				for (const auto& b : app.World()->War->Battles)
					if (!b->Over && b->Body == body) return true;
				return false;
			};
			cs::Ref<Lane> lane;
			for (const auto& l : lanes->Lanes)
				if (lanes->Carries(l) && !l->IsGateSpur() && !busy(l->A) && !busy(l->B)) { lane = l; break; }
			if (lane == nullptr) throw std::runtime_error("no lane carrying traffic clear of a fight");
			double jd = app.Clock->Jd;
			auto way = lanes->RampDirection(lane, lane->A, jd);
			auto ramp = lanes->RampOffset(lane, lane->A, jd);
			r->Lane = lane;
			r->Ship = app.Fleet()->SpawnAt("Lane Check Fighter", ShipClass::Fighter, app.World()->Sol, lane->A, ramp + way * (Hyperlanes::GateRadiusKm + 0.5), -way, jd);
			r->Ship->Ports = app.World()->FreeShipPorts();
			r->OwnKmS = PilotTuning::Of(ShipClass::Fighter).MaxSpeed;
			app.Select(Target::OfShip(r->Ship));
			app.TogglePiloting();
			if (!app.IsPiloting()) throw std::runtime_error("the helm was not taken");
			UE_LOG(LogSpaceGod, Display, TEXT("sg.LaneRide: the %s lane from %s, tube radius %.0f km"), *T(lane->Name), *T((*app.Catalog())[lane->A]->Name), LaneFlight::TubeRadiusKm());
			run->Script();
			return true;
		} });
		// Hands off a moment first: a key held across taking the helm is not the new hull's (ArbitrateInput).
		run->steps.push_back({ TEXT("hands off at the helm"), [run](UnitySpaceApp&, double t)
		{
			if (t < 0.5) return false;
			run->Keys.Hold(SpaceGod::Input::Key::W);
			return true;
		} });
		run->steps.push_back({ TEXT("flown in through the ring"), [run, r](UnitySpaceApp& app, double t)
		{
			if (t >= r->NextLog)
			{
				r->NextLog = t + 1.0;
				double off = (app.PlayerHelioKm() - app.Fleet()->Lanes->RampHelio(r->Lane, r->Lane->A, app.Clock->Jd)).Length();
				UE_LOG(LogSpaceGod, Display, TEXT("sg.LaneRide: t=%.0f W held (stick forward %.2f, device W %d), %.1f km/s, %.0f km from the ramp, piloting %d"), t,
					app.Input.Move.z, SpaceGod::Input::InputDevice::current()[SpaceGod::Input::Key::W].isPressed ? 1 : 0, app.PilotSpeedKmS(), off, app.IsPiloting() ? 1 : 0);
			}
			if (!app.RidingLane()) return false;
			r->NextLog = 0;
			run->Keys.Release(SpaceGod::Input::Key::W);
			return true;
		} });
		run->steps.push_back({ TEXT("on the centre line"), [log, shotIn](UnitySpaceApp& app, double t)
		{
			if (t < 3.0) return false;
			log(app, TEXT("centre"), t);
			shotIn(TEXT("lane-1-centre.png"));
			return true;
		} });
		// D for 0.7 s, let it drift still; A for 0.7 s back; E for 0.5 s up; logged each half second.
		run->steps.push_back({ TEXT("steered about inside the tube"), [run, r, log](UnitySpaceApp& app, double t)
		{
			using SpaceGod::Input::Key;
			auto& k = run->Keys;
			auto key = [&k](Key which, bool on) { if (on) k.Hold(which); else k.Release(which); };
			key(Key::D, t < 0.7);
			key(Key::A, t >= 2.5 && t < 3.2);
			key(Key::E, t >= 5.0 && t < 5.5);
			if (t >= r->NextLog)
			{
				r->NextLog = t + 0.5;
				log(app, k.held.count(Key::D) ? TEXT("D held") : k.held.count(Key::A) ? TEXT("A held") : k.held.count(Key::E) ? TEXT("E held") : TEXT("hands off"), t);
			}
			if (!app.RidingLane()) throw std::runtime_error("left the lane while steering inside it");
			if (t < 7.0) return false;
			r->NextLog = 0;
			return true;
		} });
		run->steps.push_back({ TEXT("shot off the centre line"), [shotIn](UnitySpaceApp&, double) { shotIn(TEXT("lane-2-offset.png")); return true; } });
		// On until the lane has it at most of its pace, so there is speed to carry out.
		run->steps.push_back({ TEXT("near the lane's pace"), [r, log](UnitySpaceApp& app, double t)
		{
			if (t >= r->NextLog) { r->NextLog = t + 1.0; log(app, TEXT("riding"), t); }
			if (!app.RidingLane()) throw std::runtime_error("the ride ended before the lane got up to speed");
			return app.LaneRideSpeed >= 0.6f || t > 25;
		} });
		run->steps.push_back({ TEXT("out past the tube's wall"), [run, r, log](UnitySpaceApp& app, double t)
		{
			run->Keys.Hold(SpaceGod::Input::Key::D);
			if (app.RidingLane())
			{
				if (t >= r->NextLog) { r->NextLog = t + 0.5; log(app, TEXT("D held for the wall"), t); }
				return false;
			}
			run->Keys.Release(SpaceGod::Input::Key::D);
			log(app, TEXT("out"), t);
			r->NextLog = 0;
			return true;
		} });
		run->steps.push_back({ TEXT("speed bled down to its own"), [r, log, shotIn](UnitySpaceApp& app, double t)
		{
			if (t >= r->NextLog) { r->NextLog = t + 0.25; log(app, TEXT("wake"), t); }
			if (r->ExitedAt < 0 && t >= 0.3) { r->ExitedAt = t; shotIn(TEXT("lane-3-exit.png")); }
			if (t >= 6.0) { shotIn(TEXT("lane-4-after.png")); return true; }
			return false;
		} });
		run->steps.push_back({ TEXT("written"), Wait(1.5) });
		run->Go(run);
	}

	// ------------------------------------------------------------------------------------------------ sg.CapitalRoll

	cs::List<ShipClass> RollFleet()
	{
		return cs::List<ShipClass>{
			ShipClass::Carrier, ShipClass::Battleship, ShipClass::Cruiser,
			ShipClass::Destroyer, ShipClass::Destroyer, ShipClass::Frigate, ShipClass::Corvette, ShipClass::Corvette,
			ShipClass::Fighter, ShipClass::Fighter, ShipClass::Fighter, ShipClass::Interceptor, ShipClass::Interceptor,
		};
	}

	double Deg(double rad) { return rad * 180.0 / cs::Math::PI; }

	/// <c>sg.CapitalRoll [shot &lt;dir&gt;] [seconds=60] [preroll=90] [quit]</c>: the big battle's fight off Uranus (sg.BigBattle's),
	/// run on preroll battle seconds so shields are worn by face, the camera on the capital the battle's AI flies that takes
	/// the most fire; each second its roll off level, the bearing of the weight of fire on it, the field of the face it shows
	/// that fire against its strongest face's and against every other roll it could be at, and the roll it has chosen; a
	/// shot every 5 s.
	void CapitalRollCommand(const TArray<FString>& args)
	{
		FString dir;
		bool quit = false;
		double seconds = 60;
		ParseArgs(args, dir, quit);
		for (const auto& a : args)
			if (a.StartsWith(TEXT("seconds="), ESearchCase::IgnoreCase)) seconds = FMath::Max(5.0, FCString::Atod(*a.Mid(8)));
		auto run = std::make_shared<Stage>();
		run->name = TEXT("sg.CapitalRoll");
		run->quit = quit;
		run->timeout = seconds + 60;
		struct Watch
		{
			cs::Ref<Battle> Fight;
			cs::Ref<BattleUnit> Unit;
			double Preroll = 90;
			double NextLog = 0, NextShot = 3 /* the camera there first */, FirstRoll = cs::Double::NaN, Travelled = 0, PrevRoll = cs::Double::NaN;
			int Shots = 0, Samples = 0, Shown = 0, Fired = 0, Choosy = 0;
			double ShownSum = 0, BestSum = 0, Placed = 0;
		};
		auto w = std::make_shared<Watch>();
		for (const auto& a : args)
			if (a.StartsWith(TEXT("preroll="), ESearchCase::IgnoreCase)) w->Preroll = FMath::Max(0.0, FCString::Atod(*a.Mid(8)));
		run->steps.push_back({ TEXT("game up"), Wait(6.0) });
		run->steps.push_back({ TEXT("the big battle off Uranus"), [w](UnitySpaceApp& app, double)
		{
			ToCommandView(app);
			auto uranus = (*app.Catalog())["Uranus"];
			w->Fight = SpaceGod::SceneTour::TriggerSkirmish(app, uranus != nullptr ? uranus->Index : 0, app.World()->Sol, app.World()->Reach, RollFleet());
			if (w->Fight == nullptr) throw std::runtime_error("the battle did not start");
			// On past contact (sg.BigBattle's 20 s) until the capitals' shields are worn by face, so the roll has a face to choose.
			w->Fight->Advance(w->Preroll, false);
			app.Clock->SetPaused(false);
			app.WatchBattle(w->Fight);
			return true;
		} });
		run->steps.push_back({ TEXT("following a capital"), [w](UnitySpaceApp& app, double)
		{
			// The capital taking the most fire (the one with shields worn by face to show), the biggest among equals.
			cs::Ref<BattleUnit> best;
			double bestFire = -1;
			for (const auto& u : w->Fight->Units)
			{
				if (!u->Present() || !CapitalManeuver::Flies(u)) continue;
				double fire = 0;
				for (const auto& f : WeakPatches::IncomingFireOn(w->Fight, u)) fire += f.Weight;
				auto faces = CapitalManeuver::Facings(u->Combat);
				fire += 10 * (1 - faces.Of(faces.Weakest()));
				if (fire > bestFire + 1e-9 || (fire > bestFire - 1e-9 && best != nullptr && u->LengthKm > best->LengthKm)) { best = u; bestFire = fire; }
			}
			if (best == nullptr) throw std::runtime_error("no capital the battle flies");
			w->Unit = best;
			auto t = best->Ship != nullptr ? Target::OfShip(best->Ship) : Target::OfUnit(w->Fight, best);
			app.SpaceCamera->FocusOn(t, 3.5);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.CapitalRoll: following %s (%s, %.2f km), roll rate %.1f deg/s, spin-up %.2f deg/s^2"),
				best->Ship ? *T(best->Ship->Name) : TEXT("a unit"), best->Ship && best->Ship->Spec ? UTF8_TO_TCHAR(ToString(best->Ship->Spec->Class)) : TEXT("?"),
				best->LengthKm, CapitalManeuver::RollRateDegS(best), Deg(CapitalManeuver::RollAlpha(best)));
			return true;
		} });
		run->steps.push_back({ TEXT("watched"), [w, dir, seconds](UnitySpaceApp& app, double t)
		{
			auto u = w->Unit;
			if (w->Fight->Over || u == nullptr || !u->Present())
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.CapitalRoll: the capital is gone or the fight over at t=%.0f"), t);
				return true;
			}
			if (t >= w->NextShot && !dir.IsEmpty())
			{
				w->NextShot = t + 5.0;
				Shoot(TEXT("sg.CapitalRoll"), FPaths::Combine(dir, FString::Printf(TEXT("roll-%02d.png"), w->Shots++)));
			}
			if (t < w->NextLog) return t >= seconds;
			w->NextLog = t + 1.0;
			auto f = u->Forward.LengthSquared() > 1e-18 ? u->Forward.Normalized() : DVec3::UnitX;
			auto roof = u->Roof.LengthSquared() > 1e-12 ? u->Roof : WeakPatches::LevelRoof(f);
			double roll = Deg(WeakPatches::RollAngle(WeakPatches::LevelRoof(f), roof, f));
			if (cs::Double::IsNaN(w->FirstRoll)) w->FirstRoll = roll;
			if (!cs::Double::IsNaN(w->PrevRoll))
			{
				double d = roll - w->PrevRoll;
				while (d > 180) d -= 360;
				while (d < -180) d += 360;
				w->Travelled += cs::Math::Abs(d);
			}
			w->PrevRoll = roll;
			auto threat = CapitalManeuver::ThreatDir(w->Fight, u);
			auto faces = CapitalManeuver::Facings(u->Combat);
			auto strongest = faces.Strongest();
			auto cmd = Broadsides::Of(u);
			FString chosen = cmd != nullptr && cmd->RollChosen ? FString::Printf(TEXT("chosen roll %.0f deg, spin %.2f deg/s"), cmd->RollDeg, Deg(cmd->RollSpin)) : FString(TEXT("no roll chosen"));
			FString faceText = FString::Printf(TEXT("fore %.2f aft %.2f port %.2f stbd %.2f dorsal %.2f ventral %.2f"), faces.Strength[0], faces.Strength[1], faces.Strength[2], faces.Strength[3], faces.Strength[4], faces.Strength[5]);
			w->Samples++;
			if (threat.LengthSquared() < 1e-18)
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.CapitalRoll: t=%.0f roll %+.1f deg, no fire on it; faces %s; %s"), t, roll, *faceText, *chosen);
				return t >= seconds;
			}
			DVec3 right, up;
			u->Basis(right, up);
			DVec3 local(DVec3::Dot(threat, right), DVec3::Dot(threat, up), DVec3::Dot(threat, f));
			auto toward = CapitalManeuver::FacingToward(u, threat);
			double shown = faces.Toward(local), best = faces.Of(strongest);
			// The field the threat would meet at every other roll about the nose (the heading as it is): where in that range
			// the hull's own roll puts it, 1 = the best roll it could show, 0 = the worst.
			double rollBest = -1, rollWorst = 2, rollBestDeg = 0;
			for (int k = 0; k < 24; k++)
			{
				double a = k * 15.0 * cs::Math::PI / 180.0, c = cs::Math::Cos(a), s = cs::Math::Sin(a);
				double field = faces.Toward(DVec3(local.X * c - local.Y * s, local.X * s + local.Y * c, local.Z));
				if (field > rollBest) { rollBest = field; rollBestDeg = k * 15.0; }
				rollWorst = cs::Math::Min(rollWorst, field);
			}
			double placed = rollBest - rollWorst > 1e-6 ? (shown - rollWorst) / (rollBest - rollWorst) : 1;
			w->Fired++;
			w->ShownSum += shown;
			w->BestSum += best;
			if (toward == strongest || faces.Of(toward) >= best - 1e-6) w->Shown++;
			if (rollBest - rollWorst >= 0.03) { w->Choosy++; w->Placed += placed; }
			UE_LOG(LogSpaceGod, Display, TEXT("sg.CapitalRoll: t=%.0f roll %+.1f deg; threat bearing %+.0f deg el %+.0f deg (meets %s %.2f, field shown %.2f; over every roll %.2f..%.2f, best %+.0f deg away: placed %.2f); strongest %s %.2f, weakest %s %.2f; faces %s; %s"),
				t, roll, Deg(cs::Math::Atan2(local.X, local.Z)), Deg(cs::Math::Asin(cs::Math::Max(-1.0, cs::Math::Min(1.0, local.Y)))),
				UTF8_TO_TCHAR(ToString(toward)), faces.Of(toward), shown, rollWorst, rollBest, rollBestDeg > 180 ? rollBestDeg - 360 : rollBestDeg, placed,
				UTF8_TO_TCHAR(ToString(strongest)), best, UTF8_TO_TCHAR(ToString(faces.Weakest())), faces.Of(faces.Weakest()), *faceText, *chosen);
			return t >= seconds;
		} });
		run->onFinish = [w](UnitySpaceApp*)
		{
			if (w->Samples == 0) return;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.CapitalRoll: summary: %d samples, %d under fire; roll %+.1f -> %+.1f deg, %.0f deg rolled in all; the face toward the threat was the strongest (or as strong) in %d of %d; mean field shown %.2f against the strongest face's %.2f; where the roll had a choice (%d samples with 0.03 or more between the best and worst roll) it was placed %.2f of the way from the worst roll to the best"),
				w->Samples, w->Fired, w->FirstRoll, w->PrevRoll, w->Travelled, w->Shown, w->Fired, w->Fired > 0 ? w->ShownSum / w->Fired : 0.0, w->Fired > 0 ? w->BestSum / w->Fired : 0.0,
				w->Choosy, w->Choosy > 0 ? w->Placed / w->Choosy : 0.0);
		};
		run->Go(run);
	}

	// ------------------------------------------------------------------------------------------------ sg.CruiseRun

	/// The heading as a compass: yaw in the ecliptic from +X and elevation off it, degrees.
	FString HeadingText(DVec3 f)
	{
		return FString::Printf(TEXT("yaw %+.1f el %+.1f deg"), Deg(cs::Math::Atan2(f.Y, f.X)), Deg(cs::Math::Asin(cs::Math::Max(-1.0, cs::Math::Min(1.0, f.Z)))));
	}

	FString KmText(double km)
	{
		if (km >= 0.01 * Astro::AuKm) return FString::Printf(TEXT("%.3f AU"), km / Astro::AuKm);
		return FString::Printf(TEXT("%.0f km"), km);
	}

	/// <c>sg.CruiseRun [shot &lt;dir&gt;] [quit]</c>: a fighter at the helm (the hull a new game's Influence reaches) in open space 1.3 AU out, nose off the ecliptic so the
	/// cruise is a free one; Shift+J spools the drive, the stick turns it at cruise, Shift+J drops it out; then it is spooled
	/// twice more, once to take a hit on the shields at cruise and once to meet a hull parked across its path. Each half second
	/// the drive's regime, the drive's and the hull's speed, the heading and the distance covered; shots cruise-1-spool.png,
	/// cruise-2-steering.png, cruise-3-fast.png, cruise-4-dropped.png, cruise-5-shield.png, cruise-6-blocked.png.
	void CruiseRunCommand(const TArray<FString>& args)
	{
		FString dir;
		bool quit = false;
		ParseArgs(args, dir, quit);
		auto run = std::make_shared<Stage>();
		run->name = TEXT("sg.CruiseRun");
		run->quit = quit;
		run->timeout = 60;
		struct Run
		{
			cs::Ref<Ship> Ship, Obstacle;
			DVec3 From, Heading0;
			double NextLog = 0, EngagedAt = -1, TopKmS = 0, Turned = 0, SpoolS = 0, PrevT = -1;
			bool SpoolShot = false, Hit = false;
			int Checks = 0, Passed = 0;
			FString Failures;
		};
		auto r = std::make_shared<Run>();
		auto shotIn = [dir](const TCHAR* file) { if (!dir.IsEmpty()) Shoot(TEXT("sg.CruiseRun"), FPaths::Combine(dir, file), true); };
		auto expect = [r](bool ok, const TCHAR* what)
		{
			r->Checks++;
			if (ok) r->Passed++;
			else r->Failures += FString::Printf(TEXT(" [%s]"), what);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.CruiseRun: CHECK %s: %s"), ok ? TEXT("PASS") : TEXT("FAIL"), what);
		};
		auto log = [r](UnitySpaceApp& app, const TCHAR* what, double t)
		{
			const auto& c = app.SystemCruiseState();
			auto f = app.PilotForward();
			double turned = r->Heading0.LengthSquared() > 0 ? Deg(cs::Math::Acos(cs::Math::Max(-1.0, cs::Math::Min(1.0, DVec3::Dot(f, r->Heading0))))) : 0;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.CruiseRun: %s t=%.2f regime %s (%s%s), drive %.0f km/s (%.3f c), hull %.2f km/s; heading %s, %.1f deg off the start; %s covered; shields %.1f"),
				what, t, UTF8_TO_TCHAR(ToString(app.PilotDriveRegime())), UTF8_TO_TCHAR(ToString(c.Phase)),
				c.Spooling() ? *FString::Printf(TEXT(" %.0f%%, %.1f s left"), c.SpoolFraction() * 100, c.SpoolLeftS) : TEXT(""),
				c.SpeedKmS, c.SpeedKmS / SystemCruise::LightKmS, app.PilotSpeedKmS(), *HeadingText(f), turned, *KmText((app.PlayerHelioKm() - r->From).Length()),
				r->Ship && r->Ship->Combat ? r->Ship->Combat->Shield() : 0.0);
		};
		auto shiftJ = [run](UnitySpaceApp&, double t)
		{
			using SpaceGod::Input::Key;
			// Shift down a frame first, then J tapped under it (InputState: J pressed this frame with Shift held).
			if (t == 0 || !run->Keys.held.count(Key::LeftShift)) { run->Keys.Hold(Key::LeftShift); return false; }
			run->Keys.Tap(Key::J);
			return true;
		};
		auto shiftUp = [run](UnitySpaceApp&, double) { run->Keys.Release(SpaceGod::Input::Key::LeftShift); return true; };
		// Spooled until it engages: logged, and shot half way.
		auto spooled = [r, log, shotIn, expect](const TCHAR* label, bool shoot)
		{
			return [r, log, shotIn, expect, label, shoot](UnitySpaceApp& app, double t)
			{
				const auto& c = app.SystemCruiseState();
				if (t >= r->NextLog) { r->NextLog = t + 0.5; log(app, label, t); }
				if (shoot && !r->SpoolShot && c.Spooling() && c.SpoolFraction() >= 0.5) { r->SpoolShot = true; shotIn(TEXT("cruise-1-spool.png")); }
				if (c.Spooling() && r->SpoolS <= 0) r->SpoolS = c.SpoolS;
				if (app.PilotDriveRegime() == DriveRegime::Cruise)
				{
					log(app, TEXT("engaged"), t);
					r->NextLog = 0;
					r->EngagedAt = t;
					r->From = app.PlayerHelioKm();
					r->Heading0 = app.PilotForward();
					return true;
				}
				if (!app.SystemCruiseActive() && t > 0.3)
				{
					log(app, TEXT("dropped while spooling"), t);
					expect(false, TEXT("spooled up and engaged"));
					throw std::runtime_error("the drive was not spooling (blocked or dropped)");
				}
				return false;
			};
		};
		run->steps.push_back({ TEXT("game up"), Wait(6.0) });
		run->steps.push_back({ TEXT("a fighter at the helm in open space"), [run, r](UnitySpaceApp& app, double)
		{
			ToCommandView(app);
			app.Clock->SetPaused(false);
			int root = app.Catalog()->Root()->Index;
			auto at = DVec3(0.35, 1.25, 0.04) * Astro::AuKm;
			// Up out of the ecliptic, where no world's disc is near the nose: a free cruise, nothing to slow it.
			auto nose = DVec3(-0.55, 0.25, 0.8).Normalized();
			r->Ship = app.Fleet()->SpawnAt("Cruise Check", ShipClass::Fighter, app.World()->Sol, root, at, nose, app.Clock->Jd);
			if (!r->Ship) throw std::runtime_error("the fighter would not spawn");
			app.Select(Target::OfShip(r->Ship));
			app.TogglePiloting();
			if (!app.IsPiloting())
			{
				cs::Ref<IEmbodiable> helm = Embodiables::HelmOf(app.World(), r->Ship);
				auto verdict = Embodiables::MayEmbody(EmbodyContext::Of(app.World(), cs::List<cs::Ref<IEmbodiable>>()), helm);
				UE_LOG(LogSpaceGod, Warning, TEXT("sg.CruiseRun: not at the helm: selected a ship %d, at the controls %d, piloted none %d, can embody %d (%s), gate %d (%s)"),
					app.Selected.IsShip() ? 1 : 0, app.AtControls() ? 1 : 0, app.Piloted.IsNone() ? 1 : 0, helm && helm->CanEmbody() ? 1 : 0, helm ? *T(helm->Blocker()) : TEXT("no helm"),
					verdict.Ok ? 1 : 0, *T(verdict.Reason));
				throw std::runtime_error("the helm was not taken");
			}
			int emitters = 0;
			for (const auto& s : r->Ship->Combat->Subsystems)
				if (s->Kind() == SubsystemKind::ShieldGenerator) emitters++;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.CruiseRun: at the helm of the %s (%s), control %d, %d shield emitters, %.1f shields; heading %s; blocker '%s'; spool %.1f s"),
				*T(r->Ship->Name), UTF8_TO_TCHAR(ToString(r->Ship->Spec->Class)), static_cast<int>(app.PilotControlMode), emitters, r->Ship->Combat->Shield(),
				*HeadingText(app.PilotForward()), *T(app.SystemCruiseBlocker()), SystemCruise::SpoolSecondsFor(HullDynamics::Of(r->Ship->Combat->Spec).MassKt));
			run->Script();
			return true;
		} });
		run->steps.push_back({ TEXT("hands off at the helm"), Wait(1.0) });
		run->steps.push_back({ TEXT("Shift+J"), shiftJ });
		run->steps.push_back({ TEXT("Shift let go"), shiftUp });
		run->steps.push_back({ TEXT("spooled up and engaged"), spooled(TEXT("spooling"), true) });
		run->steps.push_back({ TEXT("checked the spool"), [r, expect](UnitySpaceApp& app, double)
		{
			expect(r->SpoolS > 0 && r->SpoolShot, TEXT("the drive spooled (a count-down) before it engaged"));
			expect(app.PilotDriveRegime() == DriveRegime::Cruise, TEXT("regime Cruise once spooled"));
			return true;
		} });
		// At cruise: 3 s straight, the stick turned 3-9 s (the mouse for a fighter's or a heavy's stick, D on a capital's helm),
		// straight again to 14 s.
		run->steps.push_back({ TEXT("steered at cruise speed"), [run, r, log, shotIn](UnitySpaceApp& app, double t)
		{
			bool steer = t >= 3.0 && t < 9.0;
			bool helm = app.PilotControlMode == PilotControl::Helm;
			if (helm) { if (steer) run->Keys.Hold(SpaceGod::Input::Key::D); else run->Keys.Release(SpaceGod::Input::Key::D); }
			else if (steer) run->Keys.mouseDelta = SpaceGod::Vector2(1.5f, 0.0f);
			const auto& c = app.SystemCruiseState();
			r->TopKmS = cs::Math::Max(r->TopKmS, c.SpeedKmS);
			if (t >= r->NextLog) { r->NextLog = t + 0.5; log(app, steer ? (helm ? TEXT("cruise, D held") : TEXT("cruise, mouse right")) : TEXT("cruise"), t); }
			if (t >= 6.0 && r->PrevT < 6.0) shotIn(TEXT("cruise-2-steering.png"));
			if (t >= 13.0 && r->PrevT < 13.0) shotIn(TEXT("cruise-3-fast.png"));
			r->PrevT = t;
			if (app.PilotDriveRegime() != DriveRegime::Cruise) throw std::runtime_error("dropped out of cruise while steering");
			if (t < 14.0) return false;
			r->Turned = Deg(cs::Math::Acos(cs::Math::Max(-1.0, cs::Math::Min(1.0, DVec3::Dot(app.PilotForward(), r->Heading0)))));
			run->Keys.Release(SpaceGod::Input::Key::D);
			r->NextLog = 0;
			return true;
		} });
		run->steps.push_back({ TEXT("checked the cruise"), [r, expect](UnitySpaceApp& app, double)
		{
			double covered = (app.PlayerHelioKm() - r->From).Length();
			UE_LOG(LogSpaceGod, Display, TEXT("sg.CruiseRun: cruised %s in 14 s, top %.0f km/s (%.1f c), turned %.1f deg"), *KmText(covered), r->TopKmS, r->TopKmS / SystemCruise::LightKmS, r->Turned);
			expect(r->TopKmS > 100 * SystemCruise::MinKmS, TEXT("the drive ran up well past its slowest cruise"));
			expect(covered > 0.05 * Astro::AuKm, TEXT("crossed a good part of the system"));
			expect(r->Turned > 10, TEXT("the stick turned the hull at cruise speed"));
			return true;
		} });
		run->steps.push_back({ TEXT("Shift+J to drop out"), shiftJ });
		run->steps.push_back({ TEXT("Shift let go"), shiftUp });
		run->steps.push_back({ TEXT("dropped out"), [r, log, shotIn, expect](UnitySpaceApp& app, double t)
		{
			if (t >= r->NextLog) { r->NextLog = t + 0.25; log(app, TEXT("dropping"), t); }
			if (t < 1.5) return false;
			const auto& w = app.DriveRegimeNow();
			UE_LOG(LogSpaceGod, Display, TEXT("sg.CruiseRun: after the drop: regime %s (was %s, last cue %s %.2f s ago)"), UTF8_TO_TCHAR(ToString(w.Now)), UTF8_TO_TCHAR(ToString(w.Was)), UTF8_TO_TCHAR(ToString(w.LastCue)), w.CueSinceS);
			expect(app.PilotDriveRegime() == DriveRegime::Normal && !app.SystemCruiseActive(), TEXT("Shift+J dropped it out of cruise"));
			expect(app.PilotSpeedKmS() < 1000, TEXT("out of cruise at a sublight pace"));
			shotIn(TEXT("cruise-4-dropped.png"));
			r->NextLog = 0;
			return true;
		} });
		run->steps.push_back({ TEXT("written"), Wait(1.5) });
		// A hit on the shields at cruise drops it.
		run->steps.push_back({ TEXT("Shift+J again"), shiftJ });
		run->steps.push_back({ TEXT("Shift let go"), shiftUp });
		run->steps.push_back({ TEXT("spooled up again"), spooled(TEXT("spooling for the shield check"), false) });
		run->steps.push_back({ TEXT("hit on the shields"), [r, log, shotIn, expect](UnitySpaceApp& app, double t)
		{
			if (t < 2.0) return false;
			if (!r->Hit)
			{
				r->Hit = true;
				double before = r->Ship->Combat->Shield();
				for (const auto& s : r->Ship->Combat->Subsystems)
					if (s->Kind() == SubsystemKind::ShieldGenerator && s->Shield > 0) { s->Shield *= 0.5; break; }
				UE_LOG(LogSpaceGod, Display, TEXT("sg.CruiseRun: one shield emitter halved at cruise: %.1f -> %.1f"), before, r->Ship->Combat->Shield());
				return false;
			}
			if (t < 2.6) return false;
			log(app, TEXT("after the shield hit"), t);
			expect(app.PilotDriveRegime() == DriveRegime::Normal, TEXT("a hit on the shields dropped it out of cruise"));
			shotIn(TEXT("cruise-5-shield.png"));
			r->NextLog = 0;
			return true;
		} });
		run->steps.push_back({ TEXT("written"), Wait(1.5) });
		// A hull parked across the path drops it short of it.
		run->steps.push_back({ TEXT("Shift+J a third time"), shiftJ });
		run->steps.push_back({ TEXT("Shift let go"), shiftUp });
		run->steps.push_back({ TEXT("spooled up a third time"), spooled(TEXT("spooling for the path check"), false) });
		run->steps.push_back({ TEXT("a hull across the path"), [r, log, shotIn, expect](UnitySpaceApp& app, double t)
		{
			if (t < 3.0) return false;
			if (r->Obstacle == nullptr)
			{
				const auto& c = app.SystemCruiseState();
				int root = app.Catalog()->Root()->Index;
				// Five seconds out at the pace it holds now.
				double km = cs::Math::Max(2e5, c.SpeedKmS * 5);
				auto at = app.PlayerHelioKm() + app.PilotForward() * km - app.Ephemeris()->Helio(root);
				r->Obstacle = app.Fleet()->SpawnAt("Cruise Check Obstacle", ShipClass::Destroyer, app.World()->Sol, root, at, DVec3::UnitX, app.Clock->Jd);
				if (!r->Obstacle) throw std::runtime_error("the obstacle would not spawn");
				r->Obstacle->Phase = ShipPhase::Parked;
				r->Obstacle->PhaseEndJd = app.Clock->Jd + 1;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.CruiseRun: the %s parked %s ahead on the nose, the drive at %.0f km/s"), *T(r->Obstacle->Name), *KmText(km), c.SpeedKmS);
				r->NextLog = 0;
			}
			r->Obstacle->Phase = ShipPhase::Parked;
			double off = (app.Fleet()->StateOf(r->Obstacle, app.Clock->Jd).HelioKm - app.PlayerHelioKm()).Length();
			if (t >= r->NextLog) { r->NextLog = t + 0.25; log(app, *FString::Printf(TEXT("closing, %s to the hull"), *KmText(off)), t); }
			if (app.PilotDriveRegime() == DriveRegime::Cruise && t < 30) return false;
			log(app, *FString::Printf(TEXT("out, %s short of the hull"), *KmText(off)), t);
			expect(app.PilotDriveRegime() == DriveRegime::Normal, TEXT("a hull in the path dropped it out of cruise"));
			expect(off > 1, TEXT("dropped out short of the hull, not through it"));
			shotIn(TEXT("cruise-6-blocked.png"));
			return true;
		} });
		run->steps.push_back({ TEXT("written"), Wait(1.5) });
		run->onFinish = [r](UnitySpaceApp*)
		{
			UE_LOG(LogSpaceGod, Display, TEXT("sg.CruiseRun: summary: %d of %d checks passed%s"), r->Passed, r->Checks, r->Failures.IsEmpty() ? TEXT("") : *(TEXT("; failed:") + r->Failures));
		};
		run->Go(run);
	}

	// ------------------------------------------------------------------------------------------------ sg.Squadron

	/// Where a wingman's slot is now, as FighterAI::Wingman puts it, without the pilot's own wander and reaction lag.
	DVec3 SlotNow(const cs::Ref<BattleUnit>& lead, const cs::Ref<BattleUnit>& u)
	{
		auto f = u->Fighter;
		auto lf = lead->Fighter;
		auto up = WeakPatches::LevelRoof(lead->Forward);
		auto right = DVec3::Cross(lead->Forward, up).Normalized();
		double spread = lf != nullptr && lf->Phase == FighterPhase::AttackRun ? 2.6 : lf != nullptr && lf->Phase == FighterPhase::Dogfight ? 1.5 : 1.0;
		spread *= 1.6 - f->Discipline;
		auto slot = Formations::SlotOf(u) * spread;
		return lead->Pos + right * slot.X + up * slot.Y + lead->Forward * slot.Z;
	}

	/// <c>sg.Squadron [shot &lt;dir&gt;] [quit]</c>: two carriers off Uranus 84 km apart, the far one's wing held aboard; one of the near one's
	/// fighter squadrons followed as it flies out in formation with no enemy fighter near (each wingman's distance from its
	/// slot logged each second), then the far carrier's interceptors put out 4 km ahead of it, the squadron breaking into
	/// dogfights (each wingman's broken state and the manoeuvre it flies logged each second), and once they are gone the
	/// squadron re-formed. Shots squad-1-formation.png, squad-2-breaking.png, squad-3-furball.png, squad-4-reformed.png.
	void SquadronCommand(const TArray<FString>& args)
	{
		FString dir;
		bool quit = false;
		ParseArgs(args, dir, quit);
		auto run = std::make_shared<Stage>();
		run->name = TEXT("sg.Squadron");
		run->quit = quit;
		run->timeout = 130;
		struct Squad
		{
			cs::Ref<Battle> Fight;
			cs::Ref<BattleUnit> Ours, Theirs, Focused;
			int Q = -1;
			cs::List<cs::Ref<BattleUnit>> Enemies;
			double NextLog = 0, ShotBreakAt = -1, AllClearAt = -1, ReformedAt = -1;
			bool Shot1 = false, Shot2 = false, Shot3 = false, Shot4 = false;
			// Slot error in formation before the fight and after it: count, sum, worst; wingmen ever broken, maneuvers flown.
			int NA = 0, NC = 0, MaxBroken = 0;
			double SumA = 0, MaxA = 0, SumC = 0, MaxC = 0;
			TSet<int> EverBroken;
			TMap<FString, int> Tactics;
		};
		auto s = std::make_shared<Squad>();
		auto shotIn = [dir](const TCHAR* file) { if (!dir.IsEmpty()) Shoot(TEXT("sg.Squadron"), FPaths::Combine(dir, file), true); };
		auto members = [s]()
		{
			cs::List<cs::Ref<BattleUnit>> out;
			if (s->Fight == nullptr) return out;
			for (const auto& u : s->Fight->Units)
				if (u->Fighter != nullptr && u->Present() && u->Fighter->Carrier == s->Ours && u->Fighter->Squadron == s->Q) out.Add(u);
			return out;
		};
		auto leadOf = [](const cs::List<cs::Ref<BattleUnit>>& m)
		{
			for (const auto& u : m)
				if (u->Fighter->Lead == nullptr || !u->Fighter->Lead->Present()) return u;
			return m.Count() > 0 ? m[0] : cs::Ref<BattleUnit>();
		};
		auto foesPresent = [s]()
		{
			cs::List<cs::Ref<BattleUnit>> out;
			for (const auto& e : s->Enemies)
				if (e->Present() && !e->Surrendering && !e->Captured) out.Add(e);
			return out;
		};
		// The camera on the lead, back far enough that the squadron's spread fits.
		auto frame = [s](UnitySpaceApp& app, const cs::Ref<BattleUnit>& lead, const cs::List<cs::Ref<BattleUnit>>& m)
		{
			auto t = Target::OfUnit(s->Fight, lead);
			auto at = app.FocusScene(t);
			double span = 0;
			for (const auto& u : m) span = cs::Math::Max(span, (app.FocusScene(Target::OfUnit(s->Fight, u)) - at).Length());
			double radius = cs::Math::Max(app.TargetRadius(t), 1e-9);
			double radii = cs::Math::Max(10.0, 2.4 * span / radius);
			app.SpaceCamera->FocusOn(t, radii, 18.0f);
			s->Focused = lead;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Squadron: camera on the lead %s, %.0f radii back (the squadron spans %.4g in the scene, the lead's radius %.4g)"), *T(lead->Name()), radii, span, radius);
		};
		// One line per wingman: its slot error (in formation), broken or not, its phase, target and manoeuvre.
		auto logSquad = [s, members, leadOf, foesPresent](const TCHAR* what, double t, int phase)
		{
			auto m = members();
			auto lead = leadOf(m);
			if (lead == nullptr) return;
			auto foes = foesPresent();
			double nearest = cs::Double::PositiveInfinity;
			for (const auto& e : foes) nearest = cs::Math::Min(nearest, (e->Pos - lead->Pos).Length());
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Squadron: %s t=%.0f battle %.0f s; lead %s %s, %.3f km/s (top %.3f), target %s; %d wingmen; %d enemy fighters, nearest %.2f km from the lead"),
				what, t, s->Fight->Seconds, *T(lead->Name()), UTF8_TO_TCHAR(ToString(lead->Fighter->Phase)), lead->Vel.Length(), lead->Combat->Spec->MaxSpeed,
				lead->Target != nullptr ? *T(lead->Target->Name()) : TEXT("none"), m.Count() - 1, foes.Count(), foes.Count() > 0 ? nearest : -1.0);
			int broken = 0;
			for (const auto& u : m)
			{
				if (u == lead) continue;
				auto f = u->Fighter;
				bool br = Formations::Broken(u);
				if (br) { broken++; s->EverBroken.Add(u->Id); }
				auto off = u->Pos - SlotNow(lead, u);
				double err = off.Length();
				auto lup = WeakPatches::LevelRoof(lead->Forward);
				auto lright = DVec3::Cross(lead->Forward, lup).Normalized();
				double slotKm = Formations::SlotOf(u).Length();
				FString tactic = br ? FString(UTF8_TO_TCHAR(ToString(f->Tactics.Tactic))) : FString(TEXT("-"));
				if (br) s->Tactics.FindOrAdd(tactic)++;
				if (!br && f->Lead == lead && phase == 0 && t >= 10) { s->NA++; s->SumA += err; s->MaxA = cs::Math::Max(s->MaxA, err); }
				if (!br && f->Lead == lead && phase == 2) { s->NC++; s->SumC += err; s->MaxC = cs::Math::Max(s->MaxC, err); }
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Squadron:   slot %d %s: %s, %.3f km off its slot (right %+.2f up %+.2f ahead %+.2f; slot %.3f km out, discipline %.2f), %.3f km/s, phase %s, order %s, target %s %.2f km%s%s"),
					f->Slot, *T(u->Name()), br ? TEXT("BROKEN") : TEXT("in formation"), err, DVec3::Dot(off, lright), DVec3::Dot(off, lup), DVec3::Dot(off, lead->Forward), slotKm, f->Discipline, u->Vel.Length(), UTF8_TO_TCHAR(ToString(f->Phase)), UTF8_TO_TCHAR(ToString(f->Order)),
					u->Target != nullptr ? *T(u->Target->Name()) : TEXT("none"), u->Target != nullptr ? (u->Target->Pos - u->Pos).Length() : -1.0,
					br ? *FString::Printf(TEXT(", flying %s (stage %d, %.1f s in)"), *tactic, f->Tactics.Stage, f->Tactics.Time) : TEXT(""),
					f->Lead != lead ? TEXT(" [not on this lead]") : TEXT(""));
			}
			s->MaxBroken = FMath::Max(s->MaxBroken, broken);
		};
		run->steps.push_back({ TEXT("game up"), Wait(6.0) });
		run->steps.push_back({ TEXT("two carriers off Uranus"), [s](UnitySpaceApp& app, double)
		{
			ToCommandView(app);
			app.Clock->SetPaused(false);
			auto uranus = (*app.Catalog())["Uranus"];
			if (uranus == nullptr) throw std::runtime_error("no Uranus");
			int body = uranus->Index;
			double jd = app.Clock->Jd;
			// As SceneTour::TriggerSkirmish lays a showcase out, one carrier a side.
			auto dir = DVec3(1, 0.15, 0.4).Normalized();
			auto side = DVec3::Cross(dir, DVec3::UnitZ).Normalized();
			double standoff = Battle::AnchorDistanceKm(uranus);
			auto ours = app.Fleet()->SpawnAt("Squadron Check Carrier", ShipClass::Carrier, app.World()->Sol, body, dir * standoff, side, jd);
			auto theirs = app.Fleet()->SpawnAt("Squadron Check Foe", ShipClass::Carrier, app.World()->Reach, body, dir * standoff + side * 3000, -side, jd);
			if (!ours || !theirs) throw std::runtime_error("the carriers would not spawn");
			s->Fight = app.World()->War->TryStartBattle(body, app.World()->Sol, jd);
			if (s->Fight == nullptr) throw std::runtime_error("the battle did not start");
			s->Ours = s->Fight->UnitOf(ours);
			s->Theirs = s->Fight->UnitOf(theirs);
			if (!s->Ours || !s->Theirs) throw std::runtime_error("a carrier is not in the battle");
			// The far carrier's wing back aboard, to be put out on cue.
			int held = 0;
			for (const auto& u : s->Fight->Units)
				if (u->Fighter != nullptr && u->Fighter->Carrier == s->Theirs && !u->Docked)
				{
					u->Docked = true;
					FighterAI::TakeAboard(s->Theirs, u);
					held++;
				}
			// Far enough apart that the squadron flies a long while in formation before it nears the far carrier: each side
			// moved out along the line between them to 42 km from the fight's centre, inside the battle's bounds (a craft
			// further out than 0.8 Battle::EscapeKm is turned back toward the centre).
			auto gap = s->Theirs->Pos - s->Ours->Pos;
			double km = gap.Length();
			auto axis = km > 1e-6 ? gap / km : side;
			auto oursBy = -axis * 42 - s->Ours->Pos, theirsBy = axis * 42 - s->Theirs->Pos;
			for (const auto& u : s->Fight->Units)
			{
				if (u->Side == s->Ours->Side) u->Pos = u->Pos + oursBy;
				else if (u->Side == s->Theirs->Side) u->Pos = u->Pos + theirsBy;
			}
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Squadron: %s: %s (side %d) and %s (side %d), %.1f km apart (laid out %.1f); the foe's %d craft held aboard"),
				*T(s->Fight->Name), *T(s->Ours->Name()), s->Ours->Side, *T(s->Theirs->Name()), s->Theirs->Side, (s->Theirs->Pos - s->Ours->Pos).Length(), km, held);
			app.WatchBattle(s->Fight);
			return true;
		} });
		run->steps.push_back({ TEXT("a fighter squadron out and formed"), [s, members, leadOf, frame](UnitySpaceApp& app, double t)
		{
			if (t < 4.0) return false;
			// The fighting squadron with the most craft (interceptors first): bombers and recon never break.
			int bestQ = -1, bestN = 0;
			bool bestInterceptor = false;
			TMap<int, int> count;
			TMap<int, bool> fit;
			for (const auto& u : s->Fight->Units)
			{
				if (u->Fighter == nullptr || !u->Present() || u->Fighter->Carrier != s->Ours) continue;
				auto role = u->Fighter->Role;
				bool fights = !u->Fighter->Bomber && role != FighterRole::Bomber && role != FighterRole::Recon && role != FighterRole::AssaultShuttle;
				count.FindOrAdd(u->Fighter->Squadron)++;
				bool& ok = fit.FindOrAdd(u->Fighter->Squadron, true);
				ok = ok && fights;
			}
			for (const auto& kv : count)
			{
				if (!fit[kv.Key] || kv.Value < 3) continue;
				bool interceptor = false;
				for (const auto& u : s->Fight->Units)
					if (u->Fighter != nullptr && u->Fighter->Carrier == s->Ours && u->Fighter->Squadron == kv.Key) { interceptor = u->Fighter->Role == FighterRole::Interceptor; break; }
				if (bestQ < 0 || (interceptor && !bestInterceptor) || (interceptor == bestInterceptor && kv.Value > bestN)) { bestQ = kv.Key; bestN = kv.Value; bestInterceptor = interceptor; }
			}
			if (bestQ < 0)
			{
				if (t > 20) throw std::runtime_error("no fighting squadron launched");
				return false;
			}
			s->Q = bestQ;
			auto m = members();
			auto lead = leadOf(m);
			bool out = lead != nullptr && lead->Fighter->Phase != FighterPhase::Launch;
			for (const auto& u : m) out = out && u->Fighter->Deck == nullptr;
			if (!out && t < 25) return false;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Squadron: following squadron %d, %d %s craft, shape %s"), s->Q, m.Count(), UTF8_TO_TCHAR(ToString(lead->Fighter->Role)), UTF8_TO_TCHAR(ToString(Formations::ShapeOf(lead))));
			frame(app, lead, m);
			return true;
		} });
		run->steps.push_back({ TEXT("in formation, no enemy fighter near"), [s, members, leadOf, logSquad, shotIn, frame](UnitySpaceApp& app, double t)
		{
			auto m = members();
			auto lead = leadOf(m);
			if (lead == nullptr) throw std::runtime_error("the squadron is gone");
			if (lead != s->Focused) frame(app, lead, m);
			if (t >= s->NextLog) { s->NextLog = t + 1.0; logSquad(TEXT("formation"), t, 0); }
			if (!s->Shot1 && t >= 8.0) { s->Shot1 = true; shotIn(TEXT("squad-1-formation.png")); }
			// Until the lead is near enough its capital for the wingmen to split off onto strafing runs (Formations::StrafeBreakKm).
			auto lt = lead->Target;
			if (lead->Fighter->Phase == FighterPhase::AttackRun && lt != nullptr && (lead->Pos - lt->Pos).Length() < Formations::StrafeBreakKm + lt->Radius() + 1)
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Squadron: the lead is %.1f km off %s at t=%.0f, near enough to strafe"), (lead->Pos - lt->Pos).Length(), *T(lt->Name()), t);
			}
			else if (t < 16.0) return false;
			s->NextLog = 0;
			return true;
		} });
		run->steps.push_back({ TEXT("enemy fighters put out ahead"), [s, members, leadOf](UnitySpaceApp&, double)
		{
			auto lead = leadOf(members());
			auto launched = FighterAI::LaunchOrdered(s->Fight, s->Theirs, FighterRole::Interceptor);
			if (launched.Count() == 0) launched = FighterAI::LaunchOrdered(s->Fight, s->Theirs, FighterRole::SpaceSuperiority);
			if (launched.Count() == 0) throw std::runtime_error("the far carrier put no fighters out");
			auto fwd = lead->Forward;
			auto up = WeakPatches::LevelRoof(fwd);
			auto right = DVec3::Cross(fwd, up).Normalized();
			for (int i = 0; i < launched.Count(); i++)
			{
				const auto& e = launched[i];
				auto f = e->Fighter;
				// Out of the deck's hands (as LaunchOrdered clears a craft's deck visit), 4 km ahead, coming head on.
				if (auto v = f->Deck)
				{
					if (v->Deck != nullptr)
					{
						v->Deck->Aboard.Remove(e);
						if (v->Deck->Final == e) v->Deck->Final = nullptr;
					}
					f->Deck = nullptr;
				}
				f->Phase = FighterPhase::Approach;
				f->PhaseTime = 0;
				e->Pos = lead->Pos + fwd * 4 + right * ((i - launched.Count() * 0.5) * 0.25) + up * (0.05 * i);
				e->Forward = -fwd;
				e->Vel = lead->Vel - fwd * 0.2;
				s->Enemies.Add(e);
			}
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Squadron: %d %s from the %s put out 4 km ahead of the squadron, head on"), launched.Count(), UTF8_TO_TCHAR(ToString(launched[0]->Fighter->Role)), *T(s->Theirs->Name()));
			return true;
		} });
		run->steps.push_back({ TEXT("the fight"), [s, members, leadOf, logSquad, foesPresent, shotIn, frame](UnitySpaceApp& app, double t)
		{
			auto m = members();
			auto lead = leadOf(m);
			if (lead == nullptr) throw std::runtime_error("the squadron is gone");
			if (lead != s->Focused) frame(app, lead, m);
			if (t >= s->NextLog) { s->NextLog = t + 1.0; logSquad(TEXT("fight"), t, 1); }
			int broken = 0;
			for (const auto& u : m) if (u != lead && Formations::Broken(u)) broken++;
			if (s->ShotBreakAt < 0 && broken >= 1) s->ShotBreakAt = t + 1.5;
			if (!s->Shot2 && s->ShotBreakAt >= 0 && t >= s->ShotBreakAt) { s->Shot2 = true; shotIn(TEXT("squad-2-breaking.png")); }
			if (!s->Shot3 && s->ShotBreakAt >= 0 && t >= s->ShotBreakAt + 6) { s->Shot3 = true; shotIn(TEXT("squad-3-furball.png")); }
			// Over: no enemy fighter left in the fight round the lead, and every wingman back off its dogfight.
			bool nearFoe = false;
			for (const auto& e : foesPresent()) nearFoe = nearFoe || (e->Pos - lead->Pos).Length() < Formations::ReformKm;
			if (!nearFoe && broken == 0 && s->EverBroken.Num() > 0)
			{
				if (s->AllClearAt < 0) { s->AllClearAt = t; UE_LOG(LogSpaceGod, Display, TEXT("sg.Squadron: the furball is over at t=%.0f, every wingman off its dogfight"), t); }
				s->NextLog = 0;
				return true;
			}
			if (t > 80)
			{
				UE_LOG(LogSpaceGod, Warning, TEXT("sg.Squadron: the fight still going at t=%.0f (%d broken, an enemy fighter near: %d)"), t, broken, nearFoe ? 1 : 0);
				s->NextLog = 0;
				return true;
			}
			return false;
		} });
		run->steps.push_back({ TEXT("re-formed"), [s, members, leadOf, logSquad, shotIn, frame](UnitySpaceApp& app, double t)
		{
			auto m = members();
			auto lead = leadOf(m);
			if (lead == nullptr) throw std::runtime_error("the squadron is gone");
			if (lead != s->Focused || t == 0) frame(app, lead, m);
			if (t >= s->NextLog) { s->NextLog = t + 1.0; logSquad(TEXT("after"), t, s->ReformedAt >= 0 ? 2 : 3); }
			// Until every wingman back on the lead is within half a km of its slot (a dogfight can leave one km astern, closing
			// at the headroom it has over the lead's pace), the lead near enough its capital to strafe, or 100 s.
			bool formed = true;
			for (const auto& u : m)
				if (u != lead && u->Fighter->Lead == lead && !Formations::Broken(u) && u->Fighter->Phase != FighterPhase::Return)
					formed = formed && (u->Pos - SlotNow(lead, u)).Length() < 0.5;
			auto lt = lead->Target;
			bool strafing = lead->Fighter->Phase == FighterPhase::AttackRun && lt != nullptr && (lead->Pos - lt->Pos).Length() < Formations::StrafeBreakKm + lt->Radius();
			if (s->ReformedAt < 0 && (formed || strafing || t >= 100))
			{
				s->ReformedAt = t;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Squadron: %s at t=%.0f"), formed ? TEXT("re-formed: every wingman on the lead within 0.5 km of its slot") : strafing ? TEXT("the lead is in strafing range before the squadron re-formed") : TEXT("not re-formed in 100 s"), t);
			}
			if (!s->Shot4 && s->ReformedAt >= 0) { s->Shot4 = true; shotIn(TEXT("squad-4-reformed.png")); }
			return s->ReformedAt >= 0 && t >= s->ReformedAt + 8;
		} });
		run->steps.push_back({ TEXT("written"), Wait(1.5) });
		run->onFinish = [s](UnitySpaceApp*)
		{
			FString tactics;
			for (const auto& kv : s->Tactics) tactics += FString::Printf(TEXT(" %s x%d"), *kv.Key, kv.Value);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Squadron: summary: in formation before the fight (from 10 s on) %d samples, mean %.3f km off the slot, worst %.3f; %d wingmen broke (at most %d at once), manoeuvres flown (wingman-seconds):%s; once re-formed %d samples, mean %.3f km, worst %.3f"),
				s->NA, s->NA > 0 ? s->SumA / s->NA : 0.0, s->MaxA, s->EverBroken.Num(), s->MaxBroken, tactics.IsEmpty() ? TEXT(" none") : *tactics, s->NC, s->NC > 0 ? s->SumC / s->NC : 0.0, s->MaxC);
		};
		run->Go(run);
	}

	FAutoConsoleCommand GGhostCommand(TEXT("sg.Ghost"),
		TEXT("US-T218: a hull between the camera and a focused one, drawn as the see-through ghost (US-T185): sg.Ghost [shot <file>] [quit]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& a) { GhostCommand(a); }));
	FAutoConsoleCommand GLaneRideCommand(TEXT("sg.LaneRide"),
		TEXT("US-T218: into a space lane, steered about inside its tube, out past its wall and the speed bled off (US-T196): sg.LaneRide [shot <dir>] [quit]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& a) { LaneRideCommand(a); }));
	FAutoConsoleCommand GCapitalRollCommand(TEXT("sg.CapitalRoll"),
		TEXT("US-T218: a capital in the big battle rolling its strongest shield face to the threat, logged each second (US-T187): sg.CapitalRoll [shot <dir>] [seconds=60] [preroll=90] [quit]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& a) { CapitalRollCommand(a); }));
	FAutoConsoleCommand GCruiseRunCommand(TEXT("sg.CruiseRun"),
		TEXT("US-T224: a lone helm's cruise drive spooled, steered at cruise and dropped on Shift+J, then dropped by a shield hit and by a hull in its path (US-T127, US-T197): sg.CruiseRun [shot <dir>] [quit]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& a) { CruiseRunCommand(a); }));
	FAutoConsoleCommand GSquadronCommand(TEXT("sg.Squadron"),
		TEXT("US-T224: a carrier squadron in formation, breaking into dogfights on enemy fighters and re-formed after (US-T198, US-T188): sg.Squadron [shot <dir>] [quit]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& a) { SquadronCommand(a); }));
}
