// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class aether_test : ModuleRules
{
	public aether_test(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Keep runtime gameplay code on a compact project PCH. The project also has an editor-only
		// Blueprint repair module; without this explicit header UBT may force the huge UnrealEd PCH
		// into every combat source file in editor builds.
		PrivatePCHHeaderFile = "aether_test.h";
	
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayAbilities",
			"UMG",
			"SlateCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GameplayTags",
			"GameplayTasks",
			"EnhancedInput",
			"Niagara",
			"Slate"
		});
	}
}
