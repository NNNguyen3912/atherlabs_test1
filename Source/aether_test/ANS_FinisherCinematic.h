#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "CombatCinematicTypes.h"
#include "ANS_FinisherCinematic.generated.h"

/** Opens the E-finisher camera window before the confirmed impact notify. */
UCLASS(meta = (DisplayName = "Finisher Cinematic Window"))
class AETHER_TEST_API UANS_FinisherCinematic : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	/**
	 * Drag the notify length to choose the camera interval. Shorter window + the
	 * same OrbitDegrees rotates faster; a longer window rotates slower.
	 */
	UPROPERTY(EditAnywhere, Category = "Cinematic", meta = (ShowOnlyInnerProperties))
	FFinisherCinematicSettings Settings;

	virtual FString GetNotifyName_Implementation() const override
	{
		return TEXT("FinisherCinematicWindow");
	}

	virtual void NotifyBegin(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
};
