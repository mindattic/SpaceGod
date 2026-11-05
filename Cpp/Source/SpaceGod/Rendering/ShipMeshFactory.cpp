// Port of Unity.SpaceGod Runtime/Rendering/ShipMeshFactory.cs (US-T30).
#include "Rendering/ShipMeshFactory.h"

#include <iterator>
#include <tuple>

#include "Ships.h"
#include "Combat/BulkFreighters.h"
#include "Combat/CombatSpecs.h"
#include "Economy/Industry.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Rendering
{
	// The C# nested Builder sits at the end of the class; C++ needs it defined before the methods that use it.

	/// <summary>
	/// Accumulates flat-shaded primitives. With a section chosen, only primitives (or the parts of
	/// frustums) on that side of <see cref="SplitZ"/> are kept, so the two halves meet exactly.
	/// </summary>
	class ShipMeshFactory::Builder final
	{
		cs::List<Vector3> vertices;
		cs::List<Vector3> normals;
		cs::List<Color> colors;
		cs::List<int> triangles;
		int section;
		bool keep = true;

	public:
		Color Hull;
		Color Dark = Color(0.2f, 0.21f, 0.24f, 1.0f);
		Color Light = Color(0.85f, 0.86f, 0.88f, 1.0f);
		Color Accent;

		Builder(const cs::Ref<Faction>& faction, int section_)
		{
			this->section = section_;
			auto f = faction != nullptr ? Color(faction->Color.R, faction->Color.G, faction->Color.B, 1.0f) : Color::gray();
			// Factions are told apart by the shade of their plating as well as their trim.
			Hull = Color::Lerp(Color(0.6f, 0.62f, 0.66f, 1.0f), f, 0.32f);
			Accent = f;
		}

		Color Glow(Color c) const { return Color(c.r, c.g, c.b, 0.0f); }

	private:
		void Keep(float z) { keep = section == WholeHull || (section == Bow) == (z >= SplitZ); }

		void Quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color)
		{
			if (!keep) return;
			auto n = Vector3::Cross(b - a, c - a).normalized();
			int i = vertices.Count();
			vertices.Add(a);
			vertices.Add(b);
			vertices.Add(c);
			vertices.Add(d);
			for (int k = 0; k < 4; k++)
			{
				normals.Add(n);
				colors.Add(color);
			}
			triangles.Add(i);
			triangles.Add(i + 1);
			triangles.Add(i + 2);
			triangles.Add(i);
			triangles.Add(i + 2);
			triangles.Add(i + 3);
		}

	public:
		/// <summary>Frustum along Z from z0 (back size) to z1 (front size), centred at height y.</summary>
		void Frustum(float z0, float z1, Vector2 back, Vector2 front, float y, Color color)
		{
			if (section != WholeHull && z0 < SplitZ && z1 > SplitZ)
			{
				// Cut at the split so each section gets its own closed piece.
				auto mid = Vector2::Lerp(back, front, (SplitZ - z0) / (z1 - z0));
				Frustum(z0, SplitZ, back, mid, y, color);
				Frustum(SplitZ, z1, mid, front, y, color);
				return;
			}
			Keep((z0 + z1) * 0.5f);
			auto B = [&](float sx, float sy) { return Vector3(sx * back.x * 0.5f, y + sy * back.y * 0.5f, z0); };
			auto F = [&](float sx, float sy) { return Vector3(sx * front.x * 0.5f, y + sy * front.y * 0.5f, z1); };
			// Unity front faces wind clockwise as seen from outside.
			Quad(F(1, -1), F(1, 1), F(-1, 1), F(-1, -1), color);
			Quad(B(-1, -1), B(-1, 1), B(1, 1), B(1, -1), color);
			Quad(F(-1, 1), F(1, 1), B(1, 1), B(-1, 1), color);
			Quad(F(1, -1), F(-1, -1), B(-1, -1), B(1, -1), color);
			Quad(F(-1, -1), F(-1, 1), B(-1, 1), B(-1, -1), color);
			Quad(F(1, 1), F(1, -1), B(1, -1), B(1, 1), color);
		}

		/// <summary>A box turned <paramref name="degrees"/> about Z, then moved to <paramref name="center"/> (for rings and spokes).</summary>
		void BoxRotated(Vector3 center, Vector3 size, float degrees, Color color)
		{
			int before = vertices.Count();
			Box(Vector3::zero(), size, color);
			auto q = Quaternion::Euler(0, 0, degrees);
			for (int i = before; i < vertices.Count(); i++)
			{
				vertices[i] = q * vertices[i] + center;
				normals[i] = q * normals[i];
			}
		}

		void Box(Vector3 center, Vector3 size, Color color)
		{
			auto h = size * 0.5f;
			int before = vertices.Count();
			Frustum(center.z - h.z, center.z + h.z, Vector2(size.x, size.y), Vector2(size.x, size.y), center.y, color);
			if (center.x != 0)
				for (int i = before; i < vertices.Count(); i++) vertices[i] += Vector3(center.x, 0, 0);
		}

		/// <summary>Cylinder along Z centred at <paramref name="center"/>.</summary>
		void Cylinder(Vector3 center, float radius, float length, Color color, bool emissiveBack = false, int segments = 10)
		{
			Keep(center.z);
			float z0 = center.z - length * 0.5f, z1 = center.z + length * 0.5f;
			for (int s = 0; s < segments; s++)
			{
				float a0 = Mathf::PI * 2 * s / segments, a1 = Mathf::PI * 2 * (s + 1) / segments;
				auto p0 = Vector3(Mathf::Cos(a0) * radius, Mathf::Sin(a0) * radius, 0) + Vector3(center.x, center.y, 0);
				auto p1 = Vector3(Mathf::Cos(a1) * radius, Mathf::Sin(a1) * radius, 0) + Vector3(center.x, center.y, 0);
				Quad(Vector3(p0.x, p0.y, z0), Vector3(p1.x, p1.y, z0), Vector3(p1.x, p1.y, z1), Vector3(p0.x, p0.y, z1), color);
				auto c0 = Vector3(center.x, center.y, z0);
				auto back = emissiveBack ? Glow(Color::white()) : color;
				Quad(c0, Vector3(p1.x, p1.y, z0), Vector3(p0.x, p0.y, z0), c0, back);
				auto c1 = Vector3(center.x, center.y, z1);
				Quad(c1, Vector3(p0.x, p0.y, z1), Vector3(p1.x, p1.y, z1), c1, color);
			}
		}

		/// <summary>A faceted sphere about <paramref name="center"/>, poles along Z.</summary>
		void Sphere(Vector3 center, float radius, Color color, int bands = 6, int segments = 10)
		{
			Keep(center.z);
			auto P = [&](int band, int seg)
			{
				float lat = Mathf::PI * band / bands - Mathf::PI * 0.5f, lon = Mathf::PI * 2 * seg / segments;
				float r = Mathf::Cos(lat) * radius;
				return center + Vector3(Mathf::Cos(lon) * r, Mathf::Sin(lon) * r, Mathf::Sin(lat) * radius);
			};
			for (int band = 0; band < bands; band++)
				for (int s = 0; s < segments; s++)
				{
					Vector3 p0 = P(band, s), p1 = P(band, s + 1), p2 = P(band + 1, s + 1), p3 = P(band + 1, s);
					// At the south pole p0 and p1 are one point: start the quad from the far edge so its face has a normal.
					if (band == 0) Quad(p2, p3, p0, p1, color);
					else Quad(p0, p1, p2, p3, color);
				}
		}

		void Nozzle(Vector3 center, float radius, float length)
		{
			Cylinder(center, radius, length, Dark, true, 12);
		}

		cs::Ref<Mesh> ToMesh(const cs::String& name)
		{
			auto mesh = cs::New<Mesh>(name);
			mesh->SetVertices(vertices);
			mesh->SetNormals(normals);
			mesh->SetColors(colors);
			mesh->SetTriangles(triangles, 0);
			mesh->RecalculateBounds();
			return mesh;
		}
	};

	namespace
	{
		cs::Dictionary<std::tuple<ShipClass, cs::Ref<Faction>, int>, cs::Ref<Mesh>>& Cache()
		{
			static cs::Dictionary<std::tuple<ShipClass, cs::Ref<Faction>, int>, cs::Ref<Mesh>> cache;
			return cache;
		}
		cs::Dictionary<std::tuple<SubsystemKind, cs::Ref<Faction>, bool>, cs::Ref<Mesh>>& PartCache()
		{
			static cs::Dictionary<std::tuple<SubsystemKind, cs::Ref<Faction>, bool>, cs::Ref<Mesh>> cache;
			return cache;
		}
		cs::Dictionary<cs::Ref<Faction>, cs::Ref<Mesh>>& PlatformCache()
		{
			static cs::Dictionary<cs::Ref<Faction>, cs::Ref<Mesh>> cache;
			return cache;
		}
		cs::Dictionary<std::tuple<cs::Ref<Faction>, int, int, int>, cs::Ref<Mesh>>& TrussCache()
		{
			static cs::Dictionary<std::tuple<cs::Ref<Faction>, int, int, int>, cs::Ref<Mesh>> cache;
			return cache;
		}
		cs::Ref<Mesh> scar;

		cs::Dictionary<cs::String, cs::Ref<Faction>>& Canonical()
		{
			static cs::Dictionary<cs::String, cs::Ref<Faction>> canonical;
			return canonical;
		}

		cs::Dictionary<std::tuple<SubsystemKind, cs::Ref<Faction>>, cs::Ref<Mesh>>& SpinalCache()
		{
			static cs::Dictionary<std::tuple<SubsystemKind, cs::Ref<Faction>>, cs::Ref<Mesh>> cache;
			return cache;
		}

		cs::Dictionary<cs::Ref<Faction>, cs::Ref<Mesh>>& PodCache()
		{
			static cs::Dictionary<cs::Ref<Faction>, cs::Ref<Mesh>> cache;
			return cache;
		}

		cs::Dictionary<std::tuple<cs::Ref<Faction>, ShipClass>, cs::Ref<Mesh>>& FighterCache()
		{
			static cs::Dictionary<std::tuple<cs::Ref<Faction>, ShipClass>, cs::Ref<Mesh>> cache;
			return cache;
		}

		cs::Dictionary<std::tuple<cs::String, cs::Ref<Faction>>, cs::Ref<Mesh>>& StructureCache()
		{
			static cs::Dictionary<std::tuple<cs::String, cs::Ref<Faction>>, cs::Ref<Mesh>> cache;
			return cache;
		}

		cs::Dictionary<cs::Ref<Faction>, cs::Ref<Mesh>>& ProjectorCache()
		{
			static cs::Dictionary<cs::Ref<Faction>, cs::Ref<Mesh>> cache;
			return cache;
		}

		cs::Ref<Mesh> towRock, towIce;

		cs::Dictionary<cs::Ref<Faction>, cs::Ref<Mesh>>& RoundCache()
		{
			static cs::Dictionary<cs::Ref<Faction>, cs::Ref<Mesh>> cache;
			return cache;
		}
	}

	/// <summary>
	/// The first faction seen with this name: caches key on it, so rebuilding the worlds (time
	/// reset) with new but identical faction objects reuses the meshes instead of piling up copies.
	/// </summary>
	cs::Ref<Faction> ShipMeshFactory::Canon(cs::Ref<Faction> f)
	{
		if (f == nullptr) return nullptr;
		cs::Ref<Faction> c;
		if (Canonical().TryGetValue(f->Name, c)) return c;
		Canonical()[f->Name] = f;
		return f;
	}

	bool ShipMeshFactory::IsWarship(ShipClass c)
	{
		return c == ShipClass::Carrier || c == ShipClass::Battleship || c == ShipClass::Cruiser || c == ShipClass::Destroyer
			|| c == ShipClass::CommandCruiser || c == ShipClass::Frigate || c == ShipClass::Corvette
			|| c == ShipClass::GateBreaker;
	}

	/// <summary>The whole hull (both sections).</summary>
	cs::Ref<Mesh> ShipMeshFactory::Get(ShipClass shipClass, cs::Ref<Faction> faction) { return GetSection(shipClass, faction, WholeHull); }

	/// <summary>One section of a hull: <see cref="Bow"/>, <see cref="Stern"/> or <see cref="WholeHull"/>.</summary>
	cs::Ref<Mesh> ShipMeshFactory::GetSection(ShipClass shipClass, cs::Ref<Faction> faction, int section)
	{
		faction = Canon(faction);
		cs::Ref<Mesh> mesh;
		if (Cache().TryGetValue(std::make_tuple(shipClass, faction, section), mesh)) return mesh;
		Builder b(faction, section);
		switch (shipClass)
		{
			case ShipClass::Carrier: BuildCarrier(b); break;
			case ShipClass::Battleship: BuildBattleship(b); break;
			case ShipClass::Cruiser: BuildCruiser(b); break;
			case ShipClass::Destroyer: BuildDestroyer(b); break;
			case ShipClass::Miner: BuildHauler(b); break;
			case ShipClass::Courier:
			case ShipClass::Explorer: BuildCourier(b); break;
			case ShipClass::Fighter: BuildFighter(b); break;
			case ShipClass::Gunship: BuildGunship(b); break;
			case ShipClass::OreTug: BuildOreTug(b); break;
			case ShipClass::Interceptor: BuildInterceptor(b); break;
			case ShipClass::Bomber: BuildBomber(b); break;
			case ShipClass::LaneTender: BuildLaneTender(b); break;
			case ShipClass::GatePicket: BuildPicket(b); break;
			case ShipClass::CommandCruiser: BuildCommandCruiser(b); break;
			case ShipClass::Frigate: BuildFrigate(b); break;
			case ShipClass::Corvette: BuildCorvette(b); break;
			case ShipClass::Recon: BuildRecon(b); break;
			case ShipClass::AssaultShuttle: BuildShuttle(b); break;
			case ShipClass::GateBreaker: BuildGateBreaker(b); break;
			case ShipClass::CaptureTug: BuildCaptureTug(b); break;
			default: BuildFreighter(b); break;
		}
		mesh = b.ToMesh(cs::Str(shipClass) + "_" + faction->Prefix + "_" + cs::Str(section));
		Cache()[std::make_tuple(shipClass, faction, section)] = mesh;
		return mesh;
	}

	/// <summary>
	/// A truss freighter (U.SPC-T482) as it is loaded now: its spine with a clamp for each of its <paramref name="pods"/>,
	/// a cargo pod in <paramref name="loaded"/> of them (so the pods on it show its load: a bare truss empty, every clamp
	/// filled full) and its <paramref name="spheres"/> fuel spheres. One mesh per combination, built once.
	/// </summary>
	cs::Ref<Mesh> ShipMeshFactory::GetTruss(cs::Ref<Faction> faction, int pods, int loaded, int spheres)
	{
		faction = Canon(faction);
		pods = Mathf::Clamp(pods, 0, MaxTrussPods);
		loaded = Mathf::Clamp(loaded, 0, pods);
		spheres = Mathf::Clamp(spheres, 0, MaxTrussSpheres);
		auto key = std::make_tuple(faction, pods, loaded, spheres);
		cs::Ref<Mesh> mesh;
		if (TrussCache().TryGetValue(key, mesh)) return mesh;
		Builder b(faction, WholeHull);
		BuildTruss(b, pods, loaded, spheres);
		mesh = b.ToMesh("Truss_" + faction->Prefix + "_" + cs::Str(pods) + "_" + cs::Str(loaded) + "_" + cs::Str(spheres));
		TrussCache()[key] = mesh;
		return mesh;
	}

	/// <summary>An orbital defence platform: a gun core inside a ring (its guns are parts too).</summary>
	cs::Ref<Mesh> ShipMeshFactory::GetPlatform(cs::Ref<Faction> faction)
	{
		faction = Canon(faction);
		cs::Ref<Mesh> mesh;
		if (PlatformCache().TryGetValue(faction, mesh)) return mesh;
		Builder b(faction, WholeHull);
		b.Box(Vector3(0, 0, 0), Vector3(0.16f, 0.32f, 0.16f), b.Hull);
		b.Box(Vector3(0, 0, 0), Vector3(0.2f, 0.06f, 0.2f), b.Dark);
		for (int k = 0; k < 8; k++)
		{
			float a = Mathf::PI * 2 * k / 8;
			auto p = Vector3(Mathf::Cos(a) * 0.4f, 0, Mathf::Sin(a) * 0.4f);
			b.Box(p, Vector3(0.12f, 0.035f, 0.12f), k % 2 == 0 ? b.Accent : b.Hull);
		}
		b.Box(Vector3(0.24f, 0, 0), Vector3(0.3f, 0.02f, 0.02f), b.Dark);
		b.Box(Vector3(-0.24f, 0, 0), Vector3(0.3f, 0.02f, 0.02f), b.Dark);
		b.Box(Vector3(0, 0, 0), Vector3(0.02f, 0.02f, 0.78f), b.Dark);
		b.Box(Vector3(0, 0.03f, 0.3f), Vector3(0.1f, 0.01f, 0.02f), b.Glow(Color(1.0f, 0.85f, 0.55f)));
		mesh = b.ToMesh("Platform_" + faction->Prefix);
		PlatformCache()[faction] = mesh;
		return mesh;
	}

	/// <summary>
	/// A hardpoint's model, centred on its mount, built upright (+Y) and facing +Z, in ship lengths.
	/// Turrets come in two meshes: the traversing base and the elevating barrels.
	/// </summary>
	cs::Ref<Mesh> ShipMeshFactory::GetPart(SubsystemKind kind, cs::Ref<Faction> faction, bool barrels)
	{
		faction = Canon(faction);
		cs::Ref<Mesh> mesh;
		if (PartCache().TryGetValue(std::make_tuple(kind, faction, barrels), mesh)) return mesh;
		Builder b(faction, WholeHull);
		switch (kind)
		{
			case SubsystemKind::HeavyLaser:
				if (barrels)
				{
					b.Cylinder(Vector3(-0.011f, 0, 0.04f), 0.0048f, 0.075f, b.Dark);
					b.Cylinder(Vector3(0.011f, 0, 0.04f), 0.0048f, 0.075f, b.Dark);
					b.Box(Vector3(0, 0, 0.004f), Vector3(0.036f, 0.012f, 0.02f), b.Hull);
				}
				else
				{
					b.Box(Vector3(0, 0.01f, 0), Vector3(0.056f, 0.022f, 0.056f), b.Hull);
					b.Box(Vector3(0, 0.023f, -0.01f), Vector3(0.03f, 0.006f, 0.024f), b.Accent);
				}
				break;
			case SubsystemKind::PhaserArray:
				// A phaser array (U.SPC-T488): no barrel to traverse, a long low emitter strip laid along the hull
				// with a glowing amber collimator band down its length, and the beam can leave from any point of it.
				b.Box(Vector3(0, 0.004f, 0), Vector3(0.07f, 0.008f, 0.02f), b.Hull);
				b.Box(Vector3(0, 0.009f, 0), Vector3(0.064f, 0.003f, 0.008f), b.Glow(Color(1.0f, 0.62f, 0.25f)));
				b.Box(Vector3(-0.036f, 0.006f, 0), Vector3(0.006f, 0.01f, 0.024f), b.Dark);
				b.Box(Vector3(0.036f, 0.006f, 0), Vector3(0.006f, 0.01f, 0.024f), b.Dark);
				break;
			case SubsystemKind::PlasmaCannon:
				// A plasma cannon (U.SPC-T488): a squat armoured turret, one short fat barrel ringed by the magnetic
				// bottle's glowing green coils that hold the bolt together until it leaves.
				if (barrels)
				{
					b.Cylinder(Vector3(0, 0, 0.034f), 0.0085f, 0.06f, b.Dark);
					b.Cylinder(Vector3(0, 0, 0.022f), 0.0105f, 0.006f, b.Glow(Color(0.45f, 1.0f, 0.55f)));
					b.Cylinder(Vector3(0, 0, 0.044f), 0.0105f, 0.006f, b.Glow(Color(0.45f, 1.0f, 0.55f)));
					b.Box(Vector3(0, 0, 0.002f), Vector3(0.03f, 0.016f, 0.02f), b.Hull);
				}
				else
				{
					b.Box(Vector3(0, 0.011f, 0), Vector3(0.058f, 0.024f, 0.058f), b.Hull);
					b.Box(Vector3(0, 0.024f, -0.014f), Vector3(0.026f, 0.006f, 0.02f), b.Accent);
				}
				break;
			case SubsystemKind::Railgun:
				// Fixed spinal gun, not a turret: one long rail barrel on a bulky capacitor base,
				// with a glowing coil band where the round gets its final kick.
				b.Box(Vector3(0, 0.014f, -0.01f), Vector3(0.05f, 0.03f, 0.05f), b.Hull);
				b.Cylinder(Vector3(0, 0.014f, 0.06f), 0.009f, 0.14f, b.Dark);
				b.Cylinder(Vector3(0, 0.014f, 0.1f), 0.011f, 0.012f, b.Glow(Color(0.75f, 0.85f, 1.0f)));
				break;
			case SubsystemKind::TachyonLance:
				// A fixed spinal lance (U.SPC-T487): a long armoured emitter housing on a squat capacitor block,
				// ringed by three focusing coils that glow violet, and an open emitter mouth at the bow end.
				b.Box(Vector3(0, 0.018f, -0.03f), Vector3(0.07f, 0.036f, 0.07f), b.Hull);
				b.Box(Vector3(0, 0.038f, -0.03f), Vector3(0.04f, 0.006f, 0.05f), b.Accent);
				b.Cylinder(Vector3(0, 0.018f, 0.07f), 0.014f, 0.2f, b.Dark);
				for (int ring = 0; ring < 3; ring++)
					b.Cylinder(Vector3(0, 0.018f, 0.02f + ring * 0.05f), 0.017f, 0.01f, b.Glow(Color(0.7f, 0.45f, 1.0f)));
				b.Cylinder(Vector3(0, 0.018f, 0.172f), 0.01f, 0.004f, b.Glow(Color(0.9f, 0.8f, 1.0f)));
				break;
			case SubsystemKind::LaserTurret:
				if (barrels)
				{
					b.Cylinder(Vector3(-0.007f, 0, 0.026f), 0.003f, 0.05f, b.Dark);
					b.Cylinder(Vector3(0.007f, 0, 0.026f), 0.003f, 0.05f, b.Dark);
				}
				else b.Box(Vector3(0, 0.007f, 0), Vector3(0.036f, 0.015f, 0.036f), b.Hull);
				break;
			case SubsystemKind::PointDefense:
				if (barrels) b.Cylinder(Vector3(0, 0, 0.015f), 0.0018f, 0.028f, b.Dark);
				else b.Box(Vector3(0, 0.005f, 0), Vector3(0.016f, 0.012f, 0.016f), b.Light);
				break;
			case SubsystemKind::MissileLauncher:
				b.Box(Vector3(0, 0.008f, 0), Vector3(0.03f, 0.018f, 0.05f), b.Hull);
				for (int r = 0; r < 2; r++)
					for (int c = 0; c < 2; c++)
						b.Box(Vector3(-0.007f + c * 0.014f, 0.004f + r * 0.009f, 0.026f), Vector3(0.008f, 0.006f, 0.003f), b.Glow(Color(1.0f, 0.55f, 0.3f)));
				break;
			case SubsystemKind::TorpedoTube:
				// A fixed tube (U.SPC-T377): an armoured breech housing with a wide muzzle and a glowing door rim.
				b.Box(Vector3(0, 0.008f, -0.012f), Vector3(0.03f, 0.016f, 0.04f), b.Hull);
				b.Cylinder(Vector3(0, 0.008f, 0.018f), 0.009f, 0.03f, b.Dark);
				b.Cylinder(Vector3(0, 0.008f, 0.034f), 0.0105f, 0.003f, b.Glow(Color(1.0f, 0.45f, 0.25f)));
				break;
			case SubsystemKind::AntimatterTorpedo:
				// An antimatter launcher (U.SPC-T489): a heavier tube than a torpedo's, ringed by violet containment coils.
				b.Box(Vector3(0, 0.01f, -0.014f), Vector3(0.036f, 0.02f, 0.044f), b.Hull);
				b.Cylinder(Vector3(0, 0.01f, 0.02f), 0.011f, 0.034f, b.Dark);
				b.Cylinder(Vector3(0, 0.01f, 0.006f), 0.0125f, 0.004f, b.Glow(Color(0.75f, 0.4f, 1.0f)));
				b.Cylinder(Vector3(0, 0.01f, 0.026f), 0.0125f, 0.004f, b.Glow(Color(0.75f, 0.4f, 1.0f)));
				break;
			case SubsystemKind::FlakCannon:
				// A rotary flak cannon (U.SPC-T489): a squat turret and a cluster of six short barrels round a hub.
				if (barrels)
				{
					b.Cylinder(Vector3(0, 0, 0.012f), 0.004f, 0.012f, b.Accent);
					for (int k = 0; k < 6; k++)
					{
						float a = Mathf::PI * 2 * k / 6;
						b.Cylinder(Vector3(Mathf::Cos(a) * 0.0045f, Mathf::Sin(a) * 0.0045f, 0.024f), 0.0014f, 0.034f, b.Dark);
					}
				}
				else b.Box(Vector3(0, 0.006f, 0), Vector3(0.024f, 0.014f, 0.024f), b.Hull);
				break;
			case SubsystemKind::VulcanCannon:
				// A vulcan rotary cannon (US-T101): a small turret and a long six-barrel cluster round a hub, the drum
				// feed behind it.
				if (barrels)
				{
					b.Cylinder(Vector3(0, 0, 0.008f), 0.0045f, 0.016f, b.Accent);
					for (int k = 0; k < 6; k++)
					{
						float a = Mathf::PI * 2 * k / 6;
						b.Cylinder(Vector3(Mathf::Cos(a) * 0.0035f, Mathf::Sin(a) * 0.0035f, 0.03f), 0.0011f, 0.046f, b.Dark);
					}
				}
				else
				{
					b.Box(Vector3(0, 0.006f, 0), Vector3(0.022f, 0.013f, 0.024f), b.Hull);
					b.Box(Vector3(0, 0.008f, -0.016f), Vector3(0.016f, 0.012f, 0.01f), b.Dark);
				}
				break;
			case SubsystemKind::HiveLauncher:
				// A hive launcher (US-T101): a squat box with four big cells, their hatches lit amber.
				b.Box(Vector3(0, 0.008f, 0), Vector3(0.03f, 0.016f, 0.036f), b.Hull);
				for (int r = 0; r < 2; r++)
					for (int c = 0; c < 2; c++)
						b.Box(Vector3(-0.007f + c * 0.014f, 0.0165f, -0.008f + r * 0.016f), Vector3(0.011f, 0.002f, 0.012f), b.Glow(Color(1.0f, 0.72f, 0.28f)));
				break;
			case SubsystemKind::RocketPod:
				// A rocket pod (US-T101): a short round pod, its face a honeycomb of tubes.
				b.Cylinder(Vector3(0, 0.008f, 0), 0.008f, 0.034f, b.Hull);
				for (int k = 0; k < 6; k++)
				{
					float a = Mathf::PI * 2 * k / 6;
					b.Box(Vector3(Mathf::Cos(a) * 0.0045f, 0.008f + Mathf::Sin(a) * 0.0045f, 0.0175f), Vector3(0.0025f, 0.0025f, 0.002f), b.Dark);
				}
				break;
			case SubsystemKind::InterceptorMissile:
				// An interceptor launcher (U.SPC-T489): a box of vertical cells, their hatches lit pale blue.
				b.Box(Vector3(0, 0.006f, 0), Vector3(0.026f, 0.012f, 0.026f), b.Hull);
				for (int r = 0; r < 3; r++)
					for (int c = 0; c < 3; c++)
						b.Box(Vector3(-0.008f + c * 0.008f, 0.0125f, -0.008f + r * 0.008f), Vector3(0.005f, 0.002f, 0.005f), b.Glow(Color(0.6f, 0.85f, 1.0f)));
				break;
			case SubsystemKind::ShieldGenerator:
				b.Cylinder(Vector3(0, 0.008f, 0), 0.008f, 0.016f, b.Dark);
				b.Box(Vector3(0, 0.018f, 0), Vector3(0.03f, 0.004f, 0.03f), b.Glow(Color(0.4f, 0.75f, 1.0f)));
				break;
			case SubsystemKind::Bridge:
				b.Box(Vector3(0, 0.02f, 0), Vector3(0.004f, 0.04f, 0.004f), b.Light);
				b.Box(Vector3(0, 0.035f, 0), Vector3(0.024f, 0.003f, 0.003f), b.Light);
				b.Box(Vector3(0, 0.005f, 0.012f), Vector3(0.02f, 0.006f, 0.004f), b.Glow(Color(0.75f, 0.9f, 1.0f)));
				break;
			case SubsystemKind::Hangar:
				b.Box(Vector3(0, 0, 0), Vector3(0.05f, 0.014f, 0.006f), b.Glow(Color(1.0f, 0.85f, 0.55f)));
				break;
			case SubsystemKind::Radiator:
				// A radiator panel (U.SPC-T491): a broad thin sheet spread flat to the sky across the flank, on a
				// stub boom, its coolant channels glowing a dull red.
				b.Box(Vector3(0, 0, 0), Vector3(0.11f, 0.003f, 0.07f), Color(0.78f, 0.8f, 0.82f, 1.0f));
				b.Box(Vector3(0, 0.002f, 0), Vector3(0.012f, 0.004f, 0.075f), b.Dark);
				for (int k = 0; k < 4; k++)
					b.Box(Vector3(-0.045f + k * 0.03f, 0.0018f, 0), Vector3(0.004f, 0.0008f, 0.064f), b.Glow(Color(1.0f, 0.35f, 0.18f)));
				break;
			default:
				b.Box(Vector3::zero(), Vector3(0.01f, 0.01f, 0.01f), b.Dark);
				break;
		}
		mesh = b.ToMesh("Part_" + cs::Str(kind) + (barrels ? "_Barrels" : "") + "_" + faction->Prefix);
		PartCache()[std::make_tuple(kind, faction, barrels)] = mesh;
		return mesh;
	}

	/// <summary>
	/// A spinal mount's model (U.SPC-T490): a gun as long as the hull, laid down the keel from its muzzle at the mount
	/// (+Z, at the bow) back to its breech and power couplings near the stern, in ship lengths. A railgun is a pair of
	/// rails between armoured braces, a coilgun a barrel ringed by coils along its length, a plasma spinal cannon a fat
	/// bottle ringed in green, and a tachyon lance an emitter tube ringed in violet.
	/// </summary>
	cs::Ref<Mesh> ShipMeshFactory::GetSpinal(SubsystemKind kind, cs::Ref<Faction> faction)
	{
		faction = Canon(faction);
		cs::Ref<Mesh> mesh;
		if (SpinalCache().TryGetValue(std::make_tuple(kind, faction), mesh)) return mesh;
		Builder b(faction, WholeHull);
		constexpr float length = 0.8f, back = -length * 0.5f;
		// The armoured trunk the gun runs in, and its breech block astern.
		b.Box(Vector3(0, 0, back), Vector3(0.03f, 0.024f, length), b.Hull);
		b.Box(Vector3(0, 0, -length + 0.03f), Vector3(0.05f, 0.04f, 0.06f), b.Accent);
		switch (kind)
		{
			case SubsystemKind::Railgun:
				b.Box(Vector3(-0.008f, 0.014f, back), Vector3(0.005f, 0.008f, length), b.Dark);
				b.Box(Vector3(0.008f, 0.014f, back), Vector3(0.005f, 0.008f, length), b.Dark);
				for (int i = 0; i < 6; i++)
					b.Box(Vector3(0, 0.014f, -0.04f - i * 0.12f), Vector3(0.026f, 0.012f, 0.008f), b.Hull);
				b.Box(Vector3(0, 0.014f, -0.004f), Vector3(0.02f, 0.004f, 0.006f), b.Glow(Color(0.75f, 0.85f, 1.0f)));
				break;
			case SubsystemKind::Coilgun:
				b.Cylinder(Vector3(0, 0.012f, back), 0.009f, length, b.Dark);
				for (int i = 0; i < 8; i++)
					b.Cylinder(Vector3(0, 0.012f, -0.03f - i * 0.095f), 0.013f, 0.012f, b.Glow(Color(0.55f, 0.8f, 1.0f)));
				break;
			case SubsystemKind::PlasmaCannon:
				b.Cylinder(Vector3(0, 0.014f, back), 0.014f, length, b.Dark);
				for (int i = 0; i < 4; i++)
					b.Cylinder(Vector3(0, 0.014f, -0.05f - i * 0.18f), 0.018f, 0.014f, b.Glow(Color(0.45f, 1.0f, 0.55f)));
				break;
			default:
				b.Cylinder(Vector3(0, 0.014f, back), 0.011f, length, b.Dark);
				for (int i = 0; i < 5; i++)
					b.Cylinder(Vector3(0, 0.014f, -0.03f - i * 0.15f), 0.015f, 0.01f, b.Glow(Color(0.7f, 0.45f, 1.0f)));
				b.Cylinder(Vector3(0, 0.014f, -0.002f), 0.008f, 0.004f, b.Glow(Color(0.9f, 0.8f, 1.0f)));
				break;
		}
		mesh = b.ToMesh("Spinal_" + cs::Str(kind) + "_" + faction->Prefix);
		SpinalCache()[std::make_tuple(kind, faction)] = mesh;
		return mesh;
	}

	/// <summary>
	/// A boarding pod: an armoured cylinder with a fluted drill tip on +Z, one unit long. Stuck in a
	/// hull, the tip is buried and the tail and its thruster bells stand out of the plating.
	/// </summary>
	cs::Ref<Mesh> ShipMeshFactory::GetPod(cs::Ref<Faction> faction)
	{
		faction = Canon(faction);
		cs::Ref<Mesh> mesh;
		if (PodCache().TryGetValue(faction, mesh)) return mesh;
		Builder b(faction, WholeHull);
		b.Cylinder(Vector3(0, 0, -0.1f), 0.16f, 0.6f, b.Dark, false, 10);
		b.Frustum(0.2f, 0.5f, Vector2(0.3f, 0.3f), Vector2(0.02f, 0.02f), 0, Color(0.55f, 0.52f, 0.48f, 1.0f));
		for (float a : { 0.0f, 1.57f, 3.14f, 4.71f })
			b.Box(Vector3(Mathf::Cos(a) * 0.12f, Mathf::Sin(a) * 0.12f, 0.3f), Vector3(0.04f, 0.04f, 0.25f), Color(0.4f, 0.38f, 0.35f, 1.0f));
		b.Box(Vector3(0, 0, -0.1f), Vector3(0.34f, 0.04f, 0.08f), b.Accent);
		b.Nozzle(Vector3(0.07f, 0, -0.42f), 0.05f, 0.06f);
		b.Nozzle(Vector3(-0.07f, 0, -0.42f), 0.05f, 0.06f);
		mesh = b.ToMesh("Pod_" + faction->Prefix);
		PodCache()[faction] = mesh;
		return mesh;
	}

	/// <summary>A carrier's launched craft, drawn as its own hull class (U.SPC-T484): an interceptor, a space
	/// superiority fighter, a bomber or a gunship as the hull of that class; anything else as a swept-wing
	/// carrier fighter with twin guns.</summary>
	cs::Ref<Mesh> ShipMeshFactory::GetFighter(cs::Ref<Faction> faction, ShipClass cls)
	{
		faction = Canon(faction);
		cs::Ref<Mesh> mesh;
		if (FighterCache().TryGetValue(std::make_tuple(faction, cls), mesh)) return mesh;
		Builder b(faction, WholeHull);
		switch (cls)
		{
			case ShipClass::Interceptor: BuildInterceptor(b); break;
			case ShipClass::Fighter: BuildFighter(b); break;
			case ShipClass::Bomber: BuildBomber(b); break;
			case ShipClass::Gunship: BuildGunship(b); break;
			case ShipClass::Recon: BuildRecon(b); break;
			case ShipClass::AssaultShuttle: BuildShuttle(b); break;
			default:
				b.Frustum(-0.45f, 0.5f, Vector2(0.16f, 0.12f), Vector2(0.02f, 0.02f), 0, b.Hull);
				b.Frustum(-0.35f, 0.05f, Vector2(0.95f, 0.025f), Vector2(0.12f, 0.02f), 0, b.Accent);
				b.Box(Vector3(0.22f, 0, 0.15f), Vector3(0.02f, 0.02f, 0.4f), b.Dark);
				b.Box(Vector3(-0.22f, 0, 0.15f), Vector3(0.02f, 0.02f, 0.4f), b.Dark);
				b.Box(Vector3(0, 0.06f, 0.12f), Vector3(0.07f, 0.04f, 0.16f), b.Glow(Color(0.6f, 0.85f, 1.0f)));
				b.Nozzle(Vector3(0, 0, -0.47f), 0.06f, 0.06f);
				break;
		}
		mesh = b.ToMesh("Craft_" + cs::Str(cls) + "_" + faction->Prefix);
		FighterCache()[std::make_tuple(faction, cls)] = mesh;
		return mesh;
	}

	/// <summary>A blackened stub left where a hardpoint was shot away.</summary>
	cs::Ref<Mesh> ShipMeshFactory::GetScar()
	{
		if (scar != nullptr) return scar;
		Builder b(nullptr, WholeHull);
		auto burnt = Color(0.07f, 0.06f, 0.06f, 1.0f);
		b.Box(Vector3(0, 0.003f, 0), Vector3(0.03f, 0.006f, 0.03f), burnt);
		b.Box(Vector3(0.006f, 0.008f, 0.004f), Vector3(0.012f, 0.008f, 0.006f), burnt);
		b.Box(Vector3(-0.008f, 0.006f, -0.006f), Vector3(0.008f, 0.01f, 0.01f), Color(0.12f, 0.08f, 0.05f, 1.0f));
		scar = b.ToMesh("Scar");
		return scar;
	}

	// ---------------------------------------------------------------- orbital structures

	cs::Ref<Mesh> ShipMeshFactory::Cached(const cs::String& key, cs::Ref<Faction> faction, const cs::Action<Builder&>& build)
	{
		faction = Canon(faction);
		cs::Ref<Mesh> mesh;
		if (StructureCache().TryGetValue(std::make_tuple(key, faction), mesh)) return mesh;
		Builder b(faction, WholeHull);
		build(b);
		mesh = b.ToMesh(key + "_" + (faction != nullptr ? faction->Prefix : cs::String("none")));
		StructureCache()[std::make_tuple(key, faction)] = mesh;
		return mesh;
	}

	/// <summary>A ring of boxes standing in for a torus, in the XY plane (axis +Z), radius r.</summary>
	void ShipMeshFactory::Torus(Builder& b, float r, float thickness, float depth, int segments, Color color, float z)
	{
		for (int k = 0; k < segments; k++)
		{
			float a = Mathf::PI * 2 * (k + 0.5f) / segments;
			float chord = 2 * r * Mathf::Sin(Mathf::PI / segments) * 1.04f;
			auto c = Vector3(Mathf::Cos(a) * r, Mathf::Sin(a) * r, z);
			b.BoxRotated(c, Vector3(chord, thickness, depth), a * Mathf::Rad2Deg + 90, color);
		}
	}

	/// <summary>A shipyard: an open lattice cradle along +Z with a control block and gantries (the hull is drawn inside it).</summary>
	cs::Ref<Mesh> ShipMeshFactory::GetShipyard(cs::Ref<Faction> faction)
	{
		return Cached("Shipyard", faction, [](Builder& b)
		{
			auto steel = Color(0.5f, 0.52f, 0.55f, 1.0f);
			for (float x : { -0.26f, 0.26f })
				for (float y : { -0.17f, 0.17f })
					b.Box(Vector3(x, y, 0), Vector3(0.025f, 0.025f, 1.0f), steel);
			for (int i = 0; i <= 8; i++)
			{
				float z = -0.5f + i * 0.125f;
				b.Box(Vector3(0, 0.17f, z), Vector3(0.54f, 0.02f, 0.02f), steel);
				b.Box(Vector3(0, -0.17f, z), Vector3(0.54f, 0.02f, 0.02f), steel);
				b.Box(Vector3(0.26f, 0, z), Vector3(0.02f, 0.36f, 0.02f), steel);
				b.Box(Vector3(-0.26f, 0, z), Vector3(0.02f, 0.36f, 0.02f), steel);
			}
			b.Box(Vector3(0.36f, 0.12f, -0.42f), Vector3(0.16f, 0.14f, 0.2f), b.Hull);
			b.Box(Vector3(0.36f, 0.12f, -0.31f), Vector3(0.12f, 0.03f, 0.02f), b.Glow(Color(1.0f, 0.85f, 0.55f)));
			b.Box(Vector3(0, 0.21f, 0.1f), Vector3(0.6f, 0.05f, 0.06f), b.Accent);
			b.Box(Vector3(0, 0.21f, -0.2f), Vector3(0.6f, 0.05f, 0.06f), b.Accent);
			for (int i = 0; i < 6; i++)
				b.Box(Vector3(i % 2 == 0 ? 0.27f : -0.27f, 0, -0.45f + i * 0.18f), Vector3(0.03f, 0.03f, 0.03f), b.Glow(Color(1.0f, 0.6f, 0.3f)));
		});
	}

	/// <summary>One rib of a hull being framed out on a slip (a hull is about 0.14 wide at this scale).</summary>
	cs::Ref<Mesh> ShipMeshFactory::GetSkeletonRib()
	{
		return Cached("Rib", nullptr, [](Builder& b)
		{
			auto steel = Color(0.62f, 0.6f, 0.56f, 1.0f);
			b.Box(Vector3(0, 0.05f, 0), Vector3(0.14f, 0.012f, 0.012f), steel);
			b.Box(Vector3(0, -0.05f, 0), Vector3(0.14f, 0.012f, 0.012f), steel);
			b.Box(Vector3(0.07f, 0, 0), Vector3(0.012f, 0.1f, 0.012f), steel);
			b.Box(Vector3(-0.07f, 0, 0), Vector3(0.012f, 0.1f, 0.012f), steel);
			b.Box(Vector3(0, 0, 0), Vector3(0.02f, 0.02f, 0.09f), Color(0.45f, 0.43f, 0.4f, 1.0f));
		});
	}

	/// <summary>A factory station: a pressurised core, an industry-coloured process block and big radiator wings.</summary>
	cs::Ref<Mesh> ShipMeshFactory::GetFactory(cs::Ref<Faction> faction, IndustryKind industry)
	{
		return Cached("Factory" + cs::Str(industry), faction, [industry](Builder& b)
		{
			Color heat;
			switch (industry)
			{
				case IndustryKind::Smelter: heat = Color(1.0f, 0.5f, 0.2f); break;
				case IndustryKind::ElectronicsFab: heat = Color(0.4f, 0.75f, 1.0f); break;
				case IndustryKind::MunitionsPlant: heat = Color(1.0f, 0.3f, 0.25f); break;
				case IndustryKind::PharmaLab: heat = Color(0.5f, 1.0f, 0.6f); break;
				default: heat = Color(1.0f, 0.85f, 0.5f); break;
			}
			b.Cylinder(Vector3(0, 0, 0), 0.12f, 0.8f, b.Hull, false, 10);
			b.Box(Vector3(0, 0, 0.12f), Vector3(0.34f, 0.3f, 0.26f), b.Dark);
			b.Box(Vector3(0, 0.16f, 0.12f), Vector3(0.22f, 0.02f, 0.2f), b.Glow(heat));
			b.Box(Vector3(0, 0, -0.2f), Vector3(0.2f, 0.2f, 0.18f), b.Accent);
			auto radiator = Color(0.78f, 0.8f, 0.82f, 1.0f);
			for (float s : { -1.0f, 1.0f })
			{
				b.Box(Vector3(s * 0.42f, 0, -0.05f), Vector3(0.55f, 0.01f, 0.36f), radiator);
				b.Box(Vector3(s * 0.15f, 0, -0.05f), Vector3(0.1f, 0.03f, 0.03f), b.Dark);
			}
			b.Box(Vector3(0, 0, 0.44f), Vector3(0.1f, 0.1f, 0.06f), b.Dark);
			b.Box(Vector3(0, 0, 0.47f), Vector3(0.05f, 0.05f, 0.01f), b.Glow(Color(1.0f, 0.9f, 0.7f)));
		});
	}

	/// <summary>A habitat: a spinning ring on four spokes round a docking hub (axis +Z).</summary>
	cs::Ref<Mesh> ShipMeshFactory::GetHabitat(cs::Ref<Faction> faction)
	{
		return Cached("Habitat", faction, [](Builder& b)
		{
			Torus(b, 0.44f, 0.08f, 0.14f, 24, b.Hull);
			Torus(b, 0.44f, 0.02f, 0.15f, 24, b.Glow(Color(1.0f, 0.92f, 0.75f)));
			for (int k = 0; k < 4; k++)
			{
				float a = Mathf::PI * 0.5f * k;
				b.BoxRotated(Vector3(Mathf::Cos(a) * 0.22f, Mathf::Sin(a) * 0.22f, 0), Vector3(0.44f, 0.025f, 0.025f), a * Mathf::Rad2Deg, b.Dark);
			}
			b.Cylinder(Vector3::zero(), 0.07f, 0.5f, b.Accent, false, 10);
			b.Box(Vector3(0, 0, 0.27f), Vector3(0.05f, 0.05f, 0.03f), b.Glow(Color(1.0f, 0.85f, 0.55f)));
		});
	}

	/// <summary>A free-flying station (Rigil Gate, Toliman Forge, Blackhollow): a long spine with twin habitat rings, docks and arrays. Radius one.</summary>
	cs::Ref<Mesh> ShipMeshFactory::GetStationBody(cs::String name)
	{
		return Cached("StationBody" + name, nullptr, [name](Builder& b)
		{
			bool derelict = name == "Blackhollow";
			auto hull = derelict ? Color(0.32f, 0.3f, 0.3f, 1.0f) : Color(0.7f, 0.71f, 0.74f, 1.0f);
			auto trim = derelict ? Color(0.5f, 0.15f, 0.25f, 1.0f) : name == "Toliman Forge" ? Color(1.0f, 0.6f, 0.25f, 1.0f) : Color(1.0f, 0.78f, 0.3f, 1.0f);
			b.Cylinder(Vector3::zero(), 0.09f, 1.9f, hull, false, 12);
			for (float z : { -0.45f, 0.45f })
			{
				Torus(b, 0.75f, 0.1f, 0.16f, 32, hull, z);
				Torus(b, 0.75f, 0.03f, 0.17f, 32, derelict ? b.Dark : b.Glow(Color(1.0f, 0.9f, 0.7f)), z);
				for (int k = 0; k < 3; k++)
				{
					float a = Mathf::PI * 2 * k / 3 + z;
					auto c = Vector3(Mathf::Cos(a) * 0.38f, Mathf::Sin(a) * 0.38f, z);
					b.BoxRotated(c, Vector3(0.68f, 0.04f, 0.04f), a * Mathf::Rad2Deg, trim);
				}
			}
			b.Box(Vector3(0, 0, 0), Vector3(0.34f, 0.34f, 0.4f), trim);
			for (float s : { -1.0f, 1.0f })
			{
				b.Box(Vector3(0, s * 0.6f, -0.95f), Vector3(0.05f, 1.0f, 0.05f), b.Dark);
				b.Box(Vector3(0, s * 0.75f, -0.95f), Vector3(0.45f, 0.7f, 0.01f), Color(0.12f, 0.16f, 0.3f, 1.0f));
			}
			for (int k = 0; k < 6; k++)
			{
				float a = Mathf::PI * 2 * k / 6;
				b.BoxRotated(Vector3(Mathf::Cos(a) * 0.2f, Mathf::Sin(a) * 0.2f, 0.95f), Vector3(0.24f, 0.03f, 0.03f), a * Mathf::Rad2Deg, b.Dark);
				b.Box(Vector3(Mathf::Cos(a) * 0.32f, Mathf::Sin(a) * 0.32f, 0.95f), Vector3(0.03f, 0.03f, 0.03f), b.Glow(Color(0.6f, 0.9f, 1.0f)));
			}
		});
	}

	/// <summary>
	/// A docking ring: a broad torus (axis +Z, the way ships pass through to the atmosphere) with
	/// docking arms round its rim and approach lights on its inner edge. One unit across.
	/// </summary>
	cs::Ref<Mesh> ShipMeshFactory::GetDockingRing(cs::Ref<Faction> faction)
	{
		return Cached("DockingRing", faction, [](Builder& b)
		{
			auto hull = Color(0.72f, 0.73f, 0.76f, 1.0f);
			Torus(b, 0.42f, 0.1f, 0.12f, 40, hull);
			Torus(b, 0.36f, 0.02f, 0.13f, 40, b.Glow(Color(0.55f, 0.9f, 1.0f)));
			Torus(b, 0.48f, 0.03f, 0.08f, 40, b.Accent);
			for (int k = 0; k < 8; k++)
			{
				float a = Mathf::PI * 2 * k / 8;
				auto c = Vector3(Mathf::Cos(a) * 0.56f, Mathf::Sin(a) * 0.56f, 0);
				b.BoxRotated(c, Vector3(0.14f, 0.035f, 0.05f), a * Mathf::Rad2Deg, b.Dark);
				b.Box(Vector3(Mathf::Cos(a) * 0.64f, Mathf::Sin(a) * 0.64f, 0), Vector3(0.025f, 0.025f, 0.06f), b.Glow(Color(1.0f, 0.4f, 0.3f)));
			}
		});
	}

	/// <summary>The ISS or Tiangong: a truss with modules and solar arrays (one unit across).</summary>
	cs::Ref<Mesh> ShipMeshFactory::GetCrewedStation(bool tiangong)
	{
		return Cached(tiangong ? "Tiangong" : "ISS", nullptr, [tiangong](Builder& b)
		{
			auto white = Color(0.86f, 0.86f, 0.84f, 1.0f);
			if (tiangong)
			{
				b.Cylinder(Vector3::zero(), 0.07f, 0.55f, white);
				b.Box(Vector3(0, 0, 0.2f), Vector3(0.5f, 0.11f, 0.11f), white);
				for (float s : { -1.0f, 1.0f })
					b.Box(Vector3(s * 0.55f, 0, 0.2f), Vector3(0.45f, 0.01f, 0.13f), Color(0.15f, 0.2f, 0.42f, 1.0f));
				return;
			}
			auto panel = Color(0.55f, 0.42f, 0.18f, 1.0f);
			b.Box(Vector3::zero(), Vector3(1.0f, 0.04f, 0.04f), Color(0.6f, 0.6f, 0.6f, 1.0f));
			b.Cylinder(Vector3(0, 0, 0.05f), 0.05f, 0.45f, white);
			b.Box(Vector3(0, 0, -0.05f), Vector3(0.18f, 0.08f, 0.08f), white);
			for (float x : { -0.46f, -0.32f, 0.32f, 0.46f })
				for (float s : { -1.0f, 1.0f })
					b.Box(Vector3(x, 0, s * 0.2f), Vector3(0.1f, 0.006f, 0.34f), panel);
			b.Box(Vector3(0.1f, 0.12f, 0), Vector3(0.12f, 0.2f, 0.005f), white);
		});
	}

	/// <summary>A telescope (Hubble's tube, or JWST's gold mirror over its sunshield) or a robotic probe.</summary>
	cs::Ref<Mesh> ShipMeshFactory::GetSpacecraft(cs::String name)
	{
		return Cached("Craft" + (name == "JWST" || name == "Hubble" ? name : cs::String("Probe")), nullptr, [name](Builder& b)
		{
			if (name == "JWST")
			{
				auto gold = Color(1.0f, 0.78f, 0.3f, 1.0f);
				for (int k = 0; k < 6; k++)
				{
					float a = Mathf::PI * 2 * k / 6;
					b.BoxRotated(Vector3(Mathf::Cos(a) * 0.14f, Mathf::Sin(a) * 0.14f, 0.15f), Vector3(0.16f, 0.14f, 0.01f), a * Mathf::Rad2Deg, gold);
				}
				b.Box(Vector3(0, 0, 0.15f), Vector3(0.12f, 0.12f, 0.01f), gold);
				b.Box(Vector3(0, -0.22f, 0), Vector3(0.9f, 0.01f, 0.5f), Color(0.75f, 0.6f, 0.75f, 1.0f));
				b.Box(Vector3(0, -0.27f, 0), Vector3(0.12f, 0.08f, 0.12f), Color(0.3f, 0.3f, 0.32f, 1.0f));
				return;
			}
			if (name == "Hubble")
			{
				b.Cylinder(Vector3::zero(), 0.16f, 1.0f, Color(0.8f, 0.8f, 0.82f, 1.0f), false, 12);
				for (float s : { -1.0f, 1.0f })
					b.Box(Vector3(s * 0.38f, 0, -0.1f), Vector3(0.45f, 0.01f, 0.18f), Color(0.15f, 0.2f, 0.42f, 1.0f));
				return;
			}
			b.Box(Vector3::zero(), Vector3(0.25f, 0.25f, 0.3f), Color(0.75f, 0.65f, 0.35f, 1.0f));
			b.Cylinder(Vector3(0, 0, 0.22f), 0.2f, 0.03f, Color(0.85f, 0.85f, 0.85f, 1.0f), false, 12);
			for (float s : { -1.0f, 1.0f })
				b.Box(Vector3(s * 0.45f, 0, 0), Vector3(0.6f, 0.01f, 0.22f), Color(0.15f, 0.2f, 0.42f, 1.0f));
		});
	}

	void ShipMeshFactory::BuildBattleship(Builder& b)
	{
		b.Frustum(-0.42f, 0.30f, Vector2(0.15f, 0.085f), Vector2(0.11f, 0.06f), 0, b.Hull);
		b.Frustum(0.30f, 0.50f, Vector2(0.11f, 0.06f), Vector2(0.018f, 0.016f), 0, b.Hull);
		b.Frustum(-0.36f, 0.26f, Vector2(0.05f, 0.03f), Vector2(0.03f, 0.02f), -0.055f, b.Dark);
		for (float side : { -1.0f, 1.0f })
		{
			b.Box(Vector3(side * 0.071f, 0.0f, -0.05f), Vector3(0.008f, 0.022f, 0.6f), b.Accent);
			b.Box(Vector3(side * 0.06f, 0.035f, -0.02f), Vector3(0.03f, 0.012f, 0.035f), b.Hull);
		}
		b.Box(Vector3(0, 0.065f, -0.12f), Vector3(0.06f, 0.05f, 0.14f), b.Hull);
		b.Box(Vector3(0, 0.098f, -0.1f), Vector3(0.09f, 0.018f, 0.035f), b.Dark);
		// Turret barbettes: the turrets themselves are parts.
		b.Box(Vector3(0, 0.038f, 0.20f), Vector3(0.05f, 0.01f, 0.05f), b.Dark);
		b.Box(Vector3(0, 0.048f, 0.07f), Vector3(0.05f, 0.03f, 0.05f), b.Hull);
		b.Box(Vector3(0, 0.038f, -0.28f), Vector3(0.05f, 0.01f, 0.05f), b.Dark);
		b.Box(Vector3(0, 0, -0.45f), Vector3(0.13f, 0.075f, 0.06f), b.Dark);
		for (float x : { -0.042f, 0.0f, 0.042f })
			b.Nozzle(Vector3(x, 0, -0.48f), 0.022f, 0.04f);
	}

	/// <summary>A gate breaker (U.SPC-T478): a short, broad, slab-sided siege hull under thick armour belts, its blunt ram
	/// bow banded in plate, the plasma batteries' barbettes down both beams (the cannons themselves are parts) with their
	/// magnetic bottles glowing between them, the shuttle deck's doors under the keel, the siege engineering bay's cranes
	/// and gate-control mast on the dorsal spine, and three big bells astern.</summary>
	void ShipMeshFactory::BuildGateBreaker(Builder& b)
	{
		b.Frustum(-0.44f, 0.34f, Vector2(0.17f, 0.1f), Vector2(0.15f, 0.09f), 0, b.Hull);
		b.Frustum(0.34f, 0.50f, Vector2(0.15f, 0.09f), Vector2(0.07f, 0.05f), 0, b.Hull);
		// The ram bow's armour bands.
		for (float z : { 0.38f, 0.44f })
			b.Box(Vector3(0, 0, z), Vector3(0.14f, 0.085f, 0.015f), b.Dark);
		auto glow = b.Glow(Color(0.75f, 0.55f, 1.0f));
		for (float side : { -1.0f, 1.0f })
		{
			// An armour belt down each beam, and the battery barbettes along it.
			b.Box(Vector3(side * 0.088f, 0.0f, -0.04f), Vector3(0.012f, 0.07f, 0.66f), b.Accent);
			for (float z : { 0.15f, -0.02f, -0.2f })
			{
				b.Box(Vector3(side * 0.085f, 0.03f, z), Vector3(0.03f, 0.016f, 0.045f), b.Dark);
				// The plasma bottle feeding each cannon, lit with its containment.
				b.Cylinder(Vector3(side * 0.07f, 0.05f, z - 0.05f), 0.008f, 0.03f, glow);
			}
			b.Box(Vector3(side * 0.04f, 0.05f, 0.3f), Vector3(0.028f, 0.014f, 0.04f), b.Dark);
		}
		// The siege engineering bay's cranes and gate-control mast on the dorsal spine, plated over amidships where the
		// bridge is buried at the centre (U.SPC-T492).
		b.Box(Vector3(0, 0.07f, -0.05f), Vector3(0.07f, 0.03f, 0.2f), b.Hull);
		b.Box(Vector3(0, 0.09f, -0.26f), Vector3(0.05f, 0.04f, 0.08f), b.Hull);
		b.Box(Vector3(0, 0.14f, -0.26f), Vector3(0.01f, 0.07f, 0.01f), b.Light);
		for (float side : { -1.0f, 1.0f })
			b.Box(Vector3(side * 0.03f, 0.1f, -0.16f), Vector3(0.006f, 0.006f, 0.14f), b.Light);
		// The shuttle deck's doors under the keel.
		b.Box(Vector3(0, -0.047f, -0.05f), Vector3(0.07f, 0.006f, 0.12f), b.Dark);
		b.Box(Vector3(0, 0, -0.46f), Vector3(0.15f, 0.09f, 0.05f), b.Dark);
		for (float x : { -0.045f, 0.0f, 0.045f })
			b.Nozzle(Vector3(x, 0, -0.48f), 0.024f, 0.042f);
	}

	void ShipMeshFactory::BuildCarrier(Builder& b)
	{
		b.Frustum(-0.44f, 0.32f, Vector2(0.2f, 0.07f), Vector2(0.17f, 0.06f), 0, b.Hull);
		b.Frustum(0.32f, 0.50f, Vector2(0.17f, 0.06f), Vector2(0.06f, 0.028f), 0, b.Hull);
		b.Box(Vector3(0, 0.041f, 0.0f), Vector3(0.19f, 0.012f, 0.92f), b.Dark);
		b.Box(Vector3(0, 0.048f, 0.0f), Vector3(0.006f, 0.002f, 0.85f), b.Accent);
		b.Box(Vector3(-0.055f, 0.048f, 0.02f), Vector3(0.003f, 0.002f, 0.7f), b.Light);
		b.Box(Vector3(0.055f, 0.048f, 0.02f), Vector3(0.003f, 0.002f, 0.7f), b.Light);
		b.Box(Vector3(0, 0.012f, 0.455f), Vector3(0.11f, 0.028f, 0.02f), b.Dark);
		for (float side : { -1.0f, 1.0f })
		{
			b.Box(Vector3(side * 0.115f, 0, -0.05f), Vector3(0.03f, 0.05f, 0.5f), b.Hull);
			b.Box(Vector3(side * 0.131f, 0, -0.05f), Vector3(0.004f, 0.012f, 0.45f), b.Accent);
		}
		b.Box(Vector3(0.07f, 0.09f, -0.12f), Vector3(0.035f, 0.08f, 0.13f), b.Hull);
		b.Box(Vector3(0.07f, 0.135f, -0.11f), Vector3(0.05f, 0.015f, 0.05f), b.Dark);
		b.Box(Vector3(0, 0, -0.46f), Vector3(0.18f, 0.065f, 0.05f), b.Dark);
		for (float x : { -0.075f, -0.027f, 0.027f, 0.075f })
			b.Nozzle(Vector3(x, 0, -0.49f), 0.02f, 0.035f);
	}

	void ShipMeshFactory::BuildCruiser(Builder& b)
	{
		b.Frustum(-0.40f, 0.25f, Vector2(0.07f, 0.05f), Vector2(0.06f, 0.045f), 0, b.Hull);
		b.Frustum(0.25f, 0.50f, Vector2(0.12f, 0.05f), Vector2(0.035f, 0.02f), 0, b.Hull);
		b.Box(Vector3(0, 0, -0.1f), Vector3(0.2f, 0.012f, 0.18f), b.Hull);
		for (float side : { -1.0f, 1.0f })
		{
			b.Box(Vector3(side * 0.1f, 0, -0.1f), Vector3(0.014f, 0.018f, 0.19f), b.Accent);
			b.Box(Vector3(side * 0.1f, 0, -0.205f), Vector3(0.02f, 0.02f, 0.03f), b.Dark);
			b.Nozzle(Vector3(side * 0.1f, 0, -0.225f), 0.008f, 0.015f);
		}
		b.Box(Vector3(0, 0.024f, 0.15f), Vector3(0.036f, 0.006f, 0.036f), b.Dark);
		b.Box(Vector3(0, 0.024f, 0.0f), Vector3(0.036f, 0.006f, 0.036f), b.Dark);
		b.Box(Vector3(0, 0.045f, -0.2f), Vector3(0.04f, 0.03f, 0.08f), b.Hull);
		b.Box(Vector3(0, 0.055f, -0.17f), Vector3(0.042f, 0.008f, 0.02f), b.Dark);
		b.Box(Vector3(0, 0, -0.44f), Vector3(0.09f, 0.05f, 0.05f), b.Dark);
		b.Nozzle(Vector3(-0.025f, 0, -0.47f), 0.022f, 0.035f);
		b.Nozzle(Vector3(0.025f, 0, -0.47f), 0.022f, 0.035f);
	}

	/// <summary>A command cruiser (U.SPC-T477): a long battlecruiser's hull with a light battery, the sensor array's big
	/// dish on a dorsal mast forward, the communications suite's antenna spars along both beams and the targeting
	/// computers' fire-control domes amidships, lit in the array's glow.</summary>
	void ShipMeshFactory::BuildCommandCruiser(Builder& b)
	{
		b.Frustum(-0.44f, 0.28f, Vector2(0.13f, 0.065f), Vector2(0.11f, 0.055f), 0, b.Hull);
		b.Frustum(0.28f, 0.50f, Vector2(0.11f, 0.055f), Vector2(0.035f, 0.022f), 0, b.Hull);
		b.Box(Vector3(0, 0.036f, -0.05f), Vector3(0.1f, 0.01f, 0.6f), b.Dark);
		b.Box(Vector3(0, 0.042f, -0.05f), Vector3(0.006f, 0.002f, 0.55f), b.Accent);
		// The array: a mast, and its dish facing forward with a glowing feed horn.
		auto glow = b.Glow(Color(0.55f, 0.95f, 0.85f));
		b.Box(Vector3(0, 0.075f, 0.24f), Vector3(0.016f, 0.07f, 0.03f), b.Hull);
		b.Cylinder(Vector3(0, 0.12f, 0.25f), 0.055f, 0.008f, b.Light, false, 16);
		b.Cylinder(Vector3(0, 0.12f, 0.265f), 0.008f, 0.03f, glow);
		// The fire-control tower amidships, and its domes either side of it: plated over, with no viewports, since the
		// flag bridge is buried at the centre of the hull beneath it (U.SPC-T492).
		b.Box(Vector3(0, 0.06f, 0.05f), Vector3(0.05f, 0.04f, 0.09f), b.Hull);
		for (float side : { -1.0f, 1.0f })
		{
			b.Cylinder(Vector3(side * 0.04f, 0.05f, -0.02f), 0.012f, 0.02f, glow);
			// The communications suite's spars, long and thin along each beam.
			b.Box(Vector3(side * 0.085f, 0.01f, 0.0f), Vector3(0.006f, 0.006f, 0.5f), b.Light);
			b.Box(Vector3(side * 0.085f, 0.01f, 0.27f), Vector3(0.02f, 0.02f, 0.012f), b.Accent);
			b.Box(Vector3(side * 0.07f, 0, -0.2f), Vector3(0.02f, 0.04f, 0.3f), b.Hull);
		}
		b.Box(Vector3(0, 0.03f, 0.2f), Vector3(0.032f, 0.006f, 0.032f), b.Dark);
		b.Box(Vector3(0, 0.03f, -0.3f), Vector3(0.032f, 0.006f, 0.032f), b.Dark);
		b.Box(Vector3(0, 0, -0.45f), Vector3(0.12f, 0.055f, 0.05f), b.Dark);
		for (float x : { -0.04f, 0.0f, 0.04f })
			b.Nozzle(Vector3(x, 0, -0.48f), 0.018f, 0.035f);
	}

	/// <summary>A frigate (U.SPC-T475): a slim, light spine with the two torpedo launchers' glowing mouths in its bow, the
	/// four missile racks paired above and below it forward, and a big four-bell engine block astern that is a third of
	/// the hull.</summary>
	void ShipMeshFactory::BuildFrigate(Builder& b)
	{
		b.Frustum(-0.2f, 0.5f, Vector2(0.07f, 0.045f), Vector2(0.016f, 0.014f), 0, b.Hull);
		// The engine block: wider than the spine, all drive.
		b.Box(Vector3(0, 0, -0.33f), Vector3(0.15f, 0.1f, 0.26f), b.Dark);
		b.Box(Vector3(0, 0.052f, -0.33f), Vector3(0.1f, 0.006f, 0.22f), b.Accent);
		for (float x : { -0.045f, 0.045f })
			for (float y : { -0.025f, 0.025f })
				b.Nozzle(Vector3(x, y, -0.47f), 0.022f, 0.04f);
		// The missile racks, a pair above the spine and a pair below, with their hatches.
		for (float y : { -0.035f, 0.035f })
			for (float x : { -0.06f, 0.06f })
			{
				b.Box(Vector3(x, y, 0.15f), Vector3(0.04f, 0.022f, 0.16f), b.Hull);
				b.Box(Vector3(x, y + Mathf::Sign(y) * 0.012f, 0.15f), Vector3(0.03f, 0.003f, 0.12f), b.Dark);
			}
		// The torpedo launchers' mouths, lit with the antimatter's containment glow.
		auto glow = b.Glow(Color(0.65f, 0.7f, 1.0f));
		for (float x : { -0.03f, 0.03f })
			b.Cylinder(Vector3(x, 0, 0.43f), 0.01f, 0.04f, glow);
		// A sensor housing low on the spine amidships, plated over: the bridge is buried at the centre of the hull
		// beneath it, with no viewports (U.SPC-T492).
		b.Box(Vector3(0, 0.04f, -0.02f), Vector3(0.035f, 0.02f, 0.06f), b.Hull);
	}

	/// <summary>A corvette (U.SPC-T476): a short, stocky patrol hull with no drive room, broad saddle tanks either side for
	/// its weeks on the beat, a spinal light railgun under the nose, a laser turret above and below, a search mast over the
	/// bridge and twin bells astern.</summary>
	void ShipMeshFactory::BuildCorvette(Builder& b)
	{
		b.Frustum(-0.42f, 0.46f, Vector2(0.13f, 0.08f), Vector2(0.03f, 0.025f), 0, b.Hull);
		// The saddle tanks: the patrol's endurance, where a drive room would be.
		for (float side : { -1.0f, 1.0f })
			b.Box(Vector3(side * 0.1f, 0, -0.16f), Vector3(0.07f, 0.07f, 0.34f), b.Accent);
		// The spinal light railgun slung under the nose.
		b.Box(Vector3(0, -0.035f, 0.32f), Vector3(0.025f, 0.022f, 0.3f), b.Dark);
		// The laser turrets, dorsal forward and ventral aft, with their twin barrels.
		for (float y : { 1.0f, -1.0f })
		{
			float z = y > 0 ? 0.15f : -0.1f;
			b.Box(Vector3(0, y * 0.06f, z), Vector3(0.06f, 0.025f, 0.06f), b.Dark);
			for (float side : { -1.0f, 1.0f })
				b.Box(Vector3(side * 0.014f, y * 0.065f, z + 0.06f), Vector3(0.009f, 0.009f, 0.07f), b.Light);
		}
		// The bridge, its glazing and the patrol search mast over it.
		b.Box(Vector3(0, 0.055f, 0.05f), Vector3(0.05f, 0.025f, 0.08f), b.Hull);
		b.Box(Vector3(0, 0.07f, 0.08f), Vector3(0.05f, 0.008f, 0.02f), b.Glow(Color(0.55f, 0.85f, 1.0f)));
		b.Box(Vector3(0, 0.11f, 0.0f), Vector3(0.012f, 0.08f, 0.012f), b.Dark);
		b.Cylinder(Vector3(0, 0.15f, 0.0f), 0.045f, 0.01f, b.Light, false, 10);
		for (float x : { -0.04f, 0.04f })
			b.Nozzle(Vector3(x, 0, -0.47f), 0.025f, 0.045f);
	}

	void ShipMeshFactory::BuildDestroyer(Builder& b)
	{
		b.Frustum(-0.45f, 0.50f, Vector2(0.15f, 0.07f), Vector2(0.02f, 0.018f), 0, b.Hull);
		b.Frustum(-0.42f, 0.30f, Vector2(0.06f, 0.03f), Vector2(0.02f, 0.01f), -0.04f, b.Dark);
		b.Box(Vector3(0, 0.05f, -0.2f), Vector3(0.012f, 0.06f, 0.25f), b.Accent);
		b.Box(Vector3(0, 0.04f, -0.08f), Vector3(0.05f, 0.03f, 0.1f), b.Hull);
		b.Box(Vector3(0, 0.026f, 0.12f), Vector3(0.036f, 0.006f, 0.036f), b.Dark);
		b.Nozzle(Vector3(-0.035f, 0, -0.47f), 0.025f, 0.04f);
		b.Nozzle(Vector3(0.035f, 0, -0.47f), 0.025f, 0.04f);
	}

	/// <summary>
	/// The interdiction field projector's visible part (U.SPC-T507, <see cref="LaneInterdictors"/>): the glowing emitter
	/// ring on its struts, centred on the keel amidships, drawn on whatever hull carries the projector (a destroyer, a
	/// cruiser, a raider's converted freighter). In ship lengths, like the hull it is set on.
	/// </summary>
	cs::Ref<Mesh> ShipMeshFactory::GetFieldProjector(cs::Ref<Faction> faction)
	{
		faction = Canon(faction);
		cs::Ref<Mesh> mesh;
		if (ProjectorCache().TryGetValue(faction, mesh)) return mesh;
		Builder b(faction, WholeHull);
		// The projector ring: segments round the keel, lit with the field's glow.
		constexpr int segments = 14;
		constexpr float ring = 0.17f;
		auto glow = b.Glow(Color(0.55f, 0.75f, 1.0f));
		for (int i = 0; i < segments; i++)
		{
			float a = 360.0f * i / segments;
			float rad = a * Mathf::Deg2Rad;
			auto at = Vector3(Mathf::Cos(rad) * ring, Mathf::Sin(rad) * ring, 0);
			b.BoxRotated(at, Vector3(2 * Mathf::PI * ring / segments * 1.05f, 0.02f, 0.04f), a + 90, i % 2 == 0 ? glow : b.Accent);
		}
		// Four struts from the hull out to the ring.
		for (int i = 0; i < 4; i++)
		{
			float a = 45.0f + 90.0f * i;
			float rad = a * Mathf::Deg2Rad;
			b.BoxRotated(Vector3(Mathf::Cos(rad) * ring * 0.55f, Mathf::Sin(rad) * ring * 0.55f, 0), Vector3(ring * 0.8f, 0.012f, 0.02f), a, b.Hull);
		}
		mesh = b.ToMesh("FieldProjector_" + faction->Prefix);
		ProjectorCache()[faction] = mesh;
		return mesh;
	}

	/// <summary>The stock freighter, fully loaded (for thumbnails and the yard's slips).</summary>
	void ShipMeshFactory::BuildFreighter(Builder& b) { BuildTruss(b, BulkFreighters::StockPods, BulkFreighters::StockPods, BulkFreighters::StockSpheres); }

	/// <summary>A side of the spine: right, left, over, under.</summary>
	Vector2 ShipMeshFactory::SideOf(int place)
	{
		return place == 0 ? Vector2(1, 0) : place == 1 ? Vector2(-1, 0) : place == 2 ? Vector2(0, 1) : Vector2(0, -1);
	}

	/// <summary>
	/// A truss freighter (U.SPC-T482): a skeletal square truss from the cockpit at the bow to the engine astern, a
	/// clamp arm at every pod's place along it, a standard container in the first <paramref name="loaded"/> clamps
	/// (two abreast, or four round the spine on a long truss), and the fuel spheres ringed round the spine ahead of the
	/// drive's shadow shield.
	/// </summary>
	void ShipMeshFactory::BuildTruss(Builder& b, int pods, int loaded, int spheres)
	{
		auto steel = Color(0.46f, 0.47f, 0.5f, 1.0f);
		constexpr float front = 0.36f, back = -0.4f, half = 0.022f, bar = 0.008f;
		// The four longerons and a square frame every so often.
		for (float x : { -half, half })
			for (float y : { -half, half })
				b.Box(Vector3(x, y, (front + back) * 0.5f), Vector3(bar, bar, front - back), steel);
		for (float z = back; z <= front + 1e-3f; z += 0.076f)
		{
			b.Box(Vector3(0, half, z), Vector3(half * 2 + bar, bar, bar), steel);
			b.Box(Vector3(0, -half, z), Vector3(half * 2 + bar, bar, bar), steel);
			b.Box(Vector3(half, 0, z), Vector3(bar, half * 2, bar), steel);
			b.Box(Vector3(-half, 0, z), Vector3(bar, half * 2, bar), steel);
		}

		// The cockpit module at the bow.
		b.Frustum(front, 0.5f, Vector2(0.07f, 0.06f), Vector2(0.035f, 0.03f), 0, b.Hull);
		b.Box(Vector3(0, 0.026f, 0.44f), Vector3(0.04f, 0.01f, 0.025f), b.Glow(Color(0.8f, 0.9f, 1.0f)));
		b.Box(Vector3(0, 0.04f, 0.4f), Vector3(0.012f, 0.02f, 0.012f), b.Accent);

		// The pods: in rows of two abreast (four round the spine past eight), from behind the cockpit back towards the spheres.
		Color paint[] = { Color(0.75f, 0.35f, 0.2f), Color(0.25f, 0.45f, 0.6f), Color(0.7f, 0.65f, 0.3f), b.Hull, Color(0.35f, 0.55f, 0.35f) };
		int abreast = pods > 8 ? 4 : 2;
		int rows = Mathf::Max(1, (pods + abreast - 1) / abreast);
		constexpr float podFront = 0.3f, podBack = -0.14f, reach = 0.068f, size = 0.075f;
		float pitch = rows > 1 ? (podFront - podBack) / (rows - 1) : 0;
		float length = Mathf::Min(0.1f, rows > 1 ? pitch * 0.9f : 0.1f);
		for (int i = 0; i < pods; i++)
		{
			int row = i / abreast;
			float z = rows > 1 ? podFront - pitch * row : (podFront + podBack) * 0.5f;
			auto dir = SideOf(i % abreast);
			// The clamp arm, there whether a pod is in it or not.
			b.Box(Vector3(dir.x * (half + 0.012f), dir.y * (half + 0.012f), z),
				Vector3(dir.x != 0 ? 0.024f : 0.01f, dir.y != 0 ? 0.024f : 0.01f, length * 0.5f), b.Dark);
			if (i >= loaded) continue;
			auto centre = Vector3(dir.x * reach, dir.y * reach, z);
			b.Box(centre, Vector3(size, size, length), paint[(i * 3 + row) % static_cast<int>(std::size(paint))]);
			// The container's end frames.
			b.Box(centre + Vector3(0, 0, length * 0.5f - 0.004f), Vector3(size + 0.004f, size + 0.004f, 0.006f), b.Dark);
			b.Box(centre - Vector3(0, 0, length * 0.5f - 0.004f), Vector3(size + 0.004f, size + 0.004f, 0.006f), b.Dark);
		}

		// The fuel spheres: a ring of up to four round the spine, a second ring ahead of it for more.
		for (int i = 0; i < spheres; i++)
		{
			float z = i < 4 ? -0.27f : -0.19f;
			auto dir = SideOf(i % 4);
			b.Box(Vector3(dir.x * (half + 0.01f), dir.y * (half + 0.01f), z), Vector3(dir.x != 0 ? 0.02f : 0.01f, dir.y != 0 ? 0.02f : 0.01f, 0.02f), b.Dark);
			b.Sphere(Vector3(dir.x * 0.07f, dir.y * 0.07f, z), 0.045f, b.Light);
			b.Box(Vector3(dir.x * 0.07f, dir.y * 0.07f, z), Vector3(0.094f, 0.094f, 0.008f), b.Accent);
		}

		// The drive: a shadow shield, the reactor and engine block, and one big bell.
		b.Box(Vector3(0, 0, -0.355f), Vector3(0.11f, 0.11f, 0.01f), b.Dark);
		b.Box(Vector3(0, 0, -0.42f), Vector3(0.08f, 0.08f, 0.08f), b.Hull);
		b.Nozzle(Vector3(0, 0, -0.475f), 0.04f, 0.05f);
	}

	void ShipMeshFactory::BuildHauler(Builder& b)
	{
		b.Box(Vector3(0, 0, 0), Vector3(0.22f, 0.16f, 0.55f), b.Hull);
		b.Box(Vector3(0, 0.09f, 0.05f), Vector3(0.18f, 0.03f, 0.4f), b.Accent);
		b.Frustum(0.27f, 0.45f, Vector2(0.16f, 0.12f), Vector2(0.08f, 0.06f), 0, b.Dark);
		b.Box(Vector3(0, 0, -0.35f), Vector3(0.18f, 0.12f, 0.15f), b.Dark);
		b.Nozzle(Vector3(-0.05f, 0, -0.45f), 0.04f, 0.05f);
		b.Nozzle(Vector3(0.05f, 0, -0.45f), 0.04f, 0.05f);
	}

	/// <summary>
	/// An ore tug (U.SPC-T483): no hold, just a big engine with claws. A short blocky frame, a cluster of three heavy
	/// nozzles astern, a push plate across the bow, and four jointed claws reaching forward round it to grip a rock.
	/// </summary>
	void ShipMeshFactory::BuildOreTug(Builder& b)
	{
		auto steel = Color(0.55f, 0.53f, 0.49f, 1.0f);
		// The spine and the engine block.
		b.Box(Vector3(0, 0, -0.05f), Vector3(0.16f, 0.14f, 0.5f), b.Hull);
		b.Box(Vector3(0, 0, -0.33f), Vector3(0.3f, 0.2f, 0.2f), b.Dark);
		b.Box(Vector3(0, 0.085f, 0.1f), Vector3(0.08f, 0.04f, 0.1f), b.Accent);
		b.Nozzle(Vector3(-0.09f, 0, -0.45f), 0.05f, 0.06f);
		b.Nozzle(Vector3(0, 0, -0.47f), 0.06f, 0.07f);
		b.Nozzle(Vector3(0.09f, 0, -0.45f), 0.05f, 0.06f);
		// The push plate the load rests against.
		b.Box(Vector3(0, 0, 0.22f), Vector3(0.28f, 0.24f, 0.03f), b.Dark);
		// Four claws round the bow: an arm angled out and forward, then a hooked tip turned in.
		for (int k = 0; k < 4; k++)
		{
			float deg = 45.0f + 90.0f * k, a = deg * Mathf::Deg2Rad;
			auto radial = Vector3(Mathf::Cos(a), Mathf::Sin(a), 0);
			b.BoxRotated(radial * 0.15f + Vector3(0, 0, 0.24f), Vector3(0.04f, 0.035f, 0.06f), deg, b.Accent);
			b.BoxRotated(radial * 0.17f + Vector3(0, 0, 0.36f), Vector3(0.03f, 0.03f, 0.2f), deg, steel);
			b.BoxRotated(radial * 0.13f + Vector3(0, 0, 0.47f), Vector3(0.09f, 0.03f, 0.04f), deg, steel);
		}
	}

	/// <summary>
	/// A capture tug (U.SPC-T522): an ore tug's plan on a lighter frame. A slim spine with a cab on top, a pair of nozzles
	/// astern, a narrow push plate, and three longer, thinner claws reaching well forward to close round a tumbling rock.
	/// </summary>
	void ShipMeshFactory::BuildCaptureTug(Builder& b)
	{
		auto steel = Color(0.6f, 0.58f, 0.54f, 1.0f);
		b.Box(Vector3(0, 0, -0.05f), Vector3(0.12f, 0.1f, 0.55f), b.Hull);
		b.Box(Vector3(0, 0, -0.34f), Vector3(0.22f, 0.14f, 0.16f), b.Dark);
		b.Box(Vector3(0, 0.07f, 0.08f), Vector3(0.07f, 0.05f, 0.12f), b.Accent);
		b.Nozzle(Vector3(-0.06f, 0, -0.46f), 0.04f, 0.05f);
		b.Nozzle(Vector3(0.06f, 0, -0.46f), 0.04f, 0.05f);
		b.Box(Vector3(0, 0, 0.23f), Vector3(0.2f, 0.18f, 0.025f), b.Dark);
		for (int k = 0; k < 3; k++)
		{
			float deg = 90.0f + 120.0f * k, a = deg * Mathf::Deg2Rad;
			auto radial = Vector3(Mathf::Cos(a), Mathf::Sin(a), 0);
			b.BoxRotated(radial * 0.11f + Vector3(0, 0, 0.25f), Vector3(0.03f, 0.03f, 0.05f), deg, b.Accent);
			b.BoxRotated(radial * 0.13f + Vector3(0, 0, 0.38f), Vector3(0.022f, 0.022f, 0.24f), deg, steel);
			b.BoxRotated(radial * 0.09f + Vector3(0, 0, 0.5f), Vector3(0.07f, 0.022f, 0.03f), deg, steel);
		}
	}

	/// <summary>
	/// What an ore tug pushes (U.SPC-T498, <see cref="Ship.Tow"/>): a lumpy boulder one unit across, centred, made of a big
	/// faceted body and a few smaller knobs fused into it -- dark rock, or pale blue-white comet ice.
	/// </summary>
	cs::Ref<Mesh> ShipMeshFactory::GetTowLoad(bool ice)
	{
		auto cached = ice ? towIce : towRock;
		if (cached != nullptr) return cached;
		Builder b(nullptr, WholeHull);
		auto main = ice ? Color(0.78f, 0.86f, 0.93f, 1.0f) : Color(0.36f, 0.33f, 0.3f, 1.0f);
		auto knob = ice ? Color(0.66f, 0.78f, 0.9f, 1.0f) : Color(0.28f, 0.26f, 0.24f, 1.0f);
		b.Sphere(Vector3::zero(), 0.42f, main, 7, 11);
		b.Sphere(Vector3(0.18f, 0.12f, 0.1f), 0.24f, knob, 5, 8);
		b.Sphere(Vector3(-0.16f, -0.1f, 0.14f), 0.22f, main, 5, 8);
		b.Sphere(Vector3(0.05f, -0.2f, -0.18f), 0.2f, knob, 5, 8);
		b.Sphere(Vector3(-0.12f, 0.2f, -0.12f), 0.18f, main, 5, 8);
		auto mesh = b.ToMesh(ice ? "TowIce" : "TowRock");
		if (ice) towIce = mesh;
		else towRock = mesh;
		return mesh;
	}

	/// <summary>
	/// A Breacher round (U.SPC-T466, U.SPC-T517): the bullet-shaped armoured round a Breacher bay fires, its team sealed
	/// in the casing, a fluted drill head for a nose and the steering thrusters' nozzles at its base. Its own model, not
	/// a ship's: drawn in flight and stuck in the hull it struck. One mesh per faction, built once.
	/// </summary>
	cs::Ref<Mesh> ShipMeshFactory::GetBreacherRound(cs::Ref<Faction> faction)
	{
		faction = Canon(faction);
		cs::Ref<Mesh> mesh;
		if (RoundCache().TryGetValue(faction, mesh)) return mesh;
		Builder b(faction, WholeHull);
		BuildBreacherRound(b);
		mesh = b.ToMesh("BreacherRound_" + faction->Prefix);
		RoundCache()[faction] = mesh;
		return mesh;
	}

	void ShipMeshFactory::BuildBreacherRound(Builder& b)
	{
		// The casing: a round-section slug, a little fatter at the shoulder than at the base, with armour bands.
		b.Frustum(-0.45f, 0.1f, Vector2(0.2f, 0.2f), Vector2(0.24f, 0.24f), 0, b.Hull);
		for (float z : { -0.3f, -0.08f })
			b.Box(Vector3(0, 0, z), Vector3(0.255f, 0.255f, 0.03f), b.Accent);
		// The drill: a collar, then a long cone with fluting ridges.
		auto steel = Color(0.58f, 0.56f, 0.52f, 1.0f);
		b.Box(Vector3(0, 0, 0.12f), Vector3(0.22f, 0.22f, 0.04f), b.Dark);
		b.Frustum(0.14f, 0.5f, Vector2(0.2f, 0.2f), Vector2(0.015f, 0.015f), 0, steel);
		for (int k = 0; k < 4; k++)
		{
			float a = Mathf::PI * 0.5f * k + 0.4f;
			b.Box(Vector3(Mathf::Cos(a) * 0.06f, Mathf::Sin(a) * 0.05f, 0.27f), Vector3(0.02f, 0.02f, 0.22f), Color(0.42f, 0.4f, 0.37f, 1.0f));
		}
		b.Box(Vector3(0, 0, -0.43f), Vector3(0.18f, 0.18f, 0.05f), b.Dark);
		b.Nozzle(Vector3(-0.06f, 0, -0.46f), 0.035f, 0.04f);
		b.Nozzle(Vector3(0.06f, 0, -0.46f), 0.035f, 0.04f);
	}

	/// <summary>A gunship (U.SPC-T467), the heavy fighter: a broad armoured wedge with plated cheeks, a long spinal
	/// railgun under the nose, a glazed flight deck, a turret on its spine and one under its keel, and twin
	/// drives -- bigger and squarer than a fighter, with no drill.</summary>
	void ShipMeshFactory::BuildGunship(Builder& b)
	{
		b.Frustum(-0.44f, 0.36f, Vector2(0.3f, 0.12f), Vector2(0.12f, 0.06f), 0, b.Hull);
		for (float side : { -1.0f, 1.0f })
		{
			// Armour cheeks and stub pylons.
			b.Box(Vector3(side * 0.16f, 0, -0.12f), Vector3(0.05f, 0.1f, 0.48f), b.Accent);
			b.Box(Vector3(side * 0.24f, -0.01f, -0.2f), Vector3(0.12f, 0.015f, 0.18f), b.Dark);
		}
		// The spinal railgun: a long barrel slung under the nose.
		b.Box(Vector3(0, -0.04f, 0.3f), Vector3(0.035f, 0.03f, 0.36f), b.Dark);
		b.Box(Vector3(0, -0.04f, 0.49f), Vector3(0.05f, 0.04f, 0.03f), b.Dark);
		// The flight deck's glazing.
		b.Box(Vector3(0, 0.07f, 0.16f), Vector3(0.08f, 0.03f, 0.12f), b.Glow(Color(0.55f, 0.85f, 1.0f)));
		// Dorsal and ventral turrets: a squat base and twin barrels each.
		for (float y : { 1.0f, -1.0f })
		{
			b.Box(Vector3(0, y * 0.085f, y > 0 ? 0.04f : -0.05f), Vector3(0.09f, 0.03f, 0.09f), b.Dark);
			for (float side : { -1.0f, 1.0f })
				b.Box(Vector3(side * 0.02f, y * 0.09f, (y > 0 ? 0.04f : -0.05f) + 0.08f), Vector3(0.012f, 0.012f, 0.1f), b.Light);
		}
		b.Box(Vector3(0, 0, -0.42f), Vector3(0.22f, 0.1f, 0.05f), b.Dark);
		b.Nozzle(Vector3(-0.07f, 0, -0.46f), 0.04f, 0.045f);
		b.Nozzle(Vector3(0.07f, 0, -0.46f), 0.04f, 0.045f);
	}

	/// <summary>
	/// A lane tender (U.SPC-T481): a stubby work hull with no guns, a glowing service ring held out ahead of its bow on
	/// two booms (the rig it aligns a lane's accelerator rings with), and twin drives.
	/// </summary>
	void ShipMeshFactory::BuildLaneTender(Builder& b)
	{
		b.Box(Vector3(0, 0, -0.08f), Vector3(0.2f, 0.15f, 0.52f), b.Hull);
		b.Box(Vector3(0, 0.09f, 0.08f), Vector3(0.1f, 0.04f, 0.14f), b.Dark);
		b.Box(Vector3(0, 0.1f, 0.12f), Vector3(0.08f, 0.012f, 0.04f), b.Glow(Color(0.6f, 0.85f, 1.0f)));
		for (float side : { -1.0f, 1.0f })
		{
			b.Box(Vector3(side * 0.11f, 0, -0.1f), Vector3(0.02f, 0.1f, 0.36f), b.Accent);
			b.Box(Vector3(side * 0.07f, 0, 0.3f), Vector3(0.02f, 0.02f, 0.2f), b.Dark);
		}
		// The service ring: segments round the bow, lit along their faces.
		auto ringGlow = b.Glow(Color(0.55f, 1.0f, 0.85f));
		for (int k = 0; k < 12; k++)
		{
			float a = Mathf::PI * 2 * k / 12.0f;
			b.BoxRotated(Vector3(Mathf::Cos(a) * 0.16f, Mathf::Sin(a) * 0.16f, 0.42f), Vector3(0.085f, 0.022f, 0.03f), a * Mathf::Rad2Deg + 90, k % 3 == 0 ? ringGlow : b.Dark);
		}
		b.Box(Vector3(0, 0, -0.38f), Vector3(0.18f, 0.12f, 0.08f), b.Dark);
		b.Nozzle(Vector3(-0.05f, 0, -0.44f), 0.035f, 0.05f);
		b.Nozzle(Vector3(0.05f, 0, -0.44f), 0.035f, 0.05f);
	}

	void ShipMeshFactory::BuildCourier(Builder& b)
	{
		b.Frustum(-0.4f, 0.5f, Vector2(0.18f, 0.06f), Vector2(0.01f, 0.01f), 0, b.Hull);
		b.Box(Vector3(0, 0.035f, 0.05f), Vector3(0.05f, 0.02f, 0.15f), b.Glow(Color(0.6f, 0.85f, 1.0f)));
		b.Box(Vector3(0, 0, -0.1f), Vector3(0.4f, 0.01f, 0.12f), b.Accent);
		b.Nozzle(Vector3(0, 0, -0.44f), 0.03f, 0.05f);
	}

	/// <summary>A star fighter: a slim single-seat dogfighter, a glazed canopy, and twin forward
	/// guns on stub wings -- smaller than the Courier.</summary>
	void ShipMeshFactory::BuildFighter(Builder& b)
	{
		b.Frustum(-0.42f, 0.47f, Vector2(0.13f, 0.075f), Vector2(0.012f, 0.012f), 0, b.Hull);
		b.Box(Vector3(0, 0.065f, 0.04f), Vector3(0.05f, 0.03f, 0.15f), b.Glow(Color(0.55f, 0.85f, 1.0f)));
		for (float side : { -1.0f, 1.0f })
		{
			b.Box(Vector3(side * 0.2f, 0, -0.06f), Vector3(0.08f, 0.012f, 0.2f), b.Accent);
			b.Box(Vector3(side * 0.24f, 0, 0.1f), Vector3(0.018f, 0.018f, 0.09f), b.Dark);
		}
		b.Nozzle(Vector3(0, 0, -0.44f), 0.04f, 0.055f);
	}

	/// <summary>An interceptor (U.SPC-T484): a needle of a hull on a small delta, a bubble canopy well forward and
	/// light guns at the wing roots -- slimmer and sharper than the Fighter.</summary>
	void ShipMeshFactory::BuildInterceptor(Builder& b)
	{
		b.Frustum(-0.44f, 0.5f, Vector2(0.1f, 0.07f), Vector2(0.01f, 0.01f), 0, b.Hull);
		b.Box(Vector3(0, 0.05f, 0.1f), Vector3(0.04f, 0.025f, 0.12f), b.Glow(Color(0.55f, 0.85f, 1.0f)));
		b.Frustum(-0.4f, -0.02f, Vector2(0.62f, 0.014f), Vector2(0.08f, 0.014f), 0, b.Accent);
		for (float side : { -1.0f, 1.0f })
		{
			b.Box(Vector3(side * 0.07f, -0.01f, 0.14f), Vector3(0.012f, 0.012f, 0.14f), b.Dark);
			b.Box(Vector3(side * 0.28f, 0.03f, -0.3f), Vector3(0.01f, 0.07f, 0.08f), b.Accent);
		}
		b.Nozzle(Vector3(0, 0, -0.44f), 0.035f, 0.05f);
	}

	/// <summary>A bomber (U.SPC-T484): a deep, broad hull on straight wings, a torpedo slung under the keel, a glazed
	/// nose and twin engines.</summary>
	void ShipMeshFactory::BuildBomber(Builder& b)
	{
		b.Frustum(-0.45f, 0.45f, Vector2(0.22f, 0.16f), Vector2(0.08f, 0.07f), 0, b.Hull);
		b.Box(Vector3(0, -0.02f, -0.05f), Vector3(0.9f, 0.03f, 0.25f), b.Accent);
		b.Box(Vector3(0, -0.12f, 0.05f), Vector3(0.07f, 0.07f, 0.55f), b.Dark);
		b.Box(Vector3(0, 0.07f, 0.15f), Vector3(0.09f, 0.04f, 0.18f), b.Glow(Color(0.6f, 0.85f, 1.0f)));
		b.Nozzle(Vector3(-0.07f, 0, -0.47f), 0.05f, 0.06f);
		b.Nozzle(Vector3(0.07f, 0, -0.47f), 0.05f, 0.06f);
	}

	/// <summary>A boarding shuttle (U.SPC-T486): a blunt, boxy armoured hull with a glazed cockpit forward, a docking
	/// collar on its flank where the team goes across, armour skirts along its troop bay, and twin drives. No guns.</summary>
	void ShipMeshFactory::BuildShuttle(Builder& b)
	{
		b.Frustum(-0.42f, 0.3f, Vector2(0.26f, 0.2f), Vector2(0.2f, 0.15f), 0, b.Hull);
		b.Frustum(0.3f, 0.48f, Vector2(0.2f, 0.15f), Vector2(0.09f, 0.07f), 0, b.Hull);
		b.Box(Vector3(0, 0.1f, 0.34f), Vector3(0.14f, 0.04f, 0.12f), b.Glow(Color(0.6f, 0.85f, 1.0f)));
		for (float side : { -1.0f, 1.0f })
			b.Box(Vector3(side * 0.14f, -0.02f, -0.05f), Vector3(0.025f, 0.16f, 0.5f), b.Accent);
		// The docking collar: a squat ring of plate on the starboard flank over the troop bay.
		b.Box(Vector3(0.17f, 0, 0.02f), Vector3(0.04f, 0.12f, 0.14f), b.Dark);
		b.Box(Vector3(0.195f, 0, 0.02f), Vector3(0.01f, 0.08f, 0.09f), Color(0.55f, 0.53f, 0.5f, 1.0f));
		b.Nozzle(Vector3(-0.08f, 0, -0.45f), 0.05f, 0.06f);
		b.Nozzle(Vector3(0.08f, 0, -0.45f), 0.05f, 0.06f);
	}

	/// <summary>A recon / EW fighter (U.SPC-T485): an interceptor's needle on a wider delta, a sensor pod at each wing
	/// tip, a jamming blister glowing under the keel and a short antenna spine behind the canopy.</summary>
	void ShipMeshFactory::BuildRecon(Builder& b)
	{
		b.Frustum(-0.44f, 0.5f, Vector2(0.11f, 0.08f), Vector2(0.015f, 0.015f), 0, b.Hull);
		b.Box(Vector3(0, 0.055f, 0.12f), Vector3(0.045f, 0.025f, 0.12f), b.Glow(Color(0.55f, 0.85f, 1.0f)));
		b.Frustum(-0.38f, 0.0f, Vector2(0.72f, 0.016f), Vector2(0.1f, 0.016f), 0, b.Accent);
		for (float side : { -1.0f, 1.0f })
			b.Box(Vector3(side * 0.34f, 0, -0.2f), Vector3(0.03f, 0.03f, 0.2f), b.Dark);
		b.Box(Vector3(0, -0.06f, -0.05f), Vector3(0.06f, 0.035f, 0.18f), b.Glow(Color(1.0f, 0.55f, 0.85f)));
		b.Box(Vector3(0, 0.07f, -0.12f), Vector3(0.01f, 0.06f, 0.16f), b.Dark);
		b.Nozzle(Vector3(0, 0, -0.44f), 0.04f, 0.05f);
	}

	/// <summary>A gate picket (U.SPC-T480): a slim hull carrying a tall sensor mast and a broad search dish, more
	/// antenna than ship, with one small drive.</summary>
	void ShipMeshFactory::BuildPicket(Builder& b)
	{
		b.Frustum(-0.42f, 0.45f, Vector2(0.12f, 0.08f), Vector2(0.02f, 0.02f), 0, b.Hull);
		b.Box(Vector3(0, 0.06f, 0.1f), Vector3(0.04f, 0.025f, 0.12f), b.Glow(Color(0.6f, 0.85f, 1.0f)));
		// The mast and its dish, the picket's whole purpose.
		b.Box(Vector3(0, 0.16f, -0.08f), Vector3(0.02f, 0.22f, 0.02f), b.Dark);
		b.Cylinder(Vector3(0, 0.28f, -0.08f), 0.11f, 0.015f, b.Light, false, 12);
		b.Box(Vector3(0, 0, -0.15f), Vector3(0.3f, 0.01f, 0.1f), b.Accent);
		b.Nozzle(Vector3(0, 0, -0.44f), 0.035f, 0.05f);
	}
}
