#pragma once
// US-T158: the EVA repair worker drawn on the hull (US-T135 had the job, the HUD and the camera, but no figure).

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// The suited EVA repair worker the SpaceGod holds (and since US-T166 a pilot ejected from its craft, in open space or on
	/// the casing of the torpedo it rides, drawn under its boots to the suit's scale), standing on the plating where <c>UnitySpaceApp::WorkerScene</c> puts
	/// it (on the hull as drawn), its feet on the hull's surface, its back along the hull's normal and its front toward
	/// where the player looks: a white suit (torso, legs, arms reaching forward), a gold visor, an orange-banded backpack,
	/// and the torch's spark (a hot white-blue flicker with an amber flare) while there is work under it. Built from the
	/// built-in cube, capsule and sphere under the procedural ship material: no purchased pack has a suited figure.
	/// Drawn a little larger than life (<c>UnitySpaceApp::WorkerHeight</c>, a fixed fraction of its hull's drawn length),
	/// like the cargo pods, so it reads against the plating and the camera keeps a distance the near plane allows at the
	/// realistic scale too.
	/// </summary>
	class EvaWorkerView final
	{
		UnitySpaceApp* app;
		cs::Ref<Transform> root;
		/// <summary>US-T166: the casing of the torpedo an ejected pilot rides, drawn under its boots to the suit's scale.</summary>
		cs::Ref<Transform> casing;
		MaterialPropertyBlock hull, warhead;
		cs::Ref<GameObject> spark;
		cs::Ref<Mesh> sparkMesh;
		MaterialPropertyBlock suit, visor, pack, band;
		cs::List<Vector3> vertices;
		cs::List<Color> colors;
		cs::List<Vector3> uvs;
		cs::List<int> indices;

		void Part(const cs::Ref<Mesh>& mesh, Vector3 at, Vector3 scale, Vector3 euler, const MaterialPropertyBlock& block, const char* name,
			const cs::Ref<Transform>& parent = nullptr);
		void Sprite(Vector3 c, float radius, Color color, Vector3 right, Vector3 up);

	public:
		EvaWorkerView(UnitySpaceApp* app, const cs::Ref<Transform>& parent);
		~EvaWorkerView();

		void Update();
	};
}
