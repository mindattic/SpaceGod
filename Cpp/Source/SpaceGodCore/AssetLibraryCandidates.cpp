// Port of Unity.SpaceGod Core/AssetLibraryCandidates.cs (see docs/MIGRATION.md).

#include "AssetLibraryCandidates.h"

namespace SpaceGod::Core
{
	const char* ToString(CandidateRole v)
	{
		switch (v)
		{
		case CandidateRole::FlightAndWeapons: return "FlightAndWeapons";
		case CandidateRole::HardpointBuilding: return "HardpointBuilding";
		case CandidateRole::StaticStructures: return "StaticStructures";
		}
		return "";
	}

	const char* ToString(Compatibility v)
	{
		switch (v)
		{
		case Compatibility::Unchecked: return "Unchecked";
		case Compatibility::Works: return "Works";
		case Compatibility::Fails: return "Fails";
		}
		return "";
	}

	AssetCandidate::AssetCandidate(const cs::String& id, const cs::String& name, CandidateRole role, const cs::String& purpose,
		Compatibility unity6, Compatibility urp, Compatibility hdrp)
	{
		Id = id;
		Name = name;
		Role = role;
		Purpose = purpose;
		DemoScenePath = cs::Cat("Assets/AssetLibraryPoc/", id, "/", id, "Demo.unity");
		Unity6 = unity6;
		Urp = urp;
		Hdrp = hdrp;
	}

	const cs::List<cs::Ref<AssetCandidate>>& AssetLibraryCandidates::All()
	{
		static const cs::List<cs::Ref<AssetCandidate>> value =
		{
			cs::New<AssetCandidate>("SpaceCombatKit", "Space Combat Kit", CandidateRole::FlightAndWeapons,
				"Ship flight model and weapons."),
			cs::New<AssetCandidate>("IngameSpaceshipBuilder", "Ingame Spaceship Builder", CandidateRole::HardpointBuilding,
				"Snap-together ship building with hardpoints."),
			cs::New<AssetCandidate>("ModularStarshipConstructor", "Modular Starship Constructor", CandidateRole::HardpointBuilding,
				"Alternative to Ingame Spaceship Builder for snap-together hardpoint building."),
			cs::New<AssetCandidate>("SpaceSatelliteKit", "Space Satellite Kit", CandidateRole::StaticStructures,
				"Satellites as static structures."),
			cs::New<AssetCandidate>("ModularSpaceStation", "Modular Space Station", CandidateRole::StaticStructures,
				"Space stations as static structures."),
		};
		return value;
	}

	cs::List<cs::Ref<AssetCandidate>> AssetLibraryCandidates::For(CandidateRole role)
	{
		// C# yields lazily; nothing depends on that, so the list is built eagerly.
		cs::List<cs::Ref<AssetCandidate>> result;
		for (const auto& c : All()) if (c->Role == role) result.Add(c);
		return result;
	}
}
