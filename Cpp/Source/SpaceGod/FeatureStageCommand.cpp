// US-T218: three staged checks for features best judged in the real renderer, each a step list run once a frame on the
// core ticker (as sg.WarpTunnel), logging the numbers that say whether the feature works and, with shot, screenshots:
//   sg.Ghost [shot <file>] [quit]        the occluder ghost (US-T185 / US-T206): a hull between the camera and a focused one
//   sg.LaneRide [shot <dir>] [quit]      steering inside a space lane's tube and the speed bled off past its wall (US-T196)
//   sg.CapitalRoll [shot <dir>] [quit]   a capital rolling its strongest shield face toward its main threat (US-T187)

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
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Ships.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/Broadsides.h"
#include "Combat/CapitalManeuver.h"
#include "Combat/CombatState.h"
#include "Combat/LaneFlight.h"
#include "Combat/PilotTuning.h"
#include "Combat/WeakPatches.h"

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

	void Shoot(const TCHAR* who, const FString& path)
	{
		if (path.IsEmpty()) return;
		FScreenshotRequest::RequestScreenshot(path, false, false);
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

	FAutoConsoleCommand GGhostCommand(TEXT("sg.Ghost"),
		TEXT("US-T218: a hull between the camera and a focused one, drawn as the see-through ghost (US-T185): sg.Ghost [shot <file>] [quit]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& a) { GhostCommand(a); }));
	FAutoConsoleCommand GLaneRideCommand(TEXT("sg.LaneRide"),
		TEXT("US-T218: into a space lane, steered about inside its tube, out past its wall and the speed bled off (US-T196): sg.LaneRide [shot <dir>] [quit]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& a) { LaneRideCommand(a); }));
	FAutoConsoleCommand GCapitalRollCommand(TEXT("sg.CapitalRoll"),
		TEXT("US-T218: a capital in the big battle rolling its strongest shield face to the threat, logged each second (US-T187): sg.CapitalRoll [shot <dir>] [seconds=60] [preroll=90] [quit]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& a) { CapitalRollCommand(a); }));
}
