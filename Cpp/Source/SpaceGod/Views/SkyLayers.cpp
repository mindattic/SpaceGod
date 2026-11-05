// US-T145: the sky as two layers, the travel between them, and the jump's tunnel.
#include "Views/SkyLayers.h"
#include "Views/BodyView.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/Pipeline/RenderBackend.h"
#include "Rendering/SystemSkies.h"
#include "Rendering/SystemSkyBake.h"

#include "Rendering/Skyboxes.h"
#include "Settings/GameSetting.h"
#include "Systems/Biomes.h"

#include "Async/Async.h"

#include <algorithm>
#include <cctype>
#include <cmath>

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;

	namespace
	{
		/// Volume by default: the two-layer sky is the travel model (US-T145); the cube and the sphere are for comparing.
		SkyMode& ModeRef()
		{
			static SkyMode mode = SkyMode::Volume;
			return mode;
		}

		constexpr float FarFadeSeconds = 0.6f;

		void SetAxes(const cs::Ref<Material>& m, int frame)
		{
			DVec3 x, y, z;
			SkyboxCatalog::FrameAxes(static_cast<SkyFrame>(frame), x, y, z);
			m->SetVector("_AxisX", BodyView::ToUnity(x));
			m->SetVector("_AxisY", BodyView::ToUnity(y));
			m->SetVector("_AxisZ", BodyView::ToUnity(z));
		}
	}

	SkyMode SkyLayers::Mode() { return ModeRef(); }
	void SkyLayers::SetMode(SkyMode mode) { ModeRef() = mode; }

	const char* SkyLayers::ModeName(SkyMode mode)
	{
		switch (mode)
		{
			case SkyMode::Cube: return "cube";
			case SkyMode::Sphere: return "sphere";
			default: return "volume";
		}
	}

	bool SkyLayers::ParseMode(const cs::String& text, SkyMode& mode)
	{
		cs::String t = text;
		std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (t == "cube" || t == "skybox" || t == "painted") { mode = SkyMode::Cube; return true; }
		if (t == "sphere" || t == "hero") { mode = SkyMode::Sphere; return true; }
		if (t == "volume" || t == "layers" || t == "nebula") { mode = SkyMode::Volume; return true; }
		return false;
	}

	float& SkyLayers::VolumeDensity() { static float v = 14.0f; return v; }
	float& SkyLayers::VolumeBrightness() { static float v = 1.0f; return v; }
	float& SkyLayers::VolumeSteps() { static float v = 40.0f; return v; }

	cs::Ref<SkyboxSpec> SkyLayers::HeroSpec()
	{
		static cs::Ref<SkyboxSpec> spec = []
		{
			auto s = cs::New<SkyboxSpec>(HeroSkyId, "Alien Space Nebula 1 (hero sky sphere)", SkyKind::Image, HeroSkyPath, SkyFrame::Ecliptic, false,
				"\"Alien Space Nebula 1 (Skybox)\" by Jungle Jim (sketchfab.com/jungle_jim)", "CC BY 4.0");
			// Cyclable with Shift+K like any shipped sky; already listed (a second app) is fine.
			SkyboxCatalog::Register(s);
			auto listed = SkyboxCatalog::Find(HeroSkyId);
			return listed ? listed : s;
		}();
		return spec;
	}

	DVec3 SkyLayers::HeroHeart()
	{
		// The painting's brightest blurred point (tools: the 7680 x 3840 original, 6 px Gaussian at 512 x 256): longitude
		// -3.16 degrees, latitude +1.05 degrees in its own (ecliptic) frame.
		double lon = -3.164 * cs::Math::PI / 180.0, lat = 1.055 * cs::Math::PI / 180.0;
		return DVec3(cs::Math::Cos(lat) * cs::Math::Cos(lon), cs::Math::Cos(lat) * cs::Math::Sin(lon), cs::Math::Sin(lat));
	}

	Rendering::SystemGrade SkyLayers::HeroGrade(const cs::String& place)
	{
		auto g = Rendering::SystemSkies::GradeFor(place);
		g.Active = true;
		// The white-hot core lights the system: a cool violet-white key, a fill from the core in its lilac and teal.
		g.KeyTint = Vector4(1.0f, 0.95f, 1.0f, 1);
		Vector3 heart = BodyView::ToUnity(HeroHeart());
		g.FillDirection = Vector4(heart.x, heart.y, heart.z, 0);
		g.FillColor = Vector4(0.78f, 0.74f, 1.0f, 0.55f);
		g.Haze = Vector4(0.010f, 0.007f, 0.016f, 0);
		g.Tint = Vector4(0.98f, 0.96f, 1.0f, 1);
		return g;
	}

	SkyLayers::SkyLayers(const cs::Ref<Transform>& parent)
	{
		root = NewGameObject("SkyLayers");
		root->transform->SetParent(parent, false);
		cube = BuildCube();
		sphere = BuildInwardSphere();

		auto make = [this](const char* name, const cs::Ref<Mesh>& mesh, const cs::Ref<Material>& material, cs::Ref<GameObject>& go, cs::Ref<MeshRenderer>& r)
		{
			go = NewGameObject(name);
			go->transform->SetParent(root->transform, false);
			AddComponent<MeshFilter>(go)->sharedMesh = mesh;
			r = AddComponent<MeshRenderer>(go);
			r->SetSharedMaterial(material);
			r->shadowCastingMode = ShadowCastingMode::Off;
			r->receiveShadows = false;
			r->enabled = false;
		};

		farMaterial = Materials::MarkShared(Materials::Create(Materials::Sky(), "Sky layers: far"));
		farMaterial->renderQueue = 999;
		make("Far sky", cube, farMaterial, farGo, farRenderer);

		volumeMaterial = Materials::MarkShared(Materials::Create(Rendering::RenderBackend::Active()->LoadShader("Nebula"), "Sky layers: volume"));
		make("Painted volume", sphere, volumeMaterial, volumeGo, volumeRenderer);

		warpMaterial = Materials::MarkShared(Materials::Create(Rendering::RenderBackend::Active()->LoadShader("Warp"), "Sky layers: warp"));
		make("Jump tunnel", cube, warpMaterial, warpGo, warpRenderer);

		jumpMaterial = Materials::MarkShared(Materials::Create(Materials::Stars(), "Sky layers: jump stars"));
		jumpMaterial->SetFloat("_Brightness", 2.4f);
		jumpMaterial->SetFloat("_PointSizePx", 3.0f);
		jumpMesh = cs::New<Mesh>("Jump stars");
		jumpMesh->hideFlags = HideFlags::DontSave;
		make("Jump stars", jumpMesh, jumpMaterial, jumpGo, jumpRenderer);
	}

	cs::Ref<Mesh> SkyLayers::BuildCube()
	{
		std::vector<Vector3> v(8);
		for (int i = 0; i < 8; i++) v[i] = Vector3((i & 1) == 0 ? -1.0f : 1.0f, (i & 2) == 0 ? -1.0f : 1.0f, (i & 4) == 0 ? -1.0f : 1.0f);
		std::vector<int> t{ 0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5 };
		auto m = cs::New<Mesh>("Sky layers cube");
		m->hideFlags = HideFlags::DontSave;
		m->SetVertices(v);
		m->SetTriangles(t, 0);
		m->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 1e9f));
		return m;
	}

	cs::Ref<Mesh> SkyLayers::BuildInwardSphere()
	{
		// The planets' unit sphere turned inside out: its faces look in, so from inside every pixel is drawn once and from
		// outside only the far half is (the shader works out the whole ray through the sphere either way).
		auto src = Rendering::MeshFactory::UvSphere(64, 32);
		auto tris = src->triangles();
		for (size_t i = 0; i + 2 < tris.size(); i += 3) std::swap(tris[i + 1], tris[i + 2]);
		auto m = cs::New<Mesh>("Painted volume sphere");
		m->hideFlags = HideFlags::DontSave;
		m->SetVertices(src->Vertices);
		m->SetTriangles(tris, 0);
		m->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 1e9f));
		Materials::DestroyMesh(src);
		return m;
	}

	void SkyLayers::Load(Panorama& p, const cs::String& path, int frame)
	{
		if (p.Key == path && (p.Texture || p.Loading)) return;
		Release(p);
		p.Key = path;
		p.Frame = frame;
		p.Request = Resources::LoadAsync(path, ResourceType::Texture2D);
		p.Loading = p.Request != nullptr;
		if (!p.Request) Debug::LogError("[Unity.SpaceGod] Missing sky Resources/" + path);
	}

	bool SkyLayers::Ready(Panorama& p)
	{
		if (p.Loading && p.Request && p.Request->isDone)
		{
			p.Loading = false;
			if (std::holds_alternative<cs::Ref<Texture2D>>(p.Request->asset)) p.Texture = std::get<cs::Ref<Texture2D>>(p.Request->asset);
			p.FromResources = true;
			p.Request = nullptr;
			if (!p.Texture) Debug::LogError("[Unity.SpaceGod] Could not load the sky Resources/" + p.Key);
		}
		return p.Texture != nullptr;
	}

	void SkyLayers::Release(Panorama& p)
	{
		if (p.Texture)
		{
			if (p.FromResources) Resources::UnloadAsset(p.Texture);
			else Destroy(p.Texture);
		}
		p = Panorama();
	}

	void SkyLayers::ShowFar(Panorama& p, bool instant)
	{
		if (farShown != p.Key)
		{
			farShown = p.Key;
			farMaterial->SetTexture("_MainTex", p.Texture);
			SetAxes(farMaterial, p.Frame);
			farFade = instant ? 1.0f : 0.0f;
		}
		farRenderer->enabled = true;
	}

	void SkyLayers::UpdateLocal(const Frame& f)
	{
		// The volume's painting: the hero painting in the hero system, the system's own painted sky (US-T132) elsewhere.
		if (f.Hero)
		{
			if (Ready(hero) && local.Key != hero.Key)
			{
				// Shared with the far layer's hero: the volume holds it without owning it.
				if (local.Texture && !local.FromResources) Destroy(local.Texture);
				local = Panorama();
				local.Key = hero.Key;
				local.Texture = hero.Texture;
				local.Frame = hero.Frame;
				local.FromResources = true; // never destroyed through local (hero owns it)
			}
			return;
		}
		// The system's painting (US-T132) without its giant worlds, which go into the volume as bodies of their own
		// (BuildGiants). The volume reads a blurred mip, so 1024 wide is plenty: painted in a fraction of a second.
		cs::String key = cs::Cat("painted:", f.System);
		if (local.Key == key) return;
		if (localPendingFor != key)
		{
			localPendingFor = key;
			auto job = std::make_shared<LocalBake>();
			localPending = job;
			auto layout = Rendering::SystemSkyBake::Layout(Biomes::For(f.System));
			layout.Worlds.clear();
			Async(EAsyncExecution::ThreadPool, [job, layout]
			{
				try
				{
					job->Levels = Rendering::SystemSkyBake::Bake(layout, 1024, 2);
					job->Width = 1024;
				}
				catch (const std::exception&) {}
				job->Done = true;
			});
		}
		auto painted = localPending;
		if (!painted || !painted->Done) return;
		if (local.Texture && !local.FromResources) Destroy(local.Texture);
		local = Panorama();
		local.Key = key;
		local.Frame = static_cast<int>(SkyFrame::Ecliptic);
		if (!painted->Levels.empty())
		{
			local.Texture = UnityCompat::TextureFactory::FromMips("Painted volume", painted->Width, painted->Width / 2, painted->Levels,
				TextureWrapMode::Repeat, TextureWrapMode::Clamp, false);
			if (local.Texture) local.Texture->hideFlags = HideFlags::DontSave;
		}
		localPending = nullptr;
		localPendingFor.clear();
	}

	void SkyLayers::ClearGiants()
	{
		for (auto& g : giants)
		{
			if (Alive(g.Mat)) Destroy(g.Mat);
			if (Alive(g.Go)) Destroy(g.Go);
		}
		giants.clear();
		giantsFor.clear();
	}

	void SkyLayers::BuildGiants(const cs::String& system)
	{
		if (giantsFor == system) return;
		ClearGiants();
		giantsFor = system;
		if (!Alive(planetMesh)) planetMesh = Rendering::MeshFactory::UvSphere(64, 32);
		auto layout = Rendering::SystemSkyBake::Layout(Biomes::For(system));
		for (const auto& w : layout.Worlds)
		{
			Giant g;
			g.Direction = BodyView::ToUnity(DVec3(w.Direction.X, w.Direction.Y, w.Direction.Z)).normalized();
			g.AngularRadius = static_cast<float>(w.AngularRadius);
			g.Go = NewGameObject("Backdrop giant");
			g.Go->transform->SetParent(root->transform, false);
			AddComponent<MeshFilter>(g.Go)->sharedMesh = planetMesh;
			auto r = AddComponent<MeshRenderer>(g.Go);
			g.Mat = Materials::Create(Materials::Planet(), "Backdrop giant");
			g.Mat->SetFloat("_UseTexture", 0);
			g.Mat->SetColor("_Color", Color(w.Light.R, w.Light.G, w.Light.B));
			g.Mat->SetFloat("_NoiseStrength", w.Banded ? 0.18f : 0.3f);
			g.Mat->SetFloat("_NoiseScale", w.Banded ? 1.4f : 3.0f);
			g.Mat->SetColor("_AtmoColor", Color(w.Atmosphere.R, w.Atmosphere.G, w.Atmosphere.B));
			g.Mat->SetFloat("_AtmoStrength", 0.7f);
			g.Mat->SetFloat("_Ambient", 0.06f);
			r->SetSharedMaterial(g.Mat);
			r->shadowCastingMode = ShadowCastingMode::Off;
			r->enabled = false;
			g.Renderer = r;
			giants.push_back(g);
		}
	}

	bool SkyLayers::VolumeShown() const { return Alive(volumeRenderer) && volumeRenderer->enabled; }

	void SkyLayers::Update(const Frame& f, float dt)
	{
		SkyMode mode = Mode();
		bool inSystem = !f.System.empty() && !f.Interstellar;
		// What owns the backdrop: the Milky Way between the stars and behind the volume, the hero sphere in sphere mode.
		bool wantMilkyWay = f.Interstellar || (inSystem && mode == SkyMode::Volume && f.Wild);
		bool wantHero = inSystem && (mode == SkyMode::Sphere || (mode == SkyMode::Volume && f.Hero));
		if (wantMilkyWay || f.Warp > 0) Load(milkyWay, MilkyWayPath, static_cast<int>(SkyFrame::Galactic));
		if (wantHero) { HeroSpec(); Load(hero, HeroSkyPath, static_cast<int>(SkyFrame::Ecliptic)); }

		owns = false;
		farRenderer->enabled = false;
		// Under the jump's streaks at full strength a sky goes up at once; otherwise it fades in.
		bool instant = f.Warp > 0.8f;
		if (wantMilkyWay && Ready(milkyWay)) { ShowFar(milkyWay, instant); owns = true; }
		else if (inSystem && mode == SkyMode::Sphere && Ready(hero)) { ShowFar(hero, instant); owns = true; }
		else if (wantMilkyWay && f.Interstellar) owns = true; // black until the Milky Way is up, never the old system's sky
		if (farFade < 1)
		{
			farFade = std::min(1.0f, farFade + dt / FarFadeSeconds);
		}
		float brightness = static_cast<float>(SkyboxSettings::Brightness()->Value) * farFade * farFade;
		farMaterial->SetFloat("_Brightness", brightness);

		// The local layer.
		bool wantVolume = inSystem && mode == SkyMode::Volume && f.Wild && f.RimRadius > 0;
		if (wantVolume) UpdateLocal(f);
		bool volume = wantVolume && local.Texture != nullptr;
		volumeRenderer->enabled = volume;
		if (volume)
		{
			// A little larger than the volume, so a camera right at the rim is still inside the mesh's faces.
			volumeGo->transform->localPosition = f.CenterRelative;
			volumeGo->transform->localScale = Vector3::one() * (f.RimRadius * 1.04f);
			volumeMaterial->SetTexture("_MainTex", local.Texture);
			SetAxes(volumeMaterial, local.Frame);
			volumeMaterial->SetVector("_NebulaPosRadius", Vector4(f.CenterRelative.x, f.CenterRelative.y, f.CenterRelative.z, f.RimRadius));
			// The painting fades in with the far layer and out under the jump's streaks.
			volumeMaterial->SetFloat("_Brightness", VolumeBrightness() * farFade * (1.0f - f.Warp));
			volumeMaterial->SetFloat("_Density", VolumeDensity() * (1.0f - 0.8f * f.Warp));
			volumeMaterial->SetFloat("_Steps", VolumeSteps());
			// The system's 1024 painting is read at its 512 mip, the 8k hero at its 1024: the stars blur away, the gas stays.
			volumeMaterial->SetFloat("_Mip", local.Texture->width >= 8000 ? 3.0f : 1.0f);
		}

		// The painting's giant worlds, out in the volume near its rim: from the centre each covers the disc it was painted
		// with; flying out, they drift by as the distant bodies they are.
		bool showGiants = volume && !f.Hero && f.Warp < 0.6f;
		if (showGiants) BuildGiants(f.System);
		float giantDistance = f.RimRadius * 0.86f;
		for (auto& g : giants)
		{
			g.Renderer->enabled = showGiants;
			if (!showGiants) continue;
			g.Go->transform->localPosition = f.CenterRelative + g.Direction * giantDistance;
			g.Go->transform->localScale = Vector3::one() * (giantDistance * std::tan(g.AngularRadius));
		}

		// The jump's tunnel over everything, and the far layer's own smear.
		warpRenderer->enabled = f.Warp > 0.005f;
		if (warpRenderer->enabled)
		{
			warpMaterial->SetFloat("_Warp", f.Warp);
			warpMaterial->SetVector("_WarpAxis", Vector4(f.WarpAxis.x, f.WarpAxis.y, f.WarpAxis.z, 0));
		}
		float smear = SmearFor(f.Warp);
		if (smear != lastSmear)
		{
			lastSmear = smear;
			farMaterial->SetFloat("_Smear", smear);
			farMaterial->SetVector("_SmearAxis", Vector4(f.WarpAxis.x, f.WarpAxis.y, f.WarpAxis.z, 0));
		}
		else if (smear > 0) farMaterial->SetVector("_SmearAxis", Vector4(f.WarpAxis.x, f.WarpAxis.y, f.WarpAxis.z, 0));

		// Let go of what nothing wants any more (the 8k hero is ~85 MB on the GPU).
		if (!wantHero && hero.Texture && !(mode == SkyMode::Volume && local.Key == hero.Key && wantVolume))
		{
			if (local.Key == hero.Key) local = Panorama();
			if (farShown == hero.Key) { farShown.clear(); farMaterial->SetTexture("_MainTex", nullptr); }
			Release(hero);
		}
		if (!wantVolume && local.Texture && local.Key != hero.Key)
		{
			if (!local.FromResources) Destroy(local.Texture);
			local = Panorama();
		}
	}

	void SkyLayers::SetJumpStars(const std::vector<Vector3>& directions, const std::vector<Color>& colors)
	{
		jumpMesh->SetVertices(directions);
		jumpMesh->SetColors(colors);
		std::vector<int> indices(directions.size());
		for (size_t i = 0; i < indices.size(); i++) indices[i] = static_cast<int>(i);
		jumpMesh->SetIndices(indices, MeshTopology::Points, 0, false);
		jumpMesh->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 1e9f));
	}

	void SkyLayers::ShowJumpStars(bool shown) { jumpRenderer->enabled = shown && jumpMesh->vertexCount() > 0; }

	void SkyLayers::Dispose()
	{
		if (local.Texture && !local.FromResources) Destroy(local.Texture);
		local = Panorama();
		Release(hero);
		Release(milkyWay);
		localPending = nullptr;
		ClearGiants();
		if (Alive(planetMesh)) Destroy(planetMesh);
		for (auto* m : { &farMaterial, &volumeMaterial, &warpMaterial, &jumpMaterial })
			if (Alive(*m)) Destroy(*m);
		for (auto* m : { &cube, &sphere, &jumpMesh })
			if (Alive(*m)) Destroy(*m);
	}
}
