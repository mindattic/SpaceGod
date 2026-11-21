// US-T447: sg.Effects, the world's effects seen and screenshotted. A capital, a freighter and a fighter are held still in
// open space with their main engines lit (sg.ThrustersLit 2) and framed from astern (exhaust clouds); the fighter's boost
// is opened (its charge drained) for the boost's kick; the freighter's every jet lit for its RCS vapour; a cruise drive
// engaging and an FTL jump are played on the hulls; a hull comes through a jump gate and another goes out through it; then a
// fleet action off Uranus: the furball, a hit on a hull, a hull blown apart (its shockwave, then the debris cloud hanging),
// a damaged hull venting, a fighter turning (its curved trail) and a craft on its boost.
//   sg.Effects shot <dir> [quit]

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "GameLaunch.h"
#include "SceneTour.h"
#include "SimClock.h"
#include "SpaceGodLog.h"
#include "Target.h"
#include "UnitySpaceAppActor.h"
#include "Views/GateView.h"
#include "Views/WorldFx.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/War.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
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
using SpaceGod::Target;
using SpaceGod::UnitySpaceApp;

namespace
{
	using SpaceGod::Quaternion;
	using SpaceGod::Vector3;

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

	void SetLit(int v)
	{
		if (auto* lit = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.ThrustersLit"))) lit->Set(v, ECVF_SetByCode);
	}

	Vector3 V(DVec3 d) { return Vector3(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z)); }
	DVec3 D(Vector3 v) { return DVec3(v.x, v.y, v.z); }

	Quaternion Look(DVec3 from, DVec3 at, Vector3 up = Vector3::up())
	{
		auto d = at - from;
		if (d.LengthSquared() < 1e-30) d = DVec3(0, 0, 1);
		auto f = V(d.Normalized());
		if (SpaceGod::Mathf::Abs(Vector3::Dot(f, up.normalized())) > 0.98f) up = Vector3::forward();
		return Quaternion::LookRotation(f, up);
	}

	struct Run
	{
		using Step = std::function<bool(UnitySpaceApp&, double)>;
		std::vector<std::pair<FString, Step>> steps;
		size_t at = 0;
		double stepStart = -1, started = FPlatformTime::Seconds();
		bool askedNewGame = false, quit = false;
		int shots = 0;

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
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Effects: %s"), *steps[at].first);
				at++;
				stepStart = -1;
			}
			else if (now - stepStart > 90.0) return Finish(*FString::Printf(TEXT("timed out at '%s'"), *steps[at].first));
			return true;
		}

		bool Finish(const TCHAR* failure)
		{
			SetLit(0);
			if (auto* app = App()) app->Clock->SetPaused(false);
			if (failure) { UE_LOG(LogSpaceGod, Warning, TEXT("sg.Effects: %s"), failure); }
			else { UE_LOG(LogSpaceGod, Display, TEXT("sg.Effects: done, %d screenshots"), shots); }
			if (quit) FPlatformMisc::RequestExit(false);
			return false;
		}
	};

	/// <summary>The drawn root of a ship's model: the ancestor of its renderers named after it.</summary>
	cs::Ref<SpaceGod::Transform> RootOf(UnitySpaceApp& app, const cs::Ref<Ship>& ship)
	{
		for (const auto& r : app.ModelRenderersOf(Target::OfShip(ship)))
		{
			if (!r) continue;
			for (auto t = r->transform(); t; t = t->parent.lock())
				if (t->gameObject() && t->gameObject()->name == ship->Name) return t;
		}
		return nullptr;
	}

	struct Stage
	{
		cs::Ref<Ship> Capital, Freighter, Fighter, Runner, Arrival;
		DVec3 At;
		int Body = 0;
		bool Boost = false;
		cs::Ref<Battle> Fight;
		cs::Ref<BattleUnit> Subject;
		DVec3 FxAt, FxDir, Eye;
		double FxLen = 0;
		int FxSeen = 0;
		double SubjectBoost = -1;
	};

	void Log(UnitySpaceApp& app, const FString& what)
	{
		auto* fx = app.Effects();
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Effects: %s: %d clouds (%d drawn), %d sparks/embers, %d flashes"), *what,
			fx ? fx->Clouds() : 0, fx ? fx->CloudsDrawn : 0, fx ? fx->Sparks() : 0, fx ? fx->Flashes() : 0);
	}

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
		auto push = [&run](FString name, Run::Step step) { run->steps.push_back({ name, std::move(step) }); };
		auto shot = [&push, dir, run](const TCHAR* name)
		{
			FString file = FPaths::Combine(dir, FString::Printf(TEXT("effects-%s.png"), name));
			FString tag = name;
			push(FString::Printf(TEXT("shot %s"), name), [file, tag, run](UnitySpaceApp& app, double)
			{
				Log(app, tag);
				if (!file.IsEmpty())
				{
					FScreenshotRequest::RequestScreenshot(file, false, false);
					run->shots++;
					UE_LOG(LogSpaceGod, Display, TEXT("sg.Effects: screenshot %s"), *file);
				}
				return true;
			});
		};
		// Holds the staged hulls still (parked, the clock paused) and drains the fighter's boost while it is open.
		auto hold = [s](UnitySpaceApp& app)
		{
			for (const auto& ship : { s->Capital, s->Freighter, s->Fighter })
				if (ship) { ship->Phase = ShipPhase::Parked; ship->PhaseEndJd = app.Clock->Jd + 1; }
			if (s->Boost && s->Fighter && s->Fighter->Combat)
				s->Fighter->Combat->BoostFraction = cs::Math::Max(0.0, s->Fighter->Combat->BoostFraction - 0.01);
		};
		// Frames a staged hull: eye off its own axes (off), looking at a point along its keel (lookAlong, in lengths).
		auto frame = [s, hold](std::function<cs::Ref<Ship>()> pick, Vector3 off, double distance, float lookAlong)
		{
			return [s, hold, pick, off, distance, lookAlong](UnitySpaceApp& app, double t)
			{
				hold(app);
				auto ship = pick();
				auto root = RootOf(app, ship);
				auto centre = app.FocusScene(Target::OfShip(ship));
				double length = app.ShipDisplayLength(ship);
				if (length > 0)
				{
					auto rot = root ? root->rotation() : Quaternion::identity();
					auto aim = centre + D(rot * Vector3::forward()) * (length * lookAlong);
					auto eye = aim + D(rot * off.normalized()) * (length * distance);
					app.SpaceCamera->SetPose(eye, Look(eye, aim, rot * Vector3::up()));
				}
				return (root && t > 0.0) || t > 20;
			};
		};
		auto wait = [hold](double seconds) { return [hold, seconds](UnitySpaceApp& app, double t) { hold(app); return t >= seconds; }; };
		auto keep = [](std::function<bool(UnitySpaceApp&, double)> a, std::function<bool(UnitySpaceApp&, double)> b)
		{
			return [a, b](UnitySpaceApp& app, double t) { a(app, t); return b(app, t); };
		};

		push(TEXT("game up"), [](UnitySpaceApp&, double t) { return t >= 6.0; });
		push(TEXT("hulls staged in open space"), [s](UnitySpaceApp& app, double)
		{
			app.SkyLeaveHelm();
			if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
			app.Clock->SetPaused(true);
			s->Body = app.Catalog()->Root()->Index;
			s->At = DVec3(-0.4, 1.3, 0.05) * Astro::AuKm;
			auto spawn = [&](const char* name, ShipClass c, double x)
			{
				auto ship = app.Fleet()->SpawnAt(name, c, app.World()->Sol, s->Body, s->At + DVec3(x, 0, 0), DVec3::UnitY, app.Clock->Jd);
				if (!ship) throw std::runtime_error("a hull would not spawn");
				return ship;
			};
			s->Capital = spawn("Effects Capital", ShipClass::Battleship, 0);
			s->Freighter = spawn("Effects Freighter", ShipClass::Freighter, 20000);
			s->Fighter = spawn("Effects Fighter", ShipClass::Fighter, 40000);
			SetLit(2);
			return true;
		});

		// ---- exhaust clouds: a capital and a freighter under thrust, framed from astern
		auto capital = [s] { return s->Capital; };
		auto freighter = [s] { return s->Freighter; };
		auto fighter = [s] { return s->Fighter; };
		push(TEXT("capital framed astern"), keep(frame(capital, Vector3(0.75f, 0.5f, -1.0f), 1.9, -0.7f), wait(3.0)));
		shot(TEXT("capital-exhaust"));
		push(TEXT("capital framed abeam"), keep(frame(capital, Vector3(-1.0f, 0.35f, -0.25f), 2.2, -0.9f), wait(1.0)));
		shot(TEXT("capital-exhaust-abeam"));
		push(TEXT("freighter framed astern"), keep(frame(freighter, Vector3(0.8f, 0.55f, -1.0f), 2.0, -0.7f), wait(3.0)));
		shot(TEXT("freighter-exhaust"));
		push(TEXT("fighter framed astern"), keep(frame(fighter, Vector3(0.9f, 0.45f, -1.0f), 2.6, -1.2f), wait(1.5)));
		shot(TEXT("fighter-exhaust"));
		// ---- the boost opening on the fighter (its charge drained, as a boost drains it)
		push(TEXT("fighter boost opened"), [s](UnitySpaceApp&, double) { s->Boost = true; return true; });
		push(TEXT("boost kick"), keep(frame(fighter, Vector3(0.9f, 0.45f, -1.0f), 2.6, -1.2f), wait(0.12)));
		shot(TEXT("boost-kick"));
		push(TEXT("boost burning"), keep(frame(fighter, Vector3(0.9f, 0.45f, -1.0f), 2.6, -1.2f), wait(1.2)));
		shot(TEXT("boost-burn"));
		push(TEXT("boost closed"), [s](UnitySpaceApp&, double) { s->Boost = false; return true; });
		// ---- RCS vapour: every jet of the freighter lit
		push(TEXT("freighter's every jet lit"), [](UnitySpaceApp&, double) { SetLit(1); return true; });
		push(TEXT("freighter framed abeam"), keep(frame(freighter, Vector3(-1.0f, 0.6f, 0.45f), 1.6, 0.1f), wait(1.0)));
		shot(TEXT("rcs-vapour"));
		push(TEXT("main engines alone again"), [](UnitySpaceApp&, double) { SetLit(2); return true; });

		// ---- the drives: the freighter's cruise drive engaging, the capital's FTL jump
		push(TEXT("freighter framed for cruise"), keep(frame(freighter, Vector3(0.9f, 0.5f, -0.6f), 2.6, -0.4f), wait(0.6)));
		push(TEXT("cruise engaged"), [s](UnitySpaceApp& app, double)
		{
			auto root = RootOf(app, s->Freighter);
			if (app.Effects() && root)
				app.Effects()->CruiseEngage(app.FocusScene(Target::OfShip(s->Freighter)), root->rotation() * Vector3::forward(), static_cast<float>(app.ShipDisplayLength(s->Freighter)));
			return true;
		});
		push(TEXT("cruise ring out"), wait(0.25));
		shot(TEXT("cruise-engage"));
		push(TEXT("capital framed for FTL"), keep(frame(capital, Vector3(1.0f, 0.4f, 0.3f), 3.5, 0.0f), wait(0.6)));
		push(TEXT("FTL jump"), [s](UnitySpaceApp& app, double)
		{
			auto root = RootOf(app, s->Capital);
			if (app.Effects() && root)
				app.Effects()->FtlJump(app.FocusScene(Target::OfShip(s->Capital)), root->rotation() * Vector3::forward(), static_cast<float>(app.ShipDisplayLength(s->Capital)), false);
			return true;
		});
		push(TEXT("FTL flash"), wait(0.12));
		shot(TEXT("ftl-jump"));
		push(TEXT("FTL streak"), wait(0.35));
		shot(TEXT("ftl-jump-streak"));

		// ---- a gate: a hull comes through it (new to the fleet beside it) and goes out through it (its view's last frame)
		push(TEXT("camera off a gate"), [s](UnitySpaceApp& app, double t)
		{
			if (app.ActiveSystem == nullptr || app.ActiveSystem->Gates().Count() == 0) return true;
			auto gate = app.ActiveSystem->Gates()[0];
			auto at = app.Scale()->MapNear(app.Ephemeris(), app.BodyScenes(), app.Catalog()->Root()->Index, gate->PositionKm);
			double size = app.Scale()->ShipLength(SpaceGod::Views::GateView::DiameterKm);
			auto axis = gate->PositionKm.Normalized().SwapYZ();
			auto side = DVec3::Cross(axis, DVec3::UnitY);
			if (side.LengthSquared() < 1e-12) side = DVec3::UnitX;
			side = side.Normalized();
			s->FxAt = at;
			s->FxDir = axis;
			s->FxLen = size;
			s->Eye = at + side * (size * 2.2) + DVec3::UnitY * (size * 0.6) + axis * (size * 0.8);
			app.SpaceCamera->SetPose(s->Eye, Look(s->Eye, at));
			return t > 1.5;
		});
		push(TEXT("a hull comes through"), [s](UnitySpaceApp& app, double)
		{
			if (app.ActiveSystem == nullptr || app.ActiveSystem->Gates().Count() == 0) return true;
			auto gate = app.ActiveSystem->Gates()[0];
			auto dirKm = gate->PositionKm.Normalized();
			// Just inside the ring, heading in from it.
			s->Arrival = app.Fleet()->SpawnAt("Effects Arrival", ShipClass::Destroyer, app.World()->Sol, app.Catalog()->Root()->Index,
				gate->PositionKm - dirKm * 3.0, -dirKm, app.Clock->Jd);
			app.SpaceCamera->SetPose(s->Eye, Look(s->Eye, s->FxAt));
			return true;
		});
		push(TEXT("arrival flash"), [s](UnitySpaceApp& app, double t) { app.SpaceCamera->SetPose(s->Eye, Look(s->Eye, s->FxAt)); return t > 0.15; });
		shot(TEXT("gate-arrive"));
		push(TEXT("arrival fold opening"), [s](UnitySpaceApp& app, double t) { app.SpaceCamera->SetPose(s->Eye, Look(s->Eye, s->FxAt)); return t > 0.45; });
		shot(TEXT("gate-arrive-open"));
		push(TEXT("it settles"), [s](UnitySpaceApp& app, double t) { app.SpaceCamera->SetPose(s->Eye, Look(s->Eye, s->FxAt)); return t > 2.5; });
		push(TEXT("it goes out through the gate"), [s](UnitySpaceApp& app, double)
		{
			// As a gate's transit retires it: gone through (Destroyed with GateDeparted: nothing draws a wreck).
			if (s->Arrival) { s->Arrival->Destroyed = true; s->Arrival->GateDeparted = true; }
			app.SpaceCamera->SetPose(s->Eye, Look(s->Eye, s->FxAt));
			return true;
		});
		push(TEXT("fold closing"), [s](UnitySpaceApp& app, double t) { app.SpaceCamera->SetPose(s->Eye, Look(s->Eye, s->FxAt)); return t > 0.3; });
		shot(TEXT("gate-depart-fold"));
		push(TEXT("gate flash"), [s](UnitySpaceApp& app, double t) { app.SpaceCamera->SetPose(s->Eye, Look(s->Eye, s->FxAt)); return t > 0.25; });
		shot(TEXT("gate-depart-flash"));

		// ---- a fight off Uranus
		push(TEXT("fleet action begun"), [s](UnitySpaceApp& app, double)
		{
			SetLit(0);
			auto uranus = (*app.Catalog())["Uranus"];
			int body = uranus != nullptr ? uranus->Index : 0;
			cs::List<ShipClass> fleet{ ShipClass::Carrier, ShipClass::Battleship, ShipClass::Cruiser, ShipClass::Destroyer,
				ShipClass::Destroyer, ShipClass::Frigate, ShipClass::Corvette, ShipClass::Corvette, ShipClass::Fighter,
				ShipClass::Fighter, ShipClass::Fighter, ShipClass::Interceptor, ShipClass::Interceptor };
			s->Fight = SpaceGod::SceneTour::TriggerSkirmish(app, body, app.World()->Sol, app.World()->Reach, fleet);
			if (s->Fight == nullptr) throw std::runtime_error("the fight did not start");
			s->Fight->Advance(20, false);
			app.Clock->SetPaused(false);
			app.WatchBattle(s->Fight);
			s->FxSeen = s->Fight->FxSeq;
			return true;
		});
		// The whole furball, from off its middle.
		auto furball = [s](UnitySpaceApp& app, double t)
		{
			if (!app.SpaceCamera->FocusTarget.IsNone()) app.SpaceCamera->ExitFocus();
			DVec3 sum;
			int n = 0;
			for (const auto& u : s->Fight->Units)
				if (u->Present() && u->Kind != UnitKind::Platform) { sum = sum + u->Pos; n++; }
			if (n == 0) return t > 3;
			auto c = sum / n;
			auto eye = app.BattleScene(s->Fight, c + DVec3(6, 3, 2.5));
			app.SpaceCamera->SetPose(eye, Look(eye, app.BattleScene(s->Fight, c)));
			return t > 4;
		};
		push(TEXT("furball"), furball);
		shot(TEXT("fight-furball"));
		// A hit on a hull, framed the moment it lands (sparks, specks of plating, smoke).
		push(TEXT("a hit on a hull"), [s, furball](UnitySpaceApp& app, double t)
		{
			const auto& fx = s->Fight->Fx;
			for (int i = fx.Count() - 1; i >= 0 && fx[i].Seq > s->FxSeen; i--)
			{
				const auto& e = fx[i];
				if (e.Kind != FxKind::Impact || e.Unit == nullptr || e.Unit->LengthKm < 0.12 || !e.Unit->Present()) continue;
				auto out = e.A - e.Unit->Pos;
				out = out.LengthSquared() > 1e-18 ? out.Normalized() : DVec3::UnitZ;
				s->FxAt = e.A;
				s->FxLen = e.Unit->LengthKm;
				s->Eye = e.A + out * (e.Unit->LengthKm * 0.9) + DVec3::UnitZ * (e.Unit->LengthKm * 0.35);
				auto eye = app.BattleScene(s->Fight, s->Eye);
				app.SpaceCamera->SetPose(eye, Look(eye, app.BattleScene(s->Fight, e.A)));
				s->FxSeen = s->Fight->FxSeq;
				return true;
			}
			s->FxSeen = s->Fight->FxSeq;
			furball(app, t);
			return t > 40;
		});
		auto onFx = [s](double seconds)
		{
			return [s, seconds](UnitySpaceApp& app, double t)
			{
				auto eye = app.BattleScene(s->Fight, s->Eye);
				app.SpaceCamera->SetPose(eye, Look(eye, app.BattleScene(s->Fight, s->FxAt)));
				return t >= seconds;
			};
		};
		push(TEXT("sparks flying"), onFx(0.1));
		shot(TEXT("fight-impact"));
		push(TEXT("smoke left"), onFx(0.5));
		shot(TEXT("fight-impact-smoke"));
		// A hull blown apart: framed the moment it goes, then the cloud left hanging.
		push(TEXT("a hull lost"), [s, furball](UnitySpaceApp& app, double t)
		{
			const auto& fx = s->Fight->Fx;
			for (int i = fx.Count() - 1; i >= 0 && fx[i].Seq > s->FxSeen; i--)
			{
				const auto& e = fx[i];
				if (e.Kind != FxKind::UnitDestroyed || e.Size < 0.03) continue;
				s->FxAt = e.A;
				s->FxLen = e.Size;
				s->Eye = e.A + DVec3(0.8, 0.5, 0.35).Normalized() * (e.Size * 6.0);
				auto eye = app.BattleScene(s->Fight, s->Eye);
				app.SpaceCamera->SetPose(eye, Look(eye, app.BattleScene(s->Fight, e.A)));
				s->FxSeen = s->Fight->FxSeq;
				UE_LOG(LogSpaceGod, Display, TEXT("sg.Effects: a hull %.3g km long lost"), e.Size);
				return true;
			}
			s->FxSeen = s->Fight->FxSeq;
			furball(app, t);
			return t > 80;
		});
		push(TEXT("blast"), onFx(0.25));
		shot(TEXT("fight-blast"));
		push(TEXT("shockwave out"), onFx(0.55));
		shot(TEXT("fight-blast-shock"));
		push(TEXT("debris cloud"), onFx(3.0));
		shot(TEXT("fight-blast-cloud"));
		// The most damaged hull still fighting, venting from its breaches.
		push(TEXT("a damaged hull"), [s](UnitySpaceApp& app, double t)
		{
			cs::Ref<BattleUnit> worst;
			for (const auto& u : s->Fight->Units)
				if (u->Present() && u->Kind == UnitKind::Ship && u->Ship != nullptr && u->Combat != nullptr && u->LengthKm > 0.1
					&& (worst == nullptr || u->Combat->HullFraction() < worst->Combat->HullFraction()))
					worst = u;
			s->Subject = worst;
			if (worst == nullptr) return t > 2;
			auto eye = app.BattleScene(s->Fight, worst->Pos + DVec3(0.55, 0.75, 0.45).Normalized() * (worst->LengthKm * 1.7));
			app.SpaceCamera->SetPose(eye, Look(eye, app.BattleScene(s->Fight, worst->Pos)));
			if (t <= 1.5) return false;
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Effects: most damaged hull %s at %.0f%%"), *T(worst->Ship->Name), worst->Combat->HullFraction() * 100);
			return true;
		});
		shot(TEXT("fight-venting"));
		// A small craft turning: watched from beside where it is, the camera turning to follow it (its trail curves behind it).
		push(TEXT("a fighter in a dogfight"), [s](UnitySpaceApp& app, double t)
		{
			s->Subject = nullptr;
			for (const auto& u : s->Fight->Units)
				if (u->Present() && u->Kind == UnitKind::Fighter && u->Target != nullptr && u->Target->Present() && u->Target->LengthKm < 0.2) { s->Subject = u; break; }
			if (s->Subject == nullptr)
				for (const auto& u : s->Fight->Units)
					if (u->Present() && u->Kind == UnitKind::Fighter) { s->Subject = u; break; }
			if (s->Subject == nullptr) return t > 2;
			auto u = s->Subject;
			auto side = DVec3::Cross(u->Forward, DVec3::UnitZ);
			if (side.LengthSquared() < 1e-12) side = DVec3::UnitX;
			s->Eye = u->Pos + side.Normalized() * 0.35 + DVec3::UnitZ * 0.12 - u->Forward * 0.1;
			return true;
		});
		auto follow = [s](double seconds)
		{
			return [s, seconds](UnitySpaceApp& app, double t)
			{
				if (s->Subject != nullptr && s->Subject->Present())
				{
					auto eye = app.BattleScene(s->Fight, s->Eye);
					app.SpaceCamera->SetPose(eye, Look(eye, app.BattleScene(s->Fight, s->Subject->Pos)));
				}
				return t >= seconds;
			};
		};
		push(TEXT("fighter turning 1"), follow(1.6));
		shot(TEXT("fighter-turn"));
		push(TEXT("fighter turning 2"), follow(1.2));
		shot(TEXT("fighter-turn-2"));
		// A craft on its boost (its charge falling), chased from behind and above.
		push(TEXT("a craft boosting"), [s](UnitySpaceApp& app, double t)
		{
			for (const auto& u : s->Fight->Units)
			{
				if (!u->Present() || u->LengthKm > 0.2 || u->Combat == nullptr) continue;
				if (u == s->Subject && s->SubjectBoost >= 0 && u->Combat->BoostFraction < s->SubjectBoost - 1e-4)
				{
					UE_LOG(LogSpaceGod, Display, TEXT("sg.Effects: boosting craft found, charge %.2f"), u->Combat->BoostFraction);
					return true;
				}
			}
			// Watch one craft at a time for its charge to fall.
			if (s->Subject == nullptr || !s->Subject->Present() || static_cast<int>(t * 2) % 3 == 0)
				for (const auto& u : s->Fight->Units)
					if (u->Present() && u->LengthKm < 0.2 && u->Combat != nullptr && u->Combat->BoostFraction > 0.2 && u != s->Subject) { s->Subject = u; break; }
			if (s->Subject != nullptr && s->Subject->Combat != nullptr) s->SubjectBoost = s->Subject->Combat->BoostFraction;
			if (s->Subject != nullptr)
			{
				auto u = s->Subject;
				auto eye = app.BattleScene(s->Fight, u->Pos - u->Forward * (u->LengthKm * 9) + DVec3::UnitZ * (u->LengthKm * 3));
				app.SpaceCamera->SetPose(eye, Look(eye, app.BattleScene(s->Fight, u->Pos + u->Forward * (u->LengthKm * 2))));
			}
			return t > 12;
		});
		push(TEXT("boost chased"), [s](UnitySpaceApp& app, double t)
		{
			if (s->Subject != nullptr && s->Subject->Present())
			{
				auto u = s->Subject;
				auto eye = app.BattleScene(s->Fight, u->Pos - u->Forward * (u->LengthKm * 9) + DVec3::UnitZ * (u->LengthKm * 3));
				app.SpaceCamera->SetPose(eye, Look(eye, app.BattleScene(s->Fight, u->Pos + u->Forward * (u->LengthKm * 2))));
			}
			return t > 0.4;
		});
		shot(TEXT("fight-boost"));
		push(TEXT("written"), [](UnitySpaceApp&, double t) { return t > 1.0; });

		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([run](float)
		{
			try { return run->Tick(); }
			catch (const std::exception& e) { return run->Finish(*FString(UTF8_TO_TCHAR(e.what()))); }
		}));
	}

	FAutoConsoleCommand GEffectsCommand(
		TEXT("sg.Effects"),
		TEXT("US-T447: sg.Effects [shot <dir>] [quit] frames the world's effects (exhaust clouds, a boost, RCS vapour, a cruise "
			"drive and an FTL jump, a gate transit both ways, a fight's hits, a blast and its cloud, a hull venting, a fighter's "
			"curved trail), screenshotting each into <dir>."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Start(args); }));
}
