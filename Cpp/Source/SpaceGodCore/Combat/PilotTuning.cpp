#include "Combat/PilotTuning.h"
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
		static const cs::Dictionary<ShipClass, PilotTuning> byClass = []
		{
			cs::Dictionary<ShipClass, PilotTuning> d;
			// The Courier (U.SPC-T444): a blockade runner, not a dogfighter. The highest top speed and the
			// longest boost in the game and the fastest cruise spool, on a turn wider than any strike craft's.
			d[ShipClass::Courier] = PilotTuning(0.52, 120, 2.4, 45, 10, 1.2);
			// Star fighters: Everspace 2-paced, a full about-face in a second or so. Cruise spools up
			// fast too -- small, nimble hulls should feel quick to get going in open space.
			// The heavy fighter (U.SPC-T467): a light fighter's pace less a little, stiffer in the turn, a longer boost.
			d[ShipClass::Gunship] = PilotTuning(0.4, 115, 2.0, 20, 9, 2.2);
			// The space superiority fighter, the dedicated dogfighter: trades a hair of the Courier's top
			// speed for a turn only the interceptor beats.
			d[ShipClass::Fighter] = PilotTuning(0.42, 165, 2.3, 15, 9, 1.8);
			// The interceptor (U.SPC-T484): the fastest strike craft and the tightest turn in the game, on a
			// short boost -- it runs fighters down. The bomber: slow, steady, a short boost to get clear.
			d[ShipClass::Interceptor] = PilotTuning(0.48, 180, 2.4, 12, 9, 1.6);
			d[ShipClass::Bomber] = PilotTuning(0.3, 85, 1.6, 12, 8, 2.4);
			// The recon fighter (U.SPC-T485): an interceptor's pace on a longer boost, a little wider in the turn for its pods.
			d[ShipClass::Recon] = PilotTuning(0.46, 160, 2.4, 16, 9, 1.6);
			// The boarding shuttle (U.SPC-T486): slower and heavier on the stick than a bomber, built to come alongside gently.
			d[ShipClass::AssaultShuttle] = PilotTuning(0.24, 60, 1.4, 10, 8, 2.6);
			// A truss freighter (U.SPC-T482): a weak drive for its mass, so its cruise drive takes twice a hauler's time to spool.
			d[ShipClass::Freighter] = PilotTuning(0.18, 75, 1.6, 36, 8, 6.0);
			d[ShipClass::Miner] = PilotTuning(0.16, 70, 1.6, 38, 8, 3.0);
			// The deep-space explorer (U.SPC-T419): handier than a hauler, nothing like a fighter.
			d[ShipClass::Explorer] = PilotTuning(0.22, 60, 1.6, 26, 8, 3.0);
			// The ore tug (U.SPC-T483): flown like a hauler, a touch heavier on the stick.
			d[ShipClass::OreTug] = PilotTuning(0.14, 60, 1.5, 40, 8, 3.2);
			// The capture tug (U.SPC-T522): an ore tug's controls on a lighter frame, handier for matching a tumbling rock.
			d[ShipClass::CaptureTug] = PilotTuning(0.18, 65, 1.5, 34, 8, 3.0);
			// The lane tender (U.SPC-T481): a work boat, steadier than an explorer and slower to spool.
			d[ShipClass::LaneTender] = PilotTuning(0.18, 55, 1.5, 30, 7, 3.5);
			// The gate picket (U.SPC-T480): a small hull, flown nose-first like a heavy fighter but no dogfighter.
			d[ShipClass::GatePicket] = PilotTuning(0.3, 80, 1.8, 22, 8, 2.6);

			// Capital ships: still weighty -- you're captaining a warship, not dogfighting in it --
			// but sharper than their AI-battle turn rates so they're not sluggish to the point of
			// being unplayable. Their huge pitch/yaw inertia is what sells the weight: the nose takes
			// real time to start swinging. Cruise ramps take longest here.
			d[ShipClass::Carrier] = PilotTuning(0.07, 8, 1.3, 30, 5, 5.0);
			d[ShipClass::Battleship] = PilotTuning(0.09, 10, 1.3, 32, 5, 4.5);
			d[ShipClass::Cruiser] = PilotTuning(0.14, 16, 1.4, 28, 6, 4.0);
			// Destroyer stays the nose-forward skirmisher (see Battle.BroadsideFacing).
			d[ShipClass::Destroyer] = PilotTuning(0.24, 28, 1.5, 25, 7, 3.5);
			// The command cruiser (U.SPC-T477): a battlecruiser, between the cruiser and the battleship at the helm.
			d[ShipClass::CommandCruiser] = PilotTuning(0.12, 14, 1.35, 30, 5.5, 4.5);
			// The frigate (U.SPC-T475): the fastest warship hull, quick to flip end for end, with a long, hard boost for its runs.
			d[ShipClass::Frigate] = PilotTuning(0.38, 40, 1.6, 30, 8, 3.0);
			// The corvette (U.SPC-T476): a patrol hull, handier than a destroyer, for running down a smuggler in its system.
			d[ShipClass::Corvette] = PilotTuning(0.3, 34, 1.5, 26, 7.5, 3.2);
			// The gate breaker (U.SPC-T478): a battleship's helm under more plate, slower and stiffer still.
			d[ShipClass::GateBreaker] = PilotTuning(0.08, 9, 1.25, 30, 4.5, 4.5);
			return d;
		}();
		return byClass;
	}

	PilotTuning PilotTuning::Of(ShipClass c)
	{
		PilotTuning t;
		return ByClass().TryGetValue(c, t) ? t : PilotTuning(0.1, 20, 1.3, 20, 6, 3.0);
	}

	PilotControl PilotTuning::ControlOf(ShipClass c)
	{
		switch (c)
		{
			case ShipClass::Fighter:
			case ShipClass::Interceptor:
			case ShipClass::Recon:
			case ShipClass::Courier:
				return PilotControl::FreeAim;
			case ShipClass::Gunship:
			case ShipClass::Bomber:
			case ShipClass::GatePicket:
				return PilotControl::DirectNose;
			case ShipClass::Freighter:
			case ShipClass::Miner:
			case ShipClass::OreTug:
			case ShipClass::CaptureTug:
			case ShipClass::LaneTender:
			case ShipClass::Explorer:
				return PilotControl::Hauler;
			default:
				return PilotControl::Helm;
		}
	}
}
