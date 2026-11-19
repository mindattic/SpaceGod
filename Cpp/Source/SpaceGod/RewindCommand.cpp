// US-T99: sg.Rewind, time reversal's keyframe ring from the console (docs/REWIND.md, phase 2). sg.Rewind (or sg.Rewind
// list) logs every keyframe kept, its age, size and capture time; sg.Rewind back <game seconds> loads the newest keyframe
// at least that far back (else the session start); sg.Rewind to <id> loads that keyframe. Both act at the start of the
// next frame, as Shift+F8 does.

#include "UnitySpaceApp.h"

#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

namespace
{
	using namespace SpaceGod;

	UnitySpaceApp* RewindApp()
	{
		if (!GEngine) return nullptr;
		for (const FWorldContext& c : GEngine->GetWorldContexts())
		{
			if (c.WorldType != EWorldType::Game && c.WorldType != EWorldType::PIE) continue;
			auto* actor = AUnitySpaceAppActor::Find(c.World());
			if (actor != nullptr && actor->GetApp() != nullptr) return actor->GetApp();
		}
		return nullptr;
	}

	void RewindCommand(const TArray<FString>& args)
	{
		auto* app = RewindApp();
		if (app == nullptr || app->Galaxy == nullptr)
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Rewind: no game running"));
			return;
		}
		if (args.Num() == 0 || args[0].Equals(TEXT("list"), ESearchCase::IgnoreCase))
		{
			TArray<FString> lines;
			FString(UTF8_TO_TCHAR(app->DescribeRewind().c_str())).ParseIntoArrayLines(lines, false);
			for (const FString& line : lines) UE_LOG(LogSpaceGod, Display, TEXT("%s"), *line);
			return;
		}
		if (args.Num() >= 2 && args[0].Equals(TEXT("back"), ESearchCase::IgnoreCase))
		{
			double backS = FCString::Atod(*args[1]);
			if (backS < 0) backS = 0;
			app->RequestRewindBack(backS);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Rewind: rewinding %.0f game seconds (to the keyframe at or before)"), backS);
			return;
		}
		if (args.Num() >= 2 && args[0].Equals(TEXT("to"), ESearchCase::IgnoreCase))
		{
			int id = FCString::Atoi(*args[1]);
			app->RequestRewindTo(id);
			UE_LOG(LogSpaceGod, Display, TEXT("sg.Rewind: rewinding to keyframe #%d"), id);
			return;
		}
		UE_LOG(LogSpaceGod, Warning, TEXT("sg.Rewind: usage: sg.Rewind [list | back <game seconds> | to <keyframe id>]"));
	}

	FAutoConsoleCommand GRewindCommand(
		TEXT("sg.Rewind"),
		TEXT("US-T99: time reversal's keyframe ring: sg.Rewind [list | back <game seconds> | to <keyframe id>]."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { RewindCommand(args); }));
}
