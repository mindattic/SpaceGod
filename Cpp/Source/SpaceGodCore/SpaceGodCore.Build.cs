using System;
using System.Collections.Generic;
using System.Reflection;
using EpicGames.Core;
using UnrealBuildTool;

// SpaceGodCore: Unity.SpaceGod's Core simulation translated to C++20 (docs/MIGRATION.md). Pure standard C++
// against Cs/Cs.h; Module/SpaceGodCoreModule.cpp is the only file with Unreal headers. The same sources also
// build without Unreal (tools/build-core.ps1) for the headless Bench.
public class SpaceGodCore : ModuleRules
{
	// Porting phase: while the Core waves commit not-yet-compilable files into SpaceGodCore/<Dir>/, UE compiles
	// only these folders. The integration ticket sets this to null, and then UE compiles the whole module.
	static readonly string[] PortingAllowList = { "Module", "Cs", "Shared" };

	public SpaceGodCore(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.CPlusPlus;
		PCHUsage = PCHUsageMode.NoPCHs; // no Unreal PCH: Core must never see Unreal's macros (check, TEXT, ...)
		bEnableExceptions = true;       // C# exceptions are C++ exceptions
		bUseRTTI = true;                // cs::As / cs::Is are dynamic_pointer_cast
		bUseUnity = false;              // file-local helpers of different .cpp files must not meet
		CppStandard = CppStandardVersion.Cpp20;
		// Bit-for-bit parity with the C# Core: no /fp:fast (UE's default), so no reassociation, no contraction,
		// and NaN tests (Math.Max, double.IsNaN) keep their meaning.
		FPSemantics = FPSemanticsMode.Precise;
		PublicIncludePaths.Add(ModuleDirectory);
		PublicDependencyModuleNames.Add("Core");

		if (PortingAllowList != null) RestrictToAllowList();
	}

	// UBT has no per-module source filter, so this swaps the module's own source root (normally ModuleDirectory,
	// searched recursively) for its first allow-listed folder and adds the others as extra module directories.
	// ModuleDirectory itself is unchanged, so include paths and the rules file stay where they are.
	void RestrictToAllowList()
	{
		FieldInfo Field = typeof(ModuleRules).GetField("DirectoriesForModuleSubClasses", BindingFlags.NonPublic | BindingFlags.Instance);
		Dictionary<Type, DirectoryReference> Dirs = Field == null ? null : Field.GetValue(this) as Dictionary<Type, DirectoryReference>;
		if (Dirs == null)
		{
			throw new Exception("SpaceGodCore.Build.cs: this UnrealBuildTool has no ModuleRules.DirectoriesForModuleSubClasses; " +
				"set PortingAllowList to null (compile the whole module) or update RestrictToAllowList.");
		}
		DirectoryReference Root = new DirectoryReference(ModuleDirectory);
		foreach (Type Key in new List<Type>(Dirs.Keys))
		{
			if (Dirs[Key] == Root) Dirs[Key] = DirectoryReference.Combine(Root, PortingAllowList[0]);
		}
		for (int i = 1; i < PortingAllowList.Length; i++)
		{
			ConditionalAddModuleDirectory(DirectoryReference.Combine(Root, PortingAllowList[i]));
		}
	}
}
