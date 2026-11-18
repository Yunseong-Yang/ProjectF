// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ProjectF : ModuleRules
{
	public ProjectF(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"NavigationSystem",
			"GameplayTags",
			"NetCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "RHI" });

		PublicIncludePaths.AddRange(new string[] {
			"ProjectF",
			"ProjectF/Variant_Horror",
			"ProjectF/Variant_Horror/UI",
			"ProjectF/Variant_Shooter",
			"ProjectF/Variant_Shooter/AI",
			"ProjectF/Variant_Shooter/UI",
			"ProjectF/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
