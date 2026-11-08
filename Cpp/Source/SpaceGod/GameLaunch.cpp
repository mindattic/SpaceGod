#include "GameLaunch.h"
#include "Bootstrap.h"
#include "Coop/CoopPeer.h"
#include "MindAtticHeadless.h"
#include "UI/UiMotion.h"

namespace SpaceGod
{
	bool GameLaunch::SkipsMenu()
	{
		return Bootstrap::HasCommandLineFlag("-demo") || Bootstrap::HasCommandLineFlag("-capture") || Bootstrap::HasCommandLineFlag("-continue")
			|| Bootstrap::HasCommandLineFlag("-dogfight") || Bootstrap::HasCommandLineFlag("-dogfight-heavy") // US-T88
			|| FMindAtticHeadless::IsActive() // US-T233, US-T234: a headless run has no window to click a menu in
			|| Coop::CoopPeer::SkipsMenu(); // US-T44: a co-op client (and the smoke host) goes straight in
	}

	void GameLaunch::NewGame()
	{
		tutorial = 0;
		Start(cs::String());
	}

	void GameLaunch::MenuNewGame()
	{
		tutorial = 1; // US-T254: offered if this profile never was
		Start(cs::String());
	}

	void GameLaunch::Tutorial()
	{
		tutorial = 2; // US-T254
		Start(cs::String());
	}

	int GameLaunch::TakeTutorial()
	{
		int asked = tutorial;
		tutorial = 0;
		return asked;
	}

	void GameLaunch::Load(const cs::String& slot)
	{
		tutorial = 0;
		Start(slot);
	}

	void GameLaunch::DogfightDemo(bool heavy)
	{
		tutorial = 0;
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
		Travel(GameScene);
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

	void GameLaunch::ToMainMenu() { Travel(MenuScene); }

	void GameLaunch::Host(const cs::String& slot)
	{
		tutorial = 0;
		hostCoop = true; // US-T248: the game scene opens listening (SceneLoader -> ?listen)
		Start(slot);
	}

	bool GameLaunch::TakeHost()
	{
		bool asked = hostCoop;
		hostCoop = false;
		return asked;
	}

	void GameLaunch::Join(const cs::String& address)
	{
		// US-T248: through black, as a scene change; the engine brings the game back to the menu if the host never answers.
		JoinFailure.clear();
		UI::UiMotion::FadeOutThen([address] { if (Connector) Connector(address); });
	}

	void GameLaunch::Travel(const cs::String& scene)
	{
		// US-T255: through black, back in once the scene has arrived (Bootstrap::EnsureApp -> UiMotion::SceneArrived).
		UI::UiMotion::FadeOutThen([scene] { LoadScene(scene); });
	}

	void GameLaunch::LoadScene(const cs::String& scene)
	{
		if (SceneLoader) SceneLoader(scene);
	}
}
