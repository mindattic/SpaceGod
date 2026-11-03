#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Events/Opportunity.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "BodyDefinition.h"
#include "DVec3.h"
#include "Economy/Goods.h"

namespace SpaceGod::Core
{
	class Battle;
	class EscapePod;
	class Faction;
	class Lane;
	class Ship;
	class Wreck;

	/// <summary>What kind of adventure an <see cref="Opportunity"/> is.</summary>
	enum class OpportunityKind : int
	{
		/// <summary>A ship under attack in a battle right now, or drifting with dead drives after an interdiction pulse.</summary>
		Distress,

		/// <summary>A hull boarded, stripped and left adrift (<see cref="Ship.Derelict"/>).</summary>
		Derelict,

		/// <summary>The wreck a destroyed ship left at its battle site (<see cref="Wreck"/>), and whatever is still in its hold.</summary>
		Wreck,

		/// <summary>Raiders lying in wait (<see cref="ShipPhase.Lurk"/>) beside a lane that has seen trouble.</summary>
		PirateAmbush,

		/// <summary>A merchant riding a dangerous lane who would pay for an escort to the far ramp.</summary>
		ConvoyEscort,

		/// <summary>A price on a named pirate captain's head: the leader of a raider group.</summary>
		Bounty,

		/// <summary>Something odd out in the dark: rare, and the only kind not driven by the sim's own state.</summary>
		Anomaly,

		/// <summary>Escape pods drifting where a hull died (<see cref="EscapePod"/>), broadcasting until someone picks them up or their air runs out.</summary>
		EscapePods,
	};
	SPACEGODCORE_API const char* ToString(OpportunityKind v);

	/// <summary>Where an <see cref="Opportunity"/> is in its life.</summary>
	enum class OpportunityState : int
	{
		/// <summary>Open: on the HUD, waiting for the player.</summary>
		Active,

		/// <summary>The player saw it through and was paid.</summary>
		Completed,

		/// <summary>Ignored until it expired, or the situation resolved (badly, or without the player) first.</summary>
		Failed,
	};
	SPACEGODCORE_API const char* ToString(OpportunityState v);

	/// <summary>What seeing an <see cref="Opportunity"/> through pays: credits, cargo crates and standing with a faction.</summary>
	struct SPACEGODCORE_API OpportunityReward
	{
		double Credits = 0;

		/// <summary>Crates of <see cref="CargoGood"/> put into the player's grid hold (<see cref="CargoHold"/>), 0 for none.</summary>
		int CargoCrates = 0;
		Good CargoGood = static_cast<Good>(0);

		/// <summary>Whose regard the player gains (null for none), and by how much.</summary>
		cs::Ref<Faction> ReputationWith;
		double Reputation = 0;

		/// <summary>How much of <see cref="Credits"/> the <see cref="ReputationWith"/> faction itself pays (null: all of it).</summary>
		cs::Nullable<double> PatronShare;

		bool IsEmpty() const { return Credits <= 0 && CargoCrates <= 0 && (ReputationWith == nullptr || Reputation == 0); }

		cs::String ToString() const;
	};

	/// <summary>
	/// One timed, location-anchored adventure near the player, spawned by <see cref="EventDirector"/>
	/// from something actually happening in the sim (a ship under attack, a derelict, raiders lurking
	/// off a dangerous lane...). It is anchored to the thing it is about -- a ship, a wreck, or a point
	/// in a body's frame -- so its HUD marker follows that thing; it carries a short text hook, an expiry
	/// and the reward it pays, and ends <see cref="OpportunityState.Completed"/> or
	/// <see cref="OpportunityState.Failed"/>.
	/// </summary>
	class SPACEGODCORE_API Opportunity final
	{
	public:
		int Id = 0;
		OpportunityKind Kind = OpportunityKind::Distress;

		/// <summary>Identifies the sim thing this is about ("ship:12", "wreck:3", "lane:4"...), so it is
		/// never offered twice at once and is rested a while after it ends.</summary>
		cs::String SourceKey;

		/// <summary>A few words for the HUD marker ("Distress: Kestrel's Folly").</summary>
		cs::String Title;

		/// <summary>The short text hook: one or two sentences, in the voice of whoever is calling.</summary>
		cs::String Hook;

		double SpawnJd = 0, ExpiresJd = 0;
		OpportunityState State = OpportunityState::Active;

		/// <summary>When it completed or failed, and a line saying how.</summary>
		double ResolvedJd = 0;
		cs::String Outcome;

		OpportunityReward Reward;

		/// <summary>The ship this is about (the one calling for help, the derelict, the merchant to escort,
		/// the captain or the lead raider); its marker rides on it.</summary>
		cs::Ref<SpaceGod::Core::Ship> Ship;

		/// <summary>Who is asking (and pays): the ship's own faction for a distress call or an escort, the
		/// pirate hunters for an ambush or a bounty; null when nobody is (salvage, anomalies).</summary>
		cs::Ref<Faction> Patron;

		/// <summary>The battle a distress call came out of, while it lasts.</summary>
		cs::Ref<SpaceGod::Core::Battle> Battle;

		/// <summary>The wreck to investigate.</summary>
		cs::Ref<SpaceGod::Core::Wreck> Wreck;

		/// <summary>The escape pods to pick up.</summary>
		cs::Ref<EscapePod> Pod;

		/// <summary>The lane an ambush or an escort is on.</summary>
		cs::Ref<SpaceGod::Core::Lane> Lane;

		/// <summary>For an anchor that is neither a ship nor a wreck (an anomaly): a point fixed in this body's frame, km.</summary>
		int Body = -1;
		DVec3 OffsetKm;

		/// <summary>The named pirate captain a bounty is on.</summary>
		cs::String Captain;

		/// <summary>Where it is now (heliocentric ecliptic km), refreshed every update from its anchor.</summary>
		DVec3 HelioKm;

		/// <summary>The player has been on the scene (within <see cref="EventDirector.ArriveKm"/>) at least once.</summary>
		bool Engaged = false;

		/// <summary>The piloted ship that reached it: paid on completion even if the player has let go of
		/// the helm (or lost the ship) by the time the situation resolves.</summary>
		cs::Ref<SpaceGod::Core::Ship> EngagedBy;

		/// <summary>That ship's faction when it came (the player's purse), kept in case the ship later changes hands.</summary>
		cs::Ref<Faction> EngagedFaction;

		/// <summary>A distress call from a ship actually in a fight (rather than one drifting after interdiction).</summary>
		bool UnderAttack = false;

		/// <summary>A distress call from a ship that has run out of propellant and drifts waiting for a tanker:
		/// seen through by pumping some of the player's own reaction mass across.</summary>
		bool DryTanks = false;

		bool IsActive() const { return State == OpportunityState::Active; }

		double DaysLeft(double jd) const { return cs::Math::Max(0.0, ExpiresJd - jd); }

		/// <summary>The marker's short tag on the HUD.</summary>
		cs::String MarkerLabel() const { return LabelOf(Kind); }

		/// <summary>The marker's colour on the HUD.</summary>
		Rgb MarkerColor() const { return ColorOf(Kind); }

		static cs::String LabelOf(OpportunityKind k);

		static Rgb ColorOf(OpportunityKind k);

		cs::String ToString() const { return cs::Cat(MarkerLabel(), ": ", Title); }
	};
}
