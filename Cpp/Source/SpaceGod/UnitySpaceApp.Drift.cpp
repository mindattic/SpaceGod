// US-T165: drift at the player's controls, and the RCS jets on the hull flown. Declarations in UnitySpaceApp.Drift.inl.

#include "UnitySpaceApp.h"

#include "Views/FighterViews.h"
#include "Views/RcsJets.h"
#include "Views/ShipView.h"

#include "Astro.h"
#include "Ships.h"
#include "Combat/Battle.h"
#include "Combat/Dogfighting.h"
#include "Combat/Drift.h"
#include "Combat/Fighters.h"
#include "Combat/PilotTuning.h"
#include "Embodiment/FighterSeat.h"
#include "Embodiment/HelmFlight.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		/// <summary>The spin's change, rad/s^2, that fires a jet full: the hull's whole top swing gained or lost in a quarter
		/// of a second. From the class's own turn rate, so it reads the same at either scale.</summary>
		float FullAccel(double turnRateDegS) { return static_cast<float>(cs::Math::Max(0.2, turnRateDegS * Astro::Deg2Rad / 0.25)); }

		/// <summary>The jets' brightness: drifting, the thrusters are all the hull has; in the dogfight they only trim it.</summary>
		constexpr float DriftIntensity = 1.0f, DogfightIntensity = 0.45f;
	}

	bool UnitySpaceApp::PilotDrifting()
	{
		if (IsPiloting()) return helmFlight->Input.Drift;
		auto f = cs::As<FighterSeat>(HeldSeat());
		cs::Ref<FighterControl> stick;
		return f != nullptr && f->Battle != nullptr && f->Battle->Flown.TryGetValue(f->Unit, stick) && stick != nullptr && stick->Drift.Active;
	}

	void UnitySpaceApp::PilotLookBy(double rightDeg, double upDeg)
	{
		pilotLookOwedRight += rightDeg * Astro::Deg2Rad;
		pilotLookOwedUp += upDeg * Astro::Deg2Rad;
	}

	bool UnitySpaceApp::DriftShowcase(int step, cs::String& failed)
	{
		switch (step)
		{
			case 0:
			case 1:
				DriftHeldByScript = false;
				SpawnShip(step == 0 ? ShipClass::Fighter : ShipClass::Gunship, false);
				if (!IsPiloting()) { failed = "no helm came up"; return false; }
				return true;
			case 2:
			{
				if (!IsPiloting() || !Piloted.IsShip() || Piloted.Ship == nullptr) { failed = "nothing flown"; return false; }
				auto& s = pilot();
				s.Vel = s.Forward * (PilotTuning::Of(Piloted.Ship->Spec->Class).MaxSpeed * 0.8);
				return true;
			}
			default:
				failed = "no such step";
				return false;
		}
	}

	void UnitySpaceApp::UpdatePilotRcs(float dt)
	{
		cs::Ref<Transform> on;
		Bounds box;
		DVec3 forward, up, spin;
		double turnDegS = 0;
		bool drifting = false;
		if (IsPiloting() && PilotControlMode != PilotControl::Helm && Piloted.IsShip() && Piloted.Ship != nullptr)
		{
			// A fighter's helm (light or heavy, a bomber; a hauler's too, its thrusters trimming its swing): its ship view.
			for (const auto& v : shipViews)
				if (v != nullptr && v->Ship == Piloted.Ship)
				{
					if (v->Shown() && v->Root)
					{
						on = v->Root->transform;
						box = v->HullBox();
					}
					break;
				}
			const auto& s = helmFlight->State;
			forward = s.Forward;
			up = s.Up;
			spin = s.AngularVelocity;
			turnDegS = PilotTuning::Of(Piloted.Ship->Spec->Class).TurnRateDegS;
			drifting = helmFlight->Input.Drift;
		}
		else if (auto f = cs::As<FighterSeat>(HeldSeat()))
		{
			// A launched craft's seat: its model among the battle's craft.
			cs::Ref<FighterControl> stick;
			if (f->Unit != nullptr && f->Battle != nullptr && f->Battle->Flown.TryGetValue(f->Unit, stick) && stick != nullptr && fighterViews != nullptr)
			{
				on = fighterViews->ModelOf(f->Unit);
				forward = f->Unit->Forward;
				up = stick->Up;
				spin = stick->AngularVelocity;
				auto craft = f->Unit->Fighter != nullptr ? FighterCraft::Of(f->Unit->Fighter->Role) : FighterCraft::Interceptor();
				turnDegS = craft != nullptr ? craft->Tuning(1).TurnRateDegS : 90;
				drifting = stick->Drift.Active;
			}
		}
		if (!on || forward.LengthSquared() < 1e-12)
		{
			if (pilotRcs != nullptr) pilotRcs->Hide();
			return;
		}
		if (pilotRcs == nullptr || pilotRcs->Parent() != on)
		{
			if (pilotRcs != nullptr) pilotRcs->Hide();
			pilotRcs = cs::New<Views::RcsJets>(on, box);
		}
		auto f = forward.Normalized();
		auto u = (up - f * DVec3::Dot(up, f));
		u = u.LengthSquared() > 1e-12 ? u.Normalized() : PilotState::LevelUp(f);
		auto r = DVec3::Cross(f, u).Normalized();
		// The spin about the hull's own axes: about the starboard axis lifts the nose, about the roof swings it to port
		// (so starboard is its negative), about the nose rolls it clockwise from behind.
		float pitchUp = static_cast<float>(DVec3::Dot(spin, r));
		float yawRight = static_cast<float>(-DVec3::Dot(spin, u));
		float rollClockwise = static_cast<float>(DVec3::Dot(spin, f));
		pilotRcs->Update(pitchUp, yawRight, rollClockwise, FullAccel(turnDegS), drifting ? DriftIntensity : DogfightIntensity, true, dt, Time::unscaledTime);
	}
}
