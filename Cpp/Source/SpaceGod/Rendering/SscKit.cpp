// US-T143: stations drawn from Ebal Studios' Space Station Creator Remastered (see SscKit.h, docs/ThirdPartyArt.md).
#include "Rendering/SscKit.h"
#include "Rendering/Materials.h"
#include "Rendering/ShipMeshFactory.h"
#include "SpaceGodLog.h"

#include "Economy/Industry.h"
#include "Infrastructure/Infrastructure.h"
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
		constexpr const char* MaterialRoot = "/Game/ThirdParty/SSC/Materials";
		constexpr const char* GameMaterial = "/Game/ThirdParty/SSC/Materials/M_SSCStation.M_SSCStation";

		cs::String MeshPath(const cs::String& leaf) { return cs::Cat(SscKit::Root, "/Meshes/", leaf, ".", leaf); }

		/// One imported mesh: the Mesh that draws it and its box in its own Unity frame (Unity units: the pack's metres).
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

		cs::String FamilyOf(const cs::String& model)
		{
			size_t n = model.size();
			while (n > 0 && std::isdigit(static_cast<unsigned char>(model[n - 1]))) n--;
			return model.substr(0, n);
		}

		/// A station's pieces (one per atlas for a Combined station, one for the rest) and their joint box.
		struct Model
		{
			struct Piece
			{
				cs::String Leaf, Atlas;
			};
			cs::List<Piece> Pieces;
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
			cs::List<cs::String> atlases = family == "Combined" ? cs::List<cs::String>{ "Bridge", "Cylinderical", "Hangars", "Storage" }
				: cs::List<cs::String>{ "Hull" };
			bool first = true;
			for (const auto& atlas : atlases)
			{
				cs::String leaf = cs::Cat("SM_SSC_", name, "_", atlas, "_Whole");
				const auto& info = Info(leaf);
				if (!info.Ok) continue;
				m.Pieces.Add({ leaf, atlas == "Hull" ? family : atlas });
				m.Min = first ? info.Min : Vector3::Min(m.Min, info.Min);
				m.Max = first ? info.Max : Vector3::Max(m.Max, info.Max);
				first = false;
			}
			m.Ok = m.Pieces.Count() > 0;
			return models.emplace(name, m).first->second;
		}

		/// A small, steady random stream from a name or a seed (the same layout every time).
		struct Dice
		{
			uint32_t s;
			explicit Dice(const cs::String& name) : s(2166136261u)
			{
				for (unsigned char c : name) s = (s ^ c) * 16777619u;
				if (s == 0) s = 1;
			}
			explicit Dice(int seed) : s(static_cast<uint32_t>(seed) * 2654435761u + 1u) {}
			float Next()
			{
				s ^= s << 13;
				s ^= s >> 17;
				s ^= s << 5;
				return (s & 0xFFFFFF) / static_cast<float>(0x1000000);
			}
			float Range(float a, float b) { return a + (b - a) * Next(); }
			int Pick(int n) { return Mathf::Min(n - 1, static_cast<int>(Next() * n)); }
		};

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

		bool HasInstance(const cs::String& leaf)
		{
			static std::unordered_map<cs::String, bool> known;
			auto it = known.find(leaf);
			if (it != known.end()) return it->second;
			bool ok = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), UTF8_TO_TCHAR(MaterialRoot), UTF8_TO_TCHAR(leaf.c_str()),
				UTF8_TO_TCHAR(leaf.c_str())), nullptr, LOAD_NoWarn | LOAD_Quiet) != nullptr;
			known.emplace(leaf, ok);
			return ok;
		}

		cs::String ColourOf(const cs::Ref<Faction>& faction)
		{
			if (!faction) return "Grey";
			if (faction->IsPirate) return "Brown";
			if (!faction->IsMilitary && !faction->IsHouse) return "White";
			float r = faction->Color.R, g = faction->Color.G, b = faction->Color.B;
			float mx = Mathf::Max(r, Mathf::Max(g, b)), mn = Mathf::Min(r, Mathf::Min(g, b));
			if (mx < 1e-3f || (mx - mn) / mx < 0.25f) return "Silver";
			float h = mx == r ? 60.0f * std::fmod((g - b) / (mx - mn) + 6.0f, 6.0f) : mx == g ? 60.0f * ((b - r) / (mx - mn) + 2.0f) : 60.0f * ((r - g) / (mx - mn) + 4.0f);
			if (h < 30 || h >= 330) return "Red";
			if (h < 165) return "Green";
			if (h < 250) return "Blue";
			return "Purple";
		}

		/// An atlas's material for an owner (or a derelict's: grey, darkened, its lights out). One shared material each.
		cs::Ref<Material> Livery(const cs::String& atlas, const cs::Ref<Faction>& faction_, bool derelict)
		{
			static cs::Dictionary<cs::String, cs::Ref<Material>> cache;
			auto faction = ShipMeshFactory::Canon(faction_);
			cs::String key = cs::Cat(atlas, "|", faction ? faction->Name : cs::String("-"), derelict ? "|D" : "");
			cs::Ref<Material> m;
			if (cache.TryGetValue(key, m) && Alive(m)) return m;
			cs::String colour = derelict ? cs::String("Grey") : ColourOf(faction);
			cs::String leaf = cs::Cat("MI_SSC_", atlas, "_", colour);
			if (!HasInstance(leaf)) leaf = cs::Cat("MI_SSC_", atlas, "_Grey");
			m = Materials::MarkShared(cs::New<Material>(Shader::Find(cs::Cat(MaterialRoot, "/", leaf)),
				cs::Cat("SSC_", atlas, "_", faction ? faction->Prefix : cs::String("Neutral"), derelict ? "_Derelict" : "")));
			Color tint = Color::white();
			if (derelict) tint = Color(0.34f, 0.32f, 0.3f);
			else if (faction)
			{
				Color f(faction->Color.R, faction->Color.G, faction->Color.B);
				float peak = Mathf::Max(f.r, Mathf::Max(f.g, f.b));
				if (peak > 1e-3f) f = f * (1.0f / peak);
				tint = Color::Lerp(Color::white(), f, 0.15f);
			}
			m->SetColor("Tint", tint);
			m->SetColor("_Tint", Color::white());
			if (derelict)
			{
				m->SetFloat("EmissiveIntensity", 0.0f);
				m->SetFloat("_EngineGlow", 0.0f);
			}
			cache[key] = m;
			return m;
		}

		/// A model laid into the parent: centred at `pos`, its longest side `size`, turned by `turn`. A derelict's piece
		/// `dropPiece` is moved off by `dropOffset` (in sizes) and turned by `dropTurn`, as if torn away.
		void Lay(const cs::Ref<Transform>& parent, const cs::String& model, Vector3 pos, float size, Quaternion turn, const cs::Ref<Faction>& faction,
			bool derelict, cs::List<cs::Ref<Renderer>>& into, int dropPiece = -1, Vector3 dropOffset = Vector3::zero(), Quaternion dropTurn = Quaternion::identity())
		{
			const auto& m = ModelInfo(model);
			if (!m.Ok) return;
			Vector3 extent = m.Max - m.Min;
			float k = size / Mathf::Max(Mathf::Max(extent.x, extent.y), Mathf::Max(extent.z, 1e-3f));
			Vector3 centre = (m.Min + m.Max) * 0.5f;
			auto holder = NewChild(parent, cs::Cat("Station:", model));
			holder->localPosition = pos;
			holder->localRotation = turn;
			for (int i = 0; i < m.Pieces.Count(); i++)
			{
				const auto& p = m.Pieces[i];
				auto at = NewChild(holder, cs::Cat("Module:", p.Atlas));
				at->localScale = Vector3::one() * k;
				at->localPosition = -centre * k;
				if (i == dropPiece)
				{
					at->localPosition = at->localPosition + dropOffset * size;
					at->localRotation = dropTurn;
				}
				into.Add(Draw(at, Info(p.Leaf).Drawn, Livery(p.Atlas, faction, derelict)));
			}
		}

		/// A Hangars module (a girder, a hangar) fitted into a box of `size` (across, up, along) at `pos`, its longest side
		/// along the box's z, turned by `turn`.
		void Module(const cs::Ref<Transform>& parent, const cs::String& name, Vector3 pos, Vector3 size, Quaternion turn, const cs::Ref<Faction>& faction,
			cs::List<cs::Ref<Renderer>>& into)
		{
			const auto& info = Info(cs::Cat("SM_SSC_Mod_", name));
			if (!info.Ok) return;
			Vector3 s = info.Max - info.Min;
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
			auto holder = NewChild(parent, cs::Cat("Module:", name));
			holder->localPosition = pos;
			holder->localRotation = turn;
			holder->localScale = Vector3(size.x / Mathf::Max(s.x, 1e-4f), size.y / Mathf::Max(s.y, 1e-4f), size.z / Mathf::Max(s.z, 1e-4f));
			auto inner = NewChild(holder, name);
			inner->localRotation = lay;
			inner->localPosition = -(lay * ((info.Min + info.Max) * 0.5f));
			into.Add(Draw(inner, info.Drawn, Livery("Hangars", faction, false)));
		}
	}

	bool SscKit::Available()
	{
		static int state = -1;
		if (state < 0)
		{
			bool wanted = FApp::CanEverRender() && !FParse::Param(FCommandLine::Get(), TEXT("NoSSC"));
			bool mesh = wanted && ModelInfo("Combined20").Ok;
			bool material = wanted && LoadObject<UMaterialInterface>(nullptr, UTF8_TO_TCHAR(GameMaterial), nullptr, LOAD_NoWarn | LOAD_Quiet) != nullptr;
			state = mesh && material ? 1 : 0;
			if (!wanted)
			{
				UE_LOG(LogSpaceGod, Display, TEXT("SscKit: off (%s)"), FApp::CanEverRender() ? TEXT("-NoSSC") : TEXT("no renderer"));
			}
			else if (state == 0)
			{
				UE_LOG(LogSpaceGod, Display, TEXT("SscKit: the pack is not imported; stations are procedural (tools/import-ssc.ps1)"));
			}
			else
			{
				UE_LOG(LogSpaceGod, Display, TEXT("SscKit: stations are built from Space Station Creator"));
			}
		}
		return state == 1;
	}

	bool SscKit::Station(const cs::Ref<Transform>& parent, const cs::String& model, float size, const cs::Ref<Faction>& faction,
		cs::List<cs::Ref<Renderer>>& into, Condition condition, bool axisZ, int seed)
	{
		if (!Available() || !ModelInfo(model).Ok) return false;
		Quaternion turn = axisZ ? Quaternion::Euler(90, 0, 0) : Quaternion::identity();
		if (condition == Condition::Working)
		{
			Lay(parent, model, Vector3::zero(), size, turn, faction, false, into);
			return true;
		}
		// Derelict: listing a little, one module (the hangars where it has them) torn off and tumbled out to one side.
		Dice d(seed);
		const auto& m = ModelInfo(model);
		int drop = m.Pieces.Count() > 1 ? 2 % m.Pieces.Count() : -1;
		Vector3 off(d.Range(0.35f, 0.55f) * (d.Next() < 0.5f ? -1.0f : 1.0f), d.Range(-0.15f, 0.15f), d.Range(-0.3f, 0.3f));
		Lay(parent, model, Vector3::zero(), size, turn * Quaternion::Euler(d.Range(-12, 12), d.Range(0, 360), d.Range(-12, 12)), faction, true, into,
			drop, off, Quaternion::Euler(d.Range(20, 70), d.Range(0, 360), d.Range(20, 70)));
		return true;
	}

	cs::String SscKit::ForStructure(StructureKind kind, IndustryKind industry, const cs::String&)
	{
		static const char* const factories[] = { "Storage01", "Storage04", "Storage11", "Combined21" };
		switch (kind)
		{
			case StructureKind::Factory: return factories[static_cast<int>(industry) % 4];
			case StructureKind::Depot: return "Storage18";
			case StructureKind::Habitat: return "Cylinderical12";
			case StructureKind::Gateway: return "Cylinderical18";
			default: return cs::String();
		}
	}

	cs::String SscKit::ForStationBody(const cs::String& name)
	{
		static const char* const stations[] = { "Combined20", "Combined28", "Combined30", "Combined08", "Combined01" };
		Dice d(name);
		return stations[d.Pick(5)];
	}

	bool SscKit::VoidStation(const cs::Ref<Transform>& parent, const cs::String&, const cs::Ref<Faction>& faction, cs::List<cs::Ref<Renderer>>& into)
	{
		if (!Available() || !ModelInfo("Cylinderical02").Ok) return false;
		auto root = NewChild(parent, "SSC Void Station");
		// The sunburst frame across the equator, its hub round the core, its spokes to just inside the outer radius.
		Lay(root, "Cylinderical02", Vector3::zero(), 2.1f, Quaternion::identity(), faction, false, into);
		// Docks at four of the spokes' ends, and a frame over each pole.
		for (int i = 0; i < 4; i++)
		{
			float a = (45.0f + 90.0f * i) * Mathf::Deg2Rad;
			Vector3 at(Mathf::Cos(a) * 0.82f, 0, Mathf::Sin(a) * 0.82f);
			Lay(root, i % 2 == 0 ? "Hangars05" : "Storage19", at, 0.3f, Quaternion::LookRotation(at.normalized(), Vector3::up()), faction, false, into);
		}
		for (float y : { 0.62f, -0.62f })
			Lay(root, "Storage13", Vector3(0, y, 0), 0.5f, Quaternion::identity(), faction, false, into);
		return true;
	}

	bool SscKit::Megastructure(const cs::Ref<Transform>& parent, const cs::String& name, cs::List<cs::Ref<Renderer>>& into)
	{
		if (!Available() || !ModelInfo("Storage05").Ok) return false;
		Dice d(name);
		auto root = NewChild(parent, "SSC Megastructure");
		static const char* const blocks[] = { "Storage05", "Storage12", "Storage19", "Storage13", "Bridge04" };
		// The city: a dense cluster of tall towers at the heart, each a stack of big blocks (stood upright, so they read as
		// tower storeys), the middle one tallest; the towers crowd each other as the reference's do.
		int towers = 6 + d.Pick(3);
		for (int t = 0; t < towers; t++)
		{
			float a = (360.0f * t / towers + d.Range(-20, 20)) * Mathf::Deg2Rad, r = t == 0 ? 0.0f : d.Range(0.12f, 0.3f);
			Vector3 base(Mathf::Cos(a) * r, 0, Mathf::Sin(a) * r);
			int stories = (t == 0 ? 7 : 4) + d.Pick(3);
			float block = d.Range(0.24f, 0.32f), pitch = block * 0.55f, y = -pitch * (stories - 1) * 0.5f;
			for (int s = 0; s < stories; s++)
			{
				Lay(root, blocks[d.Pick(5)], base + Vector3(0, y, 0), block, Quaternion::Euler(90.0f, d.Range(0, 360), 0), nullptr, false, into);
				y += pitch;
			}
		}
		// Long docking spars out to the edge, a dock at the tip and hangars along them.
		int spars = 4 + d.Pick(3);
		for (int s = 0; s < spars; s++)
		{
			float a = (360.0f * s / spars + d.Range(-15, 15)) * Mathf::Deg2Rad;
			Vector3 dir = Vector3(Mathf::Cos(a), d.Range(-0.12f, 0.12f), Mathf::Sin(a)).normalized();
			float from = 0.3f, to = d.Range(0.95f, 1.12f), mid = (from + to) * 0.5f;
			auto turn = Quaternion::LookRotation(dir, Vector3::up());
			Module(root, "HangarsBeamTall", dir * mid, Vector3(0.035f, 0.035f, to - from), turn, nullptr, into);
			Module(root, "HangarsBeam", dir * mid + Vector3(0, 0.05f, 0), Vector3(0.02f, 0.02f, (to - from) * 0.9f), turn, nullptr, into);
			Lay(root, "Hangars05", dir * to, 0.14f, turn, nullptr, false, into);
			for (float f : { 0.45f, 0.7f })
				Module(root, "HangarsHangar", dir * (from + (to - from) * f) + Vector3(0, -0.045f, 0), Vector3(0.07f, 0.04f, 0.06f), turn, nullptr, into);
		}
		return true;
	}

	bool SscKit::Derelict(const cs::Ref<Transform>& parent, int seed, float size, cs::List<cs::Ref<Renderer>>& into)
	{
		static const char* const wrecks[] = { "Combined20", "Combined28", "Combined30", "Combined08", "Combined21", "Combined01" };
		Dice d(seed);
		return Station(parent, wrecks[d.Pick(6)], size, nullptr, into, Condition::Derelict, false, seed);
	}

	cs::List<cs::String> SscKit::ModelNames()
	{
		return cs::List<cs::String>{ "Combined20", "Combined28", "Combined30", "Combined08", "Combined21", "Combined01", "Storage01", "Storage04", "Storage05",
			"Storage11", "Storage12", "Storage13", "Storage18", "Storage19", "Cylinderical12", "Cylinderical18", "Cylinderical02", "Hangars05", "Bridge04" };
	}

	cs::List<cs::String> SscKit::AssetPaths()
	{
		cs::List<cs::String> paths;
		for (const auto& model : ModelNames())
			for (const auto& p : ModelInfo(model).Pieces) paths.Add(MeshPath(p.Leaf));
		for (const char* mod : { "HangarsBeam", "HangarsBeamTall", "HangarsHangar" }) paths.Add(MeshPath(cs::Cat("SM_SSC_Mod_", mod)));
		paths.Add(GameMaterial);
		return paths;
	}
}
