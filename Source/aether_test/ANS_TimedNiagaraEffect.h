#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_TimedNiagaraEffect.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UAnimSequenceBase;
class USkeletalMeshComponent;

/** Timed Niagara notify with an authored per-notify scale and socket transform. */
UCLASS(Blueprintable, meta = (DisplayName = "Combat Timed Niagara Effect"))
class AETHER_TEST_API UANS_TimedNiagaraEffect : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UANS_TimedNiagaraEffect(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Niagara System",
		meta = (DisplayName = "Niagara System", ToolTip = "The Niagara system to spawn for this notify state"))
	TObjectPtr<UNiagaraSystem> Template = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Niagara System",
		meta = (ToolTip = "The socket or bone to attach the system to", AnimNotifyBoneName = "true"))
	FName SocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Niagara System",
		meta = (ToolTip = "Offset from the socket or bone"))
	FVector LocationOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Niagara System",
		meta = (ToolTip = "Rotation offset from the socket or bone"))
	FRotator RotationOffset = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Niagara System",
		meta = (DisplayName = "Scale", ClampMin = "0.01"))
	FVector Scale = FVector::OneVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Niagara System")
	bool bApplyRateScaleAsTimeDilation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Niagara System",
		meta = (DisplayName = "Destroy Immediately"))
	bool bDestroyAtEnd = false;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

private:
	bool ValidateParameters(const USkeletalMeshComponent* MeshComp) const;

	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TWeakObjectPtr<UNiagaraComponent>> ActiveEffects;
};
