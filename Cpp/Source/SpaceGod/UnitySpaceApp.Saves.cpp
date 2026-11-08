// Port of Unity.SpaceGod Runtime/UnitySpaceApp.Saves.cs (US-T29): save and load. Members are declared in
// UnitySpaceApp.Saves.inl; OnApplicationQuit (declared in UnitySpaceApp.h) is defined here. A C# null string is the
// empty string here. C#'s `catch (Exception e)` is `catch (const std::exception& e)`; `"..." + e` (the exception's
// ToString: type, message and stack) logs e.what(), and e.Message is e.what().

#include "UnitySpaceApp.h"
#include "Audio/InterfaceSounds.h" // US-T244
#include "CaptureEvents.h" // US-T72
#include "UI/UiMotion.h" // US-T255

#include <exception>

#include "Difficulty.h" // US-T253
#include "Ephemeris.h"
#include "Fleet.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/PilotFlight.h"
#include "Combat/War.h"
#include "Persistence/SaveGame.h"
#include "Session/LocalSession.h"
#include "Shared/Profiles/ProfileStore.h"
#include "Systems/FtlNavigation.h"
#include "Systems/Galaxy.h"
#include "Systems/HelmFtl.h"

#include "ActiveProfile.h"
#include "Bootstrap.h"
#include "GameLaunch.h"
#include "PlayerOptions.h" // US-T252
#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Rendering/Materials.h"
#include "Views/InterstellarView.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	void UnitySpaceApp::InitSaves()
	{
		Saves = cs::New<SaveSlots>(ActiveProfile::Current()->SavesFolder());
		autosaveEnabled = !Bootstrap::HasCommandLineFlag("-demo") && !Bootstrap::HasCommandLineFlag("-capture");
		// Started from the main menu: the save it picked (or, for a new game, none).
		cs::String picked = GameLaunch::TakeLoadSlot();
		if (!picked.empty())
		{
			LoadFrom(picked);
			return;
		}
		if (GameLaunch::FromMenu) return;
		cs::Ref<SaveHeader> latest = Saves->Latest();
		if (latest == nullptr) return;
		if (Bootstrap::HasCommandLineFlag("-continue")) LoadFrom(latest->Slot);
		else ShowToast(cs::Cat("Saved game found (", DescribeSave(latest), "): F6 to load it, F8 for the quicksave"));
	}

	void UnitySpaceApp::UpdateSaves(float dt)
	{
		if (!autosaveEnabled) return;
		// US-T252: Settings > Gameplay > Autosave (off, or every 2 to 30 minutes); a shorter interval picked takes hold at once.
		float every = PlayerOptions::AutosaveSeconds();
		if (every <= 0) return;
		autosaveDueS = Mathf::Min(autosaveDueS, every) - dt;
		// Between the stars there is no system to come back to (nor, latched to a hulk, a session to keep): try again once that is over.
		if (autosaveDueS > 0 || !SaveBlocker().empty()) return;
		autosaveDueS = every;
		// US-T252: Toasts and news below Everything takes the autosave without its toast.
		Autosave(PlayerOptions::NoticeMode() != PlayerOptions::NoticeLevel::Everything);
	}

	void UnitySpaceApp::OnApplicationQuit()
	{
		if (autosaveEnabled && Galaxy != nullptr && SaveBlocker().empty()) Autosave(true);
	}

	void UnitySpaceApp::ReturnToMainMenu()
	{
		if (autosaveEnabled && Galaxy != nullptr && SaveBlocker().empty()) Autosave(true);
		GameLaunch::ToMainMenu();
	}

	void UnitySpaceApp::RequestSave(const cs::String& slot, const cs::String& label)
	{
		pendingSave = slot;
		pendingSaveLabel = label;
	}

	void UnitySpaceApp::HandleSaveCommands()
	{
		if (pendingMainMenu)
		{
			pendingMainMenu = false;
			ReturnToMainMenu();
			return;
		}
		if (!pendingSave.empty())
		{
			SaveTo(pendingSave, pendingSaveLabel);
			pendingSave = pendingSaveLabel = cs::String();
		}
		if (!pendingLoad.empty())
		{
			LoadFrom(pendingLoad);
			pendingLoad = cs::String();
		}
		if (Input.QuickSave) SaveTo(SaveSlots::Quick, "Quicksave");
		if (Input.QuickLoad)
		{
			if (Saves->Exists(SaveSlots::Quick)) LoadFrom(SaveSlots::Quick);
			else ShowToast("No quicksave yet (F5 to quicksave)");
		}
		if (Input.ToggleSaves) ShowSaves = !ShowSaves;
	}

	cs::String UnitySpaceApp::SaveBlocker() const
	{
		return Travel->Active ? cs::String("Not between the stars: wait until the trip is over")
			// Under the hull's own FTL drive (U.SPC-T417): the charge, the run and the crossing are the helm's until it drops out.
			: InFtlCruise() || Ftl->Stage != FtlStage::Idle ? cs::String("Not under FTL: wait until the drive drops out")
			// The FTL autopilot's run out is flown on its own clock (U.SPC-T420), not the helm's.
			: FtlAutopilot->Active() ? cs::String("Not while the FTL autopilot flies: take the helm back first (End)")
			// What the clamps have brought out (survivors, crates, a fight aboard) is the session's until they come off.
			: Salvage != nullptr ? cs::String("Not while latched to a hulk: cast off first (4)")
			: cs::String();
	}

	void UnitySpaceApp::Autosave(bool quiet)
	{
		try
		{
			cs::Ref<SaveHeader> h = Saves->Autosave(CaptureSave());
			if (!quiet)
			{
				ShowToast(cs::Cat("Autosaved  (", DescribeSave(h), ")"));
				Audio::InterfaceSounds::SaveDone(); // US-T244
			}
		}
		catch (const std::exception& e)
		{
			Debug::LogError(cs::Cat("[Unity.SpaceGod] Autosave failed: ", e.what()));
			if (!quiet) ShowToast(cs::Cat("Autosave failed: ", e.what()));
		}
	}

	bool UnitySpaceApp::SaveTo(const cs::String& slot, const cs::String& label)
	{
		cs::String blocked = SaveBlocker();
		if (!blocked.empty())
		{
			ShowToast(blocked);
			return false;
		}
		try
		{
			cs::Ref<SaveData> data = CaptureSave();
			cs::Ref<SaveHeader> h = Saves->Save(slot, data, !label.empty() ? label : SlotName(slot));
			ShowToast(cs::Cat("Saved to ", SlotName(slot), "  (", DescribeSave(h), ")"));
			CaptureEvents::GameSaved(SlotName(slot)); // US-T72
			Audio::InterfaceSounds::SaveDone(); // US-T244
			return true;
		}
		catch (const std::exception& e)
		{
			Debug::LogError(cs::Cat("[Unity.SpaceGod] Save to ", slot, " failed: ", e.what()));
			ShowToast(cs::Cat("Save failed: ", e.what()));
			return false;
		}
	}

	bool UnitySpaceApp::LoadFrom(const cs::String& slot)
	{
		if (CoopRoutesLoad(slot)) return false; // US-T44 (US-T43): a client's load is a request to the host, whose save every peer loads
		// (Loading drops whatever the session held, a hulk latched to included: only a trip between the stars stops it.)
		cs::String blocked = Travel->Active || InFtlCruise() || Ftl->Stage != FtlStage::Idle ? SaveBlocker() : cs::String();
		if (!blocked.empty())
		{
			ShowToast(blocked);
			return false;
		}
		cs::Ref<SaveData> data;
		cs::Ref<SaveHeader> header;
		try
		{
			data = Saves->Load(slot, header);
		}
		catch (const std::exception& e)
		{
			Debug::LogError(cs::Cat("[Unity.SpaceGod] Loading ", slot, " failed: ", e.what()));
			ShowToast(cs::Cat("Could not load ", SlotName(slot), ": ", e.what()));
			return false;
		}
		CoopLoadingHere([this, &data] { ApplyLoaded(data); }); // US-T44 (US-T43): ApplyLoaded(data); the loaded helm is everyone's in co-op
		ShowSaves = false;
		ShowToast(cs::Cat("Loaded ", SlotName(slot), "  (", DescribeSave(header), ")"));
		CaptureEvents::GameLoaded(SlotName(slot)); // US-T72
		UI::UiMotion::FadeFromBlack(); // US-T255: a load in place (quickload, the saves panel) cuts to black and fades back in
		return true;
	}

	cs::Ref<SaveData> UnitySpaceApp::CaptureSave()
	{
		auto data = cs::New<SaveData>();
		data->Clock = Clock;
		data->Galaxy = Galaxy;
		if (Galaxy != nullptr) Galaxy->DifficultyRecord = Difficulty::Record(); // US-T253: the save keeps the level it was played at
		cs::Ref<SaveSession> s = data->Session;
		s->ActiveSystem = ActiveSystem->Name;
		s->ScaleGoal = ScaleGoal;
		if (!Piloted.IsNone()) // US-T44 (US-T43): was IsPiloting() (in co-op whoever flies it: the host's save is everyone's)
		{
			s->Piloted = Piloted.Ship;
			s->PilotFrameBody = pilotFrameBody;
			s->Pilot = pilot();
			s->PilotAim = pilotAim;
			s->FlightAssistOff = pilotFlightAssistOff;
		}
		return data;
	}

	void UnitySpaceApp::ApplyLoaded(const cs::Ref<SaveData>& data)
	{
		DropSession();
		Clock = data->Clock;
		Galaxy = data->Galaxy;
		// US-T253: the level the save was played at (a co-op client already has the host's, from the sync's settings).
		if (!CoopIsClient()) Difficulty::Restore(Galaxy->DifficultyRecord);
		for (const auto& sys : Galaxy->Systems) // US-T215: escort names saved with a doubled faction prefix
			if (sys != nullptr && sys->World != nullptr && sys->World->Fleet != nullptr) sys->World->Fleet->NormaliseNames();
		Rendering::Materials::DestroyOwned(Interstellar->Root->transform);
		GameObject::Destroy(Interstellar->Root);
		Interstellar = cs::New<Views::InterstellarView>(Galaxy, worldRoot);
		SubscribeWorlds();

		// (C#'s local is `session`, which would shadow the app's own session field here.)
		cs::Ref<SaveSession> saved = data->Session != nullptr ? data->Session : cs::New<SaveSession>();
		cs::Ref<StarSystem> found = Galaxy->Find(saved->ActiveSystem);
		Activate(found != nullptr ? found : Galaxy->Systems[0]);
		ScaleGoal = saved->ScaleGoal;
		scaleFrom = ScaleGoal;
		scaleT = 1.0f;
		Scale()->SetBlend(ScaleGoal);
		Scale()->Layout(Ephemeris(), bodyScene);
		UpdateRadii();
		GalaxyRebuilt.Invoke();

		cs::Ref<Core::Ship> ship = saved->Piloted;
		if (ship != nullptr && !ship->Destroyed && ship->Id < Fleet()->Ships().Count() && Fleet()->Ships()[ship->Id] == ship)
		{
			EnterPiloting(Target::OfShip(ship));
			// Its motion as it was, not the spot its schedule was pinned at when the helm was first taken.
			pilotFrameBody = saved->PilotFrameBody;
			pilot() = saved->Pilot;
			pilotViewUp = pilot().Up; // the view's roof follows the hull as saved, not as freshly seeded (U.SPC-T386)
			pilotAim = saved->PilotAim;
			pilotFlightAssistOff = saved->FlightAssistOff;
			// A ride under way goes on from where it was (EnterPiloting has taken it up again).
			if (ship->Battle == nullptr && ship->Phase != ShipPhase::Lane) PinWherePiloted(ship);
		}
		else
		{
			World()->PilotedShip = nullptr;
			FocusSystemOverview();
		}
		autosaveDueS = PlayerOptions::AutosaveSeconds() > 0 ? PlayerOptions::AutosaveSeconds() : AutosaveIntervalS; // US-T252
		CoopLoaded(); // US-T44
	}

	void UnitySpaceApp::DropSession()
	{
		// A fighter or gun held in the world being left goes back to its AI there (U.SPC-T422); the loaded game starts in command.
		ReleaseSeat();
		session = Coop::CoopPeer::NewSession(); // US-T44: LocalSession::Create(/*seed:*/ 0) single-player
		commandWarp = -1;
		commandFocus = Target::None();
		Piloted = Target::None();
		pilotLockTarget = Target::None();
		pilotLockUnit = nullptr;
		pilotBoosting = false;
		pilotEmpSeconds = 0;
		pilot() = PilotState{};
		rideWarpBefore = -1;
		// The FTL autopilot's run out (and the course it flies) belong to the old galaxy's hull: a load made mid-run must
		// not fly the loaded helm on it, nor leave it "flying" with no helm at all (which blocks every save after).
		FtlAutopilot->Reset();
		PlottedCourse = nullptr;
		RideOnward.Clear();
		RideChoice = -1;
		RideThreat = std::nullopt;
		warnedOf = nullptr;
		laneRejoinable = false;
		SpaceCamera->PilotLockTarget = Target::None();
		Salvage = nullptr;
		SalvageApproach = cs::String();
		salvagePrompted = nullptr;
		salvageLogShown = 0;
		TankerDueS = -1;
		propellantWarning = 0;
		termsPrompted = nullptr;
		demandPrompted = false;
		havenPrompted = nullptr;
		nearGate = nullptr;
		helmTriggerWas = false;
		SpaceCamera->ExitFocus();
	}

	cs::String UnitySpaceApp::SlotName(const cs::String& slot)
	{
		if (slot == SaveSlots::Quick) return "Quicksave";
		if (SaveSlots::IsManual(slot)) return cs::Cat("Save ", cs::Strings::Substring(slot, 5));
		if (cs::Strings::StartsWith(slot, "autosave-")) return cs::Cat("Autosave ", cs::Strings::Substring(slot, 9));
		return slot;
	}

	cs::String UnitySpaceApp::SaveTitle(const cs::String& slot, const cs::Ref<SaveHeader>& h)
	{
		return SaveSlots::IsManual(slot) && !cs::Strings::IsNullOrEmpty(h != nullptr ? h->Label : cs::String()) ? h->Label : SlotName(slot);
	}

	cs::String UnitySpaceApp::DescribeSave(const cs::Ref<SaveHeader>& h)
	{
		if (h == nullptr) return "empty";
		cs::String text = cs::Cat(h->ActiveSystem, ", ", h->SimUtc().ToString("yyyy-MM-dd HH:mm"), " UTC");
		if (!h->PilotedShip.empty()) text += cs::Cat(", flying ", h->PilotedShip, " (", cs::Fmt(h->Credits, "N0"), " cr)");
		return text;
	}
}
