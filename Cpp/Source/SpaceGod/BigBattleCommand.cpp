// US-T164: "Start a big battle and watch" -- two mixed fleets thrown at each other off an empty world, and a director
// camera that cuts between the furball, a fighter's strafing run down a capital's length and a dogfight up close.
// The dev menu's entry, the showcase tour's stop and sg.BigBattle (with screenshots a second apart for the record).

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "GameLaunch.h"
#include "SceneTour.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Target.h"
#include "Tour/ShowcaseTour.h"
#include "UI/CheatMenu.h"
#include "UnitySpaceAppActor.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Battle.h"
#include "Combat/Choreography.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/War.h"
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

	/// <summary>Each side of the big battle: a carrier (its squadrons launch as the fight opens), the line, the escorts and
	/// a flight of crewed fighters.</summary>
	cs::List<ShipClass> BigFleet()
	{
		return cs::List<ShipClass>{
			ShipClass::Carrier, ShipClass::Battleship, ShipClass::Cruiser,
			ShipClass::Destroyer, ShipClass::Destroyer, ShipClass::Frigate, ShipClass::Corvette, ShipClass::Corvette,
			ShipClass::Fighter, ShipClass::Fighter, ShipClass::Fighter, ShipClass::Interceptor, ShipClass::Interceptor,
		};
	}

	/// <summary>Seconds of the fight run before it is shown (see StartBigBattle). US-T180: 20 (was 50): the fleets now open
	/// 14 km apart and the first craft are in contact within ten seconds, so 20 s puts the watching at the squadrons' first
	/// passes rather than a minute into the fight.</summary>
	constexpr double PrerollSeconds = 20;

	/// <summary>Starts it off Uranus (empty, so the live war's own traffic keeps out of the picture), Sol against the Reach,
	/// and drops to battle time. Null if the fight did not start.</summary>
	cs::Ref<Battle> StartBigBattle(SpaceGod::UnitySpaceApp& app)
	{
		auto uranus = (*app.Catalog())["Uranus"];
		int body = uranus != nullptr ? uranus->Index : 0;
		auto battle = SpaceGod::SceneTour::TriggerSkirmish(app, body, app.World()->Sol, app.World()->Reach, BigFleet());
		if (battle != nullptr)
		{
			// The fleets open 14 km apart (US-T180): the fight is run on to contact (the squadrons out, the first runs lined up),
			// on the battle's own clock, so the watching starts in the thick of it rather than a minute of closing in.
			battle->Advance(PrerollSeconds, false);
			app.Clock->SetPaused(false);
			app.WatchBattle(battle);
		}
		return battle;
	}

	DVec3 ToScene(DVec3 battleDir) { return battleDir.SwapYZ(); }

	SpaceGod::Quaternion Look(DVec3 from, DVec3 at)
	{
		auto d = (at - from);
		if (d.LengthSquared() < 1e-24) d = DVec3(0, 0, 1);
		d = d.Normalized();
		return SpaceGod::Quaternion::LookRotation(SpaceGod::Vector3(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z)), SpaceGod::Vector3::up());
	}

	/// <summary>
	/// The director: every <see cref="ShotSeconds"/> a new shot of the battle, cycling the furball as a whole (slowly
	/// circling it), a strafing run (from off the capital's beam on the strafer's side, the whole hull in frame), and a
	/// dogfight (chasing a fighter from behind). Each shot is placed from the battle's own km through the active scale, so
	/// it frames alike at game and realistic scale.
	/// </summary>
	struct Director
	{
		static constexpr double ShotSeconds = 8;
		cs::Ref<Battle> Fight;
		int Shot = -1;
		cs::Ref<BattleUnit> Subject, Hull;

		void Frame(SpaceGod::UnitySpaceApp& app, double t)
		{
			if (Fight == nullptr || Fight->Over) return;
			int shot = static_cast<int>(t / ShotSeconds);
			if (shot != Shot)
			{
				Shot = shot;
				Pick(app, shot % 3);
			}
			double kmScene = app.BattleKmScale();
			if (Shot % 3 == 1 && Subject != nullptr && Hull != nullptr && Subject->Present() && Hull->Present())
			{
				// Off the capital's beam on the strafer's side, a little above, back far enough to hold the whole hull and the pass.
				auto a = Hull->Forward;
				auto rel = Subject->Pos - Hull->Pos;
				auto lat = rel - a * DVec3::Dot(rel, a);
				auto n = lat.LengthSquared() > 1e-12 ? lat.Normalized() : DVec3::UnitY;
				double back = Hull->LengthKm * 1.6 + 1.5;
				auto eyeKm = Hull->Pos + n * back + DVec3::UnitZ * (back * 0.35);
				auto eye = app.BattleScene(Fight, eyeKm);
				auto at = app.BattleScene(Fight, Hull->Pos + rel * 0.35);
				app.SpaceCamera->SetPose(eye, Look(eye, at));
				return;
			}
			if (Shot % 3 == 0 || Subject == nullptr || !Subject->Present())
			{
				// The whole furball, circling it slowly.
				if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
				DVec3 sum;
				int n = 0;
				for (const auto& u : Fight->Units)
					if (u->Present() && u->Kind != UnitKind::Platform) { sum = sum + u->Pos; n++; }
				if (n == 0) return;
				auto c = sum / n;
				double spread = 0;
				for (const auto& u : Fight->Units)
					if (u->Present() && u->Kind != UnitKind::Platform) spread = cs::Math::Max(spread, (u->Pos - c).Length());
				// Close enough that the hulls read as hulls, not specks against the world (stragglers may leave the frame).
				spread = cs::Math::Min(cs::Math::Max(spread, 5.0), 18.0);
				double ang = t * 0.05;
				auto eyeKm = c + DVec3(cs::Math::Cos(ang), cs::Math::Sin(ang), 0.45) * spread;
				auto eye = app.BattleScene(Fight, eyeKm);
				app.SpaceCamera->SetPose(eye, Look(eye, app.BattleScene(Fight, c)));
				(void)kmScene;
			}
		}

		void Pick(SpaceGod::UnitySpaceApp& app, int kind)
		{
			Subject = nullptr;
			Hull = nullptr;
			if (kind == 1)
			{
				// A strafer on its run, preferably with its drive cut and its guns on the hull already.
				for (int pass = 0; pass < 2 && Subject == nullptr; pass++)
					for (const auto& u : Fight->Units)
					{
						if (!u->Present() || u->Target == nullptr || !u->Target->Present() || !Choreography::Strafeable(u->Target)) continue;
						if (pass == 0 ? !Choreography::Coasting(u) : !Choreography::Strafing(u)) continue;
						Subject = u;
						Hull = u->Target;
						break;
					}
			}
			else if (kind == 2)
			{
				// A small craft in a dogfight (on a fighter, not a hull), chased from behind.
				for (const auto& u : Fight->Units)
					if (u->Present() && u->Target != nullptr && u->Target->Present() && u->LengthKm < 0.2 && u->Target->LengthKm < 0.2)
					{
						Subject = u;
						break;
					}
				if (Subject != nullptr)
				{
					auto t = Subject->Ship != nullptr ? SpaceGod::Target::OfShip(Subject->Ship) : SpaceGod::Target::OfUnit(Fight, Subject);
					app.SpaceCamera->EnterPilotChase(t, 6);
					return;
				}
			}
			if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
		}
	};

	SpaceGod::UI::CheatRegistration GBigBattleCheat([]
	{
		SpaceGod::UI::CheatRegistry::Add("Combat", SpaceGod::UI::CheatEntry::Action("Start a big battle and watch",
			"two mixed fleets off Uranus: carriers' squadrons strafing the capitals, escorts making passes, the line coming about",
			[](SpaceGod::UnitySpaceApp& app)
			{
				if (StartBigBattle(app) == nullptr) app.ShowToast("The big battle did not start");
				else app.ShowToast("BIG BATTLE off Uranus: Sol against the Reach (Tab to cycle targets, . to speed up)");
			}).WithKeys("sg.BigBattle"));
	});

	SpaceGod::Tour::TourRegistration GBigBattleTour([]
	{
		auto director = std::make_shared<Director>();
		SpaceGod::Tour::TourRegistry::Add(SpaceGod::Tour::TourStop::Make("Flight and combat", "big-battle", "Two nests of hornets",
			"A big fleet action: fighters burn in, cut their drives and rake a capital end to end before flipping out; escorts make passes; the line comes about to show fresh shields")
			.At(35)
			.WithSetup([director](SpaceGod::UnitySpaceApp& app) -> cs::String
			{
				*director = Director();
				director->Fight = StartBigBattle(app);
				return director->Fight != nullptr ? cs::String() : cs::String("the big battle did not start");
			})
			.Until([](SpaceGod::UnitySpaceApp&, double t) { return t > 6; }) // the squadrons out and the runs begun
			.WithCamera([director](SpaceGod::UnitySpaceApp& app, double t) { director->Frame(app, t); })
			.For(24).AtBothScales());
	});

	/// <summary>One run of sg.BigBattle, ticked once a frame.</summary>
	struct Run
	{
		FString Scale = TEXT("game"), Shots;
		bool Quit = false, KeepScale = false;
		int ShotCount = 20;
		double Started = FPlatformTime::Seconds();
		double WatchStart = -1, NextShot = 0;
		int Taken = 0;
		bool AskedNewGame = false, Scaled = false;
		Director Dir;

		bool Tick()
		{
			auto* app = RunningApp();
			double now = FPlatformTime::Seconds();
			if (app == nullptr)
			{
				if (!AskedNewGame && now - Started > 2.0)
				{
					AskedNewGame = true;
					SpaceGod::GameLaunch::NewGame();
				}
				return now - Started < 180.0 || Finish(TEXT("no game came up"));
			}
			if (now - Started < (Shots.IsEmpty() ? 0.5 : 4.0)) return true;
			if (!Scaled)
			{
				double goal = Scale == TEXT("game") ? 1.0 : 0.0;
				if (!KeepScale && app->ScaleGoal != goal) app->ToggleScale();
				if (!KeepScale && app->ScaleAnimating()) return true;
				Scaled = true;
			}
			if (Dir.Fight == nullptr)
			{
				Dir.Fight = StartBigBattle(*app);
				if (Dir.Fight == nullptr) return Finish(TEXT("the battle did not start"));
				WatchStart = now;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.BigBattle: %s, %d units"), UTF8_TO_TCHAR(Dir.Fight->Name.c_str()), Dir.Fight->Units.Count());
				return true;
			}
			double t = now - WatchStart;
			Dir.Frame(*app, t);
			if (Shots.IsEmpty()) return !Dir.Fight->Over;
			// Settle (the squadrons launched and the runs begun), then a picture a second.
			if (t > 6 && t >= NextShot && Taken < ShotCount)
			{
				NextShot = t + 1.0;
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(Shots, FString::Printf(TEXT("bigbattle-%s-%02d.png"), *Scale, Taken)), false, false);
				LogState(t);
				Taken++;
			}
			if (Taken >= ShotCount && t >= NextShot + 0.5) return Finish(nullptr);
			return true;
		}

		void LogState(double t)
		{
			int coast = 0, strafe = 0, loop = 0, roll = 0, crossing = 0, present = 0, onHulls = 0, onCraft = 0, engaged = 0;
			for (const auto& u : Dir.Fight->Units)
			{
				if (!u->Present()) continue;
				present++;
				if (u->LengthKm < 0.2 && u->Target != nullptr) (Choreography::Strafeable(u->Target) ? onHulls : onCraft)++;
				// US-T180's measure: a small craft within 3 km of its mark (a capital's keel), or coasting down a hull.
				if (u->LengthKm < 0.2 && u->Target != nullptr && u->Target->Present())
				{
					auto mark = u->Target;
					double d = (mark->Pos - u->Pos).Length();
					if (Choreography::Strafeable(mark))
					{
						auto keel = mark->Forward.LengthSquared() > 1e-18 ? mark->Forward.Normalized() : DVec3::UnitX;
						double half = mark->LengthKm * 0.5, along = cs::Math::Max(-half, cs::Math::Min(half, DVec3::Dot(u->Pos - mark->Pos, keel)));
						d = (mark->Pos + keel * along - u->Pos).Length();
					}
					if (d < 3 || (Choreography::Coasting(u) && Choreography::Strafeable(mark))) engaged++;
				}
				if (Choreography::Coasting(u)) coast++;
				if (Choreography::Strafing(u)) strafe++;
				if (u->Maneuver == DogfightManeuver::Loop) loop++;
				if (u->Maneuver == DogfightManeuver::Roll) roll++;
				if (u->Maneuver == DogfightManeuver::Crossing) crossing++;
			}
			UE_LOG(LogSpaceGod, Display, TEXT("sg.BigBattle: t=%.0f battle %.0f s shot %d (%d): %d present, small craft on hulls %d / on craft %d, %d engaged, %d strafing (%d coasting), %d looping, %d rolling, %d capitals crossing"),
				t, Dir.Fight->Seconds, Dir.Shot % 3, Taken, present, onHulls, onCraft, engaged, strafe, coast, loop, roll, crossing);
		}

		bool Finish(const TCHAR* failure)
		{
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.BigBattle: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.BigBattle: done")); }
			if (Quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	/// US-T164: <c>sg.BigBattle [game|real|here] [shots &lt;dir&gt; [count=N] [quit]]</c>: starts the big battle off Uranus at
	/// battle time and directs the camera round it; with shots, bigbattle-&lt;scale&gt;-NN.png a second apart into dir.
	FAutoConsoleCommand GBigBattleCommand(
		TEXT("sg.BigBattle"),
		TEXT("US-T164: start a big battle and watch: sg.BigBattle [game|real|here] [shots <dir> [count=N] [quit]]."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
		{
			auto run = std::make_shared<Run>();
			for (int i = 0; i < args.Num(); i++)
			{
				const FString& a = args[i];
				if (a.Equals(TEXT("game"), ESearchCase::IgnoreCase) || a.Equals(TEXT("real"), ESearchCase::IgnoreCase)) run->Scale = a.ToLower();
				else if (a.Equals(TEXT("here"), ESearchCase::IgnoreCase)) run->KeepScale = true;
				else if (a.Equals(TEXT("shots"), ESearchCase::IgnoreCase) && i + 1 < args.Num()) run->Shots = args[++i];
				else if (a.StartsWith(TEXT("count="), ESearchCase::IgnoreCase)) run->ShotCount = FCString::Atoi(*a.Mid(6));
				else if (a.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) run->Quit = true;
			}
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
			{
				try { return run->Tick(); }
				catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
			}));
		}));
}
