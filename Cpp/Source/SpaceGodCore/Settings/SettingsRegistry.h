#pragma once
// Port of Unity.SpaceGod Core/Settings/SettingsRegistry.cs (US-T21, see docs/MIGRATION.md).
//
// Discovery. C# marks each class that declares settings with [SettingsSource] and DiscoverDeclarations runs every
// such class's static initialiser by reflection. C++ has no reflection, and the ported classes keep their settings in
// lazily built function-local statics, so the C++ discovery is an explicit list: DiscoverLoadedDeclarations()
// (Settings/SettingsSources.cpp) touches every Core settings source, in C#'s discovery order, and then each source
// added with AddDeclarationSource (the engine side's own [SettingsSource] classes, which live outside Core). The
// SettingsSourceAttribute class itself has no C++ counterpart; a new source is added to that list instead.
//
// Ownership. A setting registers itself from its constructor (as in C#), before any shared_ptr owns it, so the
// registry records it through RegisterDeclared (a raw pointer, held as a non-owning cs::Ref) and adopts it -- swaps
// in the owning cs::Ref from weak_from_this() -- the next time anything reads the registry (All, Find, Register).
// A setting destroyed before that removes itself (ForgetDeclared). After adoption the registry owns its settings,
// as C#'s does.

#include "Cs/Cs.h"

#include <atomic>
#include <mutex>

namespace SpaceGod::Core
{
	class GameSetting;

	/// <summary>Where a registry remembers values: a player profile's settings files, or memory in a test.</summary>
	class ISettingStore
	{
	public:
		virtual ~ISettingStore() = default;

		/// <summary>The text last written for <paramref name="key"/>, or null when none was (or it cannot be read).</summary>
		/// (C# null is an empty Nullable: "" is a value that was written.)
		virtual cs::Nullable<cs::String> ReadSetting(const cs::String& key) = 0;

		virtual void WriteSetting(const cs::String& key, const cs::String& value) = 0;

		/// <summary>Forgets <paramref name="key"/>, so its default stands (and a later change to the default reaches it).</summary>
		virtual void ForgetSetting(const cs::String& key) = 0;
	};

	/// <summary>
	/// Every setting the game's systems declare (U.SPC-T431), grouped and searchable for the settings menu, each
	/// remembered in the bound <see cref="ISettingStore"/> (the active profile) the moment it changes. A setting
	/// at its default is not written, so a value never chosen follows the default if a later version moves it.
	/// </summary>
	class SPACEGODCORE_API SettingsRegistry final : public std::enable_shared_from_this<SettingsRegistry>
	{
	public:
		/// <summary>The game's own registry, which declarations join unless they name another.</summary>
		static const cs::Ref<SettingsRegistry>& Default();

	private:
		cs::List<cs::Ref<GameSetting>> settings;
		cs::Dictionary<cs::String, cs::Ref<GameSetting>> byKey; // StringComparer.Ordinal: std::string equality
		mutable std::recursive_mutex gate;                      // C#: lock (gate) (re-entered through Register -> Load)
		cs::Ref<ISettingStore> store;

		/// Not C#: settings registered from their constructors and not adopted yet (see the file comment).
		cs::List<GameSetting*> pending;

	public:
		/// <summary>Raised when the store refuses a write (a full or read-only disk): the value still stands for this run.</summary>
		cs::Event<cs::Ref<GameSetting>, const cs::Exception&> StoreFailed;

		cs::List<cs::Ref<GameSetting>> All();

		cs::Ref<ISettingStore> Store() const { return store; }

		/// <summary>Adds <paramref name="setting"/>; a second setting with the same key is a mistake and refused.</summary>
		void Register(const cs::Ref<GameSetting>& setting);

		/// Not C#: Register(this) from a setting's constructor (see the file comment).
		void RegisterDeclared(GameSetting* setting);

		/// Not C#: a setting's destructor, before the registry adopted it.
		void ForgetDeclared(GameSetting* setting);

	private:
		/// Not C#: swaps each pending setting's non-owning cs::Ref for its owning one, where it has one yet. Under gate.
		void Adopt();

		void Add(const cs::Ref<GameSetting>& setting);

		std::atomic<int> discovered{ 0 };

	public:
		/// <summary>The setting declared under <paramref name="key"/>, or null. The first miss in the default registry runs
		/// every marked class's declarations (<see cref="DiscoverLoadedDeclarations"/>), so a setting whose class nothing has
		/// touched yet is still found, whatever order the lookups come in.</summary>
		cs::Ref<GameSetting> Find(const cs::String& key);

		/// <summary>The groups that hold settings: those of <see cref="SettingGroups.Order"/> first, in that order, then the rest by name.</summary>
		cs::List<cs::String> Groups();

		/// <summary>The settings in <paramref name="group"/>, in the order they were declared.</summary>
		cs::List<cs::Ref<GameSetting>> InGroup(const cs::String& group);

		/// <summary>The settings every word of <paramref name="query"/> finds (in label, group, description or keywords),
		/// by group as the menu orders them; every setting for an empty query.</summary>
		cs::List<cs::Ref<GameSetting>> Search(const cs::String& query);

		/// <summary>
		/// Remembers values in <paramref name="newStore"/> from now on, and takes each setting's value from it (its
		/// default when it has none, or a damaged one). Every setting's <see cref="GameSetting.Changed"/> is raised,
		/// so what each system put into effect follows the profile. Null unbinds: values stay but are not written.
		/// </summary>
		void Bind(const cs::Ref<ISettingStore>& newStore);

	private:
		static void Load(GameSetting& s, ISettingStore& from);

	public:
		/// <summary>Writes <paramref name="s"/>'s new value (or forgets it, back at its default).</summary>
		/// (C# internal.)
		void Remember(GameSetting& s);

		/// <summary>Puts <paramref name="s"/> back to its default and forgets it in the profile.</summary>
		void Reset(const cs::Ref<GameSetting>& s);

		/// <summary>Puts every setting in <paramref name="group"/> back to its default.</summary>
		void ResetGroup(const cs::String& group);

		void ResetAll();

		/// <summary>Every loaded assembly's declarations (<see cref="DiscoverDeclarations"/>).</summary>
		/// C++: touches every settings source on the explicit list (Settings/SettingsSources.cpp), then each one added with
		/// AddDeclarationSource; returns how many sources there are. (C#'s DiscoverDeclarations(assemblies) has no
		/// counterpart: there is nothing to reflect over.)
		static int DiscoverLoadedDeclarations();

		/// Not C#: adds a settings source outside Core (the engine side's own [SettingsSource] classes) to what
		/// DiscoverLoadedDeclarations touches, after Core's. <paramref name="touch"/> builds the source's settings.
		static void AddDeclarationSource(cs::Action<> touch);

	private:
		static cs::List<cs::Action<>>& ExtraSources();
	};

	/// <summary>A store kept in memory: settings nobody needs to remember between runs, and tests.</summary>
	class SPACEGODCORE_API MemorySettingStore final : public ISettingStore
	{
	public:
		cs::Dictionary<cs::String, cs::String> Values; // StringComparer.Ordinal

		cs::Nullable<cs::String> ReadSetting(const cs::String& key) override
		{
			cs::String v;
			if (Values.TryGetValue(key, v)) return v;
			return std::nullopt;
		}
		void WriteSetting(const cs::String& key, const cs::String& value) override { Values[key] = value; }
		void ForgetSetting(const cs::String& key) override { Values.Remove(key); }
	};
}
