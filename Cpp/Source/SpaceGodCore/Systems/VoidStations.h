#pragma once
// Port of Assets/UnitySpace/Core/Systems/VoidStations.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "Systems/SystemSources.h"
#include "Systems/GateConstruction.h"

#include <utility>

namespace SpaceGod::Core
{
	struct DVec3;
	class StarEntry;
	class SystemSpec;
	class StarSystem;
	class Polities;

	/// <summary>
	/// The void stations of the real neighbourhood (U.SPC-T454), as a <see cref="SystemSource"/>: wherever a stretch of
	/// <see cref="MaxLegLy"/> between the real systems out to <see cref="CatalogueStars.ChartedToLy"/> (Sol, the hand-built
	/// systems and the <see cref="CatalogueStars"/>) would otherwise hold no destination, a relay stands in open space
	/// (<see cref="StarSystem.CreateVoidBase"/>), so no leg between charted destinations is longer than a gate reaches.
	///
	/// The real systems are joined by their shortest links (a minimum spanning tree over the distances, from Sol): a link
	/// within <see cref="MaxLegLy"/> needs nothing, and one longer is cut into even legs no longer than that by stations
	/// laid along it (<see cref="GateNetwork.RelayPositions"/>). Every real system can so be reached from every other in
	/// legs of <see cref="MaxLegLy"/> or less, by way of other systems and stations. A tree's long link never passes near a
	/// third system (that one would be the shorter way round), so no station sits on top of a star. They are numbered
	/// outward from Sol, nearest first, and are the same in every galaxy: they come from the real stars, not the seed.
	/// </summary>
	class VoidStations final : public SystemSource
	{
	public:
		static inline const cs::String SourceName = "Void stations";

		/// <summary>What every void station's name starts with; its number follows, nearest Sol first.</summary>
		static inline const cs::String Prefix = "Void Station";

		/// <summary>The longest leg left between charted destinations: the reach of one gate (<see cref="GateConstruction.MaxLinkLy"/>).</summary>
		static constexpr double MaxLegLy = GateConstruction::MaxLinkLy;

		/// <summary>The clear space kept about each station, so the procedural galaxy puts no star on one.</summary>
		static constexpr double ClearLy = 1;

		cs::String Name() const override;
		/// <summary>
		/// Ahead of the catalogue (U.SPC-T456), whose claim on the 20 ly about Sol and the 3 ly about each of its stars is to keep
		/// invented stars out, not the relays between real ones: after it, a station that near Sol or a star was never charted.
		/// </summary>
		int Order() const override;

		/// <summary>In the galaxy from the start like the stars they join (U.SPC-T456): known within <see cref="StarDiscovery.KnownToLy"/>
		/// of Sol, undiscovered past it, as the stars about them are.</summary>
		bool ChartedFromStart() const override;

		/// <summary>The real systems the stations join, Sol first, then the hand-built systems and the catalogue stars (by name, once each).</summary>
		static cs::List<cs::Ref<StarEntry>> Destinations();

		/// <summary>Every void station, nearest Sol first.</summary>
		static const cs::List<cs::Ref<StarEntry>>& Stations();

		/// <summary>The two real systems whose link the station called <paramref name="name"/> stands on; nulls if there is none.</summary>
		static std::pair<cs::String, cs::String> Bridges(const cs::String& name);

		bool Claims(DVec3 ly) override;

		cs::List<cs::Ref<StarEntry>> StarsNear(DVec3 centreLy, double radiusLy, int seed) override;

		cs::Ref<SystemSpec> Define(cs::Ref<StarEntry> star, int seed) override;

		/// <summary>Built as the relay it is: the generator and its tiny suns, unsettled and nobody's (see <see cref="StarSystem.IsVoidBase"/>).</summary>
		cs::Ref<StarSystem> Build(cs::Ref<StarEntry> star, cs::Ref<SystemSpec> spec, double jd, cs::Ref<Polities> polities) override;

	private:
		static void Lay();
	};
}
