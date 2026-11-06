// US-T172: real light on the prebuilt hulls (see HullLighting.h).
#include "Rendering/HullLighting.h"
#include "Rendering/Materials.h"
#include "Rendering/MeshFactory.h"
#include "UnityCompat/UnityScene.h"
#include "UnitySpaceApp.h"
#include "Camera/SpaceCamera.h"
#include "UnrealSpace.h"
#include "SpaceGodLog.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Ships.h"
#include "Systems/Galaxy.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "StaticMeshResources.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

using namespace SpaceGod::Core;

namespace SpaceGod::Rendering
{
	namespace US = SpaceGod::UnrealSpace;

	namespace
	{
		// ------------------------------------------------------------ tunables (sg.HullLighting.*)

		TAutoConsoleVariable<int32> CVarEnabled(TEXT("sg.HullLighting"), 1,
			TEXT("US-T172: 1 draws the prebuilt hulls with lit PBR materials under real lights (the star as a shadowing key, the sky "
				"as image-based light, planetshine, nav lights, lamps); 0 the old self-lit materials. Read when the first hull is "
				"built (-LegacyHullLighting on the command line is the same as 0)."));
		TAutoConsoleVariable<float> CVarKey(TEXT("sg.HullLighting.Key"), 1.0f, TEXT("US-T172: the star's key light on the hulls (1 = the old shaders' brightness)."));
		TAutoConsoleVariable<float> CVarFill(TEXT("sg.HullLighting.Fill"), 1.0f, TEXT("US-T172: the companion star's (or the sky heart's) fill."));
		TAutoConsoleVariable<float> CVarSky(TEXT("sg.HullLighting.Sky"), 1.2f, TEXT("US-T172: the sky's image-based light on the hulls."));
		TAutoConsoleVariable<float> CVarPlanetshine(TEXT("sg.HullLighting.Planetshine"), 1.5f, TEXT("US-T172: light bounced off a nearby world."));
		TAutoConsoleVariable<float> CVarLamps(TEXT("sg.HullLighting.Lamps"), 1.0f, TEXT("US-T172: small craft headlights and a Void Station's Embers as lamps."));
		TAutoConsoleVariable<int32> CVarShadows(TEXT("sg.HullLighting.Shadows"), 1, TEXT("US-T172: the key light's self-shadows on the hulls."));
		TAutoConsoleVariable<int32> CVarNav(TEXT("sg.HullLighting.NavLights"), 1, TEXT("US-T172: nav lights and strobes on the hulls."));

		/// The Unreal side of the lighting, on the scene host actor (remade when the world changes).
		struct Rig
		{
			TWeakObjectPtr<AActor> Host;
			/// The star: the one directional light (the forward renderer shades only one; a second competes with it and is
			/// dropped, with an on-screen warning). The fill and the planetshine are far point lights instead (FarLight).
			TWeakObjectPtr<UDirectionalLightComponent> Key;
			TWeakObjectPtr<UPointLightComponent> Fill, Shine;
			TWeakObjectPtr<USkyLightComponent> Sky;
			std::vector<TWeakObjectPtr<USpotLightComponent>> Lamps;
			std::vector<TWeakObjectPtr<UPointLightComponent>> Embers;
		};

		Rig TheRig;
		StarLighting Stars;
		bool StarsSet = false, Overridden = false;
		float Eclipse = 1.0f;
		Vector3 ShineFrom;
		Vector3 ShineColor;
		float ShadowReach = 0;
		int ShadowCasting = 0, LampsLit = 0, EmbersLit = 0, NavShown = 0;
		double NextCapture = 0;
		bool CaptureSoon = true;

		bool Renders() { return FApp::CanEverRender(); }

		float Peak(Vector4 c) { return std::max({ c.x, c.y, c.z, 0.0f }); }

		FLinearColor Hue(float r, float g, float b)
		{
			float p = std::max({ r, g, b });
			return p > 1e-6f ? FLinearColor(r / p, g / p, b / p, 1) : FLinearColor::White;
		}

		template <class T> T* MakeLight(AActor* host, const TCHAR* name)
		{
			auto* l = NewObject<T>(host, MakeUniqueObjectName(host, T::StaticClass(), FName(name)), RF_Transient);
			l->SetMobility(EComponentMobility::Movable);
			l->SetUsingAbsoluteLocation(true);
			l->SetUsingAbsoluteRotation(true);
			l->SetUsingAbsoluteScale(true);
			l->bAffectTranslucentLighting = false;
			l->SetVolumetricScatteringIntensity(0.0f);
			l->SetCastShadows(false);
			return l;
		}

		void Register(AActor* host, USceneComponent* c)
		{
			c->RegisterComponent();
			host->AddInstanceComponent(c);
		}

		UDirectionalLightComponent* MakeDirectional(AActor* host, const TCHAR* name, bool shadows)
		{
			auto* l = MakeLight<UDirectionalLightComponent>(host, name);
			l->bAtmosphereSunLight = false;
			l->SetIntensity(0.0f);
			if (shadows)
			{
				// Cascades fitted to the hulls in view each frame (Tick), so their texels are a sliver of a hull at any scale.
				l->SetCastShadows(true);
				l->DynamicShadowCascades = 3;
				l->CascadeDistributionExponent = 2.0f;
				l->SetDynamicShadowDistanceMovableLight(1000.0f);
				// Screen-space contact shadows for the finest greebles (a fraction of the screen: no scale to it).
				l->ContactShadowLength = 0.02f;
				l->SetLightSourceAngle(0.3f);
			}
			Register(host, l);
			return l;
		}

		/// How far out a far light sits (cm, 1000 km): every hull in view is a sliver of that from the camera at either scale,
		/// so its light arrives from one direction and at one strength, as a directional light's would.
		constexpr double FarCm = 1e8;

		/// A light standing in for a second directional one: a point light far out with no inverse-square falloff and a reach
		/// of four times its distance (so across the scene it dims by well under a percent).
		UPointLightComponent* MakeFar(AActor* host, const TCHAR* name)
		{
			auto* l = MakeLight<UPointLightComponent>(host, name);
			l->bUseInverseSquaredFalloff = false;
			l->LightFalloffExponent = 1.0f;
			l->IntensityUnits = ELightUnits::Unitless;
			l->SetSourceRadius(0.0f);
			l->SetAttenuationRadius(static_cast<float>(FarCm * 4));
			l->SetIntensity(0.0f);
			Register(host, l);
			return l;
		}

		/// The rig on the scene's host, made on first use (and again for a new world).
		Rig* Ensure()
		{
			if (!Renders() || !HullLighting::Enabled()) return nullptr;
			AActor* host = UnityCompat::UnityScene::Host();
			if (!host || !host->GetWorld()) return nullptr;
			if (TheRig.Host.Get() == host && TheRig.Key.IsValid()) return &TheRig;
			TheRig = Rig();
			TheRig.Host = host;
			TheRig.Key = MakeDirectional(host, TEXT("HullKeyLight"), true);
			TheRig.Fill = MakeFar(host, TEXT("HullFillLight"));
			TheRig.Shine = MakeFar(host, TEXT("HullPlanetshine"));
			auto* sky = NewObject<USkyLightComponent>(host, MakeUniqueObjectName(host, USkyLightComponent::StaticClass(), FName(TEXT("HullSkyLight"))), RF_Transient);
			sky->SetMobility(EComponentMobility::Movable);
			sky->SourceType = ESkyLightSourceType::SLS_CapturedScene;
			// Only the sky itself (M_Sky and the stars are drawn 1e15 uu out, SpaceGodRender's _SkyDistance): never a world,
			// a star's face or a hull, at either scale (Neptune is under 1e10 uu out at the game scale).
			sky->SkyDistanceThreshold = 1e13f;
			sky->bLowerHemisphereIsBlack = false;
			sky->CubemapResolution = 64;
			sky->SetIntensity(0.0f);
			sky->SetCastShadows(false);
			Register(host, sky);
			TheRig.Sky = sky;
			CaptureSoon = true;
			UE_LOG(LogSpaceGod, Display, TEXT("[US-T172] Hull lighting: key, fill, planetshine and sky lights made"));
			return &TheRig;
		}

		void SetDirection(UDirectionalLightComponent* l, Vector3 travel)
		{
			if (travel.sqrMagnitude() < 1e-12f) return;
			FVector d = US::DirectionToUnreal(travel.normalized());
			FRotator r = d.Rotation();
			if (!l->GetComponentRotation().Equals(r, 0.01f)) l->SetWorldRotation(r);
		}

		/// A far light (MakeFar) placed so its light travels along <paramref name="travel"/> (Unity axes) through the camera.
		void SetFar(UPointLightComponent* l, Vector3 travel)
		{
			if (travel.sqrMagnitude() < 1e-12f) return;
			Camera* screen = UnityCompat::UnityScene::ScreenCamera();
			FVector cam = screen ? US::ToUnreal(screen->transform->position()) : FVector::ZeroVector;
			FVector at = cam - US::DirectionToUnreal(travel.normalized()) * FarCm;
			if (!l->GetComponentLocation().Equals(at, 1.0)) l->SetWorldLocation(at);
		}

		void SetLight(ULightComponent* l, float intensity, FLinearColor color)
		{
			if (FMath::Abs(l->Intensity - intensity) > 1e-4f * FMath::Max(1.0f, intensity)) l->SetIntensity(intensity);
			if (!l->GetLightColor().Equals(color, 1e-3f)) l->SetLightColor(color, false);
		}

		/// The key and fill from the star light in effect, the planetshine from Tick, the sky by mode.
		void Apply()
		{
			Rig* rig = Ensure();
			if (!rig || !StarsSet) return;
			Vector3 sun(Stars.SunPosition.x, Stars.SunPosition.y, Stars.SunPosition.z);
			Vector3 sun2(Stars.Sun2Position.x, Stars.Sun2Position.y, Stars.Sun2Position.z);
			// Lambert's 1/pi: an Unreal light of pi lux on a white surface gives 1, as the old shaders' star of 1 did.
			float key = Peak(Stars.SunColor) * UE_PI * CVarKey.GetValueOnGameThread() * (Overridden ? 1.0f : Eclipse);
			float fill = Peak(Stars.Sun2Color) * UE_PI * CVarFill.GetValueOnGameThread();
			if (auto* k = rig->Key.Get())
			{
				SetDirection(k, -sun);
				SetLight(k, key, Hue(Stars.SunColor.x, Stars.SunColor.y, Stars.SunColor.z));
			}
			if (auto* f = rig->Fill.Get())
			{
				SetFar(f, -sun2);
				SetLight(f, fill, Hue(Stars.Sun2Color.x, Stars.Sun2Color.y, Stars.Sun2Color.z));
			}
			if (auto* s = rig->Shine.Get())
			{
				float shine = Overridden ? 0.0f : std::max({ ShineColor.x, ShineColor.y, ShineColor.z }) * UE_PI;
				SetFar(s, -ShineFrom);
				SetLight(s, shine, Hue(ShineColor.x, ShineColor.y, ShineColor.z));
			}
			if (auto* sky = rig->Sky.Get())
			{
				// The snapshots (thumbnails, lineup) keep their own framing: no sky colour on them.
				float want = Overridden ? 0.0f : CVarSky.GetValueOnGameThread();
				if (FMath::Abs(sky->Intensity - want) > 1e-4f) sky->SetIntensity(want);
			}
		}

		// ------------------------------------------------------------ eclipse and planetshine

		/// The share of the key star's disc the camera sees past every world (a penumbra between), and the light bounced
		/// off the world that bounces the most onto it.
		void Worlds(UnitySpaceApp& app)
		{
			Eclipse = 1.0f;
			ShineColor = Vector3::zero();
			auto catalog = app.Catalog();
			if (!catalog || !StarsSet || Overridden) return;
			const auto& scenes = app.BodyScenes();
			DVec3 cam = app.SpaceCamera->ScenePosition;
			DVec3 sun(Stars.SunPosition.x, Stars.SunPosition.y, Stars.SunPosition.z);
			double sunDist = sun.Length();
			if (sunDist <= 0) return;
			// The key star: the one drawn where the light comes from.
			int key = -1;
			double best = cs::Double::MaxValue;
			for (int i = 0; i < catalog->Count() && i < scenes.Count(); i++)
			{
				if (!(*catalog)[i]->IsStar()) continue;
				double d = ((scenes[i] - cam) - sun).Length();
				if (d < best)
				{
					best = d;
					key = i;
				}
			}
			double sunR = key >= 0 ? app.BodyDisplayRadius(key) : 0;
			double as = std::asin(std::min(1.0, sunR / sunDist));
			DVec3 sunDir = sun / sunDist;
			float sunPeak = Peak(Stars.SunColor);
			double shineBest = 0;
			for (int i = 0; i < catalog->Count() && i < scenes.Count(); i++)
			{
				const auto& b = (*catalog)[i];
				if (b->IsStar() || !b->IsVisible()) continue;
				double r = app.BodyDisplayRadius(i);
				if (!(r > 0)) continue;
				DVec3 p = scenes[i] - cam;
				double d = p.Length();
				if (d <= r) continue;
				DVec3 pDir = p / d;
				double ap = std::asin(std::min(1.0, r / d));
				// Eclipse: the world in front of the star (nearer, toward it) covering some of its disc.
				if (d < sunDist && DVec3::Dot(pDir, sunDir) > 0)
				{
					double theta = std::acos(std::clamp(DVec3::Dot(pDir, sunDir), -1.0, 1.0));
					double seen = std::clamp((theta - (ap - as)) / std::max(2 * as, 1e-9), 0.0, 1.0);
					if (as > ap) seen = std::max(seen, 1.0 - (ap / as) * (ap / as)); // an annulus at most
					Eclipse = std::min(Eclipse, static_cast<float>(seen));
				}
				// Planetshine: its lit face (how much of it the camera sees), by how big it looms, in its own colour.
				DVec3 toSun = (sun - p).Normalized();
				double phase = 0.5 * (1.0 + DVec3::Dot(toSun, -pDir));
				double looms = (r / d) * (r / d);
				double e = looms * phase * sunPeak;
				if (e > shineBest)
				{
					shineBest = e;
					float gain = static_cast<float>(std::min(e * CVarPlanetshine.GetValueOnGameThread(), 0.45 * sunPeak));
					ShineColor = Vector3(b->Color.R, b->Color.G, b->Color.B) * gain;
					ShineFrom = Vector3(static_cast<float>(p.X), static_cast<float>(p.Y), static_cast<float>(p.Z));
				}
			}
		}

		// ------------------------------------------------------------ shadows fitted to the hulls in view

		void FitShadows(Rig& rig)
		{
			auto* key = rig.Key.Get();
			if (!key) return;
			Camera* screen = UnityCompat::UnityScene::ScreenCamera();
			FVector cam = screen ? US::ToUnreal(screen->transform->position()) : FVector::ZeroVector;
			// The hulls big enough on screen (over about a quarter of a degree) for a shadow worth its texels; the reach is
			// the far side of the biggest-looking one, and of the others only those within a few times that, so a station far
			// off at the game scale never spreads the cascades thin over the fighter in front of the camera.
			struct Seen { double Far, Looks; };
			std::vector<Seen> seen;
			double subject = 0, looks = 0;
			for (const auto& w : UnityCompat::UnityScene::ShadowCasters())
			{
				UPrimitiveComponent* c = w.Get();
				if (!c || !c->CastShadow || !c->IsVisible() || c->bVisibleInSceneCaptureOnly) continue;
				const FBoxSphereBounds& b = c->Bounds;
				double d = (b.Origin - cam).Size();
				double r = b.SphereRadius;
				if (r < d * 0.004) continue;
				double l = r / std::max(d, 1e-9);
				seen.push_back({ d + r, l });
				if (l > looks)
				{
					looks = l;
					subject = d + r;
				}
			}
			double reach = 0;
			int casting = 0;
			for (const auto& s : seen)
				if (s.Far <= subject * 4)
				{
					reach = std::max(reach, s.Far);
					casting++;
				}
			ShadowCasting = casting;
			bool want = casting > 0 && CVarShadows.GetValueOnGameThread() != 0;
			if (key->CastShadows != want) key->SetCastShadows(want);
			if (!want) return;
			float distance = static_cast<float>(std::max(reach * 1.05, 1e-3));
			if (FMath::Abs(distance - ShadowReach) > 0.03f * ShadowReach)
			{
				ShadowReach = distance;
				key->SetDynamicShadowDistanceMovableLight(distance);
			}
		}

		// ------------------------------------------------------------ nav lights

		enum class NavKind { Port, Starboard, Strobe, Stern, Beacon };

		struct NavLight
		{
			cs::Ref<Transform> T;
			cs::Ref<Renderer> R;
			Vector3 Local;
			NavKind Kind;
		};

		struct NavSet
		{
			std::weak_ptr<Transform> Root;
			std::weak_ptr<Ship> Owner;
			std::weak_ptr<Renderer> Hull;
			std::vector<NavLight> Lights;
			Vector3 Nose;
			float Size = 0.02f, Phase = 0;
			bool SmallCraft = false;
		};

		std::vector<NavSet>& NavSets()
		{
			static std::vector<NavSet> sets;
			return sets;
		}

		cs::Ref<Material> NavMaterial(NavKind k)
		{
			static cs::Ref<Material> m[5];
			auto& mat = m[static_cast<int>(k)];
			if (Alive(mat)) return mat;
			static const char* names[] = { "NavPort", "NavStarboard", "NavStrobe", "NavStern", "NavBeacon" };
			static const Color colors[] = { Color(1.0f, 0.1f, 0.06f), Color(0.15f, 1.0f, 0.3f), Color(0.9f, 0.95f, 1.0f), Color(1.0f, 0.95f, 0.85f),
				Color(1.0f, 0.12f, 0.08f) };
			static const float intensity[] = { 4.0f, 4.0f, 7.0f, 2.2f, 4.0f };
			mat = Materials::MarkShared(Materials::Create(Materials::Glow(), names[static_cast<int>(k)]));
			mat->SetColor("_Color", colors[static_cast<int>(k)]);
			mat->SetFloat("_Intensity", intensity[static_cast<int>(k)]);
			mat->SetFloat("_Falloff", 2.4f);
			return mat;
		}

		/// A mesh's most extreme vertices along each of its axes (-x, +x, -y, +y, -z, +z), in its Unreal frame (cm), when the
		/// engine kept its positions on the CPU (editor builds do; a cooked mesh without CPU access has none: no nav lights).
		struct Extremes
		{
			bool Ok = false;
			FVector3f P[6];
		};

		const Extremes& ExtremesOf(const cs::String& asset)
		{
			static std::unordered_map<cs::String, Extremes> known;
			auto it = known.find(asset);
			if (it != known.end()) return it->second;
			Extremes e;
			UStaticMesh* sm = LoadObject<UStaticMesh>(nullptr, UTF8_TO_TCHAR(asset.c_str()), nullptr, LOAD_NoWarn | LOAD_Quiet);
			FStaticMeshRenderData* rd = sm ? sm->GetRenderData() : nullptr;
			if (rd && rd->LODResources.Num() > 0)
			{
				FPositionVertexBuffer& pb = rd->LODResources[0].VertexBuffers.PositionVertexBuffer;
				uint32 n = pb.GetNumVertices();
				if (n > 0 && pb.GetVertexData() != nullptr)
				{
					for (int a = 0; a < 6; a++) e.P[a] = pb.VertexPosition(0);
					for (uint32 i = 1; i < n; i++)
					{
						FVector3f v = pb.VertexPosition(i);
						for (int axis = 0; axis < 3; axis++)
						{
							if (v[axis] < e.P[axis * 2][axis]) e.P[axis * 2] = v;
							if (v[axis] > e.P[axis * 2 + 1][axis]) e.P[axis * 2 + 1] = v;
						}
					}
					e.Ok = true;
				}
			}
			return known.emplace(asset, e).first->second;
		}

		/// A 3x3 inverse (the root's world matrix, to bring hull points into its frame).
		bool Invert(const WorldMatrix& m, float out[3][3])
		{
			const auto& a = m.m;
			float det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
				+ a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
			if (FMath::Abs(det) < 1e-30f) return false;
			float k = 1.0f / det;
			out[0][0] = (a[1][1] * a[2][2] - a[1][2] * a[2][1]) * k;
			out[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * k;
			out[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * k;
			out[1][0] = (a[1][2] * a[2][0] - a[1][0] * a[2][2]) * k;
			out[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * k;
			out[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * k;
			out[2][0] = (a[1][0] * a[2][1] - a[1][1] * a[2][0]) * k;
			out[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * k;
			out[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * k;
			return true;
		}

		Vector3 Mul(const float m[3][3], Vector3 v)
		{
			return Vector3(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
				m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
		}

		bool IsSmallCraft(ShipClass c)
		{
			switch (c)
			{
				case ShipClass::Fighter: case ShipClass::Interceptor: case ShipClass::Bomber: case ShipClass::Recon: case ShipClass::Courier:
				case ShipClass::Gunship: case ShipClass::AssaultShuttle: return true;
				default: return false;
			}
		}

		void UpdateNav(UnitySpaceApp* app)
		{
			auto& sets = NavSets();
			sets.erase(std::remove_if(sets.begin(), sets.end(), [](const NavSet& s)
			{
				auto root = s.Root.lock();
				auto go = root ? root->gameObject() : nullptr;
				return !go || go->destroyed;
			}), sets.end());
			NavShown = 0;
			Camera* cam = Camera::main();
			if (!cam) return;
			Vector3 camPos = cam->transform->position();
			Vector3 camUp = cam->transform->up();
			double ppu = app ? app->PixelsPerUnitAtUnitDistance()
				: Screen::height * 0.5 / std::tan(cam->fieldOfView * 0.5 * Mathf::Deg2Rad);
			float screen = Mathf::Max(static_cast<float>(Screen::height), 240.0f) / 1080.0f;
			float now = Time::unscaledTime;
			bool on = CVarNav.GetValueOnGameThread() != 0;
			for (auto& s : sets)
			{
				auto root = s.Root.lock();
				auto ship = s.Owner.lock();
				auto hull = s.Hull.lock();
				bool alive = on && ship && !ship->Destroyed && hull && hull->enabled && root->gameObject()->activeInHierarchy();
				WorldMatrix m = WorldMatrix::Of(*root);
				float length = Vector3(m.m[0][0], m.m[1][0], m.m[2][0]).magnitude();
				float dist = (m.t - camPos).magnitude();
				float px = dist > 0 ? static_cast<float>(length / dist * ppu) : 0;
				// A hull a few pixels long shows no lights of its own (its icon is the HUD's).
				bool show = alive && length > 0 && px > 10;
				// Strobe: a 70 ms flash every 1.3 s; beacon: a slow red pulse; out of step from ship to ship.
				float t = now + s.Phase;
				bool strobe = Mathf::Repeat(t, 1.3f) < 0.07f && !Overridden;
				float beacon = 0.5f + 0.5f * Mathf::Sin(t * 3.4f);
				for (auto& l : s.Lights)
				{
					bool lit = show && (l.Kind != NavKind::Strobe || strobe) && (l.Kind != NavKind::Beacon || beacon > 0.25f);
					if (l.R->enabled != lit) l.R->enabled = lit;
					if (!lit) continue;
					Vector3 p = m.MultiplyPoint(l.Local);
					Vector3 view = p - camPos;
					float d = view.magnitude();
					if (d < 1e-12f) continue;
					l.T->SetRotation(Quaternion::LookRotation(view / d, camUp));
					// Its true size on the hull, but never under a couple of pixels (a point of light at any range).
					float world = Mathf::Max(s.Size * length, static_cast<float>(3.0 * screen * d / ppu));
					if (l.Kind == NavKind::Beacon) world *= 0.6f + 0.4f * beacon;
					l.T->localScale = Vector3::one() * (world / length);
					NavShown++;
				}
			}
		}

		// ------------------------------------------------------------ lamps: headlights and Embers

		USpotLightComponent* Lamp(Rig& rig, size_t i)
		{
			while (rig.Lamps.size() <= i)
			{
				AActor* host = rig.Host.Get();
				if (!host) return nullptr;
				auto* l = MakeLight<USpotLightComponent>(host, TEXT("HullHeadlight"));
				l->bUseInverseSquaredFalloff = false;
				l->LightFalloffExponent = 2.0f;
				l->IntensityUnits = ELightUnits::Unitless;
				l->SetInnerConeAngle(14.0f);
				l->SetOuterConeAngle(30.0f);
				l->SetLightColor(FLinearColor(1.0f, 0.93f, 0.82f), false);
				l->SetIntensity(0.0f);
				l->SetVisibility(false);
				Register(host, l);
				rig.Lamps.push_back(l);
			}
			return rig.Lamps[i].Get();
		}

		UPointLightComponent* EmberLamp(Rig& rig, size_t i)
		{
			while (rig.Embers.size() <= i)
			{
				AActor* host = rig.Host.Get();
				if (!host) return nullptr;
				auto* l = MakeLight<UPointLightComponent>(host, TEXT("EmberLamp"));
				l->bUseInverseSquaredFalloff = false;
				l->LightFalloffExponent = 2.0f;
				l->IntensityUnits = ELightUnits::Unitless;
				l->SetIntensity(0.0f);
				l->SetVisibility(false);
				Register(host, l);
				rig.Embers.push_back(l);
			}
			return rig.Embers[i].Get();
		}

		void Show(ULocalLightComponent* l, bool on)
		{
			if (l && l->IsVisible() != on) l->SetVisibility(on);
		}

		void UpdateLamps(Rig& rig, UnitySpaceApp* app)
		{
			float gain = Overridden ? 0.0f : CVarLamps.GetValueOnGameThread();
			// Headlights: the small craft nearest the camera, shown, a few at most.
			struct Near { float Dist; Vector3 At, Dir; float Length; };
			std::vector<Near> craft;
			Camera* cam = Camera::main();
			if (cam && gain > 0)
			{
				Vector3 camPos = cam->transform->position();
				for (const auto& s : NavSets())
				{
					if (!s.SmallCraft) continue;
					auto root = s.Root.lock();
					auto ship = s.Owner.lock();
					auto hull = s.Hull.lock();
					if (!root || !ship || ship->Destroyed || !hull || !hull->enabled) continue;
					WorldMatrix m = WorldMatrix::Of(*root);
					float length = Vector3(m.m[0][0], m.m[1][0], m.m[2][0]).magnitude();
					float d = (m.t - camPos).magnitude();
					if (length <= 0 || d > length * 60) continue;
					craft.push_back({ d, m.MultiplyPoint(s.Nose), m.MultiplyVector(Vector3::forward()).normalized(), length });
				}
				std::sort(craft.begin(), craft.end(), [](const Near& a, const Near& b) { return a.Dist < b.Dist; });
			}
			constexpr size_t MaxLamps = 4;
			LampsLit = 0;
			for (size_t i = 0; i < std::max(rig.Lamps.size(), std::min(craft.size(), MaxLamps)); i++)
			{
				bool on = i < craft.size() && i < MaxLamps;
				USpotLightComponent* l = on ? Lamp(rig, i) : rig.Lamps[i].Get();
				if (!l) continue;
				Show(l, on);
				if (!on) continue;
				const auto& c = craft[i];
				l->SetWorldLocationAndRotation(US::ToUnreal(c.At), US::DirectionToUnreal(c.Dir).Rotation());
				float radius = static_cast<float>(c.Length * 25 * US::CmPerUnit);
				if (FMath::Abs(l->AttenuationRadius - radius) > 0.02f * radius) l->SetAttenuationRadius(radius);
				SetLight(l, 6.0f * gain, FLinearColor(1.0f, 0.93f, 0.82f));
				LampsLit++;
			}

			// A Void Station's Embers (US-T150): lamps lighting the station round them, in their own colour.
			std::vector<std::pair<Vector3, Vector4>> embers;
			float reach = 0;
			if (app && gain > 0 && app->ActiveSystem && app->ActiveSystem->IsVoidBase && app->Catalog())
			{
				auto catalog = app->Catalog();
				const auto& scenes = app->BodyScenes();
				DVec3 cam3 = app->SpaceCamera->ScenePosition;
				for (int i = 1; i < catalog->Count() && i < scenes.Count() && embers.size() < 4; i++)
				{
					const auto& b = (*catalog)[i];
					if (!b->IsStar() || b->Gm > 1e-6) continue;
					DVec3 rel = scenes[i] - cam3;
					embers.push_back({ Vector3(static_cast<float>(rel.X), static_cast<float>(rel.Y), static_cast<float>(rel.Z)), UnitySpaceApp::LightColor(b, 1) });
					reach = std::max(reach, static_cast<float>((scenes[i] - scenes[0]).Length()));
				}
			}
			EmbersLit = 0;
			for (size_t i = 0; i < std::max(rig.Embers.size(), embers.size()); i++)
			{
				bool on = i < embers.size();
				UPointLightComponent* l = on ? EmberLamp(rig, i) : rig.Embers[i].Get();
				if (!l) continue;
				Show(l, on);
				if (!on) continue;
				l->SetWorldLocation(US::ToUnreal(embers[i].first));
				float radius = static_cast<float>(reach * 2.4f * US::CmPerUnit);
				if (FMath::Abs(l->AttenuationRadius - radius) > 0.02f * radius) l->SetAttenuationRadius(radius);
				const Vector4& c = embers[i].second;
				SetLight(l, 5.0f * gain, Hue(c.x, c.y, c.z));
				EmbersLit++;
			}
		}
	}

	bool HullLighting::Enabled()
	{
		static int state = -1;
		if (state < 0)
		{
			bool legacy = FParse::Param(FCommandLine::Get(), TEXT("LegacyHullLighting")) || CVarEnabled.GetValueOnGameThread() == 0;
			state = legacy ? 0 : 1;
			UE_LOG(LogSpaceGod, Display, TEXT("[US-T172] Hull lighting: %s"), legacy ? TEXT("legacy self-lit materials") : TEXT("lit PBR under real lights"));
		}
		return state == 1;
	}

	cs::String HullLighting::LitVariant(const cs::String& path, const char* suffix)
	{
		if (!Enabled()) return path;
		static std::unordered_map<cs::String, cs::String> known;
		auto it = known.find(path);
		if (it != known.end()) return it->second;
		cs::String lit = path + suffix;
		auto slash = lit.rfind('/');
		cs::String leaf = slash == cs::String::npos ? lit : lit.substr(slash + 1);
		bool ok = LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("%s.%s"), UTF8_TO_TCHAR(lit.c_str()), UTF8_TO_TCHAR(leaf.c_str())), nullptr,
			LOAD_NoWarn | LOAD_Quiet) != nullptr;
		if (!ok) UE_LOG(LogSpaceGod, Display, TEXT("[US-T172] No lit %s yet (re-run the pack's import): drawn self-lit"), UTF8_TO_TCHAR(lit.c_str()));
		return known.emplace(path, ok ? lit : path).first->second;
	}

	void HullLighting::SetStars(const StarLighting& light, bool overridden)
	{
		if (!Renders() || !Enabled()) return;
		Stars = light;
		StarsSet = true;
		bool was = Overridden;
		Overridden = overridden;
		if (was && !overridden) CaptureSoon = true;
		Apply();
	}

	void HullLighting::SetGrade(const SystemGrade&)
	{
		if (!Renders() || !Enabled()) return;
		// The sky being painted may land a moment later (SystemSkies paints off the game thread): capture now and again.
		CaptureSoon = true;
		NextCapture = FPlatformTime::Seconds() + 2.5;
	}

	void HullLighting::Tick(UnitySpaceApp* app, float)
	{
		Rig* rig = Ensure();
		if (!rig) return;
		if (app && app->SpaceCamera) Worlds(*app);
		Apply();
		FitShadows(*rig);
		UpdateNav(app);
		UpdateLamps(*rig, app);
		double now = FPlatformTime::Seconds();
		if (auto* sky = rig->Sky.Get(); sky && !Overridden && (CaptureSoon || now >= NextCapture))
		{
			// Recaptured on a new place and every few seconds (a sky layer fading in, the jump's tunnel gone): a 64-texel
			// cube of the sky alone, a fraction of a millisecond.
			sky->RecaptureSky();
			CaptureSoon = false;
			NextCapture = now + 8.0;
		}
	}

	void HullLighting::AddNavLights(const cs::Ref<Ship>& ship, const cs::Ref<Transform>& root, const cs::List<cs::Ref<Renderer>>& hull)
	{
		if (!Renders() || !Enabled() || !ship || !root || hull.Count() == 0) return;
		WorldMatrix rm = WorldMatrix::Of(*root);
		float inv[3][3];
		if (!Invert(rm, inv)) return;
		// Every hull piece's extremes in the root's frame (nose +Z, up +Y, starboard +X), then the hull's.
		bool any = false;
		Vector3 lo[3], hi[3];
		for (const auto& r : hull)
		{
			if (!r || !r->transform()) continue;
			auto mesh = r->DrawnMesh();
			if (!mesh || mesh->ImportedAsset.empty()) continue;
			const Extremes& e = ExtremesOf(mesh->ImportedAsset);
			if (!e.Ok) continue;
			WorldMatrix m = WorldMatrix::Of(*r->transform());
			for (const auto& p : e.P)
			{
				// Unreal (x, y, z) cm is Unity (x, z, -y) / 100 (UnrealSpace), then the import's correction.
				Vector3 local = mesh->ImportedRotation * (Vector3(p.X, p.Z, -p.Y) * mesh->ImportedScale);
				Vector3 q = Mul(inv, m.MultiplyPoint(local) - rm.t);
				for (int a = 0; a < 3; a++)
				{
					float v = a == 0 ? q.x : a == 1 ? q.y : q.z;
					if (!any || v < (a == 0 ? lo[a].x : a == 1 ? lo[a].y : lo[a].z)) lo[a] = q;
					if (!any || v > (a == 0 ? hi[a].x : a == 1 ? hi[a].y : hi[a].z)) hi[a] = q;
				}
				any = true;
			}
		}
		if (!any) return;
		static cs::Ref<Mesh> quad;
		if (!Alive(quad)) quad = MeshFactory::Quad();
		NavSet set;
		set.Root = root;
		set.Owner = ship;
		set.Hull = hull[0];
		set.SmallCraft = ship->Spec && IsSmallCraft(ship->Spec->Class);
		// A thumbnail's preview is a bare Ship with no combat state: its class's length then.
		double km = ship->Combat && ship->Combat->Spec ? ship->LengthKm()
			: ship->Spec && CombatSpec::Of(ship->Spec->Class) ? CombatSpec::Of(ship->Spec->Class)->LengthKm : 0.1;
		km = std::max(km, 1e-3);
		set.Size = static_cast<float>(std::clamp(0.0045 / std::sqrt(km), 0.004, 0.035));
		set.Phase = static_cast<float>((ship->Id * 7919) % 1300) / 1000.0f;
		set.Nose = hi[2];
		float out = set.Size * 0.4f;
		auto add = [&](NavKind kind, Vector3 at)
		{
			auto go = NewGameObject("NavLight");
			go->transform->SetParent(root, false);
			go->transform->localPosition = at;
			AddComponent<MeshFilter>(go)->sharedMesh = quad;
			auto mr = AddComponent<MeshRenderer>(go);
			mr->SetSharedMaterial(NavMaterial(kind));
			mr->shadowCastingMode = ShadowCastingMode::Off;
			mr->receiveShadows = false;
			mr->enabled = false;
			set.Lights.push_back({ go->transform, mr, at, kind });
		};
		add(NavKind::Port, lo[0] + Vector3(-out, 0, 0));
		add(NavKind::Starboard, hi[0] + Vector3(out, 0, 0));
		add(NavKind::Strobe, hi[1] + Vector3(0, out, 0));
		add(NavKind::Stern, lo[2] + Vector3(0, 0, -out));
		if (km > 0.15) add(NavKind::Beacon, lo[1] + Vector3(0, -out, 0));
		NavSets().push_back(std::move(set));
	}

	cs::String HullLighting::Describe()
	{
		if (!Enabled()) return "legacy self-lit hulls (no Unreal lights)";
		Rig* rig = Ensure();
		if (!rig) return "no scene";
		auto f = [](float v) { return cs::Fmt(static_cast<double>(v), "0.###"); };
		auto* key = rig->Key.Get();
		auto* fill = rig->Fill.Get();
		auto* shine = rig->Shine.Get();
		auto* sky = rig->Sky.Get();
		return cs::Cat("key ", f(key ? key->Intensity : 0), " lux (eclipse ", f(Eclipse), "), fill ", f(fill ? fill->Intensity : 0), ", planetshine ",
			f(shine ? shine->Intensity : 0), ", sky ", f(sky ? sky->Intensity : 0), Overridden ? " (override)" : "", "; shadows ",
			key && key->CastShadows ? cs::Cat(ShadowCasting, " casters to ", f(ShadowReach), " cm") : cs::String("off"), "; nav lights ", NavShown,
			", headlights ", LampsLit, ", Ember lamps ", EmbersLit);
	}
}
