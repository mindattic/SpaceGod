using System;

// The parts of UnityEngine's math the ported Runtime uses, reproduced with Unity's own formulas (float precision,
// Euler order ZXY, LookRotation's basis, clamped Lerps, the 1e-5 normalise threshold), so the ported views place
// and orient everything exactly as the original does. Unity's types are left-handed, Y up; see UnrealSpace.
namespace UnityEngine
{
    public static class Mathf
    {
        public const float PI = (float)Math.PI;
        public const float Infinity = float.PositiveInfinity;
        public const float NegativeInfinity = float.NegativeInfinity;
        public const float Deg2Rad = PI * 2f / 360f;
        public const float Rad2Deg = 1f / Deg2Rad;
        public static readonly float Epsilon = float.Epsilon;

        public static float Sin(float f) => (float)Math.Sin(f);
        public static float Cos(float f) => (float)Math.Cos(f);
        public static float Tan(float f) => (float)Math.Tan(f);
        public static float Asin(float f) => (float)Math.Asin(f);
        public static float Acos(float f) => (float)Math.Acos(f);
        public static float Atan(float f) => (float)Math.Atan(f);
        public static float Atan2(float y, float x) => (float)Math.Atan2(y, x);
        public static float Sqrt(float f) => (float)Math.Sqrt(f);
        public static float Abs(float f) => Math.Abs(f);
        public static int Abs(int v) => Math.Abs(v);
        public static float Min(float a, float b) => a < b ? a : b;
        public static float Min(params float[] values)
        {
            if (values.Length == 0) return 0;
            float m = values[0];
            for (int i = 1; i < values.Length; i++) if (values[i] < m) m = values[i];
            return m;
        }
        public static int Min(int a, int b) => a < b ? a : b;
        public static float Max(float a, float b) => a > b ? a : b;
        public static float Max(params float[] values)
        {
            if (values.Length == 0) return 0;
            float m = values[0];
            for (int i = 1; i < values.Length; i++) if (values[i] > m) m = values[i];
            return m;
        }
        public static int Max(int a, int b) => a > b ? a : b;
        public static float Pow(float f, float p) => (float)Math.Pow(f, p);
        public static float Exp(float power) => (float)Math.Exp(power);
        public static float Log(float f, float p) => (float)Math.Log(f, p);
        public static float Log(float f) => (float)Math.Log(f);
        public static float Log10(float f) => (float)Math.Log10(f);
        public static float Ceil(float f) => (float)Math.Ceiling(f);
        public static float Floor(float f) => (float)Math.Floor(f);
        public static float Round(float f) => (float)Math.Round(f);
        public static int CeilToInt(float f) => (int)Math.Ceiling(f);
        public static int FloorToInt(float f) => (int)Math.Floor(f);
        public static int RoundToInt(float f) => (int)Math.Round(f);
        public static float Sign(float f) => f >= 0f ? 1f : -1f;
        public static float Clamp(float value, float min, float max) => value < min ? min : value > max ? max : value;
        public static int Clamp(int value, int min, int max) => value < min ? min : value > max ? max : value;
        public static float Clamp01(float value) => value < 0f ? 0f : value > 1f ? 1f : value;
        public static float Lerp(float a, float b, float t) => a + (b - a) * Clamp01(t);
        public static float LerpUnclamped(float a, float b, float t) => a + (b - a) * t;
        public static float InverseLerp(float a, float b, float value) => a != b ? Clamp01((value - a) / (b - a)) : 0f;
        public static float Repeat(float t, float length) => Clamp(t - Floor(t / length) * length, 0f, length);
        public static float PingPong(float t, float length)
        {
            t = Repeat(t, length * 2f);
            return length - Abs(t - length);
        }
        public static float MoveTowards(float current, float target, float maxDelta) =>
            Abs(target - current) <= maxDelta ? target : current + Sign(target - current) * maxDelta;
        public static float SmoothStep(float from, float to, float t)
        {
            t = Clamp01(t);
            t = -2f * t * t * t + 3f * t * t;
            return to * t + from * (1f - t);
        }
        public static float DeltaAngle(float current, float target)
        {
            float delta = Repeat(target - current, 360f);
            if (delta > 180f) delta -= 360f;
            return delta;
        }
        public static float LerpAngle(float a, float b, float t)
        {
            float delta = Repeat(b - a, 360);
            if (delta > 180) delta -= 360;
            return a + delta * Clamp01(t);
        }
        public static bool Approximately(float a, float b) =>
            Abs(b - a) < Max(1e-6f * Max(Abs(a), Abs(b)), Epsilon * 8);
        public static bool IsPowerOfTwo(int value) => (value & (value - 1)) == 0;
        public static int NextPowerOfTwo(int value)
        {
            value -= 1;
            value |= value >> 16; value |= value >> 8; value |= value >> 4; value |= value >> 2; value |= value >> 1;
            return value + 1;
        }
    }

    public struct Vector2 : IEquatable<Vector2>
    {
        public float x, y;
        public Vector2(float x, float y) { this.x = x; this.y = y; }
        public float this[int i]
        {
            get => i == 0 ? x : i == 1 ? y : throw new IndexOutOfRangeException();
            set { if (i == 0) x = value; else if (i == 1) y = value; else throw new IndexOutOfRangeException(); }
        }
        public static Vector2 zero => new Vector2(0, 0);
        public static Vector2 one => new Vector2(1, 1);
        public static Vector2 up => new Vector2(0, 1);
        public static Vector2 down => new Vector2(0, -1);
        public static Vector2 left => new Vector2(-1, 0);
        public static Vector2 right => new Vector2(1, 0);
        public float sqrMagnitude => x * x + y * y;
        public float magnitude => (float)Math.Sqrt(x * x + y * y);
        public Vector2 normalized
        {
            get
            {
                float m = magnitude;
                return m > 1e-5f ? this / m : zero;
            }
        }
        public void Normalize() => this = normalized;
        public static float Dot(Vector2 a, Vector2 b) => a.x * b.x + a.y * b.y;
        public static float Distance(Vector2 a, Vector2 b) => (a - b).magnitude;
        public static Vector2 Lerp(Vector2 a, Vector2 b, float t)
        {
            t = Mathf.Clamp01(t);
            return new Vector2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
        }
        public static Vector2 LerpUnclamped(Vector2 a, Vector2 b, float t) => new Vector2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
        public static Vector2 Max(Vector2 a, Vector2 b) => new Vector2(Mathf.Max(a.x, b.x), Mathf.Max(a.y, b.y));
        public static Vector2 Min(Vector2 a, Vector2 b) => new Vector2(Mathf.Min(a.x, b.x), Mathf.Min(a.y, b.y));
        public static Vector2 operator +(Vector2 a, Vector2 b) => new Vector2(a.x + b.x, a.y + b.y);
        public static Vector2 operator -(Vector2 a, Vector2 b) => new Vector2(a.x - b.x, a.y - b.y);
        public static Vector2 operator -(Vector2 a) => new Vector2(-a.x, -a.y);
        public static Vector2 operator *(Vector2 a, float d) => new Vector2(a.x * d, a.y * d);
        public static Vector2 operator *(float d, Vector2 a) => new Vector2(a.x * d, a.y * d);
        public static Vector2 operator *(Vector2 a, Vector2 b) => new Vector2(a.x * b.x, a.y * b.y);
        public static Vector2 operator /(Vector2 a, float d) => new Vector2(a.x / d, a.y / d);
        public static bool operator ==(Vector2 a, Vector2 b) => (a - b).sqrMagnitude < 9.99999944E-11f;
        public static bool operator !=(Vector2 a, Vector2 b) => !(a == b);
        public static implicit operator Vector2(Vector3 v) => new Vector2(v.x, v.y);
        public static implicit operator Vector3(Vector2 v) => new Vector3(v.x, v.y, 0);
        public bool Equals(Vector2 o) => x == o.x && y == o.y;
        public override bool Equals(object? o) => o is Vector2 v && Equals(v);
        public override int GetHashCode() => x.GetHashCode() ^ (y.GetHashCode() << 2);
        public override string ToString() => $"({x:F2}, {y:F2})";
    }

    public struct Vector3 : IEquatable<Vector3>
    {
        public const float kEpsilon = 1e-5f;
        public float x, y, z;
        public Vector3(float x, float y, float z) { this.x = x; this.y = y; this.z = z; }
        public Vector3(float x, float y) { this.x = x; this.y = y; z = 0; }
        public float this[int i]
        {
            get => i == 0 ? x : i == 1 ? y : i == 2 ? z : throw new IndexOutOfRangeException();
            set { if (i == 0) x = value; else if (i == 1) y = value; else if (i == 2) z = value; else throw new IndexOutOfRangeException(); }
        }
        public static Vector3 zero => new Vector3(0, 0, 0);
        public static Vector3 one => new Vector3(1, 1, 1);
        public static Vector3 up => new Vector3(0, 1, 0);
        public static Vector3 down => new Vector3(0, -1, 0);
        public static Vector3 left => new Vector3(-1, 0, 0);
        public static Vector3 right => new Vector3(1, 0, 0);
        public static Vector3 forward => new Vector3(0, 0, 1);
        public static Vector3 back => new Vector3(0, 0, -1);
        public static Vector3 positiveInfinity => new Vector3(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity);
        public static Vector3 negativeInfinity => new Vector3(float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity);
        public float sqrMagnitude => x * x + y * y + z * z;
        public float magnitude => (float)Math.Sqrt(x * x + y * y + z * z);
        public Vector3 normalized => Normalize(this);
        public void Normalize() => this = Normalize(this);
        public void Set(float nx, float ny, float nz) { x = nx; y = ny; z = nz; }
        public static Vector3 Normalize(Vector3 v)
        {
            float m = v.magnitude;
            return m > kEpsilon ? v / m : zero;
        }
        public static float Magnitude(Vector3 v) => v.magnitude;
        public static float SqrMagnitude(Vector3 v) => v.sqrMagnitude;
        public static float Dot(Vector3 a, Vector3 b) => a.x * b.x + a.y * b.y + a.z * b.z;
        public static Vector3 Cross(Vector3 a, Vector3 b) =>
            new Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
        public static float Distance(Vector3 a, Vector3 b) => (a - b).magnitude;
        public static Vector3 Scale(Vector3 a, Vector3 b) => new Vector3(a.x * b.x, a.y * b.y, a.z * b.z);
        public void Scale(Vector3 s) { x *= s.x; y *= s.y; z *= s.z; }
        public static Vector3 Lerp(Vector3 a, Vector3 b, float t)
        {
            t = Mathf.Clamp01(t);
            return new Vector3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
        }
        public static Vector3 LerpUnclamped(Vector3 a, Vector3 b, float t) =>
            new Vector3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
        public static Vector3 MoveTowards(Vector3 current, Vector3 target, float maxDistanceDelta)
        {
            var d = target - current;
            float sq = d.sqrMagnitude;
            if (sq == 0 || (maxDistanceDelta >= 0 && sq <= maxDistanceDelta * maxDistanceDelta)) return target;
            float dist = (float)Math.Sqrt(sq);
            return current + d / dist * maxDistanceDelta;
        }
        public static Vector3 ClampMagnitude(Vector3 v, float maxLength)
        {
            float sq = v.sqrMagnitude;
            if (sq > maxLength * maxLength) return v / (float)Math.Sqrt(sq) * maxLength;
            return v;
        }
        public static Vector3 Project(Vector3 v, Vector3 onNormal)
        {
            float sq = Dot(onNormal, onNormal);
            if (sq < Mathf.Epsilon) return zero;
            return onNormal * Dot(v, onNormal) / sq;
        }
        public static Vector3 ProjectOnPlane(Vector3 v, Vector3 planeNormal)
        {
            float sq = Dot(planeNormal, planeNormal);
            if (sq < Mathf.Epsilon) return v;
            return v - planeNormal * Dot(v, planeNormal) / sq;
        }
        public static float Angle(Vector3 from, Vector3 to)
        {
            float denominator = (float)Math.Sqrt(from.sqrMagnitude * to.sqrMagnitude);
            if (denominator < 1e-15f) return 0f;
            float dot = Mathf.Clamp(Dot(from, to) / denominator, -1f, 1f);
            return (float)Math.Acos(dot) * Mathf.Rad2Deg;
        }
        public static float SignedAngle(Vector3 from, Vector3 to, Vector3 axis)
        {
            float a = Angle(from, to);
            float s = Mathf.Sign(Dot(axis, Cross(from, to)));
            return a * s;
        }
        public static Vector3 Reflect(Vector3 inDirection, Vector3 inNormal) => -2f * Dot(inNormal, inDirection) * inNormal + inDirection;
        public static Vector3 Max(Vector3 a, Vector3 b) => new Vector3(Mathf.Max(a.x, b.x), Mathf.Max(a.y, b.y), Mathf.Max(a.z, b.z));
        public static Vector3 Min(Vector3 a, Vector3 b) => new Vector3(Mathf.Min(a.x, b.x), Mathf.Min(a.y, b.y), Mathf.Min(a.z, b.z));
        public static Vector3 Slerp(Vector3 a, Vector3 b, float t)
        {
            t = Mathf.Clamp01(t);
            float ma = a.magnitude, mb = b.magnitude;
            if (ma < kEpsilon || mb < kEpsilon) return Lerp(a, b, t);
            var na = a / ma;
            var nb = b / mb;
            float dot = Mathf.Clamp(Dot(na, nb), -1, 1);
            float theta = (float)Math.Acos(dot) * t;
            var rel = (nb - na * dot).normalized;
            return (na * (float)Math.Cos(theta) + rel * (float)Math.Sin(theta)) * Mathf.Lerp(ma, mb, t);
        }
        public static Vector3 operator +(Vector3 a, Vector3 b) => new Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
        public static Vector3 operator -(Vector3 a, Vector3 b) => new Vector3(a.x - b.x, a.y - b.y, a.z - b.z);
        public static Vector3 operator -(Vector3 a) => new Vector3(-a.x, -a.y, -a.z);
        public static Vector3 operator *(Vector3 a, float d) => new Vector3(a.x * d, a.y * d, a.z * d);
        public static Vector3 operator *(float d, Vector3 a) => new Vector3(a.x * d, a.y * d, a.z * d);
        public static Vector3 operator /(Vector3 a, float d) => new Vector3(a.x / d, a.y / d, a.z / d);
        public static bool operator ==(Vector3 a, Vector3 b)
        {
            float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return dx * dx + dy * dy + dz * dz < 9.99999944E-11f;
        }
        public static bool operator !=(Vector3 a, Vector3 b) => !(a == b);
        public bool Equals(Vector3 o) => x == o.x && y == o.y && z == o.z;
        public override bool Equals(object? o) => o is Vector3 v && Equals(v);
        public override int GetHashCode() => x.GetHashCode() ^ (y.GetHashCode() << 2) ^ (z.GetHashCode() >> 2);
        public override string ToString() => $"({x:F2}, {y:F2}, {z:F2})";
    }

    public struct Vector4 : IEquatable<Vector4>
    {
        public float x, y, z, w;
        public Vector4(float x, float y, float z, float w) { this.x = x; this.y = y; this.z = z; this.w = w; }
        public Vector4(float x, float y, float z) { this.x = x; this.y = y; this.z = z; w = 0; }
        public float this[int i]
        {
            get => i switch { 0 => x, 1 => y, 2 => z, 3 => w, _ => throw new IndexOutOfRangeException() };
            set { switch (i) { case 0: x = value; break; case 1: y = value; break; case 2: z = value; break; case 3: w = value; break; default: throw new IndexOutOfRangeException(); } }
        }
        public static Vector4 zero => new Vector4(0, 0, 0, 0);
        public static Vector4 one => new Vector4(1, 1, 1, 1);
        public float sqrMagnitude => x * x + y * y + z * z + w * w;
        public float magnitude => (float)Math.Sqrt(sqrMagnitude);
        public static Vector4 operator +(Vector4 a, Vector4 b) => new Vector4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
        public static Vector4 operator -(Vector4 a, Vector4 b) => new Vector4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
        public static Vector4 operator *(Vector4 a, float d) => new Vector4(a.x * d, a.y * d, a.z * d, a.w * d);
        public static Vector4 operator /(Vector4 a, float d) => new Vector4(a.x / d, a.y / d, a.z / d, a.w / d);
        public static implicit operator Vector4(Vector3 v) => new Vector4(v.x, v.y, v.z, 0);
        public static implicit operator Vector3(Vector4 v) => new Vector3(v.x, v.y, v.z);
        public static implicit operator Vector4(Vector2 v) => new Vector4(v.x, v.y, 0, 0);
        public static bool operator ==(Vector4 a, Vector4 b) => (a - b).sqrMagnitude < 9.99999944E-11f;
        public static bool operator !=(Vector4 a, Vector4 b) => !(a == b);
        public bool Equals(Vector4 o) => x == o.x && y == o.y && z == o.z && w == o.w;
        public override bool Equals(object? o) => o is Vector4 v && Equals(v);
        public override int GetHashCode() => HashCode.Combine(x, y, z, w);
        public override string ToString() => $"({x:F2}, {y:F2}, {z:F2}, {w:F2})";
    }

    public struct Quaternion : IEquatable<Quaternion>
    {
        public const float kEpsilon = 1E-06f;
        public float x, y, z, w;
        public Quaternion(float x, float y, float z, float w) { this.x = x; this.y = y; this.z = z; this.w = w; }
        public static Quaternion identity => new Quaternion(0, 0, 0, 1);

        public static Quaternion operator *(Quaternion lhs, Quaternion rhs) => new Quaternion(
            lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.w * rhs.y + lhs.y * rhs.w + lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.w * rhs.z + lhs.z * rhs.w + lhs.x * rhs.y - lhs.y * rhs.x,
            lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z);

        public static Vector3 operator *(Quaternion rotation, Vector3 point)
        {
            float x = rotation.x * 2f, y = rotation.y * 2f, z = rotation.z * 2f;
            float xx = rotation.x * x, yy = rotation.y * y, zz = rotation.z * z;
            float xy = rotation.x * y, xz = rotation.x * z, yz = rotation.y * z;
            float wx = rotation.w * x, wy = rotation.w * y, wz = rotation.w * z;
            Vector3 res;
            res.x = (1f - (yy + zz)) * point.x + (xy - wz) * point.y + (xz + wy) * point.z;
            res.y = (xy + wz) * point.x + (1f - (xx + zz)) * point.y + (yz - wx) * point.z;
            res.z = (xz - wy) * point.x + (yz + wx) * point.y + (1f - (xx + yy)) * point.z;
            return res;
        }

        public static float Dot(Quaternion a, Quaternion b) => a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

        static bool IsEqualUsingDot(float dot) => dot > 1.0f - kEpsilon;
        public static bool operator ==(Quaternion a, Quaternion b) => IsEqualUsingDot(Dot(a, b));
        public static bool operator !=(Quaternion a, Quaternion b) => !(a == b);

        public static float Angle(Quaternion a, Quaternion b)
        {
            float dot = Math.Min(Math.Abs(Dot(a, b)), 1.0f);
            return IsEqualUsingDot(dot) ? 0.0f : (float)Math.Acos(dot) * 2.0f * Mathf.Rad2Deg;
        }

        public static Quaternion Inverse(Quaternion q)
        {
            float n = Dot(q, q);
            if (n < 1e-30f) return identity;
            return new Quaternion(-q.x / n, -q.y / n, -q.z / n, q.w / n);
        }

        public static Quaternion Normalize(Quaternion q)
        {
            float mag = (float)Math.Sqrt(Dot(q, q));
            if (mag < Mathf.Epsilon) return identity;
            return new Quaternion(q.x / mag, q.y / mag, q.z / mag, q.w / mag);
        }
        public Quaternion normalized => Normalize(this);
        public void Normalize() => this = Normalize(this);

        /// <summary>Unity's Euler: Z, then X, then Y (degrees).</summary>
        public static Quaternion Euler(float x, float y, float z)
        {
            float hx = x * Mathf.Deg2Rad * 0.5f, hy = y * Mathf.Deg2Rad * 0.5f, hz = z * Mathf.Deg2Rad * 0.5f;
            var qx = new Quaternion((float)Math.Sin(hx), 0, 0, (float)Math.Cos(hx));
            var qy = new Quaternion(0, (float)Math.Sin(hy), 0, (float)Math.Cos(hy));
            var qz = new Quaternion(0, 0, (float)Math.Sin(hz), (float)Math.Cos(hz));
            return qy * qx * qz;
        }
        public static Quaternion Euler(Vector3 e) => Euler(e.x, e.y, e.z);

        public Vector3 eulerAngles
        {
            get
            {
                var q = Normalize(this);
                float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
                float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
                float m00 = 1 - 2 * (yy + zz), m02 = 2 * (xz + wy);
                float m10 = 2 * (xy + wz), m11 = 1 - 2 * (xx + zz), m12 = 2 * (yz - wx);
                float m20 = 2 * (xz - wy), m22 = 1 - 2 * (xx + yy);
                float ex, ey, ez;
                float s = Mathf.Clamp(-m12, -1f, 1f);
                ex = (float)Math.Asin(s);
                if (Math.Abs(s) < 0.9999f)
                {
                    ey = (float)Math.Atan2(m02, m22);
                    ez = (float)Math.Atan2(m10, m11);
                }
                else
                {
                    ey = (float)Math.Atan2(-m20, m00);
                    ez = 0;
                }
                return new Vector3(Wrap(ex * Mathf.Rad2Deg), Wrap(ey * Mathf.Rad2Deg), Wrap(ez * Mathf.Rad2Deg));
            }
            set => this = Euler(value);
        }
        static float Wrap(float a)
        {
            a %= 360f;
            if (a < 0) a += 360f;
            return a;
        }

        public static Quaternion AngleAxis(float angle, Vector3 axis)
        {
            var n = axis.normalized;
            float h = angle * Mathf.Deg2Rad * 0.5f;
            float s = (float)Math.Sin(h);
            return new Quaternion(n.x * s, n.y * s, n.z * s, (float)Math.Cos(h));
        }

        public void ToAngleAxis(out float angle, out Vector3 axis)
        {
            var q = Normalize(this);
            angle = 2f * (float)Math.Acos(Mathf.Clamp(q.w, -1, 1)) * Mathf.Rad2Deg;
            float s = (float)Math.Sqrt(Math.Max(0, 1 - q.w * q.w));
            axis = s > 1e-4f ? new Vector3(q.x / s, q.y / s, q.z / s) : Vector3.right;
        }

        public static Quaternion FromToRotation(Vector3 fromDirection, Vector3 toDirection)
        {
            var from = fromDirection.normalized;
            var to = toDirection.normalized;
            float d = Vector3.Dot(from, to);
            if (d >= 1f - 1e-6f) return identity;
            if (d <= -1f + 1e-6f)
            {
                // Opposite: half a turn about any axis perpendicular to from.
                var axis = Vector3.Cross(Vector3.right, from);
                if (axis.sqrMagnitude < 1e-6f) axis = Vector3.Cross(Vector3.up, from);
                return AngleAxis(180f, axis.normalized);
            }
            var c = Vector3.Cross(from, to);
            float s = (float)Math.Sqrt((1 + d) * 2);
            float inv = 1 / s;
            return Normalize(new Quaternion(c.x * inv, c.y * inv, c.z * inv, s * 0.5f));
        }

        public static Quaternion LookRotation(Vector3 forward) => LookRotation(forward, Vector3.up);

        /// <summary>Unity's LookRotation: the rotation whose +Z is <paramref name="forward"/> and whose +Y is as close to
        /// <paramref name="upwards"/> as it can be (identity for a zero forward, as Unity returns).</summary>
        public static Quaternion LookRotation(Vector3 forward, Vector3 upwards)
        {
            var f = forward.normalized;
            if (f.sqrMagnitude < 1e-12f) return identity;
            var r = Vector3.Cross(upwards, f);
            if (r.sqrMagnitude < 1e-12f)
            {
                // Up parallel to forward: Unity turns the shortest way from +Z.
                return FromToRotation(Vector3.forward, f);
            }
            r = r.normalized;
            var u = Vector3.Cross(f, r);
            return FromBasis(r, u, f);
        }

        /// <summary>The rotation taking +X, +Y, +Z to the orthonormal <paramref name="r"/>, <paramref name="u"/>, <paramref name="f"/>.</summary>
        internal static Quaternion FromBasis(Vector3 r, Vector3 u, Vector3 f)
        {
            float m00 = r.x, m01 = u.x, m02 = f.x;
            float m10 = r.y, m11 = u.y, m12 = f.y;
            float m20 = r.z, m21 = u.z, m22 = f.z;
            float trace = m00 + m11 + m22;
            Quaternion q;
            if (trace > 0)
            {
                float s = (float)Math.Sqrt(trace + 1.0f) * 2f;
                q = new Quaternion((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s);
            }
            else if (m00 > m11 && m00 > m22)
            {
                float s = (float)Math.Sqrt(1.0f + m00 - m11 - m22) * 2f;
                q = new Quaternion(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
            }
            else if (m11 > m22)
            {
                float s = (float)Math.Sqrt(1.0f + m11 - m00 - m22) * 2f;
                q = new Quaternion((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
            }
            else
            {
                float s = (float)Math.Sqrt(1.0f + m22 - m00 - m11) * 2f;
                q = new Quaternion((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
            }
            return Normalize(q);
        }

        public static Quaternion SlerpUnclamped(Quaternion a, Quaternion b, float t)
        {
            float dot = Dot(a, b);
            if (dot < 0) { b = new Quaternion(-b.x, -b.y, -b.z, -b.w); dot = -dot; }
            if (dot > 0.9995f) return Normalize(LerpRaw(a, b, t));
            float theta = (float)Math.Acos(Mathf.Clamp(dot, -1, 1));
            float sinTheta = (float)Math.Sin(theta);
            float wa = (float)Math.Sin((1 - t) * theta) / sinTheta;
            float wb = (float)Math.Sin(t * theta) / sinTheta;
            return Normalize(new Quaternion(a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb));
        }
        public static Quaternion Slerp(Quaternion a, Quaternion b, float t) => SlerpUnclamped(a, b, Mathf.Clamp01(t));
        static Quaternion LerpRaw(Quaternion a, Quaternion b, float t) =>
            new Quaternion(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
        public static Quaternion Lerp(Quaternion a, Quaternion b, float t)
        {
            t = Mathf.Clamp01(t);
            if (Dot(a, b) < 0) b = new Quaternion(-b.x, -b.y, -b.z, -b.w);
            return Normalize(LerpRaw(a, b, t));
        }

        public static Quaternion RotateTowards(Quaternion from, Quaternion to, float maxDegreesDelta)
        {
            float angle = Angle(from, to);
            if (angle == 0.0f) return to;
            return SlerpUnclamped(from, to, Math.Min(1.0f, maxDegreesDelta / angle));
        }

        public bool Equals(Quaternion o) => x == o.x && y == o.y && z == o.z && w == o.w;
        public override bool Equals(object? o) => o is Quaternion q && Equals(q);
        public override int GetHashCode() => HashCode.Combine(x, y, z, w);
        public override string ToString() => $"({x:F5}, {y:F5}, {z:F5}, {w:F5})";
    }

    public struct Color : IEquatable<Color>
    {
        public float r, g, b, a;
        public Color(float r, float g, float b, float a) { this.r = r; this.g = g; this.b = b; this.a = a; }
        public Color(float r, float g, float b) { this.r = r; this.g = g; this.b = b; a = 1f; }
        public float this[int i]
        {
            get => i switch { 0 => r, 1 => g, 2 => b, 3 => a, _ => throw new IndexOutOfRangeException() };
            set { switch (i) { case 0: r = value; break; case 1: g = value; break; case 2: b = value; break; case 3: a = value; break; default: throw new IndexOutOfRangeException(); } }
        }
        public static Color red => new Color(1, 0, 0, 1);
        public static Color green => new Color(0, 1, 0, 1);
        public static Color blue => new Color(0, 0, 1, 1);
        public static Color white => new Color(1, 1, 1, 1);
        public static Color black => new Color(0, 0, 0, 1);
        public static Color yellow => new Color(1, 0.921568632f, 0.0156862754f, 1);
        public static Color cyan => new Color(0, 1, 1, 1);
        public static Color magenta => new Color(1, 0, 1, 1);
        public static Color gray => new Color(0.5f, 0.5f, 0.5f, 1);
        public static Color grey => new Color(0.5f, 0.5f, 0.5f, 1);
        public static Color clear => new Color(0, 0, 0, 0);
        public float grayscale => 0.299f * r + 0.587f * g + 0.114f * b;
        public float maxColorComponent => Mathf.Max(Mathf.Max(r, g), b);
        public static Color Lerp(Color a, Color b, float t)
        {
            t = Mathf.Clamp01(t);
            return new Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
        }
        public static Color LerpUnclamped(Color a, Color b, float t) =>
            new Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
        public static Color HSVToRGB(float h, float s, float v)
        {
            if (s == 0) return new Color(v, v, v, 1);
            h = (h - Mathf.Floor(h)) * 6f;
            int i = (int)Mathf.Floor(h);
            float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
            return i switch
            {
                0 => new Color(v, t, p), 1 => new Color(q, v, p), 2 => new Color(p, v, t),
                3 => new Color(p, q, v), 4 => new Color(t, p, v), _ => new Color(v, p, q),
            };
        }
        public static Color operator +(Color a, Color b) => new Color(a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a);
        public static Color operator -(Color a, Color b) => new Color(a.r - b.r, a.g - b.g, a.b - b.b, a.a - b.a);
        public static Color operator *(Color a, Color b) => new Color(a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a);
        public static Color operator *(Color a, float b) => new Color(a.r * b, a.g * b, a.b * b, a.a * b);
        public static Color operator *(float b, Color a) => new Color(a.r * b, a.g * b, a.b * b, a.a * b);
        public static Color operator /(Color a, float b) => new Color(a.r / b, a.g / b, a.b / b, a.a / b);
        public static bool operator ==(Color a, Color b) => (Vector4)a == (Vector4)b;
        public static bool operator !=(Color a, Color b) => !(a == b);
        public static implicit operator Vector4(Color c) => new Vector4(c.r, c.g, c.b, c.a);
        public static implicit operator Color(Vector4 v) => new Color(v.x, v.y, v.z, v.w);
        public bool Equals(Color o) => r == o.r && g == o.g && b == o.b && a == o.a;
        public override bool Equals(object? o) => o is Color c && Equals(c);
        public override int GetHashCode() => HashCode.Combine(r, g, b, a);
        public override string ToString() => $"RGBA({r:F3}, {g:F3}, {b:F3}, {a:F3})";
    }

    public struct Color32 : IEquatable<Color32>
    {
        public byte r, g, b, a;
        public Color32(byte r, byte g, byte b, byte a) { this.r = r; this.g = g; this.b = b; this.a = a; }
        public static implicit operator Color32(Color c) => new Color32(
            (byte)Math.Round(Mathf.Clamp01(c.r) * 255f), (byte)Math.Round(Mathf.Clamp01(c.g) * 255f),
            (byte)Math.Round(Mathf.Clamp01(c.b) * 255f), (byte)Math.Round(Mathf.Clamp01(c.a) * 255f));
        public static implicit operator Color(Color32 c) => new Color(c.r / 255f, c.g / 255f, c.b / 255f, c.a / 255f);
        public bool Equals(Color32 o) => r == o.r && g == o.g && b == o.b && a == o.a;
        public override bool Equals(object? o) => o is Color32 c && Equals(c);
        public override int GetHashCode() => r | g << 8 | b << 16 | a << 24;
    }

    public struct Bounds : IEquatable<Bounds>
    {
        Vector3 m_Center, m_Extents;
        public Bounds(Vector3 center, Vector3 size) { m_Center = center; m_Extents = size * 0.5f; }
        public Vector3 center { get => m_Center; set => m_Center = value; }
        public Vector3 size { get => m_Extents * 2f; set => m_Extents = value * 0.5f; }
        public Vector3 extents { get => m_Extents; set => m_Extents = value; }
        public Vector3 min { get => m_Center - m_Extents; set => SetMinMax(value, max); }
        public Vector3 max { get => m_Center + m_Extents; set => SetMinMax(min, value); }
        public void SetMinMax(Vector3 min, Vector3 max)
        {
            m_Extents = (max - min) * 0.5f;
            m_Center = min + m_Extents;
        }
        public void Encapsulate(Vector3 point) => SetMinMax(Vector3.Min(min, point), Vector3.Max(max, point));
        public void Encapsulate(Bounds b)
        {
            Encapsulate(b.center - b.extents);
            Encapsulate(b.center + b.extents);
        }
        public void Expand(float amount) => m_Extents += new Vector3(amount, amount, amount) * 0.5f;
        public bool Contains(Vector3 p) =>
            p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
        public bool Equals(Bounds o) => m_Center.Equals(o.m_Center) && m_Extents.Equals(o.m_Extents);
        public override bool Equals(object? o) => o is Bounds b && Equals(b);
        public override int GetHashCode() => HashCode.Combine(m_Center, m_Extents);
    }

    public struct Rect
    {
        public float x, y, width, height;
        public Rect(float x, float y, float width, float height) { this.x = x; this.y = y; this.width = width; this.height = height; }
        public float xMin => x;
        public float yMin => y;
        public float xMax => x + width;
        public float yMax => y + height;
        public Vector2 center => new Vector2(x + width / 2f, y + height / 2f);
        public bool Contains(Vector2 p) => p.x >= x && p.x < xMax && p.y >= y && p.y < yMax;
    }
}
