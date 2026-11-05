// US-T93: the sensors and power the player plays (see SensorPosture.h).

#include "Sensors/SensorPosture.h"

#include "Combat/CombatState.h"
#include "Ephemeris.h"
#include "Sensors/Sensors.h"
#include "Ships.h"
#include "World.h"

namespace SpaceGod::Core
{
	PowerState SensorPosture::StateOf(const cs::Ref<ShipCombat>& c)
	{
		if (c == nullptr) return PowerState::Online;
		switch (c->PowerDown)
		{
			case static_cast<int>(PowerState::Dark): return PowerState::Dark;
			case static_cast<int>(PowerState::Restarting): return PowerState::Restarting;
			default: return PowerState::Online;
		}
	}

	bool SensorPosture::Offline(const ShipCombat* c) { return c != nullptr && c->PowerDown != 0; }

	bool SensorPosture::Lit(const ShipCombat* c) { return c != nullptr && c->PingLitS > 0; }

	bool SensorPosture::Bursting(const ShipCombat* c) { return c != nullptr && c->PingLitS > PingLitS - PingBurstS; }

	namespace
	{
		double MassTonnes(const cs::Ref<Ship>& s) { return s != nullptr ? cs::Math::Max(1.0, s->DryMassTonnes()) : Signatures::ReferenceMassTonnes; }
	}

	double SensorPosture::CoolTauS(const cs::Ref<Ship>& s)
	{
		// Heat held goes as volume, shed as surface: the time goes as the cube root of mass.
		return cs::Math::Max(10.0, cs::Math::Min(120.0, 30 * cs::Math::Pow(MassTonnes(s) / Signatures::ReferenceMassTonnes, 1.0 / 3.0)));
	}

	double SensorPosture::RestartSeconds(const cs::Ref<Ship>& s)
	{
		// 8 s a fighter, ~13 s a corvette, ~18 s a freighter, ~34 s a carrier, a minute at most.
		return cs::Math::Max(8.0, cs::Math::Min(60.0, 6 + 6 * cs::Math::Pow(MassTonnes(s) / 1000, 1.0 / 3.0)));
	}

	cs::String SensorPosture::Refusal(const cs::Ref<Ship>& s, int key)
	{
		if (s == nullptr || s->Destroyed || s->Combat == nullptr || !s->Combat->Alive()) return "no hull to command";
		auto c = s->Combat;
		switch (static_cast<SensorKey>(key))
		{
			case SensorKey::Ping:
				if (Offline(c.get())) return "the emitters are down: power up first";
				if (c->PingCooldownS > 0) return cs::Cat("the emitters are recharging: ", static_cast<int>(cs::Math::Ceiling(c->PingCooldownS)), " s");
				return cs::String();
			case SensorKey::GoDark:
				return c->PowerDown == static_cast<int>(PowerState::Dark) ? cs::String("already dark") : cs::String();
			case SensorKey::PowerUp:
				return c->PowerDown == static_cast<int>(PowerState::Dark) ? cs::String() : cs::String("not dark");
			default:
				return "no such command";
		}
	}

	int SensorPosture::PowerToggle(const cs::Ref<Ship>& s)
	{
		return StateOf(s != nullptr ? s->Combat : nullptr) == PowerState::Dark ? static_cast<int>(SensorKey::PowerUp) : static_cast<int>(SensorKey::GoDark);
	}

	bool SensorPosture::Command(const cs::Ref<World>& world, const cs::Ref<Ship>& s, int key)
	{
		if (key == static_cast<int>(SensorKey::None) || !Refusal(s, key).empty()) return false;
		auto c = s->Combat;
		switch (static_cast<SensorKey>(key))
		{
			case SensorKey::Ping:
				c->PingLitS = PingLitS;
				c->PingCooldownS = PingCooldownS;
				if (world != nullptr && world->Sensors != nullptr && world->Ephemeris != nullptr) world->Sensors->PilotPinged(s, world->Ephemeris->Jd);
				return true;
			case SensorKey::GoDark:
				// From online the hull starts as warm as it ran (silent running had it damped already); from a restart, as
				// warm as the restart had brought it back.
				if (c->PowerDown == static_cast<int>(PowerState::Online)) c->ResidualHeat = s->SilentRunning ? Signatures::SilentFactor : 1;
				c->PowerDown = static_cast<int>(PowerState::Dark);
				c->RestartLeftS = 0;
				return true;
			case SensorKey::PowerUp:
				c->PowerDown = static_cast<int>(PowerState::Restarting);
				c->RestartLeftS = RestartSeconds(s);
				return true;
			default:
				return false;
		}
	}

	void SensorPosture::Advance(const cs::Ref<World>& world, double seconds)
	{
		if (world == nullptr) return;
		auto s = world->PilotedShip;
		if (s != nullptr && !s->Destroyed) Advance(s, seconds);
	}

	void SensorPosture::Advance(const cs::Ref<Ship>& s, double seconds)
	{
		auto c = s != nullptr ? s->Combat : nullptr;
		if (c == nullptr || !(seconds > 0)) return;
		if (c->PingLitS > 0) c->PingLitS = cs::Math::Max(0.0, c->PingLitS - seconds);
		if (c->PingCooldownS > 0) c->PingCooldownS = cs::Math::Max(0.0, c->PingCooldownS - seconds);
		switch (StateOf(c))
		{
			case PowerState::Dark:
				// Cooling toward the floor, exponentially: exact over any step, however warped the clock.
				c->ResidualHeat = DarkFloor + (c->ResidualHeat - DarkFloor) * cs::Math::Exp(-seconds / CoolTauS(s));
				break;
			case PowerState::Restarting:
				if (seconds >= c->RestartLeftS)
				{
					c->PowerDown = static_cast<int>(PowerState::Online);
					c->RestartLeftS = 0;
					c->ResidualHeat = 1;
				}
				else
				{
					// The heat climbs back in a straight line, to a running hull's as the restart completes.
					c->ResidualHeat += (1 - c->ResidualHeat) * seconds / c->RestartLeftS;
					c->RestartLeftS -= seconds;
				}
				break;
			default:
				break;
		}
	}

	void SensorPosture::Reset(const cs::Ref<ShipCombat>& c)
	{
		if (c == nullptr) return;
		c->PowerDown = static_cast<int>(PowerState::Online);
		c->RestartLeftS = 0;
		c->ResidualHeat = 1;
		c->PingLitS = 0;
		c->PingCooldownS = 0;
	}
}
