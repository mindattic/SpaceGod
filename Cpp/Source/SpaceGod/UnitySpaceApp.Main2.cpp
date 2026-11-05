// Port of Unity.SpaceGod Runtime/UnitySpaceApp.cs lines 1471-end (US-T29): the second half of UnitySpaceApp.cs,
// split from the first (UnitySpaceApp.cpp) only for porting. Members are declared in UnitySpaceApp.Main2.inl.
// A C# null string is the empty string here (SalvageApproach, EndSalvage's toast).

#include "UnitySpaceApp.h"
#include "CaptureEvents.h" // US-T72

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "BodyOrientation.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "OrbitalElements.h"
#include "RenderLod.h"
#include "ScaleModel.h"
#include "ShipLod.h"
#include "Ships.h"
#include "SimClock.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/Broadsides.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Diplomacy.h"
#include "Combat/EnergyWeapons.h"
#include "Combat/Morale.h"
#include "Combat/Outfitting.h"
#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Combat/Pursuit.h"
#include "Combat/Radiators.h"
#include "Combat/Salvage.h"
#include "Combat/War.h"
#include "Combat/WeakPatches.h"
#include "Combat/Wreck.h"
#include "Economy/Companies.h"
#include "Economy/Economy.h"
#include "Economy/Propulsion.h"
#include "Economy/Settlement.h"
#include "Events/Opportunity.h"
#include "Infrastructure/Infrastructure.h"
#include "Law/Law.h"
#include "Persistence/Profiles.h" // GameSettings::AutoReorient (Core/Persistence/Profiles.cs)
#include "Rendering/Photoreal.h"
#include "Rendering/RenderPipelineProfiles.h"
#include "Sensors/Sensors.h"
#include "Settings/GameSetting.h"
#include "Systems/AlphaCentauri.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/GateTransit.h"
#include "Systems/JumpGate.h"
#include "Systems/StarSystemBuilder.h"

#include "UnityCompat/UnityRendering.h"

#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/Pipeline/RenderBackend.h"
#include "Rendering/ShipMeshFactory.h"
#include "Rendering/ShipModels.h"
#include "UI/Icons.h"
#include "Views/AsteroidBelt.h"
#include "Views/BattleFx.h"
#include "Views/BodyView.h"
#include "Views/DebrisFields.h"
#include "Views/DiscView.h"
#include "Views/EmbeddedPods.h"
#include "Views/FighterViews.h"
#include "Views/GateView.h"
#include "Views/InterstellarView.h"
#include "Views/LaneView.h"
#include "Views/LineStrip.h"
#include "Views/PodBeacons.h"
#include "Views/SatelliteSwarm.h"
#include "Views/ShipView.h"
#include "Views/StructureViews.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		/// C#'s Array.Resize(ref a, n): keeps what fits and default-fills the rest.
		template <class T> void ResizeKeep(cs::List<T>& a, int n)
		{
			a.Raw().resize(static_cast<size_t>(n));
		}

		/// Unity's implicit Vector3 -> Vector4 (w = 0).
		Vector4 ToVector4(Vector3 v) { return Vector4(v.x, v.y, v.z, 0); }
	}

	void UnitySpaceApp::UpdatePursuit(const cs::Ref<Core::Ship>& ship, float dt)
	{
		auto pursuit = World()->Pursuit;
		if (pursuit == nullptr) return;
		pilotEmpSeconds = cs::Math::Max(0, pilotEmpSeconds - dt);
		double jd = Clock->Jd;
		pursuitCheckTimer -= dt;
		if (pursuitCheckTimer <= 0 && ship->Battle == nullptr)
		{
			pursuitCheckTimer = PursuitCheckIntervalS;
			for (const auto& g : pursuit->Consider(ship, pilotFrameBody, pilotOffsetKm(), pilotVelKmS(), jd))
			{
				if (Clock->WarpIndex > 0) Clock->SetWarp(0);
				ShowToast(cs::Cat("CONTACT: ", HunterName(g), " has turned to run you down, ",
					Mathf::RoundToInt(static_cast<float>((g->Pos - pilotOffsetKm()).Length())), " km out"));
			}
		}
		if (!pursuit->Active()) return;
		for (const auto& [g, what] : pursuit->Step(ship, pilotFrameBody, pilotOffsetKm(), pilotVelKmS(), dt, jd))
		{
			switch (what)
			{
			case Core::PursuitEvent::EmpCharging:
				ShowToast(cs::Cat("EMP CHARGING on the ", g->Leader->Name, ": get more than ", Mathf::RoundToInt(static_cast<float>(Core::Pursuit::EmpRangeKm * 1.25)), " km clear in ",
					Mathf::RoundToInt(static_cast<float>(Core::Pursuit::EmpChargeSeconds)), " s"));
				break;
			case Core::PursuitEvent::EmpHit:
				pilotEmpSeconds = Core::Pursuit::EmpStunSeconds;
				SetpilotCruise(Core::PilotCruiseState{});
				ShowToast(cs::Cat("EMP from the ", g->Leader->Name, ": drives down for ", Mathf::RoundToInt(static_cast<float>(Core::Pursuit::EmpStunSeconds)), " s"));
				break;
			case Core::PursuitEvent::EmpMissed:
				ShowToast(cs::Cat("The ", g->Leader->Name, "'s EMP goes off short of you"));
				break;
			case Core::PursuitEvent::Engage:
			{
				auto battle = World()->War->TryStartPursuit(ship, g, pilotFrameBody, pilotOffsetKm(), pilotForward(), jd);
				if (battle == nullptr) break;
				pursuit->Engaged(g);
				EnterBattleTime(battle);
				ShowToast(cs::Cat(HunterName(g), " engages: fight or run"));
				break;
			}
			case Core::PursuitEvent::GaveUp:
				ShowToast(cs::Cat("The ", g->Leader->Name, " has given up the chase"));
				break;
			default:
				break;
			}
		}
	}

	cs::String UnitySpaceApp::HunterName(const cs::Ref<Core::Pursuer>& g)
	{
		return cs::Cat("the ", g->Faction()->Name, " ", cs::Strings::ToLowerInvariant(cs::Str(g->Leader->Spec->Class)), " ", g->Leader->Name,
			(g->Ships.Count() > 1 ? cs::Cat(" and ", g->Ships.Count() - 1, " more") : cs::String()));
	}

	Core::DVec3 UnitySpaceApp::PilotGravity()
	{
		auto helio = Ephemeris()->Helio(pilotFrameBody) + pilotOffsetKm();
		auto g = Core::DVec3::Zero;
		for (int b = 0; b < Catalog()->Count(); b++)
		{
			auto body = (*Catalog())[b];
			if (body->Gm > 0) g += Core::PilotFlight::Gravity(helio - Ephemeris()->Helio(b), body->Gm, body->RadiusKm);
		}
		return g;
	}

	bool UnitySpaceApp::KeepPilotAboveSurfaces(const cs::Ref<Core::Ship>& ship)
	{
		auto frameHelio = Ephemeris()->Helio(pilotFrameBody);
		for (int b = 0; b < Catalog()->Count(); b++)
		{
			auto body = (*Catalog())[b];
			if (body->Gm <= 0) continue;
			auto centre = Ephemeris()->Helio(b) - frameHelio;
			if ((pilotOffsetKm() - centre).Length() > body->RadiusKm * Core::PilotFlight::GravityReachRadii) continue;
			double sink = 0;
			if (!Core::PilotFlight::KeepAboveSurface(pilot(), centre, body->RadiusKm + cs::Math::Max(0.5, ship->LengthKm()), sink)) continue;
			if (!SurfaceImpact(ship, b, (pilotOffsetKm() - centre).Normalized(), sink)) return false;
		}
		return true;
	}

	void UnitySpaceApp::UpdatePropellant(const cs::Ref<Core::Ship>& ship, float dt)
	{
		if (TankerDueS >= 0)
		{
			TankerDueS -= dt;
			if (TankerDueS < 0)
			{
				double tonnes = Core::Propulsion::TankerLoad(ship);
				double fee = Core::Propulsion::TankerFee(tonnes);
				ship->SetPropellant(ship->Propellant() + tonnes);
				ship->Faction->Treasury -= fee;
				ShowToast(cs::Cat("A tanker comes alongside: ", cs::Fmt(tonnes, "N0"), " t of reaction mass for ",
					cs::Fmt(fee, "N0"), " cr"));
			}
		}
		int level = ship->Propellant() <= 1e-6 ? 2 : ship->PropellantFraction() < 0.15 ? 1 : 0;
		if (level > propellantWarning)
			ShowToast(level == 2 ? cs::String("PROPELLANT DRY: drifting. Refuel at a port's market, or call a tanker") : cs::Cat("Propellant low: ", Mathf::RoundToInt(static_cast<float>(ship->PropellantFraction() * 100)), "% left"));
		propellantWarning = level;
	}

	void UnitySpaceApp::CallTanker()
	{
		if (CoopSends("tanker")) return; // US-T44 (US-T43): into the frames (the tanker's timer is the shared helm's)
		if (!IsPiloting() || TankerDueS >= 0) return;
		auto ship = Piloted.Ship;
		if (ship == nullptr || Core::Propulsion::TankerLoad(ship) < 1) { ShowToast("Tanks are full"); return; }
		TankerDueS = TankerCallS;
		ShowToast(cs::Cat("Tanker called: about ", Mathf::RoundToInt(TankerCallS), " s out, ",
			cs::Fmt(Core::Propulsion::TankerFee(Core::Propulsion::TankerLoad(ship)), "N0"), " cr to fill the tanks"));
	}

	bool UnitySpaceApp::UpdateGateActivation(const cs::Ref<Core::Ship>& ship)
	{
		if (ActiveSystem->Gates().Count() == 0 || ship->Battle != nullptr) { nearGate = nullptr; return false; }
		auto shipHelioKm = Ephemeris()->Helio(pilotFrameBody) + pilotOffsetKm();
		auto wasNear = nearGate;
		auto gate = nearGate = ActiveSystem->GateInRange(shipHelioKm);
		if (gate != nullptr && gate != wasNear)
			ShowToast(cs::Cat("In range of ", cs::Strings::ToUpperInvariant(gate->Name), " to ", cs::Strings::ToUpperInvariant(gate->Destination()->Name),
				(gate->Admits(ship->Faction, Clock->Jd) && gate->Partner->Admits(ship->Faction, Clock->Jd) ? " (Y to activate)" : " (shut: under blockade)")));
		if (gate == nullptr || !Input.ActivateGate) return false;
		// A blockaded gate (or one whose far end is) will not open for the pilot either.
		if (!gate->Link->Open(ActiveSystem, ship->Faction, Clock->Jd)) { ShowToast(cs::Cat(cs::Strings::ToUpperInvariant(gate->Name), " is shut to ", ship->Faction->Name)); return false; }

		auto destination = gate->Destination();
		cs::Ref<Core::Ship> arrived;
		if (!Core::GateTransit::TryPerform(gate, shipHelioKm, ship, World(), Clock->Jd, arrived)) return false;
		CaptureEvents::GateDeparture(ship->Name, gate->Name, ActiveSystem->Name, destination->Name); // US-T72
		AdvanceCourse(gate, arrived);
		// A charge is lost going through a gate (U.SPC-T417).
		StopFtl();
		EndSalvage(cs::String());
		SalvageApproach = cs::String();

		World()->PilotedShip = nullptr;
		// The system left behind no longer sees the pilot, nor holds a patrol's hail on it.
		if (World()->Sensors) World()->Sensors->UpdatePilot(nullptr, Core::DVec3(), false, Clock->Jd);
		if (World()->Law) World()->Law->UpdatePilot(nullptr, 0, Core::DVec3(), Core::DVec3(), Clock->Jd);
		// Through the gate, the chase is left behind in the system the ship has left.
		if (World()->Pursuit) World()->Pursuit->ReleaseAll(Clock->Jd);
		// US-T58: the far system's world holds the hull before Activate brings that system up to date (Galaxy.Promote
		// steps its fleet): left unheld, the arrival GateTransit parked with no destination (DestinationBody -1, its phase
		// already over) departs for body -1 there, and the catalogue lookup throws every frame from then on.
		destination->World->PilotedShip = arrived;
		Activate(destination);
		// The far side streams in behind the gate's own flare and fold (U.SPC-T414).
		BeginHelmGateCover();
		Piloted = Target::OfShip(arrived);
		World()->PilotedShip = arrived;
		pilotFrameBody = arrived->Orbit->Body;
		SeedPilot(arrived->Orbit->Center, Core::DVec3::Zero, arrived->Orbit->V);
		pilotBattleCheckTimer = 0;
		pilotLockTarget = Target::None();
		pilotLockUnit = nullptr;
		SpaceCamera->PilotLockTarget = Target::None();
		nearGate = nullptr;
		// Arriving beside an on-ramp is not flying in through it.
		insideRampGate = true;
		// A tanker called in the old system does not follow the hull through the gate.
		TankerDueS = -1;

		Select(Piloted);
		if (CoopHelmMine()) SpaceCamera->EnterPilotChase(Piloted, 6); // US-T44 (US-T43): the holder's view only
		ShowToast(cs::Cat("Jumped to ", cs::Strings::ToUpperInvariant(destination->Name), " through the gate"));
		return true;
	}

	void UnitySpaceApp::UpdatePilotBattle(const cs::Ref<Core::Ship>& ship, float dt)
	{
		if (ship->Battle != nullptr && Salvage != nullptr) EndSalvage("Clamps released: battle stations");
		if (ship->Battle != nullptr) SalvageApproach = cs::String();
		if (ship->Battle == nullptr)
		{
			// Out of any fight: a hail left open when the last one ended (or before release) is gone,
			// so the next battle's demand or surrender gets its own prompt.
			demandPrompted = false;
			termsPrompted = nullptr;
			// Nor is a trigger held as the last fight ended still held: the next one would open with a volley unasked.
			helmTriggerWas = false;
			// A patrol's order to heave to has the number keys (1 pays the fine) even alongside or latched to a hulk.
			bool salvaging = UpdateSalvage(ship, dt);
			bool hailed = UpdateLawHail(ship);
			if (!salvaging && !hailed) UpdateHavenAssault(ship);
			// Holding fire on a locked ship the player's flag has no quarrel with opens fire on it anyway: an
			// attack on a lawful ship, a crime if anyone sees it. An enemy is fought as usual (below), and one
			// alone with nobody armed to start that fight, a lone merchant, is attacked the same way.
			auto locked = pilotLockTarget.IsShip() ? pilotLockTarget.Ship : nullptr;
			if (locked != nullptr && Input.Fire && !pilotCruise().Active() && !ClickPanelOpen())
			{
				bool enemy = World()->Diplomacy()->Hostile(ship->Faction, locked->Faction);
				if (World()->War->TryStartPlayerAttack(ship, locked, pilotFrameBody, pilotOffsetKm(), pilotForward(), Clock->Jd) != nullptr)
				{
					ShowToast(enemy ? cs::Cat("Weapons free on the ", locked->Name)
						: cs::Cat("Weapons free on the ", locked->Name, ": an attack on a lawful ship is a crime if anyone sees it"));
					return;
				}
			}
			pilotBattleCheckTimer -= dt;
			if (pilotBattleCheckTimer > 0) return;
			pilotBattleCheckTimer = PilotBattleCheckIntervalS;
			// The piloted ship's scanners: a hidden haven within range goes on the map.
			auto scanAt = Ephemeris()->HelioAt(pilotFrameBody, Clock->Jd) + pilotOffsetKm();
			double scanRange = Core::Outfitting::ScanRangeKm(ship);
			for (const auto& found : World()->War->ScanFrom(ship->Faction, scanAt, Clock->Jd, ship->Name, scanRange))
				ShowToast(cs::Cat("Scanners pick out a hidden pirate haven: ", found->Name, " on ", (*Catalog())[found->Body]->Name));
			World()->War->TryStartPlayerBattle(ship, pilotFrameBody, pilotOffsetKm(), pilotForward(), Clock->Jd);
			return;
		}

		auto unit = ship->Battle->UnitOf(ship);
		if (unit == nullptr) return;
		unit->SetPlayerControlled(true);
		unit->Pos = pilotOffsetKm() - ship->Battle->Anchor;
		unit->Vel = pilotVelKmS();
		unit->Forward = pilotForward();
		// Rolled at the helm, its fire cones roll with it (U.SPC-T499, U.SPC-T533).
		unit->Up = pilot().Up;
		// No firing while cruising -- the cruise drive is a travel tier, not a combat one.
		// Nor while a pulse has the hull's systems down.
		bool armed = !pilotCruise().Active() && !ClickPanelOpen() && unit->Stunned <= 0;
		unit->PlayerFireHeld = Input.Fire && armed;
		// The right button launches missiles once the lock is made (U.SPC-T270); the guns fire down the crosshair.
		unit->PlayerSecondaryHeld = Input.LookHeld && armed;
		unit->PlayerAim = pilotAim;
		// At a capital ship's helm the trigger calls broadsides (U.SPC-T272): held, the battery bearing where the
		// player looks builds a volley; let go, it fires.
		bool helm = PilotControlMode == Core::PilotControl::Helm;
		unit->PlayerHelm = helm;
		unit->PlayerVolleyAim = helm ? PilotLookDirection() : Core::DVec3::Zero;
		if (helm && helmTriggerWas && !unit->PlayerFireHeld) unit->PlayerVolleyRelease = true;
		helmTriggerWas = helm && unit->PlayerFireHeld;
		// The rest of the arsenal (U.SPC-T271): the EMP on the middle button, a mine on Ctrl; an auto turret needs no hand.
		unit->PlayerEmpHeld = Input.FireEmp && armed;
		if (Input.DropMine && armed) unit->PlayerMineRequested = true;
		// A fighter locked in this fight is held as its unit; a ship as itself, so the lock outlives the fight.
		if (pilotLockUnit != nullptr && (!pilotLockUnit->Present() || !ship->Battle->Units.Contains(pilotLockUnit))) pilotLockUnit = nullptr;
		auto lockUnit = pilotLockUnit ? pilotLockUnit : (pilotLockTarget.IsShip() ? ship->Battle->UnitOf(pilotLockTarget.Ship) : nullptr);
		if (lockUnit != unit->PlayerLockTarget) unit->LockProgress = 0;
		unit->PlayerLockTarget = lockUnit;
		UpdateSurrenderPrompts(ship->Battle, unit);
		// A fight aboard, this hull's own or one its marines are in (U.SPC-T468).
		UpdateBoardingCommand(ship->Battle, unit);
		// A capital ship's captain's orders: batteries, shields, brace, damage control, the line (U.SPC-T425).
		UpdateHelmCommand(ship->Battle, unit);
		// The phaser arrays' tuning, on any hull that carries them (U.SPC-T488).
		if (Input.TunePhasers && Core::EnergyWeapons::HasPhasers(unit->Combat))
			ShowToast(cs::Cat("Phaser arrays tuned to a ", Core::EnergyWeapons::Label(Core::EnergyWeapons::Retune(unit->Combat))));
		// Run hot or throttle down, on a big ship with radiators to manage (U.SPC-T491).
		if (Input.ToggleRunHot && Core::Radiators::Carries(unit->Combat))
		{
			if (auto cmd = Core::Broadsides::Of(unit))
			{
				cmd->RunHot = !cmd->RunHot;
				ShowToast(cmd->RunHot ? "Run hot: guns and drive at full, whatever the heat does" : "Throttle down for the heat as needed");
			}
		}
		// Roll the weak shield patches away from the heaviest fire, on a capital with patches to choose between (U.SPC-T502).
		if (Input.ToggleRollPatches && PilotControlMode == Core::PilotControl::Helm && Core::Broadsides::Commands(unit) && Core::WeakPatches::HasPatches(unit->Combat))
			ShowToast(Core::WeakPatches::ToggleOrder(unit)
				? "Helm: roll our weak patches away from their fire (steer as you like; Z/X overrides it while held)"
				: "Helm: the roll is yours");
	}

	void UnitySpaceApp::UpdateHavenAssault(const cs::Ref<Core::Ship>& ship)
	{
		cs::Ref<Core::Settlement> haven;
		if (pilotOffsetKm().Length() <= Core::ScaleModel::NeighbourhoodKm((*Catalog())[pilotFrameBody]))
			for (const auto& s : Economy()->At(pilotFrameBody))
				if (World()->War->CanAssault(s, ship)) haven = s;
		if (haven == nullptr)
		{
			havenPrompted = nullptr;
			return;
		}
		bool marines = World()->War->HasMarines(ship);
		// Destroying it takes guns (or marines to set the charges), as AssaultHaven requires.
		bool canDestroy = marines || ship->Controller()->Combat->Armed();
		if (havenPrompted != haven)
		{
			havenPrompted = haven;
			ShowToast(!canDestroy ? cs::Cat(haven->Name, "'s defences are down, but this hull has neither guns nor marines to finish it")
				: cs::Cat(haven->Name, "'s defences are down:  ", (marines ? "1 board and take it, " : ""), "2 destroy it"));
		}
		int key = Input.NumberKey;
		if ((key == 1 && marines) || (key == 2 && canDestroy))
		{
			bool capture = key == 1;
			if (World()->War->AssaultHaven(haven, ship, capture, Clock->Jd))
				ShowToast(capture ? cs::Cat("Boarders take ", haven->Name, "; its raiders scatter") : cs::Cat(haven->Name, " is destroyed; its raiders scatter"));
			havenPrompted = nullptr;
		}
	}

	// ---------------------------------------------------------------- salvage and boarding of hulks

	std::tuple<Core::DVec3, Core::DVec3> UnitySpaceApp::HulkInPilotFrame(const cs::Func<Core::DVec3, double>& helioAt)
	{
		const double h = 1e-5;
		double jd = Clock->Jd;
		auto now = helioAt(jd) - Ephemeris()->HelioAt(pilotFrameBody, jd);
		auto next = helioAt(jd + h) - Ephemeris()->HelioAt(pilotFrameBody, jd + h);
		return { now, (next - now) / (h * Core::Astro::SecondsPerDay) };
	}

	Core::DVec3 UnitySpaceApp::DerelictHelio(const cs::Ref<Core::Ephemeris>& eph, const cs::Ref<Core::Ship>& s, double jd)
	{
		return eph->HelioAt(s->Orbit->Body, jd) + s->Orbit->OffsetAt(jd);
	}

	cs::String UnitySpaceApp::Num(double v, const char* format) { return cs::Fmt(v, format); }

	bool UnitySpaceApp::UpdateSalvage(const cs::Ref<Core::Ship>& ship, float dt)
	{
		if (Salvage != nullptr) return UpdateLatched(ship, dt);

		// The nearest hulk in reach of the readout.
		cs::Ref<Core::Ship> derelict;
		cs::Ref<Core::Wreck> wreck;
		Core::DVec3 rel, relVel;
		double best = SalvageApproachKm;
		for (const auto& s : Fleet()->Ships())
		{
			if (!s->Derelict || s->Destroyed || s == ship || s->Orbit == nullptr) continue;
			auto [off, vel] = HulkInPilotFrame([this, s](double jd) { return DerelictHelio(Ephemeris(), s, jd); });
			double d = (off - pilotOffsetKm()).Length();
			if (d >= best) continue;
			best = d;
			derelict = s;
			rel = pilotOffsetKm() - off;
			relVel = pilotVelKmS() - vel;
		}
		for (const auto& w : World()->War->Wrecks->Items)
		{
			auto [off, vel] = HulkInPilotFrame([this, w](double jd) { return World()->WreckHelio(w, jd); });
			double d = (off - pilotOffsetKm()).Length();
			if (d >= best) continue;
			best = d;
			wreck = w;
			derelict = nullptr;
			rel = pilotOffsetKm() - off;
			relVel = pilotVelKmS() - vel;
		}
		if (derelict == nullptr && wreck == nullptr)
		{
			SalvageApproach = cs::String();
			salvagePrompted = nullptr;
			return false;
		}

		cs::String name = derelict != nullptr ? cs::Cat("DERELICT ", derelict->Name) : cs::Cat("WRECK ", wreck->Name);
		double hulkLength = derelict != nullptr ? derelict->LengthKm() : wreck->LengthKm;
		double range = Core::SalvageSession::LatchRangeKm(hulkLength, ship->LengthKm());
		auto state = Core::SalvageSession::Check(rel, relVel, range);
		double closing = rel.LengthSquared() > 0 ? -Core::DVec3::Dot(relVel, rel.Normalized()) : 0;
		cs::String how = state == Core::LatchState::Ready ? cs::String("[1] LATCH ON")
			: state == Core::LatchState::TooFast ? cs::Cat("match velocity: ", Num(relVel.Length() * 1000, "0"), " m/s, under ", Num(Core::SalvageSession::MatchSpeedKmS * 1000, "0"), " to latch")
			: cs::Cat("come within ", Num(range, "0.0"), " km and match velocity to latch");
		SalvageApproach = cs::Cat(name, "   ", Num(best, "0.0"), " km   ", (closing >= 0 ? "closing " : "opening "), Num(cs::Math::Abs(closing) * 1000, "0"), " m/s   ", how);
		if (state != Core::LatchState::Ready)
		{
			salvagePrompted = nullptr;
			return false;
		}
		cs::Ref<void> hulk = derelict ? cs::Ref<void>(derelict) : cs::Ref<void>(wreck);
		if (salvagePrompted != hulk)
		{
			salvagePrompted = hulk;
			ShowToast(cs::Cat("Alongside ", (derelict != nullptr ? cs::Cat("the derelict ", derelict->Name) : cs::Cat("the wreck of ", wreck->Name)), ", velocity matched:  1 latch on"));
		}
		if (SalvageKey() != 1) return true;
		Salvage = derelict != nullptr ? Core::SalvageSession::Latch(ship, derelict) : Core::SalvageSession::Latch(ship, wreck);
		if (Salvage == nullptr) return true;
		salvageLatchOffsetKm = rel;
		salvageLogShown = 0;
		SalvageApproach = cs::String();
		ShowSalvageLog();
		return true;
	}

	int UnitySpaceApp::SalvageKey()
	{
		auto law = World()->Law;
		return law && law->Hail && law->Hail->Interdiction ? -1 : Input.NumberKey;
	}

	bool UnitySpaceApp::UpdateLatched(const cs::Ref<Core::Ship>& ship, float dt)
	{
		auto s = Salvage;
		bool hulkGone = s->Derelict != nullptr ? s->Derelict->Destroyed || !s->Derelict->Derelict : !World()->War->Wrecks->Items.Contains(s->Wreck);
		if (hulkGone) { EndSalvage("The hulk broke up under you: clamps released"); return false; }
		// Throttling or strafing away pulls the clamps off.
		if (Input.Move.sqrMagnitude() > 0.25f) { EndSalvage("Clamps released"); return false; }

		// Riding with the hulk: held at the offset the clamps took hold at, moving as it moves.
		auto [off, vel] = s->Derelict != nullptr
			? HulkInPilotFrame([this, s](double jd) { return DerelictHelio(Ephemeris(), s->Derelict, jd); })
			: HulkInPilotFrame([this, s](double jd) { return World()->WreckHelio(s->Wreck, jd); });
		SetpilotOffsetKm(off + salvageLatchOffsetKm);
		SetpilotVelKmS(vel);
		SetpilotCruise(Core::PilotCruiseState{});

		// The fight aboard runs on the sim clock (real time while piloting, faster under warp).
		s->Step(cs::Math::Min(10, dt * Clock->Warp().SimSecondsPerSecond * Clock->Speed));
		ShowSalvageLog();

		int key = SalvageKey();
		if (key == 4) { EndSalvage("Clamps released"); return true; }
		if (s->Hostile())
		{
			if (key == 1 && !s->Fighting() && !s->SendBoarders()) ShowToast("No marines aboard to send across");
			return true;
		}
		if (key == 1)
		{
			if (s->SalvageCargo() > 0) ShowCargo = true;
		}
		else if (key == 2)
		{
			auto part = s->BestPart();
			if (part == nullptr) ShowToast("Nothing left aboard worth stripping");
			else s->Strip(part);
		}
		else if (key == 3)
		{
			cs::String why;
			if (!s->CanClaim(why)) ShowToast(cs::Cat("Can't claim ", s->Name(), ": ", why));
			else if (s->Claim(Fleet(), Clock->Jd))
			{
				auto claimed = s->Derelict;
				EndSalvage(cs::String());
				ShowToast(cs::Cat("Claimed: the ", claimed->Name, " flies your colours (select it and press P to take its helm)"));
			}
		}
		return true;
	}

	void UnitySpaceApp::ShowSalvageLog()
	{
		if (Salvage == nullptr) return;
		const auto& log = Salvage->Log;
		if (salvageLogShown > log.Count()) salvageLogShown = log.Count();
		if (salvageLogShown == log.Count()) return;
		ShowToast(log[log.Count() - 1]);
		salvageLogShown = log.Count();
	}

	cs::String UnitySpaceApp::SalvageChoices()
	{
		auto s = Salvage;
		if (s == nullptr) return "";
		if (s->Hostile())
			return s->Fighting() ? cs::String("Marines fighting aboard   4 cast off")
				: cs::Cat((s->MarinesReady() >= 0.5 ? cs::Cat("1 send ", Num(s->MarinesReady(), "0"), " marines across   ") : cs::String("No marines to send   ")), "4 cast off");
		return "1 salvage cargo   2 strip a component   3 claim the hull   4 cast off";
	}

	void UnitySpaceApp::EndSalvage(const cs::String& toast)
	{
		auto s = Salvage;
		if (s == nullptr) return;
		Salvage = nullptr;
		salvagePrompted = nullptr;
		auto op = World()->FinishSalvage(s, Clock->Jd);
		if (op != nullptr) ShowToast(op->Outcome);
		else if (!toast.empty()) ShowToast(toast);
	}

	void UnitySpaceApp::UpdateSurrenderPrompts(const cs::Ref<Core::Battle>& battle, const cs::Ref<Core::BattleUnit>& unit)
	{
		int key = Input.NumberKey;
		if (unit->CargoDemandedBy != nullptr)
		{
			if (!demandPrompted)
			{
				demandPrompted = true;
				ShowToast(cs::Cat(unit->CargoDemandedBy->Name, " demand your cargo:  1 hand it over and go, 2 refuse and fight"));
			}
			if (key == 1 && battle->AnswerCargoDemand(unit, true)) ShowToast("Cargo handed over; the pirates let you go");
			else if (key == 2 && battle->AnswerCargoDemand(unit, false)) ShowToast("Demand refused: weapons free");
			return;
		}
		demandPrompted = false;

		auto hailing = battle->AwaitingTermsFrom(unit->Side);
		if (hailing == nullptr)
		{
			termsPrompted = nullptr;
			return;
		}
		if (termsPrompted != hailing)
		{
			termsPrompted = hailing;
			ShowToast(cs::Cat("The ", hailing->Name(), " strikes its colours:  1 demand its cargo, 2 take the ship, 3 let it go"));
		}
		cs::Nullable<Core::SurrenderTerms> terms = key == 1 ? cs::Nullable<Core::SurrenderTerms>(Core::SurrenderTerms::RansomCargo)
			: key == 2 ? cs::Nullable<Core::SurrenderTerms>(Core::SurrenderTerms::TakeShip)
			: key == 3 ? cs::Nullable<Core::SurrenderTerms>(Core::SurrenderTerms::LetGo)
			: cs::Nullable<Core::SurrenderTerms>();
		if (!terms.has_value()) return;
		if (battle->OfferSurrenderTerms(hailing, *terms))
			ShowToast(terms == Core::SurrenderTerms::RansomCargo ? cs::Cat("The ", hailing->Name(), " hands over its cargo and is let go")
				: terms == Core::SurrenderTerms::TakeShip ? cs::Cat("Boarders away to take the ", hailing->Name())
				: cs::Cat("The ", hailing->Name(), " is let go"));
		else ShowToast(cs::Cat("Nobody on this side can board the ", hailing->Name()));
	}

	void UnitySpaceApp::ToggleLockTarget()
	{
		if (!pilotLockTarget.IsNone() || pilotLockUnit != nullptr)
		{
			pilotLockTarget = Target::None();
			pilotLockUnit = nullptr;
			return;
		}
		// The piloted ship is itself the selection while piloting, so it can't stand in the way of the hovered one.
		auto Other = [this](const Target& c) { return c.IsShip() && c.Ship != Piloted.Ship; };
		auto t = Other(Selected) ? Selected : Other(Hovered) ? Hovered : Target::None();
		if (t.IsShip())
		{
			pilotLockTarget = t;
			return;
		}
		// In a fight, the hostile nearest the crosshair: a fighter as readily as a ship (U.SPC-T271).
		auto u = UnitNearestCrosshair();
		if (u == nullptr) return;
		if (u->Ship != nullptr) pilotLockTarget = Target::OfShip(u->Ship);
		else pilotLockUnit = u;
	}

	Core::DVec3 UnitySpaceApp::PilotLookDirection()
	{
		if (PilotControlMode != Core::PilotControl::Helm) return pilotAim;
		auto v = (ToRotation(pilot().Forward, pilot().Up) * Quaternion::Euler(helmLookPitch, helmLookYaw, 0)) * Vector3::forward();
		return Core::DVec3(v.x, v.y, v.z).SwapYZ().Normalized();
	}

	cs::Ref<Core::BattleUnit> UnitySpaceApp::UnitNearestCrosshair()
	{
		auto battle = Piloted.Ship ? Piloted.Ship->Battle : nullptr;
		auto me = battle ? battle->UnitOf(Piloted.Ship) : nullptr;
		if (me == nullptr) return nullptr;
		cs::Ref<Core::BattleUnit> best;
		double bestCos = cs::Math::Cos(LockPickConeDeg * Mathf::Deg2Rad);
		for (const auto& o : battle->Units)
		{
			if (o == me || !o->Present() || !battle->Hostile(me, o)) continue;
			auto to = o->Pos - me->Pos;
			if (to.LengthSquared() < 1e-12) continue;
			double cos = Core::DVec3::Dot(pilotAim, to.Normalized());
			if (cos > bestCos)
			{
				bestCos = cos;
				best = o;
			}
		}
		return best;
	}

	cs::Ref<Core::BattleUnit> UnitySpaceApp::PilotUnit()
	{
		return IsPiloting() && Piloted.Ship && Piloted.Ship->Battle != nullptr ? Piloted.Ship->Battle->UnitOf(Piloted.Ship) : nullptr;
	}

	cs::Ref<Core::BattleUnit> UnitySpaceApp::PilotLockUnit()
	{
		auto unit = PilotUnit();
		return unit ? unit->PlayerLockTarget : nullptr;
	}

	double UnitySpaceApp::OverviewSpanKm() const { return ActiveSystem->OverviewSpanKm; }

	double UnitySpaceApp::OverviewDistance() { return Scale()->LocalDistance(Catalog()->Root(), OverviewSpanKm()) * 1.9; }

	void UnitySpaceApp::FocusSystemOverview()
	{
		auto root = Target::OfBody(0);
		Select(Catalog()->Root()->IsVisible() ? root : Target::None());
		SpaceCamera->FocusOn(root, OverviewDistance() / cs::Math::Max(bodyRadius[0], 1e-9), OverviewPitch);
	}

	Vector3 UnitySpaceApp::SystemPlaneNormal() { return PlaneNormal(ActiveSystem); }

	Vector3 UnitySpaceApp::PlaneNormal(const cs::Ref<Core::StarSystem>& system)
	{
		auto catalog = system->Catalog;
		auto eph = system->Ephemeris;
		if (catalog->Root()->Name == "Sun" || catalog->Count() < 2) return Vector3::up();
		Core::DVec3 n;
		if (!catalog->Root()->IsStar()) n = Core::DVec3::Cross(eph->Relative(1), eph->RelativeVelocityKmS(1));
		else
		{
			n = Core::DVec3::Zero;
			for (int i = 0; i < catalog->Count(); i++)
				if ((*catalog)[i]->ParentIndex == catalog->Root()->Index)
					n += Core::DVec3::Cross(eph->Relative(i), eph->RelativeVelocityKmS(i)) * cs::Math::Max((*catalog)[i]->Gm, 1e-9);
		}
		if (!(n.LengthSquared() > 0)) return Vector3::up();
		n = n.Normalized().SwapYZ();
		return Vector3(static_cast<float>(n.X), static_cast<float>(n.Y), static_cast<float>(n.Z));
	}

	double UnitySpaceApp::OortCloudLy(const cs::Ref<Core::StarSystem>& system)
	{
		double gm = 0;
		for (const auto& b : system->Catalog->Bodies())
			if (b->IsStar()) gm += b->Gm;
		double mass = gm > 0 ? gm / Core::StarSystemBuilder::SunGm : 1;
		return SunOortCloudAu * cs::Math::Pow(mass, 1.0 / 3.0) / AuPerLy;
	}

	cs::String UnitySpaceApp::SystemPlaneName()
	{
		return IsSol() ? cs::String("the ecliptic") : !Catalog()->Root()->IsStar() ? cs::String("the A–B orbital plane") : cs::Cat(Catalog()->Root()->Name, "'s planetary plane");
	}

	bool UnitySpaceApp::AutoReorient() const { return AutoReorientSetting()->Value; }

	const cs::Ref<Core::ToggleSetting>& UnitySpaceApp::AutoReorientSetting()
	{
		static const cs::Ref<Core::ToggleSetting> value = cs::New<Core::ToggleSetting>(Core::GameSettings::AutoReorient, Core::SettingGroups::Controls,
			"Auto-reorient view", true, "Keep the active system's plane level by itself when the view moves to a new system (R in play).",
			"camera level plane horizon");
		return value;
	}

	void UnitySpaceApp::ToggleAutoReorient()
	{
		AutoReorientSetting()->Set(!AutoReorient()); // remembered in the profile by the settings registry
		ShowToast(AutoReorient() ? cs::Cat("AUTO-REORIENT ON: keeping ", SystemPlaneName(), " level") : cs::String("AUTO-REORIENT OFF: the view stays as you leave it"));
	}

	void UnitySpaceApp::Reorient()
	{
		// The chase camera follows the hull the mouse is flying: there is no view of its own to level,
		// and the levelling ease would compound on the chase's own easing every frame.
		if (Travel->Active || IsPiloting()) return;
		SpaceCamera->Level(SystemPlaneNormal());
	}

	void UnitySpaceApp::KeepReoriented()
	{
		if (!AutoReorient() || IsPiloting() || SpaceCamera->Levelling()) return;
		auto normal = SystemPlaneNormal();
		if (Vector3::Angle(SpaceCamera->UpFrame * Vector3::up(), normal) > 0.5f) SpaceCamera->Level(normal);
	}

	void UnitySpaceApp::TogglePause()
	{
		Clock->ToggleEasedPause();
		ShowToast(Clock->PauseHeld() ? cs::String("PAUSING: every system easing to a stop  (Space to resume)") : cs::Cat("RESUMING: time picking back up to ", Clock->Warp().Label));
	}

	void UnitySpaceApp::ToggleScale()
	{
		scaleFrom = Scale()->Blend();
		ScaleGoal = ScaleGoal > 0.5 ? 0 : 1;
		scaleT = 0;
		ShowToast(ScaleGoal > 0.5
			? "GAMEPLAY SCALE: sizes and distances compressed to be navigable"
			: "REALISTIC SCALE: true proportions, 1 unit = 1,000 km");
	}

	void UnitySpaceApp::AnimateScale(float dt)
	{
		if (scaleT >= 1.0f) return;
		scaleT = Mathf::Min(1.0f, scaleT + dt / ScaleSeconds);
		float e = scaleT * scaleT * scaleT * (scaleT * (scaleT * 6 - 15) + 10);
		Scale()->SetBlend(scaleFrom + (ScaleGoal - scaleFrom) * e);
	}

	void UnitySpaceApp::ShowToast(const cs::String& message)
	{
		if (coopMuteToasts > 0) return; // US-T44 (US-T43): another player's action, replayed here
		Toast = message;
		ToastAge = 0;
	}

	void UnitySpaceApp::ResetToNow()
	{
		// A reset can rebuild the galaxy out from under the piloted ship (the HUD's Now button lands here too).
		if (CoopAtControls()) { ShowToast("Release the helm or seat (P) before resetting the time"); return; } // US-T44 (US-T43): was AtControls() (in co-op: anyone's)
		// Nor under the hull's own FTL drive: the crossing holds the old galaxy's systems (the Now button reaches here too).
		if (InFtlCruise()) { ShowToast("Not under FTL: wait until the drive drops out before resetting the time"); return; }
		double now = CoopNow.has_value() ? *CoopNow : Core::Astro::JulianDate(cs::DateTime::UtcNow()); // US-T44 (US-T43): in co-op the asker's now
		if (CoopSends("reset", Coop::Wire::D(now))) return; // US-T44 (US-T43): into the frames
		bool backwards = now < Clock->Jd;
		// Rebuilding the systems mid-trip would leave the trip pointing at the old ones: land first.
		if (backwards && Travel->Active) Travel->Finish();
		Clock->SetJd(now);
		Clock->SetWarp(Core::SimClock::DefaultWarpIndex);
		if (backwards)
		{
			// Schedules only run forward; rebuild every system at the new date.
			int index = Galaxy->Systems.IndexOf(ActiveSystem);
			auto cameraAt = SpaceCamera->ScenePosition;
			auto cameraRotation = SpaceCamera->Rotation;
			Galaxy = Core::Galaxy::CreateDefault(now);
			Rendering::Materials::DestroyOwned(Interstellar->Root->transform);
			UnityCompat::Destroy(Interstellar->Root);
			Interstellar = cs::New<Views::InterstellarView>(Galaxy, worldRoot);
			SubscribeWorlds();
			Activate(Galaxy->Systems[cs::Math::Max(0, index)]);
			SpaceCamera->SetPose(cameraAt, cameraRotation);
			GalaxyRebuilt.Invoke();
		}
		ShowToast("Time reset to now");
	}

	void UnitySpaceApp::RefreshCharts()
	{
		if (Galaxy->ChartVersion == chartsShown || Travel->Active) return;
		chartsShown = Galaxy->ChartVersion;
		Rendering::Materials::DestroyOwned(Interstellar->Root->transform);
		UnityCompat::Destroy(Interstellar->Root);
		Interstellar = cs::New<Views::InterstellarView>(Galaxy, worldRoot);
	}

	// ---------------------------------------------------------------- ships

	void UnitySpaceApp::UpdateShips(float dt)
	{
		const auto& ships = Fleet()->Ships();
		double jd = Clock->Jd;
		float response = 1.0f - Mathf::Exp(-dt * 3.0f);

		for (int pass = 0; pass < 2; pass++)
		{
			for (int i = 0; i < ships.Count(); i++)
			{
				auto ship = ships[i];
				if (ship->IsEscort() != (pass == 1)) continue;
				shipLength[i] = Scale()->ShipLength(ship->LengthKm());

				if (ship == World()->PilotedShip && ship->Phase == Core::ShipPhase::Lane)
				{
					// Riding a lane at the helm: drawn on the lane as every rider is, nose along it.
					auto ride = Fleet()->StateOf(ship, jd);
					Core::DVec3 along;
					auto onLane = Scale()->LaneScene(Ephemeris(), bodyScene, Fleet()->Lanes, ship, ride.Progress, along);
					auto side = SideScene(Fleet()->LanePoint(ship, jd, ride.Progress), ship->LaneHoldSide);
					shipScene[i] = onLane + side * Core::Fleet::LaneRejoin(ship, jd);
					auto af = Vector3(static_cast<float>(along.X), static_cast<float>(along.Y), static_cast<float>(along.Z));
					auto roof = pilot().Up.SwapYZ();
					auto ru = Vector3(static_cast<float>(roof.X), static_cast<float>(roof.Y), static_cast<float>(roof.Z));
					if (af.sqrMagnitude() < 1e-12f) af = Vector3::forward();
					if (ru.sqrMagnitude() < 1e-12f || Mathf::Abs(Vector3::Dot(af.normalized(), ru.normalized())) > 0.98f) ru = Mathf::Abs(Vector3::Dot(af.normalized(), Vector3::up())) > 0.98f ? Vector3::right() : Vector3::up();
					shipRotation[i] = Quaternion::LookRotation(af, ru);
					shipRotationInitialised[i] = true;
					shipStates[i] = ride;
					continue;
				}
				if (ship == World()->PilotedShip)
				{
					shipScene[i] = bodyScene[pilotFrameBody] + Scale()->LocalOffset((*Catalog())[pilotFrameBody], pilotOffsetKm());
					// Nose and roof both: the hull rolls for real (U.SPC-T59).
					auto pilotFacing = pilotForward().SwapYZ();
					auto pilotRoof = pilot().Up.SwapYZ();
					auto pf = Vector3(static_cast<float>(pilotFacing.X), static_cast<float>(pilotFacing.Y), static_cast<float>(pilotFacing.Z));
					auto pup = Vector3(static_cast<float>(pilotRoof.X), static_cast<float>(pilotRoof.Y), static_cast<float>(pilotRoof.Z));
					if (pf.sqrMagnitude() < 1e-12f) pf = Vector3::forward();
					if (pup.sqrMagnitude() < 1e-12f) pup = Mathf::Abs(Vector3::Dot(pf.normalized(), Vector3::up())) > 0.98f ? Vector3::right() : Vector3::up();
					shipRotation[i] = Quaternion::LookRotation(pf, pup);
					shipRotationInitialised[i] = true;
					// Its own state, not whatever the schedule last drew (an entry glow, a drive flame).
					shipStates[i] = Core::ShipState(Ephemeris()->Helio(pilotFrameBody) + pilotOffsetKm(), pilotForward(), pilotVelKmS().Length(), false, 0);
					continue;
				}
				// Running the player down (U.SPC-T273): where the chase has it, nose on its way, drive lit.
				auto chase = World()->Pursuit != nullptr && World()->Pursuit->Active() ? World()->Pursuit->Of(ship) : nullptr;
				if (chase != nullptr)
				{
					int frame = World()->Pursuit->Body;
					auto off = Core::Pursuit::OffsetOf(chase, ship);
					auto heading = chase->Vel.LengthSquared() > 1e-12 ? chase->Vel.Normalized() : (pilotOffsetKm() - off).Normalized();
					shipScene[i] = bodyScene[frame] + Scale()->LocalOffset((*Catalog())[frame], off);
					shipRotation[i] = ToRotation(heading, Core::DVec3::UnitZ);
					shipRotationInitialised[i] = true;
					shipStates[i] = Core::ShipState(Ephemeris()->Helio(frame) + off, heading, chase->Vel.Length(), true, 0);
					continue;
				}

				auto st = Fleet()->StateOf(ship, jd);
				shipStates[i] = st;

				if (ship->IsEscort())
				{
					int leader = ship->Leader->Id;
					// Human pilots: each escort wanders slowly about its slot instead of sitting welded to it.
					float t = Time::unscaledTime * 0.25f + ship->Id * 1.7f;
					auto slot = ship->FormationSlot + Core::DVec3(Mathf::Sin(t * 1.3f) * 0.12f, Mathf::Sin(t * 0.9f + 1.1f) * 0.08f, Mathf::Sin(t * 0.7f + 2.3f) * 0.15f);
					auto offset = shipRotation[leader] * Vector3(static_cast<float>(slot.X), static_cast<float>(slot.Y), static_cast<float>(slot.Z)) * static_cast<float>(shipLength[leader]);
					shipScene[i] = shipScene[leader] + Core::DVec3(offset.x, offset.y, offset.z);
					shipRotation[i] = shipRotation[leader];
					shipRotationInitialised[i] = true;
					continue;
				}

				Core::DVec3 facing;
				if (ship->Phase == Core::ShipPhase::Parked)
				{
					auto orbit = ship->Orbit;
					shipScene[i] = bodyScene[orbit->Body] + Scale()->LocalOffset((*Catalog())[orbit->Body], orbit->OffsetAt(jd));
					facing = st.Facing.SwapYZ();
				}
				else if (ship->Phase == Core::ShipPhase::Queued || ship->Planetside())
				{
					// At a docking ring: in the ringed world's own (turning) neighbourhood.
					int body = ship->Ring->Body;
					shipScene[i] = bodyScene[body] + Scale()->LocalOffset((*Catalog())[body], st.HelioKm - Ephemeris()->Helio(body));
					facing = st.Facing.SwapYZ();
				}
				else if (ship->Phase == Core::ShipPhase::Lane)
				{
					Core::DVec3 travel;
					auto onLane = Scale()->LaneScene(Ephemeris(), bodyScene, Fleet()->Lanes, ship, st.Progress, travel);
					auto side = SideScene(Fleet()->LanePoint(ship, jd, st.Progress), ship->LaneHoldSide);
					shipScene[i] = onLane + side * Core::Fleet::LaneRejoin(ship, jd);
					facing = travel;
				}
				else if (ship->Phase == Core::ShipPhase::Lurk || ship->Phase == Core::ShipPhase::Adrift)
				{
					Core::DVec3 along;
					shipScene[i] = LaneHoldScene(ship, along);
					facing = along;
				}
				else
				{
					Core::DVec3 travel;
					shipScene[i] = TransitScene(ship, st.Progress, travel);
					facing = travel;
				}

				// Stepping aside to keep clear of another ship: the sidestep (true km) in scene units here.
				auto evasion = Fleet()->EvasionOf(ship);
				if (evasion.LengthSquared() > 0) shipScene[i] += evasion.SwapYZ() * KmToSceneNear(ship, st.HelioKm);

				auto f = Vector3(static_cast<float>(facing.X), static_cast<float>(facing.Y), static_cast<float>(facing.Z));
				if (f.sqrMagnitude() < 1e-12f) f = Vector3::forward();
				auto up = Mathf::Abs(Vector3::Dot(f.normalized(), Vector3::up())) > 0.98f ? Vector3::right() : Vector3::up();
				auto goal = Quaternion::LookRotation(f, up);
				shipRotation[i] = shipRotationInitialised[i] ? Quaternion::Slerp(shipRotation[i], goal, response) : goal;
				shipRotationInitialised[i] = true;
			}
		}
		PlaceInBattles(dt);
	}

	double UnitySpaceApp::KmToSceneNear(const cs::Ref<Core::Ship>& ship, Core::DVec3 helio)
	{
		auto c = ship->Controller();
		int b = c->Ring != nullptr && (c->Phase == Core::ShipPhase::Queued || c->Planetside()) ? c->Ring->Body : (c->Orbit ? c->Orbit->Body : 0);
		double d = (helio - Ephemeris()->Helio(b)).Length();
		auto body = (*Catalog())[b];
		if (d > Core::ScaleModel::NeighbourhoodKm(body))
		{
			body = Catalog()->Root();
			d = (helio - Ephemeris()->Helio(0)).Length();
		}
		d = cs::Math::Max(d, cs::Math::Max(body->RadiusKm, 1));
		return Scale()->LocalDistance(body, d) / d;
	}

	void UnitySpaceApp::PlaceInBattles(float dt)
	{
		const auto& ships = Fleet()->Ships();
		if (lastBattleScene.Count() != ships.Count())
		{
			ResizeKeep(lastBattleScene, ships.Count());
			ResizeKeep(lastDrawnValid, ships.Count());
		}
		for (int i = 0; i < ships.Count(); i++)
		{
			auto ship = ships[i];
			if (ship->Destroyed) continue;
			auto battle = ship->Battle;
			int body = battle != nullptr ? battle->Body : ship->Controller()->Orbit->Body;
			if (battle != shipLastBattle[i])
			{
				auto current = DrawnScene(i);
				shipBlendFrom[i] = current - bodyScene[body];
				shipBlendT[i] = 0;
				shipLastBattle[i] = battle;
			}
			if (battle != nullptr)
			{
				auto u = battle->UnitOf(ship);
				if (u != nullptr)
				{
					shipScene[i] = BattleScene(battle, u->Pos);
					auto f = u->Forward.SwapYZ();
					auto fv = Vector3(static_cast<float>(f.X), static_cast<float>(f.Y), static_cast<float>(f.Z));
					// The flown hull keeps the roll its pilot gave it (set with its pose in UpdateShips); the
					// battle's units roll as their captains roll them, to keep weak patches out of the fire (U.SPC-T502).
					if (ship == World()->PilotedShip) shipRotation[i] = ToRotation(pilotForward(), pilot().Up);
					else if (fv.sqrMagnitude() > 1e-12f)
					{
						Core::DVec3 unitRight, unitUp;
						u->Basis(unitRight, unitUp);
						shipRotation[i] = ToRotation(u->Forward, unitUp);
					}
				}
			}
			if (shipBlendT[i] < 1)
			{
				shipBlendT[i] = Mathf::Min(1.0f, shipBlendT[i] + dt / 2.0f);
				float e = shipBlendT[i] * shipBlendT[i] * (3 - 2 * shipBlendT[i]);
				shipScene[i] = Core::DVec3::Lerp(bodyScene[body] + shipBlendFrom[i], shipScene[i], e);
			}
			lastBattleScene[i] = shipScene[i];
			lastDrawnValid[i] = true;
		}
	}

	Core::DVec3 UnitySpaceApp::DrawnScene(int i)
	{
		return lastDrawnValid.Count() > i && lastDrawnValid[i] ? lastBattleScene[i] : shipScene[i];
	}

	Core::DVec3 UnitySpaceApp::TransitScene(const cs::Ref<Core::Ship>& ship, double progress, Core::DVec3& travelDirection)
	{
		return Scale()->TransitScene(Ephemeris(), bodyScene, ship, progress, travelDirection);
	}

	void UnitySpaceApp::EnsureShipSynced(const cs::Ref<Core::Ship>& ship)
	{
		if (ship->Id >= shipScene.Count() && SyncShipViews()) UpdateShips(0.0f);
	}

	Core::DVec3 UnitySpaceApp::ShipScene(const cs::Ref<Core::Ship>& ship) { EnsureShipSynced(ship); return shipScene[ship->Id]; }
	double UnitySpaceApp::ShipDisplayLength(const cs::Ref<Core::Ship>& ship) { EnsureShipSynced(ship); return shipLength[ship->Id] * Core::ShipLod::ExtentFactor(ship); }
	Core::ShipState UnitySpaceApp::ShipStateOf(const cs::Ref<Core::Ship>& ship) { EnsureShipSynced(ship); return shipStates[ship->Id]; }

	// ---------------------------------------------------------------- ICameraWorld

	Core::DVec3 UnitySpaceApp::BodyScene(int body) { return bodyScene[body]; }
	double UnitySpaceApp::BodyDisplayRadius(int body) { return bodyRadius[body]; }

	Core::DVec3 UnitySpaceApp::TargetScene(const Target& target)
	{
		// A ship spawned this same frame (the scripted tour's skirmishes, the Spawn & Pilot grid,
		// a jump-gate arrival) may not have been sized into shipScene yet -- the per-ship arrays
		// only grow once a frame, in Update, but this is also reached from a coroutine step or an
		// OnGUI click that can land before or after that. A resize alone leaves the newcomer's
		// entry zeroed, so also force one fresh UpdateShips to actually place it before reading it.
		if (target.IsShip() && SyncShipViews()) UpdateShips(0.0f);
		if (target.IsBody()) return bodyScene[target.Body];
		if (target.IsShip()) return shipScene[target.Ship->Id];
		Core::DVec3 s;
		if (target.IsStructure() && Structures->Scenes.TryGetValue(target.Structure, s)) return s;
		Core::DVec3 r;
		if (target.IsRing() && Structures->RingScenes.TryGetValue(target.Ring, r)) return r;
		if (target.IsUnit()) return BattleScene(target.Battle, target.Unit->Pos);
		return Core::DVec3::Zero;
	}

	Core::DVec3 UnitySpaceApp::FocusScene(const Target& target)
	{
		auto at = TargetScene(target);
		if (!target.IsShip()) return at;
		int i = target.Ship->Id;
		if (i >= shipViews.Count() || !shipViews[i]) return at;
		// The hull's middle, from its root's frame (one unit long) to scene units: turned and sized as Apply places it.
		auto offset = shipRotation[i] * (shipViews[i]->HullCentre() * static_cast<float>(shipLength[i]));
		return at + Core::DVec3(offset.x, offset.y, offset.z);
	}

	double UnitySpaceApp::TargetRadius(const Target& target)
	{
		if (target.IsBody()) return bodyRadius[target.Body];
		if (target.IsShip()) return shipLength[target.Ship->Id] * 0.5;
		double s = 0;
		if (target.IsStructure() && Structures->Sizes.TryGetValue(target.Structure, s)) return s * 0.5;
		double r = 0;
		if (target.IsRing() && Structures->Sizes.TryGetValue(target.Ring, r)) return r * 0.5;
		if (target.IsUnit()) return Scale()->ShipLength(target.Unit->LengthKm) * 0.5;
		return 1;
	}

	Quaternion UnitySpaceApp::TargetRotation(const Target& target)
	{
		if (target.IsShip()) return shipRotation[target.Ship->Id];
		if (target.IsUnit())
		{
			auto u = target.Unit;
			Core::DVec3 right, up;
			u->Basis(right, up);
			return Quaternion::LookRotation(ToVector(u->Forward.SwapYZ()), ToVector(up.SwapYZ()));
		}
		return Quaternion::identity();
	}

	cs::List<cs::Ref<Renderer>> UnitySpaceApp::ModelRenderersOf(const Target& target)
	{
		if (target.IsBody() && target.Body >= 0 && target.Body < bodyViews.Count() && bodyViews[target.Body])
			return GetComponentsInChildren<Renderer>(bodyViews[target.Body]->Root->transform, false);
		if (target.IsShip() && target.Ship && target.Ship->Id < shipViews.Count() && shipViews[target.Ship->Id]) return shipViews[target.Ship->Id]->HullRenderers();
		return cs::List<cs::Ref<Renderer>>();
	}

	Vector3 UnitySpaceApp::ToVector(Core::DVec3 v) { return Vector3(static_cast<float>(v.X), static_cast<float>(v.Y), static_cast<float>(v.Z)); }

	double UnitySpaceApp::NearestSurface(Core::DVec3 p)
	{
		double best = cs::Double::MaxValue;
		for (int i = 0; i < bodyScene.Count(); i++)
			best = cs::Math::Min(best, (p - bodyScene[i]).Length() - bodyRadius[i]);
		const auto& ships = Fleet()->Ships();
		for (int i = 0; i < ships.Count(); i++)
		{
			if (ships[i]->Destroyed || ships[i]->Controller()->Phase == Core::ShipPhase::Landed) continue;
			best = cs::Math::Min(best, (p - shipScene[i]).Length() - shipLength[i] * 0.5);
		}
		for (auto kv : Structures->Scenes)
			best = cs::Math::Min(best, (p - kv.second).Length() - Structures->Sizes.At(kv.first) * 0.5);
		for (auto kv : Structures->RingScenes)
			best = cs::Math::Min(best, (p - kv.second).Length() - Structures->Sizes.At(kv.first) * 0.5);
		return cs::Math::Max(best, 1e-7);
	}

	// ---------------------------------------------------------------- views

	void UnitySpaceApp::UpdateClipPlanes(float dt)
	{
		// Going through a gate, the ring passes close by, nearer than anything else.
		double nearest = cs::Math::Min(NearestSurface(SpaceCamera->ScenePosition), Travel->GateClearance());
		UnityCamera->nearClipPlane = static_cast<float>(cs::Math::Max(1e-6, cs::Math::Min(nearest * 0.25, 20.0)));
		UnityCamera->farClipPlane = 5e8f;
		UnityCamera->transform->SetPositionAndRotation(Vector3::zero(), SpaceCamera->Rotation);

		float targetFov = RidingLane() ? BaseFov + (LaneFov - BaseFov) * LaneRideSpeed
			: IsPiloting() && pilotCruise().Active() ? CruiseFov : IsPiloting() && pilotBoosting ? BoostFov : BaseFov;
		cameraFov = Mathf::Lerp(cameraFov, targetFov, 1.0f - Mathf::Exp(-dt * FovResponseHz));
		// A ring gate kicks the view wide for a moment and flashes as the hull goes through (U.SPC-T41).
		fovKick *= Mathf::Exp(-dt * 3.5f);
		GateFlash = Mathf::Max(0.0f, GateFlash - dt / GateFlashS);
		LaneExitFade = Mathf::Max(0.0f, LaneExitFade - dt / LaneExitFadeS);
		UnityCamera->fieldOfView = Mathf::Clamp(cameraFov + fovKick + Travel->FovKick() - HelmFoldDeg * HelmGateFold(), 10.0f, 150.0f);
	}

	Vector3 UnitySpaceApp::Relative(Core::DVec3 scene)
	{
		auto d = scene - SpaceCamera->ScenePosition;
		return Vector3(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z));
	}

	void UnitySpaceApp::UpdateLights()
	{
		int first = -1, second = -1;
		double best = 0, next = 0;
		for (int i = 0; i < Catalog()->Count(); i++)
		{
			auto b = (*Catalog())[i];
			if (!b->IsStar()) continue;
			double d = cs::Math::Max((SpaceCamera->ScenePosition - bodyScene[i]).Length(), bodyRadius[i]);
			double flux = b->LuminositySolar / (d * d);
			if (flux > best)
			{
				second = first;
				next = best;
				first = i;
				best = flux;
			}
			else if (flux > next)
			{
				second = i;
				next = flux;
			}
		}
		if (first < 0) first = 0;
		Rendering::StarLighting light;
		light.SunPosition = ToVector4(Relative(bodyScene[first]));
		light.SunColor = LightColor((*Catalog())[first], 1);
		if (second >= 0)
		{
			// The square root keeps a companion's light a visible fill; the true ratio would hide it.
			float strength = Mathf::Clamp01(static_cast<float>(cs::Math::Sqrt(next / best))) * 0.9f;
			light.Sun2Position = ToVector4(Relative(bodyScene[second]));
			light.Sun2Color = LightColor((*Catalog())[second], strength);
		}
		else
		{
			light.Sun2Position = ToVector4(Relative(bodyScene[first]));
			light.Sun2Color = Vector4::zero();
		}
		Rendering::RenderBackend::Active()->SetLighting(light);
		UpdateAtmosphere();
	}

	void UnitySpaceApp::UpdateAtmosphere()
	{
		auto backend = Rendering::RenderBackend::Active();
		if (!backend->Profile()->Offers(Core::PipelineFeatures::PhysicalSky)) return;
		auto ridden = IsPiloting() ? Piloted.Ship : SpaceCamera->FocusTarget.IsShip() ? SpaceCamera->FocusTarget.Ship : nullptr;
		int body = Core::PlanetAtmospheres::EntryBody(ridden);
		if (body < 0 || body >= Catalog()->Count() || !(*Catalog())[body]->IsVisible())
		{
			ClearAtmosphere();
			return;
		}
		auto b = (*Catalog())[body];
		double distance = (SpaceCamera->ScenePosition - bodyScene[body]).Length();
		if (cs::Double::IsPositiveInfinity(Core::PlanetAtmospheres::Closeness(distance, bodyRadius[body], b->AtmosphereStrength, b->IsStar())))
		{
			// An airless world, or the camera pulled far back from the descent: the rims carry it.
			ClearAtmosphere();
			return;
		}
		auto shell = Core::PlanetAtmospheres::Describe(body, bodyRadius[body], distance, b->AtmosphereColor.R, b->AtmosphereColor.G,
			b->AtmosphereColor.B, b->AtmosphereStrength);
		backend->SetAtmosphere(UnityCamera, shell, Relative(bodyScene[body]));
		atmosphereSet = true;
	}

	void UnitySpaceApp::ClearAtmosphere()
	{
		if (!atmosphereSet) return;
		atmosphereSet = false;
		Rendering::RenderBackend::Active()->SetAtmosphere(UnityCamera, Core::AtmosphereShell::None(), Vector3::zero());
	}

	const Core::Rgb& UnitySpaceApp::SunLight()
	{
		static const Core::Rgb value = Core::AlphaCentauri::StarColor(5772);
		return value;
	}

	Vector4 UnitySpaceApp::LightColor(const cs::Ref<Core::BodyDefinition>& star, float strength)
	{
		auto c = Core::AlphaCentauri::StarColor(star->TeffK);
		auto v = Vector3(c.R / Mathf::Max(SunLight().R, 1e-3f), c.G / Mathf::Max(SunLight().G, 1e-3f), c.B / Mathf::Max(SunLight().B, 1e-3f));
		float max = Mathf::Max(v.x, Mathf::Max(v.y, v.z));
		v /= Mathf::Max(max, 1e-3f);
		return Vector4(v.x * strength, v.y * strength, v.z * strength, strength);
	}

	void UnitySpaceApp::UpdateViews(float dt)
	{
		if (!SystemVisible)
		{
			ClearAtmosphere();
			return;
		}
		UpdateLights();
		auto forward = SpaceCamera->Rotation * Vector3::forward();
		double pixelsPerUnit = PixelsPerUnitAtUnitDistance();
		double warp = Clock->Paused() ? 0 : Clock->Warp().SimSecondsPerSecond * Clock->Speed;

		for (int i = 0; i < Catalog()->Count(); i++)
		{
			// Body-fixed frame: IAU prime meridians, or 0° toward the parent for synchronous moons.
			auto rel = Ephemeris()->Relative(i);
			auto orbitNormal = Core::DVec3::Cross(rel, Ephemeris()->RelativeVelocityKmS(i));
			Core::DVec3 north, meridian;
			Core::BodyOrientation::Frame((*Catalog())[i], Clock->Jd, -rel, orbitNormal, north, meridian);
			auto target = Target::OfBody(i);
			bodyViews[i]->Apply(Relative(bodyScene[i]), static_cast<float>(bodyRadius[i]), north, meridian,
				target == Selected || target == Hovered, forward);
			bodyViews[i]->UpdateWeather(Clock->Jd, warp);
		}
		if (IsSol() && AsteroidBelt != nullptr) AsteroidBelt->Render(Clock->Jd, Scale(), Relative(bodyScene[0]));

		int glowIndex = 0;
		for (int i = 0; i < shipViews.Count(); i++)
		{
			auto ship = Fleet()->Ships()[i];
			// Not streamed in (U.SPC-T414): out of sight, or drawn as its icon until its model is built.
			if (shipViews[i] == nullptr) continue;
			if (ship->Destroyed)
			{
				// A jump gate retires the origin hull the same way a kill does (so its id stays put
				// for every ship after it -- see GateTransit) but it flew off, not blew up.
				if (ship->GateDeparted) { shipViews[i]->SetVisible(false); continue; }
				DrawWreck(i, ship);
				continue;
			}
			if (viewFaction.Count() != shipViews.Count()) ResizeKeep(viewFaction, shipViews.Count());
			if (viewFaction[i] == nullptr) viewFaction[i] = ship->Faction;
			if (viewFaction[i] != ship->Faction || shipViews[i]->BuiltFor != ship->Combat)
			{
				// Taken as a prize: repaint in the captor's livery (freeing the old view's own meshes and materials).
				// Refitted at a yard: rebuild its hardpoints from the new loadout.
				Rendering::Materials::DestroyOwned(shipViews[i]->Root->transform);
				UnityCompat::Destroy(shipViews[i]->Root);
				shipViews[i] = cs::New<Views::ShipView>(ship, systemRoot);
				viewFaction[i] = ship->Faction;
			}
			auto rel = Relative(shipScene[i]);
			auto controller = ship->Controller();
			if (controller->Phase == Core::ShipPhase::Landed)
			{
				shipViews[i]->SetVisible(false);
				continue;
			}
			float heat = shipStates[i].Heat;
			if (heat > 0.01f) PlaceEntryGlow(glowIndex++, i, rel, heat, pixelsPerUnit);
			// An ore tug with its load on is drawn by the length of both (U.SPC-T498).
			double px = Core::ShipLod::ApparentPx(shipLength[i] * Core::ShipLod::ExtentFactor(ship), rel.magnitude(), pixelsPerUnit);
			// Too small to read: hide the model, the HUD draws its named faction icon instead (U.SPC-T376), folded
			// into one mark with its neighbours where they crowd; too small for its hardpoints, the hull alone (U.SPC-T415).
			auto tier = Core::RenderLod::Tier(Core::RenderLod::Ship(), px);
			bool visible = Core::RenderLod::DrawsModel(tier);
			shipViews[i]->SetVisible(visible);
			if (!visible) continue;
			shipViews[i]->SetDetail(tier == Core::LodTier::Full && !ship->Derelict);
			if (ship->Derelict)
			{
				shipViews[i]->Derelict(rel, shipRotation[i], static_cast<float>(shipLength[i]), Time::unscaledTime + ship->Id);
				continue;
			}
			auto target = Target::OfShip(ship);
			auto unit = ship->Battle ? ship->Battle->UnitOf(ship) : nullptr;
			bool thrusting = shipStates[i].Burning || (unit != nullptr && unit->Vel.LengthSquared() > 1e-4);
			shipViews[i]->Apply(rel, shipRotation[i], static_cast<float>(shipLength[i]), shipStates[i].Burning,
				target == Selected || target == Hovered);
			shipViews[i]->ApplyCombat(ship->Combat, ship->Battle != nullptr, thrusting, Time::unscaledTime, dt);
		}
		for (int g = glowIndex; g < entryGlows.Count(); g++) std::get<0>(entryGlows[g])->gameObject()->SetActive(false);

		UpdatePlatforms();
		laneView->Update(ShowLanes);
		gateView->Update(true, nearGate);
		fighterViews->Update();
		embeddedPods->Update();
		battleFx->Update();
		debrisFields->Update();
		podBeacons->Update();
		Structures->Update(true);
		swarm->Update(ShowSatellites);
		UpdateOrbitLines(dt);
		discView->Update(ShowOrbits);
		UpdatePathLine();
	}

	void UnitySpaceApp::PlaceEntryGlow(int index, int ship, Vector3 rel, float heat, double pixelsPerUnit)
	{
		if (index >= entryGlows.Count())
		{
			if (!glowQuad) glowQuad = Rendering::MeshFactory::Quad();
			auto go = UnityCompat::NewGameObject("Entry glow");
			go->transform->SetParent(systemRoot, false);
			UnityCompat::AddComponent<UnityCompat::MeshFilter>(go)->sharedMesh = glowQuad;
			auto mr = UnityCompat::AddComponent<UnityCompat::MeshRenderer>(go);
			auto m = Rendering::Materials::Create(Rendering::Materials::Glow(), "Entry glow");
			m->SetFloat("_Falloff", 2.0f);
			mr->SetSharedMaterial(m);
			mr->shadowCastingMode = UnityCompat::ShadowCastingMode::Off;
			entryGlows.Add(std::make_tuple(go->transform, m));
		}
		auto [t, material] = entryGlows[index];
		t->gameObject()->SetActive(true);
		float length = static_cast<float>(shipLength[ship]);
		// Ships come down tail-first: the sheath forms on the face meeting the air, below the drive.
		auto nose = shipRotation[ship] * Vector3::forward();
		auto p = rel - nose * length * 0.55f;
		float dist = Mathf::Max(p.magnitude(), 1e-6f);
		// At least a few pixels, so a descent shows as a spark from orbit.
		float size = Mathf::Max(length * (0.45f + 0.75f * heat), dist * 5.0f / static_cast<float>(pixelsPerUnit) * heat);
		t->localPosition = p;
		t->localRotation = Quaternion::LookRotation(p / dist, Vector3::up());
		t->localScale = Vector3::one() * size;
		material->SetColor("_Color", Color::Lerp(Color(1.0f, 0.55f, 0.25f), Color(1.0f, 0.4f, 0.75f), heat));
		material->SetFloat("_Intensity", 0.35f + 0.9f * heat);
	}

	void UnitySpaceApp::DrawWreck(int i, const cs::Ref<Core::Ship>& ship)
	{
		auto wreck = World()->War->Wrecks->Of(ship);
		if (wreck == nullptr)
		{
			shipViews[i]->SetVisible(false);
			return;
		}
		if (wreckStart[i] < 0) wreckStart[i] = Time::unscaledTime;
		auto rel = Relative(WreckScene(wreck));
		float length = static_cast<float>(Scale()->ShipLength(ship->LengthKm()));
		// A wreck has no icon: below a pixel it is not drawn at all (U.SPC-T415).
		if (Core::RenderLod::Tier(Core::RenderLod::Wreck(), Core::RenderLod::ApparentPx(length, rel.magnitude(), PixelsPerUnitAtUnitDistance())) == Core::LodTier::Culled)
		{
			shipViews[i]->SetVisible(false);
			return;
		}
		shipViews[i]->SetDetail(true);
		bool stillBurning = wreckStart[i] != cs::Single::MaxValue
			&& shipViews[i]->Wreck(rel, shipRotation[i], length, Time::unscaledTime - wreckStart[i], Time::unscaledTime);
		if (!stillBurning)
		{
			wreckStart[i] = cs::Single::MaxValue;
			// Still in two halves, its guns gone: a burnt-out wreck, not a whole hull adrift.
			shipViews[i]->BurntOut(rel, shipRotation[i], length, Time::unscaledTime + ship->Id);
		}
	}

	void UnitySpaceApp::UpdatePlatforms()
	{
		double ppu = PixelsPerUnitAtUnitDistance();
		cs::HashSet<cs::Ref<Core::ShipCombat>> live;
		for (const auto& b : World()->War->Battles)
		{
			for (const auto& u : b->Units)
			{
				if (u->Kind != Core::UnitKind::Platform || !u->Present()) continue;
				live.Add(u->Combat);
				cs::Ref<GameObject> go;
				if (!platformViews.TryGetValue(u->Combat, go))
				{
					go = UnityCompat::NewGameObject(u->Name());
					go->transform->SetParent(systemRoot, false);
					UnityCompat::AddComponent<UnityCompat::MeshFilter>(go)->sharedMesh = Rendering::ShipMeshFactory::GetPlatform(u->Faction);
					auto mr = UnityCompat::AddComponent<UnityCompat::MeshRenderer>(go);
					mr->SetSharedMaterial(Rendering::ShipModels::ProceduralMaterial());
					mr->shadowCastingMode = UnityCompat::ShadowCastingMode::Off;
					platformViews[u->Combat] = go;
				}
				auto rel = Relative(BattleScene(b, u->Pos));
				float length = static_cast<float>(Scale()->ShipLength(Core::CombatSpec::PlatformLengthKm));
				// Below a pixel a platform is not drawn at all (U.SPC-T415); its battle still shows.
				bool shown = Core::RenderLod::DrawsModel(Core::RenderLod::Tier(Core::RenderLod::Platform(), Core::RenderLod::ApparentPx(length, rel.magnitude(), ppu)));
				if (go->activeSelf != shown) go->SetActive(shown);
				if (!shown) continue;
				go->transform->localPosition = rel;
				go->transform->localRotation = Quaternion::Euler(20, static_cast<float>(cs::Mod(b->Seconds * 0.5, 360.0)), 0);
				go->transform->localScale = Vector3::one() * length;
			}
		}
		cs::List<cs::Ref<Core::ShipCombat>> gone;
		for (auto kv : platformViews)
			if (!live.Contains(kv.first))
			{
				UnityCompat::Destroy(kv.second);
				gone.Add(kv.first);
			}
		for (const auto& k : gone) platformViews.Remove(k);
	}

	void UnitySpaceApp::UpdateOrbitLines(float dt)
	{
		orbitRefreshTimer -= dt;
		bool refresh = orbitRefreshTimer <= 0 || ScaleAnimating();
		if (refresh) orbitRefreshTimer = 0.2f;

		for (int i = 0; i < Catalog()->Count(); i++)
		{
			auto line = orbitLines[i];
			if (line == nullptr) continue;
			auto body = (*Catalog())[i];
			auto parent = (*Catalog())[body->ParentIndex];
			line->SetVisible(ShowOrbits);
			if (!ShowOrbits) continue;

			double orbitScene = Scale()->LocalDistance(parent, body->Elements->AKm);
			double cameraDistance = (SpaceCamera->ScenePosition - bodyScene[parent->Index]).Length();
			float alpha;
			Color color;
			bool selected = Selected == Target::OfBody(i);
			if (body->Kind == Core::BodyKind::Moon)
			{
				alpha = Mathf::Clamp01(1.6f - static_cast<float>(cameraDistance / (orbitScene * 14.0)));
				color = Color(0.55f, 0.65f, 0.78f, 0.45f);
			}
			else
			{
				alpha = 1.0f;
				color = body->Kind == Core::BodyKind::DwarfPlanet || body->Kind == Core::BodyKind::Asteroid ? Color(0.7f, 0.62f, 0.5f, 0.32f)
					: body->Kind == Core::BodyKind::Station ? Color(0.9f, 0.75f, 0.45f, 0.3f)
					: body->IsStar() ? Color(1.0f, 0.85f, 0.55f, 0.35f)
					: body->Status != Core::BodyStatus::Confirmed ? Color(0.6f, 0.6f, 0.7f, 0.25f)
					: Color(0.45f, 0.62f, 0.9f, 0.42f);
			}
			if (selected) color = Color(0.55f, 0.9f, 1.0f, 0.85f);
			line->SetVisible(alpha > 0.01f);
			if (!line->Visible()) continue;

			if (refresh || line->Capacity() == 0)
			{
				Ephemeris()->SampleOrbitTrail(i, Clock->Jd, orbitSamples);
				for (int k = 0; k < orbitSamples.Count(); k++)
				{
					auto o = Scale()->LocalOffset(parent, orbitSamples[k]);
					orbitPoints[k] = Vector3(static_cast<float>(o.X), static_cast<float>(o.Y), static_cast<float>(o.Z));
				}
				line->SetPoints(orbitPoints, orbitPoints.Count(), color, 1.0f, 0.12f);
			}
			line->SetTint(Color(1, 1, 1, alpha));
			line->Root->transform->localPosition = Relative(bodyScene[parent->Index]);
			line->Root->transform->localRotation = Quaternion::identity();
		}
	}

	void UnitySpaceApp::UpdatePathLine()
	{
		auto t = Selected.IsShip() ? Selected : Hovered.IsShip() ? Hovered : Target::None();
		auto ship = t.IsShip() ? t.Ship->Controller() : nullptr;
		if (ship == nullptr || ship->Destroyed || ship->Derelict || ship->Battle != nullptr || (ship->Phase != Core::ShipPhase::Transit && ship->Phase != Core::ShipPhase::Lane))
		{
			pathLine->SetVisible(false);
			return;
		}
		double progress = shipStates[ship->Id].Progress;
		for (int k = 0; k < pathPoints.Count(); k++)
		{
			double u = progress + (1 - progress) * k / (pathPoints.Count() - 1);
			Core::DVec3 unused;
			pathPoints[k] = Relative(ship->Phase == Core::ShipPhase::Lane ? Scale()->LaneScene(Ephemeris(), bodyScene, Fleet()->Lanes, ship, u, unused) : TransitScene(ship, u, unused));
		}
		// A company hull's path is drawn in its company's livery.
		auto c = ship->Company ? ship->Company->Livery : ship->Faction->Color;
		pathLine->SetPoints(pathPoints, pathPoints.Count(), Color(c.R, c.G, c.B, 0.9f), 1.0f, 0.35f);
		pathLine->SetTint(Color::white());
		pathLine->Root->transform->localPosition = Vector3::zero();
		pathLine->SetVisible(true);
	}

	double UnitySpaceApp::PixelsPerUnitAtUnitDistance()
	{
		return Screen::height / (2.0 * cs::Math::Tan(UnityCamera->fieldOfView * 0.5 * Mathf::Deg2Rad));
	}

	Vector3 UnitySpaceApp::SkyDirection(const cs::Ref<Core::StarSystem>& other)
	{
		auto d = (other->PositionLy - ActiveSystem->PositionLy).Normalized().SwapYZ();
		return Vector3(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z));
	}

	// ---------------------------------------------------------------- picking

	bool UnitySpaceApp::ToGui(Core::DVec3 scene, Vector2& gui, float& depth)
	{
		auto rel = Relative(scene);
		auto sp = UnityCamera->WorldToScreenPoint(rel);
		depth = sp.z;
		gui = Vector2(sp.x, Screen::height - sp.y);
		return sp.z > 0;
	}

	bool UnitySpaceApp::RelativeToGui(Vector3 rel, Vector2& gui)
	{
		auto sp = UnityCamera->WorldToScreenPoint(rel);
		gui = Vector2(sp.x, Screen::height - sp.y);
		return sp.z > 0;
	}

	bool UnitySpaceApp::Occluded(Core::DVec3 p, int ignoreBody)
	{
		auto dir = p - SpaceCamera->ScenePosition;
		double len = dir.Length();
		if (len <= 0) return false;
		dir /= len;
		for (int i = 0; i < bodyScene.Count(); i++)
		{
			if (i == ignoreBody || !(*Catalog())[i]->IsVisible()) continue;
			auto oc = SpaceCamera->ScenePosition - bodyScene[i];
			double b = Core::DVec3::Dot(oc, dir);
			double c = oc.LengthSquared() - bodyRadius[i] * bodyRadius[i];
			if (c < 0) continue;
			double disc = b * b - c;
			if (disc <= 0) continue;
			double hit = -b - cs::Math::Sqrt(disc);
			if (hit > 0 && hit < len - bodyRadius[i] * 1e-3) return true;
		}
		return false;
	}

	void UnitySpaceApp::UpdateHover()
	{
		if (Input.LookHeld || IsPointerOverUi() || Travel->Active)
		{
			Hovered = Target::None();
			return;
		}
		auto mouse = Vector2(Input.MousePosition.x, Screen::height - Input.MousePosition.y);
		Hovered = Pick(mouse);
	}

	void UnitySpaceApp::HandleSelectionClicks()
	{
		// The fire trigger shares the left button; a locked, centred cursor while piloting would
		// otherwise register every release as an in-place click and reselect something underfoot.
		if (!Input.SelectClicked || Travel->Active || AtControls()) return;
		Select(Hovered);
		if (Input.SelectDoubleClicked && !Hovered.IsNone()) SpaceCamera->FocusOn(Hovered);
	}

	Target UnitySpaceApp::Pick(Vector2 gui)
	{
		double ppu = PixelsPerUnitAtUnitDistance();
		auto best = Target::None();
		double bestScore = cs::Double::MaxValue;

		for (int i = 0; i < Catalog()->Count(); i++)
		{
			if (!(*Catalog())[i]->IsVisible()) continue;
			Vector2 p;
			float depth;
			if (!ToGui(bodyScene[i], p, depth)) continue;
			double pxRadius = bodyRadius[i] / depth * ppu;
			double pickRadius = cs::Math::Max(pxRadius, UI::Icons::PixelSize((*Catalog())[i]) * 0.5 + 5);
			double d = (p - gui).magnitude();
			if (d > pickRadius) continue;
			if (pxRadius < 3 && Occluded(bodyScene[i], i)) continue;
			double score = d / pickRadius + 0.5;
			if (score < bestScore)
			{
				bestScore = score;
				best = Target::OfBody(i);
			}
		}

		const auto& ships = Fleet()->Ships();
		// At the helm only what the scanners hold can be picked (U.SPC-T66), unless it is close enough to see.
		auto picture = IsPiloting() && Piloted.Ship != nullptr && World()->Sensors ? World()->Sensors->Player : nullptr;
		for (int i = 0; i < ships.Count(); i++)
		{
			if (ships[i]->Destroyed || ships[i]->Controller()->Phase == Core::ShipPhase::Landed) continue;
			Vector2 p;
			float depth;
			if (!ToGui(shipScene[i], p, depth)) continue;
			double px = Core::ShipLod::ApparentPx(shipLength[i] * Core::ShipLod::ExtentFactor(ships[i]), depth, ppu);
			if (picture != nullptr && ships[i] != Piloted.Ship && !Core::ShipLod::DrawsModel(px) && picture->Of(ships[i]) == nullptr) continue;
			if (FoldedShips.Contains(ships[i]) && !Core::ShipLod::DrawsModel(px)) continue;
			double pickRadius = cs::Math::Max(px * 0.5, UI::Icons::PixelSize(ships[i]->Spec->Class) * 0.5 + 4);
			double d = (p - gui).magnitude();
			if (d > pickRadius) continue;
			if (Occluded(shipScene[i])) continue;
			// Ships win ties against the planet they orbit.
			double score = d / pickRadius;
			if (score < bestScore)
			{
				bestScore = score;
				best = Target::OfShip(ships[i]);
			}
		}

		// A ship's name beside its icon is as good as the icon itself.
		for (const auto& [rect, ship] : ShipNameRects)
		{
			if (ship->Destroyed || !rect.Contains(gui)) continue;
			const double nameScore = 0.15;
			if (nameScore < bestScore)
			{
				bestScore = nameScore;
				best = Target::OfShip(ship);
			}
		}

		// Structures and rings, only once they are drawn large enough to be things.
		auto Consider = [&](const cs::Ref<void>& key, Core::DVec3 scene, const Target& t)
		{
			Vector2 p;
			float depth;
			if (!ToGui(scene, p, depth)) return;
			double px = Structures->Sizes.At(key) / depth * ppu;
			if (px < 4) return;
			double pickRadius = cs::Math::Max(px * 0.5, 8);
			double d = (p - gui).magnitude();
			if (d > pickRadius || Occluded(scene)) return;
			double score = d / pickRadius + 0.2;
			if (score < bestScore)
			{
				bestScore = score;
				best = t;
			}
		};
		for (auto kv : Structures->Scenes) Consider(kv.first, kv.second, Target::OfStructure(kv.first));
		for (auto kv : Structures->RingScenes) Consider(kv.first, kv.second, Target::OfRing(kv.first));
		return best;
	}
}
