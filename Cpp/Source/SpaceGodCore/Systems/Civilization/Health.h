#pragma once
// US-T286: Health, the galaxy's sickness, births, disasters and hunger (Civilization.h).
//  - Epidemics: a fever breaks out (likelier where care is poor and crime high), spreads through a world's people
//    (Civics.Infected, Immune), kills (fewer where hospitals are good, named people too), and travels with the trips and
//    the refugees. A badly hit world is quarantined (trips there are called off) until it is clear.
//  - Hospitals: worlds with rich houses build them (Civics.Care); a sick world short of care runs out of medicine.
//  - Births and age: peace after a war brings a baby boom; a grim, poor world grows old.
//  - Disasters: now and then catastrophe strikes a world (people and businesses lost, unrest); its state's friends send
//    relief, and are liked the better for it.
//  - Famine: a rationed world goes hungry; friendly states ship food.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Civilization;
	class Faction;
	class Galaxy;
	class Settlement;
	class Society;

	class SPACEGODCORE_API Health final
	{
	public:
		static constexpr int MaxOutbreaks = 2;
		static constexpr double QuarantineDays = 60;

		/// <summary>Outbreaks under way (worlds still sick), and all there have been.</summary>
		int Active = 0, Outbreaks = 0;

		void Day(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Month(Civilization& civ, Galaxy& galaxy, Society& society, double jd);

		// ---------------------------------------------------------------- the SpaceGod's hand (each returns "" or why not)
		cs::String Plague(Civilization& civ, Galaxy& galaxy, const cs::Ref<Settlement>& s, double jd);
		cs::String Cure(Civilization& civ, Galaxy& galaxy, const cs::Ref<Settlement>& s, double jd);
		cs::String Strike(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Settlement>& s, double jd);
		cs::String Aid(Civilization& civ, Galaxy& galaxy, const cs::Ref<Faction>& by, const cs::Ref<Settlement>& s, double jd);

	private:
		void Sickness(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Care(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Births(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Disasters(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Disaster(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Settlement>& s, double jd, bool steered);
		void Hunger(Civilization& civ, Galaxy& galaxy, double jd);
		/// <summary>A friendly state helps a stricken world (relief or food); true if one did.</summary>
		bool Help(Civilization& civ, Galaxy& galaxy, const cs::Ref<Settlement>& s, bool food, double jd, const cs::Ref<Faction>& forced);
		void Kill(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Settlement>& s, double share, double jd);

		/// <summary>Worlds that were at war last month (for the baby boom), by name hash.</summary>
		cs::List<int> lastWar = cs::List<int>();

		SG_SERIALIZABLE(Health);
	};
}
