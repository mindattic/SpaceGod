#include "UnityCompat/UnityScene.h"
#include "UnityCompat/ShaderBinding.h"
#include "UnityCompat/UnityParticles.h"
#include "UnrealSpace.h"
#include "UnrealSpaceGodLog.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CoreMisc.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

namespace SpaceGod::UnityCompat
{
	namespace US = SpaceGod::UnrealSpace;
	using UE::Geometry::FDynamicMesh3;
	using UE::Geometry::FIndex3i;

	namespace
	{
		TWeakObjectPtr<AActor> HostActor;
		UnityScene::Stats LastStats, Building;
		int MaterialInstancesMade = 0;
		uint64_t PushedGlobals = 0;
		int64_t Frame = 0;
		/// US-T53: the screen camera's culling mask this Sync, and the renderer of its clear colour (always drawn).
		int ScreenMask = -1;
		const Renderer* BackdropRenderer = nullptr;

		/// A strong UObject reference that is left alone when it is destroyed after the object system (a static
		/// material's instance at exit): releasing it then touches freed engine state.
		template <class T>
		struct ExitSafeStrong
		{
			TStrongObjectPtr<T>* p = new TStrongObjectPtr<T>();
			ExitSafeStrong() = default;
			ExitSafeStrong(const ExitSafeStrong&) = delete;
			ExitSafeStrong& operator=(const ExitSafeStrong&) = delete;
			ExitSafeStrong(ExitSafeStrong&& o) noexcept : p(o.p) { o.p = nullptr; }
			ExitSafeStrong& operator=(ExitSafeStrong&& o) noexcept { std::swap(p, o.p); return *this; }
			~ExitSafeStrong() { if (p && UObjectInitialized() && !GExitPurge) delete p; }
			T* Get() const { return p ? p->Get() : nullptr; }
			T* operator->() const { return Get(); }
			bool IsValid() const { return p && p->IsValid(); }
			void Reset(T* v = nullptr)
			{
				if (!p) p = new TStrongObjectPtr<T>();
				p->Reset(v);
			}
		};

		/// A mesh converted to Unreal's frame and layouts, shared by every renderer drawing it.
		struct MeshHandle final : NativeHandle
		{
			FDynamicMesh3 Mesh;
			int Sections = 0;
		};

		/// A material's instance of its SpaceGodRender material.
		struct MaterialHandle final : NativeHandle
		{
			ExitSafeStrong<UMaterialInstanceDynamic> Mid;
			uint64_t Pushed = 0;
		};

		/// A renderer's component, and its own material instances while it has a property block.
		struct RendererHandle final : NativeHandle
		{
			TWeakObjectPtr<UMeshComponent> Component;
			/// Whether Component is a UStaticMeshComponent (an imported model part) rather than a UDynamicMeshComponent.
			bool Imported = false;
			const Mesh* Drawn = nullptr;
			uint64_t DrawnVersion = 0;
			bool Visible = false;
			std::vector<ExitSafeStrong<UMaterialInstanceDynamic>> BlockMids;
			std::vector<std::pair<const Material*, uint64_t>> BlockPushed;
			uint64_t BlockPushedVersion = 0;
			std::vector<UMaterialInterface*> Bound;

			~RendererHandle() override
			{
				if (!IsEngineExitRequested() && Component.IsValid()) Component->DestroyComponent();
			}
		};

		// ------------------------------------------------------------ mesh conversion

		/// The Unreal local frame of a Unity local vector: the axes permuted, no unit scale (the transform has it).
		FVector3d Local(Vector3 v)
		{
			FVector d = US::DirectionToUnreal(v);
			return FVector3d(d.X, d.Y, d.Z);
		}

		FVector2f Uv(const std::vector<Vector4>& uv, int i, bool flip)
		{
			if (i < 0 || i >= static_cast<int>(uv.size())) return FVector2f(0, flip ? 1.0f : 0.0f);
			return FVector2f(uv[i].x, flip ? 1.0f - uv[i].y : uv[i].y);
		}

		/// Appends vertices with an overlay element each (element id == vertex id), so a triangle names the same ids in
		/// every overlay, and re-appends a triangle's corners as fresh vertices where Unreal's manifold mesh refuses it.
		struct Builder
		{
			FDynamicMesh3& M;
			UE::Geometry::FDynamicMeshUVOverlay* UvLayers[4];
			UE::Geometry::FDynamicMeshNormalOverlay* Normals;
			UE::Geometry::FDynamicMeshColorOverlay* Colors;
			UE::Geometry::FDynamicMeshMaterialAttribute* Materials;

			struct V
			{
				FVector3d P;
				FVector3f N;
				FVector4f C;
				FVector2f Uv[4];
			};
			std::vector<V> Vertices;

			explicit Builder(FDynamicMesh3& m) : M(m)
			{
				M.EnableAttributes();
				M.Attributes()->SetNumUVLayers(4);
				for (int i = 0; i < 4; i++) UvLayers[i] = M.Attributes()->GetUVLayer(i);
				Normals = M.Attributes()->PrimaryNormals();
				M.Attributes()->EnablePrimaryColors();
				Colors = M.Attributes()->PrimaryColors();
				M.Attributes()->EnableMaterialID();
				Materials = M.Attributes()->GetMaterialID();
			}

			int Add(const V& v)
			{
				int id = M.AppendVertex(v.P);
				for (int i = 0; i < 4; i++) UvLayers[i]->AppendElement(v.Uv[i]);
				Normals->AppendElement(v.N);
				Colors->AppendElement(v.C);
				Vertices.push_back(v);
				return id;
			}

			void Tri(int a, int b, int c, int material)
			{
				// Unity's front face is clockwise as seen by the viewer, Unreal's counter-clockwise; the axis mapping is a
				// rotation, so every triangle is re-wound on upload (the C# track's UnrealSpace.FlipWinding). Without it the
				// one-sided bodies and ships showed their back faces (the far hemisphere from inside, lit from behind).
				std::swap(b, c);
				if (a == b || b == c || a == c) return; // degenerate: Unity draws nothing for it either
				int t = M.AppendTriangle(FIndex3i(a, b, c));
				if (t < 0)
				{
					// A non-manifold edge (a face drawn both ways, three faces on an edge): its own corners.
					V va = Vertices[a], vb = Vertices[b], vc = Vertices[c];
					a = Add(va);
					b = Add(vb);
					c = Add(vc);
					t = M.AppendTriangle(FIndex3i(a, b, c));
					if (t < 0) return;
				}
				FIndex3i e(a, b, c);
				for (int i = 0; i < 4; i++) UvLayers[i]->SetTriangle(t, e);
				Normals->SetTriangle(t, e);
				Colors->SetTriangle(t, e);
				Materials->SetValue(t, material);
			}
		};

		void BuildTriangles(Builder& b, const Mesh& mesh, const Mesh::SubMesh& sub, int material, std::vector<int>& map)
		{
			int n = mesh.vertexCount();
			bool hasUv1 = !mesh.Uv[1].empty();
			auto vertex = [&](int i) -> int
			{
				if (map[i] >= 0) return map[i];
				Builder::V v;
				v.P = Local(mesh.Vertices[i]);
				FVector3d nn = i < static_cast<int>(mesh.Normals.size()) ? Local(mesh.Normals[i]) : FVector3d(0, 0, 1);
				v.N = FVector3f(static_cast<float>(nn.X), static_cast<float>(nn.Y), static_cast<float>(nn.Z));
				Color c = i < static_cast<int>(mesh.Colors.size()) ? mesh.Colors[i] : Color::white();
				// Raw, as Unity's mesh colours reach the shader (no colour-space conversion).
				v.C = FVector4f(c.r, c.g, c.b, c.a);
				v.Uv[0] = Uv(mesh.Uv[0], i, !mesh.RawUv0);
				if (hasUv1) v.Uv[1] = Uv(mesh.Uv[1], i, true);
				else if (i < static_cast<int>(mesh.Uv[0].size())) v.Uv[1] = FVector2f(mesh.Uv[0][i].z, mesh.Uv[0][i].w); // Fx's shape slot
				v.Uv[2] = Uv(mesh.Uv[2], i, true);
				v.Uv[3] = Uv(mesh.Uv[3], i, true);
				return map[i] = b.Add(v);
			};
			const auto& idx = sub.Indices;
			auto ok = [&](int i) { return i >= 0 && i < n; };
			if (sub.Topology == MeshTopology::Quads)
			{
				for (size_t i = 0; i + 3 < idx.size(); i += 4)
				{
					if (!ok(idx[i]) || !ok(idx[i + 1]) || !ok(idx[i + 2]) || !ok(idx[i + 3])) continue;
					int a = vertex(idx[i]), bb = vertex(idx[i + 1]), c = vertex(idx[i + 2]), d = vertex(idx[i + 3]);
					b.Tri(a, bb, c, material);
					b.Tri(a, c, d, material);
				}
				return;
			}
			for (size_t i = 0; i + 2 < idx.size(); i += 3)
			{
				if (!ok(idx[i]) || !ok(idx[i + 1]) || !ok(idx[i + 2])) continue;
				// Unity's clockwise front faces are Unreal's too: the axis change is a rotation, so the order stands.
				b.Tri(vertex(idx[i]), vertex(idx[i + 1]), vertex(idx[i + 2]), material);
			}
		}

		void BuildPoints(Builder& b, const Mesh& mesh, const Mesh::SubMesh& sub, int material)
		{
			static const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
			for (int i : sub.Indices)
			{
				if (i < 0 || i >= mesh.vertexCount()) continue;
				Builder::V v;
				v.P = Local(mesh.Vertices[i]);
				FVector3d nn = Local(mesh.Vertices[i].normalized());
				v.N = FVector3f(static_cast<float>(nn.X), static_cast<float>(nn.Y), static_cast<float>(nn.Z));
				Color c = i < static_cast<int>(mesh.Colors.size()) ? mesh.Colors[i] : Color::white();
				v.C = FVector4f(c.r, c.g, c.b, c.a);
				int ids[4];
				for (int k = 0; k < 4; k++)
				{
					v.Uv[0] = FVector2f(corners[k][0], corners[k][1]);
					ids[k] = b.Add(v);
				}
				b.Tri(ids[0], ids[2], ids[1], material);
				b.Tri(ids[0], ids[3], ids[2], material);
			}
		}

		void AddSegment(Builder& b, const Mesh& mesh, int ia, int ib, int material)
		{
			int n = mesh.vertexCount();
			if (ia < 0 || ib < 0 || ia >= n || ib >= n) return;
			FVector3d pa = Local(mesh.Vertices[ia]), pb = Local(mesh.Vertices[ib]);
			auto col = [&](int i) { Color c = i < static_cast<int>(mesh.Colors.size()) ? mesh.Colors[i] : Color::white(); return FVector4f(c.r, c.g, c.b, c.a); };
			FVector3d dir = (pb - pa).GetSafeNormal();
			auto add = [&](const FVector3d& p, const FVector3d& other, int src, float side)
			{
				Builder::V v;
				v.P = p;
				v.N = FVector3f(static_cast<float>(dir.X), static_cast<float>(dir.Y), static_cast<float>(dir.Z));
				v.C = col(src);
				v.Uv[0] = Uv(mesh.Uv[0], src, true);
				v.Uv[1] = FVector2f(static_cast<float>(other.X), static_cast<float>(other.Y));
				v.Uv[2] = FVector2f(static_cast<float>(other.Z), side);
				return b.Add(v);
			};
			int a0 = add(pa, pb, ia, -1), a1 = add(pa, pb, ia, 1), b0 = add(pb, pa, ib, 1), b1 = add(pb, pa, ib, -1);
			// The ribbon's two edges: a vertex at A sees B as its other end, and B sees A, so B's side is mirrored.
			b.Tri(a0, b0, b1, material);
			b.Tri(a0, b1, a1, material);
		}

		void Convert(const Mesh& mesh, MeshHandle& out)
		{
			out.Mesh = FDynamicMesh3();
			Builder b(out.Mesh);
			int material = 0;
			for (const auto& sub : mesh.SubMeshes)
			{
				switch (ShaderBinding::LayoutFor(sub.Topology))
				{
					case MeshLayout::Points: BuildPoints(b, mesh, sub, material); break;
					case MeshLayout::Lines:
						if (sub.Topology == MeshTopology::LineStrip)
							for (size_t i = 0; i + 1 < sub.Indices.size(); i++) AddSegment(b, mesh, sub.Indices[i], sub.Indices[i + 1], material);
						else
							for (size_t i = 0; i + 1 < sub.Indices.size(); i += 2) AddSegment(b, mesh, sub.Indices[i], sub.Indices[i + 1], material);
						break;
					default:
					{
						std::vector<int> map(static_cast<size_t>(mesh.vertexCount()), -1);
						BuildTriangles(b, mesh, sub, material, map);
						break;
					}
				}
				material++;
			}
			out.Sections = FMath::Max(1, material);
			if (mesh.ExplicitBounds)
			{
				// Unity culls by the bounds it was given (the sky, the starfield and the effects meshes give vast ones):
				// two unused vertices at the box's corners make Unreal's bounds the same.
				Bounds bb = mesh.bounds();
				out.Mesh.AppendVertex(Local(bb.min()));
				out.Mesh.AppendVertex(Local(bb.max()));
			}
		}

		MeshHandle* Converted(Mesh& mesh)
		{
			auto* h = static_cast<MeshHandle*>(mesh.Engine.get());
			if (!h)
			{
				mesh.Engine = std::make_unique<MeshHandle>();
				h = static_cast<MeshHandle*>(mesh.Engine.get());
				mesh.EngineVersion = 0;
			}
			if (mesh.EngineVersion != mesh.Version)
			{
				Convert(mesh, *h);
				mesh.EngineVersion = mesh.Version;
			}
			return h;
		}

		// ------------------------------------------------------------ materials

		UMaterialInstanceDynamic* SharedInstance(Material& m)
		{
			auto* h = static_cast<MaterialHandle*>(m.Engine.get());
			if (!h)
			{
				m.Engine = std::make_unique<MaterialHandle>();
				h = static_cast<MaterialHandle*>(m.Engine.get());
				h->Mid.Reset(UMaterialInstanceDynamic::Create(ShaderBinding::Parent(m.shader.get()), GetTransientPackage()));
				MaterialInstancesMade++;
			}
			if (h->Pushed != m.Properties.Version)
			{
				ShaderBinding::Push(m.Properties, h->Mid.Get());
				h->Pushed = m.Properties.Version;
			}
			return h->Mid.Get();
		}

		// ------------------------------------------------------------ transforms

		/// A Unity world matrix as Unreal's transform: Cm * P * M * P^-1 for the axes change P, translation P * t * Cm.
		FTransform ToUnreal(const WorldMatrix& w)
		{
			// The Unity direction along each Unreal axis.
			static const Vector3 axis[3] = {
				ToVector3(US::FromUnreal(FVector(US::CmPerUnit, 0, 0))),
				ToVector3(US::FromUnreal(FVector(0, US::CmPerUnit, 0))),
				ToVector3(US::FromUnreal(FVector(0, 0, US::CmPerUnit))),
			};
			FMatrix m = FMatrix::Identity;
			for (int r = 0; r < 3; r++)
			{
				// Row r of Unreal's (row-vector) matrix: where Unreal's axis r goes.
				FVector image = US::ToUnreal(w.MultiplyVector(axis[r]));
				m.M[r][0] = image.X;
				m.M[r][1] = image.Y;
				m.M[r][2] = image.Z;
			}
			FVector t = US::ToUnreal(w.t);
			m.M[3][0] = t.X;
			m.M[3][1] = t.Y;
			m.M[3][2] = t.Z;
			return FTransform(m);
		}

		template <class T> T* MakeComponent(AActor* host)
		{
			auto* c = NewObject<T>(host, NAME_None, RF_Transient);
			c->SetMobility(EComponentMobility::Movable);
			c->SetCastShadow(false);
			c->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			c->SetGenerateOverlapEvents(false);
			if constexpr (std::is_same_v<T, UDynamicMeshComponent>)
			{
				c->SetEnableRaytracing(false);
				c->SetVertexColorSpaceTransformMode(EDynamicMeshVertexColorTransformMode::NoTransform);
				c->SetTangentsType(EDynamicMeshComponentTangentsMode::NoTangents);
			}
			c->bNeverDistanceCull = true;
			c->SetUsingAbsoluteLocation(true);
			c->SetUsingAbsoluteRotation(true);
			c->SetUsingAbsoluteScale(true);
			c->RegisterComponent();
			host->AddInstanceComponent(c);
			return c;
		}

		bool Shown(const Renderer& r, const cs::Ref<Mesh>& mesh)
		{
			if (!r.enabled || !mesh || mesh->destroyed) return false;
			// Nothing to draw (an idle particle system's mesh, a line not yet given points): as the C# track's
			// UnityBridge.Sync, which wants vertexCount + (imported ? 1 : 0) > 0.
			if (mesh->vertexCount() == 0 && mesh->ImportedAsset.empty()) return false;
			if (r.OnlyFrame >= 0 && r.OnlyFrame != Frame) return false;
			auto go = r.gameObject();
			if (!go || !go->activeInHierarchy()) return false;
			auto m = r.sharedMaterial();
			if (!m || m->destroyed) return false;
			// US-T53: the screen camera draws the layers in its culling mask (the model viewer's sees only its own), and
			// its clear colour (the backdrop), as the C# track's UnityBridge.Sync.
			if (&r != BackdropRenderer && ((ScreenMask >> (go->layer & 31)) & 1) == 0)
			{
				Building.Masked++;
				return false;
			}
			if (&r != BackdropRenderer) Building.VisibleLayers |= 1 << (go->layer & 31);
			return true;
		}

		// ------------------------------------------------------------ lights (US-T57)

		std::vector<cs::Ref<Light>>& Lights();

		/// Unity's Light components as URP's forward renderer applies them, the C# track's UnityLights: in Unity.SpaceGod
		/// only URP Lit materials take lights (the imported glTF craft, M_Lit); the game's own shaders are unlit and light
		/// themselves from the Sun globals, so a Light touches nothing else. Per frame: the enabled directional Lights the
		/// screen camera can see (Light.cullingMask overlapping the camera's), the brightest first (URP's main light;
		/// RenderSettings.sun is never set); a renderer on layer L takes those whose culling mask has L, into M_Lit's
		/// slots: the direction towards the light and Light.color.linear * intensity.
		struct FrameLight
		{
			FLinearColor Direction, Color;
			int Mask = 0;
		};
		std::vector<FrameLight> FrameLights;
		constexpr int LitSlots = 3;
		TMap<TWeakObjectPtr<UMaterialInstanceDynamic>, TArray<FLinearColor>> LitPushed;

		const FName& LitParam(int i)
		{
			static const FName names[LitSlots * 2] = {
				FName(TEXT("_MainLightDirection")), FName(TEXT("_MainLightColor")),
				FName(TEXT("_AdditionalLight0Direction")), FName(TEXT("_AdditionalLight0Color")),
				FName(TEXT("_AdditionalLight1Direction")), FName(TEXT("_AdditionalLight1Color")),
			};
			return names[i];
		}

		void GatherLights(Camera* screen)
		{
			FrameLights.clear();
			auto& lights = Lights();
			lights.erase(std::remove_if(lights.begin(), lights.end(), [](const cs::Ref<Light>& l)
			{
				auto t = l->Parent.lock();
				auto go = t ? t->gameObject() : nullptr;
				return !t || (go && go->destroyed);
			}), lights.end());
			int cameraMask = screen ? screen->cullingMask : -1;
			std::vector<const Light*> seen;
			for (const auto& l : lights)
			{
				if (!l->enabled || l->type != LightType::Directional || l->intensity <= 0 || (l->cullingMask & cameraMask) == 0) continue;
				seen.push_back(l.get());
			}
			// URP's GetMainLightIndex: the brightest directional light (the first of equals) is the main light.
			std::stable_sort(seen.begin(), seen.end(), [](const Light* a, const Light* b) { return a->intensity > b->intensity; });
			for (const Light* l : seen)
			{
				auto t = l->Parent.lock();
				Quaternion rotation = t ? t->rotation() * l->localRotation : l->localRotation;
				// Towards the light: a directional light shines along its transform's forward.
				FVector d = US::DirectionToUnreal(-(rotation * Vector3::forward()));
				Color c = ShaderBinding::Linear(l->color);
				FrameLight f;
				f.Direction = FLinearColor(static_cast<float>(d.X), static_cast<float>(d.Y), static_cast<float>(d.Z), 0);
				f.Color = FLinearColor(c.r * l->intensity, c.g * l->intensity, c.b * l->intensity, 1);
				f.Mask = l->cullingMask;
				FrameLights.push_back(f);
			}
		}

		/// Puts the lights of `layer` into a Lit material instance (only when they changed).
		void ApplyLights(UMaterialInstanceDynamic* mid, int layer)
		{
			if (!mid) return;
			TArray<FLinearColor> v;
			v.Init(FLinearColor(0, 0, 0, 0), LitSlots * 2);
			int slot = 0;
			for (const auto& l : FrameLights)
			{
				if (((l.Mask >> (layer & 31)) & 1) == 0) continue;
				if (slot >= LitSlots) break;
				v[slot * 2] = l.Direction;
				v[slot * 2 + 1] = l.Color;
				slot++;
			}
			if (slot > 0) Building.KeyLit = true;
			if (const TArray<FLinearColor>* old = LitPushed.Find(mid); old && *old == v) return;
			for (int i = 0; i < v.Num(); i++) mid->SetVectorParameterValue(LitParam(i), v[i]);
			LitPushed.Add(mid, v);
		}

		void SyncRenderer(AActor* host, Renderer& r)
		{
			auto mesh = r.DrawnMesh();
			bool visible = Shown(r, mesh);
			auto* h = static_cast<RendererHandle*>(r.Engine.get());
			if (!visible)
			{
				if (h && h->Visible && h->Component.IsValid()) h->Component->SetVisibility(false);
				if (h) h->Visible = false;
				return;
			}
			if (!h)
			{
				r.Engine = std::make_unique<RendererHandle>();
				h = static_cast<RendererHandle*>(r.Engine.get());
			}
			bool imported = !mesh->ImportedAsset.empty();
			if (h->Component.IsValid() && h->Imported != imported)
			{
				h->Component->DestroyComponent();
				h->Component.Reset();
				h->Bound.clear();
				h->Drawn = nullptr;
			}
			if (!h->Component.IsValid())
			{
				if (imported) h->Component = MakeComponent<UStaticMeshComponent>(host);
				else h->Component = MakeComponent<UDynamicMeshComponent>(host);
				h->Imported = imported;
			}
			UMeshComponent* c = h->Component.Get();

			int sections = 1;
			if (imported)
			{
				auto* sm = static_cast<UStaticMeshComponent*>(c);
				if (h->Drawn != mesh.get())
				{
					sm->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, UTF8_TO_TCHAR(mesh->ImportedAsset.c_str())));
					Building.MeshUploads++;
					h->Drawn = mesh.get();
					h->DrawnVersion = mesh->Version;
				}
				sections = FMath::Max(1, sm->GetNumMaterials());
				if (UStaticMesh* drawn = sm->GetStaticMesh(); drawn && drawn->GetRenderData() && drawn->GetRenderData()->LODResources.Num() > 0)
					Building.Triangles += drawn->GetRenderData()->LODResources[0].GetNumTriangles();
			}
			else
			{
				auto* dm = static_cast<UDynamicMeshComponent*>(c);
				auto* converted = Converted(*mesh);
				if (h->Drawn != mesh.get() || h->DrawnVersion != mesh->Version)
				{
					FDynamicMesh3 copy = converted->Mesh;
					dm->SetMesh(MoveTemp(copy));
					Building.MeshUploads++;
					h->Drawn = mesh.get();
					h->DrawnVersion = mesh->Version;
				}
				sections = converted->Sections;
				Building.Triangles += converted->Mesh.TriangleCount();
				if (dm->GetNumMaterials() != sections) dm->SetNumMaterials(sections);
			}

			// Materials: the shared instance of each, or the renderer's own while it has a property block.
			const auto& mats = r.sharedMaterials();
			int slots = FMath::Max(sections, 1);
			bool block = r.HasPropertyBlock() && !r.PropertyBlock().IsEmpty();
			if (block && h->BlockMids.size() != static_cast<size_t>(slots))
			{
				h->BlockMids.resize(static_cast<size_t>(slots));
				h->BlockPushed.assign(static_cast<size_t>(slots), { nullptr, 0 });
			}
			h->Bound.resize(static_cast<size_t>(slots), nullptr);
			for (int i = 0; i < slots; i++)
			{
				const cs::Ref<Material>& m = mats.empty() ? cs::Ref<Material>() : mats[FMath::Min(i, static_cast<int>(mats.size()) - 1)];
				if (!m || m->destroyed) continue;
				UMaterialInterface* bind = nullptr;
				if (block)
				{
					auto& mid = h->BlockMids[i];
					UMaterialInterface* parent = ShaderBinding::Parent(m->shader.get());
					if (!mid.IsValid() || mid->Parent != parent)
					{
						mid.Reset(UMaterialInstanceDynamic::Create(parent, GetTransientPackage()));
						MaterialInstancesMade++;
					}
					auto& pushed = h->BlockPushed[i];
					if (pushed.first != m.get() || pushed.second != m->Properties.Version || h->BlockPushedVersion != r.BlockVersion)
					{
						ShaderBinding::Push(m->Properties, mid.Get());
						ShaderBinding::Push(r.PropertyBlock(), mid.Get());
						pushed = { m.get(), m->Properties.Version };
					}
					bind = mid.Get();
				}
				else bind = SharedInstance(*m);
				if (ShaderBinding::IsLit(m->shader.get())) ApplyLights(Cast<UMaterialInstanceDynamic>(bind), r.gameObject()->layer);
				if (h->Bound[i] != bind)
				{
					c->SetMaterial(i, bind);
					h->Bound[i] = bind;
				}
			}
			if (block) h->BlockPushedVersion = r.BlockVersion;
			if (auto first = r.sharedMaterial()) c->TranslucencySortPriority = ShaderBinding::SortPriority(first->shader.get());

			if (auto t = r.transform())
			{
				FTransform world = ToUnreal(WorldMatrix::Of(*t));
				// An imported part is drawn in its import's frame: corrected to the Unity mesh first.
				c->SetWorldTransform(imported ? ImportedCorrection(mesh->ImportedScale, mesh->ImportedRotation) * world : world);
			}
			if (!h->Visible) c->SetVisibility(true);
			h->Visible = true;
			Building.Visible++;
		}
	}

	// ---------------------------------------------------------------- the screen camera (US-T53)

	namespace
	{
		std::vector<cs::Ref<Light>>& Lights()
		{
			static std::vector<cs::Ref<Light>> lights;
			return lights;
		}

		/// A camera's clear colour (CameraClearFlags.SolidColor, not black, which is what Unreal clears to): a Sky cube
		/// on a 1x1 texture of that colour around the camera, the C# track's UnityBridge.Backdrop.
		struct Backdrop
		{
			cs::Ref<GameObject> go;
			cs::Ref<MeshRenderer> renderer;
			cs::Ref<Material> material;
			Color shown = Color(-1, -1, -1, -1);

			static float Encode(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * Mathf::Pow(c, 1.0f / 2.4f) - 0.055f; }

			void Update(Camera* cam)
			{
				Color c = cam && cam->clearFlags == CameraClearFlags::SolidColor ? cam->backgroundColor : Color::black();
				bool want = HostActor.IsValid() && c.r + c.g + c.b > 0;
				if (!want)
				{
					if (renderer) renderer->enabled = false;
					BackdropRenderer = renderer.get();
					return;
				}
				if (!go || go->destroyed)
				{
					go = NewGameObject("Camera backdrop");
					go->hideFlags = HideFlags::HideAndDontSave;
					std::vector<Vector3> v(8);
					for (int i = 0; i < 8; i++) v[i] = Vector3((i & 1) == 0 ? -1.0f : 1.0f, (i & 2) == 0 ? -1.0f : 1.0f, (i & 4) == 0 ? -1.0f : 1.0f);
					std::vector<int> t{ 0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5 };
					auto mesh = cs::New<Mesh>("Backdrop");
					mesh->hideFlags = HideFlags::DontSave;
					mesh->SetVertices(v);
					mesh->SetTriangles(t, 0);
					mesh->SetBounds(Bounds(Vector3::zero(), Vector3::one() * 1e9f));
					AddComponent<MeshFilter>(go)->sharedMesh = mesh;
					renderer = AddComponent<MeshRenderer>(go);
					material = cs::New<Material>(Shader::Find("Unity.SpaceGod/Sky"), "Backdrop");
					material->renderQueue = 999;
					material->SetFloat("_Brightness", 1.0f);
					material->SetVector("_AxisX", Vector4(1, 0, 0, 0));
					material->SetVector("_AxisY", Vector4(0, 1, 0, 0));
					material->SetVector("_AxisZ", Vector4(0, 0, 1, 0));
					renderer->SetSharedMaterial(material);
					shown = Color(-1, -1, -1, -1);
				}
				go->transform->localPosition = cam->transform->position();
				// Inside the near plane it would be clipped: a cube well beyond it (the Sky material draws it as the sky).
				float s = Mathf::Max(1.0f, cam->nearClipPlane * 10.0f);
				go->transform->localScale = Vector3(s, s, s);
				if (shown.r != c.r || shown.g != c.g || shown.b != c.b)
				{
					shown = c;
					// M_Sky's texel does not reach the screen as Unity's clear colour does: measured on this track
					// (docs/images/cpp-fidelity-model-viewer.png), the screen's linear value is 0.66 of the texel's (the
					// C# track's camera, set up differently, measured a * decoded^g, US-T40). The texel is chosen for
					// the screen to show Unity's value (its gamma colour).
					constexpr float gain = 0.66f;
					auto texel = [](float value)
					{
						float v = Mathf::Max(value, 0.0f);
						float linear = v <= 0.04045f ? v / 12.92f : Mathf::Pow((v + 0.055f) / 1.055f, 2.4f);
						return Encode(Mathf::Min(1.0f, linear / gain));
					};
					auto tex = cs::New<Texture2D>(1, 1);
					tex->name = "Backdrop";
					tex->SetPixel(0, 0, Color(texel(c.r), texel(c.g), texel(c.b), 1));
					tex->Apply();
					material->SetTexture("_MainTex", tex);
				}
				renderer->enabled = true;
				BackdropRenderer = renderer.get();
				Building.Backdrop = true;
			}
		};

		Backdrop TheBackdrop;
	}

	Camera* UnityScene::ScreenCamera()
	{
		Camera* best = nullptr;
		Camera* main = Camera::main();
		for (Camera* c : Camera::allCameras())
		{
			if (!c->isActiveAndEnabled()) continue;
			if (!best || c->depth > best->depth || (c->depth == best->depth && c == main)) best = c;
		}
		return best ? best : main;
	}

	void UnityScene::AddLight(const cs::Ref<Light>& light)
	{
		if (light) Lights().push_back(light);
	}

	Light* UnityScene::KeyLightOf(Camera* camera)
	{
		if (!camera) return nullptr;
		for (const auto& l : Lights())
		{
			if (!l->enabled || l->type != LightType::Directional || (l->cullingMask & camera->cullingMask) == 0) continue;
			auto go = l->Parent.lock() ? l->Parent.lock()->gameObject() : nullptr;
			if (go && go->destroyed) continue;
			for (auto t = l->Parent.lock(); t; t = t->parent.lock())
				if (t == camera->transform) return l.get();
		}
		return nullptr;
	}

	void UnityScene::Attach(AActor* host) { HostActor = host; }

	void UnityScene::Detach()
	{
		SceneRegistry::Get().ForEachRenderer([](const cs::Ref<Renderer>& r) { r->Engine.reset(); });
		HostActor.Reset();
	}

	int64_t UnityScene::CurrentFrame() { return Frame; }

	const UnityScene::Stats& UnityScene::Last() { return LastStats; }

	cs::String UnityScene::Stats::ToString() const
	{
		return cs::Cat(Renderers, " renderers, ", Primitives, " Unreal primitives (", Visible, " visible, ", Triangles, " triangles), ",
			MeshUploads, " mesh uploads, ", MaterialInstances, " material instances");
	}

	AActor* UnityScene::Host() { return HostActor.Get(); }

	UWorld* UnityScene::World() { return HostActor.IsValid() ? HostActor->GetWorld() : nullptr; }

	void UnityScene::BeginFrame(float dt)
	{
		Resources::Pump();
		SceneRegistry::Get().ForEach<ParticleSystem>([dt](const cs::Ref<ParticleSystem>& ps)
		{
			auto go = ps->gameObject();
			if (go && !go->destroyed) ps->Simulate(dt);
		});
	}

	void UnityScene::Sync()
	{
		SyncNow();
		Frame++;
	}

	void UnityScene::SyncNow()
	{
		AActor* host = HostActor.Get();
		if (!host) return;
		// What was destroyed since the last frame leaves the screen now (its component goes with its handle).
		SceneRegistry::Get().Purge([](Renderer& r) { r.Engine.reset(); });
		Building = UnityScene::Stats();
		// US-T53: what the screen shows is what its camera renders (the C# UnityBridge.Sync): the layers in its culling
		// mask over its clear colour.
		Camera* screen = ScreenCamera();
		ScreenMask = screen ? screen->cullingMask : -1;
		TheBackdrop.Update(screen);
		// US-T57: Unity's Light components reach only URP Lit materials (M_Lit), per renderer layer.
		GatherLights(screen);
		SceneRegistry::Get().ForEachRenderer([host](const cs::Ref<Renderer>& r)
		{
			SyncRenderer(host, *r);
			Building.Renderers++;
			if (auto* h = static_cast<RendererHandle*>(r->Engine.get()); h && h->Component.IsValid()) Building.Primitives++;
		});
		Building.MaterialInstances = MaterialInstancesMade;
		// The shader globals are the game's own: Unity's Lights never touch them (US-T57).
		const auto& globals = Shader::Globals();
		if (globals.Version != PushedGlobals)
		{
			ShaderBinding::PushGlobals(host->GetWorld(), globals);
			PushedGlobals = globals.Version;
		}
		LastStats = Building;
	}

	TArray<UPrimitiveComponent*> UnityScene::ComponentsUnder(const cs::Ref<Transform>& root)
	{
		TArray<UPrimitiveComponent*> out;
		for (auto& r : GetComponentsInChildren<Renderer>(root, true))
			if (auto* h = static_cast<RendererHandle*>(r->Engine.get()); h && h->Component.IsValid()) out.Add(h->Component.Get());
		return out;
	}
}
