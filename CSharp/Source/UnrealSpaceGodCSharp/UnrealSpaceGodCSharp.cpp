#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "RenderCore.h"

// The primary game module: the game itself is C# (see Script/ManagedUnrealSpaceGodCSharp).
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, UnrealSpaceGodCSharp, "UnrealSpaceGodCSharp");

namespace
{
	// US-T45: the engine's r.SetNearClipPlane clamps to 1 cm, and one Unity unit is one Unreal unit in this track, so
	// the original's dynamic near plane (nearest surface x 0.25: about 0.2 units beside Earth at the start) became 1 unit
	// and cut away Earth, its rings and every line near the camera. This sets the global near plane as given (the C++
	// track sets GNearClippingPlane directly), with the C++ track's floor of a ten-thousandth of a scene unit.
	void SetSpaceGodNearClipPlane(const TArray<FString>& Args)
	{
		if (Args.Num() == 0) return;
		const float Near = FMath::Max(FCString::Atof(*Args[0]), 1e-4f);
		SetNearClipPlaneGlobals(Near);
	}

	FAutoConsoleCommand GSpaceGodNearClipPlaneCmd(
		TEXT("sg.SetNearClipPlane"),
		TEXT("Sets the global near clipping plane in Unreal units, without r.SetNearClipPlane's 1-unit floor (US-T45)"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&SetSpaceGodNearClipPlane));
}
