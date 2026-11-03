// Port of Assets/UnitySpace/Core/Systems/GateTransit.cs (US-T17).

#include "Systems/GateTransit.h"
#include "Systems/Galaxy.h"
#include "Systems/JumpGate.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Fleet.h"
#include "Ships.h"
#include "World.h"
#include "Combat/War.h"
#include "Combat/GatePickets.h"
#include "Economy/Economy.h"

namespace SpaceGod::Core
{
	bool GateTransit::TryPerform(const cs::Ref<JumpGate>& gate, DVec3 shipHelioKm, const cs::Ref<Ship>& ship, const cs::Ref<World>& origin, double jd, cs::Ref<Ship>& arrived)
	{
		arrived = nullptr;
		if (gate == nullptr || ship == nullptr || origin == nullptr || !gate->CanActivate(shipHelioKm) || !origin->Fleet->IsPresent(ship)) return false;

		// Its orders, cooldowns and bookings here are for a hull no longer here (U.SPC-T311): the ship goes on
		// under its own steam, not a husk that stayed flagged for these books to read. Any band it led carries
		// on under the strongest of its escorts left behind (U.SPC-T310), same as a jump under its own drive;
		// the old flagship's hold goes with the hull that is actually leaving, not a stay-behind heir.
		if (origin->War) origin->War->Forget(ship);
		if (origin->Economy) origin->Economy->Release(ship);
		if (!ship->IsEscort())
		{
			bool wasDestroyed = ship->Destroyed;
			ship->Destroyed = true;
			if (origin->War) origin->War->HandOverCommand(ship);
			ship->Destroyed = wasDestroyed;
		}
		if (origin->Fleet->LetGo(ship) == nullptr) return false;
		ship->Leader = nullptr;
		ship->FormationSlot = DVec3();
		ship->Voyage = nullptr;

		auto destination = gate->Destination();
		ship->ResetForArrival();
		ship->Role = ShipRole::Free;
		ship->Ports = destination->World->FreeShipPorts();

		auto forward = gate->ArrivalForward();
		auto up = cs::Math::Abs(forward.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX;
		auto right = DVec3::Cross(forward, up).Normalized();
		auto orbit = cs::New<ParkingOrbit>();
		orbit->Body = destination->Catalog->Root()->Index;
		orbit->RadiusKm = 0;
		orbit->Center = gate->ArrivalOffsetKm();
		orbit->U = right;
		orbit->V = forward;
		orbit->EpochJd = jd;
		orbit->PhaseAtEpoch = 0;
		orbit->RateRadPerDay = 0;
		orbit->Fixed = true;
		ship->Orbit = orbit;
		ship->Phase = ShipPhase::Parked;
		ship->PhaseStartJd = jd;

		destination->World->Fleet->Receive(ship);
		// Announced like any other hull coming into the system (as gate traffic is, see InterstellarTraffic), so a
		// company hull is taken back into its company's fleet rather than dropped from it.
		destination->World->NotifyAdded(ship);
		// The far gate's pickets see it come out (U.SPC-T480).
		GatePickets::OnArrival(gate->Partner, cs::List<cs::Ref<Ship>>{ ship }, jd);
		arrived = ship;
		return true;
	}
}
