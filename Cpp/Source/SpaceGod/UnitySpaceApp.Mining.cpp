// US-T264: hands-on mining at the helm, as the player sees it (see UnitySpaceApp.Mining.inl).

#include "UnitySpaceApp.h"

#include "Economy/HandMining.h"
#include "Embodiment/ShipHelm.h"
#include "Ships.h"
#include "Systems/Exploration.h" // US-T260
#include "Systems/Galaxy.h"
#include "UI/Hud.h"
#include "World.h"

namespace SpaceGod
{
	using namespace Core;

	void UnitySpaceApp::ShowSurveyNotices()
	{
		auto exploration = Galaxy != nullptr ? Exploration::Find(*Galaxy) : nullptr;
		if (exploration == nullptr || exploration->Notices.Count() == 0) return;
		for (const auto& n : exploration->TakeNotices()) ShowToast(n);
	}

	void UnitySpaceApp::UpdateMining(const cs::Ref<Ship>& ship)
	{
		ShowSurveyNotices(); // US-T260: finds made at the helm
		miningReadout = cs::String();
		if (ship == nullptr || pilotHelm == nullptr || pilotHelm->Ship != ship) return;
		auto beam = pilotHelm->Beam;
		if (beam == nullptr) return;
		for (const auto& n : beam->TakeNotices()) ShowToast(n);
		miningReadout = beam->Readout;
	}

	bool UnitySpaceApp::MiningBeamScene(DVec3& from, DVec3& to)
	{
		if (!IsPiloting() || pilotHelm == nullptr || pilotHelm->Beam == nullptr || !pilotHelm->Beam->Active()) return false;
		auto ship = World()->PilotedShip;
		if (ship == nullptr || ship->Destroyed || pilotHelm->Ship != ship) return false;
		auto beam = pilotHelm->Beam;
		from = ShipScene(ship);
		// The face is a few km off at most: laid out at the ship scale, as anything about a hull is.
		to = from + (beam->Direction * beam->DistanceKm).SwapYZ() * BattleKmScale();
		return true;
	}

	void UnitySpaceApp::DrawMiningHud()
	{
		if (miningReadout.empty() || !IsPiloting() || !ShowHud || ScreenCleared) return;
		if (miningStyle == nullptr)
		{
			miningBack = UI::Hud::Solid(Color(0.08f, 0.05f, 0.02f, 0.82f), Color(1.0f, 0.62f, 0.25f, 0.7f));
			miningPanel = cs::New<GUIStyle>();
			miningPanel->normal.background = miningBack;
			miningPanel->border = RectOffset(2, 2, 2, 2);
			miningStyle = cs::New<GUIStyle>();
			miningStyle->fontSize = 14;
			miningStyle->fontStyle = FontStyle::Bold;
			miningStyle->alignment = TextAnchor::MiddleCenter;
			miningStyle->normal.textColor = Color(1.0f, 0.74f, 0.4f);
			miningStyle->clipping = TextClipping::Clip;
			miningSmall = cs::New<GUIStyle>(*miningStyle);
			miningSmall->fontSize = 11;
			miningSmall->fontStyle = FontStyle::Normal;
			miningSmall->normal.textColor = UI::Hud::Dim;
		}
		auto bar = miningReadout.find('|');
		cs::String head = bar == cs::String::npos ? miningReadout : miningReadout.substr(0, bar);
		cs::String tail = bar == cs::String::npos ? cs::String() : miningReadout.substr(bar + 1);
		float w = 820, h = tail.empty() ? 28.0f : 46.0f;
		// Above the tractor beam's readout when that shows too.
		float lift = tractorReadout.empty() ? 0.0f : 54.0f;
		auto r = Rect((Screen::width - w) * 0.5f, Screen::height - 124 - h - lift, w, h);
		if (!UiRects.Contains(r)) UiRects.Add(r); // world names skip it (US-T212)
		GUI::Box(r, GUIContent::none(), miningPanel);
		GUI::Label(Rect(r.x, r.y + 4, w, 20), head, miningStyle);
		if (!tail.empty()) GUI::Label(Rect(r.x, r.y + 24, w, 18), tail, miningSmall);
		auto beam = pilotHelm != nullptr ? pilotHelm->Beam : nullptr;
		if (beam != nullptr && beam->Active())
		{
			// The face's heat toward the next fracture.
			float heat = static_cast<float>(cs::Math::Clamp(beam->Heat, 0.0, 1.0));
			GUI::DrawTexture(Rect(r.x + 2, r.yMax() - 4, (w - 4) * heat, 3), miningBack);
		}
	}
}
