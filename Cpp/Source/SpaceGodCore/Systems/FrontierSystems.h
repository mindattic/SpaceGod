#pragma once
// Port of Assets/UnitySpace/Core/Systems/FrontierSystems.cs (US-T17).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class SystemSpec;
	class StarSpec;
	class PlanetSpec;
	class StationSpec;

	/// <summary>
	/// The stars past the gate network's edge (U.SPC-T97): real neighbours that <see cref="Galaxy.Neighbours"/> shows but
	/// nobody simulates until a faction builds a gate to one (see <see cref="GateConstruction"/>). Each is a plain catalogue
	/// entry like <see cref="NearbySystems"/>' (the primary only, with the planets that are well established); the first gate
	/// in makes it a <see cref="StarSystem"/> of its own, empty and open to settlement. Values from the published
	/// measurements named in Systems/SOURCES.md; where a star's luminosity is marked derived it is worked out from its
	/// radius and temperature (Stefan-Boltzmann), and an unmeasured rotation is assumed.
	/// </summary>
	class SPACEGODCORE_API FrontierSystems
	{
	public:
		// IEnumerable + yield: built eagerly (making an entry has no side effects).
		static cs::List<cs::Ref<SystemSpec>> All();

		/// <summary>The frontier star called <paramref name="name"/>, or null.</summary>
		static cs::Ref<SystemSpec> Find(const cs::String& name);

		/// <summary>Luminosity (solar) from radius (solar) and effective temperature: L = R^2 (T / 5772 K)^4.</summary>
		static double LuminosityOf(double radiusSolar, double teffK) { return radiusSolar * radiusSolar * cs::Math::Pow(teffK / 5772.0, 4); }

	private:
		static cs::Ref<SystemSpec> System(const cs::String& name, double rh, double rm, double rs, double dd, double dm, double ds, bool south, double ly,
			const cs::Ref<StarSpec>& star, const cs::String& summary);

	public:
		/// <summary>Stellar radii a habitat site keeps clear of its star, at the least.</summary>
		static constexpr double HabitatMinRadii = 5;

		/// <summary>
		/// The orbit marked out for the first settlers' habitat (U.SPC-T293), as the settled frontier's hubs stand
		/// (<see cref="NearbySocieties"/>): the middle of the star's conservative habitable zone, or, round a star too
		/// cold to have one clear of its surface (a brown dwarf), <see cref="HabitatMinRadii"/> stellar radii out. Its
		/// water and air are assumed to come from captured comets, as at the frontier's ice stations. Fiction.
		/// </summary>
		static cs::Ref<StationSpec> Habitat(const cs::String& system, const cs::Ref<StarSpec>& star);

	private:
		static cs::Ref<StarSpec> Star(const cs::String& name, const cs::String& spectral, double mass, double radius, double luminosity, double teff, double rotationDays, const cs::String& summary);

		static cs::Ref<PlanetSpec> Planet(const cs::String& name, const cs::String& host, double periodDays, double aAu, double e, double minMassEarth, int year, const cs::String& reference, const cs::String& summary);

		static constexpr double JupiterMassSolar = 9.546e-4;

	public:
		// ---------------------------------------------------------------- 7.43 ly
		static cs::Ref<SystemSpec> Wise0855();
		// ---------------------------------------------------------------- 8.77 ly
		static cs::Ref<SystemSpec> Gliese65();
		// ---------------------------------------------------------------- 9.71 ly
		static cs::Ref<SystemSpec> Ross154();
		// ---------------------------------------------------------------- 10.31 ly
		static cs::Ref<SystemSpec> Ross248();
		// ---------------------------------------------------------------- 10.72 ly
		static cs::Ref<SystemSpec> Gliese887();
		// ---------------------------------------------------------------- 11.40 ly
		static cs::Ref<SystemSpec> Cygni61();
		// ---------------------------------------------------------------- 11.46 ly
		static cs::Ref<SystemSpec> Procyon();
		// ---------------------------------------------------------------- 11.49 ly
		static cs::Ref<SystemSpec> Struve2398();
		// ---------------------------------------------------------------- 11.62 ly
		static cs::Ref<SystemSpec> Groombridge34();
		// ---------------------------------------------------------------- 11.68 ly
		static cs::Ref<SystemSpec> DxCancri();
	};
}
