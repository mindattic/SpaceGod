// US-T142: every ship drawn from Ebal Studios' Ultimate Spaceships Creator (see UscKit.h, docs/ThirdPartyArt.md).
#include "Rendering/UscKit.h"
#include "Rendering/Materials.h"
#include "Rendering/ShipMeshFactory.h"
#include "Rendering/StarSparrowKit.h"
#include "SpaceGodLog.h"

#include "Combat/CombatSpecs.h"
#include "Economy/LocalTraffic.h"
#include "Ships.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include <cctype>
#include <cmath>
#include <unordered_map>

using namespace SpaceGod::Core;

namespace SpaceGod::Rendering
{
	namespace
	{
		constexpr const char* MaterialRoot = "/Game/ThirdParty/USC/Materials";
		constexpr const char* GameMaterial = "/Game/ThirdParty/USC/Materials/M_USCShip.M_USCShip";

		/// The pack's frame, as an imported piece reaches the game (tools/usc_prepare.py: nose -Y, up +Z in Blender, which
		/// the FBX export and Unreal's import bring to Unity's x, up y and nose -z), turned to the model frame (nose +Z).
		Quaternion PackToModel() { return Quaternion::Euler(0, 180, 0); }

		cs::String MeshPath(const cs::String& leaf) { return cs::Cat(UscKit::Root, "/Meshes/", leaf, ".", leaf); }

		/// One imported mesh: the shared Mesh that draws it and its box in its own Unity frame (Unity units).
		struct PieceInfo
		{
			cs::Ref<Mesh> Drawn;
			Vector3 Min, Max;
			bool Ok = false;
		};

		const PieceInfo& Info(const cs::String& leaf)
		{
			static std::unordered_map<cs::String, PieceInfo> infos;
			auto it = infos.find(leaf);
			if (it != infos.end()) return it->second;
			PieceInfo info;
			cs::String path = MeshPath(leaf);
			if (UStaticMesh* asset = LoadObject<UStaticMesh>(nullptr, UTF8_TO_TCHAR(path.c_str()), nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				FBox box = asset->GetBoundingBox();
				// Unreal (x, y, z) cm is Unity (x, z, -y) / 100 (UnrealSpace).
				info.Min = Vector3(static_cast<float>(box.Min.X), static_cast<float>(box.Min.Z), static_cast<float>(-box.Max.Y)) / 100.0f;
				info.Max = Vector3(static_cast<float>(box.Max.X), static_cast<float>(box.Max.Z), static_cast<float>(-box.Min.Y)) / 100.0f;
				auto mesh = cs::New<Mesh>(leaf);
				mesh->ImportedAsset = path;
				mesh->ImportedScale = 0.01f;
				mesh->SetBounds(Bounds((info.Min + info.Max) * 0.5f, info.Max - info.Min));
				info.Drawn = mesh;
				info.Ok = true;
			}
			return infos.emplace(leaf, info).first->second;
		}

		// ------------------------------------------------------------ the models

		/// A class's ship for the navies and civilians, and for the pirates (empty: the same).
		struct Pick
		{
			const char* Model;
			const char* Pirate;
		};

		Pick PickOf(ShipClass cls, CivilCraft civil, int id)
		{
			switch (civil)
			{
				case CivilCraft::Taxi: return { "ForceBadger17", "" };
				case CivilCraft::Skiff: return { "HyperFalcon20", "" };
				case CivilCraft::Runabout: return { "ForceBadger04", "" };
				case CivilCraft::Tourer: return { "ForceBadger14", "" };
				case CivilCraft::Bus: return { "ForceBadger11", "" };
				default: break;
			}
			switch (cls)
			{
				// Capitals: Galactic Leopard for the navies, Void Whale for the pirates.
				case ShipClass::Battleship: return { "GalacticLeopard31", "VoidWhale30" };
				case ShipClass::Carrier: return { "GalacticLeopard44", "VoidWhale10" };
				case ShipClass::GateBreaker: return { "GalacticLeopard39", "VoidWhale27" };
				case ShipClass::Cruiser: return { "GalacticLeopard41", "VoidWhale8" };
				case ShipClass::CommandCruiser: return { "GalacticLeopard27", "VoidWhale25" };
				case ShipClass::Destroyer: return { "GalacticLeopard43", "VoidWhale16" };
				case ShipClass::Frigate: return { "GalacticLeopard40", "VoidWhale20" };
				case ShipClass::Corvette: return { "GalacticLeopard32", "VoidWhale14" };
				// Small craft.
				case ShipClass::Gunship: return { "StriderOx14", "StriderOx10" };
				case ShipClass::AssaultShuttle: return { "StriderOx18", "StriderOx01" };
				case ShipClass::Bomber: return { "NightAye20", "NightAye09" };
				case ShipClass::Interceptor: return { "HyperFalcon17", "HyperFalcon08" };
				case ShipClass::Fighter: return { "HyperFalcon07", "HyperFalcon10" };
				case ShipClass::Recon: return { "HyperFalcon19", "NightAye12" };
				case ShipClass::Courier: return { id % 2 == 0 ? "HyperFalcon14" : "HyperFalcon24", "" };
				// Work and auxiliary hulls.
				case ShipClass::Miner: return { "VoidWhale40", "" };
				case ShipClass::OreTug: return { "VoidWhale24", "" };
				case ShipClass::CaptureTug: return { "VoidWhale31", "" };
				case ShipClass::LaneTender: return { "GalacticLeopard16", "" };
				case ShipClass::GatePicket: return { "GalacticLeopard7", "" };
				case ShipClass::Explorer: return { "GalacticLeopard13", "" };
				default: return { "GalacticLeopard11", "" }; // the truss freighter and every other hauler
			}
		}

		cs::String FamilyOf(const cs::String& model)
		{
			size_t n = model.size();
			while (n > 0 && std::isdigit(static_cast<unsigned char>(model[n - 1]))) n--;
			return model.substr(0, n);
		}

		bool TwoAtlases(const cs::String& family) { return family == "GalacticLeopard" || family == "VoidWhale"; }

		/// A model's pieces: atlas (Main / Parts / Hull) and section (Whole / Bow / Stern), the meshes that exist.
		struct ModelPiece
		{
			cs::String Leaf, Atlas;
			int Section = ShipMeshFactory::WholeHull;
		};

		struct Model
		{
			cs::List<ModelPiece> Pieces;
			Vector3 Min, Max;
			bool Ok = false;
		};

		const Model& ModelInfo(const cs::String& name)
		{
			static std::unordered_map<cs::String, Model> models;
			auto it = models.find(name);
			if (it != models.end()) return it->second;
			Model m;
			cs::String family = FamilyOf(name);
			cs::List<cs::String> atlases = TwoAtlases(family) ? cs::List<cs::String>{ "Main", "Parts" } : cs::List<cs::String>{ "Hull" };
			bool first = true;
			for (const auto& atlas : atlases)
				for (auto [section, tag] : { std::pair{ ShipMeshFactory::WholeHull, "Whole" }, std::pair{ ShipMeshFactory::Bow, "Bow" },
						 std::pair{ ShipMeshFactory::Stern, "Stern" } })
				{
					cs::String leaf = cs::Cat("SM_USC_", name, "_", atlas, "_", tag);
					const auto& info = Info(leaf);
					if (!info.Ok) continue;
					m.Pieces.Add({ leaf, atlas, section });
					m.Min = first ? info.Min : Vector3::Min(m.Min, info.Min);
					m.Max = first ? info.Max : Vector3::Max(m.Max, info.Max);
					first = false;
				}
			m.Ok = m.Pieces.Count() > 0;
			return models.emplace(name, m).first->second;
		}

		/// The model frame's transform of a model: turned nose +Z, one unit long, centred.
		struct Fit
		{
			Quaternion Turn;
			float Scale = 1;
			Vector3 Offset;
		};

		Fit FitOf(const Model& m)
		{
			Fit f;
			f.Turn = PackToModel();
			Vector3 size = m.Max - m.Min;
			// Its length is along the pack's nose axis (Unity z here).
			f.Scale = 1.0f / Mathf::Max(size.z, 1e-3f);
			f.Offset = -(f.Turn * ((m.Min + m.Max) * 0.5f * f.Scale));
			return f;
		}

		cs::Ref<Transform> NewChild(const cs::Ref<Transform>& parent, const cs::String& name)
		{
			auto go = NewGameObject(name);
			go->transform->SetParent(parent, false);
			return go->transform;
		}

		cs::Ref<MeshRenderer> Draw(const cs::Ref<Transform>& at, const cs::Ref<Mesh>& mesh, const cs::Ref<Material>& material)
		{
			AddComponent<MeshFilter>(at->gameObject())->sharedMesh = mesh;
			auto mr = AddComponent<MeshRenderer>(at->gameObject());
			mr->SetSharedMaterial(material);
			mr->shadowCastingMode = ShadowCastingMode::Off;
			mr->receiveShadows = false;
			return mr;
		}

		// ------------------------------------------------------------ liveries

		bool HasInstance(const cs::String& atlas, const cs::String& colour)
		{
			static std::unordered_map<cs::String, bool> known;
			cs::String leaf = cs::Cat("MI_USC_", atlas, "_", colour);
			auto it = known.find(leaf);
			if (it != known.end()) return it->second;
			bool ok = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), UTF8_TO_TCHAR(MaterialRoot), UTF8_TO_TCHAR(leaf.c_str()),
				UTF8_TO_TCHAR(leaf.c_str())), nullptr, LOAD_NoWarn | LOAD_Quiet) != nullptr;
			known.emplace(leaf, ok);
			return ok;
		}

		/// An atlas's material in a faction's livery: one shared material per atlas and faction.
		cs::Ref<Material> Livery(const cs::String& atlas, const cs::Ref<Faction>& faction_)
		{
			static cs::Dictionary<std::tuple<cs::String, cs::Ref<Faction>>, cs::Ref<Material>> cache;
			auto faction = ShipMeshFactory::Canon(faction_);
			cs::Ref<Material> m;
			if (cache.TryGetValue({ atlas, faction }, m) && Alive(m)) return m;
			cs::String colour = UscKit::ColourOf(faction);
			if (!HasInstance(atlas, colour)) colour = colour == "Black" && HasInstance(atlas, "Red") ? cs::String("Red") : cs::String("Grey");
			cs::String leaf = cs::Cat("MI_USC_", atlas, "_", colour);
			m = Materials::MarkShared(cs::New<Material>(Shader::Find(cs::Cat(MaterialRoot, "/", leaf)), cs::Cat("USC_", atlas, "_", faction ? faction->Prefix : cs::String("Neutral"))));
			// A light wash of the faction's own colour over the unified one, so two navies in the same colour still differ.
			Color tint = Color::white();
			if (faction)
			{
				Color f(faction->Color.R, faction->Color.G, faction->Color.B);
				float peak = Mathf::Max(f.r, Mathf::Max(f.g, f.b));
				if (peak > 1e-3f) f = f * (1.0f / peak);
				tint = Color::Lerp(Color::white(), f, faction->IsPirate ? 0.25f : 0.18f);
				if (faction->IsPirate) tint = tint * 0.85f;
			}
			m->SetColor("Tint", tint);
			m->SetColor("_Tint", Color::white());
			cache[{ atlas, faction }] = m;
			return m;
		}

		cs::String AtlasKey(const cs::String& family, const cs::String& atlas) { return atlas == "Hull" ? family : cs::Cat(family, "_", atlas); }

		// ------------------------------------------------------------ modules

		/// A module (SM_USC_Mod_<name>) fitted into a box of <paramref name="size"/> (across, up, along) centred at
		/// <paramref name="pos"/>, its longest side along +Z, turned by <paramref name="euler"/> after.
		cs::Ref<MeshRenderer> Module(const cs::Ref<Transform>& parent, const cs::String& name, Vector3 pos, Vector3 size, const cs::Ref<Material>& material,
			Vector3 euler = Vector3::zero(), const cs::String& label = cs::String())
		{
			const auto& info = Info(cs::Cat("SM_USC_Mod_", name));
			if (!info.Ok) return nullptr;
			Quaternion turn = PackToModel();
			Vector3 s = turn * (info.Max - info.Min);
			s = Vector3(Mathf::Abs(s.x), Mathf::Abs(s.y), Mathf::Abs(s.z));
			// Lay its longest side along the keel.
			Quaternion lay = Quaternion::identity();
			if (s.y >= s.x && s.y >= s.z)
			{
				lay = Quaternion::Euler(90, 0, 0);
				s = Vector3(s.x, s.z, s.y);
			}
			else if (s.x > s.z)
			{
				lay = Quaternion::Euler(0, 90, 0);
				s = Vector3(s.z, s.y, s.x);
			}
			Vector3 scale(size.x / Mathf::Max(s.x, 1e-4f), size.y / Mathf::Max(s.y, 1e-4f), size.z / Mathf::Max(s.z, 1e-4f));
			auto holder = NewChild(parent, cs::Cat("Module:", label.empty() ? name : label));
			holder->localPosition = pos;
			holder->localRotation = Quaternion::Euler(euler);
			auto inner = NewChild(holder, name);
			inner->localRotation = lay * turn;
			// Scaled in the laid frame, so the scale goes on the holder's axes: a child between holder and mesh.
			holder->localScale = scale;
			inner->localPosition = -(inner->localRotation * ((info.Min + info.Max) * 0.5f));
			return Draw(inner, info.Drawn, material);
		}

		bool Pirate(const cs::Ref<Faction>& f) { return f && f->IsPirate; }

		/// Thruster bells on every Engine hardpoint of the layout (their glows sit in them), sized to the class.
		void Engines(const cs::Ref<Transform>& stern, const cs::List<cs::Ref<SubsystemDef>>* layout, const cs::Ref<Faction>& faction, float r,
			cs::List<cs::Ref<Renderer>>& renderers)
		{
			if (!layout) return;
			cs::String family = Pirate(faction) ? "VoidWhale" : "GalacticLeopard";
			auto material = Livery(cs::Cat(family, "_Parts"), faction);
			int n = 0;
			for (const auto& d : *layout)
				if (d && d->Kind == SubsystemKind::Engine)
				{
					float bell = r * 2.4f;
					auto mr = Module(stern, cs::Cat(family, "Thruster"), Vector3(static_cast<float>(d->X), static_cast<float>(d->Y), static_cast<float>(d->Z) + bell * 0.2f),
						Vector3(r * 2.2f, r * 2.2f, bell), material, Vector3::zero(), cs::Cat("Engine", n++));
					if (mr) renderers.Add(mr);
				}
		}

		/// A truss freighter's containers, two abreast (four round a long spine) from the bow back, named as ShipView looks
		/// for them.
		void Cargo(const cs::Ref<Transform>& hull, int pods, const cs::Ref<Faction>& faction, cs::List<cs::Ref<Renderer>>& renderers)
		{
			pods = Mathf::Clamp(pods, 0, ShipMeshFactory::MaxTrussPods);
			auto material = Livery("GalacticLeopard_Parts", faction);
			int abreast = pods > 8 ? 4 : 2;
			int rows = Mathf::Max(1, (pods + abreast - 1) / abreast);
			constexpr float front = 0.28f, back = -0.2f, reach = 0.085f, size = 0.07f;
			float pitch = rows > 1 ? (front - back) / (rows - 1) : 0;
			float length = Mathf::Min(0.1f, rows > 1 ? pitch * 0.9f : 0.1f);
			for (int i = 0; i < pods; i++)
			{
				int row = i / abreast, place = i % abreast;
				float z = rows > 1 ? front - pitch * row : (front + back) * 0.5f;
				Vector2 dir = place == 0 ? Vector2(1, 0) : place == 1 ? Vector2(-1, 0) : place == 2 ? Vector2(0, 1) : Vector2(0, -1);
				auto mr = Module(hull, "GalacticLeopardCargo", Vector3(dir.x * reach, dir.y * reach, z), Vector3(size, size, length), material,
					Vector3::zero(), cs::Cat(StarSparrowKit::CargoName, i));
				if (mr)
				{
					// ShipView finds the containers by their GameObject's name.
					mr->gameObject()->name = cs::Cat(StarSparrowKit::CargoName, i);
					renderers.Add(mr);
				}
			}
		}

		/// The engine bell's radius by class, in ship lengths.
		float BellOf(ShipClass cls)
		{
			switch (cls)
			{
				case ShipClass::Battleship: case ShipClass::GateBreaker: case ShipClass::Carrier: return 0.018f;
				case ShipClass::Cruiser: case ShipClass::CommandCruiser: return 0.017f;
				case ShipClass::Destroyer: case ShipClass::Frigate: return 0.019f;
				case ShipClass::Corvette: return 0.022f;
				default: return 0.0f; // small craft and work hulls: their own nozzles are enough
			}
		}

		/// Builds a model's hull pieces (in the faction's livery) under the stern and bow sections.
		void Hull(const cs::String& model, const cs::Ref<Faction>& faction, bool split, const cs::Ref<Transform>& stern, const cs::Ref<Transform>& bow,
			cs::List<cs::Ref<Renderer>>& renderers)
		{
			const auto& m = ModelInfo(model);
			Fit fit = FitOf(m);
			cs::String family = FamilyOf(model);
			for (const auto& p : m.Pieces)
			{
				// A whole hull and a cut one are never both imported for one model; a split hull drawn whole keeps both halves.
				auto section = split && p.Section == ShipMeshFactory::Bow ? bow : stern;
				auto holder = NewChild(section, cs::Cat("Hull:", p.Atlas, ":", p.Section == ShipMeshFactory::Bow ? "Bow" : p.Section == ShipMeshFactory::Stern ? "Stern" : "Whole"));
				holder->localRotation = fit.Turn;
				holder->localScale = Vector3::one() * fit.Scale;
				holder->localPosition = fit.Offset;
				renderers.Add(Draw(holder, Info(p.Leaf).Drawn, Livery(AtlasKey(family, p.Atlas), faction)));
			}
		}
	}

	bool UscKit::Available()
	{
		static int state = -1;
		if (state < 0)
		{
			bool wanted = FApp::CanEverRender() && !FParse::Param(FCommandLine::Get(), TEXT("NoUSC"));
			bool hull = wanted && ModelInfo("GalacticLeopard31").Ok;
			bool material = wanted && LoadObject<UMaterialInterface>(nullptr, UTF8_TO_TCHAR(GameMaterial), nullptr, LOAD_NoWarn | LOAD_Quiet) != nullptr;
			state = hull && material ? 1 : 0;
			if (!wanted)
			{
				UE_LOG(LogSpaceGod, Display, TEXT("UscKit: off (%s)"), FApp::CanEverRender() ? TEXT("-NoUSC") : TEXT("no renderer"));
			}
			else if (state == 0)
			{
				UE_LOG(LogSpaceGod, Display, TEXT("UscKit: the pack is not imported (%s%s); tools/import-usc.ps1"), hull ? TEXT("") : TEXT("no hull meshes "),
					material ? TEXT("") : TEXT("no M_USCShip"));
			}
			else
			{
				UE_LOG(LogSpaceGod, Display, TEXT("UscKit: ships are built from Ultimate Spaceships Creator"));
			}
		}
		return state == 1;
	}

	cs::String UscKit::ModelOf(ShipClass cls, CivilCraft civil, int id, const cs::Ref<Faction>& faction)
	{
		Pick pick = PickOf(cls, civil, id);
		return cs::String(Pirate(faction) && pick.Pirate[0] ? pick.Pirate : pick.Model);
	}

	cs::String UscKit::ColourOf(const cs::Ref<Faction>& faction_)
	{
		auto faction = ShipMeshFactory::Canon(faction_);
		if (!faction) return "Grey";
		if (faction->IsPirate) return "Black";
		if (!faction->IsMilitary && !faction->IsHouse) return "White";
		// A navy: the unified colour nearest its own hue (grey when it has little colour).
		float r = faction->Color.R, g = faction->Color.G, b = faction->Color.B;
		float mx = Mathf::Max(r, Mathf::Max(g, b)), mn = Mathf::Min(r, Mathf::Min(g, b));
		if (mx < 1e-3f || (mx - mn) / mx < 0.25f) return mx > 0.75f ? "White" : "Grey";
		float h;
		if (mx == r) h = 60.0f * std::fmod((g - b) / (mx - mn), 6.0f);
		else if (mx == g) h = 60.0f * ((b - r) / (mx - mn) + 2.0f);
		else h = 60.0f * ((r - g) / (mx - mn) + 4.0f);
		if (h < 0) h += 360.0f;
		struct Hue { const char* Name; float Deg; };
		static const Hue hues[] = { { "Red", 0 }, { "Red", 360 }, { "Green", 120 }, { "Blue", 215 }, { "Purple", 285 } };
		const char* best = "Grey";
		float bestD = 1e9f;
		for (const auto& c : hues)
			if (Mathf::Abs(h - c.Deg) < bestD)
			{
				bestD = Mathf::Abs(h - c.Deg);
				best = c.Name;
			}
		return best;
	}

	bool UscKit::Build(ShipClass cls, CivilCraft civil, int id, const cs::Ref<Faction>& faction, const cs::List<cs::Ref<SubsystemDef>>* layout,
		const cs::Ref<Transform>& parent, cs::List<cs::Ref<Renderer>>& renderers, cs::Ref<Transform>& stern, cs::Ref<Transform>& bow, int pods, int)
	{
		bow = nullptr;
		if (!Available()) return false;
		cs::String model = ModelOf(cls, civil, id, faction);
		if (!ModelInfo(model).Ok)
		{
			UE_LOG(LogSpaceGod, Warning, TEXT("UscKit: %s is not imported; falling back"), UTF8_TO_TCHAR(model.c_str()));
			return false;
		}
		bool split = civil == CivilCraft::None && ShipMeshFactory::IsWarship(cls);
		auto stern_ = NewChild(parent, split ? "Stern" : "Hull");
		cs::Ref<Transform> bow_ = split ? NewChild(parent, "Bow") : nullptr;
		Hull(model, faction, split, stern_, split ? bow_ : stern_, renderers);
		if (civil == CivilCraft::None)
		{
			if (float r = BellOf(cls); r > 0) Engines(stern_, layout, faction, r, renderers);
			// The truss freighter's containers, as many as its load (ShipView shows them by name).
			if (model == "GalacticLeopard11") Cargo(stern_, pods > 0 ? pods : 8, faction, renderers);
		}
		stern = stern_;
		bow = bow_;
		return true;
	}

	bool UscKit::Turret(const cs::Ref<Transform>& parent, SubsystemKind kind, const cs::Ref<Faction>& faction, bool barrels, cs::List<cs::Ref<Renderer>>& into)
	{
		if (!Available()) return false;
		// Sized as ShipMeshFactory::GetPart's turrets, so the barrels' height (ShipView) still sits on the base.
		float w;
		int guns;
		switch (kind)
		{
			case SubsystemKind::HeavyLaser: w = 0.05f; guns = 3; break;
			case SubsystemKind::PlasmaCannon: w = 0.05f; guns = 2; break;
			case SubsystemKind::LaserTurret: w = 0.034f; guns = 2; break;
			case SubsystemKind::FlakCannon: w = 0.04f; guns = 2; break;
			case SubsystemKind::PointDefense: w = 0.022f; guns = 1; break;
			default: return false;
		}
		bool pirate = Pirate(faction);
		auto material = Livery(pirate ? "VoidWhale_Parts" : "GalacticLeopard_Parts", faction);
		auto root = NewChild(parent, barrels ? "KitBarrels" : "KitBase");
		if (!barrels)
		{
			auto mr = Module(root, pirate ? "VoidWhaleTurretBase" : "GalacticLeopardTurret", Vector3(0, w * 0.2f, 0), Vector3(w, w * 0.5f, w * 1.15f), material);
			if (!mr) return false;
			into.Add(mr);
			return true;
		}
		bool laser = kind == SubsystemKind::HeavyLaser || kind == SubsystemKind::LaserTurret || kind == SubsystemKind::PointDefense;
		cs::String barrel = pirate ? (laser ? "VoidWhaleLazer" : "VoidWhaleCannon") : "GalacticLeopardGun";
		float length = w * 1.3f, spacing = w * 0.24f;
		for (int g = 0; g < guns; g++)
		{
			auto mr = Module(root, barrel, Vector3((g - (guns - 1) * 0.5f) * spacing, 0, length * 0.5f), Vector3(w * 0.14f, w * 0.18f, length), material);
			if (!mr) return false;
			into.Add(mr);
		}
		return true;
	}

	bool UscKit::Extent(ShipClass cls, CivilCraft civil, int id, const cs::Ref<Faction>& faction, Vector3& min, Vector3& max)
	{
		if (!Available()) return false;
		const auto& m = ModelInfo(ModelOf(cls, civil, id, faction));
		if (!m.Ok) return false;
		Fit fit = FitOf(m);
		Vector3 a = fit.Turn * (m.Min * fit.Scale) + fit.Offset, b = fit.Turn * (m.Max * fit.Scale) + fit.Offset;
		min = Vector3::Min(a, b);
		max = Vector3::Max(a, b);
		return true;
	}

	cs::List<cs::String> UscKit::ModelNames()
	{
		return cs::List<cs::String>{
			"GalacticLeopard31", "GalacticLeopard44", "GalacticLeopard39", "GalacticLeopard41", "GalacticLeopard27", "GalacticLeopard43",
			"GalacticLeopard40", "GalacticLeopard32", "VoidWhale30", "VoidWhale10", "VoidWhale27", "VoidWhale8", "VoidWhale25", "VoidWhale16",
			"VoidWhale20", "VoidWhale14", "GalacticLeopard11", "GalacticLeopard16", "GalacticLeopard7", "GalacticLeopard13", "VoidWhale40",
			"VoidWhale24", "VoidWhale31", "StriderOx14", "StriderOx10", "StriderOx18", "StriderOx01", "NightAye20", "NightAye09", "NightAye12",
			"HyperFalcon17", "HyperFalcon08", "HyperFalcon07", "HyperFalcon10", "HyperFalcon19", "HyperFalcon14", "HyperFalcon24", "HyperFalcon20",
			"ForceBadger17", "ForceBadger04", "ForceBadger14", "ForceBadger11" };
	}

	cs::List<cs::String> UscKit::AssetPaths()
	{
		cs::List<cs::String> paths;
		for (const auto& model : ModelNames())
			for (const auto& p : ModelInfo(model).Pieces) paths.Add(MeshPath(p.Leaf));
		for (const char* mod : { "GalacticLeopardTurret", "GalacticLeopardGun", "GalacticLeopardThruster", "GalacticLeopardCargo", "VoidWhaleTurretBase",
				 "VoidWhaleCannon", "VoidWhaleLazer", "VoidWhaleThruster" })
			paths.Add(MeshPath(cs::Cat("SM_USC_Mod_", mod)));
		paths.Add(GameMaterial);
		return paths;
	}

	bool UscKit::Piece(const cs::Ref<Transform>& parent, const cs::String& model, float length, const cs::Ref<Faction>& faction, cs::List<cs::Ref<Renderer>>& into)
	{
		if (!Available() || !ModelInfo(model).Ok) return false;
		auto holder = NewChild(parent, model);
		holder->localScale = Vector3::one() * length;
		Hull(model, faction, false, holder, holder, into);
		return true;
	}
}
