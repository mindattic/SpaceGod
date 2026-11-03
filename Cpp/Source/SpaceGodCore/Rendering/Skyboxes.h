#pragma once
// Port of Unity.SpaceGod Assets/UnitySpace/Core/Rendering/Skyboxes.cs (see docs/MIGRATION.md).

#include "Cs/Cs.h"
#include "DVec3.h"

#include <mutex>
#include <utility>

namespace SpaceGod::Core
{
	class ChoiceSetting;
	class ISettingStore;
	class RangeSetting;

	/// <summary>What a sky is drawn from (U.SPC-T595).</summary>
	enum class SkyKind : int
	{
		/// <summary>No backdrop: only the point-cloud starfield on black (the game's sky before skyboxes).</summary>
		Starfield,

		/// <summary>An equirectangular (2:1) panorama shipped in the game's Resources.</summary>
		Image,

		/// <summary>An equirectangular panorama the player dropped into the profile's Skyboxes folder.</summary>
		ImageFile,

		/// <summary>Nebulae made from the system's own seed (<see cref="ProceduralSky"/>), under the point-cloud stars.</summary>
		Procedural,
	};
	SPACEGODCORE_API const char* ToString(SkyKind v);

	/// <summary>
	/// The frame a panorama is laid out in (U.SPC-T595). Its centre column is longitude 0, longitude rises to the left (as the
	/// sky is seen from inside), and the top row is latitude +90, so a real sky map lands where its stars really are.
	/// </summary>
	enum class SkyFrame : int
	{
		/// <summary>The game's own J2000 ecliptic axes: the horizon of the picture lies along the ecliptic.</summary>
		Ecliptic,

		/// <summary>J2000 right ascension and declination (NASA SVS Deep Star Maps).</summary>
		Equatorial,

		/// <summary>Galactic longitude and latitude, the galactic centre in the middle (ESO's Milky Way panorama).</summary>
		Galactic,
	};
	SPACEGODCORE_API const char* ToString(SkyFrame v);

	/// <summary>One sky the player can cycle to (U.SPC-T595): where it comes from, how it is laid out and under what licence.</summary>
	class SPACEGODCORE_API SkyboxSpec final
	{
	public:
		/// <summary>The stable id it is remembered by in a profile (never shown).</summary>
		cs::String Id;

		/// <summary>The name the toast, the settings menu and the manual show.</summary>
		cs::String Name;

		SkyKind Kind = SkyKind::Starfield;

		/// <summary>The Resources path of a shipped panorama (no extension), or the full path of a dropped-in file.</summary>
		cs::String Path;

		SkyFrame Frame = SkyFrame::Ecliptic;

		/// <summary>Whether the point-cloud starfield is drawn over it: a photographed sky has its own stars.</summary>
		bool PointStars = false;

		/// <summary>Who made it, its licence and the credit it asks for (recorded in Resources/Skyboxes/SOURCES.md too).</summary>
		cs::String Credit, Licence;

		// (C#'s null strings are the empty string here; `?? ""` leaves them as they are.)
		SkyboxSpec(const cs::String& id, const cs::String& name, SkyKind kind, const cs::String& path, SkyFrame frame, bool pointStars, const cs::String& credit, const cs::String& licence);

		cs::String ToString() const { return Name; }
	};

	/// <summary>
	/// Every sky the game can draw behind a star system (U.SPC-T595): the panoramas shipped in Resources/Skyboxes, the
	/// system's own seeded nebulae, the bare starfield, and any panorama the player drops into the profile's Skyboxes folder
	/// (<see cref="Register"/>). Shift+K (or numpad *) cycles through them in the system in view; the choice is kept per
	/// system (<see cref="SkyboxMemory"/>). A new sky is one more <see cref="Register"/> call, never an edit to the view.
	/// </summary>
	class SPACEGODCORE_API SkyboxCatalog
	{
	public:
		static constexpr const char* SeededNebulaId = "seeded-nebula";
		static constexpr const char* DeepStarMapId = "nasa-deep-star-map";
		static constexpr const char* StarfieldId = "starfield";

		/// <summary>The folder under Resources the shipped panoramas live in.</summary>
		static constexpr const char* ResourceFolder = "Skyboxes/";

	private:
		// The static constructor's `all.AddRange(BuiltIn)` is this list's initialiser.
		static cs::List<cs::Ref<SkyboxSpec>>& all();
		static std::mutex& gate();

	public:
		/// <summary>C# RuntimeHelpers.RunClassConstructor: the static constructor (BuiltIn, then all filled from it).</summary>
		static void RunClassConstructor();

		/// <summary>The skies shipped with the game, in the order the key cycles them.</summary>
		static const cs::List<cs::Ref<SkyboxSpec>>& BuiltIn();

		/// <summary>Every sky there is, built-in first, then the dropped-in ones in the order they were found.</summary>
		static cs::List<cs::Ref<SkyboxSpec>> All();

		/// <summary>Adds a sky (a dropped-in panorama); one whose id is already listed is refused and false returned.</summary>
		static bool Register(const cs::Ref<SkyboxSpec>& spec);

		/// <summary>The sky with this id, or null.</summary>
		static cs::Ref<SkyboxSpec> Find(const cs::String& id);

		/// <summary>The sky <paramref name="step"/> places on from <paramref name="current"/> in <see cref="All"/>, wrapping round.</summary>
		static cs::Ref<SkyboxSpec> Step(const cs::Ref<SkyboxSpec>& current, int step);

		/// <summary>
		/// The sky a system shows before the player picks one: <see cref="SkyboxSettings.DefaultSky"/>, or with that left at
		/// "Per system", the real sky in Sol and the system's own seeded nebulae everywhere else.
		/// </summary>
		static cs::Ref<SkyboxSpec> DefaultFor(const cs::String& systemName);

		/// <summary>Whether <paramref name="systemName"/> is Sol (<see cref="Galaxy.CreateSol"/> names it "Sol System").</summary>
		static bool IsSol(const cs::String& systemName) { return systemName == "Sol System" || systemName == "Sol"; }

		/// <summary>
		/// A panorama frame's three axes in the game's J2000 ecliptic coordinates: longitude 0 (x), longitude 90 (y) and the
		/// north pole (z). A sky direction <c>d</c> reads at longitude atan2(d·y, d·x) and latitude asin(d·z).
		/// </summary>
		static void FrameAxes(SkyFrame frame, DVec3& x, DVec3& y, DVec3& z);

		/// <summary>Where direction <paramref name="d"/> (ecliptic) falls on a panorama in <paramref name="frame"/>: u, v in 0..1.</summary>
		static void PanoramaUv(SkyFrame frame, DVec3 d, double& u, double& v);
	};

	/// <summary>The sky's settings (U.SPC-T595), under Graphics.</summary>
	// [SettingsSource]
	class SPACEGODCORE_API SkyboxSettings
	{
	public:
		static constexpr const char* PerSystem = "Per system (real sky in Sol, its own nebulae elsewhere)";

	private:
		static cs::List<cs::String> DefaultOptions();

	public:
		/// <summary>C# RuntimeHelpers.RunClassConstructor (what SettingsRegistry does for a [SettingsSource] class): creates
		/// every static field in textual order. Every static field accessor runs it first.</summary>
		static void RunClassConstructor();

		/// <summary>What a system shows until the player picks a sky there with Shift+K.</summary>
		static const cs::Ref<ChoiceSetting>& DefaultSky();

		/// <summary>How bright the sky is drawn (the point-cloud stars keep their own brightness).</summary>
		static const cs::Ref<RangeSetting>& Brightness();

		/// <summary>The seeded-nebula sky's resolution: a quarter of its panorama's width (a cube face's edge at the same detail).</summary>
		static const cs::Ref<ChoiceSetting>& NebulaResolution();

		static int NebulaFaceSize();
	};

	/// <summary>
	/// Which sky each star system was last given (U.SPC-T595), kept in the active profile's settings under
	/// <see cref="Key"/> as "system=sky" pairs, so every system keeps its own across sessions; a system never set shows
	/// <see cref="SkyboxCatalog.DefaultFor"/>. With no profile bound the choices last for the run.
	/// </summary>
	// (A null system name is the empty string here: the C# `systemName != null` tests are `!systemName.empty()`.)
	class SPACEGODCORE_API SkyboxMemory final
	{
	public:
		static constexpr const char* Key = "sky.bySystem";

	private:
		cs::Func<cs::Ref<ISettingStore>> store;
		cs::Dictionary<cs::String, cs::String> chosen; // StringComparer.Ordinal
		cs::Ref<ISettingStore> loadedFrom;
		bool loaded = false;

	public:
		/// <summary>A memory over <paramref name="store"/> (read each time, so a profile switched later is followed).</summary>
		SkyboxMemory(cs::Func<cs::Ref<ISettingStore>> store = nullptr);

		/// <summary>The sky <paramref name="systemName"/> shows: the one picked there (if it still exists), else its default.</summary>
		cs::Ref<SkyboxSpec> For(const cs::String& systemName);

		/// <summary>Whether the player picked this system's sky (rather than it showing the default).</summary>
		bool IsChosen(const cs::String& systemName);

		/// <summary>Gives <paramref name="systemName"/> <paramref name="spec"/> and writes it to the profile.</summary>
		void Set(const cs::String& systemName, const cs::Ref<SkyboxSpec>& spec);

		/// <summary>Moves <paramref name="systemName"/>'s sky <paramref name="step"/> places on through the catalogue; returns it.</summary>
		cs::Ref<SkyboxSpec> Cycle(const cs::String& systemName, int step = 1);

		/// <summary>Forgets every system's pick (each shows its default again).</summary>
		void Clear();

	private:
		void Sync();

		void Write();

	public:
		/// <summary>"Sol=nasa-deep-star-map;Alpha%20Centauri=seeded-nebula": names and ids escaped, sorted by system.</summary>
		static cs::String Format(const cs::Dictionary<cs::String, cs::String>& picks);

		/// <summary>Reads what <see cref="Format"/> wrote; damaged pairs are skipped.</summary>
		static cs::List<std::pair<cs::String, cs::String>> Parse(const cs::String& text);
	};
}
