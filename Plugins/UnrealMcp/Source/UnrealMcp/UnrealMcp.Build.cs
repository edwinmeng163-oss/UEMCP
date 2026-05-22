using UnrealBuildTool;

public class UnrealMcp : ModuleRules
{
	public UnrealMcp(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Disable unity build for this module. Several per-file helpers
		// (GetTaskRoot, GetEditorAssetSubsystem, GetActivityLogRoot,
		// IsEditorPlaying, MakePieBlockedResult, MakeErrorObject, etc.)
		// live in per-file anonymous namespaces and collide when UBT
		// concatenates the .cpp files. UE 5.7 adaptive unity exclusion
		// hid the issue against the example host but UE 5.6 dev-host
		// builds against UEvolve.uproject surfaced redefinition errors.
		// Refactoring all helpers to unique names is invasive; per-file
		// compilation removes the risk entirely with only modest
		// full-build cost (incremental builds unaffected).
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"AIModule",
			"ApplicationCore",
			"AssetRegistry",
			"AssetTools",
			"BlueprintGraph",
			"ContentBrowser",
			"CoreUObject",
			"DeveloperSettings",
			"DesktopPlatform",
			"Engine",
			"FileUtilities",
			"HTTP",
			"HTTPServer",
			"InputCore",
			"Json",
			"JsonUtilities",
			"Kismet",
			"KismetCompiler",
			"MovieScene",
			"Projects",
			"PythonScriptPlugin",
			"Settings",
			"Slate",
			"SlateCore",
			"Sockets",
			"ToolMenus",
			"UMG",
			"UMGEditor",
			"UnrealEd",
			"WebSockets"
		});
	}
}
