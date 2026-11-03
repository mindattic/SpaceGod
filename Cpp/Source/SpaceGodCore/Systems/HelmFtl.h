#pragma once
// Port of Assets/UnitySpace/Core/Systems/HelmFtl.cs (US-T17).

#include "Cs/Cs.h"
#include "DVec3.h"
#include "GalacticPosition.h"

namespace SpaceGod::Core
{
	class StarSystem;
	class Galaxy;
	class Ship;
	class World;
	class Cruise;
	class CruiseDriveSpec;
	struct NearbyStar;

	/// <summary>
	/// A star the helm's FTL drive can be pointed at (U.SPC-T417): a running system, or a plain star on the charts (built
	/// into a system when a pilot sets out for it). Canon (U.SPC-T455): a jump only ever goes from one star to another,
	/// never to somewhere inside a solar system, whose gravity wells make jumping there impossible.
	/// </summary>
	class FtlTarget final
	{
	public:
		cs::String Name;

		/// <summary>The running system the star is, or null for a plain star on the charts.</summary>
		cs::Ref<StarSystem> System;

		/// <summary>Where it is in the galaxy.</summary>
		GalacticPosition Origin;

		// private in C#; public so cs::New can make one (only OfStar does).
		FtlTarget(const cs::String& name, const cs::Ref<StarSystem>& system, GalacticPosition origin);

		/// <summary>A running system's star.</summary>
		static cs::Ref<FtlTarget> OfStar(const cs::Ref<StarSystem>& system);

		/// <summary>A plain star on the charts, one nobody simulates yet.</summary>
		static cs::Ref<FtlTarget> OfStar(const NearbyStar& star);

		/// <summary>A beacon rather than a star (U.SPC-T420): a void station out in the dark between the stars, which the
		/// gate network relays through, with no star of its own.</summary>
		bool IsBeacon() const;

		/// <summary>"beacon" or "star", for the navigator's lines.</summary>
		cs::String KindName() const { return IsBeacon() ? "beacon" : "star"; }

		/// <summary>Where it is, km from <paramref name="here"/>'s root.</summary>
		DVec3 LocalKm(const cs::Ref<StarSystem>& here) const;

		cs::String ToString() const { return Name; }
	};

	/// <summary>
	/// What the helm can point its FTL drive at from a system (U.SPC-T417): the other stars, running systems and the
	/// charts. A new kind of destination plugs in by adding a source to <see cref="Sources"/>, never by editing the helm.
	/// </summary>
	class FtlTargets
	{
	public:
		/// <summary>A source of targets: (the galaxy, the system the ship is in) to what it offers.</summary>
		// C# delegate IEnumerable<FtlTarget> Source(Galaxy galaxy, StarSystem here), built eagerly.
		using Source = cs::Func<cs::List<cs::Ref<FtlTarget>>, cs::Ref<Galaxy>, cs::Ref<StarSystem>>;

		/// <summary>Where targets come from, in order.</summary>
		static cs::List<Source>& Sources();

		/// <summary>Every target offered from <paramref name="here"/>.</summary>
		static cs::List<cs::Ref<FtlTarget>> All(const cs::Ref<Galaxy>& galaxy, const cs::Ref<StarSystem>& here);

		/// <summary>The other running systems' stars, and the plain stars on the charts.</summary>
		static cs::List<cs::Ref<FtlTarget>> Stars(const cs::Ref<Galaxy>& galaxy, const cs::Ref<StarSystem>& here);
	};

	/// <summary>Where the helm's FTL drive stands.</summary>
	enum class FtlStage : int
	{
		Idle,

		/// <summary>Charging, beyond the rim, nose held on the star.</summary>
		Spooling,
	};
	const char* ToString(FtlStage v);

	/// <summary>What a step of the helm's FTL drive came to.</summary>
	enum class FtlEvent : int
	{
		None,

		/// <summary>The charge was lost (the nose swung off, a fight, back inside the rim, or called off): see <see cref="HelmFtl.AbortReason"/>.</summary>
		Aborted,

		/// <summary>Charged: the drive engages now (<see cref="HelmFtl.EngageInterstellar"/>).</summary>
		Charged,
	};
	const char* ToString(FtlEvent v);

	/// <summary>
	/// The helm's own FTL drive, I-War 2 style (U.SPC-T417): a carrier, a battleship fitted with a drive or a deep-space
	/// explorer (anything carrying a <see cref="CruiseDriveSpec"/> by the owner's roster, <see cref="FtlRoster"/>, U.SPC-T472)
	/// is pointed at another star, spools its drive while the nose is held on it, engages,
	/// and is handed to <see cref="DeepSpace"/>, which flies it across open space as it does every other group under its
	/// own drive and drops it out at the edge of that star's well.
	///
	/// Canon and lore (U.SPC-T455): jumping is impossible inside a solar system's gravity wells. A hull must first fly
	/// beyond the system's outer rim (<see cref="RimKm"/>, the same edge the jump gates stand toward and the cruise drives
	/// engage at), and the drive only ever jumps to another star, never to somewhere in the same system. Cruisers, destroyers
	/// and small craft carry no drive and are refused at the helm: the jump gates and lanes are their way, or a berth aboard
	/// a carrier. The
	/// helm's own escorts go with it (U.SPC-T419): those with drives of their own alongside, fighters and small craft
	/// docked aboard as far as the hangars have room (<see cref="FtlCarriage"/>).
	///
	/// The drive's physics (U.SPC-T418, <see cref="FtlRules"/>): no charge inside a planet's or a moon's well or a gravity
	/// projector's field either, none while the drive cools from its last drop-out, and none without the reaction mass
	/// the jump burns; the hull comes out with the velocity it went in with.
	///
	/// Engine-free and driven only by the sim clock, so the Runtime helm just feeds it the hull's place and heading.
	/// </summary>
	// A C# null string (no refusal) is the empty string here.
	class HelmFtl final
	{
	public:
		/// <summary>How near the nose must be to a star to pick it and start the charge, degrees.</summary>
		static constexpr double AlignDeg = 3;

		/// <summary>How far the nose may wander from the star while charging before the charge is lost, degrees.</summary>
		static constexpr double HoldDeg = 8;

		FtlStage Stage = FtlStage::Idle;
		cs::Ref<FtlTarget> Target;
		cs::Ref<CruiseDriveSpec> Drive;

		/// <summary>When the charge began and when it is done (the drive's <see cref="CruiseDriveSpec.SpoolDays"/>).</summary>
		double SpoolStartJd = cs::Double::NaN;
		double ChargedJd = cs::Double::NaN;

		/// <summary>Why the last charge was lost or the jump refused.</summary>
		cs::String AbortReason;

		/// <summary>The system's outer rim, km from its root: inside it the gravity wells make a jump impossible.</summary>
		static double RimKm(const cs::Ref<StarSystem>& system);

		/// <summary>Whether <paramref name="fromKm"/> (km from <paramref name="here"/>'s root) is beyond the outer rim, clear to jump.</summary>
		static bool BeyondRim(const cs::Ref<StarSystem>& here, DVec3 fromKm) { return fromKm.Length() >= RimKm(here); }

		/// <summary>0..1 through the charge.</summary>
		double SpoolFraction(double jd) const
		{
			return Stage != FtlStage::Spooling ? 0 : ChargedJd <= SpoolStartJd ? 1 : cs::Math::Max(0.0, cs::Math::Min(1.0, (jd - SpoolStartJd) / (ChargedJd - SpoolStartJd)));
		}

		/// <summary>Why <paramref name="ship"/> cannot jump under its own power at all, or null if it can.</summary>
		static cs::String Refusal(const cs::Ref<Ship>& ship);

		/// <summary>Why no jump can be made from <paramref name="fromKm"/> in <paramref name="here"/> (inside the rim), or null.</summary>
		static cs::String RimRefusal(const cs::Ref<StarSystem>& here, DVec3 fromKm);

		/// <summary>
		/// Why no jump can be made from <paramref name="fromKm"/> in <paramref name="here"/> for gravity (U.SPC-T418): inside the
		/// outer rim, inside a planet's or a moon's well, or inside a gravity projector's field (<paramref name="galaxy"/>'s).
		/// Null if clear.
		/// </summary>
		static cs::String WellRefusal(const cs::Ref<StarSystem>& here, DVec3 fromKm, const cs::Ref<Galaxy>& galaxy = nullptr);

		/// <summary>
		/// Why <paramref name="ship"/>'s drive cannot charge for a jump of <paramref name="km"/> at <paramref name="jd"/> on its
		/// own account (U.SPC-T418): still cooling from its last drop-out, or too little in the tanks. Null if it can.
		/// </summary>
		static cs::String DriveRefusal(const cs::Ref<Ship>& ship, double km, double jd);

		/// <summary>The way to <paramref name="target"/> from <paramref name="fromKm"/> in <paramref name="here"/> (unit), and how far, km.</summary>
		static DVec3 Bearing(const cs::Ref<FtlTarget>& target, const cs::Ref<StarSystem>& here, DVec3 fromKm, double& km);

		/// <summary>The angle between <paramref name="forward"/> and the way to <paramref name="target"/>, degrees.</summary>
		static double OffNoseDeg(const cs::Ref<FtlTarget>& target, const cs::Ref<StarSystem>& here, DVec3 fromKm, DVec3 forward);

		/// <summary>The star the nose is on: the one nearest dead ahead within <paramref name="toleranceDeg"/> (of two as near, the nearer). Null if none.</summary>
		static cs::Ref<FtlTarget> Pointed(const cs::List<cs::Ref<FtlTarget>>& targets, const cs::Ref<StarSystem>& here, DVec3 fromKm, DVec3 forward, double toleranceDeg = AlignDeg);

		/// <summary>
		/// Starts the charge for <paramref name="target"/> at <paramref name="jd"/>: the ship must carry a drive, be out of
		/// any fight, be beyond the system's outer rim and clear of every other well and projector's field (U.SPC-T418), have
		/// its nose on another star, a cool drive and the reaction mass for the jump. False (with <see cref="AbortReason"/>) if not.
		/// </summary>
		bool Spool(const cs::Ref<Ship>& ship, const cs::Ref<StarSystem>& here, const cs::Ref<FtlTarget>& target, DVec3 fromKm, DVec3 forward, bool inCombat, double jd, const cs::Ref<Galaxy>& galaxy = nullptr);

		/// <summary>
		/// A step of the charge at <paramref name="jd"/>, the hull at <paramref name="fromKm"/> heading <paramref name="forward"/>:
		/// lost if the nose wanders, a fight starts or the hull falls back inside the rim; done when its time is up.
		/// </summary>
		FtlEvent Step(const cs::Ref<StarSystem>& here, DVec3 fromKm, DVec3 forward, bool inCombat, double jd, const cs::Ref<Galaxy>& galaxy = nullptr);

		/// <summary>Calls the charge off; the reason is kept in <see cref="AbortReason"/>.</summary>
		FtlEvent Abort(const cs::String& why);

		/// <summary>Back to idle.</summary>
		void Reset();

		/// <summary>
		/// Charged: <paramref name="ship"/> (at <paramref name="fromKm"/> in <paramref name="here"/>) goes out into open space
		/// under its own drive (<see cref="DeepSpace"/>), already charged, bound for the edge of the star's well. A plain star
		/// on the charts is built into a running system first, so there is somewhere to arrive. The hull itself goes, the same
		/// object (<see cref="Fleet.LetGo"/> leaves a marker in its slot here), never a copy; the cruise is marked
		/// <see cref="Cruise.Piloted"/>, so nobody else gives it orders when it drops out. Null (with
		/// <see cref="AbortReason"/>) if there is nowhere to go.
		/// </summary>
		cs::Ref<Cruise> EngageInterstellar(const cs::Ref<Galaxy>& galaxy, const cs::Ref<StarSystem>& here, const cs::Ref<Ship>& ship, DVec3 fromKm, double jd, DVec3 velocityKmS = DVec3());

		/// <summary>
		/// The helm's escorts that go with <paramref name="ship"/> through FTL (U.SPC-T419): those with drives of their own,
		/// then the fighters and small craft that fit docked aboard it or them, and the escorts that fit clamped to a carrier's
		/// hull (U.SPC-T473) (<paramref name="aboard"/>: craft to carrier). An escort still fighting or left a hulk stays
		/// behind, as does one with no berth or clamp to ride on.
		/// </summary>
		static cs::List<cs::Ref<Ship>> Wing(const cs::Ref<World>& world, const cs::Ref<Ship>& ship, cs::Dictionary<cs::Ref<Ship>, cs::Ref<Ship>>& aboard);

		/// <summary>As <see cref="EngageInterstellar(Galaxy, StarSystem, Ship, DVec3, double, DVec3)"/>, saying how many of the helm's
		/// escorts went with it (<paramref name="wing"/>: alongside or docked aboard, U.SPC-T419).</summary>
		cs::Ref<Cruise> EngageInterstellar(const cs::Ref<Galaxy>& galaxy, const cs::Ref<StarSystem>& here, const cs::Ref<Ship>& ship, DVec3 fromKm, double jd, DVec3 velocityKmS, int& wing);
	};
}
