#include "UnitySpaceAppActor.h"
#include "UnitySpaceApp.h"
#include "SessionAudio.h"
#include "UnrealSpace.h"
#include "UnrealSpaceGodLog.h"
#include "Input/UnrealInputDevice.h"

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
	bReplicates = false; // each machine runs its own app; ASpaceGameState carries what is shared

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

	App = std::make_shared<UnitySpaceApp>();
	App->Awake();
	ApplyCamera();
	ApplyCursor();
	ApplyAudio();
}

void AUnitySpaceAppActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (App)
	{
		if (EndPlayReason == EEndPlayReason::Quit || EndPlayReason == EEndPlayReason::EndPlayInEditor) App->OnApplicationQuit();
		App->OnDestroy();
		App.reset();
	}
	Super::EndPlay(EndPlayReason);
}

void AUnitySpaceAppActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!App) return;

	// Unity's unscaled delta: real seconds, not dilated (the app clamps it to 0.1 s itself).
	const float RealDelta = static_cast<float>(FApp::GetDeltaTime());
	RefreshUnityStatics(RealDelta);

	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->GetViewTarget() != this) PC->SetViewTarget(this);
		Input::UnrealInputDevice::Feed(PC, RealDelta);
	}

	if (App->enabled) App->Update();
	ApplyCamera();
	ApplyCursor();
	ApplyAudio();
}

void AUnitySpaceAppActor::DrawGui(UCanvas* Canvas)
{
	if (!App || !App->enabled) return;
	CurrentCanvas = Canvas;
	App->OnGUI();
	CurrentCanvas = nullptr;
}

void AUnitySpaceAppActor::RefreshUnityStatics(float RealDeltaSeconds)
{
	Time::unscaledDeltaTime = RealDeltaSeconds;
	Time::deltaTime = RealDeltaSeconds;
	Time::realtimeSinceStartupAsDouble = FPlatformTime::Seconds() - StartSeconds;
	Time::unscaledTime = static_cast<float>(Time::realtimeSinceStartupAsDouble);
	Time::time = Time::unscaledTime;
	Time::frameCount = static_cast<int>(GFrameCounter);

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
	Camera* Cam = Camera::main();
	if (!Cam || !MainCamera) return;
	// The app keeps the camera at the origin and rotates it (floating origin); everything else is placed at
	// scene - camera, so the camera component only ever turns. Unity's fov is vertical, Unreal's horizontal.
	MainCamera->SetWorldLocationAndRotation(UnrealSpace::ToUnreal(Cam->transform->position()), UnrealSpace::ToUnreal(Cam->transform->rotation()));
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
