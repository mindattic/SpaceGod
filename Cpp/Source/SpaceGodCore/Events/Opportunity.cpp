// Port of Unity.SpaceGod Assets/UnitySpace/Core/Events/Opportunity.cs (see docs/MIGRATION.md).

#include "Events/Opportunity.h"

#include "Ships.h"

namespace SpaceGod::Core
{
	const char* ToString(OpportunityKind v)
	{
		switch (v)
		{
			case OpportunityKind::Distress: return "Distress";
			case OpportunityKind::Derelict: return "Derelict";
			case OpportunityKind::Wreck: return "Wreck";
			case OpportunityKind::PirateAmbush: return "PirateAmbush";
			case OpportunityKind::ConvoyEscort: return "ConvoyEscort";
			case OpportunityKind::Bounty: return "Bounty";
			case OpportunityKind::Anomaly: return "Anomaly";
			case OpportunityKind::EscapePods: return "EscapePods";
		}
		return "";
	}

	const char* ToString(OpportunityState v)
	{
		switch (v)
		{
			case OpportunityState::Active: return "Active";
			case OpportunityState::Completed: return "Completed";
			case OpportunityState::Failed: return "Failed";
		}
		return "";
	}

	cs::String OpportunityReward::ToString() const
	{
		cs::List<cs::String> parts;
		if (Credits > 0) parts.Add(cs::Cat(cs::Fmt(Credits, "N0"), " cr"));
		if (CargoCrates > 0) parts.Add(cs::Cat(CargoCrates, " x ", GoodInfo::Of(CargoGood)->Name));
		if (ReputationWith != nullptr && Reputation != 0)
			parts.Add(cs::Cat(Reputation > 0 ? "+" : "", cs::Fmt(Reputation, "0"), " ", ReputationWith->Name));
		return parts.Count() == 0 ? cs::String("nothing") : cs::Strings::Join(", ", parts);
	}

	cs::String Opportunity::LabelOf(OpportunityKind k)
	{
		switch (k)
		{
			case OpportunityKind::Distress: return "DISTRESS";
			case OpportunityKind::Derelict: return "DERELICT";
			case OpportunityKind::Wreck: return "WRECK";
			case OpportunityKind::PirateAmbush: return "AMBUSH";
			case OpportunityKind::ConvoyEscort: return "ESCORT";
			case OpportunityKind::Bounty: return "BOUNTY";
			case OpportunityKind::EscapePods: return "PODS";
			default: return "ANOMALY";
		}
	}

	Rgb Opportunity::ColorOf(OpportunityKind k)
	{
		switch (k)
		{
			case OpportunityKind::Distress: return Rgb(1.0f, 0.35f, 0.3f);
			case OpportunityKind::Derelict: return Rgb(0.75f, 0.78f, 0.82f);
			case OpportunityKind::Wreck: return Rgb(0.95f, 0.6f, 0.3f);
			case OpportunityKind::PirateAmbush: return Rgb(1.0f, 0.2f, 0.55f);
			case OpportunityKind::ConvoyEscort: return Rgb(0.45f, 0.9f, 0.55f);
			case OpportunityKind::Bounty: return Rgb(1.0f, 0.85f, 0.3f);
			case OpportunityKind::EscapePods: return Rgb(0.35f, 0.95f, 1.0f);
			default: return Rgb(0.6f, 0.55f, 1.0f);
		}
	}
}
