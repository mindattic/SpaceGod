// Port of Unity.SpaceGod Assets/UnitySpace/Core/Rendering/Skyboxes.cs (see docs/MIGRATION.md).

#include "Rendering/Skyboxes.h"

#include "Astro.h"
#include "Settings/GameSetting.h"
#include "Settings/SettingsRegistry.h"

namespace SpaceGod::Core
{
	const char* ToString(SkyKind v)
	{
		switch (v)
		{
			case SkyKind::Starfield: return "Starfield";
			case SkyKind::Image: return "Image";
			case SkyKind::ImageFile: return "ImageFile";
			case SkyKind::Procedural: return "Procedural";
		}
		return "";
	}

	const char* ToString(SkyFrame v)
	{
		switch (v)
		{
			case SkyFrame::Ecliptic: return "Ecliptic";
			case SkyFrame::Equatorial: return "Equatorial";
			case SkyFrame::Galactic: return "Galactic";
		}
		return "";
	}

	namespace
	{
		// System.Uri.EscapeDataString (.NET Core): every byte of the UTF-8 text but RFC 3986's unreserved characters
		// as %XX, upper-case hex.
		cs::String EscapeDataString(const cs::String& s)
		{
			static const char* hex = "0123456789ABCDEF";
			cs::String r;
			for (unsigned char c : s)
			{
				bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
				if (unreserved) r += static_cast<char>(c);
				else { r += '%'; r += hex[c >> 4]; r += hex[c & 15]; }
			}
			return r;
		}

		int HexValue(char c)
		{
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			return -1;
		}

		// System.Uri.UnescapeDataString: each %XX back to its byte; anything else (a stray '%') left as it is. (.NET
		// also leaves escapes that do not form valid UTF-8 escaped; Format never writes any.)
		cs::String UnescapeDataString(const cs::String& s)
		{
			cs::String r;
			for (size_t i = 0; i < s.size(); i++)
			{
				int hi, lo;
				if (s[i] == '%' && i + 2 < s.size() && (hi = HexValue(s[i + 1])) >= 0 && (lo = HexValue(s[i + 2])) >= 0)
				{
					r += static_cast<char>(hi * 16 + lo);
					i += 2;
				}
				else r += s[i];
			}
			return r;
		}
	}

	// ---------------------------------------------------------------- SkyboxSpec

	SkyboxSpec::SkyboxSpec(const cs::String& id, const cs::String& name, SkyKind kind, const cs::String& path, SkyFrame frame, bool pointStars, const cs::String& credit, const cs::String& licence)
	{
		if (cs::Strings::IsNullOrWhiteSpace(id)) throw cs::ArgumentException("A sky needs an id. (Parameter 'id')");
		Id = id;
		Name = cs::Strings::IsNullOrWhiteSpace(name) ? id : name;
		Kind = kind;
		Path = path;
		Frame = frame;
		PointStars = pointStars;
		Credit = credit;
		Licence = licence;
	}

	// ---------------------------------------------------------------- SkyboxCatalog

	const cs::List<cs::Ref<SkyboxSpec>>& SkyboxCatalog::BuiltIn()
	{
		static const cs::List<cs::Ref<SkyboxSpec>> value
		{
			cs::New<SkyboxSpec>(SeededNebulaId, "This system's own nebulae", SkyKind::Procedural, "", SkyFrame::Ecliptic, true,
				"Generated in game from the system's seed, after wwwtyro's Space 3D (github.com/wwwtyro/space-3d)", "Unlicense (public domain)"),
			cs::New<SkyboxSpec>(DeepStarMapId, "Real sky: NASA Deep Star Map", SkyKind::Image, cs::Cat(ResourceFolder, "nasa_deep_star_map_8k"), SkyFrame::Equatorial, false,
				"NASA/Goddard Space Flight Center Scientific Visualization Studio; Gaia DR2: ESA/Gaia/DPAC", "Public domain (NASA)"),
			cs::New<SkyboxSpec>("eso-milky-way", "Real sky: ESO Milky Way panorama", SkyKind::Image, cs::Cat(ResourceFolder, "eso_milky_way_6k"), SkyFrame::Galactic, false,
				"ESO/S. Brunier", "CC BY 4.0"),
			cs::New<SkyboxSpec>("spheremaps-blue-nebulae", "Blue nebulae", SkyKind::Image, cs::Cat(ResourceFolder, "spheremaps_blue_nebulae_8k"), SkyFrame::Ecliptic, false,
				"Space Spheremaps (space-spheremaps.itch.io)", "Free for any use, redistribution allowed, attribution appreciated"),
			cs::New<SkyboxSpec>("spheremaps-hazy-nebulae", "Hazy nebulae", SkyKind::Image, cs::Cat(ResourceFolder, "spheremaps_hazy_nebulae_8k"), SkyFrame::Ecliptic, false,
				"Space Spheremaps (space-spheremaps.itch.io)", "Free for any use, redistribution allowed, attribution appreciated"),
			cs::New<SkyboxSpec>("spheremaps-plain-starfield", "Deep starfield", SkyKind::Image, cs::Cat(ResourceFolder, "spheremaps_plain_starfield_8k"), SkyFrame::Ecliptic, false,
				"Space Spheremaps (space-spheremaps.itch.io)", "Free for any use, redistribution allowed, attribution appreciated"),
			cs::New<SkyboxSpec>("spheremaps-ringed-gas-giant", "Ringed giant", SkyKind::Image, cs::Cat(ResourceFolder, "spheremaps_ringed_gas_giant_8k"), SkyFrame::Ecliptic, false,
				"Space Spheremaps (space-spheremaps.itch.io)", "Free for any use, redistribution allowed, attribution appreciated"),
			cs::New<SkyboxSpec>(StarfieldId, "Bare starfield", SkyKind::Starfield, "", SkyFrame::Ecliptic, true, "The game's own point-cloud stars", "Part of the game"),
		};
		return value;
	}

	cs::List<cs::Ref<SkyboxSpec>>& SkyboxCatalog::all()
	{
		static cs::List<cs::Ref<SkyboxSpec>> value = []
		{
			cs::List<cs::Ref<SkyboxSpec>> l;
			l.AddRange(BuiltIn());
			return l;
		}();
		return value;
	}

	std::mutex& SkyboxCatalog::gate()
	{
		static std::mutex value;
		return value;
	}

	void SkyboxCatalog::RunClassConstructor()
	{
		BuiltIn();
		all();
		gate();
	}

	cs::List<cs::Ref<SkyboxSpec>> SkyboxCatalog::All()
	{
		auto& list = all();
		std::lock_guard<std::mutex> lock(gate());
		return list.ToArray();
	}

	bool SkyboxCatalog::Register(const cs::Ref<SkyboxSpec>& spec)
	{
		if (spec == nullptr) throw cs::ArgumentNullException("Value cannot be null. (Parameter 'spec')");
		auto& list = all();
		std::lock_guard<std::mutex> lock(gate());
		for (const auto& s : list)
			if (s->Id == spec->Id) return false;
		list.Add(spec);
		return true;
	}

	cs::Ref<SkyboxSpec> SkyboxCatalog::Find(const cs::String& id)
	{
		if (cs::Strings::IsNullOrEmpty(id)) return nullptr;
		auto& list = all();
		std::lock_guard<std::mutex> lock(gate());
		for (const auto& s : list)
			if (s->Id == id) return s;
		return nullptr;
	}

	cs::Ref<SkyboxSpec> SkyboxCatalog::Step(const cs::Ref<SkyboxSpec>& current, int step)
	{
		auto list = All();
		int i = -1;
		for (int k = 0; k < list.Count(); k++)
			if (list[k] == current) { i = k; break; }
		if (i < 0) return list[0];
		int n = list.Count();
		return list[((i + step) % n + n) % n];
	}

	cs::Ref<SkyboxSpec> SkyboxCatalog::DefaultFor(const cs::String& systemName)
	{
		int pick = SkyboxSettings::DefaultSky()->Index;
		if (pick > 0 && pick - 1 < BuiltIn().Count()) return BuiltIn()[pick - 1];
		return Find(IsSol(systemName) ? DeepStarMapId : SeededNebulaId);
	}

	void SkyboxCatalog::FrameAxes(SkyFrame frame, DVec3& x, DVec3& y, DVec3& z)
	{
		switch (frame)
		{
			case SkyFrame::Equatorial:
				x = Astro::PoleFromRaDec(0, 0);
				y = Astro::PoleFromRaDec(90, 0);
				z = Astro::PoleFromRaDec(0, 90);
				return;
			case SkyFrame::Galactic:
				// The galactic centre and north galactic pole (J2000, Reid & Brunthaler 2004 / IAU 1958 frame).
				x = Astro::PoleFromRaDec(266.40499, -28.93617);
				z = Astro::PoleFromRaDec(192.85948, 27.12825);
				y = DVec3::Cross(z, x).Normalized();
				return;
			default:
				x = DVec3::UnitX;
				y = DVec3::UnitY;
				z = DVec3::UnitZ;
				return;
		}
	}

	void SkyboxCatalog::PanoramaUv(SkyFrame frame, DVec3 d, double& u, double& v)
	{
		DVec3 ax, ay, az;
		FrameAxes(frame, ax, ay, az);
		d = d.Normalized();
		double lon = cs::Math::Atan2(DVec3::Dot(d, ay), DVec3::Dot(d, ax));
		double lat = cs::Math::Asin(cs::Math::Max(-1.0, cs::Math::Min(1.0, DVec3::Dot(d, az))));
		u = 0.5 - lon / (2 * cs::Math::PI);
		u -= cs::Math::Floor(u);
		v = 0.5 + lat / cs::Math::PI;
	}

	// ---------------------------------------------------------------- SkyboxSettings

	cs::List<cs::String> SkyboxSettings::DefaultOptions()
	{
		auto options = cs::NewArray<cs::String>(SkyboxCatalog::BuiltIn().Count() + 1);
		options[0] = PerSystem;
		for (int i = 0; i < SkyboxCatalog::BuiltIn().Count(); i++) options[i + 1] = SkyboxCatalog::BuiltIn()[i]->Name;
		return options;
	}

	void SkyboxSettings::RunClassConstructor()
	{
		static bool started = false;
		if (started) return;
		started = true;
		DefaultSky();
		Brightness();
		NebulaResolution();
	}

	const cs::Ref<ChoiceSetting>& SkyboxSettings::DefaultSky()
	{
		RunClassConstructor();
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("sky.default", SettingGroups::Graphics, "Skybox", DefaultOptions(), 0,
			cs::Cat("The sky behind every star system you have not picked one for (Shift+K or numpad * cycles the sky in the system in view, and that ",
				"system keeps it). The real skies are NASA's Deep Star Map and ESO's Milky Way panorama, laid where the stars really are; the ",
				"nebula skies are 8K panoramas; 'its own nebulae' is made from the system's seed; 'Bare starfield' is the old black sky."),
			"sky skybox background nebula stars milky way space backdrop");
		return value;
	}

	const cs::Ref<RangeSetting>& SkyboxSettings::Brightness()
	{
		RunClassConstructor();
		// C#'s `v.ToString("0.##")` names no culture (the current one); invariant here, as the rest of Core.
		static const cs::Ref<RangeSetting> value = cs::New<RangeSetting>("sky.brightness", SettingGroups::Graphics, "Skybox brightness", 1, 0.1, 3, 0.05,
			"How bright the skybox is drawn behind everything. Lower keeps the sky from competing with ships and worlds.",
			[](double v) { return cs::Cat(cs::Fmt(v, "0.##"), "x"); }, "sky skybox brightness background dim");
		return value;
	}

	const cs::Ref<ChoiceSetting>& SkyboxSettings::NebulaResolution()
	{
		RunClassConstructor();
		static const cs::Ref<ChoiceSetting> value = cs::New<ChoiceSetting>("sky.nebulaResolution", SettingGroups::Graphics, "Seeded nebula detail",
			cs::List<cs::String>{ "512", "1024", "2048" }, 0,
			cs::Cat("How finely a system's own nebulae are made, on background threads (the old sky stays up and the new one fades in when done): ",
				"512 is a 2048 x 1024 panorama, made in a second or two; 2048 is 8192 x 4096 and can take half a minute."),
			"sky skybox nebula procedural resolution detail");
		return value;
	}

	int SkyboxSettings::NebulaFaceSize() { return cs::Int::Parse(NebulaResolution()->Selected()); }

	// ---------------------------------------------------------------- SkyboxMemory

	SkyboxMemory::SkyboxMemory(cs::Func<cs::Ref<ISettingStore>> store)
	{
		this->store = store ? store : cs::Func<cs::Ref<ISettingStore>>([] { return SettingsRegistry::Default()->Store(); });
	}

	cs::Ref<SkyboxSpec> SkyboxMemory::For(const cs::String& systemName)
	{
		Sync();
		cs::String id;
		if (!systemName.empty() && chosen.TryGetValue(systemName, id))
		{
			auto spec = SkyboxCatalog::Find(id);
			if (spec != nullptr) return spec;
		}
		return SkyboxCatalog::DefaultFor(systemName);
	}

	bool SkyboxMemory::IsChosen(const cs::String& systemName)
	{
		Sync();
		return !systemName.empty() && chosen.ContainsKey(systemName);
	}

	void SkyboxMemory::Set(const cs::String& systemName, const cs::Ref<SkyboxSpec>& spec)
	{
		if (systemName.empty() || spec == nullptr) return;
		Sync();
		chosen[systemName] = spec->Id;
		Write();
	}

	cs::Ref<SkyboxSpec> SkyboxMemory::Cycle(const cs::String& systemName, int step)
	{
		auto next = SkyboxCatalog::Step(For(systemName), step == 0 ? 1 : step);
		Set(systemName, next);
		return next;
	}

	void SkyboxMemory::Clear()
	{
		Sync();
		chosen.Clear();
		Write();
	}

	void SkyboxMemory::Sync()
	{
		auto s = store();
		if (loaded && s == loadedFrom) return;
		loaded = true;
		loadedFrom = s;
		if (s == nullptr) return; // no profile: keep what this run has chosen
		chosen.Clear();
		cs::Nullable<cs::String> text;
		// C# catches IOException and UnauthorizedAccessException (Cs.h has no UnauthorizedAccessException).
		try { text = s->ReadSetting(Key); }
		catch (const cs::IOException&) {}
		for (const auto& pair : Parse(text.value_or(cs::String()))) chosen[pair.first] = pair.second;
	}

	void SkyboxMemory::Write()
	{
		auto s = store();
		if (s == nullptr) return;
		try
		{
			if (chosen.Count() == 0) s->ForgetSetting(Key);
			else s->WriteSetting(Key, Format(chosen));
		}
		catch (const cs::IOException&) {}
	}

	cs::String SkyboxMemory::Format(const cs::Dictionary<cs::String, cs::String>& picks)
	{
		auto keys = picks.Keys();
		keys.Sort([](const cs::String& a, const cs::String& b) { return cs::Strings::CompareOrdinal(a, b); });
		cs::StringBuilder sb;
		for (const auto& k : keys)
		{
			if (sb.Length() > 0) sb.Append(';');
			sb.Append(EscapeDataString(k)).Append('=').Append(EscapeDataString(picks[k]));
		}
		return sb.ToString();
	}

	cs::List<std::pair<cs::String, cs::String>> SkyboxMemory::Parse(const cs::String& text)
	{
		cs::List<std::pair<cs::String, cs::String>> result;
		if (cs::Strings::IsNullOrEmpty(text)) return result;
		for (const auto& part : cs::Strings::Split(text, ';'))
		{
			int eq = cs::Strings::IndexOf(part, '=');
			if (eq <= 0 || eq == static_cast<int>(part.size()) - 1) continue;
			cs::String name = UnescapeDataString(cs::Strings::Substring(part, 0, eq));
			cs::String id = UnescapeDataString(cs::Strings::Substring(part, eq + 1));
			result.Add(std::make_pair(name, id));
		}
		return result;
	}
}
