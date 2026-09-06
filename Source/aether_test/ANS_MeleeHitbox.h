#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayEffect.h"
#include "Templates/SubclassOf.h"
#include "ANS_MeleeHitbox.generated.h"

class ACombatCharacterBase;
class UNiagaraSystem;

UENUM(BlueprintType)
enum class ECombatKnockbackDirection : uint8
{
	AwayFromAttacker UMETA(DisplayName = "Away From Attacker"),
	AttackerForward UMETA(DisplayName = "Attacker Forward")
};

/**
 * Hitbox don danh: dat dai notify quanh frame cham cua montage.
 * Sweep sphere theo socket giua 2 tick — moi nan nhan chi dinh 1 lan cho moi lan phat.
 * LUU Y: notify object DUNG CHUNG giua moi character phat cung montage,
 * nen state per-activation phai key theo MeshComp (khong duoc de member tran lan).
 */
UCLASS(meta = (DisplayName = "Melee Hitbox"))
class AETHER_TEST_API UANS_MeleeHitbox : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	/** Socket tren mesh lam tam hitbox (hand_r, hand_l, foot_r...) */
	UPROPERTY(EditAnywhere, Category = "Hitbox")
	FName SocketName = TEXT("hand_r");

	UPROPERTY(EditAnywhere, Category = "Hitbox")
	float Radius = 60.f;

	UPROPERTY(EditAnywhere, Category = "Hitbox")
	float Damage = 10.f;

	/** >0 thi hat nan nhan len (launcher L2 dung 650) */
	UPROPERTY(EditAnywhere, Category = "Hitbox")
	float LaunchZ = 0.f;

	/** Horizontal displacement applied after a successful hit. Zero preserves the old behavior. */
	UPROPERTY(EditAnywhere, Category = "Hitbox|Reaction", meta = (ClampMin = "0.0"))
	float HorizontalKnockback = 0.f;

	/** Optional vertical lift applied together with horizontal knockback. */
	UPROPERTY(EditAnywhere, Category = "Hitbox|Reaction", meta = (ClampMin = "0.0"))
	float KnockbackLiftZ = 0.f;

	/** Direction used by horizontal knockback: away from the attacker or along its forward axis. */
	UPROPERTY(EditAnywhere, Category = "Hitbox|Reaction")
	ECombatKnockbackDirection KnockbackDirection = ECombatKnockbackDirection::AwayFromAttacker;

	/** Disable the default hit-reaction montage for multi-hit or scripted impact windows. */
	UPROPERTY(EditAnywhere, Category = "Hitbox|Reaction")
	bool bPlayHitReaction = true;

	/** Camera shake intensity after a confirmed hit. Zero disables the shake for this window. */
	UPROPERTY(EditAnywhere, Category = "Hitbox|Feedback", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float CameraShakeScale = 0.35f;

	/** Optional Niagara effect spawned only after ApplyDamageToTarget confirms real HP loss. */
	UPROPERTY(EditAnywhere, Category = "Hitbox|Feedback")
	TObjectPtr<UNiagaraSystem> ConfirmedHitVFX;

	UPROPERTY(EditAnywhere, Category = "Hitbox|Feedback", meta = (ClampMin = "0.01"))
	float ConfirmedHitVFXScale = 1.f;

	/** Starts the short E4/S7 finisher camera after the first confirmed hit in this window. */
	UPROPERTY(EditAnywhere, Category = "Hitbox|Feedback")
	bool bStartFinisherCinematic = false;

	/** Optional one-shot VFX placed at the attacker's feet when the finisher starts. */
	UPROPERTY(EditAnywhere, Category = "Hitbox|Feedback")
	TObjectPtr<UNiagaraSystem> FinisherVFX;

	/** Optional stamina cost paid when this authored hit window starts (E uses 12.5 x 4). */
	UPROPERTY(EditAnywhere, Category = "Hitbox|Stamina", meta = (ClampMin = "0.0"))
	float StaminaCostOnBegin = 0.f;

	/** Optional minimum stamina required before this window can start (S4 uses 50). */
	UPROPERTY(EditAnywhere, Category = "Hitbox|Stamina", meta = (ClampMin = "0.0"))
	float MinimumStaminaOnBegin = 0.f;

	/** Stamina restored only after this window confirms real damage on a player-controlled attacker. */
	UPROPERTY(EditAnywhere, Category = "Hitbox|Stamina", meta = (ClampMin = "0.0"))
	float StaminaGainOnConfirmedHit = 0.f;

	/** GE phu ap them len nan nhan khi trung (enemy gan GE_PoisonDoT de doc player) */
	UPROPERTY(EditAnywhere, Category = "Hitbox")
	TSubclassOf<UGameplayEffect> ExtraEffectOnHit;

	/** Enemy bat len de khong danh trung dong loai — chi trung player */
	UPROPERTY(EditAnywhere, Category = "Hitbox")
	bool bOnlyHitPlayers = false;

	/** Only used when bOnlyHitPlayers is set. Stops Tick-driven enemy attacks from looping without a recovery beat. */
	UPROPERTY(EditAnywhere, Category = "Hitbox", meta = (ClampMin = "0.0"))
	float AttackCooldown = 1.25f;

	UPROPERTY(EditAnywhere, Category = "Hitbox")
	bool bDrawDebug = false;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		float FrameDeltaTime, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

private:
	struct FHitboxState
	{
		TSet<TWeakObjectPtr<AActor>> HitActors;
		FVector PrevPos = FVector::ZeroVector;
	};

	/** State rieng cho tung mesh dang phat (key weak — khong giu song component) */
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FHitboxState> ActiveStates;

	FVector GetHitboxLocation(const USkeletalMeshComponent* MeshComp) const;
};
