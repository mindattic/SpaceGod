#pragma once
// Port of Unity.SpaceGod Runtime/Views/AsteroidBelt.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class AsteroidField;
	class ScaleModel;
}

namespace SpaceGod::UnityCompat
{
	class GUIStyle;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// Draws the ~111,000 real asteroids of <see cref="AsteroidField"/> in one procedural draw call.
	/// The GPU propagates every orbit per frame (Asteroids.shader), so the CPU cost is a handful of
	/// uniforms; only the few named asteroids that get labels are propagated on the CPU.
	/// </summary>
	/// <remarks>
	/// Unreal: a MonoBehaviour on the app's GameObject in Unity, a plain object owned by the app here (the app calls
	/// <see cref="OnGUI"/> from its own GUI pass while <see cref="enabled"/>). A material cannot read a StructuredBuffer
	/// or SV_VertexID, so (SpaceGodRender's README) the packed float4s are a float RGBA texture and the draw is a mesh of
	/// six vertices per asteroid, uv0 = (index, corner), drawn on the frames <see cref="Render"/> asks for it as
	/// Graphics.RenderPrimitives draws.
	/// </remarks>
	class AsteroidBelt final
	{
		struct NamedAsteroid
		{
			int Number;
			const char* Name;
		};

		/// <summary>Asteroids labelled by name (the largest and best known; Ceres, Psyche and the Trojans Hektor, Patroclus and Eurybates are full bodies,
		/// labelled as such).</summary>
		static const cs::List<NamedAsteroid>& Named();

	public:
		cs::Ref<Core::AsteroidField> Field;

		/// <summary>MonoBehaviour.enabled: the app turns the belt off outside Sol.</summary>
		bool enabled = true;

	private:
		UnitySpaceApp* app = nullptr;
		cs::Ref<Texture2D> buffer;
		cs::Ref<Material> material;
		cs::Ref<GameObject> drawn;
		cs::Ref<MeshRenderer> drawnRenderer;
		cs::List<int> namedIndex;
		cs::Ref<UnityCompat::GUIStyle> labelStyle, shadowStyle;

	public:
		static cs::Ref<AsteroidBelt> Create(UnitySpaceApp* app);

		/// <summary>Called by the app after the camera and layout are final for the frame.</summary>
		void Render(double jd, const cs::Ref<Core::ScaleModel>& scale, Vector3 sunRelative);

		/// <summary>Scene position of a field asteroid, mapped like the shader does (the Sun's mapping).</summary>
		Core::DVec3 SceneOf(int index, double jd);

		void OnGUI();

		void OnDestroy();
	};
}
