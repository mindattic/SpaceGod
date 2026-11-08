// US-T253: difficulty levels; see Difficulty.h.

#include "Difficulty.h"

#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	namespace
	{
		struct Preset
		{
			double Damage, Accuracy, Lock, Aggression, Pay;
		};

		// Story: the galaxy goes easy on you; Hard: it does not. Normal is the game as designed (and as the Bench runs it).
		constexpr Preset StoryPreset{ 0.5, 0.6, 2.0, 0.4, 1.25 };
		constexpr Preset NormalPreset{ 1, 1, 1, 1, 1 };
		constexpr Preset HardPreset{ 1.5, 1.3, 0.6, 1.6, 0.85 };

		/// The pirate target bias at full keenness either way: a pirate's mark score is roughly its distance in km.
		constexpr double PirateBiasKm = 12;

		cs::String Times(double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }

		Preset Now()
		{
			switch (Difficulty::Current())
			{
			case DifficultyLevel::Story: return StoryPreset;
			case DifficultyLevel::Hard: return HardPreset;
			case DifficultyLevel::Custom:
				return { Difficulty::CustomDamageTaken()->Value, Difficulty::CustomAccuracy()->Value, Difficulty::CustomLockTime()->Value,
					Difficulty::CustomAggression()->Value, Difficulty::CustomPay()->Value };
			default: return NormalPreset;
			}
		}
	}

	const cs::Ref<ChoiceSetting>& Difficulty::Level()
	{
		static const cs::Ref<ChoiceSetting> setting = cs::New<ChoiceSetting>("gameplay.difficulty", SettingGroups::Gameplay, "Difficulty",
			cs::List<cs::String>{ "Story", "Normal", "Hard", "Custom" }, static_cast<int>(DifficultyLevel::Normal),
			cs::Cat("How hard the galaxy is on the unit you embody, and only on it: the damage your hull takes, how well hostile gunners aim at you and ",
				"how fast their missiles lock you, how keen pirates are to go for you, and what the contracts you take pay. Story: half the damage, ",
				"wilder fire, slow locks, pirates less keen, a quarter more pay. Hard: half again the damage, sharper fire, quick locks, pirates ",
				"keener, less pay. Custom: the five Custom sliders below. Fights between other ships are the same at every level. Chosen beside New ",
				"Game, changeable here at any time, kept in each save; in co-op the host's is everyone's."),
			"difficulty story normal hard custom easy damage accuracy lock pirates pay contracts challenge");
		return setting;
	}

	const cs::Ref<RangeSetting>& Difficulty::CustomDamageTaken()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("gameplay.custom.damageTaken", SettingGroups::Gameplay,
			"Custom: damage taken", 1.0, 0.25, 2.5, 0.05,
			"With Difficulty on Custom: the damage the hull you embody takes from shots, missiles and impacts, times the usual.",
			[](double v) { return Times(v); }, "difficulty custom damage taken incoming hull shields");
		return setting;
	}

	const cs::Ref<RangeSetting>& Difficulty::CustomAccuracy()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("gameplay.custom.accuracy", SettingGroups::Gameplay,
			"Custom: hostile accuracy", 1.0, 0.25, 2.0, 0.05,
			"With Difficulty on Custom: how well hostile gunners and point defence aim at the hull you embody, times their skill.",
			[](double v) { return Times(v); }, "difficulty custom enemy hostile accuracy aim gunners");
		return setting;
	}

	const cs::Ref<RangeSetting>& Difficulty::CustomLockTime()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("gameplay.custom.lockTime", SettingGroups::Gameplay,
			"Custom: hostile lock time", 1.0, 0.25, 3.0, 0.05,
			"With Difficulty on Custom: how long a hostile missile mount takes to lock the hull you embody between launches, times the usual.",
			[](double v) { return Times(v); }, "difficulty custom missile lock time torpedo enemy");
		return setting;
	}

	const cs::Ref<RangeSetting>& Difficulty::CustomAggression()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("gameplay.custom.pirateAggression", SettingGroups::Gameplay,
			"Custom: pirate aggression", 1.0, 0.0, 2.0, 0.05,
			"With Difficulty on Custom: how keen pirates are to pick the hull you embody over other marks and to come hunting it, times the usual.",
			[](double v) { return Times(v); }, "difficulty custom pirates raiders aggression hunt target");
		return setting;
	}

	const cs::Ref<RangeSetting>& Difficulty::CustomPay()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("gameplay.custom.pay", SettingGroups::Gameplay,
			"Custom: contract pay", 1.0, 0.25, 3.0, 0.05,
			"With Difficulty on Custom: what the contracts you take pay, times the price posted on the board.",
			[](double v) { return Times(v); }, "difficulty custom pay contracts reward credits money");
		return setting;
	}

	DifficultyLevel Difficulty::Current()
	{
		int i = Level()->Index;
		return i >= 0 && i <= static_cast<int>(DifficultyLevel::Custom) ? static_cast<DifficultyLevel>(i) : DifficultyLevel::Normal;
	}

	cs::String Difficulty::Name() { return Level()->Selected(); }

	double Difficulty::DamageTaken() { return Now().Damage; }
	double Difficulty::HostileAccuracy() { return Now().Accuracy; }
	double Difficulty::LockTime() { return Now().Lock; }
	double Difficulty::PirateAggression() { return Now().Aggression; }
	double Difficulty::Pay() { return Now().Pay; }

	bool Difficulty::Active()
	{
		auto p = Now();
		return p.Damage != 1 || p.Accuracy != 1 || p.Lock != 1 || p.Aggression != 1 || p.Pay != 1;
	}

	// ---------------------------------------------------------------- hooks

	double Difficulty::Incoming(bool playerHeld, double damage)
	{
		if (!playerHeld) return damage;
		double k = DamageTaken();
		return k == 1 ? damage : damage * k;
	}

	double Difficulty::AccuracyAt(bool playerHeld, double accuracyScale)
	{
		if (!playerHeld) return accuracyScale;
		double k = HostileAccuracy();
		return k == 1 ? accuracyScale : accuracyScale * k;
	}

	double Difficulty::ReloadAfterLock(bool playerHeld, double cooldown, double lockSeconds)
	{
		if (!playerHeld) return cooldown;
		double k = LockTime();
		if (k == 1) return cooldown;
		// The crew's own lock is part of the design's reload; what the level changes is how much longer (or shorter) it takes.
		return cs::Math::Max(cooldown * 0.25, cooldown + lockSeconds * (k - 1));
	}

	double Difficulty::PirateTargetBias(bool pirateShooter, bool playerHeld)
	{
		if (!pirateShooter || !playerHeld) return 0;
		double k = PirateAggression();
		return k == 1 ? 0 : (1 - k) * PirateBiasKm;
	}

	double Difficulty::PirateHuntKm(bool pirateHunter, double huntKm)
	{
		if (!pirateHunter) return huntKm;
		double k = PirateAggression();
		return k == 1 ? huntKm : huntKm * k;
	}

	double Difficulty::PayFor(bool byPlayer, double pay)
	{
		if (!byPlayer) return pay;
		double k = Pay();
		return k == 1 ? pay : cs::Math::Round(pay * k * 0.01) * 100;
	}

	// ---------------------------------------------------------------- the save

	cs::String Difficulty::Record()
	{
		if (Current() != DifficultyLevel::Custom) return Name();
		return cs::Cat(Name(), " ", cs::Fmt(CustomDamageTaken()->Value, "R"), " ", cs::Fmt(CustomAccuracy()->Value, "R"), " ",
			cs::Fmt(CustomLockTime()->Value, "R"), " ", cs::Fmt(CustomAggression()->Value, "R"), " ", cs::Fmt(CustomPay()->Value, "R"));
	}

	bool Difficulty::Restore(const cs::String& record)
	{
		if (cs::Strings::IsNullOrWhiteSpace(record)) return false;
		cs::List<cs::String> parts;
		for (const auto& p : cs::Strings::Split(record, ' '))
			if (!p.empty()) parts.Add(p);
		if (parts.Count() == 0) return false;
		int index = Level()->Options.IndexOf(parts[0]);
		if (index < 0) return false;
		if (static_cast<DifficultyLevel>(index) == DifficultyLevel::Custom && parts.Count() >= 6)
		{
			const cs::Ref<RangeSetting>* custom[] = { &CustomDamageTaken(), &CustomAccuracy(), &CustomLockTime(), &CustomAggression(), &CustomPay() };
			for (int i = 0; i < 5; i++)
			{
				double v = 0;
				if (cs::Double::TryParse(parts[i + 1], v) && !cs::Double::IsNaN(v) && !cs::Double::IsInfinity(v)) (*custom[i])->Set(v);
			}
		}
		Level()->Set(index);
		return true;
	}
}
