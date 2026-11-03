#pragma once
// Port of Unity.SpaceGod Runtime/Views/PodBeacons.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class EscapePod;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// Every drifting cluster of escape pods (<see cref="Crews.Pods"/>): a handful of small capsules tumbling
	/// slowly where their hull died, and a distress beacon that blinks a few pixels wide however far off it is,
	/// so a pilot can find them. Drawn at the battle scale, where the fight that launched them was drawn;
	/// gone once they are picked up or their air runs out.
	/// </summary>
	class PodBeacons final
	{
		class View final
		{
		public:
			cs::List<cs::Ref<Transform>> Capsules;
			cs::Ref<Transform> Beacon;
		};

		UnitySpaceApp* app;
		cs::Ref<Transform> parent;
		cs::Dictionary<cs::Ref<Core::EscapePod>, cs::Ref<View>> views;
		cs::List<cs::Ref<Core::EscapePod>> gone;
		MaterialPropertyBlock block;
		static constexpr const char* ColorId = "_Tint"; // Shader.PropertyToID("_Tint")

		// C#'s statics `static Mesh capsule, quad; static Material beaconMaterial;`: shared by every PodBeacons.
		static cs::Ref<Mesh>& capsule();
		static cs::Ref<Mesh>& quad();
		static cs::Ref<Material>& beaconMaterial();

		/// <summary>The most capsules drawn for one cluster, however many it launched.</summary>
		static constexpr int MaxCapsules = 6;

		/// <summary>A pod's drawn length, km (a lifeboat capsule, a little larger than life so it reads beside its hull).</summary>
		static constexpr double PodLengthKm = 0.02;

	public:
		PodBeacons(UnitySpaceApp* app, const cs::Ref<Transform>& parent);

		void Update();

	private:
		cs::Ref<View> Create(const cs::Ref<Core::EscapePod>& p);
	};
}
