#pragma once
// Credits (US-T246): everyone and everything the game is made from, for the Credits screen.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	/// <summary>One credit: who or what (a person, a pack, a library, a data set), what it is or did, and its licence
	/// (empty where none applies: the team, the game's own work).</summary>
	struct CreditEntry
	{
		cs::String Name;
		cs::String Detail;
		cs::String Licence;

		CreditEntry() = default;
		CreditEntry(const cs::String& name, const cs::String& detail, const cs::String& licence = "")
			: Name(name), Detail(detail), Licence(licence)
		{
		}
	};

	/// <summary>One heading of the credits (the team, the original game, the engine, art packs...) and its entries.</summary>
	class SPACEGODCORE_API CreditSection final
	{
	public:
		cs::String Title;
		cs::List<CreditEntry> Entries;

		explicit CreditSection(const cs::String& title) : Title(title) {}
	};

	/// <summary>
	/// The game's credits (US-T246), in the order the Credits screen rolls them: the team, the original game, the
	/// engine, every third-party art pack, sky, map and data set with its licence (docs/ThirdPartyArt.md,
	/// Cpp/SourceArt/Textures/SOURCES.md), the code libraries, and the tools the assets were prepared with. The
	/// shipped skies come from <see cref="SkyboxCatalog::BuiltIn"/>, so a new sky is credited by being registered.
	/// A new pack or library is one more entry here.
	/// </summary>
	class SPACEGODCORE_API Credits
	{
	public:
		/// <summary>The game's name and the line under it, at the top of the roll.</summary>
		static constexpr const char* Title = "SPACEGOD";
		static constexpr const char* Tagline = "The real Sol system, played co-op";

		static const cs::List<cs::Ref<CreditSection>>& Sections();

		/// <summary>The line at the very end of the roll.</summary>
		static constexpr const char* Closing = "Thank you for playing.";
	};
}
