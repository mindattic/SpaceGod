#include "UnityCompat/UnityRendering.h"
#include "UnrealSpace.h"
#include "UnrealSpaceGodLog.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"

#include <atomic>
#include <mutex>
#include <random>

namespace SpaceGod::UnityCompat
{
	static FString ToF(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }
	static cs::String FromF(const FString& s) { return cs::String(TCHAR_TO_UTF8(*s)); }

	// ---------------------------------------------------------------- Mesh

	void Mesh::Clear(bool keepVertexLayout)
	{
		Vertices.clear();
		Normals.clear();
		Colors.clear();
		for (auto& uv : Uv) uv.clear();
		SubMeshes.clear();
		meshBounds = Bounds();
		ExplicitBounds = false;
		MarkModified();
	}

	void Mesh::RecalculateBounds()
	{
		Bounds b;
		if (!Vertices.empty())
		{
			Vector3 mn = Vertices[0], mx = Vertices[0];
			for (const auto& v : Vertices)
			{
				mn = Vector3::Min(mn, v);
				mx = Vector3::Max(mx, v);
			}
			b.SetMinMax(mn, mx);
		}
		meshBounds = b;
		ExplicitBounds = false;
		MarkModified();
	}

	void Mesh::RecalculateNormals()
	{
		std::vector<Vector3> n(Vertices.size());
		for (const auto& sub : SubMeshes)
		{
			if (sub.Topology != MeshTopology::Triangles) continue;
			for (size_t i = 0; i + 2 < sub.Indices.size(); i += 3)
			{
				int a = sub.Indices[i], b = sub.Indices[i + 1], c = sub.Indices[i + 2];
				if (a < 0 || b < 0 || c < 0 || a >= vertexCount() || b >= vertexCount() || c >= vertexCount()) continue;
				// Unity's front face is clockwise: (b - a) x (c - a) points out of it.
				Vector3 f = Vector3::Cross(Vertices[b] - Vertices[a], Vertices[c] - Vertices[a]).normalized();
				n[a] += f;
				n[b] += f;
				n[c] += f;
			}
		}
		for (auto& v : n) v = v.normalized();
		Normals = std::move(n);
		MarkModified();
	}

	void Mesh::SetVertices(const std::vector<Vector3>& v)
	{
		Vertices = v;
		// Unity grows the bounds to new vertices only on SetTriangles/RecalculateBounds; keep the given ones.
		MarkModified();
	}

	void Mesh::SetNormals(const std::vector<Vector3>& n)
	{
		Normals = n;
		MarkModified();
	}

	void Mesh::SetColors(const std::vector<Color>& c)
	{
		Colors = c;
		MarkModified();
	}

	void Mesh::SetUVs(int channel, const std::vector<Vector2>& uv)
	{
		if (channel < 0 || channel > 3) return;
		auto& dst = Uv[channel];
		dst.resize(uv.size());
		for (size_t i = 0; i < uv.size(); i++) dst[i] = Vector4(uv[i].x, uv[i].y, 0, 0);
		MarkModified();
	}

	void Mesh::SetUVs(int channel, const std::vector<Vector3>& uv)
	{
		if (channel < 0 || channel > 3) return;
		auto& dst = Uv[channel];
		dst.resize(uv.size());
		for (size_t i = 0; i < uv.size(); i++) dst[i] = Vector4(uv[i].x, uv[i].y, uv[i].z, 0);
		MarkModified();
	}

	void Mesh::SetUVs(int channel, const std::vector<Vector4>& uv)
	{
		if (channel < 0 || channel > 3) return;
		Uv[channel] = uv;
		MarkModified();
	}

	void Mesh::SetTriangles(const std::vector<int>& triangles, int submesh, bool calculateBounds)
	{
		SetIndices(triangles, MeshTopology::Triangles, submesh, calculateBounds);
	}

	void Mesh::SetIndices(const std::vector<int>& indices, MeshTopology topology, int submesh, bool calculateBounds)
	{
		if (submesh < 0) return;
		if (static_cast<int>(SubMeshes.size()) <= submesh) SubMeshes.resize(static_cast<size_t>(submesh) + 1);
		SubMeshes[static_cast<size_t>(submesh)].Indices = indices;
		SubMeshes[static_cast<size_t>(submesh)].Topology = topology;
		if (calculateBounds) RecalculateBounds();
		MarkModified();
	}

	// ---------------------------------------------------------------- Shader

	static std::unordered_map<cs::String, cs::Ref<Shader>>& Shaders()
	{
		static std::unordered_map<cs::String, cs::Ref<Shader>> shaders;
		return shaders;
	}

	cs::Ref<Shader> Shader::Find(const cs::String& name)
	{
		auto& all = Shaders();
		auto it = all.find(name);
		if (it != all.end()) return it->second;
		auto s = cs::New<Shader>(name);
		all[name] = s;
		return s;
	}

	static MaterialProperties& GlobalStore()
	{
		static MaterialProperties globals;
		return globals;
	}

	const MaterialProperties& Shader::Globals() { return GlobalStore(); }
	void Shader::SetGlobalVector(const cs::String& name, Vector4 value) { GlobalStore().SetVector(name, value); }
	Vector4 Shader::GetGlobalVector(const cs::String& name) { return GlobalStore().GetVector(name); }
	void Shader::SetGlobalFloat(const cs::String& name, float value) { GlobalStore().SetFloat(name, value); }
	float Shader::GetGlobalFloat(const cs::String& name) { return GlobalStore().GetFloat(name); }

	// ---------------------------------------------------------------- properties

	MaterialValue& MaterialProperties::Slot(const cs::String& name, MaterialValue::Kind kind)
	{
		++Version;
		auto it = index.find(name);
		if (it != index.end())
		{
			auto& v = values[it->second].second;
			v.kind = kind;
			return v;
		}
		index[name] = values.size();
		values.emplace_back(name, MaterialValue());
		values.back().second.kind = kind;
		return values.back().second;
	}

	void MaterialProperties::SetFloat(const cs::String& name, float value) { Slot(name, MaterialValue::Kind::Float).f = value; }
	void MaterialProperties::SetVector(const cs::String& name, Vector4 value) { Slot(name, MaterialValue::Kind::Vector).v = value; }
	void MaterialProperties::SetColor(const cs::String& name, Color c) { Slot(name, MaterialValue::Kind::Color).v = Vector4(c.r, c.g, c.b, c.a); }
	void MaterialProperties::SetTexture(const cs::String& name, const cs::Ref<Texture>& value) { Slot(name, MaterialValue::Kind::Texture).texture = value; }

	void MaterialProperties::SetFloatArray(const cs::String& name, const std::vector<float>& v)
	{
		Slot(name, MaterialValue::Kind::FloatArray).array = v;
	}

	void MaterialProperties::SetFloatArray(const cs::String& name, const cs::List<float>& v) { SetFloatArray(name, std::vector<float>(v.begin(), v.end())); }

	void MaterialProperties::Clear()
	{
		values.clear();
		index.clear();
		++Version;
	}

	const MaterialValue* MaterialProperties::Find(const cs::String& name) const
	{
		auto it = index.find(name);
		return it == index.end() ? nullptr : &values[it->second].second;
	}

	float MaterialProperties::GetFloat(const cs::String& name) const
	{
		auto v = Find(name);
		return v ? v->f : 0.0f;
	}

	Color MaterialProperties::GetColor(const cs::String& name) const
	{
		auto v = Find(name);
		return v ? Color(v->v.x, v->v.y, v->v.z, v->v.w) : Color::white();
	}

	Vector4 MaterialProperties::GetVector(const cs::String& name) const
	{
		auto v = Find(name);
		return v ? v->v : Vector4();
	}

	cs::Ref<Texture> MaterialProperties::GetTexture(const cs::String& name) const
	{
		auto v = Find(name);
		return v ? v->texture : nullptr;
	}

	// ---------------------------------------------------------------- components

	cs::Ref<Transform> Component::transform() const
	{
		auto go = gameObject();
		return go ? go->transform : nullptr;
	}

	cs::Ref<Component> MeshFilter::Clone() const
	{
		auto c = cs::New<MeshFilter>();
		c->name = name;
		c->sharedMesh = sharedMesh;
		return c;
	}

	bool MaterialProperties::SameAs(const MaterialProperties& other) const
	{
		if (values.size() != other.values.size()) return false;
		for (size_t i = 0; i < values.size(); i++)
		{
			const auto& [n, a] = values[i];
			const auto& [m, b] = other.values[i];
			if (n != m || a.kind != b.kind || a.f != b.f || a.v.x != b.v.x || a.v.y != b.v.y || a.v.z != b.v.z || a.v.w != b.v.w
				|| a.texture != b.texture || a.array != b.array)
				return false;
		}
		return true;
	}

	void Renderer::SetPropertyBlock(const MaterialPropertyBlock& b)
	{
		// Unity copies the block each call; reaching Unreal again only matters when something changed.
		if (hasBlock && block.SameAs(b)) return;
		block = b;
		hasBlock = true;
		++BlockVersion;
	}

	cs::Ref<Mesh> Renderer::DrawnMesh() const
	{
		auto filter = GetComponent<MeshFilter>(gameObject());
		return filter ? filter->sharedMesh : nullptr;
	}

	Bounds Renderer::bounds() const
	{
		auto mesh = DrawnMesh();
		auto t = transform();
		if (!mesh || !t) return Bounds(t ? t->position() : Vector3(), Vector3());
		auto m = WorldMatrix::Of(*t);
		Bounds local = mesh->bounds();
		Vector3 mn = local.min(), mx = local.max();
		Bounds world(m.MultiplyPoint(mn), Vector3());
		for (int i = 0; i < 8; i++)
			world.Encapsulate(m.MultiplyPoint(Vector3((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z)));
		return world;
	}

	void Renderer::CopyRendererTo(Renderer& r) const
	{
		r.name = name;
		r.enabled = enabled;
		r.shadowCastingMode = shadowCastingMode;
		r.receiveShadows = receiveShadows;
		r.materials = materials;
		if (hasBlock) r.SetPropertyBlock(block);
	}

	cs::Ref<Component> MeshRenderer::Clone() const
	{
		auto c = cs::New<MeshRenderer>();
		CopyRendererTo(*c);
		return c;
	}

	SceneRegistry& SceneRegistry::Get()
	{
		static SceneRegistry registry;
		return registry;
	}

	ComponentBag& SceneRegistry::BagOf(const cs::Ref<GameObject>& go)
	{
		auto& bag = bags[go.get()];
		if (!bag.Owner) bag.Owner = go;
		return bag;
	}

	const ComponentBag* SceneRegistry::Find(const GameObject* go) const
	{
		auto it = bags.find(go);
		return it == bags.end() ? nullptr : &it->second;
	}

	void SceneRegistry::ForEachRenderer(const std::function<void(const cs::Ref<Renderer>&)>& visit) const
	{
		for (auto& kv : bags)
		{
			if (kv.second.Owner->destroyed) continue;
			for (auto& c : kv.second.Components)
				if (auto r = std::dynamic_pointer_cast<Renderer>(c)) visit(r);
		}
	}

	void SceneRegistry::Purge(const std::function<void(Renderer&)>& onDropped)
	{
		for (auto it = bags.begin(); it != bags.end();)
		{
			if (!it->second.Owner->destroyed)
			{
				++it;
				continue;
			}
			for (auto& c : it->second.Components)
			{
				c->destroyed = true;
				if (auto r = std::dynamic_pointer_cast<Renderer>(c)) onDropped(*r);
			}
			it = bags.erase(it);
		}
	}

	cs::Ref<GameObject> NewGameObject(const cs::String& name)
	{
		auto go = GameObject::Create(name);
		SceneRegistry::Get().BagOf(go);
		return go;
	}

	// ---------------------------------------------------------------- transforms

	static void Mul(const float a[3][3], const float b[3][3], float out[3][3])
	{
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
				out[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
	}

	Vector3 WorldMatrix::MultiplyVector(Vector3 v) const
	{
		return Vector3(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
			m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
			m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
	}

	Vector3 WorldMatrix::MultiplyPoint(Vector3 p) const { return MultiplyVector(p) + t; }

	WorldMatrix WorldMatrix::Of(const Transform& tr)
	{
		// Local TRS: columns of the rotation scaled by the local scale.
		WorldMatrix local;
		Vector3 cx = tr.localRotation * Vector3(tr.localScale.x, 0, 0);
		Vector3 cy = tr.localRotation * Vector3(0, tr.localScale.y, 0);
		Vector3 cz = tr.localRotation * Vector3(0, 0, tr.localScale.z);
		local.m[0][0] = cx.x; local.m[0][1] = cy.x; local.m[0][2] = cz.x;
		local.m[1][0] = cx.y; local.m[1][1] = cy.y; local.m[1][2] = cz.y;
		local.m[2][0] = cx.z; local.m[2][1] = cy.z; local.m[2][2] = cz.z;
		local.t = tr.localPosition;
		auto parent = tr.parent.lock();
		if (!parent) return local;
		WorldMatrix p = Of(*parent);
		WorldMatrix w;
		Mul(p.m, local.m, w.m);
		w.t = p.MultiplyPoint(local.t);
		return w;
	}

	// ---------------------------------------------------------------- destroy, instantiate

	void Destroy(const cs::Ref<GameObject>& go) { GameObject::Destroy(go); }

	void Destroy(const cs::Ref<Mesh>& mesh)
	{
		if (mesh) mesh->destroyed = true;
	}

	void Destroy(const cs::Ref<Material>& material)
	{
		if (material) material->destroyed = true;
	}

	void Destroy(const cs::Ref<Texture2D>&)
	{
		// Released when its last reference goes (UnityTexture.h).
	}

	static cs::Ref<GameObject> CloneTree(const cs::Ref<GameObject>& src, const cs::Ref<Transform>& parent)
	{
		auto copy = NewGameObject(src->name);
		copy->tag = src->tag;
		copy->activeSelf = src->activeSelf;
		copy->transform->localPosition = src->transform->localPosition;
		copy->transform->localRotation = src->transform->localRotation;
		copy->transform->localScale = src->transform->localScale;
		if (parent) copy->transform->SetParent(parent, false);
		if (auto bag = SceneRegistry::Get().Find(src.get()))
		{
			auto& dst = SceneRegistry::Get().BagOf(copy).Components;
			for (auto& c : bag->Components)
			{
				auto clone = c->Clone();
				clone->owner = copy;
				dst.push_back(clone);
			}
		}
		for (auto& child : src->transform->children)
			if (auto go = child->gameObject()) CloneTree(go, copy->transform);
		return copy;
	}

	cs::Ref<GameObject> Instantiate(const cs::Ref<GameObject>& original, const cs::Ref<Transform>& parent, bool worldPositionStays)
	{
		if (!original) return nullptr;
		auto copy = CloneTree(original, nullptr);
		if (parent) copy->transform->SetParent(parent, worldPositionStays);
		return copy;
	}

	// ---------------------------------------------------------------- Random

	// The C# facade's generator, draw for draw (US-T45): .NET's System.Random (cs::Random is bit-exact with it), clock
	// seeded until InitState, so a seeded run (-Mode shots) gives both tracks the same flares, glow phases and spins.
	static cs::Random& Rng()
	{
		static cs::Random rng;
		return rng;
	}

	void Random::InitState(int seed) { Rng() = cs::Random(seed); }
	float Random::value() { return static_cast<float>(Rng().NextDouble()); }
	float Random::Range(float a, float b) { return a + static_cast<float>(Rng().NextDouble()) * (b - a); }
	int Random::Range(int a, int b) { return b <= a ? a : Rng().Next(a, b); }

	// ---------------------------------------------------------------- textures

	/// A Texture2D over an Unreal texture made here, kept from the garbage collector as long as it is referenced.
	class HeldTexture2D final : public Texture2D
	{
	public:
		HeldTexture2D(UTexture* texture, int w, int h) : hold(new TStrongObjectPtr<UTexture>(texture))
		{
			Native = texture;
			width = w;
			height = h;
			OwnsNative = false;
		}

		~HeldTexture2D() override
		{
			// A static texture (a sky, a swatch) is destroyed at exit, after the object system has gone: the reference
			// is left as it is then (releasing it would touch freed engine state).
			if (UObjectInitialized() && !GExitPurge) delete hold;
		}

	private:
		TStrongObjectPtr<UTexture>* hold;
	};

	static TextureAddress ToAddress(TextureWrapMode w)
	{
		switch (w)
		{
			case TextureWrapMode::Clamp: return TA_Clamp;
			case TextureWrapMode::Mirror:
			case TextureWrapMode::MirrorOnce: return TA_Mirror;
			default: return TA_Wrap;
		}
	}

	static UTexture2D* MakeTexture(const FString& name, int width, int height, const std::vector<std::vector<uint8_t>>& levels,
		TextureWrapMode wrapU, TextureWrapMode wrapV, bool linear)
	{
		UTexture2D* tex = NewObject<UTexture2D>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), FName(*name)), RF_Transient);
		auto* data = new FTexturePlatformData();
		data->SizeX = width;
		data->SizeY = height;
		data->PixelFormat = PF_B8G8R8A8;
		int w = width, h = height;
		for (const auto& level : levels)
		{
			auto* mip = new FTexture2DMipMap(w, h, 1);
			data->Mips.Add(mip);
			mip->BulkData.Lock(LOCK_READ_WRITE);
			void* dst = mip->BulkData.Realloc(static_cast<int64>(level.size()));
			FMemory::Memcpy(dst, level.data(), level.size());
			mip->BulkData.Unlock();
			w = FMath::Max(1, w / 2);
			h = FMath::Max(1, h / 2);
		}
		tex->SetPlatformData(data);
		tex->SRGB = !linear;
		tex->AddressX = ToAddress(wrapU);
		tex->AddressY = ToAddress(wrapV);
		tex->Filter = TF_Trilinear;
		tex->NeverStream = true;
		tex->UpdateResource();
		return tex;
	}

	/// BGRA8 levels from level 0 down to 1x1 (a 2x2 box filter, as Unity's default mip filter).
	std::vector<std::vector<uint8_t>> TextureFactory::MipChain(const uint8_t* bgra, int width, int height)
	{
		std::vector<std::vector<uint8_t>> levels;
		levels.emplace_back(bgra, bgra + static_cast<size_t>(width) * height * 4);
		int w = width, h = height;
		while (w > 1 || h > 1)
		{
			int nw = FMath::Max(1, w / 2), nh = FMath::Max(1, h / 2);
			const auto& src = levels.back();
			std::vector<uint8_t> dst(static_cast<size_t>(nw) * nh * 4);
			for (int y = 0; y < nh; y++)
				for (int x = 0; x < nw; x++)
					for (int c = 0; c < 4; c++)
					{
						int sum = 0, n = 0;
						for (int dy = 0; dy < 2; dy++)
							for (int dx = 0; dx < 2; dx++)
							{
								int sx = FMath::Min(x * 2 + dx, w - 1), sy = FMath::Min(y * 2 + dy, h - 1);
								sum += src[(static_cast<size_t>(sy) * w + sx) * 4 + c];
								n++;
							}
						dst[(static_cast<size_t>(y) * nw + x) * 4 + c] = static_cast<uint8_t>((sum + n / 2) / n);
					}
			levels.push_back(std::move(dst));
			w = nw;
			h = nh;
		}
		return levels;
	}

	TextureFactory::DecodedImage TextureFactory::Decode(const cs::String& file)
	{
		DecodedImage out;
		FImage image;
		if (!FImageUtils::LoadImage(*ToF(file), image)) return out;
		image.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		out.Width = image.SizeX;
		out.Height = image.SizeY;
		out.Levels = MipChain(image.RawData.GetData(), image.SizeX, image.SizeY);
		return out;
	}

	cs::Ref<Texture2D> TextureFactory::FromMips(const cs::String& name, int width, int height, const std::vector<std::vector<uint8_t>>& levels,
		TextureWrapMode wrapU, TextureWrapMode wrapV, bool linear)
	{
		if (width <= 0 || height <= 0 || levels.empty()) return nullptr;
		UTexture2D* tex = MakeTexture(ToF(name), width, height, levels, wrapU, wrapV, linear);
		auto t = cs::New<HeldTexture2D>(tex, width, height);
		t->name = name;
		t->wrapMode = wrapU;
		return t;
	}

	cs::Ref<Texture2D> TextureFactory::Wrap(UTexture* texture, int width, int height, const cs::String& name)
	{
		if (!texture) return nullptr;
		cs::Ref<Texture2D> t = cs::New<HeldTexture2D>(texture, width, height);
		t->name = name;
		return t;
	}

	static bool IsLinearMap(const FString& file)
	{
		// Unity imports the relief maps as normal maps (linear); every colour map is sRGB.
		return FPaths::GetBaseFilename(file).Contains(TEXT("_normal"));
	}

	cs::Ref<Texture2D> TextureFactory::FromImageFile(const cs::String& file, bool linear)
	{
		auto decoded = Decode(file);
		if (!decoded.Ok()) return nullptr;
		return FromMips(FromF(FPaths::GetBaseFilename(ToF(file))), decoded.Width, decoded.Height, decoded.Levels,
			TextureWrapMode::Repeat, TextureWrapMode::Repeat, linear);
	}

	void TextureFactory::EnsureNative(Texture2D& texture)
	{
		if (texture.Native || texture.pixels.Count() == 0 || texture.width <= 0 || texture.height <= 0) return;
		// Unity's rows run bottom-up; Unreal's top-down.
		std::vector<uint8_t> bgra(static_cast<size_t>(texture.width) * texture.height * 4);
		for (int y = 0; y < texture.height; y++)
			for (int x = 0; x < texture.width; x++)
			{
				const auto& p = texture.pixels[y * texture.width + x];
				size_t o = (static_cast<size_t>(texture.height - 1 - y) * texture.width + x) * 4;
				bgra[o] = p.b;
				bgra[o + 1] = p.g;
				bgra[o + 2] = p.r;
				bgra[o + 3] = p.a;
			}
		auto levels = texture.mipChain ? TextureFactory::MipChain(bgra.data(), texture.width, texture.height) : std::vector<std::vector<uint8_t>>{ bgra };
		UTexture2D* tex = MakeTexture(ToF(texture.name.empty() ? cs::String("Texture2D") : texture.name), texture.width, texture.height, levels,
			texture.wrapMode, texture.wrapMode, texture.linear);
		if (texture.filterMode == FilterMode::Point) tex->Filter = TF_Nearest;
		// Owned: UnityTexture.h releases it from the root with the Texture2D.
		tex->AddToRoot();
		texture.Native = tex;
		texture.OwnsNative = true;
		texture.Dirty = false;
	}

	// ---------------------------------------------------------------- Resources

	FTransform ImportedCorrection(float scale)
	{
		// Unity's glTF import is glTF with x negated, (-x, y, z); Unreal's Interchange import is (x, z, y) times its unit
		// scale. Through UnrealSpace's (Unity.z, Unity.x, Unity.y) the Unity mesh is (z, -x, y): from the import that is
		// X' = Y, Y' = -X, a quarter turn about Z (yaw -90), and the scale back to the glTF's units.
		return FTransform(FRotator(0, -90, 0), FVector::ZeroVector, FVector(scale));
	}

	cs::String Resources::ObjectPath(const cs::String& path)
	{
		auto slash = path.rfind('/');
		cs::String leaf = slash == cs::String::npos ? path : path.substr(slash + 1);
		return cs::String(AssetRoot) + "/" + path + "." + leaf;
	}

	cs::String Resources::FileFor(const cs::String& path)
	{
		// The shared plugin's Content/Resources first, then where the C# track's import put the data files
		// (CSharp/Content/SpaceGod/Data, staged by its DefaultGame.ini).
		TArray<FString> roots;
		if (auto plugin = IPluginManager::Get().FindPlugin(TEXT("SpaceGodRender"))) roots.Add(plugin->GetContentDir() / TEXT("Resources"));
		roots.Add(FPaths::ProjectDir() / TEXT("../CSharp/Content/SpaceGod"));
		roots.Add(FPaths::ProjectContentDir() / TEXT("SpaceGod"));
		const TCHAR* exts[] = { TEXT(".bytes"), TEXT(".json"), TEXT(".txt") };
		for (const FString& root : roots)
			for (const TCHAR* ext : exts)
			{
				FString f = FPaths::ConvertRelativePathToFull(root / ToF(path) + ext);
				if (FPaths::FileExists(f)) return FromF(f);
			}
		return cs::String();
	}

	namespace
	{
		/// Resources.Load gives the same object for a path every time: one Texture2D per imported texture.
		std::unordered_map<cs::String, std::weak_ptr<Texture2D>>& LoadedTextures()
		{
			static std::unordered_map<cs::String, std::weak_ptr<Texture2D>> loaded;
			return loaded;
		}

		cs::Ref<Texture2D> WrapTexture(const cs::String& path, UTexture2D* tex)
		{
			if (!tex) return nullptr;
			auto& loaded = LoadedTextures();
			if (auto t = loaded[path].lock(); t && t->Native == tex) return t;
			cs::Ref<Texture2D> t = cs::New<HeldTexture2D>(tex, tex->GetSizeX(), tex->GetSizeY());
			t->name = path;
			loaded[path] = t;
			return t;
		}

		FStreamableManager& Streamable()
		{
			static FStreamableManager manager;
			return manager;
		}

		/// Data/ShipModels.json (US-T27's import): per model, its parts in node order (static mesh, glTF material name and
		/// linear base colour) and the scale back to the glTF's units.
		struct ModelPart
		{
			FString Mesh, Material;
			FLinearColor BaseColor = FLinearColor::White;
		};

		struct ModelSpec
		{
			std::vector<ModelPart> Parts;
			float Scale = 1;
		};

		const std::unordered_map<cs::String, ModelSpec>& ModelSpecs()
		{
			static std::unordered_map<cs::String, ModelSpec> specs = []
			{
				std::unordered_map<cs::String, ModelSpec> out;
				auto file = Resources::FileFor("Data/ShipModels");
				FString text;
				if (file.empty() || !FFileHelper::LoadFileToString(text, *ToF(file)))
				{
					UE_LOG(LogSpaceGod, Warning, TEXT("Resources: no Data/ShipModels.json (the ship models' import); Kenney craft fall back to procedural hulls"));
					return out;
				}
				TSharedPtr<FJsonObject> json;
				if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(text), json) || !json) return out;
				for (const auto& kv : json->Values)
				{
					auto o = kv.Value->AsObject();
					if (!o) continue;
					ModelSpec spec;
					double scale = 1;
					if (o->TryGetNumberField(TEXT("correctionScale"), scale)) spec.Scale = static_cast<float>(scale);
					const TArray<TSharedPtr<FJsonValue>>* parts = nullptr;
					if (o->TryGetArrayField(TEXT("parts"), parts))
						for (const auto& pv : *parts)
						{
							auto po = pv->AsObject();
							ModelPart part;
							po->TryGetStringField(TEXT("mesh"), part.Mesh);
							po->TryGetStringField(TEXT("material"), part.Material);
							const TArray<TSharedPtr<FJsonValue>>* c = nullptr;
							if (po->TryGetArrayField(TEXT("baseColor"), c) && c->Num() == 4)
								part.BaseColor = FLinearColor((*c)[0]->AsNumber(), (*c)[1]->AsNumber(), (*c)[2]->AsNumber(), (*c)[3]->AsNumber());
							spec.Parts.push_back(part);
						}
					out[FromF(FString(kv.Key))] = spec;
				}
				return out;
			}();
			return specs;
		}

		cs::String Leaf(const cs::String& path)
		{
			auto slash = path.rfind('/');
			return slash == cs::String::npos ? path : path.substr(slash + 1);
		}

		/// An imported part's vertex as it is drawn in Unreal's local frame of its Unity transform, in Unity units.
		FVector ImportedToLocal(const FVector& imported, float scale) { return ImportedCorrection(scale).TransformPosition(imported); }

		float LinearToSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * Mathf::Pow(c, 1.0f / 2.4f) - 0.055f; }

		/// The model as Unity's glTF import makes it: an inactive root with one child renderer per node, in node order,
		/// each with its glTF material (the base colour as the sRGB value Material.GetColor("_BaseColor") gives there).
		cs::Ref<GameObject> BuildModel(const cs::String& path, const ModelSpec& spec)
		{
			namespace US = SpaceGod::UnrealSpace;
			auto root = NewGameObject(Leaf(path));
			root->SetActive(false);
			auto shader = Shader::Find("glTF/PbrMetallicRoughness");
			for (const auto& part : spec.Parts)
			{
				UStaticMesh* asset = LoadObject<UStaticMesh>(nullptr, *part.Mesh);
				if (!asset) continue;
				auto go = NewGameObject(FromF(FPaths::GetBaseFilename(part.Mesh)));
				go->transform->SetParent(root->transform, false);
				auto mesh = cs::New<Mesh>(go->name);
				mesh->ImportedAsset = FromF(part.Mesh);
				mesh->ImportedScale = spec.Scale;
				// Its bounds in Unity's frame: the asset's box through the same correction UnityScene draws it with.
				FBox box = asset->GetBoundingBox();
				Bounds b;
				for (int i = 0; i < 8; i++)
				{
					FVector corner((i & 1) ? box.Max.X : box.Min.X, (i & 2) ? box.Max.Y : box.Min.Y, (i & 4) ? box.Max.Z : box.Min.Z);
					Vector3 u = ToVector3(US::FromUnreal(ImportedToLocal(corner, spec.Scale) * US::CmPerUnit));
					if (i == 0) b = Bounds(u, Vector3());
					else b.Encapsulate(u);
				}
				mesh->SetBounds(b);
				AddComponent<MeshFilter>(go)->sharedMesh = mesh;
				auto material = cs::New<Material>(shader, FromF(part.Material));
				material->SetColor("_BaseColor", Color(LinearToSrgb(part.BaseColor.R), LinearToSrgb(part.BaseColor.G), LinearToSrgb(part.BaseColor.B), part.BaseColor.A));
				AddComponent<MeshRenderer>(go)->SetSharedMaterial(material);
			}
			return root;
		}
	}

	cs::Ref<Texture2D> Resources::LoadTexture(const cs::String& path)
	{
		return WrapTexture(path, LoadObject<UTexture2D>(nullptr, *ToF(ObjectPath(path)), nullptr, LOAD_NoWarn | LOAD_Quiet));
	}

	cs::Ref<TextAsset> Resources::LoadText(const cs::String& path)
	{
		auto file = FileFor(path);
		if (file.empty()) return nullptr;
		TArray<uint8> bytes;
		if (!FFileHelper::LoadFileToArray(bytes, *ToF(file))) return nullptr;
		auto asset = cs::New<TextAsset>();
		asset->name = path;
		asset->bytes = cs::List<uint8_t>(bytes.GetData(), bytes.GetData() + bytes.Num());
		return asset;
	}

	cs::Ref<GameObject> Resources::LoadModel(const cs::String& path)
	{
		const auto& specs = ModelSpecs();
		auto it = specs.find(Leaf(path));
		return it == specs.end() ? nullptr : BuildModel(path, it->second);
	}

	namespace
	{
		struct PendingLoad
		{
			cs::Ref<ResourceRequest> Request;
			ResourceType Type = ResourceType::Texture2D;
			cs::String Path;
			TArray<uint8> Bytes;
			std::atomic<bool> Done{ false };
		};

		std::vector<std::shared_ptr<PendingLoad>>& PendingLoads()
		{
			static std::vector<std::shared_ptr<PendingLoad>> pending;
			return pending;
		}
	}

	cs::Ref<ResourceRequest> Resources::LoadAsync(const cs::String& path, ResourceType type)
	{
		auto load = std::make_shared<PendingLoad>();
		load->Request = cs::New<ResourceRequest>();
		load->Type = type;
		load->Path = path;
		switch (type)
		{
			case ResourceType::Texture2D:
			{
				FSoftObjectPath asset(ToF(ObjectPath(path)));
				if (!FPackageName::DoesPackageExist(asset.GetLongPackageName())) return nullptr;
				Streamable().RequestAsyncLoad(asset, FStreamableDelegate::CreateLambda([load] { load->Done = true; }));
				break;
			}
			case ResourceType::GameObject:
			{
				const auto& specs = ModelSpecs();
				auto it = specs.find(Leaf(path));
				if (it == specs.end()) return nullptr;
				TArray<FSoftObjectPath> parts;
				for (const auto& part : it->second.Parts) parts.Add(FSoftObjectPath(part.Mesh));
				Streamable().RequestAsyncLoad(parts, FStreamableDelegate::CreateLambda([load] { load->Done = true; }));
				break;
			}
			default:
			{
				auto file = FileFor(path);
				if (file.empty()) return nullptr;
				Async(EAsyncExecution::ThreadPool, [load, file]
				{
					FFileHelper::LoadFileToArray(load->Bytes, *ToF(file));
					load->Done = true;
				});
				break;
			}
		}
		PendingLoads().push_back(load);
		return load->Request;
	}

	int Resources::Pending() { return static_cast<int>(PendingLoads().size()); }

	void Resources::Pump()
	{
		auto& pending = PendingLoads();
		std::vector<std::shared_ptr<PendingLoad>> done;
		for (auto it = pending.begin(); it != pending.end();)
		{
			if ((*it)->Done)
			{
				done.push_back(*it);
				it = pending.erase(it);
			}
			else ++it;
		}
		for (auto& load : done)
		{
			auto& req = *load->Request;
			switch (load->Type)
			{
				case ResourceType::Texture2D:
					if (auto t = LoadTexture(load->Path)) req.asset = t;
					break;
				case ResourceType::GameObject:
					if (auto m = LoadModel(load->Path)) req.asset = m;
					break;
				default:
					if (load->Bytes.Num() > 0)
					{
						auto asset = cs::New<TextAsset>();
						asset->name = load->Path;
						asset->bytes = cs::List<uint8_t>(load->Bytes.GetData(), load->Bytes.GetData() + load->Bytes.Num());
						req.asset = asset;
					}
					break;
			}
			req.isDone = true;
			try
			{
				req.completed.Invoke(load->Request);
			}
			catch (const std::exception& e)
			{
				UE_LOG(LogSpaceGod, Error, TEXT("Resources %s: %s"), *ToF(load->Path), UTF8_TO_TCHAR(e.what()));
			}
		}
	}

	void Resources::UnloadAsset(const ResourceAsset& asset)
	{
		if (auto t = std::get_if<cs::Ref<Texture2D>>(&asset)) UnloadAsset(*t);
	}

	void Resources::UnloadAsset(const cs::Ref<Texture2D>& texture)
	{
		// Unity frees the texture's memory now; here the asset is let go of once nothing references it.
		if (!texture) return;
		auto& loaded = LoadedTextures();
		auto it = loaded.find(texture->name);
		if (it != loaded.end() && it->second.lock() == texture) loaded.erase(it);
	}
}
