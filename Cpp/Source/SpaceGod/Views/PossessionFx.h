#pragma once
// US-T446: possession as a show. As the SpaceGod takes a seat (a helm, a craft, a gun, a station, a satellite, a mech, a
// person out on the plating or in open space) an almost imperceptible energy crosses from the god's eye to it and seeps
// into the hull; what can be seen then becomes what that pilot perceives; letting go, the energy leaves and the god's full
// view returns. Every tunable of it is here, in one place (PossessionTune). View only: nothing in the simulation changes,
// and each co-op peer plays its own player's show.

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Views
{
	/// <summary>Every number of the possession show and of the perceived view, in one place.</summary>
	struct PossessionTune
	{
		// ---- the show's clock (real seconds; a seat switched straight to another plays it SwitchSpeed times as fast)
		static constexpr float EnterSeconds = 2.7f, LeaveSeconds = 2.0f, SwitchSpeed = 1.8f;

		// ---- the energy crossing: motes from the edge of the god's view to the hull
		static constexpr int MoteCount = 22;
		/// <summary>When the first mote leaves and how far apart they leave, and how long each takes to cross (seconds).</summary>
		static constexpr float MoteFirstS = 0.05f, MoteStaggerS = 0.03f, MoteCrossS = 0.66f;
		/// <summary>A mote's core and trail on screen (px at a 1080-line screen) and its brightness.</summary>
		static constexpr float MotePx = 4.4f, MoteTrail = 11.0f, MoteIntensity = 2.4f;
		/// <summary>Where a mote starts: this share of the way from the view's centre to its edge, a little ahead of the eye.</summary>
		static constexpr float MoteStartSpread = 0.42f;
		/// <summary>The sideways curl of a mote's path (a share of the way across).</summary>
		static constexpr float MoteCurl = 0.16f;
		/// <summary>Letting go: how far out towards the eye the motes drift before they are gone (a share of the way).</summary>
		static constexpr float MoteLeaveReach = 0.7f;

		// ---- the seep into the hull: a rim glow behind it, a sheen sliding over it, its own hull material's rim lit
		static constexpr float HaloInS = 0.5f, HaloPeakS = 1.05f, HaloOutS = 2.45f, HaloIntensity = 0.8f;
		static constexpr float SweepFromS = 0.62f, SweepToS = 1.85f, SweepIntensity = 2.2f;
		/// <summary>The hull model's own rim term (the selection rim, _Highlight) at its peak.</summary>
		static constexpr float HullRimPeak = 2.2f;
		/// <summary>A person, a mech: the glow round the suit, in suit heights.</summary>
		static constexpr float PersonGlowHeights = 0.6f;
		/// <summary>A person's glow is this much of a hull's (the eye is close by: a hull's would wash the view).</summary>
		static constexpr float PersonGlowShare = 0.7f;

		// ---- the breath: the view narrows a touch as the energy goes in (widens as it leaves), and a faint wash
		static constexpr float FovBreathDeg = 2.6f, FovBreathS = 1.7f;
		static constexpr float WashAlpha = 0.055f, WashPeakS = 0.95f, WashS = 1.9f;

		// ---- the perceived view: when it comes in, and how long it takes to come in (and to go, letting go)
		static constexpr float SettleStartS = 1.25f, SettleSeconds = 0.9f, ReleaseSeconds = 1.1f;

		// ---- what each kind perceives (the helm's picture is the simulation's own: Sensors::Player)
		/// <summary>How often the seat's view-side picture is swept (real seconds), and how long a cold track is held (sim seconds).</summary>
		static constexpr float SweepSeconds = 0.5f;
		static constexpr double HoldSimSeconds = 180;
		/// <summary>A launched craft's sensors, a mech's and a satellite's (range km at a one-unit signature, resolution).</summary>
		static constexpr double CraftRangeKm = 1.5e5, CraftResolution = 1.2;
		static constexpr double MechRangeKm = 6e4, MechResolution = 1.0;
		static constexpr double SatelliteRangeKm = 2e5, SatelliteResolution = 1.5;
		/// <summary>Eyes alone: seen within visual range (one arcminute), its class within this share of it, who it is within this.</summary>
		static constexpr double EyeClassShare = 0.33, EyeIdentifyShare = 0.08;

		// ---- the perceived view's character on screen (alphas at full)
		static constexpr float BracketAlpha = 0.26f, BracketInset = 26.0f, BracketArm = 54.0f;
		static constexpr float VisorAlpha = 0.78f;
		static constexpr float PanoramaBand = 0.075f, PanoramaAlpha = 0.6f;
		static constexpr float CaptionAlpha = 0.7f;

		/// <summary>The energy's colour: a pale blue-white (the hull rim's own hue, so the two read as one light).</summary>
		static constexpr Color Energy{0.62f, 0.84f, 1.0f, 1.0f};
		static constexpr Color EnergyCore{0.9f, 0.96f, 1.0f, 1.0f};
	};

	/// <summary>One frame of the show, as the app works it out: where the hull is (camera-relative) and how far along it is.</summary>
	struct PossessionFrame
	{
		bool Show = false;
		/// <summary>Entering (the energy goes in) or leaving (it comes back out).</summary>
		bool Entering = true;
		/// <summary>Seconds into the show (already sped up for a switch).</summary>
		float T = 0;
		/// <summary>The hull's centre relative to the camera (scene units), its half length and its nose.</summary>
		Vector3 Rel;
		float Radius = 0;
		Vector3 Forward;
		/// <summary>A person or a mech (a round glow about the suit, not a hull's long rim).</summary>
		bool Person = false;
		/// <summary>0 for none: reduced motion holds the motes still (they only fade) and Reduce flashes dims the light.</summary>
		bool Still = false;
		float Brightness = 1;
	};

	/// <summary>
	/// The 3D half of the show: additive glow quads (the Glow material, no new shader): the motes crossing from the edge of the
	/// view to the hull, a rim glow just behind the hull and a sheen sliding over its near side. The hull model's own rim is lit
	/// by the app (ShipView's _Highlight), the screen's breath and the perceived view's frame by the HUD.
	/// </summary>
	class PossessionFx final
	{
	public:
		PossessionFx(UnitySpaceApp* app, const cs::Ref<Transform>& parent);

		void Update(const PossessionFrame& f);

		/// <summary>How many motes were lit last frame (for the console command's log).</summary>
		int MotesLit = 0;

		/// <summary>The halo's and the sheen's brightness last frame, 0..1 of their peak (for the log).</summary>
		float HaloNow = 0, SweepNow = 0;

	private:
		UnitySpaceApp* app;
		cs::Ref<Transform> parent;
		cs::Ref<Transform> halo, sweep;
		cs::Ref<Material> haloM, sweepM;
		cs::List<cs::Ref<Transform>> motes;
		cs::List<cs::Ref<Material>> moteM;
		static cs::Ref<Mesh>& quad();

		cs::Ref<Transform> Quad(const cs::String& name, cs::Ref<Material>& material, float falloff);
		static void Show(const cs::Ref<Transform>& t, const cs::Ref<Material>& m, bool on);
	};
}
