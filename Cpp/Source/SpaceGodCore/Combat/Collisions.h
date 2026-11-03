#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/Collisions.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	class CombatSpec;
	class ShipCombat;
	class Subsystem;

	/// <summary>
	/// A collision volume (U.SPC-T60): a capsule, a segment of half-length <see cref="HalfLengthKm"/> along
	/// <see cref="Axis"/> swept by a sphere of <see cref="RadiusKm"/>. A hull is a capsule as long as the
	/// ship and as thick as its beam; a station, an asteroid or a piece of debris is a sphere (no axis).
	/// Positions and velocities are km and km/s in whatever frame the caller shares between the two.
	/// </summary>
	struct CollisionVolume
	{
		DVec3 Pos, Vel;

		/// <summary>Unit long axis (a hull's nose), or zero for a sphere.</summary>
		DVec3 Axis;

		double HalfLengthKm = 0, RadiusKm = 0;

		/// <summary>Kilotonnes; zero or less for something immovable (a station, an asteroid): it takes
		/// no impulse and the other body bounces off it as off a wall.</summary>
		double MassKt = 0;

		/// <summary>Its motion is driven elsewhere (a ship on its schedule, a drawn debris piece): it counts
		/// with its mass in the impulse, but <see cref="Collisions.Resolve"/> moves neither its position nor
		/// its velocity, and the other body is pushed all the way out of it.</summary>
		bool Pinned = false;

		bool Immovable() const { return MassKt <= 0; }

		/// <summary>The ends of the core segment.</summary>
		DVec3 Head() const { return Pos + Axis * HalfLengthKm; }
		DVec3 Tail() const { return Pos - Axis * HalfLengthKm; }

		/// <summary>The radius of a sphere that holds the whole volume, for broad-phase tests.</summary>
		double BoundingRadiusKm() const { return RadiusKm + HalfLengthKm; }

		static CollisionVolume Sphere(DVec3 pos, DVec3 vel, double radiusKm, double massKt);

		/// <summary>A hull of design <paramref name="spec"/>, nose on <paramref name="forward"/>: as long as the
		/// ship, its beam (<see cref="CombatSpec.BeamFraction"/> of its length) the capsule's diameter.</summary>
		static CollisionVolume Hull(const cs::Ref<CombatSpec>& spec, DVec3 pos, DVec3 vel, DVec3 forward, double massKt = cs::Double::NaN);
	};

	/// <summary>What one contact did: where, along which normal, how hard, and how much energy the
	/// crumpling took out of it (each body's share of which is what damages it).</summary>
	struct Impact
	{
		bool Hit = false;

		/// <summary>The contact point (between the two surfaces) and the unit normal from A toward B.</summary>
		DVec3 Point, Normal;

		/// <summary>How deep the volumes overlapped before they were pushed apart, km.</summary>
		double DepthKm = 0;

		/// <summary>The speed they were closing at along the normal, km/s (0 if already parting).</summary>
		double ClosingKmS = 0;

		/// <summary>The impulse B received (A received its opposite), kt·km/s.</summary>
		DVec3 Impulse;

		/// <summary>Kinetic energy the impact dissipated, TJ (kt·km²/s²): the relative kinetic energy in
		/// the bodies' centre-of-mass frame before it, less what is left after. Split evenly between them
		/// (<see cref="EnergyShareTJ"/>): what crumples one hull crumples the other.</summary>
		double EnergyTJ = 0;

		double EnergyShareTJ() const { return EnergyTJ * 0.5; }
	};

	/// <summary>
	/// Collisions and ramming (U.SPC-T60): hulls, stations, asteroids and wrecks have volumes
	/// (<see cref="CollisionVolume"/>), a contact between two of them exchanges momentum by an impulse along the
	/// contact normal (part elastic, the rest crumpling) with friction across it, so a glancing pass is a
	/// scrape, and the kinetic energy it dissipates is damage, done to the subsystems on the struck side.
	///
	/// Damage scales with that energy over the hull's own mass (<see cref="DamageHp"/>): a hull is about as
	/// tough as it is heavy, so a courier that rams a battleship is wrecked while the battleship is
	/// scratched, though both absorbed the same energy. Contacts slower than <see cref="SafeImpactKmS"/>
	/// are fender bumps: no bounce and no damage, which is what lets a pilot nudge alongside a hulk.
	/// The impulse acts through the centres (no spin is imparted).
	/// </summary>
	class Collisions
	{
	public:
		/// <summary>The share of closing speed a hard impact gives back as bounce; the rest crumples metal.</summary>
		static constexpr double Restitution = 0.2;

		/// <summary>Coulomb friction between scraping hulls: the most tangential impulse a contact can give,
		/// as a share of its normal impulse.</summary>
		static constexpr double Friction = 0.3;

		/// <summary>Closing speeds under this (km/s, 5 m/s) are fender bumps: no bounce, and below the energy
		/// a hull shrugs off (<see cref="DamageHp"/>).</summary>
		static constexpr double SafeImpactKmS = 0.005;

		/// <summary>Energy per kilotonne of hull, TJ/kt, that would take a hull's whole worth of hit points:
		/// a courier into a station at its full flight-assist speed (0.45 km/s) loses well over half its hull,
		/// a carrier at its own (0.07 km/s) a percent or two.</summary>
		static constexpr double ToughnessTJPerKt = 0.084;

		/// <summary>Of the damage an impact does, the share taken by the hardpoints on the struck side (the
		/// rest goes straight to the hull, as does whatever is left over once a hardpoint is gone).</summary>
		static constexpr double SubsystemShare = 0.6;

		/// <summary>How far from the point of impact (in ship lengths) a hardpoint still takes a share.</summary>
		static constexpr double SubsystemReachLengths = 0.35;

		/// <summary>A design's mass, kt: its own, or for a design that has none (a defence platform, a fighter)
		/// as heavy as a cruiser-like hull of its length (mass scales with the cube of size).</summary>
		static double MassOf(const cs::Ref<CombatSpec>& spec);

		// ---------------------------------------------------------------- detection

		/// <summary>
		/// Whether two volumes overlap and, if so, the unit normal from <paramref name="a"/> toward
		/// <paramref name="b"/>, how deep they overlap and the contact point.
		/// </summary>
		static bool Contact(const CollisionVolume& a, const CollisionVolume& b, DVec3& normal, double& depthKm, DVec3& point);

		/// <summary>
		/// Continuous detection for a fast pass: the earliest fraction (0..1) of a step over which
		/// <paramref name="b"/> moves by <paramref name="moveB"/> relative to <paramref name="a"/> (b's offset
		/// from a going from <paramref name="relStart"/> to <paramref name="relStart"/> + moveB) at which their
		/// bounding spheres (radii summing to <paramref name="reachKm"/>) first touch, or -1 if they don't in it.
		/// A hull at cruise speed covers more than its own length a frame, so without this it could pass
		/// straight through a piece of debris between two frames.
		/// </summary>
		static double TimeOfImpact(DVec3 relStart, DVec3 moveB, double reachKm);

		/// <summary>
		/// Detects and resolves a contact: pushes the two apart out of any overlap (by inverse mass, or all
		/// of it onto the one free to move) and, if they were closing, exchanges the impulse that stops them
		/// closing and gives back <see cref="Restitution"/> of it as bounce (none for a fender bump), with
		/// friction against their sliding across each other. Momentum between two free bodies is conserved
		/// exactly; an immovable one takes none of it, and a <see cref="CollisionVolume.Pinned"/> one is left as it was.
		/// </summary>
		static Impact Resolve(CollisionVolume& a, CollisionVolume& b);

		/// <summary>
		/// A hull settling onto (or slamming into) a world's surface: the sink speed it loses there is a
		/// fully inelastic impact with something immovable, and this is the hull's share of the energy.
		/// </summary>
		static double SurfaceImpactShareTJ(double massKt, double sinkKmS);

		// ---------------------------------------------------------------- damage

		/// <summary>
		/// The hit points an impact's energy share does to a hull of design <paramref name="spec"/>: what is
		/// left after the energy it shrugs off (a <see cref="SafeImpactKmS"/> bump, more with armour), over the
		/// energy that takes its whole hull (<see cref="ToughnessTJPerKt"/> per kilotonne), times its hull.
		/// So it is linear in the energy and, for a given pair, goes as the square of the closing speed.
		/// </summary>
		static double DamageHp(const cs::Ref<CombatSpec>& spec, double energyShareTJ);

		/// <summary>
		/// Ship lengths in a hull's own frame (right, up, forward: a <see cref="SubsystemDef"/>'s X, Y, Z) of a
		/// point given in the shared frame.
		/// </summary>
		static DVec3 ToLocal(DVec3 point, DVec3 centre, DVec3 forward, DVec3 up, double lengthKm);

		/// <summary>The inverse of <see cref="ToLocal"/> for an offset: a hull-frame vector (right, up, forward)
		/// in the shared frame, unscaled.</summary>
		static DVec3 ToWorld(DVec3 local, DVec3 forward, DVec3 up);

		/// <summary>The point on a hull's surface (ship lengths, hull frame) farthest along <paramref name="dirLocal"/>:
		/// where it touches a flat surface lying that way, such as the ground it comes down on.</summary>
		static DVec3 SurfacePoint(const cs::Ref<CombatSpec>& spec, DVec3 dirLocal);

		/// <summary>The share of an impact at <paramref name="local"/> (ship lengths, hull frame) each live
		/// hardpoint takes: those on the struck side of the hull's axis, more the nearer they sit to the point
		/// of impact, none beyond <see cref="SubsystemReachLengths"/>. Zero everywhere if none is in reach.</summary>
		static cs::List<double> StruckSideWeights(const cs::Ref<ShipCombat>& c, DVec3 local);

		/// <summary>
		/// Does <paramref name="hp"/> of impact damage to a hull struck at <paramref name="local"/> (ship lengths
		/// in its frame): the field of the shield emitter whose patch is struck soaks what it can, as it would a shot; then the
		/// hardpoints on the struck side take <see cref="SubsystemShare"/> between them by nearness (what a lost
		/// one cannot take goes through to the hull) and the hull the rest. A lost reactor takes the ship with it.
		/// Returns the damage that got past the shields; hardpoints knocked out go into <paramref name="destroyed"/>.
		/// </summary>
		static double ApplyDamage(const cs::Ref<ShipCombat>& c, DVec3 local, double hp, double nowSeconds = -999, cs::List<cs::Ref<Subsystem>>* destroyed = nullptr);

		// ---------------------------------------------------------------- geometry

		/// <summary>Closest points between segments p1-q1 and p2-q2 (Ericson, Real-Time Collision Detection 5.1.9).</summary>
		static void ClosestPoints(DVec3 p1, DVec3 q1, DVec3 p2, DVec3 q2, DVec3& c1, DVec3& c2);

	private:
		static double Clamp01(double x);
	};
}
