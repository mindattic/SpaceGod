#pragma once
// US-T280: Strife, the violent side of the Civilization layer (Civilization.h).
//  - Protests, riots and strikes: a world that dislikes its rulers protests; a furious, restless one riots (businesses
//    burned, unrest up) and may down tools, its industry idled for days (the same lever MarketShocks' strikes pull).
//  - Insurgency: a world drifting from its flag, occupied or hating its rulers grows an armed cell (Org Insurgents); the
//    cell grows on discontent and crime, strikes at the state (sabotage) and, strong enough on a restless enough world,
//    rises and breaks it away (GalaxyLife::Secede). A content, well-held world starves it.
//  - Spies: states at war or at odds keep an agency (Org Agency) that leaks their rivals' pacts (a third state cools, may
//    break the pact), steals their research and turns their governors, and is caught now and then.
//  - War weariness and conscription: a long war wears its people down; past a point they say so, and past another the
//    state sues for peace (Statecraft::MakePeace). Hawkish states call up their reserves, at a price in weariness.
//  - Mercenaries: free companies (Org Mercenaries) hire out to states at war (easing their people's burden) and to
//    feuding houses; a richer enemy may buy them away.
//  - Admirals: a state at war names an admiral from its houses, whose fame grows with the war (and feeds coups, Politics).
//  - Prisoners: peace sends the prisoners home, warming the two states a little.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Citizen;
	class Civilization;
	class Faction;
	class Galaxy;
	class Org;
	class Settlement;
	class Society;

	/// <summary>A world downing tools (US-T280; Commerce's unions call them too).</summary>
	class SPACEGODCORE_API Walkout final
	{
	public:
		cs::Ref<SpaceGod::Core::Settlement> Settlement;
		cs::Ref<Org> Union;
		double StartJd = 0, EndJd = 0;
	};

	/// <summary>A state's admiral (US-T280).</summary>
	class SPACEGODCORE_API Admiral final
	{
	public:
		cs::Ref<SpaceGod::Core::Faction> Faction;
		cs::Ref<Citizen> Who;
		double SinceJd = 0;
	};

	class SPACEGODCORE_API Strife final
	{
	public:
		static constexpr int MaxMercs = 6;

		cs::List<cs::Ref<Walkout>> Walkouts = cs::List<cs::Ref<Walkout>>();
		cs::List<cs::Ref<Admiral>> Admirals = cs::List<cs::Ref<Admiral>>();

		void Day(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Month(Civilization& civ, Galaxy& galaxy, Society& society, double jd);

		/// <summary>Downs tools on a world for <paramref name="days"/> (idling its industry); false if it is already out.</summary>
		bool CallStrike(Civilization& civ, Galaxy& galaxy, const cs::Ref<Settlement>& s, const cs::Ref<Org>& by, double days, double jd);
		bool OnStrike(const cs::Ref<Settlement>& s) const;
		cs::Ref<Admiral> AdmiralOf(const cs::Ref<Faction>& f) const;

		// ---------------------------------------------------------------- the SpaceGod's hand (each returns "" or why not)
		cs::String Incite(Civilization& civ, Galaxy& galaxy, const cs::Ref<Settlement>& s, double jd);
		cs::String Arm(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Settlement>& s, double jd);
		cs::String Spy(Civilization& civ, Galaxy& galaxy, const cs::Ref<Faction>& by, const cs::Ref<Faction>& on, double jd);
		cs::String Hire(Civilization& civ, Galaxy& galaxy, const cs::Ref<Org>& company, const cs::Ref<Faction>& by, double jd);

	private:
		int seenEvent = 0;
		double nextWearyNewsJd = 0;

		void Streets(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Insurgency(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Spies(Civilization& civ, Galaxy& galaxy, double jd);
		bool Operation(Civilization& civ, Galaxy& galaxy, const cs::Ref<Org>& agency, const cs::Ref<Faction>& on, double jd, bool steered);
		void Weariness(Civilization& civ, Galaxy& galaxy, double jd);
		void Mercenaries(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Admiralty(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void React(Civilization& civ, Galaxy& galaxy, double jd);
		void Burn(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Settlement>& s, double share, double jd);

		SG_SERIALIZABLE(Strife);
	};
}
