// US-T50: Unity's AudioListener.volume (EngineSettings.MasterVolume, 0 under the -muted CLI mute) on Unreal's main audio
// device, as the C++ track's UnitySpaceAppActor::ApplyAudio does (SetTransientPrimaryVolume): UnrealSharp exposes no
// setter for it, so the C# host (Runtime/Bridge/UnrealHost.cs) runs sg.SetPrimaryVolume whenever the volume changes.
// sg.AppliedPrimaryVolume reads back what the device took (-1: no audio device), for the audit's audio probes.
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "AudioDevice.h"
#include "AudioThread.h"

namespace
{
	TAutoConsoleVariable<float> CVarAppliedPrimaryVolume(
		TEXT("sg.AppliedPrimaryVolume"), -1.0f,
		TEXT("The main audio device's transient primary volume after the last sg.SetPrimaryVolume (-1: no device) (US-T50)"));

	void SetSpaceGodPrimaryVolume(const TArray<FString>& Args)
	{
		if (Args.Num() == 0) return;
		const float Volume = FMath::Clamp(FCString::Atof(*Args[0]), 0.0f, 1.0f);
		float Applied = -1.0f;
		if (GEngine)
			if (FAudioDeviceHandle Device = GEngine->GetMainAudioDevice())
			{
				Device->SetTransientPrimaryVolume(Volume);
				// The device keeps it on the audio thread: read it there, after the set queued just before.
				FAudioDevice* D = Device.GetAudioDevice();
				FAudioThread::RunCommandOnAudioThread([D, &Applied] { Applied = D->GetTransientPrimaryVolume(); });
				FAudioCommandFence Fence;
				Fence.BeginFence();
				Fence.Wait();
			}
		CVarAppliedPrimaryVolume->Set(Applied, ECVF_SetByCode);
	}

	FAutoConsoleCommand GSpaceGodPrimaryVolumeCmd(
		TEXT("sg.SetPrimaryVolume"),
		TEXT("Sets the main audio device's transient primary volume, 0-1 (Unity's AudioListener.volume) (US-T50)"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SetSpaceGodPrimaryVolume));
}
