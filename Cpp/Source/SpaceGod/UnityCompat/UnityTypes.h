#pragma once

// Brings the Unity stand-ins into namespace SpaceGod, as `using UnityEngine;` does for the Runtime's C#, so Runtime
// code under SpaceGod (and SpaceGod::Cameras, ::Input, ::UI, ::Views, ::Rendering) writes Vector3, Quaternion, Mathf,
// Time... unqualified. (Using-declarations of our own types, not a using-directive.)
//
// Core types: .cpp files put `using namespace SpaceGod::Core;` at GLOBAL scope, before `namespace SpaceGod {`, so a
// Runtime name (SpaceGod::Target) wins over a Core one of the same name (Core::Target in Sensors) as C# resolves it.

#include "UnityCompat/UnityMath.h"
#include "UnityCompat/UnityRuntime.h"
#include "UnityCompat/UnityObjects.h"

namespace SpaceGod
{
	using UnityCompat::Mathf;
	using UnityCompat::Vector2;
	using UnityCompat::Vector3;
	using UnityCompat::Vector4;
	using UnityCompat::Quaternion;
	using UnityCompat::Color;
	using UnityCompat::Rect;
	using UnityCompat::Time;
	using UnityCompat::Screen;
	using UnityCompat::Cursor;
	using UnityCompat::CursorLockMode;
	using UnityCompat::Debug;
	using UnityCompat::Application;
	using UnityCompat::AudioListener;
	using UnityCompat::GameObject;
	using UnityCompat::Transform;
	using UnityCompat::Camera;
	using UnityCompat::CameraClearFlags;
	using UnityCompat::Material;
	using UnityCompat::Mesh;
	using UnityCompat::Texture2D;
	using UnityCompat::Light;
	using UnityCompat::LightType;
	using UnityCompat::ToVector3;
	using UnityCompat::ToDVec3;
}
