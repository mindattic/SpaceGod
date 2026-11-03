// Port of Unity.SpaceGod Runtime/Rendering/ShipThumbnails.cs (US-T30).
#include "Rendering/ShipThumbnails.h"
#include "Rendering/Pipeline/RenderBackend.h"
#include "Rendering/ShipMeshFactory.h"
#include "Rendering/ShipModels.h"
#include "UnityCompat/UnityScene.h"
#include "UnrealSpace.h"

#include "Ships.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"

#include <tuple>

using namespace SpaceGod::Core;

namespace SpaceGod::Rendering
{
	namespace US = SpaceGod::UnrealSpace;

	namespace
	{
		TWeakObjectPtr<USceneCaptureComponent2D> camera;

		// Keyed by livery too: the spawn screen shows the same class in the home or the hostile colours.
		cs::Dictionary<std::tuple<ShipClass, cs::Ref<Faction>>, cs::Ref<Texture>>& Cache()
		{
			static cs::Dictionary<std::tuple<ShipClass, cs::Ref<Faction>>, cs::Ref<Texture>> cache;
			return cache;
		}

		USceneCaptureComponent2D* EnsureCamera()
		{
			if (camera.IsValid()) return camera.Get();
			AActor* host = UnityCompat::UnityScene::Host();
			if (!host) return nullptr;
			auto* c = NewObject<USceneCaptureComponent2D>(host, TEXT("ShipThumbnailCamera"), RF_Transient);
			// Never auto-rendered into the normal camera stack -- only CaptureScene() called explicitly below.
			c->bCaptureEveryFrame = false;
			c->bCaptureOnMovement = false;
			// Only the preview (Unity's culling mask of the ModelViewer layer).
			c->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
			c->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
			// Unity's default 60 degree (vertical) field of view on a square target.
			c->FOVAngle = 60.0f;
			c->ShowFlags.SetAtmosphere(false);
			c->ShowFlags.SetFog(false);
			c->SetUsingAbsoluteLocation(true);
			c->SetUsingAbsoluteRotation(true);
			c->RegisterComponent();
			host->AddInstanceComponent(c);
			RenderBackend::Active()->SetupCamera(nullptr, CameraRole::Offscreen);
			camera = c;
			return c;
		}
	}

	cs::Ref<Texture> ShipThumbnails::For(ShipClass cls, const cs::Ref<Faction>& faction)
	{
		auto key = std::make_tuple(cls, ShipMeshFactory::Canon(faction));
		cs::Ref<Texture> tex;
		if (Cache().TryGetValue(key, tex) && tex) return tex;
		tex = Render(cls, faction);
		Cache()[key] = tex;
		return tex;
	}

	void ShipThumbnails::Clear()
	{
		// The render targets go with their last reference.
		Cache().Clear();
	}

	cs::Ref<Texture> ShipThumbnails::Render(ShipClass cls, const cs::Ref<Faction>& faction)
	{
		USceneCaptureComponent2D* capture = EnsureCamera();
		if (!capture) return nullptr;

		auto root = NewGameObject("ShipThumbPreview");
		root->hideFlags = HideFlags::HideAndDontSave;
		// Off to one side, but near enough the origin for float precision: at 1e6 world units the hull's
		// vertices snapped to a 1/16-unit grid (6% of a one-unit hull), stair-stepping every edge. The
		// preview lives only inside this call and on its own layer, so nothing else ever draws it.
		root->transform->localPosition = Vector3(1000.0f, 1000.0f, 1000.0f);

		auto ship = cs::New<Ship>();
		ship->Id = 0;
		ship->Spec = ShipClassSpec::Of(cls);
		ship->Faction = faction;
		cs::List<cs::Ref<Renderer>> renderers;
		cs::Ref<Transform> stern, bow;
		ShipModels::Build(ship, root->transform, renderers, stern, bow);

		Bounds bounds(root->transform->localPosition, Vector3::one() * 0.01f);
		bool any = false;
		for (const auto& r : renderers)
		{
			if (!any)
			{
				bounds = r->bounds();
				any = true;
			}
			else bounds.Encapsulate(r->bounds());
		}
		float radius = Mathf::Max(bounds.extents.magnitude(), 0.05f);

		Vector3 position = bounds.center + Quaternion::Euler(18.0f, -35.0f, 0.0f) * Vector3::back() * (radius * 2.6f);
		// Transform.LookAt(center): forward to the target, world up.
		Quaternion rotation = Quaternion::LookRotation(bounds.center - position, Vector3::up());
		capture->SetWorldLocationAndRotation(US::ToUnreal(position), US::ToUnreal(rotation));
		capture->bOverride_CustomNearClippingPlane = true;
		capture->CustomNearClippingPlane = US::ClipToUnreal(Mathf::Max(radius * 0.01f, 0.001f));
		capture->MaxViewDistanceOverride = US::ClipToUnreal(radius * 10.0f);

		auto* rt = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UTextureRenderTarget2D::StaticClass(),
			FName(*FString::Printf(TEXT("ShipThumb_%s"), UTF8_TO_TCHAR(Core::ToString(cls))))), RF_Transient);
		rt->RenderTargetFormat = RTF_RGBA8;
		// Unity's camera clears to this before drawing the hull.
		rt->ClearColor = FLinearColor(0.05f, 0.06f, 0.09f, 1.0f);
		rt->InitAutoFormat(Size, Size);
		rt->UpdateResourceImmediate(true);

		// Ship.shader lights only from the scene's global star(s), not Unity lights: light the hull from
		// a fixed key direction for the snapshot, not from wherever the game's Sun happens to be just now.
		auto backend = RenderBackend::Active();
		StarLighting gameLight = backend->Lighting();
		Vector3 key = bounds.center + Quaternion::Euler(35.0f, -30.0f, 0.0f) * Vector3::back() * (radius * 1000.0f);
		StarLighting snapshot;
		snapshot.SunPosition = Vector4(key.x, key.y, key.z, 0);
		snapshot.SunColor = Vector4(1.2f, 1.2f, 1.2f, 1.0f);
		snapshot.Sun2Position = snapshot.SunPosition;
		snapshot.Sun2Color = Vector4::zero();
		backend->SetLighting(snapshot);

		// The preview's components exist and stand where they should before the capture renders (Camera.Render).
		UnityCompat::UnityScene::SyncNow();
		UWorld* world = UnityCompat::UnityScene::World();
		if (world)
		{
			world->UpdateParameterCollectionInstances(true, true);
			world->SendAllEndOfFrameUpdates();
		}
		capture->ShowOnlyComponents.Reset();
		for (auto* c : UnityCompat::UnityScene::ComponentsUnder(root->transform)) capture->ShowOnlyComponents.Add(c);
		capture->TextureTarget = rt;
		capture->CaptureScene();
		capture->TextureTarget = nullptr;
		capture->ShowOnlyComponents.Reset();
		backend->SetLighting(gameLight);

		Destroy(root);
		UnityCompat::UnityScene::SyncNow();
		if (world) world->UpdateParameterCollectionInstances(true, true);
		return UnityCompat::TextureFactory::Wrap(rt, Size, Size, cs::Cat("ShipThumb_", Core::ToString(cls)));
	}
}
