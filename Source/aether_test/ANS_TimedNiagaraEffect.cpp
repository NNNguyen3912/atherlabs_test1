#include "ANS_TimedNiagaraEffect.h"

#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

UANS_TimedNiagaraEffect::UANS_TimedNiagaraEffect(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UANS_TimedNiagaraEffect::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (!ValidateParameters(MeshComp))
	{
		return;
	}

	if (TWeakObjectPtr<UNiagaraComponent>* Existing = ActiveEffects.Find(MeshComp))
	{
		if (UNiagaraComponent* ExistingComponent = Existing->Get())
		{
			ExistingComponent->DestroyComponent();
		}
		ActiveEffects.Remove(MeshComp);
	}

	UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAttached(
		Template,
		MeshComp,
		SocketName,
		LocationOffset,
		RotationOffset,
		Scale,
		EAttachLocation::KeepRelativeOffset,
		bDestroyAtEnd,
		ENCPoolMethod::None,
		true,
		true);
	if (!Component)
	{
		return;
	}

	if (bApplyRateScaleAsTimeDilation && Animation)
	{
		Component->SetCustomTimeDilation(Animation->RateScale);
	}
	ActiveEffects.Add(MeshComp, Component);
}

void UANS_TimedNiagaraEffect::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	if (TWeakObjectPtr<UNiagaraComponent>* Existing = ActiveEffects.Find(MeshComp))
	{
		if (UNiagaraComponent* Component = Existing->Get())
		{
			if (bDestroyAtEnd)
			{
				Component->DestroyComponent();
			}
			else
			{
				Component->Deactivate();
			}
		}
		ActiveEffects.Remove(MeshComp);
	}

	Super::NotifyEnd(MeshComp, Animation, EventReference);
}

FString UANS_TimedNiagaraEffect::GetNotifyName_Implementation() const
{
	return Template ? Template->GetName() : Super::GetNotifyName_Implementation();
}

bool UANS_TimedNiagaraEffect::ValidateParameters(const USkeletalMeshComponent* MeshComp) const
{
	return MeshComp && Template &&
		(MeshComp->DoesSocketExist(SocketName) || MeshComp->GetBoneIndex(SocketName) != INDEX_NONE);
}
