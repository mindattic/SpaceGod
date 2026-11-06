#pragma once
// US-T165: the attitude (RCS) thrusters on a flown hull, firing as it swings.

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod::Views
{
	/// <summary>
	/// A hull's reaction-control thrusters (US-T165), drawn as short white-blue jets: a quad of them at the bow and at the
	/// stern (up, down, port, starboard) and a pair under and over each beam. Each fires when, and as hard as, the hull's
	/// spin is changing the way that jet would push it -- spinning up into a turn or braking out of it, in pitch, yaw or
	/// roll -- so a steady swing is quiet and a snap or a stop flares at both ends, opposite ways, as real thrusters do.
	/// Parented to the hull's view (a <see cref="ShipView"/> root, one unit long), so they scale with it at either scale.
	/// </summary>
	class RcsJets final
	{
	public:
		/// <summary>Builds the jets on <paramref name="parent"/> (a hull's root, one unit long) round <paramref name="hull"/>,
		/// the box of the hull as drawn in that frame.</summary>
		RcsJets(const cs::Ref<Transform>& parent, Bounds hull);

		/// <summary>The hull's root the jets were built on: rebuild them when the flown hull's view is another.</summary>
		cs::Ref<Transform> Parent() const { return parent; }

		/// <summary>
		/// One frame: <paramref name="pitchUp"/>, <paramref name="yawRight"/> and <paramref name="rollClockwise"/> are the
		/// hull's spin about its own axes, rad/s (nose up, nose to starboard, clockwise from behind). The jets fire on its
		/// change since the last frame, full at <paramref name="fullAccel"/> rad/s^2; <paramref name="intensity"/> scales
		/// their brightness (drifting, the thrusters are all the hull has, and burn bright). <paramref name="show"/> false
		/// puts them all away.
		/// </summary>
		void Update(float pitchUp, float yawRight, float rollClockwise, float fullAccel, float intensity, bool show, float dt, float now);

		/// <summary>Puts every jet away and forgets the last spin, so the next shown frame does not flare from a stale one.</summary>
		void Hide();

	private:
		struct Jet
		{
			cs::Ref<Transform> T;
			cs::Ref<MeshRenderer> R;
			/// <summary>How it pushes the hull: its share of nose-up, nose-to-starboard and clockwise acceleration (-1, 0, 1).</summary>
			float Pitch = 0, Yaw = 0, Roll = 0;
			float Level = 0, Phase = 0, Width = 0, Length = 0;
		};

		cs::Ref<Transform> parent;
		cs::List<Jet> jets;
		float lastPitch = 0, lastYaw = 0, lastRoll = 0;
		bool hasLast = false;
		/// <summary>The spin's change, smoothed a little so one frame's jitter does not strobe the jets.</summary>
		float accPitch = 0, accYaw = 0, accRoll = 0;

		void Add(Vector3 at, Vector3 exhaust, float pitch, float yaw, float roll, float size);

		/// <summary>The jets' glow, built once and shared by every hull's.</summary>
		static cs::Ref<Material> JetMaterial();
	};
}
