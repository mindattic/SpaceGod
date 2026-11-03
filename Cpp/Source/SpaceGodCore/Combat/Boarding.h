#pragma once
// Port of Assets/UnitySpace/Core/Combat/Boarding.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Combat/BoardingCommand.h"

namespace SpaceGod::Core
{
	class Faction;
	class FacilitySite;
	enum class ShipClass : int;

	enum class CompartmentKind : int
	{
		Bow,
		Hangar,
		Hold,
		Quarters,
		Battery,
		Midships,
		Bridge,
		Engineering,
		/// <summary>A Breacher bay that fires Breacher rounds (U.SPC-T521, once the assault bay), or a round's own troop
		/// compartment (U.SPC-T466).</summary>
		BreacherBay,

		/// <summary>A marine habitat (U.SPC-T465): where the ship's marines live and drill, and muster from to repel boarders.</summary>
		Barracks,
	};
	SPACEGODCORE_API const char* ToString(CompartmentKind v);

	/// <summary>
	/// One space inside a hull. Who is in it (crew, defending marines, boarders), whether it is open
	/// to vacuum, and its place in the ship: ship lengths along the keel and its neighbours. This is
	/// everything a shipboard battle view will need to draw the fight deck by deck.
	/// </summary>
	class SPACEGODCORE_API Compartment final
	{
	public:
		int Index = 0;
		cs::String Name;
		CompartmentKind Kind = CompartmentKind::Bow;

		/// <summary>Position along the keel (ship lengths, nose +0.5) and height, for mapping hits and pods to a space.</summary>
		double Z = 0, Y = 0;

		cs::List<int> Links;

		/// <summary>Buried at the centre of the hull (U.SPC-T492, a warship's bridge): no pod, Breacher round or airlock
		/// opens into it (<see cref="Interior.Nearest"/> passes it by), so boarders reach it only through its neighbours.</summary>
		bool Buried = false;

		double Crew = 0;
		double Marines = 0;
		double Boarders = 0;

		/// <summary>People sheltering here who take no part in a fight: the crew left behind on a derelict,
		/// sealed in and waiting for a rescue (see <see cref="Interior.Abandon"/>).</summary>
		double Survivors = 0;

		/// <summary>True once boarders hold it (no defenders left in it).</summary>
		bool Taken = false;

		/// <summary>Open to space: a pod or a Breacher round's drill holed it. Crew die until the bulkheads seal.</summary>
		bool Vented = false;
		double SealIn = 0;

		bool Contested() const { return Boarders > 0.01 && (Crew + Marines) > 0.01; }
	};

	/// <summary>A line in a ship's boarding log: what happened, where, and when (battle seconds).</summary>
	struct SPACEGODCORE_API BoardingNote
	{
		double Seconds = 0;
		int Compartment = 0;
		cs::String Text;

		BoardingNote() = default;
		BoardingNote(double seconds, int compartment, const cs::String& text)
		{
			Seconds = seconds;
			Compartment = compartment;
			Text = text;
		}
	};

	/// <summary>
	/// One faction's part in a boarding (U.SPC-T392): the marines it has landed and the fighting strength they brought
	/// (marines times quality). Allied factions on one side fight a hull's defenders as one party; what each committed is
	/// its claim on the prize when it falls (see <see cref="PrizeSettlement"/>).
	/// </summary>
	class SPACEGODCORE_API BoardingParty final
	{
	public:
		cs::Ref<::SpaceGod::Core::Faction> Faction; // qualified: the member's own name hides the type
		double Landed = 0;
		double Strength = 0;

		/// <summary>Order of landing: the first faction aboard is 0.</summary>
		int Order = 0;
	};

	/// <summary>
	/// The inside of a hull and any fight going on in it. Defending marines sit in their barracks
	/// and react to a breach after a delay; the fight in each compartment trades casualties by
	/// numbers, training (<see cref="Faction.MarineQuality"/>) and a defender's advantage; boarders
	/// who clear a space push on toward the bridge and engineering by the shortest way. Holding both
	/// takes the ship; losing every boarder repels them. A breach vents its compartment: crew there
	/// die until the emergency bulkheads close.
	/// </summary>
	class SPACEGODCORE_API Interior final
	{
	public:
		cs::List<cs::Ref<Compartment>> Compartments;
		cs::List<BoardingNote> Log;

		/// <summary>Who is boarding (null when nobody is aboard who should not be): with allies boarding together
		/// (U.SPC-T392), the faction that landed first, which leads the party.</summary>
		cs::Ref<Faction> Boarder;

		/// <summary>The quality of the boarders aboard: with several landings (allies', or the same faction's from hulls
		/// that drilled differently) the strength-weighted mean of everyone who came aboard.</summary>
		double BoarderQuality = 1;

		/// <summary>The battle side the boarding party fights for, or -1 when the caller gave none (then no other faction
		/// may join it). Allies on that side may land and fight as one party with the first (U.SPC-T392).</summary>
		int BoarderSide = -1;

		/// <summary>Every faction with marines in the boarding, in order of landing (U.SPC-T392). Kept once the hull is
		/// taken, until it is handed over, so the allies' claims on the prize can be settled.</summary>
		cs::List<cs::Ref<BoardingParty>> Parties;
		double DefenderQuality = 1;

		/// <summary>How far the marines aboard have drilled in their habitats (U.SPC-T465), 0 (raw) to 1 (fully trained):
		/// it raises their quality (<see cref="DrillFactor"/>) both repelling boarders and boarding from this hull.</summary>
		double Training = 0;

		/// <summary>What the marines' drill multiplies their quality by.</summary>
		double DrillFactor() const;

		/// <summary>Set when the boarders hold the bridge and engineering.</summary>
		bool Captured = false;

		/// <summary>Marines that boarded in total, and how many of them fell.</summary>
		double BoardersLanded = 0, BoardersLost = 0, CrewLost = 0, MarinesLost = 0;

	private:
		double reactIn = 0;

	public:
		/// <summary>The player's standing orders to the boarders and to the defenders (U.SPC-T468, see
		/// <see cref="BoardingCommand"/>): <see cref="BoardingOrder.Auto"/>, the resolver's own choice, unless a player
		/// commanding that side has said otherwise. Each order names its objective compartment, or -1.</summary>
		BoardingOrder BoarderOrder = BoardingOrder::Auto, DefenderOrder = BoardingOrder::Auto;
		int BoarderObjective = -1, DefenderObjective = -1;

	private:
		/// <summary>Casualties per second per unit of effective strength.</summary>
		static constexpr double Lethality = 0.012;

		/// <summary>Ship's crew fight, badly; marines are the defence.</summary>
		static constexpr double CrewFight = 0.15;

		static constexpr double DefenderAdvantage = 1.4;

	public:
		double Crew() const;
		double Marines() const;
		double Boarders() const;

		bool UnderBoarding() const;

		/// <summary>Whether <paramref name="faction"/>, fighting for <paramref name="side"/>, may land boarders now: nobody
		/// is boarding, it is that faction's own boarding, or (U.SPC-T392) an ally's on the same side. Never once taken.</summary>
		bool Admits(const cs::Ref<Faction>& faction, int side = -1) const;

		/// <summary>Whether <paramref name="faction"/> has marines in the boarding (as its lead or an ally).</summary>
		bool IsParty(const cs::Ref<Faction>& faction) const;

		/// <summary>Whether more than one faction is boarding together (U.SPC-T392).</summary>
		bool Joint() const { return Parties.Count() > 1; }

		/// <summary>Who is boarding, as a log or the HUD names them: the lead, or every allied faction in it.</summary>
		cs::String BoarderNames() const;

		/// <summary>Non-combatants sheltering aboard (a derelict's left-behind crew).</summary>
		double Survivors() const;

		int TakenCount() const;

		cs::Ref<Compartment> Of(CompartmentKind kind) const;

		/// <summary>The compartment nearest a point on the hull (ship lengths along the keel, height) that a breach or an
		/// airlock can open into: never one buried at the centre (U.SPC-T492), unless the hull has no other.</summary>
		cs::Ref<Compartment> Nearest(double z, double y) const;

		// ---------------------------------------------------------------- building

		/// <summary>The inside of a ship of the class as designed: its facilities' spaces (<see cref="FacilityLayouts.For"/>).</summary>
		static cs::Ref<Interior> For(ShipClass cls, double crew, double marines);

		/// <summary>A defence platform (or any station or satellite): a gun deck around a control room and its reactor.</summary>
		static cs::Ref<Interior> ForPlatform(double crew, double marines);

		/// <summary>
		/// The inside of a hull built from its facilities (U.SPC-T470): each facility that is a space of its own
		/// (<see cref="FacilityKind.Compartment"/>) is a compartment, named and placed as its site says, joined on to the
		/// one before it along the keel or branching off the facility its site links to. A facility fitted later with no
		/// place of its own opens off midships (or the middle of the ship). Plate, racks and the like are no space.
		/// </summary>
		static cs::Ref<Interior> For(const cs::List<cs::Ref<FacilitySite>>& facilities, double crew, double marines);

		/// <summary>The marine habitats' barracks aboard (U.SPC-T465).</summary>
		cs::List<cs::Ref<Compartment>> Barracks() const;

		/// <summary>Whether the hull has a marine habitat at all; one without repels boarders with its crew alone.</summary>
		bool HasBarracks() const { return Of(CompartmentKind::Barracks) != nullptr; }

	private:
		/// <summary>Whether the ship's company works in a compartment: everywhere but the marines' barracks (unless the
		/// barracks are all there is).</summary>
		bool CrewSpace(const cs::Ref<Compartment>& c) const { return c->Kind != CompartmentKind::Barracks || !HasCrewSpace(); }

		bool HasCrewSpace() const;

		int CrewSpaces() const;

		/// <summary>Spreads <paramref name="crew"/> evenly through the spaces the ship's company works in.</summary>
		void SpreadCrew(double crew, bool add);

		/// <summary>Quarters <paramref name="marines"/>: shared among the habitats' barracks, or, on a hull with none (a
		/// prize crew, a Breacher round's team in its troop compartment), amidships or wherever there is room.</summary>
		void Quarter(double marines);

		/// <summary>Crew spread through the ship; marines in their habitats' barracks (or amidships, or wherever there is room).</summary>
		void Crew0(double crew, double marines);

	public:
		/// <summary>
		/// A day's drill (U.SPC-T465): marines in their habitats' barracks train, the whole complement going from raw to
		/// fully trained in <see cref="MarineHabitats.DrillDays"/> days. Nothing is learned without a habitat, with
		/// nobody in it, or while the ship is being boarded.
		/// </summary>
		void Drill(double days);

		// ---------------------------------------------------------------- orders (U.SPC-T468)

		/// <summary>
		/// Gives one side of the fight a standing order. <see cref="BoardingOrder.Push"/> needs an objective
		/// compartment; without a valid one the order is refused (false). Orders change only where people move:
		/// the fighting in each compartment is the resolver's as ever.
		/// </summary>
		bool Command(BoardingRole role, BoardingOrder order, int objective = -1);

		/// <summary>Hands one side back to the resolver: the auto-resolve.</summary>
		void AutoResolve(BoardingRole role) { Command(role, BoardingOrder::Auto); }

		/// <summary>Whether either side is under orders other than the resolver's.</summary>
		bool Commanded() const { return BoarderOrder != BoardingOrder::Auto || DefenderOrder != BoardingOrder::Auto; }

		/// <summary>Every order back to the resolver's (a fight over, or nobody left to command it).</summary>
		void ClearOrders();

		// ---------------------------------------------------------------- the fight

		/// <summary>Marines come aboard at a compartment (a pod, a Breacher round's drill, or an airlock after docking).
		/// Returns false when nobody was landed (the ship is already taken, or a faction not allied with this one is
		/// boarding it), so the caller can take its marines back rather than lose them. <paramref name="side"/> is the battle
		/// side the marines fight for: an ally on the side already boarding joins the party and fights the defenders with
		/// it as one (U.SPC-T392); without a side, only the faction already boarding may land more.</summary>
		bool Board(const cs::Ref<Faction>& boarder, double quality, double marines, const cs::Ref<Compartment>& at, bool breach, double seconds, cs::Random& rng, int side = -1);

	private:
		void JoinParty(const cs::Ref<Faction>& faction, double marines, double quality);

		/// <summary>The boarding is over without a prize (repelled, recalled, gone with the battle) or the prize is
		/// handed over: nobody's party now.</summary>
		void EndBoarding();

	public:
		/// <summary>Advances the fight aboard by <paramref name="dt"/> seconds. Returns true the moment the ship falls.</summary>
		bool Step(double dt, double seconds, cs::Random& rng);

	private:
		void Reinforce(double dt);

		void Advance(double dt);

		/// <summary>The neighbour on the shortest way from a compartment to <paramref name="objective"/>, or (-1) to the
		/// nearest of the bridge and engineering not yet held; -1 when there is no way, or the party is already there.</summary>
		int NextToward(int from, int objective);

		void Note(double seconds, int compartment, const cs::String& text);

	public:
		/// <summary>After a fight, survivors regroup: boarders aboard a taken ship become its prize crew.</summary>
		void Settle();

		/// <summary>
		/// The ship got away (released, fled, or the battle ran out) with the fight aboard undecided: the
		/// boarders are gone with the battle, and what they held is the crew's again. Without this the
		/// boarding would stay "under way" for good, keeping the ship's marines tied up.
		/// </summary>
		void BoardersGone();

	private:
		/// <summary>The bulkheads round every breach still venting close (they take seconds, and nothing aboard stops
		/// them once the boarders are gone): its timer would otherwise stand frozen, as <see cref="Step"/> runs only
		/// while boarders are aboard, and go on killing crew at the start of the next boarding.</summary>
		void SealBreaches();

	public:
		/// <summary>A prize crew takes over: the boarders become the ship's people; the old crew are prisoners.</summary>
		void HandOver(double prizeCrew);

		/// <summary>
		/// The hull is left adrift: its own people are gone, apart from <paramref name="survivors"/> who
		/// sealed themselves in at the far end from the airlocks, and <paramref name="lurkers"/> raiders
		/// still aboard stripping it, who hold the holds (or wherever there is loot) as defenders of
		/// <paramref name="lurkerQuality"/>. Any boarding that took it is over.
		/// </summary>
		void Abandon(double survivors, double lurkers, double lurkerQuality);

		/// <summary>
		/// Boarders still aboard go back the way they came: they are taken off and counted, and whatever
		/// they held is nobody's again (a derelict cleared, or a boarding party called back to its ship).
		/// </summary>
		double Recall();

		/// <summary>Takes up to <paramref name="n"/> crew off the ship, evenly from every compartment; returns how many went.</summary>
		double TakeCrew(double n);

		/// <summary>Takes up to <paramref name="n"/> marines off the ship (a boarding party leaving through the airlock).</summary>
		double TakeMarines(double n);

		/// <summary>People come aboard: crew spread through the ship, marines to their barracks (the ship's own coming back
		/// from a boarding, so their drill is kept).</summary>
		void Embark(double crew, double marines);

		/// <summary>Back at a yard: crew and marines made up to strength. Raw recruits dilute the complement's drill.</summary>
		void Muster(double crew, double marines);
	};
}
