#pragma once
// US-T253: difficulty levels. Not a port of a C# file.
//
// Story / Normal / Hard / Custom, in the Gameplay group of the settings (a profile setting the menu shows by itself), picked
// on the main menu beside New Game and changeable later. It scales only what touches a unit a player embodies
// (BattleUnit::PlayerControlled, set through the seat commands so every co-op peer and every rewind agrees): the damage the
// held hull takes, the accuracy of hostile fire at it and the time a hostile missile mount needs to lock it, how keen pirates
// are to go for it, and the pay of the contracts a player takes. AI-vs-AI fighting is untouched, and at Normal every scale
// is exactly 1 and every hook a no-op, so the Bench's digests hold.
//
// Co-op: the Gameplay group is shared (Coop/CoopSettings), so the host's choice is everyone's. The save records it
// (Galaxy::Difficulty, written by the game when it captures a save and put back when one is loaded).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;
	class RangeSetting;

	enum class DifficultyLevel : int
	{
		Story,
		Normal,
		Hard,
		Custom,
	};

	// [SettingsSource]: the settings registry finds these by touching each accessor (Settings/SettingsSources.cpp).
	class SPACEGODCORE_API Difficulty final
	{
	public:
		/// <summary>The level: Story, Normal (the default), Hard, or Custom (the five Custom ranges below).</summary>
		static const cs::Ref<ChoiceSetting>& Level();

		/// <summary>Custom only: damage the held hull takes, times the design's.</summary>
		static const cs::Ref<RangeSetting>& CustomDamageTaken();
		/// <summary>Custom only: the accuracy of hostile fire at the held hull, times the gunner's.</summary>
		static const cs::Ref<RangeSetting>& CustomAccuracy();
		/// <summary>Custom only: how long a hostile missile mount takes to lock the held hull, times the usual.</summary>
		static const cs::Ref<RangeSetting>& CustomLockTime();
		/// <summary>Custom only: how keen pirates are to go for the held hull (1 as usual, 0 not at all keener than any other mark).</summary>
		static const cs::Ref<RangeSetting>& CustomAggression();
		/// <summary>Custom only: the pay of a contract a player takes, times its posted price.</summary>
		static const cs::Ref<RangeSetting>& CustomPay();

		static DifficultyLevel Current();
		static cs::String Name();

		/// <summary>The scales now in effect (exactly 1 each at Normal).</summary>
		static double DamageTaken();
		static double HostileAccuracy();
		static double LockTime();
		static double PirateAggression();
		static double Pay();

		/// <summary>Whether anything is scaled at all (false at Normal, and at a Custom left at 1s).</summary>
		static bool Active();

		// ---------------------------------------------------------------- the hooks the simulation calls

		/// <summary>Damage a shot or impact does to a unit: scaled when a player holds it, as it was otherwise.</summary>
		static double Incoming(bool playerHeld, double damage);

		/// <summary>The accuracy scale of a hostile gunner aiming at a unit: scaled when a player holds it.</summary>
		static double AccuracyAt(bool playerHeld, double accuracyScale);

		/// <summary>A hostile missile mount's reload after a launch at a unit a player holds: the lock it must build again
		/// before the next (<paramref name="lockSeconds"/>, stretched or cut by the lock-time scale) is added to it.</summary>
		static double ReloadAfterLock(bool playerHeld, double cooldown, double lockSeconds);

		/// <summary>The extra a pirate's target score (km-like, lower is keener) gives a unit a player holds: 0 at Normal,
		/// positive (less keen) on Story, negative (keener) on Hard.</summary>
		static double PirateTargetBias(bool pirateShooter, bool playerHeld);

		/// <summary>How near (km) a pirate fighter must be to go hunting the player's hull, from the usual
		/// <paramref name="huntKm"/>.</summary>
		static double PirateHuntKm(bool pirateHunter, double huntKm);

		/// <summary>What a contract pays its taker: scaled for a player's, as posted otherwise.</summary>
		static double PayFor(bool byPlayer, double pay);

		// ---------------------------------------------------------------- the save

		/// <summary>The difficulty as a save records it: the level's name, and for Custom its five scales ("Custom 1 0.8 1.5 1 1.2").</summary>
		static cs::String Record();

		/// <summary>Puts back what <see cref="Record"/> wrote; empty (a save from before difficulty) leaves the setting alone.
		/// Returns whether it was understood.</summary>
		static bool Restore(const cs::String& record);
	};
}
