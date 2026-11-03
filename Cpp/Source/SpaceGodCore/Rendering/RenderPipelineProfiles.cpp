// Port of Unity.SpaceGod Assets/UnitySpace/Core/Rendering/RenderPipelineProfiles.cs (see docs/MIGRATION.md).

#include "Rendering/RenderPipelineProfiles.h"

#include "Rendering/Photoreal.h"

#include <filesystem>

namespace SpaceGod::Core
{
	cs::String ToString(PipelineFeatures v)
	{
		switch (v)
		{
			case PipelineFeatures::None: return "None";
			case PipelineFeatures::PhysicalSky: return "PhysicalSky";
			case PipelineFeatures::Volumetrics: return "Volumetrics";
			case PipelineFeatures::Exposure: return "Exposure";
			case PipelineFeatures::RayTracing: return "RayTracing";
			case PipelineFeatures::All: return "All";
		}
		// A combination: .NET lists the set flags' names, lowest value first; leftover bits print as the number.
		int bits = static_cast<int>(v);
		cs::List<cs::String> names;
		const std::pair<int, const char*> flags[] = { { 1, "PhysicalSky" }, { 2, "Volumetrics" }, { 4, "Exposure" }, { 8, "RayTracing" } };
		int left = bits;
		for (const auto& f : flags)
			if ((bits & f.first) == f.first) { names.Add(f.second); left &= ~f.first; }
		if (left != 0) return cs::Str(bits);
		return cs::Strings::Join(", ", names);
	}

	namespace
	{
		// string.TrimEnd(char) / string.Trim(params char[]).
		cs::String TrimEndChars(const cs::String& s, std::string_view chars)
		{
			size_t end = s.size();
			while (end > 0 && chars.find(s[end - 1]) != std::string_view::npos) end--;
			return s.substr(0, end);
		}

		cs::String TrimChars(const cs::String& s, std::string_view chars)
		{
			size_t start = 0, end = s.size();
			while (start < end && chars.find(s[start]) != std::string_view::npos) start++;
			while (end > start && chars.find(s[end - 1]) != std::string_view::npos) end--;
			return s.substr(start, end - start);
		}

		// System.IO.Path, near enough for the one use here (a player's exe path): GetDirectoryName (null at a root,
		// the empty string here), GetFileName and Combine. .NET normalises '/' to '\' on Windows; std::filesystem keeps them.
		cs::String GetDirectoryName(const cs::String& p)
		{
			std::filesystem::path path(p);
			if (!path.has_relative_path() || !path.has_filename()) return cs::String();
			return path.parent_path().string();
		}

		cs::String GetFileName(const cs::String& p) { return std::filesystem::path(p).filename().string(); }
	}

	RenderPipelineProfile::RenderPipelineProfile(const cs::String& id, const cs::String& displayName, const cs::String& assetTypeName, const cs::String& shaderTag, const cs::String& shaderFolder,
		bool honoursCameraHdr, bool honoursCameraMsaa, const cs::List<cs::String>& renderFeatures,
		const cs::String& menuName, double referenceEv100, PipelineFeatures features,
		bool requiresComputeShaders, const cs::String& playerFolder)
	{
		if (cs::Strings::IsNullOrEmpty(id)) throw cs::ArgumentException("A render pipeline needs an id. (Parameter 'id')");
		if (cs::Strings::IsNullOrEmpty(shaderFolder)) throw cs::ArgumentException("A render pipeline needs a shader folder. (Parameter 'shaderFolder')");
		Id = id;
		DisplayName = cs::Strings::IsNullOrEmpty(displayName) ? id : displayName;
		AssetTypeName = assetTypeName;
		ShaderTag = shaderTag;
		ShaderFolder = TrimEndChars(shaderFolder, "/");
		HonoursCameraHdr = honoursCameraHdr;
		HonoursCameraMsaa = honoursCameraMsaa;
		RenderFeatures = renderFeatures;
		MenuName = cs::Strings::IsNullOrEmpty(menuName) ? DisplayName : menuName;
		ReferenceEv100 = referenceEv100;
		Features = features;
		RequiresComputeShaders = requiresComputeShaders;
		PlayerFolder = TrimChars(playerFolder, "/\\");
	}

	double RenderPipelineProfile::ReferenceLuminance() const { return HasExposure() ? ExposureMath::LuminanceAtEv100(ReferenceEv100) : 1.0; }

	const cs::List<cs::String>& RenderShaders::All()
	{
		static const cs::List<cs::String> value{ Planet, Sun, Glow, Ring, Ship, Line, Stars, GasGiant, Asteroids, Fx, Points, Sky };
		return value;
	}

	const cs::Ref<RenderPipelineProfile>& RenderPipelineProfiles::Universal()
	{
		static const cs::Ref<RenderPipelineProfile> value = cs::New<RenderPipelineProfile>("urp", "Universal Render Pipeline",
			"UniversalRenderPipelineAsset", "UniversalPipeline", "Shaders", true, true, cs::List<cs::String>(), "URP (default, lighter)");
		return value;
	}

	const cs::Ref<RenderPipelineProfile>& RenderPipelineProfiles::HighDefinition()
	{
		static const cs::Ref<RenderPipelineProfile> value = cs::New<RenderPipelineProfile>("hdrp", "High Definition Render Pipeline",
			"HDRenderPipelineAsset", "HDRenderPipeline", "Shaders", false, false, cs::List<cs::String>(),
			"HDRP (photoreal)", 15.0, PipelineFeatures::All, true,
			"HDRP");
		return value;
	}

	cs::List<cs::Ref<RenderPipelineProfile>>& RenderPipelineProfiles::registered()
	{
		static cs::List<cs::Ref<RenderPipelineProfile>> value{ Universal(), HighDefinition() };
		return value;
	}

	void RenderPipelineProfiles::Register(const cs::Ref<RenderPipelineProfile>& profile)
	{
		if (profile == nullptr) throw cs::ArgumentNullException("Value cannot be null. (Parameter 'profile')");
		int i = registered().FindIndex([&](const cs::Ref<RenderPipelineProfile>& p) { return p->Id == profile->Id; });
		if (i >= 0) registered()[i] = profile;
		else registered().Add(profile);
	}

	cs::Ref<RenderPipelineProfile> RenderPipelineProfiles::Find(const cs::String& id)
	{
		return registered().Find([&](const cs::Ref<RenderPipelineProfile>& p) { return p->Id == id; });
	}

	cs::Ref<RenderPipelineProfile> RenderPipelineProfiles::ForMenuName(const cs::String& menuName)
	{
		if (cs::Strings::IsNullOrEmpty(menuName)) return nullptr;
		auto byName = registered().Find([&](const cs::Ref<RenderPipelineProfile>& p) { return p->MenuName == menuName; });
		if (byName) return byName;
		return registered().Find([&](const cs::Ref<RenderPipelineProfile>& p) { return cs::Strings::EqualsOrdinalIgnoreCase(p->Id, menuName); });
	}

	cs::String RenderPipelineProfiles::PlayerExecutable(const cs::String& runningExe, const cs::Ref<RenderPipelineProfile>& running, const cs::Ref<RenderPipelineProfile>& wanted)
	{
		if (cs::Strings::IsNullOrEmpty(runningExe) || running == nullptr || wanted == nullptr || running->Id == wanted->Id) return cs::String();
		cs::String dir = GetDirectoryName(runningExe);
		cs::String file = GetFileName(runningExe);
		// Up out of the running pipeline's own folder (one level per segment) to the default player's.
		if (running->PlayerFolder.size() > 0)
			for (const auto& segment : cs::Strings::Split(running->PlayerFolder, std::string_view("/\\")))
				if (!segment.empty()) // StringSplitOptions.RemoveEmptyEntries
					dir = GetDirectoryName(dir);
		return wanted->PlayerFolder.size() > 0
			? (std::filesystem::path(dir) / wanted->PlayerFolder / file).string()
			: (std::filesystem::path(dir) / file).string();
	}

	int RenderPipelineProfiles::QualityLevelFor(const cs::Ref<RenderPipelineProfile>& wanted, const cs::List<cs::String>* levelAssetTypes, const cs::String& defaultAssetType, int current)
	{
		if (wanted == nullptr || levelAssetTypes == nullptr) return -1;
		auto Runs = [&](int i)
		{
			cs::String type = (*levelAssetTypes)[i];
			if (cs::Strings::IsNullOrEmpty(type)) type = defaultAssetType;
			return type == wanted->AssetTypeName;
		};
		if (current >= 0 && current < levelAssetTypes->Count() && Runs(current)) return current;
		for (int i = levelAssetTypes->Count() - 1; i >= 0; i--)
			if (Runs(i)) return i;
		return -1;
	}

	cs::Ref<RenderPipelineProfile> RenderPipelineProfiles::ForAsset(const cs::String& assetTypeName)
	{
		if (cs::Strings::IsNullOrEmpty(assetTypeName)) return nullptr;
		return registered().Find([&](const cs::Ref<RenderPipelineProfile>& p) { return p->AssetTypeName == assetTypeName; });
	}
}
