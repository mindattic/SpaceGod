#pragma once
// US-T293: the SpaceGod's hand in the Civilization layer, shared by sg.Civ (CivCommand.cpp), the Society screen
// (UI/SocietyScreen) and co-op's "civ" entry (UnitySpaceApp.Society.cpp), so a click, the console and every peer of a co-op
// party make exactly the same call. Bodies in CivHandCommands.cpp.

#include "Cs/Cs.h"

#include <vector>

namespace SpaceGod::Core
{
	class Civilization;
	class Galaxy;
}

namespace SpaceGod
{
	/// <summary>What a line of a view, or the Society screen's selection, is about.</summary>
	enum class CivPickKind : int
	{
		None, House, Person, Org, World, Trial, Business, Faction,
	};

	/// <summary>One thing in the galaxy's society, by what sg.Civ names it with: an id, or a world's or a state's name.</summary>
	struct CivPick
	{
		CivPickKind Kind = CivPickKind::None;
		int Id = 0;
		cs::String Name;

		bool IsNone() const { return Kind == CivPickKind::None; }
		bool operator==(const CivPick& o) const { return Kind == o.Kind && Id == o.Id && Name == o.Name; }
		bool operator!=(const CivPick& o) const { return !(*this == o); }
		/// <summary>The words naming it in an sg.Civ verb: the id; a world's name; a state's name, its spaces as '_' (one word).</summary>
		cs::String Arg() const;

		static CivPick House(int id) { return CivPick{ CivPickKind::House, id, cs::String() }; }
		static CivPick Person(int id) { return CivPick{ CivPickKind::Person, id, cs::String() }; }
		static CivPick Org(int id) { return CivPick{ CivPickKind::Org, id, cs::String() }; }
		static CivPick Trial(int id) { return CivPick{ CivPickKind::Trial, id, cs::String() }; }
		static CivPick Business(int id) { return CivPick{ CivPickKind::Business, id, cs::String() }; }
		static CivPick World(const cs::String& name) { return CivPick{ CivPickKind::World, 0, name }; }
		static CivPick Faction(const cs::String& name) { return CivPick{ CivPickKind::Faction, 0, name }; }
	};

	/// <summary>A line of a module view, and what it is about (none for a line about nothing to pick).</summary>
	struct CivRow
	{
		cs::String Text;
		CivPick Pick;
	};

	namespace CivHands
	{
		/// <summary>Every hand verb sg.Civ has (feud, reconcile, fame, ... donate), in the console's order.</summary>
		const std::vector<const char*>& Verbs();
		bool IsHand(const cs::String& verb);

		/// <summary>The module views sg.Civ prints (feuds, regimes, trials, admirals, crime, money, chronicle), in order.</summary>
		const std::vector<const char*>& Views();
		/// <summary>The lines of the module view <paramref name="view"/> as they stand on <paramref name="jd"/>; false when there is
		/// no such view or its module has not woken yet.</summary>
		bool View(const cs::String& view, Core::Galaxy& galaxy, Core::Civilization& civ, double jd, cs::List<CivRow>& rows);

		/// <summary>
		/// Applies a hand line ("feud 12 40") here, now: the module's own hand, hash-based on the day, so the same line on the same
		/// <paramref name="jd"/> does the same on every peer. Returns why not ("" when done) and the line said for it in
		/// <paramref name="done"/>.
		/// </summary>
		cs::String Apply(Core::Galaxy& galaxy, Core::Civilization& civ, const cs::String& line, double jd, cs::String& done);

		/// <summary>The words of a line, split on spaces (empty words dropped).</summary>
		std::vector<cs::String> Words(const cs::String& line);
	}
}
