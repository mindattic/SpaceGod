#pragma once

// UnityRendering.h's stand-ins brought into namespace SpaceGod, as UnityTypes.h does for the rest (`using UnityEngine;`
// and `using UnityEngine.Rendering;` for the Views/Rendering port, US-T30).

#include "UnityCompat/UnityTypes.h"
#include "UnityCompat/UnityRendering.h"
#include "UnityCompat/UnityParticles.h"

namespace SpaceGod
{
	using UnityCompat::Alive;
	using UnityCompat::AddComponent;
	using UnityCompat::GetComponent;
	using UnityCompat::GetComponentsInChildren;
	using UnityCompat::NewGameObject;
	using UnityCompat::Instantiate;
	using UnityCompat::Bounds;
	using UnityCompat::Color32;
	using UnityCompat::Component;
	using UnityCompat::HideFlags;
	using UnityCompat::IndexFormat;
	using UnityCompat::Material;
	using UnityCompat::MaterialPropertyBlock;
	using UnityCompat::Mesh;
	using UnityCompat::MeshFilter;
	using UnityCompat::MeshRenderer;
	using UnityCompat::MeshTopology;
	using UnityCompat::Renderer;
	using UnityCompat::ResourceRequest;
	using UnityCompat::ResourceType;
	using UnityCompat::Resources;
	using UnityCompat::ShadowCastingMode;
	using UnityCompat::Shader;
	using UnityCompat::TextAsset;
	using UnityCompat::Texture;
	using UnityCompat::Texture2D;
	using UnityCompat::TextureFormat;
	using UnityCompat::TextureWrapMode;
	using UnityCompat::FilterMode;
	using UnityCompat::WorldMatrix;
	using UnityCompat::ParticleSystem;
	using UnityCompat::ParticleSystemRenderer;

	/// UnityEngine.Random (System.Random is cs::Random): `Random.value` -> `UnityRandom::value()`.
	using UnityRandom = UnityCompat::Random;

	/// Object.Destroy for every Unity object kind.
	using UnityCompat::Destroy;
}
