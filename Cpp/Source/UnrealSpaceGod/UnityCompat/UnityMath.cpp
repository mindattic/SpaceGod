#include "UnityCompat/UnityMath.h"

// Unity's own formulas (its reference C# for Vector3/Mathf, and the published equivalents of its native Quaternion
// routines), in single precision as Unity computes them.

namespace SpaceGod::UnityCompat
{
	float Mathf::SmoothDamp(float current, float target, float& currentVelocity, float smoothTime, float maxSpeed, float deltaTime)
	{
		smoothTime = Max(0.0001f, smoothTime);
		float omega = 2.0f / smoothTime;
		float x = omega * deltaTime;
		float e = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
		float change = current - target;
		float originalTo = target;
		float maxChange = maxSpeed * smoothTime;
		change = Clamp(change, -maxChange, maxChange);
		target = current - change;
		float temp = (currentVelocity + omega * change) * deltaTime;
		currentVelocity = (currentVelocity - omega * temp) * e;
		float output = target + (change + temp) * e;
		if ((originalTo - current > 0.0f) == (output > originalTo))
		{
			output = originalTo;
			currentVelocity = (output - originalTo) / deltaTime;
		}
		return output;
	}

	Vector3 Vector3::MoveTowards(Vector3 current, Vector3 target, float maxDistanceDelta)
	{
		Vector3 d = target - current;
		float sq = d.sqrMagnitude();
		if (sq == 0 || (maxDistanceDelta >= 0 && sq <= maxDistanceDelta * maxDistanceDelta)) return target;
		float dist = Mathf::Sqrt(sq);
		return current + d / dist * maxDistanceDelta;
	}

	Vector3 Vector3::ClampMagnitude(Vector3 v, float maxLength)
	{
		float sq = v.sqrMagnitude();
		if (sq > maxLength * maxLength)
		{
			float m = Mathf::Sqrt(sq);
			return Vector3(v.x / m * maxLength, v.y / m * maxLength, v.z / m * maxLength);
		}
		return v;
	}

	float Vector3::Angle(Vector3 from, Vector3 to)
	{
		float denominator = Mathf::Sqrt(from.sqrMagnitude() * to.sqrMagnitude());
		if (denominator < 1E-15f) return 0.0f;
		float dot = Mathf::Clamp(Dot(from, to) / denominator, -1.0f, 1.0f);
		return Mathf::Acos(dot) * Mathf::Rad2Deg;
	}

	float Vector3::SignedAngle(Vector3 from, Vector3 to, Vector3 axis)
	{
		float unsignedAngle = Angle(from, to);
		Vector3 c = Cross(from, to);
		float sign = Mathf::Sign(axis.x * c.x + axis.y * c.y + axis.z * c.z);
		return unsignedAngle * sign;
	}

	Vector3 Vector3::Project(Vector3 v, Vector3 onNormal)
	{
		float sqrMag = Dot(onNormal, onNormal);
		if (sqrMag < Mathf::Epsilon) return zero();
		return onNormal * (Dot(v, onNormal) / sqrMag);
	}

	Vector3 Vector3::ProjectOnPlane(Vector3 v, Vector3 planeNormal)
	{
		float sqrMag = Dot(planeNormal, planeNormal);
		if (sqrMag < Mathf::Epsilon) return v;
		return v - planeNormal * (Dot(v, planeNormal) / sqrMag);
	}

	Vector3 Vector3::Reflect(Vector3 inDirection, Vector3 inNormal)
	{
		float factor = -2.0f * Dot(inNormal, inDirection);
		return inNormal * factor + inDirection;
	}

	Vector3 Vector3::Slerp(Vector3 a, Vector3 b, float t) { return SlerpUnclamped(a, b, Mathf::Clamp01(t)); }

	Vector3 Vector3::SlerpUnclamped(Vector3 a, Vector3 b, float t)
	{
		float ma = a.magnitude(), mb = b.magnitude();
		if (ma < 1E-05f || mb < 1E-05f) return LerpUnclamped(a, b, t);
		Vector3 na = a / ma, nb = b / mb;
		float dot = Mathf::Clamp(Dot(na, nb), -1.0f, 1.0f);
		float length = ma + (mb - ma) * t;
		if (dot > 0.9995f) return LerpUnclamped(na, nb, t).normalized() * length;
		float theta = Mathf::Acos(dot) * t;
		Vector3 rel = nb - na * dot;
		if (rel.sqrMagnitude() < 1E-12f)
		{
			// Opposite directions: swing about any axis perpendicular to a.
			Vector3 axis = Cross(na, Vector3::right());
			if (axis.sqrMagnitude() < 1E-06f) axis = Cross(na, Vector3::up());
			rel = Cross(axis.normalized(), na);
		}
		rel.Normalize();
		return (na * Mathf::Cos(theta) + rel * Mathf::Sin(theta)) * length;
	}

	Vector3 Vector3::RotateTowards(Vector3 current, Vector3 target, float maxRadiansDelta, float maxMagnitudeDelta)
	{
		float ma = current.magnitude(), mb = target.magnitude();
		if (ma > 1E-05f && mb > 1E-05f)
		{
			Vector3 from = current / ma, to = target / mb;
			float dot = Mathf::Clamp(Dot(from, to), -1.0f, 1.0f);
			float angle = Mathf::Acos(dot);
			float newMag = Mathf::MoveTowards(ma, mb, maxMagnitudeDelta);
			if (angle <= maxRadiansDelta || angle < 1E-06f) return to * newMag;
			return SlerpUnclamped(from, to, maxRadiansDelta / angle).normalized() * newMag;
		}
		return MoveTowards(current, target, maxMagnitudeDelta);
	}

	Vector3 Quaternion::operator*(Vector3 point) const
	{
		float num1 = x * 2.0f, num2 = y * 2.0f, num3 = z * 2.0f;
		float num4 = x * num1, num5 = y * num2, num6 = z * num3;
		float num7 = x * num2, num8 = x * num3, num9 = y * num3;
		float num10 = w * num1, num11 = w * num2, num12 = w * num3;
		Vector3 r;
		r.x = (1.0f - (num5 + num6)) * point.x + (num7 - num12) * point.y + (num8 + num11) * point.z;
		r.y = (num7 + num12) * point.x + (1.0f - (num4 + num6)) * point.y + (num9 - num10) * point.z;
		r.z = (num8 - num11) * point.x + (num9 + num10) * point.y + (1.0f - (num4 + num5)) * point.z;
		return r;
	}

	float Quaternion::Angle(Quaternion a, Quaternion b)
	{
		float dot = Mathf::Min(Mathf::Abs(Dot(a, b)), 1.0f);
		return dot > 0.999999f ? 0.0f : Mathf::Acos(dot) * 2.0f * Mathf::Rad2Deg;
	}

	Quaternion Quaternion::AngleAxis(float angle, Vector3 axis)
	{
		Vector3 n = axis.normalized();
		if (n.sqrMagnitude() == 0) return identity();
		float half = angle * Mathf::Deg2Rad * 0.5f;
		float s = Mathf::Sin(half);
		return Quaternion(n.x * s, n.y * s, n.z * s, Mathf::Cos(half));
	}

	Quaternion Quaternion::Euler(float ex, float ey, float ez)
	{
		// Unity rotates about Z, then X, then Y: q = qY * qX * qZ.
		float hx = ex * Mathf::Deg2Rad * 0.5f, hy = ey * Mathf::Deg2Rad * 0.5f, hz = ez * Mathf::Deg2Rad * 0.5f;
		float sx = Mathf::Sin(hx), cx = Mathf::Cos(hx);
		float sy = Mathf::Sin(hy), cy = Mathf::Cos(hy);
		float sz = Mathf::Sin(hz), cz = Mathf::Cos(hz);
		Quaternion qx(sx, 0, 0, cx), qy(0, sy, 0, cy), qz(0, 0, sz, cz);
		return qy * qx * qz;
	}

	Vector3 Quaternion::eulerAngles() const
	{
		auto wrap = [](float deg) { deg = Mathf::Repeat(deg, 360.0f); return deg >= 360.0f ? 0.0f : deg; };
		float sinX = 2.0f * (w * x - y * z);
		float ex, ey, ez;
		if (Mathf::Abs(sinX) >= 0.99999f)
		{
			// Gimbal lock: X is +-90, put the whole remaining turn into Y.
			ex = Mathf::Sign(sinX) * 90.0f;
			ey = Mathf::Atan2(2.0f * (w * y - x * z), 1.0f - 2.0f * (y * y + z * z)) * Mathf::Rad2Deg;
			ez = 0;
		}
		else
		{
			ex = Mathf::Asin(sinX) * Mathf::Rad2Deg;
			ey = Mathf::Atan2(2.0f * (w * y + x * z), 1.0f - 2.0f * (x * x + y * y)) * Mathf::Rad2Deg;
			ez = Mathf::Atan2(2.0f * (w * z + x * y), 1.0f - 2.0f * (x * x + z * z)) * Mathf::Rad2Deg;
		}
		return Vector3(wrap(ex), wrap(ey), wrap(ez));
	}

	void Quaternion::ToAngleAxis(float& angle, Vector3& axis) const
	{
		Quaternion q = Normalize(*this);
		if (q.w < 0) q = Quaternion(-q.x, -q.y, -q.z, -q.w);
		angle = 2.0f * Mathf::Acos(Mathf::Clamp(q.w, -1.0f, 1.0f)) * Mathf::Rad2Deg;
		float s = Mathf::Sqrt(1.0f - q.w * q.w);
		axis = s < 1E-06f ? Vector3::right() : Vector3(q.x / s, q.y / s, q.z / s);
	}

	Quaternion Quaternion::Normalize(Quaternion q)
	{
		float m = Mathf::Sqrt(Dot(q, q));
		if (m < Mathf::Epsilon) return identity();
		return Quaternion(q.x / m, q.y / m, q.z / m, q.w / m);
	}

	Quaternion Quaternion::LookRotation(Vector3 forward, Vector3 upwards)
	{
		Vector3 f = forward.normalized();
		if (f.sqrMagnitude() == 0) return identity(); // Unity: "Look rotation viewing vector is zero"
		Vector3 r = Vector3::Cross(upwards, f);
		if (r.sqrMagnitude() < 1E-12f)
		{
			// Up parallel to forward: Unity falls back to the shortest turn from +Z.
			return FromToRotation(Vector3::forward(), f);
		}
		r.Normalize();
		Vector3 u = Vector3::Cross(f, r);
		// Rotation matrix with columns r, u, f -> quaternion.
		float m00 = r.x, m01 = u.x, m02 = f.x;
		float m10 = r.y, m11 = u.y, m12 = f.y;
		float m20 = r.z, m21 = u.z, m22 = f.z;
		float trace = m00 + m11 + m22;
		Quaternion q;
		if (trace > 0.0f)
		{
			float s = Mathf::Sqrt(trace + 1.0f) * 2.0f;
			q.w = 0.25f * s;
			q.x = (m21 - m12) / s;
			q.y = (m02 - m20) / s;
			q.z = (m10 - m01) / s;
		}
		else if (m00 > m11 && m00 > m22)
		{
			float s = Mathf::Sqrt(1.0f + m00 - m11 - m22) * 2.0f;
			q.w = (m21 - m12) / s;
			q.x = 0.25f * s;
			q.y = (m01 + m10) / s;
			q.z = (m02 + m20) / s;
		}
		else if (m11 > m22)
		{
			float s = Mathf::Sqrt(1.0f + m11 - m00 - m22) * 2.0f;
			q.w = (m02 - m20) / s;
			q.x = (m01 + m10) / s;
			q.y = 0.25f * s;
			q.z = (m12 + m21) / s;
		}
		else
		{
			float s = Mathf::Sqrt(1.0f + m22 - m00 - m11) * 2.0f;
			q.w = (m10 - m01) / s;
			q.x = (m02 + m20) / s;
			q.y = (m12 + m21) / s;
			q.z = 0.25f * s;
		}
		return Normalize(q);
	}

	Quaternion Quaternion::FromToRotation(Vector3 fromDirection, Vector3 toDirection)
	{
		Vector3 a = fromDirection.normalized(), b = toDirection.normalized();
		float dot = Vector3::Dot(a, b);
		if (dot >= 0.999999f) return identity();
		if (dot <= -0.999999f)
		{
			Vector3 axis = Vector3::Cross(Vector3::right(), a);
			if (axis.sqrMagnitude() < 1E-06f) axis = Vector3::Cross(Vector3::up(), a);
			return AngleAxis(180.0f, axis);
		}
		Vector3 c = Vector3::Cross(a, b);
		return Normalize(Quaternion(c.x, c.y, c.z, 1.0f + dot));
	}

	Quaternion Quaternion::SlerpUnclamped(Quaternion a, Quaternion b, float t)
	{
		float dot = Dot(a, b);
		if (dot < 0.0f)
		{
			b = Quaternion(-b.x, -b.y, -b.z, -b.w);
			dot = -dot;
		}
		if (dot > 0.9995f) return LerpUnclamped(a, b, t);
		float theta = Mathf::Acos(Mathf::Clamp(dot, -1.0f, 1.0f));
		float sinTheta = Mathf::Sin(theta);
		float wa = Mathf::Sin((1.0f - t) * theta) / sinTheta;
		float wb = Mathf::Sin(t * theta) / sinTheta;
		return Normalize(Quaternion(a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb));
	}

	Quaternion Quaternion::LerpUnclamped(Quaternion a, Quaternion b, float t)
	{
		// Unity's Lerp takes the shorter way round and normalizes.
		float sign = Dot(a, b) < 0.0f ? -1.0f : 1.0f;
		return Normalize(Quaternion(
			a.x + (b.x * sign - a.x) * t,
			a.y + (b.y * sign - a.y) * t,
			a.z + (b.z * sign - a.z) * t,
			a.w + (b.w * sign - a.w) * t));
	}

	Quaternion Quaternion::RotateTowards(Quaternion from, Quaternion to, float maxDegreesDelta)
	{
		float angle = Angle(from, to);
		if (angle == 0.0f) return to;
		return SlerpUnclamped(from, to, Mathf::Min(1.0f, maxDegreesDelta / angle));
	}
}
