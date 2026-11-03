#include "ModelViewer.h"

#include "Rendering/Pipeline/RenderBackend.h"
#include "Shared/Profiles/Profiles.h"
#include "UI/UiInput.h"
#include "UnityCompat/GuiHost.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	int ModelViewer::ModelViewerLayer()
	{
		static const int layer = ModelViewerBackend::NameToLayer("ModelViewer");
		return layer;
	}

	cs::Ref<ModelViewer>& ModelViewer::Instance()
	{
		static cs::Ref<ModelViewer> current;
		return current;
	}

	void ModelViewer::Ensure()
	{
		if (Instance() != nullptr) return;
		auto go = NewGameObject("ModelViewer");
		// UnityEngine.Object.DontDestroyOnLoad(go): the instance is a static, so it outlives every scene anyway.
		auto viewer = cs::New<ModelViewer>();
		viewer->gameObject = go;
		Instance() = viewer;
		viewer->Awake();
	}

	void ModelViewer::Tick()
	{
		const cs::Ref<ModelViewer> viewer = Instance();
		if (!viewer) return;
		if (!viewer->started)
		{
			viewer->started = true;
			viewer->Start();
		}
		viewer->Update();
		viewer->LateUpdate();
	}

	void ModelViewer::DestroyInstance()
	{
		const cs::Ref<ModelViewer> viewer = Instance();
		if (!viewer) return;
		viewer->OnDestroy();
		Instance() = nullptr;
	}

	void ModelViewer::Awake()
	{
		cs::List<cs::String> names;
		for (const auto& g : ModelViewerBackend::LoadAllModels("Ships")) names.Add(g->name);
		modelNames = cs::OrderBy(names, [](const cs::String& n) { return n; });
	}

	void ModelViewer::Start()
	{
		// OnGUI from here on, in the GUI host's passes (over the game's own HUD).
		GuiHost::Register(this, [this] { OnGUI(); }, 100);

		const cs::List<cs::String>& args = Shared::Profiles::CommandLineArgs;
		for (int i = 0; i < args.Count() - 1; i++)
		{
			if (!cs::Strings::EqualsOrdinalIgnoreCase(args[i], CliFlag)) continue;
			const cs::String& requested = args[i + 1];
			int index = modelNames.IndexOf(requested);
			if (index >= 0) Open(index);
			else Debug::LogWarning(cs::Cat("[ModelViewer] -modelviewer '", requested, "' not found under Resources/Ships"));
			break;
		}
	}

	void ModelViewer::Update()
	{
		if (!UI::UiInput::present()) return;

		// F9, not V: V opens the Spawn & Pilot screen (Hud.DrawSpawn).
		if (UI::UiInput::WasPressedThisFrame(KeyCode::F9))
		{
			if (open) Close();
			else Open(selectedIndex >= 0 ? selectedIndex : 0);
		}

		if (!open) return;

		if (UI::UiInput::WasPressedThisFrame(KeyCode::Escape)) Close();

		// mouse != null: the keyboard and mouse come together (UiInput::present() above).
		{
			if (UI::UiInput::IsMousePressed(1))
			{
				auto delta = UI::UiInput::MouseDelta();
				orbitYaw += delta.x * 0.3f;
				orbitPitch = Mathf::Clamp(orbitPitch - delta.y * 0.3f, -85.0f, 85.0f);
			}
			auto scrollY = UI::UiInput::ScrollY();
			if (Mathf::Abs(scrollY) > 0.01f)
				zoom = Mathf::Clamp(zoom * Mathf::Pow(0.9f, scrollY * 0.05f), 1.2f, 20.0f);
		}

		PositionCamera();
	}

	void ModelViewer::LateUpdate()
	{
		if (!open || viewCamera == nullptr) return;
		for (Camera* cam : Camera::allCameras())
		{
			if (cam == viewCamera || !cam->enabled) continue;
			cam->enabled = false;
			if (!disabledCameras.Contains(cam)) disabledCameras.Add(cam);
		}
	}

	void ModelViewer::Open(int index)
	{
		if (modelNames.Count() == 0) return;
		index = Mathf::Clamp(index, 0, modelNames.Count() - 1);

		if (viewCamera == nullptr)
		{
			auto camGo = NewGameObject("ModelViewerCamera");
			camGo->transform->SetParent(gameObject->transform, false);
			viewCameraObject = camGo;
			viewCamera = ModelViewerBackend::AddCamera(camGo);
			// Whatever the render pipeline needs of a camera added at runtime (URP: its per-camera data).
			Rendering::RenderBackend::Active()->SetupCamera(viewCamera, Rendering::CameraRole::Overlay);
			viewCamera->clearFlags = CameraClearFlags::SolidColor;
			viewCamera->backgroundColor = Color(0.05f, 0.06f, 0.09f);
			// Highest depth in the scene wins the screen regardless of how the game's own camera is
			// tagged/found -- more robust than hunting for a "MainCamera"-tagged camera to disable.
			ModelViewerBackend::SetDepth(viewCamera, 1000);
			// The game's own content is drawn camera-relative (always near the origin, same as this
			// viewer's), so restrict this camera to its own layer rather than relying on disabling
			// other cameras to keep that content out of frame.
			ModelViewerBackend::SetCullingMask(viewCamera, 1 << ModelViewerLayer());
			Rendering::RenderBackend::Active()->AddKeyLight(camGo->transform, "ModelViewerLight", Quaternion::Euler(35.0f, -30.0f, 0.0f), 1.2f,
				1 << ModelViewerLayer());
		}

		disabledCameras.Clear();
		for (Camera* cam : Camera::allCameras())
		{
			if (cam == viewCamera || !cam->enabled) continue;
			cam->enabled = false;
			disabledCameras.Add(cam);
		}
		viewCameraObject->SetActive(true);
		viewCamera->enabled = true;

		SelectModel(index);
		open = isOpen = true;
	}

	void ModelViewer::Close()
	{
		open = isOpen = false;
		closedFrame = Time::frameCount;
		if (viewCamera != nullptr) viewCamera->enabled = false;
		for (Camera* cam : disabledCameras) if (cam != nullptr) cam->enabled = true;
		disabledCameras.Clear();
		if (instance != nullptr) Destroy(instance);
		instance = nullptr;
	}

	void ModelViewer::SelectModel(int index)
	{
		selectedIndex = index;
		if (instance != nullptr) Destroy(instance);

		auto prefab = Resources::LoadModel(cs::Cat("Ships/", modelNames[index]));
		instance = Instantiate(prefab, gameObject->transform);
		instance->transform->localPosition = Vector3::zero();
		SetLayerRecursively(instance, ModelViewerLayer());

		auto renderers = GetComponentsInChildren<Renderer>(instance, true);
		instanceBounds = renderers.Count() > 0 ? renderers[0]->bounds() : Bounds(Vector3::zero(), Vector3::one());
		for (const auto& r : renderers) instanceBounds.Encapsulate(r->bounds());

		orbitYaw = 30.0f;
		orbitPitch = 15.0f;
		// A multiple of the model's radius (PositionCamera scales it by that), not a distance.
		zoom = 3.0f;
		PositionCamera();
	}

	void ModelViewer::SetLayerRecursively(const cs::Ref<GameObject>& go, int layer)
	{
		ModelViewerBackend::SetLayer(go, layer);
		for (const auto& child : go->transform->children) SetLayerRecursively(child->gameObject(), layer);
	}

	void ModelViewer::PositionCamera()
	{
		if (viewCamera == nullptr) return;
		auto rotation = Quaternion::Euler(orbitPitch, orbitYaw, 0.0f);
		auto center = instanceBounds.center;
		auto radius = Mathf::Max(instanceBounds.extents.magnitude(), 0.5f);
		viewCamera->transform->SetPosition(center + rotation * Vector3::back() * (radius * zoom));
		// Transform.LookAt(center): forward to the target, world up.
		viewCamera->transform->SetRotation(Quaternion::LookRotation(center - viewCamera->transform->position(), Vector3::up()));
		viewCamera->nearClipPlane = Mathf::Max(radius * 0.01f, 0.001f);
		viewCamera->farClipPlane = Mathf::Max(radius * 100.0f, 100.0f);
	}

	void ModelViewer::OnGUI()
	{
		if (!open) return;
		EnsureStyles();

		constexpr float panelWidth = 220.0f;
		GUI::Box(Rect(12, 12, panelWidth, static_cast<float>(Screen::height - 24)), cs::String(), panelStyle);
		GUI::Label(Rect(24, 20, panelWidth - 24, 24), "MODEL VIEWER", titleStyle);
		GUI::Label(Rect(24, 46, panelWidth - 24, 40),
			"Right-drag: orbit\nScroll: zoom\nF9 / Esc: close", buttonStyle);

		scroll = GUI::BeginScrollView(Rect(12, 92, panelWidth, static_cast<float>(Screen::height - 92 - 48)), scroll,
			Rect(0, 0, panelWidth - 20, static_cast<float>(modelNames.Count() * 26)));
		for (int i = 0; i < modelNames.Count(); i++)
		{
			const auto& style = i == selectedIndex ? buttonActiveStyle : buttonStyle;
			if (GUI::Button(Rect(4, static_cast<float>(i * 26), panelWidth - 28, 24), modelNames[i], style))
				SelectModel(i);
		}
		GUI::EndScrollView();

		if (GUI::Button(Rect(24, static_cast<float>(Screen::height - 40), panelWidth - 24, 24), "Close  [Esc]", buttonStyle))
			Close();
	}

	void ModelViewer::OnDestroy()
	{
		GuiHost::Unregister(this);
	}

	void ModelViewer::EnsureStyles()
	{
		if (stylesReady) return;
		stylesReady = true;
		auto panelTex = cs::New<Texture2D>(1, 1);
		panelTex->SetPixel(0, 0, Color(0.06f, 0.07f, 0.1f, 0.92f));
		panelTex->Apply();
		panelStyle = cs::New<GUIStyle>(*GUI::skin().box);
		panelStyle->normal.background = panelTex;
		titleStyle = cs::New<GUIStyle>(*GUI::skin().label);
		titleStyle->fontStyle = FontStyle::Bold;
		titleStyle->normal.textColor = Color::white();
		buttonStyle = cs::New<GUIStyle>(*GUI::skin().button);
		buttonStyle->alignment = TextAnchor::MiddleLeft;
		buttonActiveStyle = cs::New<GUIStyle>(*buttonStyle);
		auto activeTex = cs::New<Texture2D>(1, 1);
		activeTex->SetPixel(0, 0, Color(0.2f, 0.45f, 0.65f, 1.0f));
		activeTex->Apply();
		buttonActiveStyle->normal.background = activeTex;
		buttonActiveStyle->normal.textColor = Color::white();
	}
}
