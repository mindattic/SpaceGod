#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Infrastructure/Satellite.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class BodyDefinition;
	class Constellation;

	/// <summary>The player's hand on a satellite while it is embodied (see <see cref="SatelliteSeat"/>), in world terms.</summary>
	struct SatelliteCommand
	{
		/// <summary>Where the player wants the boresight (unit, ecliptic axes); zero holds the current pointing.</summary>
		DVec3 Aim;

		/// <summary>Thrust along the boresight, to its right and along its roof, each -1..1, and roll about the boresight.</summary>
		double Throttle = 0, StrafeRight = 0, StrafeUp = 0, Roll = 0;
	};

	/// <summary>
	/// One satellite of a <see cref="Constellation"/> flying on its own state (U.SPC-T448): a position and velocity about its
	/// body (km, km/s, ecliptic axes) under the body's gravity, and an attitude (the boresight its sensors and relay antenna
	/// look down, and its roof). A constellation's satellites sit exactly in their Walker slots until one is taken or
	/// knocked off; only then does it carry a state of its own, stepped here.
	/// <para>
	/// Held by the player (<see cref="Held"/>), it turns toward the player's aim at <see cref="SlewDegS"/> and its thrusters
	/// push it along its own axes at up to <see cref="ThrustKmS2"/>; nothing else acts on it but gravity. Handed back, its
	/// station-keeping AI flies it home to its slot from wherever it was left: a closing speed it can still brake from,
	/// trimmed to the slot's orbit, and a slew back to its working attitude (boresight on the ground below, roof along
	/// its track). Back on station, it is placed in its slot again and the constellation lets the state go.
	/// </para>
	/// </summary>
	// (The constellation holds a flying satellite in Flying and the satellite holds its constellation: a reference cycle
	// while it flies, broken when the constellation lets it go back on station.)
	class SPACEGODCORE_API Satellite final
	{
	public:
		/// <summary>Top thrust of its station-keeping engines, km/s² (2 m/s², a game-scale figure, not a real ion drive's).</summary>
		static constexpr double ThrustKmS2 = 0.002;

		/// <summary>How fast it turns its boresight and rolls, deg/s.</summary>
		static constexpr double SlewDegS = 20, RollDegS = 30;

		/// <summary>Half-width of its sensor field and relay beam about the boresight, degrees.</summary>
		static constexpr double BeamHalfDeg = 15;

		/// <summary>Longest integration step, seconds.</summary>
		static constexpr double MaxStepSeconds = 1;

		/// <summary>A gap longer than this (s) with nobody holding it: its AI has long since put it back on station.</summary>
		static constexpr double MaxCatchUpSeconds = 20000;

	private:
		/// <summary>Station-keeping: the closing-speed ramp near the slot (1/s) and how hard it chases that speed (1/s).</summary>
		static constexpr double ClosingRate = 0.02, ChaseGain = 0.1;

		/// <summary>Within these of its slot (km, km/s, degrees) it is on station.</summary>
		static constexpr double StationKm = 0.01, StationKmS = 1e-3, StationDeg = 1;

	public:
		cs::Ref<SpaceGod::Core::Constellation> Constellation;
		cs::Ref<BodyDefinition> Body;

		/// <summary>Its place in the constellation (the order of <see cref="Constellation.Offsets"/>).</summary>
		int Index = 0;

		/// <summary>The date its state is at.</summary>
		double Jd = 0;

		/// <summary>From the body's centre, km, and its velocity there, km/s (ecliptic axes, not turning with the body).</summary>
		DVec3 OffsetKm, VelocityKmS;

		/// <summary>Unit boresight (where its sensors and relay antenna point) and roof.</summary>
		DVec3 Forward, Up;

	private:
		bool held = false;

	public:
		/// <summary>Whether the player holds it (its AI stands aside). Taking or handing it back puts it off station until
		/// its AI has it settled in its slot again.</summary>
		bool Held() const { return held; }
		void SetHeld(bool value)
		{
			held = value;
			OnStation = false;
		}

		/// <summary>What the player last asked of it, while <see cref="Held"/>.</summary>
		SatelliteCommand Command;

		/// <summary>The thrust it put out on its last step, km/s².</summary>
		double LastThrustKmS2 = 0;

		/// <summary>Back in its slot at its working attitude, with nobody holding it.</summary>
		bool OnStation = false;

		Satellite(cs::Ref<SpaceGod::Core::Constellation> constellation, cs::Ref<BodyDefinition> body, int index, double jd);

		cs::String Name() const;

		/// <summary>Unit vector to its right (boresight × roof).</summary>
		DVec3 Right() const { return DVec3::Cross(Forward, Up); }

		/// <summary>How far it is from its slot at its state's date, km.</summary>
		double SlotErrorKm() const;

		/// <summary>
		/// The working attitude at a place in orbit: boresight straight down at the body, roof along the track
		/// (the velocity, squared off to the boresight).
		/// </summary>
		static void WorkingAttitude(DVec3 offsetKm, DVec3 velocityKmS, DVec3& forward, DVec3& up);

		/// <summary>Puts it in its slot at <paramref name="jd"/>, at its working attitude.</summary>
		void ReturnToSlot(double jd);

		/// <summary>
		/// Steps it on to <paramref name="jd"/>: under the player's command while <see cref="Held"/>, under its
		/// station-keeping AI otherwise. A date at or before its own leaves it as it is.
		/// </summary>
		void AdvanceTo(double jd);

	private:
		DVec3 Gravity(DVec3 r) const;

		/// <summary>One step of <paramref name="h"/> seconds from <paramref name="jd"/> (semi-implicit Euler).</summary>
		void Step(double jd, double h);

		/// <summary>
		/// The station-keeping thrust: a trim that gives it the slot's own acceleration (so it moves with the slot rather
		/// than on an orbit of its own), and up to <see cref="ThrustKmS2"/> more to close on the slot at a speed it can brake
		/// from, slowing as it comes in.
		/// </summary>
		DVec3 KeepStation(double jd, DVec3 gravity) const;

		bool Settled(double jd) const;

		/// <summary>Turns the boresight toward <paramref name="aim"/> (unit) by at most its slew rate, carrying the roof with it.</summary>
		void Slew(DVec3 aim, double h);

		void Roll(double radians);

		/// <summary>Rolls the roof toward <paramref name="up"/> (squared off to the boresight) at no more than its roll rate.</summary>
		void RollToward(DVec3 up, double h);

	public:
		/// <summary>
		/// Whether a point (km from the body's centre) lies in its sensor field and relay beam: within
		/// <see cref="BeamHalfDeg"/> of the boresight and not hidden behind the body.
		/// </summary>
		bool Covers(DVec3 pointKm) const;

	private:
		static double Clamp(double x) { return cs::Math::Max(-1.0, cs::Math::Min(1.0, x)); }

		static double AngleDeg(DVec3 a, DVec3 b);

	public:
		cs::String ToString() const { return Name(); }
	};
}
