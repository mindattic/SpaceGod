// Port of Assets/UnitySpace/Core/Combat/ModularHull.cs (see docs/MIGRATION.md).

#include "Combat/ModularHull.h"

#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/FireCones.h"
#include "Combat/Outfitting.h"
#include "Combat/Salvage.h"
#include "Combat/ShipComponents.h" // US-T269: reinforced bulkheads
#include "Economy/Goods.h"
#include "Economy/Settlement.h"

namespace SpaceGod::Core
{
	const char* ToString(HullModuleKind v)
	{
		switch (v)
		{
			case HullModuleKind::Bow: return "Bow";
			case HullModuleKind::Midsection: return "Midsection";
			case HullModuleKind::Engineering: return "Engineering";
			case HullModuleKind::Sponson: return "Sponson";
		}
		return "";
	}

	double HullModule::DistanceTo(DVec3 local) const
	{
		double dx = cs::Math::Max(0.0, cs::Math::Abs(local.X - Centre.X) - HalfExtent.X);
		double dy = cs::Math::Max(0.0, cs::Math::Abs(local.Y - Centre.Y) - HalfExtent.Y);
		double dz = cs::Math::Max(0.0, cs::Math::Abs(local.Z - Centre.Z) - HalfExtent.Z);
		return cs::Math::Sqrt(dx * dx + dy * dy + dz * dz);
	}

	// ---------------------------------------------------------------- ModularHull

	int ModularHull::KeelSections(double lengthKm)
	{
		return cs::Math::Clamp(2 + cs::ToInt(cs::Math::Round(cs::Math::Max(0.0, lengthKm) / KmPerSection)), 2, MaxSections);
	}

	namespace
	{
		// The C# tuple key (int section, int side).
		struct SectionSide
		{
			int section = 0;
			int side = 0;
			bool operator==(const SectionSide& o) const { return section == o.section && side == o.side; }
			size_t HashCode() const { return cs::HashCombine(std::hash<int>{}(section), std::hash<int>{}(side)); }
		};
	}

	cs::Ref<ModularHull> ModularHull::For(const cs::Ref<CombatSpec>& spec)
	{
		auto h = cs::New<ModularHull>();
		int n = KeelSections(spec->LengthKm);
		double halfBeam = 0.5 * cs::Math::Max(0.02, spec->BeamFraction);
		double sectionLen = 1.0 / n;
		const auto& layout = spec->Layout; // `?? Array.Empty<SubsystemDef>()`: a cs::List is never null

		for (int i = 0; i < n; i++)
		{
			auto m = cs::New<HullModule>();
			m->Id = i;
			m->Kind = i == n - 1 ? HullModuleKind::Bow : i == 0 ? HullModuleKind::Engineering : HullModuleKind::Midsection;
			m->Section = i;
			m->Centre = DVec3(0, 0, -0.5 + (i + 0.5) * sectionLen);
			m->HalfExtent = DVec3(halfBeam, halfBeam * 0.6, 0.5 * sectionLen);
			h->Modules.Add(m);
		}

		// Each mount goes on the section it sits in, or on a sponson off it when it is set out past the beam.
		cs::Dictionary<SectionSide, cs::Ref<HullModule>> sponsons;
		cs::Dictionary<cs::Ref<HullModule>, cs::List<DVec3>> mounts;
		for (int k = 0; k < layout.Count(); k++)
		{
			const auto& d = layout[k];
			int section = cs::Math::Clamp(cs::ToInt(cs::Math::Floor((d->Z + 0.5) * n)), 0, n - 1);
			auto host = h->Modules[section];
			if (OnSponson(d, halfBeam))
			{
				int side = d->X > 0 ? 1 : -1;
				if (!sponsons.TryGetValue(SectionSide{ section, side }, host))
				{
					host = cs::New<HullModule>();
					host->Id = h->Modules.Count();
					host->Kind = HullModuleKind::Sponson;
					host->Section = section;
					host->Side = side;
					sponsons[SectionSide{ section, side }] = host;
					h->Modules.Add(host);
				}
			}
			host->Hardpoints.Add(k);
			// `if (!mounts.TryGetValue(host, out var at)) mounts[host] = at = new List<DVec3>();` then `at.Add(...)`.
			mounts[host].Add(DVec3(d->X, d->Y, d->Z));
		}

		// A sponson is laid round its own mounts, out from the keel section's side.
		for (const auto& s : sponsons.Values())
		{
			double x = 0, y = 0, z = 0, reach = halfBeam;
			for (const auto& p : mounts.At(s))
			{
				x += p.X;
				y += p.Y;
				z += p.Z;
				reach = cs::Math::Max(reach, cs::Math::Abs(p.X));
			}
			int c = mounts.At(s).Count();
			double inner = halfBeam * SponsonReach;
			s->HalfExtent = DVec3(cs::Math::Max(0.005, 0.5 * (reach - inner)), halfBeam * 0.3, 0.5 * sectionLen * 0.8);
			s->Centre = DVec3(s->Side * (inner + s->HalfExtent.X), y / c, z / c);
		}

		// Mass: each sponson its share, the keel sections the rest evenly.
		double sponsonShare = sponsons.Count() == 0 ? 0 : cs::Math::Min(SponsonMassShare, 0.3 / sponsons.Count());
		double keelShare = (1 - sponsonShare * sponsons.Count()) / n;
		for (const auto& m : h->Modules)
		{
			m->MassShare = m->Kind == HullModuleKind::Sponson ? sponsonShare : keelShare;
			m->MaxStructure = m->Structure = spec->Hull * m->MassShare * StructurePerMass;
		}

		// Joints: section to section along the keel, and each sponson at its root.
		for (int i = 0; i + 1 < n; i++)
			h->AddJoint(i, i + 1, DVec3(0, 0, -0.5 + (i + 1) * sectionLen), spec->Hull * KeelJointShare);
		for (const auto& s : sponsons.Values())
			h->AddJoint(s->Section, s->Id, DVec3(s->Side * halfBeam * SponsonReach, s->Centre.Y, s->Centre.Z), spec->Hull * SponsonJointShare);

		h->Root = RootSection(h, layout, n);
		// US-T269: a component with a reinforced bulkhead in its sockets holds out longer, and so do the joints round it.
		ShipComponents::Reinforce(h, spec);
		return h;
	}

	bool ModularHull::OnSponson(const cs::Ref<SubsystemDef>& d, double halfBeam)
	{
		if (d->Buried || d->Spinal) return false;
		switch (d->Kind)
		{
			case SubsystemKind::Bridge:
			case SubsystemKind::Reactor:
			case SubsystemKind::Hangar:
			case SubsystemKind::Drill:
				return false;
			default:
				break;
		}
		return cs::Math::Abs(d->X) >= halfBeam * SponsonReach;
	}

	int ModularHull::RootSection(const cs::Ref<ModularHull>& h, const cs::List<cs::Ref<SubsystemDef>>& layout, int n)
	{
		for (auto want : { SubsystemKind::Bridge, SubsystemKind::Reactor })
			for (int k = 0; k < layout.Count(); k++)
				if (layout[k]->Kind == want)
					for (const auto& m : h->Modules)
						if (m->Hardpoints.Contains(k)) return m->Kind == HullModuleKind::Sponson ? m->Section : m->Id;
		return n / 2;
	}

	void ModularHull::AddJoint(int a, int b, DVec3 at, double strength)
	{
		auto j = cs::New<HullJoint>();
		j->A = a;
		j->B = b;
		j->At = at;
		j->MaxStrength = strength;
		j->Strength = strength;
		Joints.Add(j);
	}

	cs::Ref<HullModule> ModularHull::ModuleAt(DVec3 local) const
	{
		cs::Ref<HullModule> best = nullptr;
		double bestD = cs::Double::MaxValue;
		for (const auto& m : Modules)
		{
			if (m->Detached) continue;
			// Inside a sponson's box beats being inside the keel's, which overlaps it at the root.
			double d = m->DistanceTo(local) - (m->Kind == HullModuleKind::Sponson ? 1e-6 : 0);
			if (d < bestD)
			{
				bestD = d;
				best = m;
			}
		}
		return best;
	}

	cs::Ref<HullModule> ModularHull::ModuleOf(int subsystem) const
	{
		for (const auto& m : Modules)
			if (m->Hardpoints.Contains(subsystem)) return m;
		return nullptr;
	}

	cs::List<cs::Ref<HullFragment>> ModularHull::Strike(DVec3 local, double damage)
	{
		auto m = ModuleAt(local);
		if (m == nullptr || damage <= 0) return cs::List<cs::Ref<HullFragment>>();
		m->Structure -= damage;

		double total = 0;
		for (const auto& j : Joints)
			if (!j->Broken() && j->Joins(m->Id)) total += Nearness(j, local);
		if (total > 0)
			for (const auto& j : Joints)
				if (!j->Broken() && j->Joins(m->Id)) j->Strength -= damage * JointLoad * Nearness(j, local) / total;

		if (m->Shattered())
			for (const auto& j : Joints)
				if (j->Joins(m->Id)) j->Strength = cs::Math::Min(j->Strength, 0.0);
		return Separate();
	}

	double ModularHull::Nearness(const cs::Ref<HullJoint>& j, DVec3 local) { return 1 / (0.05 + DVec3::Distance(j->At, local)); }

	cs::List<cs::Ref<HullFragment>> ModularHull::Separate()
	{
		auto reach = Reachable();
		cs::List<cs::Ref<HullFragment>> pieces;
		cs::List<bool> seen(std::vector<bool>(static_cast<size_t>(Modules.Count()))); // new bool[Modules.Count]
		for (const auto& m : Modules)
		{
			if (reach[m->Id] || m->Detached || seen[m->Id]) continue;
			auto piece = Gather(m->Id, seen, /*includeDetached:*/ false);
			for (int id : piece->Modules) Modules[id]->Detached = true;
			pieces.Add(piece);
		}
		return pieces;
	}

	cs::List<cs::Ref<HullFragment>> ModularHull::BreakUp(int seed)
	{
		uint32_t h = Hash(static_cast<uint32_t>(seed) * 2654435761u + 0x9E3779B9u);
		bool any = false;
		cs::Ref<HullJoint> strongest = nullptr;
		for (const auto& j : Joints)
		{
			if (j->Broken()) { any = true; continue; }
			h = Hash(h);
			double chance = 0.25 + 0.7 * (1 - j->Condition());
			if ((h & 0xFFFF) / 65535.0 < chance)
			{
				j->Strength = 0;
				any = true;
			}
			else if (strongest == nullptr || j->Condition() < strongest->Condition()) strongest = j;
		}
		// A destroyed hull never stays in one piece: the most strained joint left gives.
		if (!any && strongest != nullptr) strongest->Strength = 0;
		Separate();

		cs::List<cs::Ref<HullFragment>> pieces;
		cs::List<bool> seen(std::vector<bool>(static_cast<size_t>(Modules.Count()))); // new bool[Modules.Count]
		auto hulk = Gather(Root, seen, /*includeDetached:*/ true);
		hulk->Hulk = true;
		pieces.Add(hulk);
		for (const auto& m : Modules)
			if (!seen[m->Id]) pieces.Add(Gather(m->Id, seen, /*includeDetached:*/ true));
		return pieces;
	}

	void ModularHull::Repair(double share)
	{
		for (const auto& m : Modules)
			if (!m->Detached) m->Structure = cs::Math::Min(m->MaxStructure, cs::Math::Max(0.0, m->Structure) + m->MaxStructure * share);
		for (const auto& j : Joints)
			if (!j->Broken()) j->Strength = cs::Math::Min(j->MaxStrength, j->Strength + j->MaxStrength * share);
	}

	bool ModularHull::Intact() const
	{
		for (const auto& m : Modules) if (m->Detached || m->Shattered()) return false;
		for (const auto& j : Joints) if (j->Broken()) return false;
		return true;
	}

	cs::List<bool> ModularHull::Reachable() const
	{
		cs::List<bool> reach(std::vector<bool>(static_cast<size_t>(Modules.Count()))); // new bool[Modules.Count]
		cs::Queue<int> queue;
		reach[Root] = true;
		queue.Enqueue(Root);
		while (queue.Count() > 0)
		{
			int at = queue.Dequeue();
			if (Modules[at]->Shattered() && at != Root) continue;
			for (const auto& j : Joints)
			{
				if (j->Broken() || !j->Joins(at)) continue;
				int next = j->Other(at);
				if (reach[next]) continue;
				reach[next] = true;
				queue.Enqueue(next);
			}
		}
		return reach;
	}

	cs::Ref<HullFragment> ModularHull::Gather(int start, cs::List<bool>& seen, bool includeDetached) const
	{
		auto f = cs::New<HullFragment>();
		f->Shattered = true;
		cs::Queue<int> queue;
		seen[start] = true;
		queue.Enqueue(start);
		DVec3 weighted = DVec3::Zero;
		while (queue.Count() > 0)
		{
			int at = queue.Dequeue();
			const auto& m = Modules[at];
			f->Modules.Add(at);
			f->Hardpoints.AddRange(m->Hardpoints);
			f->MassShare += m->MassShare;
			weighted += m->Centre * m->MassShare;
			if (!m->Shattered()) f->Shattered = false;
			// A shattered module holds nothing together.
			if (m->Shattered()) continue;
			for (const auto& j : Joints)
			{
				if (j->Broken() || !j->Joins(at)) continue;
				int next = j->Other(at);
				if (seen[next] || (!includeDetached && Modules[next]->Detached)) continue;
				seen[next] = true;
				queue.Enqueue(next);
			}
		}
		f->Hulk = f->Modules.Contains(Root);
		f->Centroid = f->MassShare > 0 ? weighted / f->MassShare : Modules[start]->Centre;
		return f;
	}

	uint32_t ModularHull::Hash(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352du;
		x ^= x >> 15; x *= 0x846ca68bu;
		x ^= x >> 16;
		return x;
	}

	// ---------------------------------------------------------------- ModularHulls

	int ModularHulls::ApplyBreak(const cs::Ref<ShipCombat>& combat, const cs::Ref<HullFragment>& piece)
	{
		if (combat == nullptr || piece == nullptr || piece->Hulk) return 0;
		int lost = 0;
		for (int k : piece->Hardpoints)
		{
			if (k < 0 || k >= combat->Subsystems.Count()) continue;
			const auto& s = combat->Subsystems[k];
			if (!s->Destroyed()) lost++;
			s->Hp = 0;
			s->Restoring = 0;
			if (s->Kind() == SubsystemKind::ShieldGenerator) s->Shield = 0;
		}
		combat->Hull = cs::Math::Max(cs::Math::Min(combat->Hull, 1.0), combat->Hull - combat->Spec->Hull * piece->MassShare * HullLossPerMass);
		return lost;
	}

	// ---------------------------------------------------------------- HullSalvage

	cs::List<SalvageStack> HullSalvage::Scrap(const cs::Ref<ModularHull>& hull, const cs::Ref<HullFragment>& piece, const cs::Ref<CombatSpec>& spec,
		const cs::Ref<ShipCombat>& combat)
	{
		auto tonnes = cs::NewArray<double>(GoodInfo::Count);
		double dry = spec->DryMassTonnes > 0 ? spec->DryMassTonnes : spec->MassKt * 1000 * 0.1;
		for (int id : piece->Modules)
		{
			auto m = hull->Module(id);
			tonnes[static_cast<int>(Good::Alloys)] += dry * m->MassShare * StructureRecovery * (m->Shattered() ? 0.5 : 0.25 + 0.75 * m->Condition());
		}
		for (int k : piece->Hardpoints)
		{
			const auto& def = spec->Layout[k];
			double health = combat != nullptr && k < combat->Subsystems.Count() ? combat->Subsystems[k]->Health() : 1;
			if (health <= 0) continue;
			auto module = FireCones::ModuleOf(def->Kind);
			if (module.has_value())
			{
				const auto& ms = ModuleSpec::Of(*module);
				double scale = cs::Math::Max(0.1, spec->LengthKm / 0.45);
				for (int i = 0; i < ms->Bill.Count(); i++) tonnes[static_cast<int>(ms->Bill[i].first)] += ms->Tonnes(i, scale) * PartRecovery * health;
			}
			else tonnes[static_cast<int>(SalvageSession::ScrapOf(def->Kind))] += def->MaxHp * TonnesPerHp * health;
		}
		cs::List<SalvageStack> stacks;
		for (int g = 0; g < tonnes.Count(); g++)
			if (tonnes[g] > 0) stacks.Add(SalvageStack(static_cast<Good>(g), tonnes[g]));
		return stacks;
	}

	double HullSalvage::Value(const cs::List<SalvageStack>& stacks, const cs::Ref<Settlement>& port)
	{
		double total = 0;
		for (const auto& s : stacks) total += s.Tonnes * PriceOf(s.Good, port);
		return total;
	}

	double HullSalvage::PartValue(SubsystemKind kind, double health, double lengthKm, const cs::Ref<Settlement>& port)
	{
		auto module = FireCones::ModuleOf(kind);
		if (!module.has_value() || health <= 0) return 0;
		const auto& ms = ModuleSpec::Of(*module);
		double scale = cs::Math::Max(0.1, lengthKm / 0.45), total = 0;
		for (int i = 0; i < ms->Bill.Count(); i++) total += ms->Tonnes(i, scale) * PriceOf(ms->Bill[i].first, port);
		return total * SecondhandShare * cs::Math::Min(1.0, health);
	}

	double HullSalvage::PriceOf(Good g, const cs::Ref<Settlement>& port)
	{
		return port != nullptr && port->Price[static_cast<int>(g)] > 0 ? port->Price[static_cast<int>(g)] : GoodInfo::Of(g)->BasePrice;
	}

	// ---------------------------------------------------------------- WreckPiece

	DVec3 WreckPiece::OffsetKm(const cs::Ref<Wreck>& wreck, double jd) const
	{
		if (Hulk()) return DVec3::Zero;
		auto start = Fragment->Centroid * wreck->LengthKm;
		uint32_t h = ModularHull::Hash(static_cast<uint32_t>(wreck->Id) * 31337u + static_cast<uint32_t>(Index) * 7919u);
		auto heading = Fragment->Centroid.LengthSquared() > 1e-12 ? Fragment->Centroid.Normalized() : DVec3::UnitZ;
		// A little scatter off the line straight out from the hull, fixed per piece.
		double u = (h & 0xFFFF) / 65535.0 - 0.5, v = ((h >> 16) & 0xFFFF) / 65535.0 - 0.5;
		heading = (heading + DVec3(u, v, u * v) * 0.8).Normalized();
		double share = 0.5 + 0.5 * ((ModularHull::Hash(h) & 0xFFFF) / 65535.0);
		double spread = cs::Math::Max(0.0, wreck->DebrisRadiusKm(jd) - 0.5 * wreck->LengthKm);
		return start + heading * (spread * share);
	}
}
