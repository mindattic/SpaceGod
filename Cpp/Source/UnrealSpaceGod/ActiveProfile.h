#pragma once

#include "Cs/Cs.h"

namespace SpaceGod::Shared
{
	class ProfileStore;
	class Profile;
}

namespace SpaceGod
{
	/// The player profile this run uses, from the shared MindAttic profile store (the Unity.Shared package,
	/// <c>Profiles</c> in the player's data folder): current.txt names it, its one settings.json holds the common
	/// settings and the game's own under <c>game</c> (see GameSettings), and its Saves folder the
	/// save slots. Profiles from before settings.json are migrated the first time the store is used.
	class ActiveProfile
	{
	public:
		static cs::Ref<Shared::ProfileStore> Store();

		static cs::Ref<Shared::Profile> Current();

		/// Makes <paramref name="name"/> the profile in use (made if it is new, and remembered for the next
		/// run) and puts its display settings, and every other setting it remembers, into effect.
		static cs::Ref<Shared::Profile> Switch(const cs::String& name);

	private:
		static inline bool migrated = false;
		static inline cs::Ref<Shared::Profile> current;
	};
}
