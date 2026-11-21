// US-T435: stations big enough for dozens of ships at once, and the berths that hold them.

#include "Infrastructure/Berths.h"

#include "Infrastructure/Infrastructure.h"

namespace SpaceGod::Core
{
	double Berths::LengthKmFor(StructureKind kind)
	{
		switch (kind)
		{
			case StructureKind::Habitat: return BerthTuning::HabitatKm;
			case StructureKind::Gateway: return BerthTuning::GatewayKm;
			case StructureKind::Shipyard: return BerthTuning::ShipyardKm;
			case StructureKind::Factory: return BerthTuning::FactoryKm;
			case StructureKind::Depot: return BerthTuning::DepotKm;
			case StructureKind::Relay: return BerthTuning::RelayKm;
			default: return 0;
		}
	}

	double Berths::CoreKmFor(StructureKind kind)
	{
		switch (kind)
		{
			case StructureKind::Habitat: return BerthTuning::HabitatCoreKm;
			case StructureKind::Gateway: return BerthTuning::GatewayCoreKm;
			case StructureKind::Shipyard: return BerthTuning::ShipyardCoreKm;
			case StructureKind::Factory: return BerthTuning::FactoryCoreKm;
			case StructureKind::Depot: return BerthTuning::DepotCoreKm;
			default: return 0;
		}
	}

	double Berths::SolidKm(const OrbitalStructure& s)
	{
		double core = s.Real ? 0 : CoreKmFor(s.Kind);
		return core > 0 ? cs::Math::Min(core, s.LengthKm) : s.LengthKm;
	}

	bool Berths::Spins(StructureKind kind) { return kind == StructureKind::Habitat || kind == StructureKind::Gateway; }

	bool Berths::TakesShips(StructureKind kind)
	{
		switch (kind)
		{
			case StructureKind::Habitat:
			case StructureKind::Gateway:
			case StructureKind::Shipyard:
			case StructureKind::Factory:
			case StructureKind::Depot:
				return true;
			default:
				return false;
		}
	}

	int Berths::PerPier(StructureKind kind, double lengthKm)
	{
		if (!TakesShips(kind) || lengthKm <= 0) return 0;
		if (Spins(kind))
		{
			// Along the spine from the wheel's face out to its tip, both sides of the wheel.
			int perSide = static_cast<int>(cs::Math::Floor((0.5 - 0.5 * BerthTuning::WheelShare) * lengthKm / BerthTuning::PitchKm));
			return 2 * cs::Math::Max(1, perSide);
		}
		return cs::Math::Max(1, static_cast<int>(cs::Math::Floor(BerthTuning::PierShare * lengthKm / BerthTuning::PitchKm)));
	}

	int Berths::Capacity(StructureKind kind, double lengthKm) { return BerthTuning::Piers * PerPier(kind, lengthKm); }

	int Berths::Capacity(const OrbitalStructure& s) { return s.Real ? 0 : Capacity(s.Kind, s.LengthKm); }
}
