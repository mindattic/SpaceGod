#pragma once
// Port of Unity.SpaceGod Runtime/Views/InterstellarView.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

#include <utility>

namespace SpaceGod::Core
{
	class Galaxy;
	class StarSystem;
}

namespace SpaceGod::Views
{
	class LineStrip;

	/// <summary>
	/// The view between star systems: the simulated systems' stars and the other nearest stars at
	/// their real positions (light-years, J2000 ecliptic axes like everything else), each a glow sized
	/// by its luminosity, with the jump-gate lattice (<see cref="GateNetwork"/>) drawn as lines (red
	/// where a gate is under blockade) and the ships inside the gates as sparks moving along them. It
	/// has its own coordinates (the camera's position in light-years) and never mixes with a system's
	/// scene. The seed of a galactic map.
	/// </summary>
	class InterstellarView final
	{
	public:
		cs::Ref<GameObject> Root;

		class Star final
		{
		public:
			cs::String Name;

			/// <summary>What the HUD writes beside it: the system's name for a simulated system's first star, null for none.</summary>
			// C#'s null label is the empty string here.
			cs::String Label;

			/// <summary>The simulated system whose name it carries (its first star), for who holds it; null otherwise.</summary>
			cs::Ref<Core::StarSystem> Of;

			Core::DVec3 PositionLy;
			UnityCompat::Color Color;
			float Size = 0;
			bool System = false;
			cs::Ref<Transform> T;
			cs::Ref<UnityCompat::Material> Material;

			/// <summary>Camera-relative position this frame.</summary>
			Vector3 Relative;
		};

	private:
		cs::List<cs::Ref<Star>> stars;
		cs::List<cs::Ref<LineStrip>> links;
		cs::List<std::pair<cs::Ref<Transform>, cs::Ref<Material>>> sparks; // (t, m)
		cs::Ref<Core::Galaxy> galaxy;

		// C#'s `static Mesh quad;`: shared by every InterstellarView.
		static cs::Ref<Mesh>& quad();

	public:
		/// <summary>Scene units per light-year inside the view.</summary>
		static constexpr double UnitsPerLy = 1000;

		const cs::List<cs::Ref<Star>>& Stars() const { return stars; }

		InterstellarView(const cs::Ref<Core::Galaxy>& galaxy, const cs::Ref<Transform>& parent);

	private:
		cs::Ref<Star> Add(const cs::String& name, Core::DVec3 ly, Color color, float size, bool system, const cs::String& label);

	public:
		bool Visible() const { return Root->activeSelf; }
		void SetVisible(bool value) { Root->SetActive(value); }

		/// <summary>Places every star relative to a camera at <paramref name="cameraLy"/>, keeping each a steady few pixels across.</summary>
		void Update(Core::DVec3 cameraLy, Quaternion cameraRotation, float fade);
	};
}
