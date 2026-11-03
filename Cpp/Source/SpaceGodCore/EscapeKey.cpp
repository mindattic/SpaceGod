// Port of Unity.SpaceGod Core/EscapeKey.cs (see docs/MIGRATION.md).

#include "EscapeKey.h"

namespace SpaceGod::Core
{
	const char* ToString(EscapeAction v)
	{
		switch (v)
		{
		case EscapeAction::ClosePanels: return "ClosePanels";
		case EscapeAction::LeaveFocus: return "LeaveFocus";
		case EscapeAction::Deselect: return "Deselect";
		case EscapeAction::OpenPauseMenu: return "OpenPauseMenu";
		}
		return "";
	}

	EscapeAction EscapeKey::Route(bool panelOpen, bool piloting, bool focused, bool selected)
	{
		if (panelOpen) return EscapeAction::ClosePanels;
		if (piloting) return EscapeAction::OpenPauseMenu;
		if (focused) return EscapeAction::LeaveFocus;
		if (selected) return EscapeAction::Deselect;
		return EscapeAction::OpenPauseMenu;
	}
}
