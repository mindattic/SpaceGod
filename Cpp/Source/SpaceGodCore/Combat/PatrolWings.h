#pragma once
#include "Cs/Cs.h"
#include <utility>

namespace SpaceGod::Core
{
	class World;
	class Faction;
	class Ship;
	enum class ShipClass : int;

	/// <summary>
	/// A navy's standing patrol wings (U.SPC-T371): besides its battle groups, every navy that holds ports in a system keeps
	/// a few small wings flying a beat of them, so the space between its worlds is never empty. A wing is a light wing
	/// (three fighters), a heavy-fighter wing (a gunship leading two light fighters) or a destroyer wing (a destroyer
	/// leading a heavy and two light fighters), raised in that order, so a navy with a port or two flies only light fighters
	/// and the destroyer wings are the big navies' (a navy's gate pickets are another thing, see <see cref="GatePickets"/>); each on a beat of up to <see cref="BeatLength"/> of its navy's ports, the wings spread so
	/// that between them they cover every port.
	/// <para>A wing is commanded, not loosed on whatever it meets (see <see cref="War.Commanded"/>): it defends the ports on
	/// its beat when an enemy turns up there, hunts raiders seen working them, and puts in at a yard to repair, but it never
	/// strikes out at an enemy port, is never merged into a battle group, sent through a gate on a campaign, or counted
	/// against the yards' warship cap (<see cref="Ship.PatrolWing"/>).</para>
	/// <para>A navy's corvettes fly as wings do, each stationed for good at one of its ports (U.SPC-T476, see <see cref="Corvettes"/>).</para>
	/// <para>Beside its wings a navy runs supply convoys (U.SPC-T375): one of its own freighters carrying naval stores
	/// round a run of its ports under two fighters, flown as a wing is but never turning aside to defend or hunt
	/// (<see cref="Ship.Convoy"/>).</para>
	/// </summary>
	class SPACEGODCORE_API PatrolWings
	{
	public:
		/// <summary>The most wings one navy flies in one system.</summary>
		static constexpr int MaxWingsPerFaction = 4;

		/// <summary>The most ports on one wing's beat.</summary>
		static constexpr int BeatLength = 3;

		/// <summary>One fitting: which member (0 the leader) takes which facility (C#'s (int member, string facility)).</summary>
		using Fitting = std::pair<int, cs::String>;

	private:
		/// <summary>The kinds of wing, in the order a navy raises them: light wing, heavy-fighter wing, lane picket (two
		/// destroyers, the second fitted with a field projector as the wing's lane interdictor, that lie in wait on the lanes
		/// between the ports of its beat for raiders, U.SPC-T479, U.SPC-T507), destroyer wing.</summary>
		static const cs::List<cs::List<ShipClass>>& Compositions();

		/// <summary>The heavy equipment each kind of wing fits to its hulls as it is raised (U.SPC-T507): which member (0 the
		/// leader) takes which facility. The lane picket's second destroyer carries the field projector.</summary>
		static const cs::List<cs::List<Fitting>>& Fittings();

		static const cs::List<cs::String>& CallSigns();

		static const cs::List<cs::String>& Wingmen();

	public:
		/// <summary>The most supply convoys one navy runs in one system (U.SPC-T375).</summary>
		static constexpr int MaxConvoysPerFaction = 3;

		/// <summary>The most ports on one convoy's run.</summary>
		static constexpr int RunLength = 4;

	private:
		/// <summary>A convoy's escort: two light fighters, or for every other one a heavy fighter and a light one.</summary>
		static const cs::List<cs::List<ShipClass>>& ConvoyEscorts();

		static const cs::List<cs::String>& ConvoyNames();

	public:
		/// <summary>How many supply convoys a navy holding <paramref name="ports"/> ports here runs (U.SPC-T375): one for
		/// every two ports, up to <see cref="MaxConvoysPerFaction"/>; none with fewer than two, with nowhere to run to.</summary>
		static int ConvoysFor(int ports);

		/// <summary>The escort classes of the <paramref name="index"/>th convoy a navy runs.</summary>
		static cs::List<ShipClass> ConvoyEscort(int index);

		/// <summary>How many wings a navy holding <paramref name="ports"/> ports here flies: one, plus one for every two
		/// ports, up to <see cref="MaxWingsPerFaction"/>; none with fewer than two, since a beat runs between ports and a
		/// navy's one port is its battle group's to hold.</summary>
		static int WingsFor(int ports);

		/// <summary>The classes of the <paramref name="index"/>th wing a navy raises, its leader first.</summary>
		static cs::List<ShipClass> Composition(int index);

		/// <summary>The facilities the <paramref name="index"/>th wing a navy raises fits to its hulls: which member (0 the
		/// leader, then its wingmen in <see cref="Composition"/>'s order) takes which facility kind.</summary>
		static const cs::List<Fitting>& FittingsOf(int index);

	private:
		static void Fit(const cs::Ref<Ship>& ship, int index, int member);

	public:
		/// <summary>The bodies of <paramref name="f"/>'s standing ports in <paramref name="w"/>, the most populous first.</summary>
		static cs::List<int> Ports(const cs::Ref<World>& w, const cs::Ref<Faction>& f);

		/// <summary>The beat of the <paramref name="index"/>th of <paramref name="wings"/> wings over <paramref name="ports"/>:
		/// up to <see cref="BeatLength"/> ports in a row, each wing's beat starting further round, so the wings between them
		/// cover them all.</summary>
		static cs::List<int> Beat(const cs::List<int>& ports, int index, int wings);

		/// <summary>The run of the <paramref name="index"/>th of <paramref name="convoys"/> supply convoys over
		/// <paramref name="ports"/>: up to <see cref="RunLength"/> ports in a row, each convoy's starting further round.</summary>
		static cs::List<int> ConvoyRun(const cs::List<int>& ports, int index, int convoys);

	private:
		static cs::List<int> Run(const cs::List<int>& ports, int index, int wings, int maxLength);

	public:
		/// <summary>
		/// Raises the standing patrol wings of every navy (not a pirate clan, nor a power without a navy) that holds ports in
		/// <paramref name="w"/>, each stationed at the first port of its beat. Returns the wings' leaders.
		/// </summary>
		static cs::List<cs::Ref<Ship>> Raise(const cs::Ref<World>& w, double jd);

		/// <summary>The next port on <paramref name="leader"/>'s beat after <paramref name="here"/> (its first if it is off
		/// its beat), or <paramref name="here"/> if it has none.</summary>
		static int NextOnBeat(const cs::Ref<Ship>& leader, int here);

		/// <summary>Whether <paramref name="body"/> is on <paramref name="leader"/>'s beat (its patrol route).</summary>
		static bool OnBeat(const cs::Ref<Ship>& leader, int body);
	};
}
