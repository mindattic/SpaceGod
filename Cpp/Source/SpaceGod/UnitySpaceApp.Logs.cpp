// US-T261: recovering Prose books (ship logs, station records) and the list the logs screen reads (declared in
// UnitySpaceApp.Main.inl; the screen itself is UI/LogsScreen). A book is recovered only when Prose has one for the thing it
// was found on (Core/Lore/LoreBooks): salvaging a derelict or a wreck once nobody hostile is aboard, a prize taken in a
// fight, a port's records on docking clearance. Display only: the list is saved with the World, no simulation code reads it.

#include "UnitySpaceApp.h"

#include "GameLore.h"
#include "Ships.h"
#include "World.h"
#include "Combat/Battle.h"
#include "Combat/Salvage.h"
#include "Combat/Wreck.h"
#include "Lore/LoreBooks.h"
#include "Systems/Galaxy.h"

namespace SpaceGod
{
	using namespace Core;

	void UnitySpaceApp::RecoverLog(const cs::String& key)
	{
		if (key.empty() || ActiveSystem == nullptr || World() == nullptr) return;
		if (GameLore().Find(key) == nullptr) return; // the usual case, every frame a prize is held: no text, nothing to do
		for (const auto& k : RecoveredLogKeys())
			if (k == key) return;
		if (!LoreBooks::Recover(World()->RecoveredLogs, GameLore(), key)) return;
		LoreBook book;
		LoreBooks::Read(GameLore(), key, book);
		ShowToast(cs::Cat(cs::String(key.size() > 8 && key.compare(key.size() - 8, 8, "/records") == 0 ? "RECORDS RECOVERED: " : "LOG RECOVERED: "),
			book.Title, "   (Shift+L to read)"));
	}

	cs::List<cs::String> UnitySpaceApp::RecoveredLogKeys() const
	{
		cs::List<cs::String> keys;
		auto add = [&keys](const cs::Ref<Core::World>& w)
		{
			if (w == nullptr) return;
			for (const auto& k : w->RecoveredLogs)
				if (!keys.Contains(k)) keys.Add(k);
		};
		if (ActiveSystem != nullptr) add(ActiveSystem->World);
		if (Galaxy != nullptr)
			for (const auto& s : Galaxy->Systems)
				if (s != nullptr && s != ActiveSystem) add(s->World);
		return keys;
	}

	void UnitySpaceApp::RecoverHulkLog(const cs::Ref<SalvageSession>& s)
	{
		if (s == nullptr || s->Hostile() || ActiveSystem == nullptr) return;
		cs::Ref<Ship> hull = s->Derelict != nullptr ? s->Derelict : (s->Wreck != nullptr ? s->Wreck->Ship : nullptr);
		if (hull == nullptr) return; // a defence platform's wreck keeps no log
		RecoverLog(LoreBooks::ShipLog(ActiveSystem->Name, hull->Name));
	}

	void UnitySpaceApp::RecoverPrizeLogs(const cs::Ref<Battle>& battle, const cs::Ref<BattleUnit>& unit)
	{
		if (battle == nullptr || unit == nullptr || ActiveSystem == nullptr) return;
		for (const auto& u : battle->Units)
		{
			if (u == nullptr || u == unit || !u->Captured || u->Side != unit->Side || u->Ship == nullptr) continue;
			RecoverLog(LoreBooks::ShipLog(ActiveSystem->Name, u->Ship->Name));
		}
	}
}
