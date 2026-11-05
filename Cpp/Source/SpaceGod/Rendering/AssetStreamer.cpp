// Port of Unity.SpaceGod Runtime/Rendering/AssetStreamer.cs (US-T30).
#include "Rendering/AssetStreamer.h"
#include "Rendering/ShipModels.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Fleet.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
#include "World.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Rendering
{
	cs::Dictionary<cs::String, cs::Ref<AssetStreamer::Entry>>& AssetStreamer::Entries()
	{
		static cs::Dictionary<cs::String, cs::Ref<Entry>> entries;
		return entries;
	}

	cs::HashSet<cs::String>& AssetStreamer::Pinned()
	{
		static cs::HashSet<cs::String> pinned;
		return pinned;
	}

	namespace
	{
		struct Color32Key
		{
			uint8_t r, g, b, a;
			bool operator==(const Color32Key& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
			size_t HashCode() const { return (static_cast<size_t>(r) << 24) | (static_cast<size_t>(g) << 16) | (static_cast<size_t>(b) << 8) | a; }
		};

		cs::Dictionary<Color32Key, cs::Ref<Texture2D>>& Swatches()
		{
			static cs::Dictionary<Color32Key, cs::Ref<Texture2D>> swatches;
			return swatches;
		}
	}

	void AssetStreamer::Pin(const cs::String& path)
	{
		if (Pinned().Add(path)) Warm(path);
	}

	UnityCompat::ResourceType AssetStreamer::TypeAt(const cs::String& path)
	{
		return cs::Strings::StartsWith(path, "Ships/") ? UnityCompat::ResourceType::GameObject : UnityCompat::ResourceType::Texture2D;
	}

	cs::Ref<AssetStreamer::Entry> AssetStreamer::Get(const cs::String& path, bool force)
	{
		cs::Ref<Entry> e;
		if (!Entries().TryGetValue(path, e))
		{
			e = cs::New<Entry>();
			Entries()[path] = e;
		}
		if (e->Started) return e;
		// US-T133: held back until the streaming warms it (the view draws its stand-in meanwhile).
		if (!force && Admit && !Pinned().Contains(path) && !Admit(path)) return e;
		Start(path, e);
		return e;
	}

	void AssetStreamer::Start(const cs::String& path, const cs::Ref<Entry>& e)
	{
		e->Started = true;
		auto request = Resources::LoadAsync(path, TypeAt(path));
		if (!request)
		{
			// No such asset. A view that waited on a path held back hears there is none, as it would have on asking.
			InFlight++; // Complete counts it off again
			Complete(path, e);
			return;
		}
		e->Request = request;
		InFlight++;
		request->completed += [path, e](cs::Ref<ResourceRequest>) { Complete(path, e); };
	}

	bool AssetStreamer::Started(const cs::String& path)
	{
		cs::Ref<Entry> e;
		return Entries().TryGetValue(path, e) && e->Started;
	}

	void AssetStreamer::Complete(const cs::String&, const cs::Ref<Entry>& e)
	{
		if (e->Done) return;
		InFlight = cs::Math::Max(0, InFlight - 1);
		e->Done = true;
		e->Asset = e->Request ? e->Request->asset : UnityCompat::ResourceAsset();
		e->Request = nullptr;
		auto waiting = e->Waiting;
		bool any = e->HasWaiting;
		e->Waiting.Clear();
		e->HasWaiting = false;
		if (!any) return;
		for (auto& w : waiting)
		{
			try
			{
				w(e->Asset);
			}
			catch (const std::exception& ex)
			{
				Debug::LogError(ex.what());
			}
		}
	}

	void AssetStreamer::WarmSystem(const cs::Ref<StarSystem>& system)
	{
		if (!system) return;
		// Held-back paths (US-T133: a relief map, loaded only near its body) stay held back.
		for (const auto& path : SystemPaths(system))
			if (!Admit || Admit(path)) Warm(path);
	}

	cs::List<cs::String> AssetStreamer::SystemPaths(const cs::Ref<StarSystem>& system)
	{
		cs::List<cs::String> paths;
		const auto& catalog = system->Catalog;
		for (int i = 0; i < catalog->Count(); i++)
		{
			if (!(*catalog)[i]->Texture.empty()) paths.Add(TexturePath((*catalog)[i]->Texture));
			if (!(*catalog)[i]->NormalMap.empty()) paths.Add(TexturePath((*catalog)[i]->NormalMap));
		}
		cs::HashSet<cs::String> seen;
		for (const auto& ship : system->World->Fleet->Ships())
		{
			if (ship->Destroyed) continue;
			auto model = ShipModels::KenneyModelFor(ship);
			if (!model.empty() && seen.Add(model)) paths.Add(ShipPath(model));
		}
		return paths;
	}

	void AssetStreamer::ReleaseExcept(const cs::HashSet<cs::String>& keep)
	{
		cs::List<cs::String> drop;
		for (const auto& kv : Entries())
		{
			if (!kv.second->Done || keep.Contains(kv.first) || Pinned().Contains(kv.first)) continue;
			if (!std::holds_alternative<cs::Ref<Texture2D>>(kv.second->Asset)) continue;
			drop.Add(kv.first);
		}
		if (drop.Count() == 0) return;
		for (const auto& path : drop)
		{
			auto asset = Entries()[path]->Asset;
			Entries().Remove(path);
			Resources::UnloadAsset(asset);
		}
	}

	cs::Ref<Texture2D> AssetStreamer::Swatch(Color color)
	{
		auto c = UnityCompat::Color32::From(color);
		Color32Key key{ c.r, c.g, c.b, c.a };
		cs::Ref<Texture2D> t;
		if (Swatches().TryGetValue(key, t) && t) return t;
		t = cs::New<Texture2D>(1, 1, TextureFormat::RGBA32, false);
		t->name = "Swatch";
		t->hideFlags = HideFlags::DontSave;
		t->SetPixel(0, 0, color);
		t->Apply(false, true);
		Swatches()[key] = t;
		return t;
	}
}
