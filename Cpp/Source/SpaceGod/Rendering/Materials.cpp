// Port of Unity.SpaceGod Runtime/Rendering/Materials.cs (US-T30).
#include "Rendering/Materials.h"
#include "Rendering/Pipeline/RenderBackend.h"
#include "Rendering/RenderPipelineProfiles.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Rendering
{
	namespace
	{
		/// <summary>Every material made here and not yet destroyed: DontSave keeps them alive past their scene.</summary>
		cs::List<cs::Ref<Material>>& Created()
		{
			static cs::List<cs::Ref<Material>> created;
			return created;
		}

		cs::HashSet<cs::Ref<Material>>& Shared()
		{
			static cs::HashSet<cs::Ref<Material>> shared;
			return shared;
		}

		cs::HashSet<cs::Ref<Mesh>>& OwnedMeshes()
		{
			static cs::HashSet<cs::Ref<Mesh>> owned;
			return owned;
		}

		/// <summary>Created as the HashSet it is in C#: added once, removed by value.</summary>
		void AddCreated(const cs::Ref<Material>& m)
		{
			auto& c = Created();
			if (!c.Contains(m)) c.Add(m);
		}
	}

	cs::Ref<Shader> Materials::Planet() { return Load(RenderShaders::Planet); }
	cs::Ref<Shader> Materials::Sun() { return Load(RenderShaders::Sun); }
	cs::Ref<Shader> Materials::Glow() { return Load(RenderShaders::Glow); }
	cs::Ref<Shader> Materials::Ring() { return Load(RenderShaders::Ring); }
	cs::Ref<Shader> Materials::Ship() { return Load(RenderShaders::Ship); }
	cs::Ref<Shader> Materials::Line() { return Load(RenderShaders::Line); }
	cs::Ref<Shader> Materials::Stars() { return Load(RenderShaders::Stars); }
	cs::Ref<Shader> Materials::GasGiant() { return Load(RenderShaders::GasGiant); }
	cs::Ref<Shader> Materials::Asteroids() { return Load(RenderShaders::Asteroids); }
	cs::Ref<Shader> Materials::Fx() { return Load(RenderShaders::Fx); }
	cs::Ref<Shader> Materials::Points() { return Load(RenderShaders::Points); }
	cs::Ref<Shader> Materials::Sky() { return Load(RenderShaders::Sky); }

	cs::Ref<Shader> Materials::Load(const cs::String& name) { return RenderBackend::Active()->LoadShader(name); }

	cs::Ref<Material> Materials::Create(const cs::Ref<Shader>& shader, const cs::String& name)
	{
		auto m = cs::New<Material>(shader, name);
		m->hideFlags = HideFlags::DontSave;
		// Views that destroy their own material directly leave dead entries: sweep them out now and then.
		if (Created().Count() >= 2048) Created().RemoveAll([](const cs::Ref<Material>& x) { return !Alive(x); });
		AddCreated(m);
		return m;
	}

	cs::Ref<Mesh> Materials::Own(const cs::Ref<Mesh>& mesh)
	{
		if (Alive(mesh)) OwnedMeshes().Add(mesh);
		return mesh;
	}

	void Materials::DestroyMesh(const cs::Ref<Mesh>& mesh)
	{
		if (!Alive(mesh)) return;
		OwnedMeshes().Remove(mesh);
		Destroy(mesh);
	}

	cs::Ref<Material> Materials::MarkShared(const cs::Ref<Material>& m)
	{
		if (Alive(m)) Shared().Add(m);
		return m;
	}

	void Materials::DestroyOwned(const cs::Ref<Transform>& root)
	{
		cs::HashSet<cs::Ref<Material>> owned;
		cs::List<cs::Ref<Material>> order;
		for (auto& r : GetComponentsInChildren<Renderer>(root, true))
			for (auto& mat : r->sharedMaterials())
				if (Alive(mat) && !Shared().Contains(mat) && owned.Add(mat)) order.Add(mat);
		for (auto& mat : order)
		{
			Created().Remove(mat);
			Destroy(mat);
		}
		// Meshes too, but only those built for these views (cached hull meshes and imported models stay).
		cs::HashSet<cs::Ref<Mesh>> meshes;
		cs::List<cs::Ref<Mesh>> meshOrder;
		for (auto& mf : GetComponentsInChildren<MeshFilter>(root, true))
			if (Alive(mf->sharedMesh) && OwnedMeshes().Contains(mf->sharedMesh) && meshes.Add(mf->sharedMesh)) meshOrder.Add(mf->sharedMesh);
		for (auto& mesh : meshOrder) DestroyMesh(mesh);
	}

	void Materials::DestroyAllOwned()
	{
		for (auto& mat : Created())
			if (Alive(mat) && !Shared().Contains(mat)) Destroy(mat);
		Created().RemoveAll([](const cs::Ref<Material>& m) { return !Alive(m) || !Shared().Contains(m); });
		for (auto& mesh : OwnedMeshes())
			if (Alive(mesh)) Destroy(mesh);
		OwnedMeshes().Clear();
	}
}
