// Port of Unity.SpaceGod Core/Trajectories.cs (see docs/MIGRATION.md).

#include "Trajectories.h"
#include "Astro.h"

#include <cmath>

namespace SpaceGod::Core
{
	const char* ToString(DriveKind v)
	{
		switch (v)
		{
		case DriveKind::Torch: return "Torch";
		case DriveKind::LowThrust: return "LowThrust";
		}
		return "";
	}

	// ---------------------------------------------------------------- Conics

	double Conics::StumpffC(double z)
	{
		// 1 - cos x as 2·sin²(x/2): exact even as √z nears 2π, where the Lambert solver works.
		if (z > 1e-6)
		{
			double h = cs::Math::Sin(0.5 * cs::Math::Sqrt(z));
			return 2 * h * h / z;
		}
		if (z < -1e-6)
		{
			double h = cs::Math::Sinh(0.5 * cs::Math::Sqrt(-z));
			return 2 * h * h / -z;
		}
		return 0.5 - z / 24 + z * z / 720;
	}

	double Conics::StumpffS(double z)
	{
		if (z > 1e-6)
		{
			double s = cs::Math::Sqrt(z);
			return (s - cs::Math::Sin(s)) / (s * s * s);
		}
		if (z < -1e-6)
		{
			double s = cs::Math::Sqrt(-z);
			return (cs::Math::Sinh(s) - s) / (s * s * s);
		}
		return 1.0 / 6 - z / 120 + z * z / 5040;
	}

	void Conics::Propagate(DVec3 r0, DVec3 v0, double mu, double dt, DVec3& r, DVec3& v)
	{
		double r0n = r0.Length();
		if (mu <= 0 || r0n <= 0 || dt == 0)
		{
			r = r0 + v0 * dt;
			v = v0;
			return;
		}
		double sqrtMu = cs::Math::Sqrt(mu);
		double vr0 = DVec3::Dot(r0, v0) / r0n;
		double alpha = 2 / r0n - v0.LengthSquared() / mu;

		// dt(χ) climbs monotonically with χ (its slope is r/√μ > 0): bracket the root, then Newton
		// inside the bracket, falling back to bisection whenever a step would leave it.
		auto Residual = [&](double x, double& slope)
		{
			double z = alpha * x * x;
			double c = StumpffC(z), s = StumpffS(z);
			double f = r0n * vr0 / sqrtMu * x * x * c + (1 - alpha * r0n) * x * x * x * s + r0n * x - sqrtMu * dt;
			slope = r0n * vr0 / sqrtMu * x * (1 - z * s) + (1 - alpha * r0n) * x * x * c + r0n;
			return f;
		};

		double sign = cs::Math::Sign(dt);
		double lo = 0, hi = sign * cs::Math::Max(1e-12, sqrtMu * cs::Math::Abs(dt) / r0n);
		double discard;
		for (int i = 0; i < 200 && sign * Residual(hi, discard) < 0; i++)
		{
			lo = hi;
			hi *= 2;
		}
		if (sign < 0)
		{
			double t = lo;
			lo = hi;
			hi = t;
		}
		double chi = 0.5 * (lo + hi);
		for (int i = 0; i < 100; i++)
		{
			double slope;
			double f = Residual(chi, slope);
			if (f < 0) lo = chi;
			else hi = chi;
			double next = slope > 0 ? chi - f / slope : cs::Double::NaN;
			if (!(next > lo && next < hi)) next = 0.5 * (lo + hi);
			if (cs::Math::Abs(next - chi) <= 1e-13 * cs::Math::Max(1.0, cs::Math::Abs(chi))) { chi = next; break; }
			chi = next;
		}

		double zz = alpha * chi * chi;
		double cc = StumpffC(zz), ss = StumpffS(zz);
		double fl = 1 - chi * chi / r0n * cc;
		double gl = dt - chi * chi * chi / sqrtMu * ss;
		r = r0 * fl + v0 * gl;
		double rn = r.Length();
		double fdot = sqrtMu / (rn * r0n) * (alpha * chi * chi * chi * ss - chi);
		double gdot = 1 - chi * chi / rn * cc;
		v = r0 * fdot + v0 * gdot;
	}

	DVec3 Conics::PositionAfter(DVec3 r0, DVec3 v0, double mu, double dt)
	{
		DVec3 r, discard;
		Propagate(r0, v0, mu, dt, r, discard);
		return r;
	}

	bool Conics::Lambert(DVec3 r1, DVec3 r2, double tof, double mu, DVec3 normal, DVec3& v1, DVec3& v2)
	{
		v1 = v2 = DVec3::Zero;
		double r1n = r1.Length(), r2n = r2.Length();
		if (mu <= 0 || tof <= 0 || r1n <= 0 || r2n <= 0) return false;
		double cosTheta = cs::Math::Max(-1.0, cs::Math::Min(1.0, DVec3::Dot(r1, r2) / (r1n * r2n)));
		double theta = cs::Math::Acos(cosTheta);
		if (DVec3::Dot(DVec3::Cross(r1, r2), normal) < 0) theta = Astro::TwoPi - theta;
		double sinTheta = cs::Math::Sin(theta);
		if (1 - cosTheta < 1e-12 || cs::Math::Abs(sinTheta) < 1e-9) return false;
		double a = sinTheta * cs::Math::Sqrt(r1n * r2n / (1 - cosTheta));
		double sqrtMu = cs::Math::Sqrt(mu);

		auto Y = [&](double z) { return r1n + r2n + a * (z * StumpffS(z) - 1) / cs::Math::Sqrt(StumpffC(z)); };

		// F(z) = time-of-flight residual, rising with z; y < 0 (no orbit there) counts as too short.
		auto F = [&](double z)
		{
			double y = Y(z);
			if (y < 0) return cs::Double::NegativeInfinity;
			double c = StumpffC(z);
			return cs::Math::Pow(y / c, 1.5) * StumpffS(z) + a * cs::Math::Sqrt(y) - sqrtMu * tof;
		};

		double hi = Astro::TwoPi * Astro::TwoPi * (1 - 1e-9);
		double lo = -4 * cs::Math::PI * cs::Math::PI;
		for (int i = 0; i < 80 && F(lo) > 0; i++) lo *= 2;
		if (F(lo) > 0 || !(F(hi) > 0)) return false;
		for (int i = 0; i < 200; i++)
		{
			double mid = 0.5 * (lo + hi);
			if (F(mid) > 0) hi = mid;
			else lo = mid;
			if (hi - lo < 1e-14 * cs::Math::Max(1.0, cs::Math::Abs(mid))) break;
		}
		double zr = 0.5 * (lo + hi);
		double yr = Y(zr);
		if (!(yr > 0)) return false;
		double f = 1 - yr / r1n;
		double g = a * cs::Math::Sqrt(yr / mu);
		double gdot = 1 - yr / r2n;
		if (cs::Math::Abs(g) < 1e-300) return false;
		v1 = (r2 - r1 * f) / g;
		v2 = (r2 * gdot - r1) / g;
		return IsFinite(v1) && IsFinite(v2);
	}

	bool Conics::IsFinite(DVec3 v) { return !cs::Double::IsNaN(v.X + v.Y + v.Z) && !cs::Double::IsInfinity(v.X + v.Y + v.Z); }

	double Conics::HohmannTime(double mu, double r1, double r2)
	{
		double a = 0.5 * (r1 + r2);
		return cs::Math::PI * cs::Math::Sqrt(a * a * a / mu);
	}

	void Conics::HohmannDeltaV(double mu, double r1, double r2, double& dv1, double& dv2)
	{
		double a = 0.5 * (r1 + r2);
		dv1 = cs::Math::Abs(cs::Math::Sqrt(mu * (2 / r1 - 1 / a)) - CircularSpeed(mu, r1));
		dv2 = cs::Math::Abs(CircularSpeed(mu, r2) - cs::Math::Sqrt(mu * (2 / r2 - 1 / a)));
	}

	double Conics::WaitForPhase(double phase, double lead, double n1, double n2)
	{
		double rate = n2 - n1;
		if (cs::Math::Abs(rate) < 1e-15) return cs::Double::PositiveInfinity;
		double gap = rate > 0 ? lead - phase : phase - lead;
		gap = std::fmod(gap, Astro::TwoPi); // C# double %
		if (gap < 0) gap += Astro::TwoPi;
		return gap / cs::Math::Abs(rate);
	}

	// ---------------------------------------------------------------- Hyperbola

	cs::Ref<Hyperbola> Hyperbola::Through(double mu, DVec3 periapsis, DVec3 vInf, DVec3 fallbackNormal)
	{
		double rp = periapsis.Length(), v = vInf.Length();
		if (mu <= 0 || rp <= 0 || v <= 0) return nullptr;
		auto p = periapsis / rp;
		auto q = vInf / v - p * DVec3::Dot(vInf / v, p);
		if (q.LengthSquared() < 1e-12) q = DVec3::Cross(fallbackNormal, p);
		if (q.LengthSquared() < 1e-12) q = DVec3::Cross(cs::Math::Abs(p.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX, p);
		double e = 1 + rp * v * v / mu;
		auto h = cs::New<Hyperbola>();
		h->Mu = mu; h->Rp = rp; h->VInf = v; h->E = e; h->A = -mu / (v * v); h->P = p; h->Q = q.Normalized();
		return h;
	}

	cs::Ref<Hyperbola> Hyperbola::Build(double mu, double rp, DVec3 vInf, DVec3 normal, bool leaving)
	{
		double v = vInf.Length();
		if (mu <= 0 || rp <= 0 || v <= 0) return nullptr;
		auto vHat = vInf / v;
		auto n = normal - vHat * DVec3::Dot(normal, vHat);
		if (n.LengthSquared() < 1e-12)
		{
			n = DVec3::Cross(vHat, cs::Math::Abs(vHat.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX);
			n = DVec3::Cross(n, vHat);
		}
		n = n.Normalized();
		auto t = DVec3::Cross(n, vHat);
		double e = 1 + rp * v * v / mu;
		double c = -1 / e, s = cs::Math::Sqrt(1 - c * c);
		// Outgoing asymptote cosθ∞·P + sinθ∞·Q = v̂; incoming one -cosθ∞·P + sinθ∞·Q = v̂ (Q = n × P).
		auto p = leaving ? vHat * c - t * s : vHat * -c - t * s;
		auto h = cs::New<Hyperbola>();
		h->Mu = mu; h->Rp = rp; h->VInf = v; h->E = e; h->A = -mu / (v * v); h->P = p; h->Q = DVec3::Cross(n, p);
		return h;
	}

	double Hyperbola::AsymptoteError(double mu, DVec3 periapsis, DVec3 vInf, bool leaving)
	{
		(void)leaving; // unused in the original too
		double rp = periapsis.Length(), v = vInf.Length();
		if (rp <= 0 || v <= 0) return 0;
		double e = 1 + rp * v * v / mu;
		double theta = cs::Math::Acos(cs::Math::Max(-1.0, cs::Math::Min(1.0, DVec3::Dot(periapsis / rp, vInf / v))));
		return cs::Math::Abs(theta - cs::Math::Acos(-1 / e));
	}

	DVec3 Hyperbola::PositionAt(double t) const
	{
		double m = MeanMotion() * t;
		double f = cs::Math::Abs(m) < 1e-12 ? 0 : Asinh(m / E);
		for (int i = 0; i < 60; i++)
		{
			double g = E * cs::Math::Sinh(f) - f - m;
			double gp = E * cs::Math::Cosh(f) - 1;
			double step = g / gp;
			f -= step;
			if (cs::Math::Abs(step) < 1e-13 * cs::Math::Max(1.0, cs::Math::Abs(f))) break;
		}
		double aa = -A;
		double x = aa * (E - cs::Math::Cosh(f));
		double y = aa * cs::Math::Sqrt(E * E - 1) * cs::Math::Sinh(f);
		return P * x + Q * y;
	}

	DVec3 Hyperbola::VelocityAt(double t) const
	{
		auto p = PositionAt(t);
		double r = p.Length();
		double aa = -A;
		// cosh F from the radius, sinh F from the side of periapsis it is on.
		double coshF = (r / aa + 1) / E;
		double sinhF = cs::Math::Sign(t) * cs::Math::Sqrt(cs::Math::Max(0.0, coshF * coshF - 1));
		double fDot = MeanMotion() / (E * coshF - 1);
		return P * (-aa * sinhF * fDot) + Q * (aa * cs::Math::Sqrt(E * E - 1) * coshF * fDot);
	}

	double Hyperbola::TimeToRadius(double r) const
	{
		if (r <= Rp) return 0;
		double aa = -A;
		double coshF = (r / aa + 1) / E;
		double f = cs::Math::Log(coshF + cs::Math::Sqrt(cs::Math::Max(0.0, coshF * coshF - 1)));
		return (E * cs::Math::Sinh(f) - f) / MeanMotion();
	}

	// ---------------------------------------------------------------- ConicTransfer

	DVec3 ConicTransfer::OffsetAt(double tau) const
	{
		tau = cs::Math::Max(0.0, cs::Math::Min(Days, tau));
		if (Escape != nullptr && tau < ExitDays)
			return Conics::PositionAfter(FromR0, FromV0, Mu, tau) + Escape->PositionAt(tau);
		if (Capture != nullptr && tau > Days - EntryDays)
		{
			double t = tau - Days;
			return Conics::PositionAfter(ToR1, ToV1, Mu, t) + Capture->PositionAt(t);
		}
		return Conics::PositionAfter(R0, V0, Mu, tau - ExitDays);
	}
}
