// Not a C# file (US-T41): the rest of C#'s static-object walk (docs/PERSISTENCE.md, "Statics"), so the C++ build names
// every object a static reaches exactly as the C# build does. Statics.cpp (US-T36) declared the statics whose type holds a
// class a save reaches; C#'s walk also goes through every other static of every Core type, and the first path that
// reaches an object names it, so a table walked earlier can name a saved object first (HullFrames::All names the orbital
// station's design "HullFrames::All[23].Design", not "HullFrames::OrbitalStation"). Here: every other static the C# walk
// names anything through, and a walk registration for each class it names there (never in a save themselves: only the
// fields through which the C# walk reaches further names are listed). Left out, as in no save: settings (their names
// follow the order the process happened to register them in), compiler closure caches (`+<>c::<>9`), ObjectGraph's
// own tables, locks, and LunarTheory's coefficient tables (constexpr in C++: no object to name). VoidStations' and
// RenderLod's file-local tables are declared in their own .cpp. Generated from the C# walk (Unity.SpaceGod.Core.dll after
// Galaxy.CreateDefault); regenerate rather than hand-edit.

#include "AssetLibraryCandidates.h"
#include "Combat/AlphaStrike.h"
#include "Combat/BulkFreighters.h"
#include "Combat/Corvettes.h"
#include "Combat/Dogfighting.h"
#include "Combat/EnergyWeapons.h"
#include "Combat/Facilities.h"
#include "Combat/FleetPatterns.h"
#include "Combat/FlightDecks.h"
#include "Combat/FlightModels.h"
#include "Combat/GateBreakers.h"
#include "Combat/HaulerFlight.h"
#include "Combat/Outfitting.h"
#include "Combat/PatrolWings.h"
#include "Combat/PilotTuning.h"
#include "Combat/RearLine.h"
#include "Combat/SpinalMounts.h"
#include "Combat/TachyonLances.h"
#include "ControlsHelp.h"
#include "DisplaySettings.h"
#include "Economy/CargoHold.h"
#include "Economy/Goods.h"
#include "Economy/LocalTraffic.h"
#include "Economy/People.h"
#include "Economy/Propulsion.h"
#include "Economy/TradeDesk.h"
#include "Events/EventSources.h"
#include "GameManual.h"
#include "GasGiantWeather.h"
#include "RenderLod.h"
#include "Rendering/ProceduralSky.h"
#include "Rendering/RenderPipelineProfiles.h"
#include "Rendering/Skyboxes.h"
#include "Sensors/Sensors.h"
#include "Settings/GameSetting.h"
#include "Systems/CatalogueStars.h"
#include "Systems/CruiseDrive.h"
#include "Systems/DeepSpace.h"
#include "Systems/DeepSpaceLife.h"
#include "Systems/FleetDoctrine.h"
#include "Systems/HelmFtl.h"
#include "Systems/NearbySocieties.h"
#include "Systems/ProceduralSystems.h"
#include "Systems/StarDiscovery.h"
#include "Systems/SystemSources.h"
#include "Systems/VoidStations.h"
#include "Persistence/Registrations/Registrations.h"

namespace SpaceGod::Core
{
	namespace
	{
		// A private table's address without a friend declaration in the ported header (which would rebuild every file that
		// includes it): the names in an explicit instantiation are not access-checked ([temp.explicit]), and the friend
		// function the instantiation defines hands the address on.
		template <auto Member, class Tag> struct Expose { friend constexpr auto Address(Tag) { return Member; } };
		struct P0 {}; constexpr auto Address(P0);
		template struct Expose<&AlphaStrike::classes, P0>;
		struct P1 {}; constexpr auto Address(P1);
		template struct Expose<&BulkFreighters::trusses, P1>;
		struct P2 {}; constexpr auto Address(P2);
		template struct Expose<&CapitalHandling::ByClass, P2>;
		struct P3 {}; constexpr auto Address(P3);
		template struct Expose<&CargoFootprint::ByGood, P3>;
		struct P4 {}; constexpr auto Address(P4);
		template struct Expose<&CargoHoldSpec::ByClass, P4>;
		struct P5 {}; constexpr auto Address(P5);
		template struct Expose<&CatalogueStars::aliases, P5>;
		struct P6 {}; constexpr auto Address(P6);
		template struct Expose<&CatalogueStars::stars, P6>;
		struct P7 {}; constexpr auto Address(P7);
		template struct Expose<&Corvettes::Names, P7>;
		struct P8 {}; constexpr auto Address(P8);
		template struct Expose<&DeepSpaceFeatures::StaticConstructor, P8>;
		struct P9 {}; constexpr auto Address(P9);
		template struct Expose<&DeepSpaceFeatures::Greek, P9>;
		struct P10 {}; constexpr auto Address(P10);
		template struct Expose<&DeepSpaceFeatures::Hulls, P10>;
		struct P11 {}; constexpr auto Address(P11);
		template struct Expose<&DeepSpaceFeatures::Ices, P11>;
		struct P12 {}; constexpr auto Address(P12);
		template struct Expose<&DeepSpaceFeatures::Veils, P12>;
		struct P13 {}; constexpr auto Address(P13);
		template struct Expose<&DeepSpaceFeatures::Wanderers, P13>;
		struct P14 {}; constexpr auto Address(P14);
		template struct Expose<&DeepSpaceFeatures::specs, P14>;
		struct P15 {}; constexpr auto Address(P15);
		template struct Expose<&DisplaySettings::AspectValues, P15>;
		struct P16 {}; constexpr auto Address(P16);
		template struct Expose<&DisplaySettings::StandardSizes, P16>;
		struct P17 {}; constexpr auto Address(P17);
		template struct Expose<&EnergyWeapons::Roles, P17>;
		struct P18 {}; constexpr auto Address(P18);
		template struct Expose<&FacilityLayouts::Barred, P18>;
		struct P19 {}; constexpr auto Address(P19);
		template struct Expose<&FleetDoctrine::flanks, P19>;
		struct P20 {}; constexpr auto Address(P20);
		template struct Expose<&FleetPatterns::StaticConstructor, P20>;
		struct P21 {}; constexpr auto Address(P21);
		template struct Expose<&FleetPatterns::doctrine, P21>;
		struct P22 {}; constexpr auto Address(P22);
		template struct Expose<&FleetPatterns::patterns, P22>;
		struct P23 {}; constexpr auto Address(P23);
		template struct Expose<&FleetPatterns::raiders, P23>;
		struct P24 {}; constexpr auto Address(P24);
		template struct Expose<&FtlCarriage::berths, P24>;
		struct P25 {}; constexpr auto Address(P25);
		template struct Expose<&FtlCarriage::clampSizes, P25>;
		struct P26 {}; constexpr auto Address(P26);
		template struct Expose<&FtlCarriage::clamps, P26>;
		struct P27 {}; constexpr auto Address(P27);
		template struct Expose<&FtlCarriage::sizes, P27>;
		struct P28 {}; constexpr auto Address(P28);
		template struct Expose<&FtlRoster::byClass, P28>;
		struct P29 {}; constexpr auto Address(P29);
		template struct Expose<&GasGiantWeather::Jupiter, P29>;
		struct P30 {}; constexpr auto Address(P30);
		template struct Expose<&GasGiantWeather::Neptune, P30>;
		struct P31 {}; constexpr auto Address(P31);
		template struct Expose<&GasGiantWeather::Saturn, P31>;
		struct P32 {}; constexpr auto Address(P32);
		template struct Expose<&GasGiantWeather::Uranus, P32>;
		struct P33 {}; constexpr auto Address(P33);
		template struct Expose<&GateBreakers::classes, P33>;
		struct P34 {}; constexpr auto Address(P34);
		template struct Expose<&GoodInfo::All, P34>;
		struct P35 {}; constexpr auto Address(P35);
		template struct Expose<&HandBuiltSystems::stars, P35>;
		struct P36 {}; constexpr auto Address(P36);
		template struct Expose<&LocalTraffic::PersonalKinds, P36>;
		struct P37 {}; constexpr auto Address(P37);
		template struct Expose<&LocalTraffic::PersonalNames, P37>;
		struct P38 {}; constexpr auto Address(P38);
		template struct Expose<&ModuleSpec::All, P38>;
		struct P39 {}; constexpr auto Address(P39);
		template struct Expose<&NearbySocieties::Metals, P39>;
		struct P40 {}; constexpr auto Address(P40);
		template struct Expose<&NearbySocieties::Precious, P40>;
		struct P41 {}; constexpr auto Address(P41);
		template struct Expose<&NearbySocieties::Skimmed, P41>;
		struct P42 {}; constexpr auto Address(P42);
		template struct Expose<&NearbySocieties::Volatiles, P42>;
		struct P43 {}; constexpr auto Address(P43);
		template struct Expose<&PatrolWings::CallSigns, P43>;
		struct P44 {}; constexpr auto Address(P44);
		template struct Expose<&PatrolWings::Compositions, P44>;
		struct P45 {}; constexpr auto Address(P45);
		template struct Expose<&PatrolWings::ConvoyEscorts, P45>;
		struct P46 {}; constexpr auto Address(P46);
		template struct Expose<&PatrolWings::ConvoyNames, P46>;
		struct P47 {}; constexpr auto Address(P47);
		template struct Expose<&PatrolWings::Fittings, P47>;
		struct P48 {}; constexpr auto Address(P48);
		template struct Expose<&PatrolWings::Wingmen, P48>;
		struct P49 {}; constexpr auto Address(P49);
		template struct Expose<&People::needs, P49>;
		struct P50 {}; constexpr auto Address(P50);
		template struct Expose<&PilotTuning::ByClass, P50>;
		struct P51 {}; constexpr auto Address(P51);
		template struct Expose<&PirateCaptains::Epithet, P51>;
		struct P52 {}; constexpr auto Address(P52);
		template struct Expose<&PirateCaptains::First, P52>;
		struct P53 {}; constexpr auto Address(P53);
		template struct Expose<&PirateCaptains::Last, P53>;
		struct P54 {}; constexpr auto Address(P54);
		template struct Expose<&ProceduralSky::P, P54>;
		struct P55 {}; constexpr auto Address(P55);
		template struct Expose<&ProceduralSystems::Classes, P55>;
		struct P56 {}; constexpr auto Address(P56);
		template struct Expose<&RearLine::classes, P56>;
		struct P57 {}; constexpr auto Address(P57);
		template struct Expose<&RenderPipelineProfiles::registered, P57>;
		struct P58 {}; constexpr auto Address(P58);
		template struct Expose<&SensorSuite::baseByClass, P58>;
		struct P59 {}; constexpr auto Address(P59);
		template struct Expose<&SkyboxCatalog::all, P59>;
		struct P60 {}; constexpr auto Address(P60);
		template struct Expose<&SpinalMounts::kinds, P60>;
		struct P61 {}; constexpr auto Address(P61);
		template struct Expose<&SystemSources::MainSequence, P61>;
		struct P62 {}; constexpr auto Address(P62);
		template struct Expose<&SystemSources::sources, P62>;
		struct P63 {}; constexpr auto Address(P63);
		template struct Expose<&WeakPoints::Specs, P63>;
		struct P64 {}; constexpr auto Address(P64);
		template struct Expose<&WeaponGroups::ByKind, P64>;
		struct P65 {}; constexpr auto Address(P65);
		template struct Expose<&FlightModelRegistry::classModels, P65>;
		struct P66 {}; constexpr auto Address(P66);
		template struct Expose<&FlightModelRegistry::families, P66>;
		struct P67 {}; constexpr auto Address(P67);
		template struct Expose<&FlightModelRegistry::models, P67>;

		decltype(auto) S0() { return (Address(P0{})()); } // Unity.SpaceGod.Core.AlphaStrike::classes (1 name)
		decltype(auto) S1() { return (AssetLibraryCandidates::All()); } // Unity.SpaceGod.Core.AssetLibraryCandidates::All (6 names)
		decltype(auto) S2() { return (Address(P1{})()); } // Unity.SpaceGod.Core.BulkFreighters::trusses (1 name)
		decltype(auto) S3() { return (Address(P2{})()); } // Unity.SpaceGod.Core.CapitalHandling::ByClass (1 name)
		decltype(auto) S4() { return (Address(P3{})()); } // Unity.SpaceGod.Core.CargoFootprint::ByGood (1 name)
		decltype(auto) S5() { return (Address(P4{})()); } // Unity.SpaceGod.Core.CargoHoldSpec::ByClass (1 name)
		decltype(auto) S6() { return (Address(P5{})()); } // Unity.SpaceGod.Core.CatalogueStars::aliases (1 name)
		decltype(auto) S7() { return (Address(P6{})()); } // Unity.SpaceGod.Core.CatalogueStars::stars (123 names)
		decltype(auto) S8() { return (ControlsHelp::Sections()); } // Unity.SpaceGod.Core.ControlsHelp::Sections (15 names)
		decltype(auto) S9() { return (Address(P7{})()); } // Unity.SpaceGod.Core.Corvettes::Names (1 name)
		decltype(auto) S10() { return (DeepSpace::EncounterKinds()); } // Unity.SpaceGod.Core.DeepSpace::EncounterKinds (1 name)
		decltype(auto) S11() { Address(P8{})(); return (Address(P9{})()); } // Unity.SpaceGod.Core.DeepSpaceFeatures::Greek (1 name)
		decltype(auto) S12() { Address(P8{})(); return (Address(P10{})()); } // Unity.SpaceGod.Core.DeepSpaceFeatures::Hulls (1 name)
		decltype(auto) S13() { Address(P8{})(); return (Address(P11{})()); } // Unity.SpaceGod.Core.DeepSpaceFeatures::Ices (1 name)
		decltype(auto) S14() { Address(P8{})(); return (Address(P12{})()); } // Unity.SpaceGod.Core.DeepSpaceFeatures::Veils (1 name)
		decltype(auto) S15() { Address(P8{})(); return (Address(P13{})()); } // Unity.SpaceGod.Core.DeepSpaceFeatures::Wanderers (1 name)
		decltype(auto) S16() { Address(P8{})(); return (Address(P14{})()); } // Unity.SpaceGod.Core.DeepSpaceFeatures::specs (9 names)
		decltype(auto) S17() { return (Address(P15{})()); } // Unity.SpaceGod.Core.DisplaySettings::AspectValues (1 name)
		decltype(auto) S18() { return (DisplaySettings::Aspects()); } // Unity.SpaceGod.Core.DisplaySettings::Aspects (1 name)
		decltype(auto) S19() { return (Address(P16{})()); } // Unity.SpaceGod.Core.DisplaySettings::StandardSizes (1 name)
		decltype(auto) S20() { return (Address(P17{})()); } // Unity.SpaceGod.Core.EnergyWeapons::Roles (3 names)
		decltype(auto) S21() { return (Address(P18{})()); } // Unity.SpaceGod.Core.FacilityLayouts::Barred (2 names)
		decltype(auto) S22() { return (Address(P19{})()); } // Unity.SpaceGod.Core.FleetDoctrine::flanks (1 name)
		decltype(auto) S23() { Address(P20{})(); return (Address(P21{})()); } // Unity.SpaceGod.Core.FleetPatterns::doctrine (8 names)
		decltype(auto) S24() { Address(P20{})(); return (Address(P22{})()); } // Unity.SpaceGod.Core.FleetPatterns::patterns (5 names)
		decltype(auto) S25() { Address(P20{})(); return (Address(P23{})()); } // Unity.SpaceGod.Core.FleetPatterns::raiders (1 name)
		decltype(auto) S26() { return (FlightDecks::Lod()); } // Unity.SpaceGod.Core.FlightDecks::Lod (1 name)
		decltype(auto) S27() { return (FlightModelRegistry::Default()); } // Unity.SpaceGod.Core.FlightModelRegistry::<Default>k__BackingField (19 names)
		decltype(auto) S28() { return (Address(P24{})()); } // Unity.SpaceGod.Core.FtlCarriage::berths (1 name)
		decltype(auto) S29() { return (Address(P25{})()); } // Unity.SpaceGod.Core.FtlCarriage::clampSizes (1 name)
		decltype(auto) S30() { return (Address(P26{})()); } // Unity.SpaceGod.Core.FtlCarriage::clamps (1 name)
		decltype(auto) S31() { return (Address(P27{})()); } // Unity.SpaceGod.Core.FtlCarriage::sizes (1 name)
		decltype(auto) S32() { return (Address(P28{})()); } // Unity.SpaceGod.Core.FtlRoster::byClass (1 name)
		decltype(auto) S33() { return (FtlTargets::Sources()); } // Unity.SpaceGod.Core.FtlTargets::Sources (1 name)
		decltype(auto) S34() { return (GameManual::Chapters()); } // Unity.SpaceGod.Core.GameManual::Chapters (40 names)
		decltype(auto) S35() { return (Address(P29{})()); } // Unity.SpaceGod.Core.GasGiantWeather::Jupiter (1 name)
		decltype(auto) S36() { return (Address(P30{})()); } // Unity.SpaceGod.Core.GasGiantWeather::Neptune (1 name)
		decltype(auto) S37() { return (Address(P31{})()); } // Unity.SpaceGod.Core.GasGiantWeather::Saturn (1 name)
		decltype(auto) S38() { return (Address(P32{})()); } // Unity.SpaceGod.Core.GasGiantWeather::Uranus (1 name)
		decltype(auto) S39() { return (Address(P33{})()); } // Unity.SpaceGod.Core.GateBreakers::classes (1 name)
		decltype(auto) S40() { return (Address(P34{})()); } // Unity.SpaceGod.Core.GoodInfo::All (17 names)
		decltype(auto) S41() { return (Address(P35{})()); } // Unity.SpaceGod.Core.HandBuiltSystems::stars (24 names)
		decltype(auto) S42() { return (Address(P36{})()); } // Unity.SpaceGod.Core.LocalTraffic::PersonalKinds (1 name)
		decltype(auto) S43() { return (Address(P37{})()); } // Unity.SpaceGod.Core.LocalTraffic::PersonalNames (1 name)
		decltype(auto) S44() { return (Address(P38{})()); } // Unity.SpaceGod.Core.ModuleSpec::All (49 names)
		decltype(auto) S45() { return (Address(P39{})()); } // Unity.SpaceGod.Core.NearbySocieties::Metals (1 name)
		decltype(auto) S46() { return (Address(P40{})()); } // Unity.SpaceGod.Core.NearbySocieties::Precious (1 name)
		decltype(auto) S47() { return (Address(P41{})()); } // Unity.SpaceGod.Core.NearbySocieties::Skimmed (1 name)
		decltype(auto) S48() { return (Address(P42{})()); } // Unity.SpaceGod.Core.NearbySocieties::Volatiles (1 name)
		decltype(auto) S49() { return (Outfitting::NavyFacilities()); } // Unity.SpaceGod.Core.Outfitting::NavyFacilities (1 name)
		decltype(auto) S50() { return (Outfitting::RaiderFacilities()); } // Unity.SpaceGod.Core.Outfitting::RaiderFacilities (1 name)
		decltype(auto) S51() { return (Address(P43{})()); } // Unity.SpaceGod.Core.PatrolWings::CallSigns (1 name)
		decltype(auto) S52() { return (Address(P44{})()); } // Unity.SpaceGod.Core.PatrolWings::Compositions (5 names)
		decltype(auto) S53() { return (Address(P45{})()); } // Unity.SpaceGod.Core.PatrolWings::ConvoyEscorts (3 names)
		decltype(auto) S54() { return (Address(P46{})()); } // Unity.SpaceGod.Core.PatrolWings::ConvoyNames (1 name)
		decltype(auto) S55() { return (Address(P47{})()); } // Unity.SpaceGod.Core.PatrolWings::Fittings (5 names)
		decltype(auto) S56() { return (Address(P48{})()); } // Unity.SpaceGod.Core.PatrolWings::Wingmen (1 name)
		decltype(auto) S57() { return (Address(P49{})()); } // Unity.SpaceGod.Core.People::needs (5 names)
		decltype(auto) S58() { return (Address(P50{})()); } // Unity.SpaceGod.Core.PilotTuning::ByClass (1 name)
		decltype(auto) S59() { return (Address(P51{})()); } // Unity.SpaceGod.Core.PirateCaptains::Epithet (1 name)
		decltype(auto) S60() { return (Address(P52{})()); } // Unity.SpaceGod.Core.PirateCaptains::First (1 name)
		decltype(auto) S61() { return (Address(P53{})()); } // Unity.SpaceGod.Core.PirateCaptains::Last (1 name)
		decltype(auto) S62() { return (Address(P54{})()); } // Unity.SpaceGod.Core.ProceduralSky::P (1 name)
		decltype(auto) S63() { return (Address(P55{})()); } // Unity.SpaceGod.Core.ProceduralSystems::Classes (1 name)
		decltype(auto) S64() { return (Propulsion::ReactionMass()); } // Unity.SpaceGod.Core.Propulsion::ReactionMass (1 name)
		decltype(auto) S65() { return (Address(P56{})()); } // Unity.SpaceGod.Core.RearLine::classes (1 name)
		decltype(auto) S66() { return (RenderLod::Body()); } // Unity.SpaceGod.Core.RenderLod::Body (1 name)
		decltype(auto) S67() { return (RenderLod::DockingRing()); } // Unity.SpaceGod.Core.RenderLod::DockingRing (1 name)
		decltype(auto) S68() { return (RenderLod::Fighter()); } // Unity.SpaceGod.Core.RenderLod::Fighter (1 name)
		decltype(auto) S69() { return (RenderLod::Gate()); } // Unity.SpaceGod.Core.RenderLod::Gate (1 name)
		decltype(auto) S70() { return (RenderLod::Platform()); } // Unity.SpaceGod.Core.RenderLod::Platform (1 name)
		decltype(auto) S71() { return (RenderLod::Pod()); } // Unity.SpaceGod.Core.RenderLod::Pod (1 name)
		decltype(auto) S72() { return (RenderLod::Ship()); } // Unity.SpaceGod.Core.RenderLod::Ship (1 name)
		decltype(auto) S73() { return (RenderLod::Structure()); } // Unity.SpaceGod.Core.RenderLod::Structure (1 name)
		decltype(auto) S74() { return (RenderLod::Wreck()); } // Unity.SpaceGod.Core.RenderLod::Wreck (1 name)
		decltype(auto) S75() { return (RenderLod::Classes()); } // Unity.SpaceGod.Core.RenderLod::classes (1 name)
		decltype(auto) S76() { return (RenderPipelineProfiles::HighDefinition()); } // Unity.SpaceGod.Core.RenderPipelineProfiles::HighDefinition (1 name)
		decltype(auto) S77() { return (RenderPipelineProfiles::Universal()); } // Unity.SpaceGod.Core.RenderPipelineProfiles::Universal (1 name)
		decltype(auto) S78() { return (Address(P57{})()); } // Unity.SpaceGod.Core.RenderPipelineProfiles::registered (1 name)
		decltype(auto) S79() { return (RenderShaders::All()); } // Unity.SpaceGod.Core.RenderShaders::All (1 name)
		decltype(auto) S80() { return (Address(P58{})()); } // Unity.SpaceGod.Core.SensorSuite::baseByClass (1 name)
		decltype(auto) S81() { return (SettingGroups::Order()); } // Unity.SpaceGod.Core.SettingGroups::Order (1 name)
		decltype(auto) S82() { return (SkyboxCatalog::BuiltIn()); } // Unity.SpaceGod.Core.SkyboxCatalog::BuiltIn (9 names)
		decltype(auto) S83() { return (Address(P59{})()); } // Unity.SpaceGod.Core.SkyboxCatalog::all (1 name)
		decltype(auto) S84() { return (SpinalMounts::Classes()); } // Unity.SpaceGod.Core.SpinalMounts::Classes (1 name)
		decltype(auto) S85() { return (Address(P60{})()); } // Unity.SpaceGod.Core.SpinalMounts::kinds (7 names)
		decltype(auto) S86() { return (StarDiscovery::Lookouts()); } // Unity.SpaceGod.Core.StarDiscovery::Lookouts (1 name)
		decltype(auto) S87() { return (SystemSources::Content()); } // Unity.SpaceGod.Core.SystemSources::Content (1 name)
		decltype(auto) S88() { return (Address(P61{})()); } // Unity.SpaceGod.Core.SystemSources::MainSequence (1 name)
		decltype(auto) S89() { return (Address(P62{})()); } // Unity.SpaceGod.Core.SystemSources::sources (5 names)
		decltype(auto) S90() { return (TachyonLances::CapitalClasses()); } // Unity.SpaceGod.Core.TachyonLances::CapitalClasses (1 name)
		decltype(auto) S91() { return (TradeDesk::TipSources()); } // Unity.SpaceGod.Core.TradeDesk::TipSources (1 name)
		decltype(auto) S92() { return (Address(P63{})()); } // Unity.SpaceGod.Core.WeakPoints::Specs (4 names)
		decltype(auto) S93() { return (Address(P64{})()); } // Unity.SpaceGod.Core.WeaponGroups::ByKind (1 name)
	}

	SG_STATIC_OF("Unity.SpaceGod.Core.AlphaStrike", "classes", S0())
	SG_STATIC_OF("Unity.SpaceGod.Core.AssetLibraryCandidates", "All", S1())
	SG_STATIC_OF("Unity.SpaceGod.Core.BulkFreighters", "trusses", S2())
	SG_STATIC_OF("Unity.SpaceGod.Core.CapitalHandling", "ByClass", S3())
	SG_STATIC_OF("Unity.SpaceGod.Core.CargoFootprint", "ByGood", S4())
	SG_STATIC_OF("Unity.SpaceGod.Core.CargoHoldSpec", "ByClass", S5())
	SG_STATIC_OF("Unity.SpaceGod.Core.CatalogueStars", "aliases", S6())
	SG_STATIC_OF("Unity.SpaceGod.Core.CatalogueStars", "stars", S7())
	SG_STATIC_OF("Unity.SpaceGod.Core.ControlsHelp", "Sections", S8())
	SG_STATIC_OF("Unity.SpaceGod.Core.Corvettes", "Names", S9())
	SG_STATIC_OF("Unity.SpaceGod.Core.DeepSpace", "EncounterKinds", S10())
	SG_STATIC_OF("Unity.SpaceGod.Core.DeepSpaceFeatures", "Greek", S11())
	SG_STATIC_OF("Unity.SpaceGod.Core.DeepSpaceFeatures", "Hulls", S12())
	SG_STATIC_OF("Unity.SpaceGod.Core.DeepSpaceFeatures", "Ices", S13())
	SG_STATIC_OF("Unity.SpaceGod.Core.DeepSpaceFeatures", "Veils", S14())
	SG_STATIC_OF("Unity.SpaceGod.Core.DeepSpaceFeatures", "Wanderers", S15())
	SG_STATIC_OF("Unity.SpaceGod.Core.DeepSpaceFeatures", "specs", S16())
	SG_STATIC_OF("Unity.SpaceGod.Core.DisplaySettings", "AspectValues", S17())
	SG_STATIC_OF("Unity.SpaceGod.Core.DisplaySettings", "Aspects", S18())
	SG_STATIC_OF("Unity.SpaceGod.Core.DisplaySettings", "StandardSizes", S19())
	SG_STATIC_OF("Unity.SpaceGod.Core.EnergyWeapons", "Roles", S20())
	SG_STATIC_OF("Unity.SpaceGod.Core.FacilityLayouts", "Barred", S21())
	SG_STATIC_OF("Unity.SpaceGod.Core.FleetDoctrine", "flanks", S22())
	SG_STATIC_OF("Unity.SpaceGod.Core.FleetPatterns", "doctrine", S23())
	SG_STATIC_OF("Unity.SpaceGod.Core.FleetPatterns", "patterns", S24())
	SG_STATIC_OF("Unity.SpaceGod.Core.FleetPatterns", "raiders", S25())
	SG_STATIC_OF("Unity.SpaceGod.Core.FlightDecks", "Lod", S26())
	SG_STATIC_OF("Unity.SpaceGod.Core.FlightModelRegistry", "<Default>k__BackingField", S27())
	SG_STATIC_OF("Unity.SpaceGod.Core.FtlCarriage", "berths", S28())
	SG_STATIC_OF("Unity.SpaceGod.Core.FtlCarriage", "clampSizes", S29())
	SG_STATIC_OF("Unity.SpaceGod.Core.FtlCarriage", "clamps", S30())
	SG_STATIC_OF("Unity.SpaceGod.Core.FtlCarriage", "sizes", S31())
	SG_STATIC_OF("Unity.SpaceGod.Core.FtlRoster", "byClass", S32())
	SG_STATIC_OF("Unity.SpaceGod.Core.FtlTargets", "Sources", S33())
	SG_STATIC_OF("Unity.SpaceGod.Core.GameManual", "Chapters", S34())
	SG_STATIC_OF("Unity.SpaceGod.Core.GasGiantWeather", "Jupiter", S35())
	SG_STATIC_OF("Unity.SpaceGod.Core.GasGiantWeather", "Neptune", S36())
	SG_STATIC_OF("Unity.SpaceGod.Core.GasGiantWeather", "Saturn", S37())
	SG_STATIC_OF("Unity.SpaceGod.Core.GasGiantWeather", "Uranus", S38())
	SG_STATIC_OF("Unity.SpaceGod.Core.GateBreakers", "classes", S39())
	SG_STATIC_OF("Unity.SpaceGod.Core.GoodInfo", "All", S40())
	SG_STATIC_OF("Unity.SpaceGod.Core.HandBuiltSystems", "stars", S41())
	SG_STATIC_OF("Unity.SpaceGod.Core.LocalTraffic", "PersonalKinds", S42())
	SG_STATIC_OF("Unity.SpaceGod.Core.LocalTraffic", "PersonalNames", S43())
	SG_STATIC_OF("Unity.SpaceGod.Core.ModuleSpec", "All", S44())
	SG_STATIC_OF("Unity.SpaceGod.Core.NearbySocieties", "Metals", S45())
	SG_STATIC_OF("Unity.SpaceGod.Core.NearbySocieties", "Precious", S46())
	SG_STATIC_OF("Unity.SpaceGod.Core.NearbySocieties", "Skimmed", S47())
	SG_STATIC_OF("Unity.SpaceGod.Core.NearbySocieties", "Volatiles", S48())
	SG_STATIC_OF("Unity.SpaceGod.Core.Outfitting", "NavyFacilities", S49())
	SG_STATIC_OF("Unity.SpaceGod.Core.Outfitting", "RaiderFacilities", S50())
	SG_STATIC_OF("Unity.SpaceGod.Core.PatrolWings", "CallSigns", S51())
	SG_STATIC_OF("Unity.SpaceGod.Core.PatrolWings", "Compositions", S52())
	SG_STATIC_OF("Unity.SpaceGod.Core.PatrolWings", "ConvoyEscorts", S53())
	SG_STATIC_OF("Unity.SpaceGod.Core.PatrolWings", "ConvoyNames", S54())
	SG_STATIC_OF("Unity.SpaceGod.Core.PatrolWings", "Fittings", S55())
	SG_STATIC_OF("Unity.SpaceGod.Core.PatrolWings", "Wingmen", S56())
	SG_STATIC_OF("Unity.SpaceGod.Core.People", "needs", S57())
	SG_STATIC_OF("Unity.SpaceGod.Core.PilotTuning", "ByClass", S58())
	SG_STATIC_OF("Unity.SpaceGod.Core.PirateCaptains", "Epithet", S59())
	SG_STATIC_OF("Unity.SpaceGod.Core.PirateCaptains", "First", S60())
	SG_STATIC_OF("Unity.SpaceGod.Core.PirateCaptains", "Last", S61())
	SG_STATIC_OF("Unity.SpaceGod.Core.ProceduralSky", "P", S62())
	SG_STATIC_OF("Unity.SpaceGod.Core.ProceduralSystems", "Classes", S63())
	SG_STATIC_OF("Unity.SpaceGod.Core.Propulsion", "ReactionMass", S64())
	SG_STATIC_OF("Unity.SpaceGod.Core.RearLine", "classes", S65())
	SG_STATIC_OF("Unity.SpaceGod.Core.RenderLod", "Body", S66())
	SG_STATIC_OF("Unity.SpaceGod.Core.RenderLod", "DockingRing", S67())
	SG_STATIC_OF("Unity.SpaceGod.Core.RenderLod", "Fighter", S68())
	SG_STATIC_OF("Unity.SpaceGod.Core.RenderLod", "Gate", S69())
	SG_STATIC_OF("Unity.SpaceGod.Core.RenderLod", "Platform", S70())
	SG_STATIC_OF("Unity.SpaceGod.Core.RenderLod", "Pod", S71())
	SG_STATIC_OF("Unity.SpaceGod.Core.RenderLod", "Ship", S72())
	SG_STATIC_OF("Unity.SpaceGod.Core.RenderLod", "Structure", S73())
	SG_STATIC_OF("Unity.SpaceGod.Core.RenderLod", "Wreck", S74())
	SG_STATIC_OF("Unity.SpaceGod.Core.RenderLod", "classes", S75())
	SG_STATIC_OF("Unity.SpaceGod.Core.RenderPipelineProfiles", "HighDefinition", S76())
	SG_STATIC_OF("Unity.SpaceGod.Core.RenderPipelineProfiles", "Universal", S77())
	SG_STATIC_OF("Unity.SpaceGod.Core.RenderPipelineProfiles", "registered", S78())
	SG_STATIC_OF("Unity.SpaceGod.Core.RenderShaders", "All", S79())
	SG_STATIC_OF("Unity.SpaceGod.Core.SensorSuite", "baseByClass", S80())
	SG_STATIC_OF("Unity.SpaceGod.Core.SettingGroups", "Order", S81())
	SG_STATIC_OF("Unity.SpaceGod.Core.SkyboxCatalog", "BuiltIn", S82())
	SG_STATIC_OF("Unity.SpaceGod.Core.SkyboxCatalog", "all", S83())
	SG_STATIC_OF("Unity.SpaceGod.Core.SpinalMounts", "Classes", S84())
	SG_STATIC_OF("Unity.SpaceGod.Core.SpinalMounts", "kinds", S85())
	SG_STATIC_OF("Unity.SpaceGod.Core.StarDiscovery", "Lookouts", S86())
	SG_STATIC_OF("Unity.SpaceGod.Core.SystemSources", "Content", S87())
	SG_STATIC_OF("Unity.SpaceGod.Core.SystemSources", "MainSequence", S88())
	SG_STATIC_OF("Unity.SpaceGod.Core.SystemSources", "sources", S89())
	SG_STATIC_OF("Unity.SpaceGod.Core.TachyonLances", "CapitalClasses", S90())
	SG_STATIC_OF("Unity.SpaceGod.Core.TradeDesk", "TipSources", S91())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeakPoints", "Specs", S92())
	SG_STATIC_OF("Unity.SpaceGod.Core.WeaponGroups", "ByKind", S93())

	// The classes the walk names there: only the fields it goes on through.
	SG_SERIALIZE_ABSTRACT(AssetCandidate, "Unity.SpaceGod.Core.AssetCandidate")
	SG_SERIALIZE_ABSTRACT(BomberFlightModel, "Unity.SpaceGod.Core.BomberFlightModel")
	SG_SERIALIZE_ABSTRACT(CapitalFlightModel, "Unity.SpaceGod.Core.CapitalFlightModel")
	SG_SERIALIZE_ABSTRACT(CatalogueStars, "Unity.SpaceGod.Core.CatalogueStars")
	SG_SERIALIZE_ABSTRACT(DeepSpaceFeatureSpec, "Unity.SpaceGod.Core.DeepSpaceFeatureSpec")
	SG_SERIALIZE_ABSTRACT(FlightModelRegistry, "Unity.SpaceGod.Core.FlightModelRegistry", SG_P(Fallback), b.Field("classModels", Address(P65{})), b.Field("families", Address(P66{})), b.Field("models", Address(P67{})))
	SG_SERIALIZE_ABSTRACT(GoodInfo, "Unity.SpaceGod.Core.GoodInfo")
	SG_SERIALIZE_ABSTRACT(HandBuiltSystems, "Unity.SpaceGod.Core.HandBuiltSystems")
	SG_SERIALIZE_ABSTRACT(HaulerFlightModel, "Unity.SpaceGod.Core.HaulerFlightModel")
	SG_SERIALIZE_ABSTRACT(HeavyFighterFlightModel, "Unity.SpaceGod.Core.HeavyFighterFlightModel")
	SG_SERIALIZE_ABSTRACT(HelpSection, "Unity.SpaceGod.Core.HelpSection", SG_F(Lines))
	SG_SERIALIZE_ABSTRACT(LightFighterFlightModel, "Unity.SpaceGod.Core.LightFighterFlightModel")
	SG_SERIALIZE_ABSTRACT(LodClass, "Unity.SpaceGod.Core.LodClass")
	SG_SERIALIZE_ABSTRACT(ManualChapter, "Unity.SpaceGod.Core.ManualChapter", SG_F(ControlGroups), SG_F(Topics))
	SG_SERIALIZE_ABSTRACT(ModuleSpec, "Unity.SpaceGod.Core.ModuleSpec", SG_F(Bill))
	SG_SERIALIZE_ABSTRACT(NewtonianFlightModel, "Unity.SpaceGod.Core.NewtonianFlightModel")
	SG_SERIALIZE_ABSTRACT(ProceduralSystems, "Unity.SpaceGod.Core.ProceduralSystems")
	SG_SERIALIZE_ABSTRACT(RenderPipelineProfile, "Unity.SpaceGod.Core.RenderPipelineProfile")
	SG_SERIALIZE_ABSTRACT(SkyboxSpec, "Unity.SpaceGod.Core.SkyboxSpec")
	SG_SERIALIZE_ABSTRACT(SpinalMounts::Kind, "Unity.SpaceGod.Core.SpinalMounts+Kind", SG_F(Part))
	SG_SERIALIZE_ABSTRACT(StarEntry, "Unity.SpaceGod.Core.StarEntry")
	SG_SERIALIZE_ABSTRACT(VoidStations, "Unity.SpaceGod.Core.VoidStations")
	SG_SERIALIZE_ABSTRACT(WeakPointSpec, "Unity.SpaceGod.Core.WeakPointSpec")
	SG_SERIALIZE_ABSTRACT(WeaponPattern, "Unity.SpaceGod.Core.WeaponPattern")
}
