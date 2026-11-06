#include "Combat/PilotTuning.h"
#include "Combat/ShipCatalog.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	const char* ToString(PilotControl v)
	{
		switch (v)
		{
			case PilotControl::FreeAim: return "FreeAim";
			case PilotControl::DirectNose: return "DirectNose";
			case PilotControl::Hauler: return "Hauler";
			case PilotControl::Helm: return "Helm";
		}
		return "";
	}

	const cs::Dictionary<ShipClass, PilotTuning>& PilotTuning::ByClass()
	{
		// Every hull's numbers are authored once, in the ship catalog's table (US-T193, Combat/ShipCatalog.cpp): each
		// class's tuning is its row there, its roll as a factor of its turn so a flight model that narrows the turn
		// narrows the roll with it.
		static const cs::Dictionary<ShipClass, PilotTuning> byClass = []
		{
			cs::Dictionary<ShipClass, PilotTuning> d;
			for (ShipClass c : ShipCatalog::Classes())
			{
				HullHandling h;
				if (!ShipCatalog::TryHandling(c, h)) continue;
				double roll = h.TurnRateDegS > 0 ? h.RollRateDegS / h.TurnRateDegS : DefaultRollFactor;
				d[c] = PilotTuning(h.TopSpeedKmS, h.TurnRateDegS, h.BoostSpeedMultiplier, h.BoostDurationS, h.CruiseSpeedMultiplier, h.CruiseRampS, roll);
			}
			return d;
		}();
		return byClass;
	}

	PilotTuning PilotTuning::Of(ShipClass c)
	{
		PilotTuning t;
		return ByClass().TryGetValue(c, t) ? t : PilotTuning(0.1, 20, 1.3, 20, 6, 3.0);
	}

	PilotControl PilotTuning::ControlOf(ShipClass c) { return ShipCatalog::ControlOf(c); }
}
