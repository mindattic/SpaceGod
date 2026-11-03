// Port of Runtime/UnitySpaceApp.Hauler.cs (declarations in UnitySpaceApp.Hauler.inl).

#include "UnitySpaceApp.h"

#include "Combat/FlightModels.h"
#include "Combat/HaulerFlight.h"
#include "Combat/PilotFlight.h"
#include "Economy/Economy.h"
#include "Economy/Settlement.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	bool UnitySpaceApp::PortApproach()
	{
		PilotInPortApproach = false;
		for (const auto& s : Economy()->At(pilotFrameBody))
			if (NearPort(s))
			{
				PilotInPortApproach = true;
				break;
			}
		return PilotInPortApproach;
	}

	void UnitySpaceApp::HaulerReadout(const cs::Ref<IFlightModel>& model, const HullDynamics& hull, const PilotInput& input)
	{
		auto hauler = cs::As<HaulerFlightModel>(model);
		PilotStop = hauler != nullptr ? cs::Nullable<HaulerStop>(hauler->StopPoint(pilot(), hull, input)) : cs::Nullable<HaulerStop>();
		PilotDocking = hauler != nullptr && HaulerFlightModel::Docking(input);
	}
}
