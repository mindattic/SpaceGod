#pragma once
// US-T125: the Bio Mass drawn: pulsing, viscous globules of flesh, bone, tissue, mouths and hair.

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

#include <memory>
#include <unordered_map>

namespace SpaceGod
{
	class UnitySpaceApp;
	namespace Views { class LineStrip; }
}

namespace SpaceGod::Core
{
	class BioGlobule;
	class Ship;
	enum class BioForm : int;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// Every globule of the Bio Mass in the shown system (<see cref="Core::BioMass"/>), drawn as soft-body meat. Each is
	/// one procedural mesh built from its seed: a vertex-displaced icosphere (the body) with a few smaller lobes half sunk
	/// into it (metaball-like, swelling and merging), gaping mouths ringed with teeth that slowly open and close, shards of
	/// bone breaking the surface, and tufts of hair that sway. Its surface moves like fluid but slow and viscous: a few
	/// broad lumps swell and sag at under a hertz, a peristaltic wave rolls over it, and a hit sets off a damped ripple.
	///
	/// Cost scales with screen size: a globule large on screen is re-shaped on the CPU (about 2,500 vertices, at most
	/// 30 times a second); a small one keeps its last shape and only breathes (its transform pulses); one under a few
	/// pixels is hidden (the HUD's marker still shows it). Its size is its radius drawn at ship scale, and it sits at its
	/// anchor through the scale model like a wreck, so it reads among the hulls in real and game scale alike.
	///
	/// Shots at it are drawn as tracers: from gun satellites ringing the body whose orbital guns fire, from the gate, or
	/// from the hull.
	///
	/// US-T192: blooms and tumors (<see cref="Core::BioForm"/>) have looks of their own. A bloom is a small, fast, writhing
	/// cluster: a knot of bright bulbs round one gaping mouth on long tendrils that lash and curl, their tips aglow, tumbling
	/// as it darts. A tumor is a large pulsing growth: a cauliflower of swellings, veined thick, that beats like a heart,
	/// with dark roots sunk into what it is on. The model is rebuilt when a bloom takes root and becomes a tumor. A growth
	/// latched onto a hull is drawn on the hull as its view draws it (a spot on its skin from the growth's seed, in the
	/// hull's own frame, so it turns with it, no bigger than the hull can carry); one on a station or asteroid sits on the
	/// body as drawn, its roots toward it.
	/// </summary>
	class BioMassView final
	{
	public:
		struct Shape;

		/// <summary>Where and how a globule is drawn: its scene point, its drawn radius, and for one latched on, the scene
		/// direction to its host (its roots, local -Y, are turned that way).</summary>
		struct Placement
		{
			Core::DVec3 Scene;
			float Radius = 0;
			Vector3 Down = Vector3(0, 0, 0);
			bool Rooted = false;
		};

	private:
		struct View
		{
			cs::Ref<Core::BioGlobule> Owner;
			cs::Ref<GameObject> Go;
			cs::Ref<::SpaceGod::UnityCompat::Mesh> Body;
			std::shared_ptr<BioMassView::Shape> Form;
			/// The form its model was built for (US-T192): rebuilt when the globule's changes.
			Core::BioForm Kind{};
			float LastShaped = -1;
			int Tracers = 0;
		};

		UnitySpaceApp* app;
		cs::Ref<Transform> parent;
		std::unordered_map<Core::BioGlobule*, View> views;
		cs::List<cs::Ref<LineStrip>> tracers;
		int tracersUsed = 0;

		static cs::Ref<Material>& material();

	public:
		BioMassView(UnitySpaceApp* app, const cs::Ref<Transform>& parent);
		~BioMassView();

		void Update();

		/// <summary>A globule's mesh shaped at time <paramref name="t"/> (seconds): the model, used by the view and its tests.</summary>
		static std::shared_ptr<Shape> Build(int seed, Core::BioForm form);
		static void Animate(Shape& shape, Mesh& mesh, float t, float flinch, Vector3 hitDir);

		/// <summary>Where a globule is drawn (scene): its anchor through the scale model, its local offset at ship scale;
		/// a growth on a hull on the hull as drawn, one on a station or asteroid on the body as drawn (US-T192).</summary>
		static Core::DVec3 SceneOf(UnitySpaceApp* app, const Core::BioGlobule& g);
		static Placement Place(UnitySpaceApp* app, const Core::BioGlobule& g);

		/// <summary>The hull a growth is latched onto (null if none, or it is gone).</summary>
		static cs::Ref<Core::Ship> HostShip(UnitySpaceApp* app, const Core::BioGlobule& g);

	private:
		View Create(Core::BioGlobule* g);
		void Tracer(Vector3 from, Vector3 to, Color color);
	};
}
