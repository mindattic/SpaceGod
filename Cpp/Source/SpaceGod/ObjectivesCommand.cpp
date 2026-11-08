// US-T105: sg.Objectives, the SpaceGod's war objectives and the refugee tribes wars have left (Core's LastingConsequences).
// sg.Objectives lists the open objectives, the tribes still fleeing or plotting and the last plots; sg.Objectives accept <n>
// takes objective n up (siding with that side in its war), sg.Objectives decline <n> turns it down. How an accepted one is
// carried through is the SpaceGod's own affair: only who holds the settlement is judged. Single-player only for now: in
// co-op a choice must reach every peer through the session's commands, so it is refused there.

#include "UnitySpaceApp.h"

#include "Coop/CoopPeer.h"
#include "SpaceGodLog.h"
#include "UnitySpaceAppActor.h"

#include "SimClock.h"
#include "Systems/Galaxy.h"
#include "Systems/LastingConsequences.h"

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

namespace
{
	using namespace SpaceGod;

	UnitySpaceApp* ObjectivesApp()
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

	FString U(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

	void List(Core::LastingConsequences& lc)
	{
		auto open = lc.OpenObjectives();
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Objectives: %d open objective(s)"), open.Count());
		for (const auto& o : open)
			UE_LOG(LogSpaceGod, Display, TEXT("  %d [%s] %s"), o->Id, *U(Core::ToString(o->Status)), *U(Core::LastingConsequences::Describe(o)));
		auto tribes = lc.ActiveTribes();
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Objectives: %d refugee tribe(s) fleeing or plotting (%d in all)"), tribes.Count(), lc.Tribes.Count());
		for (const auto& t : tribes)
			UE_LOG(LogSpaceGod, Display, TEXT("  %s"), *U(Core::LastingConsequences::Describe(t)));
		int from = lc.Plots.Count() > 5 ? lc.Plots.Count() - 5 : 0;
		for (int i = from; i < lc.Plots.Count(); i++)
			UE_LOG(LogSpaceGod, Display, TEXT("  plot: %s"), *U(lc.Plots[i]->Outcome));
	}

	void Objectives(const TArray<FString>& args)
	{
		auto* app = ObjectivesApp();
		if (app == nullptr || app->Galaxy == nullptr || app->Clock == nullptr)
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Objectives: no game running"));
			return;
		}
		auto lc = Core::LastingConsequences::Of(*app->Galaxy);
		if (args.Num() == 0)
		{
			List(*lc);
			return;
		}
		if (args.Num() < 2 || !(args[0].Equals(TEXT("accept"), ESearchCase::IgnoreCase) || args[0].Equals(TEXT("decline"), ESearchCase::IgnoreCase)))
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Objectives: usage: sg.Objectives [accept <n> | decline <n>]"));
			return;
		}
		if (Coop::CoopPeer::Current() != nullptr)
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Objectives: objectives are taken up in single-player only for now"));
			return;
		}
		auto o = lc->ObjectiveById(FCString::Atoi(*args[1]));
		bool accept = args[0].Equals(TEXT("accept"), ESearchCase::IgnoreCase);
		cs::String why = accept ? lc->Accept(*app->Galaxy, o, app->Clock->Jd) : lc->Decline(o, app->Clock->Jd);
		if (!why.empty())
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("sg.Objectives: %s"), *U(why));
			return;
		}
		cs::String done = cs::Cat(accept ? "Objective taken up: " : "Objective declined: ", Core::LastingConsequences::Describe(o));
		UE_LOG(LogSpaceGod, Display, TEXT("sg.Objectives: %s"), *U(done));
		app->ShowToast(done);
	}

	FAutoConsoleCommand GObjectivesCommand(
		TEXT("sg.Objectives"),
		TEXT("US-T105: the SpaceGod's war objectives and the refugee tribes: sg.Objectives [accept <n> | decline <n>]."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args) { Objectives(args); }));
}
