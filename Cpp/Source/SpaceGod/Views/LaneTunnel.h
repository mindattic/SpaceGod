#pragma once
// US-T195: the hyperlanes drawn as highway tunnels (X3 / X Rebirth's highways, in the streaming blue-white and violet
// of a warp-field effect), on top of LaneView's ribbons and ring chains.

#include "UnityCompat/UnityRenderingTypes.h"
#include "ScaleModel.h"

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
	/// <summary>
	/// Each hyperlane as a translucent tube of light (US-T195): seen from outside, a glowing tube whose edges burn
	/// brightest where the eye grazes its wall, with bright seams running its length and rippling rings of light rolling
	/// along it; from inside, the same rings rush past and blue-white and violet streaks stream by on the walls, faster
	/// with the ride's speed. Entirely procedural: one additive dynamic mesh on the Fx material, vertex colours and the
	/// Fx beam shape (no textures, no third-party art).
	///
	/// The true tube (<see cref="Hyperlanes.GateRadiusKm"/>, 1500 km) mapped along a compressed gameplay-scale lane
	/// draws smaller than a fighter, so the tunnel is drawn at <see cref="DrawnRadius"/>: the true radius at the
	/// realistic scale, opening to <see cref="GameTubeRadius"/> scene units (a few carrier lengths at the hardware
	/// scale) at the gameplay scale. Near each ramp it funnels into that ramp's ring gate. A tunnel under a pixel or so
	/// across is not drawn at all (LaneView's ribbon carries the lane from afar).
	///
	/// LaneView feeds it each lane's drawn curve; the lane the player is riding is resampled densely round the hull
	/// along the rider's own curve (the one the hull is placed on), so the camera rides inside a smooth tube at both
	/// scales. Anything else drawn in the tube's cross-section (an in-lane hull offset, US-T196) maps true km to scene
	/// units by <c>DrawnRadius(trueRadius, blend) / Hyperlanes::GateRadiusKm</c> so it meets the drawn wall.
	/// </summary>
	class LaneTunnel final
	{
	public:
		/// <summary>The tunnel's drawn radius at the gameplay scale, scene units: 3.5 km at the hardware scale.</summary>
		static constexpr float GameTubeRadius = static_cast<float>(3.5 * Core::ScaleModel::GameShipScale);

		/// <summary>
		/// The tunnel's drawn radius, scene units, given the true tube radius as mapped along the lane
		/// (<paramref name="trueRadius"/>) and the scale model's blend (0 realistic, 1 gameplay): never under the true one.
		/// </summary>
		static float DrawnRadius(float trueRadius, double blend);

		LaneTunnel(UnitySpaceApp* app, const cs::Ref<Transform>& parent);

		/// <summary>Starts a frame's tunnels (hidden, and nothing drawn, when not <paramref name="visible"/>).</summary>
		void Begin(bool visible);

		/// <summary>
		/// One lane's tunnel along <paramref name="curve"/> (<paramref name="samples"/> + 1 points, A to B, camera
		/// relative), <paramref name="radius"/> its drawn radius, funnelling into ring gates of
		/// <paramref name="gateA"/> and <paramref name="gateB"/> at its ends (0: no funnel). <paramref name="rider"/> is
		/// the player's hull riding it, or null.
		/// </summary>
		void Draw(const cs::Ref<Core::Lane>& lane, const cs::List<Vector3>& curve, int samples, float radius, float gateA, float gateB,
			const cs::Ref<Core::Ship>& rider, float now);

		/// <summary>Hands the frame's tunnels to the mesh.</summary>
		void End();

	private:
		static constexpr int RideDense = 72;
		static constexpr int AmbientStreaks = 28;
		static constexpr int RideStreaks = 150;
		static constexpr float RideWindowRadii = 70.0f;

		UnitySpaceApp* app;
		cs::Ref<Mesh> mesh;
		cs::Ref<MeshRenderer> renderer;
		bool shown = false;
		cs::List<Vector3> vertices;
		cs::List<Color> colors;
		cs::List<Vector3> uvs;
		cs::List<int> indices;

		// The lane being drawn, sample by sample: fraction along it (from its first end), centre, frame, radius, and
		// distance along it in scene units.
		cs::List<float> fr;
		cs::List<Vector3> pts, us, vs;
		cs::List<float> rs, ds;
		// The ridden lane's coarse pass along the rider's curve, before the window round the hull is filled in densely.
		cs::List<float> coarseFr;
		cs::List<Vector3> coarsePts;

		struct Point
		{
			Vector3 centre, u, v;
			float radius;
		};

		/// <summary>The tube at <paramref name="f"/> along the samples (clamped to its ends).</summary>
		Point At(float f) const;

		/// <summary>Frames, radii and lengths for the samples in <see cref="pts"/>.</summary>
		void Frame(float radius, float gateA, float gateB);

		/// <summary>The tube's wall; returns its largest radius on screen, pixels (0: too thin to draw).</summary>
		float Wall(bool spur, float now, float rideSpeed, bool inside);

		void Streak(float f0, float f1, float angle, float depth, Color color);

		float PixelSize(Vector3 p, float pixels) const;

		static float Hash(float n);
	};
}
