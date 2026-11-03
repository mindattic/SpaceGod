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
#include "Systems/Galaxy.h"
#include "Systems/JumpGate.h"

#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Rendering/AssetStreamer.h"
#include "Rendering/Materials.h"
#include "Rendering/ShipModels.h"
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
		if (helmCover->Active) helmCover->Advance(dt, StreamingPending > 0);

		DVec3 camera = StreamFocus.has_value() ? *StreamFocus : SpaceCamera->ScenePosition;
		if (streamSystem != ActiveSystem)
		{
			// A new system: the camera's jump into it is no motion to lead by.
			streamSystem = ActiveSystem;
			streamVelocity = DVec3::Zero;
			OnSystemStreamed(ActiveSystem);
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
		for (const cs::String& path : activePaths)
			if (!Rendering::AssetStreamer::Ready(path)) pending++;
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
		warmTimer -= dt;
		if (releaseInS > 0) releaseInS -= dt;
		if (warmTimer > 0) return;
		warmTimer = WarmEvery;

		streamKeep.Clear();
		for (const cs::String& p : activePaths) streamKeep.Add(p);
		if (Travel->Active && Travel->To != nullptr) { Warm(Travel->To); if (Travel->Hop() != nullptr) Warm(Travel->Hop()); }
		if (nearGate != nullptr) Warm(nearGate->Destination());
		double ring = Scale()->ShipLength(Views::GateView::DiameterKm);
		for (const cs::Ref<JumpGate>& gate : ActiveSystem->Gates())
		{
			if (gate->Destination() == nullptr) continue;
			DVec3 at = Scale()->MapNear(Ephemeris(), bodyScene, Catalog()->Root()->Index, gate->PositionKm);
			if (Core::Streaming::ShouldWarm(camera, streamVelocity, at, ring * WarmGateDiameters)) Warm(gate->Destination());
		}
		if (releasePending && releaseInS <= 0)
		{
			releasePending = false;
			Rendering::AssetStreamer::ReleaseExcept(streamKeep);
		}
	}

	void UnitySpaceApp::Warm(const cs::Ref<StarSystem>& system)
	{
		if (system == nullptr) return;
		for (const cs::String& path : Rendering::AssetStreamer::SystemPaths(system))
		{
			streamKeep.Add(path);
			Rendering::AssetStreamer::Warm(path);
		}
	}
}
