// Port of Unity.SpaceGod Core/Settings/GameSetting.cs (US-T21, see docs/MIGRATION.md).

#include "Settings/GameSetting.h"
#include "Settings/SettingsRegistry.h"

namespace SpaceGod::Core
{
	const cs::List<cs::String>& SettingGroups::Order()
	{
		static const cs::List<cs::String> value =
		{
			Gameplay, Graphics, RenderPipeline, RenderBudget, Streaming, Simulation, Ftl, Civilizations, Ai, Dogfighting, Broadsides, Weapons, Economy, Controls, Audio,
		};
		return value;
	}

	const char* ToString(SettingKind v)
	{
		switch (v)
		{
		case SettingKind::Toggle: return "Toggle";
		case SettingKind::Range: return "Range";
		case SettingKind::Choice: return "Choice";
		}
		return "";
	}

	// ------------------------------------------------------------------------------------------------ GameSetting

	GameSetting::GameSetting(const cs::String& key, const cs::String& group, const cs::String& label, const cs::String& description, const cs::String& keywords)
	{
		// C#: throw new ArgumentException("Not a setting key ...: " + key, nameof(key));
		if (!IsValidKey(key)) throw cs::ArgumentException(cs::Cat("Not a setting key (letters, digits, '.', '-' and '_'): ", key, " (Parameter 'key')"));
		Key = key;
		Group = cs::Strings::IsNullOrWhiteSpace(group) ? cs::String("General") : group;
		Label = cs::Strings::IsNullOrWhiteSpace(label) ? key : label;
		Description = description; // C#: description ?? ""
		Keywords = keywords;       // C#: keywords ?? ""
	}

	GameSetting::~GameSetting()
	{
		// Not C#: a setting destroyed before its registry adopted it takes itself back out (see SettingsRegistry.h).
		if (Registry) Registry->ForgetDeclared(this);
	}

	void GameSetting::Reset()
	{
		if (Registry != nullptr) Registry->Reset(Self());
		else if (!IsDefault())
		{
			SetToDefault();
			RaiseChanged();
		}
	}

	void GameSetting::RaiseChanged() { Changed.Invoke(Self()); }

	void GameSetting::OnSet()
	{
		RaiseChanged();
		if (Registry) Registry->Remember(*this);
	}

	cs::Ref<GameSetting> GameSetting::Self()
	{
		auto owned = weak_from_this().lock();
		if (owned) return owned;
		return cs::Ref<GameSetting>(cs::Ref<GameSetting>(), this); // non-owning alias (aliasing constructor, empty owner)
	}

	void GameSetting::Attach(const cs::Ref<SettingsRegistry>& registry) { Registry = registry; }

	bool GameSetting::Matches(const cs::String& query) const
	{
		if (cs::Strings::IsNullOrWhiteSpace(query)) return true;
		cs::String hay = cs::Strings::ToLowerInvariant(cs::Cat(Label, " ", Group, " ", Description, " ", Keywords, " ", Key));
		// C#: query.ToLowerInvariant().Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries)
		for (const auto& word : cs::Strings::Split(cs::Strings::ToLowerInvariant(query), std::string_view(" \t")))
		{
			if (word.empty()) continue;
			if (cs::Strings::IndexOf(hay, word) < 0) return false;
		}
		return true;
	}

	bool GameSetting::IsValidKey(const cs::String& key)
	{
		if (cs::Strings::IsNullOrEmpty(key) || static_cast<int>(key.size()) > 64 || key[0] == '.' || key[key.size() - 1] == '.') return false;
		for (char c : key)
			if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) return false;
		return true;
	}

	cs::String GameSetting::ToString() const { return cs::Cat(Group, " / ", Label, ": ", ValueText()); }

	// ------------------------------------------------------------------------------------------------ ToggleSetting

	ToggleSetting::ToggleSetting(const cs::String& key, const cs::String& group, const cs::String& label, bool defaultValue, const cs::String& description,
		const cs::String& keywords, const cs::Ref<SettingsRegistry>& registry)
		: GameSetting(key, group, label, description, keywords)
	{
		Default = Value = defaultValue;
		(registry ? registry : SettingsRegistry::Default())->RegisterDeclared(this);
	}

	void ToggleSetting::Set(bool value)
	{
		if (value == Value) return;
		Value = value;
		OnSet();
	}

	bool ToggleSetting::TryLoad(const cs::String& text)
	{
		// C#: bool.TryParse -- "True"/"False" in any case, white space around it allowed.
		cs::String t = cs::Strings::Trim(text);
		bool v;
		if (cs::Strings::EqualsOrdinalIgnoreCase(t, "True")) v = true;
		else if (cs::Strings::EqualsOrdinalIgnoreCase(t, "False")) v = false;
		else return false;
		Value = v;
		return true;
	}

	cs::Ref<ToggleSetting> ToggleSetting::Applying(const cs::Action<bool>& apply)
	{
		Changed += [this, apply](cs::Ref<GameSetting>) { apply(Value); };
		apply(Value);
		return std::static_pointer_cast<ToggleSetting>(Self());
	}

	// ------------------------------------------------------------------------------------------------ RangeSetting

	RangeSetting::RangeSetting(const cs::String& key, const cs::String& group, const cs::String& label, double defaultValue, double min, double max, double step,
		const cs::String& description, cs::Func<cs::String, double> display, const cs::String& keywords, const cs::Ref<SettingsRegistry>& registry)
		: GameSetting(key, group, label, description, keywords)
	{
		if (!(min <= max)) throw cs::ArgumentException(cs::Cat("A range's minimum must not be above its maximum: ", key));
		if (step < 0) throw cs::ArgumentException(cs::Cat("A range's step must not be negative: ", key));
		Min = min;
		Max = max;
		Step = step;
		this->display = std::move(display);
		Default = Value = Snap(defaultValue);
		(registry ? registry : SettingsRegistry::Default())->RegisterDeclared(this);
	}

	cs::String RangeSetting::ValueText() const { return display ? display(Value) : cs::Fmt(Value, "0.##"); }

	double RangeSetting::Snap(double v) const
	{
		if (cs::Double::IsNaN(v)) return Default;
		v = cs::Math::Max(Min, cs::Math::Min(Max, v));
		if (Step > 0)
		{
			v = Min + cs::Math::Round((v - Min) / Step) * Step;
			v = cs::Math::Max(Min, cs::Math::Min(Max, cs::Math::Round(v, 9)));
		}
		return v;
	}

	void RangeSetting::Set(double value)
	{
		value = Snap(value);
		if (value == Value) return;
		Value = value;
		OnSet();
	}

	cs::Ref<RangeSetting> RangeSetting::Applying(const cs::Action<double>& apply)
	{
		Changed += [this, apply](cs::Ref<GameSetting>) { apply(Value); };
		apply(Value);
		return std::static_pointer_cast<RangeSetting>(Self());
	}

	bool RangeSetting::TryLoad(const cs::String& text)
	{
		// C#: double.TryParse(text, NumberStyles.Float, Invariant) (cs::Double::TryParse also takes ',' group separators,
		// which NumberStyles.Float refuses: only a hand-edited profile could differ).
		double v;
		if (!cs::Double::TryParse(text, v) || cs::Double::IsNaN(v) || cs::Double::IsInfinity(v))
			return false;
		Value = Snap(v);
		return true;
	}

	// ------------------------------------------------------------------------------------------------ ChoiceSetting

	ChoiceSetting::ChoiceSetting(const cs::String& key, const cs::String& group, const cs::String& label, const cs::List<cs::String>& options, int defaultIndex,
		const cs::String& description, const cs::String& keywords, const cs::Ref<SettingsRegistry>& registry)
		: GameSetting(key, group, label, description, keywords)
	{
		if (options.Count() == 0) throw cs::ArgumentException(cs::Cat("A choice needs at least one option: ", key));
		if (defaultIndex < 0 || defaultIndex >= options.Count()) throw cs::ArgumentOutOfRangeException("defaultIndex");
		Options = options;
		Default = Index = defaultIndex;
		(registry ? registry : SettingsRegistry::Default())->RegisterDeclared(this);
	}

	void ChoiceSetting::Set(int index)
	{
		index = cs::Math::Max(0, cs::Math::Min(Options.Count() - 1, index));
		if (index == Index) return;
		Index = index;
		OnSet();
	}

	cs::Ref<ChoiceSetting> ChoiceSetting::Applying(const cs::Action<int>& apply)
	{
		Changed += [this, apply](cs::Ref<GameSetting>) { apply(Index); };
		apply(Index);
		return std::static_pointer_cast<ChoiceSetting>(Self());
	}

	bool ChoiceSetting::TryLoad(const cs::String& text)
	{
		for (int i = 0; i < Options.Count(); i++)
			if (Options[i] == text)
			{
				Index = i;
				return true;
			}
		return false;
	}
}
