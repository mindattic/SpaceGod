#include "GameLaunch.h"
#include "Bootstrap.h"
#include "Coop/CoopPeer.h"

namespace SpaceGod
{
	bool GameLaunch::SkipsMenu()
	{
		return Bootstrap::HasCommandLineFlag("-demo") || Bootstrap::HasCommandLineFlag("-capture") || Bootstrap::HasCommandLineFlag("-continue")
			|| Bootstrap::HasCommandLineFlag("-dogfight") || Bootstrap::HasCommandLineFlag("-dogfight-heavy") // US-T88
			|| Bootstrap::HasCommandLineFlag("--headless") // US-T233: a headless run has no window to click a menu in
			|| Coop::CoopPeer::SkipsMenu(); // US-T44: a co-op client (and the smoke host) goes straight in
	}

	void GameLaunch::NewGame() { Start(cs::String()); }

	void GameLaunch::Load(const cs::String& slot) { Start(slot); }

	void GameLaunch::DogfightDemo(bool heavy)
	{
		dogfightDemo = heavy ? 2 : 1;
		Start(cs::String());
	}

	int GameLaunch::TakeDogfightDemo()
	{
		int asked = dogfightDemo;
		dogfightDemo = 0;
		// The switch counts for the first game only, not again after a trip back to the menu.
		if (asked == 0 && !dogfightFlagTaken)
		{
			dogfightFlagTaken = true;
			if (Bootstrap::HasCommandLineFlag("-dogfight-heavy")) asked = 2;
			else if (Bootstrap::HasCommandLineFlag("-dogfight")) asked = 1;
		}
		return asked;
	}

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
