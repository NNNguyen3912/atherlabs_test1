#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CombatWaveSpawner.generated.h"

class ACombatCharacterBase;
class ATargetPoint;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCombatWaveEvent, int32, WaveNumber);

USTRUCT(BlueprintType)
struct FCombatWaveDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave")
	TSubclassOf<ACombatCharacterBase> EnemyClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave", meta = (ClampMin = "1"))
	int32 EnemyCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave", meta = (ClampMin = "0.0"))
	float SpawnInterval = 0.6f;
};

/**
 * Spawns each configured wave without Tick polling. Enemy logical death advances state immediately;
 * OnDestroyed is a deduplicated safety fallback when something removes an enemy outside combat damage.
 */
UCLASS()
class AETHER_TEST_API ACombatWaveSpawner : public AActor
{
	GENERATED_BODY()

public:
	ACombatWaveSpawner();

	/** Editable on the placed actor and BP child. World Partition external actors require EditAnywhere here. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave")
	TArray<TObjectPtr<ATargetPoint>> SpawnPoints;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave")
	TArray<FCombatWaveDefinition> Waves;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave", meta = (ClampMin = "0.0"))
	float DelayBetweenWaves = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave", meta = (ClampMin = "0.0"))
	float SpawnHeightOffset = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wave")
	bool bAutoStart = true;

	UPROPERTY(BlueprintReadOnly, Category = "Wave")
	int32 CurrentWaveIndex = INDEX_NONE;

	UPROPERTY(BlueprintAssignable, Category = "Wave")
	FCombatWaveEvent OnWaveStarted;

	UPROPERTY(BlueprintAssignable, Category = "Wave")
	FCombatWaveEvent OnWaveCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Wave")
	FCombatWaveEvent OnAllWavesCompleted;

	UFUNCTION(BlueprintCallable, Category = "Wave")
	void StartWaves();

	/** Clears the current run and starts the configured waves again from wave 1. */
	UFUNCTION(BlueprintCallable, Category = "Wave")
	void ResetWaves();

	/** True once the final configured wave has no remaining enemies. */
	UFUNCTION(BlueprintPure, Category = "Wave")
	bool HasCompletedAllWaves() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void StartCurrentWave();
	void SpawnNextEnemy();
	void EvaluateWaveCompletion();
	void AdvanceToNextWave();
	FTransform GetSpawnTransform(int32 SpawnOrdinal) const;

	UFUNCTION()
	void HandleEnemyDied(ACombatCharacterBase* DeadCharacter);

	UFUNCTION()
	void HandleEnemyDestroyed(AActor* DestroyedActor);

	void RemoveActiveEnemy(AActor* EnemyActor);

	int32 SpawnedThisWave = 0;
	bool bRunning = false;
	bool bWaveCompletionQueued = false;
	TSet<TWeakObjectPtr<AActor>> ActiveEnemies;
	FTimerHandle SpawnTimer;
	FTimerHandle NextWaveTimer;
};
