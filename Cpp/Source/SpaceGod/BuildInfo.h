#pragma once

#include "Cs/Cs.h"

namespace SpaceGod
{
	/// US-T245: which build this is, for the main menu's corner (and anyone filing a bug): the game's version
	/// (ProjectVersion in Config/DefaultGame.ini), the build number (the commit count of the checkout it was built from)
	/// and the short commit hash. The number and hash are stamped by SpaceGod.Build.cs into a header under Intermediate
	/// (rewritten when HEAD moves); a build from a tree with no git says "unknown".
	class BuildInfo
	{
	public:
		/// "0.1.0" (or "dev" when the ini names none).
		static cs::String Version();

		/// The commit count ("456"), or empty when unknown.
		static cs::String BuildNumber();

		/// The short commit hash ("40185b5"), or "unknown".
		static cs::String Commit();

		/// "v0.1.0  ·  build 456  ·  40185b5" (the menu's corner line).
		static cs::String Describe();
	};
}
