#pragma once
// US-T84: the metal Dyson-sphere model every Void Station (and Void Base) is drawn as.

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod::Rendering
{
	/// <summary>
	/// The model of a void station's generator (US-T84): a metal Dyson sphere built in code round the "black hole in a
	/// bottle" at its heart. A geodesic frame of girders carries partial shell plating, left open in a band about the
	/// equator and in scattered gaps, so the violet light of the accretion ring circling the dark core shows through;
	/// two gimbal rings turn slowly outside it.
	///
	/// Everything is in units of the body's radius (the body's root is scaled to its display radius), and nothing
	/// reaches past <see cref="OuterRadius"/>: in game scale a Void Base's tiny suns come in to about 1.23 radii of
	/// its centre, and the focus framing, labels and picking all take the body's radius as its size, so the model
	/// stays that size in both scales.
	/// </summary>
	class VoidStationModel
	{
	public:
		/// <summary>How far out the furthest part (the outer gimbal ring) reaches, in body radii.</summary>
		static constexpr float OuterRadius = 1.14f;

		/// <summary>The dark core (the generator's event horizon) inside the frame, in body radii.</summary>
		static constexpr float CoreRadius = 0.34f;

		/// <summary>
		/// The shell: girders, hubs and plating. <paramref name="near"/> is the close-up level of detail (every far
		/// panel split in four, twice the girders, hub nodes and seams); the far one is a few thousand vertices.
		/// </summary>
		static cs::Ref<Mesh> Shell(bool near);

		/// <summary>The glowing accretion ring about the core, in the XZ plane (axis +Y), radius about 0.55.</summary>
		static cs::Ref<Mesh> AccretionRing();

		/// <summary>One of the two gimbal rings outside the shell (0 inner, 1 outer), in the XZ plane (axis +Y).</summary>
		static cs::Ref<Mesh> Gimbal(int which);

		/// <summary>
		/// A material for one station's metal: the ship shader (sun-lit plated metal, vertex coloured), its emissive
		/// parts glowing the generator's violet. Owned by the view (destroyed with its system).
		/// </summary>
		static cs::Ref<Material> CreateMetal(const cs::String& name);
	};
}
