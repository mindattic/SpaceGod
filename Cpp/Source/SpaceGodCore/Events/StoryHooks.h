#pragma once
// US-T139: the pregenerated story pools for the dynamic events (EventDirector's opportunities).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Opportunity;

	/// <summary>
	/// The words a caller says (US-T139). A distress call (a ship pulled out of a lane and adrift, one drifting with dry
	/// tanks, one under fire, and a raider band's lure, which reads as the first) is told from pools of pregenerated
	/// openers, predicaments and pleas, combined into thousands of calls; the call's seed (its source and id) picks one, so a
	/// call always reads the same, on every peer and after a rewind. Every other kind keeps the director's own hook.
	///
	/// Presentation only: the director's hook (<see cref="Opportunity.Hook"/>) stays what the world logs, so nothing here
	/// moves the simulation or the Bench digests. A BYOKey voice (<see cref="StoryVoice"/>) may reword a call; this is its
	/// fallback, and what it is handed as the facts.
	/// </summary>
	class SPACEGODCORE_API StoryHooks final
	{
	public:
		/// <summary>The call as the caller says it, or <see cref="Opportunity.Hook"/> for a kind with no pool.</summary>
		static cs::String Tell(const cs::Ref<Opportunity>& op);

		/// <summary>How many different calls the pools can make, all kinds together (the manual quotes it).</summary>
		static int Combinations();

		/// <summary>The name a raider band broadcasts its false call under: an honest-sounding hull, fixed per seed.</summary>
		static cs::String LureName(int seed);

		/// <summary>The seed a call is told with: its source and id, stable across peers and rewinds.</summary>
		static uint32_t SeedOf(const cs::Ref<Opportunity>& op);
	};
}
