#pragma once
// US-T146: draws the gas giants' works: the skimmer platforms in low orbit, the skimmers diving into the cloud tops with
// a plume of scooped gas between, and the tankers shuttling the gas out.

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class HarvestOperation;
	class HarvestTanker;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// The active system's gas works (Core's GasHarvest, US-T146). Each skimmer platform is a tank farm (Space Station
	/// Creator's storage complex when the pack is imported, else the procedural truss of fuel spheres round a station hub)
	/// in low orbit, long axis along its orbit; below it a plume of glowing gas runs from the platform down to the cloud
	/// tops, where an intake flare marks the skimmer's dip, and the skimmer itself climbs and dives along it. The plume
	/// spans the true gap from the platform to the clouds through the scale model's mapping, so it reads as a bright
	/// thread over the planet from far off at the realistic scale and as a short bright column at game scale. The skimmer
	/// and the tankers are the purchased Ultimate Spaceships Creator pack's courier and freighter hulls as they come
	/// (UscKit), procedural hulls only without the pack. The
	/// platform, skimmer and tankers are sized at ship scale (ScaleModel.ShipLength) as stations and hulls are. Tankers
	/// lie astern of their platform waiting for a load and fly out to their port and back, engines lit. A platform knocked
	/// out by a raid goes dark: no plume, no skimmer, its lights out, a fire flickering on it.
	/// </summary>
	class HarvesterView final
	{
		struct Platform
		{
			cs::Ref<Core::HarvestOperation> Op;
			int Index = 0;
			cs::Ref<Transform> Root, Plume, Intake, Skimmer, SkimmerGlow, Beacon, Fire;
			cs::Ref<Material> PlumeMaterial, IntakeMaterial;
			float Phase = 0;
		};

		struct Tanker
		{
			cs::Ref<Core::HarvestOperation> Op;
			cs::Ref<Core::HarvestTanker> T;
			cs::Ref<Transform> Root, Engine;
		};

		UnitySpaceApp* app;
		cs::Ref<Transform> parent;
		cs::List<Platform> platforms;
		cs::List<Tanker> tankers;
		bool built = false;

		void Build();

	public:
		HarvesterView(UnitySpaceApp* app, const cs::Ref<Transform>& parent);

		void Update();

		/// <summary>Where a platform is drawn now (scene units): for the HUD's markers.</summary>
		static Core::DVec3 PlatformScene(UnitySpaceApp* app, const cs::Ref<Core::HarvestOperation>& op, int platform);

		/// <summary>The drawn size of a platform (scene units).</summary>
		static double PlatformSize(UnitySpaceApp* app, const cs::Ref<Core::HarvestOperation>& op, int platform);
	};
}
