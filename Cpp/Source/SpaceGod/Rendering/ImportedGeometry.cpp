// US-T433: the triangles of an imported mesh (see ImportedGeometry.h).
#include "Rendering/ImportedGeometry.h"

#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"

#include <unordered_map>

namespace SpaceGod::Rendering
{
	const ImportedGeometry* ImportedGeometry::Of(const cs::Ref<Mesh>& mesh)
	{
		if (!mesh || mesh->ImportedAsset.empty()) return nullptr;
		static std::unordered_map<cs::String, ImportedGeometry> known;
		const Quaternion& q = mesh->ImportedRotation;
		cs::String key = cs::Cat(mesh->ImportedAsset, "@", mesh->ImportedScale, "@", q.x, ",", q.y, ",", q.z, ",", q.w);
		auto it = known.find(key);
		if (it != known.end()) return it->second.Ok ? &it->second : nullptr;
		ImportedGeometry g;
		UStaticMesh* sm = LoadObject<UStaticMesh>(nullptr, UTF8_TO_TCHAR(mesh->ImportedAsset.c_str()), nullptr, LOAD_NoWarn | LOAD_Quiet);
		FStaticMeshRenderData* rd = sm ? sm->GetRenderData() : nullptr;
		if (rd && rd->LODResources.Num() > 0)
		{
			const FStaticMeshLODResources& lod = rd->LODResources[0];
			const FPositionVertexBuffer& pb = lod.VertexBuffers.PositionVertexBuffer;
			uint32 n = pb.GetNumVertices();
			if (n > 0 && pb.GetVertexData() != nullptr)
			{
				// Unreal (x, y, z) cm is Unity (x, z, -y) / 100 (UnrealSpace); the mesh draws it at ImportedScale per cm.
				float k = mesh->ImportedScale;
				g.Vertices.reserve(n);
				for (uint32 i = 0; i < n; i++)
				{
					FVector3f v = pb.VertexPosition(i);
					g.Vertices.push_back(mesh->ImportedRotation * (Vector3(v.X, v.Z, -v.Y) * k));
				}
				TArray<uint32> indices;
				if (lod.IndexBuffer.GetNumIndices() > 0) lod.IndexBuffer.GetCopy(indices);
				g.Triangles.reserve(indices.Num());
				for (int i = 0; i + 2 < indices.Num(); i += 3)
				{
					if (indices[i] >= n || indices[i + 1] >= n || indices[i + 2] >= n) continue;
					g.Triangles.push_back(static_cast<int>(indices[i]));
					g.Triangles.push_back(static_cast<int>(indices[i + 1]));
					g.Triangles.push_back(static_cast<int>(indices[i + 2]));
				}
				g.Ok = !g.Triangles.empty();
			}
		}
		auto& stored = known.emplace(key, std::move(g)).first->second;
		return stored.Ok ? &stored : nullptr;
	}
}
