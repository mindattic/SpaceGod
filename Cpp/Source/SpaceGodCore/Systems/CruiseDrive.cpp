// Port of Assets/UnitySpace/Core/Systems/CruiseDrive.cs (US-T17).

#include "Systems/CruiseDrive.h"
#include "Systems/CsGaps.h"
#include "Systems/FtlRules.h"
#include "Systems/Galaxy.h"
#include "Systems/JumpGate.h"
#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/Facilities.h"
#include "Combat/Outfitting.h"
#include "Settings/GameSetting.h"

namespace SpaceGod::Core
{
	// ================================================================ CruiseDriveSpec

	double CruiseDriveSpec::SpoolDays() const { return RatedSpoolDays * FtlRules::SpoolScale()->Value; }

	double CruiseDriveSpec::CooldownDays() const { return RatedCooldownDays * FtlRules::CooldownScale()->Value; }

	CruiseDriveSpec::CruiseDriveSpec(const cs::String& name, double cruiseC, double spoolDays, double rampDays, double cooldownDays,
		double chargePropellantShare, double propellantSharePerLy)
	{
		if (!(cruiseC > 0)) throw cs::ArgumentOutOfRangeException("cruiseC: A cruise drive must move: " + name);
		Name = name;
		CruiseC = cruiseC;
		RatedSpoolDays = cs::Math::Max(0.0, spoolDays);
		RampDays = cs::Math::Max(1e-6, rampDays);
		RatedCooldownDays = cs::Math::Max(0.0, cooldownDays);
		ChargePropellantShare = cs::Math::Max(0.0, chargePropellantShare);
		PropellantSharePerLy = cs::Math::Max(0.0, propellantSharePerLy);
	}

	double CruiseDriveSpec::LyPerDay() const { return CruiseC * CruiseDrives::SpeedScale()->Value / (Astro::DaysPerCentury / 100); }

	// ================================================================ CruiseProfile

	CruiseProfile::CruiseProfile(double ly, double lyPerDay, double rampDays, double spoolDays)
	{
		Ly = cs::Math::Max(0.0, ly);
		LyPerDay = cs::Math::Max(1e-12, lyPerDay);
		RampDays = cs::Math::Max(1e-9, rampDays);
		SpoolDays = cs::Math::Max(0.0, spoolDays);
	}

	CruiseProfile CruiseProfile::For(const cs::Ref<CruiseDriveSpec>& drive, double ly)
	{
		// C# evaluates the arguments left to right (each reads a setting; none has side effects, kept in order anyway).
		double lyPerDay = drive->LyPerDay();
		double rampDays = drive->RampDays;
		double spoolDays = drive->SpoolDays();
		return CruiseProfile(ly, lyPerDay, rampDays, spoolDays);
	}

	double CruiseProfile::LyAt(double days) const
	{
		double t = days - SpoolDays;
		if (t <= 0) return 0;
		double total = FlightDays();
		if (t >= total) return Ly;
		double a = Accel();
		if (Short())
		{
			double half = total / 2;
			if (t <= half) return 0.5 * a * t * t;
			double r = total - t;
			return Ly - 0.5 * a * r * r;
		}
		if (t <= RampDays) return 0.5 * a * t * t;
		double coastEnd = total - RampDays;
		if (t <= coastEnd) return 0.5 * LyPerDay * RampDays + LyPerDay * (t - RampDays);
		double left = total - t;
		return Ly - 0.5 * a * left * left;
	}

	double CruiseProfile::SpeedAt(double days) const
	{
		double t = days - SpoolDays;
		double total = FlightDays();
		if (t <= 0 || t >= total) return 0;
		double a = Accel();
		double peak = Short() ? a * total / 2 : LyPerDay;
		return cs::Math::Min(peak, cs::Math::Min(a * t, a * (total - t)));
	}

	const char* ToString(FtlAccess v)
	{
		switch (v)
		{
		case FtlAccess::Own: return "Own";
		case FtlAccess::Fitted: return "Fitted";
		case FtlAccess::Gates: return "Gates";
		case FtlAccess::Hangar: return "Hangar";
		case FtlAccess::HomeSystem: return "HomeSystem";
		}
		return "";
	}

	// ================================================================ FtlRoster

	cs::Dictionary<ShipClass, FtlAccess>& FtlRoster::byClass()
	{
		static cs::Dictionary<ShipClass, FtlAccess> map = []
		{
			cs::Dictionary<ShipClass, FtlAccess> m;
			m[ShipClass::Carrier] = FtlAccess::Own;
			m[ShipClass::Explorer] = FtlAccess::Own;
			// The gate breaker (U.SPC-T478) always has its own: it attacks the gates the rest of the fleet would use.
			m[ShipClass::GateBreaker] = FtlAccess::Own;
			m[ShipClass::Battleship] = FtlAccess::Fitted;
			m[ShipClass::CommandCruiser] = FtlAccess::Fitted;
			m[ShipClass::Cruiser] = FtlAccess::Gates;
			m[ShipClass::Destroyer] = FtlAccess::Gates;
			// No drive of its own (U.SPC-T475): the gates, or clamped to a carrier's hull (FtlCarriage).
			m[ShipClass::Frigate] = FtlAccess::Gates;
			// System defence (U.SPC-T476): no drive, no gate, no carriage; it never leaves its home system.
			m[ShipClass::Corvette] = FtlAccess::HomeSystem;
			m[ShipClass::Fighter] = FtlAccess::Hangar;
			m[ShipClass::Interceptor] = FtlAccess::Hangar;
			m[ShipClass::Bomber] = FtlAccess::Hangar;
			m[ShipClass::Recon] = FtlAccess::Hangar;
			// A boarding shuttle (U.SPC-T486) rides in a carrier's hangar.
			m[ShipClass::AssaultShuttle] = FtlAccess::Hangar;
			return m;
		}();
		return map;
	}

	FtlAccess FtlRoster::Of(ShipClass cls)
	{
		FtlAccess a;
		return byClass().TryGetValue(cls, a) ? a : CruiseDrives::Of(cls) != nullptr ? FtlAccess::Own : FtlAccess::Gates;
	}

	bool FtlRoster::DriveFitted(const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr || ship->Spec == nullptr) return false;
		if (ship->Loadout != nullptr) return ship->Loadout->CountFacility(FacilityKinds::FtlDrive) > 0;
		for (const auto& f : FacilityLayouts::For(ship->Spec->Class))
			if (EqualsNoCase(f->Kind, FacilityKinds::FtlDrive)) return true;
		return false;
	}

	cs::String FtlRoster::NoDriveReason(const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr || ship->Spec == nullptr) return "No ship";
		if (CruiseDrives::Of(ship) != nullptr) return cs::String(); // null
		auto classSpec = ShipClassSpec::Of(ship->Spec->Class);
		cs::String cls = classSpec != nullptr && !classSpec->DisplayName.empty() ? cs::Strings::ToLowerInvariant(classSpec->DisplayName) : cs::Strings::ToLowerInvariant(ToString(ship->Spec->Class));
		switch (Of(ship->Spec->Class))
		{
		case FtlAccess::Fitted:
			return "The " + ship->Name + " has no FTL drive fitted: a yard can fit one into its free space; until then the jump gates are its way";
		case FtlAccess::Hangar:
			return "A " + cls + " carries no FTL drive: it crosses between the stars in a carrier's hangar, or through the jump gates";
		case FtlAccess::HomeSystem:
			return "A " + cls + " carries no FTL drive and never leaves its home system";
		default:
			return FtlCarriage::RidesClamped(ship->Spec->Class)
				? "A " + cls + " carries no FTL drive of its own: the jump gates and lanes are its way, or clamped to a carrier's hull"
				: "A " + cls + " carries no FTL drive of its own: the jump gates and lanes are its way, or docked aboard a carrier";
		}
	}

	// ================================================================ CruiseDrives

	const cs::Ref<CruiseDriveSpec>& CruiseDrives::Capital()
	{
		static const cs::Ref<CruiseDriveSpec> d = cs::New<CruiseDriveSpec>("Capital cruise drive", (Astro::DaysPerCentury / 100), 0.25, 0.1,
			0.5, 0.02, 0.002);
		return d;
	}

	const cs::Ref<CruiseDriveSpec>& CruiseDrives::Survey()
	{
		static const cs::Ref<CruiseDriveSpec> d = cs::New<CruiseDriveSpec>("Survey cruise drive", 2 * (Astro::DaysPerCentury / 100), 0.1, 0.05,
			0.25, 0.01, 0.001);
		return d;
	}

	cs::Dictionary<ShipClass, cs::Ref<CruiseDriveSpec>>& CruiseDrives::byClass()
	{
		static cs::Dictionary<ShipClass, cs::Ref<CruiseDriveSpec>> map = []
		{
			cs::Dictionary<ShipClass, cs::Ref<CruiseDriveSpec>> m;
			m[ShipClass::Carrier] = Capital();
			// Only a battleship (or command cruiser) whose design fits the drive facility carries it (FtlAccess.Fitted, U.SPC-T472).
			m[ShipClass::Battleship] = Capital();
			// A command cruiser, like a battleship, only when its design fits the drive facility (U.SPC-T477, U.SPC-T472).
			m[ShipClass::CommandCruiser] = Capital();
			// The gate breaker's own capital drive, built into its engineering (U.SPC-T478).
			m[ShipClass::GateBreaker] = Capital();
			m[ShipClass::Explorer] = Survey();
			return m;
		}();
		return map;
	}

	void CruiseDrives::Register(ShipClass cls, const cs::Ref<CruiseDriveSpec>& drive)
	{
		if (drive == nullptr) byClass().Remove(cls);
		else byClass()[cls] = drive;
	}

	cs::Ref<CruiseDriveSpec> CruiseDrives::Of(ShipClass cls)
	{
		cs::Ref<CruiseDriveSpec> d;
		return byClass().TryGetValue(cls, d) ? d : nullptr;
	}

	cs::Ref<CruiseDriveSpec> CruiseDrives::Of(const cs::Ref<Ship>& ship)
	{
		if (ship == nullptr || ship->Spec == nullptr) return nullptr;
		auto drive = Of(ship->Spec->Class);
		if (drive == nullptr) return nullptr;
		switch (FtlRoster::Of(ship->Spec->Class))
		{
		case FtlAccess::Own: return drive;
		case FtlAccess::Fitted: return FtlRoster::DriveFitted(ship) ? drive : nullptr;
		default: return nullptr;
		}
	}

	const cs::Ref<RangeSetting>& CruiseDrives::SpeedScale()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("ftl.cruiseSpeedScale", SettingGroups::Ftl,
			"Cruise drive top speed", 1, 0.25, 50, 0.25,
			cs::String("How fast every cruise drive crosses between the stars, as a multiple of its rated speed (a capital drive is rated at a light-year a day: ") +
			"the galaxy's width in some 270 years; at the top of the range, in about five). Applies to trips set out on from now.",
			[](double v) { return cs::Fmt(v, "0.##") + "x"; }, "ftl interstellar cruise speed drive");
		return setting;
	}

	const cs::Ref<RangeSetting>& CruiseDrives::WellLimit()
	{
		static const cs::Ref<RangeSetting> setting = cs::New<RangeSetting>("ftl.wellLimitAu", SettingGroups::Ftl,
			"Gravity well limit", 2, 0.25, 20, 0.25,
			cs::String("How far from a star (AU) a cruise drive must be to engage, and where it drops out on arrival, the rest of the way flown under ") +
			"the ordinary drive. Applies to trips set out on from now.",
			[](double v) { return cs::Fmt(v, "0.##") + " AU"; }, "ftl well drop out gravity");
		return setting;
	}

	double CruiseDrives::WellLimitKm(const cs::Ref<StarSystem>& system)
	{
		return cs::Math::Max(WellLimit()->Value * Astro::AuKm, system == nullptr ? 0 : (system->Catalog->Root()->RadiusKm + JumpGate::RadiusKm) * 4);
	}

	// ================================================================ FtlCarriage

	cs::Dictionary<ShipClass, int>& FtlCarriage::berths()
	{
		static cs::Dictionary<ShipClass, int> map = []
		{
			cs::Dictionary<ShipClass, int> m;
			m[ShipClass::Carrier] = 12;
			m[ShipClass::Battleship] = 4;
			m[ShipClass::CommandCruiser] = 3;
			// Its shuttle deck (U.SPC-T478): room for its own two boarding shuttles' worth of small craft.
			m[ShipClass::GateBreaker] = 4;
			return m;
		}();
		return map;
	}

	cs::Dictionary<ShipClass, int>& FtlCarriage::sizes()
	{
		static cs::Dictionary<ShipClass, int> map = []
		{
			cs::Dictionary<ShipClass, int> m;
			m[ShipClass::Fighter] = 1;
			m[ShipClass::Interceptor] = 1;
			m[ShipClass::Bomber] = 2;
			m[ShipClass::Recon] = 1;
			m[ShipClass::AssaultShuttle] = 2;
			m[ShipClass::Courier] = 2;
			m[ShipClass::Gunship] = 2;
			return m;
		}();
		return map;
	}

	cs::Dictionary<ShipClass, int>& FtlCarriage::clamps()
	{
		static cs::Dictionary<ShipClass, int> map = []
		{
			cs::Dictionary<ShipClass, int> m;
			m[ShipClass::Carrier] = 4;
			return m;
		}();
		return map;
	}

	cs::Dictionary<ShipClass, int>& FtlCarriage::clampSizes()
	{
		static cs::Dictionary<ShipClass, int> map = []
		{
			cs::Dictionary<ShipClass, int> m;
			m[ShipClass::Destroyer] = 2;
			// A frigate rides clamped to a carrier's hull (U.SPC-T475), never in a hangar.
			m[ShipClass::Frigate] = 1;
			return m;
		}();
		return map;
	}

	void FtlCarriage::RegisterBerths(ShipClass cls, int count)
	{
		if (count <= 0) berths().Remove(cls);
		else berths()[cls] = count;
	}

	void FtlCarriage::RegisterCraft(ShipClass cls, int size)
	{
		if (size <= 0) sizes().Remove(cls);
		else sizes()[cls] = size;
	}

	void FtlCarriage::RegisterClamps(ShipClass cls, int count)
	{
		if (count <= 0) clamps().Remove(cls);
		else clamps()[cls] = count;
	}

	void FtlCarriage::RegisterClampable(ShipClass cls, int size)
	{
		if (size <= 0) clampSizes().Remove(cls);
		else clampSizes()[cls] = size;
	}

	int FtlCarriage::BerthsOf(ShipClass cls) { int n; return berths().TryGetValue(cls, n) ? n : 0; }

	int FtlCarriage::BerthsOf(const cs::Ref<Ship>& ship)
	{
		return ship == nullptr || ship->Spec == nullptr || CruiseDrives::Of(ship) == nullptr ? 0 : BerthsOf(ship->Spec->Class);
	}

	int FtlCarriage::ClampsOf(ShipClass cls) { int n; return clamps().TryGetValue(cls, n) ? n : 0; }

	int FtlCarriage::ClampsOf(const cs::Ref<Ship>& ship)
	{
		return ship == nullptr || ship->Spec == nullptr || CruiseDrives::Of(ship) == nullptr ? 0 : ClampsOf(ship->Spec->Class);
	}

	int FtlCarriage::SizeOf(ShipClass cls) { int n; return sizes().TryGetValue(cls, n) ? n : 0; }

	int FtlCarriage::ClampSizeOf(ShipClass cls) { int n; return clampSizes().TryGetValue(cls, n) ? n : 0; }

	bool FtlCarriage::Carriable(const cs::Ref<Ship>& craft)
	{
		return craft != nullptr && craft->Spec != nullptr && !craft->Destroyed && !craft->Derelict && CruiseDrives::Of(craft) == nullptr
			&& (SizeOf(craft->Spec->Class) > 0 || ClampSizeOf(craft->Spec->Class) > 0);
	}

	cs::Dictionary<cs::Ref<Ship>, cs::Ref<Ship>> FtlCarriage::Stow(const cs::List<cs::Ref<Ship>>& carriers, const cs::List<cs::Ref<Ship>>& craft)
	{
		struct Free { cs::Ref<Ship> hull; int berths = 0; int clamps = 0; };
		cs::List<Free> free;
		for (const auto& c : carriers)
		{
			int n = BerthsOf(c), k = ClampsOf(c);
			if (n > 0 || k > 0) free.Add({ c, n, k });
		}
		cs::Dictionary<cs::Ref<Ship>, cs::Ref<Ship>> aboard;
		for (const auto& s : craft)
		{
			if (!Carriable(s) || aboard.ContainsKey(s)) continue;
			bool clamped = RidesClamped(s);
			int size = clamped ? ClampSizeOf(s->Spec->Class) : SizeOf(s->Spec->Class);
			for (int i = 0; i < free.Count(); i++)
			{
				auto f = free[i];
				if (f.hull == s || (clamped ? f.clamps : f.berths) < size) continue;
				aboard[s] = f.hull;
				free[i] = clamped ? Free{ f.hull, f.berths, f.clamps - size } : Free{ f.hull, f.berths - size, f.clamps };
				break;
			}
		}
		return aboard;
	}

	int FtlCarriage::CountClamped(const cs::List<cs::Ref<Ship>>& aboard)
	{
		int n = 0;
		for (const auto& s : aboard)
			if (RidesClamped(s)) n++;
		return n;
	}

	cs::String FtlCarriage::Describe(int alongside, int docked, int clamped)
	{
		cs::List<cs::String> parts;
		if (alongside > 0) parts.Add(cs::Cat(alongside, " escort", (alongside == 1 ? "" : "s"), " alongside"));
		if (docked > 0) parts.Add(cs::Cat(docked, " craft docked aboard"));
		if (clamped > 0) parts.Add(cs::Cat(clamped, " escort", (clamped == 1 ? "" : "s"), " clamped to the hull"));
		if (parts.Count() == 0) return "";
		if (parts.Count() == 1) return ", " + parts[0];
		cs::List<cs::String> head(parts.begin(), parts.begin() + (parts.Count() - 1)); // parts.GetRange(0, Count - 1)
		return ", " + cs::Strings::Join(", ", head) + " and " + parts[parts.Count() - 1];
	}
}
