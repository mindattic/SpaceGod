#pragma once
// Port of Unity.SpaceGod Core/Settings/GameSetting.cs (US-T21, see docs/MIGRATION.md).
//
// A setting is created with cs::New<RangeSetting>(...) (its declaring class keeps it in a function-local static) and,
// as in C#, its constructor registers it: `(registry ?? SettingsRegistry.Default).Register(this)`. A C++ constructor
// cannot hand out a shared_ptr to itself, so the registry takes it through RegisterDeclared (raw pointer) and adopts
// it as an owning cs::Ref the first time the registry is read after the constructor returned (see SettingsRegistry.h).
// A setting destroyed before then takes itself back out.

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class SettingsRegistry;

	/// <summary>
	/// The groups the settings menu shows, in the order it shows them (U.SPC-T431). A setting may name a group
	/// that is not here; it is listed after these, by name. A group with no settings declared in it is not shown.
	/// </summary>
	class SPACEGODCORE_API SettingGroups
	{
	public:
		static constexpr const char* Graphics = "Graphics";
		static constexpr const char* RenderPipeline = "Render pipeline";
		static constexpr const char* RenderBudget = "LOD & render budget";
		static constexpr const char* Streaming = "Streaming";
		static constexpr const char* Simulation = "Simulation";
		static constexpr const char* Ftl = "FTL";
		static constexpr const char* Civilizations = "Civilizations";
		static constexpr const char* Ai = "AI";
		static constexpr const char* Dogfighting = "Dogfighting";
		static constexpr const char* Broadsides = "Broadsides";
		static constexpr const char* Weapons = "Weapons";
		static constexpr const char* Economy = "Economy";
		static constexpr const char* Controls = "Controls";
		static constexpr const char* Audio = "Audio";

		static const cs::List<cs::String>& Order();
	};

	/// <summary>What sort of control the menu draws for a setting.</summary>
	enum class SettingKind : int
	{
		Toggle,
		Range,
		Choice,
	};
	SPACEGODCORE_API const char* ToString(SettingKind v);

	/// <summary>
	/// One tunable a system declares (U.SPC-T431): its key (the field of the profile's settings it is kept in), the group
	/// and label the settings menu shows it under, its default, and what it may be set to. A system declares its
	/// settings as static fields of a class marked <see cref="SettingsSourceAttribute"/>; they appear in the menu
	/// by themselves, so a new mechanic adds a setting by declaring it, never by editing the menu.
	/// </summary>
	class SPACEGODCORE_API GameSetting : public std::enable_shared_from_this<GameSetting>
	{
	public:
		// C#: public readonly string Key, Group, Label, Description;
		cs::String Key, Group, Label, Description;

		/// <summary>Extra words the menu's search finds this setting by.</summary>
		cs::String Keywords;

		/// <summary>The registry this setting is in: the one that remembers its value in a profile.</summary>
		cs::Ref<SettingsRegistry> Registry;

	protected:
		GameSetting(const cs::String& key, const cs::String& group, const cs::String& label, const cs::String& description, const cs::String& keywords);

	public:
		virtual ~GameSetting();

		virtual SettingKind Kind() const = 0;

		/// <summary>Whether the setting stands at its default (the menu's Reset does nothing then).</summary>
		virtual bool IsDefault() const = 0;

		/// <summary>The value as the menu shows it ("144 fps", "On", "75%").</summary>
		virtual cs::String ValueText() const = 0;

		/// <summary>How the value is written in the profile.</summary>
		virtual cs::String Format() const = 0;

		/// <summary>Takes back what <see cref="Format"/> wrote; false (and the value left alone) for anything else.</summary>
		/// (C# internal.)
		virtual bool TryLoad(const cs::String& text) = 0;

		/// (C# internal.)
		virtual void SetToDefault() = 0;

		/// <summary>Puts the value back to its default, and forgets it in the profile.</summary>
		void Reset();

		/// <summary>Raised after the value changes (set, reset or loaded from a profile): where a system puts it into effect.</summary>
		cs::Event<cs::Ref<GameSetting>> Changed;

		/// (C# internal.)
		void RaiseChanged();

	protected:
		/// <summary>Called by the subclass when the player (or code) changed the value.</summary>
		void OnSet();

		/// Not C#: this setting as a cs::Ref (C#'s `this`): the owning one once cs::New has returned, a non-owning alias
		/// while the constructor is still running (when only the registry's own bookkeeping sees it).
		cs::Ref<GameSetting> Self();

	public:
		/// (C# internal.)
		void Attach(const cs::Ref<SettingsRegistry>& registry);

		/// <summary>Whether every word of <paramref name="query"/> is in the label, group, description or keywords.</summary>
		bool Matches(const cs::String& query) const;

		/// <summary>A plain key: letters, digits, '.', '-' and '_', not starting or ending with '.'.</summary>
		static bool IsValidKey(const cs::String& key);

		cs::String ToString() const;
	};

	/// <summary>An on/off setting.</summary>
	class SPACEGODCORE_API ToggleSetting final : public GameSetting
	{
	public:
		bool Default = false;
		bool Value = false;

		ToggleSetting(const cs::String& key, const cs::String& group, const cs::String& label, bool defaultValue, const cs::String& description = cs::String(),
			const cs::String& keywords = cs::String(), const cs::Ref<SettingsRegistry>& registry = nullptr);

		SettingKind Kind() const override { return SettingKind::Toggle; }
		bool IsDefault() const override { return Value == Default; }
		cs::String ValueText() const override { return Value ? "On" : "Off"; }

		void Set(bool value);

		cs::String Format() const override { return Value ? "true" : "false"; }

		bool TryLoad(const cs::String& text) override;

		void SetToDefault() override { Value = Default; }

		/// <summary>Puts the value into effect now and after every change (set, reset or a profile loaded); returns this.</summary>
		cs::Ref<ToggleSetting> Applying(const cs::Action<bool>& apply);

		/// C#: public static implicit operator bool(ToggleSetting s) => s.Value;
		operator bool() const { return Value; }
	};

	/// <summary>A number between <see cref="Min"/> and <see cref="Max"/>, in steps of <see cref="Step"/> (0: any value).</summary>
	class SPACEGODCORE_API RangeSetting final : public GameSetting
	{
	public:
		double Default = 0, Min = 0, Max = 0, Step = 0;

	private:
		/// <summary>How the menu writes a value ("1.25x", "4000 effects"); plain digits if none is given.</summary>
		cs::Func<cs::String, double> display;

	public:
		double Value = 0;

		/// <summary>The value rounded to a whole number, for a count.</summary>
		int IntValue() const { return cs::ToInt(cs::Math::Round(Value)); }

		RangeSetting(const cs::String& key, const cs::String& group, const cs::String& label, double defaultValue, double min, double max, double step = 0,
			const cs::String& description = cs::String(), cs::Func<cs::String, double> display = nullptr, const cs::String& keywords = cs::String(),
			const cs::Ref<SettingsRegistry>& registry = nullptr);

		SettingKind Kind() const override { return SettingKind::Range; }
		bool IsDefault() const override { return Value == Default; }
		cs::String ValueText() const override;

		/// <summary>Where <paramref name="v"/> lands: within the range, on a step counted from <see cref="Min"/>.</summary>
		double Snap(double v) const;

		void Set(double value);

		/// <summary>Where the value sits in the range, 0 to 1 (a slider's position).</summary>
		double Fraction() const { return Max > Min ? (Value - Min) / (Max - Min) : 0; }

		/// <summary>Puts the value into effect now and after every change (set, reset or a profile loaded); returns this.</summary>
		cs::Ref<RangeSetting> Applying(const cs::Action<double>& apply);

		void SetFraction(double f) { Set(Min + cs::Math::Max(0.0, cs::Math::Min(1.0, f)) * (Max - Min)); }

		cs::String Format() const override { return cs::Fmt(Value, "R"); }

		bool TryLoad(const cs::String& text) override;

		void SetToDefault() override { Value = Default; }
	};

	/// <summary>One of a list of named options, remembered by name so options added or reordered later keep the choice.</summary>
	class SPACEGODCORE_API ChoiceSetting final : public GameSetting
	{
	public:
		cs::List<cs::String> Options;
		int Default = 0;
		int Index = 0;

		cs::String Selected() const { return Options[Index]; }

		ChoiceSetting(const cs::String& key, const cs::String& group, const cs::String& label, const cs::List<cs::String>& options, int defaultIndex,
			const cs::String& description = cs::String(), const cs::String& keywords = cs::String(), const cs::Ref<SettingsRegistry>& registry = nullptr);

		SettingKind Kind() const override { return SettingKind::Choice; }
		bool IsDefault() const override { return Index == Default; }
		cs::String ValueText() const override { return Selected(); }

		void Set(int index);

		/// <summary>The next option, or the previous for a negative <paramref name="step"/>, wrapping round.</summary>
		void Cycle(int step) { Set(((Index + step) % Options.Count() + Options.Count()) % Options.Count()); }

		/// <summary>Puts the choice into effect now and after every change (set, reset or a profile loaded); returns this.</summary>
		cs::Ref<ChoiceSetting> Applying(const cs::Action<int>& apply);

		cs::String Format() const override { return Selected(); }

		bool TryLoad(const cs::String& text) override;

		void SetToDefault() override { Index = Default; }
	};
}
