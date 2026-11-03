#pragma once

// The Unity engine statics the Runtime reads (Time, Screen, Cursor, Debug, Application, AudioListener) and the
// scene-graph handles it builds views on (GameObject, Transform, Camera), as small C++ stand-ins. The host actor
// (AUnitySpaceAppActor) fills Time/Screen each frame and applies Cursor/Camera/Application back to Unreal; the
// Views/Rendering tickets back GameObject/Transform with real scene components through Transform::Native.
// Names follow docs/MIGRATION.md (properties -> fields where Unity's are plain values, methods where bodied).

#include "UnityCompat/UnityMath.h"

namespace SpaceGod::UnityCompat
{
	/// UnityEngine.Time: written by the host actor at the start of each tick.
	struct Time
	{
		static inline float unscaledDeltaTime = 0;
		static inline float unscaledTime = 0;
		static inline float deltaTime = 0;
		static inline float time = 0;
		static inline double realtimeSinceStartupAsDouble = 0;
		static inline int frameCount = 0;
	};

	/// UnityEngine.Screen: the viewport size in pixels, written by the host actor.
	struct Screen
	{
		static inline int width = 1920;
		static inline int height = 1080;
		static inline float dpi = 96;
	};

	enum class CursorLockMode : int { None = 0, Locked = 1, Confined = 2 };

	/// UnityEngine.Cursor: read back by the player controller each frame (shows/hides and locks the mouse).
	struct Cursor
	{
		static inline bool visible = true;
		static inline CursorLockMode lockState = CursorLockMode::None;
	};

	/// UnityEngine.Debug -> UE_LOG(LogSpaceGod, ...).
	struct Debug
	{
		static void Log(const cs::String& message);
		static void LogWarning(const cs::String& message);
		static void LogError(const cs::String& message);
	};

	/// UnityEngine.Application.
	struct Application
	{
		/// The player's data folder (Unity's persistentDataPath): Unreal's per-user Saved folder for this game.
		static cs::String persistentDataPath();
		/// -1 = uncapped; applied through t.MaxFPS.
		static void SetTargetFrameRate(int fps);
		static int targetFrameRate();
		static void Quit();
		static bool isEditor();
	};

	/// UnityEngine.AudioListener.volume (0..1), applied to the master sound mix by the host.
	struct AudioListener
	{
		static inline float volume = 1.0f;
	};

	class GameObject;

	/// A node of the scene graph (Unity's Transform). Positions are Unity axes and scene units, camera-relative
	/// (the floating origin): UnrealSpace converts them at the moment they reach a scene component.
	class Transform final : public std::enable_shared_from_this<Transform>
	{
	public:
		std::weak_ptr<GameObject> Owner;
		std::weak_ptr<Transform> parent;
		cs::List<cs::Ref<Transform>> children;
		Vector3 localPosition;
		Quaternion localRotation = Quaternion::identity();
		Vector3 localScale = Vector3::one();
		/// The Unreal side of this node (a USceneComponent*), set by the Views/Rendering tickets; null until then.
		void* Native = nullptr;

		cs::Ref<GameObject> gameObject() const { return Owner.lock(); }
		void SetParent(const cs::Ref<Transform>& newParent, bool worldPositionStays = true);
		void SetPositionAndRotation(Vector3 position, Quaternion rotation);
		Vector3 position() const;
		Quaternion rotation() const;
		void SetPosition(Vector3 p);
		void SetRotation(Quaternion q);
		Vector3 forward() const { return rotation() * Vector3::forward(); }
		Vector3 up() const { return rotation() * Vector3::up(); }
		Vector3 right() const { return rotation() * Vector3::right(); }
	};

	/// Unity's GameObject: a named, activatable node holding a Transform. Destroy() detaches it and marks it dead.
	class GameObject final : public std::enable_shared_from_this<GameObject>
	{
	public:
		cs::String name;
		cs::String tag;
		bool activeSelf = true;
		bool destroyed = false;
		cs::Ref<Transform> transform;

		explicit GameObject(const cs::String& name_ = "GameObject");
		/// new GameObject(name): made with its Transform.
		static cs::Ref<GameObject> Create(const cs::String& name);
		void SetActive(bool value) { activeSelf = value; }
		bool activeInHierarchy() const;
		/// Object.Destroy(gameObject): it and its children leave the scene.
		static void Destroy(const cs::Ref<GameObject>& go);
	};

	enum class CameraClearFlags : int { Skybox = 1, SolidColor = 2, Depth = 3, Nothing = 4 };

	/// The game's one camera (Unity's Camera.main): pose (always at the origin, see the floating origin), vertical
	/// field of view in degrees and clip planes in scene units. The host actor applies it to the player's camera
	/// manager each frame through UnrealSpace (vertical -> horizontal fov, metres -> cm).
	class Camera final
	{
	public:
		cs::Ref<Transform> transform = cs::New<Transform>();
		CameraClearFlags clearFlags = CameraClearFlags::Skybox;
		Color backgroundColor = Color::black();
		float fieldOfView = 60.0f;
		float nearClipPlane = 0.3f;
		float farClipPlane = 1000.0f;
		bool enabled = true;

		static Camera* main();
		static cs::List<Camera*> allCameras();
		float aspect() const { return Screen::height > 0 ? static_cast<float>(Screen::width) / Screen::height : 1.0f; }
		/// A ray direction (Unity axes) through a screen point (pixels, origin bottom-left as Unity's).
		Vector3 ScreenPointToRayDirection(Vector2 screen) const;
		/// Screen point (pixels, origin bottom-left) and depth of a camera-relative scene point; z below 0 behind.
		Vector3 WorldToScreenPoint(Vector3 world) const;
	};
}
