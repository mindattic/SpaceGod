#pragma once
// US-T447: every number of the world's effects in one place: exhaust clouds, boost puffs, RCS vapour, hull breaches venting,
// the sparks, debris and lingering smoke of a fight, explosions' shockwaves, and the flashes of a gate transit and a drive
// engaging. Views::WorldFx draws them all (and BattleFx adds its part through it). View only: nothing here touches the
// simulation, so co-op peers and the bench never see it.

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod::Views
{
	struct EffectsTune
	{
		// ---- budgets: past these the oldest go first (performance)
		/// <summary>The most clouds (exhaust, vapour, smoke) drawn at once, and the most sparks, embers and flashes.</summary>
		static constexpr int MaxClouds = 5000, MaxSparks = 3000, MaxFlashes = 400;
		/// <summary>The most clouds one hull may keep alive at once (so one capital cannot take the whole budget).</summary>
		static constexpr int MaxCloudsPerEmitter = 260;
		/// <summary>A hull smaller than this on screen (px of its length) throws no clouds; at FullPx it throws its full rate.</summary>
		static constexpr float EmitMinPx = 7.0f, EmitFullPx = 220.0f;
		/// <summary>Below this share of the full rate the clouds are fewer but bigger (a far hull's trail stays a trail).</summary>
		static constexpr float FarRateShare = 0.18f, FarSizeBoost = 1.9f;
		/// <summary>A hull moving more than this many of its lengths in a frame jumped (a rebuilt view, a time warp): its trail restarts.</summary>
		static constexpr float JumpLengths = 6.0f;
		/// <summary>Faster than this many lengths a second (high time warp) a hull leaves no clouds: they would be strung out, not a trail.</summary>
		static constexpr float MaxEmitLengthsPerS = 60.0f;

		// ---- exhaust clouds (main engines), in hull lengths and real seconds. Light = a fighter, heavy = a freighter or a capital.
		/// <summary>Clouds per second per nozzle at full throttle, light and heavy.</summary>
		static constexpr float ExhaustRateLight = 26.0f, ExhaustRateHeavy = 16.0f;
		/// <summary>How long a cloud lives, light and heavy.</summary>
		static constexpr float ExhaustLifeLight = 0.9f, ExhaustLifeHeavy = 3.0f;
		/// <summary>A cloud's size at birth, a share of its nozzle's width, and how many times that it grows to.</summary>
		static constexpr float ExhaustStartWidths = 0.85f, ExhaustGrowLight = 2.6f, ExhaustGrowHeavy = 3.6f;
		/// <summary>How fast a cloud leaves its nozzle (hull lengths a second), light and heavy, and how fast it slows (1/s).</summary>
		static constexpr float ExhaustSpeedLight = 4.0f, ExhaustSpeedHeavy = 1.7f, ExhaustDrag = 0.9f;
		/// <summary>How much of the hull's own motion a cloud keeps (the rest is left behind: the trail).</summary>
		static constexpr float ExhaustInherit = 0.12f;
		/// <summary>Sideways scatter at birth, a share of the exhaust speed (the billow).</summary>
		static constexpr float ExhaustScatter = 0.12f;
		/// <summary>How dense a cloud is at birth, light and heavy (opacity 0..1).</summary>
		static constexpr float ExhaustOpacityLight = 0.16f, ExhaustOpacityHeavy = 0.3f;
		/// <summary>How hot a cloud is at birth (the drive's colour glowing out of it) and how fast it cools (1/s).</summary>
		static constexpr float ExhaustHeat = 2.4f, ExhaustCoolPerS = 2.2f;
		/// <summary>How bright the cloud's own body is (0..1 albedo): pale vapour.</summary>
		static constexpr float ExhaustAlbedo = 0.75f;
		/// <summary>The drive's colour (the main jets' flame), and the vapour it cools into.</summary>
		static constexpr Color DriveColor{0.45f, 0.75f, 1.0f, 1.0f};
		static constexpr Color VapourColor{0.78f, 0.84f, 0.92f, 1.0f};

		// ---- boost / afterburner: the burn opened up past cruise
		/// <summary>The exhaust's rate, size, heat and speed while boosting, times the plain burn's.</summary>
		static constexpr float BoostRate = 2.0f, BoostSize = 1.35f, BoostHeat = 1.9f, BoostSpeed = 1.6f;
		/// <summary>The kick as the boost opens: a ring of puffs round the nozzles and a flash.</summary>
		static constexpr int BoostRingPuffs = 10;
		static constexpr float BoostFlashLengths = 0.3f, BoostFlashS = 0.3f;
		static constexpr Color BoostColor{0.75f, 0.88f, 1.0f, 1.0f};
		/// <summary>A boost read from the charge falling: less than this per second of the charge is noise.</summary>
		static constexpr float BoostDrainPerS = 0.004f, BoostHoldS = 0.35f;

		// ---- RCS vapour: a small cold puff from each jet as it fires
		static constexpr float RcsRate = 14.0f, RcsLife = 0.7f, RcsStartWidths = 1.2f, RcsGrow = 2.6f, RcsSpeed = 0.9f;
		static constexpr float RcsOpacity = 0.22f, RcsAlbedo = 0.95f;
		/// <summary>A jet lit past this level puffs.</summary>
		static constexpr float RcsLitLevel = 0.2f;

		// ---- a damaged hull venting from its breaches (gas, with a little fire in it)
		static constexpr float VentRate = 7.0f, VentLife = 1.7f, VentStartLengths = 0.02f, VentGrow = 5.0f, VentSpeed = 0.28f;
		static constexpr float VentOpacity = 0.4f, VentAlbedo = 0.8f, VentFireHeat = 0.9f;
		static constexpr Color VentColor{0.86f, 0.9f, 0.96f, 1.0f};
		static constexpr Color FireColor{1.0f, 0.5f, 0.18f, 1.0f};
		/// <summary>A wreck smokes darker and slower.</summary>
		static constexpr float WreckSmokeAlbedo = 0.32f, WreckSmokeLife = 3.2f;

		// ---- a fight: a hit on the hull, a part shot out, a hull lost
		/// <summary>Sparks off a hit: how many (a heavier hit more), how fast (hull lengths a second), how long.</summary>
		static constexpr int ImpactSparksMin = 5, ImpactSparksMax = 14;
		static constexpr float ImpactSparkSpeed = 0.9f, ImpactSparkLife = 0.55f, ImpactSparkDrag = 2.5f;
		/// <summary>Bits of plating knocked off a hit, tumbling clear (dim specks).</summary>
		static constexpr int ImpactDebris = 4;
		static constexpr float ImpactDebrisLife = 1.6f;
		/// <summary>The puff of smoke a hit leaves on the hull.</summary>
		static constexpr float ImpactSmokeLengths = 0.05f, ImpactSmokeLife = 1.4f;
		static constexpr Color SparkColor{1.0f, 0.78f, 0.42f, 1.0f};

		/// <summary>A hull blown apart: its shockwave, sparks, glowing embers and the debris cloud left hanging.</summary>
		static constexpr float ShockLengths = 3.0f, ShockS = 0.75f, ShockSecondS = 1.4f;
		static constexpr int BlastSparks = 34, BlastEmbers = 28, BlastClouds = 14;
		static constexpr float BlastSparkSpeed = 1.6f, BlastSparkLife = 1.3f;
		static constexpr float EmberSpeed = 0.35f, EmberLife = 6.5f, EmberDrag = 0.35f;
		static constexpr float BlastCloudLengths = 0.45f, BlastCloudGrow = 4.2f, BlastCloudLife = 8.0f, BlastCloudSpeed = 0.28f;
		static constexpr float BlastCloudHeat = 2.2f, BlastCloudCoolPerS = 0.55f, BlastCloudAlbedo = 0.3f, BlastCloudOpacity = 0.6f;
		static constexpr Color BlastColor{1.0f, 0.6f, 0.25f, 1.0f};
		static constexpr Color ShockColor{1.0f, 0.86f, 0.62f, 1.0f};
		/// <summary>A part shot out: a smaller blast (shares of the hull's).</summary>
		static constexpr float SubsystemBlastShare = 0.35f;

		/// <summary>A boarding pod's breach: a jet of air streaming out of the hole for this long.</summary>
		static constexpr float BreachJetS = 1.6f;
		static constexpr int BreachJetPuffs = 18;

		// ---- drives and gates
		/// <summary>A gate transit: the hull drawn into a fold of light (a ring closing on it, then a flash and a streak on).</summary>
		static constexpr float GateFoldS = 0.55f, GateFlashS = 0.6f, GateRingLengths = 3.0f;
		static constexpr Color GateColor{0.72f, 0.58f, 1.0f, 1.0f};
		static constexpr Color GateCore{0.9f, 0.92f, 1.0f, 1.0f};
		/// <summary>A hull coming through a gate is read from where it first shows: within this many gate diameters of one.</summary>
		static constexpr double GateArrivalDiameters = 3.0;
		/// <summary>A gate transit is drawn at least this share of the gate's own size (a hull is small against its ring).</summary>
		static constexpr float GateScaleShare = 0.45f;
		/// <summary>The cruise drive engaging: a shock ring off the stern and a burst of exhaust.</summary>
		static constexpr float CruiseRingLengths = 2.2f, CruiseRingS = 0.7f;
		static constexpr Color CruiseColor{0.6f, 0.85f, 1.0f, 1.0f};
		/// <summary>The FTL drive: the spool's flash, the jump's ring and its streak, and the arrival's flash.</summary>
		static constexpr float FtlFlashLengths = 1.3f, FtlFlashS = 0.55f, FtlStreakLengths = 14.0f;
		static constexpr Color FtlColor{0.7f, 0.88f, 1.0f, 1.0f};

		// ---- comfort (PlayerOptions: Reduce flashes and shake; reduced motion)
		/// <summary>Reduced motion: this share of the sparks and embers, and no shockwave rings sweeping the view.</summary>
		static constexpr float CalmSparkShare = 0.4f;
	};
}
