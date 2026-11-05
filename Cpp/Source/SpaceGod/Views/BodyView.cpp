// Port of Unity.SpaceGod Runtime/Views/BodyView.cs (US-T30).
#include "Views/BodyView.h"
#include "Rendering/AssetStreamer.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "Rendering/ShipMeshFactory.h"
#include "Rendering/ShipModels.h"
#include "Rendering/VoidStationModel.h"
#include "UnitySpaceApp.h"

#include "Astro.h"
#include "BodyDefinition.h"
#include "BodyOrientation.h"

#include <cmath>

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	using Rendering::AssetStreamer;
	using Rendering::Materials;
	using Rendering::MeshFactory;
	using Rendering::ShipMeshFactory;
	using Rendering::ShipModels;
	using Rendering::VoidStationModel;

	namespace
	{
		/// <summary>How fast the shell turns about its pole, and its rings, degrees per real second (US-T84).</summary>
		constexpr float ShellSpinDegS = 1.5f;
		constexpr float AccretionSpinDegS = 24.0f;
		constexpr float InnerGimbalSpinDegS = 5.0f;
		constexpr float OuterGimbalSpinDegS = -7.0f;
		constexpr float OuterGimbalPrecessDegS = 2.0f;

		/// <summary>The close-up shell is drawn once the body's radius is this much of its distance (about a tenth of the
		/// screen's height), and given up again below the second, so it does not flicker at the edge.</summary>
		constexpr float NearDetailOn = 0.05f;
		constexpr float NearDetailOff = 0.035f;
	}

	cs::Ref<Mesh>& BodyView::sphereMesh()
	{
		static cs::Ref<Mesh> mesh;
		return mesh;
	}

	cs::Ref<Mesh>& BodyView::quadMesh()
	{
		static cs::Ref<Mesh> mesh;
		return mesh;
	}

	BodyView::BodyView(const cs::Ref<BodyDefinition>& body, const cs::Ref<Transform>& parent)
	{
		Body = body;
		Root = NewGameObject(body->Name);
		Root->transform->SetParent(parent, false);

		if (!sphereMesh()) sphereMesh() = MeshFactory::UvSphere();
		if (!quadMesh()) quadMesh() = MeshFactory::Quad();

		station = body->Kind == BodyKind::Station;
		bool generator = body->IsVisible() && body->IsStar() && body->Singularity;
		AddComponent<MeshFilter>(Root)->sharedMesh = station ? ShipMeshFactory::GetStationBody(body->Name)
			: generator ? VoidStationModel::Shell(false) : sphereMesh();
		renderer = AddComponent<MeshRenderer>(Root);
		renderer->shadowCastingMode = ShadowCastingMode::Off;
		renderer->receiveShadows = false;

		Color tint(body->Color.R, body->Color.G, body->Color.B);
		if (!body->IsVisible())
		{
			// A barycentre: an empty point in space.
			material = Materials::Create(Materials::Planet(), body->Name);
			renderer->SetSharedMaterial(material);
			renderer->enabled = false;
			return;
		}
		if (station)
		{
			material = ShipModels::ProceduralMaterial();
			renderer->SetSharedMaterial(material);
			return;
		}
		if (body->IsStar() && body->Singularity)
		{
			// A Void Base's generator (U.SPC-T372): dark and light-swallowing, not lit like an ordinary star --
			// its own Color stands as-is, skipping the usual blackbody tint, and a tight violet glow stands in
			// for an event horizon instead of a corona that would otherwise balloon with distance (see Apply).
			// US-T84: the machine round it is a metal Dyson sphere; the dark sphere is its core.
			BuildVoidStation(parent);
			return;
		}
		else if (body->IsStar())
		{
			// Tinted by photosphere temperature relative to the Sun's, so the Sun keeps its look.
			Vector4 light = UnitySpaceApp::LightColor(body, 1);
			Color t(light.x, light.y, light.z);
			// Cool red dwarfs flare; brown dwarfs, cooler still, do not.
			flareStar = body->TeffK < 3300 && body->TeffK > 2400;
			nextFlareAt = Time::unscaledTime + 6.0f + UnityRandom::value() * 10.0f;
			material = Materials::Create(Materials::Sun(), body->Name);
			material->SetColor("_Color", Color(1.0f, 0.9f, 0.72f) * t);

			auto glowGo = NewGameObject("Corona");
			glowGo->transform->SetParent(parent, false);
			AddComponent<MeshFilter>(glowGo)->sharedMesh = quadMesh();
			auto glowRenderer = AddComponent<MeshRenderer>(glowGo);
			glowMaterial = Materials::Create(Materials::Glow(), "Corona");
			glowMaterial->SetColor("_Color", Color(1.0f, 0.72f, 0.4f) * t);
			glowMaterial->SetFloat("_Intensity", 1.1f);
			glowMaterial->SetFloat("_Falloff", 2.6f);
			glowRenderer->SetSharedMaterial(glowMaterial);
			glowRenderer->shadowCastingMode = ShadowCastingMode::Off;
			glow = glowGo->transform;
		}
		else
		{
			// Its maps load in the background (U.SPC-T414): until they arrive it is drawn in its own colour (a
			// gas giant, whose shader has no tint, on a swatch of it), the way an unmapped body always is.
			bool mapped = !body->Texture.empty();
			windProfile = body->GasGiant && mapped ? GasGiantWeather::ProfileFor(body->Name) : nullptr;
			material = Materials::Create(windProfile ? Materials::GasGiant() : Materials::Planet(), body->Name);
			material->SetFloat("_UseTexture", 0);
			material->SetColor("_Color", tint);
			bool clouds = body->SurfaceHaze >= 0.99f;
			// A visible-light cloud deck (Venus) is nearly featureless: keep its contrast faint.
			material->SetFloat("_NoiseStrength", clouds ? 0.05f : body->Kind == BodyKind::Planet ? 0.12f : 0.32f);
			material->SetFloat("_NoiseScale", clouds ? 1.6f : body->Kind == BodyKind::Planet ? 2.2f : 3.5f);
			if (windProfile) material->SetTexture("_MainTex", AssetStreamer::Swatch(tint));
			if (mapped)
			{
				std::weak_ptr<Material> m = material;
				bool tintTexture = body->TintTexture;
				float poleMargin = body->TexturePoleMargin;
				AssetStreamer::Request<Texture2D>(AssetStreamer::TexturePath(body->Texture), [m, tintTexture, poleMargin, tint](cs::Ref<Texture2D> texture)
				{
					auto mat = m.lock();
					if (!Alive(mat) || !texture) return;
					mat->SetTexture("_MainTex", texture);
					mat->SetFloat("_UseTexture", 1);
					mat->SetFloat("_PoleMargin", poleMargin);
					mat->SetColor("_Color", tintTexture ? tint : Color::white());
				});
			}
			if (!body->NormalMap.empty())
			{
				std::weak_ptr<Material> m = material;
				AssetStreamer::Request<Texture2D>(AssetStreamer::TexturePath(body->NormalMap), [m](cs::Ref<Texture2D> normalMap)
				{
					auto mat = m.lock();
					if (!Alive(mat) || !normalMap) return;
					mat->SetTexture("_NormalMap", normalMap);
					mat->SetFloat("_UseNormalMap", 1);
					// The maps are baked with relief exaggerated x8 (SOURCES.md); this brings it back toward legible.
					mat->SetFloat("_NormalStrength", 0.6f);
				});
			}
			material->SetFloat("_Haze", body->SurfaceHaze);
			material->SetColor("_HazeColor", tint);
			material->SetColor("_AtmoColor", Color(body->AtmosphereColor.R, body->AtmosphereColor.G, body->AtmosphereColor.B));
			material->SetFloat("_AtmoStrength", body->AtmosphereStrength);

			if (windProfile)
			{
				GasGiantWeather::SampleDrift(body, windProfile, drift);
				material->SetFloatArray("_Drift", drift);
				float maxDrift = 0;
				for (float d : drift) maxDrift = Mathf::Max(maxDrift, Mathf::Abs(d));
				// Loop short enough that the fastest jet moves at most ~4 degrees before the cross-fade.
				flowLoopDays = Mathf::Clamp(4.0f / Mathf::Max(maxDrift, 1e-3f), 0.02f, 2.0f);
				hasGreatRedSpot = body->Name == "Jupiter";
				material->SetVector("_GrsShape", Vector4(static_cast<float>(GasGiantWeather::GreatRedSpot::HalfWidthDeg),
					static_cast<float>(GasGiantWeather::GreatRedSpot::HalfHeightDeg), static_cast<float>(GasGiantWeather::GreatRedSpot::RotationPeriodDays), 0));
			}
		}
		renderer->SetSharedMaterial(material);

		if (body->HasRings())
		{
			auto ringGo = NewGameObject(body->Name + " Rings");
			ringGo->transform->SetParent(Root->transform, false);
			float inner = static_cast<float>(body->RingInnerKm / body->RadiusKm);
			float outer = static_cast<float>(body->RingOuterKm / body->RadiusKm);
			AddComponent<MeshFilter>(ringGo)->sharedMesh = Materials::Own(MeshFactory::Annulus(inner, outer));
			auto rr = AddComponent<MeshRenderer>(ringGo);
			ringMaterial = Materials::Create(Materials::Ring(), body->Name + " Rings");
			ringMaterial->SetFloat("_InnerKm", static_cast<float>(body->RingInnerKm));
			ringMaterial->SetFloat("_OuterKm", static_cast<float>(body->RingOuterKm));
			rr->SetSharedMaterial(ringMaterial);
			rr->shadowCastingMode = ShadowCastingMode::Off;
			ring = ringGo->transform;
		}
	}

	void BodyView::Apply(Vector3 relative, float radius, DVec3 mapNorth, DVec3 primeMeridian, bool highlighted, Vector3 cameraForward)
	{
		auto t = Root->transform;
		t->localPosition = relative;
		t->localScale = Vector3::one() * radius;
		if (station)
		{
			// Spine along the station's pole, turning slowly about it.
			Vector3 axis = ToUnity(Body->Pole);
			Vector3 side = Vector3::Cross(axis, Vector3::up()).sqrMagnitude() > 1e-6f ? Vector3::Cross(axis, Vector3::up()).normalized() : Vector3::right();
			t->localRotation = Quaternion::LookRotation(axis, Quaternion::AngleAxis(Time::unscaledTime * 6.0f, axis) * side);
			return;
		}
		if (voidStation)
		{
			ApplyVoidStation(relative, radius, highlighted, cameraForward);
			return;
		}
		Vector3 pole = ToUnity(Body->Pole);
		// The sphere mesh has north on +Y, longitude 0 on +X and east on +Z. Ecliptic→Unity is a
		// reflection, which turns the right-handed (meridian, north, east) triad into a proper rotation.
		DVec3 east = DVec3::Cross(mapNorth, primeMeridian);
		t->localRotation = Quaternion::LookRotation(ToUnity(east), ToUnity(mapNorth));
		if (ring)
		{
			// Rings lie in the equator and must not spin with the surface.
			ring->SetRotation(Quaternion::FromToRotation(Vector3::up(), pole));
			ringMaterial->SetVector("_PlanetPosRadius", Vector4(relative.x, relative.y, relative.z, radius));
		}
		if (!Body->IsStar()) material->SetFloat("_Highlight", highlighted ? 1.0f : 0.0f);

		if (glow)
		{
			float distance = relative.magnitude();
			// Corona: at least ~4% of the view so the Sun reads as a star from anywhere in the system;
			// dimmer stars (K and M dwarfs) a little less.
			float presence = Mathf::Clamp(Mathf::Pow(static_cast<float>(Body->LuminositySolar), 0.12f), 0.45f, 1.1f);
			float size = Mathf::Max(radius * 7.0f, distance * 0.07f * presence * CoronaPresence) * (1 + flare * 0.8f);
			Vector3 toCamera = distance > 0 ? -relative / distance : -cameraForward;
			glow->localPosition = relative + toCamera * Mathf::Min(radius * 1.05f, distance * 0.5f);
			glow->localRotation = Quaternion::LookRotation(-toCamera, Vector3::up());
			glow->localScale = Vector3::one() * size;
		}
	}

	void BodyView::BuildVoidStation(const cs::Ref<Transform>& parent)
	{
		voidStation = true;
		metal = VoidStationModel::CreateMetal(Body->Name + " Frame");
		renderer->SetSharedMaterial(metal);

		auto child = [this](const cs::String& name, const cs::Ref<Mesh>& mesh, const cs::Ref<Material>& mat, cs::Ref<MeshRenderer>* made)
		{
			auto go = NewGameObject(name);
			go->transform->SetParent(Root->transform, false);
			AddComponent<MeshFilter>(go)->sharedMesh = mesh;
			auto r = AddComponent<MeshRenderer>(go);
			r->SetSharedMaterial(mat);
			r->shadowCastingMode = ShadowCastingMode::Off;
			r->receiveShadows = false;
			if (made) *made = r;
			return go->transform;
		};

		// The generator itself, dark as before, now the core the frame is built round.
		material = Materials::Create(Materials::Sun(), Body->Name);
		material->SetColor("_Color", Color(Body->Color.R, Body->Color.G, Body->Color.B));
		child("Generator Core", sphereMesh(), material, nullptr)->localScale = Vector3::one() * VoidStationModel::CoreRadius;

		child("Frame (near)", VoidStationModel::Shell(true), metal, &nearShell);
		nearShell->enabled = false;
		accretion = child("Accretion Ring", VoidStationModel::AccretionRing(), metal, nullptr);
		gimbals[0] = child("Gimbal Ring (inner)", VoidStationModel::Gimbal(0), metal, nullptr);
		gimbals[1] = child("Gimbal Ring (outer)", VoidStationModel::Gimbal(1), metal, nullptr);

		// The event horizon's violet glow, now at the centre: the core hides its middle and the plating its edge, so it
		// shows as a halo round the core and light spilling out through the frame's gaps (see ApplyVoidStation).
		auto horizonGo = NewGameObject("Event Horizon");
		horizonGo->transform->SetParent(parent, false);
		AddComponent<MeshFilter>(horizonGo)->sharedMesh = quadMesh();
		auto horizonRenderer = AddComponent<MeshRenderer>(horizonGo);
		glowMaterial = Materials::Create(Materials::Glow(), "Event Horizon");
		glowMaterial->SetColor("_Color", Color(0.55f, 0.15f, 0.85f));
		glowMaterial->SetFloat("_Intensity", 1.3f);
		glowMaterial->SetFloat("_Falloff", 2.2f);
		horizonRenderer->SetSharedMaterial(glowMaterial);
		horizonRenderer->shadowCastingMode = ShadowCastingMode::Off;
		glow = horizonGo->transform;
	}

	void BodyView::ApplyVoidStation(Vector3 relative, float radius, bool highlighted, Vector3 cameraForward)
	{
		// Everything is in body radii under the root, so the model is the body's size in either scale and the focus
		// framing, labels and picking (which take the body's radius) fit it as they do any body.
		auto t = Root->transform;
		float now = Time::unscaledTime;
		Vector3 up = Vector3::up();
		t->localRotation = Quaternion::FromToRotation(up, ToUnity(Body->Pole)) * Quaternion::AngleAxis(now * ShellSpinDegS, up);

		float distance = relative.magnitude();
		float size = distance > 0 ? radius / distance : 1.0f;
		if (nearDetail ? size < NearDetailOff : size > NearDetailOn) nearDetail = !nearDetail;
		renderer->enabled = !nearDetail;
		nearShell->enabled = nearDetail;

		// The rings turn on their own, not with the shell: its spin is taken back out first.
		Quaternion unspin = Quaternion::AngleAxis(-now * ShellSpinDegS, up);
		accretion->localRotation = unspin * Quaternion::AngleAxis(now * AccretionSpinDegS, up);
		gimbals[0]->localRotation = unspin * Quaternion::Euler(14.0f, 0, 0) * Quaternion::AngleAxis(now * InnerGimbalSpinDegS, up);
		gimbals[1]->localRotation = unspin * Quaternion::AngleAxis(now * OuterGimbalPrecessDegS, up) * Quaternion::Euler(0, 0, 68.0f)
			* Quaternion::AngleAxis(now * OuterGimbalSpinDegS, up);

		metal->SetFloat("_Highlight", highlighted ? 1.0f : 0.0f);

		Vector3 toCamera = distance > 0 ? -relative / distance : -cameraForward;
		glow->localPosition = relative;
		glow->localRotation = Quaternion::LookRotation(-toCamera, up);
		glow->localScale = Vector3::one() * (radius * 1.9f);
	}

	void BodyView::UpdateWeather(double jd, double simSecondsPerSecond)
	{
		if (flareStar)
		{
			// Proxima is a flare star: every so often it brightens several-fold in a second and fades.
			float now = Time::unscaledTime;
			if (now >= nextFlareAt)
			{
				float a = UnityRandom::value();
				flare = 0.6f + a * 1.4f;
				float b = UnityRandom::value();
				nextFlareAt = now + 14.0f + b * 26.0f;
			}
			flare = Mathf::Max(0.0f, flare - flare * Time::unscaledDeltaTime * 0.9f - Time::unscaledDeltaTime * 0.02f);
			material->SetFloat("_Intensity", 1.15f * (1 + flare));
			glowMaterial->SetFloat("_Intensity", 1.1f * (1 + flare * 1.5f));
		}
		if (windProfile)
		{
			double phase = jd / flowLoopDays;
			phase -= cs::Math::Floor(phase);
			double loopRealSeconds = flowLoopDays * Astro::SecondsPerDay / cs::Math::Max(simSecondsPerSecond, 1e-9);
			float amount = Mathf::Clamp01(static_cast<float>((loopRealSeconds - 1.5) / 3.0));
			material->SetVector("_Flow", Vector4(static_cast<float>(phase), static_cast<float>(std::fmod(phase + 0.5, 1.0)), flowLoopDays, amount));
			if (hasGreatRedSpot)
			{
				material->SetVector("_Grs", Vector4(static_cast<float>(GasGiantWeather::GreatRedSpot::EastLongitudeDeg(jd)),
					static_cast<float>(GasGiantWeather::GreatRedSpot::LatitudeDeg), static_cast<float>(GasGiantWeather::GreatRedSpot::MapShiftDeg(jd)), 1));
			}
			else
			{
				material->SetVector("_Grs", Vector4::zero());
			}
		}
		else if (!Body->IsStar() && !station && Body->SurfaceHaze >= 0.99f)
		{
			// Cloud deck angle relative to the (slowly turning) surface frame, in the map's east sense.
			double cloudRate = -360.0 / VenusCloudPeriodDays - BodyOrientation::RateDegPerDay(Body);
			double angle = std::fmod(cloudRate * (jd - Astro::J2000), 360.0);
			// The shader samples its noise at (longitude + _CloudSpin), so features move by -_CloudSpin.
			material->SetFloat("_CloudSpin", static_cast<float>(-angle * Astro::Deg2Rad));
		}
	}

	void BodyView::SetVisible(bool visible)
	{
		renderer->enabled = visible && Body->IsVisible();
	}
}
