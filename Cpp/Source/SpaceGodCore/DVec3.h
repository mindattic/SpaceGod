#pragma once
// Port of Unity.SpaceGod Core/DVec3.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"

namespace SpaceGod::Core
{
	/// <summary>
	/// Double-precision 3-vector. All simulation state lives in doubles: a float cannot hold a
	/// heliocentric position to better than ~10 km at Neptune's distance, which is visible jitter
	/// when the camera sits a few kilometres from a ship. Conversion to float happens only after
	/// subtracting the camera position (camera-relative rendering).
	/// </summary>
	struct SPACEGODCORE_API DVec3
	{
		double X = 0;
		double Y = 0;
		double Z = 0;

		DVec3() = default;
		DVec3(double x, double y, double z)
		{
			X = x;
			Y = y;
			Z = z;
		}

		static const DVec3 Zero;
		static const DVec3 UnitX;
		static const DVec3 UnitY;
		static const DVec3 UnitZ;

		double LengthSquared() const { return X * X + Y * Y + Z * Z; }
		double Length() const { return cs::Math::Sqrt(LengthSquared()); }

		DVec3 Normalized() const;

		friend DVec3 operator+(const DVec3& a, const DVec3& b) { return DVec3(a.X + b.X, a.Y + b.Y, a.Z + b.Z); }
		friend DVec3 operator-(const DVec3& a, const DVec3& b) { return DVec3(a.X - b.X, a.Y - b.Y, a.Z - b.Z); }
		friend DVec3 operator-(const DVec3& a) { return DVec3(-a.X, -a.Y, -a.Z); }
		friend DVec3 operator*(const DVec3& a, double s) { return DVec3(a.X * s, a.Y * s, a.Z * s); }
		friend DVec3 operator*(double s, const DVec3& a) { return DVec3(a.X * s, a.Y * s, a.Z * s); }
		friend DVec3 operator/(const DVec3& a, double s) { return DVec3(a.X / s, a.Y / s, a.Z / s); }
		// C# compound assignment on a struct variable: `a += b` is `a = a + b`.
		DVec3& operator+=(const DVec3& b) { return *this = *this + b; }
		DVec3& operator-=(const DVec3& b) { return *this = *this - b; }
		DVec3& operator*=(double s) { return *this = *this * s; }
		DVec3& operator/=(double s) { return *this = *this / s; }

		static double Dot(const DVec3& a, const DVec3& b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }

		static DVec3 Cross(const DVec3& a, const DVec3& b)
		{
			return DVec3(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
		}

		static double Distance(const DVec3& a, const DVec3& b) { return (a - b).Length(); }

		static DVec3 Lerp(const DVec3& a, const DVec3& b, double t) { return a + (b - a) * t; }

		/// <summary>
		/// Rotates unit direction <paramref name="a"/> a fraction <paramref name="t"/> of the angle
		/// round to unit direction <paramref name="b"/> (a slerp): a straight lerp turns well short of
		/// that at wide angles, next to nothing when nearly opposite, and never leaves the line when
		/// exactly opposite, where any perpendicular is taken as the way round.
		/// </summary>
		static DVec3 TurnStep(const DVec3& a, const DVec3& b, double t);

		/// <summary>
		/// Ecliptic (right-handed, z = ecliptic north) to scene (Unity, left-handed, y = up).
		/// Swapping y and z is a reflection, which is exactly the handedness change; it is its own inverse.
		/// </summary>
		DVec3 SwapYZ() const { return DVec3(X, Z, Y); }

		bool Equals(const DVec3& other) const { return X == other.X && Y == other.Y && Z == other.Z; }
		// C# DVec3 defines no == operator; this is Equals, for cs::Dictionary/HashSet keys and List.IndexOf.
		bool operator==(const DVec3& other) const { return Equals(other); }
		bool operator!=(const DVec3& other) const { return !Equals(other); }
		size_t HashCode() const;
		cs::String ToString() const;
	};

	inline const DVec3 DVec3::Zero = DVec3(0, 0, 0);
	inline const DVec3 DVec3::UnitX = DVec3(1, 0, 0);
	inline const DVec3 DVec3::UnitY = DVec3(0, 1, 0);
	inline const DVec3 DVec3::UnitZ = DVec3(0, 0, 1);

	inline DVec3 DVec3::Normalized() const
	{
		double len = Length();
		return len > 0 ? *this / len : Zero;
	}
}
