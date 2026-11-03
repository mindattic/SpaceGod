#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/Facilities.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	enum class CompartmentKind : int;
	enum class Good : int;
	enum class HullCategory : int;
	enum class ModuleKind : int;
	enum class ShipClass : int;
	class CombatSpec;
	class Loadout;

	/// <summary>A part a yard builds into a hull: what it is called, what it is built from and what it weighs. Both a
	/// hardpoint's module (<see cref="ModuleSpec"/>) and a facility (<see cref="FacilityKind"/>) are one.</summary>
	/// <remarks>
	/// C++ shape: both implementers keep public fields named <c>Name</c> and <c>Bill</c> and implement the interface's
	/// <c>Name</c>/<c>Bill</c> explicitly (<c>string IPartSpec.Name => Name;</c>). C++ cannot have a field and a member
	/// function of one name, so the interface's two properties are <see cref="PartName"/> and <see cref="PartBill"/>: code
	/// that reads <c>part.Name</c> / <c>part.Bill</c> through an <c>IPartSpec</c> calls <c>part->PartName()</c> /
	/// <c>part->PartBill()</c>. A C# <c>(Good good, double tonnes)</c> bill line is <c>std::pair&lt;Good, double&gt;</c>
	/// (<c>.first</c> is the good, <c>.second</c> the tonnes).
	/// </remarks>
	class IPartSpec
	{
	public:
		virtual ~IPartSpec() = default;

		/// <summary>C#'s <c>string Name { get; }</c>.</summary>
		virtual cs::String PartName() const = 0;

		/// <summary>The bill of materials, tonnes, for a hull of scale 1 (a destroyer) if the part grows with the hull.</summary>
		/// <remarks>C#'s <c>(Good good, double tonnes)[] Bill { get; }</c>.</remarks>
		virtual const cs::List<std::pair<Good, double>>& PartBill() const = 0;

		/// <summary>Materials for a hull of <paramref name="scale"/>, tonnes.</summary>
		virtual double Tonnes(int i, double scale) = 0;

		/// <summary>Installed mass on a hull of <paramref name="scale"/>, tonnes.</summary>
		virtual double Mass(double scale) = 0;
	};

	/// <summary>
	/// One kind of facility (U.SPC-T470): a thing inside the body of a ship, not on its skin. A facility has no
	/// position on the hull and no firing arc, only a size: the share of the hull's internal space it takes
	/// (<see cref="Loadout.InternalSpace"/>), in sections (one section is what an internal bay was before facilities).
	/// Every kind is data in <see cref="FacilityKinds"/>: its size, mass and bill of materials, which hull categories
	/// may fit it, whether a yard fits and strips it, the boarding compartment it gives the hull's interior, and what it
	/// does to the hull's fighting spec (<see cref="Effect"/>). A new facility (a marine habitat, a Breacher bay, a
	/// radiator, a drive room) is a new registered kind, never an edit to the code that fits the first.
	/// </summary>
	class SPACEGODCORE_API FacilityKind final : public IPartSpec
	{
	public:
		/// <summary>A stable name for saves and bindings ("armour-plating", "hangar").</summary>
		cs::String Id;

		cs::String Name;

		/// <summary>Sections of internal space it takes. A hull whose free space is smaller cannot fit it at all.</summary>
		double Size = 0;

		/// <summary>Installed mass, tonnes (per unit of hull scale if <see cref="Scaled"/>).</summary>
		double MassTonnes = 0;

		/// <summary>Whether the bill and mass grow with the hull (<see cref="Loadout.Scale"/>) or are one fixed fitting.</summary>
		bool Scaled = false;

		cs::List<std::pair<Good, double>> Bill;

		/// <summary>The hull categories that may fit it (null: any).</summary>
		cs::Nullable<cs::List<HullCategory>> Categories;

		/// <summary>Whether a yard fits it into free space and strips it out again. False for the hull's structure as
		/// designed (its bridge, engineering, quarters, hangar and holds), which comes with the frame.</summary>
		bool Fittable = false;

		/// <summary>The boarding compartment it is in the hull's interior (<see cref="Interior.For(IReadOnlyList{FacilitySite}, double, double)"/>),
		/// or null for one that is not a space of its own (plate, racks, a scanner).</summary>
		cs::Nullable<CompartmentKind> Compartment;

		/// <summary>The module it is, for the facilities that were internal-bay modules before (U.SPC-T65): their effects
		/// are still read by <see cref="Loadout.Count(ModuleKind)"/> (plate in <see cref="Loadout.Build"/>, racks in the
		/// holds, scanners in <see cref="SensorSuite"/>, a brig's berths).</summary>
		cs::Nullable<ModuleKind> Module;

		/// <summary>
		/// What one of it does to the hull's fighting spec as <see cref="Loadout.Build"/> makes it, beyond its mass:
		/// called with +1 for each fitted over the design and -1 for each of the design's own taken out. Null for one
		/// whose effect is read elsewhere (by <see cref="Module"/>, or by the system that uses it).
		/// </summary>
		cs::Action<cs::Ref<CombatSpec>, cs::Ref<Loadout>, int> Effect;

		/// <summary>Marines it quarters (U.SPC-T465), per unit of hull scale if <see cref="Scaled"/>: what one adds to the
		/// hull's marine complement (<see cref="CombatSpec.Marines"/>). A site may set its own (<see cref="FacilitySite.Berths"/>).</summary>
		double Berths = 0;

		/// <summary>Tonnes of trade cargo one of it carries (U.SPC-T482): a truss freighter's cargo pod is its hold, so its
		/// capacity is the pods it carries (<see cref="Loadout.CapacityTonnes"/>). Zero for most.</summary>
		double CargoTonnes = 0;

		/// <summary>Rows of the grid hold (<see cref="CargoHold"/>) one of it gives (a pod's share of the hold).</summary>
		int HoldRows = 0;

		/// <summary>The ship classes that may fit it (null: any class its <see cref="Categories"/> allow), so a pod clamps
		/// only to a hull that has a truss (U.SPC-T482).</summary>
		/// <remarks>C#'s ICollection&lt;ShipClass&gt;, a reference to the caller's live set (BulkFreighters.TrussClasses):
		/// held as a pointer to it so a class made a truss later is seen here too, as in C#.</remarks>
		const cs::HashSet<ShipClass>* Classes = nullptr;

		/// <remarks>A C# null <paramref name="name"/> is an empty string here; a null <paramref name="bill"/> an empty list.</remarks>
		FacilityKind(const cs::String& id, const cs::String& name, double size, double massTonnes, bool scaled, const cs::List<std::pair<Good, double>>& bill,
			cs::Nullable<cs::List<HullCategory>> categories = std::nullopt, bool fittable = true, cs::Nullable<CompartmentKind> compartment = std::nullopt,
			cs::Nullable<ModuleKind> module = std::nullopt, cs::Action<cs::Ref<CombatSpec>, cs::Ref<Loadout>, int> effect = nullptr, double berths = 0,
			double cargoTonnes = 0, int holdRows = 0, const cs::HashSet<ShipClass>* classes = nullptr);

		/// <summary>A facility that is one of the former internal-bay modules, with that module's name, bill and mass.</summary>
		static cs::Ref<FacilityKind> OfModule(const cs::String& id, ModuleKind module, double size, cs::Nullable<cs::List<HullCategory>> categories = std::nullopt);

		// string IPartSpec.Name => Name; (Good good, double tonnes)[] IPartSpec.Bill => Bill;
		cs::String PartName() const override { return Name; }
		const cs::List<std::pair<Good, double>>& PartBill() const override { return Bill; }

		double Tonnes(int i, double scale) override;

		double Mass(double scale) override;

		/// <summary>Whether a hull of this category may carry it at all.</summary>
		bool AllowedOn(HullCategory category);

		/// <summary>Which ship classes within its categories may fit it (null: any of them), for a facility that only some
		/// hulls of a category take: the FTL drive goes in a command cruiser but not a plain cruiser, both warships
		/// (U.SPC-T472). Asked with the hull's class, or null for a station or satellite.</summary>
		cs::Func<bool, cs::Nullable<ShipClass>> ClassFilter;

		/// <summary>The id of a facility the hull must already carry to fit this one, or null (U.SPC-T466): a Breacher bay
		/// is fitted only where a marine habitat supplies its teams, and that habitat cannot be stripped from under it.</summary>
		/// <remarks>C# null is an empty string here.</remarks>
		cs::String Requires;

		/// <summary>The reactor power one draws, MW (U.SPC-T521): a hull fits it only with that much of its reactor's reserve
		/// for fittings still free (<see cref="Loadout.FreePowerMw"/>). Zero for most (a projector brings its own reactor).</summary>
		double PowerMw = 0;

		/// <summary>The crew one takes to man, beyond the hull's running crew (U.SPC-T521): a hull fits it only with that many
		/// hands still to spare (<see cref="Loadout.FreeCrew"/>). Zero for most.</summary>
		double Crew = 0;

		/// <summary>Whether a hull of <paramref name="category"/> and <paramref name="cls"/> may carry it at all: not one the
		/// class bars (<see cref="FacilityLayouts.Bars"/>), and, for a kind with <see cref="Classes"/>, only those.</summary>
		bool AllowedOn(HullCategory category, cs::Nullable<ShipClass> cls);
	};

	/// <summary>
	/// The registry of facility kinds (U.SPC-T470). The hull's structure (bridge, engineering, quarters, hangar deck,
	/// holds, batteries' magazines, the bow and midships sections, a Breacher round's troop compartment) and the four fittable
	/// facilities that were internal-bay modules (armour plate, cargo racks, a scanner suite, a brig) are registered
	/// here, with the marine habitat (U.SPC-T465, <see cref="MarineHabitats"/>) and the Breacher bay that fires Breacher
	/// rounds (U.SPC-T466, U.SPC-T521, <see cref="BreacherBays"/>); new kinds plug in with <see cref="Register"/>.
	/// </summary>
	class SPACEGODCORE_API FacilityKinds
	{
		static cs::List<cs::Ref<FacilityKind>>& All();

		/// <remarks>C#'s Dictionary(StringComparer.OrdinalIgnoreCase).</remarks>
		static cs::Dictionary<cs::String, cs::Ref<FacilityKind>, cs::OrdinalIgnoreCaseComparer>& ById();

	public:
		/// <summary>C# runs the static constructor before the first use of any static member: every static field accessor
		/// runs this first (the fields in textual order, then the constructor's body, which registers the stock kinds).</summary>
		static void RunClassConstructor();

		// The structure every hull is built around (not fittable: it comes with the frame).
		// (One declaration each: MSVC rejects the later declarators of a multi-declarator static in a dllexport class, C2487.)
		static constexpr const char* Bridge = "bridge"; static constexpr const char* Engineering = "engineering";
		static constexpr const char* Quarters = "quarters"; static constexpr const char* Hangar = "hangar"; static constexpr const char* Hold = "hold";
		static constexpr const char* Battery = "battery"; static constexpr const char* Bow = "bow"; static constexpr const char* Midships = "midships";
		static constexpr const char* TroopCompartment = "troop-compartment";

		// What a yard fits into free space.
		static constexpr const char* ArmourPlating = "armour-plating"; static constexpr const char* CargoRacks = "cargo-racks";
		static constexpr const char* Scanner = "scanner-suite"; static constexpr const char* Brig = "brig";
		static constexpr const char* MarineHabitat = "marine-habitat";

		/// <summary>The firing room that launches Breacher rounds (U.SPC-T466, U.SPC-T521, see <see cref="BreacherBays"/>): any
		/// ship's to fit that has the space, the reactor power and the crew for it, and only beside a marine habitat to
		/// supply its teams.</summary>
		static constexpr const char* BreacherBay = "breacher-bay";

		/// <summary>The Breacher bay's id before U.SPC-T521 renamed it; <see cref="Of"/> still finds the bay by it.</summary>
		static constexpr const char* OldBreacherBayId = "assault-bay";

		/// <summary>The hulls that may fit a Breacher bay (U.SPC-T521): any ship that can quarter marines, navy, pirate or a
		/// merchant's escort, so long as it has the space, power and crew for it; never a station.</summary>
		static const cs::List<HullCategory>& BreacherHulls();

		/// <summary>The hulls big enough for a marine habitat (U.SPC-T465): every one but fighters, small craft and
		/// satellites, whose crews repel boarders alone.</summary>
		static const cs::List<HullCategory>& MarineHulls();

		// What makes a hull a fleet's command ship (U.SPC-T477, see FleetCoordination): a capital's to fit.
		static constexpr const char* SensorArray = "sensor-array"; static constexpr const char* CommsSuite = "comms-suite";
		static constexpr const char* TargetingComputers = "targeting-computers";

	private:
		static const cs::List<HullCategory>& Capitals();

	public:
		/// <summary>
		/// A self-contained FTL drive (U.SPC-T472): what lets a hull whose class carries one only when its design fits it
		/// (<see cref="FtlAccess.Fitted"/>: battleships, and command cruisers when they are a class) cross between the stars
		/// under its own power (<see cref="CruiseDrives.Of(Ship)"/>). The stock battleship is designed with one; a yard can
		/// strip it out for two more sections of plate or racks, and the hull then travels by the gates like a cruiser.
		/// </summary>
		static constexpr const char* FtlDrive = "ftl-drive";

		/// <summary>
		/// A spinal power plant (U.SPC-T490, see <see cref="SpinalMounts"/>): the large reactor and capacitor banks a
		/// ship-length spinal mount draws on, three sections deep in the hull. Only a hull built around spinal mounts may fit
		/// one; the stock battleship is designed with one for each of its two mounts, and with fewer plants than standing
		/// mounts they recharge slowly (<see cref="SpinalMounts.ChargeRate"/>).
		/// </summary>
		static constexpr const char* SpinalPlant = "spinal-plant";

		// What clamps along a truss freighter's spine (U.SPC-T482, see BulkFreighters).
		static constexpr const char* CargoPod = "cargo-pod"; static constexpr const char* FuelSphere = "fuel-sphere";

		/// <summary>
		/// An interdiction field projector (U.SPC-T507, see <see cref="LaneInterdictors"/>): the heavy equipment that disrupts
		/// a hyperlane's magnetic field and throws the ships riding past out of it, with the reactor that feeds it, two
		/// sections deep. Any warship, capital or freighter hull big enough to carry it and its reactor load
		/// (<see cref="LaneInterdictors.CanCarry(ShipClass)"/>) may fit it; a yard fits and strips it like any other fitting,
		/// and its ring shows on whatever hull carries it.
		/// </summary>
		static constexpr const char* FieldProjector = "field-projector";

		/// <summary>
		/// A siege engineering bay (U.SPC-T478, see <see cref="GateBreakers"/>): the gate breaker's engineering teams, their
		/// workshops, cutting gear, gate-control rigs and demolition charges, two sections deep. With its boarding teams they
		/// seize a jump gate intact and patch its ring. Part of the hull as designed, not a fitting, and only a hull of a
		/// gate-breaking class has one (<see cref="GateBreakers.Classes"/>).
		/// </summary>
		static constexpr const char* SiegeEngineering = "siege-engineering";

	private:
		static cs::Ref<FacilityKind> Structure(const cs::String& id, const cs::String& name, double size, CompartmentKind compartment);

	public:
		/// <summary>Adds a kind (or replaces the one with its id).</summary>
		static void Register(const cs::Ref<FacilityKind>& kind);

		static const cs::List<cs::Ref<FacilityKind>>& Catalogue();

		/// <remarks>A C# null <paramref name="id"/> is an empty string here.</remarks>
		static cs::Ref<FacilityKind> Of(const cs::String& id);

		/// <summary>The facility that is <paramref name="module"/>, or null if the module goes on a hardpoint.</summary>
		static cs::Ref<FacilityKind> ForModule(ModuleKind module);
	};

	/// <summary>
	/// One facility in a hull (U.SPC-T470): its kind, the name its space goes by aboard ("Forward hold", "Cockpit"), where
	/// that space lies for the boarding interior (ship lengths along the keel, nose +0.5, and height) and how it joins the
	/// rest: on to the previous space along the keel (<see cref="Keel"/>), off another of the layout's facilities, or, for
	/// one a yard fitted later, off midships (<see cref="Amidships"/>). It has no firing arc: only a hardpoint
	/// (<see cref="Hardpoint"/>) has one.
	/// </summary>
	class SPACEGODCORE_API FacilitySite final
	{
	public:
		cs::String Kind;
		cs::String Name;
		double Z = 0, Y = 0;

		/// <summary><see cref="Keel"/>: the next space along the keel from the one before it; <see cref="Amidships"/>: a
		/// space opening off midships; otherwise the index of the facility in the same layout it branches off.</summary>
		int Link = 0;

		static constexpr int Keel = -1; static constexpr int Amidships = -2;

		/// <summary>The marines this space quarters as the hull was designed (U.SPC-T465), or null for its kind's
		/// (<see cref="FacilityKind.Berths"/>, grown with the hull): a stock design's habitat holds the complement it was drawn for.</summary>
		cs::Nullable<double> Berths;

		/// <summary>A space buried at the centre of the hull (U.SPC-T492, a warship's bridge): no pod, Breacher round or
		/// airlock opens into it, so boarders reach it only by fighting through the spaces around it.</summary>
		bool Buried = false;

		/// <remarks>A C# null <paramref name="name"/> is an empty string here.</remarks>
		FacilitySite(const cs::String& kind, const cs::String& name = cs::String(), double z = 0, double y = 0, int link = Keel, cs::Nullable<double> berths = std::nullopt);

		/// <summary>Marines it quarters on a hull of <paramref name="scale"/> (<see cref="Loadout.Scale"/>).</summary>
		double BerthsOn(double scale);

		/// <summary>A facility a yard fits into a hull's free space (it opens off midships, if it is a space at all).</summary>
		static cs::Ref<FacilitySite> Fitted(const cs::Ref<FacilityKind>& kind);

		cs::Ref<FacilityKind> Def() const;

		/// <summary>The space it takes, sections.</summary>
		double Size() const;
	private:
		FacilitySite() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(FacilitySite); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>
	/// The facilities each hull is designed with (U.SPC-T470): its bridge, engineering, quarters, hangar, holds and the
	/// rest, laid out along the keel as the boarding interior has them, and the free space each class has for fitted
	/// facilities beyond them. A new frame can bring its own layout (<see cref="HullFrame"/>); a new class registers one
	/// with <see cref="Register"/>.
	/// </summary>
	class SPACEGODCORE_API FacilityLayouts
	{
		/// <summary>A warship's bridge (U.SPC-T492): buried at the centre of the hull behind its full thickness, with no
		/// viewports (the crew sees out through its sensors and holo displays), opening only off the space it links to.</summary>
		static cs::Ref<FacilitySite> Buried(int link, const cs::String& name = "Bridge");

		// The warships' bridges are buried at the centre of the hull, off midships (U.SPC-T492).
		// The warships' marine habitats (U.SPC-T465) are barracks branching off below their quarters (or midships), each
		// quartering the complement the class was drawn with; a freighter keeps a security post for its few guards.
		static cs::Dictionary<ShipClass, cs::List<cs::Ref<FacilitySite>>>& ByClass();

	public:
		/// <summary>A small hull's: a cockpit and its engineering.</summary>
		static const cs::List<cs::Ref<FacilitySite>>& Small();

		/// <summary>A defence platform's (and any station's or satellite's without its own): a gun deck around a control
		/// room and its reactor, and the garrison's barracks off the control room.</summary>
		static const cs::List<cs::Ref<FacilitySite>>& Platform();

		/// <summary>A satellite's: too small for a garrison, its spaces only.</summary>
		static const cs::List<cs::Ref<FacilitySite>>& Satellite();

		/// <summary>An orbital station's: a platform's spaces with barracks for its larger garrison.</summary>
		static const cs::List<cs::Ref<FacilitySite>>& Station();

		/// <summary>The facilities a ship of the class is designed with.</summary>
		static const cs::List<cs::Ref<FacilitySite>>& For(ShipClass c);

		/// <summary>The marines a hull with these facilities quarters (U.SPC-T465): every space's berths on a hull of
		/// <paramref name="scale"/>. A hull with no habitat (or troop compartment) carries none.</summary>
		static double Complement(const cs::List<cs::Ref<FacilitySite>>& facilities, double scale);

		/// <summary>Facilities of kind <paramref name="kind"/> in a layout.</summary>
		static int CountOf(const cs::List<cs::Ref<FacilitySite>>& layout, const cs::String& kind);

		/// <summary>The marine complement a class's design is drawn with: its layout's berths.</summary>
		static double Complement(ShipClass c, const cs::Ref<CombatSpec>& design);

		/// <summary>Sets (or replaces) the facilities a class is designed with.</summary>
		static void Register(ShipClass c, const cs::List<cs::Ref<FacilitySite>>& layout);

	private:
		/// <remarks>C#'s HashSet(StringComparer.OrdinalIgnoreCase).</remarks>
		static cs::Dictionary<ShipClass, cs::HashSet<cs::String, cs::OrdinalIgnoreCaseComparer>>& Barred();

	public:
		/// <summary>Bars (or, with <paramref name="barred"/> false, allows again) a facility kind on every hull of a class.</summary>
		static void Bar(ShipClass c, const cs::String& kind, bool barred = true);

		/// <summary>Whether hulls of <paramref name="c"/> may never fit a facility of kind <paramref name="kind"/>.</summary>
		/// <remarks>A C# null <paramref name="kind"/> is an empty string here.</remarks>
		static bool Bars(ShipClass c, const cs::String& kind);

		/// <summary>Free internal space a hull of each class has for fitted facilities beyond its structure, sections
		/// (as many as it had internal bays before U.SPC-T470).</summary>
		static double SpareSpace(ShipClass c);

		/// <summary>The space a layout's facilities take, sections.</summary>
		static double SpaceOf(const cs::List<cs::Ref<FacilitySite>>& layout);
		SG_SERIALIZABLE(FacilityLayouts); // ObjectGraph registration (Persistence/Registrations)
	};
}
