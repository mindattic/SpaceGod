#pragma once
// Port of Assets/UnitySpace/Core/Systems/VoidBaseBuilder.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class BodyCatalog;

	/// <summary>
	/// The handful of bodies a Void Base ever has (U.SPC-T372, see <see cref="StarSystem.CreateVoidBase"/>): the
	/// artificial gravity/sun generator at its centre -- a "black hole in a bottle" (see
	/// <see cref="BodyDefinition.Singularity"/>), dark and light-swallowing rather than lit, giving the relay
	/// station around it an orbit and a "down" with no real star or planet anywhere near -- circled by a few tiny,
	/// perfectly ordinary suns that light the place without lighting what sits at its heart. Nothing else is ever
	/// added: nothing could really orbit a machine this light, and nothing is meant to settle here.
	/// </summary>
	class VoidBaseBuilder
	{
	public:
		/// <summary>The generator's own bulk: a compact machine, nothing like a star's scale.</summary>
		static constexpr double RadiusKm = 50;

		/// <summary>Its catalogue gravitational parameter: just enough to be a body, not to hold anything in orbit for real.</summary>
		static constexpr double Gm = 1e-6;

		/// <summary>How many tiny suns circle the generator.</summary>
		static constexpr int TinySuns = 3;

		/// <summary>How far out they orbit, and how fast: close enough, and quick enough, to read at a glance as the
		/// pretty, misleadingly cheerful ring a lawless relay hides behind.</summary>
		static constexpr double TinySunOrbitKm = RadiusKm * 12;
		static constexpr double TinySunPeriodDays = 5;
		static constexpr double TinySunRadiusKm = 4;
		static constexpr double TinySunTeffK = 5800;

		/// <summary>How much of it the overview frames: just past the tiny suns' own orbit, there being nothing else out here to see.</summary>
		static constexpr double OverviewSpanKm = TinySunOrbitKm * 2.5;

		static cs::Ref<BodyCatalog> Build(const cs::String& name);
	};
}
