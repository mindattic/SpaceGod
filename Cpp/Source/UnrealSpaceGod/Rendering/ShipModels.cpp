// Port of Unity.SpaceGod Runtime/Rendering/ShipModels.cs (US-T30).
#include "Rendering/ShipModels.h"
#include "Rendering/AssetStreamer.h"
#include "Rendering/Materials.h"
#include "Rendering/ShipMeshFactory.h"

#include "Combat/BulkFreighters.h"
#include "Combat/LaneInterdictors.h"
#include "Economy/LocalTraffic.h"
#include "Ships.h"

#include <algorithm>
#include <cctype>

using namespace SpaceGod::Core;

namespace SpaceGod::Rendering
{
	namespace
	{
		cs::Dictionary<cs::String, cs::Ref<GameObject>>& Prototypes()
		{
			static cs::Dictionary<cs::String, cs::Ref<GameObject>> prototypes;
			return prototypes;
		}

		cs::Dictionary<std::tuple<cs::String, cs::Ref<Faction>>, cs::List<cs::Ref<Material>>>& MaterialCache()
		{
			static cs::Dictionary<std::tuple<cs::String, cs::Ref<Faction>>, cs::List<cs::Ref<Material>>> cache;
			return cache;
		}

		cs::Ref<Material> proceduralMaterial;
		cs::Ref<Transform> prototypeRoot;
	}

	cs::Ref<Material> ShipModels::ProceduralMaterial()
	{
		if (!Alive(proceduralMaterial))
		{
			proceduralMaterial = Materials::MarkShared(Materials::Create(Materials::Ship(), "ShipProcedural"));
			proceduralMaterial->SetFloat("_UseVertexColor", 1);
			Plate(proceduralMaterial, 10.0f);
		}
		return proceduralMaterial;
	}

	void ShipModels::Build(const cs::Ref<Ship>& ship, const cs::Ref<Transform>& parent, cs::List<cs::Ref<Renderer>>& renderers,
		cs::Ref<Transform>& stern, cs::Ref<Transform>& bow)
	{
		BuildHull(ship, parent, renderers, stern, bow);
		// Heavy equipment shows on whatever hull carries it (U.SPC-T507): a field projector's ring amidships. A yard's
		// refit gives the ship a new combat state and its view is rebuilt, so the ring comes and goes with the fitting.
		if (LaneInterdictors::Carries(ship))
		{
			auto ring = Section(stern, renderers, ShipMeshFactory::GetFieldProjector(ship->Faction), "FieldProjector");
			ring->localPosition = Vector3(0, 0, -0.05f);
		}
	}

	void ShipModels::BuildHull(const cs::Ref<Ship>& ship, const cs::Ref<Transform>& parent, cs::List<cs::Ref<Renderer>>& renderers,
		cs::Ref<Transform>& stern, cs::Ref<Transform>& bow)
	{
		bow = nullptr;
		cs::String kenney = KenneyModelFor(ship);
		if (!kenney.empty())
		{
			auto model = InstantiateKenney(kenney, ship->Faction, parent, renderers);
			if (Alive(model))
			{
				stern = model->transform;
				return;
			}
		}
		if (BulkFreighters::IsTruss(ship->Spec->Class))
		{
			// A truss freighter (U.SPC-T482): drawn as it is loaded now (see ShipView, which swaps it as its load changes).
			stern = Section(parent, renderers, TrussMesh(ship), TrussSection);
			return;
		}
		if (ShipMeshFactory::IsWarship(ship->Spec->Class))
		{
			stern = Section(ship, parent, renderers, ShipMeshFactory::Stern, "Stern");
			bow = Section(ship, parent, renderers, ShipMeshFactory::Bow, "Bow");
			return;
		}
		stern = Section(ship, parent, renderers, ShipMeshFactory::WholeHull, "Hull");
	}

	cs::Ref<Mesh> ShipModels::TrussMesh(const cs::Ref<Ship>& ship)
	{
		auto [pods, loaded, spheres] = TrussLoad(ship);
		return ShipMeshFactory::GetTruss(ship->Faction, pods, loaded, spheres);
	}

	std::tuple<int, int, int> ShipModels::TrussLoad(const cs::Ref<Ship>& ship)
	{
		int pods = BulkFreighters::Pods(ship);
		int loaded = ship->CapacityTonnes > 0 ? BulkFreighters::LoadedPods(ship) : pods;
		int spheres = BulkFreighters::Spheres(ship);
		return { pods, loaded, spheres };
	}

	cs::Ref<Transform> ShipModels::Section(const cs::Ref<Ship>& ship, const cs::Ref<Transform>& parent, cs::List<cs::Ref<Renderer>>& renderers, int section,
		const cs::String& name)
	{
		return Section(parent, renderers, ShipMeshFactory::GetSection(ship->Spec->Class, ship->Faction, section), name);
	}

	cs::Ref<Transform> ShipModels::Section(const cs::Ref<Transform>& parent, cs::List<cs::Ref<Renderer>>& renderers, const cs::Ref<Mesh>& mesh, const cs::String& name)
	{
		auto go = NewGameObject(name);
		go->transform->SetParent(parent, false);
		AddComponent<MeshFilter>(go)->sharedMesh = mesh;
		auto mr = AddComponent<MeshRenderer>(go);
		mr->SetSharedMaterial(ProceduralMaterial());
		mr->shadowCastingMode = ShadowCastingMode::Off;
		mr->receiveShadows = false;
		renderers.Add(mr);
		return go->transform;
	}

	cs::Ref<MeshRenderer> ShipModels::Part(const cs::Ref<Transform>& parent, const cs::Ref<Mesh>& mesh, const cs::String& name)
	{
		auto go = NewGameObject(name);
		go->transform->SetParent(parent, false);
		AddComponent<MeshFilter>(go)->sharedMesh = mesh;
		auto mr = AddComponent<MeshRenderer>(go);
		mr->SetSharedMaterial(ProceduralMaterial());
		mr->shadowCastingMode = ShadowCastingMode::Off;
		mr->receiveShadows = false;
		return mr;
	}

	bool ShipModels::Ready(const cs::Ref<Ship>& ship)
	{
		cs::String kenney = KenneyModelFor(ship);
		return kenney.empty() || Prototypes().ContainsKey(kenney) || AssetStreamer::Ready(AssetStreamer::ShipPath(kenney));
	}

	cs::String ShipModels::KenneyModelFor(const cs::Ref<Ship>& ship)
	{
		// Local traffic (U.SPC-T375): a boxy bus, nippy taxis and skiffs, and a roomier six-seat tourer, each drawn
		// at its own design's length.
		switch (ship->Civil)
		{
			case CivilCraft::Bus: return "craft_cargoB";
			case CivilCraft::Taxi: return "craft_speederA";
			case CivilCraft::Skiff: return "craft_racer";
			case CivilCraft::Runabout: return "craft_speederA";
			case CivilCraft::Tourer: return "craft_cargoA";
			default: break;
		}
		switch (ship->Spec->Class)
		{
			case ShipClass::Miner: return "craft_miner";
			case ShipClass::Courier: return ship->Id % 2 == 0 ? "craft_speederA" : "craft_racer";
			case ShipClass::Explorer: return "craft_speederA";
			default: return cs::String();
		}
	}

	cs::Ref<GameObject> ShipModels::InstantiateKenney(const cs::String& modelName, const cs::Ref<Faction>& faction, const cs::Ref<Transform>& parent,
		cs::List<cs::Ref<Renderer>>& renderers)
	{
		cs::Ref<GameObject> prototype;
		if (!Prototypes().TryGetValue(modelName, prototype))
		{
			// Loaded in the background beforehand (see Ready); should a view be built before it arrives, the
			// procedural hull stands in rather than the frame waiting on the disk.
			if (!AssetStreamer::Ready(AssetStreamer::ShipPath(modelName))) return nullptr;
			auto source = AssetStreamer::Loaded<GameObject>(AssetStreamer::ShipPath(modelName));
			prototype = Alive(source) ? Normalise(source, modelName) : nullptr;
			Prototypes()[modelName] = prototype;
		}
		if (!Alive(prototype)) return nullptr;

		auto instance = Instantiate(prototype, parent, false);
		instance->name = modelName;
		instance->SetActive(true);
		auto materials = MaterialsFor(modelName, faction, prototype);
		int index = 0;
		for (auto& r : GetComponentsInChildren<Renderer>(instance, true))
		{
			cs::List<cs::Ref<Material>> mats;
			int n = static_cast<int>(r->sharedMaterials().size());
			for (int i = 0; i < n; i++) mats.Add(materials[Mathf::Min(index++, materials.Count() - 1)]);
			r->SetSharedMaterials(mats);
			r->shadowCastingMode = ShadowCastingMode::Off;
			r->receiveShadows = false;
			renderers.Add(r);
		}
		return instance;
	}

	cs::Ref<GameObject> ShipModels::Normalise(const cs::Ref<GameObject>& source, const cs::String& modelName)
	{
		if (!prototypeRoot)
		{
			auto go = NewGameObject("ShipPrototypes");
			prototypeRoot = go->transform;
			go->SetActive(false);
		}

		auto wrapper = NewGameObject(modelName);
		wrapper->transform->SetParent(prototypeRoot, false);
		auto model = Instantiate(source, wrapper->transform, false);

		auto renderers = GetComponentsInChildren<Renderer>(model, true);
		if (renderers.Count() == 0) return nullptr;
		return wrapper;
	}

	void ShipModels::Plate(const cs::Ref<Material>& m, float tilesPerUnit)
	{
		m->SetFloat("_PlatingScale", tilesPerUnit);
		AssetStreamer::Pin(PlatingPath);
		std::weak_ptr<Material> weak = m;
		AssetStreamer::Request<Texture2D>(PlatingPath, [weak](cs::Ref<Texture2D> plating)
		{
			auto material = weak.lock();
			if (Alive(material) && plating) material->SetTexture("_Plating", plating);
		});
	}

	cs::List<cs::Ref<Material>> ShipModels::MaterialsFor(const cs::String& modelName, const cs::Ref<Faction>& faction_, const cs::Ref<GameObject>& prototype)
	{
		auto faction = ShipMeshFactory::Canon(faction_);
		cs::List<cs::Ref<Material>> cached;
		if (MaterialCache().TryGetValue({ modelName, faction }, cached)) return cached;
		cs::List<cs::Ref<Material>> list;
		Color accent(faction->Color.R, faction->Color.G, faction->Color.B);
		for (auto& r : GetComponentsInChildren<Renderer>(prototype, true))
		{
			for (auto& source : r->sharedMaterials())
			{
				auto m = Materials::MarkShared(Materials::Create(Materials::Ship(), "Ship_" + modelName));
				m->SetFloat("_UseVertexColor", 0);
				Color color = Color::gray();
				if (source)
				{
					if (source->HasProperty("_BaseColor")) color = source->GetColor("_BaseColor");
					else if (source->HasProperty("_Color")) color = source->GetColor("_Color");
					// Kenney's red trim becomes the owner's livery; the rest of the hull takes a wash of it.
					cs::String lower = source->name;
					std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (lower.find("red") != cs::String::npos) color = accent;
					else color = Color::Lerp(color, color * accent * 1.6f, 0.35f);
				}
				m->SetColor("_Color", color);
				// Ship.shader's plating is triplanar-projected in OBJECT SPACE specifically because
				// procedural hulls have no UVs (see its own header comment) -- it was never meant for
				// these Kenney craft, which do have real UVs and their own baked-in surface detail.
				// Confirmed by isolating it: with _PlatingStrength at its 0.6 default, these low-poly,
				// many-small-flat-faces hulls render as a checkerboard-like mess, because triplanar
				// blending snaps between the three axis projections face-to-face on boxy, multi-angled
				// low-poly geometry (each facet picking a different slice of the repeating plating
				// texture) -- a procedural hull's simpler, more uniformly-oriented panels don't expose
				// this. Leaving plating off entirely for Kenney models renders them correctly.
				m->SetFloat("_PlatingStrength", 0.0f);
				list.Add(m);
			}
		}
		if (list.Count() > 0) cached = list;
		else cached = cs::List<cs::Ref<Material>>{ Materials::MarkShared(Materials::Create(Materials::Ship(), "Ship_" + modelName)) };
		MaterialCache()[{ modelName, faction }] = cached;
		return cached;
	}
}
