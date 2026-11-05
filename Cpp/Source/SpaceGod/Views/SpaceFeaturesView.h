#pragma once
// US-T130: draws a place's nebulae (layered, coloured glowing puffs) and its drawn ice fields (glittering chunks).

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// The active place's nebulae and ice fields (Core's SpaceFeatures). A nebula is drawn as layers of soft glowing puffs,
	/// each a camera-facing billboard in its own colour, piled up thickest in the nebula's knots, so it reads as a coloured
	/// volume from outside and as a haze about you from inside. An ice field is one mesh of faceted ice chunks, a few faces
	/// of each catching the light.
	///
	/// Everything is placed by the root's own mapping (the scale model's LocalOffset from the root), as a ship in open
	/// space is: a puff's size is its true size scaled the way the mapping scales distances where it sits, and the chunks
	/// are laid out again when the scale blend moves, so the clouds hold the ships inside them at both scales.
	/// </summary>
	class SpaceFeaturesView final
	{
		struct Puff
		{
			Core::DVec3 OffsetKm;
			double RadiusKm = 1;
			cs::Ref<Transform> Quad;
		};

		struct Field
		{
			Core::DVec3 CentreKm;
			cs::Ref<Mesh> Shape;
			cs::Ref<Transform> Root;
			int Index = 0;
		};

		UnitySpaceApp* app;
		cs::List<Puff> puffs;
		cs::List<Field> fields;
		double laidOutBlend = cs::Double::NaN;

		void LayField(Field& f);

	public:
		SpaceFeaturesView(UnitySpaceApp* app, const cs::Ref<Transform>& parent);

		void Update();
	};
}
