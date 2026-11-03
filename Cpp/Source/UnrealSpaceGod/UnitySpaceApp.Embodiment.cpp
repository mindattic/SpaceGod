// Port of Runtime/UnitySpaceApp.Embodiment.cs (declarations in UnitySpaceApp.Embodiment.inl).

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/War.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/Embodiment.h"
#include "Embodiment/FighterSeat.h"
#include "Embodiment/MountedTurret.h"
#include "Embodiment/ShipHelm.h"
#include "Fleet.h"
#include "Infrastructure/Infrastructure.h"
#include "Session/Commands.h"
#include "Session/ISession.h"
#include "Session/LocalSession.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"

#include <tuple>

using namespace SpaceGod::Core;

namespace SpaceGod
{
	cs::Ref<IEmbodiable> UnitySpaceApp::HeldNonHelm() const
	{
		return session->State()->SeatOf(session->LocalPlayer());
	}

	void UnitySpaceApp::ReleaseSeat()
	{
		if (CoopRoutes(cs::New<ReleaseSeatCommand>(session->LocalPlayer()))) return; // US-T44: to the host in co-op
		session->Submit(cs::New<ReleaseSeatCommand>(session->LocalPlayer()));
		session->ApplyPending();
	}

	bool UnitySpaceApp::SwitchSeat(const cs::Ref<IEmbodiable>& next)
	{
		// (C# evaluates LocalPlayer, then IdOf, which may assign the seat its id.)
		PlayerId player = session->LocalPlayer();
		int seat = session->IdOf(next);
		session->Submit(cs::New<TakeSeatCommand>(player, seat));
		session->ApplyPending();
		return EmbodiableEquals(HeldNonHelm(), next);
	}

	bool UnitySpaceApp::SeatToHost(const cs::Ref<IEmbodiable>& host)
	{
		if (CoopRoutes(cs::New<MoveToHostCommand>(session->LocalPlayer()))) return true; // US-T44: to the host in co-op
		session->Submit(cs::New<MoveToHostCommand>(session->LocalPlayer()));
		session->ApplyPending();
		return host != nullptr && EmbodiableEquals(HeldNonHelm(), host);
	}

	bool UnitySpaceApp::IsSeated() const { return HeldNonHelm() != nullptr; }

	cs::Ref<IEmbodiable> UnitySpaceApp::Seat() const { return HeldNonHelm(); }

	bool UnitySpaceApp::AtControls() { return IsPiloting() || IsSeated(); }

	void UnitySpaceApp::RememberCommand()
	{
		if (AtControls()) return;
		commandWarp = Clock->WarpIndex;
		commandFocus = SpaceCamera->Mode != Cameras::SpaceCamera::CameraMode::Free ? SpaceCamera->FocusTarget : Target::None();
	}

	void UnitySpaceApp::TakeSeat(const cs::Ref<IEmbodiable>& next)
	{
		if (next == nullptr) return;
		if (!next->CanEmbody())
		{
			ShowToast(cs::Cat("Can't take ", next->Name(), ": ", next->Blocker()));
			return;
		}
		RememberCommand();
		if (auto helm = cs::As<ShipHelm>(next))
		{
			ReleaseSeat();
			if (IsPiloting() && Piloted.Ship == helm->Ship) return;
			if (IsPiloting()) ExitPiloting();
			EnterPiloting(Target::OfShip(helm->Ship));
			return;
		}
		if (IsPiloting()) ExitPiloting();
		if (CoopTakeSeat(next)) return; // US-T44: to the host in co-op; the view follows when a frame grants it
		if (!SwitchSeat(next))
		{
			cs::String blocker = next->Blocker();
			ShowToast(cs::Cat("Can't take ", next->Name(), ": ", !blocker.empty() ? blocker : cs::String("out of reach")));
			return;
		}
		auto pose = next->PoseAt(Clock->Jd);
		seatAim = ToVector(pose.Forward.SwapYZ()).normalized();
		Clock->SetWarp(0);
		SpaceCamera->EnterPilotChase(SeatTarget(next), cs::Is<FighterSeat>(next) ? 7 : 2.5);
		// A gun is laid from the mount itself (U.SPC-T445): the eye glides in to sit on it, looking down the barrel.
		if (auto gun = cs::As<MountedTurret>(next))
		{
			SpaceCamera->PilotView = Quaternion::LookRotation(seatAim, MountRoof(pose, seatAim));
			SpaceCamera->PilotEye = [this, gun]() { return MountEye(gun); };
		}
		ShowToast(cs::Cat(cs::Is<FighterSeat>(next) ? "Flying " : "Manning ", next->Name(), ", ", Clock->Warp().Label, SeatKeys(next)));
	}

	cs::String UnitySpaceApp::SeatKeys(const cs::Ref<IEmbodiable>& s)
	{
		return cs::Is<FighterSeat>(s)
			? cs::String("  (mouse aims, W/S throttle, A/D Q/E strafe, Z/X roll, Shift boost, F flight assist, left mouse guns, right mouse "
				"weapon group, 1 next group, T lock, 6 next weak point, Ctrl flares, 2 attack my target, 3 cover me, 4 form up, "
				"5 engage at will, 7/8/9 power to weapons/shields/engines, 0 even power, ' ; next seat, P back to command)")
			: cs::String("  (mouse lays it, left mouse fires, / next gun, Backspace up to the helm, P back to command)");
	}

	DVec3 UnitySpaceApp::MountEye(const cs::Ref<MountedTurret>& t)
	{
		auto host = SeatTarget(t);
		auto mount = (t->Unit->PointOf(t->Mount->Def) - t->Unit->Pos).SwapYZ() * BattleKmScale();
		DVec3 right, up;
		t->Unit->Basis(right, up);
		return TargetScene(host) + mount + up.SwapYZ() * (TargetRadius(host) * MountEyeRise);
	}

	Vector3 UnitySpaceApp::MountRoof(const EmbodimentPose& pose, Vector3 aim)
	{
		auto roof = ToVector(pose.Up.SwapYZ());
		return Mathf::Abs(Vector3::Dot(roof.normalized(), aim)) > 0.99f ? SpaceCamera->Rotation * Vector3::up() : roof;
	}

	void UnitySpaceApp::MoveSeatToHost()
	{
		auto gun = cs::As<MountedTurret>(HeldNonHelm());
		if (!gun) return;
		if (gun->Host() == nullptr)
		{
			ShowToast(cs::Cat("The ", gun->Unit->Name(), " has no helm to take: / next gun, P back to command"));
			return;
		}
		if (cs::Is<ShipHelm>(gun->Host()))
		{
			TakeSeat(gun->Host());
			return;
		}
		auto host = gun->Host();
		if (!SeatToHost(host))
		{
			cs::String blocker = host->Blocker();
			ShowToast(cs::Cat("Can't take ", host->Name(), ": ", !blocker.empty() ? blocker : cs::String("out of reach")));
			if (!IsSeated()) ReturnToCommand();
			return;
		}
		SpaceCamera->PilotEye = nullptr;
		ShowToast(cs::Cat("Holding ", host->Name(), ", ", Clock->Warp().Label, "  (mouse lays its guns, left mouse fires, P back to command)"));
	}

	Target UnitySpaceApp::SeatTarget(const cs::Ref<IEmbodiable>& s)
	{
		if (auto f = cs::As<FighterSeat>(s)) return Target::OfUnit(f->Battle, f->Unit);
		if (auto t = cs::As<MountedTurret>(s)) return t->Unit->Ship != nullptr ? Target::OfShip(t->Unit->Ship) : Target::OfUnit(t->Battle, t->Unit);
		return Target::None();
	}

	void UnitySpaceApp::ReturnToCommand(const cs::String& why, const cs::String& lostName)
	{
		// A seat already lost (the session let go of it) is named by the caller, so the toast can still say why.
		auto held = HeldNonHelm();
		cs::String name = held ? held->Name() : lostName;
		ReleaseSeat();
		if (IsPiloting())
		{
			name = Piloted.Ship ? Piloted.Ship->Name : cs::String();
			ExitPiloting();
		}
		SpaceCamera->PilotView = std::nullopt;
		SpaceCamera->PilotEye = nullptr;
		SpaceCamera->ExitFocus();
		if (!commandFocus.IsNone() && !(commandFocus.IsShip() && commandFocus.Ship->Destroyed) && !commandFocus.IsUnit())
			SpaceCamera->FocusOn(commandFocus);
		if (commandWarp >= 0) Clock->SetWarp(commandWarp);
		commandWarp = -1;
		commandFocus = Target::None();
		cs::String head = !why.empty() && !name.empty() ? cs::Cat("The ", name, " is ", why)
			: !name.empty() ? cs::Cat(name, " handed back to its crew") : cs::String("Handed back");
		ShowToast(cs::Cat(head, ": back in command, ", Clock->Warp().Label));
	}

	cs::Ref<Battle> UnitySpaceApp::BattleInView()
	{
		if (auto f = cs::As<FighterSeat>(HeldNonHelm())) return f->Battle;
		if (auto t = cs::As<MountedTurret>(HeldNonHelm())) return t->Battle;
		if (IsPiloting() && Piloted.Ship && Piloted.Ship->Battle != nullptr) return Piloted.Ship->Battle;
		if (Selected.IsShip() && Selected.Ship->Battle != nullptr) return Selected.Ship->Battle;
		auto focus = SpaceCamera->FocusTarget;
		if (focus.IsShip() && focus.Ship->Battle != nullptr) return focus.Ship->Battle;
		cs::Ref<Battle> best = nullptr;
		double bestD = cs::Double::MaxValue;
		for (const auto& b : World()->War->Battles)
		{
			if (b->Over) continue;
			double d = (BattleScene(b, DVec3::Zero) - SpaceCamera->ScenePosition).Length();
			if (d < bestD)
			{
				bestD = d;
				best = b;
			}
		}
		return best;
	}

	DVec3 UnitySpaceApp::SeatScene(const cs::Ref<IEmbodiable>& s)
	{
		if (auto h = cs::As<ShipHelm>(s)) return TargetScene(Target::OfShip(h->Ship));
		if (auto f = cs::As<FighterSeat>(s)) return BattleScene(f->Battle, f->Unit->Pos);
		if (auto t = cs::As<MountedTurret>(s)) return BattleScene(t->Battle, t->Unit->PointOf(t->Mount->Def));
		return SpaceCamera->ScenePosition;
	}

	void UnitySpaceApp::CycleSeat(int step)
	{
		auto battle = BattleInView();
		auto seats = Embodiables::SeatsIn(World(), battle);
		if (seats.Count() == 0)
		{
			ShowToast(battle == nullptr ? cs::String("No battles under way: nothing to take charge of") : cs::Cat("Nobody left to fly in ", battle->Name));
			return;
		}
		cs::Ref<IEmbodiable> current = HeldNonHelm();
		if (!current) current = IsPiloting() && Piloted.Ship != nullptr ? cs::Ref<IEmbodiable>(Embodiables::HelmOf(World(), Piloted.Ship)) : nullptr;
		if (auto gun = cs::As<MountedTurret>(current)) current = gun->Host();
		cs::Ref<IEmbodiable> next;
		if (current == nullptr || EmbodiableIndexOf(seats, current) < 0)
		{
			// From command: what the player is looking at.
			auto forward = SpaceCamera->Rotation * Vector3::forward();
			next = nullptr;
			float best = cs::Single::MaxValue;
			for (const auto& s : seats)
			{
				auto to = ToVector(SeatScene(s) - SpaceCamera->ScenePosition);
				float angle = to.sqrMagnitude() > 0 ? Vector3::Angle(forward, to) : 0;
				if (angle < best)
				{
					best = angle;
					next = s;
				}
			}
		}
		else next = Embodiables::Cycle(seats, current, step);
		TakeSeat(next);
	}

	void UnitySpaceApp::ManNextGun()
	{
		cs::Ref<BattleUnit> unit = nullptr;
		cs::Ref<Battle> battle = nullptr;
		if (auto held = cs::As<MountedTurret>(HeldNonHelm()))
		{
			unit = held->Unit;
			battle = held->Battle;
		}
		else if (IsPiloting() && Piloted.Ship && Piloted.Ship->Battle != nullptr)
		{
			battle = Piloted.Ship->Battle;
			unit = battle->UnitOf(Piloted.Ship);
		}
		else if (Selected.IsShip() && Selected.Ship->Battle != nullptr)
		{
			battle = Selected.Ship->Battle;
			unit = battle->UnitOf(Selected.Ship);
		}
		else unit = DefencePlatformAt(Selected, battle);
		if (unit == nullptr)
		{
			ShowToast("Select a hull in a fight (or fly one), or a station whose defences are fighting, to man its guns");
			return;
		}
		cs::List<cs::Ref<IEmbodiable>> guns;
		for (const auto& g : Embodiables::TurretsOn(World(), battle, unit)) guns.Add(g);
		if (guns.Count() == 0)
		{
			ShowToast(cs::Cat("The ", unit->Name(), " has no gun left to man"));
			return;
		}
		if (auto last = cs::As<MountedTurret>(HeldNonHelm()))
		{
			if (EmbodiableIndexOf(guns, last) == guns.Count() - 1 && last->Host() != nullptr)
			{
				MoveSeatToHost();
				return;
			}
		}
		TakeSeat(Embodiables::Cycle(guns, HeldNonHelm(), 1));
	}

	// ---------------------------------------------------------------- the picker (U.SPC-T446)

	void UnitySpaceApp::TogglePicker()
	{
		ShowPicker = !ShowPicker;
		pickerBuiltAt = -10.0f;
	}

	cs::Ref<IEmbodiable> UnitySpaceApp::HeldSeat()
	{
		auto held = HeldNonHelm();
		if (held) return held;
		return IsPiloting() && Piloted.Ship != nullptr ? cs::Ref<IEmbodiable>(Embodiables::HelmOf(World(), Piloted.Ship)) : nullptr;
	}

	cs::List<cs::Ref<IEmbodiable>> UnitySpaceApp::PickerSeats()
	{
		if (Time::unscaledTime - pickerBuiltAt < 0.25f) return pickerSeats;
		pickerBuiltAt = Time::unscaledTime;
		auto battle = BattleInView();
		cs::Ref<BattleUnit> onUnit = nullptr;
		cs::Ref<Ship> onShip = nullptr;
		auto held = HeldNonHelm();
		if (auto t = cs::As<MountedTurret>(held))
		{
			onUnit = t->Unit;
			battle = t->Battle;
		}
		else if (auto f = cs::As<FighterSeat>(held))
		{
			onUnit = f->Unit;
			battle = f->Battle;
		}
		else
		{
			if (IsPiloting()) onShip = Piloted.Ship;
			else if (Selected.IsShip()) onShip = Selected.Ship;
			else if (Selected.IsUnit() && !Selected.Battle->Over)
			{
				onUnit = Selected.Unit;
				battle = Selected.Battle;
			}
		}
		pickerSeats = Embodiables::Pickable(World(), battle, onUnit, onShip, ShipsNearCamera(PickerNearbyShips));
		return pickerSeats;
	}

	cs::List<cs::Ref<Ship>> UnitySpaceApp::ShipsNearCamera(int count)
	{
		auto nearShips = cs::List<std::tuple<double, cs::Ref<Ship>>>();
		auto eye = SpaceCamera->ScenePosition;
		for (const auto& s : Fleet()->Ships())
		{
			if (s->Destroyed || s->Battle != nullptr || s->Phase == ShipPhase::Landed) continue;
			nearShips.Add(std::make_tuple((TargetScene(Target::OfShip(s)) - eye).Length(), s));
		}
		nearShips.Sort([](const std::tuple<double, cs::Ref<Ship>>& a, const std::tuple<double, cs::Ref<Ship>>& b)
		{
			return cs::CompareTo(std::get<0>(a), std::get<0>(b));
		});
		auto list = cs::List<cs::Ref<Ship>>();
		for (int i = 0; i < nearShips.Count() && i < count; i++) list.Add(std::get<1>(nearShips[i]));
		return list;
	}

	void UnitySpaceApp::PickSeat(const cs::Ref<IEmbodiable>& next)
	{
		if (next == nullptr) return;
		ShowPicker = false;
		if (EmbodiableEquals(next, HeldSeat())) return;
		TakeSeat(next);
	}

	void UnitySpaceApp::PickCommand()
	{
		ShowPicker = false;
		if (AtControls()) ReturnToCommand();
	}

	cs::Ref<BattleUnit> UnitySpaceApp::DefencePlatformAt(const Target& t, cs::Ref<Battle>& battle)
	{
		battle = nullptr;
		if (t.IsUnit())
		{
			if (t.Unit->Kind != UnitKind::Platform) return nullptr;
			battle = t.Battle;
			return t.Unit;
		}
		if (!(t.IsStructure() || t.IsRing() || t.IsBody())) return nullptr;
		auto settlement = t.IsStructure() ? t.Structure->Settlement : nullptr;
		// A gate fort stands out at the system's root: selecting the star finds it there.
		cs::Ref<BattleUnit> any = nullptr;
		cs::Ref<Battle> anyBattle = nullptr;
		for (const auto& b : World()->War->Battles)
		{
			if (b->Over || b->Body != t.Body) continue;
			for (const auto& u : b->Units)
			{
				if (u->Kind != UnitKind::Platform || !u->Present() || u->Captured || (u->Platform == nullptr && u->Fort == nullptr)) continue;
				if (Embodiables::TurretsOn(b, u).Count() == 0) continue;
				if (settlement != nullptr && u->Platform == settlement)
				{
					battle = b;
					return u;
				}
				if (any == nullptr)
				{
					any = u;
					anyBattle = b;
				}
			}
		}
		battle = anyBattle;
		return any;
	}

	void UnitySpaceApp::UpdateSeat(float dt)
	{
		if (Coop::CoopPeer::Active()) { CoopUpdateSeat(dt); return; } // US-T44: every player's seat, stepped on every peer
		auto held = HeldNonHelm();
		if (held == nullptr) return;
		auto look = ClickPanelOpen() ? Vector2::zero() : Input.MouseDelta;
		auto rot = SpaceCamera->Rotation;
		auto up = rot * Vector3::up();
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
		if (auto cockpit = cs::As<FighterSeat>(held))
			if (!ClickPanelOpen()) UpdateCockpit(cockpit, input.Aim);
		// On the sim clock's time, so a fight run faster is flown faster too.
		double simDt = dt * Clock->Warp().SimSecondsPerSecond * Clock->Speed;
		// The stick goes in as the local player's command; the session's authority steps the seat with it.
		session->Submit(cs::New<SeatControlCommand>(session->LocalPlayer(), input));
		session->Advance(simDt);
		if (HeldNonHelm() == nullptr)
		{
			cs::String why = session->State()->EndedBecause(session->LocalPlayer());
			ReturnToCommand(!why.empty() ? why : cs::String("lost"), held->Name());
			return;
		}
		auto pose = held->PoseAt(Clock->Jd);
		auto roof = ToVector(pose.Up.SwapYZ());
		SpaceCamera->PilotView = Quaternion::LookRotation(seatAim, Vector3::Dot(roof, seatAim) > 0.99f ? up : roof);
	}

	void UnitySpaceApp::UpdateCockpit(const cs::Ref<FighterSeat>& f, DVec3 aim)
	{
		if (Input.ToggleFlightAssist) ShowToast(f->ToggleFlightAssist() ? "Flight assist off: it drifts on its momentum" : "Flight assist on");
		if (Input.LockTarget)
		{
			auto locked = f->CycleLock(aim);
			ShowToast(locked == nullptr ? cs::String("Nothing hostile near the crosshair to lock")
				: cs::Cat("Locking ", locked->Name(), (f->Unit->PlayerSubTarget != nullptr ? cs::Cat(" @ ", WeakPoints::Label(f->Unit->PlayerSubTarget)) : cs::String()), ": hold the crosshair on it"));
		}
		if (Input.DropMine) f->DropCountermeasures();
		switch (Input.NumberKey)
		{
			case 1:
			{
				auto nextGroup = f->CycleSecondaryGroup();
				ShowToast(!nextGroup.has_value() ? cs::String("No guided weapons aboard") : cs::Cat("Second trigger: ", WeaponGroups::Label(*nextGroup)));
				break;
			}
			case 2: OrderWing(f, WingOrder::AttackMyTarget); break;
			case 3: OrderWing(f, WingOrder::CoverMe); break;
			case 4: OrderWing(f, WingOrder::FormUp); break;
			case 5: OrderWing(f, WingOrder::EngageAtWill); break;
			case 6:
			{
				auto weak = f->CycleWeakPoint();
				ShowToast(weak == nullptr ? cs::String("Lock a capital ship (T) to pick a weak point") : cs::Cat("Weak point: ", WeakPoints::Label(weak)));
				break;
			}
			case 7: f->ShiftPower(PowerSystem::Weapons); break;
			case 8: f->ShiftPower(PowerSystem::Shields); break;
			case 9: f->ShiftPower(PowerSystem::Engines); break;
			case 0: f->BalancePower(); break;
		}
	}

	void UnitySpaceApp::OrderWing(const cs::Ref<FighterSeat>& f, WingOrder order)
	{
		int heard = f->OrderWing(order);
		ShowToast(heard == 0 ? cs::String("No wingmen left to order") : cs::Cat("Wing (", heard, "): ", Wing::Label(order)));
	}

	cs::String UnitySpaceApp::SeatLine()
	{
		auto held = HeldNonHelm();
		if (auto f = cs::As<FighterSeat>(held))
			return cs::Cat(f->Readout(), "  \xC2\xB7  ' ; next seat   P back to command"); // (\xC2\xB7 is the UTF-8 middle dot)
		if (auto t = cs::As<MountedTurret>(held))
			return cs::Cat("GUN  \xC2\xB7  ", t->Name(), "  \xC2\xB7  ", (t->Mount->Cooldown > 0 ? "reloading" : "ready"), "  \xC2\xB7  / next gun   ",
				(cs::Is<ShipHelm>(t->Host()) ? "Backspace helm   " : t->Host() != nullptr ? "Backspace whole station   " : ""), "P back to command");
		return cs::String();
	}
}
