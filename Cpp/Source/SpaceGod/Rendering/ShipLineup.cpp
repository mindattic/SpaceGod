// US-T110: a debug lineup of every hull class (and of the Star Sparrow kit's parts), as screenshots of the game's own view.
//
//   -SpaceGodShipLineup=<dir>   on the game's command line: once the game is up (the main menu will do), each sheet is
//                               drawn by a camera of its own, screenshotted to <dir>/<sheet>.png, and the game quits
//                               after the last.
//   SpaceGod.ShipLineup <dir>   the same from the console, without quitting.
//
// Sheets: kit-pieces (the 8 modules then fighters 1..40, row by row), classes-<livery> (every hull class in LineupOrder,
// row by row, as its ShipView draws it: hull, turrets and all), detail-small/-large (the navy's, larger), classes-top,
// classes-side and capitals-to-scale. They are screenshots of the game's view rather than an off-screen scene capture:
// the game's materials expose themselves against the view's own exposure (SG_Expose), which a capture does not
// reproduce (it came out black, as ShipThumbnails' does today).
#include "ModelViewer.h"
#include "Rendering/Pipeline/RenderBackend.h"
#include "Rendering/StarSparrowKit.h"
#include "UnityCompat/UnityScene.h"
#include "Views/ShipView.h"
#include "SpaceGodLog.h"

#include "Combat/CombatSpecs.h"
#include "Combat/CombatState.h"
#include "Economy/LocalTraffic.h"
#include "Ships.h"

#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include <functional>

using namespace SpaceGod::Core;

namespace SpaceGod::Rendering
{
	namespace
	{
		struct Entry
		{
			ShipClass Class;
			CivilCraft Civil = CivilCraft::None;
		};

		/// Small to large, then the civil craft.
		const cs::List<Entry>& LineupOrder()
		{
			static const cs::List<Entry> order{
				{ ShipClass::Interceptor }, { ShipClass::Fighter }, { ShipClass::Recon }, { ShipClass::Bomber }, { ShipClass::Gunship },
				{ ShipClass::AssaultShuttle }, { ShipClass::Courier }, { ShipClass::Explorer }, { ShipClass::CaptureTug }, { ShipClass::OreTug },
				{ ShipClass::Miner }, { ShipClass::LaneTender }, { ShipClass::GatePicket }, { ShipClass::Freighter }, { ShipClass::Corvette },
				{ ShipClass::Frigate }, { ShipClass::Destroyer }, { ShipClass::Cruiser }, { ShipClass::CommandCruiser }, { ShipClass::GateBreaker },
				{ ShipClass::Battleship }, { ShipClass::Carrier },
				{ ShipClass::Courier, CivilCraft::Bus }, { ShipClass::Courier, CivilCraft::Taxi }, { ShipClass::Courier, CivilCraft::Skiff },
				{ ShipClass::Courier, CivilCraft::Runabout }, { ShipClass::Courier, CivilCraft::Tourer },
			};
			return order;
		}

		cs::Ref<Faction> MakeFaction(const char* name, const char* prefix, Rgb color, bool military, bool pirate)
		{
			auto f = cs::New<Faction>(name, prefix, color);
			f->IsMilitary = military;
			f->IsPirate = pirate;
			return f;
		}

		cs::Ref<Ship> MakeShip(const Entry& e, const cs::Ref<Faction>& faction, int id)
		{
			auto ship = cs::New<Ship>();
			ship->Id = id;
			ship->Name = cs::Cat(Core::ToString(e.Class), id);
			ship->Spec = ShipClassSpec::Of(e.Class);
			ship->Faction = faction;
			ship->Civil = e.Civil;
			ship->Combat = cs::New<ShipCombat>(CombatSpec::Of(e.Class), cs::Nullable<ShipClass>(e.Class));
			return ship;
		}

		/// One sheet: its items laid out in the sheet's plane (x right, y up, the viewer looking along +z), `Width` x
		/// `Height` across, each turned so the viewer sees it from `View` (the direction of view in the item's own frame).
		struct Sheet
		{
			FString Name;
			float Width = 1, Height = 1;
			std::function<void(const cs::Ref<Transform>& root, cs::List<cs::Ref<Views::ShipView>>& views)> Build;
		};

		/// A grid cell's centre, row by row from the top left.
		Vector3 Cell(int i, int count, int cols, float cell)
		{
			int rows = (count + cols - 1) / cols;
			int c = i % cols, r = i / cols;
			return Vector3((c - (cols - 1) * 0.5f) * cell, -(r - (rows - 1) * 0.5f) * cell, 0);
		}

		/// How an item is turned so the viewer (looking along the sheet's +z) sees it along `view`, its up kept up.
		Quaternion Facing(Vector3 view, Vector3 up = Vector3::up()) { return Quaternion::Inverse(Quaternion::LookRotation(view.normalized(), up)); }

		const Vector3 ThreeQuarter(-0.55f, -0.45f, -0.7f);

		cs::List<Sheet> Sheets()
		{
			cs::List<Sheet> sheets;
			auto grey = MakeFaction("Lineup", "LN", Rgb(0.6f, 0.6f, 0.62f), true, false);
			if (StarSparrowKit::Available())
			{
				auto pieceSheet = [grey](int first, int count, int cols, Vector3 view, Vector3 up)
				{
					return [grey, first, count, cols, view, up](const cs::Ref<Transform>& root, cs::List<cs::Ref<Views::ShipView>>&)
					{
						auto names = StarSparrowKit::PieceNames();
						for (int i = 0; i < count; i++)
						{
							auto holder = NewGameObject(names[first + i]);
							holder->transform->SetParent(root, false);
							holder->transform->localPosition = Cell(i, count, cols, 1.25f);
							holder->transform->localRotation = Facing(view, up);
							StarSparrowKit::Piece(holder->transform, names[first + i], 1.0f, grey);
						}
					};
				};
				Sheet side;
				side.Name = TEXT("kit-modules-side");
				side.Width = 4 * 1.25f;
				side.Height = 2 * 1.25f;
				side.Build = pieceSheet(0, 8, 4, Vector3(-1, 0, 0), Vector3::up());
				sheets.Add(side);
				Sheet top = side;
				top.Name = TEXT("kit-modules-top");
				top.Build = pieceSheet(0, 8, 4, Vector3(0, -1, 0), Vector3::forward());
				sheets.Add(top);
				Sheet fighters;
				fighters.Name = TEXT("kit-fighters");
				fighters.Width = 8 * 1.25f;
				fighters.Height = 5 * 1.25f;
				fighters.Build = pieceSheet(8, 40, 8, ThreeQuarter, Vector3::up());
				sheets.Add(fighters);
			}
			struct Livery
			{
				const TCHAR* Name;
				cs::Ref<Faction> F;
			};
			cs::List<Livery> liveries{
				{ TEXT("sol-navy"), MakeFaction("Sol Federation", "SFS", Rgb(0.35f, 0.65f, 1.0f), true, false) },
				{ TEXT("belt-navy"), MakeFaction("Belt Consortium", "BCS", Rgb(1.0f, 0.6f, 0.25f), true, false) },
				{ TEXT("reach-navy"), MakeFaction("Outer Reach Compact", "ORS", Rgb(0.72f, 0.5f, 1.0f), true, false) },
				{ TEXT("pirate"), MakeFaction("Red Knives", "RK", Rgb(0.85f, 0.2f, 0.25f), false, true) },
				{ TEXT("civil"), MakeFaction("Independent", "MV", Rgb(0.85f, 0.85f, 0.78f), false, false) },
			};
			auto classes = [](const cs::Ref<Faction>& f, Vector3 view, int first = 0, int count = -1, int cols = 7)
			{
				return [f, view, first, count, cols](const cs::Ref<Transform>& root, cs::List<cs::Ref<Views::ShipView>>& views)
				{
					const auto& all = LineupOrder();
					int n = count < 0 ? all.Count() - first : count;
					cs::List<Entry> order;
					for (int i = 0; i < n; i++) order.Add(all[first + i]);
					for (int i = 0; i < order.Count(); i++)
					{
						auto holder = NewGameObject("Cell");
						holder->transform->SetParent(root, false);
						holder->transform->localPosition = Cell(i, order.Count(), cols, 1.3f);
						holder->transform->localRotation = Facing(view, view.y < -0.99f ? Vector3::forward() : Vector3::up());
						auto v = cs::New<Views::ShipView>(MakeShip(order[i], f, i + 1), holder->transform);
						v->Apply(Vector3::zero(), Quaternion::identity(), 1.0f, false, false);
						views.Add(v);
					}
				};
			};
			// The navy's in two halves, larger: small craft and work hulls, then the warships and the civil craft.
			for (int half = 0; half < 2; half++)
			{
				Sheet s;
				s.Name = half == 0 ? TEXT("detail-small") : TEXT("detail-large");
				s.Width = 5 * 1.3f;
				s.Height = 3 * 1.3f;
				s.Build = classes(liveries[0].F, ThreeQuarter, half == 0 ? 0 : 14, half == 0 ? 14 : 13, 5);
				sheets.Add(s);
			}
			for (const auto& l : liveries)
			{
				Sheet s;
				s.Name = FString::Printf(TEXT("classes-%s"), l.Name);
				s.Width = 7 * 1.3f;
				s.Height = 4 * 1.3f;
				s.Build = classes(l.F, ThreeQuarter);
				sheets.Add(s);
			}
			Sheet top;
			top.Name = TEXT("classes-top");
			top.Width = 7 * 1.3f;
			top.Height = 4 * 1.3f;
			top.Build = classes(liveries[0].F, Vector3(0, -1, 0));
			sheets.Add(top);
			Sheet side;
			side.Name = TEXT("classes-side");
			side.Width = 7 * 1.3f;
			side.Height = 4 * 1.3f;
			side.Build = classes(liveries[0].F, Vector3(-1, 0, 0));
			sheets.Add(side);

			// The big hulls side by side at their true lengths (the carrier 3.2 km), seen from the side.
			ShipClass big[] = { ShipClass::Carrier, ShipClass::Battleship, ShipClass::GateBreaker, ShipClass::CommandCruiser, ShipClass::Cruiser,
				ShipClass::Freighter, ShipClass::Destroyer, ShipClass::Frigate, ShipClass::Explorer, ShipClass::Corvette, ShipClass::Miner };
			cs::List<float> lengths;
			float total = 0, gap = 0.12f;
			for (auto c : big)
			{
				float len = static_cast<float>(CombatSpec::DesignsOf(c).Count() > 0 ? CombatSpec::Of(c)->LengthKm : c == ShipClass::Freighter ? 0.6 : 0.2);
				lengths.Add(len);
				total += len + gap;
			}
			Sheet scale;
			scale.Name = TEXT("capitals-to-scale");
			scale.Width = total;
			scale.Height = total * 0.3f;
			auto sol = liveries[0].F;
			std::vector<ShipClass> bigList(std::begin(big), std::end(big));
			scale.Build = [sol, bigList, lengths, total, gap](const cs::Ref<Transform>& root, cs::List<cs::Ref<Views::ShipView>>& views)
			{
				float x = -total * 0.5f;
				for (int i = 0; i < static_cast<int>(bigList.size()); i++)
				{
					float len = lengths[i];
					auto holder = NewGameObject("Cell");
					holder->transform->SetParent(root, false);
					holder->transform->localPosition = Vector3(x + len * 0.5f + gap * 0.5f, 0, 0);
					// Nose to the right, seen a little from above.
					holder->transform->localRotation = Facing(Vector3(-1, -0.35f, 0));
					auto v = cs::New<Views::ShipView>(MakeShip({ bigList[i] }, sol, i + 1), holder->transform);
					v->Apply(Vector3::zero(), Quaternion::identity(), len, false, false);
					views.Add(v);
					x += len + gap;
				}
			};
			sheets.Add(scale);
			return sheets;
		}

		/// Steps through the sheets a few frames each: build, let it stream in, screenshot, tear down. Drawn as the model
		/// viewer draws (ModelViewer.cpp): by a camera of its own, over everything (depth 1000), that sees only the
		/// lineup's layer against a plain backdrop, the game's own cameras off meanwhile.
		class Lineup
		{
		public:
			FString Dir;
			bool Quit = false;

			void Start()
			{
				IFileManager::Get().MakeDirectory(*Dir, true);
				sheets = Sheets();
				index = 0;
				frame = 0;
				handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &Lineup::Tick));
			}

		private:
			cs::List<Sheet> sheets;
			int index = 0, frame = 0;
			cs::Ref<GameObject> root, cameraObject;
			Camera* camera = nullptr;
			cs::List<Camera*> disabled;
			cs::List<cs::Ref<Views::ShipView>> views;
			FTSTicker::FDelegateHandle handle;

			static int Layer()
			{
				static const int layer = ModelViewerBackend::NameToLayer("ShipLineup");
				return layer;
			}

			static void SetLayer(const cs::Ref<GameObject>& go)
			{
				ModelViewerBackend::SetLayer(go, Layer());
				for (const auto& child : go->transform->children) SetLayer(child->gameObject());
			}

			void EnsureCamera()
			{
				if (camera) return;
				cameraObject = NewGameObject("ShipLineupCamera");
				camera = ModelViewerBackend::AddCamera(cameraObject);
				RenderBackend::Active()->SetupCamera(camera, CameraRole::Overlay);
				camera->clearFlags = CameraClearFlags::SolidColor;
				camera->backgroundColor = Color(0.045f, 0.05f, 0.07f);
				camera->fieldOfView = 24.0f;
				ModelViewerBackend::SetDepth(camera, 1000);
				ModelViewerBackend::SetCullingMask(camera, 1 << Layer());
			}

			void Place()
			{
				EnsureCamera();
				for (Camera* cam : Camera::allCameras())
				{
					if (cam == camera || !cam->enabled) continue;
					cam->enabled = false;
					if (!disabled.Contains(cam)) disabled.Add(cam);
				}
				camera->enabled = true;
				const auto& sheet = sheets[index];
				float tanHalf = Mathf::Tan(camera->fieldOfView * 0.5f * Mathf::Deg2Rad);
				float aspect = 16.0f / 9.0f; // the lineup runs in a 16:9 window
				float distance = Mathf::Max(sheet.Height * 0.5f / (tanHalf * 0.94f), sheet.Width * 0.5f / (tanHalf * aspect * 0.96f));
				camera->nearClipPlane = distance * 0.2f;
				camera->farClipPlane = distance * 4.0f;
				cameraObject->transform->localPosition = Vector3(0, 0, -distance);
				cameraObject->transform->localRotation = Quaternion::identity();
				root->transform->localPosition = Vector3::zero();
				// Lit from over the viewer's right shoulder.
				StarLighting light;
				Vector3 key = Vector3(1.2f, 1.4f, -1.0f).normalized() * (distance * 1000.0f);
				light.SunPosition = Vector4(key.x, key.y, key.z, 0);
				light.SunColor = Vector4(1.3f, 1.27f, 1.22f, 1.0f);
				light.Sun2Position = light.SunPosition;
				light.Sun2Color = Vector4::zero();
				RenderBackend::Active()->SetLighting(light);
			}

			bool Tick(float)
			{
				if (index >= sheets.Count())
				{
					if (camera) camera->enabled = false;
					for (Camera* cam : disabled) if (cam) cam->enabled = true;
					disabled.Clear();
					UE_LOG(LogSpaceGod, Display, TEXT("ShipLineup: done (%s)"), *Dir);
					if (Quit) FPlatformMisc::RequestExit(false);
					return false;
				}
				if (frame == 0)
				{
					root = NewGameObject("ShipLineup");
					views.Clear();
					sheets[index].Build(root->transform, views);
					SetLayer(root);
				}
				Place();
				frame++;
				if (frame == 45)
				{
					FString file = Dir / (sheets[index].Name + TEXT(".png"));
					FScreenshotRequest::RequestScreenshot(file, false, false);
					UE_LOG(LogSpaceGod, Display, TEXT("ShipLineup: %s"), *file);
				}
				if (frame >= 60)
				{
					Destroy(root);
					root = nullptr;
					views.Clear();
					index++;
					frame = 0;
				}
				return true;
			}
		};

		Lineup& TheLineup()
		{
			static Lineup lineup;
			return lineup;
		}

		FAutoConsoleCommand LineupCommand(TEXT("SpaceGod.ShipLineup"), TEXT("Screenshots every hull class (and the Star Sparrow kit's parts) to PNGs in the given folder (US-T110)."),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& args)
			{
				auto& l = TheLineup();
				l.Dir = args.Num() > 0 ? args[0] : FPaths::ProjectSavedDir() / TEXT("ShipLineup");
				l.Quit = false;
				l.Start();
			}));

		struct CommandLineLineup
		{
			FTSTicker::FDelegateHandle Handle;
			FString Dir;
			double Since = -1;

			CommandLineLineup()
			{
				if (!FParse::Value(FCommandLine::Get(), TEXT("SpaceGodShipLineup="), Dir)) return;
				Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
				{
					// Once the game is running (its frames flowing) and has had a few seconds to settle.
					if (!UnityCompat::UnityScene::Host()) return true;
					double now = FPlatformTime::Seconds();
					if (Since < 0 && GFrameCounter > 300) Since = now;
					if (Since < 0 || now - Since < 8.0) return true;
					auto& l = TheLineup();
					l.Dir = Dir;
					l.Quit = true;
					l.Start();
					return false;
				}));
			}
		} commandLineLineup;
	}
}
