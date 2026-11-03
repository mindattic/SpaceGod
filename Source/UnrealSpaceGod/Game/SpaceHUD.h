#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SpaceHUD.generated.h"

class ASolSystem;
class ASpaceGameState;
class ASpacePlayerController;

/** Body labels, teammate markers and pings, the date and warp, the selection's facts, the co-op log and the help screen. */
UCLASS()
class ASpaceHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	bool ToScreen(const FVector& World, FVector2D& Out) const;
	void DrawLabels(const ASolSystem* Sol, const ASpacePlayerController* PC);
	void DrawTeammates(const ASolSystem* Sol);
	void DrawPings(const ASolSystem* Sol, const ASpaceGameState* State);
	void DrawStatus(const ASolSystem* Sol, const ASpaceGameState* State, const ASpacePlayerController* PC);
	void DrawLog(const ASpaceGameState* State);
	void DrawHelp();
	void DrawCircle(const FVector2D& Center, float Radius, const FLinearColor& Color, float Thickness);
	float Line(const FString& Text, float X, float Y, const FLinearColor& Color, class UFont* Font = nullptr);
};
