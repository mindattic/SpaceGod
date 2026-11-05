// Hud sights: the fighter seat's two reticles (US-T129). Declarations in Hud.Main.inl.

#include "UI/Hud.h"

#include "UnitySpaceApp.h"
#include "UI/Icons.h"

#include "Combat/Battle.h"
#include "Embodiment/FighterSeat.h"

using namespace SpaceGod::Core;

namespace SpaceGod::UI
{
	// ---------------------------------------------------------------- the seat's two reticles (US-T129)

	void Hud::DrawSeatNose()
	{
		auto f = cs::As<FighterSeat>(app->HeldSeat());
		if (f == nullptr || f->Unit == nullptr || f->Unit->Forward.LengthSquared() < 1e-12) return;
		// The view is the mouse's (the crosshair in the middle); the nose swings over to it at the craft's own rate.
		Vector2 nose;
		if (!DirectionToGui(f->Unit->Forward.Normalized(), nose)) return;
		auto centre = Vector2(Screen::width * 0.5f, Screen::height * 0.5f);
		float apart = (nose - centre).magnitude();
		if (apart < 3) return;
		if (apart > 22) DrawLine(nose, centre, Color(1, 1, 1, 0.18f), 1.0f);
		DrawBoresight(nose, Color(1, 1, 1, 0.85f));
	}
}
