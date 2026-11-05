#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SpaceHUD.generated.h"

/**
 * Unity's OnGUI pass: every frame DrawHUD hands its canvas to the app (AUnitySpaceAppActor::DrawGui), whose OnGUI
 * draws the HUD, menus and screens (UI ticket: Hud, PauseMenu, HelpScreen, GalaxyMapScreen... draw through
 * AUnitySpaceAppActor::CurrentCanvas), and the main menu scene's MainMenu.
 */
UCLASS()
class ASpaceHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
