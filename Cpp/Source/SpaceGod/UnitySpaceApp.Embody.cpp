// US-T135: the universal embody layer (declarations in UnitySpaceApp.Embody.inl).

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "Combat/Battle.h"
#include "Coop/CoopPeer.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/Embodiment.h"
#include "Embodiment/RepairWorker.h"
#include "Embodiment/ShipHelm.h"
#include "Embodiment/StationSeat.h"
#include "Session/ISession.h"
#include "Session/LocalSession.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	// ---------------------------------------------------------------- who embodies what

	cs::List<UnitySpaceApp::EmbodiedUnit> UnitySpaceApp::EmbodiedNow()
	{
		cs::List<EmbodiedUnit> list;
		for (const auto& h : session->State()->Held())
		{
			if (h.Seat == nullptr) continue;
			EmbodiedUnit e;
			e.Player = h.Player;
			e.Seat = h.Seat;
			e.Subject = h.Seat->Subject();
			list.Add(e);
		}
		// The helm is the pilot loop's: in co-op, the helm holder's (every peer flies it on their hands); alone, the local player's.
		if (!Piloted.IsNone() && Piloted.Ship != nullptr && World() != nullptr)
		{
			PlayerId who = Coop::CoopPeer::Active() ? (CoopHelmSeat() >= 0 ? PlayerId(CoopHelmSeat()) : PlayerId::None()) : session->LocalPlayer();
			if (!who.IsNone())
			{
				auto helm = Embodiables::HelmOf(World(), Piloted.Ship);
				EmbodiedUnit e;
				e.Player = who;
				e.Seat = helm;
				e.Subject = helm->Subject();
				e.Helm = true;
				list.Add(e);
			}
		}
		return list;
	}

	PlayerId UnitySpaceApp::EmbodiedBy(const cs::Ref<BattleUnit>& unit)
	{
		if (unit == nullptr) return PlayerId::None();
		for (const auto& e : EmbodiedNow())
			if (e.Subject.Unit == unit || (unit->Ship != nullptr && e.Subject.Ship == unit->Ship)) return e.Player;
		return PlayerId::None();
	}

	PlayerId UnitySpaceApp::EmbodiedBy(const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr) return PlayerId::None();
		for (const auto& e : EmbodiedNow())
			if (e.Subject.Ship == ship || (e.Subject.Unit != nullptr && e.Subject.Unit->Ship == ship)) return e.Player;
		return PlayerId::None();
	}

	bool UnitySpaceApp::HeldRolePanel(EmbodimentRolePanel& panel)
	{
		auto held = HeldSeat();
		if (held == nullptr) return false;
		panel = held->RolePanel();
		return true;
	}

	// ---------------------------------------------------------------- the embody action

	bool UnitySpaceApp::PassesEmbodyGate(const cs::Ref<IEmbodiable>& next)
	{
		auto verdict = Embodiables::MayEmbody(next);
		if (verdict.Ok) return true;
		ShowToast(cs::Cat("Can't embody ", next != nullptr ? next->Name() : cs::String("that"), ": ", verdict.Reason));
		return false;
	}

	cs::Ref<IEmbodiable> UnitySpaceApp::EmbodiableAt(const Target& t)
	{
		if (World() == nullptr) return nullptr;
		if (t.IsShip()) return Embodiables::HelmOf(World(), t.Ship);
		if (t.IsUnit())
		{
			if (t.Battle == nullptr || t.Battle->Over) return nullptr;
			if (auto seat = Embodiables::SeatOf(World(), t.Battle, t.Unit)) return seat;
			return Embodiables::StationOf(World(), t.Battle, t.Unit);
		}
		cs::Ref<Battle> battle;
		auto platform = DefencePlatformAt(t, battle);
		return platform != nullptr ? cs::Ref<IEmbodiable>(Embodiables::StationOf(World(), battle, platform)) : nullptr;
	}

	bool UnitySpaceApp::EmbodyTarget()
	{
		auto t = !Selected.IsNone() ? Selected : Hovered;
		auto next = EmbodiableAt(t);
		if (next == nullptr && !Hovered.IsNone() && !(t == Hovered)) next = EmbodiableAt(Hovered);
		if (next == nullptr) return false;
		TakeSeat(next);
		return true;
	}

	cs::Ref<BattleUnit> UnitySpaceApp::HullInHand(cs::Ref<Battle>& battle)
	{
		battle = nullptr;
		auto held = HeldNonHelm();
		// A seat on a hull (a gun, a whole station, a worker): that hull. A launched craft's cockpit is its own small craft.
		if (held != nullptr && held->InputProfile() != EmbodimentInputProfile::Cockpit)
		{
			auto sub = held->Subject();
			if (sub.Unit != nullptr && sub.Battle != nullptr)
			{
				battle = sub.Battle;
				return sub.Unit;
			}
		}
		if (IsPiloting() && Piloted.Ship && Piloted.Ship->Battle != nullptr)
		{
			battle = Piloted.Ship->Battle;
			return battle->UnitOf(Piloted.Ship);
		}
		if (Selected.IsShip() && Selected.Ship->Battle != nullptr)
		{
			battle = Selected.Ship->Battle;
			return battle->UnitOf(Selected.Ship);
		}
		if (Selected.IsUnit() && Selected.Battle != nullptr && !Selected.Battle->Over && Selected.Unit->Kind != UnitKind::Platform)
		{
			battle = Selected.Battle;
			return Selected.Unit;
		}
		return DefencePlatformAt(Selected, battle);
	}

	void UnitySpaceApp::CycleWorker()
	{
		cs::Ref<Battle> battle;
		auto unit = HullInHand(battle);
		if (unit == nullptr)
		{
			ShowToast("Fly or select a capital ship or station in a fight (or hold one of its guns) to go out on its hull");
			return;
		}
		cs::List<cs::Ref<IEmbodiable>> workers;
		for (const auto& w : Embodiables::CrewOn(World(), battle, unit)) workers.Add(w);
		if (workers.Count() == 0)
		{
			ShowToast(cs::Cat("The ", unit->Name(), " has no EVA crew to send out (capital ships and stations do)"));
			return;
		}
		TakeSeat(Embodiables::Cycle(workers, HeldNonHelm(), 1));
	}

	// ---------------------------------------------------------------- the camera rig

	void UnitySpaceApp::RideCameraRig(const cs::Ref<IEmbodiable>& next, const EmbodimentPose& pose, bool switching)
	{
		auto rig = next->CameraRig();
		SpaceCamera->EnterPilotChase(SeatTarget(next), rig.ChaseRadii);
		if (switching) SpaceCamera->Hurry();
		if (rig.Kind == EmbodimentCameraKind::Chase)
		{
			SpaceCamera->PilotEye = nullptr;
			return;
		}
		// The eye on the hull (U.SPC-T445 a gun's mount; US-T135 a worker's shoulder), looking along the stick.
		SpaceCamera->PilotView = Quaternion::LookRotation(seatAim, MountRoof(pose, seatAim));
		SpaceCamera->PilotEye = [this, next]() { return RigEye(next); };
	}

	DVec3 UnitySpaceApp::RigEye(const cs::Ref<IEmbodiable>& s)
	{
		auto sub = s->Subject();
		auto rig = s->CameraRig();
		if (sub.Unit == nullptr || !sub.HasPoint) return SeatScene(s);
		auto host = SeatTarget(s);
		auto offset = (sub.PointKm - sub.Unit->Pos).SwapYZ() * BattleKmScale();
		DVec3 roof;
		if (rig.Kind == EmbodimentCameraKind::Mount)
		{
			DVec3 right;
			sub.Unit->Basis(right, roof);
		}
		else roof = s->PoseAt(Clock->Jd).Up;
		double radius = TargetRadius(host);
		auto eye = TargetScene(host) + offset + roof.SwapYZ() * (radius * rig.Rise);
		if (rig.Back > 0) eye = eye - DVec3(seatAim.x, seatAim.y, seatAim.z) * (radius * rig.Back);
		return eye;
	}
}
