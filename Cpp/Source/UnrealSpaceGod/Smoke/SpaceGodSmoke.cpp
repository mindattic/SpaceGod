#include "Smoke/SpaceGodSmoke.h"
#include "Smoke/ScriptedDevice.h"

#include "Bootstrap.h"
#include "CaptureRunner.h"
#include "GameLaunch.h"
#include "Target.h"
#include "UnitySpaceApp.h"
#include "UnitySpaceAppActor.h"
#include "UnrealSpaceGodLog.h"
#include "Camera/SpaceCamera.h"
#include "Input/InputDevice.h"
#include "Rendering/Pipeline/RenderBackend.h"
#include "Rendering/ShipModels.h"
#include "UI/MainMenu.h"
#include "UI/PauseMenu.h"
#include "UnityCompat/GuiHost.h"
#include "UnityCompat/UnityRendering.h"
#include "UnityCompat/UnityRuntime.h"
#include "UnityCompat/UnityScene.h"
#include "Views/BodyView.h"
#include "Views/InterstellarView.h"
#include "Views/LineStrip.h"
#include "Views/ShipView.h"
#include "Views/SkyView.h"
#include "Views/Starfield.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "BodyOrientation.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Rendering/Skyboxes.h"
#include "Shared/Profiles/Profiles.h"
#include "Systems/Galaxy.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include <set>

namespace SpaceGod
{
	/// The smoke's view of the app's private members (C#: reflection on UnitySpaceApp.pauseMenu).
	struct SmokeAccess
	{
		static const cs::Ref<UI::PauseMenu>& PauseMenu(UnitySpaceApp& app) { return app.pauseMenu; }
	};
}

namespace SpaceGod::Smoke
{
	using namespace SpaceGod::Core;
	using namespace SpaceGod::UnityCompat;
	using Input::Key;
	using Input::MouseButton;

	namespace
	{
		constexpr float FrameSeconds = 1.0f / 60.0f;

		FString ToF(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

		void Log(const cs::String& line) { UE_LOG(LogSpaceGod, Display, TEXT("%s"), *ToF(line)); }
		void LogError(const cs::String& line) { UE_LOG(LogSpaceGod, Error, TEXT("%s"), *ToF(line)); }

		UWorld* GameWorld()
		{
			if (!GEngine) return nullptr;
			for (const FWorldContext& c : GEngine->GetWorldContexts())
				if (c.World() && c.World()->IsGameWorld() && c.World()->HasBegunPlay()) return c.World();
			return nullptr;
		}

		void Quit(UWorld* world)
		{
			if (GEngine) GEngine->Exec(world, TEXT("quit"));
		}

		cs::String CommandLineValue(const TCHAR* key)
		{
			FString v;
			if (!FParse::Value(FCommandLine::Get(), key, v)) return cs::String();
			return cs::String(TCHAR_TO_UTF8(*v));
		}

		// ---------------------------------------------------------------- the run

		struct Run
		{
			enum class Mode { None, Smoke, Shots, Views };
			Mode mode = Mode::None;
			int phase = 0;
			int waited = 0;
			TWeakObjectPtr<AUnitySpaceAppActor> host;
			ScriptedDevice script;
			ScriptedGuiInput gui;
			int failures = 0;
			double startedSeconds = 0;
			FTSTicker::FDelegateHandle ticker;
			/// A click that loads another scene (New Game): its screen is drawn once that scene is up.
			cs::String pendingScreen, pendingVia;
			/// The screenshots: Time::frameCount at the game's first frame (US-T45).
			int gameStartFrame = -1;
		};
		Run run;

		// ---- the app loop

		void Frames(int n)
		{
			AUnitySpaceAppActor* h = run.host.Get();
			if (!h) return;
			for (int i = 0; i < n; i++)
			{
				run.script.Write();
				h->Step(FrameSeconds);
				run.script.EndFrame();
			}
		}

		bool TakeHost(AUnitySpaceAppActor* a)
		{
			if (!a || !a->HasActorBegunPlay()) return false;
			run.host = a;
			a->bExternallyStepped = true;
			a->bScriptedInput = true;
			return true;
		}

		// ---- the UI tour (UiSmoke.Run)

		void GuiFrame()
		{
			GuiHost::ScriptedFrame(run.gui, Time::unscaledTime);
			run.gui.Clear();
		}

		void Fail(const cs::String& what)
		{
			run.failures++;
			LogError(cs::Cat("UI smoke: ", what));
		}

		/// One frame of the app, then one GUI frame; logs what was drawn.
		void Draw(const cs::String& screen, bool allowEmpty = false, const cs::String& via = cs::String())
		{
			Frames(1);
			GuiFrame();
			const auto& s = GUI::Stats();
			if (s.Labels + s.Boxes + s.Buttons + s.Textures == 0 && !allowEmpty) Fail(cs::Cat(screen, ": drew nothing"));
			else Log(cs::Cat("UI smoke [", screen, "]", via.empty() ? cs::String() : cs::Cat(" (clicked '", via, "')"), ": ", s.ToString()));
		}

		/// Clicks the button labelled `label` where the last frame drew it. `travels`: the click loads another scene,
		/// whose first frame is drawn (and logged as `next`) once it is up.
		void Click(const cs::String& label, const cs::String& next, bool travels = false)
		{
			auto& buttons = GUI::DrawnButtons();
			auto it = buttons.find(label);
			if (it == buttons.end())
			{
				Fail(cs::Cat("no '", label, "' button to click on the way to ", next));
				return;
			}
			Rect r = it->second;
			run.gui.mouse = Vector2(r.x + r.width / 2, r.y + r.height / 2);
			GuiFrame(); // the move
			run.gui.pressed[0] = run.gui.held[0] = true;
			GuiFrame();
			run.gui.held[0] = false;
			run.gui.released[0] = true;
			Frames(1);
			GuiFrame();
			int clicks = GUI::Stats().Clicks;
			run.gui.mouse = Vector2(-1000, -1000);
			if (travels)
			{
				if (clicks == 0) Fail(cs::Cat("'", label, "' did not take the click"));
				run.pendingScreen = next;
				run.pendingVia = clicks == 0 ? cs::String() : label;
				return;
			}
			if (clicks == 0)
			{
				Fail(cs::Cat("'", label, "' did not take the click"));
				Draw(next);
			}
			else Draw(next, false, label);
		}

		void KeyDown(KeyCode code, const cs::String& next)
		{
			run.gui.keysDown.push_back({ code, 0 });
			GuiFrame();
			Draw(next);
		}

		void Toggle(bool& flag, const cs::String& screen, const std::function<void()>& inside = nullptr, bool allowEmpty = false)
		{
			flag = true;
			Draw(screen, allowEmpty);
			if (inside) inside();
			flag = false;
		}

		void MenuTour()
		{
			Draw("main menu");
			Click("Profiles", "main menu > profiles");
			Click("Back", "main menu");
			Click("Settings", "main menu > settings");
			Click("Back", "main menu");
			Click("Help", "main menu > help");
			KeyDown(KeyCode::DownArrow, "main menu > help, next chapter (Down)");
			Click("Next", "main menu > help, next chapter (Next)");
			Click("Back", "main menu");
			Click("Asset Library POC", "main menu > asset library POC");
			Click("Back", "main menu");
			Click("New Game", "new game", /*travels*/ true);
		}

		void GameTour(UnitySpaceApp& app)
		{
			if (!run.pendingScreen.empty())
			{
				Frames(2);
				Draw(run.pendingScreen, false, run.pendingVia);
				run.pendingScreen.clear();
			}
			Draw("game HUD");
			Toggle(app.ShowHelp, "help (H)");
			Toggle(app.ShowGalaxyMap, "galaxy map ([)", []
			{
				Click("DIPLOMACY", "galaxy map > diplomacy");
				Click("COMMAND", "galaxy map > command");
				Click("Close", "game HUD");
			});
			Toggle(app.ShowMarket, "market board");
			Toggle(app.ShowCargo, "cargo hold");
			Toggle(app.ShowTrade, "trade desk");
			Toggle(app.ShowFleet, "fleet");
			Toggle(app.ShowNews, "news");
			Toggle(app.ShowContracts, "contracts");
			Toggle(app.ShowBounties, "bounties");
			Toggle(app.ShowOutfit, "outfitting");
			Toggle(app.ShowSaves, "saves");
			Toggle(app.ShowSpawn, "spawn & pilot (V)");
			Toggle(app.ScreenCleared, "clear screen (F11): only the reticle, none while not piloting", nullptr, true);

			const auto& pause = SmokeAccess::PauseMenu(app);
			if (!pause) Fail("no pause menu on the app");
			else
			{
				pause->Open();
				Draw("pause menu (Esc)");
				Click("Settings", "pause menu > settings");
				Click("Display", "pause menu > settings > display");
				Click("Cancel", "pause menu > settings");
				Click("Back", "pause menu");
				Click("Help", "help from the pause menu");
				if (app.ShowHelp) app.ShowHelp = false;
				if (pause->IsOpen) pause->Close();
				Draw("game HUD");
			}
			Log(run.failures == 0 ? cs::String("UI smoke: every screen drew") : cs::Cat("UI smoke: ", run.failures, " step(s) failed"));
		}

		// ---- the app smoke (SpaceGameMode.RunSmoke)

		cs::String Describe(UnitySpaceApp& app, const Target& t)
		{
			if (t.IsBody()) return cs::Cat("body ", (*app.Catalog())[t.Body]->Name);
			if (t.IsShip()) return cs::Cat("ship ", t.Ship ? t.Ship->Name : cs::String());
			return ToString(t.Kind);
		}

		void Report(UnitySpaceApp& app, const cs::String& label)
		{
			auto cam = app.SpaceCamera->ScenePosition;
			Camera* c = app.UnityCamera ? app.UnityCamera : Camera::main();
			Log(cs::Cat("[", Time::frameCount, "] ", label, ": JD ", cs::Fmt(app.Clock->Jd, "F6"), " = ", app.Clock->Utc().ToString("yyyy-MM-dd HH:mm:ss"),
				" UTC, warp ", app.Clock->Warp().Label, "; camera scene (", cs::Fmt(cam.X, "G6"), ", ", cs::Fmt(cam.Y, "G6"), ", ", cs::Fmt(cam.Z, "G6"), ") ",
				Cameras::ToString(app.SpaceCamera->Mode), ", fov ", cs::Fmt(static_cast<double>(c->fieldOfView), "F1"), ", near ",
				cs::Fmt(static_cast<double>(c->nearClipPlane), "G3"), "; toast '", app.Toast, "'; facade ", UnityScene::Last().ToString()));
		}

		void AppSmoke(UnitySpaceApp& app)
		{
			double t0 = FPlatformTime::Seconds();
			auto ms = [t0] { return static_cast<long long>((FPlatformTime::Seconds() - t0) * 1000.0); };
			Frames(10);
			Report(app, cs::Cat("10 frames in ", ms(), " ms"));

			// Time warp: '.' one step faster.
			cs::String warpBefore = app.Clock->Warp().Label;
			run.script.Tap(Key::Period);
			Frames(1);
			Log(cs::Cat("SpaceGodSmoke: '.' warp ", warpBefore, " -> ", app.Clock->Warp().Label));

			// Free swim: W held for 30 frames, then a right-drag look.
			DVec3 from = app.SpaceCamera->ScenePosition;
			run.script.Hold(Key::W);
			Frames(30);
			run.script.Release(Key::W);
			double moved = (app.SpaceCamera->ScenePosition - from).Length();
			Quaternion rotBefore = app.SpaceCamera->Rotation;
			run.script.HoldButton(MouseButton::Right);
			for (int i = 0; i < 15; i++)
			{
				run.script.mouseDelta = Vector2(12.0f, 0.0f);
				Frames(1);
			}
			run.script.ReleaseButton(MouseButton::Right);
			Frames(1);
			float turned = Quaternion::Angle(rotBefore, app.SpaceCamera->Rotation);
			Log(cs::Cat("SpaceGodSmoke: camera moved ", cs::Fmt(moved, "G4"), " units (W, ", Cameras::ToString(app.SpaceCamera->Mode), "), turned ",
				cs::Fmt(static_cast<double>(turned), "F1"), " deg (right-drag)"));

			// Number key 3 twice: select Earth and focus on it.
			run.script.Tap(Key::Digit3);
			Frames(1);
			run.script.Tap(Key::Digit3);
			Frames(90);
			Log(cs::Cat("SpaceGodSmoke: '3' x2 selected ", Describe(app, app.Selected), ", camera ", Cameras::ToString(app.SpaceCamera->Mode), " on ",
				Describe(app, app.SpaceCamera->FocusTarget)));

			// Save, run on, load: the clock comes back to the saved moment.
			double savedJd = app.Clock->Jd;
			int savedShips = app.Fleet()->Ships().Count();
			bool saved = app.SaveTo("smoke", "US-T38 smoke");
			Frames(60);
			double ranOn = app.Clock->Jd;
			bool loaded = app.LoadFrom("smoke");
			Frames(2);
			Log(cs::Cat("SpaceGodSmoke: save ", saved ? "ok" : "FAILED", ", load ", loaded ? "ok" : "FAILED", ": JD saved ", cs::Fmt(savedJd, "F6"),
				", ran on to ", cs::Fmt(ranOn, "F6"), ", back to ", cs::Fmt(app.Clock->Jd, "F6"), " (|d| = ",
				cs::Fmt(cs::Math::Abs(app.Clock->Jd - savedJd) * 86400.0, "G3"), " s); ships ", savedShips, " -> ", app.Fleet()->Ships().Count()));
			Report(app, cs::Cat("smoke done in ", ms(), " ms"));
		}

		void LogAppUp(UnitySpaceApp& app)
		{
			Log(cs::Cat("UnitySpaceApp up: ", app.Galaxy->Systems.Count(), " systems, ", app.Catalog()->Count(), " bodies in ", app.ActiveSystem->Name, ", ",
				app.Fleet()->Ships().Count(), " ships (C++; screen ", Screen::width, " x ", Screen::height, ")"));
		}

		/// The smoke, a step a core tick: the menu scene's tour in its world, then (after New Game's map load) the game's.
		void SmokeTick(UWorld* world)
		{
			AUnitySpaceAppActor* a = AUnitySpaceAppActor::Find(world);
			if (!a || !a->HasActorBegunPlay()) return;
			if (run.phase == 0)
			{
				if (a->GetApp() == nullptr)
				{
					if (!UI::MainMenu::Showing() || !TakeHost(a)) return;
					MenuTour();
					run.phase = 1;
					if (run.pendingScreen.empty())
					{
						LogError("UI smoke: no game after New Game");
						Quit(world);
						run.phase = 3;
					}
					return;
				}
				run.phase = 1; // a flag skipped the menu
			}
			if (run.phase == 1)
			{
				UnitySpaceApp* app = a->GetApp();
				if (app == nullptr || !TakeHost(a)) return;
				run.phase = 2;
				LogAppUp(*app);
				try
				{
					GameTour(*app);
					AppSmoke(*app);
				}
				catch (const std::exception& e)
				{
					LogError(cs::Cat("SpaceGodSmoke failed: ", e.what()));
				}
				Log("SpaceGodSmoke: done, quitting");
				a->bExternallyStepped = false;
				a->bScriptedInput = false;
				Quit(world);
				run.phase = 3;
			}
		}

		// ---- the UI screenshots (UiSmoke.Shots), a frame at a time in a window

		cs::String ShotFolder()
		{
			cs::String dir = CommandLineValue(TEXT("-capturedir="));
			if (dir.empty()) dir = cs::String(TCHAR_TO_UTF8(*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"))));
			return dir;
		}

		void Shot(const cs::String& name)
		{
			cs::String path = cs::Cat(ShotFolder(), "/", name, ".png");
			CaptureBackend::Screenshot(path);
			Log(cs::Cat("UI shots: ", name, " -> ", path));
		}

		/// The screenshots' start, 2026-11-03 12:00 UTC, seed and game frames before the HUD shot: UiSmoke.Shots' (US-T45).
		constexpr double ShotsStartJd = 2461348.0;
		constexpr int ShotsSeed = 1;
		constexpr int ShotsGameFrames = 240;

		/// Logs the moment a shot shows, as UiSmoke.LogShotMoment, so the two tracks' runs can be checked to match.
		void LogShotMoment(UnitySpaceApp& app, const cs::String& name)
		{
			Log(cs::Cat("UI shots: ", name, " at frame ", Time::frameCount, ", JD ", cs::Fmt(app.Clock->Jd, "F6"), ", ",
				app.Fleet()->Ships().Count(), " ships"));
		}

		/// Each step waits its frames, then acts; a shot is written at the end of its frame, so the next step waits.
		void ShotsTick(UWorld* world)
		{
			AUnitySpaceAppActor* a = AUnitySpaceAppActor::Find(world);
			if (!a || !a->HasActorBegunPlay()) return;
			UnitySpaceApp* app = a->GetApp();
			run.waited++;
			auto after = [](int frames)
			{
				if (run.waited < frames) return false;
				run.waited = 0;
				run.phase++;
				return true;
			};
			switch (run.phase)
			{
				case 0:
					// US-T45: the same game on both tracks, pixel for pixel (UiSmoke.Shots): a fixed start, not now, a
					// seeded UnityEngine.Random and (tools/play.ps1) a fixed 1/60 s frame.
					UnitySpaceApp::StartJd = ShotsStartJd;
					if (app) { run.phase = 2; run.waited = 0; return; } // no menu (a flag skipped it)
					if (UI::MainMenu::Showing() && after(90)) Shot("cpp-ui-main-menu");
					return;
				case 1:
					if (after(5))
					{
						UnityRandom::InitState(ShotsSeed);
						GameLaunch::NewGame();
					}
					return;
				case 2:
					// The game's own frames from its first (the map load in between counts none): its clock is then the
					// same on both tracks.
					if (!app) { run.waited = 0; return; }
					if (run.gameStartFrame < 0) run.gameStartFrame = Time::frameCount;
					if (Time::frameCount - run.gameStartFrame >= ShotsGameFrames)
					{
						run.waited = 0;
						run.phase = 3;
						LogShotMoment(*app, "cpp-ui-hud");
						Shot("cpp-ui-hud");
					}
					return;
				case 3:
					if (after(5)) app->ShowHelp = true;
					return;
				case 4:
					if (after(10)) { LogShotMoment(*app, "cpp-ui-help"); Shot("cpp-ui-help"); }
					return;
				case 5:
					if (after(5)) { app->ShowHelp = false; app->ShowGalaxyMap = true; }
					return;
				case 6:
					if (after(30)) { LogShotMoment(*app, "cpp-ui-galaxy-map"); Shot("cpp-ui-galaxy-map"); }
					return;
				case 7:
					if (after(5)) { app->ShowGalaxyMap = false; app->ShowMarket = true; app->ShowCargo = true; }
					return;
				case 8:
					if (after(10)) { LogShotMoment(*app, "cpp-ui-market-cargo"); Shot("cpp-ui-market-cargo"); }
					return;
				case 9:
					if (after(5))
					{
						app->ShowMarket = false;
						app->ShowCargo = false;
						if (const auto& pause = SmokeAccess::PauseMenu(*app)) pause->Open();
					}
					return;
				case 10:
					if (after(10)) { LogShotMoment(*app, "cpp-ui-pause-menu"); Shot("cpp-ui-pause-menu"); }
					return;
				case 11:
					if (after(10))
					{
						if (const auto& pause = SmokeAccess::PauseMenu(*app)) pause->Close();
						Log(cs::Cat("UI shots: done, in ", ShotFolder()));
						Quit(world);
					}
					return;
				default:
					return;
			}
		}

		// ---------------------------------------------------------------- the views check (ViewsSmoke)

		Vector3 ToV3(const DVec3& d) { return Vector3(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z)); }

		class ViewsCheck
		{
		public:
			explicit ViewsCheck(const cs::Ref<Galaxy>& galaxy_) : galaxy(galaxy_)
			{
				sol = galaxy->Find("Sol System");
				if (!sol) sol = galaxy->Systems[0];
				scale.SetBlend(1);
			}

			cs::String Build(bool showOnScreen)
			{
				double t0 = FPlatformTime::Seconds();
				auto catalog = sol->Catalog;
				scene = cs::List<DVec3>();
				for (int i = 0; i < catalog->Count(); i++) scene.Add(DVec3());
				scale.Layout(sol->Ephemeris, scene);

				// The camera as UnitySpaceApp.Awake places it: nine display radii off Earth, looking at it.
				camera = Camera::main();
				camera->fieldOfView = 60.0f;
				camera->nearClipPlane = 0.01f;
				int earth = (*catalog)["Earth"]->Index;
				cameraScene = scene[earth] + DVec3(-0.6, 0.35, -1).Normalized() * (scale.DisplayRadius((*catalog)[earth]) * 9);
				DVec3 look = (scene[earth] - cameraScene).Normalized();
				camera->transform->SetRotation(Quaternion::Euler(static_cast<float>(-cs::Math::Asin(look.Y) * Mathf::Rad2Deg),
					static_cast<float>(cs::Math::Atan2(look.X, look.Z) * Mathf::Rad2Deg), 0));
				Rendering::RenderBackend::Active()->SetupCamera(camera, Rendering::CameraRole::Main);
				if (showOnScreen)
				{
					// Unreal's near plane is global (reversed-Z, no far plane): a thousandth of the way to Earth.
					camera->nearClipPlane = static_cast<float>(cs::Math::Max(1e-4, (scene[earth] - cameraScene).Length() * 1e-3));
				}

				root = NewGameObject("World")->transform;
				auto starfield = Views::Starfield::Create(root);
				sky = cs::New<Views::SkyView>(root, starfield);
				sky->Show(sol->Name);
				interstellar = cs::New<Views::InterstellarView>(galaxy, root);
				auto systemRoot = NewGameObject(sol->Name)->transform;
				systemRoot->SetParent(root, false);
				for (int i = 0; i < catalog->Count(); i++) bodies.Add(cs::New<Views::BodyView>((*catalog)[i], systemRoot));

				int kenney = 0, procedural = 0, failed = 0;
				for (const auto& ship : sol->World->Fleet->Ships())
				{
					if (ship->Destroyed) continue;
					try
					{
						auto view = cs::New<Views::ShipView>(ship, systemRoot);
						view->SetVisible(false);
						ships.Add(view);
						if (!Rendering::ShipModels::KenneyModelFor(ship).empty()) kenney++;
						else procedural++;
					}
					catch (const std::exception& e)
					{
						if (failed++ < 3) LogError(cs::Cat("ShipView ", ship->Name, ": ", e.what()));
					}
				}
				orbit = cs::New<Views::LineStrip>("Earth orbit", systemRoot);
				Place();
				UnityScene::Sync();
				auto skyId = sky->Current() ? sky->Current()->Id : cs::String("none");
				return cs::Cat("Views up in ", static_cast<long long>((FPlatformTime::Seconds() - t0) * 1000.0), " ms: ", bodies.Count(), " bodies, ", ships.Count(),
					" ship views (", kenney, " Kenney-class, ", procedural, " procedural, ", failed, " failed), starfield + sky (", skyId,
					") + interstellar (", galaxy->Systems.Count(), " stars) + 1 line; Unreal: ", UnityScene::Last().ToString());
			}

			/// One more frame (the host's SceneUpdate, between BeginFrame and Sync).
			void Frame(float dt)
			{
				sky->Update(dt);
				Place();
			}

		private:
			cs::Ref<Galaxy> galaxy;
			cs::Ref<StarSystem> sol;
			ScaleModel scale;
			cs::List<DVec3> scene;
			DVec3 cameraScene;
			cs::List<cs::Ref<Views::BodyView>> bodies;
			cs::List<cs::Ref<Views::ShipView>> ships;
			Camera* camera = nullptr;
			cs::Ref<Transform> root;
			cs::Ref<Views::SkyView> sky;
			cs::Ref<Views::InterstellarView> interstellar;
			cs::Ref<Views::LineStrip> orbit;

			Vector3 Relative(const DVec3& s) const { return ToV3(s - cameraScene); }

			/// Positions everything for this frame, as UnitySpaceApp.UpdateViews does.
			void Place()
			{
				auto catalog = sol->Catalog;
				auto eph = sol->Ephemeris;
				double jd = eph->Jd;
				Vector3 forward = camera->transform->rotation() * Vector3::forward();
				for (int i = 0; i < catalog->Count(); i++)
				{
					DVec3 rel = eph->Relative(i);
					DVec3 orbitNormal = DVec3::Cross(rel, eph->RelativeVelocityKmS(i));
					DVec3 north, meridian;
					BodyOrientation::Frame((*catalog)[i], jd, -rel, orbitNormal, north, meridian);
					float radius = (*catalog)[i]->IsVisible() ? static_cast<float>(scale.DisplayRadius((*catalog)[i])) : 1.0f;
					bodies[i]->Apply(Relative(scene[i]), radius, north, meridian, false, forward);
					bodies[i]->UpdateWeather(jd, 3600);
				}
				// The Sun lights the shaders (UnitySpaceApp.UpdateLights, Sol: the Sun alone).
				Rendering::StarLighting lighting;
				Vector3 sunAt = Relative(scene[0]);
				lighting.SunPosition = Vector4(sunAt.x, sunAt.y, sunAt.z, 0);
				lighting.SunColor = UnitySpaceApp::LightColor((*catalog)[0], 1);
				Rendering::RenderBackend::Active()->SetLighting(lighting);
				interstellar->Update(sol->PositionLy, camera->transform->rotation(), 0.0f);

				// Earth's orbit around the Sun: 256 samples of a circle at its scene distance, in the ecliptic.
				int earth = (*catalog)["Earth"]->Index;
				double r = (scene[earth] - scene[0]).Length();
				cs::List<Vector3> points;
				for (int k = 0; k < 257; k++)
				{
					double a = cs::Math::PI * 2 * k / 256;
					points.Add(sunAt + Vector3(static_cast<float>(cs::Math::Cos(a) * r), 0, static_cast<float>(cs::Math::Sin(a) * r)));
				}
				orbit->SetPoints(points, points.Count(), Color(0.4f, 0.6f, 1.0f, 0.5f));
			}
		};

		cs::Ref<ViewsCheck> views;
		cs::String viewShot;
		int viewFrames = 0;

		void RunViews(UWorld* world)
		{
			viewShot = CommandLineValue(TEXT("-SpaceGodShot="));
			FActorSpawnParameters params;
			params.Name = TEXT("SpaceGodViews");
			params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			params.bDeferConstruction = true;
			AUnitySpaceAppActor* h = world->SpawnActor<AUnitySpaceAppActor>(AUnitySpaceAppActor::StaticClass(), FTransform::Identity, params);
			if (!h)
			{
				LogError("SpaceGodViews failed: no scene host");
				Quit(world);
				return;
			}
			h->bSceneHost = true;
			h->bScriptedInput = true;
			h->SceneUpdate = [] { if (views) views->Frame(Time::deltaTime); };
			h->FinishSpawning(FTransform::Identity);
			// The views hold material instances and textures (strong UObject references): let go of them while the
			// object system is still up, not in static destruction after it.
			FCoreDelegates::OnEnginePreExit.AddLambda([] { views = nullptr; });
			try
			{
				// The C# track's fixed start (2026-11-03 12:00 UTC), as US-T6's smoke used.
				auto clock = cs::New<SimClock>(2461348.0);
				auto galaxy = Galaxy::CreateDefault(clock->Jd);
				views = cs::New<ViewsCheck>(galaxy);
				Log(cs::Cat("SpaceGodViews: ", views->Build(!viewShot.empty())));
			}
			catch (const std::exception& e)
			{
				LogError(cs::Cat("SpaceGodViews failed: ", e.what()));
				views = nullptr;
			}
			if (viewShot.empty()) Quit(world);
		}

		void ViewsTick(UWorld* world)
		{
			if (!views || viewShot.empty()) return;
			viewFrames++;
			if (viewFrames == 90)
			{
				FString file = ToF(viewShot).Replace(TEXT("\\"), TEXT("/"));
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(file), true);
				// The C# track's HighResShot 1600x900 (the window's size): the scene alone, no UI.
				FScreenshotRequest::RequestScreenshot(file, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
				Log(cs::Cat("SpaceGodViews: shot -> ", viewShot));
			}
			if (viewFrames == 150) Quit(world);
		}

		bool Tick(float)
		{
			UWorld* world = GameWorld();
			if (!world) return true;
			switch (run.mode)
			{
				case Run::Mode::Smoke: if (run.phase < 3) SmokeTick(world); break;
				case Run::Mode::Shots: ShotsTick(world); break;
				case Run::Mode::Views: ViewsTick(world); break;
				default: break;
			}
			return true;
		}
	}

	bool BeginScene(UWorld* world)
	{
		static bool started = false;
		if (!started)
		{
			started = true;
			if (Bootstrap::HasCommandLineFlag("-SpaceGodSmoke")) run.mode = Run::Mode::Smoke;
			else if (Bootstrap::HasCommandLineFlag("-SpaceGodUiShots")) run.mode = Run::Mode::Shots;
			else if (Bootstrap::HasCommandLineFlag("-SpaceGodViews") || !CommandLineValue(TEXT("-SpaceGodShot=")).empty()) run.mode = Run::Mode::Views;
			if (run.mode == Run::Mode::None) return false;

			if (run.mode != Run::Mode::Views)
			{
				// The run's profile (and its saves) in a scratch folder, never the player's own: a fresh one each run, so
				// the menu reads as a first start ("No saves yet"), as the C# track's screenshots do.
				FString root = FPaths::Combine(FPaths::ProjectSavedDir(), run.mode == Run::Mode::Smoke ? TEXT("SmokeProfiles") : TEXT("ShotProfiles"));
				root = FPaths::ConvertRelativePathToFull(root);
				IFileManager::Get().DeleteDirectory(*root, false, true);
				IFileManager::Get().MakeDirectory(*root, true);
				Shared::Profiles::UseRoot(cs::String(TCHAR_TO_UTF8(*root)));
			}
			run.startedSeconds = FPlatformTime::Seconds();
			run.ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), 0.0f);
			if (run.mode == Run::Mode::Views)
			{
				RunViews(world);
				return true;
			}
		}
		return run.mode == Run::Mode::Views;
	}
}
