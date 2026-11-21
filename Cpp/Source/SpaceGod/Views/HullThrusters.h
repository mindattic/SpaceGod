#pragma once
// US-T231: every hull's thrusters -- main engines at the stern, RCS pods at its corners and flanks, retros at the bow --
// placed on the hull's own surface and firing with its thrust, turn, roll and braking.

#include "UnityCompat/UnityRenderingTypes.h"
#include "Rendering/EnginePorts.h"
#include "Views/HullSurface.h"

#include <vector>

namespace SpaceGod::Views
{
	/// <summary>
	/// A hull's thrusters (US-T231), built once from the hull as drawn and parented to its view's root (one unit long, nose
	/// +Z, up +Y, starboard +X), so they scale with it at either scale:
	/// <list type="bullet">
	/// <item>main engines out of the nozzles the model draws (US-T432); a model with none marked gets them on its stern
	/// face: on the hull's engine hardpoints where it has them, else one, two or three across its aft face as its shape
	/// fits, each set on the rearmost plating behind it;</item>
	/// <item>RCS quads at each corner towards the bow and the stern: an up or down jet and a port or starboard one;</item>
	/// <item>retros on the bow's forward-facing plating, port and starboard, firing forward to brake;</item>
	/// <item>a pair of roll jets on each flank amidships, on its top firing up and underneath firing down.</item>
	/// </list>
	/// US-T433: every RCS jet sits on the hull's own plating (HullSurface: its triangles, an imported kit part's real ones
	/// included), on a face that looks the way the jet fires, the outermost such its way at its station, with nothing of the
	/// hull in its exhaust's way, and fires straight along the axis it serves. Which way each RCS jet
	/// turns the hull comes from where it sits (its lever about the hull's middle), so a pitch, yaw or roll -- spun up into
	/// or braked out of -- fires the jets that would make it, at both ends, opposite ways.
	/// </summary>
	class HullThrusters final
	{
	public:
		/// <summary>Builds the thrusters on <paramref name="root"/> round the hull drawn by <paramref name="hull"/> (renderers
		/// under that root). <paramref name="ports"/> are the engine nozzles its model draws (US-T432, Rendering::EnginePorts,
		/// in the root's frame): the main engines fire out of them, each along its own axis. A model with none marked falls
		/// back to <paramref name="engines"/>, its engine hardpoints in the root's frame, or its stern's own shape.</summary>
		HullThrusters(const cs::Ref<Transform>& root, const cs::List<cs::Ref<Renderer>>& hull, const cs::List<Vector3>& engines,
			const cs::List<Rendering::EnginePort>& ports);

		/// <summary>Where the main engines' exhaust leaves the hull: the middle of their nozzles, in the root's frame.</summary>
		Vector3 SternCentre() const { return sternCentre; }

		int MainCount() const { return mains; }
		int JetCount() const { return jets.Count(); }

		/// <summary>The main engines are lit this frame (past a flicker), for the exhaust that trails them.</summary>
		bool MainLit() const { return mainLevel > 0.3f; }

		/// <summary>
		/// One frame. <paramref name="rotation"/> is the hull's drawn rotation: its spin is read from how it moved since the
		/// last frame, and the RCS fires on the spin's change, full at <paramref name="fullAccel"/> rad/s^2.
		/// <paramref name="thrust"/> (0..1) lights the main engines, <paramref name="brake"/> (0..1) the retros;
		/// <paramref name="intensity"/> scales the RCS's brightness.
		/// </summary>
		void Update(Quaternion rotation, float thrust, float brake, float fullAccel, float intensity, float dt, float now);

		/// <summary>Puts every jet away and forgets the last spin, so the next shown frame does not flare from a stale one.</summary>
		void Hide();

	private:
		enum class Kind { Main, Retro, Rcs };

		struct Jet
		{
			cs::Ref<Transform> T;
			cs::Ref<MeshRenderer> R;
			Kind K = Kind::Rcs;
			/// <summary>How it turns the hull: its share of nose-up, nose-to-starboard and clockwise acceleration (-1..1).</summary>
			float Pitch = 0, Yaw = 0, Roll = 0;
			float Level = 0, Phase = 0, Width = 0, Length = 0;
		};

		cs::Ref<Transform> root;
		cs::List<Jet> jets;
		Vector3 sternCentre;
		int mains = 0;
		float mainLevel = 0;

		Quaternion lastRotation;
		bool hasLast = false, spinKnown = false;
		float spinPitch = 0, spinYaw = 0, spinRoll = 0;
		float accPitch = 0, accYaw = 0, accRoll = 0;

		void Add(Kind kind, Vector3 at, Vector3 exhaust, Vector3 centre, float width, float length);
		void AddMains(const std::vector<Vector3>& pts, Vector3 mn, Vector3 mx, Vector3 c, float length, const cs::List<Vector3>& engines);
		void AddRcs(const HullSurface& surface);

		static cs::Ref<Material> RcsMaterial();
		static cs::Ref<Material> MainMaterial();
	};
}
