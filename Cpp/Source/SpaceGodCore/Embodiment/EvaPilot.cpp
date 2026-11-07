// US-T166: eject and EVA (see EvaPilot.h).

#include "Embodiment/EvaPilot.h"
#include "Combat/Battle.h"
#include "Combat/CombatState.h"
#include "Combat/Fighters.h"
#include "Embodiment/FighterSeat.h"

namespace SpaceGod::Core
{
	namespace
	{
		const char* const SafingNames[EvaPilot::SafingSteps] = { "arming plug pulled", "fuse cut", "booster unseated", "detonator out" };

		DVec3 PerpendicularTo(DVec3 v)
		{
			auto a = cs::Math::Abs(v.Y) < 0.9 ? DVec3(0, 1, 0) : DVec3(1, 0, 0);
			auto p = a - v * DVec3::Dot(a, v);
			return p.LengthSquared() > 1e-12 ? p.Normalized() : DVec3(0, 0, 1);
		}

		/// Where a round is going: the part it is sent at, else its target's centre.
		DVec3 AimOf(const cs::Ref<Missile>& m)
		{
			auto t = m->Target;
			return m->TargetSub != nullptr && !m->TargetSub->Destroyed() ? t->PointOf(m->TargetSub->Def) : t->Pos;
		}
	}

	EvaPilot::EvaPilot(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& craft)
	{
		Battle = battle;
		Craft = craft;
		Side = craft != nullptr ? craft->Side : 0;
		Callsign = craft != nullptr ? cs::Cat(FighterSeat(battle, craft).Name(), " pilot") : cs::String("pilot");
		DeltaV = SuitDeltaVKmS;
	}

	cs::Ref<EvaPilot> EvaPilot::Eject(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& craft, cs::String& why)
	{
		why = cs::String();
		cs::Ref<FighterControl> stick;
		if (battle == nullptr || craft == nullptr || craft->Kind != UnitKind::Fighter) why = "not a launched craft";
		else if (battle->Over) why = "the fight is over";
		else if (!craft->Present() || craft->Dead) why = "the craft is gone";
		else if (craft->Docked) why = "down on a flight deck: climb out there";
		else if (!battle->Flown.TryGetValue(craft, stick) || stick->Abandoned) why = "nobody at its stick to eject";
		if (!why.empty()) return nullptr;
		auto up = FighterSeat(battle, craft).PoseAt(0).Up;
		if (up.LengthSquared() < 1e-12) up = PerpendicularTo(craft->Forward.LengthSquared() > 1e-12 ? craft->Forward.Normalized() : DVec3(0, 0, 1));
		up = up.Normalized();
		// The stick let go: nothing aimed, no burn, assist off, so the flight model (after a load too) only coasts it.
		stick->Abandoned = true;
		stick->Aim = DVec3::Zero;
		stick->Throttle = 0;
		stick->Burn = 0;
		stick->FlightAssistOff = true;
		stick->Secondary = stick->Boost = stick->Flip = stick->DriftHeld = false;
		stick->StrafeRight = stick->StrafeUp = stick->Roll = 0;
		craft->PlayerFireHeld = craft->PlayerSecondaryHeld = false;
		craft->PlayerLockTarget = nullptr;
		craft->PlayerSubTarget = nullptr;
		auto p = cs::New<EvaPilot>(battle, craft);
		p->Pos = craft->Pos + up * (craft->Radius() + 0.004);
		p->Vel = craft->Vel + up * EjectKickKmS;
		p->Facing = craft->Forward.LengthSquared() > 1e-12 ? craft->Forward.Normalized() : PerpendicularTo(up);
		p->Up = up;
		p->Note("EJECTED: the canopy blown, the craft coasting on without you");
		p->ScanNearest();
		return p;
	}

	void EvaPilot::Unabandon(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& craft, bool stillHeld)
	{
		cs::Ref<FighterControl> stick;
		if (battle == nullptr || craft == nullptr || !battle->Flown.TryGetValue(craft, stick) || !stick->Abandoned) return;
		stick->Abandoned = false;
		stick->FlightAssistOff = false;
		stick->Aim = craft->Forward;
		// Let go of by the player on the way out: back in, and its own pilot flies it on from here.
		if (!stillHeld) FighterSeat(battle, craft).HandBack();
	}

	bool EvaPilot::Rideable(const cs::Ref<Missile>& m)
	{
		return m != nullptr && !m->Gone && (m->Kind == MissileKind::Torpedo || m->Kind == MissileKind::Antimatter);
	}

	double EvaPilot::SecondsToImpact(const cs::Ref<Missile>& m)
	{
		if (m == nullptr || m->Target == nullptr || !m->Target->Present()) return -1;
		auto to = AimOf(m) - m->Pos;
		double d = to.Length();
		if (d < 1e-9) return 0;
		double closing = DVec3::Dot(m->Vel - m->Target->Vel, to / d);
		return d / cs::Math::Max(closing, cs::Math::Max(m->Speed * 0.25, 1e-3));
	}

	DVec3 EvaPilot::AxisOf(const cs::Ref<Missile>& m)
	{
		if (m == nullptr) return DVec3(0, 0, 1);
		if (m->Vel.LengthSquared() > 1e-12) return m->Vel.Normalized();
		if (m->Target != nullptr)
		{
			auto to = m->Target->Pos - m->Pos;
			if (to.LengthSquared() > 1e-12) return to.Normalized();
		}
		return DVec3(0, 0, 1);
	}

	cs::String EvaPilot::Name() const { return Callsign; }

	cs::Ref<Core::Faction> EvaPilot::Faction() const { return Craft != nullptr ? Craft->Faction : nullptr; }

	cs::String EvaPilot::Blocker() const
	{
		if (!Lost.empty()) return Lost;
		if (Battle == nullptr || Battle->Over) return "picked up: the fight is over";
		return cs::String();
	}

	EmbodimentPose EvaPilot::PoseAt(double /*jd*/)
	{
		return EmbodimentPose(Battle != nullptr ? Battle->Body : 0, Pos + (Battle != nullptr ? Battle->Anchor : DVec3::Zero), Facing, Up);
	}

	void EvaPilot::Note(cs::String what)
	{
		Event = std::move(what);
		EventS = 3.5;
	}

	void EvaPilot::Control(const EmbodimentInput& input, double dt)
	{
		if (Battle == nullptr || !Lost.empty()) return;
		dt = cs::Math::Max(0.0, dt);
		bool press = input.Primary && !primaryWas;
		bool letGo = input.Secondary && !secondaryWas;
		primaryWas = input.Primary;
		secondaryWas = input.Secondary;
		EventS = cs::Math::Max(0.0, EventS - dt);
		// The suit turns where the player looks; its roof stays square to the look.
		if (input.Aim.LengthSquared() > 1e-12)
		{
			Facing = input.Aim.Normalized();
			auto up = Up - Facing * DVec3::Dot(Up, Facing);
			Up = up.LengthSquared() > 1e-9 ? up.Normalized() : PerpendicularTo(Facing);
		}
		if (Latched != nullptr)
		{
			if (letGo) LetGo("Let go: the torpedo flies on without you");
			else Ride(input, dt, press);
		}
		else Fly(input, dt, press);
		ScanNearest();
	}

	void EvaPilot::Fly(const EmbodimentInput& input, double dt, bool press)
	{
		auto right = DVec3::Cross(Facing, Up); // the battle's own handedness (BattleUnit::Basis: right = forward x roof)
		auto thrust = Facing * input.Throttle + right * input.StrafeRight + Up * input.StrafeUp;
		if (thrust.LengthSquared() > 1) thrust = thrust.Normalized();
		if (thrust.LengthSquared() > 1e-12 && DeltaV > 0 && dt > 0)
		{
			double accel = SuitAccelKmS2 * (input.Boost ? JetFactor : 1.0);
			double dv = cs::Math::Min(DeltaV, accel * dt * thrust.Length());
			Vel = Vel + thrust.Normalized() * dv;
			DeltaV -= dv;
			if (DeltaV <= 0) Note("SUIT DRY: no delta-v left, you drift where you are going");
		}
		Pos = Pos + Vel * dt;
		if (!press) return;
		ScanNearest();
		if (Nearest == nullptr)
		{
			Note("Nothing to tether onto: no torpedo in flight");
			return;
		}
		if (NearestKm > LatchReachKm)
		{
			Note(cs::Cat("Out of tether reach: the nearest torpedo is ", cs::Fmt(NearestKm * 1000, "N0"), " m off (", cs::Fmt(LatchReachKm * 1000, "N0"), " m reach)"));
			return;
		}
		if (NearestRelKmS > LatchSpeedKmS)
		{
			Note(cs::Cat("Too fast to hold: ", cs::Fmt(NearestRelKmS * 1000, "N0"), " m/s against you (", cs::Fmt(LatchSpeedKmS * 1000, "N0"), " m/s at most): match its speed first"));
			return;
		}
		Latched = Nearest;
		Along = LatchAlong;
		Cleared = 0;
		Safing.Reset();
		Safing.PrimaryWas = true;
		Vel = Latched->Vel;
		double eta = SecondsToImpact(Latched);
		Note(cs::Cat("LATCHED: mag boots on the casing", Latched->Target != nullptr ? cs::Cat(" of a torpedo bound for the ", Latched->Target->Name()) : cs::String(),
			eta >= 0 ? cs::Cat(", ", cs::Fmt(eta, "0.0"), " s from impact") : cs::String(), ": walk forward (W) to the warhead"));
	}

	void EvaPilot::Ride(const EmbodimentInput& input, double dt, bool /*press*/)
	{
		auto m = Latched;
		if (m->Gone)
		{
			// It went off, burnt out or was shot down under the pilot (a round disarmed here is let go of at once).
			bool struck = m->Target != nullptr && (m->Pos - m->Target->Pos).Length() <= m->Target->Radius() * 1.5 + 0.05;
			if (struck)
			{
				Latched = nullptr;
				Lost = cs::Cat("killed: the torpedo you rode struck the ", m->Target->Name());
				return;
			}
			LetGo("The torpedo broke up under you: thrown clear");
			return;
		}
		auto axis = AxisOf(m);
		double pace = WalkPerS * (input.Boost ? 1.6 : 1.0);
		Along = cs::Math::Clamp(Along + input.Throttle * pace * dt, 0.0, 1.0);
		// The round's position is its nose; the pilot stands on the casing behind it, mag boots holding on.
		Pos = m->Pos + axis * ((Along - 1.0) * TorpedoLengthKm);
		Vel = m->Vel;
		if (Along < WarheadAt)
		{
			// Not at the panel yet: no tap counts, and the trigger held on arriving is not a press.
			Safing.PrimaryWas = input.Primary;
			return;
		}
		int hits = Safing.Hits, misses = Safing.Misses;
		Safing.Step(dt, input.Primary);
		if (Safing.Hits > hits && Cleared < SafingSteps)
		{
			Note(cs::Cat("SAFING ", Cleared + 1, "/", SafingSteps, ": ", SafingNames[Cleared]));
			Cleared++;
		}
		else if (Safing.Misses > misses)
		{
			if (Cleared > 0) Cleared--;
			Note(cs::Cat("SLIPPED: the tools jump, a step undone (", Cleared, "/", SafingSteps, ")"));
		}
		if (Cleared < SafingSteps) return;
		double eta = SecondsToImpact(m);
		m->Gone = true;
		m->Damage = 0;
		Battle->Emit(FxKind::MissileKill, m->Pos, m->Pos, 0.02, m->Side, nullptr, -1);
		Disarmed++;
		Latched = nullptr;
		Cleared = 0;
		Note(cs::Cat("WARHEAD SAFE: the torpedo is dead", m->Target != nullptr ? cs::Cat(", the ", m->Target->Name(), " spared") : cs::String(),
			eta >= 0 ? cs::Cat(" with ", cs::Fmt(eta, "0.0"), " s to spare") : cs::String()));
	}

	void EvaPilot::LetGo(cs::String what)
	{
		if (Latched != nullptr) Vel = Latched->Vel;
		Latched = nullptr;
		Cleared = 0;
		Safing.Reset();
		Note(std::move(what));
	}

	void EvaPilot::ScanNearest()
	{
		Nearest = nullptr;
		NearestKm = NearestRelKmS = 0;
		if (Battle == nullptr) return;
		double best = cs::Double::MaxValue;
		for (const auto& m : Battle->Missiles)
		{
			if (!Rideable(m) || m == Latched) continue;
			double d = (m->Pos - Pos).Length();
			if (d >= best) continue;
			best = d;
			Nearest = m;
		}
		if (Nearest == nullptr) return;
		NearestKm = best;
		NearestRelKmS = (Nearest->Vel - Vel).Length();
	}

	cs::Ref<BattleUnit> EvaPilot::Anchor() const
	{
		if (Craft != nullptr && Craft->Present()) return Craft;
		if (Battle == nullptr) return Craft;
		// Its craft gone, the nearest hull of its own side still fighting stands for it (the camera's frame, the fight in view).
		cs::Ref<BattleUnit> best = nullptr;
		double bestD = cs::Double::MaxValue;
		for (const auto& u : Battle->Units)
		{
			if (!u->Present() || u->Side != Side) continue;
			double d = (u->Pos - Pos).LengthSquared();
			if (d < bestD)
			{
				bestD = d;
				best = u;
			}
		}
		return best != nullptr ? best : Craft;
	}

	EmbodimentCameraRig EvaPilot::CameraRig() const { return EmbodimentCameraRig(EmbodimentCameraKind::Shoulder, 7, 0.05, 0.07); }

	EmbodimentRolePanel EvaPilot::RolePanel()
	{
		EmbodimentRolePanel p;
		p.Title = "EVA";
		p.Verb = "Out on EVA as";
		auto eta = [](const cs::Ref<Missile>& m)
		{
			double s = SecondsToImpact(m);
			return s >= 0 ? cs::Cat("impact in ", cs::Fmt(s, "0.0"), " s") : cs::String("no target");
		};
		cs::String state;
		bool atWarhead = Latched != nullptr && Along >= WarheadAt;
		if (Latched != nullptr)
		{
			state = atWarhead
				? cs::Cat("On the warhead: safing ", Cleared, "/", SafingSteps, "  \xC2\xB7  ", eta(Latched))
				: cs::Cat("Riding a torpedo", Latched->Target != nullptr ? cs::Cat(" bound for the ", Latched->Target->Name()) : cs::String(), ": ",
					cs::Fmt((WarheadAt - Along) * TorpedoLengthKm * 1000, "0.0"), " m to the warhead  \xC2\xB7  ", eta(Latched));
		}
		else
		{
			state = cs::Cat("Free in the suit  \xC2\xB7  delta-v ", cs::Fmt(DeltaV * 1000, "N0"), " m/s left");
			if (Nearest != nullptr)
				state = cs::Cat(state, "  \xC2\xB7  torpedo ", cs::Fmt(NearestKm * 1000, "N0"), " m off at ", cs::Fmt(NearestRelKmS * 1000, "N0"), " m/s",
					NearestKm <= LatchReachKm && NearestRelKmS <= LatchSpeedKmS ? cs::String(": IN REACH") : cs::String(), ", ", eta(Nearest));
			else state = cs::Cat(state, "  \xC2\xB7  no torpedo in flight");
		}
		if (Disarmed > 0) state = cs::Cat(state, "  \xC2\xB7  ", Disarmed, " disarmed");
		p.Status = EventS > 0 && !Event.empty() ? cs::Cat(Event, "  \xC2\xB7  ", state) : state;
		bool inReach = Latched == nullptr && Nearest != nullptr && NearestKm <= LatchReachKm && NearestRelKmS <= LatchSpeedKmS;
		if (Latched == nullptr)
		{
			p.Abilities.Add(EmbodimentAbility("W/S A/D Q/E", "suit thrusters along your look", DeltaV > 0));
			p.Abilities.Add(EmbodimentAbility("Shift", "the jets at full", DeltaV > 0));
			p.Abilities.Add(EmbodimentAbility("Left mouse", "tether onto the torpedo in reach", inReach));
		}
		else
		{
			p.Abilities.Add(EmbodimentAbility("W/S", "walk the casing on mag boots (Shift faster)"));
			p.Abilities.Add(EmbodimentAbility("Left mouse", "safing: tap as the marker crosses the bright zone", atWarhead));
			p.Abilities.Add(EmbodimentAbility("Right mouse", "let go of it"));
		}
		p.Abilities.Add(EmbodimentAbility(", .", "time slower / faster (slow motion below real time)"));
		p.Abilities.Add(EmbodimentAbility("' ;", "next seat in the fight"));
		p.Abilities.Add(EmbodimentAbility("P", "back to the free SpaceGod view"));
		if (atWarhead)
		{
			p.Meter.Show = true;
			p.Meter.Label = "SAFING";
			p.Meter.Marker = Safing.Marker();
			p.Meter.ZoneLo = Safing.ZoneCentre() - Safing.ZoneHalf();
			p.Meter.ZoneHi = Safing.ZoneCentre() + Safing.ZoneHalf();
			p.Meter.Progress = Cleared / static_cast<double>(SafingSteps);
			p.Meter.Quality = Safing.Quality;
			if (Safing.VerdictS > 0) p.Meter.Verdict = Safing.VerdictGood ? cs::String("STEP CLEARED") : cs::String("SLIPPED");
			p.Meter.VerdictGood = Safing.VerdictGood;
		}
		return p;
	}

	EmbodimentSubject EvaPilot::Subject() const
	{
		EmbodimentSubject s;
		s.Battle = Battle;
		s.Unit = Anchor();
		s.Ship = s.Unit != nullptr ? s.Unit->Ship : nullptr;
		s.HasPoint = true;
		s.PointKm = Pos;
		return s;
	}
}
