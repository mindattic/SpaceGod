// US-T44: co-op's side of the app (declarations in UnitySpaceApp.Coop.inl), the C# track's Runtime/UnitySpaceApp.Coop.cs
// (US-T39) member for member. The ported originals call in here only at lines marked US-T44, and every call is the
// original's own behaviour when Coop::CoopPeer::Current() is null (single-player).

#include "UnitySpaceApp.h"

#include "Coop/CoopPeer.h"
#include "Camera/SpaceCamera.h"
#include "UnityCompat/UnityGui.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/War.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/Embodiment.h"
#include "Embodiment/FighterSeat.h"
#include "Embodiment/MountedTurret.h"
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
		for (const auto& e : entries)
		{
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
		SpaceCamera->EnterPilotChase(SeatTarget(next), cs::Is<FighterSeat>(next) ? 7 : 2.5);
		if (auto gun = cs::As<MountedTurret>(next))
		{
			SpaceCamera->PilotView = Quaternion::LookRotation(seatAim, MountRoof(pose, seatAim));
			SpaceCamera->PilotEye = [this, gun]() { return MountEye(gun); };
		}
		else SpaceCamera->PilotEye = nullptr;
		ShowToast(cs::Cat(cs::Is<FighterSeat>(next) ? "Flying " : "Manning ", next->Name(), ", ", Clock->Warp().Label, SeatKeys(next)));
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
			EmbodimentInput input;
			input.Aim = DVec3(seatAim.x, seatAim.y, seatAim.z).SwapYZ();
			input.Throttle = Mathf::Clamp(Input.Move.z, -1.0f, 1.0f);
			input.StrafeRight = Mathf::Clamp(Input.Move.x, -1.0f, 1.0f);
			input.StrafeUp = Mathf::Clamp(Input.Move.y, -1.0f, 1.0f);
			input.Roll = Mathf::Clamp(Input.Roll, -1.0f, 1.0f);
			input.Boost = Input.Boost;
			input.Primary = Input.Fire && !ClickPanelOpen();
			input.Secondary = Input.LookHeld && !ClickPanelOpen();
			cs::String line = CommandCodec::Write(cs::New<SeatControlCommand>(session->LocalPlayer(), input));
			if (line != coopLastControl)
			{
				coopLastControl = line;
				peer->Submit(cs::Cat("s ", line));
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

	bool UnitySpaceApp::CoopRefusesHelm()
	{
		if (CoopPeer::Current() == nullptr) return false;
		ShowToast("The helm is single-player in this build: in co-op man a gun, a fighter or a station (' ; / F9)");
		return true;
	}

	bool UnitySpaceApp::CoopRefusesTravel()
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr || peer->IsHost()) return false;
		ShowToast(cs::Cat("In co-op ", peer->NameOf(0), " (the host) picks the star system"));
		return true;
	}

	void UnitySpaceApp::CoopActivated(const cs::Ref<StarSystem>& system)
	{
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr || !peer->IsHost() || peer->Applying() || peer->Open() == nullptr) return;
		peer->Open()->Post.push_back(cs::Cat("activate ", system->Name));
	}

	void UnitySpaceApp::CoopLoaded()
	{
		coopLeftValid = false;
		coopLastControl.clear();
		CoopPeer* peer = CoopPeer::Current();
		if (peer == nullptr || peer->Applying()) return;
		if (peer->IsHost())
		{
			if (peer->Open() != nullptr) peer->Open()->Post.clear();
			peer->RequestSync("the host loaded a save");
		}
	}

	void UnitySpaceApp::CoopApplyPost(const FrameRecord& rec)
	{
		for (const auto& p : rec.Post)
		{
			if (!Wire::StartsWith(p, "activate ")) continue;
			auto system = Galaxy->Find(p.substr(9));
			if (system != nullptr && system != ActiveSystem) Activate(system);
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

	void UnitySpaceApp::CoopApplySave(const cs::List<uint8_t>& bytes)
	{
		ApplyLoaded(SaveGame::FromBytes(bytes));
		CoopWatchSession();
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
		return Wire::Hex(Wire::Fnv1a64(sb));
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
			cs::String sel = CoopTargetName(p->Selection);
			cs::String line = cs::Cat("P", p->Seat + 1, "  ", p->Name, p->Seat == 0 ? " (host)" : "", p->Seat == peer->LocalSeat() ? " (you)" : "",
				"  \xC2\xB7  ", seat != nullptr ? seat->Name() : cs::String("in command"), !sel.empty() ? cs::Cat("  \xC2\xB7  ", sel) : cs::String());
			GUI::Label(Rect(x, y, 334, 20), line);
			y += 18;
			if (p->Seat == peer->LocalSeat() || p->Body < 0 || p->Body >= Catalog()->Count() || !SystemVisible) continue;
			DVec3 scene = BodyScene(p->Body) + Scale()->LocalOffset((*Catalog())[p->Body], p->OffsetKm);
			Vector2 g;
			float depth;
			if (ToGui(scene, g, depth)) GUI::Label(Rect(g.x - 4, g.y - 10, 220, 20), cs::Cat("+ P", p->Seat + 1, " ", p->Name));
		}
		GUI::color = color;
	}
}
