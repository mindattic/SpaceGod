#pragma once
// US-T433: a hull's plating as points with the way each faces, for thrusters set on it.

#include "UnityCompat/UnityRenderingTypes.h"

#include <vector>

namespace SpaceGod::Views
{
	/// <summary>
	/// A hull's surface in its view root's frame (one unit long, nose +Z, up +Y, starboard +X), sampled from the triangles
	/// it is drawn with (US-T433): a procedural hull's own, a kit's imported meshes' as the engine keeps them
	/// (Rendering::ImportedGeometry), finely enough that a broad flat face has points across it, each point with the way
	/// its face looks out. A part with no triangles to read (a cooked import) is its own box, face by face, so even then
	/// each piece is sampled where it is rather than as one box round the whole hull.
	/// </summary>
	class HullSurface
	{
	public:
		struct Sample
		{
			Vector3 P;
			/// <summary>The way its face looks out from the hull (unit).</summary>
			Vector3 N;
		};

		/// <summary>Samples the hull drawn by <paramref name="hull"/> (renderers under <paramref name="root"/>); none drawn
		/// gives a nominal fighter's box, one long and a third wide.</summary>
		HullSurface(const cs::Ref<Transform>& root, const cs::List<cs::Ref<Renderer>>& hull);

		const std::vector<Sample>& Samples() const { return samples; }
		Vector3 Min() const { return mn; }
		Vector3 Max() const { return mx; }
		Vector3 Centre() const { return (mn + mx) * 0.5f; }
		float Length() const { return Mathf::Max(mx.z - mn.z, 0.05f); }

		/// <summary>
		/// Where a thruster firing along <paramref name="fire"/> goes near station <paramref name="z"/>: on plating that
		/// faces that way, the outermost such point along <paramref name="side"/> (across the hull from its keel line; zero
		/// takes the outermost along <paramref name="fire"/> itself), kept off to the side of it by <paramref name="sideways"/>,
		/// with nothing of the hull in its exhaust's way for a while (a nozzle <paramref name="radius"/> across). Falls back
		/// to the outermost point that way if no face looks along it there. False only for an empty hull.
		/// </summary>
		bool Place(float z, Vector3 side, Vector3 fire, float sideways, float radius, Vector3& at, Vector3& normal) const;

	private:
		std::vector<Sample> samples;
		Vector3 mn, mx;

		bool Clear(Vector3 at, Vector3 fire, float radius) const;
	};
}
