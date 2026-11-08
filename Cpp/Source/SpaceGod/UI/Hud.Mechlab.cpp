// US-T89: the Mechlab (declared in UI/Hud.Boards.inl), the outfitting panel's second view. The piloted hull is laid out as
// its components (Core::ShipComponents: the bow, the midsections, engineering, the sponsons), each a card with its type
// limits, the hardpoints mounted on it and its utility sockets. A hardpoint chosen here is refitted exactly as in the list
// view (OutfitOptions / OutfitOption, the co-op "outfit" entry); a utility socket through ShipComponents::Fit, carried as
// a co-op "utility" entry (Hud.Coop.cpp) so every peer refits the same hull the same way. US-T269: each card shows its
// utility tonnage, a part shot out with its component reads so, and a part too heavy for what is left is listed but not offered.

#include "UI/Hud.h"
#include "UI/UiMotion.h" // US-T255

#include "UnitySpaceApp.h"

#include "Ships.h"
#include "World.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/Construction.h"
#include "Combat/ModularHull.h"
#include "Combat/Outfitting.h"
#include "Combat/ShipComponents.h"
#include "Economy/Economy.h"
#include "Economy/Goods.h"
#include "Economy/Settlement.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	namespace
	{
		/// The tag a hardpoint carries on its card: its type and size, as the Mechlab's legend reads them.
		cs::String MountTag(const cs::Ref<OutfitSlot>& s)
		{
			const char* type = "";
			switch (s->Type)
			{
				case MountType::Ballistic: type = "B"; break;
				case MountType::Missile: type = "M"; break;
				case MountType::Energy: type = "E"; break;
				case MountType::Shield: type = "SH"; break;
				case MountType::Engine: type = "EN"; break;
				case MountType::Spinal: type = "SP"; break;
			}
			const char* size = s->Size == HardpointSize::Large ? "L" : s->Size == HardpointSize::Medium ? "M" : "S";
			return s->IsWeapon() && s->Type != MountType::Spinal ? cs::Cat(type, "-", size) : cs::String(type);
		}

		/// Each type's colour on the cards, so a component's limits read at a glance.
		Color MountColor(MountType t)
		{
			switch (t)
			{
				case MountType::Ballistic: return Color(1.0f, 0.78f, 0.35f, 1.0f);
				case MountType::Missile: return Color(0.45f, 0.75f, 1.0f, 1.0f);
				case MountType::Energy: return Color(0.5f, 1.0f, 0.55f, 1.0f);
				case MountType::Shield: return Color(0.55f, 0.9f, 1.0f, 1.0f);
				case MountType::Engine: return Color(1.0f, 0.55f, 0.35f, 1.0f);
				case MountType::Spinal: return Color(1.0f, 0.45f, 0.85f, 1.0f);
			}
			return Color::white();
		}

		constexpr Color UtilityColor{0.85f, 0.85f, 0.6f, 1.0f};

		/// "E 2  ·  M 1  ·  U 2 (45 / 80 t)": what a component's hardpoints allow by type, and its sockets and their tonnage.
		cs::String Limits(const ShipComponent& c, const cs::Ref<Loadout>& l)
		{
			static const MountType types[] = { MountType::Energy, MountType::Ballistic, MountType::Missile, MountType::Spinal, MountType::Shield, MountType::Engine };
			static const char* names[] = { "E", "B", "M", "SP", "SH", "EN" };
			cs::String s;
			for (int i = 0; i < 6; i++)
			{
				int n = c.Limit(l, types[i]);
				if (n > 0) s += cs::Cat(s.empty() ? "" : "  ·  ", names[i], " ", n);
			}
			if (c.Sockets > 0)
				s += cs::Cat(s.empty() ? "" : "  ·  ", "U ", c.Sockets, " (", cs::Fmt(ShipComponents::UsedTonnes(l, c), "0"), " / ",
					cs::Fmt(c.CapacityTonnes(l->Scale()), "0"), " t)");
			return s.empty() ? cs::String("structure only") : s;
		}

		/// What an empty socket on <paramref name="c"/> takes, for its row.
		cs::String SocketTakes(const ShipComponent& c, bool smallCraft)
		{
			static const char* names[] = { "thrusters", "life support", "heat sink", "power cell", "bulkhead" };
			cs::String s;
			for (const auto& u : UtilitySpec::Catalogue())
				if (c.Takes(u->Kind, smallCraft)) s += cs::Cat(s.empty() ? "" : " / ", names[static_cast<int>(u->Kind)]);
			return s.empty() ? cs::String("nothing") : s;
		}
	}

	void Hud::DrawMechlab(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, const cs::Ref<Settlement>& port, const cs::Ref<Loadout>& loadout)
	{
		UiMotion::Scope motion("hud.mechlab");
		constexpr float w = 800, rowH = 18, gap = 8;
		auto spec = ship->Combat->Spec;
		auto parts = ShipComponents::Of(loadout);
		int hardpoints = loadout->Slots.Count(), sockets = ShipComponents::SocketCount(loadout);
		if (outfitSlot >= hardpoints) outfitSlot = -1; // a facility row chosen in the list view
		if (outfitSocket >= sockets) outfitSocket = -1;
		if (outfitSlot >= 0) outfitSocket = -1;

		// What the choice offers: the modules a hardpoint takes, or the utility parts a socket takes (and emptying it).
		cs::List<int> utilityOptions;
		int chosenPart = outfitSocket >= 0 ? ShipComponents::ComponentOf(parts, outfitSocket) : -1;
		if (chosenPart >= 0)
		{
			for (const auto& u : UtilitySpec::Catalogue())
				if (parts[chosenPart].Takes(u->Kind, loadout->SmallCraft)) utilityOptions.Add(static_cast<int>(u->Kind));
			if (ShipComponents::At(loadout, outfitSocket).has_value()) utilityOptions.Add(-1);
		}
		int options = yard == nullptr ? 0 : outfitSlot >= 0 ? OutfitOptions(loadout, outfitSlot).Count() : utilityOptions.Count();

		// The cards, laid out in a grid: each as tall as its hardpoints and sockets, each grid row as tall as its tallest.
		int cols = parts.Count() <= 2 ? 2 : parts.Count() == 4 ? 2 : 3;
		float cardW = (w - 16 - 18 - gap * (cols - 1)) / cols;
		cs::List<float> rowHeights;
		for (int i = 0; i < parts.Count(); i++)
		{
			float h = 44 + (parts[i].Slots.Count() + parts[i].Sockets) * rowH;
			if (i % cols == 0) rowHeights.Add(h);
			else rowHeights[rowHeights.Count() - 1] = Mathf::Max(rowHeights[rowHeights.Count() - 1], h);
		}
		float contentH = 0;
		for (float h : rowHeights) contentH += h + gap;
		float optionsH = options > 0 ? 30 + options * rowH : 0;
		float listH = Mathf::Max(80.0f, Mathf::Min(contentH, Mathf::Min(380.0f, Screen::height - 190 - 112 - optionsH - 20)));
		float h = 112 + listH + optionsH;
		auto r = Panel(Rect((Screen::width - w) * 0.5f, 190, w, h));

		GUI::Label(Rect(r.x + 12, r.y + 6, w - 24, 22), cs::Cat("MECHLAB  ·  ", ship->Name, "  ·  ",
			yard != nullptr ? yard->Name : port != nullptr ? cs::Cat(port->Name, " (no yard)") : cs::String("not alongside a port")), title);
		if (GUI::Button(Rect(r.xMax() - 112, r.y + 6, 100, 20), "List view", button)) { outfitMechlab = false; outfitSocket = -1; }
		auto old = GUI::contentColor;
		GUI::contentColor = Dim;
		// The spec the hull fights with: what is fitted is what the battle uses.
		GUI::Label(Rect(r.x + 12, r.y + 28, w - 24, 18),
			cs::Cat("Hull ", cs::Fmt(spec->Hull, "N0"), "  ·  shield ", cs::Fmt(spec->Shield, "N0"), "  ·  armour ", cs::Fmt(spec->Armor, "0"),
				"  ·  dry mass ", Tonnes(spec->DryMassTonnes), "  ·  max speed ", cs::Fmt(spec->MaxSpeed, "0.###"), "  ·  turn ", cs::Fmt(spec->TurnRateDegS, "0.#"), " deg/s"), small);
		GUI::Label(Rect(r.x + 12, r.y + 46, w - 24, 18),
			cs::Cat("Crew ", cs::Fmt(spec->Crew, "N0"), "  ·  side thrust ", cs::Fmt(spec->LateralThrustMN, "0.##"), " MN  ·  ",
				hardpoints, " hardpoints on ", parts.Count(), " components  ·  utility ", ShipComponents::Fitted(loadout),
				" of ", sockets, " sockets  ·  credits ", Credits(ship->Faction->Treasury)), small);
		GUI::Label(Rect(r.x + 12, r.y + 64, w - 24, 18), yard != nullptr
			? "Pick a hardpoint or a utility socket on a component, then what to fit.   E energy · B ballistic · M missile · SP spinal · SH shield · EN engine · U utility.   F2 to close"
			: "Bring the ship alongside a shipyard (or a haven) to refit.   F2 to close", small);

		auto view = Rect(r.x + 8, r.y + 86, w - 16, listH);
		auto content = Rect(0, 0, w - 34, contentH);
		mechlabScroll = GUI::BeginScrollView(view, mechlabScroll, content);
		float y = 0;
		for (int i = 0; i < parts.Count(); i++)
		{
			const auto& c = parts[i];
			int col = i % cols, gridRow = i / cols;
			if (col == 0 && gridRow > 0) y += rowHeights[gridRow - 1] + gap;
			float x = col * (cardW + gap);
			auto card = Rect(x, y, cardW, rowHeights[gridRow]);
			GUI::Box(card, "", panel);
			GUI::contentColor = Accent;
			GUI::Label(Rect(x + 8, y + 4, cardW - 16, 18), cs::Strings::ToUpperInvariant(c.Name), small);
			GUI::contentColor = Dim;
			GUI::Label(Rect(x + 8, y + 21, cardW - 16, 16), Limits(c, loadout), small);
			float ry = y + 40;
			for (int s : c.Slots)
			{
				auto slot = loadout->Slots[s];
				cs::String fitted = loadout->Fitted[s].has_value() ? loadout->PartAt(s, *loadout->Fitted[s])->PartName() : cs::String("(empty)");
				if (slot->LayoutIndex >= 0 && slot->LayoutIndex < ship->Combat->Subsystems.Count())
				{
					auto sub = ship->Combat->Subsystems[slot->LayoutIndex];
					fitted += sub->Destroyed() ? cs::String("  (shot away)") : sub->Health() < 0.99 ? cs::Cat("  (", Percent(sub->Health()), ")") : cs::String();
				}
				if (slot->Stock.has_value() && loadout->Fitted[s] != slot->Stock) fitted += "  *";
				GUI::contentColor = Color::white();
				if (GUI::Button(Rect(x + 4, ry, cardW - 8, rowH - 1), cs::Cat("        ", fitted), s == outfitSlot ? rowSelected : row))
				{
					outfitSlot = s == outfitSlot ? -1 : s;
					outfitSocket = -1;
				}
				GUI::contentColor = MountColor(slot->Type);
				GUI::Label(Rect(x + 8, ry, 40, rowH), MountTag(slot), small);
				ry += rowH;
			}
			for (int k = 0; k < c.Sockets; k++)
			{
				int socket = c.FirstSocket + k;
				auto in = ShipComponents::At(loadout, socket);
				cs::String socketLabel = in.has_value() ? UtilitySpec::Of(*in)->Name : cs::Cat("(empty: ", SocketTakes(c, loadout->SmallCraft), ")");
				// US-T269: shot out with its component; a yard that mends the hull whole brings it back.
				if (in.has_value() && ShipComponents::ShotOut(spec, socket)) socketLabel += "  (shot out)";
				GUI::contentColor = in.has_value() ? Color::white() : Dim;
				if (GUI::Button(Rect(x + 4, ry, cardW - 8, rowH - 1), cs::Cat("        ", socketLabel), socket == outfitSocket ? rowSelected : row))
				{
					outfitSocket = socket == outfitSocket ? -1 : socket;
					outfitSlot = -1;
				}
				GUI::contentColor = UtilityColor;
				GUI::Label(Rect(x + 8, ry, 40, rowH), "U", small);
				ry += rowH;
			}
		}
		GUI::EndScrollView();

		if (options == 0) { GUI::contentColor = old; return; }
		float oy = r.y + 94 + listH;
		GUI::contentColor = Accent;
		cs::String into = outfitSlot >= 0
			? cs::Cat("Fit onto ", cs::Strings::ToLowerInvariant(loadout->Slots[outfitSlot]->Name()), ":")
			: cs::Cat("Utility socket on the ", cs::Strings::ToLowerInvariant(parts[chosenPart].Name), ":");
		GUI::Label(Rect(r.x + 12, oy, w - 24, 18), into, small);
		oy += 22;
		if (outfitSlot >= 0)
			for (const auto& option : OutfitOptions(loadout, outfitSlot))
			{
				OutfitOption(ship, yard, loadout, option, r.x, oy, w, rowH);
				oy += rowH;
			}
		else
			for (int kind : utilityOptions)
			{
				MechlabUtilityOption(ship, yard, loadout, kind, r.x, oy, w, rowH);
				oy += rowH;
			}
		GUI::contentColor = old;
	}

	void Hud::MechlabUtilityOption(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, const cs::Ref<Loadout>& loadout, int kind, float x, float y, float w, float rowH)
	{
		cs::Nullable<UtilityKind> k = kind >= 0 ? cs::Nullable<UtilityKind>(static_cast<UtilityKind>(kind)) : std::nullopt;
		bool current = k.has_value() && ShipComponents::At(loadout, outfitSocket) == k;
		auto q = ShipComponents::Quote(ship, yard, outfitSocket, k);
		GUI::contentColor = current ? Accent : Color::white();
		GUI::Label(Rect(x + 20, y, 200, rowH), k.has_value() ? UtilitySpec::Of(*k)->Name : cs::String("Empty the socket"), small);
		GUI::contentColor = Dim;
		if (k.has_value()) GUI::Label(Rect(x + 220, y, 230, rowH), UtilitySpec::Of(*k)->Effect, small);
		// US-T269: a part its component has no tonnage left for is listed, not offered.
		bool fits = current || ShipComponents::CanFit(loadout, outfitSocket, k);
		cs::String price = current ? cs::String("fitted")
			: !fits ? cs::String("too heavy for the component")
			: !q.InStock ? cs::Cat("yard is short of ", cs::Strings::ToLowerInvariant(GoodInfo::Of(q.Short)->Name))
			: q.Net() >= 0 ? Credits(q.Net()) : cs::Cat("+", Credits(-q.Net()), " back");
		GUI::Label(Rect(x + 460, y, 220, rowH), price, small);
		GUI::contentColor = Color::white();
		if (current || !fits || !q.InStock) return;
		if (!GUI::Button(Rect(x + w - 100, y - 1, 84, rowH - 1), k.has_value() ? "Fit" : "Empty", button)) return;
		if (CoopSends("utility", K(yard), outfitSocket, kind)) return; // an entry in co-op, as every refit is
		UtilityApply(ship, yard, outfitSocket, kind);
	}

	void Hud::UtilityApply(const cs::Ref<Ship>& ship, const cs::Ref<Settlement>& yard, int socket, int kind)
	{
		if (kind >= UtilitySpec::Catalogue().Count()) return;
		cs::Nullable<UtilityKind> k = kind >= 0 ? cs::Nullable<UtilityKind>(static_cast<UtilityKind>(kind)) : std::nullopt;
		auto loadout = Outfitting::LoadoutOf(ship);
		auto was = ShipComponents::At(loadout, socket);
		OutfitQuote paid;
		auto outcome = ShipComponents::Fit(app->Economy(), ship, yard, socket, k, paid);
		cs::String message;
		switch (outcome)
		{
		case OutfitOutcome::Ok:
			message = cs::Cat(k.has_value() ? cs::Cat(UtilitySpec::Of(*k)->Name, " fitted") : cs::Cat(was.has_value() ? UtilitySpec::Of(*was)->Name : cs::String("Socket"), " taken out"),
				paid.Net() >= 0 ? cs::Cat(" for ", Credits(paid.Net())) : cs::Cat(", ", Credits(-paid.Net()), " back"));
			break;
		case OutfitOutcome::CantAfford: message = "Not enough credits"; break;
		case OutfitOutcome::NoStock: message = cs::Cat(yard->Name, " is short of the materials"); break;
		case OutfitOutcome::Busy: message = "Not while fighting or boarded"; break;
		case OutfitOutcome::CannotDock: message = "Not welcome to refit here"; break;
		case OutfitOutcome::NotAYard: message = "Only a shipyard (or a haven) refits a hull"; break;
		case OutfitOutcome::WontFit: message = "That socket doesn't take it, or the component hasn't the tonnage"; break;
		default: message = "That socket doesn't take it"; break;
		}
		app->ShowToast(message);
	}
}
