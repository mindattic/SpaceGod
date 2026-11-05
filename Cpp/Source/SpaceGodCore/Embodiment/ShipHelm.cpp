// Port of Unity.SpaceGod Core/Embodiment/ShipHelm.cs (US-T21, see docs/MIGRATION.md).

#include "Embodiment/ShipHelm.h"
#include "Embodiment/HelmAbilities.h"
#include "Embodiment/HelmFlight.h"
#include "Embodiment/SelfDestruct.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/Broadsides.h"
#include "Combat/CombatState.h"
#include "Combat/Dogfighting.h"
#include "Combat/FlightModels.h"
#include "Combat/HaulerFlight.h"
#include "Combat/PilotFlight.h"
#include "Combat/PilotTuning.h"
#include "Combat/War.h"
#include "Combat/WeakPatches.h"
#include "Economy/Propulsion.h"
#include "Infrastructure/Infrastructure.h" // DockingRing

namespace SpaceGod::Core
{
	ShipHelm::ShipHelm(const cs::Ref<World>& world, const cs::Ref<Core::Ship>& ship)
	{
		this->world = world;
		Ship = ship;
	}

	cs::String ShipHelm::Name() const { return Ship->Name; }

	cs::Ref<Core::Faction> ShipHelm::Faction() const { return Ship->Faction; }

	bool ShipHelm::Embodied() const { return world->PilotedShip == Ship && Ship != nullptr; }

	cs::Ref<BattleUnit> ShipHelm::Unit() const { return Ship->Battle ? Ship->Battle->UnitOf(Ship) : nullptr; }

	cs::String ShipHelm::BlockerOf(const cs::Ref<Core::Ship>& ship)
	{
		if (ship == nullptr) return "nothing there";
		if (ship->Destroyed) return ship->Combat != nullptr && ship->Combat->SelfDestructed ? "self-destructed" : "destroyed";
		if (ship->Derelict) return "derelict";
		// C#: ship.Battle?.UnitOf(ship)?.Captured ?? false
		if (ship->Battle != nullptr)
		{
			auto unit = ship->Battle->UnitOf(ship);
			if (unit != nullptr && unit->Captured) return "taken as a prize";
		}
		return cs::String();
	}

	std::tuple<int, DVec3> ShipHelm::FrameOf(const cs::Ref<World>& world, const cs::Ref<Core::Ship>& ship, double jd)
	{
		// An escort has no schedule of its own (no orbit, no phase): it is wherever its leader is.
		auto c = ship->Controller();
		auto st = world->Fleet->StateOf(c, jd);
		if (c->Phase == ShipPhase::Parked)
			return { c->Orbit->Body, c->Orbit->OffsetAt(jd) };
		if ((c->Phase == ShipPhase::Queued || c->Planetside()) && c->Ring != nullptr)
			return { c->Ring->Body, st.HelioKm - world->Ephemeris->Helio(c->Ring->Body) };
		return { c->FrameBody, st.HelioKm - world->Ephemeris->Helio(c->FrameBody) };
	}

	EmbodimentPose ShipHelm::PoseAt(double jd)
	{
		auto unit = Unit();
		if (unit != nullptr)
		{
			DVec3 unusedRight, up;
			unit->Basis(unusedRight, up);
			return EmbodimentPose(Ship->Battle->Body, unit->Pos + Ship->Battle->Anchor, unit->Forward, up);
		}
		auto [body, offset] = FrameOf(world, Ship, jd);
		auto f = world->Fleet->StateOf(Ship, jd).Facing;
		f = f.LengthSquared() > 1e-18 ? f.Normalized() : DVec3::UnitX;
		return EmbodimentPose(body, offset, f, PilotState::LevelUp(f));
	}

	void ShipHelm::TakeCharge()
	{
		world->PilotedShip = Ship;
		if (Ship->IsEscort()) Ship->Leader = nullptr;
		else if (world->War) world->War->HandOverCommand(Ship);
		auto unit = Unit();
		if (unit != nullptr)
		{
			// Keep the hull's roll: the helm's roof starts where the captain's (or level) roof was.
			DVec3 unusedRight, rolled;
			unit->Basis(unusedRight, rolled);
			unit->Up = rolled;
			unit->SetPlayerControlled(true);
			unit->PlayerFireHeld = false;
			unit->PlayerLockTarget = nullptr;
		}
	}

	HelmControls ShipHelm::Controls() const { return HelmControls::Of(Ship); }

	void ShipHelm::Control(const EmbodimentInput& input, double dt)
	{
		// The reactor split asked for this frame (US-T94), whole, so a repeated command lands the same.
		if (input.PowerSplit >= 0 && Ship->Combat != nullptr)
		{
			if (Ship->Combat->Reactor == nullptr) Ship->Combat->Reactor = cs::New<PowerDistribution>();
			Ship->Combat->Reactor->SetCode(input.PowerSplit);
		}
		// The self-destruct (US-T97): armed or aborted here; the sim counts it down (US-T114), and run out the hull goes.
		if (!Ship->Destroyed && SelfDestruct::AtControls(Ship->Combat, input.SelfDestruct, SelfDestruct::FuseSeconds(Ship->Combat, false, false)))
		{
			SetOff();
			return;
		}
		// A helm ability ordered this frame (US-T106): a squadron launched, the wing recalled or called, a boarding order.
		if (input.HelmAbility != 0 && !Ship->Destroyed) HelmAbilities::Use(Ship, static_cast<HelmAbility>(input.HelmAbility));
		auto unit = Unit();
		if (unit != nullptr && unit->PlayerControlled())
		{
			unit->PlayerFireHeld = input.Primary;
			unit->PlayerSecondaryHeld = input.Secondary;
			unit->PlayerAim = input.Aim;
		}
		if (Flight != nullptr && !Ship->Destroyed) Fly(Flight, input, dt);
	}

	void ShipHelm::SetOff()
	{
		auto unit = Unit();
		if (unit != nullptr && unit->Present())
		{
			SelfDestruct::InBattle(Ship->Battle, unit);
			return;
		}
		// Out of any fight: where the helm has flown it, else where its schedule has it.
		auto pose = PoseAt(world->Ephemeris->Jd);
		int body = pose.FrameBody;
		DVec3 at = pose.OffsetKm, forward = pose.Forward;
		if (Flight != nullptr && Flight->FrameBody >= 0)
		{
			body = Flight->FrameBody;
			at = Flight->State.Pos;
			forward = Flight->State.Forward;
		}
		SelfDestruct::Outside(world, Ship->Combat, Ship->Name, Ship->Faction, Ship, body, at, forward);
	}

	void ShipHelm::Fly(const cs::Ref<HelmFlight>& f, const EmbodimentInput& e, double dt)
	{
		auto ship = Ship;
		auto controls = Controls();
		auto tuning = PilotTuning::Of(ship->Spec->Class);
		auto hull = HullDynamics::Of(ship->Combat->Spec);
		auto model = FlightModelRegistry::Default()->For(ship->Spec->Class);
		auto capital = cs::As<CapitalFlightModel>(model);
		PilotInput input;
		input.DesiredForward = e.Aim;
		input.Throttle = Clamp1(e.Throttle);
		input.StrafeRight = Clamp1(e.StrafeRight);
		input.StrafeUp = Clamp1(e.StrafeUp);
		input.Roll = Clamp1(e.Roll);
		input.Boost = e.Boost;
		input.Cruise = e.Cruise;
		input.InCombat = ship->Battle != nullptr;
		input.FlightAssistOff = f->AssistOff;
		input.TelegraphStep = controls.Telegraph ? e.TelegraphStep : 0;
		// A hauler flies heavy on what it carries, and Ctrl inside a port's approach is docking mode (U.SPC-T443).
		input.MassRatio = HaulerFlightModel::LadenRatio(ship);
		input.Fine = e.Fine;
		input.DockingApproach = f->DockingApproach;
		// The player's own hands (US-T87): the models' player-facing handling applies, never a crew's.
		input.Piloted = true;
		// The engines' share of the reactor (US-T94): top speed (assist's set point), every thruster's push and the boost's
		// recharge, a quarter a pip either side of an even share. The helm is only ever the player's, so crews fly as before.
		auto reactor = ship->Combat->Reactor;
		double engines = reactor != nullptr ? reactor->Factor(PowerSystem::Engines) : 1;
		if (engines != 1)
		{
			tuning.MaxSpeed *= engines;
			hull = HullDynamics(hull.MassKt, hull.MainAccel * engines, hull.RetroAccel * engines, hull.LateralAccel * engines,
				hull.VerticalAccel * engines, hull.PitchYawAngularAccel, hull.RollAngularAccel);
			input.BoostRecharge = engines;
		}
		// The turn-and-burn (US-T87): held, the nose swings onto the retrograde while the hull slides on along its track.
		// A capital ship's is the broadside stance instead (US-T81): the helm lays it beam-on to the locked hull (or holds
		// its heading with none) while the turn keys, passed through as strafe, slide it sideways and up or down.
		cs::String broadside;
		if (e.Manoeuvre && capital != nullptr) input.DesiredForward = BroadsideAim(f->State, broadside);
		else if (e.Manoeuvre) input.DesiredForward = PilotFlight::FlipAim(f->State);
		if (f->DrivesDown)
		{
			input.DesiredForward = f->State.Forward;
			input.Throttle = input.StrafeRight = input.StrafeUp = input.Roll = 0;
			input.Boost = input.Cruise = false;
			input.FlightAssistOff = true;
			input.TelegraphStep = 0;
		}
		// Ordered to roll the weak patches away (U.SPC-T502): the helm assist rolls the hull while the helmsman's hands
		// are off the roll keys; the nose stays the helmsman's.
		if (capital != nullptr && !input.FlightAssistOff && cs::Math::Abs(input.Roll) < 0.01 && ship->Battle != nullptr)
		{
			// C#: ship.Battle.UnitOf(ship) is BattleUnit helmUnit && helmUnit.Broadside != null && helmUnit.Broadside.RollWeakPatches
			auto helmUnit = ship->Battle->UnitOf(ship);
			if (helmUnit != nullptr && helmUnit->Broadside != nullptr && helmUnit->Broadside->RollWeakPatches)
				input.Roll = WeakPatches::HelmRoll(ship->Battle, helmUnit, f->State.Forward, f->State.Up,
					capital->PeakTurnRateDegS(hull) * capital->Handling.RollFactor, hull.RollAngularAccel, dt);
		}
		// Read before the model drains the charge further this frame (a capital rung to Flank boosts too).
		bool flank = capital != nullptr && !input.FlightAssistOff && f->State.Telegraph == EngineTelegraph::Flank;
		f->Boosting = (input.Boost || flank) && tuning.BoostDurationS > 0 && ship->Combat->BoostFraction > 0;

		auto gravity = f->Gravity;
		auto posBefore = f->State.Pos;
		auto velBefore = f->State.Vel;
		model->Step(f->State, ship->Combat, hull, tuning, input, gravity, dt);
		auto pos = f->State.Pos;
		auto vel = f->State.Vel;
		Propulsion::ApplyManoeuvre(ship, pos, vel, posBefore, velBefore + gravity * dt, dt);
		f->State.Pos = pos;
		f->State.Vel = vel;

		f->Model = model;
		f->Hull = hull;
		f->Input = input;
		f->PosBefore = posBefore;
		f->VelBefore = velBefore;
		f->Burning = dt > 0 && (vel - (velBefore + gravity * dt)).Length() / dt > HelmFlight::BurnKmS2;
		// With assist off nothing brakes for the pilot (US-T87): what stopping costs, a flip and a burn or the retros.
		// Assist off, every model flies the Newtonian rules on the class's own turn rate, so that is the flip's rate.
		f->Note = input.FlightAssistOff && !f->DrivesDown ? PilotFlight::Brake(f->State, hull, tuning.TurnRateDegS).Text() : broadside;
	}

	DVec3 ShipHelm::BroadsideAim(const PilotState& s, cs::String& note) const
	{
		auto unit = Unit();
		auto target = unit != nullptr ? unit->PlayerLockTarget : nullptr;
		if (target == nullptr || !target->Present())
		{
			note = "BROADSIDE STANCE: holding the heading, A/D Q/E slide the hull (lock a hull, T, to lay a flank on it)";
			return s.Forward;
		}
		auto to = target->Pos - unit->Pos;
		auto aim = CapitalFlightModel::BeamOn(s.Forward, s.Up, to);
		// Starboard is the hull's right: the flank that bears is the one the target lies off.
		bool starboard = DVec3::Dot(DVec3::Cross(aim, s.Up), to) > 0;
		note = cs::Cat("BROADSIDE: ", starboard ? "starboard" : "port", " flank to ", target->Name(), ", A/D Q/E slide the hull");
		return aim;
	}

	double ShipHelm::Clamp1(double v) { return cs::Math::Max(-1.0, cs::Math::Min(1.0, v)); }

	void ShipHelm::HandBack()
	{
		// Leaving the helm stops a self-destruct under way (US-T97): no crew carries one on.
		if (Ship != nullptr) SelfDestruct::Disarm(Ship->Combat);
		if (world->PilotedShip == Ship) world->PilotedShip = nullptr;
		if (Ship->Destroyed) return;
		auto unit = Unit();
		if (unit != nullptr)
		{
			// Keep the hull's roll: the captain's roof picks up from the helm's.
			if (unit->PlayerControlled()) unit->Roof = unit->Up;
			unit->SetPlayerControlled(false);
			unit->PlayerFireHeld = false;
			unit->PlayerLockTarget = nullptr;
		}
	}

	bool ShipHelm::Equals(const cs::Ref<IEmbodiable>& obj) const
	{
		auto h = cs::As<ShipHelm>(obj);
		return h && h->Ship == Ship;
	}

	size_t ShipHelm::GetHashCode() const { return Ship ? cs::Hash<cs::Ref<Core::Ship>>{}(Ship) : 0; }
}
