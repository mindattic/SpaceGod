#pragma once
// Port of Assets/UnitySpace/Core/Systems/NearbySocieties.cs (US-T17).

#include <initializer_list>
#include "Cs/Cs.h"
#include "BodyDefinition.h"
#include "Economy/Goods.h"
#include "Economy/Industry.h"
#include "Economy/Settlement.h"
#include "Systems/SocietySpec.h"

namespace SpaceGod::Core
{
	class SystemSpec;

	/// <summary>
	/// The societies of the twelve nearby systems (U.SPC-T90), all game-layer fiction placed on the real
	/// bodies of <see cref="NearbySystems"/>: colonies on the habitable-zone worlds, mines on the hot
	/// rocky ones and in the known belts and discs, skimmers on the giants, and invented stations in orbit
	/// where no world will do (Sirius and the brown dwarfs have none). Each is sized by its distance from
	/// Sol (<see cref="SocietySpec.Frontier"/>), so Barnard's Star is a busy half-Centauri and the twelve
	/// light-year frontier a scatter of outposts. Physical values stay those of the catalogue; the only
	/// numbers invented here are the stations' orbits and the people.
	/// </summary>
	class SPACEGODCORE_API NearbySocieties
	{
	public:
		/// <summary>The society planned for a system, or null if it has none.</summary>
		static cs::Ref<SocietySpec> For(const cs::Ref<SystemSpec>& system);

	private:
		static Rgb C(float r, float g, float b) { return Rgb(r, g, b); }

		// The goods each kind of place digs.
		static const cs::List<Good>& Metals();
		static const cs::List<Good>& Precious();
		static const cs::List<Good>& Volatiles();
		static const cs::List<Good>& Skimmed();

		static cs::List<Good> G(std::initializer_list<Good> goods) { return cs::List<Good>(goods); }

		static cs::List<Good> Join(std::initializer_list<cs::List<Good>> sets);

		static cs::List<Good> With(const cs::List<Good>& set, std::initializer_list<Good> more) { return Join({ set, cs::List<Good>(more) }); }

		static cs::List<IndustryKind> I(std::initializer_list<IndustryKind> k) { return cs::List<IndustryKind>(k); }

		static constexpr IndustryKind Smelter = IndustryKind::Smelter; static constexpr IndustryKind Fab = IndustryKind::ElectronicsFab; static constexpr IndustryKind Works = IndustryKind::MachineWorks; static constexpr IndustryKind Pharma = IndustryKind::PharmaLab;
		static constexpr SettlementKind Colony = SettlementKind::Colony; static constexpr SettlementKind Station = SettlementKind::Station; static constexpr SettlementKind Outpost = SettlementKind::MiningOutpost; static constexpr SettlementKind Haven = SettlementKind::Haven;
		static constexpr SocietyRole Gov = SocietyRole::Authority; static constexpr SocietyRole Co = SocietyRole::Company; static constexpr SocietyRole Raid = SocietyRole::Raiders;

		// ---------------------------------------------------------------- 5.96 ly
		static cs::Ref<SocietySpec> BarnardsStar();
		// ---------------------------------------------------------------- 6.51 ly
		static cs::Ref<SocietySpec> Luhman16();
		// ---------------------------------------------------------------- 7.86 ly
		static cs::Ref<SocietySpec> Wolf359();
		// ---------------------------------------------------------------- 8.30 ly
		static cs::Ref<SocietySpec> Lalande21185();
		// ---------------------------------------------------------------- 8.61 ly
		static cs::Ref<SocietySpec> Sirius();
		// ---------------------------------------------------------------- 10.47 ly
		static cs::Ref<SocietySpec> EpsilonEridani();
		// ---------------------------------------------------------------- 11.01 ly
		static cs::Ref<SocietySpec> Ross128();
		// ---------------------------------------------------------------- 11.87 ly
		static cs::Ref<SocietySpec> EpsilonIndi();
		// ---------------------------------------------------------------- 11.91 ly
		static cs::Ref<SocietySpec> TauCeti();
		// ---------------------------------------------------------------- 11.98 ly
		static cs::Ref<SocietySpec> Gj1061();
		// ---------------------------------------------------------------- 12.35 ly
		static cs::Ref<SocietySpec> LuytensStar();
		// ---------------------------------------------------------------- 12.50 ly
		static cs::Ref<SocietySpec> TeegardensStar();
	};
}
