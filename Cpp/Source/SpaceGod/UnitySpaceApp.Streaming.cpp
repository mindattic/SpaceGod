// Port of Unity.SpaceGod Runtime/UnitySpaceApp.Streaming.cs (US-T29): seamless streaming of ship models and maps.
// Members are declared in UnitySpaceApp.Streaming.inl.
//
// C#'s `shipViews == null` (before the first system is built) has no C++ form, the views being a cs::List: an empty
// list stands for it (ShipModelShown's bound check and UpdateStreaming's count both treat it as no views).

#include "UnitySpaceApp.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "ScaleModel.h"
#include "ShipLod.h"
#include "Ships.h"
#include "Streaming.h"
#include "World.h"
#include "Combat/War.h"
#include "Combat/Wreck.h"
#include "Settings/GameSetting.h"
#include "Systems/DeepSpace.h"
#include "Systems/Galaxy.h"
#include "Systems/JumpGate.h"
#include "Systems/TravelCourse.h"

#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Rendering/AssetStreamer.h"
#include "Rendering/Materials.h"
#include "Rendering/ShipModels.h"
#include "Streaming/AreaStreaming.h"
#include "Views/GateView.h"
#include "Views/ShipView.h"

using namespace SpaceGod::Core;

namespace SpaceGod
{
	namespace
	{
		/// C#'s Array.Resize(ref a, n): keeps what fits, default-fills the rest.
		template <class T> void ResizeArray(cs::List<T>& a, int n) { a.Raw().resize(static_cast<size_t>(n)); }
	}

	bool UnitySpaceApp::ShipModelShown(const cs::Ref<Core::Ship>& ship, double px) const
	{
		return ShipLod::DrawsModel(px) && ship != nullptr && ship->Id < shipViews.Count() && shipViews[ship->Id] != nullptr;
	}

	void UnitySpaceApp::BeginHelmGateCover()
	{
		helmCover->Begin(GateCover::HelmSeconds, GateCover::HelmHoldAt, Core::Streaming::GateCoverMaxSeconds()->Value);
		GateFlash = 1;
	}

	void UnitySpaceApp::OnSystemStreamed(const cs::Ref<StarSystem>& system)
	{
		activePaths.Clear();
		cs::Ref<BodyCatalog> catalog = system->Catalog;
		for (int i = 0; i < catalog->Count(); i++)
		{
			if (!(*catalog)[i]->Texture.empty()) activePaths.Add(Rendering::AssetStreamer::TexturePath((*catalog)[i]->Texture));
			if (!(*catalog)[i]->NormalMap.empty()) activePaths.Add(Rendering::AssetStreamer::TexturePath((*catalog)[i]->NormalMap));
		}
		releaseInS = ReleaseAfterS;
		releasePending = true;
	}

	void UnitySpaceApp::UpdateStreaming(float dt)
	{
		// (C#: shipViews == null || ActiveSystem == null.)
		if (ActiveSystem == nullptr) return;
		AreaStreaming::Attach(Sky, Galaxy); // US-T133
		if (helmCover->Active) helmCover->Advance(dt, StreamingPending > 0);

		DVec3 camera = StreamFocus.has_value() ? *StreamFocus : SpaceCamera->ScenePosition;
		if (streamSystem != ActiveSystem)
		{
			// A new system: the camera's jump into it is no motion to lead by.
			streamSystem = ActiveSystem;
			streamVelocity = DVec3::Zero;
			OnSystemStreamed(ActiveSystem);
			AreaStreaming::EnterSystem(ActiveSystem); // US-T133
		}
		else if (dt > 1e-4f && !StreamFocus.has_value())
		{
			DVec3 v = (camera - streamLastCamera) / dt;
			streamVelocity = DVec3::Lerp(streamVelocity, v, 1 - cs::Math::Exp(static_cast<double>(-6 * dt))); // (C#: -6 * dt is a float, Math.Exp a double)
		}
		streamLastCamera = camera;
		DVec3 heading;
		if (StreamFocus.has_value()) heading = StreamFocusHeading;
		else
		{
			Vector3 f = SpaceCamera->Rotation * Vector3::forward();
			heading = DVec3(f.x, f.y, f.z);
		}
		double ppu = PixelsPerUnitAtUnitDistance();
		double modelPx = ShipLod::IconBelowPx()->Value;
		StreamView view(camera, StreamFocus.has_value() ? DVec3::Zero : streamVelocity, heading, ppu, modelPx, Core::Streaming::LeadSeconds()->Value);

		// US-T133: the bodies' detail rings, from the same view (the body the camera is on, or the one selected, held close).
		int focusBody = SpaceCamera->FocusTarget.IsBody() ? SpaceCamera->FocusTarget.Body : Selected.IsBody() ? Selected.Body : -1;
		AreaStreaming::UpdateRings(view, bodyScene, bodyRadius, focusBody);

		const cs::List<cs::Ref<Core::Ship>>& ships = Fleet()->Ships();
		int n = cs::Math::Min(ships.Count(), shipViews.Count());
		streamItems.Clear();
		for (int i = 0; i < n; i++)
		{
			cs::Ref<Core::Ship> ship = ships[i];
			bool loaded = shipViews[i] != nullptr;
			bool shown = !ship->GateDeparted && ship->Controller()->Phase != ShipPhase::Landed;
			cs::Ref<Wreck> wreck = nullptr;
			if (ship->Destroyed && shown)
			{
				wreck = World()->War->Wrecks->Of(ship);
				shown = wreck != nullptr;
			}
			if (!shown)
			{
				// Nothing of it is drawn: a view it has goes.
				if (loaded) streamItems.Add(StreamItem(i, camera, 0, true));
				continue;
			}
			DVec3 at = wreck != nullptr ? WreckScene(wreck) : shipScene[i];
			bool always = ship == Piloted.Ship || (Selected.IsShip() && Selected.Ship == ship) ||
			              (SpaceCamera->FocusTarget.IsShip() && SpaceCamera->FocusTarget.Ship == ship);
			streamItems.Add(StreamItem(i, at, shipLength[i] * ShipLod::ExtentFactor(ship), loaded, always));
		}
		Core::Streaming::Plan(streamItems, view, streamLoad, streamUnload);

		if (!buildShipView) buildShipView = [this](int i) { return BuildStreamedShipView(i); };
		if (!streamNow) streamNow = [this]() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - streamClockStart).count(); };
		Core::Streaming::Spend(streamLoad, buildShipView, Core::Streaming::FrameBudgetMs()->Value, streamNow);

		int unloads = 0;
		for (int i : streamUnload)
		{
			if (unloads++ >= MaxUnloadsPerFrame) break;
			DropShipView(i);
		}

		// What the view needs now and does not have yet.
		int pending = 0;
		for (int i : streamLoad)
		{
			if (shipViews[i] != nullptr) continue;
			double px = ShipLod::ApparentPx(shipLength[i] * ShipLod::ExtentFactor(Fleet()->Ships()[i]), (shipScene[i] - camera).Length(), ppu);
			if (ShipLod::DrawsModel(px)) pending++;
		}
		// US-T133: only the maps of bodies big enough on screen to show them (a far body is its impostor, its own colour,
		// until it is near); before, every map of the system was waited on, holding a gate's cover open for a moon across it.
		pending += AreaStreaming::PendingInView();
		StreamingPending = pending;

		UpdateWarming(dt, camera);
	}

	bool UnitySpaceApp::BuildStreamedShipView(int i)
	{
		if (shipViews[i] != nullptr) return false;
		cs::Ref<Core::Ship> ship = Fleet()->Ships()[i];
		if (!Rendering::ShipModels::Ready(ship)) return false;
		shipViews[i] = cs::New<Views::ShipView>(ship, systemRoot);
		if (viewFaction.Count() != shipViews.Count()) ResizeArray(viewFaction, shipViews.Count());
		viewFaction[i] = ship->Faction;
		shipViews[i]->SetVisible(false);
		return true;
	}

	void UnitySpaceApp::DropShipView(int i)
	{
		cs::Ref<Views::ShipView> v = shipViews[i];
		if (v == nullptr) return;
		Rendering::Materials::DestroyOwned(v->Root->transform);
		GameObject::Destroy(v->Root);
		shipViews[i] = nullptr;
	}

	void UnitySpaceApp::UpdateWarming(float dt, DVec3 camera)
	{
		// US-T133: where the view is headed is asked for every frame, and the area streaming works through it within its
		// own budget (a system's sky, maps, models and meshes; the kit's meshes).
		PredictAreas(camera);
		AreaStreaming::Tick(dt);
		// An area let go of (no longer headed for): what it held is released with the rest nothing draws.
		if (AreaStreaming::ConsumeReleased()) releasePending = true;

		warmTimer -= dt;
		if (releaseInS > 0) releaseInS -= dt;
		if (warmTimer > 0) return;
		warmTimer = WarmEvery;
		if (releasePending && releaseInS <= 0)
		{
			releasePending = false;
			streamKeep.Clear();
			for (const cs::String& p : activePaths) streamKeep.Add(p);
			AreaStreaming::Keep(streamKeep);
			Rendering::AssetStreamer::ReleaseExcept(streamKeep);
		}
	}

	void UnitySpaceApp::PredictAreas(DVec3 camera)
	{
		using P = StreamPriority;
		// A trip between the stars under way (the god view's, gate by gate or across): its destination and next stop.
		if (Travel->Active && Travel->To != nullptr)
		{
			AreaStreaming::Prefetch(Travel->To, P::Urgent);
			AreaStreaming::Prefetch(Travel->Hop(), P::Urgent);
		}
		// The jump drive (US-T86): a crossing under way, a charge spooling, an autopilot flying a course, a course plotted.
		if (FtlCruise != nullptr) AreaStreaming::Prefetch(FtlCruise->To, FtlCruise->ToName, P::Urgent);
		if (Ftl->Stage == Core::FtlStage::Spooling && Ftl->Target != nullptr) AreaStreaming::Prefetch(Ftl->Target->System, Ftl->Target->Name, P::Imminent);
		if (FtlAutopilot->Active() && FtlAutopilot->Course != nullptr && FtlAutopilot->Course->Target != nullptr)
			AreaStreaming::Prefetch(FtlAutopilot->Course->Target->System, FtlAutopilot->Course->Target->Name, P::Ahead);
		if (PlottedCourse != nullptr && PlottedCourse->Target != nullptr)
			AreaStreaming::Prefetch(PlottedCourse->Target->System, PlottedCourse->Target->Name, P::Background);
		// The helm's gate-by-gate course: the system past its next gate.
		if (Course != nullptr && !Course->Done() && Course->Next() != nullptr) AreaStreaming::Prefetch(Course->Next()->Destination(), P::Ahead);
		// A gate in activation range, and any the view is near or closing on at its present speed (the cruise drive's
		// speeds included, US-T127): the rings are the gate's own size, so they scale with the scale model.
		if (nearGate != nullptr) AreaStreaming::Prefetch(nearGate->Destination(), P::Urgent);
		double ring = Scale()->ShipLength(Views::GateView::DiameterKm);
		for (const cs::Ref<JumpGate>& gate : ActiveSystem->Gates())
		{
			if (gate->Destination() == nullptr) continue;
			DVec3 at = Scale()->MapNear(Ephemeris(), bodyScene, Catalog()->Root()->Index, gate->PositionKm);
			if (Core::Streaming::ShouldWarm(camera, streamVelocity, at, ring * WarmGateDiameters)) AreaStreaming::Prefetch(gate->Destination(), P::Imminent);
		}
	}
}
