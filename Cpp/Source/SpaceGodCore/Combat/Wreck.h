#pragma once
// Port of Assets/UnitySpace/Core/Combat/Wreck.cs (see docs/MIGRATION.md).
// Kept light: Ships.h includes it for SalvageStack (held by value in Ship::Salvage).

#include <cstdint>

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::Core
{
	enum class Good : int;
	class Faction;
	class Ship;
	class LaneSite;
	class BattleUnit;
	class WreckPiece;
	struct CollisionVolume;

	/// <summary>One good and how many tonnes of it a wreck's hold still carries for whoever salvages it.</summary>
	struct SalvageStack
	{
		SpaceGod::Core::Good Good{};
		double Tonnes = 0;

		SalvageStack() = default;
		SalvageStack(SpaceGod::Core::Good good, double tonnes)
		{
			Good = good;
			Tonnes = tonnes;
		}
	};

	/// <summary>
	/// A destroyed hull left behind at the battle site: a slowly tumbling wreck and its drifting
	/// debris field, standing in for the ship until it finally breaks up. <see cref="Pos"/> is an
	/// offset (km, ecliptic) from <see cref="Body"/>'s centre, fixed in its frame -- the same
	/// convention as <see cref="Battle.Anchor"/> and <see cref="BattleUnit.Pos"/>, since a wreck is
	/// created exactly where its unit died and then just sits there.
	/// </summary>
	class Wreck final
	{
	public:
		int Id = 0;
		cs::String Name;
		cs::Ref<SpaceGod::Core::Faction> Faction;

		/// <summary>The hull this wreck was: null if a settlement defence platform died rather than a ship.</summary>
		cs::Ref<SpaceGod::Core::Ship> Ship;

		int Body = 0;
		DVec3 Pos;

		/// <summary>The battle's anchor and the unit's battle-local position when it died, km, so the wreck is drawn where the fight was drawn.</summary>
		DVec3 Anchor, Local;

		/// <summary>Set when the ship died in an ambush beside a hyperlane rather than at a body.</summary>
		cs::Ref<LaneSite> Site;

		double LengthKm = 0;
		double CreatedJd = 0;

		/// <summary>Days after <see cref="CreatedJd"/> this wreck breaks up and disperses for good.</summary>
		double LifetimeDays = 0;

		/// <summary>What a salvager still finds aboard: cargo the ship was carrying when it died.</summary>
		cs::List<SalvageStack> Salvage;

		/// <summary>
		/// The pieces the hull broke into when it died (U.SPC-T596, see <see cref="ModularHull.BreakUp"/>): the main hulk
		/// first, then every piece blown off it, each drifting out through the debris field (<see cref="WreckPiece.OffsetKm"/>)
		/// with the alloys and parts it breaks down into aboard (<see cref="HullSalvage.Scrap"/>). Empty for a wreck that was
		/// not a ship with a combat state (a defence platform's, or a bare hull's).
		/// </summary>
		cs::List<cs::Ref<WreckPiece>> Pieces;

		/// <summary>Breaks <see cref="Ship"/>'s hull up into <see cref="Pieces"/>, the same way every time for this wreck.</summary>
		void BreakUp(); // internal

		/// <summary>How fast the debris cloud spreads, in hull lengths per day.</summary>
		static constexpr double DebrisSpreadLengthsPerDay = 0.6;

		/// <summary>The cloud never spreads wider than this many hull lengths, however long the wreck lasts.</summary>
		static constexpr double DebrisMaxLengths = 8;

		/// <summary>Past its lifetime: due to be cleared away the next time <see cref="Wrecks.AdvanceTo"/> runs.</summary>
		bool Expired(double jd) const { return jd - CreatedJd > LifetimeDays; }

		/// <summary>Bigger hulls scatter more pieces: 6 for a fighter-sized wreck up to 24 for a capital ship.</summary>
		int DebrisPieces() const;

		/// <summary>A debris piece's radius in hull lengths while the wreck is fresh (as drawn); it shrinks as the wreck fades.</summary>
		static constexpr double DebrisPieceLengths = 0.035;

		/// <summary>How big each piece of debris is at <paramref name="jd"/>, km: what a hull flying through the field can hit (U.SPC-T60).</summary>
		double DebrisPieceRadiusKm(double jd) const { return LengthKm * DebrisPieceLengths * Fade(jd); }

		/// <summary>Each piece's mass, kt: half the hull it was, shared between the pieces (the rest is gone as gas and dust).</summary>
		double DebrisPieceMassKt() const;

		/// <summary>Piece <paramref name="k"/> as a collision volume, around a wreck centred at <paramref name="centreKm"/>
		/// and moving at <paramref name="velKmS"/> in the caller's frame: pinned, since the field drifts on its own.</summary>
		CollisionVolume DebrisCollider(int k, double jd, DVec3 centreKm, DVec3 velKmS) const;

		/// <summary>Radius of the drifting debris cloud at <paramref name="jd"/>, km: it starts at the hull and spreads, slowing as it goes.</summary>
		double DebrisRadiusKm(double jd) const;

		/// <summary>1 while the wreck is fresh, fading to 0 over the last fifth of its lifetime as it breaks up.</summary>
		double Fade(double jd) const;

		/// <summary>
		/// Where debris piece <paramref name="k"/> has drifted to at <paramref name="jd"/>, km from the wreck:
		/// each piece keeps its own fixed heading and share of the cloud's radius, so the field spreads without
		/// pieces jumping about from frame to frame.
		/// </summary>
		DVec3 DebrisOffsetKm(int k, double jd) const;

	private:
		static uint32_t Hash(uint32_t x);
	};

	/// <summary>
	/// Every lasting wreck in the system: one is left wherever a ship dies in battle, and each is
	/// cleared away once its own lifetime runs out.
	/// </summary>
	class Wrecks final
	{
	public:
		/// <summary>Lifetime given to a new wreck when <see cref="Add"/> is not told one explicitly, days.</summary>
		double DefaultLifetimeDays = 21;

		cs::List<cs::Ref<Wreck>> Items;

	private:
		int nextId = 0;

	public:
		/// <summary>
		/// Leaves a wreck where <paramref name="unit"/> died, carrying over whatever cargo it still
		/// held. <paramref name="body"/>/<paramref name="anchor"/>/<paramref name="site"/> are the battle's own (see
		/// <see cref="Battle.Body"/>/<see cref="Battle.Anchor"/>/<see cref="Battle.Site"/>), so the wreck sits exactly
		/// where the unit's battle-local <see cref="BattleUnit.Pos"/> placed it.
		/// </summary>
		cs::Ref<Wreck> Add(const cs::Ref<BattleUnit>& unit, int body, DVec3 anchor, double jd, double lifetimeDays = -1,
			const cs::Ref<LaneSite>& site = nullptr);

		/// <summary>
		/// Leaves a wreck where <paramref name="ship"/> broke up outside any battle (in a collision, U.SPC-T60),
		/// <paramref name="offsetKm"/> from <paramref name="body"/>'s centre, fixed in its frame, with its cargo aboard.
		/// </summary>
		cs::Ref<Wreck> Add(const cs::Ref<Ship>& ship, int body, DVec3 offsetKm, double jd, double lifetimeDays = -1);

		/// <summary>The wreck a destroyed ship left, or null once it has been cleared away (or it never left one).</summary>
		cs::Ref<Wreck> Of(const cs::Ref<Ship>& ship) const;

		/// <summary>Clears away every wreck whose lifetime has run out as of <paramref name="jd"/>.</summary>
		void AdvanceTo(double jd);
	};
}
