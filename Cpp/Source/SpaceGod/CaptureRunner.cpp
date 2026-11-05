#include "CaptureRunner.h"

#include "Bootstrap.h"
#include "UnitySpaceApp.h"
#include "Camera/SpaceCamera.h"
#include "Capture/MindAtticCapture.h"

#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

#include <memory>

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		// MindAttic.Capture's CaptureUnity, as far as the runner reads it (US-T72: Capture/MindAtticCapture.h).
		// -capturedir=<dir> (tools/play.ps1 -Mode demo, US-T58) keeps the tour's shots as Unreal screenshots in that
		// folder, with no capture host: a tooling switch of this repo's, not one of the game's.

		bool LegacyShots()
		{
			static const bool legacy = []
			{
				FString dir;
				return FParse::Value(FCommandLine::Get(), TEXT("-capturedir="), dir) && !dir.IsEmpty();
			}();
			return legacy;
		}

		/// `CaptureUnity.Args != null && CaptureUnity.Args.Any`: -capture was asked for (the host says whether as a mode,
		/// so a run that asked waits for it, as Unity's runner waits for the engine).
		bool CaptureRequested() { return LegacyShots() ? Bootstrap::HasCommandLineFlag("-capture") : MindAtticCapture::Requested(); }

		/// `CaptureUnity.Engine.Enabled`.
		bool CaptureEnabled() { return LegacyShots() || (MindAtticCapture::Ready() && MindAtticCapture::Any() && MindAtticCapture::Enabled()); }

		/// The host has answered (or there is none to wait for).
		bool CaptureSettled() { return LegacyShots() || MindAtticCapture::Ready(); }

		/// `CaptureUnity.Engine.SessionFolder`; for the -capturedir= shots, that folder.
		const cs::String& CaptureSessionFolder()
		{
			if (!LegacyShots())
			{
				static cs::String session;
				session = cs::String(TCHAR_TO_UTF8(*MindAtticCapture::SessionFolder()));
				return session;
			}
			static const cs::String folder = []
			{
				// US-T58: -capturedir=<dir> (tools/play.ps1 -Mode demo) puts the shots there, as the C# track's runner does.
				FString dir;
				if (FParse::Value(FCommandLine::Get(), TEXT("-capturedir="), dir) && !dir.IsEmpty())
					return cs::String(TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(dir)));
				dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Capture"), FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
				return cs::String(TCHAR_TO_UTF8(*FPaths::ConvertRelativePathToFull(dir)));
			}();
			return folder;
		}
	}

	CaptureRunner*& CaptureRunner::CurrentSlot()
	{
		static CaptureRunner* current = nullptr;
		return current;
	}

	CaptureRunner* CaptureRunner::Current() { return CurrentSlot(); }

	CaptureRunner::CaptureRunner(UnitySpaceApp* inApp) : hostApp(inApp)
	{
		CurrentSlot() = this;
	}

	CaptureRunner::~CaptureRunner()
	{
		if (CurrentSlot() == this) CurrentSlot() = nullptr;
	}

	void CaptureRunner::Start()
	{
		if (started) return;
		started = true;
		startCoroutine = SceneTour::Runner(StartRoutine());
		startCoroutine.MoveNext();
	}

	void CaptureRunner::Update()
	{
		if (!started) return;
		if (tour.Running()) tour.MoveNext();
		if (startCoroutine.Running()) startCoroutine.MoveNext();
	}

	void CaptureRunner::OnDestroy()
	{
		tour.Stop();
		startCoroutine.Stop();
		if (CurrentSlot() == this) CurrentSlot() = nullptr;
	}

	SceneTour::Routine CaptureRunner::StartRoutine()
	{
		UnitySpaceApp& app = *hostApp; // GetComponent<UnitySpaceApp>()
		auto capturing = CaptureRequested();
		if (capturing)
		{
			auto wait = 0.0f;
			// Here the host answers first (Unity knows Args.Any at once, then waits on Enabled).
			while (!CaptureSettled() && wait < 15.0f)
			{
				wait += Time::unscaledDeltaTime;
				co_yield nullptr;
			}
			// -capture with no mode after it (Args.Any false): not capturing, and nothing to complain about.
			if (CaptureSettled() && !LegacyShots() && !MindAtticCapture::Any()) capturing = false;
			else
			{
				while (!CaptureEnabled() && wait < 15.0f)
				{
					wait += Time::unscaledDeltaTime;
					co_yield nullptr;
				}
				capturing = CaptureEnabled();
				if (!capturing) Debug::LogError("[Unity.SpaceGod] MindAttic.Capture did not start; the tour will not be captured.");
			}
		}

		// A time reset rebuilds the galaxy under the tour (its ships, bodies and battles are gone):
		// stop there rather than run on, framing and waiting on objects that no longer exist.
		// (Held by the handlers as C#'s closures hold them.)
		auto done = std::make_shared<bool>(false);
		auto rebuilt = std::make_shared<bool>(false);
		int onRebuilt = app.GalaxyRebuilt += [rebuilt] { *rebuilt = true; };
		// StartCoroutine: the tour's first step runs now.
		tour = SceneTour::Runner(RunTour(app, capturing, [done] { *done = true; }));
		tour.MoveNext();
		while (!*done && !*rebuilt) co_yield nullptr;
		app.GalaxyRebuilt -= onRebuilt;
		if (!*done)
		{
			tour.Stop();
			// A stop cut off mid-way never lets go of the camera itself (a dogfight's chase lock on a hull nobody flies).
			if (!app.IsPiloting()) app.SpaceCamera->ExitFocus();
			Debug::LogWarning("[Unity.SpaceGod] The galaxy was rebuilt mid-tour; the tour stops here.");
		}

		if (capturing) Debug::Log(cs::Cat("[Unity.SpaceGod] Tour captured to ", CaptureSessionFolder()));
		Application::Quit();
	}

	SceneTour::Routine CaptureRunner::RunTour(UnitySpaceApp& app, bool capturing, cs::Action<> finished)
	{
		co_yield SceneTour::Run(app, capturing ? SceneTour::StopFunc(&CaptureRunner::Shot) : SceneTour::StopFunc(&CaptureRunner::NoShot));
		finished();
	}

	SceneTour::Routine CaptureRunner::NoShot(cs::String name)
	{
		(void)name;
		co_return;
	}

	SceneTour::Routine CaptureRunner::Shot(cs::String name)
	{
		if (LegacyShots())
		{
			// -capturedir=: one Unreal shot of the frame, the UI in it, named for its stop.
			auto path = cs::Cat(CaptureSessionFolder(), "/", name, ".png");
			CaptureBackend::Screenshot(path);
			// The shot is written at the end of this frame.
			co_yield nullptr;
			Debug::Log(cs::Cat("[Unity.SpaceGod] Captured ", path));
			co_return;
		}
		// var shot = CaptureUnity.Engine.ScreenshotAsync(name);
		const int32 shot = MindAtticCapture::Screenshot(FString(UTF8_TO_TCHAR(name.c_str())));
		// while (!shot.IsCompleted) yield return null;
		bool ok = false;
		FString detail;
		while (!MindAtticCapture::ScreenshotResult(shot, ok, detail)) co_yield nullptr;
		if (ok) Debug::Log(cs::Cat("[Unity.SpaceGod] Captured ", cs::String(TCHAR_TO_UTF8(*detail))));
		else Debug::LogError(cs::Cat("[Unity.SpaceGod] Screenshot ", name, " failed: ", cs::String(TCHAR_TO_UTF8(*detail))));
	}
}
