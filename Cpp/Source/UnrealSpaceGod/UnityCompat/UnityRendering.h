#pragma once

// Unity's rendering objects as the Views/Rendering port uses them (US-T30): Mesh, Shader, Material,
// MaterialPropertyBlock, Bounds, the components a GameObject carries (MeshFilter, MeshRenderer, Renderer) and
// Object.Destroy / Instantiate / Resources. They are engine-free data with Unity's semantics; UnityScene (once a
// frame) backs every live renderer with an Unreal UDynamicMeshComponent, its materials with
// UMaterialInstanceDynamic of the SpaceGodRender plugin's materials (ShaderBinding), and converts Unity's axes
// and units through UnrealSpace.h, so the ported views read line for line like Unity.SpaceGod's Runtime/Views.
//
// Name rules (docs/MIGRATION.md): Unity's bodied properties are methods (mesh->vertexCount(), r->bounds()),
// setters of bodied properties SetX (mesh->SetVertices(v) for `mesh.vertices = v`, r->SetSharedMaterial(m) for
// `r.sharedMaterial = m`, mesh->SetBounds(b) for `mesh.bounds = b`); plain fields stay fields (r->enabled).
// `go.AddComponent<MeshFilter>()` -> `AddComponent<MeshFilter>(go)`; `go.GetComponent<Renderer>()` ->
// `GetComponent<Renderer>(go)`; `x != null` on a Unity object (destroyed counts as null) -> `Alive(x)`;
// `new GameObject(name)` -> `NewGameObject(name)` (held by the scene until Object.Destroy, as Unity's are).

#include "CoreMinimal.h"
#include "UnityCompat/UnityMath.h"
#include "UnityCompat/UnityRuntime.h"
#include "UnityCompat/UnityTexture.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <variant>
#include <vector>

class UTexture;

namespace SpaceGod::UnityCompat
{
	enum class MeshTopology : int { Triangles = 0, Quads = 2, Lines = 3, LineStrip = 4, Points = 5 };
	enum class IndexFormat : int { UInt16 = 0, UInt32 = 1 };
	enum class ShadowCastingMode : int { Off = 0, On = 1, TwoSided = 2, ShadowsOnly = 3 };

	/// UnityEngine.Bounds: an axis-aligned box by centre and half-size.
	struct Bounds
	{
		Vector3 center;
		Vector3 extents;
		Bounds() = default;
		Bounds(Vector3 center_, Vector3 size) : center(center_), extents(size * 0.5f) {}
		Vector3 size() const { return extents * 2.0f; }
		Vector3 min() const { return center - extents; }
		Vector3 max() const { return center + extents; }
		void SetMinMax(Vector3 mn, Vector3 mx)
		{
			extents = (mx - mn) * 0.5f;
			center = mn + extents;
		}
		void Encapsulate(Vector3 point) { SetMinMax(Vector3::Min(min(), point), Vector3::Max(max(), point)); }
		void Encapsulate(const Bounds& b)
		{
			Encapsulate(b.center - b.extents);
			Encapsulate(b.center + b.extents);
		}
	};

	/// The engine side of a Mesh, Material or Renderer (UnityScene's converted mesh, material instance, component):
	/// released with the object it backs.
	struct NativeHandle
	{
		virtual ~NativeHandle() = default;
	};

	/// Unity's "an object destroyed compares equal to null": every engine object carries the flag.
	class Object
	{
	public:
		cs::String name;
		HideFlags hideFlags = HideFlags::None;
		bool destroyed = false;
		virtual ~Object() = default;
	};

	/// `x != null` for a Unity object held by reference.
	template <class T> bool Alive(const cs::Ref<T>& o)
	{
		if constexpr (std::is_base_of_v<Object, T>) return o && !o->destroyed;
		else if constexpr (std::is_same_v<T, GameObject>) return o && !o->destroyed;
		else return static_cast<bool>(o);
	}

	// ---------------------------------------------------------------- Mesh

	/// UnityEngine.Mesh: vertex streams (positions, normals, up to four uv channels of up to four components,
	/// colours) and sub-meshes of indices with their topology. Every change bumps Version, which is how UnityScene
	/// knows to re-upload it. Positions and normals are Unity axes; UnityScene converts them.
	class Mesh final : public Object
	{
	public:
		Mesh() = default;
		explicit Mesh(const cs::String& name_) { name = name_; }

		IndexFormat indexFormat = IndexFormat::UInt16;

		std::vector<Vector3> Vertices;
		std::vector<Vector3> Normals;
		std::vector<Color> Colors;
		/// Each uv channel as Vector4 (Unity's SetUVs takes 2, 3 or 4 components; the rest are 0).
		std::vector<Vector4> Uv[4];

		struct SubMesh
		{
			std::vector<int> Indices;
			MeshTopology Topology = MeshTopology::Triangles;
		};
		std::vector<SubMesh> SubMeshes;

		/// Bumped by every change (UnityScene re-uploads a mesh whose version moved on).
		uint64_t Version = 1;
		/// Set explicitly (mesh.bounds = ...): culling uses it as given, as Unity's does.
		bool ExplicitBounds = false;
		/// MarkDynamic(): rebuilt often (lines, effects); a hint for the upload path only.
		bool Dynamic = false;
		/// Unity's uv0 handed to the material as it is (no V flip): data, not a texture coordinate (the asteroid
		/// field's (index, corner) pairs).
		bool RawUv0 = false;
		/// An imported model's part (Unity's glTF import of the Kenney craft): the Unreal static mesh drawn instead of
		/// the streams above (an object path, see Resources::LoadModel), and the scale that brings it to Unity units.
		/// The streams are left empty; bounds() is set from the asset.
		cs::String ImportedAsset;
		float ImportedScale = 1;
		/// Engine-side converted copy (UnityScene), rebuilt when Version moves on.
		std::unique_ptr<NativeHandle> Engine;
		uint64_t EngineVersion = 0;

		void MarkDynamic() { Dynamic = true; }
		void MarkModified() { ++Version; }
		void Clear(bool keepVertexLayout = true);

		int vertexCount() const { return static_cast<int>(Vertices.size()); }
		int subMeshCount() const { return static_cast<int>(SubMeshes.size()); }

		Bounds bounds() const { return meshBounds; }
		void SetBounds(const Bounds& b)
		{
			meshBounds = b;
			ExplicitBounds = true;
			MarkModified();
		}
		void RecalculateBounds();
		/// Per-vertex normals averaged from the faces that share each vertex (Unity's RecalculateNormals).
		void RecalculateNormals();

		// `mesh.vertices = v` / `mesh.SetVertices(v)` (and the same for the other streams).
		void SetVertices(const std::vector<Vector3>& v);
		void SetVertices(const cs::List<Vector3>& v) { SetVertices(v.Raw()); }
		void SetNormals(const std::vector<Vector3>& n);
		void SetNormals(const cs::List<Vector3>& n) { SetNormals(n.Raw()); }
		void SetColors(const std::vector<Color>& c);
		void SetColors(const cs::List<Color>& c) { SetColors(c.Raw()); }
		void SetUVs(int channel, const std::vector<Vector2>& uv);
		void SetUVs(int channel, const cs::List<Vector2>& uv) { SetUVs(channel, uv.Raw()); }
		void SetUVs(int channel, const std::vector<Vector3>& uv);
		void SetUVs(int channel, const cs::List<Vector3>& uv) { SetUVs(channel, uv.Raw()); }
		void SetUVs(int channel, const std::vector<Vector4>& uv);
		void SetUVs(int channel, const cs::List<Vector4>& uv) { SetUVs(channel, uv.Raw()); }
		/// `mesh.triangles = t` is SetTriangles(t, 0).
		void SetTriangles(const std::vector<int>& triangles, int submesh, bool calculateBounds = true);
		void SetTriangles(const cs::List<int>& triangles, int submesh, bool calculateBounds = true) { SetTriangles(triangles.Raw(), submesh, calculateBounds); }
		void SetIndices(const std::vector<int>& indices, MeshTopology topology, int submesh, bool calculateBounds = true);
		void SetIndices(const cs::List<int>& indices, MeshTopology topology, int submesh, bool calculateBounds = true) { SetIndices(indices.Raw(), topology, submesh, calculateBounds); }
		std::vector<int> triangles() const { return SubMeshes.empty() ? std::vector<int>() : SubMeshes[0].Indices; }

	private:
		Bounds meshBounds;
	};

	// ---------------------------------------------------------------- Shader, Material

	/// A Unity shader by name ("Unity.SpaceGod/Planet"); ShaderBinding maps it to its SpaceGodRender material.
	class Shader final : public Object
	{
	public:
		explicit Shader(const cs::String& name_) { name = name_; }
		/// Shader.Find: one shared instance per name.
		static cs::Ref<Shader> Find(const cs::String& name);
		/// Shader.PropertyToID: the property name itself stands for its id here.
		static cs::String PropertyToID(const cs::String& name) { return name; }
		/// Shader.SetGlobalVector / GetGlobalVector (the stars' light): the MPC_SpaceGod collection, pushed by UnityScene.
		static void SetGlobalVector(const cs::String& name, Vector4 value);
		static Vector4 GetGlobalVector(const cs::String& name);
		static void SetGlobalFloat(const cs::String& name, float value);
		static float GetGlobalFloat(const cs::String& name);
		/// Every global set so far (UnityScene pushes them when their Version moves on).
		static const class MaterialProperties& Globals();
	};

	/// One material property, as Unity keeps them: a float, a vector (a colour set with SetColor is marked so its
	/// sRGB value is linearised as Unity's SetColor does in a linear-space project), a texture, or a float array.
	struct MaterialValue
	{
		enum class Kind : uint8_t { Float, Vector, Color, Texture, FloatArray } kind = Kind::Float;
		float f = 0;
		Vector4 v;
		cs::Ref<Texture> texture;
		std::vector<float> array;
	};

	/// The properties a material or a property block sets, in the order they were first set.
	class MaterialProperties
	{
	public:
		void SetFloat(const cs::String& name, float value);
		void SetInt(const cs::String& name, int value) { SetFloat(name, static_cast<float>(value)); }
		void SetVector(const cs::String& name, Vector4 value);
		void SetVector(const cs::String& name, Vector3 value) { SetVector(name, Vector4(value.x, value.y, value.z, 0)); }
		void SetColor(const cs::String& name, Color value);
		void SetTexture(const cs::String& name, const cs::Ref<Texture>& value);
		void SetTexture(const cs::String& name, std::nullptr_t) { SetTexture(name, cs::Ref<Texture>()); }
		void SetFloatArray(const cs::String& name, const cs::List<float>& values);
		void SetFloatArray(const cs::String& name, const std::vector<float>& values);
		void Clear();

		bool Has(const cs::String& name) const { return index.count(name) != 0; }
		const MaterialValue* Find(const cs::String& name) const;
		float GetFloat(const cs::String& name) const;
		Color GetColor(const cs::String& name) const;
		Vector4 GetVector(const cs::String& name) const;
		cs::Ref<Texture> GetTexture(const cs::String& name) const;
		bool IsEmpty() const { return values.empty(); }
		/// The same properties with the same values (a block set again unchanged need not reach Unreal again).
		bool SameAs(const MaterialProperties& other) const;

		const std::vector<std::pair<cs::String, MaterialValue>>& All() const { return values; }
		/// Bumped by every change.
		uint64_t Version = 1;

	private:
		MaterialValue& Slot(const cs::String& name, MaterialValue::Kind kind);
		std::vector<std::pair<cs::String, MaterialValue>> values;
		std::unordered_map<cs::String, size_t> index;
	};

	/// UnityEngine.Material: a shader and its properties. Backed by one UMaterialInstanceDynamic (Native) made by
	/// UnityScene from the shader's SpaceGodRender material.
	class Material final : public Object
	{
	public:
		explicit Material(const cs::Ref<Shader>& shader_) : shader(shader_) {}
		Material(const cs::Ref<Shader>& shader_, const cs::String& name_) : shader(shader_) { name = name_; }

		cs::Ref<Shader> shader;
		/// Unity's render queue (2000 opaque, 3000 transparent, 999 the sky): the draw order among translucent things.
		int renderQueue = -1;
		MaterialProperties Properties;
		/// Legacy `material.color` (the _Color property).
		Color color() const { return Properties.Has("_Color") ? Properties.GetColor("_Color") : Color::white(); }
		void SetColorProperty(Color c) { Properties.SetColor("_Color", c); }

		void SetFloat(const cs::String& n, float v) { Properties.SetFloat(n, v); }
		void SetInt(const cs::String& n, int v) { Properties.SetInt(n, v); }
		void SetColor(const cs::String& n, Color v) { Properties.SetColor(n, v); }
		void SetVector(const cs::String& n, Vector4 v) { Properties.SetVector(n, v); }
		void SetVector(const cs::String& n, Vector3 v) { Properties.SetVector(n, v); }
		void SetTexture(const cs::String& n, const cs::Ref<Texture>& t) { Properties.SetTexture(n, t); }
		void SetTexture(const cs::String& n, const cs::Ref<Texture2D>& t) { Properties.SetTexture(n, std::static_pointer_cast<Texture>(t)); }
		void SetTexture(const cs::String& n, std::nullptr_t) { Properties.SetTexture(n, nullptr); }
		void SetFloatArray(const cs::String& n, const cs::List<float>& v) { Properties.SetFloatArray(n, v); }
		void SetFloatArray(const cs::String& n, const std::vector<float>& v) { Properties.SetFloatArray(n, v); }
		float GetFloat(const cs::String& n) const { return Properties.GetFloat(n); }
		Color GetColor(const cs::String& n) const { return Properties.GetColor(n); }
		/// Whether the shader has the property: the imported models' materials carry _BaseColor (glTF), the game's
		/// shaders what they were given.
		bool HasProperty(const cs::String& n) const { return Properties.Has(n); }

		/// Engine side (its UMaterialInstanceDynamic), made and kept by UnityScene.
		std::unique_ptr<NativeHandle> Engine;
		uint64_t EngineVersion = 0;
	};

	/// UnityEngine.MaterialPropertyBlock: per-renderer overrides of its material's properties.
	class MaterialPropertyBlock final : public MaterialProperties
	{
	};

	// ---------------------------------------------------------------- components

	/// UnityEngine.Component: something a GameObject carries.
	class Component : public Object, public std::enable_shared_from_this<Component>
	{
	public:
		std::weak_ptr<GameObject> owner;
		cs::Ref<GameObject> gameObject() const { return owner.lock(); }
		cs::Ref<Transform> transform() const;
		/// Called by Instantiate: a copy of this component for another GameObject.
		virtual cs::Ref<Component> Clone() const = 0;
	};

	/// UnityEngine.MeshFilter: which mesh a renderer draws.
	class MeshFilter final : public Component
	{
	public:
		cs::Ref<Mesh> sharedMesh;
		cs::Ref<Component> Clone() const override;
	};

	/// UnityEngine.Renderer.
	class Renderer : public Component
	{
	public:
		bool enabled = true;
		/// Graphics.RenderPrimitives: drawn only on the frame UnityScene numbers this (-1: every frame, as a renderer is).
		int64_t OnlyFrame = -1;
		ShadowCastingMode shadowCastingMode = ShadowCastingMode::On;
		bool receiveShadows = true;

		cs::Ref<Material> sharedMaterial() const { return materials.empty() ? nullptr : materials[0]; }
		void SetSharedMaterial(const cs::Ref<Material>& m)
		{
			materials.assign(1, m);
			++MaterialsVersion;
		}
		const std::vector<cs::Ref<Material>>& sharedMaterials() const { return materials; }
		void SetSharedMaterials(const std::vector<cs::Ref<Material>>& m)
		{
			materials = m;
			++MaterialsVersion;
		}
		void SetSharedMaterials(const cs::List<cs::Ref<Material>>& m) { SetSharedMaterials(m.Raw()); }

		/// SetPropertyBlock copies the block (as Unity does): later changes to the caller's block need another call.
		void SetPropertyBlock(const MaterialPropertyBlock& block);
		bool HasPropertyBlock() const { return hasBlock; }
		const MaterialPropertyBlock& PropertyBlock() const { return block; }

		/// The world-space box the renderer covers this frame (its mesh's bounds through its transform).
		Bounds bounds() const;

		/// The mesh drawn (the MeshFilter's, or the renderer's own for particles).
		virtual cs::Ref<Mesh> DrawnMesh() const;

		/// Engine side (its UDynamicMeshComponent and per-renderer material instances), made by UnityScene.
		std::unique_ptr<NativeHandle> Engine;
		uint64_t MaterialsVersion = 1;
		uint64_t BlockVersion = 0;

	protected:
		void CopyRendererTo(Renderer& r) const;

	private:
		std::vector<cs::Ref<Material>> materials;
		MaterialPropertyBlock block;
		bool hasBlock = false;
	};

	class MeshRenderer final : public Renderer
	{
	public:
		cs::Ref<Component> Clone() const override;
	};

	/// The components of one GameObject (Unity keeps them on the GameObject; the scene keeps them here, with the
	/// GameObject itself, until it is destroyed).
	struct ComponentBag
	{
		cs::Ref<GameObject> Owner;
		std::vector<cs::Ref<Component>> Components;
	};

	/// The scene: every GameObject made through NewGameObject, and every component, until Object.Destroy.
	class SceneRegistry
	{
	public:
		static SceneRegistry& Get();
		ComponentBag& BagOf(const cs::Ref<GameObject>& go);
		const ComponentBag* Find(const GameObject* go) const;
		/// Calls visit for every live renderer.
		void ForEachRenderer(const std::function<void(const cs::Ref<Renderer>&)>& visit) const;
		/// Forgets everything destroyed since the last call, handing each dropped renderer to onDropped first.
		void Purge(const std::function<void(Renderer&)>& onDropped);
		/// Every live component of type T (for the per-frame passes: particle simulation).
		template <class T> void ForEach(const std::function<void(const cs::Ref<T>&)>& visit) const
		{
			for (auto& kv : bags)
				for (auto& c : kv.second.Components)
					if (auto t = std::dynamic_pointer_cast<T>(c)) visit(t);
		}

	private:
		std::unordered_map<const GameObject*, ComponentBag> bags;
	};

	/// `new GameObject(name)`: made with its Transform and kept by the scene until destroyed.
	cs::Ref<GameObject> NewGameObject(const cs::String& name);

	/// `go.AddComponent<T>()`.
	template <class T> cs::Ref<T> AddComponent(const cs::Ref<GameObject>& go)
	{
		auto c = cs::New<T>();
		c->owner = go;
		SceneRegistry::Get().BagOf(go).Components.push_back(c);
		return c;
	}

	/// `go.GetComponent<T>()` (null when it has none).
	template <class T> cs::Ref<T> GetComponent(const cs::Ref<GameObject>& go)
	{
		if (!go) return nullptr;
		if (auto bag = SceneRegistry::Get().Find(go.get()))
			for (auto& c : bag->Components)
				if (auto t = std::dynamic_pointer_cast<T>(c)) return t;
		return nullptr;
	}
	template <class T> cs::Ref<T> GetComponent(const cs::Ref<Transform>& t) { return t ? GetComponent<T>(t->gameObject()) : nullptr; }
	template <class T> cs::Ref<T> GetComponent(const cs::Ref<Component>& c) { return c ? GetComponent<T>(c->gameObject()) : nullptr; }

	/// `go.GetComponentsInChildren<T>(includeInactive)`: the GameObject's own first, then its children depth first.
	template <class T> cs::List<cs::Ref<T>> GetComponentsInChildren(const cs::Ref<Transform>& root, bool includeInactive = false)
	{
		cs::List<cs::Ref<T>> found;
		std::function<void(const cs::Ref<Transform>&)> walk = [&](const cs::Ref<Transform>& t)
		{
			if (!t) return;
			auto go = t->gameObject();
			if (go && !includeInactive && !go->activeSelf) return;
			if (go)
				if (auto bag = SceneRegistry::Get().Find(go.get()))
					for (auto& c : bag->Components)
						if (auto x = std::dynamic_pointer_cast<T>(c)) found.Add(x);
			for (auto& child : t->children) walk(child);
		};
		walk(root);
		return found;
	}
	template <class T> cs::List<cs::Ref<T>> GetComponentsInChildren(const cs::Ref<GameObject>& go, bool includeInactive = false)
	{
		return go ? GetComponentsInChildren<T>(go->transform, includeInactive) : cs::List<cs::Ref<T>>();
	}

	/// Unity's world matrix of a transform (all its ancestors' positions, rotations and scales).
	struct WorldMatrix
	{
		// Row-major 3x3 linear part and the translation, Unity axes.
		float m[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
		Vector3 t;
		Vector3 MultiplyPoint(Vector3 p) const;
		Vector3 MultiplyVector(Vector3 v) const;
		static WorldMatrix Of(const Transform& tr);
	};

	/// Object.Destroy for the objects of this file (a GameObject goes through GameObject::Destroy).
	void Destroy(const cs::Ref<GameObject>& go);
	void Destroy(const cs::Ref<Mesh>& mesh);
	void Destroy(const cs::Ref<Material>& material);
	void Destroy(const cs::Ref<Texture2D>& texture);

	/// Object.Instantiate(original, parent, false): a copy of the hierarchy (sharing meshes and materials).
	cs::Ref<GameObject> Instantiate(const cs::Ref<GameObject>& original, const cs::Ref<Transform>& parent, bool worldPositionStays = false);

	/// UnityEngine.Random's static generator (Random.value, Random.Range): not seeded by the game; the C# facade's
	/// (System.Random) draw for draw, so a run seeded with InitState (the screenshots) matches the C# track's.
	struct Random
	{
		static void InitState(int seed);
		static float value();
		static float Range(float minInclusive, float maxInclusive);
		static int Range(int minInclusive, int maxExclusive);
	};

	// ---------------------------------------------------------------- Resources

	/// TextAsset: the bytes of a Resources data file (Data/asteroids.bytes).
	class TextAsset final : public Object
	{
	public:
		cs::List<uint8_t> bytes;
	};

	/// What a Resources load can give: a texture, a model (a GameObject prototype, inactive) or a data file.
	using ResourceAsset = std::variant<std::monostate, cs::Ref<Texture2D>, cs::Ref<GameObject>, cs::Ref<TextAsset>>;

	enum class ResourceType : int { Texture2D, GameObject, TextAsset };

	/// UnityEngine.ResourceRequest: an asynchronous load, completed on the game thread (Resources::Pump).
	class ResourceRequest final
	{
	public:
		bool isDone = false;
		ResourceAsset asset;
		/// `request.completed += handler` (raised once, on the frame the load is done).
		cs::Event<cs::Ref<ResourceRequest>> completed;
	};

	/// UnityEngine.Resources over the shared plugin's imports of Unity.SpaceGod's Assets/UnitySpace/Resources (US-T27's
	/// spacegod_import_resources.py): a texture "Textures/earth_color" is the UTexture2D /SpaceGodRender/Resources/
	/// Textures/earth_color, a ship model "Ships/craft_miner" the Interchange static meshes listed (with their glTF
	/// materials and the import's scale) in Data/ShipModels.json, and a data file "Data/asteroids" the file
	/// asteroids.bytes, read as it is. Both tracks draw from the very same assets. A path is Unity's: no extension.
	struct Resources
	{
		static constexpr const char* AssetRoot = "/SpaceGodRender/Resources";
		/// The object path of an imported asset ("/SpaceGodRender/Resources/Textures/x.x").
		static cs::String ObjectPath(const cs::String& path);
		/// The data file a Resources path names (Data/asteroids -> .../asteroids.bytes), or empty if there is none.
		static cs::String FileFor(const cs::String& path);
		static cs::Ref<Texture2D> LoadTexture(const cs::String& path);
		static cs::Ref<TextAsset> LoadText(const cs::String& path);
		static cs::Ref<GameObject> LoadModel(const cs::String& path);
		/// Null when there is no such file (Unity's LoadAsync gives null for a missing path's type, too).
		static cs::Ref<ResourceRequest> LoadAsync(const cs::String& path, ResourceType type);
		static void UnloadAsset(const ResourceAsset& asset);
		static void UnloadAsset(const cs::Ref<Texture2D>& texture);
		/// Finishes the loads whose work is done (raising completed); UnityScene calls it once a frame.
		static void Pump();
		/// Loads still on their way.
		static int Pending();
	};

	/// Unreal's transform from an imported model part (Mesh::ImportedAsset) to the Unreal local frame of its Unity
	/// transform, in Unity units (see UnityScene).
	FTransform ImportedCorrection(float scale);

	/// A texture made from a decoded image (or from raw RGBA8 mip levels), wrapped as a Unity Texture2D.
	struct TextureFactory
	{
		/// An image file decoded to BGRA8 levels, top row first (level 0 down to 1x1).
		struct DecodedImage
		{
			int Width = 0, Height = 0;
			std::vector<std::vector<uint8_t>> Levels;
			bool Ok() const { return Width > 0 && !Levels.empty(); }
		};
		/// Decodes a .jpg/.png on any thread (no Unreal object is made).
		static DecodedImage Decode(const cs::String& file);
		/// BGRA8 level 0 (top row first) to a full mip chain.
		static std::vector<std::vector<uint8_t>> MipChain(const uint8_t* bgra, int width, int height);
		/// sRGB colour (the game's maps), Unity's Repeat wrap; linear for data (normal maps: Unity imports them linear).
		static cs::Ref<Texture2D> FromImageFile(const cs::String& file, bool linear);
		/// RGBA8 levels (level 0 first), width x height at level 0.
		static cs::Ref<Texture2D> FromMips(const cs::String& name, int width, int height, const std::vector<std::vector<uint8_t>>& levels,
			TextureWrapMode wrapU, TextureWrapMode wrapV, bool linear);
		/// Wraps an Unreal texture made elsewhere (kept from the garbage collector while the Texture2D lives).
		static cs::Ref<Texture2D> Wrap(UTexture* texture, int width, int height, const cs::String& name);
		/// Uploads a Texture2D made from pixels (new Texture2D + SetPixel + Apply) if it has no Unreal texture yet.
		static void EnsureNative(Texture2D& texture);
	};
}
