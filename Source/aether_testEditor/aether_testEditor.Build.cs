using UnrealBuildTool;

public class aether_testEditor : ModuleRules
{
	public aether_testEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivatePCHHeaderFile = "aether_testEditor.h";

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Niagara",
			"NiagaraAnimNotifies",
			"UnrealEd",
			"BlueprintGraph",
			"LevelEditor",
			"aether_test"
		});
	}
}
