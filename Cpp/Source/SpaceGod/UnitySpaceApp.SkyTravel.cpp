// US-T145: the sky's layers and the travel between them (members in UnitySpaceApp.SkyTravel.inl).

#include "UnitySpaceApp.h"

#include "BodyCatalog.h"
#include "Ephemeris.h"
#include "Camera/SpaceCamera.h"
#include "Camera/StarTravel.h"
#include "Embodiment/Embodiables.h"
#include "Embodiment/ShipHelm.h"
#include "Fleet.h"
#include "Combat/PilotFlight.h"
#include "ScaleModel.h"
#include "Ships.h"
#include "SimClock.h"
#include "Systems/Biomes.h"
#include "Systems/DeepSpace.h"
#include "Systems/Galaxy.h"
#include "Systems/HelmFtl.h"
#include "World.h"

#include "Rendering/Pipeline/RenderBackend.h"
#include "Rendering/SystemSkies.h"
#include "Views/BodyView.h"
#include "Views/SkyLayers.h"
#include "Views/SkyView.h"

#include <cmath>
#include <vector>

namespace SpaceGod
{
	using Views::SkyLayers;

	namespace
	{
		/// How fast the tunnel builds toward what the drive asks and how fast it unwinds after a peak, per real second.
		constexpr float WarpRise = 3.0f, WarpFall = 0.75f;
		/// The tunnel's level through the crossing itself: the Milky Way shows through gentle streaks.
		constexpr float CruiseWarp = 0.28f;
		/// Real seconds before a drop-out that the streaks build back to full strength.
		constexpr double DropRampSeconds = 1.6;

		Vector3 Unity(Core::DVec3 ecliptic)
		{
			Vector3 v = Views::BodyView::ToUnity(ecliptic);
			float m = v.magnitude();
			return m > 1e-6f ? v / m : Vector3(0, 0, 1);
		}
	}

	void UnitySpaceApp::BeginSkyTravel()
	{
		SkyLayersView = cs::New<Views::SkyLayers>(worldRoot);
		SkyLayers::HeroSpec(); // in the catalogue from the start, so Shift+K can cycle to it too
	}

	void UnitySpaceApp::EndSkyTravel()
	{
		if (SkyLayersView != nullptr) SkyLayersView->Dispose();
		SkyLayersView = nullptr;
	}

	cs::String UnitySpaceApp::SkyHeroSystem()
	{
		if (Galaxy == nullptr) return cs::String();
		if (skyHeroGalaxy == Galaxy.get() && !skyHero.empty()) return skyHero;
		skyHeroGalaxy = Galaxy.get();
		skyHero.clear();
		cs::String first;
		for (const auto& s : Galaxy->Systems)
		{
			if (!Rendering::SystemSkies::IsWild(s->Name)) continue;
			if (first.empty()) first = s->Name;
			if (Core::Biomes::KindOf(s->Name) == Core::BiomeKind::VioletStorm) { skyHero = s->Name; break; }
		}
		if (skyHero.empty()) skyHero = first;
		return skyHero;
	}

	double UnitySpaceApp::SkyRimScene()
	{
		if (ActiveSystem == nullptr || Catalog() == nullptr || Catalog()->Count() == 0) return 0;
		double rim = Scale()->LocalDistance((*Catalog())[0], Core::HelmFtl::RimKm(ActiveSystem));
		// The game scale lays the worlds out on a scale of its own: the rim is never drawn inside the outermost of them.
		Core::DVec3 star = BodyScene(0);
		for (int i = 1; i < Catalog()->Count(); i++)
			if ((*Catalog())[i]->IsVisible()) rim = cs::Math::Max(rim, (BodyScene(i) - star).Length() * 1.15);
		return rim;
	}

	void UnitySpaceApp::SkyPreview(float warp, bool interstellar)
	{
		skyPreviewWarp = warp;
		skyPreviewInterstellar = warp >= 0 && interstellar;
		if (warp >= 0)
		{
			Vector3 f = SpaceCamera->Rotation * Vector3::forward();
			skyWarpAxis = f.normalized();
		}
	}

	void UnitySpaceApp::RefreshJumpStars()
	{
		skyJumpAge -= Time::unscaledDeltaTime;
		if (skyJumpFor == ActiveSystem && skyJumpChart == Galaxy->ChartVersion && skyJumpAge > 0) return;
		skyJumpFor = ActiveSystem;
		skyJumpChart = Galaxy->ChartVersion;
		skyJumpAge = 3.0f;
		std::vector<Vector3> dirs;
		std::vector<Color> colors;
		for (const auto& t : Core::FtlTargets::All(Galaxy, ActiveSystem))
		{
			double km;
			Core::DVec3 d = Core::HelmFtl::Bearing(t, ActiveSystem, Core::DVec3::Zero, km);
			if (d.LengthSquared() < 1e-12) continue;
			dirs.push_back(Unity(d));
			// A running system's star warm and bright, a beacon cyan, a plain star on the charts a little dimmer.
			if (t->IsBeacon()) colors.push_back(Color(0.55f, 0.95f, 1.0f, 0.85f));
			else if (t->System != nullptr) colors.push_back(Color(1.0f, 0.93f, 0.8f, 1.0f));
			else colors.push_back(Color(0.9f, 0.92f, 1.0f, 0.7f));
		}
		SkyLayersView->SetJumpStars(dirs, colors);
	}

	void UnitySpaceApp::UpdateSkyTravel(float dt)
	{
		if (SkyLayersView == nullptr || ActiveSystem == nullptr) return;
		double jd = Clock->Jd;
		bool crossing = InFtlCruise();
		auto c = FtlCruise;
		bool atStop = crossing && c != nullptr && c->AtVoidStop(jd);

		// What the drive asks of the tunnel now.
		float want = 0;
		if (!crossing && Ftl->Stage == Core::FtlStage::Spooling && Ftl->Target != nullptr && IsPiloting())
		{
			float f = static_cast<float>(Ftl->SpoolFraction(jd));
			want = std::pow(f, 2.2f);
			double km;
			skyWarpAxis = Unity(Core::HelmFtl::Bearing(Ftl->Target, ActiveSystem, PilotRootKm(), km));
		}
		else if (crossing && c != nullptr)
		{
			skyWarpAxis = Unity(c->Direction());
			if (atStop)
			{
				// Cooling at a void stop: the open sky; spooling again: the streaks build as the charge does.
				double charge = c->ChargeFraction(jd);
				want = charge > 0 ? std::pow(static_cast<float>(charge), 2.2f) : 0.0f;
			}
			else
			{
				want = CruiseWarp;
				double left = FtlSecondsTo(c->ArriveJd());
				if (left > 0 && left < DropRampSeconds) want = Mathf::Lerp(1.0f, CruiseWarp, static_cast<float>(left / DropRampSeconds));
			}
		}

		// The moments the sky is swapped: each at the tunnel's peak.
		if (crossing != skyWasCrossing) skyWarp = 1; // engaged out of the system, or dropped out into the next
		else if (crossing && atStop != skyWasStop) skyWarp = 1; // dropped out at a void stop, or engaged again from one
		skyWasCrossing = crossing;
		skyWasStop = atStop;
		if (want > skyWarp) skyWarp = Mathf::Min(want, skyWarp + dt * WarpRise);
		else skyWarp = Mathf::Max(want, skyWarp - dt * WarpFall);
		float warp = skyPreviewWarp >= 0 ? skyPreviewWarp : skyWarp;
		if (skyPreviewWarp >= 0)
		{
			Vector3 f = SpaceCamera->Rotation * Vector3::forward();
			skyWarpAxis = f.normalized();
		}
		bool interstellar = crossing || skyPreviewInterstellar;

		SkyLayers::Frame frame;
		frame.System = ActiveSystem->Name;
		frame.Wild = Rendering::SystemSkies::IsWild(frame.System);
		frame.Hero = frame.System == SkyHeroSystem();
		frame.Interstellar = interstellar;
		frame.CenterRelative = Relative(BodyScene(0));
		frame.RimRadius = static_cast<float>(SkyRimScene());
		frame.Warp = warp;
		frame.WarpAxis = skyWarpAxis;
		SkyLayersView->Update(frame, dt);

		Sky->Suppress(SkyLayersView->OwnsBackdrop());
		float smear = SkyLayers::SmearFor(warp);
		if (smear != skyLastSmear || smear > 0)
		{
			skyLastSmear = smear;
			Sky->SetSmear(smear, skyWarpAxis);
		}

		// The stars the drive can jump to, where they are, over the system's sky (between the stars the interstellar
		// view draws them itself).
		bool jumpStars = !interstellar && !Travel->Active;
		if (jumpStars) RefreshJumpStars();
		SkyLayersView->ShowJumpStars(jumpStars);

		// The light: the hero painting's core lights the hero system whenever its painting is the sky.
		bool heroLit = !interstellar && frame.Hero && SkyLayers::Mode() != Views::SkyMode::Cube;
		cs::String gradeKey = heroLit ? cs::Cat("hero:", frame.System, "|", Sky->ShownKey()) : cs::String();
		if (gradeKey != skyGradeKey)
		{
			bool wasHero = !skyGradeKey.empty();
			skyGradeKey = gradeKey;
			if (heroLit) Rendering::RenderBackend::Active()->SetSystemGrade(SkyLayers::HeroGrade(frame.System));
			else if (wasHero) Rendering::RenderBackend::Active()->SetSystemGrade(Rendering::SystemSkies::GradeFor(frame.System));
		}
	}

	bool UnitySpaceApp::SkyTakeHelm(bool wantDrive)
	{
		if (World() == nullptr) return false;
		if (IsPiloting() && (!wantDrive || Core::HelmFtl::Refusal(Piloted.Ship).empty())) return true;
		cs::Ref<Core::Ship> best, fallback;
		for (const auto& s : Fleet()->Ships())
		{
			if (s->Destroyed || s->IsEscort() || !Fleet()->IsPresent(s) || s->Phase == Core::ShipPhase::Landed) continue;
			if (s->Battle != nullptr) continue;
			if (!Core::Embodiables::HelmOf(World(), s)->CanEmbody()) continue;
			if (Core::HelmFtl::Refusal(s).empty()) { best = s; break; }
			if (fallback == nullptr) fallback = s;
		}
		auto ship = best != nullptr ? best : wantDrive ? nullptr : fallback;
		if (ship == nullptr) return false;
		RememberCommand();
		ReleaseSeat();
		if (IsPiloting()) ExitPiloting();
		EnterPiloting(Target::OfShip(ship));
		return IsPiloting();
	}

	void UnitySpaceApp::SkyLeaveHelm()
	{
		if (IsPiloting()) ExitPiloting();
		if (AtControls()) ReturnToCommand();
	}

	bool UnitySpaceApp::SkyEdgePlace(double rimFraction, Core::DVec3 outward, bool facingIn)
	{
		if (!IsPiloting()) return false;
		auto ship = Piloted.Ship;
		Core::DVec3 out = outward.Normalized();
		// A fraction of the rim as drawn (the volume's radius at the scale in use), back to true km.
		double km = Scale()->InverseLocalDistance((*Catalog())[0], SkyRimScene() * rimFraction);
		DropSystemCruise(cs::String());
		pilotFrameBody = 0;
		SetpilotOffsetKm(out * km - Ephemeris()->Helio(0));
		SetpilotVelKmS(Core::DVec3::Zero);
		Core::DVec3 nose = facingIn ? -out : out;
		pilot().Forward = nose;
		pilot().Up = Core::PilotState::LevelUp(nose);
		pilot().AngularVelocity = Core::DVec3::Zero;
		ResetPilotAim();
		PinWherePiloted(ship);
		return true;
	}
}
