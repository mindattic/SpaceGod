// UnitySpaceApp.Boarding.inl -- included INSIDE class UnitySpaceApp (see UnitySpaceApp.h).
// Port of Runtime/UnitySpaceApp.Boarding.cs; bodies in UnitySpaceApp.Boarding.cpp.
//
// <summary>
// Interactive boarding (U.SPC-T468, docs/BOARDING.md). While piloting in a battle with a fight aboard, the piloted
// hull's own (defending) or one its faction's marines are in (boarding), B opens the boarding panel and the number
// keys give that side its standing orders over the compartments: 1 picks the next compartment, 2 sends the boarders
// for it or musters the defenders on it, 3 has them hold where they stand, 0 hands the side back to the resolver
// (the auto-resolve). The fight in each compartment is the resolver's weighted roll as ever. A hail still has the
// number keys first.
// </summary>

public:
	/// <summary>The boarding panel is open (B, at the controls).</summary>
	bool ShowBoarding = false;

	/// <summary>The fight aboard the player can command, and on which side, as of the last battle frame (null for none).</summary>
	/// C# `(BattleUnit hull, BoardingRole role)?`: get<0> is the hull, get<1> the role.
	cs::Nullable<std::tuple<cs::Ref<Core::BattleUnit>, Core::BoardingRole>> BoardingFight;

	/// <summary>The compartment picked on the boarding panel (-1 for none yet).</summary>
	int BoardingPick = -1;

	/// <summary>Whether the panel is up with a fight to command.</summary>
	bool BoardingPanelOpen();

private:
	void UpdateBoardingCommand(const cs::Ref<Core::Battle>& battle, const cs::Ref<Core::BattleUnit>& unit);

	static cs::String Side(Core::BoardingRole role);
