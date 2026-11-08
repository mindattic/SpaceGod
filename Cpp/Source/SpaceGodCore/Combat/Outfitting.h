#pragma once
// Port of Assets/UnitySpace/Core/Combat/Outfitting.cs (see docs/MIGRATION.md).

#include <memory>
#include <utility>

#include "Cs/Cs.h"
#include "Combat/Facilities.h" // IPartSpec: ModuleSpec's base
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	enum class Good : int;
	enum class ShipClass : int;
	enum class SubsystemKind : int;
	enum class HullCategory : int;
	enum class HardpointSize : int;
	class CombatSpec;
	class Settlement;
	class Ship;
	class Economy;
	class CargoHold;
	class Interior;
	class BuildPlan;

	/// <summary>What a yard can fit into a hull (U.SPC-T65).</summary>
	enum class ModuleKind : int
	{
		LaserTurret,
		HeavyLaser,
		Railgun,
		MissileLauncher,
		PointDefense,

		/// <summary>A stock shield generator, as the design was built with.</summary>
		ShieldGenerator,

		/// <summary>A heavier generator: half again the stock one's share of shield and regen.</summary>
		HardenedShield,

		/// <summary>A stock fusion drive, as the design was built with.</summary>
		Engine,

		/// <summary>A bigger drive: 30% more thrust, at a little more mass.</summary>
		TorchEngine,

		/// <summary>Plate bolted over the hull: more hull points and armour, and mass to push.</summary>
		ArmourPlating,

		/// <summary>Racks in the hull's free space (a facility, U.SPC-T470): more trade hold and two more rows in the grid hold.</summary>
		CargoRack,

		/// <summary>A sensor suite: half the hull's base scanner range again and finer resolution (see <see cref="SensorSuite.Of"/>),
		/// so contacts are seen further off and made out sooner, and a warship's sweep for hidden havens reaches a million km further.</summary>
		Scanner,

		/// <summary>Cells for prisoners taken off prizes, handed over in port for bounty or ransom.</summary>
		Brig,

		// The dogfight arsenal (U.SPC-T271).
		Autocannon,
		LightRailgun,
		EmpLauncher,
		MineLayer,
		AutoTurret,

		/// <summary>A capital torpedo tube (U.SPC-T377), with its first six rounds.</summary>
		TorpedoTube,

		/// <summary>A capital ship's spinal tachyon lance (U.SPC-T487, see <see cref="TachyonLances"/>): a bow mount of a
		/// capital hull only, built only at the greatest yards, and dear.</summary>
		TachyonLance,

		/// <summary>A phaser-style beam array (U.SPC-T488): destroyers and cruisers only, on any energy mount (U.SPC-T508), a point-defence one included.</summary>
		PhaserArray,

		/// <summary>A plasma cannon (U.SPC-T488): the brawlers' heavy gun (battleships, breachers), on a medium or large energy mount.</summary>
		PlasmaCannon,

		// Heavy warheads and the anti-fighter screen (U.SPC-T489).

		/// <summary>An antimatter torpedo launcher, with its first four rounds: a medium or large missile mount that bears on the bow, or a tube, takes one.</summary>
		AntimatterTorpedo,

		/// <summary>A rotary flak cannon: any ballistic mount takes one.</summary>
		FlakCannon,

		/// <summary>An interceptor missile launcher, with its first twenty rounds: any missile mount takes one.</summary>
		InterceptorMissile,

		/// <summary>A spinal coilgun (U.SPC-T490, see <see cref="SpinalMounts"/>): only a spinal mount takes one.</summary>
		Coilgun,

		// The full arsenal (US-T101), appended so the catalogue's indices and saved values keep theirs.

		/// <summary>A vulcan rotary cannon: any ballistic mount takes one, a fighter's nose gun included.</summary>
		VulcanCannon,

		/// <summary>A hive missile launcher, with its first four rounds: any missile mount takes one.</summary>
		HiveLauncher,

		/// <summary>A dumb-fire rocket pod, with its first twenty-four rockets: any missile mount takes one.</summary>
		RocketPod,

		/// <summary>US-T264: a mining laser fitted into the hull's free space (a facility, see <see cref="HandMining"/>): the
		/// primary trigger out of a fight cuts asteroids into ore crates. Each one fitted is a grade, up to 3, on top of a
		/// miner's own industrial laser (grade 2).</summary>
		MiningLaser,

		/// <summary>US-T102: a nanite repair system (see <see cref="NaniteRepairs"/>): a shield mount takes one in place of
		/// a shield array, giving up that patch's field for a swarm that rebuilds the hull, its damaged sections and its
		/// standing hardpoints over a fight. It can be shot out like any mount.</summary>
		NaniteRepair,
	};
	SPACEGODCORE_API const char* ToString(ModuleKind v);

	/// <summary>
	/// What kind of part a hull's hardpoint is reserved for, MechWarrior style (U.SPC-T508). A weapon hardpoint is
	/// <see cref="Ballistic"/> (railguns, autocannons, flak, an auto turret), <see cref="Missile"/> (missile racks,
	/// torpedo tubes, antimatter torpedoes, interceptors, an EMP launcher, mines) or <see cref="Energy"/> (lasers,
	/// point-defence lasers, phaser arrays, plasma cannons, the tachyon lance), and each has a size
	/// (<see cref="HardpointSize"/>): it takes a part of its own type no bigger than itself. Shield and engine mounts
	/// take only generators and drives. So a design is shaped by which kinds of mount its hull has, not just how many.
	/// A module's type and size are data on its <see cref="ModuleSpec"/>. What is inside the hull is not a hardpoint but
	/// a facility (<see cref="FacilityKind"/>, U.SPC-T470). A dreadnought's <see cref="Spinal"/> mount (U.SPC-T490) is a
	/// gun the length of the hull, a type of its own: it takes a railgun, a coilgun, a tachyon lance or a plasma cannon
	/// built to its length (<see cref="SpinalMounts"/>).
	/// </summary>
	enum class MountType : int { Ballistic, Missile, Energy, Shield, Engine, Spinal };
	SPACEGODCORE_API const char* ToString(MountType v);

	class OutfitSlot;

	/// <summary>One module: what it is called, what a yard builds it from (tonnes of alloys, electronics,
	/// machinery and munitions, scaled with the hull for plate, drives, generators and racks) and its mass.</summary>
	class SPACEGODCORE_API ModuleSpec final : public IPartSpec
	{
	public:
		ModuleKind Kind{};
		cs::String Name;

		/// <summary>The bill of materials, tonnes, for a hull of scale 1 (a destroyer) if <see cref="Scaled"/>.</summary>
		// A (Good good, double tonnes) line is a std::pair: .first the good, .second the tonnes (see IPartSpec).
		cs::List<std::pair<Good, double>> Bill;

		/// <summary>Installed mass, tonnes (per unit of hull scale if <see cref="Scaled"/>).</summary>
		double MassTonnes = 0;

		/// <summary>Whether the bill and mass grow with the hull (plate, drives, generators, racks) or are one fixed part (a gun).</summary>
		bool Scaled = false;

		/// <summary>The kind of hardpoint it goes on (U.SPC-T508), or null for a facility module (plate, racks, a scanner
		/// suite, a brig), which goes inside the hull.</summary>
		cs::Nullable<MountType> Mount;

		/// <summary>The smallest hardpoint of its <see cref="Mount"/> type that takes it.</summary>
		HardpointSize Size{};

		/// <summary>A spinal or fixed part (a railgun, a torpedo tube, an antimatter launcher, the tachyon lance): it goes
		/// only on a mount that bears on the bow or is a fixed launcher (<see cref="OutfitSlot.Spinal"/>).</summary>
		bool Spinal = false;

		/// <summary>How far a weapon of this module can bear off its mount's axis, deg (U.SPC-T533, see <see cref="FireCones"/>): a
		/// turret's wide cone round its mount's normal, a spinal or fixed part's narrow one along the keel (180 for a part that
		/// aims nothing). A hull frame may give a hardpoint its own (<see cref="SubsystemDef.ConeHalfDeg"/>).</summary>
		double ConeHalfDeg = 0;

		// Private in C#; public here so cs::New (make_shared) can reach it. Only the catalogue builds one.
		ModuleSpec(ModuleKind kind, cs::String name, double mass, bool scaled, cs::Nullable<MountType> mount, HardpointSize size, bool spinal, double coneHalfDeg,
			cs::List<std::pair<Good, double>> bill);

	private:
		// The C# private constants B, Mi, E, Sh, En (mount types) and S, M, L (sizes) live in Outfitting.cpp, where the
		// catalogue that uses them is built.

		// Each module's mount type and size (U.SPC-T508): the light guns and launchers (and a phaser array, light enough
		// for a point-defence mount) are small, the heavy guns, tubes and launchers medium, the tachyon lance large. Then its
		// fire cone's half-angle (U.SPC-T533): turrets wide (a hemisphere and more round the mount's normal, the heavy guns'
		// and the light railgun's slower mountings less), the spinal and fixed parts narrow along the keel.
		static const cs::List<cs::Ref<ModuleSpec>>& All();

	public:
		static const cs::Ref<ModuleSpec>& Of(ModuleKind k);

		/// <summary>Whether <paramref name="yard"/> can build <paramref name="m"/>: any yard builds anything but a tachyon
		/// lance, which only the greatest lawful yards can (<see cref="TachyonLances.YardBuilds"/>).</summary>
		static bool YardBuilds(ModuleKind m, const cs::Ref<Settlement>& yard);

		static const cs::List<cs::Ref<ModuleSpec>>& Catalogue();

		/// <summary>Materials for a hull of <paramref name="scale"/>, tonnes.</summary>
		double Tonnes(int i, double scale) override;

		double Mass(double scale) override;

		// `string IPartSpec.Name => Name;` and `(Good good, double tonnes)[] IPartSpec.Bill => Bill;`
		cs::String PartName() const override { return Name; }
		const cs::List<std::pair<Good, double>>& PartBill() const override { return Bill; }

		/// <summary>The facility this module is (plate, racks, a scanner suite, a brig: they go inside the hull, not on a
		/// hardpoint), or null.</summary>
		cs::Ref<FacilityKind> Facility() const;

		/// <summary>Whether a module goes on a hardpoint of <paramref name="type"/> and <paramref name="size"/> (U.SPC-T508):
		/// a part of the mount's own type, no bigger than it, and a spinal part only on a <paramref name="spinal"/> mount.</summary>
		static bool Fits(ModuleKind m, MountType type, HardpointSize size, bool spinal);

		/// <summary>Whether a module goes on <paramref name="slot"/> by its type and size.</summary>
		static bool Fits(ModuleKind m, const cs::Ref<OutfitSlot>& slot);

		/// <summary>The hardpoint kind a mount module becomes in the hull's layout.</summary>
		static SubsystemKind SubsystemOf(ModuleKind m);
	};

	/// <summary>One hardpoint of a hull: its type and size (U.SPC-T508), which hardpoint of the layout it is and what the
	/// design put there.</summary>
	class SPACEGODCORE_API OutfitSlot final
	{
	public:
		/// <summary>The kind of part the hardpoint is reserved for.</summary>
		MountType Type{};

		/// <summary>The biggest part it takes.</summary>
		HardpointSize Size{};

		/// <summary>The hardpoint's index in <see cref="CombatSpec.Layout"/>.</summary>
		int LayoutIndex = 0;

		/// <summary>What the design was built with here.</summary>
		cs::Nullable<ModuleKind> Stock;

		/// <summary>A mount that bears on the bow.</summary>
		bool BearsOnBow = false;

		/// <summary>A mount that takes a spinal or fixed part (a railgun, a torpedo tube, the lance): one that bears on the
		/// bow, or a fixed launcher the design built with one.</summary>
		bool Spinal = false;

		OutfitSlot(MountType type, HardpointSize size, int layoutIndex, cs::Nullable<ModuleKind> stock, bool bearsOnBow);

		/// <summary>Whether a gun or launcher goes here (not a shield generator or a drive).</summary>
		bool IsWeapon() const { return IsWeaponType(Type); }

		static bool IsWeaponType(MountType t);

		/// <summary>"Large energy mount", "Small missile mount", "Spinal mount", "Shield mount".</summary>
		cs::String Name() const;

		static cs::String TypeName(MountType t);
	private:
		OutfitSlot() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(OutfitSlot); // ObjectGraph registration (Persistence/Registrations)
	};

	enum class OutfitOutcome : int;

	/// <summary>
	/// What is fitted into a hull (U.SPC-T65), over the design it was built as, in two kinds of part (U.SPC-T470).
	/// Its hardpoints (<see cref="Slots"/>) are places on the hull's surface with a position and a fire cone: each refittable
	/// mount of the design (guns and launchers, point defence, torpedo tubes, shield generators, engines). Its
	/// facilities (<see cref="Facilities"/>) are inside the body of the ship and have only a size: the structure the
	/// hull is designed with (bridge, engineering, quarters, hangar, holds) and whatever a yard fits into the free
	/// space left over (plate, racks, a scanner suite, a brig, and any facility kind registered later), each taking a
	/// share of the hull's limited <see cref="InternalSpace"/>. Bridges, reactors, hangars, drills, pod launchers and a
	/// fighter's guns are part of the hull and are not refitted. <see cref="Build"/> turns the loadout into the hull's
	/// <see cref="CombatSpec"/>: its hardpoints, shield, hull, armour, drive and mass.
	/// </summary>
	// enable_shared_from_this: members hand `this` on as a cs::Ref<Loadout> (Outfitting.RackTonnes, FacilityKind.Effect).
	class SPACEGODCORE_API Loadout final : public std::enable_shared_from_this<Loadout>
	{
	public:
		cs::Ref<CombatSpec> Base;

		/// <summary>The hull's class, or null for a station or satellite (U.SPC-T377), which has none.</summary>
		cs::Nullable<ShipClass> Class;

		/// <summary>What the hull is (U.SPC-T470), for the facilities only some hulls may fit.</summary>
		HullCategory Category{};

		/// <summary>The hull's hardpoints.</summary>
		cs::List<cs::Ref<OutfitSlot>> Slots;
		cs::List<cs::Nullable<ModuleKind>> Fitted;

		/// <summary>The facilities inside the hull now (U.SPC-T470): the design's own first, in its layout's order, then
		/// those fitted since.</summary>
		cs::List<cs::Ref<FacilitySite>> Facilities;

		/// <summary>The facilities the hull was designed with.</summary>
		cs::List<cs::Ref<FacilitySite>> StockFacilities;

		/// <summary>The hull's whole internal space, sections: its design's facilities and the free space beyond them.</summary>
		double InternalSpace = 0;

		/// <summary>The trade hold the hull was built with, tonnes, before any racks.</summary>
		double BaseCapacityTonnes = 0;

		/// <summary>A small craft, whose hardpoints are all small ones (bar any bigger part its design builds in).</summary>
		bool SmallCraft = false;

		Loadout(cs::Ref<CombatSpec> design, ShipClass cls, double baseCapacityTonnes);

		/// <summary>A loadout for any built hull (U.SPC-T377): a ship of a class, or a station or satellite (no class),
		/// with the facilities it is designed with and the free space it has beyond them (sections).</summary>
		Loadout(cs::Ref<CombatSpec> design, cs::Nullable<ShipClass> cls, HullCategory category, double baseCapacityTonnes, bool smallCraft,
			const cs::List<cs::Ref<FacilitySite>>& facilities, double spareSpace);

		/// <summary>The hulls flown as fighters, whose hardpoints are small.</summary>
		static bool IsSmallCraft(ShipClass cls);

		/// <summary>The type of hardpoint a part of the design becomes (that of the module it was built with, U.SPC-T508),
		/// or null if it is part of the hull (a bridge, a reactor).</summary>
		static cs::Nullable<MountType> MountOf(SubsystemKind k);

	private:
		/// <summary>The screen mounts (point defence, flak, interceptors): small on any hull.</summary>
		static bool IsScreen(SubsystemKind k);

	public:
		/// <summary>
		/// How big a hardpoint of the design is (U.SPC-T508): a small craft's and a screen mount's are small; any other is
		/// large on a hull a kilometre long or more (a capital ship, a station) and medium otherwise; and never smaller than
		/// what the design built there, so every stock design keeps its own parts.
		/// </summary>
		static HardpointSize MountSize(SubsystemKind k, ModuleKind stock, bool smallCraft, double lengthKm);

	private:
		static cs::Nullable<ModuleKind> StockOf(SubsystemKind k);

	public:
		/// <summary>How big the hull is for plate, drives, generators and racks: 1 for a destroyer (9,000 t dry),
		/// about 4 for a carrier, a sixth for a courier.</summary>
		double Scale() const { return HullScale(Base); }

		static double HullScale(const cs::Ref<CombatSpec>& design);

		/// <summary>Internal space the facilities take now, sections.</summary>
		double UsedSpace() const;

		/// <summary>Internal space left for another facility, sections.</summary>
		double FreeSpace() const;

		/// <summary>The reactor output a hull keeps in reserve for its fittings, MW per unit of hull scale (U.SPC-T521): a
		/// destroyer's reactor has 50 MW to spare beyond its drive, guns and shields, a carrier's about 200.</summary>
		static constexpr double ReactorReservePerScale = 50;

		/// <summary>The share of a hull's crew that can be spared from running it to man its fittings (U.SPC-T521).</summary>
		static constexpr double CrewSpareShare = 0.25;

	private:
		static double PowerOf(const cs::List<cs::Ref<FacilitySite>>& sites);

		static double CrewOf(const cs::List<cs::Ref<FacilitySite>>& sites);

	public:
		/// <summary>The reactor power the hull has for its fittings, MW (U.SPC-T521): its reserve
		/// (<see cref="ReactorReservePerScale"/>) plus whatever its design's own facilities draw, as its reactor was sized for them.</summary>
		double ReactorMw() const;

		/// <summary>The reactor power its facilities draw now, MW.</summary>
		double PowerDrawMw() const;

		/// <summary>Reactor power still free for another fitting, MW.</summary>
		double FreePowerMw() const;

		/// <summary>The hands the hull can spare to man its fittings (U.SPC-T521): a share of its crew
		/// (<see cref="CrewSpareShare"/>) plus those its design's own facilities were crewed for.</summary>
		double FittingCrew() const;

		/// <summary>The hands its facilities take to man now.</summary>
		double CrewManning() const;

		/// <summary>Hands still free to man another fitting.</summary>
		double FreeCrew() const;

		/// <summary>Hardpoints fitted with <paramref name="k"/>, plus facilities that are that module (plate, racks, a
		/// scanner suite, a brig).</summary>
		int Count(ModuleKind k) const;

		/// <summary>Facilities of kind <paramref name="id"/> in the hull.</summary>
		int CountFacility(const cs::String& id) const;

		/// <summary>The design's own facilities no longer in the hull.</summary>
		// An eager list of what the C# iterator yields: its callers only walk it.
		cs::List<cs::Ref<FacilitySite>> RemovedFacilities() const;

		/// <summary>The facilities fitted over the design.</summary>
		cs::List<cs::Ref<FacilitySite>> AddedFacilities() const;

		/// <summary>Whether a facility in the hull is one of the design's own.</summary>
		bool IsStockFacility(const cs::Ref<FacilitySite>& s) const;

		/// <summary>Whether anything differs from the design as built.</summary>
		bool IsStock() const;

		/// <summary>Armour each plate adds (flat damage off every hit) and the share of the design's hull points.</summary>
		static constexpr double ArmourPerPlate = 2; static constexpr double HullPerPlate = 0.08;

		/// <summary>A hardened generator's grade, and a torch drive's, against stock.</summary>
		static constexpr double HardenedRating = 1.5; static constexpr double TorchRating = 1.3;

		/// <summary>Extra trade hold each rack adds, as a share of the hull's own, and grid hold rows.</summary>
		static constexpr double RackCapacityShare = 0.15;
		static constexpr int RackRows = 2;

		/// <summary>Extra reach each scanner suite gives a warship's sweep for hidden havens, km (half its base
		/// <see cref="SensorSuite.Military"/> range on a <see cref="Signatures.Haven"/> signature).</summary>
		static constexpr double ScannerRangeKm = 1e6;

		/// <summary>
		/// The trade hold this loadout gives, tonnes: the design's own (<see cref="BaseCapacityTonnes"/>), what its racks add,
		/// and the cargo of every facility that carries some (a truss freighter's pods, U.SPC-T482) fitted over the design,
		/// less that of the design's own taken out.
		/// </summary>
		double CapacityTonnes() const;

		/// <summary>Rows the grid hold has beyond its class's (<see cref="CargoHoldSpec"/>): the racks', and those of the
		/// facilities that give some (pods) fitted over the design, less those of the design's own taken out.</summary>
		int ExtraHoldRows() const;

		/// <summary>Whether a facility of <paramref name="kind"/> changes the hull's holds (racks, pods).</summary>
		static bool IsHold(const cs::Ref<FacilityKind>& kind);

		/// <summary>Prisoners each brig holds (more on a bigger hull).</summary>
		int BrigBerthsEach() const;

		/// <summary>Dry mass the loadout adds over the design as built, tonnes.</summary>
		double AddedMassTonnes() const;

		/// <summary>What <paramref name="m"/> is on hardpoint <paramref name="slot"/>: built to the hull's length in a spinal
		/// mount (U.SPC-T490, <see cref="SpinalMounts.Part"/>), else the module itself.</summary>
		cs::Ref<IPartSpec> PartAt(int slot, ModuleKind m) const;

		/// <summary>
		/// The hull's fighting spec with this loadout: each refitted hardpoint swapped in place (same position and
		/// hull-frame fire cone), shield and regen shared by the generators' grades, hull and armour raised by plate, each facility's own
		/// <see cref="FacilityKind.Effect"/>, the drive's thrust by its engines' mean grade, and every added tonne of dry
		/// mass slowing it.
		/// </summary>
		cs::Ref<CombatSpec> Build();

		/// <summary>Why <paramref name="kind"/> could or could not go into this hull's free space now.</summary>
		OutfitOutcome CanAdd(const cs::Ref<FacilityKind>& kind) const;

		/// <summary>Whether the facility at <paramref name="index"/> may be taken out (a fittable one; never the
		/// structure the hull is built around).</summary>
		bool CanRemove(int index) const;

		/// <summary>An independent copy, fitted the same way (a newly built hull's own loadout).</summary>
		cs::Ref<Loadout> Copy() const;

	private:
		cs::Ref<Loadout> Self() const { return std::const_pointer_cast<Loadout>(shared_from_this()); }
	private:
		Loadout() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(Loadout); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>Why a refit did or did not go through (<see cref="NoRoom"/>: the facility is bigger than the hull's free space;
	/// <see cref="NoPower"/>, <see cref="NoCrew"/>: the hull has not the reactor power or the hands to spare for it, U.SPC-T521).</summary>
	enum class OutfitOutcome : int { Ok, NotAYard, CannotDock, Busy, NoSuchSlot, WontFit, AlreadyFitted, NoStock, CantAfford, HoldTooFull, BrigOccupied, YardCannotBuild, NoRoom, NoPower, NoCrew };
	SPACEGODCORE_API const char* ToString(OutfitOutcome v);

	/// <summary>What a refit costs at a yard: the new module's materials bought from its market, the yard's labour,
	/// and what the part taken out fetches sold back into it.</summary>
	struct OutfitQuote
	{
		double Materials = 0, Labour = 0, TradeIn = 0;

		/// <summary>Whether the yard's market can spare every material the new module takes.</summary>
		bool InStock = false;

		/// <summary>The first material it is short of, when it is not <see cref="InStock"/>.</summary>
		Good Short{};

		OutfitQuote() = default;
		OutfitQuote(double materials, double labour, double tradeIn, bool inStock, Good shortOf)
		{
			Materials = materials;
			Labour = labour;
			TradeIn = tradeIn;
			InStock = inStock;
			Short = shortOf;
		}

		/// <summary>What the owner pays, net of the trade-in (negative: the yard pays out).</summary>
		double Net() const { return Materials + Labour - TradeIn; }
	};

	/// <summary>
	/// Ship outfitting at yards (U.SPC-T65). At a shipyard (or, for raiders, a haven) a ship's owner buys a part and has
	/// it fitted: on one of the hull's hardpoints a gun or launcher, a hardened generator on a shield mount, a torch drive
	/// on an engine mount (<see cref="Fit"/>); or, into the hull's free internal space, a facility (U.SPC-T470): armour
	/// plate, cargo racks, a scanner suite, a brig or any other registered <see cref="FacilityKind"/> the hull has room
	/// for (<see cref="FitFacility"/>, and <see cref="StripFacility"/> to take one out). The part's alloys, electronics,
	/// machinery and munitions come out of that yard's own market at its prices (so a yard short of electronics cannot
	/// fit shields, and one where they are dear charges for it), the yard's labour is a quarter of the materials' worth,
	/// and the part taken out is sold back into the market for half its materials. The hull's <see cref="CombatSpec"/>
	/// is rebuilt from the loadout (<see cref="Loadout.Build"/>), carrying its damage, people and stores over. The player
	/// does this at the helm; navies and raiders upgrade their warships while they refit at their own yards
	/// (<see cref="AiRefit"/>).
	/// </summary>
	class SPACEGODCORE_API Outfitting final
	{
	public:
		/// <summary>The yard's labour, as a share of the materials' base worth.</summary>
		static constexpr double LabourShare = 0.25;

		/// <summary>Share of a removed part's materials a yard takes back (the rest is scrap).</summary>
		static constexpr double TradeInShare = 0.5;

		/// <summary>Credits a pirate prisoner fetches a navy port as bounty, and anyone else's people as ransom.</summary>
		static constexpr double PirateBounty = 50000; static constexpr double Ransom = 20000;

		/// <summary>Share of a taken prize's crew its captors lock up, as far as their brigs have room.</summary>
		static constexpr double PrisonerShare = 0.5;

		/// <summary>A place that fits modules: any shipyard, and a raider haven's black-market slips.</summary>
		static bool IsYard(const cs::Ref<Settlement>& s);

		/// <summary>The ship's loadout, made from its design (stock) the first time it is asked for.</summary>
		static cs::Ref<Loadout> LoadoutOf(const cs::Ref<Ship>& ship);

		/// <summary>How far this ship's scanners pick out a hidden haven, km: its suite's reach on a haven's signature
		/// (<see cref="Sensors.HavenReachKm"/>; <see cref="War.ScanRangeKm"/> for a warship as built).</summary>
		static double ScanRangeKm(const cs::Ref<Ship>& ship);

		/// <summary>Prisoners this ship's brigs hold.</summary>
		static double BrigCapacity(const cs::Ref<Ship>& ship);

		/// <summary>Free berths in this ship's brigs: their capacity less the prisoners and captains already in them.</summary>
		static double BrigRoom(const cs::Ref<Ship>& ship);

		/// <summary>
		/// Fits <paramref name="module"/> as part of a new hull's build (no yard bill: the hull's price covers it): a
		/// facility module (a brig, plate, racks, a scanner) into the hull's free space, any other into the first empty
		/// hardpoint that takes it. False if there is no room for it.
		/// </summary>
		static bool Install(const cs::Ref<Ship>& ship, ModuleKind module);

		/// <summary>Fits a facility into the hull's free space as part of its build (no yard bill). False if it has no room
		/// for it or may not carry it.</summary>
		static bool Install(const cs::Ref<Ship>& ship, const cs::Ref<FacilityKind>& kind);

		/// <summary>Extra trade hold this ship's racks give, tonnes.</summary>
		static double RackTonnes(const cs::Ref<Loadout>& l, int racks);

		/// <summary>
		/// What fitting <paramref name="module"/> onto hardpoint <paramref name="slot"/> of the ship costs at
		/// <paramref name="yard"/>, at that market's prices and stock now.
		/// </summary>
		static OutfitQuote Quote(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, int slot, cs::Nullable<ModuleKind> module);

		/// <summary>What fitting a facility of <paramref name="kind"/> into the ship's free space costs at <paramref name="yard"/>.</summary>
		static OutfitQuote QuoteFacility(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, const cs::Ref<FacilityKind>& kind);

		/// <summary>What taking out the ship's facility number <paramref name="index"/> fetches (or costs) at <paramref name="yard"/>.</summary>
		static OutfitQuote QuoteStrip(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, int index);

	private:
		static OutfitQuote Price(const cs::Ref<Settlement>& yard, double scale, const cs::Ref<IPartSpec>& add, const cs::Ref<IPartSpec>& remove, double health);

		/// <summary>The health of what is fitted in a slot (a shot-away gun fetches nothing).</summary>
		static double PartHealth(const cs::Ref<Ship>& ship, const cs::Ref<OutfitSlot>& slot);

	public:
		/// <summary>Whether <paramref name="module"/> may go on hardpoint <paramref name="slot"/>: a part of the mount's
		/// type no bigger than it (U.SPC-T508), spinal parts only on a spinal mount, and only on the hulls its role allows
		/// (a hardpoint is never left empty; a facility goes into free space, <see cref="Loadout.CanAdd"/>).</summary>
		static bool CanFit(const cs::Ref<Loadout>& l, int slot, cs::Nullable<ModuleKind> module);

	private:
		/// <summary>Whether the ship can be refitted at the yard now: a yard that lets it dock, out of battle and not boarded.</summary>
		static OutfitOutcome Ready(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard);

	public:
		/// <summary>
		/// Fits <paramref name="module"/> onto hardpoint <paramref name="slot"/> of <paramref name="ship"/> at
		/// <paramref name="yard"/>: buys the materials from its market (they leave its stock and count as demand there),
		/// pays the yard's labour and the port's tax, sells the old part back, and rebuilds the hull's combat spec.
		/// Nothing changes unless it returns <see cref="OutfitOutcome.Ok"/>. The ship must be out of battle; the caller
		/// checks it is alongside the yard.
		/// </summary>
		static OutfitOutcome Fit(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, int slot,
			cs::Nullable<ModuleKind> module, OutfitQuote& quote);

		/// <summary>
		/// Fits a facility of <paramref name="kind"/> into the free internal space of <paramref name="ship"/> at
		/// <paramref name="yard"/> (U.SPC-T470), bought from the yard's market as <see cref="Fit"/> buys a gun. It must be
		/// a kind the hull may carry (<see cref="OutfitOutcome.WontFit"/>) and fit in the space left
		/// (<see cref="OutfitOutcome.NoRoom"/>: strip something else first).
		/// </summary>
		static OutfitOutcome FitFacility(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard,
			const cs::Ref<FacilityKind>& kind, OutfitQuote& quote);

		/// <summary>
		/// Takes facility number <paramref name="index"/> out of <paramref name="ship"/> at <paramref name="yard"/>, sold
		/// back for half its materials. Only a fitted facility comes out (never the hull's own structure), racks only
		/// when what is in them fits the hold that is left, and a brig only when it is empty.
		/// </summary>
		static OutfitOutcome StripFacility(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, int index,
			OutfitQuote& quote);

	private:
		/// <summary>Settles a refit's bill with the yard: buys the new part's materials out of its market, sells the old
		/// part's back into it, pays labour and tax. Nothing is paid unless it returns <see cref="OutfitOutcome.Ok"/>.</summary>
		static OutfitOutcome Charge(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, double scale, const cs::Ref<IPartSpec>& add,
			const cs::Ref<IPartSpec>& remove, double health, const OutfitQuote& quote);

		/// <summary>The hull after a facility of <paramref name="kind"/> went in or came out: its combat spec, its interior
		/// (when the facility is a space of its own) and, for racks and pods, both its holds.</summary>
		static void Rebuild(const cs::Ref<Ship>& ship, const cs::Ref<Loadout>& l, const cs::Ref<FacilityKind>& kind, const cs::Ref<CargoHold>& resized);

		/// <summary>A new interior for the hull when a facility that is a compartment went in or came out (its people
		/// carried over, and any marines the new <paramref name="spec"/> adds or loses), or null to keep the one it has.</summary>
		static cs::Ref<Interior> InteriorAfter(const cs::Ref<Ship>& ship, const cs::Ref<Loadout>& l, const cs::Ref<FacilityKind>& kind,
			const cs::Ref<CombatSpec>& spec);

		// ---------------------------------------------------------------- the AI's refits

	public:
		/// <summary>
		/// A navy's or raiders' warship refitting at its own yard takes one upgrade a visit, when the owner is
		/// flush (<see cref="AiFlush"/> times its price, and two new hulls' worth besides) and the yard has the
		/// materials to spare (<see cref="AiStockMargin"/>): the facilities it wants first, as far as its free space goes
		/// (<see cref="NavyFacilities"/>: plate, a scanner and a brig; <see cref="RaiderFacilities"/>: plate, a brig and
		/// racks for plunder; for both a Breacher bay where there is room and a team to man it, U.SPC-T521), then hardened shields, the role weapons its doctrine favours (U.SPC-T505,
		/// <see cref="FleetPatterns.Wishes"/>: phaser arrays, plasma cannons), heavier guns on a big hull, a tachyon lance
		/// for a capital of a navy that favours one, then torch drives, and the lance last of all for any other navy.
		/// <paramref name="plan"/> is the owner's build plan, its doctrine (null: the balanced fleet's). Returns what was
		/// fitted (a <see cref="ModuleSpec"/> or a <see cref="FacilityKind"/>), or null.
		/// </summary>
		static cs::Ref<IPartSpec> AiRefit(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard,
			const cs::Ref<BuildPlan>& plan = nullptr);

		/// <summary>How many times an upgrade's price an owner must hold (besides two new hulls) before the AI buys it.</summary>
		static constexpr double AiFlush = 50;

		/// <summary>The AI only buys out of a yard's surplus: it leaves <see cref="AiStockMargin"/> times the module's
		/// materials on the shelves, so its upgrades never starve the yard's own industry, repairs or new hulls.</summary>
		static constexpr double AiStockMargin = 4;

		/// <summary>The facilities a navy's warship wants in its free space, in order (facility kind ids; a new kind joins
		/// the AI's designs by being added here). A Breacher bay (U.SPC-T521) comes after the brig, so a big hull with
		/// room left takes one to fire its marines into prizes; it is wanted only by a hull whose habitats can fill a team.</summary>
		static cs::List<cs::String>& NavyFacilities();

		/// <summary>The facilities a raider wants in its free space, in order (a Breacher bay after its racks, U.SPC-T521).</summary>
		static cs::List<cs::String>& RaiderFacilities();

	private:
		static bool Surplus(const cs::Ref<Settlement>& yard, const cs::Ref<IPartSpec>& part, double scale);

		// The C# `(int slot, ModuleKind module, FacilityKind facility)` tuple.
		struct AiWish
		{
			int slot = 0;
			ModuleKind module{};
			cs::Ref<FacilityKind> facility;
		};

		// An eager list of what the C# iterator yields: AiRefit stops at the first wish that is fitted, and nothing a
		// wish it passes over does changes what the later ones are (a refit that fails changes nothing).
		static cs::List<AiWish> AiWishes(const cs::Ref<Ship>& ship, const cs::Ref<Loadout>& l, const cs::Ref<BuildPlan>& plan);

		/// <summary>
		/// Where a capital hull without one would take a tachyon lance, or null: in place of its spinal coilgun on a hull
		/// built around spinal mounts (U.SPC-T490), else of a bow heavy laser (U.SPC-T487). The yard must still be able to
		/// build one, and the navy be flush enough for it.
		/// </summary>
		static cs::Nullable<int> LanceWish(const cs::Ref<Loadout>& l);

		// ---------------------------------------------------------------- prisoners

	public:
		/// <summary>
		/// The captors lock up a share of a taken prize's crew in their brigs, as far as there is room: each
		/// prisoner is worth <see cref="PirateBounty"/> if they are raiders taken by anyone else, or
		/// <see cref="Ransom"/> otherwise, paid when they are handed over in port. Returns the prisoners taken.
		/// </summary>
		static double TakePrisoners(const cs::Ref<Ship>& prize, const cs::List<cs::Ref<Ship>>& captors);

		/// <summary>Hands the ship's prisoners over at <paramref name="port"/> (one it may dock at) for their bounty
		/// and ransom, paid to its owner. Returns the credits.</summary>
		static double HandOverPrisoners(const cs::Ref<Economy>& economy, const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& port);
	};
}
