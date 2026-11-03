#include "GameLaunch.h"
#include "Bootstrap.h"

namespace SpaceGod
{
	bool GameLaunch::SkipsMenu()
	{
		return Bootstrap::HasCommandLineFlag("-demo") || Bootstrap::HasCommandLineFlag("-capture") || Bootstrap::HasCommandLineFlag("-continue");
	}

	void GameLaunch::NewGame() { Start(cs::String()); }

	void GameLaunch::Load(const cs::String& slot) { Start(slot); }

	void GameLaunch::Start(const cs::String& slot)
	{
		FromMenu = true;
		loadSlot = slot;
		LoadScene(GameScene);
	}

	cs::String GameLaunch::TakeLoadSlot()
	{
		cs::String slot = loadSlot;
		loadSlot.clear();
		return slot;
	}

	void GameLaunch::ToMainMenu() { LoadScene(MenuScene); }

	void GameLaunch::LoadScene(const cs::String& scene)
	{
		if (SceneLoader) SceneLoader(scene);
	}
}
