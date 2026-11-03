#pragma once
// Port of Unity.SpaceGod Core/Combat/FleetPatterns.cs (US-T10).

#include <utility>

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Ship;
	class Faction;
	class Settlement;
	class Loadout;
	class IPartSpec;
	class BuildPlan;
	class RangeSetting;
	enum class ShipClass : int;
	enum class ModuleKind : int;

	/// <summary>
	/// One stock design's place for a role weapon (U.SPC-T505, docs/SHIP_ROSTER.md "Weapons by tier"): the class it is for,
	/// the weapon, and the stock part it goes in place of on that design, up to <see cref="Count"/> of them. A hull built to
	/// the pattern carries the weapon there from the slips; a warship refitting at its own yard may take it there later.
	/// </summary>
	class SPACEGODCORE_API WeaponPattern final
	{
	public:
		const cs::String Name;
		const ShipClass Class;
		const ModuleKind Weapon;

		/// <summary>The stock part the weapon goes in place of.</summary>
		const ModuleKind Replaces;

		/// <summary>How many of those parts it takes the place of (the first ones in the design's layout).</summary>
		const int Count;

		WeaponPattern(cs::String name, ShipClass cls, ModuleKind weapon, ModuleKind replaces, int count);

		cs::String ToString() const { return Name; }
	};

	/// <summary>
	/// How the navies and raiders fit the role weapons (U.SPC-T505): phaser arrays on destroyers and cruisers, plasma cannons
	/// on the brawlers, the tachyon lance on a rare capital. (Antimatter torpedoes on frigates and capitals, and flak and
	/// interceptor missiles on destroyers, are already in those stock designs, U.SPC-T489.) Each weapon has a place on its
	/// classes' stock designs (<see cref="Patterns"/>), and every new hull a navy or a raider band lays down is built to
	/// those patterns, or not, by three things: the owner's doctrine (its build plan, and each navy's own leaning, so two
	/// navies under the same plan still differ, <see cref="Leaning"/>), its wealth (it must hold <see cref="Wealth"/> times
	/// what the weapons add to the hull's price) and what the yard can build (only the greatest lawful yards build a lance).
	/// A navy that favours a weapon also refits its older hulls with it at its own yards (<see cref="Wishes"/>). Every
	/// figure is a setting in Settings &gt; AI.
	/// </summary>
	// [SettingsSource]
	class SPACEGODCORE_API FleetPatterns
	{
	public:
		/// <summary>C# RuntimeHelpers.RunClassConstructor (what SettingsRegistry does for a [SettingsSource] class): creates
		/// every static field in textual order, then runs the static constructor. Every static field accessor runs it first.</summary>
		static void RunClassConstructor();

		// ------------------------------------------------------------ settings

	private:
		static cs::String Times(double v);
		static cs::String Percent(double v);

	public:
		static const cs::Ref<RangeSetting>& Wealth();

		static const cs::Ref<RangeSetting>& Spread();

		static const cs::Ref<RangeSetting>& LanceShare();

		// ------------------------------------------------------------ the patterns

	private:
		static cs::List<cs::Ref<WeaponPattern>>& patterns();

	public:
		static const cs::List<cs::Ref<WeaponPattern>>& Patterns();

		/// <summary>Adds a pattern (a new class taking up a weapon's role, the gate breaker's plasma cannons once it is a
		/// class), replacing any with the same name.</summary>
		static void Register(const cs::Ref<WeaponPattern>& p);

		/// <summary>The patterns for hulls of <paramref name="cls"/>.</summary>
		static cs::List<cs::Ref<WeaponPattern>> For(ShipClass cls);

		// ------------------------------------------------------------ doctrine

	private:
		/// <summary>
		/// How much each build plan's doctrine likes each role weapon, 0 to 1: a battle line its plasma cannons and lances, a
		/// carrier navy and its screens the phaser arrays that sweep fighters and missiles away. A plan not listed likes
		/// them as the balanced fleet does.
		/// </summary>
		static cs::Dictionary<cs::String, cs::Dictionary<ModuleKind, double>>& doctrine();

		/// <summary>The raiders' own taste: phaser arrays when they can pay for them; no lance, which no haven builds.</summary>
		static cs::Dictionary<ModuleKind, double>& raiders();

		/// <summary>The C# static constructor's body (run by <see cref="RunClassConstructor"/> after the static fields).</summary>
		static void StaticConstructor();

	public:
		/// <summary>Sets how much <paramref name="plan"/>'s doctrine likes phaser arrays, plasma cannons and the lance.</summary>
		static void Lean(const cs::Ref<BuildPlan>& plan, double phasers, double plasma, double lance);

		/// <summary>
		/// How much <paramref name="f"/> likes <paramref name="weapon"/>, 0 to 1: its doctrine's liking (its build plan's,
		/// <paramref name="plan"/>, the balanced fleet's if none; the raiders' own if it is a raider), moved either way by up to
		/// half of <see cref="Spread"/> by the navy's own taste, which is fixed by its name, so the same navy always leans the
		/// same way.
		/// </summary>
		static double Leaning(const cs::Ref<Faction>& f, ModuleKind weapon, const cs::Ref<BuildPlan>& plan);

	private:
		/// <summary>A navy's own taste for a weapon, 0 to 1, the same every game (a hash of its name and the weapon).</summary>
		static double Taste(const cs::String& name, ModuleKind weapon);

	public:
		/// <summary>The liking at which a navy refits its older hulls with a weapon, and not just builds new ones with it.</summary>
		static constexpr double RefitLeaning = 0.5;

		// ------------------------------------------------------------ fitting

		/// <summary>The hardpoints of <paramref name="l"/> that pattern <paramref name="p"/> puts its weapon on and that still
		/// carry the part it replaces: its places are the first <see cref="WeaponPattern.Count"/> of the design's own mounts
		/// built with that part that take the weapon. Empty if none are left.</summary>
		static cs::List<int> Slots(const cs::Ref<Loadout>& l, const cs::Ref<WeaponPattern>& p);

		/// <summary>What the pattern's weapons add to the hull's price, credits: their materials at base prices less those of
		/// the parts they replace.</summary>
		static double ExtraCost(const cs::Ref<Loadout>& l, const cs::List<int>& slots, ModuleKind weapon);

	private:
		static double Worth(const cs::Ref<IPartSpec>& part, double scale);

		/// <summary>Whether <paramref name="yard"/> can build <paramref name="weapon"/> (a raider band raised with no haven
		/// counts as the haven's black-market slips: anything but a lance).</summary>
		static bool YardBuilds(ModuleKind weapon, const cs::Ref<Settlement>& yard);

	public:
		/// <summary>
		/// Builds a navy's or raiders' new hull to its owner's patterns as it leaves the slips at <paramref name="yard"/>
		/// (null for a raider band raised with no haven): each pattern for its class is taken with a chance of the owner's
		/// <see cref="Leaning"/> for the weapon (times <see cref="LanceShare"/> for the lance), if the yard can build the
		/// weapon and the owner holds <see cref="Wealth"/> times what it adds to the hull's price, which it pays. Returns the
		/// patterns it was built to.
		/// </summary>
		/// <remarks>C#'s `Random rng = null` (a nullable reference) is a pointer here: null seeds one from the owner and hull.</remarks>
		static cs::List<cs::Ref<WeaponPattern>> Commission(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, const cs::Ref<BuildPlan>& plan, cs::Random* rng = nullptr);

		/// <summary>
		/// The role weapons a navy's or raiders' warship wants at its own yard (<see cref="Outfitting.AiRefit"/>): each
		/// pattern for its class whose weapon its owner favours (<see cref="RefitLeaning"/>), on the hardpoints the pattern
		/// puts it. The lance is left to <see cref="Outfitting.AiRefit"/>'s own ordering (<see cref="FavoursLance"/>).
		/// </summary>
		/// <remarks>C# yields (slot, module) tuples lazily; nothing relies on the laziness, so the list is built eagerly.</remarks>
		static cs::List<std::pair<int, ModuleKind>> Wishes(const cs::Ref<Ship>& ship, const cs::Ref<Loadout>& l, const cs::Ref<BuildPlan>& plan);

		/// <summary>Whether <paramref name="f"/>'s doctrine favours the lance enough to fit one before its drives are
		/// upgraded, rather than last of all.</summary>
		static bool FavoursLance(const cs::Ref<Faction>& f, const cs::Ref<BuildPlan>& plan);
	};
}
