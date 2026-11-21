// US-T435: sg.Berths, a busy station seen with dozens of ships in its berths. At the first world of the active system with a
// port habitat (Earth in Sol), civilian hulls are brought in to call at the habitat and at the port's shipyard until every
// berth is taken and a few more hold off each station waiting for one; the clock is held and each station is framed whole
// and then along a pier, and screenshotted. The berth counts are logged, and the player's own docking is asked of a full
// station (refused into the line) and of one with a berth free.
//   sg.Berths [quit]   (screenshots: Saved/Screenshots/berths-*.png)

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "GameLaunch.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"
#include "Views/StructureViews.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Fleet.h"
#include "Infrastructure/Berths.h"
#include "Infrastructure/Infrastructure.h"
#include "OrbitalElements.h"
#include "ScaleModel.h"
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

	struct Run
	{
		using Step = std::function<bool(UnitySpaceApp&, double)>;
		std::vector<std::pair<FString, Step>> steps;
		size_t at = 0;
		double stepStart = -1, started = FPlatformTime::Seconds();
		bool askedNewGame = false, quit = false;

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
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Berths: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 60.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			if (auto* app = App()) app->Clock->SetPaused(false);
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Berths: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Berths: done")); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	struct Stage
	{
		cs::Ref<OrbitalStructure> habitat, yard;
		std::vector<cs::Ref<Ship>> callers;
	};

	void Log(UnitySpaceApp& app, const cs::Ref<OrbitalStructure>& st)
	{
		if (st == nullptr) return;
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Berths: %s (%s, %.1f km, drawn %.1f units): %d of %d berths taken, %d holding"), *T(st->Name),
			*T(ToString(st->Kind)), st->LengthKm, app.Scale()->ShipLength(st->LengthKm), st->BerthsTaken(), st->BerthCount(), st->Holding.Count());
	}

	/// Brings in civilian hulls of the station's owner, parked at its world, until every berth is taken and
	/// <paramref name="extra"/> more hold for one.
	void Fill(UnitySpaceApp& app, Stage& stage, const cs::Ref<OrbitalStructure>& st, int extra)
	{
		static const ShipClass kinds[] = { ShipClass::Freighter, ShipClass::Courier, ShipClass::Freighter, ShipClass::Miner, ShipClass::Freighter };
		auto infra = app.World()->Infrastructure;
		double jd = app.Clock->Jd;
		int want = st->BerthCount() - st->BerthsTaken() + extra - st->Holding.Count();
		auto out = st->Orbit != nullptr ? st->Orbit->OffsetAt(jd) : DVec3::UnitX;
		for (int i = 0; i < want; i++)
		{
			auto faction = st->Owner() != nullptr ? st->Owner() : app.World()->Sol;
			auto ship = app.Fleet()->SpawnAt(cs::Cat(st->Name, " caller ", i + 1), kinds[i % 5], faction, st->Body, out * 1.02 + DVec3(0, 0, 20.0 * i), DVec3::UnitX, jd);
			if (ship == nullptr) throw std::runtime_error("a caller would not spawn");
			stage.callers.push_back(ship);
			// Its own station: asked for, as a ship calling there would be cleared to it (this world may have several).
			infra->RequestBerth(st, ship);
		}
	}

	/// Holds every caller parked (the clock is paused too) so none sails before the shots.
	void Hold(UnitySpaceApp& app, Stage& stage)
	{
		for (const auto& ship : stage.callers)
			if (ship != nullptr) { ship->Phase = ShipPhase::Parked; ship->PhaseEndJd = app.Clock->Jd + 1; }
	}

	/// Frames <paramref name="st"/>: <paramref name="off"/> in its own frame, <paramref name="reach"/> of its drawn size away.
	bool FrameStation(UnitySpaceApp& app, const cs::Ref<OrbitalStructure>& st, SpaceGod::Vector3 off, double reach, SpaceGod::Vector3 aim)
	{
		DVec3 centre;
		double size = 0;
		SpaceGod::Quaternion rot;
		if (st == nullptr || app.Structures == nullptr || !app.Structures->FrameOf(st, centre, size, rot)) return false;
		auto d = rot * off.normalized();
		auto a = rot * aim;
		auto target = centre + DVec3(a.x, a.y, a.z) * size;
		auto eye = target + DVec3(d.x, d.y, d.z) * (size * reach);
		auto look = target - eye;
		app.SpaceCamera->SetPose(eye, SpaceGod::Quaternion::LookRotation(
			SpaceGod::Vector3(static_cast<float>(look.X), static_cast<float>(look.Y), static_cast<float>(look.Z)), rot * SpaceGod::Vector3::up()));
		return true;
	}

	void Start(const TArray<FString>& args)
	{
		bool quit = false;
		for (const FString& a : args)
			if (a.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) quit = true;
		auto run = std::make_shared<Run>();
		run->quit = quit;
		auto stage = std::make_shared<Stage>();
		FString dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"));
		auto push = [&run](FString name, Run::Step step) { run->steps.push_back({ name, std::move(step) }); };

		push(TEXT("game up"), [](UnitySpaceApp&, double t) { return t >= 6.0; });
		push(TEXT("a port's habitat and yard filled with callers"), [stage](UnitySpaceApp& app, double)
		{
			app.SkyLeaveHelm();
			if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
			if (app.ScaleGoal < 0.5) app.ToggleScale();
			app.Clock->SetPaused(true);
			auto infra = app.World()->Infrastructure;
			if (infra == nullptr) throw std::runtime_error("no infrastructure in this system");
			// The busiest port's habitat (Earth's in Sol), and that port's own yard.
			for (const auto& st : infra->Structures)
			{
				if (st->Kind != StructureKind::Habitat || st->AtLagrange() || st->Settlement == nullptr || st->BerthCount() <= 0) continue;
				if (stage->habitat == nullptr || st->Settlement->Population > stage->habitat->Settlement->Population) stage->habitat = st;
			}
			if (stage->habitat == nullptr) throw std::runtime_error("no port habitat in this system");
			stage->yard = infra->YardOf(stage->habitat->Settlement);
			infra->UpdateBerths(app.Fleet()->Ships(), app.Economy(), app.World()->PilotedShip);
			Log(app, stage->habitat);
			Log(app, stage->yard);
			Fill(app, *stage, stage->habitat, 4);
			if (stage->yard != nullptr) Fill(app, *stage, stage->yard, 3);
			Hold(app, *stage);
			infra->UpdateBerths(app.Fleet()->Ships(), app.Economy(), app.World()->PilotedShip);
			Log(app, stage->habitat);
			Log(app, stage->yard);
			// The player's docking asked of a full station: into the line, not a berth.
			if (!stage->callers.empty())
			{
				auto probe = stage->callers.back();
				int place = -1;
				auto line = infra->HoldingAt(probe, place);
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Berths: %s holds %s (%d in line) with every berth taken"), *T(probe->Name),
					line != nullptr ? *T(line->Name) : TEXT("nowhere"), place + 1);
			}
			return true;
		});
		struct Shot { const TCHAR* Tag; bool Yard; SpaceGod::Vector3 Off; double Reach; SpaceGod::Vector3 Aim; };
		// The habitat faces its wheel to the planet's pole (root +Z out from the world); the yard lies along its orbit.
		const Shot shots[] = {
			{ TEXT("habitat-whole"), false, SpaceGod::Vector3(0.55f, 0.45f, 1.0f), 1.6, SpaceGod::Vector3(0, 0, 0) },
			{ TEXT("habitat-spine"), false, SpaceGod::Vector3(1.0f, 0.25f, 0.35f), 0.55, SpaceGod::Vector3(0, 0, 0.3f) },
			{ TEXT("yard-whole"), true, SpaceGod::Vector3(0.8f, 1.0f, 0.5f), 1.5, SpaceGod::Vector3(0, 0, 0) },
			// From below and abeam, outside the yard's girders, down its underside pier and the hulls hanging from it.
			{ TEXT("yard-pier"), true, SpaceGod::Vector3(0.6f, -0.8f, 0.9f), 0.6, SpaceGod::Vector3(0, -0.45f, 0) },
		};
		for (const auto& sh : shots)
		{
			bool yard = sh.Yard;
			auto off = sh.Off;
			double reach = sh.Reach;
			auto aim = sh.Aim;
			FString tag = sh.Tag;
			push(FString::Printf(TEXT("%s framed"), *tag), [stage, yard, off, reach, aim](UnitySpaceApp& app, double t)
			{
				Hold(app, *stage);
				auto st = yard ? stage->yard : stage->habitat;
				if (st == nullptr) return true;
				FrameStation(app, st, off, reach, aim);
				return t >= 3.0;
			});
			FString file = FPaths::Combine(dir, FString::Printf(TEXT("berths-%s.png"), *tag));
			push(FString::Printf(TEXT("shot %s"), *file), [file, stage, yard](UnitySpaceApp& app, double)
			{
				if ((yard ? stage->yard : stage->habitat) == nullptr) return true;
				Log(app, yard ? stage->yard : stage->habitat);
				FScreenshotRequest::RequestScreenshot(file, false, false);
				return true;
			});
			push(TEXT("written"), [stage, yard, off, reach, aim](UnitySpaceApp& app, double t)
			{
				Hold(app, *stage);
				FrameStation(app, yard ? stage->yard : stage->habitat, off, reach, aim);
				return t >= 1.0;
			});
		}
		// One caller sails: its berth frees and the first ship holding is cleared into it.
		push(TEXT("a caller sails and the line moves up"), [stage](UnitySpaceApp& app, double)
		{
			auto infra = app.World()->Infrastructure;
			auto st = stage->habitat;
			if (st == nullptr || st->Berthed.Count() == 0) return true;
			auto leaving = st->Berthed[0];
			cs::Ref<Ship> first = st->Holding.Count() > 0 ? st->Holding[0] : nullptr;
			if (leaving != nullptr) infra->ReleaseBerth(leaving);
			int berth = -1;
			auto now = first != nullptr ? infra->BerthedAt(first, berth) : nullptr;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Berths: %s left berth 1; %s, first in line, now %s berth %d"),
				leaving != nullptr ? *T(leaving->Name) : TEXT("nobody"), first != nullptr ? *T(first->Name) : TEXT("nobody"),
				now == st ? TEXT("in") : TEXT("not in"), berth + 1);
			Log(app, st);
			return true;
		});
		// Let go: the callers are ordinary traffic again (berthed by the usual rule while they stay parked, freed as they sail).
		push(TEXT("callers released to the traffic"), [stage](UnitySpaceApp& app, double)
		{
			for (const auto& ship : stage->callers) app.World()->Infrastructure->ReleaseBerth(ship);
			return true;
		});
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	FAutoConsoleCommand GBerthsCommand(
		TEXT("sg.Berths"),
		TEXT("US-T435: sg.Berths [quit] fills a port's habitat and yard with calling ships (every berth taken, a few holding), "
			"frames each whole and along a pier and screenshots them (Saved/Screenshots/berths-*.png)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
