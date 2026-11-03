// Port of Assets/UnitySpace/Core/Combat/Salvage.cs (see docs/MIGRATION.md).

#include "Combat/Salvage.h"

#include "Fleet.h"
#include "Ships.h"
#include "Combat/Boarding.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Economy/CargoHold.h"
#include "Economy/Goods.h"

namespace SpaceGod::Core
{
	const char* ToString(LatchState v)
	{
		switch (v)
		{
			case LatchState::TooFar: return "TooFar";
			case LatchState::TooFast: return "TooFast";
			case LatchState::Ready: return "Ready";
		}
		return "";
	}

	const char* ToString(StripUse v)
	{
		switch (v)
		{
			case StripUse::None: return "None";
			case StripUse::Fitted: return "Fitted";
			case StripUse::Scrapped: return "Scrapped";
			case StripUse::NoRoom: return "NoRoom";
		}
		return "";
	}

	double SalvageSession::LatchRangeKm(double hulkLengthKm, double playerLengthKm)
	{
		return LatchClearanceKm + 0.5 * (cs::Math::Max(0.0, hulkLengthKm) + cs::Math::Max(0.0, playerLengthKm));
	}

	LatchState SalvageSession::Check(DVec3 relativeKm, DVec3 relativeVelocityKmS, double rangeKm)
	{
		if (relativeKm.Length() > rangeKm) return LatchState::TooFar;
		if (relativeVelocityKmS.Length() > MatchSpeedKmS) return LatchState::TooFast;
		return LatchState::Ready;
	}

	// ---------------------------------------------------------------- left adrift

	void SalvageSession::Strand(const cs::Ref<Ship>& ship, const cs::Ref<Faction>& raiders, Good good, double leftTonnes, double jd, double aboard)
	{
		ship->Salvage.Clear();
		if (leftTonnes > 0) ship->Salvage.Add(SalvageStack(good, leftTonnes));
		auto crewSpec = ship->Combat ? ship->Combat->Spec : nullptr;
		if (crewSpec != nullptr && crewSpec->Crew >= 1)
		{
			ship->Salvage.Add(SalvageStack(Good::Food, CargoFootprint::Of(Good::Food).Tonnes));
			ship->Salvage.Add(SalvageStack(Good::MedicalSupplies, CargoFootprint::Of(Good::MedicalSupplies).Tonnes));
		}
		if (ship->Spec != nullptr && ship->Spec->IsMilitary) ship->Salvage.Add(SalvageStack(Good::Munitions, 2 * CargoFootprint::Of(Good::Munitions).Tonnes));
		if (ship->Combat == nullptr) return;

		cs::Random rng(cs::WrapAdd(cs::WrapMul(ship->Id, 7919), cs::ToInt(cs::Math::Floor(jd * 24))));
		double survivors = 0, lurkers = 0;
		if (rng.NextDouble() < 0.35) survivors = cs::Math::Max(1.0, cs::Math::Round(ship->Combat->Spec->Crew * (0.05 + 0.15 * rng.NextDouble())));
		// Only those still aboard can hide there.
		survivors = cs::Math::Min(survivors, cs::Math::Floor(aboard));
		if (rng.NextDouble() < 0.3) lurkers = 2 + rng.Next(7);
		ship->Combat->Interior->Abandon(survivors, lurkers, raiders ? raiders->MarineQuality : 1);
	}

	// ---------------------------------------------------------------- the session

	cs::List<SalvageStack>& SalvageSession::Manifest() const { return Derelict != nullptr ? Derelict->Salvage : Wreck->Salvage; }

	cs::String SalvageSession::Name() const { return Derelict != nullptr ? Derelict->Name : cs::Cat("the wreck of ", Wreck->Name); }

	cs::Ref<Faction> SalvageSession::Holders() const { return Derelict ? Derelict->DerelictBy : nullptr; }

	bool SalvageSession::Hostile() const { return Interior->Marines() > 0.5; }

	bool SalvageSession::Fighting() const { return Interior->UnderBoarding(); }

	SalvageSession::SalvageSession(cs::Ref<Ship> player, cs::Ref<Ship> derelict, cs::Ref<SpaceGod::Core::Wreck> wreck,
		cs::Ref<SpaceGod::Core::Interior> interior, int seed)
		: Player(player), Derelict(derelict), Wreck(wreck), Interior(interior),
		  Owners(derelict != nullptr ? derelict->Faction : wreck->Faction), rng(seed)
	{
		Note(cs::Cat("Clamps on: latched to ", Name()));
		if (Hostile()) Note(cs::Cat(cs::Fmt(Interior->Marines(), "0"), " ", Holders() ? Holders()->Name : cs::String("raiders"), " still aboard, armed"));
		else RescueSurvivors();
	}

	cs::Ref<SalvageSession> SalvageSession::Latch(const cs::Ref<Ship>& player, const cs::Ref<Ship>& derelict)
	{
		if (player == nullptr || derelict == nullptr || !derelict->Derelict || derelict->Destroyed || derelict->Combat == nullptr) return nullptr;
		return cs::New<SalvageSession>(player, derelict, nullptr, derelict->Combat->Interior, cs::WrapAdd(cs::WrapMul(derelict->Id, 31), player->Id));
	}

	cs::Ref<SalvageSession> SalvageSession::Latch(const cs::Ref<Ship>& player, const cs::Ref<SpaceGod::Core::Wreck>& wreck)
	{
		if (player == nullptr || wreck == nullptr) return nullptr;
		auto shell = (wreck->Ship ? wreck->Ship->Spec : nullptr) != nullptr ? SpaceGod::Core::Interior::For(wreck->Ship->Spec->Class, 0, 0)
		                                                                   : SpaceGod::Core::Interior::ForPlatform(0, 0);
		for (const auto& c : shell->Compartments) c->Vented = true;
		return cs::New<SalvageSession>(player, nullptr, wreck, shell, cs::WrapAdd(cs::WrapMul(wreck->Id, 37), player->Id));
	}

	double SalvageSession::MarinesReady() const { return Player->Combat ? Player->Combat->MarinesAvailable() : 0; }

	bool SalvageSession::SendBoarders()
	{
		if (Ended || !Hostile() || Fighting() || Player->Combat == nullptr) return false;
		double marines = Player->Combat->Interior->TakeMarines(MarinesReady());
		if (marines < 0.5) return false;
		auto airlock = Interior->Nearest(0, 0);
		Interior->Board(Player->Faction, Player->Faction->MarineQuality, marines, airlock, false, Seconds, rng);
		Note(cs::Cat(cs::Fmt(marines, "0"), " marines through the airlock at ", airlock->Name));
		return true;
	}

	void SalvageSession::Step(double dt)
	{
		if (Ended || dt <= 0) return;
		Seconds += dt;
		if (!Fighting()) return;
		Interior->Step(dt, Seconds, rng);
		if (Interior->Marines() <= 0.5 || Interior->Captured)
		{
			// The last of them (or, with the bridge and engineering lost, all of them) throw down
			// their guns; the boarders come back aboard.
			for (const auto& c : Interior->Compartments) c->Marines = 0;
			double back = Interior->Recall();
			Player->Combat->Interior->Embark(0, back);
			HostilesCleared = true;
			Note(cs::Cat("The raiders aboard are beaten; ", cs::Fmt(back, "0"), " marines come back aboard"));
			RescueSurvivors();
		}
		else if (Interior->Boarder == nullptr)
		{
			Note("The boarding party is wiped out");
		}
	}

	void SalvageSession::RescueSurvivors()
	{
		double n = Interior->Survivors();
		if (n < 0.5) return;
		for (const auto& c : Interior->Compartments) c->Survivors = 0;
		SurvivorsRescued += n;
		survivorsHeld += n;
		Note(cs::Cat(cs::Fmt(n, "0"), " survivors found sealed in and brought into the airlock"));
	}

	int SalvageSession::SalvageCargo()
	{
		if (Ended || Hostile()) return 0;
		auto hold = Player->PersonalHold();
		auto& manifest = Manifest();
		int moved = 0;
		for (int i = 0; i < manifest.Count();)
		{
			auto stack = manifest[i];
			double crate = CargoFootprint::Of(stack.Good).Tonnes;
			double left = stack.Tonnes;
			bool full = false;
			// Half a crate's worth or more fills a crate; less is not worth the space.
			while (left >= crate * 0.5)
			{
				if (hold->AddFirstFit(stack.Good) == nullptr) { full = true; break; }
				left -= crate;
				moved++;
			}
			if (left < crate * 0.5) manifest.RemoveAt(i);
			else
			{
				manifest[i] = SalvageStack(stack.Good, left);
				i++;
			}
			if (full) break;
		}
		CratesTaken += moved;
		Note(moved > 0 ? cs::Cat(moved, " crate", moved == 1 ? "" : "s", " into the hold") + (manifest.Count() > 0 ? "; the hold is full" : "")
			: manifest.Count() > 0 ? cs::String("No room in the hold") : cs::String("Nothing left aboard to take"));
		return moved;
	}

	cs::List<cs::Ref<Subsystem>> SalvageSession::Strippable() const
	{
		cs::List<cs::Ref<Subsystem>> list;
		auto combat = Derelict ? Derelict->Combat : nullptr;
		if (combat == nullptr) combat = Wreck && Wreck->Ship ? Wreck->Ship->Combat : nullptr;
		if (combat == nullptr) return list;
		for (const auto& s : combat->Subsystems)
			if (!s->Destroyed() && CanStrip(s->Kind())) list.Add(s);
		return list;
	}

	bool SalvageSession::CanStrip(SubsystemKind k) { return k != SubsystemKind::Bridge && k != SubsystemKind::Hangar && k != SubsystemKind::Reactor; }

	cs::Ref<Subsystem> SalvageSession::FitFor(const cs::Ref<Subsystem>& part) const
	{
		if (part == nullptr || Player->Combat == nullptr) return nullptr;
		for (const auto& s : Player->Combat->Subsystems)
			// A spinal mount's gun replaces only a spinal mount's, and a turret's only a turret's (U.SPC-T490).
			if (s->Destroyed() && s->Kind() == part->Kind() && s->Def->Spinal == part->Def->Spinal) return s;
		return nullptr;
	}

	cs::Ref<Subsystem> SalvageSession::BestPart() const
	{
		cs::Ref<Subsystem> best = nullptr;
		double bestScore = cs::Double::MinValue;
		for (const auto& s : Strippable())
		{
			double score = s->Health() + (FitFor(s) != nullptr ? 10 : 0);
			if (score > bestScore) { bestScore = score; best = s; }
		}
		return best;
	}

	Good SalvageSession::ScrapOf(SubsystemKind k)
	{
		switch (k)
		{
			case SubsystemKind::LaserTurret:
			case SubsystemKind::HeavyLaser:
			case SubsystemKind::PointDefense:
			case SubsystemKind::ShieldGenerator:
			case SubsystemKind::TachyonLance:
			case SubsystemKind::PhaserArray:
				return Good::Electronics;
			case SubsystemKind::MissileLauncher:
			case SubsystemKind::Torpedo:
			case SubsystemKind::TorpedoTube:
			case SubsystemKind::AntimatterTorpedo:
			case SubsystemKind::InterceptorMissile:
			case SubsystemKind::FlakCannon:
			case SubsystemKind::FighterGuns:
				return Good::Munitions;
			case SubsystemKind::Engine:
				return Good::Machinery;
			default:
				return Good::Alloys;
		}
	}

	StripResult SalvageSession::Strip(const cs::Ref<Subsystem>& part)
	{
		if (Ended || Hostile() || part == nullptr || part->Destroyed() || !Strippable().Contains(part)) return StripResult(StripUse::None, part, nullptr, Good{}, 0);
		auto fit = FitFor(part);
		if (fit != nullptr)
		{
			fit->Hp = fit->Def->MaxHp * part->Health();
			if (fit->Def->Weapon != nullptr) fit->Ammo = cs::Math::Min(fit->Def->Weapon->Ammo, part->Ammo);
			if (fit->Kind() == SubsystemKind::ShieldGenerator) fit->Shield = 0;
			part->Hp = 0;
			PartsStripped++;
			Note(cs::Cat("Stripped a ", KindName(part->Kind()), " and fitted it in place of our own (", cs::Fmt(fit->Health(), "P0"), ")"));
			return StripResult(StripUse::Fitted, part, fit, Good{}, 0);
		}
		auto good = ScrapOf(part->Kind());
		if (Player->PersonalHold()->AddFirstFit(good) == nullptr)
		{
			Note(cs::Cat("No room in the hold for a stripped ", KindName(part->Kind())));
			return StripResult(StripUse::NoRoom, part, nullptr, good, 0);
		}
		part->Hp = 0;
		PartsStripped++;
		CratesTaken++;
		Note(cs::Cat("Stripped a ", KindName(part->Kind()), ": 1 crate of ", cs::Strings::ToLowerInvariant(GoodInfo::Of(good)->Name), " into the hold"));
		return StripResult(StripUse::Scrapped, part, nullptr, good, 1);
	}

	double SalvageSession::PrizeCrewNeeded() const
	{
		return Derelict == nullptr ? 0 : cs::Math::Max(2.0, cs::Math::Round(Derelict->Combat->Spec->Crew * 0.3));
	}

	double SalvageSession::PrizeCrewAvailable() const
	{
		return survivorsHeld + (Player->Combat ? Player->Combat->Interior->Crew() : 0) * 0.5;
	}

	bool SalvageSession::CanClaim(cs::String& why) const
	{
		why = cs::String();
		if (Ended) why = "the clamps are off";
		else if (Derelict == nullptr) why = "a wreck will never fly again";
		// Broken up (a derelict's days adrift ran out) or taken back under way since the clamps went on.
		else if (Derelict->Destroyed || !Derelict->Derelict) why = "it is gone";
		else if (Hostile()) why = "raiders still hold it";
		else if (!Derelict->Combat->Alive() || Derelict->Combat->Disabled())
			why = Derelict->Combat->EngineFactor() <= 0 ? "its engines are gone" : "it is too badly holed to fly";
		else if (PrizeCrewAvailable() < PrizeCrewNeeded())
			why = cs::Cat("it needs a prize crew of ", cs::Fmt(PrizeCrewNeeded(), "0"), ", and only ", cs::Fmt(PrizeCrewAvailable(), "0"), " can be spared");
		return why.empty();
	}

	bool SalvageSession::Claim(const cs::Ref<Fleet>& fleet, double jd)
	{
		cs::String unused;
		if (!CanClaim(unused)) return false;
		double need = PrizeCrewNeeded();
		double fromSurvivors = cs::Math::Min(survivorsHeld, need);
		survivorsHeld -= fromSurvivors;
		double fromShip = Player->Combat->Interior->TakeCrew(need - fromSurvivors);
		auto ship = Derelict;
		auto old = ship->Faction;
		auto owner = Player->Faction;
		ship->Derelict = false;
		ship->DerelictBy = nullptr;
		ship->Cargo = cs::String(); // null
		ship->Faction = owner;
		// Its old owners' prisoners are long gone from its brig (cast adrift with the hulk, or freed by the raiders).
		ship->Prisoners = 0;
		ship->PrisonerBounty = 0;
		if (old != owner && owner->Prefix.size() > 0 && old->Prefix.size() > 0 && cs::Strings::StartsWith(ship->Name, old->Prefix + " "))
			ship->Name = owner->Prefix + cs::Strings::Substring(ship->Name, static_cast<int>(old->Prefix.size()));
		Interior->Recall();
		Interior->HandOver(fromSurvivors + fromShip);
		// Its defenders are its own crew again, not the raiders who held it.
		Interior->DefenderQuality = 1;
		Claimed = true;
		Note(cs::Cat("Claimed: ", ship->Name, " flies the ", owner->Name, " colours with a prize crew of ", cs::Fmt(fromSurvivors + fromShip, "0")));
		Finish();
		if (fleet != nullptr && ship->Battle == nullptr && !ship->IsEscort()) fleet->ScheduleNext(ship, jd);
		return true;
	}

	void SalvageSession::CastOff()
	{
		if (Ended) return;
		if (Fighting())
		{
			double back = Interior->Recall();
			if (Player->Combat) Player->Combat->Interior->Embark(0, back);
			Note(cs::Cat(cs::Fmt(back, "0"), " marines pull back to the airlock"));
		}
		Note("Clamps off");
		Finish();
	}

	void SalvageSession::Finish()
	{
		if (survivorsHeld > 0 && Player->Combat) Player->Combat->Interior->Embark(survivorsHeld, 0);
		survivorsHeld = 0;
		Ended = true;
	}

	cs::String SalvageSession::Summary() const
	{
		cs::List<cs::String> parts;
		if (HostilesCleared) parts.Add("beat the raiders aboard");
		if (SurvivorsRescued > 0) parts.Add(cs::Cat("rescued ", cs::Fmt(SurvivorsRescued, "0"), " survivor", SurvivorsRescued >= 1.5 ? "s" : ""));
		if (CratesTaken > 0) parts.Add(cs::Cat("took ", CratesTaken, " crate", CratesTaken == 1 ? "" : "s"));
		if (PartsStripped > 0) parts.Add(cs::Cat("stripped ", PartsStripped, " part", PartsStripped == 1 ? "" : "s"));
		if (Claimed) parts.Add("claimed the hull");
		return parts.Count() == 0 ? cs::String("found nothing worth taking") : cs::Strings::Join(", ", parts);
	}

	cs::String SalvageSession::KindName(SubsystemKind k)
	{
		switch (k)
		{
			case SubsystemKind::LaserTurret: return "laser turret";
			case SubsystemKind::HeavyLaser: return "heavy laser";
			case SubsystemKind::PhaserArray: return "phaser array";
			case SubsystemKind::PlasmaCannon: return "plasma cannon";
			case SubsystemKind::MissileLauncher: return "missile launcher";
			case SubsystemKind::PointDefense: return "point-defence mount";
			case SubsystemKind::FighterGuns: return "gun pack";
			case SubsystemKind::Torpedo: return "torpedo rack";
			case SubsystemKind::TorpedoTube: return "torpedo tube";
			case SubsystemKind::AntimatterTorpedo: return "antimatter torpedo launcher";
			case SubsystemKind::FlakCannon: return "flak cannon";
			case SubsystemKind::InterceptorMissile: return "interceptor launcher";
			case SubsystemKind::Railgun: return "railgun";
			case SubsystemKind::Coilgun: return "coilgun";
			case SubsystemKind::TachyonLance: return "tachyon lance";
			case SubsystemKind::BoardingPods: return "pod launcher";
			case SubsystemKind::Drill: return "drill prow";
			case SubsystemKind::Engine: return "engine";
			case SubsystemKind::ShieldGenerator: return "shield generator";
			default: return cs::Strings::ToLowerInvariant(cs::Str(k));
		}
	}

	void SalvageSession::Note(const cs::String& text)
	{
		Log.Add(text);
		if (Log.Count() > 40) Log.RemoveAt(0);
	}
}
