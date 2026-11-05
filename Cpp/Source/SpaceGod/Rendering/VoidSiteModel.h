#pragma once
// US-T123: the models of the places about a void station (Core's VoidSites): its drift's rocks, its broken planetoid and
// the chunks and debris round it, and its sights.

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod::Core
{
	struct VoidSite;
}

namespace SpaceGod::Rendering
{
	/// <summary>
	/// Builds a void site's model in code, from the site alone (its shape seed), so it is the same every time.
	///
	/// Like the void station's own model, everything is in units of the body's radius: the body's root is scaled to its
	/// display radius, so the model is the body's size at either scale and the focus framing, labels and picking (which
	/// take the body's radius as its size) fit it. Nothing but a sight's glow reaches past 1.2 radii.
	///
	/// The debris drawn round a site (the shell of rubble about a broken planetoid, the boulders about a drift rock) is
	/// laid out again whenever the scale blend moves (<see cref="LayDebris"/>): each piece is placed by the scale model's
	/// own local mapping and sized as a body that size would be drawn, so it sits where a moon that far out would and stays
	/// clear of the body and of its chunks (which Core placed against the same mapping) in both scales.
	/// </summary>
	class VoidSiteModel
	{
	public:
		/// <summary>The site's own model, one radius across: a faceted rock, a broken chunk, a cracked or gutted core, or a sight.</summary>
		static cs::Ref<Mesh> Body(const Core::VoidSite& site);

		/// <summary>A material for it: the ship shader, vertex coloured, its emissive faces (cracks, crystals, lamps) glowing <see cref="GlowColor"/>.</summary>
		static cs::Ref<Material> CreateMaterial(const Core::VoidSite& site, const cs::String& name);

		/// <summary>The colour its emissive faces glow, and a halo drawn round it if <paramref name="halo"/> comes back true.</summary>
		static Color GlowColor(const Core::VoidSite& site, bool& halo);

		/// <summary>One piece of drawn debris: its direction from the body, true distance and size (body radii), and its shape.</summary>
		struct Fragment
		{
			Vector3 Direction;
			float Radii = 2;
			float Size = 0.01f;
			Vector3 Corners[6];
			Color Tint;
		};

		/// <summary>The debris drawn round a site: a planetoid's shell, a drift rock's boulders; empty for anything else.</summary>
		static cs::List<Fragment> Debris(const Core::VoidSite& site);

		/// <summary>Lays the debris out in <paramref name="mesh"/> (body radii) for a scale blend (0 realistic, 1 game).</summary>
		static void LayDebris(const cs::Ref<Mesh>& mesh, const cs::List<Fragment>& fragments, double blend);
	};
}
