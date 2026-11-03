#pragma once
// Port of Unity.SpaceGod Core/ControlsHelp.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	/// <summary>One line of the Help screen: the keys (empty for a line that carries on the one above) and what they do.</summary>
	struct HelpLine
	{
		cs::String Keys;
		cs::String Text;

		HelpLine() = default;
		// C#: keys ?? "", text ?? "" (a C++ string is never null).
		HelpLine(const cs::String& keys, const cs::String& text)
		{
			Keys = keys;
			Text = text;
		}

		cs::String ToString() const { return Keys.size() == 0 ? cs::Cat("   ", Text) : cs::Cat(Keys, "   ", Text); }
	};

	/// <summary>A group of the Help screen's lines under one heading.</summary>
	class HelpSection final
	{
	public:
		cs::String Title;
		cs::List<HelpLine> Lines;

		// C#: HelpSection(string title, params HelpLine[] lines)
		template <class... L> explicit HelpSection(const cs::String& title, const L&... lines)
		{
			Title = title;
			Lines = cs::List<HelpLine>{ HelpLine(lines)... };
		}
	};

	/// <summary>
	/// Every key, control and help line of the game, in groups (U.SPC-T494): what the Help screen prints, from the
	/// main menu, the Esc menu or H / F1 in game. A new key gets its line here, in the group it belongs to.
	/// </summary>
	class ControlsHelp
	{
	private:
		static HelpLine L(const cs::String& keys, const cs::String& text) { return HelpLine(keys, text); }
		static HelpLine More(const cs::String& text) { return HelpLine("", text); }

	public:
		static const cs::List<cs::Ref<HelpSection>>& Sections();

		/// <summary>Every line of every section, in order.</summary>
		static cs::List<HelpLine> AllLines();
	};
}
