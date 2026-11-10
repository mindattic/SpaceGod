#pragma once
// US-T288: Press, the galaxy's newspapers, gossip, muckrakers and do-gooders (Civilization.h).
//  - Outlets: media houses found news outlets (Org Outlet, owned by the house), slanted toward their flag or against it
//    by their owners' loyalties. An outlet runs the notable stories of its system its own way (the Narrator words the
//    slant), nudging its readers' approval as far as they trust the press; heavy slant wears that trust down.
//  - Boards: ordinary people post their reactions to what happened on their world.
//  - Journalists: outlets keep reporters (the curious and the honest of their world) who dig into the frauds and the
//    bribes of their system and expose them (Underworld::Expose, Society::Reveal); a cruel target may have them killed
//    (Underworld contracts), and a silenced reporter is news of its own.
//  - Charities: the rich (the honest, the pious, or those with a name to clean) found charities (Org Charity) and give:
//    their worlds' care and mood rise, and so do their reputations.

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class Citizen;
	class Civilization;
	class Galaxy;
	class Household;
	class Org;
	class Settlement;
	class Society;

	class SPACEGODCORE_API Press final
	{
	public:
		static constexpr int MaxOutlets = 12;

		/// <summary>Reporters marked for death after a story (for the news of their silencing).</summary>
		cs::List<cs::Ref<Citizen>> Marked = cs::List<cs::Ref<Citizen>>();

		void Day(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Month(Civilization& civ, Galaxy& galaxy, Society& society, double jd);

		// ---------------------------------------------------------------- the SpaceGod's hand (each returns "" or why not)
		cs::String Slant(Civilization& civ, Galaxy& galaxy, const cs::Ref<Org>& outlet, int way, double jd);
		cs::String Investigate(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Org>& outlet, double jd);
		cs::String Donate(Civilization& civ, Galaxy& galaxy, const cs::Ref<Household>& h, double jd);

	private:
		int seenEvent = 0;

		void Outlets(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		void Run(Civilization& civ, Galaxy& galaxy, double jd);
		void Boards(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		bool Dig(Civilization& civ, Galaxy& galaxy, Society& society, const cs::Ref<Org>& outlet, double jd, bool steered);
		void Charities(Civilization& civ, Galaxy& galaxy, Society& society, double jd);
		cs::Ref<Org> Found(Civilization& civ, Galaxy& galaxy, const cs::Ref<Household>& h, double jd, bool steered);

		SG_SERIALIZABLE(Press);
	};
}
