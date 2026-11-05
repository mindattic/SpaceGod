#pragma once
// Port of Unity.SpaceGod Runtime/Camera/StarTravel.cs (US-T29).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "UnityCompat/UnityTypes.h"
#include "Systems/GateJump.h" // GateJump::Phase, held by value
#include "Streaming.h"        // GateCover, made in a member initialiser

namespace SpaceGod::Core
{
	class StarSystem;
	class JumpGate;
}

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Cameras
{
	/// <summary>
	/// The camera's trip from one star system to another. Where the system left has a jump gate on the way, the
	/// camera goes through it (paced and chosen by <see cref="GateJump"/>):
	///  1. Leave: down to the gate that leads toward the destination, lined up on its ring with the star behind,
	///     then a plunge straight through it.
	///  2. Cross: a short spell inside the gate, warp lines streaming past and nothing of either system on screen.
	///  3. Arrive: out of the partner gate in the destination, looking back at it and its star, pulling back to the
	///     system's overview.
	/// Where there is no gate, the old way across, in the same three legs:
	///  1. Leave: pull straight back from the system until it shrinks to a point.
	///  2. Cross: in the interstellar view, turn toward the destination star and glide across the
	///     light-years between (eased in and out), with the neighbouring stars sliding past.
	///  3. Arrive: the destination system takes over and the camera falls in from far out to its overview.
	/// The simulation of both systems carries on throughout; this is only the view moving.
	/// </summary>
	class StarTravel final
	{
	public:
		enum class Leg : int
		{
			Leave,
			Cross,
			Arrive,
		};

		bool Active = false;
		Leg Stage = Leg::Leave;
		cs::Ref<Core::StarSystem> From;
		cs::Ref<Core::StarSystem> To;

		/// <summary>Whether this trip goes through the jump gates (false: across the interstellar view).</summary>
		bool ThroughGate = false;

		/// <summary>The view's position between the stars (light-years) during the crossing.</summary>
		Core::DVec3 CameraLy;

		/// <summary>A line for the HUD describing what is happening.</summary>
		// C# null (no trip) is the empty string here.
		cs::String Banner;

		/// <summary>0..1 through the whole trip.</summary>
		float Overall() const;

		/// <summary>How hard the view is going to warp, 0..1: building as the system falls away, swelling with the glide
		/// across, dying away on arrival. Drives the warp lines and stellar dust.</summary>
		float Warp() const;

		/// <summary>The flash of going through a gate's ring, 0..1 (the HUD washes the screen with it).</summary>
		float GateFlash() const;

		/// <summary>The glow of the inside of a gate, 0..1 (the HUD tints the screen with it).</summary>
		float GateGlow() const;

		/// <summary>How much wider than usual the view opens going through a gate, degrees (narrower in the fold).</summary>
		float FovKick() const;

		/// <summary>Whether the tunnel is being held open while the far side finishes loading (U.SPC-T414).</summary>
		bool HoldingForLoad() const;

	private:
		/// <summary>Where the effects stand: in the tunnel, its cover's progress (standing still while held for the far
		/// side, see <see cref="GateCover"/>); elsewhere the phase's own.</summary>
		float EffectT() const;

		/// <summary>The tunnel's cover: its own length, held at its height while the far side streams in.</summary>
		const cs::Ref<Core::GateCover> tunnel = cs::New<Core::GateCover>();

	public:
		/// <summary>How near the camera is to the ring of the gate it is going through (scene units), for the near
		/// clip plane; infinite when no ring is near.</summary>
		double GateClearance() const;

		/// <summary>The way the crossing heads, ecliptic: from the star left toward the one bound for, or through a gate
		/// the way the view looks.</summary>
		Core::DVec3 Heading() const;

	private:
		static constexpr float LeaveSeconds = 2.2f, CrossSeconds = 3.8f, ArriveSeconds = 3.4f;

		/// <summary>The crossing starts and ends this far from each star (light-years).</summary>
		static constexpr double StandOffLy = 0.22;

		/// The app outlives its trip (it owns it).
		UnitySpaceApp* const app;
		float t = 0;
		Core::DVec3 leaveStart;
		double leaveFrom = 0, leaveTo = 0;
		Quaternion startRotation, crossStartRotation;
		Vector3 forward;
		Core::DVec3 crossStart, crossEnd;
		Core::DVec3 arriveDirection;
		double arriveFrom = 0, arriveTo = 0;
		Quaternion arriveEndRotation;
		float arriveCorona = 0;

		/// <summary>Through the gates: the gate left by, the one come out of, where in the trip the view is, and how near
		/// it is to a ring.</summary>
		cs::Ref<Core::JumpGate> departure, arrival;
		Core::GateJump::Phase phase = Core::GateJump::Phase::Approach;
		double gateClearance = cs::Double::MaxValue;

		/// <summary>The gates the trip takes, in order, and the one being taken now (U.SPC-T516): every system on the way is
		/// crossed to its next gate and every gate is gone through, never a jump to the far system.</summary>
		// A C# List reference: GateJump.TryPlan hands back its own (the planner's) list.
		cs::Ref<cs::List<cs::Ref<Core::JumpGate>>> course = cs::New<cs::List<cs::Ref<Core::JumpGate>>>();
		int leg = 0;

	public:
		/// <summary>How many gates the trip takes (0 across the interstellar view).</summary>
		int Legs() const { return ThroughGate ? course->Count() : 0; }

		/// <summary>Which gate the trip is at, from 0.</summary>
		int LegIndex() const { return leg; }

		/// <summary>The gates of the trip, in order.</summary>
		const cs::List<cs::Ref<Core::JumpGate>>& Course() const { return *course; }

		/// <summary>The system the gate being taken leads to: the next stop, which is the destination only on the last leg.</summary>
		cs::Ref<Core::StarSystem> Hop() const;

		explicit StarTravel(UnitySpaceApp* inApp);

	private:
		/// <summary>The line on the HUD: where bound, how far, the course by the systems on it and the leg being flown.</summary>
		void RefreshBanner();

	public:
		void Begin(const cs::Ref<Core::StarSystem>& to);

	private:
		/// <summary>Whether the destination's views are already built (hidden, part way across).</summary>
		bool destinationBuilt = false;

		/// <summary>Up for the view: the plane left behind, the destination's plane, and the destination's Oort cloud radius (ly).</summary>
		Vector3 fromUp = Vector3::up(), toUp = Vector3::up();
		double oortLy = 0;

		/// <summary>0 outside the destination's Oort cloud, easing to 1 by the end of the crossing.</summary>
		float ReorientShare() const;

		/// <summary>How far out the camera pulls before the system is a point: past its farthest member.</summary>
		static double LeaveSpanKm(const cs::Ref<Core::StarSystem>& s);

	public:
		/// <summary>Skips to the end: the destination's overview.</summary>
		void Finish();

		void Update(float dt);

	private:
		// ---------------------------------------------------------------- through the gates

		void UpdateGate(float dt);

		/// <summary>Down to the gate: swinging round the system's root onto the line of the ring while dropping to just in
		/// front of it, turning to look at it.</summary>
		void UpdateApproach();

		/// <summary>Straight through the ring, gathering speed; at the ring the view is inside the gate.</summary>
		void UpdatePlunge();

		/// <summary>Inside the gate: nothing of either system on screen while the destination is built (hidden), the
		/// view's up swinging to the destination's plane; then out of the partner gate.</summary>
		void UpdateTunnel();

		/// <summary>Where <paramref name="gate"/> (in the active system) is drawn, the way out through its ring (away from
		/// the system's root, along the line to the star it leads to) and how wide the ring is drawn, scene units.</summary>
		void GateScene(const cs::Ref<Core::JumpGate>& gate, Core::DVec3& scene, Core::DVec3& axis, double& ring);

		/// <summary>How far the camera is from the ring of a gate drawn at <paramref name="gate"/>, never less than a tenth
		/// of its width (in its middle the nearest of it is half a width away).</summary>
		double Clearance(Core::DVec3 gate, double ring) const;

		/// <summary>LookRotation that keeps working when the view looks straight along <paramref name="up"/>.</summary>
		// C#'s first parameter is `forward`; renamed since it would shadow the field (UE builds treat shadowing as an error).
		static Quaternion Look(Vector3 lookForward, Vector3 up);

		// ---------------------------------------------------------------- across the interstellar view

		void UpdateLeave(float dt);

		void UpdateCross(float dt);

		/// <summary>Sets up the fall (or pull back) to the overview: from <paramref name="from"/> scene units out along
		/// <paramref name="direction"/> from the destination's root, its star's corona starting at <paramref name="corona"/>
		/// of its usual size.</summary>
		void PrepareArrival(Core::DVec3 direction, double from, float corona);

		/// <summary>How wide, in pixels, the whole destination system is when it first takes over from its star.</summary>
		static constexpr double PointPixels = 6;

		/// <summary>The destination star's corona on taking over, as a share of its usual size: about the star point's.</summary>
		static constexpr float ArrivalCorona = 0.25f;

		/// <summary>Kept well inside the camera's far plane (5e8 scene units).</summary>
		static constexpr double MaxArriveDistance = 2e8;

		Core::DVec3 arriveEndDirection;

		void UpdateArrive(float dt);

		/// <summary>The next leg of the course (U.SPC-T516): from the view as it stands in the system just come into, down to the
		/// gate that leads on, through it and so on, as the first leg did from the system left.</summary>
		void BeginNextLeg();

		// C#'s last parameter is `t`; renamed since it would shadow the field.
		static Core::DVec3 Slerp(Core::DVec3 a, Core::DVec3 b, float share);

		static float Smoother(float x) { return x * x * x * (x * (x * 6 - 15) + 10); }
		static Vector3 ToVector(Core::DVec3 v) { return Vector3(static_cast<float>(v.X), static_cast<float>(v.Y), static_cast<float>(v.Z)); }
	};

	const char* ToString(StarTravel::Leg v);
}
