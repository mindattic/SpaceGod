#include "UnityCompat/ShaderBinding.h"
#include "UnrealSpace.h"
#include "UnrealSpaceGodLog.h"

#include "Engine/Texture.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

#include <unordered_map>

namespace SpaceGod::UnityCompat
{
	static FName Name(const cs::String& s) { return FName(UTF8_TO_TCHAR(s.c_str())); }

	cs::String ShaderBinding::ShaderName(const Shader* shader)
	{
		if (!shader) return cs::String();
		const auto& n = shader->name;
		auto slash = n.rfind('/');
		return slash == cs::String::npos ? n : n.substr(slash + 1);
	}

	namespace
	{
		struct ParentEntry
		{
			TStrongObjectPtr<UMaterialInterface> Material;
			bool Real = false;
		};

		std::unordered_map<cs::String, ParentEntry>& Parents()
		{
			// Never destroyed: UObject references must not be released in static destruction, after the object system.
			static auto& parents = *new std::unordered_map<cs::String, ParentEntry>();
			return parents;
		}

		const ParentEntry& Resolve(const Shader* shader)
		{
			cs::String name = ShaderBinding::IsLit(shader) ? cs::String("Lit") : ShaderBinding::ShaderName(shader);
			auto& parents = Parents();
			auto it = parents.find(name);
			if (it != parents.end()) return it->second;
			ParentEntry e;
			FString path = FString::Printf(TEXT("%s/M_%s.M_%s"), UTF8_TO_TCHAR(ShaderBinding::MaterialRoot), UTF8_TO_TCHAR(name.c_str()), UTF8_TO_TCHAR(name.c_str()));
			UMaterialInterface* m = LoadObject<UMaterialInterface>(nullptr, *path, nullptr, LOAD_NoWarn | LOAD_Quiet);
			e.Real = m != nullptr;
			if (!m)
			{
				// The placeholder until SpaceGodRender has it.
				m = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
				UE_LOG(LogSpaceGod, Display, TEXT("ShaderBinding: %s has no %s yet; drawn with the placeholder"), UTF8_TO_TCHAR(name.c_str()), *path);
			}
			e.Material.Reset(m);
			return parents.emplace(name, std::move(e)).first->second;
		}

		/// Vector properties that hold a world position (Unreal axes and units).
		bool IsPosition(const cs::String& n)
		{
			return n == "_SunRelative" || n == "_SunPositionWS" || n == "_Sun2PositionWS";
		}

		/// Vector properties that hold a direction in Unity's world (Unreal axes).
		bool IsDirection(const cs::String& n) { return n == "_AxisX" || n == "_AxisY" || n == "_AxisZ"; }

		FLinearColor ToUnrealVector(const cs::String& n, Vector4 v)
		{
			namespace US = SpaceGod::UnrealSpace;
			if (IsPosition(n))
			{
				FVector p = US::ToUnreal(Vector3(v.x, v.y, v.z));
				return FLinearColor(static_cast<float>(p.X), static_cast<float>(p.Y), static_cast<float>(p.Z), v.w);
			}
			if (IsDirection(n))
			{
				FVector d = US::DirectionToUnreal(Vector3(v.x, v.y, v.z));
				return FLinearColor(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z), v.w);
			}
			if (n == "_PlanetPosRadius")
			{
				// A world position and a radius, both scene lengths.
				FVector p = US::ToUnreal(Vector3(v.x, v.y, v.z));
				return FLinearColor(static_cast<float>(p.X), static_cast<float>(p.Y), static_cast<float>(p.Z), static_cast<float>(v.w * US::CmPerUnit));
			}
			if (n == "_SunMap0")
			{
				// (blend, AU -> scene, game offset, game scale): the last three are scene lengths.
				float k = static_cast<float>(US::CmPerUnit);
				return FLinearColor(v.x, v.y * k, v.z * k, v.w * k);
			}
			return FLinearColor(v.x, v.y, v.z, v.w);
		}
	}

	UMaterialInterface* ShaderBinding::Parent(const Shader* shader) { return Resolve(shader).Material.Get(); }

	bool ShaderBinding::IsReal(const Shader* shader) { return Resolve(shader).Real; }

	int ShaderBinding::SortPriority(const Shader* shader)
	bool ShaderBinding::IsLit(const Shader* shader)
	{
		return shader && (shader->name == "glTF/PbrMetallicRoughness" || ShaderName(shader) == "Lit");
	}

	{
		cs::String n = ShaderName(shader);
		if (n == "Stars") return -1000;
		if (n == "Line") return -10;
		if (n == "Glow") return 10;
		if (n == "Fx") return 20;
		return 0;
	}

	MeshLayout ShaderBinding::LayoutFor(MeshTopology topology)
	{
		switch (topology)
		{
			case MeshTopology::Points: return MeshLayout::Points;
			case MeshTopology::Lines:
			case MeshTopology::LineStrip: return MeshLayout::Lines;
			default: return MeshLayout::Triangles;
		}
	}

	Color ShaderBinding::Linear(Color c)
	{
		auto f = [](float v) { return v <= 0.04045f ? v / 12.92f : Mathf::Pow((v + 0.055f) / 1.055f, 2.4f); };
		return Color(f(c.r), f(c.g), f(c.b), c.a);
	}

	namespace
	{
		float lineWidthPx = 1.0f;

		UMaterialInterface* LineParent()
		{
			// Never destroyed, as Parents(): kept loaded, and not released in static destruction.
			static auto& line = *new TStrongObjectPtr<UMaterialInterface>(LoadObject<UMaterialInterface>(nullptr,
				*FString::Printf(TEXT("%s/M_Line.M_Line"), UTF8_TO_TCHAR(ShaderBinding::MaterialRoot)), nullptr, LOAD_NoWarn | LOAD_Quiet));
			return line.Get();
		}

		const FName LineWidthParam(TEXT("_LineWidthPx"));
	}

	float ShaderBinding::LineWidthPx() { return lineWidthPx; }

	void ShaderBinding::SetLineWidthPx(float width)
	{
		lineWidthPx = width;
		UMaterialInterface* line = LineParent();
		if (!line) return;
		for (TObjectIterator<UMaterialInstanceDynamic> it; it; ++it)
			if (it->Parent == line) it->SetScalarParameterValue(LineWidthParam, width);
	}

	void ShaderBinding::Push(const MaterialProperties& properties, UMaterialInstanceDynamic* mid)
	{
		if (!mid) return;
		if (mid->Parent && mid->Parent == LineParent()) mid->SetScalarParameterValue(LineWidthParam, lineWidthPx); // US-T55
		for (const auto& [name, v] : properties.All())
		{
			switch (v.kind)
			{
				case MaterialValue::Kind::Float:
					mid->SetScalarParameterValue(Name(name), v.f);
					break;
				case MaterialValue::Kind::Color:
				{
					Color c = Linear(Color(v.v.x, v.v.y, v.v.z, v.v.w));
					mid->SetVectorParameterValue(Name(name), FLinearColor(c.r, c.g, c.b, c.a));
					break;
				}
				case MaterialValue::Kind::Vector:
					mid->SetVectorParameterValue(Name(name), ToUnrealVector(name, v.v));
					break;
				case MaterialValue::Kind::Texture:
				{
					UTexture* t = nullptr;
					if (v.texture)
					{
						if (auto t2 = std::dynamic_pointer_cast<Texture2D>(v.texture)) TextureFactory::EnsureNative(*t2);
						t = static_cast<UTexture*>(v.texture->Native);
					}
					// SetTexture(null) leaves the last texture bound (nothing draws with a cleared map: the sky is hidden then).
					if (t) mid->SetTextureParameterValue(Name(name), t);
					break;
				}
				case MaterialValue::Kind::FloatArray:
				{
					// No array parameters in Unreal: element k is component k % 4 of <name>_{k / 4}.
					for (size_t k = 0; k < v.array.size(); k += 4)
					{
						float c[4] = { 0, 0, 0, 0 };
						for (size_t j = 0; j < 4 && k + j < v.array.size(); j++) c[j] = v.array[k + j];
						mid->SetVectorParameterValue(Name(name + "_" + std::to_string(k / 4)), FLinearColor(c[0], c[1], c[2], c[3]));
					}
					break;
				}
			}
		}
	}

	UMaterialParameterCollection* ShaderBinding::Globals()
	{
		static auto& mpc = *new TStrongObjectPtr<UMaterialParameterCollection>(); // never destroyed, as the parents
		static bool tried = false;
		if (!tried)
		{
			tried = true;
			mpc.Reset(LoadObject<UMaterialParameterCollection>(nullptr, UTF8_TO_TCHAR(GlobalsCollection), nullptr, LOAD_NoWarn | LOAD_Quiet));
		}
		return mpc.Get();
	}

	void ShaderBinding::PushGlobals(UWorld* world, const MaterialProperties& globals)
	{
		auto* mpc = Globals();
		if (!world || !mpc) return;
		UMaterialParameterCollectionInstance* instance = world->GetParameterCollectionInstance(mpc);
		if (!instance) return;
		for (const auto& [name, v] : globals.All())
		{
			if (v.kind == MaterialValue::Kind::Float) instance->SetScalarParameterValue(Name(name), v.f);
			else if (v.kind == MaterialValue::Kind::Color)
			{
				Color c = Linear(Color(v.v.x, v.v.y, v.v.z, v.v.w));
				instance->SetVectorParameterValue(Name(name), FLinearColor(c.r, c.g, c.b, c.a));
			}
			else if (v.kind == MaterialValue::Kind::Vector) instance->SetVectorParameterValue(Name(name), ToUnrealVector(name, v.v));
		}
	}
}
