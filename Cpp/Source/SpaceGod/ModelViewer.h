#pragma once

// ModelViewer: the C++ port of Unity.SpaceGod's Runtime/ModelViewer.cs (US-T31).
// The MonoBehaviour is a plain class: Ensure() stands in for [RuntimeInitializeOnLoadMethod(AfterSceneLoad)] (it makes
// the one instance and runs its Awake), and the host calls the static Tick() once a frame, which runs Unity's messages
// in Unity's order (Start once, then Update and LateUpdate). OnGUI runs in the GUI host's passes (registered at Start).

#include "Cs/Cs.h"
#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityRenderingTypes.h"
#include "UnityCompat/UnityGui.h"

namespace SpaceGod
{
	/// What ModelViewer needs of Unity that the Unity facade (UnityCompat) does not have yet: a second, layer-isolated
	/// camera drawn over the game's own, and Resources.LoadAll. Declared here, implemented by the Rendering/Views side.
	namespace ModelViewerBackend
	{
		/// `Resources.LoadAll<GameObject>(path)`: every model under Resources/<path> (the ship models for "Ships"), each
		/// a prototype whose `name` is the model's name.
		cs::List<cs::Ref<GameObject>> LoadAllModels(const cs::String& path);

		/// `LayerMask.NameToLayer(layerName)`: the layer's index (-1 when there is no such layer).
		int NameToLayer(const cs::String& layerName);

		/// `go.layer = layer` (the GameObject only, not its children).
		void SetLayer(const cs::Ref<GameObject>& go, int layer);

		/// `go.AddComponent<Camera>()`: a further camera drawing to the screen, whose `transform` is `go`'s. It lives as
		/// long as `go`.
		Camera* AddCamera(const cs::Ref<GameObject>& go);

		/// `camera.depth = depth`: the highest depth draws last (over the others).
		void SetDepth(Camera* camera, float depth);

		/// `camera.cullingMask = mask`: the camera draws only renderers (and lights) on these layers.
		void SetCullingMask(Camera* camera, int mask);
	}

	/// A standalone screen for inspecting any loadable model (currently: everything under
	/// Resources/Ships/) in isolation, for visual QA — this is the tool that would have caught the
	/// Kenney-craft plating bug directly instead of by chance during a tour capture. Deliberately kept
	/// independent of UI.Hud/UnitySpaceApp (both have large unrelated
	/// in-flight changes elsewhere right now) rather than wired into the real menu — toggle with the
	/// <c>F9</c> key, or open straight to a model with <c>-modelviewer &lt;name&gt;</c> on the command
	/// line (e.g. scripted into a capture tour stop later). Not a content browser: no search/filter, no
	/// editing, no side-by-side comparison — just "put this one model in front of the camera and let me
	/// look at it."
	class ModelViewer final
	{
	public:
		ModelViewer() = default;
		ModelViewer(const ModelViewer&) = delete;
		ModelViewer& operator=(const ModelViewer&) = delete;

		/// [RuntimeInitializeOnLoadMethod(AfterSceneLoad)]: makes the one viewer (kept across scenes, as
		/// DontDestroyOnLoad) unless there is one already, and runs its Awake. The host calls it after each scene load.
		static void Ensure();

		/// C++ only: the per-frame driver Unity's player loop is. The host calls it once a frame (after the app's
		/// Update, as script order goes): the instance's Start on its first frame, then Update and LateUpdate.
		static void Tick();

		/// C++ only: Object.Destroy of the viewer (module shutdown / end of play): runs its OnDestroy and forgets it.
		static void DestroyInstance();

		/// The viewer has the keyboard and mouse this frame, so the game underneath must ignore them:
		/// while it is open, and on the frame it closes (the Esc that closes it is not also the game's Esc,
		/// whichever of the two components updates first).
		static bool CapturesInput() { return isOpen || closedFrame == Time::frameCount; }

		/// C++ only, for the checks (the C# track's FidelityShots reaches Open/Close by reflection; US-T53): opens the
		/// viewer on model `index` as F9 does (false without a viewer or models), closes it, its camera (null until
		/// first opened), its layer, the name of model `index`.
		static bool OpenForCheck(int index);
		static void CloseForCheck();
		static Camera* CheckCamera();
		static int CheckLayer() { return ModelViewerLayer(); }
		static cs::String CheckModelName();
		/// Each renderer of the model shown: its GameObject's name and layer, active, enabled, mesh, material.
		static cs::String CheckDescribe();

		// ---- Unity messages (private in C#; public here for the host).
		void Awake();
		void Start();
		void Update();

		/// Disabling the other cameras alone doesn't isolate the view: UnitySpaceApp renders
		/// camera-relative (its camera is pinned at Vector3.zero and every body/ship is placed
		/// at its world position minus the camera's, via <c>UnitySpaceApp.Relative</c>), so the live scene's
		/// renderers always sit clustered near the origin -- exactly where this viewer's own model and camera
		/// also sit. viewCamera's culling mask restricts it to ModelViewerLayer
		/// (set on the instantiated model and its light in SelectModel/Open) so it
		/// only ever sees its own content, regardless of what else is positioned nearby.
		void LateUpdate();

		void OnGUI();

		/// C++ only (the C# has no OnDestroy): takes the OnGUI handler off the GUI host.
		void OnDestroy();

	private:
		static constexpr const char* CliFlag = "-modelviewer";

		/// Layer exclusive to this viewer's camera, model and light, so camera-relative scene
		/// content (always drawn near the origin, same as this viewer's own content) never shows through.
		static int ModelViewerLayer();

		static cs::Ref<ModelViewer>& Instance();

		/// The GameObject "ModelViewer" this component sits on (its transform parents the camera and the model).
		cs::Ref<GameObject> gameObject;
		bool started = false;

		cs::List<cs::String> modelNames;
		int selectedIndex = -1;
		cs::Ref<GameObject> instance;
		Camera* viewCamera = nullptr;
		/// viewCamera's GameObject (C#'s viewCamera.gameObject).
		cs::Ref<GameObject> viewCameraObject;
		cs::List<Camera*> disabledCameras;
		bool open = false;
		static inline bool isOpen = false;
		static inline int closedFrame = -1;

		float orbitYaw = 30.0f, orbitPitch = 15.0f, zoom = 3.0f;
		Bounds instanceBounds;
		Vector2 scroll;

		cs::Ref<GUIStyle> panelStyle, titleStyle, buttonStyle, buttonActiveStyle;
		bool stylesReady = false;

		void Open(int index);
		void Close();
		void SelectModel(int index);
		static void SetLayerRecursively(const cs::Ref<GameObject>& go, int layer);
		void PositionCamera();
		void EnsureStyles();
	};
}
