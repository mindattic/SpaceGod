#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/Escorts.cs (US-T19).

#include "Cs/Cs.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	class World;
	class Ship;
	enum class ShipClass : int;

	/// <summary>
	/// Fighter escorts for the merchant fleet (U.SPC-T275). A freighter in port takes on a wing to see it out, sized by
	/// what its hold is worth and how dangerous the waters it is in and bound for are: none on a cheap run through quiet
	/// space, a light fighter or two as the stakes rise, and on a rich cargo into trouble the usual full wing of a heavy
	/// fighter (a gunship) and two light ones. Its owners pay to take each one on and a daily wage while it flies with
	/// them, out of the company's cash (or its flag's treasury for a hull no company owns), and a wing is only taken on
	/// that the owners can well afford. The escorts fly with the freighter from then on, as any escort does.
	/// </summary>
	class SPACEGODCORE_API MerchantEscorts final
	{
	public:
		/// <summary>Days between looks over the merchants in port.</summary>
		static constexpr double CheckDays = 0.25;

		/// <summary>The most fighter escorts the whole merchant fleet keeps at once, so the system does not fill with them.</summary>
		static constexpr int MaxEscorts = 60;

		/// <summary>What it costs to take on a light and a heavy fighter, and each escort's wage a day, credits.</summary>
		static constexpr double LightHireFee = 60000, HeavyHireFee = 140000, DailyWage = 4000;

	private:
		/// <summary>Owners take on a wing only with this many times its cost in hand.</summary>
		static constexpr double AffordFactor = 4;

		cs::Ref<World> world;
		cs::Random rng;
		double nextJd = 0;
		int named = 0;

		/// <summary>Every fighter taken on here and not yet paid off (U.SPC-T288). A gate crossing takes the hull this
		/// tracks (the same object) on into the destination's own fleet (see <see cref="InterstellarTraffic"/>), so
		/// the far system's <see cref="MarkHired"/> has to pick each crossed escort back up there, or it would fly
		/// on untracked and never be paid off once its merchant is gone (U.SPC-T301).</summary>
		cs::HashSet<cs::Ref<Ship>> hired = cs::HashSet<cs::Ref<Ship>>();

	public:
		/// <summary>Whether <paramref name="s"/> is a fighter hired here and not yet paid off.</summary>
		bool IsHired(const cs::Ref<Ship>& s) const;

		/// <summary>Picks up tracking of <paramref name="s"/> as hired here: how an escort that crossed a gate with its
		/// merchant is still paid off when that merchant is gone, instead of flying on forever once the system that
		/// hired it is left behind (U.SPC-T301).</summary>
		void MarkHired(const cs::Ref<Ship>& s);

		/// <summary>Drops tracking of <paramref name="s"/> here, e.g. because it has crossed a gate into another
		/// system's fleet and is <see cref="MarkHired"/> there instead (U.SPC-T301).</summary>
		void UnmarkHired(const cs::Ref<Ship>& s);

		MerchantEscorts(cs::Ref<World> world, int seed);

		/// <summary>
		/// How many escorts a freighter carrying <paramref name="cargoValue"/> credits of goods through waters as
		/// dangerous as <paramref name="danger"/> (0 quiet to 1 a war zone, see <see cref="Contracts.DangerAt"/>) wants:
		/// 0 to 3. The cargo counts by its order of magnitude, from a hundred thousand credits up; danger counts heavily.
		/// </summary>
		static int Wanted(double cargoValue, double danger);

		/// <summary>The wing a freighter wanting <paramref name="count"/> escorts takes on: light fighters for one or two,
		/// a heavy fighter leading two light ones for three.</summary>
		static cs::List<ShipClass> Wing(int count, cs::Random& rng);

	private:
		/// <summary>The hulls a wing is made of.</summary>
		static bool IsFighter(ShipClass c);

	public:
		static double HireFee(ShipClass c);

		/// <summary>Takes wings on for the freighters in port that want them, pays every escort its wage, and pays off
		/// the wings that have outlived their merchants.</summary>
		void Update(double jd);

	private:
		/// <summary>
		/// A hired fighter whose merchant is gone (lost, scrapped, or the wing handed on to its strongest) has nobody to
		/// guard and nobody paying it: at its next port, out of any fight, it is paid off and leaves the fleet (no wreck),
		/// and so does the rest of its wing still flying with it (U.SPC-T288). Until then it flies on as it was.
		/// </summary>
		void PayOffOrphans();

		/// <summary>A merchant that takes escorts on: a lawful freighter or miner in port, not the player's (neither the
		/// hull at the helm nor one of the player's own fleet, whose escorts are the player's to order).</summary>
		bool Wants(const cs::Ref<Ship>& s) const;

		double DangerAt(int body) const;

		/// <summary>What the freighter's owners have in hand: its company's cash, or its flag's treasury.</summary>
		static double Purse(const cs::Ref<Ship>& s);

		static void Pay(const cs::Ref<Ship>& s, double credits);
	private:
		MerchantEscorts() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(MerchantEscorts); // ObjectGraph registration (Persistence/Registrations)
	};
}
