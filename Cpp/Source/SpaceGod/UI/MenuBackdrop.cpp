#include "UI/MenuBackdrop.h"

#include "Misc/Paths.h"

#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/Pipeline/RenderBackend.h"
#include "Rendering/ShipMeshFactory.h"
#include "Rendering/ShipModels.h"
#include "UnityCompat/UnityRendering.h"
#include "Views/BodyView.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ships.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	using Rendering::Materials;
	using Rendering::MeshFactory;
	using Rendering::ShipMeshFactory;
	using Rendering::ShipModels;

	namespace
	{
		// The planet, in the camera's frame: low in the right of the view, its edge running off the screen.
		constexpr float PlanetDistance = 420.0f;
		constexpr float PlanetRadius = 150.0f;
		constexpr float PlanetYawDeg = 34.0f;    // right of the view's centre
		constexpr float PlanetPitchDeg = 19.0f;  // below it
		constexpr float PlanetDriftDeg = 5.0f;   // how far it swings either way as the view coasts past
		constexpr float PlanetDriftPeriod = 300.0f;
		constexpr float PlanetSpinDegPerSecond = 1.2f;
		constexpr float PlanetTiltDeg = 23.4f;

		// The ship's passes: one every PassEvery seconds, the first a few seconds in, each crossing in PassSeconds.
		constexpr float FirstPass = 6.0f;
		constexpr float PassEvery = 34.0f;
		constexpr float PassSeconds = 12.0f;
		constexpr float ShipLength = 2.6f;

		/// The light, from the upper left and a little behind the view, so the planet shows a broad lit crescent.
		Vector3 SunDirection() { return Vector3(-0.78f, 0.42f, 0.46f).normalized(); }

		const ShipClass PassHulls[] = { ShipClass::Destroyer, ShipClass::Freighter, ShipClass::Cruiser, ShipClass::Explorer };

		/// One quad for the glow, every visit to the menu (as BodyView keeps its own).
		const cs::Ref<Mesh>& GlowQuad()
		{
			static cs::Ref<Mesh> quad = MeshFactory::Quad();
			return quad;
		}

		cs::Ref<BodyDefinition> Earth()
		{
			cs::Ref<BodyDefinition> earth;
			auto sol = BodyCatalog::CreateSolSystem();
			return sol && sol->TryGet("Earth", earth) ? earth : nullptr;
		}
	}

	MenuBackdrop::MenuBackdrop(const cs::Ref<Transform>& parent)
	{
		root = NewGameObject("MenuBackdrop");
		root->transform->SetParent(parent, false);

		if (auto earth = Earth())
		{
			planet = std::make_unique<Views::BodyView>(earth, root->transform);
			planet->Root->transform->localScale = Vector3::one() * PlanetRadius;
		}

		ship = NewGameObject("MenuShip");
		ship->transform->SetParent(root->transform, false);
		shipMesh = AddComponent<MeshFilter>(ship);
		auto hull = AddComponent<MeshRenderer>(ship);
		hull->SetSharedMaterial(ShipModels::ProceduralMaterial());
		hull->shadowCastingMode = ShadowCastingMode::Off;
		hull->receiveShadows = false;

		// The engines: a soft blue glow at the stern, turned to the camera each frame.
		auto glowGo = NewGameObject("MenuShipGlow");
		glowGo->transform->SetParent(ship->transform, false);
		glowGo->transform->localPosition = Vector3(0, 0, -0.56f);
		glowGo->transform->localScale = Vector3::one() * 0.55f;
		AddComponent<MeshFilter>(glowGo)->sharedMesh = GlowQuad();
		auto glowRenderer = AddComponent<MeshRenderer>(glowGo);
		glowMaterial = Materials::Create(Materials::Glow(), "MenuShipGlow");
		glowMaterial->SetColor("_Color", Color(0.45f, 0.7f, 1.0f));
		glowMaterial->SetFloat("_Intensity", 1.4f);
		glowMaterial->SetFloat("_Falloff", 2.4f);
		glowRenderer->SetSharedMaterial(glowMaterial);
		glowRenderer->shadowCastingMode = ShadowCastingMode::Off;
		glow = glowGo->transform;
		ship->SetActive(false);
	}

	MenuBackdrop::~MenuBackdrop() { Destroy(); }

	void MenuBackdrop::Destroy()
	{
		if (!root) return;
		Materials::DestroyOwned(root->transform);
		UnityCompat::Destroy(root);
		root = nullptr;
		planet.reset();
		ship = nullptr;
		shipMesh = nullptr;
		glow = nullptr;
		glowMaterial = nullptr;
	}

	void MenuBackdrop::StartPass(int index)
	{
		pass = index;
		passStart = FirstPass + index * PassEvery;
		side = index % 2 == 0 ? -1.0f : 1.0f; // the first from the right, then turn about
		ShipClass cls = PassHulls[index % (sizeof(PassHulls) / sizeof(PassHulls[0]))];
		shipMesh->sharedMesh = ShipMeshFactory::Get(cls, nullptr);
	}

	void MenuBackdrop::Update(Camera* camera, float time)
	{
		if (!root || camera == nullptr) return;
		auto view = camera->transform;
		Vector3 eye = view->position();
		Quaternion turn = view->rotation();
		if (camera->farClipPlane < PlanetDistance * 2) camera->farClipPlane = PlanetDistance * 2;

		// The menu's star: far off in the camera's frame, so the planet keeps its crescent while the sky turns.
		Vector3 sun = eye + turn * SunDirection() * 1e9f;
		Rendering::StarLighting light;
		light.SunPosition = Vector4(sun.x, sun.y, sun.z, 0);
		light.SunColor = Vector4(1.0f, 0.97f, 0.92f, 1.0f);
		light.Sun2Position = light.SunPosition;
		light.Sun2Color = Vector4(0, 0, 0, 0);
		Rendering::RenderBackend::Active()->SetLighting(light);

		if (planet)
		{
			float drift = Mathf::Sin(time * 2.0f * Mathf::PI / PlanetDriftPeriod) * PlanetDriftDeg;
			Quaternion bearing = turn * Quaternion::Euler(PlanetPitchDeg, PlanetYawDeg + drift, 0);
			auto t = planet->Root->transform;
			t->SetPosition(eye + bearing * Vector3::forward() * PlanetDistance);
			t->SetRotation(turn * Quaternion::Euler(0, 0, -PlanetTiltDeg) * Quaternion::Euler(0, -time * PlanetSpinDegPerSecond, 0));
		}

		// The ship: which pass is due, and how far along it is.
		int due = time < FirstPass ? -1 : static_cast<int>((time - FirstPass) / PassEvery);
		if (due >= 0 && due != pass) StartPass(due);
		float k = pass < 0 ? -1.0f : (time - passStart) / PassSeconds;
		bool flying = k >= 0 && k <= 1;
		ship->SetActive(flying);
		if (!flying) return;
		// Upper part of the view, a little nearer at the end than the start, climbing gently.
		Vector3 from(side * -34.0f, 8.0f, 30.0f), to(side * 34.0f, 10.0f, 24.0f);
		Vector3 local = Vector3::Lerp(from, to, k);
		Vector3 heading = turn * (to - from).normalized();
		Quaternion facing = Quaternion::LookRotation(heading, view->up()) * Quaternion::Euler(0, 0, side * 10.0f);
		ship->transform->SetPosition(eye + turn * local);
		ship->transform->SetRotation(facing);
		ship->transform->localScale = Vector3::one() * ShipLength;
		glow->SetRotation(turn);
	}

	cs::Ref<Texture2D> MenuBackdrop::LoadLogo()
	{
		const FString candidates[] = {
			FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Brand"), TEXT("logo.png")),
			FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("assets"), TEXT("logo.png")),
		};
		for (const FString& candidate : candidates)
		{
			FString file = FPaths::ConvertRelativePathToFull(candidate);
			if (!FPaths::FileExists(file)) continue;
			auto image = UnityCompat::TextureFactory::Decode(cs::String(TCHAR_TO_UTF8(*file)));
			if (!image.Ok()) continue;
			// Black line art on white (or on clear): each pixel white, as opaque as it is dark.
			std::vector<uint8_t> level = image.Levels[0];
			for (size_t i = 0; i + 3 < level.size(); i += 4)
			{
				float luminance = (0.0722f * level[i] + 0.7152f * level[i + 1] + 0.2126f * level[i + 2]) / 255.0f; // B G R
				float alpha = (1.0f - luminance) * (level[i + 3] / 255.0f);
				level[i] = level[i + 1] = level[i + 2] = 255;
				level[i + 3] = static_cast<uint8_t>(Mathf::RoundToInt(Mathf::Clamp01(alpha) * 255.0f));
			}
			auto texture = UnityCompat::TextureFactory::FromMips("SpaceGodLogo", image.Width, image.Height,
				UnityCompat::TextureFactory::MipChain(level.data(), image.Width, image.Height), TextureWrapMode::Clamp, TextureWrapMode::Clamp, false);
			if (texture) texture->hideFlags = HideFlags::DontSave;
			return texture;
		}
		return nullptr;
	}
}
