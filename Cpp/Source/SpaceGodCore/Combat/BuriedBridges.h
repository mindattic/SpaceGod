#pragma once
// Port of Assets/UnitySpace/Core/Combat/BuriedBridges.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	class RangeSetting;
	class ShipCombat;
	class Subsystem;

	/// <summary>
	/// A warship's bridge buried at the centre of its hull (U.SPC-T492). It has no viewports, so nothing of it shows on the
	/// outside (the crew sees out through the ship's sensors and holo displays), and it is protected by the hull's full
	/// thickness: a shot aimed at it spends itself on the hull around it, and only once enough of the hull is shot away does
	/// fire get through to it (deep penetration). Collisions never reach it, and no pod, Breacher round or airlock opens into
	/// it, so boarders take it only by fighting through the spaces around it. A design buries a hardpoint with
	/// <see cref="SubsystemDef.Buried"/> and a space with <see cref="FacilitySite.Buried"/>; this class holds the figures.
	/// </summary>
	class BuriedBridges
	{
	public:
		static const cs::Ref<RangeSetting>& Depth();

		/// <summary>The share of a hit aimed at a buried hardpoint that reaches it through what is left of the hull: none while
		/// more than <see cref="Depth"/> of the hull stands, rising to all of it as the hull is shot away. 1 for anything not
		/// buried.</summary>
		static double Reach(const cs::Ref<ShipCombat>& c, const cs::Ref<Subsystem>& s);
	};
}
