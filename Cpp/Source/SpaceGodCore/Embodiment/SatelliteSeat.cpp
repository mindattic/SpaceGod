// Port of Unity.SpaceGod Core/Embodiment/SatelliteSeat.cs (US-T21, see docs/MIGRATION.md).

#include "Embodiment/SatelliteSeat.h"
#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "World.h"
#include "Infrastructure/Infrastructure.h"
#include "Infrastructure/Satellite.h"

namespace SpaceGod::Core
{
	SatelliteSeat::SatelliteSeat(const cs::Ref<Core::Constellation>& constellation, const cs::Ref<BodyDefinition>& body, int index, cs::Func<double> now)
	{
		Constellation = constellation;
		Body = body;
		Index = index;
		this->now = std::move(now);
	}

	cs::Ref<SatelliteSeat> SatelliteSeat::Of(const cs::Ref<World>& world, const cs::Ref<Core::Constellation>& constellation, int index)
	{
		return cs::New<SatelliteSeat>(constellation, constellation == nullptr ? nullptr : (*world->Catalog())[constellation->Body], index,
			cs::Func<double>([world] { return world->Jd; }));
	}

	cs::Ref<SatelliteSeat> SatelliteSeat::Nearest(const cs::Ref<World>& world, int body, DVec3 offsetKm, double jd)
	{
		auto infra = world ? world->Infrastructure : nullptr;
		if (infra == nullptr) return nullptr;
		cs::Ref<Core::Constellation> best = nullptr;
		int bestIndex = -1;
		double bestKm2 = cs::Double::MaxValue;
		for (const auto& c : infra->Constellations)
		{
			if (c->Body != body) continue;
			auto points = cs::NewArray<DVec3>(c->Count());
			c->Offsets((*world->Catalog())[body], jd, points);
			for (int i = 0; i < points.Count(); i++)
			{
				double d = (points[i] - offsetKm).LengthSquared();
				if (d >= bestKm2) continue;
				bestKm2 = d;
				best = c;
				bestIndex = i;
			}
		}
		return best == nullptr ? nullptr : Of(world, best, bestIndex);
	}

	cs::Ref<Core::Satellite> SatelliteSeat::Satellite() const
	{
		cs::Ref<Core::Satellite> s;
		return Constellation != nullptr && Constellation->Flying.TryGetValue(Index, s) ? s : nullptr;
	}

	cs::String SatelliteSeat::Name() const { return Constellation != nullptr ? Constellation->NameOf(Index) : cs::String("satellite"); }

	bool SatelliteSeat::Embodied() const
	{
		auto s = Satellite();
		return s ? s->Held() : false;
	}

	cs::String SatelliteSeat::Blocker() const
	{
		if (Constellation == nullptr || Body == nullptr || Index < 0 || Index >= Constellation->Count()) return "nothing there";
		return cs::String();
	}

	EmbodimentPose SatelliteSeat::PoseAt(double jd)
	{
		auto s = Satellite();
		if (s != nullptr)
		{
			s->AdvanceTo(jd);
			return EmbodimentPose(Body->Index, s->OffsetKm, s->Forward, s->Up);
		}
		DVec3 offset, velocity;
		Constellation->SlotAt(Body, Index, jd, offset, velocity);
		DVec3 f, u;
		Core::Satellite::WorkingAttitude(offset, velocity, f, u);
		return EmbodimentPose(Body->Index, offset, f, u);
	}

	void SatelliteSeat::TakeCharge()
	{
		double jd = now ? now() : cs::Double::NaN;
		if (cs::Double::IsNaN(jd)) jd = Astro::J2000;
		auto s = Constellation->Detach(Body, Index, jd);
		s->AdvanceTo(jd);
		s->SetHeld(true);
		s->Command = SatelliteCommand{};
	}

	void SatelliteSeat::Control(const EmbodimentInput& input, double dt)
	{
		auto s = Satellite();
		if (s == nullptr || !s->Held()) return;
		SatelliteCommand command;
		command.Aim = input.Aim;
		command.Throttle = input.Throttle;
		command.StrafeRight = input.StrafeRight;
		command.StrafeUp = input.StrafeUp;
		command.Roll = input.Roll;
		s->Command = command;
	}

	void SatelliteSeat::HandBack()
	{
		auto s = Satellite();
		if (s == nullptr) return;
		s->SetHeld(false);
		s->Command = SatelliteCommand{};
	}

	bool SatelliteSeat::Equals(const cs::Ref<IEmbodiable>& obj) const
	{
		auto t = cs::As<SatelliteSeat>(obj);
		return t && t->Constellation == Constellation && t->Index == Index;
	}

	size_t SatelliteSeat::GetHashCode() const
	{
		// C#: (Constellation?.GetHashCode() ?? 0) * 397 ^ Index (only lookup uses it; the value need not match .NET's).
		size_t h = Constellation ? cs::Hash<cs::Ref<Core::Constellation>>{}(Constellation) : 0;
		return (h * 397) ^ static_cast<size_t>(Index);
	}
}
