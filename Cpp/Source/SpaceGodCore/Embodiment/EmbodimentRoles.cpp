// US-T135: the universal embody contract (IEmbodiable's InputProfile, CameraRig, RolePanel and Subject) for every kind that
// predates it, in one place so the roles read side by side. The EVA repair worker's are in RepairWorker.cpp.

#include "Embodiment/Embodiment.h"
#include "Embodiment/FighterSeat.h"
#include "Embodiment/HelmAbilities.h"
#include "Embodiment/MountedTurret.h"
#include "Embodiment/RepairWorker.h"
#include "Embodiment/SatelliteSeat.h"
#include "Embodiment/ShipHelm.h"
#include "Embodiment/StationSeat.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	namespace
	{
		void Add(EmbodimentRolePanel& p, const char* key, cs::String label, bool ready = true) { p.Abilities.Add(EmbodimentAbility(key, std::move(label), ready)); }

		/// The keys every role shares: the switching layer's own.
		void Switching(EmbodimentRolePanel& p, bool hasGuns, bool hasCrew)
		{
			if (hasGuns) Add(p, "/", "man the next gun");
			if (hasCrew) Add(p, "Shift+P", "out on the hull as an EVA repair worker");
			Add(p, "' ;", "next seat in the fight");
			Add(p, "F9", "pick any seat from the list");
			Add(p, "Shift+Backspace (hold)", "self-destruct");
			Add(p, "P", "back to the free SpaceGod view");
		}
	}

	// ------------------------------------------------------------ a ship's helm

	EmbodimentInputProfile ShipHelm::InputProfile() const { return EmbodimentInputProfile::Helm; }

	EmbodimentCameraRig ShipHelm::CameraRig() const { return EmbodimentCameraRig(EmbodimentCameraKind::Chase, 2.5); }

	EmbodimentRolePanel ShipHelm::RolePanel()
	{
		EmbodimentRolePanel p;
		p.Title = "HELM";
		p.Verb = "Flying";
		p.Status = Ship != nullptr ? Ship->Name : cs::String();
		Add(p, "Mouse", "steer");
		Add(p, "W/S  A/D Q/E  Z/X", "thrust, strafe, roll");
		Add(p, "Left / right mouse", "guns / missiles");
		if (Ship != nullptr)
		{
			auto bar = HelmAbilities::Of(Ship);
			for (int i = 0; i < bar.Count() && i < 10; i++)
				p.Abilities.Add(EmbodimentAbility(cs::Cat("Shift+", i == 9 ? 0 : i + 1), bar[i].Label, bar[i].Ready));
		}
		auto unit = Ship != nullptr && Ship->Battle != nullptr ? Ship->Battle->UnitOf(Ship) : nullptr;
		Switching(p, unit != nullptr, RepairWorker::TeamsOn(unit) > 0);
		return p;
	}

	EmbodimentSubject ShipHelm::Subject() const
	{
		EmbodimentSubject s;
		s.Ship = Ship;
		if (Ship != nullptr && Ship->Battle != nullptr)
		{
			s.Battle = Ship->Battle;
			s.Unit = Ship->Battle->UnitOf(Ship);
		}
		return s;
	}

	// ------------------------------------------------------------ a launched craft's cockpit

	EmbodimentInputProfile FighterSeat::InputProfile() const { return EmbodimentInputProfile::Cockpit; }

	EmbodimentCameraRig FighterSeat::CameraRig() const { return EmbodimentCameraRig(EmbodimentCameraKind::Chase, 7); }

	EmbodimentRolePanel FighterSeat::RolePanel()
	{
		EmbodimentRolePanel p;
		p.Title = "COCKPIT";
		p.Verb = "Flying";
		p.Status = Readout();
		Add(p, "Mouse", "aim (the craft turns after it)");
		Add(p, "W/S  A/D Q/E  Z/X", "throttle, strafe, roll");
		Add(p, "Shift", "boost (with A/D Q/E a lateral boost)");
		Add(p, "F / Alt", "flight assist / flip for a turn and burn");
		Add(p, "Left / right mouse", "guns / weapon group");
		Add(p, "1 / T / 6 / Ctrl", "next group / lock / weak point / flares");
		Add(p, "2 3 4 5", "wing: attack my target, cover me, form up, at will");
		Add(p, "7 8 9 0", "power to weapons, shields, engines, even");
		Add(p, "Shift+/", "next craft of the wing");
		Add(p, "Backspace", "back to the carrier's helm");
		Switching(p, false, false);
		return p;
	}

	EmbodimentSubject FighterSeat::Subject() const
	{
		EmbodimentSubject s;
		s.Battle = Battle;
		s.Unit = Unit;
		s.Ship = Unit != nullptr ? Unit->Ship : nullptr;
		return s;
	}

	// ------------------------------------------------------------ a gun mount

	EmbodimentInputProfile MountedTurret::InputProfile() const { return EmbodimentInputProfile::Gunner; }

	EmbodimentCameraRig MountedTurret::CameraRig() const { return EmbodimentCameraRig(EmbodimentCameraKind::Mount, 2.5, 0.04); }

	EmbodimentRolePanel MountedTurret::RolePanel()
	{
		EmbodimentRolePanel p;
		p.Title = "GUN";
		p.Verb = "Manning";
		p.Status = cs::Cat(Name(), "  \xC2\xB7  ", Mount->Cooldown > 0 ? "reloading" : "ready");
		Add(p, "Mouse", "lay it (within its fire cone)");
		Add(p, "Left mouse", "fire");
		Add(p, "/", "next gun (past the last: up to the hull)");
		if (host != nullptr) Add(p, "Backspace", cs::Is<ShipHelm>(host) ? cs::String("up to the helm") : cs::String("up to the whole station"));
		Switching(p, false, RepairWorker::TeamsOn(Unit) > 0);
		return p;
	}

	EmbodimentSubject MountedTurret::Subject() const
	{
		EmbodimentSubject s;
		s.Battle = Battle;
		s.Unit = Unit;
		s.Ship = Unit != nullptr ? Unit->Ship : nullptr;
		s.Part = Mount;
		s.HasPoint = Unit != nullptr && Mount != nullptr;
		if (s.HasPoint) s.PointKm = Unit->PointOf(Mount->Def);
		return s;
	}

	// ------------------------------------------------------------ a station

	EmbodimentInputProfile StationSeat::InputProfile() const { return EmbodimentInputProfile::Station; }

	EmbodimentCameraRig StationSeat::CameraRig() const { return EmbodimentCameraRig(EmbodimentCameraKind::Chase, 2.5); }

	EmbodimentRolePanel StationSeat::RolePanel()
	{
		EmbodimentRolePanel p;
		p.Title = "STATION";
		p.Verb = "Holding";
		p.Status = Name();
		Add(p, "Mouse", "lay every gun on the aim");
		Add(p, "Left mouse", "fire them all");
		Add(p, "Right mouse", "launch its craft");
		cs::Ref<Battle> fight;
		auto unit = Unit(fight);
		Switching(p, unit != nullptr, RepairWorker::TeamsOn(unit) > 0);
		return p;
	}

	EmbodimentSubject StationSeat::Subject() const
	{
		EmbodimentSubject s;
		cs::Ref<Battle> fight;
		s.Unit = Unit(fight);
		s.Battle = fight;
		return s;
	}

	// ------------------------------------------------------------ a satellite

	EmbodimentInputProfile SatelliteSeat::InputProfile() const { return EmbodimentInputProfile::Satellite; }

	EmbodimentCameraRig SatelliteSeat::CameraRig() const { return EmbodimentCameraRig(EmbodimentCameraKind::Chase, 2.5); }

	EmbodimentRolePanel SatelliteSeat::RolePanel()
	{
		EmbodimentRolePanel p;
		p.Title = "SATELLITE";
		p.Verb = "Flying";
		p.Status = Name();
		Add(p, "Mouse", "point its boresight");
		Add(p, "W/S  A/D Q/E", "nudge it on its thrusters");
		Add(p, "P", "back to the free SpaceGod view");
		return p;
	}

	EmbodimentSubject SatelliteSeat::Subject() const { return EmbodimentSubject(); }
}
