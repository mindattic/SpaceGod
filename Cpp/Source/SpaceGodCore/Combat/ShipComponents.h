#pragma once
// US-T89: modular ship construction, Mechlab style: a hull's components and the hardpoints and utility sockets on each.

#include <utility>

#include "Cs/Cs.h"
#include "Combat/Facilities.h" // IPartSpec: UtilitySpec's base

namespace SpaceGod::Core
{
	enum class Good : int;
	enum class HullModuleKind : int;
	enum class MountType : int;
	enum class OutfitOutcome : int;
	struct OutfitQuote;
	class CombatSpec;
	class Economy;
	class Loadout;
	class Settlement;
	class Ship;

	/// <summary>
	/// What goes into a component's utility socket (US-T89): the fittings that are neither a gun on a hardpoint nor a
	/// facility inside the hull, but a part of one component's own plant. Saved by value in <see cref="Loadout.Utility"/>,
	/// so new kinds are appended.
	/// </summary>
	enum class UtilityKind : int
	{
		/// <summary>A cluster of manoeuvring thrusters (RCS quads and their feed): the hull turns, rolls and sidesteps faster.</summary>
		ManeuveringThrusters,

		/// <summary>An oxygen plant with its scrubbers and reserve tanks: air for more hands, so the hull carries a bigger
		/// crew (more damage control teams on a capital, more hands to hold the corridors against boarders).</summary>
		LifeSupport,
	};
	SPACEGODCORE_API const char* ToString(UtilityKind v);

	/// <summary>One utility part (US-T89): its name, what a yard builds it from and its mass, both growing with the hull.</summary>
	class SPACEGODCORE_API UtilitySpec final : public IPartSpec
	{
	public:
		UtilityKind Kind{};
		cs::String Name;

		/// <summary>What it does, in a line, for the fitting screen.</summary>
		cs::String Effect;

		/// <summary>The bill of materials, tonnes, for a hull of scale 1 (a destroyer).</summary>
		cs::List<std::pair<Good, double>> Bill;

		/// <summary>Installed mass, tonnes, for a hull of scale 1.</summary>
		double MassTonnes = 0;

		UtilitySpec(UtilityKind kind, cs::String name, cs::String effect, double mass, cs::List<std::pair<Good, double>> bill);

		static const cs::Ref<UtilitySpec>& Of(UtilityKind k);
		static const cs::List<cs::Ref<UtilitySpec>>& Catalogue();

		double Tonnes(int i, double scale) override { return Bill[i].second * scale; }
		double Mass(double scale) override { return MassTonnes * scale; }
		cs::String PartName() const override { return Name; }
		const cs::List<std::pair<Good, double>>& PartBill() const override { return Bill; }
	};

	/// <summary>
	/// One component of a hull (US-T89), as the Mechlab shows it: a structural piece of the hull
	/// (<see cref="HullModule"/>: the bow, a midsection, engineering or a sponson) with the hardpoints mounted on it, each
	/// reserved for one type of part (<see cref="MountType"/>), and its utility sockets. Its type limits are what its
	/// hardpoints allow: a bow with two energy mounts and a missile mount takes two energy weapons and a launcher, never
	/// three lasers.
	/// </summary>
	class SPACEGODCORE_API ShipComponent final
	{
	public:
		/// <summary>The <see cref="HullModule.Id"/> it is in the hull's structure.</summary>
		int Module = 0;

		HullModuleKind Kind{};

		/// <summary>The keel section it is, or is set off (0 the stern); -1 port, +1 starboard for a sponson.</summary>
		int Section = 0, Side = 0;

		/// <summary>"Bow", "Midsection 2", "Engineering", "Port sponson 3".</summary>
		cs::String Name;

		/// <summary>The hardpoints on it, indices into <see cref="Loadout.Slots"/>.</summary>
		cs::List<int> Slots;

		/// <summary>Its utility sockets: indices <see cref="FirstSocket"/> on into <see cref="Loadout.Utility"/>.</summary>
		int FirstSocket = 0, Sockets = 0;

		/// <summary>How many hardpoints of <paramref name="type"/> it has (its type limit), on <paramref name="l"/>.</summary>
		int Limit(const cs::Ref<Loadout>& l, MountType type) const;

		/// <summary>Whether its sockets take <paramref name="kind"/> (thrusters on the bow, engineering and sponsons, life
		/// support amidships and in engineering; a small craft has no room for a second oxygen plant).</summary>
		bool Takes(UtilityKind kind, bool smallCraft) const;
	};

	/// <summary>
	/// Modular ship construction, Mechlab style (US-T89). A hull is built from components (<see cref="ShipComponent"/>): the
	/// pieces of its structure (<see cref="ModularHull.For"/>, so every design has them without a hand-made breakdown),
	/// each carrying the hardpoints mounted on it with their types and sizes (the turrets, launchers, shield and engine
	/// mounts of <see cref="Loadout.Slots"/>) and a few utility sockets of its own: thrusters on the bow, engineering and
	/// sponsons, life support amidships and in engineering. A yard fits a utility part into a socket as it fits a gun on
	/// a hardpoint (bought from its market, labour and tax, the old part sold back, see <see cref="Fit"/>), and the
	/// hull's fighting spec is rebuilt from the loadout (<see cref="Apply"/>, called by <see cref="Loadout.Build"/>), so
	/// what is fitted is what fights. Sockets start empty: no stock design and no AI yard fits a utility part, so AI
	/// fights, the Bench and every hull nobody refitted are unchanged; it is plain arithmetic on the spec, so co-op stays
	/// in step (a fit is a frame's entry like any other refit).
	/// </summary>
	class SPACEGODCORE_API ShipComponents final
	{
	public:
		/// <summary>The turn rate, lateral thrust and torque each thruster cluster adds, as a share of the design's, and the most
		/// all of them together add.</summary>
		static constexpr double ThrusterShare = 0.12; static constexpr double ThrusterCap = 0.6;

		/// <summary>The crew each oxygen plant gives air for, as a share of the design's complement, and the most all add.</summary>
		static constexpr double LifeSupportShare = 0.12; static constexpr double LifeSupportCap = 0.5;

		/// <summary>The hull's components, bow first, then the keel aft to engineering, then the sponsons, with their
		/// hardpoints and sockets. Worked out afresh from the design each time (it is never saved).</summary>
		static cs::List<ShipComponent> Of(const cs::Ref<Loadout>& l);

		/// <summary>How many utility sockets a component of <paramref name="kind"/> has.</summary>
		static int SocketsOf(HullModuleKind kind, bool smallCraft);

		/// <summary>Every utility socket the hull has.</summary>
		static int SocketCount(const cs::Ref<Loadout>& l);

		/// <summary>What is in socket <paramref name="socket"/>, or null if it is empty.</summary>
		static cs::Nullable<UtilityKind> At(const cs::Ref<Loadout>& l, int socket);

		/// <summary>Utility parts of <paramref name="kind"/> fitted.</summary>
		static int Count(const cs::Ref<Loadout>& l, UtilityKind kind);

		/// <summary>Whether any utility part is fitted.</summary>
		static bool Any(const cs::Ref<Loadout>& l);

		/// <summary>Dry mass the utility parts add, tonnes.</summary>
		static double AddedMassTonnes(const cs::Ref<Loadout>& l);

		/// <summary>What the utility parts do to <paramref name="spec"/> (already built from the rest of the loadout):
		/// thrusters raise its turn rate, side thrust and torque, oxygen plants its crew.</summary>
		static void Apply(const cs::Ref<CombatSpec>& spec, const cs::Ref<Loadout>& l);

		/// <summary>The component socket <paramref name="socket"/> is on (index into <paramref name="parts"/>), or -1.</summary>
		static int ComponentOf(const cs::List<ShipComponent>& parts, int socket);

		/// <summary>Whether <paramref name="kind"/> may go into socket <paramref name="socket"/> (null: emptying it, always
		/// allowed for a socket that has something in it).</summary>
		static bool CanFit(const cs::Ref<Loadout>& l, int socket, cs::Nullable<UtilityKind> kind);

		/// <summary>What putting <paramref name="kind"/> into socket <paramref name="socket"/> (null: taking out what is there)
		/// costs at <paramref name="yard"/>.</summary>
		static OutfitQuote Quote(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, int socket, cs::Nullable<UtilityKind> kind);

		/// <summary>
		/// Fits <paramref name="kind"/> into utility socket <paramref name="socket"/> of <paramref name="ship"/> at
		/// <paramref name="yard"/> (null: strips the socket), paid for as any refit (<see cref="Outfitting.Fit"/>), and
		/// rebuilds the hull's combat spec. Hands an oxygen plant adds air for are drafted from the owner's reserve at once.
		/// Nothing changes unless it returns <see cref="OutfitOutcome.Ok"/>.
		/// </summary>
		static OutfitOutcome Fit(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, int socket,
			cs::Nullable<UtilityKind> kind, OutfitQuote& quote);
	};
}
