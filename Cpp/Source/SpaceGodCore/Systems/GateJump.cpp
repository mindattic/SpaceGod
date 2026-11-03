// Port of Assets/UnitySpace/Core/Systems/GateJump.cs (US-T17).

#include "Systems/GateJump.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/JumpGate.h"

namespace SpaceGod::Core
{
	const char* ToString(GateJump::Phase v)
	{
		switch (v)
		{
		case GateJump::Phase::Approach: return "Approach";
		case GateJump::Phase::Plunge: return "Plunge";
		case GateJump::Phase::Tunnel: return "Tunnel";
		case GateJump::Phase::Emerge: return "Emerge";
		}
		return "";
	}

	bool GateJump::TryChoose(const cs::Ref<GateNetwork>& network, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, cs::Ref<JumpGate>& departure, cs::Ref<JumpGate>& arrival)
	{
		departure = arrival = nullptr;
		if (from == nullptr || to == nullptr || from == to || from->Gates().Count() == 0) return false;
		auto direct = from->GateTo(to);
		if (direct != nullptr && direct->Partner != nullptr && direct->Link->Working())
		{
			departure = direct;
			arrival = direct->Partner;
			return true;
		}
		auto route = network ? network->Route(from, to) : nullptr;
		if (route == nullptr || route->Count() == 0) return false;
		auto last = (*route)[route->Count() - 1]->Partner;
		if ((*route)[0]->Home != from || last == nullptr || last->Home != to) return false;
		departure = (*route)[0];
		arrival = last;
		return true;
	}

	bool GateJump::TryPlan(const cs::Ref<GateNetwork>& network, const cs::Ref<StarSystem>& from, const cs::Ref<StarSystem>& to, cs::Ref<cs::List<cs::Ref<JumpGate>>>& route)
	{
		route = cs::New<cs::List<cs::Ref<JumpGate>>>();
		if (from == nullptr || to == nullptr || from == to || from->Gates().Count() == 0) return false;
		auto direct = from->GateTo(to);
		if (direct != nullptr && direct->Partner != nullptr && direct->Link->Working())
		{
			route->Add(direct);
			return true;
		}
		auto planned = network ? network->Route(from, to) : nullptr;
		if (planned == nullptr || planned->Count() == 0 || (*planned)[0]->Home != from
			|| ((*planned)[planned->Count() - 1]->Partner ? (*planned)[planned->Count() - 1]->Partner->Home : nullptr) != to)
			return false;
		route = planned;
		return true;
	}

	double GateJump::Overall(Phase phase, double t)
	{
		double before = 0;
		for (auto p = Phase::Approach; p < phase; p = static_cast<Phase>(static_cast<int>(p) + 1)) before += Seconds(p);
		return Clamp01((before + Clamp01(t) * Seconds(phase)) / TotalSeconds);
	}

	double GateJump::PlungeRemaining(double t)
	{
		t = Clamp01(t);
		return 1 - t * t * t;
	}

	double GateJump::Warp(Phase phase, double t)
	{
		t = Clamp01(t);
		switch (phase)
		{
		case Phase::Approach: return 0;
		case Phase::Plunge: return 0.6 * Smoother((t - 0.35) / 0.65);
		case Phase::Tunnel: return 0.6 + 0.4 * cs::Math::Sin(cs::Math::PI * t);
		default: return 0.6 * (1 - Smoother(t / 0.15));
		}
	}

	double GateJump::Flash(Phase phase, double t)
	{
		t = Clamp01(t);
		switch (phase)
		{
		case Phase::Approach: return 0;
		case Phase::Plunge: return Smoother((t - 0.7) / 0.3);
		case Phase::Tunnel: return cs::Math::Max(1 - Smoother(t / 0.2), Smoother((t - 0.85) / 0.15));
		default: return 1 - Smoother(t / 0.18);
		}
	}

	double GateJump::FovKick(Phase phase, double t)
	{
		t = Clamp01(t);
		switch (phase)
		{
		case Phase::Approach: return 0;
		case Phase::Plunge: return FovKickDeg * t * t;
		case Phase::Tunnel: return FovKickDeg * (1 - 1.6 * Fold(phase, t));
		default: return FovKickDeg * (1 - Smoother(t / 0.25));
		}
	}

	double GateJump::Fold(Phase phase, double t)
	{
		if (phase != Phase::Tunnel) return 0;
		t = Clamp01(t);
		return Smoother((t - 0.7) / 0.22) * (1 - Smoother((t - 0.95) / 0.05));
	}
}
