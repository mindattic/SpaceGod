#pragma once
// US-T146: the player's side of the gas works (Core's GasHarvest): the embodied unit taking and working a protection
// contract, the waves spawned beside it, raiding a skimmer, and the dev menu's and console's hooks (sg.Harvest).

#include "Cs/Cs.h"

namespace SpaceGod
{
	class UnitySpaceApp;
}

namespace SpaceGod::HarvestPlay
{
	/// <summary>A hull raids a skimmer's tanks within this of it (true km; the platforms race round in low orbit).</summary>
	constexpr double RaidKm = 1500;

	/// <summary>
	/// Once a frame (HarvesterView's update): where the embodied unit is (Core's guard check), the waves its contract has
	/// due spawned beside it (raiders, ghosts or a hostile navy's hulls into a fight with it, or a Bio Mass globule), the
	/// keys at a helm by a giant (Home takes the protection contract posted there, Shift+Home raids the nearest skimmer),
	/// and a toast as its contract ends.
	/// </summary>
	void Tick(UnitySpaceApp& app);

	/// <summary>Takes the best contract posted at the giant the hull flown is at. False with why in <paramref name="said"/>.</summary>
	bool TakeHere(UnitySpaceApp& app, cs::String& said);

	/// <summary>The dev menu's: at a helm, guard the nearest skimmer whose owner will hire you (taken there first if you
	/// are not at its giant), an offer posted for it if none is, the first wave <paramref name="firstWaveS"/> seconds off.</summary>
	bool StartContract(UnitySpaceApp& app, double firstWaveS, cs::String& said);

	/// <summary>Brings the held contract's next wave now.</summary>
	bool WaveNow(UnitySpaceApp& app, cs::String& said);

	/// <summary>Raids the nearest skimmer's tanks (within <see cref="RaidKm"/>) with the hull flown.</summary>
	bool RaidHere(UnitySpaceApp& app, cs::String& said);
}
