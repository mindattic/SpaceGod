#pragma once
// Port of Unity.SpaceGod Core/RenderLod.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

#include <mutex>

namespace SpaceGod::Core
{
	class Fleet;
	class RangeSetting;

	/// <summary>
	/// How a renderable thing is drawn by its size on screen (U.SPC-T415), from nearest to farthest: <see cref="Full"/>
	/// model, <see cref="Simplified"/> model (the hull alone, without turrets, scars, glows, shields or exhaust),
	/// <see cref="Icon"/> (a flat faction icon, or a glow impostor for small craft), <see cref="Aggregate"/> (folded with
	/// its neighbours into one mark that stands for the lot: a fleet, a crowd of traffic, or a whole other star system),
	/// and <see cref="Culled"/> (not drawn at all).
	/// </summary>
	enum class LodTier : int
	{
		Full,
		Simplified,
		Icon,
		Aggregate,
		Culled,
	};
	SPACEGODCORE_API const char* ToString(LodTier v);

	/// <summary>What a class of thing becomes once it is too small to draw as a model.</summary>
	enum class LodFar : int
	{
		/// <summary>Nothing: below its model it is culled (a structure, a docking ring, a wreck).</summary>
		None,

		/// <summary>A glow or sprite a few pixels wide while the thing itself is still at least the cull size (a fighter, a pod's beacon).</summary>
		Impostor,

		/// <summary>An icon however small it gets, folded into a mark with its neighbours when they crowd (ships, worlds).</summary>
		Icon,
	};
	SPACEGODCORE_API const char* ToString(LodFar v);

	/// <summary>
	/// One class of renderable thing and how it degrades: whether it has a simpler model, the apparent size below which
	/// its model gives way to its far form, and what that far form is. New renderable things register one with
	/// <see cref="RenderLod.Register"/> instead of drawing in full at every size.
	/// </summary>
	class SPACEGODCORE_API LodClass final
	{
	public:
		cs::String Key, Name;
		LodFar Far{};

		/// <summary>Whether it has a simplified model between its full model and its far form.</summary>
		bool Simplifies = false;

	private:
		cs::Func<double> modelBelowPx;

	public:
		LodClass(const cs::String& key, const cs::String& name, cs::Func<double> modelBelowPx, LodFar far, bool simplifies);

		/// <summary>Below this many pixels on screen the model gives way to its far form (or to nothing).</summary>
		double ModelBelowPx() const { return modelBelowPx(); }

		cs::String ToString() const { return Name; }
	};

	/// <summary>One icon on screen, for <see cref="RenderLod.Aggregate"/>.</summary>
	struct LodMark
	{
		/// <summary>Its place on screen, pixels.</summary>
		double X = 0, Y = 0;

		/// <summary>Its side: only icons of one group fold together.</summary>
		int Group = 0;

		/// <summary>How much it should stand for the others (the biggest hull leads).</summary>
		double Weight = 0;

		/// <summary>Kept apart: never folded into another mark, never folds one in.</summary>
		bool Pinned = false;

		LodMark() = default;
		LodMark(double x, double y, int group, double weight, bool pinned = false)
		{
			X = x;
			Y = y;
			Group = group;
			Weight = weight;
			Pinned = pinned;
		}
	};

	/// <summary>One mark drawn for one or more icons (see <see cref="RenderLod.Aggregate"/>).</summary>
	class SPACEGODCORE_API LodCluster final
	{
	public:
		/// <summary>Index of the icon that stands for the mark.</summary>
		int Representative = 0;

		/// <summary>Where the mark sits on screen: its representative's place.</summary>
		double X = 0, Y = 0;

		/// <summary>Every icon it stands for, by index, its representative first.</summary>
		cs::List<int> Members;

		int Count() const { return Members.Count(); }

		/// <summary>The tier its icons are drawn in: <see cref="LodTier.Icon"/> for one alone, <see cref="LodTier.Aggregate"/> for a fold.</summary>
		LodTier Tier() const { return Members.Count() > 1 ? LodTier::Aggregate : LodTier::Icon; }

		// C#: internal (public here so cs::New can reach it).
		LodCluster(int index, LodMark m);
	};

	/// <summary>
	/// The render budget at universe scale (U.SPC-T415): every renderable class (ships, worlds, gates, structures,
	/// docking rings, defence platforms, fighters, escape pods, wrecks, and any a new system registers) asks here which
	/// <see cref="LodTier"/> it is drawn in for its apparent size, so the 3D view, the HUD's icons and picking always
	/// agree. Icons that crowd on screen are folded into one mark by <see cref="Aggregate"/>, and another star system is
	/// one mark in the sky. The swap points are settings (LOD &amp; render budget), so the trade between detail and frame
	/// rate is the player's to make; a ship's own model-to-icon point stays <see cref="ShipLod.IconBelowPx"/>.
	/// </summary>
	/// <remarks>
	/// C#: [SettingsSource]. Its static fields are initialised together, in textual order, the first time any member is
	/// touched (C#'s type initialiser); here they live in one function-local State built the same way, and every
	/// static field is reached through an accessor of the same name.
	/// </remarks>
	class SPACEGODCORE_API RenderLod
	{
	public:
		/// <summary>Below this many pixels long a model drops its detail for its simplified form.</summary>
		static const cs::Ref<RangeSetting>& SimplifyBelowPx();

		/// <summary>Below this many pixels a thing with no icon is not drawn at all.</summary>
		static const cs::Ref<RangeSetting>& CullBelowPx();

		/// <summary>Icons of one side closer than this on screen fold into one mark with a count.</summary>
		static const cs::Ref<RangeSetting>& AggregatePx();

		static const cs::Ref<LodClass>& Ship();
		static const cs::Ref<LodClass>& Body();
		static const cs::Ref<LodClass>& Gate();
		static const cs::Ref<LodClass>& Structure();
		static const cs::Ref<LodClass>& DockingRing();
		static const cs::Ref<LodClass>& Platform();
		static const cs::Ref<LodClass>& Fighter();
		static const cs::Ref<LodClass>& Pod();
		static const cs::Ref<LodClass>& Wreck();

		/// <summary>Every registered render class, in the order registered.</summary>
		static const cs::List<cs::Ref<LodClass>>& Classes();

		/// <summary>Adds a render class (a new kind of renderable thing joins the scheme here); a key already taken is refused.</summary>
		static cs::Ref<LodClass> Register(cs::Ref<LodClass> c);

		static cs::Ref<LodClass> Find(const cs::String& key);

		/// <summary>How big <paramref name="length"/> scene units look at <paramref name="depth"/>, given the camera's pixels per unit at unit distance.</summary>
		static double ApparentPx(double length, double depth, double pixelsPerUnit)
		{
			return length / cs::Math::Max(depth, 1e-9) * pixelsPerUnit;
		}

		/// <summary>The tier a thing of class <paramref name="c"/> is drawn in at <paramref name="px"/> pixels, by the current settings.</summary>
		static LodTier Tier(const cs::Ref<LodClass>& c, double px);

		/// <summary>
		/// The tier for <paramref name="px"/> pixels: its model at or above the model swap point (and never below the cull
		/// size), simplified under <paramref name="simplifyBelowPx"/> if it has a simpler form; below that its icon,
		/// its impostor while it is still at least <paramref name="cullBelowPx"/>, or nothing. <see cref="LodTier.Aggregate"/>
		/// is never returned here: icons are folded into marks by <see cref="Aggregate"/>, where they crowd.
		/// </summary>
		static LodTier Tier(double px, double modelBelowPx, LodFar far, bool simplifies, double simplifyBelowPx, double cullBelowPx);

		/// <summary>Whether the tier draws a 3D model.</summary>
		static bool DrawsModel(LodTier t) { return t == LodTier::Full || t == LodTier::Simplified; }

		/// <summary>
		/// Folds icons that crowd on screen into marks (U.SPC-T415). Icons of one <see cref="LodMark.Group"/> (a side)
		/// within <paramref name="radiusPx"/> of a mark's place fold into it; the heaviest icon (the biggest hull)
		/// stands for each mark and the mark sits where it is. A pinned icon (selected, hovered, followed, flown) is
		/// always its own mark and folds nothing. Runs in about linear time on a grid, so thousands of icons cost little.
		/// Each mark's <see cref="LodCluster.Members"/> lists its icons by index into <paramref name="marks"/>, its
		/// representative first.
		/// </summary>
		static cs::List<cs::Ref<LodCluster>> Aggregate(const cs::List<LodMark>& marks, double radiusPx);

		/// <summary>How a folded mark is labelled with the count it stands for: "" for one, "×12" for twelve.</summary>
		static cs::String CountLabel(int count) { return count > 1 ? cs::Cat("×", count) : cs::String(); }

		/// <summary>
		/// How another star system's one mark in the sky is labelled (U.SPC-T415): every ship in it is folded into that
		/// mark, so it says how many there are; "" when there are none.
		/// </summary>
		static cs::String SystemShipsLabel(int ships) { return ships <= 0 ? cs::String() : ships == 1 ? cs::String("1 ship") : cs::Cat(ships, " ships"); }

		/// <summary>The ships still flying in <paramref name="fleet"/> (not destroyed, not gone through a gate), for a system's mark.</summary>
		static int LiveShips(const cs::Ref<Fleet>& fleet);

	private:
		struct State;
		static State& S();
		static void Add(State& s, const cs::Ref<LodClass>& c);
	};
}
