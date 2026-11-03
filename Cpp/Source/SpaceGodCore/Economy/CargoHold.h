#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Economy/CargoHold.cs (US-T19).

#include "Cs/Cs.h"
#include "Economy/Goods.h"
#include "Persistence/Serializable.h"

namespace SpaceGod::Core
{
	enum class ShipClass : int;

	/// <summary>
	/// Grid size of the player's personal cargo hold, per hull class. Separate from
	/// <see cref="Ship.CapacityTonnes"/>, which is the single tonnage number the AI trade loop
	/// (Economy.cs) uses for every ship and must keep meaning exactly what it already means. Keyed
	/// by <see cref="ShipClass"/> the same way <see cref="PilotTuning"/> keys arcade flight feel: how
	/// many crates physically fit is a hull-wide call, not a per-design balance knob.
	/// </summary>
	struct SPACEGODCORE_API CargoHoldSpec
	{
		int Width = 0, Height = 0;

		CargoHoldSpec() = default;
		CargoHoldSpec(int width, int height)
		{
			Width = width;
			Height = height;
		}

	private:
		static const cs::Dictionary<ShipClass, CargoHoldSpec>& ByClass();

	public:
		static CargoHoldSpec Of(ShipClass c);
	};

	/// <summary>
	/// The cell footprint one crate of a good occupies before rotation, and how many tonnes that
	/// crate holds. Bulk minerals sprawl across big cheap crates; dense, high-value goods (medical
	/// supplies, electronics, platinum) pack into a single cell, roughly tracking each good's
	/// <see cref="GoodInfo.BasePrice"/>.
	/// </summary>
	struct SPACEGODCORE_API CargoFootprint
	{
		int Width = 0, Height = 0;
		double Tonnes = 0;

		CargoFootprint() = default;
		CargoFootprint(int width, int height, double tonnes)
		{
			Width = width;
			Height = height;
			Tonnes = tonnes;
		}

	private:
		static const cs::Dictionary<Good, CargoFootprint>& ByGood();

	public:
		static CargoFootprint Of(Good g);
	};

	/// <summary>One crate placed in a hold: a good occupying a rectangle of cells with its top-left
	/// corner at (X, Y), optionally rotated 90 degrees (swapping its footprint's width and height).</summary>
	class SPACEGODCORE_API CargoItem final
	{
	public:
		::SpaceGod::Core::Good Good = static_cast<::SpaceGod::Core::Good>(0);
		int X = 0, Y = 0;
		bool Rotated = false;

		/// <summary>Bought at a pirate haven: stolen goods, contraband to any lawful patrol (see <see cref="Law.ContrabandIn"/>)
		/// and refused at a lawful market.</summary>
		bool Stolen = false;

		CargoItem(::SpaceGod::Core::Good good, int x, int y, bool rotated);

		double Tonnes() const;
		int Width() const;
		int Height() const;
	private:
		CargoItem() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(CargoItem); // ObjectGraph registration (Persistence/Registrations)
	};

	/// <summary>
	/// The player's personal grid-based cargo hold: a fixed W x H grid of cells that crates must
	/// physically fit into, rotation included. Entirely separate from the single-slot
	/// <see cref="Ship.CapacityTonnes"/>/<see cref="Ship.CargoGood"/>/<see cref="Ship.CargoTonnes"/>
	/// fields every AI trader reads and writes in Economy.cs -- this hold exists only for the one
	/// ship a human is flying and never touches those fields.
	/// </summary>
	class SPACEGODCORE_API CargoHold final
	{
	public:
		int Width = 0, Height = 0;

	private:
		cs::List<cs::Ref<CargoItem>> items = cs::List<cs::Ref<CargoItem>>();

	public:
		const cs::List<cs::Ref<CargoItem>>& Items() const { return items; }

		CargoHold(int width, int height);

		explicit CargoHold(CargoHoldSpec spec);

		/// <summary>Whether a w x h footprint at (x, y) lands fully on the grid and clear of every other
		/// crate. <paramref name="ignore"/> excludes one item from the overlap check, so a crate can be
		/// tested against a new position that overlaps its own current cells.</summary>
		bool Fits(int x, int y, int w, int h, const cs::Ref<CargoItem>& ignore = nullptr) const;

		bool Fits(Good good, int x, int y, bool rotated, const cs::Ref<CargoItem>& ignore = nullptr) const;

		/// <summary>Places a new crate at an explicit position/rotation; null (hold left unchanged) if it
		/// doesn't fit.</summary>
		cs::Ref<CargoItem> Add(Good good, int x, int y, bool rotated);

		bool Remove(const cs::Ref<CargoItem>& item);

		/// <summary>How many more crates of this good the free cells take, packed first-fit as AddFirstFit would.</summary>
		int CrateRoom(Good good) const;

		/// <summary>The tonnes of this good the free cells carry: free crates x the crate's tonnes.</summary>
		double RoomTonnes(Good good) const;

		/// <summary>Scans the grid row-major, unrotated then rotated, for the first cell a new crate of
		/// this good fits in, and places it there. Null if the hold has no room left for it.</summary>
		cs::Ref<CargoItem> AddFirstFit(Good good);

		/// <summary>Moves an existing crate to a new position/rotation, rejecting overlap or going out of
		/// bounds; on failure the crate stays exactly where it was.</summary>
		bool Move(const cs::Ref<CargoItem>& item, int newX, int newY, bool newRotated);
	private:
		CargoHold() = default; // ObjectGraph loads (docs/PERSISTENCE.md): no side effects
		SG_SERIALIZABLE(CargoHold); // ObjectGraph registration (Persistence/Registrations)
	};
}
