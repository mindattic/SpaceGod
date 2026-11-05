// Port of Unity.SpaceGod Runtime/Rendering/ShipThumbnails.cs (US-T30).
#include "Rendering/ShipThumbnails.h"
#include "Rendering/Pipeline/RenderBackend.h"
#include "Rendering/ShipMeshFactory.h"
#include "Rendering/ShipModels.h"
#include "UnityCompat/UnityScene.h"
#include "UnrealSpace.h"

#include "Ships.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Containers/Ticker.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "UObject/StrongObjectPtr.h"

#include <map>
#include <optional>
#include <tuple>
#include <vector>

using namespace SpaceGod::Core;

namespace SpaceGod::Rendering
{
	namespace US = SpaceGod::UnrealSpace;

	namespace
	{
		TWeakObjectPtr<USceneCaptureComponent2D> camera;

		// Where the previews stand: off to one side, but near enough the origin for float precision (at 1e6 world units
		// the hull's vertices snapped to a 1/16-unit grid, 6% of a one-unit hull, stair-stepping every edge).
		const Vector3 PreviewAt(1000.0f, 1000.0f, 1000.0f);

		// The snapshot's light, in the shaders' terms (Ship.shader and M_StarSparrowShip light only from the star globals,
		// never from Unreal lights): a white key from just above the camera (the hull's whole visible side lit; a key further
		// round, from 35/-30, left the kit-assembled hulls in their night-side fill) and a dim cool fill from below on the far
		// side, far off (100 km) so every hull, a 3 km carrier too, is lit from one direction.
		StarLighting SnapshotLight()
		{
			const Vector3 keyFrom = Quaternion::Euler(24.0f, -38.0f, 0.0f) * Vector3::back();
			const Vector3 fillFrom = Vector3(0.6f, -0.5f, 0.6f).normalized();
			StarLighting l;
			Vector3 key = PreviewAt + keyFrom * 1e5f, fill = PreviewAt + fillFrom * 1e5f;
			l.SunPosition = Vector4(key.x, key.y, key.z, 0);
			l.SunColor = Vector4(3.0f, 2.95f, 2.88f, 1.0f);
			l.Sun2Position = Vector4(fill.x, fill.y, fill.z, 0);
			l.Sun2Color = Vector4(0.3f, 0.36f, 0.48f, 0.4f);
			return l;
		}

		// Keyed by livery too: the spawn screen shows the same class in the home or the hostile colours.
		cs::Dictionary<std::tuple<ShipClass, cs::Ref<Faction>>, cs::Ref<Texture>>& Cache()
		{
			static cs::Dictionary<std::tuple<ShipClass, cs::Ref<Faction>>, cs::Ref<Texture>> cache;
			return cache;
		}

		/// Each thumbnail's render target, for SaveSheet.
		std::map<const Texture*, TWeakObjectPtr<UTextureRenderTarget2D>>& Targets()
		{
			static std::map<const Texture*, TWeakObjectPtr<UTextureRenderTarget2D>> targets;
			return targets;
		}

		/// A thumbnail on its way: its target (already handed out, cleared to the backdrop) and, once built, its preview.
		struct Job
		{
			ShipClass Class;
			cs::Ref<Faction> Livery;
			TStrongObjectPtr<UTextureRenderTarget2D> Target;
			cs::Ref<GameObject> Root;
			Bounds Box;
		};

		/// Jobs asked for wait in the queue while a batch is in flight.
		std::vector<Job>& Queue()
		{
			static std::vector<Job> jobs;
			return jobs;
		}

		std::vector<Job>& Batch()
		{
			static std::vector<Job> jobs;
			return jobs;
		}

		int batchFrames = 0;
		std::optional<StarLighting> lightBefore;
		FTSTicker::FDelegateHandle ticker;

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
			// US-T116: a fixed exposure of exactly 1. The game's materials are unlit and expose themselves (SG_Expose):
			// they divide by the view's EyeAdaptation value so that Unreal's exposure, applied after them, cancels out.
			// A capture has no view state, so EyeAdaptation reads 1 there, while the tonemapper applied whatever
			// exposure the capture's own settings came to. With eye adaptation off the exposure is Unreal's neutral
			// manual one, 1.0, matching the 1 the materials divide by: the image is the shaders' colour, as the game's
			// own view draws it.
			c->ShowFlags.SetEyeAdaptation(false);
			c->ShowFlags.SetLocalExposure(false);
			FPostProcessSettings& pp = c->PostProcessSettings;
			pp.bOverride_AutoExposureMethod = true;
			pp.AutoExposureMethod = AEM_Manual;
			pp.bOverride_AutoExposureBias = true;
			pp.AutoExposureBias = 0;
			pp.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
			pp.AutoExposureApplyPhysicalCameraExposure = false;
			// The game's neutral look (UnrealRenderBackend::Apply with the look off): no tone curve, gamut expansion,
			// vignette, bloom or colour gain, whatever the settings menu's look does to the screen.
			pp.bOverride_ToneCurveAmount = true;
			pp.ToneCurveAmount = 0;
			pp.bOverride_ExpandGamut = true;
			pp.ExpandGamut = 0;
			pp.bOverride_VignetteIntensity = true;
			pp.VignetteIntensity = 0;
			pp.bOverride_BloomIntensity = true;
			pp.BloomIntensity = 0;
			pp.bOverride_ColorGain = true;
			pp.ColorGain = FVector4(1, 1, 1, 1);
			c->PostProcessBlendWeight = 1.0f;
			c->SetUsingAbsoluteLocation(true);
			c->SetUsingAbsoluteRotation(true);
			c->RegisterComponent();
			host->AddInstanceComponent(c);
			RenderBackend::Active()->SetupCamera(nullptr, CameraRole::Offscreen);
			camera = c;
			return c;
		}

		/// The hull, built as a real ship's (ShipModels.Build) under a throwaway root: a bare Ship never added to a fleet.
		void Build(Job& job)
		{
			job.Root = NewGameObject("ShipThumbPreview");
			job.Root->hideFlags = HideFlags::HideAndDontSave;
			job.Root->transform->localPosition = PreviewAt;
			auto ship = cs::New<Ship>();
			ship->Id = 0;
			ship->Spec = ShipClassSpec::Of(job.Class);
			ship->Faction = job.Livery;
			cs::List<cs::Ref<Renderer>> renderers;
			cs::Ref<Transform> stern, bow;
			ShipModels::Build(ship, job.Root->transform, renderers, stern, bow);
			job.Box = Bounds(PreviewAt, Vector3::one() * 0.01f);
			bool any = false;
			for (const auto& r : renderers)
			{
				if (!any) job.Box = r->bounds();
				else job.Box.Encapsulate(r->bounds());
				any = true;
			}
		}

		/// The previews live a few frames: only scene captures draw them, never the screen.
		void HideFromScreen(const Job& job)
		{
			for (auto* c : UnityCompat::UnityScene::ComponentsUnder(job.Root->transform))
				if (!c->bVisibleInSceneCaptureOnly) c->SetVisibleInSceneCaptureOnly(true);
		}

		void Shoot(USceneCaptureComponent2D* capture, const Job& job)
		{
			float radius = Mathf::Max(job.Box.extents.magnitude(), 0.05f);
			Vector3 position = job.Box.center + Quaternion::Euler(18.0f, -35.0f, 0.0f) * Vector3::back() * (radius * 2.2f);
			// Transform.LookAt(center): forward to the target, world up.
			Quaternion rotation = Quaternion::LookRotation(job.Box.center - position, Vector3::up());
			// US-T116: a Unity camera looks down its +Z, which UnrealSpace maps to Unreal's -Y, while a scene capture looks
			// down its +X: the converted rotation needs the quarter turn the game's own camera gets
			// (AUnitySpaceAppActor::ApplyCamera), or the capture looks 90 degrees off the hull, at empty space.
			static const FQuat LookAlongUnityForward(FVector::UpVector, FMath::DegreesToRadians(-90.0));
			capture->SetWorldLocationAndRotation(US::ToUnreal(position), US::ToUnreal(rotation) * LookAlongUnityForward);
			capture->bOverride_CustomNearClippingPlane = true;
			capture->CustomNearClippingPlane = US::ClipToUnreal(Mathf::Max(radius * 0.01f, 0.001f));
			capture->MaxViewDistanceOverride = US::ClipToUnreal(radius * 10.0f);
			capture->ShowOnlyComponents.Reset();
			for (auto* c : UnityCompat::UnityScene::ComponentsUnder(job.Root->transform)) capture->ShowOnlyComponents.Add(c);
			capture->TextureTarget = job.Target.Get();
			capture->CaptureScene();
			capture->TextureTarget = nullptr;
			capture->ShowOnlyComponents.Reset();
		}

		void EndBatch()
		{
			for (auto& job : Batch())
				if (job.Root) Destroy(job.Root);
			Batch().clear();
			UnityCompat::UnityScene::SyncNow();
			RenderBackend::Active()->OverrideLighting(lightBefore);
			lightBefore.reset();
		}

		/// US-T116: a capture taken in the frame its preview was built drew the hull black: the new primitives and the
		/// snapshot light (the star globals, a material parameter collection) reach the renderer a frame late. So each
		/// batch is built (and its light held over the game's) one frame and captured two frames on, when both are in
		/// place; then the previews go and the game's own light comes back (the spawn screen's panel covers the
		/// screen meanwhile).
		bool Tick(float)
		{
			if (Batch().empty())
			{
				if (Queue().empty())
				{
					ticker.Reset();
					return false;
				}
				if (!UnityCompat::UnityScene::Host()) return true;
				Batch().swap(Queue());
				for (auto& job : Batch()) Build(job);
				UnityCompat::UnityScene::SyncNow();
				for (auto& job : Batch()) HideFromScreen(job);
				auto backend = RenderBackend::Active();
				lightBefore = backend->LightingOverride();
				backend->OverrideLighting(SnapshotLight());
				batchFrames = 0;
				return true;
			}
			// Keep the previews off the screen (and the light held) until they are captured.
			for (auto& job : Batch()) HideFromScreen(job);
			RenderBackend::Active()->OverrideLighting(SnapshotLight());
			if (++batchFrames < 2) return true;
			if (USceneCaptureComponent2D* capture = EnsureCamera())
			{
				UnityCompat::UnityScene::SyncNow();
				if (UWorld* world = UnityCompat::UnityScene::World())
				{
					world->UpdateParameterCollectionInstances(true, true);
					world->SendAllEndOfFrameUpdates();
				}
				for (const auto& job : Batch()) Shoot(capture, job);
			}
			EndBatch();
			return true;
		}

		UTextureRenderTarget2D* NewTarget(ShipClass cls, int size)
		{
			auto* rt = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UTextureRenderTarget2D::StaticClass(),
				FName(*FString::Printf(TEXT("ShipThumb_%s"), UTF8_TO_TCHAR(Core::ToString(cls))))), RF_Transient);
			// US-T116: sRGB. The capture writes the image's linear colour; into a plain RGBA8 target it was read back (and
			// drawn on the spawn screen) as if already gamma-encoded, two to three times too dark.
			rt->RenderTargetFormat = RTF_RGBA8_SRGB;
			// Unity's camera clears to this before drawing the hull.
			rt->ClearColor = FLinearColor(0.05f, 0.06f, 0.09f, 1.0f);
			rt->InitAutoFormat(size, size);
			rt->UpdateResourceImmediate(true);
			return rt;
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
		// The render targets go with their last reference; a snapshot on its way still lands in its own.
		Cache().Clear();
		Targets().clear();
	}

	bool ShipThumbnails::Pending() { return !Queue().empty() || !Batch().empty(); }

	cs::Ref<Texture> ShipThumbnails::Render(ShipClass cls, const cs::Ref<Faction>& faction)
	{
		if (!UnityCompat::UnityScene::Host()) return nullptr;
		UTextureRenderTarget2D* rt = NewTarget(cls, Size);
		Job job;
		job.Class = cls;
		job.Livery = faction;
		job.Target.Reset(rt);
		Queue().push_back(std::move(job));
		if (!ticker.IsValid()) ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
		cs::Ref<Texture> tex = UnityCompat::TextureFactory::Wrap(rt, Size, Size, cs::Cat("ShipThumb_", Core::ToString(cls)));
		Targets()[tex.get()] = rt;
		return tex;
	}

	bool ShipThumbnails::SaveSheet(const cs::List<ShipClass>& classes, const cs::Ref<Faction>& faction, const FString& file, int columns)
	{
		if (classes.Count() == 0) return true;
		cs::List<UTextureRenderTarget2D*> targets;
		for (auto cls : classes)
		{
			auto tex = For(cls, faction);
			auto it = tex ? Targets().find(tex.get()) : Targets().end();
			targets.Add(it != Targets().end() ? it->second.Get() : nullptr);
		}
		if (Pending()) return false;
		int cols = FMath::Clamp(columns, 1, classes.Count());
		int rows = (classes.Count() + cols - 1) / cols;
		FImage sheet(cols * Size, rows * Size, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		TArrayView64<FColor> out = sheet.AsBGRA8();
		for (FColor& c : out) c = FColor(12, 13, 18, 255);
		for (int i = 0; i < classes.Count(); i++)
		{
			FTextureRenderTargetResource* res = targets[i] ? targets[i]->GameThread_GetRenderTargetResource() : nullptr;
			TArray<FColor> pixels;
			if (!res || !res->ReadPixels(pixels) || pixels.Num() != Size * Size) continue;
			int x0 = (i % cols) * Size, y0 = (i / cols) * Size;
			for (int y = 0; y < Size; y++)
				for (int x = 0; x < Size; x++)
				{
					FColor c = pixels[y * Size + x];
					c.A = 255;
					out[static_cast<int64>(y0 + y) * sheet.SizeX + x0 + x] = c;
				}
		}
		FImageUtils::SaveImageByExtension(*file, sheet);
		return true;
	}
}
