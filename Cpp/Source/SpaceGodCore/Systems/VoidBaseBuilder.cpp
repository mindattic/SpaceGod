// Port of Assets/UnitySpace/Core/Systems/VoidBaseBuilder.cs (see docs/MIGRATION.md).

#include "Systems/VoidBaseBuilder.h"

#include "Astro.h"
#include "BodyCatalog.h"
#include "BodyDefinition.h"
#include "DVec3.h"
#include "OrbitalElements.h"
#include "Systems/AlphaCentauri.h"

namespace SpaceGod::Core
{
	cs::Ref<BodyCatalog> VoidBaseBuilder::Build(const cs::String& name)
	{
		cs::List<cs::Ref<BodyDefinition>> bodies;
		{
			auto b = cs::New<BodyDefinition>();
			b->Name = name; b->Kind = BodyKind::Star; b->RadiusKm = RadiusKm; b->Gm = Gm; b->Singularity = true;
			b->Status = BodyStatus::Fictional; b->Color = Rgb(0.05f, 0.02f, 0.08f);
			b->Summary = cs::Cat("The artificial gravity/sun generator at the heart of this relay: a \"black hole in a bottle\", ",
				"drawn dark and light-swallowing rather than lit, with a tight violet glow standing in for an ",
				"event horizon. No star, no planet, just the machine -- and the tiny suns it keeps circling it. Fiction.");
			bodies.Add(b);
		}
		for (int i = 0; i < TinySuns; i++)
		{
			double meanAnomalyDeg = 360.0 * i / TinySuns;
			auto b = cs::New<BodyDefinition>();
			b->Name = cs::Cat(name, " Ember ", i + 1); b->Kind = BodyKind::Star; b->ParentName = name;
			b->RadiusKm = TinySunRadiusKm; b->Gm = 1e-9;
			b->Elements = OrbitalElements::FromVectors(Astro::J2000, TinySunOrbitKm, 0, DVec3::UnitX, DVec3::UnitZ, meanAnomalyDeg, TinySunPeriodDays);
			b->TeffK = TinySunTeffK; b->LuminositySolar = 1e-6; b->Color = AlphaCentauri::StarColor(TinySunTeffK);
			b->Status = BodyStatus::Fictional;
			b->Summary = "One of the tiny suns circling the generator, bright and perfectly ordinary against the dark of the thing at their centre. Fiction.";
			bodies.Add(b);
		}
		return cs::New<BodyCatalog>(bodies);
	}
}
