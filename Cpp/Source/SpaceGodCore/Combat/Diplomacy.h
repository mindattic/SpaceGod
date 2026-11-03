#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Combat/Diplomacy.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class Faction;
	class Settlement;

	enum class Relation : int
	{
		Peace,
		Ceasefire,
		War,
	};
	SPACEGODCORE_API const char* ToString(Relation v);

	/// <summary>One faction closing its ports to another's ships (see <see cref="Diplomacy.DeclareEmbargo"/>).</summary>
	class SPACEGODCORE_API Embargo final
	{
	public:
		cs::Ref<Faction> By, Against;
		double SinceJd = 0, UntilJd = 0;

		/// <summary>Why it was declared, for the news ("running munitions to the Outer Reach Compact").</summary>
		cs::String Reason;
	};

	/// <summary>
	/// The peace that ends an interstellar war (U.SPC-T92): who signed it, who came out ahead, the systems the loser
	/// cedes (every settlement and gate it held there goes to the winner; whatever else either side had occupied goes
	/// back), and how long the two keep the peace before either may go to war with the other again.
	/// </summary>
	class SPACEGODCORE_API Treaty final
	{
	public:
		cs::Ref<Faction> A, B;

		/// <summary>The side that gains systems by it; null for a white peace that cedes nothing.</summary>
		cs::Ref<Faction> Winner;

		double Jd = 0;

		/// <summary>No new war between the two before this date.</summary>
		double TruceUntilJd = 0;

		/// <summary>The systems ceded to the <see cref="Winner"/>, by name.</summary>
		cs::List<cs::String> Ceded = cs::List<cs::String>();

		/// <summary>The terms, for the news.</summary>
		cs::String Terms;

		cs::Ref<Faction> Loser() const;

		bool Between(const cs::Ref<Faction>& x, const cs::Ref<Faction>& y) const;
	};

	/// <summary>
	/// Who is at war with whom. Pirates are hostile to everyone and everyone to them. Besides war and
	/// peace a faction can declare an embargo (its ports refuse another faction's ships) and a besieger
	/// can declare a blockade of a port (nobody docks there until it is lifted).
	/// </summary>
	class SPACEGODCORE_API Diplomacy final
	{
		using FactionPair = std::pair<cs::Ref<Faction>, cs::Ref<Faction>>;
		// Cs.h has no Hash for a tuple key: hash the two references, as C#'s ValueTuple hashes its items.
		struct SPACEGODCORE_API FactionPairHash
		{
			size_t operator()(const FactionPair& k) const
			{
				return cs::HashCombine(cs::Hash<cs::Ref<Faction>>{}(k.first), cs::Hash<cs::Ref<Faction>>{}(k.second));
			}
		};

		cs::Dictionary<FactionPair, Relation, FactionPairHash> relations = cs::Dictionary<FactionPair, Relation, FactionPairHash>();
		cs::List<cs::Ref<Embargo>> embargoes = cs::List<cs::Ref<Embargo>>();
		cs::Dictionary<cs::Ref<Settlement>, cs::Ref<Faction>> blockades = cs::Dictionary<cs::Ref<Settlement>, cs::Ref<Faction>>();

		static FactionPair Key(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b);

	public:
		Relation Get(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) const;

		void Set(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, Relation r);

		// ---------------------------------------------------------------- treaties

	private:
		cs::List<cs::Ref<Treaty>> treaties = cs::List<cs::Ref<Treaty>>();

	public:
		/// <summary>Every peace signed so far, oldest first (U.SPC-T92).</summary>
		const cs::List<cs::Ref<Treaty>>& Treaties() const;

		/// <summary>
		/// Ends the war between the treaty's two sides (U.SPC-T92): they are at peace from <see cref="Treaty.Jd"/>, and the
		/// treaty goes on the record. Handing over what it cedes is for whoever holds the systems (see <see cref="InterstellarWars"/>).
		/// </summary>
		cs::Ref<Treaty> Sign(const cs::Ref<Treaty>& t);

		/// <summary>Whether a treaty still binds <paramref name="a"/> and <paramref name="b"/> to keep the peace at <paramref name="jd"/>.</summary>
		bool InTruce(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b, double jd) const;

		bool Hostile(const cs::Ref<Faction>& a, const cs::Ref<Faction>& b) const;

		/// <summary>A ship of <paramref name="ship"/> may trade at a port owned by <paramref name="port"/>.</summary>
		bool CanDock(const cs::Ref<Faction>& ship, const cs::Ref<Faction>& port) const;

		/// <summary>A ship of <paramref name="ship"/> may trade at <paramref name="port"/>: its owner lets it in and no blockade shuts it.</summary>
		bool CanDockAt(const cs::Ref<Faction>& ship, const cs::Ref<Settlement>& port) const;

		// ---------------------------------------------------------------- embargoes

		const cs::List<cs::Ref<Embargo>>& Embargoes() const;

		/// <summary>Whether <paramref name="by"/>'s ports are closed to <paramref name="against"/>'s ships.</summary>
		bool IsEmbargoed(const cs::Ref<Faction>& by, const cs::Ref<Faction>& against) const;

		/// <summary>
		/// <paramref name="by"/> closes its ports to <paramref name="against"/> until <paramref name="untilJd"/>
		/// (or until lifted). Returns the embargo, or null if one is already in force.
		/// </summary>
		cs::Ref<Embargo> DeclareEmbargo(const cs::Ref<Faction>& by, const cs::Ref<Faction>& against, double jd, double untilJd, const cs::String& reason);

		bool LiftEmbargo(const cs::Ref<Embargo>& e);

		// ---------------------------------------------------------------- blockades

		const cs::Dictionary<cs::Ref<Settlement>, cs::Ref<Faction>>& Blockades() const;

		/// <summary>The faction blockading <paramref name="port"/>, or null.</summary>
		cs::Ref<Faction> BlockadedBy(const cs::Ref<Settlement>& port) const;

		/// <summary><paramref name="by"/> declares <paramref name="port"/> blockaded: no ship docks there. False if it already was.</summary>
		bool DeclareBlockade(const cs::Ref<Settlement>& port, const cs::Ref<Faction>& by);

		bool LiftBlockade(const cs::Ref<Settlement>& port);
	};
}
