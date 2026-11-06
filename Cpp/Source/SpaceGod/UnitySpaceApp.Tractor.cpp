// US-T95: the tractor beam at the helm, the cargo pods drawn, and the dev menu's cargo wreck (see UnitySpaceApp.Tractor.inl).

#include "UnitySpaceApp.h"
#include "UnitySpaceAppActor.h"

#include "BodyCatalog.h"
#include "Camera/StarTravel.h"
#include "Combat/CargoPods.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Combat/War.h"
#include "Combat/Wreck.h"
#include "Comms/Comms.h"
#include "Economy/CargoHold.h"
#include "Economy/Goods.h"
#include "Ephemeris.h"
#include "Fleet.h"
#include "Sensors/SensorPosture.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "SimClock.h"
#include "UI/CheatMenu.h"
#include "UI/Hud.h"
#include "Views/CargoPodsView.h"
#include "World.h"

#include "Engine/Engine.h"

namespace SpaceGod
{
	using namespace Core;

	namespace
	{
		cs::String GoodName(Good g) { return cs::Strings::ToLowerInvariant(GoodInfo::Of(g)->Name); }

		cs::String Km(double km) { return km < 10 ? cs::Cat(cs::Fmt(km, "0.00"), " km") : cs::Cat(cs::Fmt(km, "N0"), " km"); }

		cs::String PodLine(const cs::Ref<CargoPod>& p)
		{
			return cs::Cat(cs::Fmt(p->Tonnes, "N0"), " t of ", GoodName(p->What), p->Stolen ? " (stolen)" : "", " from the ", p->From);
		}

		/// The app the game runs, for the comms hook (it is handed only the drop).
		UnitySpaceApp* RunningApp()
		{
			if (!GEngine) return nullptr;
			for (const FWorldContext& c : GEngine->GetWorldContexts())
			{
				if (c.WorldType != EWorldType::Game && c.WorldType != EWorldType::PIE) continue;
				auto* actor = AUnitySpaceAppActor::Find(c.World());
				if (actor != nullptr && actor->GetApp() != nullptr) return actor->GetApp();
			}
			return nullptr;
		}

		/// US-T137's "comply and drop your cargo": what the threatened hull jettisons floats out as pods to tractor in (the
		/// hook Core::Comms leaves for this), rather than appearing in the hold. Only the threats the hull flown makes.
		struct InstallCommsTractor
		{
			InstallCommsTractor()
			{
				Comms::Tractor() = [](const CargoDrop& drop) -> double
				{
					auto* app = RunningApp();
					auto world = app != nullptr ? app->World() : nullptr;
					if (world == nullptr || world->War == nullptr || world->War->Wrecks == nullptr) return 0;
					if (drop.To == nullptr || drop.To != world->PilotedShip) return 0;
					return world->War->Wrecks->SpilledPods()->Jettison(world, drop);
				};
			}
		} GInstallCommsTractor;

		int cargoWrecks = 0;
	}

	DVec3 UnitySpaceApp::TractorShipHelio() { return Ephemeris()->HelioAt(pilotFrameBody, Clock->Jd) + pilotOffsetKm(); }

	DVec3 UnitySpaceApp::CargoPodScene(const cs::Ref<CargoPod>& p)
	{
		double jd = Clock->Jd;
		auto anchor = AnchorScene(p->Site, p->Body, p->Anchor);
		if (p == tractorPod && tractorShip != nullptr && !tractorShip->Destroyed)
		{
			// Down the beam onto the hull, by the share of the gap closed (real km): the same at either scale.
			auto from = anchor + (p->DriftedAt(jd) + tractorTowAtLock).SwapYZ() * BattleKmScale();
			double t = tractorStartKm > 1e-9 ? cs::Math::Clamp(1 - tractorDistKm / tractorStartKm, 0.0, 1.0) : 0.0;
			return DVec3::Lerp(from, ShipScene(tractorShip), t);
		}
		return anchor + p->LocalAt(jd).SwapYZ() * BattleKmScale();
	}

	bool UnitySpaceApp::TractorLock(cs::String& said)
	{
		said = cs::String();
		auto ship = World()->PilotedShip;
		if (!IsPiloting() || ship == nullptr || ship->Destroyed || ship->Spec == nullptr) { said = "Take a helm to work a tractor beam"; return false; }
		if (Salvage != nullptr) { said = "Latched on: the clamps hold the hull, cast off (4) to work the beam"; return false; }
		// US-T167: the beam is a manoeuvring tool; not through a lane, the cruise drive, FTL, or with the ship powered down.
		if (ship->Phase == ShipPhase::Lane) { said = "No tractor beam in a hyperlane tube"; return false; }
		if (SystemCruiseActive()) { said = "No tractor beam under the cruise drive: drop out first (Shift+J)"; return false; }
		if (InFtlCruise() || Travel->Active) { said = "No tractor beam between the stars"; return false; }
		if (SensorPosture::Offline(ship->Combat)) { said = "No tractor beam while powered down (Shift+F3 to power up)"; return false; }
		auto pods = World()->War->Wrecks->Pods;
		auto spec = TractorSpec::Of(ship->Spec->Class);
		double d = 0;
		auto me = TractorShipHelio();
		auto hold = ship->PersonalHold();
		cs::Ref<CargoPod> pick;
		if (pods != nullptr)
		{
			// The nearest in reach that the hold has room for.
			double best = cs::Double::PositiveInfinity;
			for (const auto& p : pods->Items)
			{
				if (p->Expired(Clock->Jd) || hold->CrateRoom(p->What) <= 0) continue;
				double dist = (p->Helio(World(), Clock->Jd) - me).Length();
				if (dist <= spec.RangeKm && dist < best) { best = dist; pick = p; }
			}
			d = best;
		}
		if (pick == nullptr)
		{
			double nearest = 0;
			auto any = pods != nullptr ? pods->Nearest(World(), me, Clock->Jd, spec.RangeKm * 40, nearest) : nullptr;
			if (any != nullptr && nearest <= spec.RangeKm) said = cs::Cat("No room in the hold for ", GoodName(any->What));
			else if (any != nullptr) said = cs::Cat("Nearest cargo pod ", Km(nearest), " off: the beam reaches ", Km(spec.RangeKm));
			else said = cs::Cat("No cargo pods within the beam's ", Km(spec.RangeKm));
			return false;
		}
		tractorPod = pick;
		tractorShip = ship;
		tractorSpeedKmS = 0;
		tractorStartKm = tractorDistKm = d;
		tractorTowAtLock = pick->TowKm;
		return true;
	}

	void UnitySpaceApp::TractorRelease(const cs::String& why)
	{
		tractorPod = nullptr;
		tractorShip = nullptr;
		tractorSpeedKmS = 0;
		if (!why.empty()) ShowToast(why);
	}

	void UnitySpaceApp::UpdateTractor(const cs::Ref<Ship>& ship, float dt)
	{
		tractorReadout = cs::String();
		if (ship == nullptr || ship->Destroyed || ship->Spec == nullptr || World()->War == nullptr) return;
		if (Salvage != nullptr)
		{
			if (tractorPod != nullptr) TractorRelease("Beam off: the clamps hold the hull");
			return;
		}
		auto pods = World()->War->Wrecks->Pods;
		auto spec = TractorSpec::Of(ship->Spec->Class);
		double jd = Clock->Jd;
		auto me = TractorShipHelio();

		if (Input.ToggleTractor)
		{
			if (tractorPod != nullptr) TractorRelease("Tractor beam off");
			else
			{
				cs::String said;
				if (TractorLock(said)) ShowToast(cs::Cat("Tractor beam on: ", PodLine(tractorPod)));
				else ShowToast(said);
			}
		}

		if (tractorPod != nullptr && (ship->Phase == ShipPhase::Lane || SystemCruiseActive() || InFtlCruise() || Travel->Active || SensorPosture::Offline(ship->Combat)))
		{
			TractorRelease("Tractor beam off: the drive regime changed"); // US-T167
			return;
		}
		if (tractorPod != nullptr)
		{
			if (tractorShip != ship || pods == nullptr || !pods->Items.Contains(tractorPod) || tractorPod->Expired(jd))
			{
				TractorRelease(cs::String());
				return;
			}
			auto toShip = me - tractorPod->Helio(World(), jd);
			tractorDistKm = toShip.Length();
			if (tractorDistKm > spec.RangeKm * TractorBeam::SnapRange)
			{
				TractorRelease(cs::Cat("Tractor beam lost the pod: past ", Km(spec.RangeKm * TractorBeam::SnapRange)));
				return;
			}
			if (tractorDistKm <= TractorBeam::CatchRangeKm(ship->LengthKm()))
			{
				auto pod = tractorPod;
				auto got = TractorBeam::Catch(World(), ship, pod, jd);
				tractorCrates += got.Crates;
				cs::String line = got.Crates > 0
					? cs::Cat("Tractored aboard: ", got.Crates, " crate", got.Crates == 1 ? "" : "s", " of ", GoodName(pod->What), " (", cs::Fmt(got.Tonnes, "N0"), " t) from the ", pod->From)
					: cs::Cat("No room in the hold for ", GoodName(pod->What));
				if (pod->Tonnes <= 0) pods->Items.Remove(pod);
				TractorRelease(cs::String());
				if (got.HoldFull) { ShowToast(cs::Cat(line, ";  the hold is full, the rest drifts")); return; }
				// Then the next pod in reach, by itself, so a field can be swept from one place.
				cs::String said;
				ShowToast(TractorLock(said) ? cs::Cat(line, ";  beam on the next: ", PodLine(tractorPod)) : line);
				return;
			}
			double simDt = cs::Math::Min(10.0, dt * Clock->Warp().SimSecondsPerSecond * Clock->Speed);
			TractorBeam::Pull(tractorPod, spec, toShip, simDt, tractorSpeedKmS);
			tractorReadout = cs::Cat("TRACTOR BEAM  ·  ", PodLine(tractorPod), "  ·  ", Km(tractorDistKm), "  ·  hauling ",
				cs::Fmt(tractorSpeedKmS * 1000, "N0"), " m/s|Shift+T lets go  ·  reach ", Km(spec.RangeKm), "  ·  pull ",
				cs::Fmt(spec.PullKmS2 * 1000, "N0"), " m/s²  (", ship->Spec->DisplayName, ")");
			return;
		}

		// Free: the nearest pod about, for the readout.
		if (pods == nullptr || pods->Items.Count() == 0) return;
		double d = 0;
		auto near = pods->Nearest(World(), me, jd, spec.RangeKm * 4, d);
		if (near == nullptr) return;
		tractorReadout = d <= spec.RangeKm
			? cs::Cat("CARGO POD  ·  ", PodLine(near), "  ·  ", Km(d), "|Shift+T  tractor it in  ·  reach ", Km(spec.RangeKm))
			: cs::Cat("CARGO POD  ·  ", PodLine(near), "  ·  ", Km(d), "|close to ", Km(spec.RangeKm), " for the tractor beam (Shift+T)");
	}

	void UnitySpaceApp::UpdateCargoPodsView()
	{
		if (World() == nullptr || systemRoot == nullptr) return;
		if (cargoPodsView == nullptr || cargoPodsRoot != systemRoot)
		{
			cargoPodsView = cs::New<Views::CargoPodsView>(this, systemRoot);
			cargoPodsRoot = systemRoot;
		}
		// Nobody flies the hull that held the beam (the helm let go, another taken, a gate gone through): it lets go too.
		if (tractorPod != nullptr && (!IsPiloting() || World()->PilotedShip != tractorShip)) TractorRelease(cs::String());
		if (!IsPiloting()) tractorReadout = cs::String();
		cargoPodsView->Update();
	}

	void UnitySpaceApp::DrawTractorHud()
	{
		if (tractorReadout.empty() || !ShowHud || ScreenCleared) return;
		if (tractorStyle == nullptr)
		{
			tractorBack = UI::Hud::Solid(Color(0.03f, 0.07f, 0.06f, 0.82f), Color(0.45f, 1.0f, 0.78f, 0.7f));
			tractorPanel = cs::New<GUIStyle>();
			tractorPanel->normal.background = tractorBack;
			tractorPanel->border = RectOffset(2, 2, 2, 2);
			tractorStyle = cs::New<GUIStyle>();
			tractorStyle->fontSize = 14;
			tractorStyle->fontStyle = FontStyle::Bold;
			tractorStyle->alignment = TextAnchor::MiddleCenter;
			tractorStyle->normal.textColor = Color(0.55f, 1.0f, 0.82f);
			tractorStyle->clipping = TextClipping::Clip;
			tractorSmall = cs::New<GUIStyle>(*tractorStyle);
			tractorSmall->fontSize = 11;
			tractorSmall->fontStyle = FontStyle::Normal;
			tractorSmall->normal.textColor = UI::Hud::Dim;
		}
		auto bar = tractorReadout.find('|');
		cs::String head = bar == cs::String::npos ? tractorReadout : tractorReadout.substr(0, bar);
		cs::String tail = bar == cs::String::npos ? cs::String() : tractorReadout.substr(bar + 1);
		float w = 760, h = tail.empty() ? 28.0f : 46.0f;
		auto r = Rect((Screen::width - w) * 0.5f, Screen::height - 124 - h, w, h);
		if (!UiRects.Contains(r)) UiRects.Add(r); // US-T212: world names skip it
		GUI::Box(r, GUIContent::none(), tractorPanel);
		GUI::Label(Rect(r.x, r.y + 4, w, 20), head, tractorStyle);
		if (!tail.empty()) GUI::Label(Rect(r.x, r.y + 24, w, 18), tail, tractorSmall);
		if (tractorPod != nullptr && tractorStartKm > 1e-9)
		{
			float done = static_cast<float>(cs::Math::Clamp(1 - tractorDistKm / tractorStartKm, 0.0, 1.0));
			GUI::DrawTexture(Rect(r.x + 2, r.yMax() - 4, (w - 4) * done, 3), tractorBack);
		}
	}

	cs::Ref<Wreck> UnitySpaceApp::SpawnCargoWreck(cs::String& said)
	{
		said = CheatBlocker();
		if (!said.empty()) return nullptr;
		auto owner = World()->Independents != nullptr ? World()->Independents : World()->Sol;
		if (owner == nullptr) { said = "nobody here to own a freighter"; return nullptr; }
		int frame;
		DVec3 base, forward, up;
		CheatPlayerFrame(frame, base, forward, up);
		auto right = DVec3::Cross(forward, up);
		right = right.LengthSquared() > 1e-12 ? right.Normalized() : DVec3::UnitY;
		auto player = IsPiloting() ? World()->PilotedShip : nullptr;
		// A little ahead and to the side, so its pods spill within a strike craft's beam and clear of the hull flown.
		double ahead = 1.4 + (player != nullptr ? player->LengthKm() * 3 : 0.0);
		auto at = base + forward * ahead + right * (ahead * 0.35);
		double jd = Clock->Jd;
		static const Good goods[] = { Good::Electronics, Good::MedicalSupplies, Good::Machinery, Good::Platinum, Good::Food, Good::Munitions };
		auto good = goods[cargoWrecks % 6];
		auto ship = Fleet()->SpawnAt(cs::Cat("Laden Hauler ", ++cargoWrecks), ShipClass::Freighter, owner, frame, at, forward, jd);
		if (ship == nullptr) { said = "could not spawn the freighter"; return nullptr; }
		// Laden in the economy's own terms (CargoGood / CargoTonnes, the single slot every trader carries): a dozen crates'
		// worth, or what it can hold.
		double crate = CargoFootprint::Of(good).Tonnes;
		ship->HasCargo = true;
		ship->CargoGood = good;
		ship->CargoTonnes = ship->CapacityTonnes > 0 ? cs::Math::Min(ship->CapacityTonnes, crate * 12) : crate * 12;
		if (!World()->War->LoseOutsideBattle(ship, frame, at, jd, cs::Cat("The freighter ", ship->Name, " is blown apart; ", GoodName(good), " spills into space")))
		{
			said = "the freighter would not break up";
			return nullptr;
		}
		return World()->War->Wrecks->Of(ship);
	}

	bool UnitySpaceApp::TractorShowcase(int step, cs::String& failed)
	{
		failed = cs::String();
		if (step == 0)
		{
			if (IsPiloting() && World()->PilotedShip != nullptr && !World()->PilotedShip->Destroyed) return true;
			int frame;
			DVec3 base, forward, up;
			CheatPlayerFrame(frame, base, forward, up);
			auto ship = Fleet()->SpawnAt("Tractor Demo", ShipClass::Corvette, World()->Sol, frame, base, forward, Clock->Jd);
			if (ship == nullptr) { failed = "could not spawn a hull to fly"; return false; }
			ship->Ports = World()->FreeShipPorts();
			EnterPiloting(Target::OfShip(ship));
			return true;
		}
		if (!IsPiloting()) { failed = "the helm was not taken"; return false; }
		if (step == 1)
		{
			Clock->WarpIndex = 0; // real time, so the pull can be watched
			if (SpawnCargoWreck(failed) == nullptr) return false;
			return true;
		}
		if (tractorPod != nullptr) return true;
		return TractorLock(failed);
	}

	void UnitySpaceApp::RegisterTractorCheats()
	{
		using UI::CheatEntry;
		using UI::CheatRegistry;
		CheatRegistry::Add("Spawn", CheatEntry::Action("Cargo-laden wreck", "a laden freighter blown apart just ahead: its cargo spills as pods to tractor in (Shift+T, US-T95)",
			[](UnitySpaceApp& a)
			{
				cs::String said;
				auto w = a.SpawnCargoWreck(said);
				a.ShowToast(w != nullptr ? cs::Cat("The ", w->Name, " breaks up just ahead, cargo pods spilling: Shift+T to tractor them in") : said);
			})
			.WithBlocker([](UnitySpaceApp& a) { return a.CheatBlocker(); }));
	}

	namespace
	{
		UI::CheatRegistration GTractorCheats(&UnitySpaceApp::RegisterTractorCheats);
	}
}
