#include "ANS_MeleeHitbox.h"
#include "CombatCharacterBase.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/EngineTypes.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystem.h"

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

		const bool bHasUnifiedVFXEntries = ConfirmedHitVFXEntries.Num() > 0;
		const bool bHasLegacyVFXList = ConfirmedHitParticleVFXList.Num() > 0
			|| ConfirmedHitVFXList.Num() > 0;
		const bool bHasLegacyVFXData = bHasLegacyVFXList
			|| ConfirmedHitParticleVFX || ConfirmedHitVFX;
		if (bHasUnifiedVFXEntries || bHasLegacyVFXData)
		{
			FVector ImpactLocation = Hit.ImpactPoint;
			if (ImpactLocation.IsNearlyZero())
			{
				ImpactLocation = Hit.Location;
			}
			if (ImpactLocation.IsNearlyZero())
			{
				ImpactLocation = Victim->GetActorLocation();
			}

			FVector ImpactNormal = Hit.Normal;
			if (ImpactNormal.IsNearlyZero())
			{
				ImpactNormal = (Attacker->GetActorLocation() - ImpactLocation).GetSafeNormal();
			}
			if (ImpactNormal.IsNearlyZero())
			{
				ImpactNormal = FVector::UpVector;
			}

			const float GlobalVFXScale = FMath::Max(0.01f, ConfirmedHitVFXScale);
			const FVector LegacyVFXScale(GlobalVFXScale);
			const FRotator HitNormalRotation = FRotationMatrix::MakeFromZ(ImpactNormal).Rotator();
			auto SpawnParticleVFX = [&](UParticleSystem* Effect, const FVector& Location,
				const FRotator& Rotation, const FVector& Scale)
			{
				if (!Effect)
				{
					return;
				}
				UGameplayStatics::SpawnEmitterAtLocation(
					MeshComp, Effect, Location, Rotation,
					Scale, true, EPSCPoolMethod::AutoRelease, true);
			};
			auto SpawnNiagaraVFX = [&](UNiagaraSystem* Effect, const FVector& Location,
				const FRotator& Rotation, const FVector& Scale)
			{
				if (!Effect)
				{
					return;
				}
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(
					MeshComp, Effect, Location, Rotation,
					Scale, true, true);
			};

			if (bHasUnifiedVFXEntries)
			{
				for (const FConfirmedHitVFXEntry& Entry : ConfirmedHitVFXEntries)
				{
					UFXSystemAsset* EffectAsset = Entry.Effect.Get();
					if (!EffectAsset)
					{
						continue;
					}

					const FRotator BaseRotation = Entry.bAlignToHitNormal
						? HitNormalRotation
						: FRotator::ZeroRotator;
					const FQuat BaseQuaternion = BaseRotation.Quaternion();
					const FVector EntryLocation = ImpactLocation + BaseQuaternion.RotateVector(Entry.LocationOffset);
					const FRotator EntryRotation =
						(BaseQuaternion * Entry.RotationOffset.Quaternion()).Rotator();
					const FVector EntryScale(
						FMath::Max(0.01f, Entry.Scale.X),
						FMath::Max(0.01f, Entry.Scale.Y),
						FMath::Max(0.01f, Entry.Scale.Z));
					if (UNiagaraSystem* NiagaraEffect = Cast<UNiagaraSystem>(EffectAsset))
					{
						SpawnNiagaraVFX(NiagaraEffect, EntryLocation, EntryRotation, EntryScale);
					}
					else if (UParticleSystem* ParticleEffect = Cast<UParticleSystem>(EffectAsset))
					{
						SpawnParticleVFX(ParticleEffect, EntryLocation, EntryRotation, EntryScale);
					}
				}
			}
			else if (bHasLegacyVFXList)
			{
				for (const TObjectPtr<UParticleSystem>& Effect : ConfirmedHitParticleVFXList)
				{
					SpawnParticleVFX(Effect.Get(), ImpactLocation, HitNormalRotation, LegacyVFXScale);
				}
				for (const TObjectPtr<UNiagaraSystem>& Effect : ConfirmedHitVFXList)
				{
					SpawnNiagaraVFX(Effect.Get(), ImpactLocation, HitNormalRotation, LegacyVFXScale);
				}
			}
			else if (ConfirmedHitParticleVFX)
			{
				SpawnParticleVFX(ConfirmedHitParticleVFX, ImpactLocation, HitNormalRotation, LegacyVFXScale);
			}
			else
			{
				SpawnNiagaraVFX(ConfirmedHitVFX, ImpactLocation, HitNormalRotation, LegacyVFXScale);
			}
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
