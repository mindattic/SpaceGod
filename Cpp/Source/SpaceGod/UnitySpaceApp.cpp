// Port of Runtime/UnitySpaceApp.cs lines 1-1470 (declarations in UnitySpaceApp.Main.inl): the lifecycle (Awake, Update,
// OnGUI, OnDestroy), system activation, commands and piloting. Lines 1471-end are UnitySpaceApp.Main2.cpp.

#include "UnitySpaceApp.h"
#include "CaptureEvents.h" // US-T72

#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Input/InputState.h"
#include "Input/InputDevice.h"
#include "EngineSettings.h"
#include "GameSettingsHost.h"
#include "ModelViewer.h"
#include "Rendering/Materials.h"
#include "Rendering/Pipeline/RenderBackend.h"
#include "UI/DisplayOptions.h"
#include "UI/Hud.h"
#include "UI/GalaxyMapScreen.h" // US-T47
#include "UI/PauseMenu.h"
#include "UI/UiInput.h"
#include "Smoke/SpaceGodRace.h"
#include "Tour/ShowcaseTour.h" // US-T147
#include "UnityCompat/UnityGui.h"
#include "Views/AsteroidBelt.h"
#include "Views/BattleFx.h"
#include "Views/BodyView.h"
#include "Views/DebrisFields.h"
#include "Views/DiscView.h"
#include "Views/GraveyardView.h"
#include "Views/HarvesterView.h"
#include "Systems/Graveyards.h"
#include "Views/SpaceFeaturesView.h"
#include "Views/EmbeddedPods.h"
#include "Views/FighterViews.h"
#include "Views/GateView.h"
#include "Views/InterstellarView.h"
#include "Views/LaneView.h"
#include "Views/LineStrip.h"
#include "Views/PodBeacons.h"
#include "Views/BioMassView.h"
#include "Views/EmbodiedShine.h" // US-T136
#include "Views/SatelliteSwarm.h"
#include "Views/ShipView.h"
#include "Views/SkyView.h"
#include "Views/Starfield.h"
#include "Views/StructureViews.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Crews.h"
#include "Combat/FlightModels.h"
#include "Combat/PilotAim.h"
#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Combat/Pursuit.h"
#include "Combat/Salvage.h"
#include "Combat/War.h"
#include "Combat/Wreck.h"
#include "Economy/Contracts.h"
#include "Economy/Economy.h"
#include "Economy/Holdings.h"
#include "Economy/Settlement.h"
#include "Economy/TradeDesk.h"
#include "Embodiment/CraftPower.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/Embodiment.h"
#include "Embodiment/HelmControls.h"
#include "Embodiment/HelmFlight.h"
#include "Embodiment/ShipHelm.h"
#include "Ephemeris.h"
#include "EscapeKey.h"
#include "Events/Opportunity.h"
#include "Fleet.h"
#include "Hyperlanes.h"
#include "Law/Law.h"
#include "Law/Outlaws.h"
#include "Rendering/Skyboxes.h"
#include "ScaleModel.h"
#include "Sensors/Sensors.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "SimClock.h"
#include "Systems/DeepSpace.h"
#include "Systems/FtlNavigation.h"
#include "Systems/Galaxy.h"
#include "Systems/JumpGate.h"
#include "Systems/Polities.h"
#include "Systems/Statecraft.h"
#include "World.h"

#include <tuple>

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		/// Array.Resize(ref a, n): keeps the contents, default-fills the rest (or drops the tail).
		template <class T> void ResizeArray(cs::List<T>& a, int n) { a.Raw().resize(static_cast<size_t>(n)); }
	}

	// ---------------------------------------------------------------- bodied properties

	const cs::Ref<BodyCatalog>& UnitySpaceApp::Catalog() const { return ActiveSystem->Catalog; }
	const cs::Ref<Core::Ephemeris>& UnitySpaceApp::Ephemeris() const { return ActiveSystem->Ephemeris; }
	const cs::Ref<Core::World>& UnitySpaceApp::World() const { return ActiveSystem->World; }
	const cs::Ref<Core::Fleet>& UnitySpaceApp::Fleet() const { return World()->Fleet; }
	const cs::Ref<Core::Economy>& UnitySpaceApp::Economy() const { return World()->Economy; }

	float UnitySpaceApp::PilotLookSensitivity() { return std::isnan(CoopLookSensitivity) ? Cameras::SpaceCamera::LookSensitivity() : CoopLookSensitivity; } // US-T44 (US-T43): the helm holder's in co-op

	void UnitySpaceApp::ResetPilotAim()
	{
		pilotAim = pilot().Forward;
		pilotViewUp = pilot().Up;
		helmLookYaw = helmLookPitch = 0;
		pilotLookOwedRight = pilotLookOwedUp = 0; // US-T168
	}

	void UnitySpaceApp::HailPolity()
	{
		auto statecraft = Galaxy->Polities->Statecraft;
		cs::Ref<Faction> to = pilotLockTarget.IsShip() ? pilotLockTarget.Ship->Faction : nullptr;
		if (to == nullptr && Selected.IsShip()) to = Selected.Ship->Faction;
		if (to == nullptr) to = World()->Law != nullptr && World()->Law->Hail != nullptr ? World()->Law->Hail->By : nullptr;
		auto from = statecraft->Player();
		if (from == nullptr) from = Piloted.Ship != nullptr ? Piloted.Ship->Faction : nullptr;
		if (to == nullptr || to == from)
		{
			ShowToast("Nobody to hail: lock or select a ship of another polity first (F12 hails it)");
			return;
		}
		CaptureEvents::Hail(from != nullptr ? from->Name : cs::String(), to->Name); // US-T72: on the peer whose player hailed, before co-op routing
		if (UI::GalaxyMapScreen::CoopSendsHail(this, from, to)) return; // US-T47: the hail at its frame on every peer, the envoy opened here
		ShowToast(statecraft->Hail(from, to, Clock->Jd));
		if (!Core::Statecraft::Party(to)) return;
		EnvoyTo = to;
		ShowGalaxyMap = true;
	}

	bool UnitySpaceApp::ClickPanelOpen() const
	{
		if (coopPanelOverride.has_value()) return *coopPanelOverride; // US-T44 (US-T43): the helm holder's, while its frame is flown in co-op
		return !ScreenCleared && (ShowSpawn || ShowSaves || ShowGalaxyMap || ShowHelp || (ShowHud && (ShowMarket || ShowCargo || ShowNews || ShowOutfit || ShowBounties || ShowContracts || ShowFleet || ShowTrade || ShowPicker))); // a cleared screen (F11) shows none of them; market, cargo, outfitting, bounties, contracts and news are HUD panels, hidden with it
	}

	bool UnitySpaceApp::IsSol() const { return ActiveSystem->Catalog->Root()->Name == "Sun"; }

	double UnitySpaceApp::BattleKmScale() const { return Scale()->ShipLength(1.0); }

	DVec3 UnitySpaceApp::BattleScene(const cs::Ref<Battle>& b, DVec3 localKm) { return BattleAnchorScene(b) + localKm.SwapYZ() * BattleKmScale(); }

	DVec3 UnitySpaceApp::BattleAnchorScene(const cs::Ref<Battle>& b) { return AnchorScene(b->Site, b->Body, b->Anchor); }

	DVec3 UnitySpaceApp::WreckScene(const cs::Ref<Wreck>& w) { return AnchorScene(w->Site, w->Body, w->Anchor) + w->Local.SwapYZ() * BattleKmScale(); }

	DVec3 UnitySpaceApp::PodScene(const cs::Ref<EscapePod>& p) { return AnchorScene(p->Site, p->Body, p->Anchor) + p->LocalAt(Clock->Jd).SwapYZ() * BattleKmScale(); }

	DVec3 UnitySpaceApp::AnchorScene(const cs::Ref<LaneSite>& site, int body, DVec3 anchor)
	{
		if (site == nullptr) return bodyScene[body] + Scale()->LocalOffset((*Catalog())[body], anchor);
		const auto& hop = site->Hop;
		auto aKm = Ephemeris()->Helio(hop.From) + site->FromOffset;
		auto cKm = Ephemeris()->Helio(hop.To) + site->ToOffset;
		DVec3 discard;
		auto onLane = Scale()->LaneCurveScene(Ephemeris(), bodyScene, Fleet()->Lanes, hop.Lane, hop.From, aKm, hop.To, cKm, site->Progress, discard);
		return onLane + SideScene(aKm, site->Side);
	}

	DVec3 UnitySpaceApp::SideScene(DVec3 nearHelio, DVec3 sideKm)
	{
		return sideKm.SwapYZ() * Scale()->LocalDistanceDerivative(Catalog()->Root(), cs::Math::Max(nearHelio.Length(), 1e6));
	}

	DVec3 UnitySpaceApp::LaneHoldScene(const cs::Ref<Ship>& ship, DVec3& along)
	{
		auto on = Scale()->LaneScene(Ephemeris(), bodyScene, Fleet()->Lanes, ship, ship->LaneHold, along);
		double ease = Astro::SmoothStep(0, 0.08, Clock->Jd - ship->PhaseStartJd);
		return on + SideScene(Fleet()->LanePoint(ship, Clock->Jd, ship->LaneHold), ship->LaneHoldSide) * ease;
	}

	cs::Ref<Transform> UnitySpaceApp::UnitTransform(const cs::Ref<BattleUnit>& u)
	{
		if (u->Ship != nullptr) return u->Ship->Id < shipViews.Count() && shipViews[u->Ship->Id] != nullptr ? shipViews[u->Ship->Id]->Root->transform : nullptr;
		cs::Ref<GameObject> go;
		if (u->Kind == UnitKind::Platform && platformViews.TryGetValue(u->Combat, go)) return go->transform;
		return nullptr;
	}

	// ---------------------------------------------------------------- lifecycle

	void UnitySpaceApp::Awake()
	{
		GameSettingsHost::Bind(); // every declared setting, from the active profile (U.SPC-T431)
		UI::DisplayOptions::ApplySaved(); // the resolution, window mode and VSync last kept (VSync on until then)
		pauseMenu->ShowSaves = [this] { ShowSaves = true; };
		pauseMenu->ShowHelp = [this] { ShowHelp = true; };
		pauseMenu->ToMainMenu = [this] { pendingMainMenu = true; };

		Clock = std::isnan(StartJd) ? SimClock::StartingNow() : cs::New<SimClock>(StartJd); // US-T45: was SimClock::StartingNow()
		Clock->SetWarp(SimClock::StartingWarp()->Index);
		scaleModel = cs::New<ScaleModel>();
		scaleModel->SetBlend(1);
		Galaxy = Core::Galaxy::CreateDefault(Clock->Jd);

		SetupCamera();
		worldRoot = NewGameObject("World")->transform; // held by the scene until Destroy (US-T30), as Unity's new GameObject
		auto starfield = Views::Starfield::Create(worldRoot);
		// The skybox behind every system (U.SPC-T595): the player's own panoramas join the catalogue first.
		Views::SkyView::DiscoverDropIns();
		Sky = cs::New<Views::SkyView>(worldRoot, starfield);
		BeginSkyTravel(); // US-T145: the Milky Way, the painted volume, the jump stars and the tunnel
		orbitSamples = cs::NewArray<DVec3>(257);
		orbitPoints = cs::NewArray<Vector3>(orbitSamples.Count());

		hud = cs::New<UI::Hud>(this);
		SpaceCamera = cs::New<Cameras::SpaceCamera>(this);
		Travel = cs::New<Cameras::StarTravel>(this);
		Interstellar = cs::New<Views::InterstellarView>(Galaxy, worldRoot);
		SubscribeWorlds();

		Activate(Galaxy->Systems[0]);
		AsteroidBelt = Views::AsteroidBelt::Create(this);

		auto earth = (*Catalog())["Earth"]->Index;
		auto start = bodyScene[earth] + DVec3(-0.6, 0.35, -1).Normalized() * (Scale()->DisplayRadius((*Catalog())[earth]) * 9);
		SpaceCamera->PlaceFree(start, 0, 0);
		auto look = (bodyScene[earth] - start).Normalized();
		SpaceCamera->PlaceFree(start, static_cast<float>(cs::Math::Atan2(look.X, look.Z) * Mathf::Rad2Deg), static_cast<float>(-cs::Math::Asin(look.Y) * Mathf::Rad2Deg));
		Select(Target::OfBody(earth));
		InitSaves();
		CaptureEvents::Attach(this); // US-T72: the capture events' subscriptions and polling (docs/CAPTURE.md)
	}

	void UnitySpaceApp::SubscribeWorlds()
	{
		for (const auto& system : Galaxy->Systems) SubscribeWorld(system);
		Galaxy->SystemAdded += [this](cs::Ref<StarSystem> s, double) { SubscribeWorld(s); };
		Galaxy->SystemOpened += [this](cs::Ref<StarSystem> s, cs::Ref<Faction> by, double)
		{
			ShowToast(cs::Cat(cs::Strings::ToUpperInvariant(s->Name), " IS OPEN: ", by->Name, " has finished the first gate in"));
		};
	}

	void UnitySpaceApp::SubscribeWorld(const cs::Ref<StarSystem>& s)
	{
		// A raw pointer, not the Ref: the handler lives in s's own War, and holding s from it would keep s alive forever
		// (C#'s GC collects the cycle). Compared by identity only, as C#'s `s == ActiveSystem`.
		StarSystem* system = s.get();
		s->World->War->BattleStarted += [this, system](cs::Ref<Battle> b)
		{
			if (system == ActiveSystem.get()) ShowToast(cs::Cat(cs::Strings::ToUpperInvariant(b->Name), " has begun"));
		};
	}

	void UnitySpaceApp::Activate(const cs::Ref<StarSystem>& system)
	{
		// Let go of anything in the old system first: its ships, structures and indices are about to go.
		SpaceCamera->ExitFocus();
		if (systemRoot != nullptr)
		{
			Rendering::Materials::DestroyOwned(systemRoot);
			GameObject::Destroy(systemRoot->gameObject());
		}
		ActiveSystem = system;
		// The view to go back to on leaving the helm (RememberCommand) was the old system's: its body and ship indices mean
		// nothing here (US-T113: released after a gate jump or an FTL drop-out it focused out of range). Only a hull that
		// came along, now in this system's fleet, is still there to look at.
		if (!(commandFocus.IsShip() && commandFocus.Ship && Fleet()->IsPresent(commandFocus.Ship))) commandFocus = Target::None();
		// The system the player is in runs at full detail, the rest at coarse (U.SPC-T104): this one is brought
		// up to the moment from its last coarse step before anything of it is drawn.
		Galaxy->Promote(system, Clock->Jd);
		// The galaxy is charted and built about wherever the player is (U.SPC-T413).
		Galaxy->Viewpoint = system->Origin;
		systemRoot = NewGameObject(system->Name)->transform;
		systemRoot->SetParent(worldRoot, false);
		SystemVisible = true;

		int n = Catalog()->Count();
		bodyScene = cs::NewArray<DVec3>(n);
		bodyRadius = cs::NewArray<double>(n);
		bodyViews = cs::NewArray<cs::Ref<Views::BodyView>>(n);
		orbitLines = cs::NewArray<cs::Ref<Views::LineStrip>>(n);
		for (int i = 0; i < n; i++)
		{
			bodyViews[i] = cs::New<Views::BodyView>((*Catalog())[i], systemRoot);
			if ((*Catalog())[i]->ParentIndex >= 0) orbitLines[i] = cs::New<Views::LineStrip>(cs::Cat("Orbit ", (*Catalog())[i]->Name), systemRoot);
		}
		shipViews = cs::List<cs::Ref<Views::ShipView>>(); // C#: null (the old views went with systemRoot)
		// US-T131: the place's graveyards are laid the first time a player comes (their hulks are wrecks in its fleet).
		Core::Graveyards::Populate(World(), Clock->Jd);
		BuildShipViews();
		pathLine = cs::New<Views::LineStrip>("Flight path", systemRoot);
		pathLine->SetVisible(false);
		battleFx = cs::New<Views::BattleFx>(this, systemRoot);
		debrisFields = cs::New<Views::DebrisFields>(this, systemRoot);
		podBeacons = cs::New<Views::PodBeacons>(this, systemRoot);
		bioMassView = cs::New<Views::BioMassView>(this, systemRoot);
		embodiedShine = cs::New<Views::EmbodiedShine>(this, systemRoot); // US-T136
		discView = cs::New<Views::DiscView>(this, systemRoot);
		graveyardView = cs::New<Views::GraveyardView>(this, systemRoot); // US-T131
		harvesterView = cs::New<Views::HarvesterView>(this, systemRoot); // US-T146
		spaceFeaturesView = cs::New<Views::SpaceFeaturesView>(this, systemRoot); // US-T130
		fighterViews = cs::New<Views::FighterViews>(this, systemRoot);
		laneView = cs::New<Views::LaneView>(this, systemRoot);
		gateView = cs::New<Views::GateView>(this, systemRoot);
		embeddedPods = cs::New<Views::EmbeddedPods>(this, systemRoot);
		platformViews.Clear();
		entryGlows.Clear();
		Structures = cs::New<Views::StructureViews>(this, systemRoot);
		swarm = cs::New<Views::SatelliteSwarm>(this, systemRoot);
		if (AsteroidBelt != nullptr) AsteroidBelt->enabled = IsSol();

		viewFaction = cs::List<cs::Ref<Faction>>();
		lastBattleScene = cs::List<DVec3>();
		lastDrawnValid = cs::List<bool>();
		battleTimeFor = nullptr;
		Selected = Target::None();
		Hovered = Target::None();
		Scale()->Layout(Ephemeris(), bodyScene);
		UpdateRadii();
		UpdateShips(0);
		Structures->Layout();
		// A new system starts level with the ecliptic again (auto-reorient then swings to its own plane).
		SpaceCamera->SetUpFrame(Quaternion::identity());
		SpaceCamera->SetPose(SpaceCamera->ScenePosition, SpaceCamera->Rotation);
		// Each system keeps its own sky (U.SPC-T595); the last one stays up until this one has loaded.
		if (Sky != nullptr) Sky->Show(system->Name);
		CoopActivated(system); // US-T44: the host's frame tells its clients
	}

	void UnitySpaceApp::SetSystemVisible(bool visible)
	{
		SystemVisible = visible;
		if (systemRoot != nullptr) systemRoot->gameObject()->SetActive(visible);
	}

	void UnitySpaceApp::TravelTo(const cs::Ref<StarSystem>& system)
	{
		if (system == nullptr || system == ActiveSystem || Travel->Active) return;
		if (CoopRefusesTravel(system)) return; // US-T44 (US-T39/US-T43): in co-op the party travels together, or not while another player is at the controls
		// Already on the way somewhere under the hull's own drive (U.SPC-T417).
		if (InFtlCruise()) { ShowToast(cs::Cat("Under FTL for ", FtlCruise->ToName, ": the view goes where the hull goes")); return; }
		// The pilot session belongs to this system's ship and world: the gate is the way across while flying.
		if (IsSeated()) ReturnToCommand();
		// At the helm the trip is a plotted course, flown gate by gate by the pilot (U.SPC-T516).
		if (IsPiloting()) { PlotCourse(system); return; }
		// A ship followed in the god view is sent on the course and the view goes with it (U.SPC-T516); no ship, the view's own trip.
		if (TrySendFollowedShip(system)) return;
		if (CoopPartyTrip(system)) return; // US-T44 (US-T43): the trip goes into the frames; the host's view makes it, every peer arrives
		Select(Target::None());
		Travel->Begin(system);
	}

	void UnitySpaceApp::UpdateRadii()
	{
		// The barycentre is drawn as nothing but still needs a size to frame the system around it.
		for (int i = 0; i < Catalog()->Count(); i++) bodyRadius[i] = (*Catalog())[i]->IsVisible() ? Scale()->DisplayRadius((*Catalog())[i]) : 1.0;
	}

	void UnitySpaceApp::SetupCamera()
	{
		// Camera.main always exists here: the one camera the host actor (AUnitySpaceAppActor) applies to Unreal's view,
		// so the C#'s "make a Main Camera if the scene has none" branch has nothing to do.
		UnityCamera = Camera::main();
		UnityCamera->transform->SetPositionAndRotation(Vector3::zero(), Quaternion::identity());
		UnityCamera->clearFlags = CameraClearFlags::SolidColor;
		UnityCamera->backgroundColor = Color::black();
		UnityCamera->fieldOfView = 60.0f;
		UnityCamera->nearClipPlane = 0.1f;
		UnityCamera->farClipPlane = 5e8f;
		// Whatever the pipeline needs of its camera (HDR off, its per-camera data) and no post-processing yet.
		auto backend = Rendering::RenderBackend::Active();
		backend->SetupCamera(UnityCamera, Rendering::CameraRole::Main);
		backend->SetMsaa(UnityCamera, EngineSettings::Msaa()->Value);
		backend->SetPostProcessing(UnityCamera, Rendering::PostProcessLook::None());
		// The camera's AudioListener is switched on in Unity; Unreal's listener is the player controller's, which follows
		// the view target (the app's camera), so there is nothing to enable.
	}

	bool UnitySpaceApp::SyncShipViews()
	{
		const auto& ships = Fleet()->Ships();
		int have = shipViews.Count();
		if (ships.Count() == have) return false;
		ResizeArray(shipViews, ships.Count());
		ResizeArray(shipScene, ships.Count());
		ResizeArray(shipRotation, ships.Count());
		ResizeArray(shipLength, ships.Count());
		ResizeArray(shipStates, ships.Count());
		ResizeArray(shipRotationInitialised, ships.Count());
		ResizeArray(shipBlendFrom, ships.Count());
		ResizeArray(shipBlendT, ships.Count());
		ResizeArray(shipLastBattle, ships.Count());
		ResizeArray(wreckStart, ships.Count());
		// Their models are built when they are wanted (U.SPC-T414, see UpdateStreaming), not here.
		for (int i = have; i < ships.Count(); i++)
		{
			shipBlendT[i] = 1;
			wreckStart[i] = -1;
		}
		return true;
	}

	void UnitySpaceApp::BuildShipViews()
	{
		for (const auto& v : shipViews)
		{
			if (v == nullptr) continue;
			Rendering::Materials::DestroyOwned(v->Root->transform);
			GameObject::Destroy(v->Root);
		}
		const auto& ships = Fleet()->Ships();
		shipViews = cs::NewArray<cs::Ref<Views::ShipView>>(ships.Count());
		shipScene = cs::NewArray<DVec3>(ships.Count());
		shipRotation = cs::NewArray<Quaternion>(ships.Count());
		shipLength = cs::NewArray<double>(ships.Count());
		shipStates = cs::NewArray<ShipState>(ships.Count());
		shipRotationInitialised = cs::NewArray<bool>(ships.Count());
		shipBlendFrom = cs::NewArray<DVec3>(ships.Count());
		shipBlendT = cs::NewArray<float>(ships.Count());
		shipLastBattle = cs::NewArray<cs::Ref<Battle>>(ships.Count());
		wreckStart = cs::NewArray<float>(ships.Count());
		// No model is built on entering a system (U.SPC-T414): each is streamed in by UpdateStreaming as it is
		// wanted, a few a frame, and drawn as its icon until then.
		for (int i = 0; i < ships.Count(); i++)
		{
			shipBlendT[i] = 1;
			wreckStart[i] = ships[i]->Destroyed ? cs::Single::MaxValue : -1;
		}
	}

	void UnitySpaceApp::OnDestroy()
	{
		if (Sky != nullptr) Sky->Dispose();
		EndSkyTravel(); // US-T145
		Rendering::Materials::DestroyAllOwned();
		if (hud != nullptr) hud->DestroyOwnedTextures();
		pauseMenu->DestroyOwnedTextures();
		DestroyCommsTextures(); // US-T137
		cheatMenu->DestroyOwnedTextures(); // US-T126
		Tour::ShowcaseTour::Abandon(); // US-T147: a tour ends with its game
		Tour::ShowcaseTour::DestroyOwnedTextures();
	}

	void UnitySpaceApp::Update()
	{
		float dt = Mathf::Min(Time::unscaledDeltaTime, 0.1f);
		bool overUi = IsPointerOverUi();
		Input.Poll(overUi, ModelViewer::CapturesInput());
		CaptureEvents::Hotkeys(Input); // US-T72: Alt+F9/F10/F11 while capture runs, on this machine only
		// The galaxy map turns with its own drag: the camera behind it does not look about meanwhile.
		if ((ShowGalaxyMap || ShowHelp) && !ScreenCleared) Input.ReleaseLook();
		// The pause menu has the frame: nothing else moves, and the stick and trigger do nothing, until it closes.
		if (pauseMenu->IsOpen)
		{
			pauseMenu->Update(Input.Escape, Time::unscaledDeltaTime);
			FreeCursorForMenu();
			return;
		}
		// US-T126: the dev menu (Shift+F1) comes next in the order: while it is open the keys and the mouse are its, and the
		// craft and the camera get an idle frame (the simulation runs on).
		if (UpdateCheatMenu()) Input.Handover();
		if (UpdateComms()) Input.Handover(); // US-T137: the comms panel (Shift+H) has the keys while open; US-T138: the held pilot
		UpdateInfluence(); // US-T140: the embody gate's world, this frame
		if (Tour::ShowcaseTour::Update(*this, !cheatMenu->IsOpen)) Input.Handover(); // US-T147: the showcase tour has the keys while it runs
		ArbitrateInput(true); // US-T90: one owner per input this frame (the craft held, else the command view)
		int screensOpen = OpenScreens();
		HandleCommands();
		ArbitrateInput(false); // US-T90: a seat taken or let go just now: the new owner starts on an idle frame
		// A panel or screen opened while the screen is cleared (F11) brings the UI back, rather than opening unseen.
		if (ScreenCleared && (OpenScreens() & ~screensOpen) != 0) ScreenCleared = false;
		if (pauseMenu->IsOpen)
		{
			FreeCursorForMenu();
			return;
		}

		// The hull flies in real seconds while the battle it is in steps in sim seconds (U.SPC-T555): under warp the
		// AI units would outrun it and the swept collision test (which assumes real-dt motion) would miss them, so
		// time is held at real speed for as long as the piloted hull is in a battle ('.' is refused at the helm).
		if (!Piloted.IsNone() && Piloted.Ship != nullptr && Piloted.Ship->Battle != nullptr && Clock->WarpIndex > 0) // US-T44 (US-T43): was IsPiloting() (any player's helm holds the shared clock)
		{
			Clock->SetWarp(0);
			ShowToast("Time warp is held at real time while the helm is in a battle");
		}

		double raceSim = Race::Probe::Now(); // US-T46: the race's simulation time (docs/RACE.md)
		CoopAdvance(dt); // US-T44: Clock->Advance(dt), in lockstep with the host in co-op
		Galaxy->Update(Clock->Jd);
		Race::Probe::SimMs += Race::Probe::Now() - raceSim; // US-T46
		RefreshCharts();
		UpdateSaves(dt);
		SyncShipViews();

		AnimateScale(dt);
		Scale()->Layout(Ephemeris(), bodyScene);
		UpdateRadii();
		UpdateDogfightDemo(); // US-T88: the demo the game was launched for (-dogfight, the main menu's button)
		CoopUpdatePiloting(dt); // US-T44 (US-T43): UpdatePiloting(dt), on the helm holder's frame of input on every peer in co-op
		UpdateSeat(dt);
		ShowLawNotices();
		UpdateShips(dt);
		Structures->Layout();
		raceSim = Race::Probe::Now(); // US-T46
		CoopUpdateAdventures(!Travel->Active && !InFtlCruise()); // US-T44: was `if (that) World()->UpdateAdventures(Clock->Jd, PlayerHelioKm());`, the host's call in co-op
		Race::Probe::SimMs += Race::Probe::Now() - raceSim; // US-T46

		CoopShared([this] { DropLostTargets(); }); // US-T44 (US-T43): DropLostTargets(); a helm lost is lost on every peer
		BattleTime();
		CoopShared([this] { UpdateCourseShip(); }); // US-T44 (US-T43): UpdateCourseShip(); the course is the shared helm's on every peer
		if (Travel->Active) Travel->Update(dt);
		// Between the stars under the hull's own drive (U.SPC-T417): the view goes with it.
		else if (InFtlCruise()) UpdateFtlCruiseView(dt);
		else
		{
			KeepReoriented();
			// US-T90: at the controls the camera follows the craft and takes none of its keys (the wheel zooms the chase).
			SpaceCamera->Update(dt, AtControls() ? Input.ForChaseCamera() : Input);
		}
		UpdateClipPlanes(dt);
		UpdateStreaming(dt);
		Sky->Update(dt);
		UpdateSkyTravel(dt); // US-T145
		UpdateViews(dt);
		UpdateHover();
		HandleSelectionClicks();

		ToastAge += dt;
		bool lockCursor = (Input.LookHeld || (AtControls() && !ClickPanelOpen())) && !ModelViewer::CapturesInput() && !cheatMenu->IsOpen; // US-T126
		if (commsOpen) lockCursor = false; // US-T137: the comms panel takes clicks
		Cursor::lockState = lockCursor ? CursorLockMode::Locked : CursorLockMode::None;
		Cursor::visible = !lockCursor;
	}

	void UnitySpaceApp::FreeCursorForMenu()
	{
		Cursor::lockState = CursorLockMode::None;
		Cursor::visible = true;
	}

	void UnitySpaceApp::DropLostTargets()
	{
		if (Selected.IsShip() && Selected.Ship->Destroyed) Select(Target::None());
		if (Hovered.IsShip() && Hovered.Ship->Destroyed) Hovered = Target::None();
		if (IsPiloting() && Piloted.Ship->Destroyed) ExitPiloting();
		else if (IsPiloting() && World()->PilotedShip != Piloted.Ship) LoseHelm();
		else if (SpaceCamera->FocusTarget.IsShip() && SpaceCamera->FocusTarget.Ship->Destroyed) SpaceCamera->ExitFocus();
	}

	void UnitySpaceApp::BattleTime()
	{
		auto t = SpaceCamera->FocusTarget;
		auto battle = t.IsShip() ? t.Ship->Battle : nullptr;
		if (battle == nullptr || battle == battleTimeFor) return;
		EnterBattleTime(battle);
	}

	void UnitySpaceApp::EnterBattleTime(const cs::Ref<Battle>& battle)
	{
		battleTimeFor = battle;
		if (Clock->WarpIndex > 0)
		{
			Clock->SetWarp(0);
			ShowToast(cs::Cat("BATTLE TIME: ", battle->Name, ", ", Clock->Warp().Label, "  (. to speed up)"));
		}
	}

	cs::Ref<Battle> UnitySpaceApp::BiggestBattle()
	{
		cs::Ref<Battle> best = nullptr;
		int bestCount = 0;
		for (const auto& b : World()->War->Battles)
		{
			int n = 0;
			for (const auto& u : b->Units)
				if (u->Present()) n++;
			if (n > bestCount)
			{
				bestCount = n;
				best = b;
			}
		}
		return best;
	}

	void UnitySpaceApp::WatchBattle(const cs::Ref<Battle>& battle)
	{
		cs::Ref<Ship> flag = nullptr;
		for (const auto& u : battle->Units)
			if (u->Ship != nullptr && u->Present() && (flag == nullptr || u->Combat->Spec->Hull > flag->Combat->Spec->Hull)) flag = u->Ship;
		if (flag == nullptr) return;
		auto t = Target::OfShip(flag);
		Select(t);
		SpaceCamera->FocusOn(t, 30, 25.0f);
		EnterBattleTime(battle);
	}

	void UnitySpaceApp::OnGUI()
	{
		// The model viewer has the screen: the game's HUD would paint over the model and its buttons
		// (travel, Now, Focus...) would still act on the hidden game.
		if (ModelViewer::CapturesInput()) return;
		// Under the pause menu the HUD is shown but cannot be clicked.
		bool menu = pauseMenu->IsOpen;
		if (menu) GUI::enabled = false;
		hud->Draw();
		DrawComms(); // US-T137: the comms panel, over the HUD
		DrawSystemCruise(); // US-T127: the cruise drive's readout
		DrawInfluence(); // US-T140: the Influence meter, the embody gate's refusals, the squad's line
		Tour::ShowcaseTour::Draw(*this); // US-T147: the showcase tour's strip
		DrawTractorHud(); // US-T95: the tractor beam's readout
		cheatMenu->Draw(*this); // US-T126: the dev menu, or the HUD's hint for its key
		GUI::enabled = true;
		if (menu) pauseMenu->Draw();
	}

	bool UnitySpaceApp::IsPointerOverUi()
	{
		// US-T70: the pointer through Input's device (the real mouse, or the co-op smoke's / the audit's script), as both C#
		// copies read it (SpaceGod.Unity.CSharp ad968a1). It was the GUI host's Slate mouse, so a scripted run tested the
		// scripted pointer's clicks against wherever the real cursor was.
		const Input::InputDevice& mouse = Input::InputDevice::current();
		if (!mouse.present) return false;
		auto p = mouse.position;
		auto gui = Vector2(p.x, Screen::height - p.y);
		for (const auto& r : UiRects)
			if (r.Contains(gui)) return true;
		return false;
	}

	// ---------------------------------------------------------------- commands

	int UnitySpaceApp::OpenScreens() const
	{
		return (ShowSpawn ? 1 : 0) | (ShowSaves ? 2 : 0) | (ShowGalaxyMap ? 4 : 0) | (ShowMarket ? 8 : 0) | (ShowCargo ? 16 : 0) | (ShowNews ? 32 : 0) |
			(ShowOutfit ? 64 : 0) | (ShowBounties ? 128 : 0) | (ShowContracts ? 256 : 0) | (ShowFleet ? 512 : 0) | (ShowTrade ? 1024 : 0) | (ShowHelp ? 2048 : 0) | (ShowPicker ? 4096 : 0);
	}

	void UnitySpaceApp::HandleCommands()
	{
		auto& i = Input;
		HandleSaveCommands();
		// F11 clears the screen anywhere, between the stars too (U.SPC-T496).
		if (i.ClearScreen) ScreenCleared = !ScreenCleared;
		if (Travel->Active || InFtlCruise())
		{
			// Between the stars only time and the escape hatch work (and under the hull's own drive, U.SPC-T417, not
			// even that: there is nowhere to come out but the far star).
			if (i.Escape && Travel->Active) Travel->Finish();
			else if (i.Escape) ShowToast("Under FTL between the stars: the drive drops out at the far star");
			if (i.TimeFaster) Clock->Faster();
			if (i.TimeSlower) Clock->Slower();
			if (i.TogglePause) TogglePause();
			return;
		}
		if (i.ToggleScale) ToggleScale();
		// Being run down, time stays at the pace the chase is flown (U.SPC-T273).
		if (i.TimeFaster) TimeFaster();
		if (i.TimeSlower) Clock->Slower();
		if (i.TogglePause) TogglePause();
		if (i.ResetTime && !AtControls()) ResetToNow();
		if (i.ToggleOrbits) ShowOrbits = !ShowOrbits;
		if (i.ToggleLabels) ShowLabels = !ShowLabels;
		if (i.ToggleHelp) ShowHelp = !ShowHelp;
		if (i.ToggleHud) ShowHud = !ShowHud;
		if (i.ToggleMarket) ShowMarket = !ShowMarket;
		if (i.ToggleCargo) ShowCargo = !ShowCargo;
		if (i.ToggleOutfit) ShowOutfit = !ShowOutfit;
		if (i.ToggleBounties) ShowBounties = !ShowBounties;
		if (i.ToggleContracts) ShowContracts = !ShowContracts;
		if (i.ToggleFleet) ShowFleet = !ShowFleet;
		if (i.ToggleTrade) ShowTrade = !ShowTrade;
		if (i.ToggleGalaxyMap) ShowGalaxyMap = !ShowGalaxyMap;
		if (i.HailPolity) HailPolity();
		// At the helm X rolls the hull (Z/X): the news feed would free the cursor and take the stick and guns.
		if (i.ToggleNews && !AtControls()) ShowNews = !ShowNews;
		if (i.ToggleSpawn) ShowSpawn = !ShowSpawn;
		// Shift+V: the practice fighter at the camera, the player at its helm (U.SPC-T601).
		if (i.SpawnPractice) SpawnPracticeFighter();
		if (i.ToggleLanes) ShowLanes = !ShowLanes;
		if (i.ToggleReorient) ToggleAutoReorient();
		if (i.ToggleSatellites)
		{
			ShowSatellites = !ShowSatellites;
			ShowToast(ShowSatellites ? "SATELLITES ON: constellations drawn" : "SATELLITES OFF  (K to show)");
		}
		// Shift+K / numpad * cycles the sky of the system in view, which keeps it (U.SPC-T595).
		if (i.CycleSky != 0 && ActiveSystem != nullptr)
		{
			auto sky = Sky->Cycle(i.CycleSky);
			ShowToast(cs::Cat("SKY: ", sky->Name, "  (", ActiveSystem->Name, " keeps it; Shift+K for the next)"));
		}
		// The chase camera belongs to the pilot: nothing re-aims it at something else mid-flight.
		if (i.WatchBattle && !AtControls())
		{
			auto b = BiggestBattle();
			if (b != nullptr) WatchBattle(b);
			else ShowToast("No battles under way");
		}
		if (i.Home && !AtControls()) FocusSystemOverview();
		if (i.Focus && !AtControls()) FocusSelected();
		if (i.Escape) BackOut();
		if (i.CycleShips && !AtControls()) CycleShips(i.CycleBackward ? -1 : 1);
		if (i.NumberKey >= 0 && !AtControls()) NumberHotkey(i.NumberKey);
		if (i.Pilot) TogglePiloting();
		// Take charge of anything, any time (U.SPC-T422): the next seat in the fight in view, or the next gun on the hull.
		if (i.CycleSeat != 0) CycleSeat(i.CycleSeat);
		if (i.ManGun) ManNextGun();
		if (i.EmbodyCrew) CycleWorker(); // US-T135: Shift+P, out on the hull as an EVA repair worker
		// Shift+/ (US-T106): into the newest craft the carrier launched, the next, and back to its helm.
		if (i.JumpToCraft) JumpToCraft();
		// At a gun mount (U.SPC-T445): up to the helm of the hull it is on; in a launched craft, back to its carrier's (US-T106).
		if (i.MoveToHost && IsSeated()) MoveSeatToHost();
		if (i.TogglePicker) TogglePicker();
	}

	void UnitySpaceApp::TimeFaster()
	{
		if (IsPiloting() && World()->Pursuit != nullptr && World()->Pursuit->Active()) ShowToast("Being hunted: time stays at real speed until you shake them off or fight");
		else Clock->Faster();
	}

	void UnitySpaceApp::BackOut()
	{
		switch (EscapeKey::Route(ClickPanelOpen(), AtControls(), SpaceCamera->Mode != Cameras::SpaceCamera::CameraMode::Free, !Selected.IsNone()))
		{
			case EscapeAction::ClosePanels:
				ShowSpawn = ShowSaves = ShowMarket = ShowCargo = ShowNews = ShowOutfit = ShowBounties = ShowContracts = ShowFleet = ShowTrade = ShowGalaxyMap = ShowHelp = ShowPicker = false;
				break;
			case EscapeAction::LeaveFocus:
				SpaceCamera->ExitFocus();
				break;
			case EscapeAction::Deselect:
				Select(Target::None());
				break;
			default:
				pauseMenu->Open();
				break;
		}
	}

	const cs::List<cs::String>& UnitySpaceApp::SolNumberBodies()
	{
		static const cs::List<cs::String> bodies = { "Sun", "Mercury", "Venus", "Earth", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune", "Pluto" };
		return bodies;
	}

	const cs::List<cs::String>& UnitySpaceApp::CentauriNumberBodies()
	{
		static const cs::List<cs::String> bodies =
		{
			"Alpha Centauri A", "Alpha Centauri B", "Proxima Centauri", "Proxima b", "Proxima d", "Proxima c",
			"Alpha Centauri Ab", "Rigil Gate", "Toliman Forge", "Blackhollow",
		};
		return bodies;
	}

	cs::List<cs::String> UnitySpaceApp::NumberBodies()
	{
		if (IsSol()) return SolNumberBodies();
		if (ActiveSystem->Name == "Alpha Centauri") return CentauriNumberBodies();
		cs::List<cs::String> names;
		for (const auto& b : Catalog()->Bodies())
			if (b->IsVisible() && names.Count() < 10) names.Add(b->Name);
		return names;
	}

	void UnitySpaceApp::NumberHotkey(int key)
	{
		auto names = NumberBodies();
		cs::Ref<BodyDefinition> body;
		if (key >= names.Count() || !Catalog()->TryGet(names[key], body)) return;
		auto target = Target::OfBody(body->Index);
		bool repeat = key == lastNumberKey && Time::unscaledTime - lastNumberTime < 0.45f;
		Select(target);
		if (repeat || SpaceCamera->Mode != Cameras::SpaceCamera::CameraMode::Free) SpaceCamera->FocusOn(target);
		lastNumberKey = key;
		lastNumberTime = Time::unscaledTime;
	}

	void UnitySpaceApp::CycleShips(int direction)
	{
		cs::List<cs::Ref<Ship>> candidates;
		for (const auto& s : Fleet()->Ships())
			if (!s->IsEscort() && !s->Destroyed && s->Phase != ShipPhase::Landed) candidates.Add(s);
		if (candidates.Count() == 0) return;
		int current = Selected.IsShip() ? candidates.IndexOf(Selected.Ship->Controller()) : -1;
		// With nothing selected, forward starts at the first ship and backward at the last.
		int next = current < 0 ? (direction > 0 ? 0 : candidates.Count() - 1)
			: ((current + direction) % candidates.Count() + candidates.Count()) % candidates.Count();
		Select(Target::OfShip(candidates[next]));
		if (SpaceCamera->Mode != Cameras::SpaceCamera::CameraMode::Free) SpaceCamera->FocusOn(Selected);
	}

	void UnitySpaceApp::Select(const Target& target)
	{
		Selected = target;
	}

	void UnitySpaceApp::FocusSelected()
	{
		// The chase camera belongs to the pilot (the HUD's Focus button lands here too), or whoever holds a seat.
		if (AtControls()) return;
		if (Selected.IsNone())
		{
			if (SpaceCamera->Mode != Cameras::SpaceCamera::CameraMode::Free) SpaceCamera->ExitFocus();
			return;
		}
		bool refocus = SpaceCamera->Mode == Cameras::SpaceCamera::CameraMode::Focus && SpaceCamera->FocusTarget == Selected;
		SpaceCamera->FocusOn(Selected, refocus ? Cameras::SpaceCamera::DefaultFraming(Selected) : cs::Double::NaN);
	}

	// ---------------------------------------------------------------- piloting

	void UnitySpaceApp::TogglePiloting()
	{
		// At any seat, P hands it back to its AI and returns to command, the view and time warp it was taken from (U.SPC-T422).
		if (AtControls())
		{
			ReturnToCommand();
			return;
		}
		auto t = Selected.IsShip() ? Selected : Hovered.IsShip() ? Hovered : Target::None();
		if (!t.IsShip())
		{
			// US-T135: P is the one embody key: anything else selected (a hull or station in a fight) is embodied too.
			if (!EmbodyTarget()) ShowToast("Select something to embody: a ship, a hull or station in a fight (F9 lists every seat near you)");
			return;
		}
		// Only a wreck, a derelict or a prize already taken in a fight still on is out of reach (U.SPC-T18).
		auto helm = Embodiables::HelmOf(World(), t.Ship);
		if (!helm->CanEmbody())
		{
			ShowToast("Can't take the helm of that ship right now");
			return;
		}
		if (!PassesEmbodyGate(helm)) return; // US-T135: the one embody gate
		RememberCommand();
		EnterPiloting(t);
	}

	void UnitySpaceApp::PinWherePiloted(const cs::Ref<Ship>& ship) { Core::Fleet::PinAt(ship, pilotFrameBody, pilotOffsetKm(), pilotForward(), Clock->Jd); }

	std::tuple<int, DVec3> UnitySpaceApp::CurrentFrame(const cs::Ref<Ship>& ship, double jd) { return ShipHelm::FrameOf(World(), ship, jd); }

	void UnitySpaceApp::EnterPiloting(const Target& t)
	{
		if (CoopRoutesHelmTake(t)) return; // US-T44 (US-T39/US-T43): in co-op a take goes into the frames, and every peer takes it there
		auto ship = t.Ship;
		Piloted = t;
		// A fresh helm: no drives-down timer left over from the hull flown before (spawned or loaded in the same frame).
		pilotEmpSeconds = 0;
		// Where it is (and which way it faces) before an escort is cut loose from the leader it shares them with.
		int frame;
		DVec3 offsetKm;
		std::tie(frame, offsetKm) = CurrentFrame(ship, Clock->Jd);
		auto facing = Fleet()->StateOf(ship, Clock->Jd).Facing;
		auto velocity = DVec3::Zero;
		// Riding a lane through to its far ramp: the ride goes on, now at the helm (U.SPC-T41). One
		// that means to stop part-way (a raider's lurk) is taken where it is, as any other flight.
		bool riding = !ship->IsEscort() && ship->Battle == nullptr && ship->Phase == ShipPhase::Lane && ship->LaneEndProgress >= 1;
		if (riding)
		{
			frame = ship->LaneLeg.From;
			offsetKm = Fleet()->StateOf(ship, Clock->Jd).HelioKm - Ephemeris()->Helio(frame);
		}
		// The helm is the player's (U.SPC-T18): the fleet lets go, an escort is cut loose from its formation (the war's
		// command handed over otherwise), and a hull already fighting has its own battle unit handed over.
		Embodiables::HelmOf(World(), ship)->TakeCharge();
		if (ship->Battle != nullptr)
		{
			auto unit = ship->Battle->UnitOf(ship);
			if (unit != nullptr)
			{
				// Mid-fight, the hull is where the battle has it, not where its schedule would put it:
				// the pilot loop writes this pose back every frame (pilotOffsetKm - Anchor), so seed it from there.
				frame = ship->Battle->Body;
				offsetKm = unit->Pos + ship->Battle->Anchor;
				facing = unit->Forward;
				velocity = unit->Vel;
			}
		}
		pilotFrameBody = frame;
		SeedPilot(offsetKm, velocity, facing);
		// Mid-fight the hull keeps its roll: the pilot's roof is the unit's (TakeCharge stored it in Up), not the camera's.
		if (ship->Battle != nullptr)
			if (auto rolledUnit = ship->Battle->UnitOf(ship))
				if (rolledUnit->Up.LengthSquared() > 1e-12) pilot().Up = rolledUnit->Up.Normalized();
		// A tanker called for some earlier hull (one taken from the player, say) is not this one's.
		TankerDueS = -1;
		propellantWarning = 0;
		pilotFlightAssistOff = false;
		pilotBattleCheckTimer = 0;
		demandPrompted = false;
		termsPrompted = nullptr;
		havenPrompted = nullptr;
		// Out of a battle, the hull is the pilot's from here: no longer landed, descending or in formation.
		// In one, the battle owns its pose, but an escort cut loose still needs an orbit of its own.
		rideWarpBefore = -1;
		insideRampGate = true;
		laneRejoinable = false;
		if (!riding && (ship->Battle == nullptr || ship->Orbit == nullptr)) PinWherePiloted(ship);

		if (CoopHelmMine()) Select(t); // US-T44 (US-T43): the holder's view (every peer flies the helm, one watches from it)
		if (CoopHelmMine()) SpaceCamera->EnterPilotChase(t, 6); // US-T44
		Clock->SetWarp(0);
		ShowToast(cs::Cat("Piloting ", ship->Name, ", ", Clock->Warp().Label, "  (", HelmControls::Of(ship).Hint(), "; P to release, . to speed up, Left-click to fire, T to lock a target, Z/X to roll, F flight assist, hold J to cruise)"));
		if (riding)
		{
			BeginRide(ship, false);
			ShowToast(cs::Cat("Riding the ", ship->LaneLeg.Lane->Name, " lane at the helm of ", ship->Name, ", bound for ", Fleet()->Lanes->EndName(ship->LaneLeg.Lane, ship->LaneLeg.To),
				"  (number keys pick the off-ramp's exit, P to release)"));
		}
	}

	void UnitySpaceApp::SpawnShip(ShipClass cls, bool hostile)
	{
		RememberCommand();
		ReleaseSeat();
		if (IsPiloting()) ExitPiloting();

		// Where the camera is now: its free-flight frame goes stale while it is focused on something far off.
		int frame = SpaceCamera->FrameAt(SpaceCamera->ScenePosition);
		auto offsetKm = Scale()->InverseLocalOffset((*Catalog())[frame], SpaceCamera->ScenePosition - BodyScene(frame));
		auto forwardUnity = SpaceCamera->Rotation * Vector3::forward();
		auto forward = DVec3(forwardUnity.x, forwardUnity.y, forwardUnity.z).SwapYZ();
		if (CoopSpawns("spawn", cs::Cat(static_cast<int>(cls), " ", hostile ? 1 : 0), frame, offsetKm, forward)) return; // US-T44 (US-T43): the rest on every peer (CoopSpawnShip)

		auto faction = hostile ? World()->Pirates : World()->Sol;
		int spawned = ++spawnedCount;
		cs::String name = cs::Cat("Sandbox ", cls, " ", spawned);
		auto ship = Fleet()->SpawnAt(name, cls, faction, frame, offsetKm, forward, Clock->Jd);
		ship->Ports = World()->FreeShipPorts(); // not a hidden haven's rock, as for every free ship

		EnterPiloting(Target::OfShip(ship));
	}

	DVec3 UnitySpaceApp::PlayerHelioKm()
	{
		if (IsPiloting()) return Ephemeris()->Helio(pilotFrameBody) + pilotOffsetKm();
		// The camera's free-flight frame goes stale while it is focused on something far off.
		int frame = SpaceCamera->FrameAt(SpaceCamera->ScenePosition);
		return Ephemeris()->Helio(frame) + Scale()->InverseLocalOffset((*Catalog())[frame], SpaceCamera->ScenePosition - BodyScene(frame));
	}

	bool UnitySpaceApp::OpportunityScene(const cs::Ref<Opportunity>& op, DVec3& scene)
	{
		scene = DVec3();
		if (op->Ship != nullptr && !op->Ship->Destroyed && op->Ship->Id < shipScene.Count()) scene = shipScene[op->Ship->Id];
		else if (op->Wreck != nullptr) scene = WreckScene(op->Wreck);
		else if (op->Pod != nullptr) scene = PodScene(op->Pod);
		else if (op->Body >= 0 && op->Body < bodyScene.Count()) scene = bodyScene[op->Body] + Scale()->LocalOffset((*Catalog())[op->Body], op->OffsetKm);
		else return false;
		return true;
	}

	void UnitySpaceApp::SeedPilot(DVec3 offsetKm, DVec3 velocityKmS, DVec3 facing)
	{
		pilot() = PilotState::At(offsetKm, velocityKmS, facing);
		auto upUnity = (CoopUpFrame.has_value() ? *CoopUpFrame : SpaceCamera->UpFrame) * Vector3::up(); // US-T44 (US-T43): the helm holder's frame in co-op
		auto upReference = DVec3(upUnity.x, upUnity.y, upUnity.z).SwapYZ();
		auto level = upReference - pilot().Forward * DVec3::Dot(upReference, pilot().Forward);
		if (level.LengthSquared() > 1e-6) pilot().Up = level.Normalized();
		ResetPilotAim();
	}

	void UnitySpaceApp::LoseHelm()
	{
		StopFtl();
		EndSalvage(cs::String());
		SalvageApproach = cs::String();
		auto ship = Piloted.Ship;
		// Where it was taken: the captor's schedule (or the drifting hulk) picks up from here. If the
		// capture ended the fight in the same tick, the war has already planned the prize's next trip from
		// its old spot: plan it again from here, or the pin would hold it until that trip's departure time.
		EndRide();
		PinWherePiloted(ship);
		if (ship->Battle == nullptr && !ship->Destroyed && !ship->Derelict && !ship->IsEscort()) Fleet()->ScheduleNext(ship, Clock->Jd);
		Piloted = Target::None();
		pilotLockTarget = Target::None();
		pilotLockUnit = nullptr;
		pilotBoosting = false;
		SetpilotCruise(PilotCruiseState());
		// A trigger held as the helm was taken is not still held by the next hull flown.
		helmTriggerWas = false;
		if (CoopHelmMine()) SpaceCamera->PilotLockTarget = Target::None(); // US-T44 (US-T43): the holder's view only
		if (CoopHelmMine()) SpaceCamera->ExitFocus(); // US-T44
		ShowToast(cs::Cat("The ", ship->Name, " has been boarded and taken"));
	}

	void UnitySpaceApp::ExitPiloting()
	{
		if (CoopRoutesHelmRelease()) return; // US-T44 (US-T43): a release the player asks for goes into the frames in co-op
		// A charge stops where it is (U.SPC-T417): the crew does not jump on its own.
		StopFtl();
		EndSalvage(cs::String());
		SalvageApproach = cs::String();
		TankerDueS = -1;
		propellantWarning = 0;
		auto ship = Piloted.Ship;
		Piloted = Target::None();
		// The crew takes the helm back (U.SPC-T18): a hull still fighting has its battle unit handed back to its own AI,
		// which picks up right where manual control left off, same facing and velocity.
		if (ship != nullptr) Embodiables::HelmOf(World(), ship)->HandBack();
		World()->PilotedShip = nullptr;
		// Nobody runs down a ship no one flies: the chase ends with the helm, even if another is taken this frame.
		if (World()->Pursuit != nullptr && World()->Pursuit->Active()) World()->Pursuit->ReleaseAll(Clock->Jd);
		pilotEmpSeconds = 0;
		pilotLockTarget = Target::None();
		pilotLockUnit = nullptr;
		pilotBoosting = false;
		SetpilotCruise(PilotCruiseState());
		if (CoopHelmMine()) SpaceCamera->PilotLockTarget = Target::None(); // US-T44 (US-T43): the holder's view only
		bool rode = ship != nullptr && !ship->Destroyed && ship->Battle == nullptr && ship->Phase == ShipPhase::Lane;
		EndRide();
		// Mid-ride the crew rides it on: the fleet brings it off at the far ramp and on into that system.
		if (ship != nullptr && !ship->Destroyed && !rode)
		{
			if (ship->Battle != nullptr)
			{
				// Its crew has the fight back (above); its schedule picks up from here when the fight is over, not from wherever
				// the helm was taken (the battle's own pose doesn't move the ship's orbit).
				PinWherePiloted(ship);
			}
			else
			{
				// Pin the hull exactly where the player left it (facing included) before the AI
				// schedule resumes -- otherwise it picks up from the stale pre-piloting orbit and
				// the ship snaps back to wherever that orbit would have drifted to on its own.
				PinWherePiloted(ship);
				Fleet()->ScheduleNext(ship, Clock->Jd);
			}
		}
		if (CoopHelmMine()) SpaceCamera->ExitFocus(); // US-T44 (US-T43): the holder's view only
		// A trigger held as the helm was let go is not still held by the next hull taken.
		helmTriggerWas = false;
		// A hull shot to pieces under the helm ends the session as a loss, not as a release the player chose.
		ShowToast(ship != nullptr && ship->Destroyed ? cs::Cat("The ", ship->Name, " has been destroyed") : cs::String("Released manual control"));
	}

	bool UnitySpaceApp::CanTradeNow(const cs::Ref<Settlement>& s) { return NearPort(s) && Economy()->CanTrade(World()->PilotedShip, s); }

	bool UnitySpaceApp::NearPort(const cs::Ref<Settlement>& s)
	{
		return IsPiloting() && World()->PilotedShip != nullptr && pilotFrameBody == s->Body && pilotOffsetKm().Length() <= ScaleModel::NeighbourhoodKm((*Catalog())[s->Body]);
	}

	void UnitySpaceApp::ShowLawNotices()
	{
		// One toast slot: join the law's and the trade desk's lines so neither overwrites the other.
		cs::List<cs::String> all;
		if (World()->Law != nullptr)
		{
			auto notices = World()->Law->TakeNotices();
			all.AddRange(notices);
		}
		// The trade desk's news for the player: charters ending, claims paid, customs, a rank risen (U.SPC-T426).
		if (World()->Trade != nullptr)
		{
			auto trade = World()->Trade->TakeNotices();
			all.AddRange(trade);
		}
		if (all.Count() == 0) return;
		// US-T121: nor overwrites a toast raised earlier this same frame (it has not aged yet). A gate jump's
		// "Jumped to ..." is raised in UpdatePiloting, and Activate's Galaxy.Promote steps the far system's world up to
		// the moment (its trade desk reckoning the house's rank, its law's books) just before this drains them: the
		// jump's own line was replaced before it was ever drawn. It leads; the news follows it in the one slot.
		if (!Toast.empty() && ToastAge <= 0) all.Insert(0, Toast);
		ShowToast(cs::Strings::Join("   \xC2\xB7   ", all)); // "   ·   "
	}

	bool UnitySpaceApp::UpdateLawHail(const cs::Ref<Ship>& ship)
	{
		auto hail = World()->Law != nullptr ? World()->Law->Hail : nullptr;
		if (hail == nullptr || !hail->Interdiction) return false;
		if (Input.NumberKey == 1)
		{
			double paid = 0;
			if (World()->Law->PayFine(ship, hail->By, Clock->Jd, paid))
				ShowToast(cs::Cat("Paid ", Num(paid, "N0"), " cr in fines to ", hail->By->Name, ": you are free to go"));
			else ShowToast(cs::Cat("Not enough credits to pay the fine of ", Num(hail->Fine, "N0"), " cr"));
		}
		return true;
	}

	void UnitySpaceApp::UpdatePiloting(float dt)
	{
		// Set again below once the hull has flown this frame; a lane ride or a lost helm leaves the chase on the hull.
		SpaceCamera->PilotView = std::nullopt;
		UpdateVoidRaiderNews(); // US-T124: what is learned of the Void Raiders comes as news
		// The hull out between the stars under its own drive (U.SPC-T417): the helm is taken again where it drops out.
		if (InFtlCruise())
		{
			UpdateFtlCruise();
			if (!IsPiloting()) return;
		}
		if (!IsPiloting())
		{
			pilotBoosting = false;
			SetpilotCruise(PilotCruiseState());
			nearGate = nullptr;
			// Nobody runs down a ship no one is flying: the chasers go back to their business.
			if (World()->Pursuit != nullptr && World()->Pursuit->Active()) World()->Pursuit->ReleaseAll(Clock->Jd);
			pilotEmpSeconds = 0;
			if (World()->Sensors != nullptr) World()->Sensors->UpdatePilot(nullptr, DVec3(), false, Clock->Jd);
			// A patrol's hail ends with the helm, so it is not answered later by whatever hull is flown next.
			if (World()->Law != nullptr) World()->Law->UpdatePilot(nullptr, 0, DVec3(), DVec3(), Clock->Jd);
			return;
		}
		auto ship = Piloted.Ship;
		// Taken this very frame (the war let go of it on capture) -- even if its captors then scuttled it
		// at once: the session ends, as a loss, before anything here hands the hull back to the player.
		if (ship != nullptr && World()->PilotedShip != ship)
		{
			LoseHelm();
			return;
		}
		if (ship == nullptr || ship->Destroyed)
		{
			ExitPiloting();
			return;
		}
		// Riding a lane, or thrown out of one this frame into an ambush (U.SPC-T42).
		if (ship->Phase == ShipPhase::Lane || (rideWarpBefore >= 0 && ship->Battle != nullptr))
		{
			// Into a lane, the chase is shaken off.
			if (World()->Pursuit != nullptr && World()->Pursuit->Active())
			{
				World()->Pursuit->ReleaseAll(Clock->Jd);
				ShowToast("Into the lane: the hunters are left behind");
			}
			UpdateLaneRide(ship);
			ShowWarNotices();
			return;
		}

		// Interstellar navigation (U.SPC-T420): a course to a star or beacon picked and plotted, and the FTL autopilot
		// flying it; while it has the helm the stick is not read.
		if (Input.ClearFtlCourse && PlottedCourse != nullptr) ClearFtlCourse();
		if (Input.CycleFtlTarget != 0) CycleFtlCourse(Input.CycleFtlTarget);
		if (FtlAutopilot->Active())
		{
			if (UpdateFtlAutopilot(ship, dt)) return;
		}
		else if (Input.ToggleFtlAutopilot && EngageFtlAutopilot()) return;
		RefreshFtlCourse(ship);

		// The stick, one way per kind of hull (U.SPC-T269): a light fighter's mouse swings the view and the
		// hull turns after it, a heavy fighter's or freighter's turns the hull itself, and a capital ship
		// is turned on the keys while the mouse looks around it. Every role is flown through its ShipHelm, on the
		// control set fitted to it (U.SPC-T449); this is only the input layer turning the player's hands into it.
		if (pilotHelm == nullptr || pilotHelm->Ship != ship)
		{
			pilotHelm = cs::New<ShipHelm>(World(), ship);
			pilotHelm->Flight = helmFlight;
		}
		auto controls = pilotHelm->Controls();
		auto tuning = PilotTuning::Of(ship->Spec->Class);
		auto hull = HullDynamics::Of(ship->Combat->Spec);
		auto control = PilotControlMode = controls.Stick;
		auto model = FlightModelRegistry::Default()->For(ship->Spec->Class);
		auto capital = cs::As<CapitalFlightModel>(model);
		// A capital's helm leads the nose by what its own swing needs (U.SPC-T499).
		double leadRad = capital != nullptr ? capital->AimLeadRad(hull) : PilotFlight::AimLeadRad(hull, tuning);
		auto look = ClickPanelOpen() ? Vector2::zero() : Input.MouseDelta;
		look.y = Cameras::SpaceCamera::LookY(look.y);
		// float products, widened to double after (as C#).
		double lookRight = look.x * PilotLookSensitivity() * Mathf::Deg2Rad, lookUp = look.y * PilotLookSensitivity() * Mathf::Deg2Rad;
		double strafeRight = Input.Move.x, strafeUp = Input.Move.y;
		switch (control)
		{
			default:
			{
				// Every fighter and hauler flies "pulled by a string" (US-T168): the camera looks where the mouse puts it and
				// the hull chases it. A light fighter's string is slack (a full 180 at once, the hull whipping round after it,
				// LightFighterFeel::Whip); a heavy hull's taut, its look barely faster than its own turn. Any
				// fighter's goes slack. A hauler's slow mouse (LookFactor) swings it less.
				double hullTurnDegS = tuning.TurnRateDegS;
				if (auto heavy = cs::As<HeavyFighterFlightModel>(model)) hullTurnDegS = heavy->TurnRateDegS(tuning);
				double factor = controls.LookFactor;
				PilotAim::Look(pilotAim, pilotViewUp, pilotLookOwedRight, pilotLookOwedUp, lookRight * factor, lookUp * factor,
					pilot().Forward, pilot().Up, dt, PilotAim::StringFor(control, hullTurnDegS, false));
				break;
			}
			case PilotControl::Helm:
				// A/D and Q/E (C/]) are the helm's yaw and pitch: a capital ship turns rather than strafes.
				helmLookYaw = Mathf::Repeat(helmLookYaw + look.x * PilotLookSensitivity() + 180.0f, 360.0f) - 180.0f;
				helmLookPitch = Mathf::Clamp(helmLookPitch - look.y * PilotLookSensitivity(), -80.0f, 80.0f);
				// Alt held, the broadside stance (US-T81): the keys slide the hull instead of turning it, and the helm
				// (ShipHelm) lays it beam-on to the locked hull.
				if (Input.Manoeuvre) pilotAim = pilot().Forward;
				else
				{
					pilotAim = PilotAim::Helm(pilot().Forward, pilot().Up, strafeRight, strafeUp, leadRad);
					strafeRight = strafeUp = 0;
				}
				break;
		}
		auto desiredForward = pilotAim;

		if (Input.ToggleFlightAssist)
		{
			pilotFlightAssistOff = !pilotFlightAssistOff;
			ShowToast(pilotFlightAssistOff
				? "FLIGHT ASSIST OFF: the hull drifts on its momentum and turns free of its track; W/S raw thrust, Alt flips for a turn and burn  (F to turn it back on)"
				: "FLIGHT ASSIST ON: the thrusters cancel drift and it flies where it points");
		}

		if (Input.ToggleSilent)
		{
			ship->SilentRunning = !ship->SilentRunning;
			ShowToast(ship->SilentRunning
				? "SILENT RUNNING: transponder off, systems down, heat damped -- a burn still shows  (F3 to light up again)"
				: "SILENT RUNNING OFF: transponder on");
		}

		if (Input.LockTarget) ToggleLockTarget();
		if (!pilotLockTarget.IsNone() && (!pilotLockTarget.IsShip() || pilotLockTarget.Ship->Destroyed)) pilotLockTarget = Target::None();
		if (ship->Battle == nullptr) pilotLockUnit = nullptr;
		SpaceCamera->PilotLockTarget = pilotLockTarget;

		// What the world does to the hull this frame, for the helm to fly in: the pull of the bodies near it, flight assist,
		// drives knocked out by an interdiction pulse (U.SPC-T42), and a port's approach for a hauler's docking mode (U.SPC-T443).
		helmFlight->Gravity = PilotGravity();
		helmFlight->AssistOff = pilotFlightAssistOff;
		helmFlight->DrivesDown = PilotStunSeconds() > 0;
		helmFlight->DockingApproach = PortApproach();
		// The hull flies behind its helm (U.SPC-T449): its class's own flight model (U.SPC-T17, docs/FLIGHT_MODELS.md), on
		// its own tanks, with what its role's controls add (a hauler's load, a capital's telegraph and weak-patch roll).
		EmbodimentInput stick;
		stick.Aim = desiredForward;
		stick.Throttle = Input.Move.z;
		stick.StrafeRight = strafeRight;
		stick.StrafeUp = strafeUp;
		stick.Roll = Input.Roll;
		stick.Boost = Input.Boost;
		stick.Cruise = Input.Cruise;
		stick.Fine = Input.Slow;
		// Alt, the manoeuvre key (US-T87): the turn-and-burn's flip onto the retrograde.
		stick.Manoeuvre = Input.Manoeuvre;
		// W and S ring a capital's engine telegraph a step at a time (U.SPC-T499).
		stick.TelegraphStep = Input.ThrottleStep;
		// The trigger and the crosshair; UpdatePilotBattle below gates them on what the hull can fire right now.
		stick.Primary = Input.Fire && !ClickPanelOpen();
		stick.Secondary = Input.LookHeld && !ClickPanelOpen();
		// The arrows share the hull's reactor (US-T94): the whole split asked for goes in with the stick.
		stick.PowerSplit = CraftPower::After(pilotHelm, Input.PowerKey);
		// Shift+Backspace, the self-destruct (US-T97): the helm counts it down, and out of a fight blows where it has flown.
		stick.SelfDestruct = SelfDestructCommand(pilotHelm);
		// Shift+1..0, the hull's own abilities (US-T106): launch a squadron, recall or call the wing, a boarding order.
		stick.HelmAbility = HelmAbilityKey(ship);
		// Tab, an active ping; Shift+F3, go dark or power up again (US-T93): a command the helm lands, said on a toast.
		stick.Sensor = SensorCommand(ship);
		CruiseStick(stick); // US-T127: cruising, the stick turns the hull and the drive carries it
		helmFlight->FrameBody = pilotFrameBody;
		pilotHelm->Control(stick, dt);
		if (stick.HelmAbility != 0) NoteHelmAbility();
		if (ship->Destroyed)
		{
			// Gone up under the helm: the player is out of it at once (UpdateSeat then says what the blast did).
			ExitPiloting();
			return;
		}
		// The camera's FOV kick below, and the HUD's readouts.
		pilotBoosting = helmFlight->Boosting;
		HaulerReadout(helmFlight->Model, helmFlight->Hull, helmFlight->Input);
		PilotTelegraph = capital != nullptr ? pilot().Telegraph : cs::Nullable<EngineTelegraph>();
		PilotFlightNote = helmFlight->Note;
		SpaceCamera->PilotView = PilotViewRotation(control);
		auto posBefore = helmFlight->PosBefore;
		// Hulls, stations, rocks and wreckage have volume: what the hull flies into, it hits (U.SPC-T60).
		if (!KeepPilotAboveSurfaces(ship)) return;
		if (!UpdatePilotCollisions(ship, posBefore, dt)) return;
		UpdateSystemCruise(ship, dt); // US-T127: Shift+J, and the cruise drive's leg this frame (past the swept collision test)
		UpdatePropellant(ship, dt);

		UpdatePilotBattle(ship, dt);
		// What the ship's own scanners make of everything else, and what everyone else can see of it: its drive is
		// lit whenever its thrusters changed its velocity beyond what gravity did this frame (U.SPC-T66).
		bool burning = helmFlight->Burning;
		ReportBeltRocks(Clock->Jd); // US-T93: the rocks near the hull, for line of sight
		if (World()->Sensors != nullptr) World()->Sensors->UpdatePilot(ship, Ephemeris()->HelioAt(pilotFrameBody, Clock->Jd) + pilotOffsetKm(), burning, Clock->Jd);
		// The law around the ship: trespass, and patrols that scan it or, if it is wanted, hail it to pay up.
		World()->Holdings->PilotFrameBody = pilotFrameBody;
		World()->Law->UpdatePilot(ship, pilotFrameBody, pilotOffsetKm(), pilotForward(), Clock->Jd);
		// Bounty hunters with a contract on the ship close in once they find it (U.SPC-T62).
		World()->Outlaws->UpdatePilot(ship, pilotFrameBody, pilotOffsetKm(), pilotForward(), Clock->Jd);
		// Contracts worked at the helm: crates handed over, passengers put ashore, patrols flown, bodies surveyed (U.SPC-T101).
		if (World()->Contracts != nullptr) World()->Contracts->UpdatePilot(ship, pilotFrameBody, pilotOffsetKm(), Clock->Jd);
		// Hostiles that see the ship turn to run it down (U.SPC-T273).
		UpdatePursuit(ship, dt);
		// Through a gate, the hull flown on is another one, in another system: nothing more for this one this frame.
		if (UpdateGateActivation(ship)) return;
		// Its own FTL drive (U.SPC-T417): beyond the rim, pointed at a star, spooled and engaged, the hull leaves the system.
		if (UpdateHelmFtl(ship)) return;
		UpdateLaneEntry(ship);
		UpdateLaneRejoin(ship);
		ShowWarNotices();
	}

	cs::Nullable<Quaternion> UnitySpaceApp::PilotViewRotation(PilotControl control)
	{
		switch (control)
		{
			case PilotControl::Helm: return ToRotation(pilot().Forward, pilot().Up) * Quaternion::Euler(helmLookPitch, helmLookYaw, 0);
			// The string aim (US-T168), every fighter and hauler: the view is the mouse's, the hull turns after it.
			default: return ToRotation(pilotAim, pilotViewUp);
		}
	}

	Quaternion UnitySpaceApp::ToRotation(DVec3 forward, DVec3 up)
	{
		auto f = forward.SwapYZ();
		auto u = up.SwapYZ();
		auto fv = Vector3(static_cast<float>(f.X), static_cast<float>(f.Y), static_cast<float>(f.Z));
		auto uv = Vector3(static_cast<float>(u.X), static_cast<float>(u.Y), static_cast<float>(u.Z));
		if (fv.sqrMagnitude() < 1e-12f) fv = Vector3::forward();
		if (uv.sqrMagnitude() < 1e-12f || Mathf::Abs(Vector3::Dot(fv.normalized(), uv.normalized())) > 0.98f) uv = Mathf::Abs(fv.normalized().y) > 0.98f ? Vector3::right() : Vector3::up();
		return Quaternion::LookRotation(fv, uv);
	}
}
