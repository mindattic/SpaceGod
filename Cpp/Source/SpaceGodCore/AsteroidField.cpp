// Port of Unity.SpaceGod Core/AsteroidField.cs (see docs/MIGRATION.md).

#include "AsteroidField.h"
#include "Astro.h"
#include "BodyDefinition.h"
#include "Kepler.h"
#include "ScaleModel.h"

#include <cstring>

namespace SpaceGod::Core
{
	namespace
	{
		// BitConverter.ToInt32/ToDouble/ToSingle: little-endian, as every platform Core runs on.
		template <class T> T Read(const cs::List<uint8_t>& data, int offset)
		{
			if (offset < 0 || offset + static_cast<int>(sizeof(T)) > data.Count()) throw cs::ArgumentOutOfRangeException("startIndex");
			T v;
			std::memcpy(&v, data.Raw().data() + offset, sizeof(T));
			return v;
		}
	}

	AsteroidField::AsteroidField(int count, double epochJd)
	{
		Count = count;
		EpochJd = epochJd;
		A = cs::NewArray<float>(count);
		E = cs::NewArray<float>(count);
		InclinationDeg = cs::NewArray<float>(count);
		NodeDeg = cs::NewArray<float>(count);
		ArgPeriDeg = cs::NewArray<float>(count);
		MeanAnomalyDeg = cs::NewArray<float>(count);
		H = cs::NewArray<float>(count);
		Number = cs::NewArray<int>(count);
		Group = cs::NewArray<uint8_t>(count);
		MeanMotion = cs::NewArray<double>(count);
		px = cs::NewArray<double>(count);
		py = cs::NewArray<double>(count);
		pz = cs::NewArray<double>(count);
		qx = cs::NewArray<double>(count);
		qy = cs::NewArray<double>(count);
		qz = cs::NewArray<double>(count);
	}

	cs::Ref<AsteroidField> AsteroidField::Parse(const cs::List<uint8_t>& data)
	{
		if (data.Count() < 16 || data[0] != 'A' || data[1] != 'S' || data[2] != 'T' || data[3] != '1')
			throw cs::FormatException("Not an AST1 asteroid file");
		int count = Read<int32_t>(data, 4);
		double epoch = Read<double>(data, 8);
		if (count < 0 || data.Count() < 16 + static_cast<long long>(count) * RecordBytes)
			throw cs::FormatException("Truncated asteroid file");

		// The constructor is private (as in C#), so not cs::New.
		cs::Ref<AsteroidField> f(new AsteroidField(count, epoch));
		int o = 16;
		for (int i = 0; i < count; i++, o += RecordBytes)
		{
			f->A[i] = Read<float>(data, o);
			f->E[i] = Read<float>(data, o + 4);
			f->InclinationDeg[i] = Read<float>(data, o + 8);
			f->NodeDeg[i] = Read<float>(data, o + 12);
			f->ArgPeriDeg[i] = Read<float>(data, o + 16);
			f->MeanAnomalyDeg[i] = Read<float>(data, o + 20);
			f->H[i] = Read<float>(data, o + 24);
			f->Number[i] = Read<int32_t>(data, o + 28);
			f->Group[i] = data[o + 32];
			f->Derive(i);
		}
		return f;
	}

	void AsteroidField::Derive(int i)
	{
		double a = A[i];
		MeanMotion[i] = GaussK / (a * cs::Math::Sqrt(a));
		double w = ArgPeriDeg[i] * Astro::Deg2Rad, n = NodeDeg[i] * Astro::Deg2Rad, inc = InclinationDeg[i] * Astro::Deg2Rad;
		double cw = cs::Math::Cos(w), sw = cs::Math::Sin(w), cn = cs::Math::Cos(n), sn = cs::Math::Sin(n), ci = cs::Math::Cos(inc), si = cs::Math::Sin(inc);
		px[i] = cw * cn - sw * sn * ci;
		py[i] = cw * sn + sw * cn * ci;
		pz[i] = sw * si;
		qx[i] = -sw * cn - cw * sn * ci;
		qy[i] = -sw * sn + cw * cn * ci;
		qz[i] = cw * si;
	}

	double AsteroidField::DiameterKm(int i) const
	{
		return 1329 / cs::Math::Sqrt(TypicalAlbedo) * cs::Math::Pow(10, -H[i] / 5.0);
	}

	DVec3 AsteroidField::PositionAu(int i, double jd) const
	{
		double m = MeanAnomalyDeg[i] * Astro::Deg2Rad + MeanMotion[i] * (jd - EpochJd);
		double e = E[i];
		double ecc = Kepler::SolveEccentricAnomaly(m, e);
		double x = A[i] * (cs::Math::Cos(ecc) - e);
		double y = A[i] * cs::Math::Sqrt(1 - e * e) * cs::Math::Sin(ecc);
		return DVec3(px[i] * x + qx[i] * y, py[i] * x + qy[i] * y, pz[i] * x + qz[i] * y);
	}

	void AsteroidField::PositionsAu(double jd, int start, int count, cs::List<float>& xyz) const
	{
		int end = cs::Math::Min(Count, start + count);
		double dt = jd - EpochJd;
		for (int i = start; i < end; i++)
		{
			double e = E[i];
			double m = Astro::WrapRadians(MeanAnomalyDeg[i] * Astro::Deg2Rad + MeanMotion[i] * dt);
			// Newton's method from a good start; e < 0.5 for everything in the file.
			double ecc = m + e * cs::Math::Sin(m);
			for (int k = 0; k < 6; k++)
				ecc -= (ecc - e * cs::Math::Sin(ecc) - m) / (1 - e * cs::Math::Cos(ecc));
			double x = A[i] * (cs::Math::Cos(ecc) - e);
			double y = A[i] * cs::Math::Sqrt(1 - e * e) * cs::Math::Sin(ecc);
			xyz[i * 3] = static_cast<float>(px[i] * x + qx[i] * y);
			xyz[i * 3 + 1] = static_cast<float>(py[i] * x + qy[i] * y);
			xyz[i * 3 + 2] = static_cast<float>(pz[i] * x + qz[i] * y);
		}
	}

	void AsteroidField::PackGpu(cs::List<float>& dest) const
	{
		for (int i = 0; i < Count; i++)
		{
			int o = i * GpuFloatsPerAsteroid;
			double a = A[i], b = a * cs::Math::Sqrt(1 - E[i] * static_cast<double>(E[i]));
			dest[o] = E[i];
			dest[o + 1] = static_cast<float>(MeanMotion[i]);
			dest[o + 2] = static_cast<float>(Astro::WrapRadians(MeanAnomalyDeg[i] * Astro::Deg2Rad));
			dest[o + 3] = H[i];
			dest[o + 4] = static_cast<float>(a * px[i]);
			dest[o + 5] = static_cast<float>(a * py[i]);
			dest[o + 6] = static_cast<float>(a * pz[i]);
			dest[o + 8] = static_cast<float>(b * qx[i]);
			dest[o + 9] = static_cast<float>(b * qy[i]);
			dest[o + 10] = static_cast<float>(b * qz[i]);
		}
	}

	void AsteroidField::GpuPosition(const cs::List<float>& packed, int i, float dtDays, float& x, float& y, float& z)
	{
		int o = i * GpuFloatsPerAsteroid;
		float e = packed[o];
		float m = packed[o + 2] + packed[o + 1] * dtDays;
		constexpr float twoPi = 6.2831853f;
		m -= twoPi * static_cast<float>(cs::Math::Floor(static_cast<double>(m / twoPi + 0.5f)));
		float ecc = m + e * static_cast<float>(cs::Math::Sin(static_cast<double>(m)));
		for (int k = 0; k < 5; k++)
			ecc -= (ecc - e * static_cast<float>(cs::Math::Sin(static_cast<double>(ecc))) - m) / (1 - e * static_cast<float>(cs::Math::Cos(static_cast<double>(ecc))));
		float c = static_cast<float>(cs::Math::Cos(static_cast<double>(ecc))) - e, s = static_cast<float>(cs::Math::Sin(static_cast<double>(ecc)));
		x = packed[o + 4] * c + packed[o + 8] * s;
		y = packed[o + 5] * c + packed[o + 9] * s;
		z = packed[o + 6] * c + packed[o + 10] * s;
	}

	int AsteroidField::IndexOfNumber(int number) const
	{
		for (int i = 0; i < Count; i++)
			if (Number[i] == number) return i;
		return -1;
	}

	void AsteroidField::SunMapping(const cs::Ref<ScaleModel>& scale, const cs::Ref<BodyDefinition>& sun, cs::List<float>& c0, cs::List<float>& c1)
	{
		double sunAu = sun->RadiusKm / Astro::AuKm;
		c0 = cs::List<float>
		{
			static_cast<float>(scale->Blend()), static_cast<float>(Astro::AuKm / ScaleModel::KmPerUnit),
			static_cast<float>(ScaleModel::GameplayRadius(sun->RadiusKm)), static_cast<float>(ScaleModel::GameAuScale),
		};
		c1 = cs::List<float> { static_cast<float>(ScaleModel::GameAuExponent), static_cast<float>(cs::Math::Pow(sunAu, ScaleModel::GameAuExponent)), 0.0f, 0.0f };
	}

	float AsteroidField::SceneDistanceFromSun(const cs::List<float>& c0, const cs::List<float>& c1, float dAu)
	{
		float real = dAu * c0[1];
		float game = c0[2] + c0[3] * (static_cast<float>(cs::Math::Pow(dAu, c1[0])) - c1[1]);
		return static_cast<float>(cs::Math::Exp(cs::Math::Log(real) + (cs::Math::Log(game) - cs::Math::Log(real)) * c0[0]));
	}
}
