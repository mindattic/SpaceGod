#pragma once
// Port of Unity.SpaceGod Runtime/Views/DiscView.cs (US-T30).

#include "UnityCompat/UnityRenderingTypes.h"
#include "DVec3.h"

#include <utility>

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::Core
{
	class DebrisDisc;
}

namespace SpaceGod::Views
{
	class LineStrip;

	/// <summary>
	/// Draws a system's debris belts and discs (<see cref="StarSystem.Discs"/>: Epsilon Eridani's
	/// three belts, Tau Ceti's disc) as a band of faint concentric rings in the disc's real plane,
	/// from its inner to its outer edge, mapped through the scale model like everything else. Shown
	/// with the orbits.
	/// </summary>
	class DiscView final
	{
		static constexpr int Rings = 6, Points = 161;
		static constexpr Color DustColor = Color(0.82f, 0.72f, 0.58f, 0.32f);

		UnitySpaceApp* app;
		cs::List<std::pair<cs::Ref<Core::DebrisDisc>, cs::List<cs::Ref<LineStrip>>>> discs; // (disc, rings)
		cs::List<Vector3> points = cs::NewArray<Vector3>(Points);
		double laidOutBlend = cs::Double::NaN;

	public:
		DiscView(UnitySpaceApp* app, const cs::Ref<Transform>& parent);

		/// <summary><paramref name="fade"/> multiplies the rings (the battle fade, US-T134); each ring also fades with its distance.</summary>
		void Update(bool visible, float fade = 1.0f);

	private:
		static Core::DVec3 Perpendicular(Core::DVec3 n);
	};
}
