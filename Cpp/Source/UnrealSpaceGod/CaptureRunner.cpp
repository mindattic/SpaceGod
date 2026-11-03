#include "CaptureRunner.h"

#include "Bootstrap.h"
#include "UnitySpaceApp.h"
#include "Camera/SpaceCamera.h"

#include "Misc/DateTime.h"
#include "Misc/Paths.h"

#include <memory>

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		// MindAttic.Capture's CaptureUnity, as far as the runner reads it.

		/// `CaptureUnity.Args != null && CaptureUnity.Args.Any`: -capture was asked for.
		bool CaptureRequested() { return Bootstrap::HasCommandLineFlag("-capture"); }

		/// `CaptureUnity.Engine.Enabled`: Unreal's screenshot needs nothing started first, so it is ready at once.
		bool CaptureEnabled() { return true; }

		/// `CaptureUnity.Engine.SessionFolder`: this run's folder of shots (Saved/Capture/<when the run first asked>).
		const cs::String& CaptureSessionFolder()
		{
			static const cs::String folder = []
			{
				FString dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Capture"), FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
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
			while (!CaptureEnabled() && wait < 15.0f)
			{
				wait += Time::unscaledDeltaTime;
				co_yield nullptr;
			}
			capturing = CaptureEnabled();
			if (!capturing) Debug::LogError("[Unity.SpaceGod] MindAttic.Capture did not start; the tour will not be captured.");
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
		// ScreenshotAsync(name): one shot of the frame, the UI in it, named for its stop.
		auto path = cs::Cat(CaptureSessionFolder(), "/", name, ".png");
		CaptureBackend::Screenshot(path);
		// while (!shot.IsCompleted) yield return null: the shot is written at the end of this frame.
		co_yield nullptr;
		Debug::Log(cs::Cat("[Unity.SpaceGod] Captured ", path));
	}
}
