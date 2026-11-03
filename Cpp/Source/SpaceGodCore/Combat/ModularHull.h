#pragma once
// Port of Assets/UnitySpace/Core/Combat/ModularHull.cs (see docs/MIGRATION.md).

#include <cstdint>
#include <vector>

#include "Cs/Cs.h"
#include "DVec3.h"
#include "Combat/Wreck.h" // SalvageStack, by value

namespace SpaceGod::Core
{
	enum class Good : int;
	enum class SubsystemKind : int;
	class CombatSpec;
	class SubsystemDef;
	class ShipCombat;
	class Settlement;

	/// <summary>
	/// What part of a hull's structure a <see cref="HullModule"/> is (U.SPC-T596). Not to be confused with
	/// <see cref="ModuleKind"/>, the parts a yard fits on a hardpoint: a hull module is a piece of the hull itself, the
	/// thing those parts are bolted to, and the piece that comes away when the hull breaks apart.
	/// </summary>
	enum class HullModuleKind : int
	{
		/// <summary>The foremost keel section: the bow and whatever is mounted in it.</summary>
		Bow,

		/// <summary>A keel section between the bow and the stern.</summary>
		Midsection,

		/// <summary>The aftmost keel section: the drive section.</summary>
		Engineering,

		/// <summary>An outrigger off one side of a keel section, carrying the mounts set out past the hull's own beam
		/// (a cruiser's broadside launchers, a fighter's wing guns). The first thing to be shot off.</summary>
		Sponson,
	};
	const char* ToString(HullModuleKind v);

	/// <summary>
	/// One structural piece of a hull (U.SPC-T596): a section of the keel or a sponson off one, with its own structure
	/// points and the hardpoints mounted on it. Positions are in ship lengths, hull frame (right, up, forward), the same
	/// frame as <see cref="SubsystemDef"/>'s X, Y, Z.
	/// </summary>
	class HullModule final
	{
	public:
		int Id = 0;
		HullModuleKind Kind{};

		/// <summary>Which keel section it is, or is set off (0 the stern, counting forward).</summary>
		int Section = 0;

		/// <summary>-1 port, +1 starboard for a sponson; 0 for a keel section.</summary>
		int Side = 0;

		/// <summary>The module's centre and half its size along each axis, ship lengths, hull frame.</summary>
		DVec3 Centre, HalfExtent;

		/// <summary>Its share of the hull's dry mass (every module's together come to 1).</summary>
		double MassShare = 0;

		/// <summary>Its structure: how much it can take before it shatters, hit points, and what it has left.</summary>
		double MaxStructure = 0, Structure = 0;

		/// <summary>The hardpoints mounted on it: indices into <see cref="CombatSpec.Layout"/> and <see cref="ShipCombat.Subsystems"/>.</summary>
		cs::List<int> Hardpoints;

		/// <summary>It has come away from the hull it was part of (reported once by <see cref="ModularHull.Separate"/>).</summary>
		bool Detached = false;

		bool Shattered() const { return Structure <= 0; }

		/// <summary>What is left of its structure, 0 to 1.</summary>
		double Condition() const { return MaxStructure <= 0 ? 0 : cs::Math::Clamp(Structure / MaxStructure, 0.0, 1.0); }

		/// <summary>How far a point on the hull lies outside the module's box, ship lengths (0 inside it).</summary>
		double DistanceTo(DVec3 local) const;
	};

	/// <summary>
	/// Where two <see cref="HullModule"/>s are joined (U.SPC-T596): a ring of frames between two keel sections, or a
	/// sponson's root. Shots that land on either module load it; when its strength is gone it fails and the two
	/// modules are no longer held together.
	/// </summary>
	class HullJoint final
	{
	public:
		int A = 0, B = 0;

		/// <summary>Where it is, ship lengths, hull frame.</summary>
		DVec3 At;

		double MaxStrength = 0, Strength = 0;

		bool Broken() const { return Strength <= 0; }

		double Condition() const { return MaxStrength <= 0 ? 0 : cs::Math::Clamp(Strength / MaxStrength, 0.0, 1.0); }

		int Other(int module) const { return module == A ? B : A; }

		bool Joins(int module) const { return A == module || B == module; }
	};

	/// <summary>
	/// A piece of hull that has come away (U.SPC-T596): one or more modules still joined to each other but no longer to
	/// the rest of the ship, with every hardpoint mounted on them. Everything downstream works on this: the hull that
	/// lost it loses those hardpoints (<see cref="ModularHulls.ApplyBreak"/>), a wreck scatters its pieces as drifting
	/// wreckage (<see cref="WreckPiece"/>), and a salvager breaks a piece down or sells its parts (<see cref="HullSalvage"/>).
	/// </summary>
	class HullFragment final
	{
	public:
		/// <summary>The modules in it, by <see cref="HullModule.Id"/>.</summary>
		cs::List<int> Modules;

		/// <summary>The hardpoints mounted on it, indices into <see cref="ShipCombat.Subsystems"/>.</summary>
		cs::List<int> Hardpoints;

		/// <summary>Its share of the hull's dry mass.</summary>
		double MassShare = 0;

		/// <summary>Its centre of mass, ship lengths, hull frame.</summary>
		DVec3 Centroid;

		/// <summary>Every module in it shattered: rubble, worth only its scrap.</summary>
		bool Shattered = false;

		/// <summary>It holds the hull's <see cref="ModularHull.Root"/>: the main hulk rather than a piece blown off it.</summary>
		bool Hulk = false;
	};

	/// <summary>
	/// A hull as the structure it is built from (U.SPC-T596): its <see cref="HullModule"/>s and the
	/// <see cref="HullJoint"/>s holding them together, the graph damage, wrecks, salvage and trade work on. It is a
	/// second account alongside <see cref="ShipCombat.Hull"/> rather than a replacement for it: the hull points still say
	/// whether a ship lives, the structure says where it is coming apart. Every hull's modules are laid out from its
	/// design (<see cref="For"/>), so no design needs a hand-made breakdown: the keel is cut into sections by length, and
	/// the mounts set out past the hull's beam ride on sponsons. The section that holds the bridge (else the reactor) is
	/// the <see cref="Root"/>: whatever is still joined to it is the ship, anything else has broken away. The design and
	/// what builds on it next are in docs/MODULAR_SHIPS.md.
	/// </summary>
	class ModularHull final
	{
	public:
		/// <summary>Hull length, km, per keel section beyond the first two: a destroyer has three, a cruiser five and a
		/// battleship or carrier eight, the most.</summary>
		static constexpr double KmPerSection = 0.4;

		static constexpr int MaxSections = 8;

		/// <summary>A mount further out from the keel than this share of the hull's half-beam rides on a sponson.</summary>
		static constexpr double SponsonReach = 0.6;

		/// <summary>A sponson's share of the hull's mass; the keel sections share the rest.</summary>
		static constexpr double SponsonMassShare = 0.05;

		/// <summary>A module's structure for its share of the hull's mass, times the hull's points: each piece holds out
		/// a little longer than its share of the hull, so a hull is far more often holed than cut apart.</summary>
		static constexpr double StructurePerMass = 1.5;

		/// <summary>A keel joint's strength and a sponson root's, shares of the hull's points.</summary>
		static constexpr double KeelJointShare = 0.2, SponsonJointShare = 0.06;

		/// <summary>The share of a hit on a module that loads the joints around it, shared between them by how near
		/// each is to where it struck.</summary>
		static constexpr double JointLoad = 0.5;

		cs::List<cs::Ref<HullModule>> Modules;
		cs::List<cs::Ref<HullJoint>> Joints;

		/// <summary>The module the ship is: the keel section holding its bridge, else its reactor, else its middle one.</summary>
		int Root = 0;

		/// <summary>How many keel sections a hull of <paramref name="lengthKm"/> is cut into.</summary>
		static int KeelSections(double lengthKm);

		/// <summary>The structure of a hull built to <paramref name="spec"/>, whole: its keel sections from stern to
		/// bow, joined end to end, and a sponson off any section with mounts set out past the beam on that side.</summary>
		static cs::Ref<ModularHull> For(const cs::Ref<CombatSpec>& spec);

	private:
		/// <summary>Whether a mount rides on a sponson: set out past <see cref="SponsonReach"/> of the half-beam, and not
		/// something only the keel can carry (a buried bridge, a spinal gun, the reactor, a hangar, the drive).</summary>
		static bool OnSponson(const cs::Ref<SubsystemDef>& d, double halfBeam);

		static int RootSection(const cs::Ref<ModularHull>& h, const cs::List<cs::Ref<SubsystemDef>>& layout, int n);

		void AddJoint(int a, int b, DVec3 at, double strength);

	public:
		cs::Ref<HullModule> Module(int id) const { return Modules[id]; }

		/// <summary>The module a point on the hull (ship lengths, hull frame) lies in, or the nearest one to it, among
		/// those not yet come away (the root always is one, so there is always an answer).</summary>
		cs::Ref<HullModule> ModuleAt(DVec3 local) const;

		/// <summary>The module hardpoint <paramref name="subsystem"/> is mounted on.</summary>
		cs::Ref<HullModule> ModuleOf(int subsystem) const;

		/// <summary>Whether the module is still held to the root through unbroken joints.</summary>
		bool Attached(int module) const { return Reachable()[static_cast<size_t>(module)]; }

		/// <summary>
		/// A hit of <paramref name="damage"/> (what got through shields and armour) at a point on the hull, ship lengths,
		/// hull frame: the module it lands on takes it into its structure, and the joints round that module take
		/// <see cref="JointLoad"/> of it, the nearest the most. A module whose structure is gone shatters and lets go of
		/// everything joined to it. Returns the pieces that came away (see <see cref="Separate"/>), empty if none did.
		/// </summary>
		cs::List<cs::Ref<HullFragment>> Strike(DVec3 local, double damage);

	private:
		static double Nearness(const cs::Ref<HullJoint>& j, DVec3 local);

	public:
		/// <summary>
		/// Every piece no longer held to the <see cref="Root"/> that has not come away before, each a set of modules still
		/// joined to each other; its modules are marked <see cref="HullModule.Detached"/>, so each piece is reported once.
		/// </summary>
		cs::List<cs::Ref<HullFragment>> Separate();

		/// <summary>
		/// A hull destroyed outright breaks up (U.SPC-T596): every joint fails with a chance that grows the more it was
		/// already strained (a quarter for a sound one, all but certain for one nearly gone), fixed by
		/// <paramref name="seed"/> so the same wreck always breaks the same way, and at least one always does. Returns
		/// every piece the hull is now in, the main hulk first (<see cref="HullFragment.Hulk"/>), including any that came
		/// away earlier in the fight. Does nothing to a hull that is a single module but hand it back whole.
		/// </summary>
		cs::List<cs::Ref<HullFragment>> BreakUp(int seed);

		/// <summary>Mends every module's structure and every joint still holding by <paramref name="share"/> of its full
		/// strength; a broken joint and a module that came away stay lost until a yard rebuilds the hull.</summary>
		void Repair(double share);

		/// <summary>Whether every module is still part of the ship, unshattered, and every joint holds.</summary>
		bool Intact() const;

	private:
		// bool[] is a std::vector<bool> here: cs::List<bool> cannot hand out a bool& (vector<bool> packs its bits).
		std::vector<bool> Reachable() const;

		/// <summary>The modules joined to <paramref name="start"/> through unbroken joints, as one piece.</summary>
		cs::Ref<HullFragment> Gather(int start, std::vector<bool>& seen, bool includeDetached) const;

	public:
		static uint32_t Hash(uint32_t x); // internal
	};

	/// <summary>What a hull that has lost a piece loses with it (U.SPC-T596).</summary>
	class ModularHulls final
	{
	public:
		/// <summary>The share of a lost piece's mass the hull's points lose with it: the rest of the damage that cut it
		/// away was already done by the hits themselves.</summary>
		static constexpr double HullLossPerMass = 0.5;

		/// <summary>
		/// <paramref name="piece"/> has come away from <paramref name="combat"/>'s hull: every hardpoint on it goes with it
		/// (shot away, its shield emitter's field down), and the hull's points lose <see cref="HullLossPerMass"/> of its
		/// share of them, never below one, so cutting a piece off never kills a hull by itself. Returns how many standing
		/// hardpoints were lost.
		/// </summary>
		static int ApplyBreak(const cs::Ref<ShipCombat>& combat, const cs::Ref<HullFragment>& piece);
	};

	/// <summary>
	/// What a piece of hull is worth to a salvager (U.SPC-T596): what it breaks down into, and what its sound parts
	/// fetch whole. The structure is mostly alloys; a part a yard fits comes back as its own bill of materials
	/// (<see cref="ModuleSpec.Bill"/>), as much of it as the part's health leaves; a part no yard fits (a bridge, a reactor, a
	/// hangar) as the one good it is mostly made of (<see cref="SalvageSession.ScrapOf"/>).
	/// </summary>
	class HullSalvage final
	{
	public:
		/// <summary>The share of a structure's dry mass that is recovered as alloys: the rest is holed, buckled or gone.</summary>
		static constexpr double StructureRecovery = 0.3;

		/// <summary>The share of a part's bill a breaker gets back when it cuts the part up.</summary>
		static constexpr double PartRecovery = 0.5;

		/// <summary>What a sound part sold whole fetches against its bill at the port's prices: secondhand, uncertified.</summary>
		static constexpr double SecondhandShare = 0.6;

		/// <summary>Tonnes a part no yard fits breaks down into, per hit point it was built with.</summary>
		static constexpr double TonnesPerHp = 0.05;

		/// <summary>
		/// What <paramref name="piece"/> of a hull built to <paramref name="spec"/> breaks down into: its structure's
		/// alloys by what is left of it, and every part still on it by its health (<paramref name="combat"/>'s, or whole when
		/// there is no live state). A shattered piece is rubble and gives only half its structure's worth.
		/// </summary>
		static cs::List<SalvageStack> Scrap(const cs::Ref<ModularHull>& hull, const cs::Ref<HullFragment>& piece, const cs::Ref<CombatSpec>& spec,
			const cs::Ref<ShipCombat>& combat = nullptr);

		/// <summary>What <paramref name="stacks"/> sell for at <paramref name="port"/>'s prices, or at base prices with no port.</summary>
		static double Value(const cs::List<SalvageStack>& stacks, const cs::Ref<Settlement>& port = nullptr);

		/// <summary>What a sound part of <paramref name="kind"/> at <paramref name="health"/> sells for whole at
		/// <paramref name="port"/> (base prices with none), on a hull of <paramref name="lengthKm"/>: its bill at
		/// <see cref="SecondhandShare"/>; 0 for a part no yard fits, which a breaker only cuts up.</summary>
		static double PartValue(SubsystemKind kind, double health, double lengthKm, const cs::Ref<Settlement>& port = nullptr);

	private:
		static double PriceOf(Good g, const cs::Ref<Settlement>& port);
	};

	/// <summary>
	/// One piece of a wreck (U.SPC-T596): the main hulk, or a piece the hull broke into, drifting off through the debris
	/// field with what it breaks down into aboard. A piece starts where it was on the hull and drifts out along its own
	/// heading as the field spreads (<see cref="Wreck.DebrisRadiusKm"/>), so the pieces of a wreck scatter but stay
	/// within its field.
	/// </summary>
	class WreckPiece final
	{
	public:
		int Index = 0;
		cs::Ref<HullFragment> Fragment;

		/// <summary>What it still breaks down into for a salvager: it comes off as it is taken.</summary>
		cs::List<SalvageStack> Scrap;

		bool Hulk() const { return Fragment->Hulk; }

		/// <summary>Where it has drifted to at <paramref name="jd"/>, km from the wreck's centre, in the wreck's frame. The
		/// hulk stays at the centre.</summary>
		DVec3 OffsetKm(const cs::Ref<Wreck>& wreck, double jd) const;
	};
}
