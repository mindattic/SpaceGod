#pragma once
// Port of Unity.SpaceGod Core/AssetLibraryCandidates.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	/// <summary>What a candidate is for in the hardpoint / ship-construction decision (U.SPC-T378, part of U.SPC-T371).</summary>
	enum class CandidateRole : int
	{
		FlightAndWeapons,
		HardpointBuilding,
		StaticStructures,
	};
	const char* ToString(CandidateRole v);

	/// <summary>How sure the project is that a candidate runs on a given Unity / render pipeline.</summary>
	enum class Compatibility : int
	{
		/// <summary>Nobody has opened the package in this project yet; the store page's claim is all there is.</summary>
		Unchecked,
		Works,
		Fails,
	};
	const char* ToString(Compatibility v);

	/// <summary>
	/// One Asset Store package under evaluation. The demo scene is where a showcase goes once the package is
	/// bought and imported; the menu shows the candidate as not installed until that scene is in the build.
	/// </summary>
	class AssetCandidate final
	{
	public:
		cs::String Id;
		cs::String Name;
		CandidateRole Role{};
		cs::String Purpose;

		/// <summary>The scene the in-game demo loads, by project path.</summary>
		cs::String DemoScenePath;

		Compatibility Unity6{};
		Compatibility Urp{};
		Compatibility Hdrp{};

		AssetCandidate(const cs::String& id, const cs::String& name, CandidateRole role, const cs::String& purpose,
			Compatibility unity6 = Compatibility::Unchecked, Compatibility urp = Compatibility::Unchecked, Compatibility hdrp = Compatibility::Unchecked);

		/// <summary>True once the candidate has been checked on every one of Unity 6, URP and HDRP, which is the
		/// bar before anyone buys it for real.</summary>
		bool FullyChecked() const { return Unity6 != Compatibility::Unchecked && Urp != Compatibility::Unchecked && Hdrp != Compatibility::Unchecked; }

		static cs::String Describe(Compatibility c) { return c == Compatibility::Works ? "works" : c == Compatibility::Fails ? "fails" : "not yet checked"; }
	};

	/// <summary>The leading Asset Store candidates the proof-of-concept submenu lists.</summary>
	class AssetLibraryCandidates
	{
	public:
		static const cs::List<cs::Ref<AssetCandidate>>& All();

		/// <summary>The candidates for one role.</summary>
		static cs::List<cs::Ref<AssetCandidate>> For(CandidateRole role);
	};
}
