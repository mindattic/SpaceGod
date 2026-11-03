#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include <functional>
#include <memory>
#include "UnitySpaceAppActor.generated.h"

class UCameraComponent;
class UCanvas;

namespace SpaceGod { class UnitySpaceApp; class CaptureRunner; }

/**
 * The GameObject "Unity.SpaceGod" that carries UnitySpaceApp: owns the app (a plain C++ class, the MonoBehaviour's
 * logic) and calls its Unity messages in Unity's order -- Awake at BeginPlay; each frame Time/Screen/input are
 * refreshed, then Update; OnGUI from ASpaceHUD::DrawHUD; OnApplicationQuit and OnDestroy at EndPlay -- and applies
 * what the app set on Unity's statics back to Unreal: the main camera (pose, vertical fov, near clip) through
 * UnrealSpace, the cursor, and the listener volume.
 *
 * Its tick group is PrePhysics with a high priority, standing in for [DefaultExecutionOrder(-100)]: the app runs
 * before the views and HUD read it.
 *
 * Co-op: every machine that draws runs its own app; the host's is authoritative (ASpaceGameState mirrors its clock
 * to clients), and player actions go through the app's Core::LocalSession, which the co-op ticket networks.
 */
UCLASS()
class AUnitySpaceAppActor : public AActor
{
	GENERATED_BODY()

public:
	AUnitySpaceAppActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/// The app, once awake (null before BeginPlay and after EndPlay).
	SpaceGod::UnitySpaceApp* GetApp() const { return App.get(); }

	/// One frame of Unity's player loop for the app (Time, input, Update, the scene to Unreal, camera/cursor/audio out).
	/// Tick runs it with the real delta; a scripted run (the smoke) sets bExternallyStepped and calls it itself.
	void Step(float RealDelta);
	bool bExternallyStepped = false;
	/// The scripted run writes Input::InputDevice::current() itself (the C# track's ScriptedInputDevice): no Feed.
	bool bScriptedInput = false;

	/// A scene without the app (set before BeginPlay): the main menu scene (Bootstrap), the views check. Each frame
	/// runs Unity's loop for that scene instead -- Time, SceneUpdate (the menu's Update; the views placed), the scene's
	/// renderers to Unreal, the camera.
	bool bSceneHost = false;
	std::function<void()> SceneUpdate;

	/// Called by ASpaceHUD::DrawHUD: Unity's OnGUI pass, drawn on that canvas.
	void DrawGui(UCanvas* Canvas);

	/// The canvas OnGUI is drawing on right now (null outside DrawGui): the UI ticket's IMGUI seam draws through it.
	static UCanvas* CurrentCanvas;

	/// The one app in the world, if any.
	static AUnitySpaceAppActor* Find(const UWorld* World);

private:
	void RefreshUnityStatics(float RealDeltaSeconds);
	void ApplyCamera();
	void ApplyCursor();
	void ApplyAudio();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> MainCamera;

	std::shared_ptr<SpaceGod::UnitySpaceApp> App;
	/// -demo: the scripted tour (Unity: a CaptureRunner component Bootstrap adds to the app's GameObject, US-T31).
	std::shared_ptr<SpaceGod::CaptureRunner> Capture;
	bool CaptureStarted = false;

public:
	/// Bootstrap on -demo: adds the tour runner (once); its Start runs before the app's next Update.
	void AddCaptureRunner();
private:
	double StartSeconds = 0;
	float LastAppliedVolume = -1;
};
