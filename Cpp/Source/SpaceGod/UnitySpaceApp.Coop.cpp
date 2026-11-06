// US-T44: co-op's side of the app (declarations in UnitySpaceApp.Coop.inl), the C# track's Runtime/UnitySpaceApp.Coop.cs
// (US-T39) member for member. The ported originals call in here only at lines marked US-T44, and every call is the
// original's own behaviour when Coop::CoopPeer::Current() is null (single-player).

#include "UnitySpaceApp.h"

#include "Coop/CoopPeer.h"
#include "Coop/CoopSettings.h"
#include "Camera/SpaceCamera.h"
#include "UI/Hud.h"
#include "UI/GalaxyMapScreen.h" // US-T47
#include "Combat/Diplomacy.h"
#include "Systems/Polities.h"
#include "Systems/Statecraft.h"
#include "Systems/StrategicCommand.h"
#include "Combat/CombatSpecs.h"
#include "Embodiment/ShipHelm.h"
#include "Ephemeris.h"
#include "PracticeSpawn.h"
#include "Systems/HelmFtl.h"
#include "Systems/TravelCourse.h"
#include "Systems/FtlNavigation.h"
#include "UnityCompat/UnityGui.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/War.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Embodiment/CraftPower.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/Embodiment.h"
#include "Embodiment/FighterSeat.h"
#include "Embodiment/MountedTurret.h"
#include "Embodiment/RepairWorker.h" // US-T135
#include "Views/EmbodiedShine.h" // US-T136: seat colours
#include "Embodiment/StationSeat.h"
#include "Fleet.h"
#include "Persistence/SaveGame.h"
#include "ScaleModel.h"
#include "Session/CommandCodec.h"
#include "Session/Commands.h"
#include "Session/ISession.h"
#include "Session/LocalSession.h"
#include "Session/SessionEvents.h"
#include "Ships.h"
#include "SimClock.h"
#include "Systems/Galaxy.h"
#include "World.h"

#include <exception>

using namespace SpaceGod::Core;
using SpaceGod::Coop::CoopPeer;
using SpaceGod::Coop::ClockState;
using SpaceGod::Coop::FrameRecord;
namespace Wire = SpaceGod::Coop::Wire;

namespace SpaceGod
{
	// ---------------------------------------------------------------- the frame's clock step

	void UnitySpaceApp::CoopAdvance(float dt)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr)
		{
			Clock->Advance(dt);
			return;
		}
		CoopWatchSession();
		coopStepped = false;
		if (peer->IsHost())
		{
			FrameRecord* rec = peer->HostOpenFrame();
			for (const auto& [seat, request] : peer->TakeClockRequests()) CoopClockRequest(seat, request);
			CoopApplyEntries(*peer, rec->Entries);
			CoopShareSelection(*peer);
			CoopHostPilotFrame(*peer, *rec); // US-T43: the helm holder's hands for this frame, into the record
			rec->Before = ClockState::Of(*Clock);
			Clock->Advance(dt);
			rec->JdAfter = Clock->Jd;
		}
		else
		{
			const FrameRecord* rec = peer->ClientRecord;
			if (rec == nullptr) return; // nothing from the host yet: time holds
			CoopApplyEntries(*peer, rec->Entries);
			CoopShareSelection(*peer);
			if (coopLeftValid)
			{
				// The keys (. , Space) or this machine's own rules (a seat taken drops to real time) moved the clock: the host's call.
				if (Clock->WarpIndex != coopLeft.Warp) peer->RequestClock(cs::Cat("warp ", Clock->WarpIndex));
				if (Clock->PauseHeld() != coopLeft.PauseHeld()) peer->RequestClock(cs::Cat("pause ", Clock->PauseHeld() ? 1 : 0));
			}
			rec->Before.ApplyTo(*Clock);
			Clock->Advance(dt);
			if (Clock->Jd != rec->JdAfter)
			{
				peer->ClockCorrections++;
				Clock->SetJd(rec->JdAfter);
			}
			peer->Consumed = true;
		}
		coopLeft = ClockState::Of(*Clock);
		coopLeftValid = true;
		coopSimRate = Clock->Warp().SimSecondsPerSecond * Clock->Speed;
		coopStepped = true;
	}

	void UnitySpaceApp::CoopClockRequest(int seat, const cs::String& request)
	{
		auto f = Wire::Split(request, ' ');
		if (f.size() < 2) return;
		if (f[0] == "warp")
		{
			Clock->SetWarp(Wire::ParseI(f[1]));
			ShowToast(cs::Cat(CoopPeer::Current()->NameOf(seat), ": time ", Clock->Warp().Label));
		}
		else if (f[0] == "pause" && (f[1] == "1") != Clock->PauseHeld()) TogglePause();
	}

	void UnitySpaceApp::CoopApplyEntries(CoopPeer& peer, const std::vector<cs::String>& entries)
	{
		coopPilotFrame.reset();
		for (size_t n = 0; n < entries.size(); n++)
		{
			const cs::String e = entries[n];
			// US-T43: the app's own entries, in the frames' order with the session's.
			if (CoopAppEntry(peer, e)) continue;
			if (!Wire::StartsWith(e, "s "))
			{
				peer.ApplyRosterEntry(e);
				continue;
			}
			cs::String codec = e.substr(2);
			cs::String key;
			size_t at = codec.find(" @");
			if (at != cs::String::npos)
			{
				key = codec.substr(at + 2);
				codec = codec.substr(0, at);
			}
			cs::Ref<IGameCommand> cmd;
			try
			{
				cmd = CommandCodec::Read(codec);
			}
			catch (const std::exception& x)
			{
				CoopPeer::Warn(cs::Cat("bad command '", codec, "': ", x.what()));
				continue;
			}
			// A take names its seat by key: each peer's session gives the seat its id as it is first named (in the frames'
			// order, so the same id everywhere); the sender's id would mean nothing here.
			if (auto take = cs::As<TakeSeatCommand>(cmd); take && !key.empty())
			{
				auto seat = CoopResolveSeat(key);
				if (seat == nullptr)
				{
					CoopPeer::Log(cs::Cat(cmd->Player().ToString(), " asked for seat '", key, "', which is gone"));
					continue;
				}
				PlayerId player = take->Player();
				int id = session->IdOf(seat);
				cmd = cs::New<TakeSeatCommand>(player, id);
			}
			session->Submit(cmd);
		}
		session->ApplyPending();
	}

	void UnitySpaceApp::CoopShareSelection(CoopPeer& peer)
	{
		cs::String key = CoopTargetKey(Selected);
		if (key == coopLastSelection) return;
		coopLastSelection = key;
		peer.Selected(key);
	}

	// ---------------------------------------------------------------- adventures

	void UnitySpaceApp::CoopUpdateAdventures(bool wanted)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr)
		{
			if (wanted) World()->UpdateAdventures(Clock->Jd, PlayerHelioKm());
			return;
		}
		if (peer->IsHost())
		{
			FrameRecord* rec = peer->Open();
			if (rec == nullptr) return;
			rec->Adventures = wanted;
			if (wanted) rec->AdventurePoint = PlayerHelioKm();
			if (rec->Adventures) World()->UpdateAdventures(Clock->Jd, rec->AdventurePoint);
			return;
		}
		const FrameRecord* rec = peer->ClientRecord;
		if (rec == nullptr) return;
		if (rec->Adventures) World()->UpdateAdventures(Clock->Jd, rec->AdventurePoint);
	}

	// ---------------------------------------------------------------- seats

	void UnitySpaceApp::CoopWatchSession()
	{
		if (coopWatched.lock() == session) return;
		coopWatched = session;
		session->EventRaised() += [this](cs::Ref<IGameEvent> e) { CoopSessionEvent(e); };
	}

	void UnitySpaceApp::CoopSessionEvent(const cs::Ref<IGameEvent>& e)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr) return;
		bool mine = e->Player() == session->LocalPlayer();
		if (auto t = cs::As<SeatTaken>(e))
		{
			auto seat = session->State()->Seat(t->Seat);
			CoopPeer::Log(cs::Cat(peer->NameOf(t->Player().Seat), " (", t->Player().ToString(), ") takes seat ", t->Seat, " (",
				seat != nullptr ? seat->Name() : cs::String(), ") at frame ", peer->Frame()));
			if (mine && seat != nullptr) CoopSeatTakenHere(seat);
		}
		else if (auto r = cs::As<SeatReleased>(e))
		{
			CoopPeer::Log(cs::Cat(peer->NameOf(r->Player().Seat), " (", r->Player().ToString(), ") lets go of seat ", r->Seat,
				!r->Reason.empty() ? cs::Cat(": ", r->Reason) : cs::String(), " at frame ", peer->Frame()));
			if (mine && !r->Reason.empty())
			{
				auto seat = session->State()->Seat(r->Seat);
				ReturnToCommand(r->Reason, seat != nullptr ? seat->Name() : cs::String());
			}
		}
		else if (auto c = cs::As<CommandRefused>(e))
		{
			cs::String what = "command";
			if (c->Command != nullptr) what = Wire::Split(CommandCodec::Write(c->Command), ' ')[0];
			CoopPeer::Log(cs::Cat(peer->NameOf(c->Player().Seat), "'s ", what, " refused: ", c->Reason));
			if (mine) ShowToast(cs::Cat("Can't: ", c->Reason));
		}
	}

	void UnitySpaceApp::CoopSeatTakenHere(const cs::Ref<IEmbodiable>& next)
	{
		auto pose = next->PoseAt(Clock->Jd);
		seatAim = ToVector(pose.Forward.SwapYZ()).normalized();
		Clock->SetWarp(0);
		RideCameraRig(next, pose, false); // US-T135: the seat's own camera rig
		ShowToast(cs::Cat(next->RolePanel().Verb, " ", next->Name(), ", ", Clock->Warp().Label, SeatKeys(next)));
	}

	bool UnitySpaceApp::CoopTakeSeat(const cs::Ref<IEmbodiable>& next)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr) return false;
		cs::String key = CoopSeatKey(next);
		if (key.empty())
		{
			ShowToast(cs::Cat("Can't share ", next->Name(), " in co-op: man a gun, a fighter or a station"));
			return true;
		}
		// Id 0: the sender's session must not name the seat (that would hand out an id the others never saw).
		peer->Submit(cs::Cat("s ", CommandCodec::Write(cs::New<TakeSeatCommand>(session->LocalPlayer(), 0)), " @", key));
		ShowToast(cs::Cat("Asking for ", next->Name(), "..."));
		return true;
	}

	bool UnitySpaceApp::CoopRoutes(const cs::Ref<IGameCommand>& command)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr) return false;
		if (cs::Is<ReleaseSeatCommand>(command) && HeldNonHelm() == nullptr) return true; // nothing to hand back
		if (!peer->Applying()) peer->Submit(cs::Cat("s ", CommandCodec::Write(command)));
		return true;
	}

	void UnitySpaceApp::CoopUpdateSeat(float dt)
	{
		CoopPeer* peer = CoopPeer::Current();
		auto held = HeldNonHelm();
		auto rot = SpaceCamera->Rotation;
		auto up = rot * Vector3::up();
		if (held != nullptr)
		{
			auto look = ClickPanelOpen() ? Vector2::zero() : Input.MouseDelta;
			auto right = rot * Vector3::right();
			seatAim = (Quaternion::AngleAxis(look.x * Cameras::SpaceCamera::LookSensitivity(), up) *
				Quaternion::AngleAxis(-Cameras::SpaceCamera::LookY(look.y) * Cameras::SpaceCamera::LookSensitivity(), right) * seatAim).normalized();
			ClampSeatAim(held); // US-T129
			EmbodimentInput input;
			input.Aim = DVec3(seatAim.x, seatAim.y, seatAim.z).SwapYZ();
			input.Throttle = Mathf::Clamp(Input.Move.z, -1.0f, 1.0f);
			input.StrafeRight = Mathf::Clamp(Input.Move.x, -1.0f, 1.0f);
			input.StrafeUp = Mathf::Clamp(Input.Move.y, -1.0f, 1.0f);
			input.Roll = Mathf::Clamp(Input.Roll, -1.0f, 1.0f);
			input.Boost = Input.Boost;
			input.Manoeuvre = Input.Manoeuvre; // US-T87: the turn-and-burn's flip
			input.Drift = Input.Drift; // US-T165: R held, a launched craft drifts
			input.Primary = Input.Fire && !ClickPanelOpen();
			input.Secondary = Input.LookHeld && !ClickPanelOpen();
			input.PowerSplit = CraftPower::After(held, Input.PowerKey); // US-T94: the arrows' reactor split, whole
			input.SelfDestruct = SelfDestructCommand(held); // US-T97: arm or abort, landed once on every peer
			cs::String line = CommandCodec::Write(cs::New<SeatControlCommand>(session->LocalPlayer(), input));
			if (line != coopLastControl)
			{
				coopLastControl = line;
				peer->Submit(cs::Cat("s ", line));
			}
			// US-T43: the cockpit's own keys (UpdateCockpit's: flight assist, a lock, flares, the number keys) as commands,
			// each carried out on every peer at its frame (CoopCockpitKey).
			if (cs::Is<FighterSeat>(held) && !ClickPanelOpen() && CoopPeer::FreshEdges(coopKeyTick))
			{
				cs::String me = Wire::I(peer->LocalSeat());
				if (Input.ToggleFlightAssist) peer->Submit(cs::Cat("k ", me, " fa"));
				if (Input.LockTarget) peer->Submit(cs::Cat("k ", me, " lock ", Wire::D(input.Aim.X), " ", Wire::D(input.Aim.Y), " ", Wire::D(input.Aim.Z)));
				if (Input.DropMine) peer->Submit(cs::Cat("k ", me, " mine"));
				if (Input.NumberKey >= 0) peer->Submit(cs::Cat("k ", me, " n ", Wire::I(Input.NumberKey)));
			}
		}
		else coopLastControl.clear();
		if (!coopStepped) return;
		session->Advance(dt * coopSimRate);
		auto still = HeldNonHelm();
		if (still != nullptr && still == held)
		{
			auto pose = held->PoseAt(Clock->Jd);
			auto roof = ToVector(pose.Up.SwapYZ());
			SpaceCamera->PilotView = Quaternion::LookRotation(seatAim, Vector3::Dot(roof, seatAim) > 0.99f ? up : roof);
		}
	}

	bool UnitySpaceApp::CoopRefusesTravel(const cs::Ref<StarSystem>& system)
	{
		(void)system;
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr || coopExecuting > 0) return false;
		cs::String other = CoopOtherAtControls(peer->LocalSeat());
		if (other.empty()) return false;
		ShowToast(cs::Cat("The party travels together: ", other, " is at the controls in ", ActiveSystem->Name));
		return true;
	}

	bool UnitySpaceApp::CoopPartyTrip(const cs::Ref<StarSystem>& system)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr) return false;
		if (coopExecuting > 0) return peer->IsClient(); // a client goes with the host's view: its Activate comes in a frame
		ShowToast(cs::Cat("Asking for the party's trip to ", system->Name, "..."));
		return CoopSends("travel", system->Name);
	}

	cs::String UnitySpaceApp::CoopOtherAtControls(int seat)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr) return cs::String();
		if (coopHelmSeat >= 0 && coopHelmSeat != seat && (!Piloted.IsNone() || InFtlCruise())) return peer->NameOf(coopHelmSeat);
		for (const auto& p : session->Players())
			if (p.Seat != seat && session->State()->SeatOf(p) != nullptr) return peer->NameOf(p.Seat);
		return cs::String();
	}

	bool UnitySpaceApp::CoopAtControls()
	{
		if (CoopPeer::Current() == nullptr) return AtControls();
		if (coopExecuting == 0 && AtControls()) return true;
		return !CoopOtherAtControls(-1).empty();
	}

	void UnitySpaceApp::CoopActivated(const cs::Ref<StarSystem>& system)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr || !peer->IsHost() || peer->Applying() || peer->Open() == nullptr || coopEverywhere > 0) return;
		peer->Open()->Post.push_back(cs::Cat("activate ", system->Name));
	}

	void UnitySpaceApp::CoopLoaded()
	{
		coopLeftValid = false;
		coopLastControl.clear();
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr || peer->Applying()) return;
		// US-T43: a helm in the host's own save stays with whoever flew the helm before it, else the host.
		if (Piloted.IsNone() && !InFtlCruise()) coopHelmSeat = -1;
		else if (coopHelmSeat < 0) coopHelmSeat = 0;
		if (peer->IsHost())
		{
			if (peer->Open() != nullptr) peer->Open()->Post.clear();
			peer->RequestSync("the host loaded a save", /*load:*/ true);
		}
	}

	void UnitySpaceApp::CoopApplyPost(const FrameRecord& rec)
	{
		for (const auto& p : rec.Post)
		{
			if (!Wire::StartsWith(p, "activate ")) continue;
			auto system = Galaxy->Find(p.substr(9));
			if (system == nullptr || system == ActiveSystem) continue;
			Activate(system);
			// US-T43: the host's view made the trip; this one arrives with it, on the system's overview (as StarTravel ends).
			if (!AtControls())
			{
				SetSystemVisible(true);
				FocusSystemOverview();
			}
			CoopPeer::Log(cs::Cat("arrived in ", system->Name, " with the host's frame ", rec.Frame));
		}
	}

	// ---------------------------------------------------------------- keys for seats and targets

	namespace
	{
		template <class T>
		int IndexOfRef(const cs::List<cs::Ref<T>>& list, const cs::Ref<T>& item)
		{
			for (int i = 0; i < list.Count(); i++)
				if (list[i] == item) return i;
			return -1;
		}

		/// C# List<StationSeat>.IndexOf: EqualityComparer.Default, so the seat's own Equals.
		int IndexOfStation(const cs::List<cs::Ref<StationSeat>>& list, const cs::Ref<StationSeat>& s)
		{
			for (int i = 0; i < list.Count(); i++)
				if (EmbodiableEquals(list[i], s)) return i;
			return -1;
		}
	}

	cs::String UnitySpaceApp::CoopSeatKey(const cs::Ref<IEmbodiable>& seat)
	{
		const auto& battles = World()->War->Battles;
		if (auto f = cs::As<FighterSeat>(seat))
		{
			int b = IndexOfRef(battles, f->Battle), u = IndexOfRef(f->Battle->Units, f->Unit);
			return b < 0 || u < 0 ? cs::String() : cs::Cat("F ", b, " ", u);
		}
		if (auto t = cs::As<MountedTurret>(seat))
		{
			int b = IndexOfRef(battles, t->Battle), u = IndexOfRef(t->Battle->Units, t->Unit);
			int m = t->Unit->Combat != nullptr ? IndexOfRef(t->Unit->Combat->Subsystems, t->Mount) : -1;
			return b < 0 || u < 0 || m < 0 ? cs::String() : cs::Cat("T ", b, " ", u, " ", m);
		}
		if (auto s = cs::As<StationSeat>(seat))
		{
			int i = IndexOfStation(Embodiables::StationsIn(World()), s);
			if (i >= 0) return cs::Cat("S ", i);
			cs::Ref<Battle> battle;
			auto unit = s->Unit(battle);
			int b = battle != nullptr ? IndexOfRef(battles, battle) : -1;
			int u = unit != nullptr && battle != nullptr ? IndexOfRef(battle->Units, unit) : -1;
			return b < 0 || u < 0 ? cs::String() : cs::Cat("SB ", b, " ", u);
		}
		if (auto w = cs::As<RepairWorker>(seat)) // US-T135: an EVA worker, by its hull and team
		{
			int b = IndexOfRef(battles, w->Battle), u = w->Battle != nullptr ? IndexOfRef(w->Battle->Units, w->Unit) : -1;
			return b < 0 || u < 0 ? cs::String() : cs::Cat("W ", b, " ", u, " ", w->Team);
		}
		return cs::String();
	}

	cs::Ref<IEmbodiable> UnitySpaceApp::CoopResolveSeat(const cs::String& key)
	{
		try
		{
			auto f = Wire::Split(key, ' ');
			const auto& battles = World()->War->Battles;
			auto battleAt = [&](int i) -> cs::Ref<Battle> { return i >= 0 && i < battles.Count() ? battles[i] : nullptr; };
			auto unitAt = [](const cs::Ref<Battle>& b, int i) -> cs::Ref<BattleUnit> { return b != nullptr && i >= 0 && i < b->Units.Count() ? b->Units[i] : nullptr; };
			if (f[0] == "F")
			{
				auto b = battleAt(Wire::ParseI(f.at(1)));
				auto u = unitAt(b, Wire::ParseI(f.at(2)));
				return u == nullptr ? nullptr : cs::Ref<IEmbodiable>(Embodiables::FighterOf(b, u));
			}
			if (f[0] == "T")
			{
				auto b = battleAt(Wire::ParseI(f.at(1)));
				auto u = unitAt(b, Wire::ParseI(f.at(2)));
				int m = Wire::ParseI(f.at(3));
				if (u == nullptr || u->Combat == nullptr || m < 0 || m >= u->Combat->Subsystems.Count()) return nullptr;
				auto mount = u->Combat->Subsystems[m];
				for (const auto& gun : Embodiables::TurretsOn(World(), b, u))
					if (gun->Mount == mount) return gun;
				return nullptr;
			}
			if (f[0] == "S")
			{
				auto all = Embodiables::StationsIn(World());
				int i = Wire::ParseI(f.at(1));
				return i >= 0 && i < all.Count() ? cs::Ref<IEmbodiable>(all[i]) : nullptr;
			}
			if (f[0] == "SB")
			{
				auto b = battleAt(Wire::ParseI(f.at(1)));
				return Embodiables::StationOf(World(), b, unitAt(b, Wire::ParseI(f.at(2))));
			}
			if (f[0] == "W") // US-T135
			{
				auto b = battleAt(Wire::ParseI(f.at(1)));
				auto u = unitAt(b, Wire::ParseI(f.at(2)));
				int team = Wire::ParseI(f.at(3));
				for (const auto& w : Embodiables::WorkersOn(World(), b, u))
					if (w->Team == team) return w;
				return nullptr;
			}
		}
		catch (const std::exception& e)
		{
			CoopPeer::Warn(cs::Cat("bad seat key '", key, "': ", e.what()));
		}
		return nullptr;
	}

	cs::String UnitySpaceApp::CoopTargetKey(const Target& t)
	{
		if (t.IsNone()) return "n";
		if (t.IsBody()) return cs::Cat("b ", t.Body);
		if (t.IsShip()) return cs::Cat("s ", t.Ship->Id);
		if (t.IsUnit())
		{
			int b = IndexOfRef(World()->War->Battles, t.Battle), u = t.Battle != nullptr ? IndexOfRef(t.Battle->Units, t.Unit) : -1;
			if (b >= 0 && u >= 0) return cs::Cat("u ", b, " ", u);
		}
		return cs::Cat("o ", ToString(t.Kind));
	}

	cs::String UnitySpaceApp::CoopTargetName(const cs::String& key)
	{
		try
		{
			auto f = Wire::Split(key, ' ');
			if (f[0] == "n") return cs::String();
			if (f[0] == "b")
			{
				int i = Wire::ParseI(f.at(1));
				return i >= 0 && i < Catalog()->Count() ? (*Catalog())[i]->Name : cs::String("a body");
			}
			if (f[0] == "s")
			{
				int i = Wire::ParseI(f.at(1));
				return i >= 0 && i < Fleet()->Ships().Count() ? Fleet()->Ships()[i]->Name : cs::String("a ship");
			}
			if (f[0] == "u")
			{
				int b = Wire::ParseI(f.at(1)), u = Wire::ParseI(f.at(2));
				const auto& bs = World()->War->Battles;
				return b >= 0 && b < bs.Count() && u >= 0 && u < bs[b]->Units.Count() ? bs[b]->Units[u]->Name() : cs::String("a unit");
			}
			if (f.size() > 1)
			{
				cs::String kind = f[1];
				for (auto& ch : kind) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
				return cs::Cat("a ", kind);
			}
			return "something";
		}
		catch (const std::exception&)
		{
			return "something";
		}
	}

	// ---------------------------------------------------------------- saves, digest, presence

	cs::String UnitySpaceApp::CoopSaveBlocker() const { return SaveBlocker(); }

	cs::List<uint8_t> UnitySpaceApp::CoopCaptureSave() { return SaveGame::ToBytes(CaptureSave(), "coop", "Co-op sync"); }

	void UnitySpaceApp::CoopApplySave(const cs::List<uint8_t>& bytes, const cs::String& state)
	{
		auto lines = Wire::Split(state, '\n');
		int helm = -1;
		std::vector<std::pair<int, cs::String>> seats;
		std::vector<cs::String> settings;
		bool keepView = false;
		for (const auto& l : lines)
		{
			if (Wire::StartsWith(l, "helm ")) helm = Wire::ParseI(l.substr(5));
			else if (Wire::StartsWith(l, "seat "))
			{
				auto f = Wire::Split(l, ' ', 3);
				seats.emplace_back(Wire::ParseI(f.at(1)), f.at(2));
			}
			else if (l == "keepview") keepView = true;
			else if (!l.empty()) settings.push_back(l);
		}
		int changed = Coop::CoopSettings::ApplySnapshot(settings);
		if (changed > 0) CoopPeer::Log(cs::Cat("took ", changed, " of the host's shared settings"));
		// A resync (not a load) leaves this player's view as it was: the same system, scale, selection and camera.
		cs::String system = ActiveSystem != nullptr ? ActiveSystem->Name : cs::String();
		cs::String selected = CoopTargetKey(Selected), focus = CoopTargetKey(SpaceCamera->FocusTarget);
		auto mode = SpaceCamera->Mode;
		DVec3 posePos = SpaceCamera->ScenePosition;
		Quaternion poseRot = SpaceCamera->Rotation;
		double keepGoal = ScaleGoal, keepFrom = scaleFrom;
		float keepT = scaleT;
		double keepBlend = Scale()->Blend();
		coopHelmSeat = helm;
		coopEverywhere++;
		coopShared++;
		try
		{
			ApplyLoaded(SaveGame::FromBytes(bytes));
		}
		catch (...)
		{
			coopShared--;
			coopEverywhere--;
			throw;
		}
		coopShared--;
		coopEverywhere--;
		if (Piloted.IsNone() && !InFtlCruise()) coopHelmSeat = -1;
		CoopWatchSession();
		if (keepView && system == ActiveSystem->Name)
		{
			ScaleGoal = keepGoal;
			scaleFrom = keepFrom;
			scaleT = keepT;
			Scale()->SetBlend(keepBlend);
			Scale()->Layout(Ephemeris(), bodyScene);
			UpdateRadii();
			Selected = CoopResolveTarget(selected);
			if (!CoopHelmMine() || Piloted.IsNone())
			{
				SpaceCamera->ExitFocus();
				SpaceCamera->SetPose(posePos, poseRot);
				Target f = CoopResolveTarget(focus);
				if (mode != Cameras::SpaceCamera::CameraMode::Free && !f.IsNone() && !f.IsUnit()) SpaceCamera->FocusOn(f);
			}
		}
		CoopRetake(seats);
	}

	void UnitySpaceApp::CoopRetake(const std::vector<std::pair<int, cs::String>>& seats)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (seats.empty() || peer == nullptr) return;
		ClockState keep = ClockState::Of(*Clock);
		for (const auto& [player, key] : seats)
		{
			auto seat = CoopResolveSeat(key);
			if (seat == nullptr)
			{
				CoopPeer::Log(cs::Cat(peer->NameOf(player), "'s seat '", key, "' is not in the loaded world: back in command"));
				continue;
			}
			int id = session->IdOf(seat);
			session->Submit(cs::New<TakeSeatCommand>(PlayerId(player), id));
			session->ApplyPending();
			peer->Retakes++;
			CoopPeer::Log(cs::Cat(peer->NameOf(player), " (player ", player + 1, ") keeps seat ", session->State()->SeatIdOf(PlayerId(player)), " (", seat->Name(),
				") through the sync"));
		}
		keep.ApplyTo(*Clock);
	}

	cs::String UnitySpaceApp::CoopSyncState(bool keepView)
	{
		cs::String sb;
		if (keepView) sb += "keepview\n";
		sb += cs::Cat("helm ", Piloted.IsNone() && !InFtlCruise() ? -1 : coopHelmSeat);
		for (const auto& p : session->Players())
		{
			auto held = session->State()->SeatOf(p);
			cs::String key = held != nullptr ? CoopSeatKey(held) : cs::String();
			if (!key.empty()) sb += cs::Cat("\nseat ", p.Seat, " ", key);
		}
		return sb;
	}

	cs::String UnitySpaceApp::CoopDigest()
	{
		cs::String sb;
		sb.reserve(64 * 1024);
		sb += "clock ";
		sb += (coopLeftValid ? coopLeft : ClockState::Of(*Clock)).Write();
		sb += '\n';
		sb += cs::Cat("active ", ActiveSystem->Name, "\n");
		for (const auto& sys : Galaxy->Systems)
		{
			const auto& w = sys->World;
			sb += cs::Cat("system ", sys->Name, " ", Wire::D(w->Jd), w->Coarse() ? " c " : " f ", w->Fleet->Ships().Count(), " ",
				w->Economy->Settlements.Count(), " ", w->War->Battles.Count(), " ", w->Events.Count());
			for (const auto& f : w->Fleet->Factions()) sb += cs::Cat(" ", Wire::D(f->Treasury));
			sb += '\n';
		}
		const auto& world = World();
		double jd = world->Jd;
		for (const auto& s : world->Fleet->Ships())
		{
			sb += cs::Cat("ship ", s->Id, " ", Core::ToString(s->Phase), s->Destroyed ? " x" : "");
			if (!s->Destroyed)
			{
				try
				{
					auto st = world->Fleet->StateOf(s, jd);
					sb += cs::Cat(" ", Wire::D(st.HelioKm.X), " ", Wire::D(st.HelioKm.Y), " ", Wire::D(st.HelioKm.Z));
				}
				catch (const std::exception&)
				{
					sb += " !";
				}
			}
			sb += '\n';
		}
		for (const auto& st : world->Economy->Settlements)
		{
			sb += cs::Cat("settlement ", st->Id, " ", Wire::D(st->Population));
			for (double k : st->Stock) sb += cs::Cat(" ", Wire::D(k));
			sb += '\n';
		}
		for (const auto& b : world->War->Battles)
			for (const auto& u : b->Units)
				sb += cs::Cat("unit ", u->Id, u->Present() ? " p " : " - ", Wire::D(u->Pos.X), " ", Wire::D(u->Pos.Y), " ", Wire::D(u->Pos.Z), " ",
					u->Combat != nullptr ? Wire::D(u->Combat->Hull) : cs::String("-"), "\n");
		for (const auto& p : session->Players()) sb += cs::Cat("seat ", p.Seat, " ", session->State()->SeatIdOf(p), "\n");
		// US-T43: the helm, as every peer flies it: who, which hull, where and how fast, its tanks and hold, the course.
		sb += cs::Cat("helm ", coopHelmSeat, " ", Piloted.IsNone() ? -1 : Piloted.Ship->Id);
		if (!Piloted.IsNone())
		{
			const auto& ps = Piloted.Ship;
			sb += cs::Cat(" ", pilotFrameBody, " ", Wire::D(pilotOffsetKm().X), " ", Wire::D(pilotOffsetKm().Y), " ", Wire::D(pilotOffsetKm().Z),
				" ", Wire::D(pilotVelKmS().X), " ", Wire::D(pilotVelKmS().Y), " ", Wire::D(pilotVelKmS().Z),
				" ", Wire::D(pilot().Forward.X), " ", Wire::D(pilot().Forward.Y), " ", Wire::D(pilot().Forward.Z),
				" ", Wire::D(ps->Propellant()), " ", !ps->Cargo.empty() ? ps->Cargo : cs::String("-"), " ", Wire::D(ps->Credits),
				pilotFlightAssistOff ? " fa-off" : " fa-on");
		}
		sb += cs::Cat(" ", Course != nullptr && Course->Destination != nullptr ? Course->Destination->Name : cs::String("-"), " ",
			PlottedCourse != nullptr ? "ftl" : "-", InFtlCruise() ? " cruise" : "", "\n");
		CoopDigestStatecraft(sb); // US-T47
		return Wire::Hex(Wire::Fnv1a64(sb));
	}

	void UnitySpaceApp::CoopDigestStatecraft(cs::String& sb)
	{
		const auto& polities = Galaxy->Polities;
		const auto& command = polities->Command;
		const auto& statecraft = polities->Statecraft;
		const auto& factions = polities->Factions();
		int player = -1;
		for (int i = 0; i < factions.Count(); i++)
			if (factions[i] == command->Player) player = i;
		sb += cs::Cat("polities ", player, " ", command->ObjectiveCount(), " ", statecraft->Pacts().Count(), " ", statecraft->Inbox().Count(), " ",
			statecraft->Record().Count(), "\n");
		for (int i = 0; i < factions.Count(); i++)
		{
			auto d = command->Find(factions[i]);
			if (d == nullptr) continue;
			sb += cs::Cat("directives ", i, " ", d->Production->Name, " / ", d->Build->Name, " / ", d->Research != nullptr ? d->Research->Name : cs::String("-"),
				" ", Wire::D(d->ResearchPoints));
			for (const auto& t : Core::ResearchTrack::All()) sb += cs::Cat(" ", d->Level(t));
			sb += '\n';
		}
		for (int i = 0; i < factions.Count(); i++)
		{
			if (!Core::Statecraft::Party(factions[i])) continue;
			for (int j = i + 1; j < factions.Count(); j++)
			{
				if (!Core::Statecraft::Party(factions[j])) continue;
				sb += cs::Cat("states ", i, " ", j, " ", static_cast<int>(polities->Diplomacy->Get(factions[i], factions[j])), " ",
					Wire::D(statecraft->Opinion(factions[i], factions[j])), " ", Wire::D(statecraft->Opinion(factions[j], factions[i])), "\n");
			}
		}
	}

	void UnitySpaceApp::CoopPresence(int& body, DVec3& offsetKm)
	{
		auto held = HeldNonHelm();
		if (held != nullptr)
		{
			auto pose = held->PoseAt(Clock->Jd);
			body = pose.FrameBody;
			offsetKm = pose.OffsetKm;
			return;
		}
		int frame = SpaceCamera->FrameAt(SpaceCamera->ScenePosition);
		body = frame;
		offsetKm = Scale()->InverseLocalOffset((*Catalog())[frame], SpaceCamera->ScenePosition - BodyScene(frame));
	}

	void UnitySpaceApp::CoopGui()
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr || !ShowHud || ScreenCleared) return;
		Color color = GUI::color;
		float x = Screen::width - 340.0f, y = 6.0f;
		GUI::color = Color(0.55f, 0.85f, 1.0f, 1.0f);
		GUI::Label(Rect(x, y, 334, 20), cs::Cat("CO-OP  \xC2\xB7  ", peer->IsHost() ? cs::String("hosting") : cs::Cat("joined ", peer->NameOf(0)),
			"  \xC2\xB7  frame ", peer->Frame()));
		y += 18;
		for (const auto& p : peer->Players)
		{
			if (p == nullptr || p->Left) continue;
			auto seat = session->State()->SeatOf(PlayerId(p->Seat));
			bool mine = p->Seat == peer->LocalSeat();
			// US-T136: players need not be allies. Each in their own seat colour (the colour their unit shines in), with how
			// they stand to you by the units you both hold; a rival's selection is their own business, and a rival running
			// dark beyond visual range is off your screen: no seat named, no marker.
			auto stance = mine ? PlayerStance::None : StanceBetween(LocalPlayerId(), PlayerId(p->Seat));
			bool rival = stance == PlayerStance::Hostile || stance == PlayerStance::Neutral;
			ShiningUnit unit;
			bool hidden = rival && ShiningOf(PlayerId(p->Seat), unit) && !unit.Shows();
			cs::String sel = rival ? cs::String() : CoopTargetName(p->Selection);
			bool helm = p->Seat == coopHelmSeat && !Piloted.IsNone(); // US-T43: the party's one helm
			cs::String tag = stance == PlayerStance::Hostile ? "  [HOSTILE]" : stance == PlayerStance::Ally ? "  [ALLY]" : stance == PlayerStance::Neutral ? "  [NEUTRAL]" : "";
			cs::String line = cs::Cat("P", p->Seat + 1, "  ", p->Name, p->Seat == 0 ? " (host)" : "", mine ? " (you)" : "", tag,
				"  \xC2\xB7  ", hidden ? cs::String("running dark") : helm ? cs::Cat("at the helm of ", Piloted.Ship->Name) : seat != nullptr ? seat->Name() : cs::String("in command"),
				!sel.empty() ? cs::Cat("  \xC2\xB7  ", sel) : cs::String());
			auto seatColor = Views::EmbodiedShine::SeatColor(p->Seat);
			GUI::color = Color(seatColor.r, seatColor.g, seatColor.b, 1.0f);
			GUI::Label(Rect(x, y, 334, 20), line);
			y += 18;
			if (mine || hidden || p->Body < 0 || p->Body >= Catalog()->Count() || !SystemVisible) continue;
			DVec3 scene = BodyScene(p->Body) + Scale()->LocalOffset((*Catalog())[p->Body], p->OffsetKm);
			Vector2 g;
			float depth;
			if (ToGui(scene, g, depth)) GUI::Label(Rect(g.x - 4, g.y - 10, 220, 20), cs::Cat("+ P", p->Seat + 1, " ", p->Name));
		}
		GUI::color = color;
	}

	// ==================================================================== US-T43: the helm, the cockpit, and the Core's other writers

	bool UnitySpaceApp::CoopHelmHere() const
	{
		CoopPeer* peer = CoopPeer::Current();
		return peer == nullptr || coopShared > 0 || coopHelmSeat == peer->LocalSeat();
	}

	bool UnitySpaceApp::CoopHelmMine() const
	{
		CoopPeer* peer = CoopPeer::Current();
		return peer == nullptr || coopHelmSeat == peer->LocalSeat();
	}

	bool UnitySpaceApp::CoopIsClient() const
	{
		CoopPeer* peer = CoopPeer::Current();
		return peer != nullptr && peer->IsClient();
	}

	bool UnitySpaceApp::CoopActing() const { return CoopPeer::Current() == nullptr || coopExecuting == 0 || coopExecMine; }

	void UnitySpaceApp::CoopShared(const std::function<void()>& step)
	{
		if (CoopPeer::Current() == nullptr)
		{
			step();
			return;
		}
		coopShared++;
		try { step(); }
		catch (...) { coopShared--; throw; }
		coopShared--;
	}

	bool UnitySpaceApp::CoopSends(const cs::String& verb, const cs::String& args)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr || coopExecuting > 0 || coopShared > 0 || peer->Applying()) return false;
		peer->Submit(cs::Cat("x ", Wire::I(peer->LocalSeat()), " ", verb, args.empty() ? cs::String() : cs::Cat(" ", args)));
		return true;
	}

	namespace
	{
		cs::String UpFrameText(const Quaternion& q) { return cs::Cat(Wire::F(q.x), ",", Wire::F(q.y), ",", Wire::F(q.z), ",", Wire::F(q.w)); }

		Quaternion ParseUpFrame(const cs::String& s)
		{
			auto c = Wire::Split(s, ',');
			return Quaternion(Wire::ParseF(c.at(0)), Wire::ParseF(c.at(1)), Wire::ParseF(c.at(2)), Wire::ParseF(c.at(3)));
		}

		cs::String JoinFrom(const std::vector<cs::String>& a, size_t from)
		{
			cs::String s;
			for (size_t i = from; i < a.size(); i++)
			{
				if (i > from) s += ' ';
				s += a[i];
			}
			return s;
		}

		/// Scope counters as C#'s try/finally: undone however the step leaves.
		struct Count
		{
			int& n;
			bool on;
			Count(int& counter, bool when = true) : n(counter), on(when) { if (on) n++; }
			~Count() { if (on) n--; }
		};
	}

	bool UnitySpaceApp::CoopSpawns(const cs::String& verb, const cs::String& args, int frame, DVec3 offsetKm, DVec3 forward)
	{
		cs::String at = cs::Cat(Wire::I(frame), " ", Wire::D(offsetKm.X), " ", Wire::D(offsetKm.Y), " ", Wire::D(offsetKm.Z), " ",
			Wire::D(forward.X), " ", Wire::D(forward.Y), " ", Wire::D(forward.Z), " ", UpFrameText(SpaceCamera->UpFrame));
		return CoopSends(verb, args.empty() ? at : cs::Cat(args, " ", at));
	}

	// ---------------------------------------------------------------- the helm: taken, released, flown

	bool UnitySpaceApp::CoopRoutesHelmTake(const Target& t)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr || coopShared > 0 || coopExecuting > 0 || peer->Applying() || !t.IsShip()) return false;
		if (coopHelmSeat >= 0 && coopHelmSeat != peer->LocalSeat() && (!Piloted.IsNone() || InFtlCruise()))
		{
			ShowToast(cs::Cat(peer->NameOf(coopHelmSeat), " has the helm: the party flies one hull (man a gun, a fighter or a station meanwhile)"));
			return true;
		}
		peer->Submit(cs::Cat("h ", Wire::I(peer->LocalSeat()), " take ", Wire::I(t.Ship->Id), " ", UpFrameText(SpaceCamera->UpFrame)));
		ShowToast(cs::Cat("Asking for the helm of ", t.Ship->Name, "..."));
		return true;
	}

	bool UnitySpaceApp::CoopRoutesHelmRelease()
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr || coopShared > 0 || coopExecuting > 0 || peer->Applying()) return false;
		if (coopHelmSeat == peer->LocalSeat()) peer->Submit(cs::Cat("h ", Wire::I(peer->LocalSeat()), " release"));
		return true;
	}

	void UnitySpaceApp::CoopHelmEntry(CoopPeer& peer, int seat, const std::vector<cs::String>& f)
	{
		bool mine = seat == peer.LocalSeat();
		Count shared(coopShared), everywhere(coopEverywhere), mute(coopMuteToasts, !mine);
		if (f.at(2) == "release")
		{
			if (coopHelmSeat != seat) return;
			if (!Piloted.IsNone()) ExitPiloting();
			coopHelmSeat = -1;
			peer.ForgetPilot(seat);
			CoopPeer::Log(cs::Cat(peer.NameOf(seat), " (player ", seat + 1, ") hands the helm back at frame ", peer.Frame()));
			return;
		}
		int id = Wire::ParseI(f.at(3));
		cs::Ref<Ship> ship = id >= 0 && id < Fleet()->Ships().Count() ? Fleet()->Ships()[id] : nullptr;
		cs::String refused = ship == nullptr || ship->Destroyed ? cs::String("that hull is gone")
			: coopHelmSeat >= 0 && coopHelmSeat != seat && (!Piloted.IsNone() || InFtlCruise()) ? cs::Cat(peer.NameOf(coopHelmSeat), " has the helm")
			: !Embodiables::HelmOf(World(), ship)->CanEmbody() ? cs::String("can't take the helm of that ship right now") : cs::String();
		if (!refused.empty())
		{
			CoopPeer::Log(cs::Cat(peer.NameOf(seat), "'s take of the helm refused: ", refused));
			ShowToast(cs::Cat("Can't: ", refused));
			return;
		}
		if (!Piloted.IsNone()) ExitPiloting();
		coopHelmSeat = seat;
		CoopUpFrame = f.size() > 4 ? ParseUpFrame(f[4]) : Quaternion::identity();
		try { EnterPiloting(Target::OfShip(ship)); }
		catch (...) { CoopUpFrame.reset(); throw; }
		CoopUpFrame.reset();
		CoopPeer::Log(cs::Cat(peer.NameOf(seat), " (player ", seat + 1, ") takes the helm of ", ship->Name, " (ship ", ship->Id, ") at frame ", peer.Frame()));
	}

	Coop::PilotFrame UnitySpaceApp::CoopLocalPilotFrame()
	{
		auto p = Coop::PilotFrame::Of(Input, [](float y) { return Cameras::SpaceCamera::LookY(y); });
		p.Sensitivity = Cameras::SpaceCamera::LookSensitivity();
		p.PanelOpen = ClickPanelOpen();
		p.Selected = CoopTargetKey(Selected);
		p.Hovered = CoopTargetKey(Hovered);
		p.UpFrame = SpaceCamera->UpFrame;
		return p;
	}

	void UnitySpaceApp::CoopHostPilotFrame(CoopPeer& peer, FrameRecord& rec)
	{
		coopPilotFrame.reset();
		if (coopHelmSeat < 0) return;
		auto frame = coopHelmSeat == peer.LocalSeat() ? CoopLocalPilotFrame() : peer.HostPilotFor(coopHelmSeat);
		cs::String text = frame.Write();
		rec.Entries.push_back(cs::Cat("pi ", Wire::I(coopHelmSeat), " ", text));
		coopPilotFrame = Coop::PilotFrame::Read(text); // the same numbers the clients read
		CoopPilotFrames++;
	}

	void UnitySpaceApp::CoopUpdatePiloting(float dt)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr)
		{
			UpdatePiloting(dt);
			return;
		}
		if (peer->IsClient() && peer->ClientRecord == nullptr) return;
		if (peer->IsClient() && coopHelmSeat == peer->LocalSeat()) peer->SendPilot(CoopLocalPilotFrame());
		bool mine = CoopHelmMine();
		Coop::PilotFrame frame = coopHelmSeat >= 0 && coopPilotFrame.has_value() ? *coopPilotFrame : Coop::PilotFrame::Idle();
		auto live = Coop::PilotFrame::Snapshot(Input);
		Target selected = Selected;
		Target hovered = Hovered;
		auto view = SpaceCamera->PilotView;
		Target lockOn = SpaceCamera->PilotLockTarget;
		auto eye = SpaceCamera->PilotEye;
		frame.ApplyTo(Input, [](float y) { return Cameras::SpaceCamera::LookY(y); });
		coopPanelOverride = frame.PanelOpen;
		CoopLookSensitivity = coopHelmSeat >= 0 ? frame.Sensitivity : std::numeric_limits<float>::quiet_NaN();
		CoopUpFrame = frame.UpFrame;
		Target flownSelected = Selected = CoopResolveTarget(frame.Selected);
		Hovered = CoopResolveTarget(frame.Hovered);
		auto restore = [&]
		{
			Coop::PilotFrame::Restore(Input, live);
			coopPanelOverride.reset();
			CoopLookSensitivity = std::numeric_limits<float>::quiet_NaN();
			CoopUpFrame.reset();
			// What the loop selected (a gate's arriving hull) is the holder's to keep; the rest put back.
			if (!mine || Selected == flownSelected) Selected = selected;
			Hovered = hovered;
			if (!mine)
			{
				SpaceCamera->PilotView = view;
				SpaceCamera->PilotLockTarget = lockOn;
				SpaceCamera->PilotEye = eye;
			}
			if (Piloted.IsNone() && !InFtlCruise() && coopHelmSeat >= 0)
			{
				CoopPeer::Log(cs::Cat(peer->NameOf(coopHelmSeat), "'s helm ends at frame ", peer->Frame()));
				peer->ForgetPilot(coopHelmSeat);
				coopHelmSeat = -1;
			}
		};
		{
			Count shared(coopShared), everywhere(coopEverywhere), mute(coopMuteToasts, !mine);
			try { UpdatePiloting(dt); }
			catch (...)
			{
				restore();
				throw;
			}
		}
		restore();
	}

	Target UnitySpaceApp::CoopResolveTarget(const cs::String& key)
	{
		try
		{
			auto f = Wire::Split(key.empty() ? cs::String("n") : key, ' ');
			if (f[0] == "b")
			{
				int i = Wire::ParseI(f.at(1));
				return i >= 0 && i < Catalog()->Count() ? Target::OfBody(i) : Target::None();
			}
			if (f[0] == "s")
			{
				int i = Wire::ParseI(f.at(1));
				return i >= 0 && i < Fleet()->Ships().Count() && !Fleet()->Ships()[i]->Destroyed ? Target::OfShip(Fleet()->Ships()[i]) : Target::None();
			}
			if (f[0] == "u")
			{
				int b = Wire::ParseI(f.at(1)), u = Wire::ParseI(f.at(2));
				const auto& bs = World()->War->Battles;
				return b >= 0 && b < bs.Count() && u >= 0 && u < bs[b]->Units.Count() ? Target::OfUnit(bs[b], bs[b]->Units[u]) : Target::None();
			}
			return Target::None();
		}
		catch (const std::exception&)
		{
			return Target::None();
		}
	}

	// ---------------------------------------------------------------- a frame's app entries

	bool UnitySpaceApp::CoopAppEntry(CoopPeer& peer, const cs::String& e)
	{
		size_t sp = e.find(' ');
		cs::String kind = sp == cs::String::npos ? e : e.substr(0, sp);
		if (kind == "a")
		{
			auto a = Wire::Split(e, ' ', 4);
			if (a.size() == 4 && a[2] == "load" && peer.IsHost())
			{
				ShowToast(cs::Cat(peer.NameOf(Wire::ParseI(a[1])), " asks to load ", SlotName(a[3])));
				RequestLoad(a[3]);
			}
			return false; // the roster logs it
		}
		if (kind != "h" && kind != "pi" && kind != "k" && kind != "x" && kind != "set") return false;
		session->ApplyPending(); // the session's commands before this one, in the frame's order
		try
		{
			if (kind == "set")
			{
				auto s = Wire::Split(e, ' ');
				if (s.size() >= 3) Coop::CoopSettings::Apply(s[1], s[2]);
				return true;
			}
			auto f = Wire::Split(e, ' ', kind == "x" ? 4 : kind == "pi" ? 3 : 5);
			int seat = Wire::ParseI(f.at(1));
			if (kind == "h") CoopHelmEntry(peer, seat, Wire::Split(e, ' '));
			else if (kind == "pi") { if (peer.IsClient() && seat == coopHelmSeat) coopPilotFrame = Coop::PilotFrame::Read(f.at(2)); }
			else if (kind == "k") CoopCockpitKey(peer, seat, Wire::Split(e, ' '));
			else if (kind == "x") CoopExecute(peer, seat, f.at(2), f.size() > 3 ? f[3] : cs::String());
		}
		catch (const std::exception& x)
		{
			CoopPeer::Warn(cs::Cat("entry '", e.size() > 80 ? e.substr(0, 80) : e, "' failed: ", x.what()));
		}
		return true;
	}

	void UnitySpaceApp::CoopCockpitKey(CoopPeer& peer, int seat, const std::vector<cs::String>& f)
	{
		auto cockpit = cs::As<FighterSeat>(session->State()->SeatOf(PlayerId(seat)));
		if (cockpit == nullptr)
		{
			CoopPeer::Log(cs::Cat(peer.NameOf(seat), "'s cockpit key '", f.at(2), "' with no cockpit held: ignored"));
			return;
		}
		bool mine = seat == peer.LocalSeat();
		auto live = Coop::PilotFrame::Snapshot(Input);
		Coop::PilotFrame::Idle().ApplyTo(Input, nullptr);
		DVec3 aim;
		if (f[2] == "fa") Input.ToggleFlightAssist = true;
		else if (f[2] == "lock")
		{
			Input.LockTarget = true;
			aim = DVec3(Wire::ParseD(f.at(3)), Wire::ParseD(f.at(4)), Wire::ParseD(f.at(5)));
		}
		else if (f[2] == "mine") Input.DropMine = true;
		else if (f[2] == "n") Input.NumberKey = Wire::ParseI(f.at(3));
		{
			Count shared(coopShared), mute(coopMuteToasts, !mine);
			try { UpdateCockpit(cockpit, aim); }
			catch (...)
			{
				Coop::PilotFrame::Restore(Input, live);
				throw;
			}
		}
		Coop::PilotFrame::Restore(Input, live);
		CoopCockpitKeys++;
		cs::String what = f[2];
		if (f.size() > 3) what += cs::Cat(" ", f[3]);
		CoopPeer::Log(cs::Cat(peer.NameOf(seat), " (player ", seat + 1, ") cockpit key '", what, "' on ", cockpit->Name(), " at frame ", peer.Frame()));
	}

	void UnitySpaceApp::CoopExecute(CoopPeer& peer, int seat, const cs::String& verb, const cs::String& args)
	{
		bool mine = seat == peer.LocalSeat();
		std::vector<cs::String> a = args.empty() ? std::vector<cs::String>() : Wire::Split(args, ' ');
		bool wasMine = coopExecMine;
		coopExecMine = mine;
		struct Restore { bool& b; bool v; ~Restore() { b = v; } } restoreMine{ coopExecMine, wasMine };
		Count executing(coopExecuting), shared(coopShared), everywhere(coopEverywhere), mute(coopMuteToasts, !mine);
		if (verb == "spawn") CoopSpawnShip(peer, seat, static_cast<ShipClass>(Wire::ParseI(a.at(0))), a.at(1) == "1", a, 2);
		else if (verb == "practice") CoopSpawnPractice(peer, seat, a, 0);
		else if (verb == "travel")
		{
			auto to = Galaxy->Find(args);
			if (to != nullptr)
			{
				cs::String other = CoopOtherAtControls(seat);
				if (!other.empty()) ShowToast(cs::Cat("The party travels together: ", other, " is at the controls"));
				else
				{
					ShowToast(cs::Cat(mine ? cs::String() : cs::Cat(peer.NameOf(seat), " takes the party to "), mine ? "Setting out for " : "", to->Name));
					TravelTo(to);
				}
			}
		}
		else if (verb == "plot") PlotCourse(Galaxy->Find(args));
		else if (verb == "autopilot") EngageCourseAutopilot();
		else if (verb == "crew")
		{
			int id = Wire::ParseI(a.at(0));
			cs::Ref<Ship> ship = id >= 0 && id < Fleet()->Ships().Count() ? Fleet()->Ships()[id] : nullptr;
			auto to = Galaxy->Find(JoinFrom(a, 2));
			if (ship != nullptr && to != nullptr) StartCrewCourse(ship, to, a.at(1) == "1");
		}
		else if (verb == "tanker") CallTanker();
		else if (verb == "ftlplot")
		{
			cs::String name = JoinFrom(a, 1);
			auto system = Galaxy->Find(name);
			cs::Ref<FtlTarget> target = system != nullptr ? FtlTarget::OfStar(system) : nullptr;
			if (target == nullptr)
				for (const auto& star : Galaxy->NearbyStars)
					if (star.Name == name) { target = FtlTarget::OfStar(star); break; }
			if (target != nullptr) PlotFtlCourse(target, a.at(0) == "1");
		}
		else if (verb == "ftlclear") ClearFtlCourse();
		else if (verb == "ftlauto") EngageFtlAutopilot();
		else if (verb == "reset")
		{
			CoopNow = Wire::ParseD(a.at(0));
			try { ResetToNow(); }
			catch (...) { CoopNow.reset(); throw; }
			CoopNow.reset();
		}
		else if (!hud->CoopExecute(verb, a) && !UI::GalaxyMapScreen::CoopExecute(this, verb, a)) // US-T47: the galaxy map's screens and the hail
			CoopPeer::Warn(cs::Cat("unknown action '", verb, "' from player ", seat + 1));
		CoopActions++;
		CoopPeer::Log(cs::Cat(peer.NameOf(seat), " (player ", seat + 1, ") action '", verb,
			!args.empty() ? cs::Cat(" ", args.size() > 60 ? cs::Cat(args.substr(0, 60), "...") : args) : cs::String(), "' at frame ", peer.Frame()));
	}

	namespace
	{
		void ReadAt(const std::vector<cs::String>& a, int at, int& frame, DVec3& offsetKm, DVec3& forward, Quaternion& up)
		{
			frame = Wire::ParseI(a.at(at));
			offsetKm = DVec3(Wire::ParseD(a.at(at + 1)), Wire::ParseD(a.at(at + 2)), Wire::ParseD(a.at(at + 3)));
			forward = DVec3(Wire::ParseD(a.at(at + 4)), Wire::ParseD(a.at(at + 5)), Wire::ParseD(a.at(at + 6)));
			up = static_cast<int>(a.size()) > at + 7 ? ParseUpFrame(a[at + 7]) : Quaternion::identity();
		}
	}

	void UnitySpaceApp::CoopSpawnShip(CoopPeer& peer, int seat, ShipClass cls, bool hostile, const std::vector<cs::String>& a, int at)
	{
		int frame;
		DVec3 offsetKm, forward;
		Quaternion up;
		ReadAt(a, at, frame, offsetKm, forward, up);
		auto faction = hostile ? World()->Pirates : World()->Sol;
		int spawned = ++spawnedCount;
		cs::String name = cs::Cat("Sandbox ", cls, " ", spawned);
		auto ship = Fleet()->SpawnAt(name, cls, faction, frame, offsetKm, forward, Clock->Jd);
		ship->Ports = World()->FreeShipPorts(); // not a hidden haven's rock, as for every free ship
		CoopPeer::Log(cs::Cat(peer.NameOf(seat), " spawns ", ship->Name, " (ship ", ship->Id, ") at frame ", peer.Frame()));
		CoopHelmEntry(peer, seat, { "h", Wire::I(seat), "take", Wire::I(ship->Id), UpFrameText(up) });
	}

	void UnitySpaceApp::CoopSpawnPractice(CoopPeer& peer, int seat, const std::vector<cs::String>& a, int at)
	{
		int frame;
		DVec3 offsetKm, forward;
		Quaternion up;
		ReadAt(a, at, frame, offsetKm, forward, up);
		double jd = Clock->Jd;
		auto frameHelio = Ephemeris()->Helio(frame);
		auto flown = !Piloted.IsNone() ? Piloted.Ship : nullptr;
		if (flown != nullptr && coopHelmSeat == seat) ExitPiloting(); // the practice fighter flown is brought over too
		auto ship = ReusablePracticeFighter();
		CollectPracticeObstacles(ship);
		double lengthKm = ship ? ship->LengthKm() : CombatSpec::Of(ShipClass::Fighter)->LengthKm;
		auto spot = PracticeSpawn::Clear(frameHelio + offsetKm, forward, lengthKm, practiceObstacles) - frameHelio;
		if (ship != nullptr)
		{
			ship->Combat->Repair(1);
			ship->Combat->FreeResupply();
			ship->SetPropellant(ship->PropellantCapacity());
			ship->OutOfPropellant = false;
			ship->StrandedInFlight = false;
			Fleet()->PinAt(ship, frame, spot, forward, jd);
		}
		else
		{
			ship = Fleet()->SpawnAt(PracticeSpawn::Name, ShipClass::Fighter, World()->Sol, frame, spot, forward, jd);
			ship->Ports = World()->FreeShipPorts();
		}
		CoopHelmEntry(peer, seat, { "h", Wire::I(seat), "take", Wire::I(ship->Id), UpFrameText(up) });
		ShowToast(cs::Cat("Practice fighter: ", ship->Name, " at the camera  (Shift+V again to bring it to the camera, P to hand it back)"));
	}

	// ---------------------------------------------------------------- loads

	bool UnitySpaceApp::CoopRoutesLoad(const cs::String& slot)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr || peer->IsHost() || peer->Applying() || !peer->Running()) return false;
		peer->RequestLoad(slot);
		ShowSaves = false;
		ShowToast(cs::Cat("Asked ", peer->NameOf(0), " (the host) to load ", SlotName(slot), ": the party loads the host's save"));
		return true;
	}

	void UnitySpaceApp::CoopLoadingHere(const std::function<void()>& load)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr)
		{
			load();
			return;
		}
		if (coopHelmSeat < 0) coopHelmSeat = peer->LocalSeat();
		Count shared(coopShared);
		load();
	}
}
