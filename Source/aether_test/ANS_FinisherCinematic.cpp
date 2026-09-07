#include "ANS_FinisherCinematic.h"

#include "CombatCharacterBase.h"
#include "Components/SkeletalMeshComponent.h"

void UANS_FinisherCinematic::NotifyBegin(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (ACombatCharacterBase* Character = MeshComp ? Cast<ACombatCharacterBase>(MeshComp->GetOwner()) : nullptr)
	{
		if (Character->IsPlayerControlled() && !Character->IsFinisherCinematicActive())
		{
			Character->BeginFinisherCinematic(TotalDuration, Settings);
		}
	}
}

void UANS_FinisherCinematic::NotifyEnd(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (ACombatCharacterBase* Character = MeshComp ? Cast<ACombatCharacterBase>(MeshComp->GetOwner()) : nullptr)
	{
		if (Character->IsPlayerControlled() && Character->IsFinisherCinematicActive())
		{
			// A whiff still restores the camera when the authored window ends;
			// a confirmed hit may already have ended it, making this a no-op.
			Character->EndFinisherCinematic();
		}
	}
}
