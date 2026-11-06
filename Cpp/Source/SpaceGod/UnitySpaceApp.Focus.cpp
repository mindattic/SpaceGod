// US-T185: the focus camera's target checks, and the see-through ghost drawn over whatever model hides the focused target.

#include "UnitySpaceApp.h"

#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "Fleet.h"
#include "ShipLod.h"
#include "Ships.h"
#include "Combat/Battle.h"
#include "Camera/SpaceCamera.h"
#include "Rendering/Materials.h"
#include "Rendering/Pipeline/RenderBackend.h"
#include "UnityCompat/UnityRendering.h"
#include "Views/BodyView.h"
#include "Views/ShipView.h"
#include "Views/StructureViews.h"

#include <vector>

namespace SpaceGod
{
	using namespace Core;
	using namespace UnityCompat;

	// ---------------------------------------------------------------- is the target still there

	bool UnitySpaceApp::TargetValid(const Target& target)
	{
		if (target.IsNone()) return false;
		if (target.IsBody()) return target.Body >= 0 && target.Body < bodyScene.Count() && target.Body < bodyRadius.Count();
		if (target.IsShip())
		{
			// Gone through a gate or an FTL drop-out, its Id belongs to another fleet's slot (or none): never read its scene.
			if (target.Ship == nullptr || target.Ship->Destroyed || !Fleet()->IsPresent(target.Ship)) return false;
			return true;
		}
		Core::DVec3 s;
		if (target.IsStructure()) return target.Structure != nullptr && Structures->Scenes.TryGetValue(target.Structure, s);
		if (target.IsRing()) return target.Ring != nullptr && Structures->RingScenes.TryGetValue(target.Ring, s);
		if (target.IsUnit()) return target.Battle != nullptr && target.Unit != nullptr && target.Unit->Present();
		return false;
	}

	// ---------------------------------------------------------------- the occluder ghost

	namespace
	{
		/// One model drawn as the ghost: its renderers and the materials to give back.
		struct Ghosted
		{
			bool IsBody = false;
			int Body = -1;
			cs::Ref<Core::Ship> Ship;
			std::vector<cs::Ref<Renderer>> Renderers;
			std::vector<std::vector<cs::Ref<Material>>> Originals;
			bool Seen = false;
		};

		std::vector<Ghosted>& GhostList()
		{
			static std::vector<Ghosted> list;
			return list;
		}

		cs::Ref<Material>& GhostMaterial()
		{
			static cs::Ref<Material> material;
			return material;
		}

		/// One flat colour for every occluder, translucent and unlit (M_Ghost: opacity is the colour's alpha).
		const cs::Ref<Material>& EnsureGhostMaterial()
		{
			auto& m = GhostMaterial();
			if (!Alive(m))
			{
				m = Rendering::Materials::MarkShared(Rendering::Materials::Create(Rendering::RenderBackend::Active()->LoadShader("Ghost"), "OccluderGhost"));
				// M_Ghost exposes its colour to the scene's lit white, so this is a dark hull's tone, not a bright one: at
				// (0.35, 0.75, 1, 0.3) a single layer outshone the focused hull behind it and the veil read as opaque (US-T219).
				m->SetVector("_GhostColor", Vector4(0.05f, 0.11f, 0.15f, 0.3f));
			}
			return m;
		}

		void Restore(Ghosted& g)
		{
			for (size_t i = 0; i < g.Renderers.size(); i++)
				if (Alive(g.Renderers[i]) && i < g.Originals.size()) g.Renderers[i]->SetSharedMaterials(g.Originals[i]);
		}

		void Apply(Ghosted& g, const cs::List<cs::Ref<Renderer>>& renderers, const cs::Ref<Material>& ghost)
		{
			g.Renderers.clear();
			g.Originals.clear();
			for (const auto& r : renderers)
			{
				if (!Alive(r)) continue;
				g.Renderers.push_back(r);
				g.Originals.push_back(r->sharedMaterials());
				r->SetSharedMaterial(ghost);
			}
		}

		/// A sphere standing between the camera and the target: its near surface is in front of the target's near edge, the
		/// camera is outside it and the target is not inside it.
		bool Hides(Core::DVec3 cam, Core::DVec3 dir, double len, double targetRadius, Core::DVec3 target, Core::DVec3 centre, double radius)
		{
			if (radius <= 0) return false;
			auto oc = cam - centre;
			double c = oc.LengthSquared() - radius * radius;
			if (c < 0) return false; // the camera is inside it
			if ((target - centre).LengthSquared() < radius * radius) return false; // the target sits inside it (docked, landed)
			double b = Core::DVec3::Dot(oc, dir);
			double disc = b * b - c;
			if (disc <= 0) return false;
			double hit = -b - cs::Math::Sqrt(disc);
			return hit > 0 && hit < len - targetRadius;
		}
	}

	void UnitySpaceApp::UpdateOccluderGhost()
	{
		auto& list = GhostList();
		bool watching = SpaceCamera->Mode != Cameras::SpaceCamera::CameraMode::Free && !SpaceCamera->PilotLocked && TargetValid(SpaceCamera->FocusTarget);
		for (auto& g : list) g.Seen = false;

		if (watching)
		{
			const auto focus = SpaceCamera->FocusTarget;
			auto cam = SpaceCamera->ScenePosition;
			auto target = FocusScene(focus);
			auto dir = target - cam;
			double len = dir.Length();
			double targetRadius = TargetRadius(focus);
			if (len > targetRadius && len > 0)
			{
				dir /= len;
				const auto& ghost = EnsureGhostMaterial();

				// Marks a model as hiding the target this frame: new ones are drawn as the ghost, held ones kept, and one whose
				// own view put a material back over the ghost is drawn as the ghost again.
				auto Hold = [&](bool isBody, int body, const cs::Ref<Core::Ship>& ship, const cs::List<cs::Ref<Renderer>>& renderers)
				{
					for (auto& g : list)
					{
						if (g.IsBody != isBody || g.Body != body || g.Ship != ship) continue;
						g.Seen = true;
						for (size_t i = 0; i < g.Renderers.size(); i++)
						{
							auto& r = g.Renderers[i];
							if (Alive(r) && r->sharedMaterial() != ghost)
							{
								g.Originals[i] = r->sharedMaterials();
								r->SetSharedMaterial(ghost);
							}
						}
						return;
					}
					if (renderers.Count() == 0) return;
					Ghosted g;
					g.IsBody = isBody;
					g.Body = body;
					g.Ship = ship;
					g.Seen = true;
					Apply(g, renderers, ghost);
					list.push_back(std::move(g));
				};

				// Bodies: an exact sphere.
				for (int i = 0; i < bodyScene.Count(); i++)
				{
					if (focus.IsBody() && focus.Body == i) continue;
					if (!(*Catalog())[i]->IsVisible() || i >= bodyViews.Count() || !bodyViews[i]) continue;
					if (!Hides(cam, dir, len, targetRadius, target, bodyScene[i], bodyRadius[i])) continue;
					bool held = false;
					for (const auto& g : list) held = held || (g.IsBody && g.Body == i);
					Hold(true, i, nullptr, held ? cs::List<cs::Ref<Renderer>>() : ModelRenderersOf(Target::OfBody(i)));
				}

				// Hulls that are drawn as models (not icons): a sphere round most of the hull, so a long thin ship is not
				// ghosted by a ray passing well off its flank.
				const auto& ships = Fleet()->Ships();
				for (int i = 0; i < ships.Count() && i < shipViews.Count() && i < shipScene.Count(); i++)
				{
					const auto& ship = ships[i];
					if (!ship || ship->Destroyed || !shipViews[i] || !shipViews[i]->Shown()) continue;
					if (focus.IsShip() && focus.Ship == ship) continue;
					double radius = shipLength[i] * 0.5 * ShipLod::ExtentFactor(ship) * 0.8;
					if (!Hides(cam, dir, len, targetRadius, target, shipScene[i], radius)) continue;
					Hold(false, -1, ship, shipViews[i]->HullRenderers());
				}
			}
		}

		// Whatever stopped hiding the target gets its own material back.
		for (size_t i = list.size(); i-- > 0;)
		{
			if (list[i].Seen) continue;
			Restore(list[i]);
			list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
		}
	}
}
