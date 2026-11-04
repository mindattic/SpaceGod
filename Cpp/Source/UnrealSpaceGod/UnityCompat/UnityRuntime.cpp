#include "UnityCompat/UnityRuntime.h"
#include "UnrealSpaceGodLog.h"

#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/Engine.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace SpaceGod::UnityCompat
{
	static FString ToF(const cs::String& s) { return FString(UTF8_TO_TCHAR(s.c_str())); }

	void Debug::Log(const cs::String& message) { UE_LOG(LogSpaceGod, Display, TEXT("%s"), *ToF(message)); }
	void Debug::LogWarning(const cs::String& message) { UE_LOG(LogSpaceGod, Warning, TEXT("%s"), *ToF(message)); }
	void Debug::LogError(const cs::String& message) { UE_LOG(LogSpaceGod, Error, TEXT("%s"), *ToF(message)); }

	cs::String Application::persistentDataPath()
	{
		FString dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Persistent"));
		return cs::String(TCHAR_TO_UTF8(*dir));
	}

	static int targetFps = -1;

	void Application::SetTargetFrameRate(int fps)
	{
		targetFps = fps;
		if (IConsoleVariable* var = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
			var->Set(fps <= 0 ? 0.0f : static_cast<float>(fps), ECVF_SetByGameSetting);
	}

	int Application::targetFrameRate() { return targetFps; }

	void Application::Quit()
	{
		if (GEngine) GEngine->DeferredCommands.Add(TEXT("quit"));
	}

	bool Application::isEditor() { return GIsEditor; }

	// ---------------------------------------------------------------- scene graph

	void Transform::SetParent(const cs::Ref<Transform>& newParent, bool worldPositionStays)
	{
		Vector3 worldPos = position();
		Quaternion worldRot = rotation();
		if (auto old = parent.lock())
		{
			auto& siblings = old->children;
			for (int i = 0; i < siblings.Count(); i++)
				if (siblings[i].get() == this) { siblings.RemoveAt(i); break; }
		}
		parent = newParent;
		if (newParent) newParent->children.Add(shared_from_this());
		if (worldPositionStays)
		{
			SetPosition(worldPos);
			SetRotation(worldRot);
		}
	}

	Vector3 Transform::position() const
	{
		auto p = parent.lock();
		if (!p) return localPosition;
		return p->position() + p->rotation() * Vector3::Scale(p->localScale, localPosition);
	}

	Quaternion Transform::rotation() const
	{
		auto p = parent.lock();
		return p ? p->rotation() * localRotation : localRotation;
	}

	void Transform::SetPosition(Vector3 world)
	{
		auto p = parent.lock();
		if (!p) { localPosition = world; return; }
		Vector3 local = Quaternion::Inverse(p->rotation()) * (world - p->position());
		Vector3 s = p->localScale;
		localPosition = Vector3(s.x != 0 ? local.x / s.x : 0, s.y != 0 ? local.y / s.y : 0, s.z != 0 ? local.z / s.z : 0);
	}

	void Transform::SetRotation(Quaternion world)
	{
		auto p = parent.lock();
		localRotation = p ? Quaternion::Inverse(p->rotation()) * world : world;
	}

	void Transform::SetPositionAndRotation(Vector3 p, Quaternion q)
	{
		SetPosition(p);
		SetRotation(q);
	}

	GameObject::GameObject(const cs::String& name_) : name(name_) {}

	cs::Ref<GameObject> GameObject::Create(const cs::String& name)
	{
		auto go = cs::New<GameObject>(name);
		go->transform = cs::New<Transform>();
		go->transform->Owner = go;
		return go;
	}

	bool GameObject::activeInHierarchy() const
	{
		// A loaded asset (isAsset, US-T53) is never in the scene itself, only its Instantiate copies are (the C# facade's).
		if (!activeSelf || destroyed || isAsset) return false;
		for (auto p = transform ? transform->parent.lock() : nullptr; p; p = p->parent.lock())
			if (auto go = p->gameObject(); go && (!go->activeSelf || go->destroyed || go->isAsset)) return false;
		return true;
	}

	void GameObject::Destroy(const cs::Ref<GameObject>& go)
	{
		if (!go || go->destroyed) return;
		go->destroyed = true;
		if (go->transform)
		{
			if (auto p = go->transform->parent.lock())
			{
				auto& siblings = p->children;
				for (int i = 0; i < siblings.Count(); i++)
					if (siblings[i] == go->transform) { siblings.RemoveAt(i); break; }
			}
			go->transform->parent.reset();
			auto kids = go->transform->children;
			for (auto& c : kids)
				if (auto child = c->gameObject()) Destroy(child);
		}
	}

	// ---------------------------------------------------------------- camera

	static Camera& MainCamera()
	{
		static Camera camera;
		return camera;
	}

	Camera* Camera::main() { return &MainCamera(); }

	namespace
	{
		/// The cameras added to GameObjects (US-T53), each kept while its GameObject lives.
		std::vector<std::pair<std::weak_ptr<GameObject>, cs::Ref<Camera>>>& AddedCameras()
		{
			static std::vector<std::pair<std::weak_ptr<GameObject>, cs::Ref<Camera>>> cameras;
			return cameras;
		}

		void PurgeCameras()
		{
			auto& cameras = AddedCameras();
			cameras.erase(std::remove_if(cameras.begin(), cameras.end(), [](const auto& c)
			{
				auto go = c.first.lock();
				return !go || go->destroyed;
			}), cameras.end());
		}
	}

	cs::List<Camera*> Camera::allCameras()
	{
		PurgeCameras();
		cs::List<Camera*> all;
		all.Add(&MainCamera());
		for (const auto& c : AddedCameras()) all.Add(c.second.get());
		return all;
	}

	Camera* Camera::Add(const cs::Ref<GameObject>& go)
	{
		PurgeCameras();
		auto camera = cs::New<Camera>();
		if (go)
		{
			camera->transform = go->transform;
			camera->Owner = go;
		}
		AddedCameras().emplace_back(go, camera);
		return camera.get();
	}

	bool Camera::isActiveAndEnabled() const
	{
		if (!enabled) return false;
		if (this == &MainCamera()) return true;
		auto go = Owner.lock();
		return go && !go->destroyed && go->activeInHierarchy();
	}

	Vector3 Camera::ScreenPointToRayDirection(Vector2 screen) const
	{
		// Unity's perspective ray: through the near plane at the pixel, origin bottom-left.
		float h = static_cast<float>(Screen::height), w = static_cast<float>(Screen::width);
		float ny = h > 0 ? (screen.y / h) * 2.0f - 1.0f : 0.0f;
		float nx = w > 0 ? (screen.x / w) * 2.0f - 1.0f : 0.0f;
		float tanHalf = Mathf::Tan(fieldOfView * 0.5f * Mathf::Deg2Rad);
		Vector3 local(nx * tanHalf * aspect(), ny * tanHalf, 1.0f);
		return (transform->rotation() * local).normalized();
	}

	Vector3 Camera::WorldToScreenPoint(Vector3 world) const
	{
		Vector3 local = Quaternion::Inverse(transform->rotation()) * (world - transform->position());
		float tanHalf = Mathf::Tan(fieldOfView * 0.5f * Mathf::Deg2Rad);
		if (Mathf::Abs(local.z) < 1E-12f) return Vector3(0, 0, local.z);
		float nx = local.x / (local.z * tanHalf * aspect());
		float ny = local.y / (local.z * tanHalf);
		return Vector3((nx + 1.0f) * 0.5f * Screen::width, (ny + 1.0f) * 0.5f * Screen::height, local.z);
	}
}
