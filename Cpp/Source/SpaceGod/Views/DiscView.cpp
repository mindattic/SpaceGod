// Port of Unity.SpaceGod Runtime/Views/DiscView.cs (US-T30).
#include "Views/DiscView.h"

#include "UnitySpaceApp.h"
#include "Views/LineStrip.h"

#include "BodyCatalog.h"
#include "ScaleModel.h"
#include "Systems/Galaxy.h"
#include "Systems/StarSystemBuilder.h"

using namespace SpaceGod::Core;

namespace SpaceGod::Views
{
	DiscView::DiscView(UnitySpaceApp* app_, const cs::Ref<Transform>& parent)
		: app(app_)
	{
		for (const auto& d : app->ActiveSystem->Discs)
		{
			auto rings = cs::NewArray<cs::Ref<LineStrip>>(Rings);
			for (int i = 0; i < Rings; i++) rings[i] = cs::New<LineStrip>(cs::Cat(d->Name, " ", i), parent);
			discs.Add(std::make_pair(d, rings));
		}
	}

	void DiscView::Update(bool visible, float fade)
	{
		visible = visible && fade > 0.001f;
		bool relayout = laidOutBlend != app->Scale()->Blend();
		for (const auto& [disc, rings] : discs)
		{
			auto host = (*app->Catalog())[disc->HostIndex];
			auto at = app->Relative(app->BodyScene(disc->HostIndex));
			auto u = Perpendicular(disc->Normal);
			auto v = DVec3::Cross(disc->Normal, u);
			for (int i = 0; i < Rings; i++)
			{
				auto ring = rings[i];
				ring->SetVisible(visible);
				if (!visible) continue;
				if (relayout || ring->Capacity() == 0)
				{
					double radius = disc->InnerKm + (disc->OuterKm - disc->InnerKm) * i / (Rings - 1);
					for (int k = 0; k < Points; k++)
					{
						double a = cs::Math::PI * 2 * k / (Points - 1);
						auto o = app->Scale()->LocalOffset(host, (u * cs::Math::Cos(a) + v * cs::Math::Sin(a)) * radius);
						points[k] = Vector3(static_cast<float>(o.X), static_cast<float>(o.Y), static_cast<float>(o.Z));
					}
					// The edges read a little brighter than the dust between them.
					bool edge = i == 0 || i == Rings - 1;
					auto c = Color(DustColor.r, DustColor.g, DustColor.b, DustColor.a * (edge ? 1.0f : 0.55f));
					ring->SetPoints(points, Points, c);
				}
				// Fainter the further off it is, never gone (US-T134); the camera sits at -at in the ring's frame.
				double size = app->Scale()->LocalDistance(host, disc->OuterKm);
				ring->SetTint(Color(1, 1, 1, fade * UnitySpaceApp::OrbitDistanceFade(ring->NearestDistance(-at), size)));
				ring->Root->transform->localPosition = at;
				ring->Root->transform->localRotation = Quaternion::identity();
			}
		}
		if (visible) laidOutBlend = app->Scale()->Blend();
	}

	DVec3 DiscView::Perpendicular(DVec3 n)
	{
		auto reference = cs::Math::Abs(n.Z) < 0.9 ? DVec3::UnitZ : DVec3::UnitX;
		return DVec3::Cross(reference, n).Normalized();
	}
}
