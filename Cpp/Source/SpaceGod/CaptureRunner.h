#pragma once

// CaptureRunner: the C++ port of Unity.SpaceGod's Runtime/CaptureRunner.cs (US-T31).
// The MonoBehaviour is a plain class owned by whatever carries the app (Bootstrap adds it on -demo): the host calls
// Start() once, before the first frame's UnitySpaceApp.Update, then Update() every frame after the app's Update -- the
// point in Unity's frame where coroutines waiting on `yield return null` resume. Its coroutines (the C# Start is one,
// and it starts the tour as another) run on SceneTour::Runner; see SceneTour.h for how they are stepped.

#include "Cs/Cs.h"
#include "SceneTour.h"

namespace SpaceGod
{
	class UnitySpaceApp;

	/// Unity's ScreenCapture.CaptureScreenshot (here standing in for MindAttic.Capture's ScreenshotAsync), implemented
	/// on Unreal's screenshot request (CaptureBackend.cpp). As Unity's, the file is written at the end of the frame.
	namespace CaptureBackend
	{
		void Screenshot(const cs::String& path);
	}

	/// Started by <c>-demo</c> (see Bootstrap): runs the SceneTour
	/// once, then quits. Used to verify a build visually and to produce the README images. Add
	/// <c>-capture screen</c> (or <c>video</c>, or both) to save a screenshot at each stop through
	/// MindAttic.Capture — shots go to R:\MindAttic\Capture\Unity.SpaceGod\Unity.SpaceGod_{timestamp}.png,
	/// each sidecar tagged with its stop. Without <c>-capture</c> the tour still plays, just with no screenshots.
	///
	/// Unreal (US-T72): with <c>-capture</c> each stop is <c>ScreenshotAsync(&lt;stop&gt;)</c> through MindAttic.Capture's
	/// host (Capture/MindAtticCapture.h), into the session folder under R:\MindAttic\Capture\SpaceGod.Unreal.Cpp\, as in
	/// Unity. With tools/play.ps1's <c>-capturedir=&lt;dir&gt;</c> the stops stay Unreal screenshots through CaptureBackend,
	/// saved as <c>&lt;dir&gt;/&lt;stop&gt;.png</c>, with no capture host.
	class CaptureRunner final
	{
	public:
		/// `GetComponent<UnitySpaceApp>()`: the app on the same GameObject (it outlives this runner).
		explicit CaptureRunner(UnitySpaceApp* app);
		~CaptureRunner();
		CaptureRunner(const CaptureRunner&) = delete;
		CaptureRunner& operator=(const CaptureRunner&) = delete;

		/// `IEnumerator Start()`: starts the Start coroutine, running its first step now.
		void Start();

		/// C++ only: Unity's coroutine scheduler for this component, once a frame after the app's Update: the tour
		/// (started first) resumes, then the Start coroutine.
		void Update();

		/// Object.Destroy of the component: Unity stops its coroutines with it.
		void OnDestroy();

		/// C++ only: `app.GetComponent<CaptureRunner>()` -- the live runner, or null once there is none.
		static CaptureRunner* Current();

	private:
		UnitySpaceApp* hostApp;
		bool started = false;
		/// The Start coroutine, and the tour it starts (StartCoroutine(RunTour(...))).
		SceneTour::Runner startCoroutine, tour;

		SceneTour::Routine StartRoutine();

		static SceneTour::Routine RunTour(UnitySpaceApp& app, bool capturing, cs::Action<> finished);
		static SceneTour::Routine NoShot(cs::String name);
		static SceneTour::Routine Shot(cs::String name);

		static CaptureRunner*& CurrentSlot();
	};
}
