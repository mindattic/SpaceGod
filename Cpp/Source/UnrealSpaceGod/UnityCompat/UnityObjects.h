#pragma once

// Seams for the Unity engine objects the Runtime holds but that belong to the Views/Rendering tickets (materials,
// meshes, textures, lights). They are handles only here: Native points at the Unreal object (UMaterialInstanceDynamic*,
// UStaticMesh*/UProceduralMeshComponent*, UTexture2D*, ULightComponent*) once Rendering backs them.

#include "UnityCompat/UnityMath.h"

namespace SpaceGod::UnityCompat
{
	class Material final
	{
	public:
		cs::String name;
		void* Native = nullptr;
		Color color = Color::white();
		void SetColor(const cs::String& property, Color value);
		void SetFloat(const cs::String& property, float value);
		void SetVector(const cs::String& property, Vector4 value);
	};

	class Mesh final
	{
	public:
		cs::String name;
		void* Native = nullptr;
	};

	class Texture2D final
	{
	public:
		cs::String name;
		int width = 0, height = 0;
		void* Native = nullptr;
	};

	enum class LightType : int { Spot = 0, Directional = 1, Point = 2, Area = 3 };

	/// A light in the scene (the active system's stars). Placed and coloured by UnitySpaceApp::UpdateLights.
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
	};

	inline void Material::SetColor(const cs::String&, Color value) { color = value; } // Rendering backs the rest
	inline void Material::SetFloat(const cs::String&, float) {}
	inline void Material::SetVector(const cs::String&, Vector4) {}
}
