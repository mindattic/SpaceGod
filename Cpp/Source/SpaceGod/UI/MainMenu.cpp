#include "UI/MainMenu.h"
#include "UI/UiMotion.h" // US-T255

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/WeakObjectPtrTemplates.h"

// Windows' rpcndr.h defines `small` as a macro (UnityGui.h undefines it, but an Unreal header may bring it back).
#ifdef small
#undef small
#endif

#include "ActiveProfile.h"
#include "AssetLibraryCandidates.h"
#include "Difficulty.h" // US-T253
#include "GameLaunch.h"
#include "GameSettingsHost.h"
#include "Persistence/SaveGame.h"
#include "Settings/GameSetting.h" // US-T253
#include "Shared/Profiles/ProfileStore.h"
#include "UI/CreditsScreen.h" // US-T246
#include "UI/DisplayOptions.h"
#include "UI/HelpScreen.h"
#include "UI/Hud.h"
#include "UI/PauseMenu.h"
#include "UI/UiInput.h"
#include "UnityCompat/GuiHost.h"
#include "UnitySpaceApp.h"
#include "Views/Starfield.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	// ---------------------------------------------------------------- the scene's one menu (Unreal host)

	namespace
	{
		/// The menu scene's MainMenu object and the world (the scene) it lives in.
		cs::Ref<MainMenu> sceneMenu;
		UWorld* sceneWorld = nullptr;
		TWeakObjectPtr<UWorld> sceneWorldAlive;
		bool cleanupHooked = false;

		/// Unity destroying the scene's object: OnDestroy, and its OnGUI leaves the GUI host.
		void DestroySceneMenu()
		{
			if (!sceneMenu) return;
			cs::Ref<MainMenu> menu = sceneMenu;
			sceneMenu = nullptr;
			sceneWorld = nullptr;
			sceneWorldAlive.Reset();
			GuiHost::Unregister(menu.get());
			menu->OnDestroy();
		}
	}

	void MainMenu::Create(UWorld* world)
	{
		if (sceneMenu && sceneWorld == world) return;
		DestroySceneMenu();
		if (!cleanupHooked)
		{
			// A scene unload destroys the menu with it, also when the next scene has an app (DrawGui no longer runs then).
			cleanupHooked = true;
			FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld* w, bool, bool)
			{
				if (sceneMenu && w == sceneWorld) DestroySceneMenu();
			});
		}
		sceneMenu = cs::New<MainMenu>();
		sceneWorld = world;
		sceneWorldAlive = world;
		sceneMenu->Awake();
		MainMenu* m = sceneMenu.get();
		GuiHost::Register(m, [m]
		{
			if (sceneWorldAlive.IsValid()) m->OnGUI();
		});
	}

	bool MainMenu::Showing() { return sceneMenu != nullptr && sceneWorldAlive.IsValid(); }

	void MainMenu::DrawGui(UWorld* world, UCanvas* /*canvas*/)
	{
		if (!sceneMenu) return;
		if (world != sceneWorld || !sceneWorldAlive.IsValid())
		{
			DestroySceneMenu();
			return;
		}
		// Unity runs Update before OnGUI; the GUI host runs OnGUI on its overlay.
		sceneMenu->Update();
	}

	MainMenu::MainMenu()
		: settings(cs::New<PauseMenu>()), help(cs::New<HelpScreen>()), credits(cs::New<CreditsScreen>())
	{
	}

	// ---------------------------------------------------------------- the menu

	void MainMenu::Awake()
	{
		GameSettingsHost::Bind(); // every declared setting, from the profile (frame rate, volume...) (U.SPC-T431)
		Time::timeScale = 1;
		DisplayOptions::ApplySaved(); // the profile's resolution, window mode and VSync
		Cursor::lockState = CursorLockMode::None;
		Cursor::visible = true;
		skyCamera = Camera::main();
		Views::Starfield::Create(transform, 9000, 7, true);
		ReadProfile();
	}

	void MainMenu::Update()
	{
		if (skyCamera != nullptr) skyCamera->transform->SetRotation(Quaternion::Euler(-12.0f, Time::unscaledTime * 1.5f, 0));
		bool escape = UiInput::present() && UiInput::WasPressedThisFrame(KeyCode::Escape);
		if (settings->IsOpen)
		{
			settings->Update(escape, Time::unscaledDeltaTime);
			return;
		}
		if (escape && page != Page::Main) Open(Page::Main);
	}

	void MainMenu::OnDestroy()
	{
		// Object.Destroy(texture): dropping the last reference.
		panelTexture = buttonTexture = buttonActiveTexture = rowTexture = fieldTexture = nullptr;
		settings->DestroyOwnedTextures();
		help->DestroyOwnedTextures();
		credits->DestroyOwnedTextures(); // US-T246
	}

	void MainMenu::ReadProfile()
	{
		slots = cs::New<SaveSlots>(ActiveProfile::Current()->SavesFolder());
		saveNames = cs::List<cs::String>();
		saveHeaders = cs::List<cs::Ref<SaveHeader>>();
		for (const auto& slot : slots->AllSlots())
		{
			auto h = slots->Header(slot);
			if (h == nullptr) continue;
			saveNames.Add(slot);
			saveHeaders.Add(h);
		}
		latest = slots->Latest();
		profiles = cs::List<cs::String>(ActiveProfile::Store()->List());
	}

	void MainMenu::Open(Page p)
	{
		page = p;
		notice.clear();
		deleteArmed.clear();
		ReadProfile();
	}

	void MainMenu::CreateProfile()
	{
		cs::String name = cs::Strings::Trim(newProfileName);
		if (!Shared::ProfileStore::IsValidName(name)) notice = "A profile name is 1 to 64 characters, without / \\ : * ? \" < > |";
		else if (ActiveProfile::Store()->Exists(name)) notice = cs::Cat("There is a profile called ", name, " already.");
		else
		{
			ActiveProfile::Switch(name);
			newProfileName = "";
			Open(Page::Main);
		}
	}

	void MainMenu::Delete(const cs::String& slot)
	{
		try
		{
			slots->Delete(slot);
			notice.clear();
		}
		catch (const cs::IOException& e)
		{
			notice = cs::Cat("Could not delete ", UnitySpaceApp::SlotName(slot), ": ", e.what());
		}
		deleteArmed.clear();
		ReadProfile();
	}

	void MainMenu::Quit()
	{
		// C#: UnityEditor.EditorApplication.isPlaying = false in the editor; Application.Quit() in a player.
		Application::Quit();
	}

	// ---------------------------------------------------------------- drawing

	void MainMenu::OnGUI()
	{
		EnsureStyles();
		if (settings->IsOpen)
		{
			settings->Draw();
			return;
		}
		// US-T246: the credits, the whole screen (they fade themselves in).
		if (page == Page::Credits)
		{
			if (credits->Draw("Esc to go back")) Open(Page::Main);
			return;
		}
		// US-T255: each page eases in as it opens (Help fades itself in).
		static const char* const pageIds[] = { "menu.main", "menu.load", "menu.profiles", "menu.assets", nullptr };
		UiMotion::Scope motion(pageIds[Mathf::Clamp(static_cast<int>(page), 0, 4)]);
		switch (page)
		{
			case Page::Load: DrawLoad(); break;
			case Page::Profiles: DrawProfiles(); break;
			case Page::AssetLibraries: DrawAssetLibraries(); break;
			case Page::Help:
				help->OnTutorial = [] { GameLaunch::Tutorial(); }; // US-T254: Help's Tutorial button starts one
				if (help->Draw("Esc to go back")) Open(Page::Main);
				break;
			default: DrawMain(); break;
		}
	}

	Rect MainMenu::Centred(float w, float h)
	{
		return Rect((Screen::width - w) * 0.5f, (Screen::height - h) * 0.5f, w, h);
	}

	void MainMenu::DrawMain()
	{
		const float w = 380, h = 564;
		auto r = Centred(w, h);
		GUI::Label(Rect(r.x, r.y - 70, w, 48), "UNITYSPACE", heading);
		GUI::Box(r, GUIContent::none(), panel);
		GUI::Label(Rect(r.x + 20, r.y + 14, w - 40, 22), cs::Cat("PROFILE  ·  ", cs::Strings::ToUpperInvariant(ActiveProfile::Current()->Name)), title);
		GUI::Label(Rect(r.x + 20, r.y + 38, w - 40, 32),
			latest == nullptr ? cs::String("No saves yet") : cs::Cat("Latest: ", UnitySpaceApp::SaveTitle(latest->Slot, latest), "  ·  ", UnitySpaceApp::DescribeSave(latest)), small);

		float y = r.y + 80, bw = w - 40;
		bool enabled = GUI::enabled;
		GUI::enabled = enabled && latest != nullptr;
		if (GUI::Button(Rect(r.x + 20, y, bw, 38), "Continue", latest != nullptr ? buttonActive : button)) GameLaunch::Load(latest->Slot);
		GUI::enabled = enabled;
		// US-T253: the new game's difficulty beside it (a click steps Story / Normal / Hard / Custom; Settings > Gameplay holds it too).
		const float dw = 150;
		if (GUI::Button(Rect(r.x + 20, y + 48, bw - dw - 10, 38), "New Game", latest == nullptr ? buttonActive : button)) GameLaunch::MenuNewGame(); // US-T254: offers the tutorial once a profile
		if (GUI::Button(Rect(r.x + 20 + bw - dw, y + 48, dw, 38), cs::Cat("Difficulty: ", Difficulty::Name()), button))
			Difficulty::Level()->Cycle(1);
		GUI::enabled = enabled && saveNames.Count() > 0;
		if (GUI::Button(Rect(r.x + 20, y + 96, bw, 38), "Load Game", button)) Open(Page::Load);
		GUI::enabled = enabled;
		if (GUI::Button(Rect(r.x + 20, y + 144, bw, 38), "Profiles", button)) Open(Page::Profiles);
		if (GUI::Button(Rect(r.x + 20, y + 192, bw, 38), "Settings", button)) settings->OpenSettings();
		// US-T246: Credits beside Help.
		const float helpW = (bw - 10) * 0.5f;
		if (GUI::Button(Rect(r.x + 20, y + 240, helpW, 38), "Help", button)) Open(Page::Help);
		if (GUI::Button(Rect(r.x + 30 + helpW, y + 240, helpW, 38), "Credits", button))
		{
			credits->Restart();
			Open(Page::Credits);
		}
		if (GUI::Button(Rect(r.x + 20, y + 288, bw, 38), "Asset Library POC", button)) Open(Page::AssetLibraries);
		// US-T88: a new game straight into the dogfight demo, at a light or a heavy fighter's helm (-dogfight / -dogfight-heavy).
		float half = (bw - 10) * 0.5f;
		if (GUI::Button(Rect(r.x + 20, y + 336, half, 38), "Dogfight Demo", button)) GameLaunch::DogfightDemo(false);
		if (GUI::Button(Rect(r.x + 30 + half, y + 336, half, 38), "Dogfight (Heavy)", button)) GameLaunch::DogfightDemo(true);
		// US-T147: a new game straight into the showcase tour, every feature in turn (sg.Tour waits for the game it starts).
		// US-T254: the guided tutorial, played by doing, any time (a new game with it running).
		if (GUI::Button(Rect(r.x + 20, y + 384, half, 38), "Tutorial", button)) GameLaunch::Tutorial();
		if (GUI::Button(Rect(r.x + 30 + half, y + 384, half, 38), "Showcase Tour", button) && GEngine) GEngine->Exec(nullptr, TEXT("sg.Tour"));
		if (GUI::Button(Rect(r.x + 20, y + 432, bw, 38), "Quit", button)) Quit();
	}

	void MainMenu::DrawLoad()
	{
		const float w = 760;
		float listH = Mathf::Clamp(saveNames.Count(), 1, 14) * RowH;
		float h = 134 + listH;
		auto r = Centred(w, h);
		GUI::Box(r, GUIContent::none(), panel);
		GUI::Label(Rect(r.x + 20, r.y + 14, w - 40, 22), cs::Cat("LOAD GAME  ·  ", cs::Strings::ToUpperInvariant(ActiveProfile::Current()->Name)), title);
		GUI::Label(Rect(r.x + 20, r.y + 38, w - 40, 18), "Newest first  ·  Esc to go back", small);

		auto view = Rect(r.x + 20, r.y + 66, w - 40, listH + 2);
		auto content = Rect(0, 0, view.width - (saveNames.Count() > 14 ? 18 : 0), Mathf::Max(1, saveNames.Count()) * RowH);
		saveScroll = GUI::BeginScrollView(view, saveScroll, content);
		if (saveNames.Count() == 0) GUI::Label(Rect(6, 4, content.width, 18), "This profile has no saves.", small);
		cs::List<int> order;
		for (int i = 0; i < saveNames.Count(); i++) order.Add(i);
		order.Sort([this](int a, int b) { return saveHeaders[b]->SavedUtc.CompareTo(saveHeaders[a]->SavedUtc); });
		for (int n = 0; n < order.Count(); n++)
		{
			int i = order[n];
			cs::String slot = saveNames[i];
			auto hdr = saveHeaders[i];
			auto row0 = Rect(0, n * RowH, content.width, RowH - 2);
			GUI::Box(row0, GUIContent::none(), row);
			GUI::Label(Rect(row0.x + 6, row0.y + 3, 150, 20), UnitySpaceApp::SaveTitle(slot, hdr), text);
			GUI::Label(Rect(row0.x + 160, row0.y + 4, row0.width - 330, 20),
				cs::Cat(UnitySpaceApp::DescribeSave(hdr), "   ·   saved ", hdr->SavedUtc.ToLocalTime().ToString("MMM d HH:mm")), small);
			if (GUI::Button(Rect(row0.xMax() - 156, row0.y + 2, 74, 20), "Load", buttonActive)) GameLaunch::Load(slot);
			if (GUI::Button(Rect(row0.xMax() - 78, row0.y + 2, 74, 20), deleteArmed == slot ? "Sure?" : "Delete", button))
			{
				if (deleteArmed != slot) deleteArmed = slot;
				else
				{
					Delete(slot);
					GUIUtility::ExitGUI(); // the list just changed under this loop
				}
			}
		}
		GUI::EndScrollView();

		if (!notice.empty()) GUI::Label(Rect(r.x + 20, r.yMax() - 44, w - 170, 36), notice, small);
		if (GUI::Button(Rect(r.xMax() - 130, r.yMax() - 46, 110, 30), "Back", button)) Open(Page::Main);
	}

	void MainMenu::DrawProfiles()
	{
		const float w = 520;
		float listH = Mathf::Clamp(profiles.Count(), 1, 10) * RowH;
		float h = 250 + listH;
		auto r = Centred(w, h);
		GUI::Box(r, GUIContent::none(), panel);
		GUI::Label(Rect(r.x + 20, r.y + 14, w - 40, 22), "PROFILES", title);
		GUI::Label(Rect(r.x + 20, r.y + 38, w - 40, 18), "Each profile keeps its own saves and settings  ·  Esc to go back", small);

		auto view = Rect(r.x + 20, r.y + 66, w - 40, listH + 2);
		auto content = Rect(0, 0, view.width - (profiles.Count() > 10 ? 18 : 0), Mathf::Max(1, profiles.Count()) * RowH);
		profileScroll = GUI::BeginScrollView(view, profileScroll, content);
		for (int i = 0; i < profiles.Count(); i++)
		{
			cs::String name = profiles[i];
			bool current = name == ActiveProfile::Current()->Name;
			auto row0 = Rect(0, i * RowH, content.width, RowH - 2);
			GUI::Box(row0, GUIContent::none(), row);
			GUI::Label(Rect(row0.x + 6, row0.y + 3, row0.width - 100, 20), current ? cs::Cat(name, "   (in use)") : name, current ? title : text);
			if (!current && GUI::Button(Rect(row0.xMax() - 78, row0.y + 2, 74, 20), "Use", buttonActive))
			{
				ActiveProfile::Switch(name);
				Open(Page::Main);
				GUIUtility::ExitGUI();
			}
		}
		GUI::EndScrollView();

		float y = view.yMax() + 18;
		GUI::Label(Rect(r.x + 20, y, w - 40, 20), "New profile", text);
		GUI::SetNextControlName("NewProfileName");
		Event& e = Event::current();
		bool submit = e.type == EventType::KeyDown && cs::Strings::Trim(newProfileName).size() > 0
			&& (e.keyCode == KeyCode::Return || e.keyCode == KeyCode::KeypadEnter || e.character == U'\n' || e.character == U'\r');
		newProfileName = GUI::TextField(Rect(r.x + 20, y + 24, w - 170, 28), newProfileName, 64, field);
		if (GUI::Button(Rect(r.xMax() - 140, y + 24, 120, 28), "Create", buttonActive) || submit)
		{
			CreateProfile();
			if (submit) e.Use();
		}
		if (!notice.empty()) GUI::Label(Rect(r.x + 20, y + 58, w - 170, 36), notice, small);
		if (GUI::Button(Rect(r.xMax() - 130, r.yMax() - 46, 110, 30), "Back", button)) Open(Page::Main);
	}

	bool MainMenu::DemoInstalled(const cs::Ref<AssetCandidate>& c)
	{
		// C#: SceneUtility.GetBuildIndexByScenePath(c.DemoScenePath) >= 0.
		return GameLaunch::SceneInBuild(c->DemoScenePath);
	}

	void MainMenu::DrawAssetLibraries()
	{
		const float w = 760;
		const auto& list = AssetLibraryCandidates::All();
		float h = 150 + list.Count() * 52;
		auto r = Centred(w, h);
		GUI::Box(r, GUIContent::none(), panel);
		GUI::Label(Rect(r.x + 20, r.y + 14, w - 40, 22), "ASSET LIBRARY POC", title);
		GUI::Label(Rect(r.x + 20, r.y + 38, w - 40, 32),
			"Candidate Asset Store packages, to judge in game before buying for real. Buy and import a package, add its demo scene to the build at the path shown, then launch it here  ·  Esc to go back", small);
		for (int i = 0; i < list.Count(); i++)
		{
			auto c = list[i];
			bool installed = DemoInstalled(c);
			auto row0 = Rect(r.x + 20, r.y + 78 + i * 52, w - 40, 48);
			GUI::Box(row0, GUIContent::none(), row);
			GUI::Label(Rect(row0.x + 6, row0.y + 3, 300, 20), c->Name, text);
			GUI::Label(Rect(row0.x + 6, row0.y + 22, row0.width - 130, 24),
				cs::Cat(c->Purpose, "  ·  Unity 6: ", AssetCandidate::Describe(c->Unity6), ", URP: ", AssetCandidate::Describe(c->Urp),
					", HDRP: ", AssetCandidate::Describe(c->Hdrp), "  ·  ", (installed ? cs::String("demo installed") : cs::Cat("not installed (", c->DemoScenePath, ")"))), small);
			bool enabled = GUI::enabled;
			GUI::enabled = enabled && installed;
			if (GUI::Button(Rect(row0.xMax() - 110, row0.y + 10, 104, 28), "Launch demo", installed ? buttonActive : button))
				GameLaunch::Travel(c->DemoScenePath); // US-T255: through black
			GUI::enabled = enabled;
		}
		if (GUI::Button(Rect(r.xMax() - 130, r.yMax() - 46, 110, 30), "Back", button)) Open(Page::Main);
	}

	void MainMenu::EnsureStyles()
	{
		if (stylesReady) return;
		stylesReady = true;
		// The shared look (US-T199, UiStyle.h), over the sky: the front end's bigger buttons and fields take the heading size.
		namespace S = UiStyle;
		namespace T = UiStyle::Type;
		panelTexture = S::PanelFrame(S::Depth::Overlay);
		buttonTexture = S::ControlTexture();
		buttonActiveTexture = S::ControlOnTexture();
		rowTexture = S::Flat(Color(0.08f, 0.12f, 0.16f, 0.7f), Color::clear());
		fieldTexture = S::FieldTexture();

		panel = S::PanelStyle(panelTexture);
		heading = S::TitleStyle(T::Display);
		heading->alignment = TextAnchor::MiddleCenter;
		title = S::TitleStyle(T::Title);
		title->clipping = TextClipping::Clip;
		text = S::TextStyle(T::Body);
		text->clipping = TextClipping::Clip;
		small = S::CaptionStyle(T::Caption);
		small->clipping = TextClipping::Clip;
		button = S::ButtonStyle(T::Heading, buttonTexture, buttonActiveTexture);
		buttonActive = S::ButtonOnStyle(button, buttonActiveTexture);
		row = cs::New<GUIStyle>();
		row->normal.background = rowTexture;
		field = S::FieldStyle(T::Heading, fieldTexture);
	}
}
