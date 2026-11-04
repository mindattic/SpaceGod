#pragma once

// The bridge from the Unity scene (UnityRendering.h's GameObjects, renderers, meshes and materials) to Unreal (US-T30).
// Once a frame, after every view has placed what it draws, Sync() gives each live renderer an Unreal
// UDynamicMeshComponent on the host actor (full-precision UVs, which the line and asteroid layouts need), uploads a mesh
// again only when it changed, keeps one UMaterialInstanceDynamic per material (and one per renderer with a property
// block), and sets each component's transform from its Unity world matrix through UnrealSpace.h. Everything is drawn
// camera-relative as the original does: the Unreal camera sits at the origin (the floating origin).
//
// The host (US-T29's app actor) calls Attach once, BeginFrame at the start of its tick and Sync at its end.

#include "CoreMinimal.h"
#include "UnityCompat/UnityRendering.h"
#include "UnityCompat/UnityObjects.h"
#include "UnityCompat/UnityRuntime.h"

class AActor;
class UPrimitiveComponent;
class UWorld;

namespace SpaceGod::UnityCompat
{
	class UnityScene
	{
	public:
		/// What the last Sync pushed (the smoke logs it, as the C# track's UnityBridge.Last): the scene's renderers, the
		/// Unreal primitives backing them, those drawn this frame and their triangles, meshes uploaded this frame,
		/// material instances made so far.
		struct Stats
		{
			int Renderers = 0, Primitives = 0, Visible = 0, MeshUploads = 0, MaterialInstances = 0;
			int64_t Triangles = 0;
			/// US-T53 (not in ToString, which the smoke compares with the C# track's): renderers left out by the screen
			/// camera's culling mask, the layers (a bit each) of the renderers drawn (the backdrop aside), whether its
			/// SolidColor backdrop was drawn and whether a Light lit a material (US-T57).
			int Masked = 0, VisibleLayers = 0;
			bool Backdrop = false, KeyLit = false;
			cs::String ToString() const;
		};
		static const Stats& Last();

		/// The camera that draws the screen, as Unity picks it (the C# track's UnityBridge.ScreenCamera, US-T53): of the
		/// active, enabled cameras, the one with the highest depth (the model viewer's, depth 1000, over the game's;
		/// Camera.main on a tie); Camera.main if none is enabled.
		static Camera* ScreenCamera();

		/// A light on a GameObject (RenderBackend::AddKeyLight), kept while the transform it sits under lives.
		static void AddLight(const cs::Ref<Light>& light);

		/// The enabled directional light under `camera` whose culling mask overlaps the camera's (the model viewer's key
		/// light), or null: the C# track's UnityBridge.KeyLightOf.
		static Light* KeyLightOf(Camera* camera);

		/// The actor whose components draw the scene (and whose world it is in).
		static void Attach(AActor* host);
		/// Lets go of every Unreal component and material instance (the game scene is going).
		static void Detach();
		static AActor* Host();
		static UWorld* World();

		/// Start of a frame: finishes background loads (Resources::Pump) and steps the particle systems.
		static void BeginFrame(float dt);

		/// End of a frame: every renderer to its Unreal component, the shader globals to MPC_SpaceGod.
		static void Sync();

		/// Sync without ending the frame: brings Unreal up to date now (a scene capture about to render, ShipThumbnails).
		static void SyncNow();

		/// The number of the frame being built (Renderer::OnlyFrame), counted by Sync.
		static int64_t CurrentFrame();

		/// The Unreal components drawing the renderers under root (a scene capture's show-only list).
		static TArray<UPrimitiveComponent*> ComponentsUnder(const cs::Ref<Transform>& root);
	};
}
