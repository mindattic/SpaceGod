#pragma once

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class SettingsRegistry;
}

namespace SpaceGod
{
	/// Puts the settings registry (U.SPC-T431) to work in the game: finds every setting the game's systems declare
	/// (classes marked SettingsSourceAttribute, in every assembly that uses Core) once, and binds them
	/// to the active profile, so each takes the value that profile remembers and each change is written there.
	///
	/// C++: no reflection, so the Runtime's settings sources are registered by hand with
	/// SettingsRegistry::AddDeclarationSource (RegisterRuntimeSources, in C#'s discovery order: the Runtime assembly's
	/// [SettingsSource] classes after Core's) before the first discovery.
	class GameSettingsHost
	{
	public:
		static const cs::Ref<Core::SettingsRegistry>& Registry();

		/// Discovers the declarations (the first time) and loads the active profile's values into them.
		static void Bind();

	private:
		static void RegisterRuntimeSources();
		static inline bool discovered = false;
	};
}
