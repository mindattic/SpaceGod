// Port of Unity.SpaceGod Runtime/Views/GateView.cs (US-T30).
#include "Views/GateView.h"

#include "UnitySpaceApp.h"
#include "Rendering/ShipMeshFactory.h"
#include "Rendering/ShipModels.h"

#include "BodyCatalog.h"
#include "RenderLod.h"
#include "ScaleModel.h"
#include "SimClock.h"
#include "Systems/Galaxy.h"
#include "Systems/GateNetwork.h"
#include "Systems/JumpGate.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::ShipMeshFactory;
	using Rendering::ShipModels;

	GateView::GateView(UnitySpaceApp* app_, const cs::Ref<Transform>& parent_)
		: app(app_), parent(parent_)
	{
		AddNew();
	}

	void GateView::AddNew()
	{
		const auto& gates = app->ActiveSystem->Gates();
		for (int i = views.Count(); i < gates.Count(); i++)
		{
			auto gate = gates[i];
			auto root = NewGameObject(gate->Name);
			root->transform->SetParent(parent, false);
			AddComponent<MeshFilter>(root)->sharedMesh = ShipMeshFactory::GetDockingRing(nullptr);
			auto renderer = AddComponent<MeshRenderer>(root);
			renderer->SetSharedMaterial(ShipModels::ProceduralMaterial());
			renderer->shadowCastingMode = ShadowCastingMode::Off;
			views.Add(std::make_tuple(gate, root, renderer));
		}
	}

	void GateView::Update(bool visible, const cs::Ref<JumpGate>& readyToActivate)
	{
		if (views.Count() < app->ActiveSystem->Gates().Count()) AddNew();
		double jd = app->Clock->Jd;
		double ppu = app->PixelsPerUnitAtUnitDistance();
		for (const auto& [gate, root, renderer] : views)
		{
			if (!visible)
			{
				renderer->enabled = false;
				continue;
			}

			auto scenePos = app->Scale()->MapNear(app->Ephemeris(), app->BodyScenes(), app->Catalog()->Root()->Index, gate->PositionKm);
			auto size = app->Scale()->ShipLength(DiameterKm);
			auto rel = app->Relative(scenePos);
			// Below a pixel a gate is not drawn at all (U.SPC-T415).
			bool shown = RenderLod::DrawsModel(RenderLod::Tier(RenderLod::Gate(), RenderLod::ApparentPx(size, rel.magnitude(), ppu)));
			renderer->enabled = shown;
			if (!shown) continue;
			auto t = root->transform;
			t->localPosition = rel;
			t->localScale = Vector3::one() * static_cast<float>(size);

			// A ship passes straight through along the real line to the paired star.
			auto dir = gate->PositionKm.Normalized().SwapYZ();
			auto forward = Vector3(static_cast<float>(dir.X), static_cast<float>(dir.Y), static_cast<float>(dir.Z));
			auto up = Mathf::Abs(Vector3::Dot(forward, Vector3::up())) > 0.99f ? Vector3::forward() : Vector3::up();
			t->localRotation = Quaternion::LookRotation(forward, up);

			bool ready = gate == readyToActivate;
			bool shut = gate->Blockaded(jd) || (gate->Partner != nullptr && gate->Partner->Blockaded(jd));
			// A ring out of action (wrecked in a war, or its far end) is dark until it is rebuilt.
			bool down = gate->Link == nullptr || !gate->Link->Working();
			auto color = ready ? ReadyColor : down ? DownColor : shut ? ShutColor : IdleColor;
			block.SetFloat(EngineGlowId, ready ? 3.0f : down ? 0.2f : 1.3f + 0.5f * Mathf::Sin(Time::unscaledTime * 2.0f));
			block.SetColor(EngineColorId, color);
			block.SetColor(ColorId, Color::white());
			block.SetFloat(HighlightId, 0.0f);
			renderer->SetPropertyBlock(block);
		}
	}
}
