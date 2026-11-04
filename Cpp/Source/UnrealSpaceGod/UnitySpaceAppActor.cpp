#include "UnitySpaceAppActor.h"
#include "UnitySpaceApp.h"
#include "SessionAudio.h"
#include "UnrealSpace.h"
#include "UnrealSpaceGodLog.h"
#include "Input/UnrealInputDevice.h"
#include "UnityCompat/GuiHost.h"
#include "UnityCompat/UnityScene.h"
#include "CaptureRunner.h"
#include "Views/AsteroidBelt.h"
#include "Coop/CoopPeer.h"
#include "Smoke/SpaceGodRace.h"

#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AudioDevice.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"

using namespace SpaceGod;
using namespace SpaceGod::UnityCompat;

UCanvas* AUnitySpaceAppActor::CurrentCanvas = nullptr;

AUnitySpaceAppActor::AUnitySpaceAppActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	PrimaryActorTick.bTickEvenWhenPaused = true; // the app keeps its own pause (Esc menu, SimClock)
	bReplicates = false; // each machine runs its own app; co-op's lockstep (Coop/CoopPeer, ASpaceCoopLink) keeps them the same

	MainCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("Main Camera"));
	RootComponent = MainCamera;
	MainCamera->bConstrainAspectRatio = false;
}

AUnitySpaceAppActor* AUnitySpaceAppActor::Find(const UWorld* World)
{
	if (!World) return nullptr;
	for (TActorIterator<AUnitySpaceAppActor> It(const_cast<UWorld*>(World)); It; ++It) return *It;
	return nullptr;
}

void AUnitySpaceAppActor::BeginPlay()
{
	Super::BeginPlay();
	// Floating origin: the camera never leaves the world origin, so the actor carrying it stays there.
	SetActorLocationAndRotation(FVector::ZeroVector, FQuat::Identity);

	StartSeconds = FPlatformTime::Seconds();
	SessionAudio::OnLoad(); // [RuntimeInitializeOnLoadMethod(BeforeSceneLoad)]
	RefreshUnityStatics(0);

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->SetViewTarget(this);
		// The controller processes this frame's input first, so Feed sees this frame's keys (no one-frame lag).
		AddTickPrerequisiteActor(PC);
	}

	// The Unity scene's renderers draw as components of this actor (US-T30); attached before Awake builds the views.
	UnityScene::Attach(this);
	if (bSceneHost)
	{
		// A scene without the app (MainMenu.unity: its camera and starfield; the menu itself is Bootstrap's).
		ApplyCamera();
		ApplyCursor();
		return;
	}
	App = std::make_shared<UnitySpaceApp>();
	App->Awake();
	// OnGUI runs in the GUI host's passes (Layout, each input event, Repaint), as Unity calls it (US-T31).
	{
		UnitySpaceApp* AppPtr = App.get();
		GuiHost::Register(AppPtr, [AppPtr] { if (AppPtr->enabled) AppPtr->OnGUI(); });
		// The asteroid belt is a second MonoBehaviour on the app's GameObject in Unity, added after it: its OnGUI (the
		// named asteroids' labels) runs after the app's in every pass.
		GuiHost::Register(&AppPtr->AsteroidBelt, [AppPtr]
		{
			if (AppPtr->AsteroidBelt && AppPtr->AsteroidBelt->enabled) AppPtr->AsteroidBelt->OnGUI();
		}, 1);
		// US-T44: co-op's HUD after both (the C# track's CoopHud MonoBehaviour); draws nothing outside co-op.
		GuiHost::Register(&AppPtr->Clock, [AppPtr] { if (AppPtr->enabled) AppPtr->CoopGui(); }, 2);
	}
	ApplyCamera();
	ApplyCursor();
	ApplyAudio();
}

void AUnitySpaceAppActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (App)
	{
		if (Capture)
		{
			Capture->OnDestroy();
			Capture.reset();
		}
		GuiHost::Unregister(App.get());
		GuiHost::Unregister(&App->AsteroidBelt);
		GuiHost::Unregister(&App->Clock); // US-T44
		if (EndPlayReason == EEndPlayReason::Quit || EndPlayReason == EEndPlayReason::EndPlayInEditor) App->OnApplicationQuit();
		App->OnDestroy();
		App.reset();
	}
	UnityScene::Detach();
	Super::EndPlay(EndPlayReason);
}

void AUnitySpaceAppActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if ((!App && !bSceneHost) || bExternallyStepped) return;
	// US-T44: a co-op client replays the host's frames: its link actor (ASpaceCoopLink) steps the app, one Step per frame.
	if (GetNetMode() == NM_Client) return;
	// Unity's unscaled delta: real seconds, not dilated (the app clamps it to 0.1 s itself).
	Step(static_cast<float>(FApp::GetDeltaTime()));
}

void AUnitySpaceAppActor::Step(float RealDelta)
{
	if (!App && !bSceneHost) return;
	const double raceT0 = Race::Probe::Now(); // US-T46: the whole Unity frame, for the race (docs/RACE.md)
	RefreshUnityStatics(RealDelta);
	// Unity's player loop: finished loads and particles first (UnityScene's BeginFrame), then every Update.
	UnityScene::BeginFrame(Time::deltaTime);

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->GetViewTarget() != this) PC->SetViewTarget(this);
		if (!bScriptedInput) Input::UnrealInputDevice::Feed(PC, RealDelta);
	}
	if (!App)
	{
		// A scene without the app: its own Update (the main menu's; its OnGUI runs in the GUI host's passes).
		if (SceneUpdate) SceneUpdate();
		UnityScene::Sync();
		ApplyCamera();
		ApplyCursor();
		ApplyAudio();
		return;
	}

	// A component added this frame starts before the next Updates (Unity: Start before the first Update).
	if (Capture && !CaptureStarted)
	{
		CaptureStarted = true;
		Capture->Start();
	}
	if (App->enabled) App->Update();
	// US-T44: the host's frame to its clients (the C# track's game mode Step: CoopPeer.HostEndUpdate after the Update).
	if (Coop::CoopPeer* Peer = Coop::CoopPeer::Current(); Peer && Peer->IsHost()) Peer->HostEndUpdate(App.get());
	if (Capture) Capture->Update();
	// End of the frame (Unity's render): every renderer the views placed to its Unreal component.
	UnityScene::Sync();
	ApplyCamera();
	ApplyCursor();
	ApplyAudio();
	Race::Probe::AppMs = Race::Probe::Now() - raceT0; // US-T46
}

void AUnitySpaceAppActor::AddCaptureRunner()
{
	if (Capture || !App) return;
	Capture = std::make_shared<CaptureRunner>(App.get());
	CaptureStarted = false;
}

void AUnitySpaceAppActor::DrawGui(UCanvas* Canvas)
{
	// OnGUI is the GUI host's (registered in BeginPlay): it draws on its own Slate overlay, with Unity's event passes.
	(void)Canvas;
}

void AUnitySpaceAppActor::RefreshUnityStatics(float RealDeltaSeconds)
{
	// As the C# track's UnityBridge.Tick: the unscaled clock is real time, Time.deltaTime and Time.time are scaled by
	// Time.timeScale (the pause menu sets it to 0), and every app frame counts one.
	Time::unscaledDeltaTime = RealDeltaSeconds;
	Time::unscaledTime += RealDeltaSeconds;
	Time::deltaTime = RealDeltaSeconds * Time::timeScale;
	Time::time += Time::deltaTime;
	Time::realtimeSinceStartupAsDouble = FPlatformTime::Seconds() - StartSeconds;
	if (RealDeltaSeconds > 0) Time::frameCount++;

	if (GEngine && GEngine->GameViewport)
	{
		FVector2D Size;
		GEngine->GameViewport->GetViewportSize(Size);
		if (Size.X > 0 && Size.Y > 0)
		{
			Screen::width = static_cast<int>(Size.X);
			Screen::height = static_cast<int>(Size.Y);
		}
	}
}

void AUnitySpaceAppActor::ApplyCamera()
{
	// US-T53: the screen's camera as Unity picks it (the model viewer's over the game's), not only Camera.main.
	Camera* Cam = UnityScene::ScreenCamera();
	if (!Cam || !MainCamera) return;
	// The app keeps the camera at the origin and rotates it (floating origin); everything else is placed at
	// scene - camera, so the camera component only ever turns. Unity's fov is vertical, Unreal's horizontal.
	// A Unity camera looks down its +Z, which UnrealSpace maps to Unreal's -Y; an Unreal camera looks down +X. So the
	// camera's Unreal rotation is the converted one after a quarter turn (yaw -90) taking +X onto -Y, roof +Z on +Z.
	static const FQuat LookAlongUnityForward(FVector::UpVector, FMath::DegreesToRadians(-90.0));
	MainCamera->SetWorldLocationAndRotation(UnrealSpace::ToUnreal(Cam->transform->position()),
		UnrealSpace::ToUnreal(Cam->transform->rotation()) * LookAlongUnityForward);
	MainCamera->SetFieldOfView(UnrealSpace::HorizontalFov(Cam->fieldOfView, Cam->aspect()));
	// Unreal draws with an infinite reverse-Z far plane: Unity's farClipPlane has nothing to set. The near plane is
	// global (GNearClippingPlane, cm).
	GNearClippingPlane = FMath::Max(UnrealSpace::ClipToUnreal(Cam->nearClipPlane), 0.01f);
}

void AUnitySpaceAppActor::ApplyCursor()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return;
	const bool bLocked = Cursor::lockState == CursorLockMode::Locked;
	if (PC->bShowMouseCursor != Cursor::visible) PC->bShowMouseCursor = Cursor::visible;
	static int LastMode = -1;
	const int Mode = bLocked ? 1 : 0;
	if (Mode == LastMode) return;
	LastMode = Mode;
	if (bLocked)
	{
		FInputModeGameOnly InputMode;
		PC->SetInputMode(InputMode);
	}
	else
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(Cursor::lockState == CursorLockMode::Confined ? EMouseLockMode::LockAlways : EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);
	}
}

void AUnitySpaceAppActor::ApplyAudio()
{
	if (AudioListener::volume == LastAppliedVolume) return;
	LastAppliedVolume = AudioListener::volume;
	if (GEngine)
		if (FAudioDeviceHandle Device = GEngine->GetMainAudioDevice())
			Device->SetTransientPrimaryVolume(AudioListener::volume);
}
