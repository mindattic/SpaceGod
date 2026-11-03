// Port of Unity.SpaceGod Runtime/Views/AsteroidBelt.cs (US-T30).
#include "Views/AsteroidBelt.h"
#include "Rendering/Materials.h"
#include "UnityCompat/UnityGui.h"
#include "UnityCompat/UnityScene.h"
#include "UnitySpaceApp.h"
#include "Camera/StarTravel.h"
#include "SimClock.h"
#include "ModelViewer.h"

#include "Astro.h"
#include "AsteroidField.h"
#include "BodyCatalog.h"
#include "ScaleModel.h"

#include "Engine/Texture2D.h"
#include "TextureResource.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;
	using UnityCompat::GUI;
	using UnityCompat::GUIStyle;

	const cs::List<AsteroidBelt::NamedAsteroid>& AsteroidBelt::Named()
	{
		static cs::List<NamedAsteroid> named{
			{ 4, "Vesta" }, { 2, "Pallas" }, { 10, "Hygiea" }, { 3, "Juno" }, { 704, "Interamnia" },
			{ 588, "Achilles" },
		};
		return named;
	}

	namespace
	{
		/// The packed float4s as a float RGBA texture, row-major (SpaceGodRender's _Asteroids, read with Load).
		cs::Ref<Texture2D> PackTexture(const cs::List<float>& packed)
		{
			int texels = packed.Count() / 4;
			constexpr int width = 2048;
			int height = FMath::Max(1, (texels + width - 1) / width);
			UTexture2D* tex = NewObject<UTexture2D>(GetTransientPackage(), TEXT("Asteroids"), RF_Transient);
			auto* data = new FTexturePlatformData();
			data->SizeX = width;
			data->SizeY = height;
			data->PixelFormat = PF_A32B32G32R32F;
			auto* mip = new FTexture2DMipMap(width, height, 1);
			data->Mips.Add(mip);
			mip->BulkData.Lock(LOCK_READ_WRITE);
			int64 bytes = static_cast<int64>(width) * height * 16;
			auto* dst = reinterpret_cast<float*>(mip->BulkData.Realloc(bytes));
			FMemory::Memzero(dst, bytes);
			FMemory::Memcpy(dst, packed.Raw().data(), static_cast<size_t>(packed.Count()) * sizeof(float));
			mip->BulkData.Unlock();
			tex->SetPlatformData(data);
			tex->SRGB = false;
			tex->Filter = TF_Nearest;
			tex->AddressX = TA_Clamp;
			tex->AddressY = TA_Clamp;
			tex->NeverStream = true;
			tex->CompressionSettings = TC_HDR;
			tex->UpdateResource();
			return UnityCompat::TextureFactory::Wrap(tex, width, height, "Asteroids");
		}
	}

	cs::Ref<AsteroidBelt> AsteroidBelt::Create(UnitySpaceApp* app)
	{
		auto asset = Resources::LoadText("Data/asteroids");
		if (!asset)
		{
			Debug::LogError("[Unity.SpaceGod] Missing Resources/Data/asteroids.bytes");
			return nullptr;
		}
		auto belt = cs::New<AsteroidBelt>();
		belt->app = app;
		belt->Field = AsteroidField::Parse(asset->bytes);

		cs::List<float> packed(std::vector<float>(static_cast<size_t>(belt->Field->Count) * AsteroidField::GpuFloatsPerAsteroid));
		belt->Field->PackGpu(packed);
		belt->buffer = PackTexture(packed);
		belt->material = Materials::Create(Materials::Asteroids(), "Asteroids");
		belt->material->SetTexture("_Asteroids", belt->buffer);

		// Graphics.RenderPrimitives(Triangles, Count * 6): six vertices per asteroid, each naming its asteroid and corner.
		int n = belt->Field->Count;
		std::vector<Vector3> vertices(static_cast<size_t>(n) * 6);
		std::vector<Vector2> uvs(vertices.size());
		std::vector<int> indices(vertices.size());
		for (int i = 0; i < n; i++)
			for (int c = 0; c < 6; c++)
			{
				int v = i * 6 + c;
				uvs[v] = Vector2(static_cast<float>(i), static_cast<float>(c));
				indices[v] = v;
			}
		auto mesh = cs::New<Mesh>("Asteroids");
		mesh->indexFormat = IndexFormat::UInt32;
		mesh->RawUv0 = true;
		mesh->SetVertices(vertices);
		mesh->SetUVs(0, uvs);
		mesh->SetTriangles(indices, 0, false);
		mesh->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 2e9f));
		belt->drawn = NewGameObject("Asteroids");
		AddComponent<MeshFilter>(belt->drawn)->sharedMesh = mesh;
		belt->drawnRenderer = AddComponent<MeshRenderer>(belt->drawn);
		belt->drawnRenderer->SetSharedMaterial(belt->material);
		belt->drawnRenderer->shadowCastingMode = ShadowCastingMode::Off;
		belt->drawnRenderer->receiveShadows = false;
		belt->drawnRenderer->OnlyFrame = INT64_MIN + 1; // nothing until Render asks

		for (const auto& named : Named()) belt->namedIndex.Add(belt->Field->IndexOfNumber(named.Number));
		return belt;
	}

	void AsteroidBelt::Render(double jd, const cs::Ref<ScaleModel>& scale, Vector3 sunRelative)
	{
		if (!Alive(material)) return;
		cs::List<float> c0, c1;
		AsteroidField::SunMapping(scale, app->Catalog()->Sun(), c0, c1);
		material->SetFloat("_DtDays", static_cast<float>(jd - Field->EpochJd));
		material->SetVector("_SunMap0", Vector4(c0[0], c0[1], c0[2], c0[3]));
		material->SetVector("_SunMap1", Vector4(c1[0], c1[1], c1[2], c1[3]));
		material->SetVector("_SunRelative", sunRelative);
		// Drawn this frame only, as Graphics.RenderPrimitives draws.
		drawnRenderer->OnlyFrame = UnityCompat::UnityScene::CurrentFrame();
	}

	DVec3 AsteroidBelt::SceneOf(int index, double jd)
	{
		DVec3 helioKm = Field->PositionAu(index, jd) * Astro::AuKm;
		return app->BodyScene(0) + app->Scale()->LocalOffset(app->Catalog()->Sun(), helioKm);
	}

	void AsteroidBelt::OnGUI()
	{
		if (!enabled) return;
		if (!app || !app->ShowLabels || !app->ShowHud || app->ScreenCleared || UnityCompat::Event::current().type != UnityCompat::EventType::Repaint
			|| app->Travel->Active || !app->SystemVisible) return;
		// Not over a screen that has taken over from the HUD (the model viewer, the spawn grid).
		if (ModelViewer::CapturesInput() || app->ShowSpawn) return;
		if (!labelStyle)
		{
			labelStyle = cs::New<GUIStyle>(*GUI::skin().label);
			labelStyle->fontSize = 11;
			labelStyle->normal.textColor = Color(0.82f, 0.78f, 0.7f, 0.9f);
		}
		if (!shadowStyle)
		{
			shadowStyle = cs::New<GUIStyle>(*labelStyle);
			shadowStyle->normal.textColor = Color(0, 0, 0, 0.8f);
		}
		double jd = app->Clock->Jd;
		DVec3 eye = app->SpaceCamera->ScenePosition;
		for (int i = 0; i < Named().Count(); i++)
		{
			int k = namedIndex[i];
			if (k < 0) continue;
			DVec3 scene = SceneOf(k, jd);
			// Only label an asteroid when it is near the camera or the camera is out viewing the belt,
			// not as clutter behind a planet close-up.
			double fromSun = (scene - app->BodyScene(0)).Length();
			bool overview = (eye - app->BodyScene(0)).Length() > fromSun * 0.9;
			if (!overview && (eye - scene).Length() > fromSun * 0.25) continue;
			Vector2 p;
			float depth;
			if (!app->ToGui(scene, p, depth) || app->Occluded(scene)) continue;
			Rect r(p.x + 5, p.y - 8, 120, 18);
			GUI::Label(Rect(r.x + 1, r.y + 1, r.width, r.height), Named()[i].Name, shadowStyle);
			GUI::Label(r, Named()[i].Name, labelStyle);
		}
	}

	void AsteroidBelt::OnDestroy()
	{
		buffer = nullptr;
		if (Alive(drawn)) Destroy(drawn);
	}
}
