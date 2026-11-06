// US-T125: sg.BioMass, a Bio Mass globule spawned beside the player (and its screenshots at game scale); US-T192 its
// blooms and tumors (sg.BioMass bloom, sg.BioMass tumor).

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "GameLaunch.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Target.h"
#include "Tour/ShowcaseTour.h"
#include "UnitySpaceAppActor.h"

#include "BodyCatalog.h"
#include "Combat/BioMass.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "Systems/JumpGate.h"
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

	/// <summary>One run of the command, stepped once a frame (each step returns true when done), as sg.VoidStation's.</summary>
	struct Run
	{
		using Step = std::function<bool(SpaceGod::UnitySpaceApp&, double)>;
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
					UE_LOG(LogSpaceGod, Display, TEXT("sg.BioMass: no game running; starting a new one"));
					SpaceGod::GameLaunch::NewGame();
				}
				if (now - started > 180.0) return Finish(TEXT("no game came up"));
				return true;
			}
			if (at >= steps.size()) return Finish(nullptr);
			if (stepStart < 0) stepStart = now;
			if (steps[at].second(*app, now - stepStart))
			{
				UE_LOG(LogSpaceGod, Display, TEXT("sg.BioMass: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 240.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.BioMass: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.BioMass: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	/// <summary>The hull to spawn beside: the one piloted, else the one selected, else the first armed hull in open space.</summary>
	cs::Ref<Ship> Beside(SpaceGod::UnitySpaceApp& app)
	{
		auto world = app.World();
		if (world == nullptr) return nullptr;
		if (world->PilotedShip != nullptr && !world->PilotedShip->Destroyed) return world->PilotedShip;
		if (app.Selected.IsShip() && !app.Selected.Ship->Destroyed) return app.Selected.Ship;
		cs::Ref<Ship> any;
		for (const auto& s : world->Fleet->Ships())
		{
			if (s->Destroyed || s->Planetside() || s->Phase == ShipPhase::Lane) continue;
			if (s->Phase == ShipPhase::Parked && s->Spec != nullptr && s->Spec->IsMilitary) return s;
			if (any == nullptr) any = s;
		}
		return any;
	}

	/// <summary>Moves (or makes) the globule to a scene point: its anchor in the nearest body's frame, so the view draws it there.</summary>
	cs::Ref<BioGlobule> PlaceAt(SpaceGod::UnitySpaceApp& app, cs::Ref<BioGlobule> g, DVec3 scene, double radiusScene)
	{
		auto world = app.World();
		auto cat = app.Catalog();
		int best = 0;
		double bestD = cs::Double::PositiveInfinity;
		for (int b = 0; b < cat->Count(); b++)
		{
			double d = (app.BodyScene(b) - scene).Length();
			if (d < bestD) { bestD = d; best = b; }
		}
		DVec3 anchor = app.Scale()->InverseLocalOffset((*cat)[best], scene - app.BodyScene(best));
		double km = radiusScene / app.BattleKmScale();
		double mass = cs::Math::Pow(km / 0.012, 3.0);
		auto bio = world->EnsureBioMass(app.Clock->Jd);
		if (g == nullptr) g = bio->Spawn(best, anchor, DVec3::Zero, mass, app.Clock->Jd);
		else
		{
			g->Body = best;
			g->Anchor = anchor;
			g->Local = DVec3::Zero;
		}
		g->Hp = g->MaxHp();
		// A demonstration: it holds where it was put (no creep) until the command is done.
		g->GoalGate = g->GoalBody = -1;
		return g;
	}

	/// <summary>US-T147: a showcase tour stop: a big globule set down beside <paramref name="where"/>'s guns (a world's, or a jump
	/// gate's), held whole while it is shown, the camera on it with what is firing behind.</summary>
	SpaceGod::Tour::TourStop GunsStop(const cs::String& id, const cs::String& title, const cs::String& caption, bool gate)
	{
		auto globule = std::make_shared<cs::Ref<BioGlobule>>();
		auto frame = std::make_shared<int>(0);
		return SpaceGod::Tour::TourStop::Make("Threats", id, title, caption)
			.WithSetup([globule, frame, gate](SpaceGod::UnitySpaceApp& app) -> cs::String
			{
				auto world = app.World();
				double jd = app.Clock->Jd;
				auto bio = world->EnsureBioMass(jd);
				DVec3 anchor;
				int body = 0;
				if (gate)
				{
					if (world->Gates.Count() == 0) return "no jump gate in this system";
					auto g = world->Gates[0];
					// Just outside the gate's guns' reach, creeping in on it.
					anchor = g->PositionKm + DVec3(1, 0.3, 0.2).Normalized() * (BioMass::GateGunRangeKm * 0.6);
				}
				else
				{
					auto earth = (*app.Catalog())["Earth"];
					if (earth == nullptr) return "no Earth here";
					body = earth->Index;
					DVec3 outward;
					app.BodyStandoffKm(body, outward);
					anchor = outward * (earth->RadiusKm + BioMass::BodyGunReachKm * 0.4);
				}
				*frame = body;
				*globule = bio->Spawn(body, anchor, DVec3::Zero, 8e5, jd);
				(*globule)->GoalBody = -1;
				(*globule)->GoalGate = gate ? 0 : -1;
				app.Clock->WarpIndex = 0; // real time, so the guns' work can be watched
				return cs::String();
			})
			.Until([](SpaceGod::UnitySpaceApp&, double t) { return t > 1; })
			.WithCamera([globule, frame](SpaceGod::UnitySpaceApp& app, double)
			{
				auto g = *globule;
				if (g == nullptr || app.World()->BioMass == nullptr) return;
				g->Hp = g->MaxHp(); // held whole while it is shown
				double jd = app.Clock->Jd;
				DVec3 scene = app.Scale()->MapNear(app.Ephemeris(), app.BodyScenes(), *frame, app.World()->BioMass->Helio(g, jd));
				double r = std::max(g->RadiusKm() * app.BattleKmScale(), app.Scale()->ShipLength(g->RadiusKm()));
				// From beyond it, looking back past it at the world or the gate that is firing.
				DVec3 from = (scene - app.BodyScene(*frame));
				from = from.LengthSquared() > 1e-12 ? from.Normalized() : DVec3::UnitX;
				DVec3 side = DVec3::Cross(from, DVec3::UnitY);
				side = side.LengthSquared() > 1e-12 ? side.Normalized() : DVec3::UnitZ;
				DVec3 eye = scene + (from * 0.8 + side * 0.5 + DVec3(0, 0.3, 0)).Normalized() * (r * 9);
				auto look = (scene - eye).Normalized();
				if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
				app.SpaceCamera->SetPose(eye, SpaceGod::Quaternion::LookRotation(SpaceGod::Vector3(static_cast<float>(look.X), static_cast<float>(look.Y), static_cast<float>(look.Z)), SpaceGod::Vector3::up()));
			})
			.For(9).AtBothScales();
	}

	SpaceGod::Tour::TourRegistration GBioMassTourStops([]
	{
		SpaceGod::Tour::TourRegistry::Add(GunsStop("biomass-planet-guns", "Bio Mass under a world's guns",
			"A globule of living meat drifting in on Earth: the planet's embedded batteries and gun satellites open up on it", false).At(10));
		SpaceGod::Tour::TourRegistry::Add(GunsStop("biomass-gate-guns", "Bio Mass at a jump gate",
			"Creeping in on a jump gate to spread to the next system: the gate's guns and forts fire on it", true).At(20));
	});

	void Start(const TArray<FString>& args)
	{
		FString scale = TEXT("game"), shots;
		double radii = 7;
		bool quit = false, keepScale = false;
		// US-T192: what to spawn: a globule (the default), a cluster of blooms off the hull, or a tumor on it.
		BioForm form = BioForm::Globule;
		for (int i = 0; i < args.Num(); i++)
		{
			const FString& a = args[i];
			if (a.Equals(TEXT("bloom"), ESearchCase::IgnoreCase) || a.Equals(TEXT("blooms"), ESearchCase::IgnoreCase)) form = BioForm::Bloom;
			else if (a.Equals(TEXT("tumor"), ESearchCase::IgnoreCase)) form = BioForm::Tumor;
			else if (a.Equals(TEXT("game"), ESearchCase::IgnoreCase) || a.Equals(TEXT("real"), ESearchCase::IgnoreCase)) scale = a.ToLower();
			else if (a.Equals(TEXT("here"), ESearchCase::IgnoreCase)) keepScale = true;
			else if (a.StartsWith(TEXT("radii="), ESearchCase::IgnoreCase)) radii = FCString::Atod(*a.Mid(6));
			else if (a.Equals(TEXT("shots"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) shots = args[++i];
			else if (a.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
		}

		auto run = std::make_shared<Run>();
		run->quit = quit;
		auto ship = std::make_shared<cs::Ref<Ship>>();
		auto globule = std::make_shared<cs::Ref<BioGlobule>>();
		auto wait = [](double seconds) { return [seconds](SpaceGod::UnitySpaceApp&, double t) { return t >= seconds; }; };
		run->steps.push_back({ TEXT("game up"), wait(shots.IsEmpty() ? 0.0 : 3.0) });
		if (!keepScale)
			run->steps.push_back({ FString::Printf(TEXT("%s scale"), *scale), [scale](SpaceGod::UnitySpaceApp& app, double)
			{
				double goal = scale == TEXT("game") ? 1.0 : 0.0;
				if (app.ScaleGoal != goal) app.ToggleScale();
				return !app.ScaleAnimating();
			} });
		run->steps.push_back({ TEXT("hull found, real time"), [ship, radii](SpaceGod::UnitySpaceApp& app, double)
		{
			*ship = Beside(app);
			if (*ship == nullptr) throw std::runtime_error("no hull in this system to spawn beside");
			UE_LOG(LogSpaceGod, Display, TEXT("sg.BioMass: beside %s"), UTF8_TO_TCHAR((*ship)->Name.c_str()));
			// Real time, so the hull does not run off and the guns' work can be watched.
			app.Clock->WarpIndex = 0;
			app.SpaceCamera->FocusOn(SpaceGod::Target::OfShip(*ship), radii);
			return true;
		} });
		run->steps.push_back({ TEXT("camera settled"), wait(3.0) });

		if (form != BioForm::Globule)
		{
			// US-T192: blooms erupt a short dart off the hull (they see it, close and latch on in seconds); a tumor is
			// set rooted on it. Both are then left to the sim (and the guns).
			run->steps.push_back({ form == BioForm::Bloom ? TEXT("blooms erupted beside the hull") : TEXT("tumor rooted on the hull"),
				[ship, form](SpaceGod::UnitySpaceApp& app, double)
				{
					auto world = app.World();
					double jd = app.Clock->Jd;
					auto bio = world->EnsureBioMass(jd);
					DVec3 helio = world->Fleet->StateOf(*ship, jd).HelioKm;
					if (form == BioForm::Bloom)
					{
						int made = bio->EruptBlooms(helio + DVec3(1, 0.4, 0.25).Normalized() * 600.0, BioMass::BloomCluster, jd);
						if (made == 0) throw std::runtime_error("no room for blooms in this system (at the cap)");
						UE_LOG(LogSpaceGod, Display, TEXT("sg.BioMass: %d blooms, 600 km off"), made);
					}
					else
					{
						auto t = bio->SpawnTumor(BioHost::Ship, (*ship)->Id, 0, helio - world->Ephemeris->HelioAt(0, jd), BioMass::TumorSeedMass, jd);
						if (!t->Attached()) throw std::runtime_error("the tumor could not latch onto the hull");
						UE_LOG(LogSpaceGod, Display, TEXT("sg.BioMass: tumor %d, %.0f kt, on the hull"), t->Id, t->Mass / 1000);
					}
					return true;
				} });
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
			{
				try { return run->Tick(); }
				catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
			}));
			return;
		}

		// Beside the hull: off to the camera's right, about a third of the view away, a little smaller than the gap.
		auto beside = [ship, globule](SpaceGod::UnitySpaceApp& app, double)
		{
			DVec3 s = app.ShipScene(*ship);
			DVec3 c = app.SpaceCamera->ScenePosition;
			double d = (c - s).Length();
			auto r = app.UnityCamera->transform->right();
			DVec3 right(r.x, r.y, r.z);
			*globule = PlaceAt(app, *globule, s + right * (d * 0.32), d * 0.12);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.BioMass: globule %d, %.0f kt, %.2f km across"), (*globule)->Id, (*globule)->Mass / 1000, (*globule)->RadiusKm() * 2);
			return true;
		};
		run->steps.push_back({ TEXT("globule spawned beside the hull"), beside });
		// While the command holds it, it is kept whole (the guns would burn a small one away before the pictures).
		auto hold = [globule](double seconds)
		{
			return [globule, seconds](SpaceGod::UnitySpaceApp&, double t)
			{
				if (*globule != nullptr) (*globule)->Hp = (*globule)->MaxHp();
				return t >= seconds;
			};
		};

		if (!shots.IsEmpty())
		{
			auto shot = [](FString path)
			{
				return [path](SpaceGod::UnitySpaceApp&, double)
				{
					FScreenshotRequest::RequestScreenshot(path, false, false);
					return true;
				};
			};
			// Let the guns find it (the tracers and the flinch), then hold everything still for the pictures.
			run->steps.push_back({ TEXT("under fire"), hold(2.5) });
			run->steps.push_back({ TEXT("re-placed beside the hull"), beside });
			run->steps.push_back({ TEXT("held"), hold(0.5) });
			run->steps.push_back({ TEXT("shot near"), shot(FPaths::Combine(shots, TEXT("biomass-game-near.png"))) });
			run->steps.push_back({ TEXT("shot written"), hold(1.5) });
			// The close-up: the same globule brought halfway to the camera, filling the frame beside the hull.
			run->steps.push_back({ TEXT("brought close"), [ship, globule](SpaceGod::UnitySpaceApp& app, double)
			{
				DVec3 s = app.ShipScene(*ship);
				DVec3 c = app.SpaceCamera->ScenePosition;
				double d = (c - s).Length();
				auto r = app.UnityCamera->transform->right();
				PlaceAt(app, *globule, s + (c - s) * 0.6 + DVec3(r.x, r.y, r.z) * (d * 0.05), d * 0.12);
				return true;
			} });
			run->steps.push_back({ TEXT("held close"), hold(1.0) });
			run->steps.push_back({ TEXT("shot close"), shot(FPaths::Combine(shots, TEXT("biomass-game-close.png"))) });
			run->steps.push_back({ TEXT("shot written"), hold(1.5) });
		}
		run->steps.push_back({ TEXT("released"), [globule](SpaceGod::UnitySpaceApp& app, double)
		{
			// Free to creep (and to be shot) as any other.
			if (*globule != nullptr && app.World() != nullptr && app.World()->BioMass != nullptr) (*globule)->GoalBody = 0;
			return true;
		} });

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	/// US-T125: <c>sg.BioMass [game|real|here] [radii=R]</c> spawns a Bio Mass globule beside the piloted (or selected)
	/// hull, at game scale unless told otherwise, and drops time to real time so the guns' work can be watched.
	/// <c>shots &lt;dir&gt; [quit]</c> also takes biomass-game-near.png and biomass-game-close.png into dir.
	/// US-T192: <c>sg.BioMass bloom</c> erupts a cluster of blooms just off the hull (they latch on and bite);
	/// <c>sg.BioMass tumor</c> roots a tumor on it.
	FAutoConsoleCommand GBioMassCommand(
		TEXT("sg.BioMass"),
		TEXT("US-T125: spawn a Bio Mass globule beside your hull: sg.BioMass [game|real|here] [radii=R]; "
			"sg.BioMass shots <dir> [quit] screenshots it near the hull and close up. "
			"US-T192: sg.BioMass bloom erupts blooms off your hull; sg.BioMass tumor roots a tumor on it."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
