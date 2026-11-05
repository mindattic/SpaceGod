#pragma once
// Port of Unity.SpaceGod Runtime/Rendering/MeshFactory.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod::Rendering
{
	/// <summary>Procedural meshes: a lat-long sphere matching equirectangular maps, a ring annulus and a quad.</summary>
	class MeshFactory
	{
	public:
		/// <summary>
		/// Unit-radius UV sphere. u runs 0..1 eastward from longitude -180°, v runs 0..1 from the
		/// south pole, which is the layout of the equirectangular NASA/USGS colour maps.
		/// </summary>
		static cs::Ref<Mesh> UvSphere(int longitudeSegments = 128, int latitudeSegments = 64);

		/// <summary>Flat annulus in the XZ plane; uv.x runs 0 at the inner edge to 1 at the outer edge.</summary>
		static cs::Ref<Mesh> Annulus(float inner, float outer, int segments = 256);

		/// <summary>Unit quad in the XY plane centred on the origin.</summary>
		static cs::Ref<Mesh> Quad();

		/// <summary>Two crossed quads along -Z from the origin, for an engine plume.</summary>
		static cs::Ref<Mesh> Plume();

		/// <summary>Unity's built-in cube (Resources.GetBuiltinResource&lt;Mesh&gt;("Cube.fbx")): one unit across, 24
		/// vertices with a face's own normals and uvs.</summary>
		static cs::Ref<Mesh> BuiltinCube();

		/// <summary>Unity's built-in capsule ("Capsule.fbx"): radius 0.5, two units tall along Y.</summary>
		static cs::Ref<Mesh> BuiltinCapsule();
	};
}
