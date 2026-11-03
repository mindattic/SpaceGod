// Port of Unity.SpaceGod Core/Systems/StrategicCommand.cs (US-T15, see docs/MIGRATION.md).

#include "Systems/StrategicCommand.h"

#include "BodyCatalog.h"
#include "Combat/Corvettes.h"
#include "Combat/Diplomacy.h"
#include "Combat/CombatState.h"
#include "Combat/Facilities.h"
#include "Combat/War.h"
#include "Economy/Economy.h"
#include "Economy/Holdings.h"
#include "Economy/Industry.h"
#include "Economy/Settlement.h"
#include "Fleet.h"
#include "Settings/GameSetting.h"
#include "Ships.h"
#include "Systems/FleetDoctrine.h"
#include "Systems/Galaxy.h"
#include "Systems/InterstellarTraffic.h"
#include "Systems/InterstellarWars.h"
#include "Systems/Polities.h"
#include "World.h"

namespace SpaceGod::Core
{
	const char* ToString(ObjectiveKind v)
	{
		switch (v)
		{
			case ObjectiveKind::Take: return "Take";
			case ObjectiveKind::Hold: return "Hold";
			case ObjectiveKind::Blockade: return "Blockade";
			case ObjectiveKind::Escort: return "Escort";
			case ObjectiveKind::Explore: return "Explore";
		}
		return "";
	}

	const char* ToString(CommandScope v)
	{
		switch (v)
		{
			case CommandScope::Group: return "Group";
			case CommandScope::System: return "System";
			case CommandScope::Polity: return "Polity";
		}
		return "";
	}

	// ==================================================================== ProductionFocus

	struct ProductionFocus::Statics
	{
		cs::Ref<ProductionFocus> Balanced, War, Civil, Extraction;
	};

	cs::List<cs::Ref<ProductionFocus>>& ProductionFocus::all()
	{
		static cs::List<cs::Ref<ProductionFocus>> list = cs::List<cs::Ref<ProductionFocus>>();
		return list;
	}

	const ProductionFocus::Statics& ProductionFocus::statics()
	{
		all();
		static const Statics s{
			Add("Balanced", "Every industry follows its own market", {}),
			Add("War industry", "Smelters, fabs, machine works and munitions plants first",
				{ IndustryKind::Smelter, IndustryKind::ElectronicsFab, IndustryKind::MachineWorks, IndustryKind::MunitionsPlant }),
			Add("Civil", "Farms and pharmaceutical labs first: feed and heal the people",
				{ IndustryKind::Farm, IndustryKind::PharmaLab }),
			Add("Extraction", "Mines first: raw ore, ice and gas for export", { IndustryKind::Mine }),
		};
		return s;
	}

	const cs::List<cs::Ref<ProductionFocus>>& ProductionFocus::All() { statics(); return all(); }

	ProductionFocus::ProductionFocus(cs::String name, cs::String description, cs::List<IndustryKind> favoured)
	{
		Name = name;
		Description = description;
		this->favoured = favoured;
	}

	cs::Ref<ProductionFocus> ProductionFocus::Add(cs::String name, cs::String description, cs::List<IndustryKind> favoured)
	{
		auto f = cs::Ref<ProductionFocus>(new ProductionFocus(name, description, favoured));
		all().Add(f);
		return f;
	}

	cs::Ref<ProductionFocus> ProductionFocus::Register(cs::String name, cs::String description, cs::List<IndustryKind> favoured)
	{
		statics();
		return Add(name, description, favoured);
	}

	bool ProductionFocus::Favours(IndustryKind k) const { return favoured.IndexOf(k) >= 0; }

	cs::Ref<ProductionFocus> ProductionFocus::Find(const cs::String& name)
	{
		statics();
		return all().Find([&](const cs::Ref<ProductionFocus>& f) { return f->Name == name; });
	}

	const cs::Ref<ProductionFocus>& ProductionFocus::Balanced() { return statics().Balanced; }
	const cs::Ref<ProductionFocus>& ProductionFocus::War() { return statics().War; }
	const cs::Ref<ProductionFocus>& ProductionFocus::Civil() { return statics().Civil; }
	const cs::Ref<ProductionFocus>& ProductionFocus::Extraction() { return statics().Extraction; }

	// ==================================================================== BuildPlan

	struct BuildPlan::Statics
	{
		cs::Ref<BuildPlan> Balanced, BattleLine, Carriers, Strike, Screens, LaneSecurity, Mobilise, Merchant;
	};

	cs::List<cs::Ref<BuildPlan>>& BuildPlan::all()
	{
		static cs::List<cs::Ref<BuildPlan>> list = cs::List<cs::Ref<BuildPlan>>();
		return list;
	}

	const BuildPlan::Statics& BuildPlan::statics()
	{
		all();
		static const Statics s{
			/// <summary>What every navy builds unless told otherwise: a command cruiser among them to lead a group (U.SPC-T477).</summary>
			Add("Balanced fleet", "Destroyers, cruisers, a battleship, a command cruiser and a carrier in turn", 1,
				{ ShipClass::Destroyer, ShipClass::Cruiser, ShipClass::Destroyer, ShipClass::Battleship, ShipClass::Destroyer, ShipClass::CommandCruiser, ShipClass::Carrier }),
			Add("Battle line", "Battleships and cruisers, with a command cruiser to lead them: the heaviest guns", 1,
				{ ShipClass::Battleship, ShipClass::Cruiser, ShipClass::Battleship, ShipClass::Destroyer, ShipClass::CommandCruiser }),
			/// <summary>Carriers, their screens and the frigates that ride clamped to their hulls (U.SPC-T475).</summary>
			Add("Carrier groups", "Carriers, their screens and frigates", 1,
				{ ShipClass::Carrier, ShipClass::Destroyer, ShipClass::Frigate, ShipClass::Cruiser, ShipClass::Destroyer, ShipClass::Frigate }),
			/// <summary>Strike flotillas (U.SPC-T475): frigates that alpha-strike the enemy's capitals, with destroyers to screen them.</summary>
			Add("Strike flotillas", "Frigates and their destroyers: alpha strikes on the enemy's capitals", 1,
				{ ShipClass::Frigate, ShipClass::Destroyer, ShipClass::Frigate, ShipClass::Frigate, ShipClass::Cruiser }),
			Add("Screens", "Destroyers: cheap, quick hulls in numbers", 1,
				{ ShipClass::Destroyer, ShipClass::Destroyer, ShipClass::Cruiser }),
			/// <summary>Lane security (U.SPC-T479): destroyers and cruisers, one hull of every group fitted with a field projector
			/// as its lane interdictor (U.SPC-T507), the rest to fight what it pulls out: groups that hold pickets on the
			/// hyperlanes raiders work.</summary>
			Add("Lane security", "Destroyers and cruisers, each group with a lane interdictor's field projector: pickets on the hyperlanes", 1,
				{ ShipClass::Destroyer, ShipClass::Destroyer, ShipClass::Cruiser, ShipClass::Destroyer })->WithGroupFittings({ cs::String(FacilityKinds::FieldProjector) }),
			Add("Mobilise", "A balanced fleet half again as big", 1.5,
				{ ShipClass::Destroyer, ShipClass::Cruiser, ShipClass::Destroyer, ShipClass::Battleship, ShipClass::Destroyer, ShipClass::CommandCruiser, ShipClass::Carrier }),
			Add("Merchant marine", "No new warships: the slips go to freighters and haulers", 0, {}),
		};
		return s;
	}

	const cs::List<cs::Ref<BuildPlan>>& BuildPlan::All() { statics(); return all(); }

	BuildPlan::BuildPlan(cs::String name, cs::String description, cs::List<ShipClass> cycle, double navyShare)
	{
		Name = name;
		Description = description;
		Cycle = cycle;
		NavyShare = navyShare;
	}

	cs::Ref<BuildPlan> BuildPlan::Add(cs::String name, cs::String description, double navyShare, cs::List<ShipClass> cycle)
	{
		auto p = cs::Ref<BuildPlan>(new BuildPlan(name, description, cycle, navyShare));
		all().Add(p);
		return p;
	}

	cs::Ref<BuildPlan> BuildPlan::Register(cs::String name, cs::String description, double navyShare, cs::List<ShipClass> cycle)
	{
		statics();
		return Add(name, description, navyShare, cycle);
	}

	cs::Ref<BuildPlan> BuildPlan::Find(const cs::String& name)
	{
		statics();
		return all().Find([&](const cs::Ref<BuildPlan>& p) { return p->Name == name; });
	}

	cs::Ref<BuildPlan> BuildPlan::WithGroupFittings(cs::List<cs::String> facilities)
	{
		GroupFittings = facilities;
		// `return this`: the plan is always held by the Ref Add made for it, so find that one.
		for (const auto& p : all())
			if (p.get() == this) return p;
		return nullptr;
	}

	const cs::Ref<BuildPlan>& BuildPlan::Balanced() { return statics().Balanced; }
	const cs::Ref<BuildPlan>& BuildPlan::BattleLine() { return statics().BattleLine; }
	const cs::Ref<BuildPlan>& BuildPlan::Carriers() { return statics().Carriers; }
	const cs::Ref<BuildPlan>& BuildPlan::Strike() { return statics().Strike; }
	const cs::Ref<BuildPlan>& BuildPlan::Screens() { return statics().Screens; }
	const cs::Ref<BuildPlan>& BuildPlan::LaneSecurity() { return statics().LaneSecurity; }
	const cs::Ref<BuildPlan>& BuildPlan::Mobilise() { return statics().Mobilise; }
	const cs::Ref<BuildPlan>& BuildPlan::Merchant() { return statics().Merchant; }

	// ==================================================================== ResearchTrack

	struct ResearchTrack::Statics
	{
		cs::Ref<ResearchTrack> Marines, Shipyards, Industry;
	};

	cs::List<cs::Ref<ResearchTrack>>& ResearchTrack::all()
	{
		static cs::List<cs::Ref<ResearchTrack>> list = cs::List<cs::Ref<ResearchTrack>>();
		return list;
	}

	const ResearchTrack::Statics& ResearchTrack::statics()
	{
		all();
		static const Statics s{
			Add("Marine training", "Better marines, man for man, in boarding and on the ground", 2000, 5,
				[](cs::Ref<Faction> f, int level) { f->MarineQuality += MarinesPerLevel; }),
			Add("Shipyards", "A bigger navy for the same yards", 3000, 5, nullptr),
			Add("Industry", "The favoured industries grow faster", 2500, 5, nullptr),
		};
		return s;
	}

	const cs::List<cs::Ref<ResearchTrack>>& ResearchTrack::All() { statics(); return all(); }

	ResearchTrack::ResearchTrack(cs::String name, cs::String description, double baseCost, int maxLevel, cs::Action<cs::Ref<Faction>, int> onLevel)
	{
		Name = name;
		Description = description;
		BaseCost = baseCost;
		MaxLevel = maxLevel;
		OnLevel = onLevel;
	}

	cs::Ref<ResearchTrack> ResearchTrack::Add(cs::String name, cs::String description, double baseCost, int maxLevel, cs::Action<cs::Ref<Faction>, int> onLevel)
	{
		auto t = cs::Ref<ResearchTrack>(new ResearchTrack(name, description, baseCost, maxLevel, onLevel));
		all().Add(t);
		return t;
	}

	cs::Ref<ResearchTrack> ResearchTrack::Register(cs::String name, cs::String description, double baseCost, int maxLevel, cs::Action<cs::Ref<Faction>, int> onLevel)
	{
		statics();
		return Add(name, description, baseCost, maxLevel, onLevel);
	}

	cs::Ref<ResearchTrack> ResearchTrack::Find(const cs::String& name)
	{
		statics();
		return all().Find([&](const cs::Ref<ResearchTrack>& t) { return t->Name == name; });
	}

	const cs::Ref<ResearchTrack>& ResearchTrack::Marines() { return statics().Marines; }
	const cs::Ref<ResearchTrack>& ResearchTrack::Shipyards() { return statics().Shipyards; }
	const cs::Ref<ResearchTrack>& ResearchTrack::Industry() { return statics().Industry; }

	// ==================================================================== PolityDirectives

	int PolityDirectives::Level(const cs::Ref<ResearchTrack>& t) const
	{
		int n = 0;
		return t != nullptr && Levels.TryGetValue(t, n) ? n : 0;
	}

	// ==================================================================== StrategicCommand

	const cs::Ref<RangeSetting>& StrategicCommand::ResearchPace()
	{
		static const cs::Ref<RangeSetting> s = cs::New<RangeSetting>("strategy.researchPace", SettingGroups::Civilizations,
			"Research pace", 1, 0, 10, 0.25,
			"How fast a polity's people turn out research toward its chosen line (the strategy layer, galaxy map COMMAND).",
			[](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }, "research technology strategy polity");
		return s;
	}

	// ---------------------------------------------------------------- directives

	cs::Ref<PolityDirectives> StrategicCommand::Of(const cs::Ref<Faction>& f)
	{
		if (f == nullptr) return nullptr;
		cs::Ref<PolityDirectives> d;
		if (!directives.TryGetValue(f, d)) directives[f] = d = cs::New<PolityDirectives>(f);
		return d;
	}

	cs::Ref<PolityDirectives> StrategicCommand::Find(const cs::Ref<Faction>& f) const
	{
		cs::Ref<PolityDirectives> d;
		return f != nullptr && directives.TryGetValue(f, d) ? d : nullptr;
	}

	void StrategicCommand::SetProduction(const cs::Ref<Faction>& f, const cs::Ref<ProductionFocus>& focus)
	{
		Of(f)->Production = focus ? focus : ProductionFocus::Balanced();
	}

	void StrategicCommand::SetBuild(const cs::Ref<Faction>& f, const cs::Ref<BuildPlan>& plan)
	{
		Of(f)->Build = plan ? plan : BuildPlan::Balanced();
	}

	void StrategicCommand::SetResearch(const cs::Ref<Faction>& f, const cs::Ref<ResearchTrack>& track)
	{
		auto d = Of(f);
		if (d->Research == track) return;
		d->Research = track;
		d->ResearchPoints = 0;
	}

	int StrategicCommand::Level(const cs::Ref<Faction>& f, const cs::Ref<ResearchTrack>& track) const
	{
		auto d = Find(f);
		return d ? d->Level(track) : 0;
	}

	cs::Ref<BuildPlan> StrategicCommand::BuildPlanOf(const cs::Ref<Faction>& f) const
	{
		auto d = Find(f);
		auto b = d ? d->Build : nullptr;
		return b ? b : BuildPlan::Balanced();
	}

	double StrategicCommand::NavyShare(const cs::Ref<Faction>& f) const
	{
		return BuildPlanOf(f)->NavyShare * (1 + ResearchTrack::YardsPerLevel * Level(f, ResearchTrack::Shipyards()));
	}

	StrategicCommand::PriorityResult StrategicCommand::Priority(const cs::Ref<Faction>& owner, IndustryKind kind) const
	{
		auto d = Find(owner);
		if (d == nullptr || !d->Production->Favours(kind)) return { false, 0 };
		return { true, ResearchTrack::IndustryPerLevel * d->Level(ResearchTrack::Industry()) };
	}

	// ---------------------------------------------------------------- objectives

	cs::Ref<FleetObjective> StrategicCommand::ObjectiveOf(const cs::Ref<Ship>& leader) const
	{
		cs::Ref<FleetObjective> o;
		return leader != nullptr && objectives.TryGetValue(leader, o) ? o : nullptr;
	}

	bool StrategicCommand::Holds(const cs::Ref<Ship>& leader, const cs::Ref<World>& where) const
	{
		auto o = ObjectiveOf(leader);
		return o != nullptr && (where == nullptr || o->Where == where);
	}

	cs::List<StrategicCommand::GroupAt> StrategicCommand::Groups(const cs::Ref<Faction>& f, CommandScope scope, const cs::Ref<Ship>& group, const cs::Ref<World>& system) const
	{
		auto list = cs::List<GroupAt>();
		if (scope == CommandScope::Group)
		{
			auto leader = group ? group->Controller() : nullptr;
			auto w = WorldOf(leader);
			if (leader != nullptr && w != nullptr && Commandable(w, leader, f)) list.Add(GroupAt{ leader, w });
			return list;
		}
		for (const auto& w : polities->Worlds())
		{
			if (scope == CommandScope::System && w != system) continue;
			for (const auto& s : w->Fleet->Ships())
				if (Commandable(w, s, f)) list.Add(GroupAt{ s, w });
		}
		return list;
	}

	bool StrategicCommand::Commandable(const cs::Ref<World>& w, const cs::Ref<Ship>& s, const cs::Ref<Faction>& f)
	{
		return !s->Destroyed && !s->IsEscort() && !s->Derelict && s->Faction == f && s->Spec->IsMilitary && !s->Convoy && !Corvettes::Stationed(s) && s != w->PilotedShip &&
			(w->Holdings == nullptr || !w->Holdings->Owns(s));
	}

	cs::Ref<World> StrategicCommand::WorldOf(const cs::Ref<Ship>& ship) const
	{
		if (ship == nullptr) return nullptr;
		for (const auto& w : polities->Worlds())
			if (w->Fleet->IsPresent(ship)) return w;
		return nullptr;
	}

	cs::Ref<StarSystem> StrategicCommand::SystemOf(const cs::Ref<World>& w) const
	{
		if (Galaxy == nullptr || w == nullptr) return nullptr;
		for (const auto& s : Galaxy->Systems)
			if (s->World == w) return s;
		return nullptr;
	}

	cs::String StrategicCommand::NameOf(const cs::Ref<World>& w) const
	{
		auto s = SystemOf(w);
		return s ? s->Name : cs::String("the system");
	}

	cs::String StrategicCommand::Order(const cs::Ref<Faction>& f, CommandScope scope, const cs::Ref<Ship>& group, const cs::Ref<World>& system, ObjectiveKind kind, const cs::Ref<World>& target, double jd, int& given,
		int body, cs::Ref<Ship> charge)
	{
		given = 0;
		if (f == nullptr || target == nullptr) return "No polity or no system to command";
		auto why = Resolve(f, kind, target, body, charge);
		if (!why.empty()) return why;
		auto groups = Groups(f, scope, group, system);
		if (groups.Count() == 0) return cs::Cat("The ", f->Name, " has no fleet group there to send");
		cs::Ref<Ship> firstGiven = nullptr;
		for (const auto& [leader, where] : groups)
		{
			if (kind == ObjectiveKind::Escort && leader == charge) continue;
			auto o = cs::New<FleetObjective>();
			o->Kind = kind;
			o->Where = target;
			o->Body = body;
			o->Charge = charge;
			o->SinceJd = jd;
			objectives[leader] = o;
			if (!firstGiven) firstGiven = leader;
			given++;
			if (where == target)
			{
				// In port: it takes the order up now, not when its stay is up.
				if (leader->Phase == ShipPhase::Parked && leader->Battle == nullptr && leader->Voyage == nullptr) where->Fleet->ScheduleNext(leader, jd);
			}
			else Dispatch(leader, where, o, jd);
		}
		if (given == 0) return "No group there could be given it";

		target->Log(jd, cs::Cat("The ", f->Name, " orders ", (given == 1 ? cs::Cat("the ", firstGiven->Name) : cs::Cat(given, " groups")), " to ", cs::Strings::ToLowerInvariant(Describe(kind, target, body, charge))),
			WorldEventKind::Diplomacy, body >= 0 ? body : -1);
		return cs::String();
	}

	int StrategicCommand::Clear(const cs::Ref<Faction>& f, CommandScope scope, const cs::Ref<Ship>& group, const cs::Ref<World>& system)
	{
		int n = 0;
		for (const auto& g : Groups(f, scope, group, system))
			if (objectives.Remove(g.Leader)) n++;
		return n;
	}

	cs::String StrategicCommand::Resolve(const cs::Ref<Faction>& f, ObjectiveKind kind, const cs::Ref<World>& w, int& body, cs::Ref<Ship>& charge)
	{
		auto dip = polities->Diplomacy;
		switch (kind)
		{
			case ObjectiveKind::Take:
				if (body >= 0)
				{
					for (const auto& s : w->Economy->At(body))
						if (Enemy(f, s)) return cs::String();
					return cs::Cat("Nothing at ", (*w->Catalog())[body]->Name, " is held by an enemy of the ", f->Name);
				}
				for (const auto& s : w->Economy->Settlements)
					if (Enemy(f, s) && s->Kind != SettlementKind::Haven) return cs::String();
				return cs::Cat("The ", f->Name, " is at war with nobody holding a port in ", NameOf(w));
			case ObjectiveKind::Blockade:
			{
				if (body >= 0)
				{
					for (const auto& s : w->Economy->At(body))
						if (Blockadable(f, s)) return cs::String();
					return cs::Cat("Nobody hostile to the ", f->Name, " keeps a port at ", (*w->Catalog())[body]->Name);
				}
				cs::Ref<Settlement> best = nullptr;
				for (const auto& s : w->Economy->Settlements)
					if (Blockadable(f, s) && (best == nullptr || s->Population > best->Population)) best = s;
				if (best == nullptr) return cs::Cat("Nobody hostile to the ", f->Name, " keeps a port in ", NameOf(w));
				body = best->Body;
				return cs::String();
			}
			case ObjectiveKind::Hold:
			{
				if (body >= 0) return cs::String();
				cs::Ref<Settlement> own = nullptr, friendly = nullptr;
				for (const auto& s : w->Economy->Settlements)
				{
					if (s->Fallen || s->Kind == SettlementKind::Haven) continue;
					if (s->Owner == f && (own == nullptr || s->Population > own->Population)) own = s;
					else if (!dip->Hostile(f, s->Owner) && (friendly == nullptr || s->Population > friendly->Population)) friendly = s;
				}
				auto pick = own ? own : friendly;
				body = pick ? pick->Body : w->Catalog()->Root()->Index;
				return cs::String();
			}
			case ObjectiveKind::Escort:
			{
				if (charge == nullptr)
				{
					for (const auto& s : w->Fleet->Ships())
						if (!s->Destroyed && !s->IsEscort() && !s->Derelict && s->Faction == f && w->IsTrader(s) &&
							(charge == nullptr || s->CapacityTonnes > charge->CapacityTonnes))
							charge = s;
					if (charge == nullptr) return cs::Cat("The ", f->Name, " has no merchant hull in ", NameOf(w), " to escort");
				}
				if (charge->Destroyed || dip->Hostile(f, charge->Faction)) return cs::Cat("The ", charge->Name, " is no ship of a friend");
				return cs::String();
			}
			default:
				return cs::String();
		}
	}

	bool StrategicCommand::Enemy(const cs::Ref<Faction>& f, const cs::Ref<Settlement>& s) const
	{
		return s != nullptr && !s->Fallen && s->Owner != nullptr && s->Owner != f &&
			(s->Owner->IsPirate ? s->Kind == SettlementKind::Haven : polities->Diplomacy->Get(f, s->Owner) == Relation::War);
	}

	bool StrategicCommand::Blockadable(const cs::Ref<Faction>& f, const cs::Ref<Settlement>& s) const
	{
		return s != nullptr && !s->Fallen && s->Kind != SettlementKind::Haven && s->Owner != nullptr && s->Owner != f && !s->Owner->IsPirate && polities->Diplomacy->Hostile(f, s->Owner);
	}

	void StrategicCommand::Complete(const cs::Ref<Ship>& leader, const cs::Ref<World>& where, double jd, const cs::String& why)
	{
		if (leader == nullptr || !objectives.Remove(leader)) return;
		if (!why.empty() && where) where->Log(jd, why, WorldEventKind::Diplomacy);
	}

	void StrategicCommand::Taken(const cs::Ref<Ship>& leader, const cs::Ref<World>& where, int body, double jd)
	{
		auto o = ObjectiveOf(leader);
		if (o == nullptr) return;
		o->Kind = ObjectiveKind::Hold;
		o->Body = body >= 0 ? body : where->Catalog()->Root()->Index;
		o->Campaign = nullptr;
		where->Log(jd, cs::Cat("The ", leader->Name, "'s group has taken its objective in ", NameOf(where), " and holds ", (*where->Catalog())[o->Body]->Name), WorldEventKind::Capture, o->Body);
	}

	void StrategicCommand::OnSuccession(const cs::Ref<Ship>& dead, const cs::Ref<Ship>& heir)
	{
		cs::Ref<FleetObjective> o;
		if (dead == nullptr || heir == nullptr || !objectives.TryGetValue(dead, o)) return;
		objectives.Remove(dead);
		objectives[heir] = o;
	}

	// ---------------------------------------------------------------- the day

	void StrategicCommand::Update(double jd)
	{
		if (cs::Double::IsNaN(nextDailyJd)) nextDailyJd = jd;
		while (jd >= nextDailyJd)
		{
			Daily(nextDailyJd);
			nextDailyJd += 1;
		}
	}

	void StrategicCommand::Daily(double jd)
	{
		for (const auto& d : directives.Values()) Research(d, jd);
		cs::Ref<cs::List<cs::Ref<Ship>>> gone = nullptr;
		// C# enumerates the dictionary itself (its body only edits the objective values); a snapshot of the pairs walks
		// the same order.
		auto snapshot = cs::List<std::pair<cs::Ref<Ship>, cs::Ref<FleetObjective>>>();
		for (auto [key, value] : objectives) snapshot.Add({ key, value });
		for (const auto& kv : snapshot)
		{
			auto leader = kv.first;
			auto o = kv.second;
			if (leader->Destroyed || leader->IsEscort() || leader->Derelict || !leader->Spec->IsMilitary)
			{
				if (!gone) gone = cs::New<cs::List<cs::Ref<Ship>>>();
				gone->Add(leader);
				continue;
			}
			auto where = WorldOf(leader);
			if (where == nullptr) continue;
			if (o->Kind == ObjectiveKind::Escort)
			{
				auto charge = o->Charge;
				if (charge == nullptr || charge->Destroyed || charge->Derelict || polities->Diplomacy->Hostile(leader->Faction, charge->Faction))
				{
					if (!gone) gone = cs::New<cs::List<cs::Ref<Ship>>>();
					gone->Add(leader);
					where->Log(jd, cs::Cat("The ", leader->Name, "'s group has lost its charge, the ", (charge ? charge->Name : cs::String("ship")), ", and stands down"), WorldEventKind::Info);
					continue;
				}
				// A charge gone through the gates is followed to where it now is.
				auto chargeIn = WorldOf(charge->Controller());
				if (chargeIn != nullptr) o->Where = chargeIn;
			}
			if (o->Campaign != nullptr)
			{
				if (o->Campaign->Active()) continue;
				// The campaign it was handed to is over: what it took, it holds.
				o->Campaign = nullptr;
				if (o->Kind == ObjectiveKind::Take && !AnyEnemy(leader->Faction, o->Where, o->Body))
				{
					Taken(leader, o->Where, o->Body, jd);
					continue;
				}
				if (!gone) gone = cs::New<cs::List<cs::Ref<Ship>>>();
				gone->Add(leader);
				continue;
			}
			if (where != o->Where) Dispatch(leader, where, o, jd);
		}
		if (gone != nullptr)
			for (const auto& s : *gone) objectives.Remove(s);
	}

	bool StrategicCommand::AnyEnemy(const cs::Ref<Faction>& f, const cs::Ref<World>& w, int body) const
	{
		for (const auto& s : w->Economy->Settlements)
			if ((body < 0 || s->Body == body) && Enemy(f, s) && (body >= 0 || s->Kind != SettlementKind::Haven)) return true;
		return false;
	}

	void StrategicCommand::Research(const cs::Ref<PolityDirectives>& d, double jd)
	{
		auto track = d->Research;
		if (track == nullptr) return;
		int level = d->Level(track);
		if (level >= track->MaxLevel) return;
		double people = 0;
		for (const auto& s : polities->SettlementsOf(d->Faction)) people += s->Population;
		d->ResearchPoints += ResearchPace()->Value * PointsPerMillion * cs::Math::Sqrt(cs::Math::Max(0.0, people) / 1e6);
		double cost = track->CostOf(level);
		if (d->ResearchPoints < cost) return;
		d->ResearchPoints -= cost;
		d->Levels[track] = level + 1;
		if (track->OnLevel) track->OnLevel(d->Faction, level + 1);
		auto home = polities->CapitalSystemOf(d->Faction);
		if (!home) home = polities->HomeOf(d->Faction);
		if (home) home->Log(jd, cs::Cat("The ", d->Faction->Name, " reaches ", cs::Strings::ToLowerInvariant(track->Name), " level ", level + 1, ": ", cs::Strings::ToLowerInvariant(track->Description)), WorldEventKind::Info);
		if (level + 1 >= track->MaxLevel) d->ResearchPoints = 0;
	}

	void StrategicCommand::Dispatch(const cs::Ref<Ship>& leader, const cs::Ref<World>& from, const cs::Ref<FleetObjective>& o, double jd)
	{
		auto traffic = Galaxy ? Galaxy->Traffic : nullptr;
		auto fromSystem = SystemOf(from);
		auto toSystem = SystemOf(o->Where);
		if (traffic == nullptr || fromSystem == nullptr || toSystem == nullptr) return;
		// Already on its way there under this command: it goes on; on its way somewhere else: turned about.
		if (leader->Voyage != nullptr)
		{
			if (leader->Voyage->Purpose != VoyagePurpose::Directed || leader->Voyage->Destination == toSystem) return;
			traffic->Redirect(fromSystem, leader, toSystem, jd);
			return;
		}
		if (!InterstellarTraffic::Idle(from, leader)) return;
		if (o->Kind == ObjectiveKind::Take && Galaxy->Wars != nullptr)
		{
			auto c = Galaxy->Wars->Launch(leader, fromSystem, toSystem, jd);
			if (c != nullptr)
			{
				o->Campaign = c;
				return;
			}
		}
		if (traffic->SendDirected(leader, fromSystem, toSystem, jd) != nullptr)
			from->Log(jd, cs::Cat("The ", leader->Name, " leads its group out for ", toSystem->Name, " on the ", leader->Faction->Name, "'s orders"), WorldEventKind::Info, from->Catalog()->Root()->Index);
	}

	// ---------------------------------------------------------------- the picture

	cs::String StrategicCommand::Describe(ObjectiveKind kind, const cs::Ref<World>& w, int body, const cs::Ref<Ship>& charge) const
	{
		cs::String place = body >= 0 ? (*w->Catalog())[body]->Name : NameOf(w);
		switch (kind)
		{
			case ObjectiveKind::Take: return body >= 0 ? cs::Cat("Take ", place) : cs::Cat("Take every enemy port in ", place);
			case ObjectiveKind::Hold: return cs::Cat("Hold ", place);
			case ObjectiveKind::Blockade: return cs::Cat("Blockade ", place);
			case ObjectiveKind::Escort: return cs::Cat("Escort the ", (charge ? charge->Name : cs::String("convoy")));
			default: return body >= 0 ? cs::Cat("Explore ", place) : cs::Cat("Explore ", place);
		}
	}

	cs::String StrategicCommand::Describe(const cs::Ref<FleetObjective>& o) const
	{
		return o == nullptr ? cs::String() :
			cs::Cat(Describe(o->Kind, o->Where, o->Body, o->Charge), (o->Campaign != nullptr && o->Campaign->Active() ? " (on campaign)" : ""));
	}

	cs::String StrategicCommand::DescribeOrder(const cs::Ref<GroupOrder>& o, const cs::Ref<BodyCatalog>& catalog)
	{
		if (o == nullptr) return "awaiting orders";
		cs::String where = o->Body >= 0 && catalog != nullptr && o->Body < catalog->Count() ? (*catalog)[o->Body]->Name : cs::String();
		switch (o->Kind)
		{
			case OrderKind::Ambush: return "lying in wait on the lanes";
			case OrderKind::Attack: return cs::Cat("attacking ", where);
			case OrderKind::Defend: return cs::Cat("holding ", where);
			case OrderKind::Siege: return cs::Cat("besieging ", where);
			case OrderKind::Repair: return cs::Cat("refitting at ", where);
			case OrderKind::Raid: return cs::Cat("raiding at ", where);
			case OrderKind::Hunt: return cs::Cat("hunting raiders at ", where);
			case OrderKind::Assault: return cs::Cat("storming ", where);
			case OrderKind::GateWatch: return "lying off the gates";
			case OrderKind::HoldGate: return "holding the gates";
			case OrderKind::Blockade: return cs::Cat("blockading ", where);
			case OrderKind::Escort: return cs::Cat("escorting, at ", where);
			case OrderKind::Explore: return cs::Cat("exploring ", where);
			default: return cs::Cat("patrolling, next ", where);
		}
	}

	cs::Ref<PolityReport> StrategicCommand::Report(const cs::Ref<Faction>& f, double jd) const
	{
		auto r = cs::New<PolityReport>();
		r->Faction = f;
		if (f == nullptr) return r;
		r->Treasury = f->Treasury;
		r->MarineQuality = f->MarineQuality;
		r->Morale = f->CrewMorale;
		r->CrewLost = f->CrewLost;
		r->Systems = polities->SystemsHeldBy(f).Count();
		double unrestWeight = 0;
		for (const auto& s : polities->SettlementsOf(f))
		{
			if (s->Kind == SettlementKind::Haven && !f->IsPirate) continue;
			r->Ports++;
			r->Population += s->Population;
			r->Unrest += s->Unrest * s->Population;
			unrestWeight += s->Population;
		}
		r->Unrest = unrestWeight > 0 ? r->Unrest / unrestWeight : 0;
		for (const auto& g : polities->Factions())
			if (g != f && !g->IsPirate && polities->Diplomacy->Get(f, g) == Relation::War) r->AtWarWith.Add(g);
		for (const auto& w : polities->Worlds())
		{
			cs::String name = NameOf(w);
			for (const auto& s : w->Fleet->Ships())
			{
				if (s->Destroyed || s->Faction != f || !s->Spec->IsMilitary) continue;
				r->Warships++;
				r->Strength += s->Combat->Strength();
				if (s->IsEscort() || s->Derelict) continue;
				r->Groups++;
				int hulls = 1;
				double strength = s->Combat->Strength();
				for (const auto& e : w->Fleet->Ships())
					if (e->Leader == s && !e->Destroyed)
					{
						hulls++;
						strength += e->Combat->Strength();
					}
				auto o = ObjectiveOf(s);
				if (o != nullptr) r->Commanded++;
				if (!s->PatrolWing && s->PicketGate == nullptr)
				{
					if (FleetDoctrine::IsStrikeGroup(w, s)) r->StrikeGroups++;
					else r->GateFleets++;
				}
				cs::String order = s->Voyage != nullptr
					? cs::Cat("bound for ", (s->Voyage->Destination ? s->Voyage->Destination->Name : cs::String("another system")))
					: DescribeOrder(w->War ? w->War->OrderOf(s) : nullptr, w->Catalog());
				r->Fleet.Add(GroupLine(s, w, name, order, Describe(o), hulls, strength));
			}
		}
		if (Galaxy && Galaxy->Wars != nullptr)
			for (const auto& c : Galaxy->Wars->Campaigns())
				if (c->Active() && c->Faction == f) r->Campaigns++;
		return r;
	}
}
