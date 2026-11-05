#pragma once
// US-T136: the shine on a unit another player embodies (co-op or PvP).

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// Every hull another player embodies now (<see cref="UnitySpaceApp.ShiningNow"/>) catches the eye, so the others can
	/// tell a player's hull from the AI's at a glance, whether they fly with you or against you. Your own embodied unit does
	/// not shine for you. Up close the hull wears three things, all additive glow quads over the model (no material of the
	/// hull is touched):
	/// <list type="bullet">
	/// <item>a soft rim glow in that player's seat colour, set just behind the hull so its silhouette is lit round the edge,
	/// breathing slowly;</item>
	/// <item>a slow specular sweep: a glint of light sliding bow to stern across the near side of the plating every
	/// <see cref="SweepPeriodS"/>;</item>
	/// <item>an occasional star-glint flare (a four-pointed star) where the sweep crests, every other sweep or so.</item>
	/// </list>
	/// When the hull is too small on screen for that to read (under <see cref="FarPx"/> across) it becomes a glint sprite of
	/// fixed screen size, scaled with the screen's height: a twinkling four-pointed star in the seat colour with a white
	/// core, flaring at the same beat. Sizes come from the hull's drawn radius and the pixels per unit, so the same rules hold
	/// at realistic and game scale.
	///
	/// A hull running dark (US-T93: silent running, or its reactor down) shines only inside visual range of this machine's
	/// eye (<see cref="UnitySpaceApp.VisualRangeKm"/>): there you could see the hull itself, so marking it gives nothing
	/// away; beyond it the shine would be a sensor the dark hull has beaten. Visual only: nothing here touches the simulation.
	/// </summary>
	class EmbodiedShine final
	{
	public:
		EmbodiedShine(UnitySpaceApp* app, const cs::Ref<Transform>& parent);

		void Update();

		/// <summary>The colour of a co-op seat (P1 amber, P2 cyan, P3 rose, P4 lime), clear of the HUD's red hostile and green
		/// friendly so a player's colour never reads as a stance.</summary>
		static Color SeatColor(int seat);

		/// <summary>For screenshots: when set (0..1), every shine holds this point of its cycle instead of running.</summary>
		static inline float FreezePhase = -1.0f;

		/// <summary>How many hulls shine now, near and far (for the console command's log).</summary>
		int NearCount = 0, FarCount = 0, HiddenCount = 0;

		/// <summary>Seconds per specular sweep (the sweep itself takes the first <see cref="SweepActiveS"/>).</summary>
		static constexpr float SweepPeriodS = 5.5f, SweepActiveS = 2.4f;

		/// <summary>Below this many pixels across, the hull is drawn as the far glint sprite; above <see cref="NearPx"/>,
		/// as the close-up shine; between, both, crossfaded.</summary>
		static constexpr float FarPx = 18.0f, NearPx = 36.0f;

		/// <summary>The far glint's arm length and core, in pixels at a 1080-line screen (scaled with its height).</summary>
		static constexpr float GlintArmPx = 38.0f, GlintCorePx = 11.0f;

	private:
		struct Glint
		{
			cs::Ref<Transform> Halo, Sweep, FlareA, FlareB, FlareCore, FarA, FarB, FarCore;
			cs::Ref<Material> HaloM, SweepM, FlareAM, FlareBM, FlareCoreM, FarAM, FarBM, FarCoreM;
		};

		UnitySpaceApp* app;
		cs::Ref<Transform> parent;
		cs::List<cs::Ref<Glint>> pool;
		static cs::Ref<Mesh>& quad();

		cs::Ref<Glint> Create(int index);
		cs::Ref<Transform> Quad(const cs::String& name, cs::Ref<Material>& material, float falloff);
		static void Hide(const cs::Ref<Glint>& g);
		static void Show(const cs::Ref<Transform>& t, const cs::Ref<Material>& m, bool on);
	};
}
