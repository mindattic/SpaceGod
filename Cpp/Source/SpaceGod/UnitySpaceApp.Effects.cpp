// US-T447: the world's effects, wired to the views (declarations in UnitySpaceApp.Effects.inl). View only: nothing here
// writes to the simulation, so co-op stays in lockstep and the bench is untouched; every peer draws its own.

#include "UnitySpaceApp.h"

#include "Camera/SpaceCamera.h"
#include "Views/EffectsTune.h"
#include "Views/GateView.h"
#include "Views/HullThrusters.h"
#include "Views/ShipView.h"
#include "Views/WorldFx.h"

#include "BodyCatalog.h"
#include "Combat/Battle.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/War.h"
#include "Fleet.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "Systems/Galaxy.h"
#include "Systems/JumpGate.h"
#include "World.h"

#include <cmath>

namespace SpaceGod
{
	using namespace Core;
	using Tune = Views::EffectsTune;

	namespace
	{
		DVec3 D(Vector3 v) { return DVec3(v.x, v.y, v.z); }

		/// <summary>How thick and slow its exhaust is: a fighter 0, a capital 1, a freighter or a miner heavier than its length.</summary>
		float HeftOf(double lengthKm, ShipClass cls)
		{
			float h = Mathf::Clamp01(static_cast<float>((std::log10(cs::Math::Max(lengthKm, 1e-4)) + 1.7) / 1.7));
			if (cls == ShipClass::Freighter || cls == ShipClass::Miner || cls == ShipClass::OreTug) h = Mathf::Clamp01(h + 0.35f);
			return h;
		}
	}

	void UnitySpaceApp::BeginWorldFx(float dt)
	{
		if (worldFx == nullptr || worldFxRoot != systemRoot)
		{
			worldFx = cs::New<Views::WorldFx>(this, systemRoot);
			worldFxRoot = systemRoot;
			fxShipsSeen.Clear();
			fxShipCount = -1;
			fxCruiseWas = SystemCruiseActive();
			fxFtlWas = InFtlCruise();
		}
		worldFx->Begin(dt);
	}

	void UnitySpaceApp::UpdateWorldFx()
	{
		if (worldFx == nullptr) return;
		const auto& ships = Fleet()->Ships();
		int count = ships.Count();
		bool fresh = fxShipCount < 0;
		bool rewinding = worldFx->Rewinding();
		double ppu = PixelsPerUnitAtUnitDistance();
		while (fxShipsSeen.Count() < count) fxShipsSeen.Add(FxShipSeen());
		// A transit is drawn against the gate's ring, not only the hull (small against it).
		float gateScale = static_cast<float>(Scale()->ShipLength(Views::GateView::DiameterKm)) * Tune::GateScaleShare;

		Views::FxHull h;
		cs::List<Views::HullThrusters::JetView> mains, others;
		cs::List<Vector3> vents;
		for (int i = 0; i < count; i++)
		{
			const auto& ship = ships[i];
			auto& seen = fxShipsSeen[i];
			if (ship == nullptr) { seen.Live = false; continue; }
			bool placed = i < shipScene.Count() && i < shipRotation.Count() && i < shipLength.Count();
			// A gate taking it: space folds shut on it where it was last drawn.
			if (ship->Destroyed && ship->GateDeparted)
			{
				if (seen.Live && !rewinding) worldFx->GateTransit(seen.Scene, seen.Forward, Mathf::Max(seen.Length, gateScale), false);
				seen.Live = false;
				continue;
			}
			bool live = !ship->Destroyed && placed;
			// New to this system's fleet beside one of its gates: it came through it.
			if (live && !fresh && i >= fxShipCount && !rewinding && ActiveSystem != nullptr)
			{
				double gateSize = Scale()->ShipLength(Views::GateView::DiameterKm);
				for (const auto& gate : ActiveSystem->Gates())
				{
					auto at = Scale()->MapNear(Ephemeris(), BodyScenes(), Catalog()->Root()->Index, gate->PositionKm);
					if ((shipScene[i] - at).Length() > Tune::GateArrivalDiameters * gateSize) continue;
					worldFx->GateTransit(shipScene[i], shipRotation[i] * Vector3::forward(), Mathf::Max(static_cast<float>(shipLength[i]), gateScale), true);
					break;
				}
			}
			seen.Live = live;
			if (live)
			{
				seen.Scene = shipScene[i];
				seen.Forward = shipRotation[i] * Vector3::forward();
				seen.Length = static_cast<float>(shipLength[i]);
			}

			// Its exhaust, vapour and venting, read off the model as drawn this frame.
			if (i >= shipViews.Count()) continue;
			const auto& view = shipViews[i];
			if (view == nullptr || !view->Shown() || !view->Root) continue;
			auto t = view->Root->transform;
			Vector3 rel = t->localPosition;
			Quaternion rot = t->localRotation;
			float len = t->localScale.x;
			if (!(len > 0)) continue;
			h = Views::FxHull();
			h.Key = ship.get();
			h.Scene = SpaceCamera->ScenePosition + D(rel);
			h.Length = len;
			h.Px = static_cast<float>(len * ppu / cs::Math::Max(static_cast<double>(rel.magnitude()), 1e-9));
			h.Heft = HeftOf(ship->LengthKm(), ship->Spec->Class);
			h.Wreck = ship->Destroyed || ship->Derelict;
			h.BoostCharge = ship->Combat != nullptr && ship->Combat->Spec != nullptr && ship->Combat->Spec->BoostDurationS > 0
				? static_cast<float>(ship->Combat->BoostFraction) : -1.0f;
			if (!h.Wreck)
				if (const auto* jets = view->JetsView())
				{
					mains.Clear();
					others.Clear();
					jets->Jets(mains, others);
					auto add = [&](const Views::HullThrusters::JetView& j, cs::List<Views::FxJet>& to)
					{
						Views::FxJet f;
						f.At = h.Scene + D(rot * (j.At * len));
						f.Exhaust = (rot * j.Exhaust).normalized();
						f.Width = j.Width * len;
						f.Level = j.Level;
						to.Add(f);
					};
					for (const auto& j : mains) add(j, h.Mains);
					for (const auto& j : others) add(j, h.Rcs);
				}
			vents.Clear();
			view->Vents(vents);
			for (const auto& v : vents)
			{
				h.Vents.Add(h.Scene + D(rot * (v * len)));
				Vector3 out(v.x, v.y, 0);
				out = out.sqrMagnitude() > 1e-8f ? out.normalized() : Vector3::up();
				h.VentOut.Add(rot * out);
			}
			worldFx->Emit(h);
		}
		fxShipCount = count;

		// The craft in every fight (drawn by FighterViews, not as fleet ships): a thin, quick trail off each.
		auto war = World() != nullptr ? World()->War : nullptr;
		if (war != nullptr)
			for (const auto& b : war->Battles)
				for (const auto& u : b->Units)
				{
					if (u->Kind != UnitKind::Fighter || !u->Present() || u->Ship != nullptr) continue;
					auto scene = BattleScene(b, u->Pos);
					auto rel = Relative(scene);
					float len = static_cast<float>(Scale()->ShipLength(u->LengthKm));
					if (!(len > 0)) continue;
					auto ahead = BattleScene(b, u->Pos + u->Forward * 0.001) - scene;
					if (ahead.LengthSquared() < 1e-30) continue;
					ahead = ahead.Normalized();
					Vector3 fwd(static_cast<float>(ahead.X), static_cast<float>(ahead.Y), static_cast<float>(ahead.Z));
					h = Views::FxHull();
					h.Key = u.get();
					h.Scene = scene;
					h.Length = len;
					h.Px = static_cast<float>(len * ppu / cs::Math::Max(static_cast<double>(rel.magnitude()), 1e-9));
					h.Heft = 0;
					h.BoostCharge = u->Combat != nullptr && u->Combat->Spec != nullptr && u->Combat->Spec->BoostDurationS > 0
						? static_cast<float>(u->Combat->BoostFraction) : -1.0f;
					Views::FxJet j;
					j.At = scene - D(fwd * (len * 0.52f));
					j.Exhaust = -fwd;
					j.Width = len * 0.14f;
					j.Level = 1;
					h.Mains.Add(j);
					worldFx->Emit(h);
				}

		// The helm's drives engaging: the cruise drive's shock off the stern, the FTL drive's flash going and coming.
		bool cruise = SystemCruiseActive(), ftl = InFtlCruise();
		auto helm = IsPiloting() && World() != nullptr ? World()->PilotedShip : nullptr;
		if (helm != nullptr && !rewinding && (cruise != fxCruiseWas || ftl != fxFtlWas))
		{
			int i = ships.IndexOf(helm);
			if (i >= 0 && i < shipScene.Count() && i < shipRotation.Count() && i < shipLength.Count())
			{
				auto fwd = shipRotation[i] * Vector3::forward();
				float len = static_cast<float>(shipLength[i]);
				if (cruise && !fxCruiseWas) worldFx->CruiseEngage(shipScene[i], fwd, len);
				if (ftl != fxFtlWas) worldFx->FtlJump(shipScene[i], fwd, len, !ftl);
			}
		}
		fxCruiseWas = cruise;
		fxFtlWas = ftl;

		worldFx->Draw();
	}
}
