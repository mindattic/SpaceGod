#pragma once
// US-T89: modular ship construction, Mechlab style: a hull's components and the hardpoints and utility sockets on each.
// US-T269: utility parts shot out with their component, three more kinds, and a tonnage limit per component.

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
	class HullModule;
	class Loadout;
	class ModularHull;
	class Settlement;
	class Ship;
	class ShipCombat;

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

		/// <summary>US-T269: a phase-change heat sink on its own loop: the hull sheds more heat, so a capital's guns and drive
		/// run longer before its crew throttles them, and a player's guns cool faster.</summary>
		HeatSink,

		/// <summary>US-T269: a bank of storage cells off the reactor bus: the shields regrow faster and the boost lasts longer.</summary>
		PowerCell,

		/// <summary>US-T269: doubled frames and bulkheads through one component: it holds out far longer before it is cut away,
		/// and the hull as a whole takes a little more.</summary>
		ReinforcedBulkhead,
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

		/// <summary>Installed mass, tonnes, for a hull of scale 1: what counts against its component's tonnage (US-T269).</summary>
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
	/// three lasers; its utility parts together may weigh no more than its tonnage (US-T269).
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
		/// support amidships and in engineering, a small craft having no room for a second oxygen plant; heat sinks and
		/// bulkheads in the keel sections; power cells anywhere). Its tonnage may still rule a part out
		/// (<see cref="ShipComponents.CanFit"/>).</summary>
		bool Takes(UtilityKind kind, bool smallCraft) const;

		/// <summary>US-T269: the most its utility parts may weigh together, tonnes, on a hull of <paramref name="scale"/>.</summary>
		double CapacityTonnes(double scale) const;
	};

	/// <summary>
	/// Modular ship construction, Mechlab style (US-T89). A hull is built from components (<see cref="ShipComponent"/>): the
	/// pieces of its structure (<see cref="ModularHull.For"/>, so every design has them without a hand-made breakdown),
	/// each carrying the hardpoints mounted on it with their types and sizes (the turrets, launchers, shield and engine
	/// mounts of <see cref="Loadout.Slots"/>) and a few utility sockets of its own: thrusters on the bow, engineering and
	/// sponsons, life support amidships and in engineering, and (US-T269) heat sinks, power cells and reinforced bulkheads,
	/// as far as each component's tonnage allows. A yard fits a utility part into a socket as it fits a gun on a hardpoint
	/// (bought from its market, labour and tax, the old part sold back, see <see cref="Fit"/>), and the hull's fighting
	/// spec is rebuilt from the loadout (<see cref="Apply"/>, called by <see cref="Loadout.Build"/>), so what is fitted is
	/// what fights. A part goes down with its component (US-T269, <see cref="KnockOut"/>) and works again once a yard
	/// mends the hull whole. Sockets start empty on every stock design; an AI yard fits a few to some designs
	/// (<see cref="Outfitting.AiRefit"/>: a navy's capital a bulkhead, a raider thrusters, a radiator-cooled capital a heat
	/// sink). It is plain arithmetic on the spec and the structure, so co-op stays in step (a fit is a frame's entry like
	/// any other refit).
	/// </summary>
	class SPACEGODCORE_API ShipComponents final
	{
	public:
		/// <summary>The turn rate, lateral thrust and torque each thruster cluster adds, as a share of the design's, and the most
		/// all of them together add.</summary>
		static constexpr double ThrusterShare = 0.12; static constexpr double ThrusterCap = 0.6;

		/// <summary>The crew each oxygen plant gives air for, as a share of the design's complement, and the most all add.</summary>
		static constexpr double LifeSupportShare = 0.12; static constexpr double LifeSupportCap = 0.5;

		/// <summary>US-T269: the heat each sink lets the hull shed, as a share of what it sheds without, and the most all add.</summary>
		static constexpr double HeatSinkShare = 0.15; static constexpr double HeatSinkCap = 0.6;

		/// <summary>US-T269: the shield regrowth and boost each power cell adds, as a share of the design's, and the most all add.</summary>
		static constexpr double PowerCellShare = 0.1; static constexpr double PowerCellCap = 0.5;

		/// <summary>US-T269: the hull points each reinforced bulkhead adds, as a share of the design's, and the most all add;
		/// and the structure it adds to its own component (and the joints round it), as a share of theirs.</summary>
		static constexpr double BulkheadHullShare = 0.04; static constexpr double BulkheadHullCap = 0.2;
		static constexpr double BulkheadStructure = 0.5;

		/// <summary>US-T269: a utility part is shot out when what is left of its component's structure falls below this share
		/// (or the component is shattered or cut away): it works no more until a yard mends the hull whole.</summary>
		static constexpr double KnockOutAt = 0.35;

		/// <summary>US-T269: what a component's utility parts may weigh together, tonnes, for a hull of scale 1, by what it is.
		/// Engineering's two sockets take thrusters and an oxygen plant, but not two oxygen plants.</summary>
		static constexpr double BowTonnes = 40; static constexpr double MidsectionTonnes = 50;
		static constexpr double EngineeringTonnes = 80; static constexpr double SponsonTonnes = 30;

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

		/// <summary>Utility parts of every kind fitted.</summary>
		static int Fitted(const cs::Ref<Loadout>& l);

		/// <summary>Whether any utility part is fitted.</summary>
		static bool Any(const cs::Ref<Loadout>& l);

		/// <summary>Dry mass the utility parts add, tonnes.</summary>
		static double AddedMassTonnes(const cs::Ref<Loadout>& l);

		/// <summary>US-T269: the tonnes the parts in <paramref name="c"/>'s sockets come to, leaving out socket
		/// <paramref name="except"/>.</summary>
		static double UsedTonnes(const cs::Ref<Loadout>& l, const ShipComponent& c, int except = -1);

		/// <summary>What the utility parts do to <paramref name="spec"/> (already built from the rest of the loadout):
		/// thrusters raise its turn rate, side thrust and torque, oxygen plants its crew, heat sinks the heat it sheds, power
		/// cells its shield regrowth and boost, bulkheads its hull points. It also keeps on the spec what is fitted where and
		/// the spec as it was without them, so a part shot out can be taken off again (<see cref="Rebuilt"/>).</summary>
		static void Apply(const cs::Ref<CombatSpec>& spec, const cs::Ref<Loadout>& l);

		/// <summary>US-T269: a component's key in the hull's structure (its keel section and side), which picks out the same
		/// <see cref="HullModule"/> in any structure laid out from the design, whatever is fitted on its hardpoints.</summary>
		static int ModuleKey(int section, int side) { return section * 3 + side + 1; }

		/// <summary>US-T269: the module of <paramref name="hull"/> with <paramref name="key"/>, or null.</summary>
		static cs::Ref<HullModule> ModuleFor(const cs::Ref<ModularHull>& hull, int key);

		/// <summary>US-T269: whether the part fitted in socket <paramref name="socket"/> of the hull fighting with
		/// <paramref name="spec"/> has been shot out (false for an empty socket or one still working).</summary>
		static bool ShotOut(const cs::Ref<CombatSpec>& spec, int socket);

		/// <summary>US-T269: <paramref name="spec"/> (one <see cref="Apply"/> built) with only the parts in <paramref name="live"/>
		/// working (a <see cref="UtilityKind"/>'s value per socket, -1 for none): a new spec of the same design, its utility
		/// parts' effects worked out afresh.</summary>
		static cs::Ref<CombatSpec> Rebuilt(const cs::Ref<CombatSpec>& spec, const cs::List<int>& live);

		/// <summary>
		/// US-T269: after a hit on <paramref name="combat"/>'s structure, every utility part whose component has been shot below
		/// <see cref="KnockOutAt"/>, shattered or cut away is shot out, and the hull fights on without it (its spec rebuilt,
		/// <see cref="Rebuilt"/>; hull points past a lost bulkhead's are gone with it). Plain arithmetic on the structure, no
		/// dice, so every peer and every replay loses the same part on the same hit. Returns how many were lost.
		/// </summary>
		static int KnockOut(const cs::Ref<ShipCombat>& combat);

		/// <summary>US-T269: <paramref name="spec"/> with every utility part it was fitted with working again (itself when none
		/// was shot out): what a hull mended whole fights with (<see cref="ShipCombat.Repair"/>).</summary>
		static cs::Ref<CombatSpec> Restored(const cs::Ref<CombatSpec>& spec);

		/// <summary>US-T269: what <paramref name="spec"/>'s working bulkheads add to the structure of their own components and
		/// the joints round them, called as <paramref name="hull"/> is laid out.</summary>
		static void Reinforce(const cs::Ref<ModularHull>& hull, const cs::Ref<CombatSpec>& spec);

		/// <summary>The component socket <paramref name="socket"/> is on (index into <paramref name="parts"/>), or -1.</summary>
		static int ComponentOf(const cs::List<ShipComponent>& parts, int socket);

		/// <summary>Whether <paramref name="kind"/> may go into socket <paramref name="socket"/> (null: emptying it, always
		/// allowed for a socket that has something in it): its component takes that kind, and (US-T269) has the tonnage
		/// left for it once what is in the socket now comes out.</summary>
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

	private:
		/// <summary>The effects of the parts in <paramref name="live"/> on <paramref name="spec"/> (built without them);
		/// <paramref name="crewBase"/> is the complement an oxygen plant's share is of.</summary>
		static void Effects(const cs::Ref<CombatSpec>& spec, const cs::List<int>& live, double crewBase);
	};
}
