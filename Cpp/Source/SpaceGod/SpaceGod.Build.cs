using System;
using System.Diagnostics;
using System.IO;
using UnrealBuildTool;

// The C++ track's game module: Unity.SpaceGod's Runtime layer (UnitySpaceApp, camera, input, and later views, UI and
// rendering) on Unreal, driving SpaceGodCore.
public class SpaceGod : ModuleRules
{
	public SpaceGod(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateIncludePaths.Add(ModuleDirectory);

		// SpaceGodCore's headers are C++20 on Cs/Cs.h: C# exceptions are C++ exceptions and cs::As is a
		// dynamic_pointer_cast, so code that includes them needs both, as the Core module itself does.
		CppStandard = CppStandardVersion.Cpp20;
		bEnableExceptions = true;
		bUseRTTI = true;
		// Each ported file keeps its own file-local helpers (static ToF, anonymous-namespace caches) as the C# files
		// keep their private members: a unity build would merge them into one translation unit and collide.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "NetCore", "Slate", "SlateCore", "ProceduralMeshComponent", "ImageWrapper",
			"SpaceGodCore", // the C++ port of Unity.SpaceGod's Core (Cpp/Source/SpaceGodCore)
			// Views/Rendering (US-T30): meshes drawn as UDynamicMeshComponents, the plugin found for Resources, ShipModels.json.
			"GeometryCore", "GeometryFramework", "Projects", "Json", "ImageCore",
		});
		// UI/DisplayBackend (US-T56): the monitor's modes with their refresh rates (SpaceGodRender's SpaceGodDisplayModes.h)
		// and the rate set on the game's swap chain.
		PrivateDependencyModuleNames.AddRange(new[] { "SpaceGodRender", "RHI", "RenderCore", "ApplicationCore" });
		// US-T234: the shared --headless mode (Unreal.Shared's MindAtticShared plugin): Bootstrap and GameLaunch key the
		// game's own opt-ins (scripted input, no main menu) off FMindAtticHeadless::IsActive().
		PrivateDependencyModuleNames.Add("MindAtticHeadless");
		// US-T139: the BYOKey voice (GameVoice) calls the player's own LLM over HTTPS.
		PrivateDependencyModuleNames.Add("HTTP");
		// US-T248: the main menu's Host Co-op page lists this machine's addresses for the players joining it.
		PrivateDependencyModuleNames.Add("Sockets");
		// US-T245: the build number and short commit hash the main menu shows in its corner (BuildInfo.cpp).
		StampBuild();
	}

	/// US-T245: writes Intermediate/BuildStamp/SpaceGodBuildStamp.h (the commit count and short hash of the checkout) when
	/// it changed, so a new commit recompiles BuildInfo.cpp alone, and redoes the makefile when HEAD moves (so an
	/// incremental build picks the new commit up). No git, or no repository: BuildInfo says "unknown".
	void StampBuild()
	{
		string repo = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", ".."));
		string stampDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "Intermediate", "BuildStamp"));
		PrivateIncludePaths.Add(stampDir);
		string commit = Git(repo, "rev-parse --short HEAD") ?? "unknown";
		string count = Git(repo, "rev-list --count HEAD") ?? "";
		string text = "#pragma once\n// Written by SpaceGod.Build.cs (US-T245): the checkout this build was made from.\n"
			+ "#define SPACEGOD_BUILD_COMMIT \"" + commit + "\"\n#define SPACEGOD_BUILD_NUMBER \"" + count + "\"\n";
		try
		{
			Directory.CreateDirectory(stampDir);
			string file = Path.Combine(stampDir, "SpaceGodBuildStamp.h");
			if (!File.Exists(file) || File.ReadAllText(file) != text) File.WriteAllText(file, text);
		}
		catch (Exception) { } // another target's rules writing the same stamp at once: theirs is as good

		string gitDir = Path.Combine(repo, ".git");
		if (!Directory.Exists(gitDir)) return;
		string head = Path.Combine(gitDir, "HEAD");
		ExternalDependencies.Add(head);
		try
		{
			string h = File.ReadAllText(head).Trim();
			if (h.StartsWith("ref: "))
			{
				string branch = Path.Combine(gitDir, h.Substring(5).Replace('/', Path.DirectorySeparatorChar));
				if (File.Exists(branch)) ExternalDependencies.Add(branch);
			}
		}
		catch (Exception) { }
		string packed = Path.Combine(gitDir, "packed-refs");
		if (File.Exists(packed)) ExternalDependencies.Add(packed);
	}

	static string Git(string repo, string arguments)
	{
		try
		{
			var start = new ProcessStartInfo("git", arguments)
			{
				WorkingDirectory = repo, RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false, CreateNoWindow = true,
			};
			using (Process git = Process.Start(start))
			{
				string output = git.StandardOutput.ReadToEnd().Trim();
				if (!git.WaitForExit(10000)) return null;
				return git.ExitCode == 0 && output.Length > 0 ? output : null;
			}
		}
		catch (Exception) { return null; }
	}
}
