// Port of Unity.SpaceGod Runtime/Views/SkyView.cs (US-T30).
#include "Views/SkyView.h"
#include "Views/BodyView.h"
#include "Rendering/Materials.h"

#include "Rendering/ProceduralSky.h"
#include "Rendering/Skyboxes.h"
#include "Settings/GameSetting.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::Materials;

	cs::String SkyView::DropInFolder() { return (std::filesystem::path(Application::persistentDataPath()) / "Skyboxes").string(); }

	SkyView::SkyView(const cs::Ref<Transform>& parent, const cs::Ref<GameObject>& starfield_)
	{
		Memory = cs::New<SkyboxMemory>();
		starfield = starfield_;
		root = NewGameObject("Skybox");
		root->transform->SetParent(parent, false);
		mesh = BuildCube();
		AddComponent<MeshFilter>(root)->sharedMesh = mesh;
		renderer = AddComponent<MeshRenderer>(root);
		material = Materials::MarkShared(Materials::Create(Materials::Sky(), "Skybox"));
		material->renderQueue = 999; // just before the starfield (Background, 1000)
		renderer->SetSharedMaterial(material);
		renderer->shadowCastingMode = ShadowCastingMode::Off;
		renderer->receiveShadows = false;
		renderer->enabled = false;
		onBrightness = SkyboxSettings::Brightness()->Changed += [this](cs::Ref<GameSetting>) { ApplyBrightness(); };
		onResolution = SkyboxSettings::NebulaResolution()->Changed += [this](cs::Ref<GameSetting>)
		{
			if (wanted && wanted->Kind == SkyKind::Procedural) Apply(wanted);
		};
	}

	int SkyView::DiscoverDropIns()
	{
		int found = 0;
		try
		{
			namespace fs = std::filesystem;
			fs::path folder(DropInFolder());
			if (!fs::is_directory(folder)) return 0;
			std::vector<fs::path> files;
			for (const auto& e : fs::directory_iterator(folder))
				if (e.is_regular_file()) files.push_back(e.path());
			// Array.Sort with OrdinalIgnoreCase.
			auto lower = [](cs::String s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return s; };
			std::stable_sort(files.begin(), files.end(), [&](const fs::path& a, const fs::path& b) { return lower(a.string()) < lower(b.string()); });
			for (const auto& file : files)
			{
				cs::String ext = lower(file.extension().string());
				if (ext != ".jpg" && ext != ".jpeg" && ext != ".png") continue;
				cs::String stem = file.stem().string();
				if (SkyboxCatalog::Register(cs::New<SkyboxSpec>("file:" + file.filename().string(), "Your sky: " + stem, SkyKind::ImageFile, file.string(),
						SkyFrame::Ecliptic, false, "Dropped into " + DropInFolder(), "Yours to check")))
					found++;
			}
		}
		catch (const std::filesystem::filesystem_error& e)
		{
			Debug::LogWarning(cs::Cat("[Unity.SpaceGod] Could not read the skybox folder ", DropInFolder(), ": ", e.what()));
		}
		return found;
	}

	void SkyView::Show(const cs::String& system)
	{
		systemName = system;
		Apply(Memory->For(system));
	}

	cs::Ref<SkyboxSpec> SkyView::Cycle(int step)
	{
		auto next = Memory->Cycle(systemName, step);
		Apply(next);
		return next;
	}

	void SkyView::Apply(const cs::Ref<SkyboxSpec>& spec)
	{
		if (!spec) return;
		wanted = spec;
		wantedKey = KeyFor(spec, systemName);
		// Whatever was loading for the last choice is dropped, even when this one is already on screen.
		int id = ++request;
		CancelPending();
		if (wantedKey == shownKey && (texture || spec->Kind == SkyKind::Starfield)) return;
		// US-T133: streamed in ahead of arrival: on screen at once if it is ready, else it goes up the moment it is (Update).
		if (ahead && ahead->Key == wantedKey && !ahead->Failed)
		{
			if (ahead->Ready) TakeAhead(true);
			return;
		}
		switch (spec->Kind)
		{
			case SkyKind::Starfield:
				Release();
				renderer->enabled = false;
				if (Alive(starfield)) starfield->SetActive(true);
				shownKey = wantedKey;
				break;
			case SkyKind::Image:
			{
				auto load = Resources::LoadAsync(spec->Path, ResourceType::Texture2D);
				pendingResource = load;
				if (!load)
				{
					Debug::LogError("[Unity.SpaceGod] Missing skybox Resources/" + spec->Path);
					break;
				}
				load->completed += [this, id, spec, load](cs::Ref<ResourceRequest>) { OnResourceLoaded(id, spec, load); };
				break;
			}
			case SkyKind::ImageFile:
			{
				cs::String path = spec->Path;
				pendingFile = Async(EAsyncExecution::ThreadPool, [path] { return UnityCompat::TextureFactory::Decode(path); });
				pendingFileActive = true;
				break;
			}
			case SkyKind::Procedural:
			{
				int width = SkyboxSettings::NebulaFaceSize() * 4;
				uint64_t seed = ProceduralSky::SeedFor(systemName);
				auto mips = std::make_shared<cs::List<int>>();
				pendingBakeWidth = width;
				pendingBakeMips = mips;
				bakeCancel = std::make_shared<std::atomic<bool>>(false);
				auto token = bakeCancel;
				pendingBake = Async(EAsyncExecution::ThreadPool, [seed, width, mips, token]() -> cs::List<uint8_t>
				{
					// A bake overtaken by another choice stops at its next row and is dropped (an empty result).
					try
					{
						return ProceduralSky::BakePanorama(seed, width, *mips, token.get());
					}
					catch (const std::exception& e)
					{
						Debug::LogError(cs::Cat("[Unity.SpaceGod] The seeded sky could not be made: ", e.what()));
						return cs::List<uint8_t>();
					}
				});
				pendingBakeActive = true;
				break;
			}
		}
	}

	void SkyView::OnResourceLoaded(int id, const cs::Ref<SkyboxSpec>& spec, const cs::Ref<ResourceRequest>& req)
	{
		auto tex = std::holds_alternative<cs::Ref<Texture2D>>(req->asset) ? std::get<cs::Ref<Texture2D>>(req->asset) : nullptr;
		if (id != request)
		{
			// Overtaken by another choice: let go of it unless it is the one on screen.
			if (tex && tex != texture) Resources::UnloadAsset(tex);
			return;
		}
		pendingResource = nullptr;
		if (!tex)
		{
			Debug::LogError("[Unity.SpaceGod] Missing skybox Resources/" + spec->Path);
			return;
		}
		Swap(tex, true, spec);
	}

	cs::String SkyView::KeyFor(const cs::Ref<SkyboxSpec>& spec, const cs::String& system)
	{
		return spec->Kind == SkyKind::Procedural ? cs::Cat(spec->Id, "@", system, "@", SkyboxSettings::NebulaFaceSize()) : spec->Id;
	}

	void SkyView::Prefetch(const cs::String& system)
	{
		if (system.empty() || !Memory) return;
		auto spec = Memory->For(system);
		if (!spec || spec->Kind == SkyKind::Starfield) return; // nothing to load
		cs::String key = KeyFor(spec, system);
		// On screen, or already loading for the screen.
		if (key == shownKey || key == wantedKey) return;
		if (ahead && ahead->Key == key) return;
		DropAhead();
		auto a = std::make_shared<Ahead>();
		a->Key = key;
		a->System = system;
		a->Spec = spec;
		ahead = a;
		switch (spec->Kind)
		{
			case SkyKind::Image:
			{
				auto load = Resources::LoadAsync(spec->Path, ResourceType::Texture2D);
				if (!load) { a->Failed = true; break; }
				a->Resource = load;
				std::weak_ptr<Ahead> weak = a;
				load->completed += [this, weak, load](cs::Ref<ResourceRequest>)
				{
					auto tex = std::holds_alternative<cs::Ref<Texture2D>>(load->asset) ? std::get<cs::Ref<Texture2D>>(load->asset) : nullptr;
					auto held = weak.lock();
					if (!held || held != ahead)
					{
						// Dropped meanwhile: let go of it unless it is the one on screen.
						if (tex && tex != texture) Resources::UnloadAsset(tex);
						return;
					}
					held->Resource = nullptr;
					if (!tex) { held->Failed = true; return; }
					held->Texture = tex;
					held->FromResources = true;
					held->Ready = true;
				};
				break;
			}
			case SkyKind::ImageFile:
			{
				cs::String path = spec->Path;
				a->File = Async(EAsyncExecution::ThreadPool, [path] { return UnityCompat::TextureFactory::Decode(path); });
				a->FileActive = true;
				break;
			}
			case SkyKind::Procedural:
			{
				int width = SkyboxSettings::NebulaFaceSize() * 4;
				uint64_t seed = ProceduralSky::SeedFor(system);
				auto mips = std::make_shared<cs::List<int>>();
				a->BakeWidth = width;
				a->Mips = mips;
				a->Cancel = std::make_shared<std::atomic<bool>>(false);
				auto token = a->Cancel;
				a->Bake = Async(EAsyncExecution::ThreadPool, [seed, width, mips, token]() -> cs::List<uint8_t>
				{
					try
					{
						return ProceduralSky::BakePanorama(seed, width, *mips, token.get());
					}
					catch (const std::exception& e)
					{
						Debug::LogError(cs::Cat("[Unity.SpaceGod] The seeded sky could not be made ahead: ", e.what()));
						return cs::List<uint8_t>();
					}
				});
				a->BakeActive = true;
				break;
			}
			default:
				break;
		}
	}

	bool SkyView::PrefetchReady(const cs::String& system) const
	{
		if (system.empty() || !Memory) return true;
		auto spec = Memory->For(system);
		if (!spec || spec->Kind == SkyKind::Starfield) return true;
		cs::String key = KeyFor(spec, system);
		if (key == shownKey && texture) return true;
		return ahead && ahead->Key == key && ahead->Ready;
	}

	void SkyView::DropPrefetch(const cs::String& system)
	{
		if (ahead && ahead->System == system) DropAhead();
	}

	void SkyView::UpdateAhead()
	{
		if (!ahead) return;
		auto a = ahead;
		if (a->FileActive && a->File.IsReady())
		{
			a->FileActive = false;
			auto decoded = a->File.Get();
			if (decoded.Ok())
			{
				a->Texture = UnityCompat::TextureFactory::FromMips(a->Spec ? a->Spec->Name : cs::String("Sky"), decoded.Width, decoded.Height, decoded.Levels,
					TextureWrapMode::Repeat, TextureWrapMode::Clamp, false);
				a->Ready = a->Texture != nullptr;
			}
			a->Failed = !a->Ready;
		}
		if (a->BakeActive && a->Bake.IsReady())
		{
			a->BakeActive = false;
			const auto& data = a->Bake.Get();
			if (data.Count() > 0) a->Texture = Upload(data, a->BakeWidth, *a->Mips);
			a->Ready = a->Texture != nullptr;
			a->Failed = !a->Ready;
		}
		bool forScreen = a->Key == wantedKey && shownKey != wantedKey;
		if (a->Failed)
		{
			// Could not be made ahead: the screen's own load takes over, if the screen wants it.
			ahead = nullptr;
			if (forScreen && wanted) Apply(wanted);
			return;
		}
		// Asked for on screen before it was ready: up now, faded in like any late sky.
		if (a->Ready && forScreen) TakeAhead(false);
	}

	void SkyView::TakeAhead(bool instant)
	{
		auto a = ahead;
		ahead = nullptr;
		if (!a || !a->Texture) return;
		Swap(a->Texture, a->FromResources, a->Spec);
		if (instant)
		{
			fade = 1;
			ApplyBrightness();
		}
	}

	void SkyView::DropAhead()
	{
		if (!ahead) return;
		if (ahead->Cancel) *ahead->Cancel = true; // a bake under way stops at its next row
		if (ahead->Texture && ahead->Texture != texture) Free(ahead->Texture, ahead->FromResources);
		ahead = nullptr;
	}

	void SkyView::Update(float dt)
	{
		UpdateAhead();
		if (pendingFileActive && pendingFile.IsReady())
		{
			pendingFileActive = false;
			auto decoded = pendingFile.Get();
			if (decoded.Ok())
			{
				// Unity: wrap U repeat, V clamp, anisotropy 4.
				auto tex = UnityCompat::TextureFactory::FromMips(wanted ? wanted->Name : cs::String("Sky"), decoded.Width, decoded.Height, decoded.Levels,
					TextureWrapMode::Repeat, TextureWrapMode::Clamp, false);
				Swap(tex, false, wanted);
			}
			else Debug::LogWarning(cs::Cat("[Unity.SpaceGod] Could not load the skybox ", wanted ? wanted->Path : cs::String(), ": not a readable image"));
		}
		if (pendingBakeActive && pendingBake.IsReady())
		{
			pendingBakeActive = false;
			const auto& data = pendingBake.Get();
			if (data.Count() > 0) Swap(Upload(data, pendingBakeWidth, *pendingBakeMips), false, wanted);
		}
		if (fade < 1)
		{
			fade = Mathf::Min(1.0f, fade + dt / FadeSeconds);
			ApplyBrightness();
		}
	}

	cs::Ref<Texture2D> SkyView::Upload(const cs::List<uint8_t>& data, int width, const cs::List<int>& mips)
	{
		// The bake is RGBA rows bottom-up (Unity's SetPixelData); Unreal wants BGRA top-down.
		std::vector<std::vector<uint8_t>> levels;
		int w = width, h = width / 2;
		for (int i = 0; i < mips.Count(); i++)
		{
			std::vector<uint8_t> level(static_cast<size_t>(w) * h * 4);
			size_t at = static_cast<size_t>(mips[i]);
			for (int y = 0; y < h; y++)
				for (int x = 0; x < w; x++)
				{
					size_t src = at + (static_cast<size_t>(y) * w + x) * 4;
					size_t dst = (static_cast<size_t>(h - 1 - y) * w + x) * 4;
					if (src + 3 >= static_cast<size_t>(data.Count())) continue;
					level[dst] = data[static_cast<int>(src + 2)];
					level[dst + 1] = data[static_cast<int>(src + 1)];
					level[dst + 2] = data[static_cast<int>(src)];
					level[dst + 3] = data[static_cast<int>(src + 3)];
				}
			levels.push_back(std::move(level));
			w = std::max(1, w / 2);
			h = std::max(1, h / 2);
		}
		auto tex = UnityCompat::TextureFactory::FromMips("Seeded sky", width, width / 2, levels, TextureWrapMode::Repeat, TextureWrapMode::Clamp, false);
		if (tex) tex->hideFlags = HideFlags::DontSave;
		return tex;
	}

	void SkyView::Swap(const cs::Ref<Texture2D>& tex, bool fromResources, const cs::Ref<SkyboxSpec>& spec)
	{
		if (!spec || !tex) return;
		auto old = texture;
		bool oldFromResources = textureFromResources;
		texture = tex;
		textureFromResources = fromResources;
		material->SetTexture(MainTexId, tex);
		DVec3 x, y, z;
		SkyboxCatalog::FrameAxes(spec->Frame, x, y, z);
		material->SetVector(AxisXId, BodyView::ToUnity(x));
		material->SetVector(AxisYId, BodyView::ToUnity(y));
		material->SetVector(AxisZId, BodyView::ToUnity(z));
		renderer->enabled = true;
		if (Alive(starfield)) starfield->SetActive(spec->PointStars);
		shownKey = wantedKey;
		fade = 0;
		ApplyBrightness();
		if (old && old != tex) Free(old, oldFromResources);
	}

	void SkyView::ApplyBrightness() { material->SetFloat(BrightnessId, static_cast<float>(SkyboxSettings::Brightness()->Value) * fade * fade); }

	void SkyView::CancelPending()
	{
		// A Resources load cannot be stopped: its completion sees the request id moved on and lets the texture go.
		pendingResource = nullptr;
		// A file decode cannot be aborted midway: its result is dropped.
		pendingFileActive = false;
		pendingBakeActive = false; // a bake under way stops at its next row and is dropped
		if (bakeCancel)
		{
			// Cancelled, not disposed: the worker may still be reading the token.
			*bakeCancel = true;
			bakeCancel = nullptr;
		}
	}

	void SkyView::Release()
	{
		if (!texture) return;
		material->SetTexture(MainTexId, nullptr);
		Free(texture, textureFromResources);
		texture = nullptr;
	}

	void SkyView::Free(const cs::Ref<Texture2D>& tex, bool fromResources)
	{
		if (fromResources) Resources::UnloadAsset(tex);
		else Destroy(tex);
	}

	void SkyView::Dispose()
	{
		++request;
		SkyboxSettings::Brightness()->Changed -= onBrightness;
		SkyboxSettings::NebulaResolution()->Changed -= onResolution;
		CancelPending();
		DropAhead();
		Release();
		if (Alive(material)) Destroy(material);
		if (Alive(mesh)) Destroy(mesh);
	}

	cs::Ref<Mesh> SkyView::BuildCube()
	{
		std::vector<Vector3> v(8);
		for (int i = 0; i < 8; i++) v[i] = Vector3((i & 1) == 0 ? -1.0f : 1.0f, (i & 2) == 0 ? -1.0f : 1.0f, (i & 4) == 0 ? -1.0f : 1.0f);
		std::vector<int> t{
			0, 2, 1, 1, 2, 3, // -z
			4, 5, 6, 5, 7, 6, // +z
			0, 1, 4, 1, 5, 4, // -y
			2, 6, 3, 3, 6, 7, // +y
			0, 4, 2, 2, 4, 6, // -x
			1, 3, 5, 3, 7, 5, // +x
		};
		auto m = cs::New<Mesh>("Skybox");
		m->hideFlags = HideFlags::DontSave;
		m->SetVertices(v);
		m->SetTriangles(t, 0);
		m->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 1e9f));
		return m;
	}
}
