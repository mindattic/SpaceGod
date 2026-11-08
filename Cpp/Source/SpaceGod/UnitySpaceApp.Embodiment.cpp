// Port of Runtime/UnitySpaceApp.Embodiment.cs (declarations in UnitySpaceApp.Embodiment.inl).

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "Combat/Battle.h"
#include "Combat/BroadcastArrays.h" // US-T96
#include "Combat/CombatSpecs.h" // US-T170: a craft's dry mass, for its feel
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/Fighters.h" // US-T106: a craft's carrier
#include "Astro.h"
#include "Combat/PilotAim.h" // US-T129
#include "Combat/FlightModels.h" // US-T168: a light craft's slack string
#include "Combat/PilotFlight.h"
#include "Combat/War.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/Embodiment.h"
#include "Embodiment/CraftPower.h"
#include "Embodiment/FighterSeat.h"
#include "Embodiment/HelmAbilities.h"
#include "Embodiment/MechWalker.h" // US-T83: a mech's footfalls, felt
#include "Embodiment/MountedTurret.h"
#include "Embodiment/RepairWorker.h" // US-T170: a weld struck, felt
#include "Embodiment/ShipHelm.h"
#include "Embodiment/SelfDestruct.h"
#include "Embodiment/StationSeat.h"
#include "Fleet.h"
#include "Infrastructure/Infrastructure.h"
#include "Sensors/SensorPosture.h"
#include "Session/Commands.h"
#include "Session/ISession.h"
#include "Session/LocalSession.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"

#include <algorithm>
#include <cmath>
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

	void UnitySpaceApp::ArbitrateInput(bool frameStart)
	{
		bool craft = AtControls();
		if (frameStart)
		{
			if (craft != craftHasInput) inputHandover = true; // changed outside the frame's commands (a seat lost, a load)
			craftHasInput = craft;
			Input.Arbitrate(craft);
			// US-T83: a mech held keeps its jump jets' key (Space, over the pause); anywhere else the jets read nothing.
			auto held = craft ? HeldNonHelm() : nullptr;
			Input.GiveWalkerKeys(held != nullptr && held->InputProfile() == EmbodimentInputProfile::Walker);
			if (inputHandover)
			{
				// Let go of everything held across the change: from the next frame on, the hands are the new owner's.
				bool idle = Input.HandsIdle();
				Input.HoldHandsOff();
				if (idle) inputHandover = false;
			}
			return;
		}
		if (craft == craftHasInput) return;
		// Taken or let go by this frame's own keys: what was pressed has been spent on that.
		craftHasInput = craft;
		Input.Handover();
		inputHandover = true;
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
		if (!PassesEmbodyGate(next)) return; // US-T135: the one embody gate (US-T140's rules), its reason toasted
		// US-T90: from one seat straight into the next (mid-fight, say), the view gets there in a blink, not a glide.
		bool switching = AtControls();
		RememberCommand();
		if (auto helm = cs::As<ShipHelm>(next))
		{
			ReleaseSeat();
			if (IsPiloting() && Piloted.Ship == helm->Ship) return;
			if (IsPiloting()) ExitPiloting();
			EnterPiloting(Target::OfShip(helm->Ship));
			if (switching) SpaceCamera->Hurry();
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
		// US-T135: the camera rides it by its own rig (a chase, a gun's mount down the barrel, a worker's shoulder).
		RideCameraRig(next, pose, switching);
		ShowToast(cs::Cat(next->RolePanel().Verb, " ", next->Name(), ", ", Clock->Warp().Label, SeatKeys(next)));
	}

	cs::String UnitySpaceApp::SeatKeys(const cs::Ref<IEmbodiable>& s)
	{
		// US-T135: every kind's keys come from its own role panel.
		if (s == nullptr) return cs::String();
		auto panel = s->RolePanel();
		cs::String keys;
		for (const auto& a : panel.Abilities) keys = cs::Cat(keys, keys.empty() ? "" : ", ", a.Key, " ", a.Label);
		return keys.empty() ? cs::String() : cs::Cat("  (", keys, ")");
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
		// In a launched craft (US-T106): back to the helm of the carrier it flew from.
		if (auto craft = cs::As<FighterSeat>(HeldNonHelm()))
		{
			if (!BackToCarrier()) ShowToast(cs::Cat("The ", craft->Name(), "'s carrier is out of reach: ' ; next seat, P back to command"));
			return;
		}
		// Anything mounted on or working a hull (a gun, an EVA worker, US-T135): up to its host.
		auto held = HeldNonHelm();
		if (!held) return;
		if (held->Host() == nullptr)
		{
			ShowToast(cs::Cat("The ", held->Name(), " has nothing to move up to: P back to command"));
			return;
		}
		if (cs::Is<ShipHelm>(held->Host()))
		{
			TakeSeat(held->Host());
			return;
		}
		auto host = held->Host();
		if (!PassesEmbodyGate(host)) return; // US-T135
		if (!SeatToHost(host))
		{
			cs::String blocker = host->Blocker();
			ShowToast(cs::Cat("Can't take ", host->Name(), ": ", !blocker.empty() ? blocker : cs::String("out of reach")));
			if (!IsSeated()) ReturnToCommand();
			return;
		}
		RideCameraRig(host, host->PoseAt(Clock->Jd), false); // US-T135: the host's own rig (a station's chase)
		ShowToast(cs::Cat(host->RolePanel().Verb, " ", host->Name(), ", ", Clock->Warp().Label, SeatKeys(host)));
	}

	Target UnitySpaceApp::SeatTarget(const cs::Ref<IEmbodiable>& s)
	{
		// US-T135: from what the seat is in the world, never its type. A launched craft is followed as its unit; anything on
		// a ship's hull (a gun, a worker) as the ship; a station taken in a fight as its unit there (US-T90: without this the
		// camera stayed free, and swam on the keys flying it).
		if (s == nullptr) return Target::None();
		auto sub = s->Subject();
		if (sub.Unit == nullptr || sub.Battle == nullptr) return sub.Ship != nullptr ? Target::OfShip(sub.Ship) : Target::None();
		if (s->InputProfile() != EmbodimentInputProfile::Cockpit && sub.Unit->Ship != nullptr) return Target::OfShip(sub.Unit->Ship);
		return Target::OfUnit(sub.Battle, sub.Unit);
	}

	cs::List<cs::Ref<IEmbodiable>> UnitySpaceApp::SeatsInFight(const cs::Ref<Battle>& battle)
	{
		auto seats = Embodiables::SeatsIn(World(), battle);
		if (battle == nullptr || battle->Over) return seats;
		// US-T90: the stations fighting there too (a defence platform, a gate fort), in the battle's own order after the
		// hulls, so ' and ; reach a station and from it (/) every gun mounted on it.
		for (const auto& u : battle->Units)
		{
			auto station = Embodiables::StationOf(World(), battle, u);
			if (station != nullptr && station->CanEmbody() && EmbodiableIndexOf(seats, station) < 0) seats.Add(station);
		}
		return seats;
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
		if (auto held = HeldNonHelm()) // US-T135: whatever it is, the fight it is in
		{
			auto sub = held->Subject();
			if (sub.Battle != nullptr && sub.Unit != nullptr) return sub.Battle;
		}
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
		// US-T135: from its subject: where on its hull it is (a mount, a worker), else the hull itself.
		if (s == nullptr) return SpaceCamera->ScenePosition;
		auto sub = s->Subject();
		if (sub.Battle != nullptr && sub.Unit != nullptr) return BattleScene(sub.Battle, sub.HasPoint ? sub.PointKm : sub.Unit->Pos);
		if (sub.Ship != nullptr) return TargetScene(Target::OfShip(sub.Ship));
		return SpaceCamera->ScenePosition;
	}

	void UnitySpaceApp::CycleSeat(int step)
	{
		auto battle = BattleInView();
		auto seats = SeatsInFight(battle); // US-T90: was Embodiables::SeatsIn, which leaves the stations out
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
		// The hull of the seat held (a gun, a station: US-T90 / mans a station's own guns; an EVA worker, US-T135), the one
		// flown, the one selected, or a station fighting at what is selected.
		cs::Ref<Battle> battle = nullptr;
		cs::Ref<BattleUnit> unit = HullInHand(battle);
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

	// ---------------------------------------------------------------- the helm's abilities (US-T106)

	int UnitySpaceApp::HelmAbilityKey(const cs::Ref<Ship>& ship)
	{
		if (!Input.Boost || Input.NumberKey < 0 || ClickPanelOpen() || ship == nullptr || ship->Phase == ShipPhase::Lane) return 0;
		int slot = Input.NumberKey == 0 ? 10 : Input.NumberKey;
		auto ability = HelmAbilities::AtSlot(ship, slot, World()); // US-T140: the bar as the HUD shows it (a band captain's squad order last)
		if (ability == HelmAbility::None) return 0;
		Input.NumberKey = -1; // spent: Shift+digit is the ability, never also the captain's order on the digit
		return static_cast<int>(ability);
	}

	void UnitySpaceApp::NoteHelmAbility()
	{
		const auto& r = HelmAbilities::Last();
		if (r.Seq == helmAbilitySeq) return;
		helmAbilitySeq = r.Seq;
		for (int i = r.Launched.Count() - 1; i >= 0; i--)
		{
			launchedCraft.Remove(r.Launched[i]);
			launchedCraft.Insert(0, r.Launched[i]);
		}
		// Only what is still flying is worth remembering.
		launchedCraft.RemoveAll([](const cs::Ref<BattleUnit>& u) { return u == nullptr || u->Dead || u->Fled; });
		if (!r.Toast.empty()) ShowToast(r.Toast);
	}

	// ---------------------------------------------------------------- the sensors and power (US-T93)

	int UnitySpaceApp::SensorCommand(const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr || ClickPanelOpen()) return 0;
		int key = Input.SensorPing ? static_cast<int>(SensorKey::Ping) : Input.PowerToggle ? SensorPosture::PowerToggle(ship) : 0;
		if (key == 0) return 0;
		cs::String why = SensorPosture::Refusal(ship, key);
		if (!why.empty())
		{
			ShowToast(cs::Cat(key == static_cast<int>(SensorKey::Ping) ? "NO PING: " : "NO POWER CHANGE: ", why));
			return 0;
		}
		switch (static_cast<SensorKey>(key))
		{
			case SensorKey::Ping:
				ShowToast(cs::Cat("ACTIVE PING: a burst of range and detail -- and every sensor in reach hears it: LIT UP for ",
					static_cast<int>(SensorPosture::PingLitS), " s  (Tab again in ", static_cast<int>(SensorPosture::PingCooldownS), " s)"));
				break;
			case SensorKey::GoDark:
				ShowToast(cs::Cat("GOING DARK: reactor to minimum -- guns, shields and drive offline, transponder off, drifting on your momentum; the hull cools  (Shift+F3 powers up: a ",
					static_cast<int>(cs::Math::Ceiling(SensorPosture::RestartSeconds(ship))), " s restart)"));
				break;
			case SensorKey::PowerUp:
				ShowToast(cs::Cat("POWERING UP: reactor restart, ", static_cast<int>(cs::Math::Ceiling(SensorPosture::RestartSeconds(ship))),
					" s -- nothing online until it is done, and your heat climbs back  (Shift+F3 goes dark again)"));
				break;
			default:
				break;
		}
		return key;
	}

	cs::Ref<BattleUnit> UnitySpaceApp::JumpCarrier(cs::Ref<Battle>& battle)
	{
		battle = nullptr;
		if (auto f = cs::As<FighterSeat>(HeldNonHelm()))
		{
			battle = f->Battle;
			return f->Unit->Fighter != nullptr ? f->Unit->Fighter->Carrier : nullptr;
		}
		if (IsPiloting() && Piloted.Ship != nullptr && Piloted.Ship->Battle != nullptr && !Piloted.Ship->Battle->Over)
		{
			battle = Piloted.Ship->Battle;
			return battle->UnitOf(Piloted.Ship);
		}
		return nullptr;
	}

	void UnitySpaceApp::JumpToCraft()
	{
		cs::Ref<Battle> battle;
		auto carrier = JumpCarrier(battle);
		if (carrier == nullptr)
		{
			ShowToast(IsPiloting() ? cs::String("Not in a fight: nothing launched to jump into (Shift+/ works from a carrier's helm in battle)")
				: cs::String("Shift+/ jumps between a carrier's helm and the craft it launched: fly a carrier in a fight first"));
			return;
		}
		auto aloft = HelmAbilities::Aloft(battle, carrier, launchedCraft);
		auto held = cs::As<FighterSeat>(HeldNonHelm());
		int at = -1;
		if (held != nullptr)
			for (int i = 0; i < aloft.Count(); i++)
				if (aloft[i] == held->Unit) at = i;
		// Past the last of them (or with none up), back to the carrier's helm.
		if (held != nullptr && (at < 0 ? aloft.Count() == 0 : at == aloft.Count() - 1))
		{
			if (!BackToCarrier()) ShowToast("No other craft of the wing up, and the carrier's helm is out of reach");
			return;
		}
		if (aloft.Count() == 0)
		{
			ShowToast(cs::Cat("Nothing of the ", carrier->Name(), "'s up to jump into: launch a squadron first (Shift+1)"));
			return;
		}
		TakeSeat(Embodiables::FighterOf(battle, aloft[at + 1]));
	}

	bool UnitySpaceApp::BackToCarrier()
	{
		auto f = cs::As<FighterSeat>(HeldNonHelm());
		if (f == nullptr || f->Unit->Fighter == nullptr) return false;
		auto carrier = f->Unit->Fighter->Carrier;
		if (carrier == nullptr || carrier->Ship == nullptr || !carrier->Present() || carrier->Captured) return false;
		auto helm = Embodiables::HelmOf(World(), carrier->Ship);
		if (helm == nullptr || !helm->CanEmbody()) return false;
		TakeSeat(helm);
		return true;
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
		auto heldOn = held != nullptr ? held->Subject() : EmbodimentSubject(); // US-T135: whatever is held, the unit it is on
		if (heldOn.Unit != nullptr && heldOn.Battle != nullptr)
		{
			onUnit = heldOn.Unit;
			battle = heldOn.Battle;
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
		// US-T90: and the stations in the fight in view, which ' and ; reach too.
		for (const auto& s : SeatsInFight(battle))
			if (EmbodiableIndexOf(pickerSeats, s) < 0) pickerSeats.Add(s);
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
		NoteSelfDestruct(); // US-T97: a seat lost to its own self-destruct (in co-op, or a helm's) is told of here
		if (Coop::CoopPeer::Active()) { CoopUpdateSeat(dt); return; } // US-T44: every player's seat, stepped on every peer
		auto held = HeldNonHelm();
		if (held == nullptr) return;
		auto look = ClickPanelOpen() ? Vector2::zero() : Input.MouseDelta;
		auto rot = SpaceCamera->Rotation;
		auto up = rot * Vector3::up();
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
		input.Drift = Input.Drift; // US-T165: R held, a launched craft drifts (other seats ignore it)
		input.JumpJets = Input.JumpJets; // US-T83: Space held, a mech's jump jets (other seats ignore it)
		input.Primary = Input.Fire && !ClickPanelOpen();
		input.Secondary = Input.LookHeld && !ClickPanelOpen();
		// The arrows share the reactor the seat draws on (US-T94): a craft's own, a gun's hull's, a station's.
		input.PowerSplit = CraftPower::After(held, Input.PowerKey);
		// Shift+Backspace, the self-destruct (US-T97): held a second it arms, pressed again it aborts.
		input.SelfDestruct = SelfDestructCommand(held);
		// The keys a profile adds on top of the shared hands (US-T135): a cockpit's lock, flares and wing orders. A worker's
		// torch is the trigger itself (its timing judged in Core, RepairWorker), so it needs nothing here.
		if (held->InputProfile() == EmbodimentInputProfile::Cockpit)
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
			NoteSelfDestruct(); // US-T97
			return;
		}
		StepSeatFeel(held, input, dt); // US-T170
		auto pose = held->PoseAt(Clock->Jd);
		auto roof = ToVector(pose.Up.SwapYZ());
		SpaceCamera->PilotView = Quaternion::LookRotation(seatAim, Vector3::Dot(roof, seatAim) > 0.99f ? up : roof);
	}

	void UnitySpaceApp::StepSeatFeel(const cs::Ref<IEmbodiable>& held, const EmbodimentInput& input, float dt)
	{
		// US-T170: a launched craft flies with a fighter's feel, scaled by its own mass; an EVA worker with a drone's, nimble
		// and light, and every good weld strikes with a sharp tick. A gun mount or a station's chair has no flight to feel.
		FlightFeelInput in;
		in.Boosting = input.Boost;
		if (auto f = cs::As<FighterSeat>(held); f != nullptr && f->Unit != nullptr)
		{
			auto craft = f->Unit->Fighter != nullptr ? FighterCraft::Of(f->Unit->Fighter->Role) : FighterCraft::Interceptor();
			if (craft == nullptr) return;
			double mass = craft->Spec != nullptr ? craft->Spec->DryMassTonnes : 0;
			FeelFor(f, FlightFeelProfile::Of(FeelRole::Fighter, mass));
			in.Pace = f->Unit->Vel.Length() / cs::Math::Max(craft->Tuning().MaxSpeed, 1e-9);
			in.Thrust = std::max(std::abs(static_cast<double>(input.Throttle)),
				0.5 * std::max(std::abs(static_cast<double>(input.StrafeRight)), std::abs(static_cast<double>(input.StrafeUp))));
		}
		else if (auto w = cs::As<RepairWorker>(held))
		{
			bool fresh = feelFor != std::static_pointer_cast<void>(w);
			FeelFor(w, FlightFeelProfile::Of(FeelRole::Drone, 0.2));
			if (fresh) seatWeldHits = w->Weld.Hits;
			if (w->Weld.Hits > seatWeldHits) flightFeel.Kick(0.6 + 0.1 * std::min(w->Weld.Streak, 4));
			seatWeldHits = w->Weld.Hits;
			// The suit's jets: a light push on the crawl, more with them lit.
			in.Thrust = (input.Boost ? 0.6 : 0.25) * std::min(1.0, std::abs(static_cast<double>(input.Throttle)) + std::abs(static_cast<double>(input.StrafeRight)));
			in.Boosting = false;
		}
		else if (auto m = cs::As<MechWalker>(held))
		{
			// US-T83: the mech's weight: every footfall a thud through the cockpit, a landing a jolt by how hard it came
			// down, the jump jets a roar.
			bool fresh = feelFor != std::static_pointer_cast<void>(m);
			FeelFor(m, FlightFeelProfile::Of(FeelRole::Hauler, 60));
			if (fresh)
			{
				seatMechFootfalls = m->Footfalls;
				seatMechLandings = m->Landings;
			}
			if (m->Footfalls > seatMechFootfalls) flightFeel.Kick(0.25 + 0.25 * std::min(1.0, std::abs(m->Speed) / MechWalker::MaxForwardKmS));
			if (m->Landings > seatMechLandings) flightFeel.Kick(1.0);
			seatMechFootfalls = m->Footfalls;
			seatMechLandings = m->Landings;
			in.Pace = std::abs(m->Speed) / MechWalker::MaxForwardKmS;
			in.Thrust = m->JetsLit ? 1.0 : 0.3 * std::min(1.0, std::abs(static_cast<double>(input.Throttle)));
			in.Boosting = m->JetsLit && input.Boost;
		}
		else return;
		flightFeel.Step(dt, in);
		feelStepped = true;
	}

	void UnitySpaceApp::ClampSeatAim(const cs::Ref<IEmbodiable>& held)
	{
		auto f = cs::As<FighterSeat>(held);
		if (f == nullptr || f->Unit == nullptr) return;
		// The string aim (US-T168): a light craft's is slack, the view anywhere (a full 180) and the craft whipping round
		// after it; a heavy fighter's or bomber's taut, held within PilotAim::HeavyMaxLeadDeg of its nose. Drifting (US-T165)
		// any craft's goes slack.
		if (Input.Drift) return;
		auto craft = f->Unit->Fighter != nullptr ? FighterCraft::Of(f->Unit->Fighter->Role) : FighterCraft::Interceptor();
		if (craft == nullptr || cs::As<LightFighterFlightModel>(craft->Model()) != nullptr) return;
		auto aim = DVec3(seatAim.x, seatAim.y, seatAim.z).SwapYZ();
		if (aim.LengthSquared() < 1e-12 || f->Unit->Forward.LengthSquared() < 1e-12) return;
		aim = PilotFlight::ClampAim(aim.Normalized(), f->Unit->Forward.Normalized(), PilotAim::HeavyMaxLeadDeg * Astro::Deg2Rad);
		seatAim = ToVector(aim.SwapYZ()).normalized();
	}

	void UnitySpaceApp::UpdateCockpit(const cs::Ref<FighterSeat>& f, DVec3 aim)
	{
		if (Input.ToggleFlightAssist)
			ShowToast(f->ToggleFlightAssist()
				? "Flight assist off: it drifts on its momentum, W/S are raw thrust, Alt flips for a turn and burn"
				: "Flight assist on: the thrusters cancel drift and it flies where it points");
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
				// US-T96: any hull's broadcast array is in the cycle; knocked out, the victim cannot report the attack.
				ShowToast(weak == nullptr ? cs::String("Lock a ship (T) to pick a weak point or its broadcast array")
					: BroadcastArrays::Is(f->Unit->PlayerLockTarget, weak) ? cs::String("Broadcast array: knock it out and they cannot report the attack")
					: cs::Cat("Weak point: ", WeakPoints::Label(weak)));
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

	// ---------------------------------------------------------------- the self-destruct (US-T97)

	int UnitySpaceApp::SelfDestructCommand(const cs::Ref<IEmbodiable>& held)
	{
		if (held == nullptr || !SelfDestruct::Can(held))
		{
			selfDestructAborted = false;
			return 0;
		}
		bool armed = SelfDestruct::Armed(held);
		int key = 0;
		if (Input.SelfDestructTap && armed)
		{
			key = static_cast<int>(SelfDestructKey::Abort);
			selfDestructAborted = true; // the same press held on does not arm it again
			ShowToast(cs::Cat("Self-destruct aborted: the ", held->Name(), " stands down"));
		}
		else if (Input.SelfDestructArm && !armed && !selfDestructAborted)
		{
			key = static_cast<int>(SelfDestructKey::Arm);
			double fuse = SelfDestruct::FuseSeconds(held);
			ShowToast(cs::Cat("SELF-DESTRUCT ARMED: the ", held->Name(), " goes up in ", cs::Fmt(fuse, "0"), " s, with everyone aboard  (Shift+Backspace aborts)"));
		}
		if (Input.SelfDestructHoldS <= 0) selfDestructAborted = false;
		// Watch it from the moment it is armed, and keep where it is, so the camera can be set clear of it after.
		if (armed || key == static_cast<int>(SelfDestructKey::Arm))
		{
			if (!selfDestructWatching) selfDestructSeq = SelfDestruct::Last().Seq;
			selfDestructWatching = true;
			selfDestructScene = SeatScene(held);
		}
		else selfDestructWatching = false;
		return key;
	}

	void UnitySpaceApp::NoteSelfDestruct()
	{
		const auto& r = SelfDestruct::Last();
		if (!selfDestructWatching || r.Seq == selfDestructSeq || AtControls()) return;
		selfDestructWatching = false;
		selfDestructSeq = r.Seq;
		ShowToast(cs::Cat("The ", r.Name, " self-destructs: a ", cs::Fmt(r.RadiusKm, "0.0"), " km blast",
			r.Hit > 0 ? cs::Cat(", ", r.Hit, r.Hit == 1 ? " hull" : " hulls", " caught", r.Destroyed > 0 ? cs::Cat(", ", r.Destroyed, " destroyed") : cs::String()) : cs::String(),
			r.CrewLost >= 1 ? cs::Cat(", ", cs::Fmt(r.CrewLost, "N0"), " aboard lost") : cs::String(),
			"  --  the deed is its crew's. Back in command, ", Clock->Warp().Label));
		// The chase left the camera on the hull: back it out clear of the blast, looking at where it was.
		if (SpaceCamera->Mode != Cameras::SpaceCamera::CameraMode::Free) return;
		double safe = cs::Math::Max(r.RadiusKm * BattleKmScale() * 2.5, 1e-6);
		auto away = SpaceCamera->ScenePosition - selfDestructScene;
		double d = away.Length();
		if (d >= safe) return;
		auto dir = d > 1e-12 ? away / d : DVec3(0, 0.3, -1).Normalized();
		SpaceCamera->SetPose(selfDestructScene + dir * safe, Quaternion::LookRotation(ToVector(dir * -1.0), Vector3::up()));
	}

	cs::String UnitySpaceApp::SeatLine()
	{
		// US-T135: every kind's line is its own role panel's title and state; its keys are on the role panel beside it.
		auto held = HeldNonHelm();
		if (held == nullptr) return cs::String();
		auto panel = held->RolePanel();
		return cs::Cat(panel.Title, "  \xC2\xB7  ", panel.Status.empty() ? held->Name() : panel.Status, "  \xC2\xB7  P back to command"); // (\xC2\xB7 is the UTF-8 middle dot)
	}
}
