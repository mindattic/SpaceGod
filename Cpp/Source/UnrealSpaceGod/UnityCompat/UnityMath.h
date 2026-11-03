#pragma once

// Unity's value types (Vector2/3/4, Quaternion, Mathf, Color, Rect), reproduced in plain C++ so the Runtime port
// (UnitySpaceApp, SpaceCamera, StarTravel, ...) reads line for line like Unity.SpaceGod's Runtime/*.cs.
// Names follow docs/MIGRATION.md's rules: fields stay fields (x, y, z), bodied properties become methods
// (v.normalized(), v.magnitude(), q.eulerAngles()), static properties become static functions
// (Vector3::up(), Quaternion::identity()). The maths is Unity's own (single precision, its epsilons, its Euler order
// Z-X-Y, its clamping Lerp/Slerp), so the camera and helm feel the same.
//
// These are Unity's axes (left-handed, Y up, Z forward) and scene units; only UnrealSpace.h converts to Unreal's.

#include "Cs/Cs.h"
#include "DVec3.h"

namespace SpaceGod::UnityCompat
{
	struct Mathf
	{
		static constexpr float PI = 3.14159274f;
		static constexpr float Deg2Rad = PI * 2.0f / 360.0f;
		static constexpr float Rad2Deg = 1.0f / Deg2Rad;
		static constexpr float Epsilon = 1.401298E-45f;
		static constexpr float Infinity = std::numeric_limits<float>::infinity();
		static constexpr float NegativeInfinity = -std::numeric_limits<float>::infinity();

		static float Sin(float f) { return static_cast<float>(cs::Math::Sin(f)); }
		static float Cos(float f) { return static_cast<float>(cs::Math::Cos(f)); }
		static float Tan(float f) { return static_cast<float>(cs::Math::Tan(f)); }
		static float Asin(float f) { return static_cast<float>(cs::Math::Asin(f)); }
		static float Acos(float f) { return static_cast<float>(cs::Math::Acos(f)); }
		static float Atan(float f) { return static_cast<float>(cs::Math::Atan(f)); }
		static float Atan2(float y, float x) { return static_cast<float>(cs::Math::Atan2(y, x)); }
		static float Sqrt(float f) { return static_cast<float>(cs::Math::Sqrt(f)); }
		static float Abs(float f) { return f < 0 ? -f : f; }
		static int Abs(int v) { return v < 0 ? -v : v; }
		// Unity's Min/Max are a plain comparison (no NaN propagation, unlike Math.Max).
		static float Min(float a, float b) { return a < b ? a : b; }
		static int Min(int a, int b) { return a < b ? a : b; }
		static float Max(float a, float b) { return a > b ? a : b; }
		static int Max(int a, int b) { return a > b ? a : b; }
		static float Pow(float f, float p) { return static_cast<float>(cs::Math::Pow(f, p)); }
		static float Exp(float p) { return static_cast<float>(cs::Math::Exp(p)); }
		static float Log(float f) { return static_cast<float>(cs::Math::Log(f)); }
		static float Log10(float f) { return static_cast<float>(cs::Math::Log10(f)); }
		static float Ceil(float f) { return static_cast<float>(cs::Math::Ceiling(f)); }
		static float Floor(float f) { return static_cast<float>(cs::Math::Floor(f)); }
		/// Math.Round: banker's rounding, as Unity's Mathf.Round.
		static float Round(float f) { return static_cast<float>(cs::Math::Round(f)); }
		static int CeilToInt(float f) { return cs::ToInt(cs::Math::Ceiling(f)); }
		static int FloorToInt(float f) { return cs::ToInt(cs::Math::Floor(f)); }
		static int RoundToInt(float f) { return cs::ToInt(cs::Math::Round(f)); }
		static float Sign(float f) { return f >= 0.0f ? 1.0f : -1.0f; }
		static float Clamp(float value, float min, float max) { return value < min ? min : (value > max ? max : value); }
		static int Clamp(int value, int min, int max) { return value < min ? min : (value > max ? max : value); }
		static float Clamp01(float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }
		static float Lerp(float a, float b, float t) { return a + (b - a) * Clamp01(t); }
		static float LerpUnclamped(float a, float b, float t) { return a + (b - a) * t; }
		static float LerpAngle(float a, float b, float t)
		{
			float delta = Repeat(b - a, 360);
			if (delta > 180) delta -= 360;
			return a + delta * Clamp01(t);
		}
		static float MoveTowards(float current, float target, float maxDelta)
		{
			if (Abs(target - current) <= maxDelta) return target;
			return current + Sign(target - current) * maxDelta;
		}
		static float MoveTowardsAngle(float current, float target, float maxDelta)
		{
			float deltaAngle = DeltaAngle(current, target);
			if (-maxDelta < deltaAngle && deltaAngle < maxDelta) return target;
			target = current + deltaAngle;
			return MoveTowards(current, target, maxDelta);
		}
		static float SmoothStep(float from, float to, float t)
		{
			t = Clamp01(t);
			t = -2.0f * t * t * t + 3.0f * t * t;
			return to * t + from * (1.0f - t);
		}
		static bool Approximately(float a, float b)
		{
			return Abs(b - a) < Max(0.000001f * Max(Abs(a), Abs(b)), Epsilon * 8);
		}
		static float Repeat(float t, float length) { return Clamp(t - Floor(t / length) * length, 0.0f, length); }
		static float PingPong(float t, float length)
		{
			t = Repeat(t, length * 2.0f);
			return length - Abs(t - length);
		}
		static float InverseLerp(float a, float b, float value) { return a != b ? Clamp01((value - a) / (b - a)) : 0.0f; }
		static float DeltaAngle(float current, float target)
		{
			float delta = Repeat(target - current, 360.0f);
			if (delta > 180.0f) delta -= 360.0f;
			return delta;
		}
		static float SmoothDamp(float current, float target, float& currentVelocity, float smoothTime, float maxSpeed, float deltaTime);
		static float SmoothDamp(float current, float target, float& currentVelocity, float smoothTime, float deltaTime)
		{
			return SmoothDamp(current, target, currentVelocity, smoothTime, Infinity, deltaTime);
		}
	};

	struct Vector2
	{
		float x = 0, y = 0;
		constexpr Vector2() = default;
		constexpr Vector2(float x_, float y_) : x(x_), y(y_) {}

		static constexpr Vector2 zero() { return Vector2(0, 0); }
		static constexpr Vector2 one() { return Vector2(1, 1); }
		static constexpr Vector2 up() { return Vector2(0, 1); }
		static constexpr Vector2 down() { return Vector2(0, -1); }
		static constexpr Vector2 left() { return Vector2(-1, 0); }
		static constexpr Vector2 right() { return Vector2(1, 0); }

		float sqrMagnitude() const { return x * x + y * y; }
		float magnitude() const { return Mathf::Sqrt(sqrMagnitude()); }
		Vector2 normalized() const
		{
			float m = magnitude();
			return m > 1E-05f ? Vector2(x / m, y / m) : zero();
		}
		static float Dot(Vector2 a, Vector2 b) { return a.x * b.x + a.y * b.y; }
		static float Distance(Vector2 a, Vector2 b) { return Vector2(a.x - b.x, a.y - b.y).magnitude(); }
		static Vector2 Lerp(Vector2 a, Vector2 b, float t)
		{
			t = Mathf::Clamp01(t);
			return Vector2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
		}
		static Vector2 ClampMagnitude(Vector2 v, float maxLength)
		{
			float sq = v.sqrMagnitude();
			if (sq > maxLength * maxLength)
			{
				float m = Mathf::Sqrt(sq);
				return Vector2(v.x / m * maxLength, v.y / m * maxLength);
			}
			return v;
		}

		Vector2 operator+(Vector2 b) const { return Vector2(x + b.x, y + b.y); }
		Vector2 operator-(Vector2 b) const { return Vector2(x - b.x, y - b.y); }
		Vector2 operator-() const { return Vector2(-x, -y); }
		Vector2 operator*(float d) const { return Vector2(x * d, y * d); }
		Vector2 operator/(float d) const { return Vector2(x / d, y / d); }
		Vector2& operator+=(Vector2 b) { x += b.x; y += b.y; return *this; }
		Vector2& operator-=(Vector2 b) { x -= b.x; y -= b.y; return *this; }
		Vector2& operator*=(float d) { x *= d; y *= d; return *this; }
		/// Unity's == is approximate (squared distance under 1e-10).
		bool operator==(Vector2 b) const { float dx = x - b.x, dy = y - b.y; return dx * dx + dy * dy < 9.99999944E-11f; }
		bool operator!=(Vector2 b) const { return !(*this == b); }
	};
	inline Vector2 operator*(float d, Vector2 a) { return a * d; }

	struct Vector3
	{
		float x = 0, y = 0, z = 0;
		constexpr Vector3() = default;
		constexpr Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
		constexpr Vector3(float x_, float y_) : x(x_), y(y_), z(0) {}
		/// Unity's implicit Vector2 -> Vector3 (z = 0).
		constexpr Vector3(Vector2 v) : x(v.x), y(v.y), z(0) {}
		explicit operator Vector2() const { return Vector2(x, y); }

		static constexpr Vector3 zero() { return Vector3(0, 0, 0); }
		static constexpr Vector3 one() { return Vector3(1, 1, 1); }
		static constexpr Vector3 up() { return Vector3(0, 1, 0); }
		static constexpr Vector3 down() { return Vector3(0, -1, 0); }
		static constexpr Vector3 left() { return Vector3(-1, 0, 0); }
		static constexpr Vector3 right() { return Vector3(1, 0, 0); }
		static constexpr Vector3 forward() { return Vector3(0, 0, 1); }
		static constexpr Vector3 back() { return Vector3(0, 0, -1); }

		float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }

		float sqrMagnitude() const { return x * x + y * y + z * z; }
		float magnitude() const { return Mathf::Sqrt(sqrMagnitude()); }
		Vector3 normalized() const { return Normalize(*this); }
		void Normalize() { *this = Normalize(*this); }

		static Vector3 Normalize(Vector3 v)
		{
			float m = v.magnitude();
			return m > 1E-05f ? Vector3(v.x / m, v.y / m, v.z / m) : zero();
		}
		static float Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		static Vector3 Cross(Vector3 a, Vector3 b)
		{
			return Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
		}
		static float Distance(Vector3 a, Vector3 b) { return Vector3(a.x - b.x, a.y - b.y, a.z - b.z).magnitude(); }
		static float Magnitude(Vector3 v) { return v.magnitude(); }
		static float SqrMagnitude(Vector3 v) { return v.sqrMagnitude(); }
		static Vector3 Scale(Vector3 a, Vector3 b) { return Vector3(a.x * b.x, a.y * b.y, a.z * b.z); }
		static Vector3 Lerp(Vector3 a, Vector3 b, float t) { return LerpUnclamped(a, b, Mathf::Clamp01(t)); }
		static Vector3 LerpUnclamped(Vector3 a, Vector3 b, float t)
		{
			return Vector3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
		}
		static Vector3 MoveTowards(Vector3 current, Vector3 target, float maxDistanceDelta);
		static Vector3 ClampMagnitude(Vector3 v, float maxLength);
		/// Degrees between two directions, 0 when either is (near) zero.
		static float Angle(Vector3 from, Vector3 to);
		static float SignedAngle(Vector3 from, Vector3 to, Vector3 axis);
		static Vector3 Project(Vector3 v, Vector3 onNormal);
		static Vector3 ProjectOnPlane(Vector3 v, Vector3 planeNormal);
		static Vector3 Reflect(Vector3 inDirection, Vector3 inNormal);
		/// Spherical interpolation of directions and lengths, t clamped (Unity's Vector3.Slerp).
		static Vector3 Slerp(Vector3 a, Vector3 b, float t);
		static Vector3 SlerpUnclamped(Vector3 a, Vector3 b, float t);
		static Vector3 RotateTowards(Vector3 current, Vector3 target, float maxRadiansDelta, float maxMagnitudeDelta);
		static Vector3 Min(Vector3 a, Vector3 b) { return Vector3(Mathf::Min(a.x, b.x), Mathf::Min(a.y, b.y), Mathf::Min(a.z, b.z)); }
		static Vector3 Max(Vector3 a, Vector3 b) { return Vector3(Mathf::Max(a.x, b.x), Mathf::Max(a.y, b.y), Mathf::Max(a.z, b.z)); }

		Vector3 operator+(Vector3 b) const { return Vector3(x + b.x, y + b.y, z + b.z); }
		Vector3 operator-(Vector3 b) const { return Vector3(x - b.x, y - b.y, z - b.z); }
		Vector3 operator-() const { return Vector3(-x, -y, -z); }
		Vector3 operator*(float d) const { return Vector3(x * d, y * d, z * d); }
		Vector3 operator/(float d) const { return Vector3(x / d, y / d, z / d); }
		Vector3& operator+=(Vector3 b) { x += b.x; y += b.y; z += b.z; return *this; }
		Vector3& operator-=(Vector3 b) { x -= b.x; y -= b.y; z -= b.z; return *this; }
		Vector3& operator*=(float d) { x *= d; y *= d; z *= d; return *this; }
		Vector3& operator/=(float d) { x /= d; y /= d; z /= d; return *this; }
		bool operator==(Vector3 b) const
		{
			float dx = x - b.x, dy = y - b.y, dz = z - b.z;
			return dx * dx + dy * dy + dz * dz < 9.99999944E-11f;
		}
		bool operator!=(Vector3 b) const { return !(*this == b); }
	};
	inline Vector3 operator*(float d, Vector3 a) { return a * d; }

	struct Vector4
	{
		float x = 0, y = 0, z = 0, w = 0;
		constexpr Vector4() = default;
		constexpr Vector4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
		static constexpr Vector4 zero() { return Vector4(0, 0, 0, 0); }
		static constexpr Vector4 one() { return Vector4(1, 1, 1, 1); }
	};

	struct Quaternion
	{
		float x = 0, y = 0, z = 0, w = 0;
		constexpr Quaternion() = default;
		constexpr Quaternion(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

		static constexpr Quaternion identity() { return Quaternion(0, 0, 0, 1); }

		static float Dot(Quaternion a, Quaternion b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
		/// Degrees between two rotations.
		static float Angle(Quaternion a, Quaternion b);
		static Quaternion AngleAxis(float angle, Vector3 axis);
		/// Unity's Euler order: Z first, then X, then Y (degrees).
		static Quaternion Euler(float x, float y, float z);
		static Quaternion Euler(Vector3 e) { return Euler(e.x, e.y, e.z); }
		static Quaternion LookRotation(Vector3 forward, Vector3 upwards);
		static Quaternion LookRotation(Vector3 forward) { return LookRotation(forward, Vector3::up()); }
		static Quaternion FromToRotation(Vector3 fromDirection, Vector3 toDirection);
		static Quaternion Inverse(Quaternion q) { return Quaternion(-q.x, -q.y, -q.z, q.w); }
		static Quaternion Normalize(Quaternion q);
		static Quaternion Slerp(Quaternion a, Quaternion b, float t) { return SlerpUnclamped(a, b, Mathf::Clamp01(t)); }
		static Quaternion SlerpUnclamped(Quaternion a, Quaternion b, float t);
		static Quaternion Lerp(Quaternion a, Quaternion b, float t) { return LerpUnclamped(a, b, Mathf::Clamp01(t)); }
		static Quaternion LerpUnclamped(Quaternion a, Quaternion b, float t);
		static Quaternion RotateTowards(Quaternion from, Quaternion to, float maxDegreesDelta);

		Quaternion normalized() const { return Normalize(*this); }
		/// Degrees, each in [0, 360).
		Vector3 eulerAngles() const;
		void ToAngleAxis(float& angle, Vector3& axis) const;

		Quaternion operator*(Quaternion r) const
		{
			return Quaternion(
				w * r.x + x * r.w + y * r.z - z * r.y,
				w * r.y + y * r.w + z * r.x - x * r.z,
				w * r.z + z * r.w + x * r.y - y * r.x,
				w * r.w - x * r.x - y * r.y - z * r.z);
		}
		Vector3 operator*(Vector3 point) const;
		/// Unity's == treats rotations a hair apart as equal.
		bool operator==(Quaternion b) const { return Dot(*this, b) > 0.999999f; }
		bool operator!=(Quaternion b) const { return !(*this == b); }
	};

	struct Color
	{
		float r = 0, g = 0, b = 0, a = 0;
		constexpr Color() = default;
		constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
		static constexpr Color white() { return Color(1, 1, 1, 1); }
		static constexpr Color black() { return Color(0, 0, 0, 1); }
		static constexpr Color clear() { return Color(0, 0, 0, 0); }
		static constexpr Color red() { return Color(1, 0, 0, 1); }
		static constexpr Color green() { return Color(0, 1, 0, 1); }
		static constexpr Color blue() { return Color(0, 0, 1, 1); }
		static constexpr Color yellow() { return Color(1, 0.921568632f, 0.0156862754f, 1); }
		static constexpr Color cyan() { return Color(0, 1, 1, 1); }
		static constexpr Color magenta() { return Color(1, 0, 1, 1); }
		static constexpr Color gray() { return Color(0.5f, 0.5f, 0.5f, 1); }
		static Color Lerp(Color x, Color y, float t)
		{
			t = Mathf::Clamp01(t);
			return Color(x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t);
		}
		Color operator*(float d) const { return Color(r * d, g * d, b * d, a * d); }
		Color operator*(Color c) const { return Color(r * c.r, g * c.g, b * c.b, a * c.a); }
		Color operator+(Color c) const { return Color(r + c.r, g + c.g, b + c.b, a + c.a); }
	};

	/// A GUI rectangle (origin top-left, y down, as Unity's IMGUI).
	struct Rect
	{
		float x = 0, y = 0, width = 0, height = 0;
		constexpr Rect() = default;
		constexpr Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), width(w_), height(h_) {}
		float xMin() const { return x; }
		float yMin() const { return y; }
		float xMax() const { return x + width; }
		float yMax() const { return y + height; }
		Vector2 center() const { return Vector2(x + width / 2, y + height / 2); }
		/// Unity's Rect.Contains: min inclusive, max exclusive.
		bool Contains(Vector2 p) const { return p.x >= xMin() && p.x < xMax() && p.y >= yMin() && p.y < yMax(); }
		bool Contains(Vector3 p) const { return Contains(Vector2(p.x, p.y)); }
		bool Overlaps(const Rect& o) const { return o.xMax() > xMin() && o.xMin() < xMax() && o.yMax() > yMin() && o.yMin() < yMax(); }
		bool operator==(const Rect& o) const { return x == o.x && y == o.y && width == o.width && height == o.height; }
	};

	/// Core's DVec3 <-> Unity's Vector3 (the Runtime's (Vector3)dvec / new DVec3(v) conversions).
	inline Vector3 ToVector3(const Core::DVec3& v) { return Vector3(static_cast<float>(v.X), static_cast<float>(v.Y), static_cast<float>(v.Z)); }
	inline Core::DVec3 ToDVec3(Vector3 v) { return Core::DVec3(v.x, v.y, v.z); }
}
