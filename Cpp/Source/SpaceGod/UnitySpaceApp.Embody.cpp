// US-T135: the universal embody layer (declarations in UnitySpaceApp.Embody.inl).

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "Astro.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h" // US-T158: the hull's beam, for the plating's normal
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h" // US-T136
#include "Views/EvaWorkerView.h" // US-T158
#include "Views/MechView.h" // US-T83
#include "Fleet.h"
#include "Sensors/SensorPosture.h" // US-T136
#include "Sensors/Sensors.h"
#include "Coop/CoopPeer.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/Embodiment.h"
#include "Embodiment/EvaPilot.h" // US-T166
#include "Embodiment/MechWalker.h" // US-T83
#include "Embodiment/FighterSeat.h" // US-T166
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
		// US-T140: weighed against everything the SpaceGod's hands hold now (every seat and the helm flown), as SeatAuthority does.
		cs::List<cs::Ref<IEmbodiable>> held;
		for (const auto& h : session->State()->Held()) held.Add(h.Seat);
		auto verdict = Embodiables::MayEmbody(EmbodyContext::Of(World(), held), next);
		if (verdict.Ok) return true;
		NoteEmbodyRefusal(next != nullptr ? next->Name() : cs::String("that"), verdict);
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

	void UnitySpaceApp::EjectPilot()
	{
		auto cockpit = cs::As<FighterSeat>(HeldNonHelm());
		if (cockpit == nullptr)
		{
			ShowToast("Shift+Y ejects from a launched craft's cockpit: take one first (' ; in a fight, or Shift+/ from a carrier)");
			return;
		}
		if (Coop::CoopPeer::Active())
		{
			ShowToast("No ejecting in co-op yet: the pilot stays aboard");
			return;
		}
		cs::String why;
		auto pilot = EvaPilot::Eject(cockpit->Battle, cockpit->Unit, why);
		if (pilot == nullptr)
		{
			ShowToast(cs::Cat("Can't eject from the ", cockpit->Name(), ": ", why));
			return;
		}
		evaPilotHeight = 0; // sized afresh off the craft it leaves
		TakeSeat(pilot);
		if (EmbodiableEquals(HeldNonHelm(), pilot)) return;
		// The take refused: no ejection after all.
		EvaPilot::Unabandon(cockpit->Battle, cockpit->Unit, EmbodiableEquals(HeldNonHelm(), cockpit));
	}

	void UnitySpaceApp::DeployMech()
	{
		cs::Ref<Battle> battle;
		auto unit = HullInHand(battle);
		if (unit == nullptr)
		{
			ShowToast("Shift+N sends out a hull's mech: fly or select a capital ship or station in a fight (or hold one of its guns) first");
			return;
		}
		if (Coop::CoopPeer::Active())
		{
			ShowToast("No mechs in co-op yet: it stays in its bay");
			return;
		}
		cs::String why;
		auto mech = MechWalker::Deploy(battle, unit, why);
		if (mech == nullptr)
		{
			ShowToast(cs::Cat("The ", unit->Name(), " can't send out a mech: ", why));
			return;
		}
		if (EmbodiableEquals(HeldNonHelm(), mech)) return;
		mechHeight = 0; // sized afresh off the hull it stands on
		TakeSeat(mech);
	}

	bool UnitySpaceApp::MechPose(const cs::Ref<MechWalker>& m, DVec3& scene, DVec3& up, DVec3& forward, double& height)
	{
		if (m == nullptr || m->Battle == nullptr || m->Battle->Over || !m->Lost.empty()) return false;
		if (m->Hull != nullptr && m->Hull->Present())
		{
			// On the plating as the hull is drawn: its point in hull lengths times the drawn length (as a worker's).
			auto host = Target::OfUnit(m->Battle, m->Hull);
			double length = TargetRadius(host) * 2;
			if (length <= 0) return false;
			mechHeight = length * MechHeightLengths;
			auto n = m->Local.LengthSquared() > 1e-12 ? m->Local.Normalized() : DVec3(0, 1, 0);
			up = m->Hull->LocalDirToWorld(n).Normalized().SwapYZ();
			scene = TargetScene(host) + m->Hull->LocalDirToWorld(m->Local).SwapYZ() * length;
		}
		else
		{
			if (mechHeight <= 0)
			{
				auto anchor = m->Carrier != nullptr && m->Carrier->Present() ? m->Carrier : nullptr;
				double length = anchor != nullptr ? TargetRadius(Target::OfUnit(m->Battle, anchor)) * 2 : 0;
				mechHeight = length > 0 ? length * MechHeightLengths : cs::Math::Max(BattleKmScale() * MechWalker::HeightKm, 1e-9);
			}
			scene = BattleScene(m->Battle, m->Pos);
			up = m->Up.SwapYZ();
		}
		auto f = m->Facing.SwapYZ();
		f = f - up * DVec3::Dot(f, up);
		forward = f.LengthSquared() > 1e-12 ? f.Normalized() : DVec3::Cross(up, DVec3::UnitX).Normalized();
		height = mechHeight;
		return true;
	}

	void UnitySpaceApp::UpdateMechView()
	{
		if (World() == nullptr || systemRoot == nullptr) return;
		if (mechView == nullptr || mechRoot != systemRoot)
		{
			mechView = cs::New<Views::MechView>(this, systemRoot);
			mechRoot = systemRoot;
		}
		mechView->Update();
	}

	bool UnitySpaceApp::EvaPilotPose(const cs::Ref<EvaPilot>& p, DVec3& scene, DVec3& up, DVec3& forward, double& height)
	{
		if (p == nullptr || p->Battle == nullptr || p->Battle->Over) return false;
		if (p->Craft != nullptr && p->Craft->Present())
		{
			auto craft = Target::OfUnit(p->Battle, p->Craft);
			double length = TargetRadius(craft) * 2;
			if (length > 0) evaPilotHeight = length * PilotHeightLengths;
		}
		if (evaPilotHeight <= 0) evaPilotHeight = cs::Math::Max(BattleKmScale() * 0.004, 1e-9);
		scene = BattleScene(p->Battle, p->Pos);
		up = p->Up.SwapYZ();
		forward = p->Facing.SwapYZ();
		forward = forward - up * DVec3::Dot(forward, up);
		forward = forward.LengthSquared() > 1e-12 ? forward.Normalized() : DVec3::Cross(up, DVec3::UnitX).Normalized();
		height = evaPilotHeight;
		return true;
	}

	// ---------------------------------------------------------------- US-T136: other players' units

	PlayerId UnitySpaceApp::LocalPlayerId() const { return session->LocalPlayer(); }

	double UnitySpaceApp::VisualRangeKm(double lengthKm)
	{
		// One arcminute: the range is the length over tan(1'), 3,437.7 lengths.
		return cs::Math::Max(lengthKm, 0.005) / cs::Math::Tan(Astro::Deg2Rad / 60.0);
	}

	DVec3 UnitySpaceApp::ObserverScene()
	{
		auto held = HeldNonHelm();
		if (held != nullptr)
		{
			auto t = SeatTarget(held);
			if (!t.IsNone()) return FocusScene(t);
		}
		if (IsPiloting() && Piloted.Ship != nullptr) return FocusScene(Target::OfShip(Piloted.Ship));
		return SpaceCamera->ScenePosition;
	}

	namespace
	{
		/// <summary>The hull's own ship and battle unit, whichever the target names.</summary>
		void HullOf(const Target& t, cs::Ref<Ship>& ship, cs::Ref<Battle>& battle, cs::Ref<BattleUnit>& unit)
		{
			ship = t.IsShip() ? t.Ship : t.IsUnit() && t.Unit != nullptr ? t.Unit->Ship : nullptr;
			battle = t.IsUnit() ? t.Battle : ship != nullptr ? ship->Battle : nullptr;
			unit = t.IsUnit() ? t.Unit : battle != nullptr && ship != nullptr ? battle->UnitOf(ship) : nullptr;
		}

		/// <summary>US-T93's dark: silent running (or lying in wait), or the reactor down or restarting.</summary>
		bool RunningDark(const Target& t)
		{
			cs::Ref<Ship> ship;
			cs::Ref<Battle> battle;
			cs::Ref<BattleUnit> unit;
			HullOf(t, ship, battle, unit);
			if (ship != nullptr && Signatures::Silent(ship)) return true;
			auto combat = ship != nullptr ? ship->Combat : unit != nullptr ? unit->Combat : nullptr;
			return SensorPosture::Offline(combat);
		}
	}

	cs::List<UnitySpaceApp::ShiningUnit> UnitySpaceApp::ShiningNow()
	{
		cs::List<ShiningUnit> list;
		if (World() == nullptr || !SystemVisible) return list;
		PlayerId me = LocalPlayerId();
		DVec3 eye = ObserverScene();
		double perKm = cs::Math::Max(BattleKmScale(), 1e-30);
		auto add = [&](PlayerId who, const Target& t, bool fakeDark, bool fake)
		{
			if (t.IsNone() || who.IsNone()) return;
			if (t.IsShip())
			{
				if (t.Ship == nullptr || t.Ship->Destroyed || !Fleet()->IsPresent(t.Ship) || t.Ship->Controller()->Phase == ShipPhase::Landed) return;
			}
			else if (!t.IsUnit() || t.Unit == nullptr || t.Battle == nullptr || !t.Unit->Present()) return;
			for (const auto& s : list)
				if (s.What == t) return;
			ShiningUnit s;
			s.Player = who;
			s.What = t;
			s.Centre = FocusScene(t);
			s.Radius = TargetRadius(t);
			auto f = TargetRotation(t) * Vector3::forward();
			s.Forward = DVec3(f.x, f.y, f.z);
			s.Dark = fakeDark || RunningDark(t);
			// The eye's distance in ship km against the hull's own visual range, both through the scale's ship mapping.
			double km = (s.Centre - eye).Length() / perKm;
			s.InVisualRange = km <= VisualRangeKm(s.Radius * 2 / perKm);
			s.Fake = fake;
			list.Add(s);
		};
		for (const auto& e : EmbodiedNow())
		{
			if (e.Player == me) continue; // your own embodied unit doesn't shine for you
			add(e.Player, SeatTarget(e.Seat), false, false);
		}
		for (const auto& f : FakeHolders)
			if (PlayerId(f.Seat) != me) add(PlayerId(f.Seat), f.What, f.Dark, true);
		return list;
	}

	bool UnitySpaceApp::ShiningOf(PlayerId p, ShiningUnit& out)
	{
		for (const auto& s : ShiningNow())
			if (s.Player == p)
			{
				out = s;
				return true;
			}
		return false;
	}

	UnitySpaceApp::PlayerStance UnitySpaceApp::StanceBetween(PlayerId a, PlayerId b)
	{
		if (a.IsNone() || b.IsNone() || a == b || World() == nullptr) return PlayerStance::None;
		// What each holds: a session seat or the helm, else (sg.Shine) a test holder.
		auto heldBy = [this](PlayerId p, Target& t)
		{
			for (const auto& e : EmbodiedNow())
				if (e.Player == p)
				{
					t = SeatTarget(e.Seat);
					if (!t.IsNone()) return true;
				}
			for (const auto& f : FakeHolders)
				if (PlayerId(f.Seat) == p)
				{
					t = f.What;
					return true;
				}
			return false;
		};
		Target ta, tb;
		if (!heldBy(a, ta) || !heldBy(b, tb)) return PlayerStance::None;
		cs::Ref<Ship> sa, sb;
		cs::Ref<Battle> ba, bb;
		cs::Ref<BattleUnit> ua, ub;
		HullOf(ta, sa, ba, ua);
		HullOf(tb, sb, bb, ub);
		// In one fight its sides decide, as they decide every shot (Battle.Hostile): the other side is fair game.
		if (ba != nullptr && ba == bb && ua != nullptr && ub != nullptr) return ua->Side != ub->Side ? PlayerStance::Hostile : PlayerStance::Ally;
		auto fa = ua != nullptr ? ua->Faction : sa != nullptr ? sa->Faction : nullptr;
		auto fb = ub != nullptr ? ub->Faction : sb != nullptr ? sb->Faction : nullptr;
		if (fa == nullptr || fb == nullptr) return PlayerStance::Neutral;
		if (fa == fb) return PlayerStance::Ally;
		auto diplomacy = World()->Diplomacy();
		return diplomacy != nullptr && diplomacy->Hostile(fa, fb) ? PlayerStance::Hostile : PlayerStance::Neutral;
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
		// The eye on the hull (U.SPC-T445 a gun's mount; US-T135 a worker's shoulder), looking along the stick. A worker's
		// look starts tipped down onto the plating ahead (US-T158), so the suit and its work are in the frame from the start.
		if (rig.Kind == EmbodimentCameraKind::Shoulder)
		{
			auto roof = ToVector(pose.Up.SwapYZ()).normalized();
			auto flat = (seatAim - roof * Vector3::Dot(seatAim, roof)).normalized();
			if (flat.sqrMagnitude() > 1e-6f)
			{
				float pitch = static_cast<float>(ShoulderPitchDeg) * Mathf::Deg2Rad;
				seatAim = (flat * Mathf::Cos(pitch) - roof * Mathf::Sin(pitch)).normalized();
			}
		}
		SpaceCamera->PilotView = Quaternion::LookRotation(seatAim, MountRoof(pose, seatAim));
		SpaceCamera->PilotEye = [this, next]() { return RigEye(next); };
	}

	bool UnitySpaceApp::WorkerPose(const cs::Ref<RepairWorker>& w, DVec3& scene, DVec3& up, DVec3& forward, double& height)
	{
		if (w == nullptr || w->Unit == nullptr || w->Battle == nullptr || !w->Unit->Present()) return false;
		auto host = SeatTarget(w);
		if (host.IsNone()) return false;
		double length = TargetRadius(host) * 2; // the hull's drawn length, as its model is sized
		auto n = w->Local.LengthSquared() > 1e-12 ? w->Local.Normalized() : DVec3(0, 1, 0);
		// The plating's normal on the hull's capsule (Collisions::SurfacePoint): out from its spine, not from its centre.
		auto spec = w->Unit->Combat != nullptr ? w->Unit->Combat->Spec : nullptr;
		if (spec != nullptr)
		{
			double core = cs::Math::Max(0.0, 0.5 - spec->BeamFraction * 0.5);
			auto out = w->Local - DVec3(0, 0, cs::Math::Max(-core, cs::Math::Min(core, w->Local.Z)));
			if (out.LengthSquared() > 1e-12) n = out.Normalized();
		}
		up = w->Unit->LocalDirToWorld(n).Normalized().SwapYZ();
		auto f = w->Unit->LocalDirToWorld(w->Facing).Normalized().SwapYZ();
		f = f - up * DVec3::Dot(f, up);
		forward = f.LengthSquared() > 1e-12 ? f.Normalized() : DVec3::Cross(up, DVec3::UnitX).Normalized();
		scene = TargetScene(host) + w->Unit->LocalDirToWorld(w->Local).SwapYZ() * length;
		height = length * WorkerHeightLengths;
		return true;
	}

	void UnitySpaceApp::UpdateEvaWorkerView()
	{
		if (World() == nullptr || systemRoot == nullptr) return;
		if (evaWorkerView == nullptr || evaWorkerRoot != systemRoot)
		{
			evaWorkerView = cs::New<Views::EvaWorkerView>(this, systemRoot);
			evaWorkerRoot = systemRoot;
		}
		evaWorkerView->Update();
	}

	DVec3 UnitySpaceApp::RigEye(const cs::Ref<IEmbodiable>& s)
	{
		auto sub = s->Subject();
		auto rig = s->CameraRig();
		// US-T158: a worker's shoulder: behind and above the suit as it is drawn on the plating, set back along the look
		// (flattened onto the plating), in worker heights, so the suit, the plating and the work ahead share the frame.
		if (auto w = cs::As<RepairWorker>(s); w != nullptr && rig.Kind == EmbodimentCameraKind::Shoulder)
		{
			DVec3 at, up, forward;
			double h = 0;
			if (WorkerPose(w, at, up, forward, h))
			{
				auto look = DVec3(seatAim.x, seatAim.y, seatAim.z);
				auto flat = look - up * DVec3::Dot(look, up);
				flat = flat.LengthSquared() > 1e-12 ? flat.Normalized() : forward;
				return at + up * (h * ShoulderRise) - flat * (h * ShoulderBack);
			}
		}
		// US-T166: an ejected pilot's shoulder, in open space: behind and above the suit along the look, in suit heights.
		if (auto p = cs::As<EvaPilot>(s))
		{
			DVec3 at, up, forward;
			double h = 0;
			if (EvaPilotPose(p, at, up, forward, h))
			{
				auto look = DVec3(seatAim.x, seatAim.y, seatAim.z);
				if (look.LengthSquared() < 1e-12) look = forward;
				return at + up * (h * ShoulderRise) - look.Normalized() * (h * ShoulderBack);
			}
		}
		// US-T83: a mech's shoulder: behind and above the torso along the look, in mech heights.
		if (auto m = cs::As<MechWalker>(s))
		{
			DVec3 at, up, forward;
			double h = 0;
			if (MechPose(m, at, up, forward, h))
			{
				auto look = DVec3(seatAim.x, seatAim.y, seatAim.z);
				if (look.LengthSquared() < 1e-12) look = forward;
				return at + up * (h * MechShoulderRise) - look.Normalized() * (h * MechShoulderBack);
			}
		}
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
