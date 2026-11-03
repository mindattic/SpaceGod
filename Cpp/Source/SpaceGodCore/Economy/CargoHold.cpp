// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/CargoHold.cs (US-T19).

#include "Economy/CargoHold.h"
#include "Ships.h"
#include "Combat/BulkFreighters.h"

namespace SpaceGod::Core
{
	const cs::Dictionary<ShipClass, CargoHoldSpec>& CargoHoldSpec::ByClass()
	{
		static const cs::Dictionary<ShipClass, CargoHoldSpec> byClass = []
		{
			cs::Dictionary<ShipClass, CargoHoldSpec> d;
			d[ShipClass::Courier] = CargoHoldSpec(3, 3);
			d[ShipClass::Fighter] = CargoHoldSpec(2, 2);
			d[ShipClass::Gunship] = CargoHoldSpec(3, 2);
			d[ShipClass::Interceptor] = CargoHoldSpec(1, 2);
			d[ShipClass::Bomber] = CargoHoldSpec(3, 2);
			d[ShipClass::Recon] = CargoHoldSpec(1, 2);
			// Its troop bay is for marines (U.SPC-T486): a locker for their kit.
			d[ShipClass::AssaultShuttle] = CargoHoldSpec(3, 2);
			d[ShipClass::GatePicket] = CargoHoldSpec(3, 2);
			// A truss freighter's hold is its pods (U.SPC-T482): six cells wide, a row for each of its stock design's eight pods.
			d[ShipClass::Freighter] = CargoHoldSpec(6, BulkFreighters::StockPods * BulkFreighters::PodRows);
			d[ShipClass::Miner] = CargoHoldSpec(7, 6);
			// An ore tug carries nothing aboard: its load is held in its claws (U.SPC-T483). A locker for the crew's kit.
			d[ShipClass::OreTug] = CargoHoldSpec(2, 2);
			// So does a capture tug (U.SPC-T522).
			d[ShipClass::CaptureTug] = CargoHoldSpec(2, 2);
			d[ShipClass::Destroyer] = CargoHoldSpec(6, 5);
			// Magazines, not holds (U.SPC-T475): a frigate's stores fit a locker.
			d[ShipClass::Frigate] = CargoHoldSpec(4, 3);
			// An impound hold for what its patrol seizes (U.SPC-T476).
			d[ShipClass::Corvette] = CargoHoldSpec(5, 4);
			d[ShipClass::Cruiser] = CargoHoldSpec(7, 6);
			d[ShipClass::CommandCruiser] = CargoHoldSpec(8, 6);
			d[ShipClass::Battleship] = CargoHoldSpec(9, 7);
			// Its siege bay's stores take the room a battleship keeps for a hold (U.SPC-T478).
			d[ShipClass::GateBreaker] = CargoHoldSpec(7, 6);
			d[ShipClass::Carrier] = CargoHoldSpec(10, 8);
			return d;
		}();
		return byClass;
	}

	CargoHoldSpec CargoHoldSpec::Of(ShipClass c)
	{
		CargoHoldSpec s;
		return ByClass().TryGetValue(c, s) ? s : CargoHoldSpec(5, 5);
	}

	const cs::Dictionary<Good, CargoFootprint>& CargoFootprint::ByGood()
	{
		static const cs::Dictionary<Good, CargoFootprint> byGood = []
		{
			cs::Dictionary<Good, CargoFootprint> d;
			d[Good::Food] = CargoFootprint(2, 2, 20);
			d[Good::Water] = CargoFootprint(2, 2, 20);
			d[Good::MedicalSupplies] = CargoFootprint(1, 1, 5);
			d[Good::HeliumThree] = CargoFootprint(2, 1, 12);
			d[Good::NickelIron] = CargoFootprint(3, 3, 60);
			d[Good::Copper] = CargoFootprint(2, 2, 25);
			d[Good::Zinc] = CargoFootprint(2, 2, 25);
			d[Good::Platinum] = CargoFootprint(1, 1, 4);
			d[Good::RareEarths] = CargoFootprint(1, 1, 8);
			d[Good::Titanium] = CargoFootprint(2, 2, 22);
			d[Good::Hydrocarbons] = CargoFootprint(2, 1, 15);
			d[Good::Nitrogen] = CargoFootprint(2, 1, 15);
			d[Good::Alloys] = CargoFootprint(2, 1, 16);
			d[Good::Electronics] = CargoFootprint(1, 1, 5);
			d[Good::Machinery] = CargoFootprint(2, 2, 18);
			d[Good::Munitions] = CargoFootprint(1, 2, 10);
			return d;
		}();
		return byGood;
	}

	CargoFootprint CargoFootprint::Of(Good g)
	{
		CargoFootprint f;
		return ByGood().TryGetValue(g, f) ? f : CargoFootprint(2, 2, 20);
	}

	CargoItem::CargoItem(::SpaceGod::Core::Good good, int x, int y, bool rotated)
	{
		Good = good;
		X = x;
		Y = y;
		Rotated = rotated;
	}

	double CargoItem::Tonnes() const { return CargoFootprint::Of(Good).Tonnes; }
	int CargoItem::Width() const { return Rotated ? CargoFootprint::Of(Good).Height : CargoFootprint::Of(Good).Width; }
	int CargoItem::Height() const { return Rotated ? CargoFootprint::Of(Good).Width : CargoFootprint::Of(Good).Height; }

	CargoHold::CargoHold(int width, int height)
	{
		Width = width;
		Height = height;
	}

	CargoHold::CargoHold(CargoHoldSpec spec) : CargoHold(spec.Width, spec.Height) {}

	bool CargoHold::Fits(int x, int y, int w, int h, const cs::Ref<CargoItem>& ignore) const
	{
		if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > Width || y + h > Height) return false;
		for (const auto& item : items)
		{
			if (item == ignore) continue;
			if (x < item->X + item->Width() && item->X < x + w && y < item->Y + item->Height() && item->Y < y + h) return false;
		}
		return true;
	}

	bool CargoHold::Fits(Good good, int x, int y, bool rotated, const cs::Ref<CargoItem>& ignore) const
	{
		auto f = CargoFootprint::Of(good);
		int w = rotated ? f.Height : f.Width;
		int h = rotated ? f.Width : f.Height;
		return Fits(x, y, w, h, ignore);
	}

	cs::Ref<CargoItem> CargoHold::Add(Good good, int x, int y, bool rotated)
	{
		if (!Fits(good, x, y, rotated)) return nullptr;
		auto item = cs::New<CargoItem>(good, x, y, rotated);
		items.Add(item);
		return item;
	}

	bool CargoHold::Remove(const cs::Ref<CargoItem>& item) { return items.Remove(item); }

	int CargoHold::CrateRoom(Good good) const
	{
		auto trial = CargoHold(Width, Height);
		for (const auto& item : items) trial.items.Add(cs::New<CargoItem>(item->Good, item->X, item->Y, item->Rotated));
		int n = 0;
		while (trial.AddFirstFit(good) != nullptr) n++;
		return n;
	}

	double CargoHold::RoomTonnes(Good good) const { return CrateRoom(good) * CargoFootprint::Of(good).Tonnes; }

	cs::Ref<CargoItem> CargoHold::AddFirstFit(Good good)
	{
		auto f = CargoFootprint::Of(good);
		for (bool rotated : { false, true })
		{
			int w = rotated ? f.Height : f.Width;
			int h = rotated ? f.Width : f.Height;
			for (int y = 0; y <= Height - h; y++)
				for (int x = 0; x <= Width - w; x++)
					if (Fits(x, y, w, h))
						return Add(good, x, y, rotated);
		}
		return nullptr;
	}

	bool CargoHold::Move(const cs::Ref<CargoItem>& item, int newX, int newY, bool newRotated)
	{
		if (!Fits(item->Good, newX, newY, newRotated, item)) return false;
		item->X = newX;
		item->Y = newY;
		item->Rotated = newRotated;
		return true;
	}
}
