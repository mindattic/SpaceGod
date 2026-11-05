// US-T110: every ship drawn from Ebal Studios' Star Sparrow kit (see StarSparrowKit.h, docs/ThirdPartyArt.md).
#include "Rendering/StarSparrowKit.h"
#include "Rendering/Materials.h"
#include "Rendering/ShipMeshFactory.h"
#include "SpaceGodLog.h"

#include "Combat/CombatSpecs.h"
#include "Economy/LocalTraffic.h"
#include "Ships.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include <functional>

using namespace SpaceGod::Core;

namespace SpaceGod::Rendering
{
	namespace
	{
		constexpr const char* MaterialPath = "/Game/ThirdParty/StarSparrow/Materials/M_StarSparrowShip";

		// ------------------------------------------------------------ the pieces

		enum class P { Core, Engine, Thruster, Plasma, Weapon, Wing, Fin, Tail, Fighter };

		const char* ModuleName(P p)
		{
			switch (p)
			{
				case P::Core: return "Core";
				case P::Engine: return "Engine";
				case P::Thruster: return "Thruster";
				case P::Plasma: return "Plasma";
				case P::Weapon: return "Weapon";
				case P::Wing: return "Wing";
				case P::Fin: return "Fin";
				case P::Tail: return "Tail";
				default: return "";
			}
		}

		/// One imported mesh as the game sees it: the shared Mesh that draws it, and its box in the mesh's own Unity frame
		/// (x along its length, the pack's nose +X; y up; z across), in Unity units of its centimetres.
		struct PieceInfo
		{
			cs::Ref<Mesh> Drawn;
			Vector3 Min, Max;
			/// Wing, Fin and Tail are modelled standing on their root edge: placed by that edge, not their middle.
			bool RootEdge = false;
			bool Ok = false;
			Vector3 Size() const { return Max - Min; }
			Vector3 Anchor() const
			{
				Vector3 c = (Min + Max) * 0.5f;
				return RootEdge ? Vector3(c.x, Min.y, c.z) : c;
			}
		};

		cs::String PathOf(P p, int model)
		{
			if (p == P::Fighter)
			{
				char leaf[32];
				FCStringAnsi::Sprintf(leaf, "SM_StarSparrow%02d", model);
				return cs::Cat(StarSparrowKit::Root, "/Meshes/", leaf, ".", leaf);
			}
			cs::String leaf = cs::Cat("SM_StarSparrowModule_", ModuleName(p));
			return cs::Cat(StarSparrowKit::Root, "/Meshes/", leaf, ".", leaf);
		}

		const PieceInfo& Info(P p, int model = 0)
		{
			static std::unordered_map<cs::String, PieceInfo> infos;
			cs::String path = PathOf(p, model);
			auto it = infos.find(path);
			if (it != infos.end()) return it->second;
			PieceInfo info;
			// Referenced by path, never by a stored object: re-running the import recreates the module meshes.
			if (UStaticMesh* asset = LoadObject<UStaticMesh>(nullptr, UTF8_TO_TCHAR(path.c_str()), nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				FBox box = asset->GetBoundingBox();
				// Unreal (x, y, z) cm is Unity (x, z, -y) / 100 (UnrealSpace).
				info.Min = Vector3(static_cast<float>(box.Min.X), static_cast<float>(box.Min.Z), static_cast<float>(-box.Max.Y)) / 100.0f;
				info.Max = Vector3(static_cast<float>(box.Max.X), static_cast<float>(box.Max.Z), static_cast<float>(-box.Min.Y)) / 100.0f;
				info.RootEdge = p == P::Wing || p == P::Fin || p == P::Tail;
				auto mesh = cs::New<Mesh>(p == P::Fighter ? cs::Cat("StarSparrow", model) : cs::String(ModuleName(p)));
				mesh->ImportedAsset = path;
				// The scene draws an imported part in Unity units: its centimetres are a hundredth of one.
				mesh->ImportedScale = 0.01f;
				mesh->SetBounds(Bounds((info.Min + info.Max) * 0.5f, info.Max - info.Min));
				info.Drawn = mesh;
				info.Ok = true;
			}
			return infos.emplace(path, info).first->second;
		}

		// ------------------------------------------------------------ recipes

		/// One part of a recipe, in the model frame (one unit long, nose +Z, up +Y).
		struct Part
		{
			P Piece = P::Core;
			int Model = 0;
			/// Where its anchor goes: the middle of a module, the middle of a Wing/Fin/Tail's root edge.
			Vector3 Pos;
			/// Its extent across (x), up (y) and along (z) before it is turned; 0 across or up keeps the length's scale.
			Vector3 Size;
			/// Turned by Pre (in its own upright, nose-forward frame) and then by Euler.
			Vector3 Euler, Pre;
			/// Turned to face aft (its nose to the stern).
			bool Aft = false;
			/// Named, for a part its view looks for (a freighter's containers).
			cs::String Name;
			/// Which section a warship's part goes in when its middle would put it wrong (-1: by its middle).
			int Section = -1;
		};

		/// A class's recipe as it is assembled: helpers for the shapes every recipe uses.
		class Recipe
		{
		public:
			cs::List<Part> Parts;

			Part& Add(P piece, Vector3 pos, Vector3 size, Vector3 euler = Vector3::zero(), bool aft = false)
			{
				Part p;
				p.Piece = piece;
				p.Pos = pos;
				p.Size = size;
				p.Euler = euler;
				p.Aft = aft;
				Parts.Add(p);
				return Parts[Parts.Count() - 1];
			}

			/// A hull section: the Core module (hull and cockpit) stretched from z0 to z1, w across and h deep. Aft turns it
			/// round (its cockpit to the stern); Belly turns it over (its canopy under the keel, out of sight).
			Part& Hull(float z0, float z1, float w, float h, float y = 0, float x = 0, bool aft = false, bool belly = false)
			{
				return Add(P::Core, Vector3(x, y, (z0 + z1) * 0.5f), Vector3(w, h, z1 - z0), Vector3(0, 0, belly ? 180.0f : 0.0f), aft);
			}

			/// A block of plating: the Plasma pod (a rounded, panelled capsule) as a hull block, sponson or superstructure.
			Part& Block(Vector3 centre, Vector3 size, bool aft = false) { return Add(P::Plasma, centre, size, Vector3::zero(), aft); }

			/// A beam: the Weapon barrel as a spar, a boom, a truss longeron or a gun.
			Part& Beam(Vector3 centre, Vector3 size, Vector3 euler = Vector3::zero()) { return Add(P::Weapon, centre, size, euler); }

			/// An engine: its bell (the Thruster) with its exit at zBack, and the Engine module ahead of it.
			void Engine(float x, float y, float zBack, float r, float length)
			{
				float bell = r * 1.25f;
				Add(P::Thruster, Vector3(x, y, zBack + bell * 0.5f), Vector3(r * 2.2f, r * 2.2f, bell));
				if (length > 0) Add(P::Engine, Vector3(x, y, zBack + bell * 0.85f + length * 0.5f), Vector3(r * 1.8f, r * 1.8f, length));
			}

			/// A wing from its root at (x, y, z) out to one side (side +1 starboard, -1 port), swept back and tilted up
			/// (dihedral) by degrees.
			Part& Wing(float x, float y, float z, float span, float chord, float thick, float side, float dihedral = 0, float sweep = 0, P piece = P::Wing)
			{
				float roll = side > 0 ? -(90.0f - dihedral) : 90.0f - dihedral;
				return Add(piece, Vector3(x, y, z), Vector3(thick, span, chord), Vector3(0, side * sweep, roll));
			}

			/// A fin standing up from (x, y, z) (roll 180 hangs it under the keel).
			Part& Fin(float x, float y, float z, float height, float chord, float thick, float roll = 0, P piece = P::Fin)
			{
				return Add(piece, Vector3(x, y, z), Vector3(thick, height, chord), Vector3(0, 0, roll));
			}

			/// One of the pack's prebuilt fighters, scaled to the whole length (wingspan narrowed by span).
			Part& Fighter(int model, float span = 1.0f, Vector3 pos = Vector3::zero(), float length = 1.0f)
			{
				auto& p = Add(P::Fighter, pos, Vector3(0, 0, length));
				p.Model = model;
				const auto& info = Info(P::Fighter, model);
				if (info.Ok)
				{
					// The fighter's own proportions, its span narrowed towards the game's beam.
					Vector3 s = info.Size();
					float k = length / Mathf::Max(s.x, 1e-3f);
					p.Size = Vector3(s.z * k * span, s.y * k, length);
				}
				return p;
			}

			/// Engines on every Engine hardpoint of the layout (their glows sit just behind the bells), or else at the
			/// given places.
			void Engines(const cs::List<cs::Ref<SubsystemDef>>* layout, float r, float length, std::initializer_list<Vector3> otherwise)
			{
				int n = 0;
				if (layout)
					for (const auto& d : *layout)
						if (d && d->Kind == SubsystemKind::Engine)
						{
							Engine(static_cast<float>(d->X), static_cast<float>(d->Y), static_cast<float>(d->Z) - 0.03f, r, length);
							n++;
						}
				if (n == 0)
					for (const auto& v : otherwise) Engine(v.x, v.y, v.z, r, length);
			}

			/// Every part mirrored to port (its x and turns flipped), for symmetric work.
			void Mirror(const std::function<void(float side)>& make)
			{
				make(1.0f);
				make(-1.0f);
			}
		};

		// ------------------------------------------------------------ the classes

		using Layout = const cs::List<cs::Ref<SubsystemDef>>*;

		void Carrier(Recipe& r, Layout layout)
		{
			// Twin hulls under one broad flight deck: a catamaran, flat and wide, unlike any other class.
			r.Mirror([&](float s)
			{
				r.Hull(ShipMeshFactory::SplitZ + 0.01f, 0.5f, 0.09f, 0.07f, 0, s * 0.055f);
				r.Block(Vector3(s * 0.055f, 0, -0.27f), Vector3(0.1f, 0.075f, 0.42f));
				// Sponsons along each beam, the side hangars' bays.
				r.Block(Vector3(s * 0.118f, -0.005f, 0.13f), Vector3(0.035f, 0.05f, 0.3f));
				r.Block(Vector3(s * 0.118f, -0.005f, -0.25f), Vector3(0.035f, 0.05f, 0.34f));
				// The flight deck: wing plates laid flat from the keel out, forward and aft of the break.
				r.Wing(0, 0.04f, 0.2f, 0.135f, 0.5f, 0.012f, s);
				r.Wing(0, 0.04f, -0.3f, 0.135f, 0.38f, 0.012f, s);
			});
			// The island: a tower block and its mast, to starboard aft.
			r.Block(Vector3(0.075f, 0.085f, -0.12f), Vector3(0.035f, 0.08f, 0.13f));
			r.Fin(0.075f, 0.12f, -0.12f, 0.06f, 0.06f, 0.006f);
			r.Engines(layout, 0.021f, 0.06f, { Vector3(-0.075f, 0, -0.52f), Vector3(-0.027f, 0, -0.52f), Vector3(0.027f, 0, -0.52f), Vector3(0.075f, 0, -0.52f) });
		}

		void Battleship(Recipe& r, Layout layout)
		{
			// The heaviest gun line: a deep, armour-belted hull, a tall tower amidships and a big three-engine drive.
			r.Hull(ShipMeshFactory::SplitZ, 0.5f, 0.14f, 0.085f);
			r.Hull(0.02f, 0.42f, 0.07f, 0.045f, -0.04f, 0, false, true);
			r.Block(Vector3(0, 0, -0.27f), Vector3(0.15f, 0.09f, 0.42f));
			r.Block(Vector3(0, -0.045f, -0.25f), Vector3(0.07f, 0.04f, 0.32f));
			r.Mirror([&](float s)
			{
				// Armour belts down both beams.
				r.Block(Vector3(s * 0.074f, 0, 0.14f), Vector3(0.022f, 0.045f, 0.36f));
				r.Block(Vector3(s * 0.078f, 0, -0.26f), Vector3(0.022f, 0.05f, 0.38f));
				r.Wing(s * 0.07f, 0.0f, -0.36f, 0.04f, 0.1f, 0.008f, s, 0, 30);
			});
			r.Block(Vector3(0, 0.065f, -0.12f), Vector3(0.06f, 0.05f, 0.15f));
			r.Block(Vector3(0, 0.095f, -0.11f), Vector3(0.04f, 0.025f, 0.08f));
			r.Fin(0, 0.105f, -0.13f, 0.05f, 0.05f, 0.006f);
			r.Fin(0, 0.04f, -0.4f, 0.05f, 0.09f, 0.008f, 0, P::Tail);
			r.Engines(layout, 0.022f, 0.07f, { Vector3(-0.042f, 0, -0.51f), Vector3(0, 0, -0.51f), Vector3(0.042f, 0, -0.51f) });
		}

		void GateBreaker(Recipe& r, Layout layout)
		{
			// A short, slab-sided siege hull: three hulls side by side under thick belts, a blunt ram bow.
			r.Hull(ShipMeshFactory::SplitZ, 0.5f, 0.17f, 0.1f);
			for (float x : { -0.05f, 0.0f, 0.05f })
				r.Block(Vector3(x, 0, -0.27f), Vector3(0.065f, 0.1f, 0.42f));
			r.Mirror([&](float s)
			{
				r.Block(Vector3(s * 0.09f, 0, 0.16f), Vector3(0.025f, 0.075f, 0.42f));
				r.Block(Vector3(s * 0.09f, 0, -0.26f), Vector3(0.025f, 0.075f, 0.4f));
				// Ram plates either side of the bow.
				r.Wing(s * 0.06f, 0, 0.36f, 0.04f, 0.16f, 0.012f, s, 0, -20);
			});
			r.Block(Vector3(0, 0.07f, -0.05f), Vector3(0.07f, 0.035f, 0.2f)).Section = ShipMeshFactory::Stern;
			r.Block(Vector3(0, 0.09f, -0.26f), Vector3(0.05f, 0.04f, 0.08f));
			r.Fin(0, 0.11f, -0.26f, 0.08f, 0.04f, 0.008f);
			r.Engines(layout, 0.025f, 0.06f, { Vector3(-0.045f, 0, -0.51f), Vector3(0, 0, -0.51f), Vector3(0.045f, 0, -0.51f) });
		}

		void Cruiser(Recipe& r, Layout layout)
		{
			// A long fine bow, a slimmer drive spine, and outrigger engine nacelles on wing banks amidships.
			r.Hull(ShipMeshFactory::SplitZ, 0.5f, 0.12f, 0.055f);
			r.Block(Vector3(0, 0, -0.27f), Vector3(0.07f, 0.05f, 0.42f));
			r.Mirror([&](float s)
			{
				r.Wing(s * 0.03f, 0, -0.08f, 0.075f, 0.2f, 0.01f, s, 0, 15);
			});
			r.Block(Vector3(0, 0.045f, -0.2f), Vector3(0.04f, 0.035f, 0.1f));
			r.Fin(0, 0.06f, -0.21f, 0.04f, 0.05f, 0.006f);
			r.Fin(0, -0.02f, -0.35f, 0.04f, 0.07f, 0.006f, 180, P::Tail);
			r.Engines(layout, 0.02f, 0.08f, { Vector3(-0.025f, 0, -0.5f), Vector3(0.025f, 0, -0.5f), Vector3(-0.1f, 0, -0.255f), Vector3(0.1f, 0, -0.255f) });
		}

		void CommandCruiser(Recipe& r, Layout layout)
		{
			// A long battlecruiser hull carrying a big search dish on a dorsal mast and antenna spars down both beams.
			r.Hull(ShipMeshFactory::SplitZ, 0.5f, 0.11f, 0.055f);
			r.Block(Vector3(0, 0, -0.27f), Vector3(0.13f, 0.065f, 0.42f));
			r.Mirror([&](float s)
			{
				r.Beam(Vector3(s * 0.085f, 0.01f, 0.13f), Vector3(0.008f, 0.008f, 0.34f));
				r.Beam(Vector3(s * 0.085f, 0.01f, -0.2f), Vector3(0.008f, 0.008f, 0.28f));
				r.Block(Vector3(s * 0.07f, 0, -0.2f), Vector3(0.025f, 0.04f, 0.3f));
			});
			// The array: a mast and its dish (an engine bell opened out flat), facing forward.
			r.Fin(0, 0.03f, 0.24f, 0.08f, 0.04f, 0.008f);
			r.Add(P::Thruster, Vector3(0, 0.12f, 0.255f), Vector3(0.11f, 0.11f, 0.02f), Vector3::zero(), true);
			r.Block(Vector3(0, 0.055f, 0.05f), Vector3(0.05f, 0.04f, 0.09f));
			r.Engines(layout, 0.018f, 0.06f, { Vector3(-0.04f, 0, -0.51f), Vector3(0, 0, -0.51f), Vector3(0.04f, 0, -0.51f) });
		}

		void Destroyer(Recipe& r, Layout layout)
		{
			// A long knife-edged wedge with a deep keel fin and twin big drives.
			r.Hull(ShipMeshFactory::SplitZ, 0.5f, 0.13f, 0.06f);
			r.Block(Vector3(0, 0, -0.26f), Vector3(0.13f, 0.065f, 0.4f));
			r.Fin(0, -0.03f, -0.1f, 0.05f, 0.3f, 0.008f, 180, P::Tail).Section = ShipMeshFactory::Stern;
			r.Fin(0, 0.03f, -0.28f, 0.06f, 0.16f, 0.008f, 0, P::Tail);
			r.Block(Vector3(0, 0.04f, -0.09f), Vector3(0.05f, 0.03f, 0.1f));
			r.Mirror([&](float s) { r.Wing(s * 0.06f, 0, -0.32f, 0.05f, 0.14f, 0.008f, s, -10, 25); });
			r.Engines(layout, 0.025f, 0.08f, { Vector3(-0.035f, 0, -0.5f), Vector3(0.035f, 0, -0.5f) });
		}

		void Frigate(Recipe& r, Layout layout)
		{
			// A slim spine with paired missile racks forward and an engine block a third of the hull long.
			r.Hull(ShipMeshFactory::SplitZ, 0.5f, 0.07f, 0.045f);
			r.Block(Vector3(0, 0, -0.33f), Vector3(0.15f, 0.1f, 0.26f));
			r.Hull(-0.22f, ShipMeshFactory::SplitZ - 0.005f, 0.06f, 0.04f, 0, 0, false, true);
			r.Mirror([&](float s)
			{
				for (float y : { -0.035f, 0.035f }) r.Block(Vector3(s * 0.06f, y, 0.15f), Vector3(0.04f, 0.024f, 0.16f));
			});
			r.Engines(layout, 0.021f, 0.04f, { Vector3(-0.045f, -0.025f, -0.5f), Vector3(0.045f, -0.025f, -0.5f), Vector3(-0.045f, 0.025f, -0.5f), Vector3(0.045f, 0.025f, -0.5f) });
		}

		void Corvette(Recipe& r, Layout layout)
		{
			// A short, stocky patrol hull with broad saddle tanks, a search mast and twin bells.
			r.Hull(ShipMeshFactory::SplitZ, 0.47f, 0.12f, 0.08f);
			r.Block(Vector3(0, 0, -0.25f), Vector3(0.12f, 0.08f, 0.36f));
			r.Mirror([&](float s) { r.Add(P::Engine, Vector3(s * 0.1f, 0, -0.2f), Vector3(0.065f, 0.065f, 0.26f)); });
			r.Fin(0, 0.04f, 0.0f, 0.1f, 0.03f, 0.008f).Section = ShipMeshFactory::Bow;
			r.Add(P::Thruster, Vector3(0, 0.15f, 0.0f), Vector3(0.09f, 0.09f, 0.012f), Vector3(90, 0, 0)).Section = ShipMeshFactory::Bow;
			r.Engines(layout, 0.025f, 0.05f, { Vector3(-0.04f, 0, -0.5f), Vector3(0.04f, 0, -0.5f) });
		}

		void Gunship(Recipe& r, Layout layout)
		{
			// The heavy fighter: a broad armoured wedge with plated cheeks, a spinal gun under the nose and twin drives.
			r.Hull(-0.4f, 0.42f, 0.24f, 0.13f);
			r.Mirror([&](float s)
			{
				r.Block(Vector3(s * 0.15f, 0, -0.12f), Vector3(0.07f, 0.1f, 0.48f));
				r.Wing(s * 0.17f, -0.01f, -0.2f, 0.1f, 0.2f, 0.015f, s, -5, 20);
			});
			r.Beam(Vector3(0, -0.055f, 0.3f), Vector3(0.035f, 0.03f, 0.4f));
			r.Fin(0, 0.05f, -0.3f, 0.09f, 0.15f, 0.01f, 0, P::Tail);
			r.Engines(layout, 0.045f, 0.08f, { Vector3(-0.07f, 0, -0.5f), Vector3(0.07f, 0, -0.5f) });
		}

		void Bomber(Recipe& r, Layout layout)
		{
			// Deep and broad on straight wings, a torpedo slung under the keel, twin engines.
			r.Hull(-0.42f, 0.45f, 0.2f, 0.15f);
			r.Mirror([&](float s)
			{
				r.Wing(s * 0.08f, -0.02f, -0.05f, 0.34f, 0.28f, 0.02f, s, 3, 5);
				r.Fin(s * 0.06f, 0.05f, -0.36f, 0.12f, 0.14f, 0.012f, s * -12.0f, P::Tail);
			});
			r.Block(Vector3(0, -0.11f, 0.05f), Vector3(0.07f, 0.07f, 0.55f));
			r.Engines(layout, 0.05f, 0.1f, { Vector3(-0.07f, 0, -0.5f), Vector3(0.07f, 0, -0.5f) });
		}

		void Shuttle(Recipe& r, Layout layout)
		{
			// A blunt armoured box with a docking collar on its flank and twin drives.
			r.Hull(-0.4f, 0.48f, 0.24f, 0.19f);
			r.Mirror([&](float s) { r.Block(Vector3(s * 0.135f, -0.02f, -0.05f), Vector3(0.05f, 0.15f, 0.5f)); });
			r.Add(P::Thruster, Vector3(0.18f, 0, 0.02f), Vector3(0.12f, 0.12f, 0.03f), Vector3(0, 90, 0));
			r.Engines(layout, 0.05f, 0.08f, { Vector3(-0.08f, 0, -0.5f), Vector3(0.08f, 0, -0.5f) });
		}

		void Freighter(Recipe& r, int pods, int spheres)
		{
			// A truss freighter: cockpit at the bow, four longerons back to the drive, containers clamped along it.
			r.Hull(0.33f, 0.5f, 0.075f, 0.06f);
			constexpr float front = 0.36f, back = -0.4f, half = 0.022f;
			for (float x : { -half, half })
				for (float y : { -half, half })
					r.Beam(Vector3(x, y, (front + back) * 0.5f), Vector3(0.008f, 0.008f, front - back));
			pods = Mathf::Clamp(pods, 0, ShipMeshFactory::MaxTrussPods);
			int abreast = pods > 8 ? 4 : 2;
			int rows = Mathf::Max(1, (pods + abreast - 1) / abreast);
			constexpr float podFront = 0.3f, podBack = -0.14f, reach = 0.068f, size = 0.075f;
			float pitch = rows > 1 ? (podFront - podBack) / (rows - 1) : 0;
			float length = Mathf::Min(0.1f, rows > 1 ? pitch * 0.9f : 0.1f);
			for (int i = 0; i < pods; i++)
			{
				int row = i / abreast;
				float z = rows > 1 ? podFront - pitch * row : (podFront + podBack) * 0.5f;
				int place = i % abreast;
				Vector2 dir = place == 0 ? Vector2(1, 0) : place == 1 ? Vector2(-1, 0) : place == 2 ? Vector2(0, 1) : Vector2(0, -1);
				auto& c = r.Block(Vector3(dir.x * reach, dir.y * reach, z), Vector3(size, size, length));
				c.Name = cs::Cat(StarSparrowKit::CargoName, i);
			}
			spheres = Mathf::Clamp(spheres, 0, ShipMeshFactory::MaxTrussSpheres);
			for (int i = 0; i < spheres; i++)
			{
				int place = i % 4;
				Vector2 dir = place == 0 ? Vector2(1, 0) : place == 1 ? Vector2(-1, 0) : place == 2 ? Vector2(0, 1) : Vector2(0, -1);
				r.Add(P::Engine, Vector3(dir.x * 0.065f, dir.y * 0.065f, i < 4 ? -0.27f : -0.19f), Vector3(0.07f, 0.07f, 0.07f));
			}
			r.Block(Vector3(0, 0, -0.42f), Vector3(0.09f, 0.09f, 0.09f));
			r.Engine(0, 0, -0.5f, 0.045f, 0);
		}

		void Miner(Recipe& r)
		{
			// A deep-holded hauler: a fat hull, ore bins on its back and twin drives.
			r.Hull(-0.3f, 0.48f, 0.22f, 0.15f);
			r.Block(Vector3(0, 0.09f, 0.0f), Vector3(0.16f, 0.07f, 0.38f));
			r.Mirror([&](float s) { r.Block(Vector3(s * 0.12f, -0.02f, -0.1f), Vector3(0.07f, 0.1f, 0.4f)); });
			r.Engine(-0.06f, 0, -0.5f, 0.04f, 0.08f);
			r.Engine(0.06f, 0, -0.5f, 0.04f, 0.08f);
		}

		void OreTug(Recipe& r, bool capture)
		{
			// No hold, just a big engine with claws: a blocky frame, a cluster of heavy bells, a push plate and claws.
			float w = capture ? 0.12f : 0.16f;
			r.Hull(-0.3f, 0.2f, w, capture ? 0.1f : 0.14f, 0, 0, false, true);
			r.Block(Vector3(0, 0, -0.33f), Vector3(capture ? 0.22f : 0.3f, capture ? 0.14f : 0.2f, 0.2f));
			r.Block(Vector3(0, capture ? 0.065f : 0.085f, 0.08f), Vector3(0.07f, 0.05f, 0.12f));
			// The push plate: two wing plates either side of the keel, standing across the bow.
			r.Mirror([&](float s) { r.Wing(0, 0, 0.22f, capture ? 0.1f : 0.14f, 0.03f, 0.24f, s); });
			int claws = capture ? 3 : 4;
			for (int k = 0; k < claws; k++)
			{
				float deg = capture ? 90.0f + 120.0f * k : 45.0f + 90.0f * k, a = deg * Mathf::Deg2Rad;
				Vector3 radial(Mathf::Cos(a), Mathf::Sin(a), 0);
				// A fin turned to reach forward, splayed out from the keel at its place round the bow.
				auto& c = r.Add(P::Fin, radial * (capture ? 0.1f : 0.13f) + Vector3(0, 0, 0.22f), Vector3(0.025f, capture ? 0.32f : 0.26f, 0.06f),
					Vector3(0, 0, deg - 90.0f));
				c.Pre = Vector3(80, 0, 0);
			}
			if (capture)
			{
				r.Engine(-0.06f, 0, -0.5f, 0.04f, 0.05f);
				r.Engine(0.06f, 0, -0.5f, 0.04f, 0.05f);
			}
			else
			{
				r.Engine(-0.09f, 0, -0.49f, 0.05f, 0.05f);
				r.Engine(0, 0, -0.51f, 0.06f, 0.05f);
				r.Engine(0.09f, 0, -0.49f, 0.05f, 0.05f);
			}
		}

		void LaneTender(Recipe& r)
		{
			// A stubby work hull holding a glowing service ring out ahead on two booms.
			r.Hull(-0.35f, 0.2f, 0.2f, 0.15f);
			r.Mirror([&](float s)
			{
				r.Block(Vector3(s * 0.11f, 0, -0.1f), Vector3(0.04f, 0.1f, 0.36f));
				r.Beam(Vector3(s * 0.07f, 0, 0.3f), Vector3(0.02f, 0.02f, 0.24f));
			});
			for (int k = 0; k < 12; k++)
			{
				float deg = 30.0f * k;
				auto& f = r.Add(P::Fin, Vector3(Mathf::Cos(deg * Mathf::Deg2Rad) * 0.12f, Mathf::Sin(deg * Mathf::Deg2Rad) * 0.12f, 0.42f),
					Vector3(0.012f, 0.08f, 0.05f), Vector3(0, 0, deg - 90.0f));
				(void)f;
			}
			r.Engine(-0.05f, 0, -0.5f, 0.035f, 0.06f);
			r.Engine(0.05f, 0, -0.5f, 0.035f, 0.06f);
		}

		void Picket(Recipe& r)
		{
			// More antenna than ship: a slim hull under a tall sensor mast and a broad search dish, one small drive.
			r.Hull(-0.42f, 0.46f, 0.12f, 0.08f);
			r.Fin(0, 0.03f, -0.08f, 0.24f, 0.05f, 0.012f);
			r.Add(P::Thruster, Vector3(0, 0.28f, -0.08f), Vector3(0.22f, 0.22f, 0.03f), Vector3(-90, 0, 0));
			r.Mirror([&](float s) { r.Wing(s * 0.05f, 0, -0.15f, 0.12f, 0.12f, 0.01f, s, 0, 20); });
			r.Engine(0, 0, -0.5f, 0.035f, 0.06f);
		}

		void Explorer(Recipe& r)
		{
			// A long-legged survey hull: a slender body, a drive nacelle on each wingtip and a sensor fin.
			r.Hull(-0.38f, 0.5f, 0.11f, 0.07f);
			r.Mirror([&](float s)
			{
				r.Wing(s * 0.04f, 0, -0.15f, 0.12f, 0.18f, 0.01f, s, 0, 25);
				r.Engine(s * 0.17f, 0, -0.42f, 0.03f, 0.22f);
			});
			r.Fin(0, 0.035f, -0.25f, 0.1f, 0.12f, 0.008f, 0, P::Tail);
			r.Engine(0, 0, -0.5f, 0.03f, 0.06f);
		}

		void Bus(Recipe& r)
		{
			// An orbital bus: a long boxy cabin with a roof pod and twin drives.
			r.Hull(-0.4f, 0.5f, 0.22f, 0.2f);
			r.Block(Vector3(0, 0.1f, -0.05f), Vector3(0.16f, 0.06f, 0.6f));
			r.Engine(-0.06f, 0, -0.5f, 0.04f, 0.06f);
			r.Engine(0.06f, 0, -0.5f, 0.04f, 0.06f);
		}

		// Which prebuilt fighter each small class flies (picked from the pack's contact sheet, ShipLineup).
		int FighterModel(ShipClass cls, CivilCraft civil, int id)
		{
			switch (civil)
			{
				case CivilCraft::Taxi: return 20;
				case CivilCraft::Skiff: return 4;
				case CivilCraft::Runabout: return 21;
				case CivilCraft::Tourer: return 23;
				default: break;
			}
			switch (cls)
			{
				case ShipClass::Interceptor: return 8;
				case ShipClass::Fighter: return 13;
				case ShipClass::Recon: return 12;
				case ShipClass::Courier: return id % 2 == 0 ? 15 : 16;
				default: return 0;
			}
		}

		void RecipeFor(Recipe& r, ShipClass cls, CivilCraft civil, int id, Layout layout, int pods, int spheres)
		{
			if (civil == CivilCraft::Bus)
			{
				Bus(r);
				return;
			}
			if (int model = FighterModel(cls, civil, id); model > 0)
			{
				r.Fighter(model, 0.75f);
				return;
			}
			switch (cls)
			{
				case ShipClass::Carrier: Carrier(r, layout); break;
				case ShipClass::Battleship: Battleship(r, layout); break;
				case ShipClass::GateBreaker: GateBreaker(r, layout); break;
				case ShipClass::Cruiser: Cruiser(r, layout); break;
				case ShipClass::CommandCruiser: CommandCruiser(r, layout); break;
				case ShipClass::Destroyer: Destroyer(r, layout); break;
				case ShipClass::Frigate: Frigate(r, layout); break;
				case ShipClass::Corvette: Corvette(r, layout); break;
				case ShipClass::Gunship: Gunship(r, layout); break;
				case ShipClass::Bomber: Bomber(r, layout); break;
				case ShipClass::AssaultShuttle: Shuttle(r, layout); break;
				case ShipClass::Miner: Miner(r); break;
				case ShipClass::OreTug: OreTug(r, false); break;
				case ShipClass::CaptureTug: OreTug(r, true); break;
				case ShipClass::LaneTender: LaneTender(r); break;
				case ShipClass::GatePicket: Picket(r); break;
				case ShipClass::Explorer: Explorer(r); break;
				default: Freighter(r, pods > 0 ? pods : 8, spheres > 0 ? spheres : 4); break;
			}
		}

		// ------------------------------------------------------------ assembly

		cs::Ref<MeshRenderer> Place(const cs::Ref<Transform>& parent, const Part& p, const cs::Ref<Material>& material)
		{
			const auto& info = Info(p.Piece, p.Model);
			if (!info.Ok) return nullptr;
			Vector3 s = info.Size();
			// The pack's nose (+x) to the game's (+z): its across axis (z) then lies along the game's -x.
			static const Quaternion noseForward = Quaternion::Euler(0, -90, 0);
			// The Engine module is modelled with its exhaust forward (+x): turned round so its glow is aft. Every other
			// module and fighter has its nose on +x (seen on the lineup's kit sheets).
			bool aft = p.Aft != (p.Piece == P::Engine);
			Quaternion turn = Quaternion::Euler(p.Euler) * Quaternion::Euler(p.Pre) * (aft ? Quaternion::Euler(0, 180, 0) : Quaternion::identity()) * noseForward;
			float along = p.Size.z > 0 ? p.Size.z / Mathf::Max(s.x, 1e-4f) : p.Size.x / Mathf::Max(s.z, 1e-4f);
			Vector3 scale(along, p.Size.y > 0 ? p.Size.y / Mathf::Max(s.y, 1e-4f) : along, p.Size.x > 0 ? p.Size.x / Mathf::Max(s.z, 1e-4f) : along);
			auto go = NewGameObject(!p.Name.empty() ? p.Name : info.Drawn->name);
			go->transform->SetParent(parent, false);
			go->transform->localRotation = turn;
			go->transform->localScale = scale;
			go->transform->localPosition = p.Pos - turn * Vector3::Scale(scale, info.Anchor());
			AddComponent<MeshFilter>(go)->sharedMesh = info.Drawn;
			auto mr = AddComponent<MeshRenderer>(go);
			mr->SetSharedMaterial(material);
			mr->shadowCastingMode = ShadowCastingMode::Off;
			mr->receiveShadows = false;
			return mr;
		}

		cs::Ref<Transform> NewSection(const cs::Ref<Transform>& parent, const cs::String& name)
		{
			auto go = NewGameObject(name);
			go->transform->SetParent(parent, false);
			return go->transform;
		}

		Color Mix(Color a, Color b, float t) { return Color::Lerp(a, b, t); }
	}

	bool StarSparrowKit::Available()
	{
		static int state = -1;
		if (state < 0)
		{
			bool wanted = FApp::CanEverRender() && !FParse::Param(FCommandLine::Get(), TEXT("NoStarSparrow"));
			bool core = wanted && Info(P::Core).Ok;
			bool material = wanted && LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s.M_StarSparrowShip"), UTF8_TO_TCHAR(MaterialPath)), nullptr,
				LOAD_NoWarn | LOAD_Quiet) != nullptr;
			state = core && material ? 1 : 0;
			if (!wanted)
			{
				UE_LOG(LogSpaceGod, Display, TEXT("StarSparrowKit: off (%s); ships are procedural"), FApp::CanEverRender() ? TEXT("-NoStarSparrow") : TEXT("no renderer"));
			}
			else if (state == 0)
			{
				UE_LOG(LogSpaceGod, Display, TEXT("StarSparrowKit: the pack is not imported (%s%s); ships are procedural (tools/import-starsparrow.ps1)"),
					core ? TEXT("") : TEXT("no module meshes "), material ? TEXT("") : TEXT("no M_StarSparrowShip"));
			}
			else
			{
				UE_LOG(LogSpaceGod, Display, TEXT("StarSparrowKit: ships are built from the Star Sparrow kit"));
			}
		}
		return state == 1;
	}

	const cs::List<cs::Ref<SubsystemDef>>* StarSparrowKit::LayoutOf(ShipClass cls, const cs::List<cs::Ref<SubsystemDef>>* fitted)
	{
		// A class with no design of its own gets the destroyer's from CombatSpec.Of: its hardpoints are not this hull's.
		if (CombatSpec::DesignsOf(cls).Count() == 0) return nullptr;
		if (fitted) return fitted;
		return &CombatSpec::Of(cls)->Layout;
	}

	bool StarSparrowKit::Build(ShipClass cls, CivilCraft civil, int id, const cs::Ref<Faction>& faction, const cs::List<cs::Ref<SubsystemDef>>* layout,
		const cs::Ref<Transform>& parent, cs::List<cs::Ref<Renderer>>& renderers, cs::Ref<Transform>& stern, cs::Ref<Transform>& bow, int pods, int spheres)
	{
		bow = nullptr;
		if (!Available()) return false;
		Recipe r;
		RecipeFor(r, cls, civil, id, layout, pods, spheres);
		auto material = Livery(faction);
		bool split = civil == CivilCraft::None && ShipMeshFactory::IsWarship(cls);
		stern = NewSection(parent, split ? "Stern" : "Hull");
		if (split) bow = NewSection(parent, "Bow");
		for (const auto& p : r.Parts)
		{
			int section = p.Section >= 0 ? p.Section : p.Pos.z >= ShipMeshFactory::SplitZ ? ShipMeshFactory::Bow : ShipMeshFactory::Stern;
			auto mr = Place(split && section == ShipMeshFactory::Bow ? bow : stern, p, material);
			if (mr) renderers.Add(mr);
		}
		return true;
	}

	bool StarSparrowKit::Turret(const cs::Ref<Transform>& parent, SubsystemKind kind, const cs::Ref<Faction>& faction, bool barrels,
		cs::List<cs::Ref<Renderer>>& into)
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
		auto material = Livery(faction);
		auto root = NewSection(parent, barrels ? "KitBarrels" : "KitBase");
		if (!barrels)
		{
			Part base;
			base.Piece = P::Plasma;
			base.Pos = Vector3(0, w * 0.2f, 0);
			base.Size = Vector3(w, w * 0.5f, w * 1.15f);
			if (auto mr = Place(root, base, material)) into.Add(mr);
		}
		else
		{
			float length = w * 1.3f, spacing = w * 0.24f;
			for (int g = 0; g < guns; g++)
			{
				Part b;
				b.Piece = P::Weapon;
				b.Pos = Vector3((g - (guns - 1) * 0.5f) * spacing, 0, length * 0.5f);
				b.Size = Vector3(w * 0.14f, w * 0.18f, length);
				if (auto mr = Place(root, b, material)) into.Add(mr);
			}
		}
		return true;
	}

	cs::Ref<Material> StarSparrowKit::Livery(const cs::Ref<Faction>& faction_)
	{
		static cs::Dictionary<cs::Ref<Faction>, cs::Ref<Material>> cache;
		static cs::Ref<Material> neutral;
		auto faction = ShipMeshFactory::Canon(faction_);
		cs::Ref<Material> m;
		if (faction ? cache.TryGetValue(faction, m) && Alive(m) : Alive(neutral)) return faction ? m : neutral;
		m = Materials::MarkShared(cs::New<Material>(Shader::Find(MaterialPath), cs::Cat("StarSparrow_", faction ? faction->Prefix : cs::String("Neutral"))));
		// Tints multiply the pack's Neutral atlas inside each mask (main hull, dark trim, light panels, accent lights), as
		// its own colour variants do (the Grey fit is 0.54 / 0.33 / 1.08 / 0.94).
		Color f = faction ? Color(faction->Color.R, faction->Color.G, faction->Color.B) : Color(0.6f, 0.6f, 0.62f);
		Color hull, trim, panels, lights;
		if (faction && faction->IsPirate)
		{
			// Pirates: soot-black hulls, trim daubed in the clan's colour, red running lights.
			hull = Mix(Color(0.2f, 0.19f, 0.19f), f, 0.18f);
			trim = Mix(Color(0.5f, 0.12f, 0.1f), f, 0.55f) * 1.4f;
			panels = Color(0.62f, 0.56f, 0.5f);
			lights = Color(1.3f, 0.22f, 0.12f);
		}
		else if (faction && (faction->IsMilitary || faction->IsHouse))
		{
			// Navies: steel washed in the navy's colour, dark trim, the colour bright on the panels and lights.
			hull = Mix(Color(0.5f, 0.52f, 0.55f), f, 0.42f) * 1.15f;
			trim = Color(0.27f, 0.28f, 0.31f);
			panels = Mix(Color(1.0f, 1.0f, 1.02f), f, 0.45f) * 1.1f;
			lights = f * 1.35f;
		}
		else
		{
			// Civilians: white hulls with a stripe of the owner's colour and amber running lights.
			hull = Color(1.3f, 1.28f, 1.22f);
			trim = Mix(Color(0.45f, 0.46f, 0.48f), f, 0.25f);
			panels = Mix(Color(1.0f, 1.0f, 1.0f), f, 0.7f) * 1.05f;
			lights = Color(1.15f, 0.95f, 0.4f);
		}
		m->SetColor("Tint1", hull);
		m->SetColor("Tint2", trim);
		m->SetColor("Tint3", panels);
		m->SetColor("Tint4", lights);
		m->SetColor("_Tint", Color::white());
		if (faction) cache[faction] = m;
		else neutral = m;
		return m;
	}

	cs::List<cs::String> StarSparrowKit::PieceNames()
	{
		cs::List<cs::String> names{ "Core", "Engine", "Thruster", "Plasma", "Weapon", "Wing", "Fin", "Tail" };
		for (int i = 1; i <= 40; i++) names.Add(cs::Cat("Fighter", i));
		return names;
	}

	cs::Ref<Renderer> StarSparrowKit::Piece(const cs::Ref<Transform>& parent, const cs::String& name, float length, const cs::Ref<Faction>& faction)
	{
		Part p;
		const P modules[] = { P::Core, P::Engine, P::Thruster, P::Plasma, P::Weapon, P::Wing, P::Fin, P::Tail };
		bool found = false;
		for (P m : modules)
			if (name == ModuleName(m))
			{
				p.Piece = m;
				found = true;
			}
		if (!found)
		{
			p.Piece = P::Fighter;
			p.Model = std::atoi(name.c_str() + 7);
		}
		const auto& info = Info(p.Piece, p.Model);
		if (!info.Ok) return nullptr;
		// Its own proportions, its longest extent `length`.
		Vector3 s = info.Size();
		float k = length / Mathf::Max(Mathf::Max(s.x, s.y), Mathf::Max(s.z, 1e-4f));
		p.Size = Vector3(s.z * k, s.y * k, s.x * k);
		return Place(parent, p, Livery(faction));
	}
}
