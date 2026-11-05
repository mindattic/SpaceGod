#pragma once
// US-T95: the drifting cargo pods and the tractor beam, drawn.

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class CargoPod;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// Every drifting cargo pod (<see cref="Core.CargoPods"/>): a small tumbling canister, amber (red for stolen goods), with a
	/// faint blinking marker a few pixels wide so a pilot can find them in a debris field; and the tractor beam while it
	/// holds one: a pulsing green-white lance from the hull to the pod, rings running down it toward the hull, a glow on the
	/// pod. Placed where the app's <c>CargoPodScene</c> puts each pod (anchored like a wreck, at the battle scale), so it
	/// reads the same at the realistic and the game scale.
	/// </summary>
	class CargoPodsView final
	{
		UnitySpaceApp* app;
		cs::Ref<Transform> parent;
		cs::Dictionary<cs::Ref<Core::CargoPod>, cs::Ref<Transform>> views;
		cs::List<cs::Ref<Core::CargoPod>> gone;
		MaterialPropertyBlock block, stolenBlock;

		cs::Ref<Mesh> fx;
		cs::List<Vector3> vertices;
		cs::List<Color> colors;
		cs::List<Vector3> uvs;
		cs::List<int> indices;

		static cs::Ref<Mesh>& cube();

		/// <summary>A pod's drawn size, km: a cargo canister, a little larger than life so it reads beside a hull.</summary>
		static constexpr double PodLengthKm = 0.014;

	public:
		CargoPodsView(UnitySpaceApp* app, const cs::Ref<Transform>& parent);

		void Update();

	private:
		cs::Ref<Transform> Create(const cs::Ref<Core::CargoPod>& p);
		float PixelSize(Vector3 p, float pixels) const;
		void Sprite(Vector3 c, float radius, Color color);
		void Ribbon(Vector3 a, Vector3 b, float pixels, Color color);
		void Corners(Color color, float shape);
	};
}
