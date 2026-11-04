#pragma once

// The Unity engine objects the Runtime holds: Mesh, Material, Shader, renderers and Object.Destroy are
// UnityRendering.h's (US-T30), Texture / Texture2D UnityTexture.h's (US-T31); the scene's lights are here.

#include "UnityCompat/UnityMath.h"
#include "UnityCompat/UnityTexture.h"
#include "UnityCompat/UnityRendering.h"

namespace SpaceGod::UnityCompat
{
	enum class LightType : int { Spot = 0, Directional = 1, Point = 2, Area = 3 };

	/// A light in the scene (the active system's stars). Placed and coloured by UnitySpaceApp::UpdateLights;
	/// Native is its ULightComponent* once Rendering backs it.
	class Light final
	{
	public:
		LightType type = LightType::Point;
		Color color = Color::white();
		float intensity = 1;
		float range = 10;
		bool enabled = true;
		Vector3 direction = Vector3::forward();
		void* Native = nullptr;
		/// A light on a GameObject (a camera's key light, RenderBackend::AddKeyLight, US-T53): the transform it sits
		/// under, its rotation there, and Unity's Light.cullingMask (the layers it lights). Unset for the stars.
		std::weak_ptr<Transform> Parent;
		Quaternion localRotation = Quaternion::identity();
		int cullingMask = -1;
	};
}
