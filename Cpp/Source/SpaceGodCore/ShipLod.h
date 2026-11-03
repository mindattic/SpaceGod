#pragma once
// Port of Unity.SpaceGod Core/ShipLod.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class ChoiceSetting;
	class RangeSetting;
	class Ship;
	class Subsystem;

	/// <summary>
	/// How a ship is drawn by its size on screen (U.SPC-T376): its full 3D model while it is big enough to read, and
	/// below that a flat class icon in its faction's colour with its name beside it, which the player can click for
	/// its full card. The swap point and which icons are named are settings (LOD &amp; render budget), so the trade
	/// between detail and frame rate is the player's to make; the 3D view, the HUD's icons and picking all ask here,
	/// so they always agree on which form a ship is in.
	/// </summary>
	/// <remarks>C#: [SettingsSource]. Both settings are built together on first use (C#'s type initialiser), in textual order.</remarks>
	class SPACEGODCORE_API ShipLod
	{
	public:
		/// <summary>Below this many pixels long on screen a ship is drawn as its icon, not its model.</summary>
		static const cs::Ref<RangeSetting>& IconBelowPx();

		static constexpr int NamesOff = 0, NamesWarships = 1, NamesAll = 2;

		/// <summary>Which ships drawn as icons have their names beside them.</summary>
		static const cs::Ref<ChoiceSetting>& IconNames();

		/// <summary>How many pixels long a ship of <paramref name="length"/> scene units looks at <paramref name="depth"/>, given the camera's pixels per unit at unit distance.</summary>
		static double ApparentPx(double length, double depth, double pixelsPerUnit);

		/// <summary>Whether a ship <paramref name="px"/> pixels long on screen is drawn as its 3D model (else as its icon).</summary>
		static bool DrawsModel(double px);

		/// <summary>Whether a ship drawn as its model at <paramref name="px"/> pixels keeps its full detail (else its hull alone, U.SPC-T415).</summary>
		static bool DrawsFullModel(double px);

		static bool DrawsModel(double px, double iconBelowPx) { return px >= iconBelowPx; }

		/// <summary>
		/// How many times its hull's length a ship spans on screen (U.SPC-T498): an ore tug with a load latched on is the tug
		/// and the rock or ice ahead of its push plate together, so it turns from icon to model by that whole length; any other
		/// ship, 1.
		/// </summary>
		static double ExtentFactor(const cs::Ref<Ship>& ship);

		/// <summary>Whether a ship drawn as an icon is named beside it, by <see cref="IconNames"/>. An escort is named only when it is all there is to see of its group: it is folded into its leader's icon otherwise.</summary>
		static bool NamesIcon(bool military, bool escort);

		static bool NamesIcon(int mode, bool military, bool escort);

		/// <summary>
		/// A ship's weapons for its card: each kind with how many it mounts, the ones shot away counted as lost, e.g.
		/// "2 heavy laser, 4 point defence (1 lost)". "Unarmed" for a hull with none.
		/// </summary>
		static cs::String Armament(const cs::List<cs::Ref<Subsystem>>& subsystems);

	private:
		struct State;
		static State& S();
	};
}
