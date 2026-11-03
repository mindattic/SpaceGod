#pragma once
// Not a C# file (US-T36): what a load wires again (Persistence/Registrations/Wiring.cpp).

namespace SpaceGod::Core
{
	class Galaxy;

	namespace Registrations
	{
		/// <summary>
		/// Galaxy's SG_LOADED hook: wires every delegate of a loaded galaxy again (a C++ save holds none), each world's
		/// first and then the galaxy's, in the order construction wired them.
		/// </summary>
		void RewireGalaxy(Galaxy& g);
	}
}
