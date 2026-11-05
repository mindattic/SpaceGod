#pragma once
// Port of Unity.SpaceGod Runtime/Views/Starfield.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod::Views
{
	/// <summary>
	/// A point-cloud sky: uniformly scattered stars plus a denser band along the galactic plane
	/// (galactic north pole at RA 192.86°, Dec 27.13°, so the Milky Way sits where it really does).
	/// </summary>
	// C#'s static class: a class of static members only.
	class Starfield final
	{
		/// <summary>The menu's sky material, made once and shared: the menu scene has no teardown of its own to free one.</summary>
		static cs::Ref<Material>& sharedMaterial();

	public:
		Starfield() = delete;

		static cs::Ref<GameObject> Create(const cs::Ref<Transform>& parent, int count = 9000, int seed = 7, bool shared = false);

	private:
		static Vector3 RandomDirection(cs::Random& rng);
	};
}
