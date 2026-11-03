// ModelViewerBackend (declared in ModelViewer.h, US-T31): what the model viewer needs of Unity beyond the facade.
// As the C# track's facade does (US-T28/US-T27: LayerMask, Camera.cullingMask / depth kept as data), layers, culling
// masks and camera depth are recorded, not rendered: the viewer's camera does not yet draw on its own over the game's
// (that isolation belongs to the Rendering side). Resources.LoadAll("Ships") lists the ship models the shared
// plugin's import made (Data/ShipModels.json), each loaded through Resources::LoadModel, as the C# LoadAll does.

#include "ModelViewer.h"

#include "UnityCompat/UnityRendering.h"
#include "UnityCompat/UnityRuntime.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace SpaceGod::ModelViewerBackend
{
	namespace
	{
		/// Unity's built-in layers, then each new name as it is first asked for (the C# track's LayerMask).
		cs::List<cs::String>& LayerNames()
		{
			static cs::List<cs::String> names{ "Default", "TransparentFX", "Ignore Raycast", "", "Water", "UI" };
			return names;
		}

		std::unordered_map<const GameObject*, int>& Layers()
		{
			static std::unordered_map<const GameObject*, int> layers;
			return layers;
		}

		struct CameraData
		{
			std::weak_ptr<GameObject> owner;
			cs::Ref<Camera> camera;
			float depth = 0;
			int cullingMask = -1;
		};

		std::vector<CameraData>& Cameras()
		{
			static std::vector<CameraData> cameras;
			return cameras;
		}
	}

	cs::List<cs::Ref<GameObject>> LoadAllModels(const cs::String& path)
	{
		cs::List<cs::Ref<GameObject>> found;
		cs::String folder = path;
		while (!folder.empty() && folder.back() == '/') folder.pop_back();
		if (folder != "Ships") return found; // nothing ported asks for any other folder
		cs::String file = UnityCompat::Resources::FileFor("Data/ShipModels");
		FString text;
		if (file.empty() || !FFileHelper::LoadFileToString(text, UTF8_TO_TCHAR(file.c_str()))) return found;
		TSharedPtr<FJsonObject> json;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(text), json) || !json) return found;
		for (const auto& kv : json->Values)
		{
			cs::String name(TCHAR_TO_UTF8(*kv.Key));
			if (auto model = UnityCompat::Resources::LoadModel(folder + "/" + name))
			{
				if (model->name.empty()) model->name = name;
				found.Add(model);
			}
		}
		return found;
	}

	int NameToLayer(const cs::String& layerName)
	{
		auto& names = LayerNames();
		for (int i = 0; i < names.Count(); i++)
			if (names[i] == layerName) return i;
		names.Add(layerName);
		return names.Count() - 1;
	}

	void SetLayer(const cs::Ref<GameObject>& go, int layer)
	{
		if (go) Layers()[go.get()] = layer;
	}

	Camera* AddCamera(const cs::Ref<GameObject>& go)
	{
		auto& cameras = Cameras();
		// Cameras whose GameObject is gone go with it.
		cameras.erase(std::remove_if(cameras.begin(), cameras.end(), [](const CameraData& c) { return c.owner.expired(); }), cameras.end());
		CameraData data;
		data.owner = go;
		data.camera = cs::New<Camera>();
		if (go) data.camera->transform = go->transform;
		cameras.push_back(data);
		return cameras.back().camera.get();
	}

	void SetDepth(Camera* camera, float depth)
	{
		for (auto& c : Cameras())
			if (c.camera.get() == camera) c.depth = depth;
	}

	void SetCullingMask(Camera* camera, int mask)
	{
		for (auto& c : Cameras())
			if (c.camera.get() == camera) c.cullingMask = mask;
	}
}
