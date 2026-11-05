#include "GameLaunch.h"
#include "Bootstrap.h"
#include "Coop/CoopPeer.h"

namespace SpaceGod
{
	bool GameLaunch::SkipsMenu()
	{
		return Bootstrap::HasCommandLineFlag("-demo") || Bootstrap::HasCommandLineFlag("-capture") || Bootstrap::HasCommandLineFlag("-continue")
			|| Coop::CoopPeer::SkipsMenu(); // US-T44: a co-op client (and the smoke host) goes straight in
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

	bool GameLaunch::SceneInBuild(const cs::String& scenePath)
	{
		(void)scenePath;
		return false;
	}

	void GameLaunch::ToMainMenu() { LoadScene(MenuScene); }

	void GameLaunch::LoadScene(const cs::String& scene)
	{
		if (SceneLoader) SceneLoader(scene);
	}
}
