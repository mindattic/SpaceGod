// ModelViewerBackend (declared in ModelViewer.h, US-T31): what the model viewer needs of Unity beyond the facade.
// LayerMask.NameToLayer names layers as the C# track's LayerMask does; `go.layer`, `go.AddComponent<Camera>()`,
// `camera.depth` and `camera.cullingMask` are the facade's GameObject::layer, Camera::Add, Camera::depth and
// Camera::cullingMask, which the scene host renders (US-T53, the C# track's UnityBridge.ScreenCamera since US-T40):
// the screen shows the enabled camera of the highest depth (the viewer's, 1000), only the renderers on its mask's
// layers, over its SolidColor backdrop, lit by its own key light (UnityScene::SyncNow). Resources.LoadAll("Ships")
// lists the ship models the shared plugin's import made (Data/ShipModels.json), each loaded through
// Resources::LoadModel, as the C# LoadAll does.

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
		if (go) go->layer = layer & 31;
	}

	Camera* AddCamera(const cs::Ref<GameObject>& go) { return Camera::Add(go); }

	void SetDepth(Camera* camera, float depth)
	{
		if (camera) camera->depth = depth;
	}

	void SetCullingMask(Camera* camera, int mask)
	{
		if (camera) camera->cullingMask = mask;
	}
}
