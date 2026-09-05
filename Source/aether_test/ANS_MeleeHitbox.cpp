#include "ANS_MeleeHitbox.h"
#include "CombatCharacterBase.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/EngineTypes.h"

FVector UANS_MeleeHitbox::GetHitboxLocation(const USkeletalMeshComponent* MeshComp) const
{
	if (MeshComp->DoesSocketExist(SocketName))
	{
		return MeshComp->GetSocketLocation(SocketName);
	}
	return MeshComp->GetComponentLocation();
}

void UANS_MeleeHitbox::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (!MeshComp)
	{
		return;
	}

	ACombatCharacterBase* Attacker = Cast<ACombatCharacterBase>(MeshComp->GetOwner());
	if (bOnlyHitPlayers && Attacker && !Attacker->CanDealMeleeDamage(AttackCooldown))
	{
		// BP_Enemy currently evaluates its attack branch on Tick. Abort the repeated montage at
		// the hit window so the enemy visibly returns to idle during its recovery interval.
		if (UAnimInstance* AnimInstance = MeshComp->GetAnimInstance())
		{
			AnimInstance->Montage_Stop(0.08f);
		}
		return;
	}

	if (Attacker && Attacker->IsPlayerControlled())
	{
		if (MinimumStaminaOnBegin > 0.f && Attacker->GetStamina() + KINDA_SMALL_NUMBER < MinimumStaminaOnBegin)
		{
			return;
		}
		if (StaminaCostOnBegin > 0.f && !Attacker->TryPayStamina(StaminaCostOnBegin))
		{
			return;
		}
	}

	FHitboxState& State = ActiveStates.FindOrAdd(MeshComp);
	State.HitActors.Reset();
	State.PrevPos = GetHitboxLocation(MeshComp);
}

void UANS_MeleeHitbox::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);
	if (!MeshComp)
	{
		return;
	}
	ACombatCharacterBase* Attacker = Cast<ACombatCharacterBase>(MeshComp->GetOwner());
	if (!Attacker) // preview trong Persona / owner khong phai combat character
	{
		return;
	}

	FHitboxState* State = ActiveStates.Find(MeshComp);
	if (!State)
	{
		return;
	}

	const FVector Start = State->PrevPos;
	const FVector End = GetHitboxLocation(MeshComp);
	State->PrevPos = End;

	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));
	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(Attacker);

	TArray<FHitResult> Hits;
	UKismetSystemLibrary::SphereTraceMultiForObjects(
		MeshComp, Start, End, Radius, ObjectTypes, false, IgnoreActors,
		bDrawDebug ? EDrawDebugTrace::ForDuration : EDrawDebugTrace::None,
		Hits, true);

	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		ACombatCharacterBase* Victim = Cast<ACombatCharacterBase>(HitActor);
		if (!Victim || Victim == Attacker || State->HitActors.Contains(HitActor))
		{
			continue;
		}
		if (bOnlyHitPlayers && !Victim->IsPlayerControlled())
		{
			continue;
		}
		State->HitActors.Add(HitActor);

		// Poison/launch chi di kem cu danh THANH CONG — attacker chet giua chung
		// hay nan nhan da chet thi khong duoc dinh gi them
		const bool bIsLauncher = LaunchZ > 0.f;
		if (!Attacker->ApplyDamageToTarget(Victim, Damage, !bIsLauncher && bPlayHitReaction))
		{
			continue;
		}
		Attacker->PlayCombatCameraShake(CameraShakeScale);
		Victim->PlayCombatCameraShake(CameraShakeScale);
		if (bOnlyHitPlayers)
		{
			Attacker->StartMeleeDamageCooldown(AttackCooldown);
		}
		if (ExtraEffectOnHit)
		{
			Victim->ApplyEffectToSelf(ExtraEffectOnHit);
		}
		if (StaminaGainOnConfirmedHit > 0.f && Attacker->IsPlayerControlled())
		{
			Attacker->RestoreStamina(StaminaGainOnConfirmedHit);
		}
		if (bIsLauncher && !Victim->bIsDead)
		{
			Victim->ApplyCombatLaunch(LaunchZ);
		}
		if (HorizontalKnockback > 0.f && !Victim->bIsDead)
		{
			FVector KnockbackDirectionVector = KnockbackDirection == ECombatKnockbackDirection::AttackerForward
				? Attacker->GetActorForwardVector()
				: Victim->GetActorLocation() - Attacker->GetActorLocation();
			KnockbackDirectionVector.Z = 0.f;
			Victim->ApplyCombatKnockback(KnockbackDirectionVector, HorizontalKnockback, KnockbackLiftZ);
		}
	}
}

void UANS_MeleeHitbox::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (MeshComp)
	{
		ActiveStates.Remove(MeshComp);
	}
	// don cac key da chet (character bi destroy giua chung)
	for (auto It = ActiveStates.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}
