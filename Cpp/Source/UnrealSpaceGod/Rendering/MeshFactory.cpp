// Port of Unity.SpaceGod Runtime/Rendering/MeshFactory.cs (US-T30).
#include "Rendering/MeshFactory.h"

namespace SpaceGod::Rendering
{
	cs::Ref<Mesh> MeshFactory::UvSphere(int longitudeSegments, int latitudeSegments)
	{
		int cols = longitudeSegments + 1;
		int rows = latitudeSegments + 1;
		std::vector<Vector3> vertices(static_cast<size_t>(cols) * rows);
		std::vector<Vector3> normals(vertices.size());
		std::vector<Vector2> uvs(vertices.size());
		for (int y = 0; y < rows; y++)
		{
			float v = static_cast<float>(y) / latitudeSegments;
			float lat = (v - 0.5f) * Mathf::PI;
			for (int x = 0; x < cols; x++)
			{
				float u = static_cast<float>(x) / longitudeSegments;
				float lon = (u - 0.5f) * Mathf::PI * 2.0f;
				// East (increasing u) runs toward +Z, so seen from outside with north up the map is
				// not mirrored and the triangles below wind clockwise (Unity's front face).
				Vector3 n(Mathf::Cos(lat) * Mathf::Cos(lon), Mathf::Sin(lat), Mathf::Cos(lat) * Mathf::Sin(lon));
				int i = y * cols + x;
				vertices[i] = n;
				normals[i] = n;
				uvs[i] = Vector2(u, v);
			}
		}

		std::vector<int> triangles(static_cast<size_t>(longitudeSegments) * latitudeSegments * 6);
		int t = 0;
		for (int y = 0; y < latitudeSegments; y++)
		{
			for (int x = 0; x < longitudeSegments; x++)
			{
				int a = y * cols + x;
				int b = a + 1;
				int c = a + cols;
				int d = c + 1;
				triangles[t++] = a;
				triangles[t++] = c;
				triangles[t++] = b;
				triangles[t++] = b;
				triangles[t++] = c;
				triangles[t++] = d;
			}
		}

		auto mesh = cs::New<Mesh>("UvSphere");
		mesh->indexFormat = IndexFormat::UInt32;
		mesh->SetVertices(vertices);
		mesh->SetNormals(normals);
		mesh->SetUVs(0, uvs);
		mesh->SetTriangles(triangles, 0);
		mesh->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 2.0f));
		return mesh;
	}

	cs::Ref<Mesh> MeshFactory::Annulus(float inner, float outer, int segments)
	{
		std::vector<Vector3> vertices(static_cast<size_t>(segments + 1) * 2);
		std::vector<Vector2> uvs(vertices.size());
		std::vector<Vector3> normals(vertices.size());
		for (int i = 0; i <= segments; i++)
		{
			float a = Mathf::PI * 2.0f * i / segments;
			Vector3 dir(Mathf::Cos(a), 0, Mathf::Sin(a));
			vertices[i * 2] = dir * inner;
			vertices[i * 2 + 1] = dir * outer;
			uvs[i * 2] = Vector2(0, static_cast<float>(i) / segments);
			uvs[i * 2 + 1] = Vector2(1, static_cast<float>(i) / segments);
			normals[i * 2] = Vector3::up();
			normals[i * 2 + 1] = Vector3::up();
		}

		std::vector<int> triangles(static_cast<size_t>(segments) * 6);
		for (int i = 0; i < segments; i++)
		{
			int a = i * 2;
			triangles[i * 6] = a;
			triangles[i * 6 + 1] = a + 1;
			triangles[i * 6 + 2] = a + 2;
			triangles[i * 6 + 3] = a + 2;
			triangles[i * 6 + 4] = a + 1;
			triangles[i * 6 + 5] = a + 3;
		}

		auto mesh = cs::New<Mesh>("Annulus");
		mesh->SetVertices(vertices);
		mesh->SetUVs(0, uvs);
		mesh->SetNormals(normals);
		mesh->SetTriangles(triangles, 0);
		return mesh;
	}

	cs::Ref<Mesh> MeshFactory::Quad()
	{
		auto mesh = cs::New<Mesh>("Quad");
		mesh->SetVertices(std::vector<Vector3>{
			Vector3(-0.5f, -0.5f, 0), Vector3(0.5f, -0.5f, 0),
			Vector3(-0.5f, 0.5f, 0), Vector3(0.5f, 0.5f, 0),
		});
		mesh->SetUVs(0, std::vector<Vector2>{ Vector2(0, 0), Vector2(1, 0), Vector2(0, 1), Vector2(1, 1) });
		mesh->SetTriangles(std::vector<int>{ 0, 2, 1, 1, 2, 3 }, 0);
		mesh->RecalculateNormals();
		return mesh;
	}

	cs::Ref<Mesh> MeshFactory::Plume()
	{
		auto mesh = cs::New<Mesh>("Plume");
		mesh->SetVertices(std::vector<Vector3>{
			Vector3(-0.5f, 0, 0), Vector3(0.5f, 0, 0), Vector3(-0.5f, 0, -1), Vector3(0.5f, 0, -1),
			Vector3(0, -0.5f, 0), Vector3(0, 0.5f, 0), Vector3(0, -0.5f, -1), Vector3(0, 0.5f, -1),
		});
		// uv.y 0.5 at the nozzle fading to the tip, so the radial glow shader gives a teardrop.
		mesh->SetUVs(0, std::vector<Vector2>{
			Vector2(0, 0.5f), Vector2(1, 0.5f), Vector2(0, 1), Vector2(1, 1),
			Vector2(0, 0.5f), Vector2(1, 0.5f), Vector2(0, 1), Vector2(1, 1),
		});
		mesh->SetTriangles(std::vector<int>{ 0, 2, 1, 1, 2, 3, 4, 6, 5, 5, 6, 7 }, 0);
		mesh->SetBounds(Bounds(Vector3(0, 0, -0.5f), Vector3::one()));
		return mesh;
	}

	cs::Ref<Mesh> MeshFactory::BuiltinCube()
	{
		static cs::Ref<Mesh> cube;
		if (cube) return cube;
		std::vector<Vector3> v, n;
		std::vector<Vector2> uv;
		std::vector<int> t;
		// Six faces, each its own four vertices (flat normals), wound clockwise seen from outside.
		const Vector3 normals[6] = { Vector3::right(), Vector3::left(), Vector3::up(), Vector3::down(), Vector3::forward(), Vector3::back() };
		for (const auto& f : normals)
		{
			Vector3 u = Mathf::Abs(f.y) > 0.5f ? Vector3::right() : Vector3::up();
			Vector3 s = Vector3::Cross(u, f);
			Vector3 c = f * 0.5f;
			int i = static_cast<int>(v.size());
			v.push_back(c - s * 0.5f - u * 0.5f);
			v.push_back(c + s * 0.5f - u * 0.5f);
			v.push_back(c + s * 0.5f + u * 0.5f);
			v.push_back(c - s * 0.5f + u * 0.5f);
			for (int k = 0; k < 4; k++) n.push_back(f);
			uv.insert(uv.end(), { Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1) });
			t.insert(t.end(), { i, i + 1, i + 2, i, i + 2, i + 3 });
		}
		cube = cs::New<Mesh>("Cube");
		cube->SetVertices(v);
		cube->SetNormals(n);
		cube->SetUVs(0, uv);
		cube->SetTriangles(t, 0);
		return cube;
	}

	cs::Ref<Mesh> MeshFactory::BuiltinCapsule()
	{
		static cs::Ref<Mesh> capsule;
		if (capsule) return capsule;
		// A cylinder of radius 0.5 between y = -0.5 and 0.5 capped by hemispheres (Unity's capsule: 2 units tall).
		const int segments = 24, rings = 8;
		std::vector<Vector3> v, n;
		std::vector<Vector2> uv;
		std::vector<int> t;
		auto ringAt = [&](float y, float r, float centreY, float vv)
		{
			for (int s = 0; s <= segments; s++)
			{
				float a = Mathf::PI * 2.0f * s / segments;
				Vector3 p(Mathf::Cos(a) * r, y, Mathf::Sin(a) * r);
				v.push_back(p);
				n.push_back((p - Vector3(0, centreY, 0)).normalized());
				uv.emplace_back(static_cast<float>(s) / segments, vv);
			}
		};
		int rowsDone = 0;
		for (int i = 0; i <= rings; i++) // bottom hemisphere, south pole up to the equator
		{
			float lat = -Mathf::PI * 0.5f + Mathf::PI * 0.5f * i / rings;
			ringAt(-0.5f + Mathf::Sin(lat) * 0.5f, Mathf::Cos(lat) * 0.5f, -0.5f, 0.25f * i / rings);
			rowsDone++;
		}
		for (int i = 0; i <= rings; i++) // top hemisphere, equator up to the north pole
		{
			float lat = Mathf::PI * 0.5f * i / rings;
			ringAt(0.5f + Mathf::Sin(lat) * 0.5f, Mathf::Cos(lat) * 0.5f, 0.5f, 0.75f + 0.25f * i / rings);
			rowsDone++;
		}
		int cols = segments + 1;
		for (int r = 0; r + 1 < rowsDone; r++)
			for (int s = 0; s < segments; s++)
			{
				int a = r * cols + s, b = a + 1, c = a + cols, d = c + 1;
				t.insert(t.end(), { a, c, b, b, c, d });
			}
		capsule = cs::New<Mesh>("Capsule");
		capsule->SetVertices(v);
		capsule->SetNormals(n);
		capsule->SetUVs(0, uv);
		capsule->SetTriangles(t, 0);
		return capsule;
	}
}
