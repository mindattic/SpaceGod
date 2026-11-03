#include "Game/SpaceHUD.h"
#include "UnitySpaceAppActor.h"
#include "UI/MainMenu.h"

void ASpaceHUD::DrawHUD()
{
	Super::DrawHUD();
	if (AUnitySpaceAppActor* App = AUnitySpaceAppActor::Find(GetWorld()))
		App->DrawGui(Canvas);
	else
		SpaceGod::UI::MainMenu::DrawGui(GetWorld(), Canvas); // the menu scene has no app, only its menu
}
