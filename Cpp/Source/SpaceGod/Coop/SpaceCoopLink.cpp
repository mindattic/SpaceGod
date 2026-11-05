// US-T44: SpaceCoopLink.cs (US-T39) in C++; see SpaceCoopLink.h.

#include "Coop/SpaceCoopLink.h"

#include "Coop/CoopSmoke.h"
#include "Bootstrap.h"
#include "GameLaunch.h"
#include "Smoke/ScriptedDevice.h"
#include "UnitySpaceApp.h"
#include "UnitySpaceAppActor.h"

#include "Shared/Profiles/Profiles.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

using SpaceGod::Coop::CoopPeer;
namespace CoopSmoke = SpaceGod::Coop::CoopSmoke;

namespace
{
	FString ToF(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }
	cs::String FromF(const FString& s) { return cs::String(TCHAR_TO_UTF8(*s)); }
}

ASpaceCoopLink::ASpaceCoopLink()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	bReplicates = true;
	bOnlyRelevantToOwner = true; // each client's link goes to that client only
	bAlwaysRelevant = false;
	SetNetUpdateFrequency(10.0f); // nothing replicates but the actor itself: everything is RPCs
}

// ---------------------------------------------------------------- the four RPCs

void ASpaceCoopLink::ClientMessage_Implementation(const FString& Message)
{
	CoopPeer* peer = CoopPeer::Current();
	if (peer != nullptr && peer->IsClient()) peer->ClientReceive(FromF(Message));
}

void ASpaceCoopLink::ClientPresence_Implementation(const FString& Message)
{
	CoopPeer* peer = CoopPeer::Current();
	if (peer != nullptr && peer->IsClient()) peer->ClientReceive(FromF(Message));
}

void ASpaceCoopLink::ServerMessage_Implementation(const FString& Message)
{
	CoopPeer* peer = CoopPeer::Current();
	if (peer != nullptr && peer->IsHost()) peer->HostReceive(this, FromF(Message));
}

void ASpaceCoopLink::ServerPresence_Implementation(const FString& Message)
{
	CoopPeer* peer = CoopPeer::Current();
	if (peer != nullptr && peer->IsHost()) peer->HostReceive(this, FromF(Message));
}

void ASpaceCoopLink::Send(const cs::String& message)
{
	if (HasAuthority()) ClientMessage(ToF(message));
	else ServerMessage(ToF(message));
}

void ASpaceCoopLink::SendUnreliable(const cs::String& message)
{
	if (HasAuthority()) ClientPresence(ToF(message));
	else ServerPresence(ToF(message));
}

// ---------------------------------------------------------------- the host's side

ASpaceCoopLink* ASpaceCoopLink::SpawnFor(APlayerController* Player)
{
	if (!Player || !Player->GetWorld()) return nullptr;
	FActorSpawnParameters params;
	params.Owner = Player; // the client may call its server RPCs, and it is relevant to that client only
	params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return Player->GetWorld()->SpawnActor<ASpaceCoopLink>(ASpaceCoopLink::StaticClass(), FTransform::Identity, params);
}

// ---------------------------------------------------------------- the client's side: the player loop

void ASpaceCoopLink::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority()) return;
	CoopPeer::StartClient(this, CoopPeer::NameFromCommandLine(CoopSmoke::Active() ? "Client" : ""));
	CoopPeer::Log("connected: link to the host is up");
}

void ASpaceCoopLink::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority()) return;
	CoopPeer* peer = CoopPeer::Current();
	if (peer == nullptr || !peer->IsClient()) return;
	CoopPeer::EngineTick++; // US-T43: the device's edges belong to the tick, however many host frames it replays
	CoopSmoke::ClientTick(*peer);
	UWorld* World = GetWorld();
	if (!bStarted)
	{
		if (!World || !World->GetFirstPlayerController()) return;
		bStarted = true;
		// A client's autosaves are of the host's world: a profile of their own, never the player's single-player saves.
		FString root = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
			CoopSmoke::Active() ? TEXT("SmokeProfiles") : TEXT("SpaceGod"), TEXT("CoopClient")));
		IFileManager::Get().MakeDirectory(*root, true);
		SpaceGod::Shared::Profiles::UseRoot(FromF(root));
		StartSeconds = FPlatformTime::Seconds();
		SpaceGod::Bootstrap::EnsureApp(World, SpaceGod::GameLaunch::GameScene);
		if (CoopSmoke::Active())
		{
			CoopSmoke::UseScript();
			CoopPeer::EdgesPerTick = false; // US-T43: the script clears its edges after every Update
			SpaceGod::UnityCompat::Application::SetTargetFrameRate(60);
		}
		CoopPeer::Log(cs::Cat("client app up in ", static_cast<long long>((FPlatformTime::Seconds() - StartSeconds) * 1000.0), " ms; asking the host for a seat as ",
			peer->LocalName()));
		return;
	}
	AUnitySpaceAppActor* Actor = AUnitySpaceAppActor::Find(World);
	SpaceGod::UnitySpaceApp* app = Actor ? Actor->GetApp() : nullptr;
	if (app == nullptr || peer->Refused()) return;
	SpaceGod::Smoke::ScriptedDevice* script = CoopSmoke::Script();
	Actor->bScriptedInput = script != nullptr;
	if (!bHelloSent)
	{
		bHelloSent = true;
		peer->ClientHello();
	}
	if (!peer->Running() && !peer->ClientTryLoad(app)) return; // waiting for the host's save
	for (int k = 0; k < MaxFramesPerTick; k++)
	{
		const SpaceGod::Coop::FrameRecord* next = peer->NextFrame();
		if (next == nullptr) break;
		// The frame as replayed (the queue's copy goes when it is done; the log lines after it still name it).
		const SpaceGod::Coop::FrameRecord rec = *next;
		peer->ClientRecord = &rec;
		peer->Consumed = false;
		if (script) script->Write();
		Actor->Step(rec.Dt);
		if (script) script->EndFrame();
		if (!peer->Consumed)
		{
			// The Update stopped before the clock (the pause menu is open here): the frame waits for the next one.
			peer->ClientRecord = nullptr;
			break;
		}
		app->CoopApplyPost(rec);
		peer->ClientFrameDone(app);
		CoopSmoke::ClientFrame(*peer, *app);
		peer->ClientRecord = nullptr;
		app = Actor->GetApp();
		if (app == nullptr || !peer->Running()) break;
	}
}

void ASpaceCoopLink::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (!HasAuthority())
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer != nullptr && peer->IsClient()) CoopPeer::Stop();
	}
	Super::EndPlay(EndPlayReason);
}
