// Port of Unity.SpaceGod Core/PracticeSpawn.cs (US-T8).

#include "PracticeSpawn.h"
#include "Ships.h"

namespace SpaceGod::Core
{
	SpawnObstacle::SpawnObstacle(DVec3 centreKm, double radiusKm)
	{
		CentreKm = centreKm;
		RadiusKm = cs::Math::Max(0.0, radiusKm);
	}

	/// <summary>Whether <paramref name="ship"/> is a practice fighter.</summary>
	// C#'s `ship?.Name != null`: a C++ string is never null, so only the ship is tested.
	bool PracticeSpawn::Is(const cs::Ref<Ship>& ship) { return ship != nullptr && cs::Strings::EndsWith(ship->Name, Name); }

	double PracticeSpawn::GapKm(double selfLengthKm) { return cs::Math::Max(0.05, selfLengthKm * 3); }

	DVec3 PracticeSpawn::Clear(DVec3 atKm, DVec3 forward, double selfLengthKm, const cs::List<SpawnObstacle>& obstacles)
	{
		// (C#'s `obstacles == null` has no C++ counterpart: an IReadOnlyList parameter is a reference here.)
		if (obstacles.Count() == 0) return atKm;
		double self = cs::Math::Max(0.0, selfLengthKm) * 0.5;
		double gap = GapKm(selfLengthKm);
		auto side = Side(forward);
		auto p = atKm;
		for (int n = 0; n < MaxNudges; n++)
		{
			int worst = -1;
			double worstDepth = 0;
			for (int i = 0; i < obstacles.Count(); i++)
			{
				const auto& o = obstacles[i];
				double need = o.RadiusKm + self + gap;
				double depth = need - (p - o.CentreKm).Length();
				if (depth > worstDepth)
				{
					worstDepth = depth;
					worst = i;
				}
			}
			if (worst < 0) return p;
			const auto& w = obstacles[worst];
			auto away = p - w.CentreKm;
			auto dir = away.LengthSquared() > 1e-18 ? away.Normalized() : side;
			// A hair past the edge, so a spot squeezed between two things is not nudged back and forth forever.
			p = w.CentreKm + dir * ((w.RadiusKm + self + gap) * (1 + 1e-6) + 1e-6);
			// Squeezed between things: each further nudge also steps sideways, out of the crowd.
			if (n >= 8) p = p + side * (gap * (n - 7));
		}
		return p;
	}

	DVec3 PracticeSpawn::Side(DVec3 forward)
	{
		auto f = forward.LengthSquared() > 1e-12 ? forward.Normalized() : DVec3(1, 0, 0); // DVec3.UnitX
		auto s = DVec3::Cross(cs::Math::Abs(f.Z) < 0.9 ? DVec3(0, 0, 1) : DVec3(1, 0, 0), f); // UnitZ : UnitX
		return s.LengthSquared() > 1e-12 ? s.Normalized() : DVec3(0, 1, 0); // DVec3.UnitY
	}
}
