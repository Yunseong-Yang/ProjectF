using UnrealBuildTool;

public class ProjectFEditor : ModuleRules
{
	public ProjectFEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateDependencyModuleNames.AddRange(new[] {
			"Core", "CoreUObject", "Engine", "ProjectF", "UnrealEd", "AssetRegistry", "AssetTools",
			"StateTreeModule", "StateTreeEditorModule", "GameplayStateTreeModule", "AIModule",
			"Kismet", "KismetCompiler", "NavigationSystem", "Json", "JsonUtilities", "PropertyBindingUtils", "AnimGraph", "AnimGraphRuntime", "PhysicsCore", "BlueprintGraph"
		});
	}
}
