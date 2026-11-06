#pragma once
// Port of Unity.SpaceGod Runtime/Views/LaneView.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class Lane;
	class Ship;
}

namespace SpaceGod::Views
{
	class LaneTunnel;

	/// <summary>
	/// Draws the hyperlane network: each lane a glowing ribbon along its curve between its two ramps,
	/// a ring gate at each ramp, and pulses of light streaming both ways so it reads as a highway.
	/// Everything is a few pixels wide at any distance, so the network shows from across the system.
	/// Up close each ring gate is drawn at its true size (<see cref="Hyperlanes.GateRadiusKm"/>), square
	/// to its lane, for a pilot to fly through; it flashes as a hull goes in or comes out (U.SPC-T41).
	/// Between the ramps each lane is a chain of rings (U.SPC-T379, <see cref="RingChain"/>), and the spurs
	/// that lead the network on to the jump gates (<see cref="Hyperlanes.GateSpurs"/>) are drawn the same
	/// way in amber. Each lane is also drawn as a tunnel of light round its rings (US-T195, <see cref="LaneTunnel"/>), at the
	/// tunnel's drawn radius (<see cref="TubeRadius"/>), which the rings share.
	/// </summary>
	class LaneView final
	{
		static constexpr int Samples = 48;
		static constexpr int RingSegments = 40;
		static constexpr float FlashSeconds = 0.9f;
		static constexpr Color LaneColor = Color(0.25f, 0.6f, 1.0f);
		static constexpr Color SpurColor = Color(1.0f, 0.7f, 0.3f);
		static constexpr Color PulseColor = Color(0.6f, 0.9f, 1.0f);
		static constexpr Color GateColor = Color(0.45f, 0.8f, 1.0f);

		UnitySpaceApp* app;
		cs::Ref<Mesh> mesh;
		cs::Ref<MeshRenderer> renderer;
		cs::Ref<LaneTunnel> tunnel;
		cs::List<Vector3> vertices;
		cs::List<Color> colors;
		cs::List<Vector3> uvs;
		cs::List<int> indices;
		cs::List<Vector3> curve = cs::NewArray<Vector3>(Samples + 1);

		cs::Ref<Core::Lane> flashLane;
		int flashAt = -1;
		float flashTime = -10.0f;

	public:
		/// <summary>Lights up the ring gate at <paramref name="at"/>'s end of <paramref name="lane"/> for a moment.</summary>
		void Flash(const cs::Ref<Core::Lane>& lane, int at);

	private:
		float FlashOf(const cs::Ref<Core::Lane>& lane, int at, float now) const;

	public:
		LaneView(UnitySpaceApp* app, const cs::Ref<Transform>& parent);

		void Update(bool visible);

		/// <summary>
		/// The lane tube's drawn radius along <paramref name="lane"/> as drawn now (scene units, after <see cref="Update"/> has
		/// sampled its curve): the true <see cref="Hyperlanes.GateRadiusKm"/> mapped along the lane, opened up at the gameplay
		/// scale to <see cref="LaneTunnel.DrawnRadius"/> so the tunnel is wider than the hulls riding it (US-T195).
		/// </summary>
		float TubeRadius(const cs::Ref<Core::Lane>& lane);

		/// <summary>The player's hull when it is riding <paramref name="lane"/> at the helm, else null.</summary>
		cs::Ref<Core::Ship> Rider(const cs::Ref<Core::Lane>& lane);

	private:
		/// <summary>
		/// A ramp's ring gate: a hoop square to the lane, its true radius as mapped near its planet (never
		/// under a few pixels), brightening and swelling for a moment when it flashes. Returns its true drawn radius
		/// (before the pixel floor and the flash), which the tunnel funnels into.
		/// </summary>
		float Gate(const cs::Ref<Core::Lane>& lane, int at, Core::DVec3 rampKm, Vector3 centre, Vector3 inward, float now);

		/// <summary>
		/// The lane's chain of rings (U.SPC-T379): <see cref="Hyperlanes.RingCount"/> hoops of the ring gates' size strung
		/// evenly between the ramps, square to the lane, which accelerate a rider through the first half and brake it
		/// through the second. Rendered only where they can be seen: a ring under a couple of pixels across is skipped,
		/// so from across the system the lane is its ribbon and only up close (riding it, or flying near) does it read as
		/// a chain of hoops. The lane being ridden at the helm strings its rings along the rider's own curve, so the hull
		/// threads them, and the rings just flown through glow.
		/// </summary>
		void RingChain(const cs::Ref<Core::Lane>& lane, Color color, float now, float ringRadius);

		/// <summary>A hoop of ribbon square to <paramref name="axis"/>.</summary>
		void Hoop(Vector3 centre, Vector3 axis, float radius, float width, Color color);

		Vector3 Along(float s);

		float PixelSize(Vector3 p, float pixels);

		void Ribbon(Vector3 a, Vector3 b, float pixels, Color color);

		void Sprite(Vector3 c, float pixels, Color color, float shape);

		void Corners(Color color, float shape, float u0, float u1);

		void Indices(int i);
	};
}
