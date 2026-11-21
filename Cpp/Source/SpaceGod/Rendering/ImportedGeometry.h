#pragma once
// US-T433: the triangles of an imported mesh (a kit's static mesh) as the game's own frame has them.

#include "UnityCompat/UnityRenderingTypes.h"

#include <vector>

namespace SpaceGod::Rendering
{
	/// <summary>
	/// An imported mesh's surface (US-T433): its first LOD's vertices, in the mesh's own Unity frame and units (Unreal's
	/// (x, y, z) cm is Unity's (x, z, -y) / 100, times the mesh's ImportedScale), and its triangles. Read once per asset from
	/// the engine's copy of the mesh, which an editor build keeps on the CPU; a cooked mesh without CPU access has none
	/// (<see cref="Ok"/> false) and its callers fall back to its box. The hull thrusters sit their jets on it.
	/// </summary>
	struct ImportedGeometry
	{
		std::vector<Vector3> Vertices;
		/// <summary>Three vertex indices per triangle (their winding is not relied on: a caller orients the faces itself).</summary>
		std::vector<int> Triangles;
		bool Ok = false;

		/// <summary>The surface of <paramref name="mesh"/> when it draws an imported asset (cached per asset), else null.</summary>
		static const ImportedGeometry* Of(const cs::Ref<Mesh>& mesh);
	};
}
