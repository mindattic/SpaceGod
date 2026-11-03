#pragma once
// Port of Unity.SpaceGod Runtime/Rendering/ShipMeshFactory.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod::Core
{
	class Faction;
	enum class ShipClass : int;
	enum class SubsystemKind : int;
	enum class IndustryKind : int;
}

namespace SpaceGod::Rendering
{
	/// <summary>
	/// Kit-bashed hulls built from boxes, frustums and cylinders. Every design is one unit long on +Z
	/// (nose forward), flat-shaded, with the paint scheme in vertex colours and engine bells flagged
	/// emissive through vertex alpha (see Ship.shader).
	///
	/// Warship hulls are bare: their guns, launchers, shield emitters and sensor masts are separate
	/// parts (<see cref="GetPart"/>) bolted on at the hardpoints the combat model uses, so each can
	/// traverse, be shot away and leave a scar. Warship hulls also come in two sections, bow and
	/// stern, split amidships, so a destroyed ship breaks in two.
	/// </summary>
	class ShipMeshFactory
	{
	public:
		static constexpr int WholeHull = -1, Stern = 0, Bow = 1;

		/// <summary>Where warship hulls break in two (ship lengths along the keel).</summary>
		static constexpr float SplitZ = -0.06f;

		/// <summary>
		/// The first faction seen with this name: caches key on it, so rebuilding the worlds (time
		/// reset) with new but identical faction objects reuses the meshes instead of piling up copies.
		/// </summary>
		static cs::Ref<Core::Faction> Canon(cs::Ref<Core::Faction> f);

		static bool IsWarship(Core::ShipClass c);

		/// <summary>The whole hull (both sections).</summary>
		static cs::Ref<Mesh> Get(Core::ShipClass shipClass, cs::Ref<Core::Faction> faction);

		/// <summary>One section of a hull: <see cref="Bow"/>, <see cref="Stern"/> or <see cref="WholeHull"/>.</summary>
		static cs::Ref<Mesh> GetSection(Core::ShipClass shipClass, cs::Ref<Core::Faction> faction, int section);

		/// <summary>
		/// A truss freighter (U.SPC-T482) as it is loaded now: its spine with a clamp for each of its <paramref name="pods"/>,
		/// a cargo pod in <paramref name="loaded"/> of them (so the pods on it show its load: a bare truss empty, every clamp
		/// filled full) and its <paramref name="spheres"/> fuel spheres. One mesh per combination, built once.
		/// </summary>
		static cs::Ref<Mesh> GetTruss(cs::Ref<Core::Faction> faction, int pods, int loaded, int spheres);

		/// <summary>The most pods and spheres a truss is drawn with (a yard can clamp on more than the stock design's).</summary>
		static constexpr int MaxTrussPods = 16, MaxTrussSpheres = 8;

		/// <summary>An orbital defence platform: a gun core inside a ring (its guns are parts too).</summary>
		static cs::Ref<Mesh> GetPlatform(cs::Ref<Core::Faction> faction);

		/// <summary>
		/// A hardpoint's model, centred on its mount, built upright (+Y) and facing +Z, in ship lengths.
		/// Turrets come in two meshes: the traversing base and the elevating barrels.
		/// </summary>
		static cs::Ref<Mesh> GetPart(Core::SubsystemKind kind, cs::Ref<Core::Faction> faction, bool barrels);

		/// <summary>
		/// A spinal mount's model (U.SPC-T490): a gun as long as the hull, laid down the keel from its muzzle at the mount
		/// (+Z, at the bow) back to its breech and power couplings near the stern, in ship lengths. A railgun is a pair of
		/// rails between armoured braces, a coilgun a barrel ringed by coils along its length, a plasma spinal cannon a fat
		/// bottle ringed in green, and a tachyon lance an emitter tube ringed in violet.
		/// </summary>
		static cs::Ref<Mesh> GetSpinal(Core::SubsystemKind kind, cs::Ref<Core::Faction> faction);

		/// <summary>
		/// A boarding pod: an armoured cylinder with a fluted drill tip on +Z, one unit long. Stuck in a
		/// hull, the tip is buried and the tail and its thruster bells stand out of the plating.
		/// </summary>
		static cs::Ref<Mesh> GetPod(cs::Ref<Core::Faction> faction);

		/// <summary>A carrier's launched craft, drawn as its own hull class (U.SPC-T484): an interceptor, a space
		/// superiority fighter, a bomber or a gunship as the hull of that class; anything else as a swept-wing
		/// carrier fighter with twin guns.</summary>
		static cs::Ref<Mesh> GetFighter(cs::Ref<Core::Faction> faction, Core::ShipClass cls);

		/// <summary>A blackened stub left where a hardpoint was shot away.</summary>
		static cs::Ref<Mesh> GetScar();

		// ---------------------------------------------------------------- orbital structures

		/// <summary>A shipyard: an open lattice cradle along +Z with a control block and gantries (the hull is drawn inside it).</summary>
		static cs::Ref<Mesh> GetShipyard(cs::Ref<Core::Faction> faction);

		/// <summary>One rib of a hull being framed out on a slip (a hull is about 0.14 wide at this scale).</summary>
		static cs::Ref<Mesh> GetSkeletonRib();

		/// <summary>A factory station: a pressurised core, an industry-coloured process block and big radiator wings.</summary>
		static cs::Ref<Mesh> GetFactory(cs::Ref<Core::Faction> faction, Core::IndustryKind industry);

		/// <summary>A habitat: a spinning ring on four spokes round a docking hub (axis +Z).</summary>
		static cs::Ref<Mesh> GetHabitat(cs::Ref<Core::Faction> faction);

		/// <summary>A free-flying station (Rigil Gate, Toliman Forge, Blackhollow): a long spine with twin habitat rings, docks and arrays. Radius one.</summary>
		static cs::Ref<Mesh> GetStationBody(cs::String name);

		/// <summary>
		/// A docking ring: a broad torus (axis +Z, the way ships pass through to the atmosphere) with
		/// docking arms round its rim and approach lights on its inner edge. One unit across.
		/// </summary>
		static cs::Ref<Mesh> GetDockingRing(cs::Ref<Core::Faction> faction);

		/// <summary>The ISS or Tiangong: a truss with modules and solar arrays (one unit across).</summary>
		static cs::Ref<Mesh> GetCrewedStation(bool tiangong);

		/// <summary>A telescope (Hubble's tube, or JWST's gold mirror over its sunshield) or a robotic probe.</summary>
		static cs::Ref<Mesh> GetSpacecraft(cs::String name);

		/// <summary>
		/// The interdiction field projector's visible part (U.SPC-T507, <see cref="LaneInterdictors"/>): the glowing emitter
		/// ring on its struts, centred on the keel amidships, drawn on whatever hull carries the projector (a destroyer, a
		/// cruiser, a raider's converted freighter). In ship lengths, like the hull it is set on.
		/// </summary>
		static cs::Ref<Mesh> GetFieldProjector(cs::Ref<Core::Faction> faction);

		/// <summary>
		/// What an ore tug pushes (U.SPC-T498, <see cref="Ship.Tow"/>): a lumpy boulder one unit across, centred, made of a big
		/// faceted body and a few smaller knobs fused into it -- dark rock, or pale blue-white comet ice.
		/// </summary>
		static cs::Ref<Mesh> GetTowLoad(bool ice);

		/// <summary>
		/// A Breacher round (U.SPC-T466, U.SPC-T517): the bullet-shaped armoured round a Breacher bay fires, its team sealed
		/// in the casing, a fluted drill head for a nose and the steering thrusters' nozzles at its base. Its own model, not
		/// a ship's: drawn in flight and stuck in the hull it struck. One mesh per faction, built once.
		/// </summary>
		static cs::Ref<Mesh> GetBreacherRound(cs::Ref<Core::Faction> faction);

	private:
		/// <summary>
		/// Accumulates flat-shaded primitives. With a section chosen, only primitives (or the parts of
		/// frustums) on that side of <see cref="SplitZ"/> are kept, so the two halves meet exactly.
		/// </summary>
		class Builder;

		static cs::Ref<Mesh> Cached(const cs::String& key, cs::Ref<Core::Faction> faction, const cs::Action<Builder&>& build);

		/// <summary>A ring of boxes standing in for a torus, in the XY plane (axis +Z), radius r.</summary>
		static void Torus(Builder& b, float r, float thickness, float depth, int segments, Color color, float z = 0);

		static void BuildBattleship(Builder& b);

		/// <summary>A gate breaker (U.SPC-T478): a short, broad, slab-sided siege hull under thick armour belts, its blunt ram
		/// bow banded in plate, the plasma batteries' barbettes down both beams (the cannons themselves are parts) with their
		/// magnetic bottles glowing between them, the shuttle deck's doors under the keel, the siege engineering bay's cranes
		/// and gate-control mast on the dorsal spine, and three big bells astern.</summary>
		static void BuildGateBreaker(Builder& b);

		static void BuildCarrier(Builder& b);
		static void BuildCruiser(Builder& b);

		/// <summary>A command cruiser (U.SPC-T477): a long battlecruiser's hull with a light battery, the sensor array's big
		/// dish on a dorsal mast forward, the communications suite's antenna spars along both beams and the targeting
		/// computers' fire-control domes amidships, lit in the array's glow.</summary>
		static void BuildCommandCruiser(Builder& b);

		/// <summary>A frigate (U.SPC-T475): a slim, light spine with the two torpedo launchers' glowing mouths in its bow, the
		/// four missile racks paired above and below it forward, and a big four-bell engine block astern that is a third of
		/// the hull.</summary>
		static void BuildFrigate(Builder& b);

		/// <summary>A corvette (U.SPC-T476): a short, stocky patrol hull with no drive room, broad saddle tanks either side for
		/// its weeks on the beat, a spinal light railgun under the nose, a laser turret above and below, a search mast over the
		/// bridge and twin bells astern.</summary>
		static void BuildCorvette(Builder& b);

		static void BuildDestroyer(Builder& b);

		/// <summary>The stock freighter, fully loaded (for thumbnails and the yard's slips).</summary>
		static void BuildFreighter(Builder& b);

		/// <summary>A side of the spine: right, left, over, under.</summary>
		static Vector2 SideOf(int place);

		/// <summary>
		/// A truss freighter (U.SPC-T482): a skeletal square truss from the cockpit at the bow to the engine astern, a
		/// clamp arm at every pod's place along it, a standard container in the first <paramref name="loaded"/> clamps
		/// (two abreast, or four round the spine on a long truss), and the fuel spheres ringed round the spine ahead of the
		/// drive's shadow shield.
		/// </summary>
		static void BuildTruss(Builder& b, int pods, int loaded, int spheres);

		static void BuildHauler(Builder& b);

		/// <summary>
		/// An ore tug (U.SPC-T483): no hold, just a big engine with claws. A short blocky frame, a cluster of three heavy
		/// nozzles astern, a push plate across the bow, and four jointed claws reaching forward round it to grip a rock.
		/// </summary>
		static void BuildOreTug(Builder& b);

		/// <summary>
		/// A capture tug (U.SPC-T522): an ore tug's plan on a lighter frame. A slim spine with a cab on top, a pair of nozzles
		/// astern, a narrow push plate, and three longer, thinner claws reaching well forward to close round a tumbling rock.
		/// </summary>
		static void BuildCaptureTug(Builder& b);

		static void BuildBreacherRound(Builder& b);

		/// <summary>A gunship (U.SPC-T467), the heavy fighter: a broad armoured wedge with plated cheeks, a long spinal
		/// railgun under the nose, a glazed flight deck, a turret on its spine and one under its keel, and twin
		/// drives -- bigger and squarer than a fighter, with no drill.</summary>
		static void BuildGunship(Builder& b);

		/// <summary>
		/// A lane tender (U.SPC-T481): a stubby work hull with no guns, a glowing service ring held out ahead of its bow on
		/// two booms (the rig it aligns a lane's accelerator rings with), and twin drives.
		/// </summary>
		static void BuildLaneTender(Builder& b);

		static void BuildCourier(Builder& b);

		/// <summary>A star fighter: a slim single-seat dogfighter, a glazed canopy, and twin forward
		/// guns on stub wings -- smaller than the Courier.</summary>
		static void BuildFighter(Builder& b);

		/// <summary>An interceptor (U.SPC-T484): a needle of a hull on a small delta, a bubble canopy well forward and
		/// light guns at the wing roots -- slimmer and sharper than the Fighter.</summary>
		static void BuildInterceptor(Builder& b);

		/// <summary>A bomber (U.SPC-T484): a deep, broad hull on straight wings, a torpedo slung under the keel, a glazed
		/// nose and twin engines.</summary>
		static void BuildBomber(Builder& b);

		/// <summary>A boarding shuttle (U.SPC-T486): a blunt, boxy armoured hull with a glazed cockpit forward, a docking
		/// collar on its flank where the team goes across, armour skirts along its troop bay, and twin drives. No guns.</summary>
		static void BuildShuttle(Builder& b);

		/// <summary>A recon / EW fighter (U.SPC-T485): an interceptor's needle on a wider delta, a sensor pod at each wing
		/// tip, a jamming blister glowing under the keel and a short antenna spine behind the canopy.</summary>
		static void BuildRecon(Builder& b);

		/// <summary>A gate picket (U.SPC-T480): a slim hull carrying a tall sensor mast and a broad search dish, more
		/// antenna than ship, with one small drive.</summary>
		static void BuildPicket(Builder& b);
	};
}
