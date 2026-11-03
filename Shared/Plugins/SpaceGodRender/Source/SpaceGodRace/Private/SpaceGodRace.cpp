// SpaceGodRace (US-T46, docs/RACE.md): the race's frame sampler, one implementation shared by both tracks so the C++ and
// the C# game are measured by the very same code. Each track's race driver (-SpaceGodRace: Cpp/.../Smoke/SpaceGodRace.cpp,
// CSharp/.../Runtime/Race/RaceRun.cs) names the phases and hands over its own per-frame logic times through console
// commands; the sampler times every engine frame itself (OnEndFrame) and writes one JSON file at the end.
//
//   sg.Race.Phase <name>                  start a phase (ends the one running)
//   sg.Race.End                           end the running phase
//   sg.Race.Frame <simMs> <appMs> <guiMs> the track's own times for the frame just run (simulation; the whole app frame; IMGUI)
//   sg.Race.Note <key> <value>            a number (or text) for the results, e.g. save/load times
//   sg.Race.NoteAge <key>                 the process's age now, in ms (startup moments)
//   sg.Race.Write <file>                  write the results (JSON)
//
// -SpaceGodRaceAuto=<file> (the co-op runs): one phase, "session", from the first frame to the engine's exit, written
// to <file> then; the tracks feed sg.Race.Frame every frame while it is on.
//
// Per frame: wall time between two frame ends, the engine's game / render / RHI thread times (GGameThreadTime, ...:
// computed in every build configuration, Shipping included, for dynamic resolution) and the GPU frame time
// (RHIGetGPUFrameCycles). Memory: the process's working set and its peak, sampled every 30 frames and at each phase end.
// Network: the game world's net driver totals (bytes and packets in and out since it was made), sampled likewise.

#include "CoreMinimal.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "RenderTimer.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <processthreadsapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace SpaceGodRace
{
	struct FPhase
	{
		FString Name;
		double StartedAt = 0;
		double Seconds = 0;
		TArray<double> Wall, Game, Render, Rhi, Gpu, Sim, App, Gui;
		uint64 WorkingSetEnd = 0, WorkingSetMax = 0;
	};

	struct FNet
	{
		FString Mode;
		uint64 InBytes = 0, OutBytes = 0, InPackets = 0, OutPackets = 0;
	};

	struct FState
	{
		TArray<FPhase> Phases;
		int32 Current = -1;
		double LastEnd = 0;
		uint64 Frames = 0;
		bool bFirstFrameNoted = false;
		TArray<TPair<FString, FString>> Notes;
		uint64 PeakWorkingSet = 0;
		FNet Net;
		FString AutoFile;
	};

	static FState& State()
	{
		static FState S;
		return S;
	}

	/// Seconds since this process was created (Windows), so startup includes everything before the module loaded.
	static double ProcessAgeSeconds()
	{
#if PLATFORM_WINDOWS
		FILETIME Created, Exited, Kernel, User, Now;
		if (::GetProcessTimes(::GetCurrentProcess(), &Created, &Exited, &Kernel, &User))
		{
			::GetSystemTimeAsFileTime(&Now);
			const uint64 C = (uint64(Created.dwHighDateTime) << 32) | Created.dwLowDateTime;
			const uint64 N = (uint64(Now.dwHighDateTime) << 32) | Now.dwLowDateTime;
			return double(N - C) / 1.0e7;
		}
#endif
		return -1;
	}

	static void Note(const FString& Key, const FString& Value)
	{
		for (auto& N : State().Notes)
			if (N.Key == Key) { N.Value = Value; return; }
		State().Notes.Emplace(Key, Value);
	}

	static void SampleMemory(FPhase* Phase)
	{
		const FPlatformMemoryStats M = FPlatformMemory::GetStats();
		FState& S = State();
		S.PeakWorkingSet = FMath::Max<uint64>(S.PeakWorkingSet, FMath::Max<uint64>(M.UsedPhysical, M.PeakUsedPhysical));
		if (Phase)
		{
			Phase->WorkingSetEnd = M.UsedPhysical;
			Phase->WorkingSetMax = FMath::Max<uint64>(Phase->WorkingSetMax, M.UsedPhysical);
		}
	}

	/// The game world's net driver totals (kept at their largest: a map load makes a new driver).
	static void SampleNet()
	{
		if (!GEngine) return;
		for (const FWorldContext& C : GEngine->GetWorldContexts())
		{
			UWorld* W = C.World();
			UNetDriver* D = W && W->IsGameWorld() ? W->GetNetDriver() : nullptr;
			if (!D) continue;
			FNet& N = State().Net;
			N.Mode = W->GetNetMode() == NM_ListenServer ? TEXT("host") : W->GetNetMode() == NM_Client ? TEXT("client") : TEXT("other");
			N.InBytes = FMath::Max<uint64>(N.InBytes, D->InTotalBytes);
			N.OutBytes = FMath::Max<uint64>(N.OutBytes, D->OutTotalBytes);
			N.InPackets = FMath::Max<uint64>(N.InPackets, D->InTotalPackets);
			N.OutPackets = FMath::Max<uint64>(N.OutPackets, D->OutTotalPackets);
		}
	}

	static void EndPhase()
	{
		FState& S = State();
		if (S.Current < 0) return;
		FPhase& P = S.Phases[S.Current];
		P.Seconds = FPlatformTime::Seconds() - P.StartedAt;
		SampleMemory(&P);
		UE_LOG(LogTemp, Display, TEXT("SpaceGodRace: phase %s ended: %d frames in %.2f s"), *P.Name, P.Wall.Num(), P.Seconds);
		S.Current = -1;
	}

	static void StartPhase(const FString& Name)
	{
		EndPhase();
		FState& S = State();
		FPhase& P = S.Phases.AddDefaulted_GetRef();
		P.Name = Name;
		P.StartedAt = FPlatformTime::Seconds();
		S.Current = S.Phases.Num() - 1;
		S.LastEnd = 0; // the first frame of a phase starts at its first frame end
		UE_LOG(LogTemp, Display, TEXT("SpaceGodRace: phase %s"), *P.Name);
	}

	static void OnEndFrame()
	{
		FState& S = State();
		const double Now = FPlatformTime::Seconds();
		if (!S.bFirstFrameNoted)
		{
			S.bFirstFrameNoted = true;
			Note(TEXT("processToFirstFrameMs"), FString::Printf(TEXT("%.1f"), ProcessAgeSeconds() * 1000.0));
			if (!S.AutoFile.IsEmpty()) StartPhase(TEXT("session"));
		}
		if (S.Current >= 0 && S.LastEnd > 0)
		{
			FPhase& P = S.Phases[S.Current];
			P.Wall.Add((Now - S.LastEnd) * 1000.0);
			P.Game.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
			P.Render.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
			P.Rhi.Add(FPlatformTime::ToMilliseconds(GRHIThreadTime));
			P.Gpu.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
			if ((++S.Frames % 30) == 0) { SampleMemory(&P); SampleNet(); }
		}
		S.LastEnd = Now;
	}

	static FString Stats(const TArray<double>& In)
	{
		if (In.Num() == 0) return TEXT("null");
		TArray<double> V = In;
		V.Sort();
		double Sum = 0;
		for (double X : V) Sum += X;
		// Nearest-rank percentiles, as the Bench's.
		auto Pct = [&V](double P) { const int32 I = FMath::Clamp(FMath::CeilToInt32(P * V.Num()) - 1, 0, V.Num() - 1); return V[I]; };
		return FString::Printf(TEXT("{\"n\":%d,\"mean\":%.4f,\"p50\":%.4f,\"p95\":%.4f,\"p99\":%.4f,\"max\":%.4f,\"sum\":%.2f}"),
			V.Num(), Sum / V.Num(), Pct(0.50), Pct(0.95), Pct(0.99), V.Last(), Sum);
	}

	static FString Quote(const FString& S)
	{
		return TEXT("\"") + S.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\"")) + TEXT("\"");
	}

	static void Write(const FString& File)
	{
		EndPhase();
		FState& S = State();
		SampleMemory(nullptr);
		SampleNet();
		FString J = TEXT("{\n  \"notes\": {");
		for (int32 i = 0; i < S.Notes.Num(); ++i)
		{
			const FString& V = S.Notes[i].Value;
			const bool bNumber = !V.IsEmpty() && V.IsNumeric();
			J += FString::Printf(TEXT("%s\n    %s: %s"), i ? TEXT(",") : TEXT(""), *Quote(S.Notes[i].Key), bNumber ? *V : *Quote(V));
		}
		J += FString::Printf(TEXT("\n  },\n  \"peakWorkingSetBytes\": %llu,\n  \"net\": {\"mode\": %s, \"inBytes\": %llu, \"outBytes\": %llu, \"inPackets\": %llu, \"outPackets\": %llu},\n  \"phases\": ["),
			S.PeakWorkingSet, *Quote(S.Net.Mode), S.Net.InBytes, S.Net.OutBytes, S.Net.InPackets, S.Net.OutPackets);
		for (int32 i = 0; i < S.Phases.Num(); ++i)
		{
			const FPhase& P = S.Phases[i];
			J += FString::Printf(TEXT("%s\n    {\"name\": %s, \"seconds\": %.3f, \"frames\": %d, \"workingSetEndBytes\": %llu, \"workingSetMaxBytes\": %llu,"
				"\n     \"wallMs\": %s,\n     \"gameThreadMs\": %s,\n     \"renderThreadMs\": %s,\n     \"rhiThreadMs\": %s,\n     \"gpuMs\": %s,"
				"\n     \"simMs\": %s,\n     \"appMs\": %s,\n     \"guiMs\": %s}"),
				i ? TEXT(",") : TEXT(""), *Quote(P.Name), P.Seconds, P.Wall.Num(), P.WorkingSetEnd, P.WorkingSetMax,
				*Stats(P.Wall), *Stats(P.Game), *Stats(P.Render), *Stats(P.Rhi), *Stats(P.Gpu), *Stats(P.Sim), *Stats(P.App), *Stats(P.Gui));
		}
		J += TEXT("\n  ]\n}\n");
		FFileHelper::SaveStringToFile(J, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogTemp, Display, TEXT("SpaceGodRace: results written to %s"), *File);
	}

	static FDelegateHandle EndFrameHandle;
	static void Listen()
	{
		if (!EndFrameHandle.IsValid()) EndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&OnEndFrame);
	}

	static FAutoConsoleCommand CmdPhase(TEXT("sg.Race.Phase"), TEXT("SpaceGodRace: <name> start a phase (US-T46)"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			Listen();
			StartPhase(Args.Num() ? Args[0] : TEXT("phase"));
		}));

	static FAutoConsoleCommand CmdEnd(TEXT("sg.Race.End"), TEXT("SpaceGodRace: end the running phase (US-T46)"),
		FConsoleCommandDelegate::CreateLambda([] { EndPhase(); }));

	static FAutoConsoleCommand CmdFrame(TEXT("sg.Race.Frame"), TEXT("SpaceGodRace: <simMs> <appMs> <guiMs> for the frame just run (US-T46)"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			FState& S = State();
			if (S.Current < 0) return;
			FPhase& P = S.Phases[S.Current];
			if (Args.Num() > 0) P.Sim.Add(FCString::Atod(*Args[0]));
			if (Args.Num() > 1 && FCString::Atod(*Args[1]) > 0) P.App.Add(FCString::Atod(*Args[1]));
			if (Args.Num() > 2 && FCString::Atod(*Args[2]) > 0) P.Gui.Add(FCString::Atod(*Args[2]));
		}));

	static FAutoConsoleCommand CmdNote(TEXT("sg.Race.Note"), TEXT("SpaceGodRace: <key> <value> (US-T46)"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			Listen();
			if (Args.Num() >= 2)
			{
				FString V = Args[1];
				for (int32 i = 2; i < Args.Num(); ++i) V += TEXT(" ") + Args[i];
				Note(Args[0], V);
			}
		}));

	static FAutoConsoleCommand CmdNoteAge(TEXT("sg.Race.NoteAge"), TEXT("SpaceGodRace: <key> the process's age in ms now (US-T46)"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num()) Note(Args[0], FString::Printf(TEXT("%.1f"), ProcessAgeSeconds() * 1000.0));
		}));

	static FAutoConsoleCommand CmdWrite(TEXT("sg.Race.Write"), TEXT("SpaceGodRace: <file> write the results (US-T46)"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			FString File;
			for (int32 i = 0; i < Args.Num(); ++i) File += (i ? TEXT(" ") : TEXT("")) + Args[i];
			File = File.TrimQuotes();
			Write(File.IsEmpty() ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("race.json")) : File);
		}));
}

class FSpaceGodRaceModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		using namespace SpaceGodRace;
		const TCHAR* Cmd = FCommandLine::Get();
		FString Auto;
		if (FParse::Value(Cmd, TEXT("-SpaceGodRaceAuto="), Auto))
		{
			State().AutoFile = Auto;
			FCoreDelegates::OnEnginePreExit.AddLambda([] { Write(State().AutoFile); });
		}
		// The first frame end is seen (processToFirstFrameMs) whenever a race runs.
		if (!State().AutoFile.IsEmpty() || FCString::Strifind(Cmd, TEXT("-SpaceGodRace"))) Listen();
	}
};

IMPLEMENT_MODULE(FSpaceGodRaceModule, SpaceGodRace)
