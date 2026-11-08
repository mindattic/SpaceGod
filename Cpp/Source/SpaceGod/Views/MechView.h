#pragma once
// US-T83: the walking mech drawn (Core::MechWalker): the Unreal.Soulslike mech's 13 rigid parts, its legs walked by its
// own pantograph leg solve, its torso twisting and its arms pitching to the aim, its jump jets and muzzle flashes lit.

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

#include <unordered_map>

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class MechWalker;
}

namespace SpaceGod::Views
{
	/// <summary>
	/// Every mech out in a fight (<see cref="Core::MechWalker.DeployedIn"/>), drawn where <c>UnitySpaceApp::MechPose</c> puts
	/// it: on a hull, its soles on the plating as the hull is drawn; in flight, at its battle point. Quaternius's CC0 "George"
	/// mech, cut into rigid parts by Unreal.Soulslike's tools/convert_mech.py and baked into arrays by tools/bake_mech.py
	/// (Rendering/MechMeshData.inl), under the procedural ship material with its vertex colours. Posed as ASoulsMech posed it
	/// (SoulsMech.cpp, TickPose): the hips bob and roll with the gait and drop on landing, the torso twists by the mech's
	/// <see cref="Core::MechWalker.TorsoYawDeg"/>, the arms pitch by its aim, and each bird leg (thigh, shin, metatarsal,
	/// foot) is solved to its foot by the same pantograph two-bone solve. US-T270: on a hull each foot plants on the plating and
	/// stays put there while the body walks over it, then steps (by the walk cycle, <see cref="Core::MechWalker.GaitPhase"/>) to
	/// a spot ahead found on the plating, so the soles meet the hull's curve and the hips settle to the lower foot; in flight the
	/// feet hang tucked. The jets burn under its back while lit and each
	/// arm's muzzle flashes as it fires, camera-facing quads in the Fx shader. Presentation only.
	/// </summary>
	class MechView final
	{
		struct Rig
		{
			cs::Ref<Transform> Root;
			/// Pelvis, Torso, Canopy, ArmL, ArmR, then per leg (left, right) thigh, shin, metatarsal, foot: as MechParts.
			cs::Ref<Transform> Parts[13];
		};

		/// What the view eases per mech: how much it is walking (0 standing, 1 striding) and which way.
		struct Anim
		{
			double Activity = 0;
			double Sign = 1;
			double LastPhase = 0;
			/// US-T270: each foot planted on the plating (hull frame, hull lengths), and in a step where it lifted from and
			/// where it comes down; the hull they are on (a new one, or none, plants them afresh).
			bool Planted[2] = { false, false };
			bool Swinging[2] = { false, false };
			Core::DVec3 Plant[2], From[2], To[2];
			const void* HullWas = nullptr;
		};

		UnitySpaceApp* app;
		cs::Ref<Transform> parent;
		cs::List<Rig> rigs;
		std::unordered_map<const Core::MechWalker*, Anim> anims;
		cs::Ref<Mesh> meshes[13];
		MaterialPropertyBlock paint;

		cs::Ref<GameObject> fx;
		cs::Ref<Mesh> fxMesh;
		cs::List<Vector3> vertices;
		cs::List<Color> colors;
		cs::List<Vector3> uvs;
		cs::List<int> indices;

		void BuildMeshes();
		Rig MakeRig();
		void Pose(Rig& rig, const cs::Ref<Core::MechWalker>& m, Anim& a, float dt);
		void Sprite(Vector3 c, float radius, Color color, Vector3 right, Vector3 up);

	public:
		MechView(UnitySpaceApp* app, const cs::Ref<Transform>& parent);
		~MechView();

		void Update();
	};
}
