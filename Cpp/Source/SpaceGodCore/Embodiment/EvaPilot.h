#pragma once
// US-T166: eject and EVA. A fighter's pilot out of the cockpit in a pressure suit, an embodiable of its own: suit
// thrusters in open space, a magnetic latch onto a torpedo in flight, a mag-boot walk along it to the warhead, and the
// warhead's safing done under the clock (the torch timing of US-T135's repair weld) before it strikes.

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Embodiment/Embodiment.h"
#include "Embodiment/RepairWorker.h" // WeldTiming: the disarm's tap timing is the weld's

namespace SpaceGod::Core
{
	class Battle;
	class BattleUnit;
	class Missile;

	/// <summary>
	/// A pilot ejected from a launched craft (US-T166), as an <see cref="IEmbodiable"/>. <see cref="Eject"/> blows the canopy
	/// on a cockpit the player holds: the craft is left abandoned (<see cref="FighterControl.Abandoned"/>), coasting on its
	/// momentum with nobody at its stick, and the pilot floats free beside it with the craft's velocity and a kick off the
	/// seat. Free, the suit's thrusters fly it: W/S A/D Q/E thrust along the look, Shift the jets at full, all on a small
	/// delta-v budget (<see cref="SuitDeltaVKmS"/>). Within <see cref="LatchReachKm"/> of a torpedo (or an antimatter torpedo)
	/// and slow enough against it (<see cref="LatchSpeedKmS"/>), the left mouse fires the magnetic tether: the pilot rides it,
	/// mag boots on its casing at the tail. W/S walk the casing; at the warhead (<see cref="WarheadAt"/>) the left mouse works
	/// the safing, timed as the repair weld is (<see cref="WeldTiming"/>): each tap in the bright zone clears one of the
	/// warhead's <see cref="SafingSteps"/>, a miss undoes one and stalls the hands a moment. All of them cleared before it
	/// strikes, it is disarmed: the round goes dead (<see cref="Missile.Gone"/>, no damage done) and the pilot floats free on.
	/// If it strikes first the pilot goes with it; if it is shot down or burns out under them they are thrown clear. Right
	/// mouse lets go of it. Battle km and seconds throughout (a torpedo is no hull, so there is no scale to read): the time
	/// controls' slow motion (<see cref="SimClock.Faster"/> / <see cref="SimClock.Slower"/>, down to a near-freeze) are what
	/// make it playable. Only ever stepped while held, by the session as any seat: crews, the AI and the Bench never see it.
	/// Not saved, as a worker is not: a load drops the player back in command, and the craft it left drifts on.
	/// </summary>
	class SPACEGODCORE_API EvaPilot final : public IEmbodiable
	{
	public:
		cs::Ref<Core::Battle> Battle;

		/// <summary>The craft it ejected from, left coasting (null once gone from the fight it is still the craft named).</summary>
		cs::Ref<BattleUnit> Craft;

		/// <summary>Who it flew for, and its name ("Valiant fighter 1-3 pilot").</summary>
		int Side = 0;
		cs::String Callsign;

		/// <summary>Where it is and how fast, battle frame (km, km/s); which way the suit faces and its roof.</summary>
		DVec3 Pos, Vel, Facing = DVec3(0, 0, 1), Up = DVec3(0, 1, 0);

		/// <summary>The suit's thrusters' delta-v left, km/s.</summary>
		double DeltaV = 0;

		/// <summary>The torpedo it is latched onto, or null when free; where on its casing (0 the tail, 1 the nose).</summary>
		cs::Ref<Missile> Latched;
		double Along = 0;

		/// <summary>The warhead's safing: the steps cleared (of <see cref="SafingSteps"/>), and the tap timing it is done to.</summary>
		int Cleared = 0;
		WeldTiming Safing;

		/// <summary>Torpedoes disarmed while held, and the last thing that happened, for the HUD (and how long it shows).</summary>
		int Disarmed = 0;
		cs::String Event;
		double EventS = 0;

		/// <summary>Killed when the round it rode struck (or the fight ended under it): why, for the session's toast.</summary>
		cs::String Lost;

		/// <summary>The nearest torpedo it could latch onto, its range (km) and closing speed against the suit (km/s).</summary>
		cs::Ref<Missile> Nearest;
		double NearestKm = 0, NearestRelKmS = 0;

		static constexpr double SuitAccelKmS2 = 0.012;
		static constexpr double JetFactor = 3.0;
		static constexpr double SuitDeltaVKmS = 0.35;

		/// <summary>The seat's kick on the way out (km/s), along the craft's roof.</summary>
		static constexpr double EjectKickKmS = 0.015;

		/// <summary>How far the tether reaches (km), and the most the suit may be moving against the round to hold on (km/s).</summary>
		static constexpr double LatchReachKm = 0.12;
		static constexpr double LatchSpeedKmS = 0.45;

		/// <summary>A torpedo's casing, km (the walk is in shares of it), the walk's pace (casing a second, Shift faster),
		/// and where the warhead's access panel starts.</summary>
		static constexpr double TorpedoLengthKm = 0.009;
		static constexpr double WalkPerS = 0.35;
		static constexpr double WarheadAt = 0.85;
		static constexpr double LatchAlong = 0.1;

		/// <summary>The warhead's safing steps: arming plug, fuse, booster, detonator.</summary>
		static constexpr int SafingSteps = 4;

		EvaPilot(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& craft);

		/// <summary>
		/// Blows the canopy on <paramref name="craft"/>, flown by the player: the craft is left abandoned, coasting on its
		/// momentum (<see cref="FighterControl.Abandoned"/>), and the pilot is returned, floating clear of it, for the player to
		/// take (taking it hands the cockpit back, which leaves an abandoned craft as it is). Null, with <paramref name="why"/>,
		/// when it cannot: not a craft the player flies, or the fight over. <see cref="Unabandon"/> undoes it if the take fails.
		/// </summary>
		static cs::Ref<EvaPilot> Eject(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& craft, cs::String& why);

		/// <summary>Puts the pilot back on a craft whose ejection was not taken up (the take refused): still at its controls
		/// (<paramref name="stillHeld"/>) the player flies on as before; otherwise its own pilot flies it on.</summary>
		static void Unabandon(const cs::Ref<Core::Battle>& battle, const cs::Ref<BattleUnit>& craft, bool stillHeld);

		/// <summary>Whether <paramref name="m"/> is a round a pilot can ride and disarm: a torpedo or an antimatter torpedo.</summary>
		static bool Rideable(const cs::Ref<Missile>& m);

		/// <summary>Seconds before <paramref name="m"/> reaches its target at its closing speed now, or -1 when it has none.</summary>
		static double SecondsToImpact(const cs::Ref<Missile>& m);

		cs::String Name() const override;
		EmbodimentKind Kind() const override { return EmbodimentKind::EvaPilot; }
		cs::Ref<Core::Faction> Faction() const override;
		cs::Ref<IEmbodiable> Host() const override { return nullptr; }
		bool CanEmbody() const override { return Blocker().empty(); }
		cs::String Blocker() const override;
		bool Embodied() const override { return held; }
		EmbodimentPose PoseAt(double jd) override;
		void TakeCharge() override { held = true; }
		void Control(const EmbodimentInput& input, double dt) override;
		void HandBack() override { held = false; }

		EmbodimentInputProfile InputProfile() const override { return EmbodimentInputProfile::Eva; }
		EmbodimentCameraRig CameraRig() const override;
		EmbodimentRolePanel RolePanel() override;
		EmbodimentSubject Subject() const override;

		/// <summary>The torpedo's casing direction (tail to nose), unit.</summary>
		static DVec3 AxisOf(const cs::Ref<Missile>& m);

	private:
		bool held = false;
		bool secondaryWas = false, primaryWas = false;

		/// <summary>A friendly unit still in the fight to stand for it in the world when its craft is gone.</summary>
		cs::Ref<BattleUnit> Anchor() const;

		void Note(cs::String what);
		void Fly(const EmbodimentInput& input, double dt, bool press);
		void Ride(const EmbodimentInput& input, double dt, bool press);
		void LetGo(cs::String what);
		void ScanNearest();
	};
}
