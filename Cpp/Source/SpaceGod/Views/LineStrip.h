#pragma once
// Port of Unity.SpaceGod Runtime/Views/LineStrip.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod::Views
{
	/// <summary>
	/// A 1-pixel line strip mesh. Pixel lines stay legible at every scale, where a LineRenderer's
	/// world-space width would be either invisible or a slab depending on the zoom.
	/// </summary>
	class LineStrip final
	{
	public:
		cs::Ref<GameObject> Root;

	private:
		cs::Ref<Mesh> mesh;
		cs::Ref<MeshRenderer> renderer;
		MaterialPropertyBlock block;
		cs::List<Vector3> vertices;
		cs::List<Color> colors;
		cs::List<int> indices;
		bool allocated = false;

		static cs::Ref<Material>& sharedMaterial();
		static constexpr const char* ColorId = "_Color";

	public:
		LineStrip(const cs::String& name, const cs::Ref<Transform>& parent);

		int Capacity() const { return allocated ? vertices.Count() : 0; }

		/// <summary>Destroys the line and its mesh.</summary>
		void Dispose();

		/// <summary>Sets points (local to Root) with a per-point alpha ramp from <paramref name="alphaStart"/> to <paramref name="alphaEnd"/>.</summary>
		void SetPoints(const cs::List<Vector3>& points, int count, Color color, float alphaStart = 1.0f, float alphaEnd = 1.0f);

		void SetTint(Color tint);

		/// <summary>Distance from <paramref name="local"/> (in Root's frame) to the nearest point on the drawn
		/// strip, per segment; +inf before any points are set. Used to fade orbits with distance (US-T134).</summary>
		double NearestDistance(Vector3 local) const;

		bool Visible() const { return renderer->enabled; }
		void SetVisible(bool value) { renderer->enabled = value; }
	};
}
