#pragma once

// The ONE place a Unity shader becomes an Unreal material (US-T30), following Shared/Plugins/SpaceGodRender's README
// (US-T24): each of Unity.SpaceGod's shaders ("Unity.SpaceGod/Planet", or RenderShaders' bare "Planet") is drawn with
// the plugin's /SpaceGodRender/Materials/M_<Shader>, parameters set by their Unity names (_Color, _MainTex, ...),
// globals in MPC_SpaceGod. Until a material is in the plugin, a placeholder (the engine's default material) stands in
// behind the same lookup, so the real one changes nothing here.
//
// The plugin's Unity -> Unreal rules applied here: SetColor values sRGB -> linear, SetVector values as they are;
// float arrays _Drift[k] -> component k % 4 of _Drift_{k / 4}; positions the game sets are world positions in Unreal
// units and axes (UnrealSpace.h), lengths in Unreal units; draw order of translucent shaders by TranslucencySortPriority.

#include "UnityCompat/UnityRendering.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMaterialParameterCollection;
class UWorld;

namespace SpaceGod::UnityCompat
{
	/// How a shader's meshes go to the GPU (Unreal has no point or line topology).
	enum class MeshLayout : int
	{
		/// Triangles: Unity uv -> (u, 1 - v); uv0.zw (Fx's shape slot) -> TexCoord1 when the mesh has no uv1.
		Triangles,
		/// Each point a quad of 4 vertices at the point, TexCoord0 = corner (+-1, +-1) (M_Stars, M_Points).
		Points,
		/// Each segment a ribbon of 4 vertices at its ends, TexCoord1.xy + TexCoord2.x = the other end (local),
		/// TexCoord2.y = side (-1/+1) (M_Line).
		Lines,
	};

	struct ShaderBinding
	{
		static constexpr const char* MaterialRoot = "/SpaceGodRender/Materials";
		static constexpr const char* GlobalsCollection = "/SpaceGodRender/Materials/MPC_SpaceGod.MPC_SpaceGod";

		/// "Unity.SpaceGod/Planet" -> "Planet".
		static cs::String ShaderName(const Shader* shader);

		/// The Unreal parent material for a shader (the plugin's, else the placeholder).
		static UMaterialInterface* Parent(const Shader* shader);

		/// Whether the shader is drawn with its real SpaceGodRender material.
		static bool IsReal(const Shader* shader);

		/// Drawn with URP Lit (M_Lit, the one material Unity's Light components reach, US-T57): the glTF import's
		/// material (glTFast's glTF-pbrMetallicRoughness shader graph, a URP Lit target) and URP's own Lit shader.
		static bool IsLit(const Shader* shader);

		/// The plugin's draw order for a translucent shader (Unity's render queue order).
		static int SortPriority(const Shader* shader);

		/// Unreal draws nothing but triangles: how a sub-mesh of this topology is laid out.
		static MeshLayout LayoutFor(MeshTopology topology);

		/// Pushes Unity properties into a material instance by their Unity names (an M_Line instance also gets
		/// LineWidthPx as _LineWidthPx).
		static void Push(const MaterialProperties& properties, UMaterialInstanceDynamic* mid);

		/// The width M_Line draws a line's ribbon at, in pixels (US-T55). Unity's lines are Direct3D hardware lines: with
		/// MSAA on, Direct3D rasterises a line as a 1.4-pixel-wide quad sampled per sample; without it, one pixel per step
		/// (the 1-pixel ribbon). SetLineWidthPx also sets it on every live M_Line instance.
		static float LineWidthPx();
		static void SetLineWidthPx(float width);

		/// The globals collection (Shader.SetGlobalVector's), or null until the plugin has it.
		static UMaterialParameterCollection* Globals();

		/// Pushes the shader globals (the stars' light) into the world's MPC_SpaceGod instance.
		static void PushGlobals(UWorld* world, const MaterialProperties& globals);

		/// Unity's sRGB -> linear (what SetColor does in a linear-space project), alpha untouched.
		static Color Linear(Color c);
	};
}
