#pragma once
// US-T131: draws a place's ship graveyards: the haze they hang in, the dense wreckage round the hulks, and derelict stations.

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// The active place's graveyards (Core's Graveyards). The hulks themselves are real wrecks, drawn as any battle's are
	/// (broken ship models); this view draws what is round them:
	///  - the haze: soft glowing puffs of dust catching the light, warm orange near a hot star, icy blue-white by a cool or
	///    far one;
	///  - the wreckage: one mesh of thousands of dark chunks and container-sized boxes, tumbling slowly, a few faces still
	///    glowing (warm) or frosted bright (cold);
	///  - the derelict stations: the station model, darkened and lightless, a module broken off and drifting beside it;
	///    or, at Wolf 359 (US-T85), a cube skinned in a lattice of raised blocks and recesses, a few still faintly lit green.
	/// Everything is placed by the root's mapping (as the wrecks are) and sized at ship scale (ScaleModel.ShipLength, as the
	/// hulks are), and laid out again when the scale blend moves, so it reads with the hulks at both scales.
	/// </summary>
	class GraveyardView final
	{
		struct Puff
		{
			Core::DVec3 OffsetKm;
			double RadiusKm = 1;
			cs::Ref<Transform> Quad;
		};

		struct Field
		{
			int Index = 0;
			Core::DVec3 CentreKm;
			cs::Ref<Mesh> Shape;
			cs::Ref<Transform> Root;
		};

		struct Station
		{
			Core::DVec3 OffsetKm;
			double SizeKm = 1;
			float Spin = 1;
			/// <summary>US-T85: Wolf 359's cube: no module torn off, and it turns slowly instead of tumbling.</summary>
			bool Cube = false;
			cs::Ref<Transform> Body, Module;
		};

		UnitySpaceApp* app;
		cs::List<Puff> puffs;
		cs::List<Field> fields;
		cs::List<Station> stations;
		double laidOutBlend = cs::Double::NaN;

		void LayField(Field& f);

	public:
		GraveyardView(UnitySpaceApp* app, const cs::Ref<Transform>& parent);

		void Update();
	};
}
