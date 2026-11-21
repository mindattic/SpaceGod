#pragma once
// US-T442: the god view from inside a planet or a hull. The pieces both views share: a line mesh built from segments (the
// in-house wireframe: MeshTopology::Lines ribbons drawn with M_Line), the see-through skin's material (M_GhostTwoSided)
// and the fade as the camera crosses the surface.

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod::Views
{
	class InteriorWire final
	{
	public:
		/// <summary>Segments with a colour at each end, in the frame of the object the mesh is put under.</summary>
		struct Lines
		{
			cs::List<Vector3> Vertices;
			cs::List<Color> Colors;
			cs::List<int> Indices;

			void Segment(Vector3 a, Vector3 b, Color c);

			/// <summary>A circle of <paramref name="radius"/> about <paramref name="centre"/> in the plane of the unit axes u, v.</summary>
			void Circle(Vector3 centre, Vector3 u, Vector3 v, float radius, int steps, Color c);

			/// <summary>A sphere about the origin as lines of latitude and longitude, north on +Y.</summary>
			void Sphere(float radius, int meridians, int parallels, int steps, Color c);

			/// <summary>The twelve edges of an axis-aligned box.</summary>
			void Box(Vector3 centre, Vector3 half, Color c);
		};

		/// <summary>A line mesh of <paramref name="lines"/> (owned by the view system, so it goes with the system).</summary>
		static cs::Ref<Mesh> Build(const cs::String& name, const Lines& lines);

		/// <summary>A child of <paramref name="parent"/> drawing <paramref name="mesh"/> with a line material of its own
		/// (its _Color fades the wireframe in and out); hidden until shown.</summary>
		static cs::Ref<MeshRenderer> Attach(const cs::String& name, const cs::Ref<Transform>& parent, const cs::Ref<Mesh>& mesh);

		/// <summary>A see-through skin material of its own (M_GhostTwoSided).</summary>
		static cs::Ref<Material> Skin(const cs::String& name);

		/// <summary>Sets a skin's colour: a dark version of the body's or hull's own tint (the material is exposed to the
		/// scene's lit white, as M_Ghost is), at <paramref name="opacity"/>.</summary>
		static void SetSkin(const cs::Ref<Material>& skin, Color tint, float opacity);

		/// <summary>How far the inside is shown, 0 to 1, for a camera <paramref name="depth"/> under the surface in units of
		/// <paramref name="ramp"/> (negative outside): 0 above the surface, 1 once <paramref name="ramp"/> deep.</summary>
		static float Fade(double depth, double ramp);

		/// <summary>The skin's opacity at <paramref name="fade"/>: nearly opaque as the camera breaks through, thinning to
		/// a veil deeper in so the rest of the game shows through it (a hull's many overlapping parts take thinner layers).</summary>
		static float SkinOpacity(float fade, float surface = 0.85f, float deep = 0.32f) { return surface + (deep - surface) * fade; }
	};
}
