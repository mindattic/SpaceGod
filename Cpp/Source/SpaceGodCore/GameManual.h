#pragma once
// Port of Unity.SpaceGod Core/GameManual.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class HelpSection;

	/// <summary>One topic of a manual chapter: a short heading and the paragraph that explains it.</summary>
	struct ManualTopic
	{
		cs::String Heading;
		cs::String Text;

		ManualTopic() = default;
		// C#: heading ?? "", text ?? "" (a C++ string is never null).
		ManualTopic(const cs::String& heading, const cs::String& text)
		{
			Heading = heading;
			Text = text;
		}
	};

	/// <summary>
	/// One chapter of the in-game manual: its title in the contents list, a line saying what it covers, its topics,
	/// and the <see cref="ControlsHelp"/> groups whose keys it prints after them (the keys live in one place only).
	/// </summary>
	class SPACEGODCORE_API ManualChapter final
	{
	public:
		cs::String Title;
		cs::String Summary;
		cs::List<ManualTopic> Topics;
		cs::List<cs::String> ControlGroups;

		// C#: ManualChapter(string title, string summary, string[] controlGroups, params ManualTopic[] topics);
		// a null controlGroups is an empty list (C#: controlGroups ?? new string[0]).
		template <class... T> ManualChapter(const cs::String& title, const cs::String& summary, cs::List<cs::String> controlGroups, const T&... topics)
		{
			Title = title;
			Summary = summary;
			ControlGroups = std::move(controlGroups);
			Topics = cs::List<ManualTopic>{ ManualTopic(topics)... };
		}

		/// <summary>The Help screen's key groups this chapter prints, in the order it names them.</summary>
		cs::List<cs::Ref<HelpSection>> Controls() const;
	};

	/// <summary>
	/// The in-game manual (U.SPC-T497): everything in the game, chapter by chapter, opened from the Help screen's
	/// contents list (main menu Help, H / F1 in game, the Esc menu's Help). It is kept current as features land:
	/// a new feature is not done until its topic is written here, in the chapter it belongs to, and any new key has
	/// its line in <see cref="ControlsHelp"/> (which the chapters print, so a key is never written twice). A new
	/// chapter is one more entry in <see cref="Chapters"/>.
	/// </summary>
	class SPACEGODCORE_API GameManual
	{
	public:
		/// <summary>The last chapter: every key group of <see cref="ControlsHelp"/>, the whole reference card.</summary>
		static constexpr const char* EveryKeyTitle = "Every key";

	private:
		static ManualTopic T(const cs::String& heading, const cs::String& text) { return ManualTopic(heading, text); }
		template <class... G> static cs::List<cs::String> Keys(const G&... groups) { return cs::List<cs::String>{ cs::String(groups)... }; }

	public:
		static const cs::List<cs::Ref<ManualChapter>>& Chapters();

		/// <summary>The key groups a chapter prints: its own, or every group for <see cref="EveryKeyTitle"/>.</summary>
		static cs::List<cs::Ref<HelpSection>> ControlsOf(const cs::Ref<ManualChapter>& chapter);

		/// <summary>The chapter of that title, or null.</summary>
		static cs::Ref<ManualChapter> Find(const cs::String& title);
	};
}
