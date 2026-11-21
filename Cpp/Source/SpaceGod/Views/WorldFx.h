#pragma once
// US-T447: the world's effects beyond the guns' own fire (BattleFx): engine exhaust as clouds that billow, trail and thin
// out behind every thrusting hull (world space, so a turn leaves a curving trail), boost puffs, RCS vapour, damaged hulls
// and wrecks venting from their breaches, and for a fight the sparks, debris, shockwaves, embers and lingering smoke of
// hits and kills; the flashes of a gate transit and of a drive engaging. Every number is in EffectsTune.
//
// Two dynamic meshes of camera-facing quads: soft lit clouds (M_Smoke: translucent, lit by the system's star, billowing,
// with a hot core in the drive's colour) drawn back to front, and additive sparks, embers and flashes (M_Fx's shapes).
// Every particle moves on a closed form of its age (no per-frame integration), so the hold rewind's effect clock running
// backward plays them backward too (UnitySpaceApp::HoldRewindFxClock / HoldRewindFxEnded). View only: positions live in
// scene coordinates, nothing reads or writes the simulation, so a co-op peer draws its own and the bench never sees them.

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Views
{
	/// <summary>One nozzle or jet as an emitter sees it this frame: in scene units, its level 0..1.</summary>
	struct FxJet
	{
		Core::DVec3 At;
		/// <summary>The way its exhaust leaves (unit, scene axes).</summary>
		Vector3 Exhaust;
		/// <summary>Its nozzle's width, scene units.</summary>
		float Width = 0;
		float Level = 0;
	};

	/// <summary>A hull's emitters this frame, as the app reads them off its view (or a craft's, off the battle).</summary>
	struct FxHull
	{
		/// <summary>Who it is (one emitter's state per key: the ship, the battle unit).</summary>
		const void* Key = nullptr;
		Core::DVec3 Scene;
		/// <summary>Its drawn length (scene units) and its apparent length on screen (px).</summary>
		float Length = 0, Px = 0;
		/// <summary>0 a fighter, 1 a freighter or a capital: how thick and slow its exhaust is.</summary>
		float Heft = 0.5f;
		/// <summary>Its boost charge (0..1), or a negative for none: a boost is read from it falling.</summary>
		float BoostCharge = -1;
		cs::List<FxJet> Mains, Rcs;
		/// <summary>Breaches venting (scene positions), and the way out from each.</summary>
		cs::List<Core::DVec3> Vents;
		cs::List<Vector3> VentOut;
		bool Wreck = false;
	};

	class WorldFx final
	{
	public:
		WorldFx(UnitySpaceApp* app, const cs::Ref<Transform>& parent);

		/// <summary>The frame's clock: call once a frame before any emitter, then the emitters, then <see cref="Draw"/>.</summary>
		void Begin(float dt);

		/// <summary>One hull's exhaust, boost, RCS vapour and venting this frame.</summary>
		void Emit(const FxHull& h);

		/// <summary>Draws everything and forgets emitters not seen for a while.</summary>
		void Draw();

		/// <summary>Whether the effects run backward this frame (a hold rewind): nothing new is emitted then.</summary>
		bool Rewinding() const { return rewinding; }

		// ---- one-shot effects (BattleFx and the app call these; scene positions, unit directions in scene axes)
		/// <summary>A hit on a hull: sparks spraying out along <paramref name="out"/>, specks of plating, a puff of smoke.</summary>
		void Impact(Core::DVec3 at, Vector3 out, float hullLength, float weight);
		/// <summary>A blast (a hull lost, or a part of it shot out at <paramref name="share"/> of a hull's): a shockwave, sparks,
		/// glowing embers drifting off and a debris cloud left hanging, glowing as it cools.</summary>
		void Blast(Core::DVec3 at, float hullLength, float share);
		/// <summary>Air streaming out of a hole for a moment (a boarding pod's breach).</summary>
		void BreachJet(Core::DVec3 at, Vector3 out, float hullLength);
		/// <summary>A hull folding into a gate (leaving) or out of one (arriving), heading along <paramref name="forward"/>.</summary>
		void GateTransit(Core::DVec3 at, Vector3 forward, float hullLength, bool arriving);
		/// <summary>The cruise drive engaging: a shock ring off the stern and a burst of exhaust.</summary>
		void CruiseEngage(Core::DVec3 at, Vector3 forward, float hullLength);
		/// <summary>The FTL drive: jumping out (a flash, a ring and a streak on along the heading) or arriving (a flash, a ring).</summary>
		void FtlJump(Core::DVec3 at, Vector3 forward, float hullLength, bool arriving);

		/// <summary>Counts for the console command's log.</summary>
		int Clouds() const { return static_cast<int>(clouds.size()); }
		int Sparks() const { return static_cast<int>(sparks.size()); }
		int Flashes() const { return static_cast<int>(flashes.size()); }
		int CloudsDrawn = 0;

	private:
		struct Cloud
		{
			Core::DVec3 P0;
			Vector3 V0;
			float Drag = 1, Born = 0, Life = 1;
			float Size0 = 0, Size1 = 0;
			Color Tint;
			float Opacity = 0.5f, Albedo = 0.7f, Heat = 0, Cool = 1, Seed = 0;
			const void* Key = nullptr;
		};
		struct Spark
		{
			Core::DVec3 P0;
			Vector3 V0;
			float Drag = 1, Born = 0, Life = 1;
			Color Tint;
			/// <summary>A streak (its tail drawn back along its motion) or an ember (a dot, cooling red as it goes).</summary>
			bool Streak = true;
			float Px = 2, Size = 0;
		};
		struct Flash
		{
			Core::DVec3 At;
			float Born = 0, Life = 1;
			float R0 = 0, R1 = 0, MinPx = 0;
			Color Tint;
			/// <summary>0 radial, -1 a thin ring (M_Fx's shapes), 10 a streak from At to At + Dir * R1.</summary>
			int Shape = 0;
			Vector3 Dir;
		};
		struct Emitter
		{
			Core::DVec3 Last;
			Vector3 Vel;
			bool Has = false;
			float Seen = 0, Carry = 0, RcsCarry = 0, VentCarry = 0, LastBoost = -1, BoostUntil = -1;
			bool Boosting = false;
			int Live = 0;
		};

		UnitySpaceApp* app;
		cs::Ref<Mesh> smokeMesh, glowMesh;
		std::vector<Cloud> clouds;
		std::vector<Spark> sparks;
		std::vector<Flash> flashes;
		std::unordered_map<const void*, Emitter> emitters;
		float now = 0, dt = 0;
		bool rewinding = false;
		int fxEndsSeen = 0;
		uint32_t rng = 0x9E3779B9u;

		cs::List<Vector3> sv, gv;
		cs::List<Color> sc, gc;
		cs::List<Vector4> su;
		cs::List<Vector3> gu;
		cs::List<int> si, gi;
		std::vector<std::pair<float, int>> order;

		float Rand();
		float Rand(float a, float b) { return a + (b - a) * Rand(); }
		Vector3 RandDir();
		Vector3 Scatter(Vector3 dir, float spread);

		void AddCloud(const Cloud& c);
		void AddSpark(const Spark& s);
		void AddFlash(const Flash& f);

		static Core::DVec3 D(Vector3 v) { return Core::DVec3(v.x, v.y, v.z); }
		static Core::DVec3 PosAt(Core::DVec3 p0, Vector3 v0, float drag, float age);
		float PixelSize(Vector3 p, float px) const;
		float Flashes01() const;
		bool Calm() const;

		void DrawClouds(Vector3 right, Vector3 up);
		void DrawGlows(Vector3 right, Vector3 up);
		void GlowQuad(Vector3 c, Vector3 right, Vector3 up, float r, Color color, int shape);
		void GlowRibbon(Vector3 a, Vector3 b, float px, Color color);
	};
}
