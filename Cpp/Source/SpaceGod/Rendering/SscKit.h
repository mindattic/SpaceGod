#pragma once
// US-T143: stations drawn from Ebal Studios' Space Station Creator Remastered (docs/ThirdPartyArt.md).

#include "UnityCompat/UnityRenderingTypes.h"

namespace SpaceGod::Core
{
	class Faction;
	enum class StructureKind : int;
	enum class IndustryKind : int;
}

namespace SpaceGod::Rendering
{
	/// <summary>
	/// Builds stations from Space Station Creator (US-T143), imported to /Game/ThirdParty/SSC by tools/import-ssc.ps1: the
	/// orbital structures (factories and depots from its Storage complexes, habitats on its wheel, gateways on its gyroscope
	/// ring), the free-flying stations (Rigil Gate, Toliman Forge, Blackhollow: its big Combined complexes), the Void
	/// Stations (the generator's core and rings inside the pack's sunburst frame, docks and frames round it), the void
	/// sites' megastructure archetype (tower blocks stacked into a city with long docking spars out to its edge, after the
	/// US-T130 reference) and, for US-T131, a derelict station: the same complexes darkened and lightless with a module
	/// torn off. Each station is painted in one of the pack's colours (navies by their hue, civilians white, pirates brown,
	/// unowned grey) with a light wash of the owner's colour.
	///
	/// The pack is third-party art that is never committed. Without it, in a headless run or with <c>-NoSSC</c>,
	/// <see cref="Available"/> is false and every caller keeps the procedural model.
	/// </summary>
	class SscKit
	{
	public:
		/// <summary>Where the import puts the pack.</summary>
		static constexpr const char* Root = "/Game/ThirdParty/SSC";

		/// <summary>Whether stations are drawn from the pack: the game renders, it is imported and its material is there.
		/// Worked out once; <c>-NoSSC</c> turns it off.</summary>
		static bool Available();

		/// <summary>Working, or dark and damaged (a derelict: no lights, a module torn off and tumbling beside it).</summary>
		enum class Condition { Working, Derelict };

		/// <summary>
		/// One of the pack's stations (a model name: Combined20, Storage05, Cylinderical12 ...) under <paramref name="parent"/>,
		/// centred and scaled so its longest side is <paramref name="size"/>, flat across the parent's XZ plane (a wheel's
		/// axis on +Y; pass <paramref name="axisZ"/> for one on +Z), every renderer added to <paramref name="into"/>.
		/// False (nothing built) without the pack or the model.
		/// </summary>
		static bool Station(const cs::Ref<Transform>& parent, const cs::String& model, float size, const cs::Ref<Core::Faction>& faction,
			cs::List<cs::Ref<Renderer>>& into, Condition condition = Condition::Working, bool axisZ = false, int seed = 0);

		/// <summary>The station an orbital structure is drawn as, or empty for one the pack does not draw (shipyards keep
		/// their slip, the ISS and Tiangong their real shapes).</summary>
		static cs::String ForStructure(Core::StructureKind kind, Core::IndustryKind industry, const cs::String& name);

		/// <summary>The station a free-flying station body (BodyKind::Station) is drawn as, by its name.</summary>
		static cs::String ForStationBody(const cs::String& name);

		/// <summary>A Void Station round its generator, in body radii (inside VoidStationModel::OuterRadius, clear of the
		/// core): the sunburst frame across the equator, docks at its spokes' ends and frames over the poles.</summary>
		static bool VoidStation(const cs::Ref<Transform>& parent, const cs::String& name, const cs::Ref<Core::Faction>& faction,
			cs::List<cs::Ref<Renderer>>& into);

		/// <summary>The megastructure archetype (US-T130), in body radii, no part past 1.2: tower blocks stacked into a
		/// cluster at the heart and long docking spars out to its edge with docks along them. Laid out from
		/// <paramref name="name"/>, so each megastructure is its own and the same every time.</summary>
		static bool Megastructure(const cs::Ref<Transform>& parent, const cs::String& name, cs::List<cs::Ref<Renderer>>& into);

		/// <summary>US-T131's hook: a derelict station <paramref name="size"/> across, one of the big complexes chosen by
		/// <paramref name="seed"/>, dark and lightless, one of its modules torn off and drifting beside it. The void-content
		/// work places it (GraveyardView); this only draws it.</summary>
		static bool Derelict(const cs::Ref<Transform>& parent, int seed, float size, cs::List<cs::Ref<Renderer>>& into);

		/// <summary>Every station model the game uses, for the lineup's contact sheet.</summary>
		static cs::List<cs::String> ModelNames();

		/// <summary>US-T133: the object path of every mesh the game draws from the pack, for background streaming.</summary>
		static cs::List<cs::String> AssetPaths();
	};
}
