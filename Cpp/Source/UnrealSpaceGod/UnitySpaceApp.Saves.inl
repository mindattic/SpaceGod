// UnitySpaceApp.Saves.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h).
// Port of Runtime/UnitySpaceApp.Saves.cs; bodies in UnitySpaceApp.Saves.cpp (which also defines OnApplicationQuit,
// declared with the other Unity messages in UnitySpaceApp.h).
//
// <summary>
// Save and load (U.SPC-T67): the whole simulation (the clock, every system's world and the traffic
// between them) and the player's session at the helm go to a save slot, and come back from one.
// F5 quicksaves, F8 loads the quicksave, F6 opens the slots; an autosave is taken every few minutes
// and when the app closes, and <c>-continue</c> on the command line resumes the latest save.
// </summary>
//
// C# null strings (a slot, a label, SaveBlocker's "nothing in the way") are empty strings here.

public:
	/// <summary>The save slots, in the player's data folder.</summary>
	cs::Ref<Core::SaveSlots> Saves;

	/// <summary>The save and load panel (F6).</summary>
	bool ShowSaves = false;

	/// <summary>Real seconds between autosaves.</summary>
	static constexpr float AutosaveIntervalS = 300.0f;

private:
	float autosaveDueS = AutosaveIntervalS;

	/// <summary>Off for the scripted tour and captures, so a demo run never rotates the player's autosaves out.</summary>
	bool autosaveEnabled = true;

	void InitSaves();

	void UpdateSaves(float dt);

	// (OnApplicationQuit is declared with the Unity messages in UnitySpaceApp.h; defined in UnitySpaceApp.Saves.cpp.)

public:
	/// <summary>Back to the main menu, taking an autosave on the way as quitting does.</summary>
	void ReturnToMainMenu();

private:
	cs::String pendingSave, pendingSaveLabel, pendingLoad;

	/// <summary>Main Menu was picked in the pause menu: the app leaves at the start of the next frame, not from inside its draw.</summary>
	bool pendingMainMenu = false;

public:
	/// <summary>Saves to <paramref name="slot"/> at the start of the next frame (the HUD asks from inside its draw, where views must not be rebuilt).</summary>
	void RequestSave(const cs::String& slot, const cs::String& label = cs::String());

	/// <summary>Loads <paramref name="slot"/> at the start of the next frame.</summary>
	void RequestLoad(const cs::String& slot) { pendingLoad = slot; }

private:
	void HandleSaveCommands();

public:
	/// <summary>Why the game cannot be saved or loaded right now, or null when it can.</summary>
	/// (C++: empty when it can.)
	cs::String SaveBlocker() const;

private:
	void Autosave(bool quiet);

public:
	/// <summary>Saves the whole simulation and the session into <paramref name="slot"/>.</summary>
	bool SaveTo(const cs::String& slot, const cs::String& label = cs::String());

	/// <summary>Replaces the running simulation with the one saved in <paramref name="slot"/>.</summary>
	bool LoadFrom(const cs::String& slot);

private:
	cs::Ref<Core::SaveData> CaptureSave();

	/// <summary>
	/// Swaps the loaded galaxy and clock in, rebuilds every view around the system that was being watched,
	/// and takes the helm again of the ship that was being flown, exactly where and how it was moving.
	/// </summary>
	void ApplyLoaded(const cs::Ref<Core::SaveData>& data);

	/// <summary>Lets go of everything the session held in the old galaxy, without touching it (it is about to be dropped).</summary>
	void DropSession();

public:
	static cs::String SlotName(const cs::String& slot);

	/// <summary>What a list calls the save in <paramref name="slot"/>: the name the player's own save was given, else the slot's.</summary>
	static cs::String SaveTitle(const cs::String& slot, const cs::Ref<Core::SaveHeader>& h);

	/// <summary>One line for a save: where and when in the simulation, who was flying and their purse.</summary>
	static cs::String DescribeSave(const cs::Ref<Core::SaveHeader>& h);
