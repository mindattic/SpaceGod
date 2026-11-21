// US-T442: the god view has no collision and goes anywhere. Inside a planet, a star or a hull it draws what it is in from
// within: the skin see-through and the inside as a wireframe (BodyView/ShipView::SetInterior), so the player looks out
// onto the rest of the game. Drawing only: nothing in the simulation changes.

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Fleet.h"
#include "Ships.h"
#include "UnityCompat/UnityRendering.h"
#include "Views/BodyView.h"
#include "Views/InteriorWire.h"
#include "Views/ShipView.h"

namespace SpaceGod
{
	using namespace Core;
	using namespace UnityCompat;

	void UnitySpaceApp::UpdateGodInterior()
	{
		// The command view's free swim only: the helm's chase, a seat, a focus orbit and star travel draw as ever.
		bool god = SpaceCamera->Mode == Cameras::SpaceCamera::CameraMode::Free && !SpaceCamera->PilotLocked && !AtControls()
			&& !Travel->Active && !InFtlCruise();
		auto cam = SpaceCamera->ScenePosition;

		// Bodies: the depth under the skin as a share of the radius; the inside is all there 4% of the way in.
		for (int i = 0; i < bodyViews.Count(); i++)
		{
			const auto& view = bodyViews[i];
			if (!view) continue;
			float fade = 0;
			if (god && i < bodyScene.Count() && i < bodyRadius.Count() && bodyRadius[i] > 0)
			{
				double depth = 1.0 - (cam - bodyScene[i]).Length() / bodyRadius[i];
				fade = Views::InteriorWire::Fade(depth, 0.04);
			}
			view->SetInterior(fade);
		}

		// Hulls drawn as models: the depth inside the box round the hull as drawn (its own frame, one unit long), the
		// inside all there a fifth of the way to the box's narrowest half-width in.
		for (int i = 0; i < shipViews.Count(); i++)
		{
			const auto& view = shipViews[i];
			if (!view) continue;
			float fade = 0;
			if (god && view->Shown() && view->Root)
			{
				auto root = view->Root->transform;
				float length = root->localScale.x;
				// The camera is at the origin (floating origin): the root sits at the hull's position relative to it.
				Vector3 rel = root->position();
				if (length > 0 && rel.magnitude() < length * 1.5f)
				{
					Vector3 local = Quaternion::Inverse(root->rotation()) * (-rel) / length;
					Bounds box = view->HullBox();
					Vector3 d = local - box.center, e = box.extents;
					float gx = e.x - Mathf::Abs(d.x), gy = e.y - Mathf::Abs(d.y), gz = e.z - Mathf::Abs(d.z);
					float depth = Mathf::Min(gx, Mathf::Min(gy, gz));
					float ramp = Mathf::Max(Mathf::Min(e.x, e.y) * 0.2f, 1e-4f);
					fade = Views::InteriorWire::Fade(depth, ramp);
				}
			}
			view->SetInterior(fade);
		}
	}
}
