#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EnemyBlueprintAutomationLibrary.generated.h"

/** Editor-only, idempotent repair for the player-reference guard in BP_Enemy's AI Tick graph. */
UCLASS()
class AETHER_TESTEDITOR_API UEnemyBlueprintAutomationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Inserts: bAttacking False -> Branch(IsValid(GetPlayerCharacter)) -> distance/MoveTo branch.
	 * The false path intentionally does nothing, preventing Accessed None in SIE and menu worlds.
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Editor")
	static FString ApplyEnemyP0PlayerValidGuard();

	/** Prevents the Tick-driven enemy attack from interrupting hit/launch reaction montages. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Editor")
	static FString ApplyEnemyReactionMontageGuard();

	/** Starts a normal in-process PIE session, including local PlayerController and screen HUD. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Editor")
	static FString StartNormalPIE();

	UFUNCTION(BlueprintCallable, Category = "Combat|Editor")
	static FString EndPlaySession();

	/** Configures the D3 montage with four deterministic, non-overlapping hit windows. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Editor")
	static FString ConfigureDiveD3HitWindows();

	/** Diagnostic dump of a Blueprint EventGraph, used only while repairing authored wiring. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Editor")
	static FString DumpBlueprintEventGraph(const FString& BlueprintAssetPath);

	/** Applies the resource contract to authored player montages: E4 x 12.5, normal hit +8. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Editor")
	static FString ConfigurePlayerStaminaHitWindows();

	/** Removes the obsolete IA_Posion debug event from the player EventGraph. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Editor")
	static FString DisablePlayerPoisonDebugInput();

	/** Removes the legacy per-input stamina payment from AdvanceSkill; montage hit windows own E cost. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Editor")
	static FString FixPlayerSkillStaminaGraph();

	/** Restores a readable wind-up to AM_Enemy_Attack and aligns its damaging window with contact. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Editor")
	static FString ConfigureEnemyAttackTelegraph();

	/** Wires confirmed-hit Niagara VFX and the S7 finisher notify into authored player montages. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Editor")
	static FString ConfigureCombatVFXAndFinisher();
};
