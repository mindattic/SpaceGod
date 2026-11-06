#pragma once
// US-T191: the hyperspace toll. See HyperspaceToll below.

#include "UnityCompat/UnityGui.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Rendering
{
	/// <summary>
	/// The exposure a view has taken in hyperspace, the one value the toll is drawn from (US-T191). Time in warp grows it
	/// (<see cref="Update"/>'s <c>intensity</c>, 0..1, is how deep in hyperspace the view is this frame); back in normal
	/// space it drains, faster the more there is of it. <see cref="Severity"/> is what the screen shows, 0..1: easing up
	/// quickly at first and creeping towards 1 the longer the exposure lasts (a gate jump leaves a quarter of it, a chain
	/// of gates or a long FTL crossing nearly all).
	/// </summary>
	struct HyperspaceExposure
	{
		/// <summary>Seconds of full exposure stored up.</summary>
		float Seconds = 0;

		/// <summary>The severity drawn (eased toward <see cref="Target"/> so nothing on screen jumps).</summary>
		float Shown = 0;

		/// <summary>Seconds of full exposure that take the severity to 1 - 1/e.</summary>
		static constexpr float GrowSeconds = 28.0f;

		/// <summary>Out of hyperspace the stored seconds drain by this many a second, plus <see cref="DecayShare"/> of what is left.</summary>
		static constexpr float DecayFloor = 1.5f, DecayShare = 0.15f;

		/// <summary>The most stored up (about 0.99 severity): a crossing of any length drains in under half a minute.</summary>
		static constexpr float MaxSeconds = 130.0f;

		void Update(float intensity, float dt);

		/// <summary>The severity the stored seconds come to, 0..1.</summary>
		float Target() const;

		/// <summary>What the screen shows, 0..1.</summary>
		float Severity() const { return Shown; }

		void Reset() { Seconds = 0; Shown = 0; }
	};

	/// <summary>
	/// US-T191: the toll hyperspace takes on whoever looks into it. While the view is with a hull in warp (the player
	/// through a gate at the helm or under the hull's own FTL drive, the camera's trip through the gates or across the
	/// stars, or a ship followed in the god view while it is inside a gate or out between the stars) the exposure grows,
	/// and the screen's edges go dark with branching, writhing veins that creep in toward the centre the longer it lasts:
	/// the colour drains out of the view and its edges split (a post-process volume of its own, over the game's look),
	/// a heartbeat thumps through the darkness and the veins, shapes loom at the edges and, deep in, eyes open there and
	/// turn to the ship. Outer Gods closing in on a ship that breaks the laws of the universe. Back in normal space it
	/// drains away.
	///
	/// Everything is procedural (veins grown from a fixed seed, the eyes and the soft shapes drawn into textures made in
	/// code): no art. Drawn under the HUD, which stays readable. <c>sg.HyperspaceToll 0</c> turns it off (photosensitivity),
	/// <c>sg.HyperspaceTollForce 0..1</c> holds the severity at a value to look at it (-1, the default, lets it run).
	/// Nothing here touches the simulation.
	/// </summary>
	class HyperspaceToll
	{
	public:
		/// <summary>Each frame after the app's update (null in a scene without the app): the exposure grows or drains, and
		/// the post-process volume follows it. Registers the overlay with the GUI host the first time.</summary>
		static void Tick(UnitySpaceApp* app, float dt);

		/// <summary>How deep in hyperspace the app's view is this frame, 0 (normal space) to 1.</summary>
		static float Intensity(UnitySpaceApp& app);

		/// <summary>The severity on screen now, 0..1.</summary>
		static float Severity();

		/// <summary>The heartbeat now, 0..1 (two thumps a beat, quickening with the severity).</summary>
		static float Beat();

		/// <summary>The overlay (the GUI host's handler, Repaint only).</summary>
		static void Draw();
	};
}
