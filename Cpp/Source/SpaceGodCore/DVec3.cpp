// Port of Unity.SpaceGod Core/DVec3.cs (see docs/MIGRATION.md).

#include "DVec3.h"

namespace SpaceGod::Core
{
	DVec3 DVec3::TurnStep(const DVec3& a, const DVec3& b, double t)
	{
		double cos = cs::Math::Max(-1.0, cs::Math::Min(1.0, Dot(a, b)));
		double angle = cs::Math::Acos(cos);
		if (angle < 1e-12) return b;
		auto side = b - a * cos;
		if (side.LengthSquared() < 1e-18)
		{
			side = Cross(a, UnitZ);
			if (side.LengthSquared() < 1e-12) side = Cross(a, UnitY);
		}
		double step = t * angle;
		return (a * cs::Math::Cos(step) + side.Normalized() * cs::Math::Sin(step)).Normalized();
	}

	// C#: HashCode.Combine(X, Y, Z) (randomised per process in .NET; only lookup uses it).
	size_t DVec3::HashCode() const
	{
		size_t h = std::hash<double>{}(X);
		h = cs::HashCombine(h, std::hash<double>{}(Y));
		return cs::HashCombine(h, std::hash<double>{}(Z));
	}

	cs::String DVec3::ToString() const
	{
		return cs::Cat("(", cs::Fmt(X, "G6"), ", ", cs::Fmt(Y, "G6"), ", ", cs::Fmt(Z, "G6"), ")");
	}
}
