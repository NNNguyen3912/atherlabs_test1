#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "CombatAttributeSet.h"
#include "CombatCinematicTypes.h"
#include "CombatCharacterBase.generated.h"

class UGameplayAbility;
class UGameplayEffect;
class UInputAction;
class UAnimMontage;
class UAnimSequenceBase;
class UCameraShakeBase;
class UCombatPlayerHUDWidget;
class ACombatWaveSpawner;
class ACombatCharacterBase;
class APlayerController;
struct FOnAttributeChangeData;
struct FInputActionValue;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCombatAttributeChanged, float, NewValue, float, MaxValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCombatCharacterDied, ACombatCharacterBase*, DeadCharacter);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCombatComboChanged, int32, NewComboCount);

/**
 * Nhan vat nen co GAS: dung chung cho player va enemy.
 * ASC dat truc tiep tren Character (du an single-player).
 */
UCLASS()
class AETHER_TEST_API ACombatCharacterBase : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ACombatCharacterBase();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return ASC; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS")
	TObjectPtr<UAbilitySystemComponent> ASC;

	UPROPERTY()
	TObjectPtr<UCombatAttributeSet> Attributes;

	/** Ability cap san khi spawn — khai bao trong Blueprint con */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

	/** Effect chay luc khoi tao (init chi so, hoi stamina...) — khai bao trong Blueprint con */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS")
	TArray<TSubclassOf<UGameplayEffect>> StartupEffects;

	/** Enhanced Input action used to restart the completed test wave run. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Input")
	TObjectPtr<UInputAction> ResetWavesInputAction;

	/** GE Instant tru stamina, magnitude SetByCaller tag Data.StaminaCost — gan GE_StaminaCost trong BP con */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS")
	TSubclassOf<UGameplayEffect> StaminaCostEffect;

	/** GE Instant tru HP nan nhan, magnitude SetByCaller tag Data.Damage — gan GE_Damage trong BP con */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "GAS")
	TSubclassOf<UGameplayEffect> DamageEffect;

	/** Bao khi Health/Stamina doi (NewValue, MaxValue) — HUD va health bar enemy bind vao day */
	UPROPERTY(BlueprintAssignable, Category = "GAS")
	FCombatAttributeChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable, Category = "GAS")
	FCombatAttributeChanged OnStaminaChanged;

	/** Phat dung mot lan ngay khi Health cham 0. Wave spawner dung event nay, khong can cho corpse Destroy. */
	UPROPERTY(BlueprintAssignable, Category = "Combat|Death")
	FCombatCharacterDied OnCharacterDied;

	/** Combo duoc dem o phia player sau moi cu danh trung dich. */
	UPROPERTY(BlueprintAssignable, Category = "Combat|UI")
	FCombatComboChanged OnComboChanged;

	/** True sau khi Health cham 0 — moi logic combat phai ne */
	UPROPERTY(BlueprintReadOnly, Category = "GAS")
	bool bIsDead = false;

	/** Danh Target: apply DamageEffect cua MINH len ASC cua no. ANS_MeleeHitbox goi ham nay.
	 *  Tra true CHI khi damage thuc su duoc apply (2 ben con song, effect da wire) —
	 *  moi hieu ung an theo (poison, launch, hit react) phai gate bang gia tri nay. */
	UFUNCTION(BlueprintCallable, Category = "GAS")
	bool ApplyDamageToTarget(ACombatCharacterBase* Target, float Damage, bool bPlayHitReaction = true);

	/** BP override: chet (ragdoll, bao wave spawner, disable input...). Chi ban 1 lan. */
	UFUNCTION(BlueprintImplementableEvent, Category = "GAS")
	void OnDeath();

	/** Montage phan ung mac dinh. Gan tren BP_Enemy; de trong neu character khong can hit react. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Reaction")
	TObjectPtr<UAnimMontage> HitReactionMontage;

	/** Montage rieng khi bi launcher hat len. De trong neu character khong can animation nay. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Reaction")
	TObjectPtr<UAnimMontage> LaunchReactionMontage;

	/** Fallback sequence used to create a short runtime montage when no authored launch montage is assigned yet. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Reaction")
	TObjectPtr<UAnimSequenceBase> LaunchReactionAnimation;

	/** Cho ANS_MeleeHitbox goi khi launcher danh trung. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Reaction")
	void ApplyCombatLaunch(float LaunchZ);

	/** Apply a normalized horizontal impulse and optional lift without requiring a new ability. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Reaction")
	void ApplyCombatKnockback(FVector Direction, float HorizontalStrength, float LiftZ = 0.f);

	/** Play the native hit shake only for a local player camera. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Camera")
	void PlayCombatCameraShake(float Scale = 1.f);

	/** Starts a notify-authored camera orbit and global slow-motion window. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Camera")
	void BeginFinisherCinematic(float Duration, const FFinisherCinematicSettings& Settings);

	/** Restores camera and global time dilation after the notify or an interruption. */
	UFUNCTION(BlueprintCallable, Category = "Combat|Camera")
	void EndFinisherCinematic();

	UFUNCTION(BlueprintPure, Category = "Combat|Camera")
	bool IsFinisherCinematicActive() const { return bFinisherCinematicActive; }

	/** BP callback de them VFX/camera shake sau khi phan ung hit mac dinh da chay. */
	UFUNCTION(BlueprintImplementableEvent, Category = "GAS")
	void OnMeleeHitReceived(ACombatCharacterBase* Attacker);

	/** Blueprint co the them VFX/sound sau khi montage launcher bat dau. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Combat|Reaction")
	void OnCombatLaunched(float LaunchZ);

	/**
	 * Goi truoc moi don danh: du stamina thi tru va tra true, thieu thi khong tru va tra false.
	 * BP: Branch truoc Play Montage.
	 */
	UFUNCTION(BlueprintCallable, Category = "GAS")
	bool TryPayStamina(float Cost);

	/** Skill gate: requires one complete four-hit E combo worth of stamina. */
	UFUNCTION(BlueprintCallable, Category = "GAS")
	bool TryPayFullStamina();

	/** Restores a bounded amount of stamina after a confirmed player hit. */
	UFUNCTION(BlueprintCallable, Category = "GAS")
	void RestoreStamina(float Amount);

	/** Tunable resource budget for one four-hit E combo. Full stamina therefore allows two uses. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Stamina", meta = (ClampMin = "0.0"))
	float SkillComboTotalCost = 50.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Stamina", meta = (ClampMin = "1"))
	int32 SkillComboHitCount = 4;

	UFUNCTION(BlueprintPure, Category = "Combat|Stamina")
	float GetSkillComboHitCost() const;

	/** Apply 1 GE len chinh minh (poison DoT, buff...). Tra handle de remove som neu can. */
	UFUNCTION(BlueprintCallable, Category = "GAS")
	FActiveGameplayEffectHandle ApplyEffectToSelf(TSubclassOf<UGameplayEffect> EffectClass, float Level = 1.f);

	UFUNCTION(BlueprintPure, Category = "GAS")
	float GetHealth() const;

	UFUNCTION(BlueprintPure, Category = "GAS")
	float GetMaxHealth() const;

	UFUNCTION(BlueprintPure, Category = "GAS")
	float GetStamina() const;

	UFUNCTION(BlueprintPure, Category = "GAS")
	float GetMaxStamina() const;

	UFUNCTION(BlueprintPure, Category = "Combat|UI")
	int32 GetComboCount() const { return ComboCount; }

	/** Internal guard used by enemy melee notifies so a Tick-driven Blueprint cannot deal damage every montage loop. */
	bool CanDealMeleeDamage(float Cooldown) const;
	void StartMeleeDamageCooldown(float Cooldown);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_Controller() override;

	void InitAbilitySystem();
	/** Blueprints created before the AttributeSet defaults existed can serialize Health=0.
	 *  Repair only missing values at spawn so a live player never becomes silently dead on first hit. */
	void EnsureInitialAttributes();
	void InitializePlayerStartingStamina();

	void HandleHealthChanged(const FOnAttributeChangeData& Data);
	void HandleStaminaChanged(const FOnAttributeChangeData& Data);
	void PlayHitReaction();
	void HandleCombatDeath();
	void PlayLaunchReaction();
	void RegisterConfirmedHit();
	void ResetCombo();
	void CreatePlayerHUDIfNeeded();
	void UpdateCombatFacing(float DeltaSeconds);
	void UpdateCombatCamera(float DeltaSeconds);
	void UpdateFinisherCinematic(float DeltaSeconds);
	void HandleResetWavesInput(const FInputActionValue& ActionValue);
	ACombatCharacterBase* FindNearestCombatTarget() const;

	/** Keeps an optional world-space/screen-space HPBar widget in sync with GAS. */
	void UpdateHealthWidgets(float NewHealth, float NewMaxHealth);

	/** World time at which this character may deal the next cooldown-protected melee hit. */
	float NextMeleeDamageTime = 0.f;

	/** Exact authored or dynamic montage started by the latest launcher. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveLaunchReactionMontage;

	/** Opt-in native death hardening. Bat tren BP_Enemy de Tick AI khong the chase sau khi chet. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Death")
	bool bStopMovementOnDeath = false;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Death")
	bool bRagdollOnDeath = false;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Death")
	bool bHideHealthWidgetsOnDeath = false;

	/** 0 = Blueprint/level tu xu ly despawn. Gia tri duong tu dong destroy corpse sau so giay nay. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Death", meta = (ClampMin = "0.0"))
	float DeathDespawnDelay = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|UI")
	TSubclassOf<UCombatPlayerHUDWidget> PlayerHUDClass;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|UI", meta = (ClampMin = "0.1"))
	float ComboResetDelay = 2.f;

	/** Soft lock-on: while an attack montage is active, turn toward one nearby living enemy. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Targeting")
	bool bAutoFaceNearestEnemy = true;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Targeting", meta = (ClampMin = "100.0"))
	float AutoFaceRange = 1600.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Targeting", meta = (ClampMin = "1.0"))
	float AutoFaceTurnSpeed = 14.f;

	/** Uses the existing Blueprint spring arm/camera; no replacement camera component is created. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Camera")
	bool bCombatCameraEnabled = true;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Camera", meta = (ClampMin = "0.0"))
	float CombatCameraArmLength = 520.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Camera", meta = (ClampMin = "1.0"))
	float CombatCameraInterpSpeed = 7.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Camera", meta = (ClampMin = "1.0", ClampMax = "179.0"))
	float CombatCameraFOV = 86.f;

	/** Raises the camera's look/tracing point above the capsule center so combat frames the torso instead of the hips. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Camera", meta = (ClampMin = "-200.0", ClampMax = "200.0"))
	float CombatCameraTargetOffsetZ = 40.f;

	/** SpringArm follow damping used to keep jumps, root motion, and fast turns from being copied 1:1 into the camera. */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Camera", meta = (ClampMin = "1.0"))
	float CombatCameraLagSpeed = 8.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Camera", meta = (ClampMin = "1.0"))
	float CombatCameraRotationLagSpeed = 10.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Camera", meta = (ClampMin = "0.0"))
	float CombatCameraLagMaxDistance = 90.f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat|Camera|Finisher")
	bool bFinisherCinematicEnabled = true;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Combat|Targeting")
	TObjectPtr<ACombatCharacterBase> CurrentCombatTarget;

	UPROPERTY(BlueprintReadOnly, Category = "Combat|UI")
	int32 ComboCount = 0;

	UPROPERTY(Transient)
	TObjectPtr<UCombatPlayerHUDWidget> PlayerHUD;

	UPROPERTY(Transient)
	float DefaultCameraArmLength = 0.f;

	UPROPERTY(Transient)
	float DefaultCameraFOV = 90.f;

	UPROPERTY(Transient)
	bool bCameraDefaultsCaptured = false;

	UPROPERTY(Transient)
	bool bCameraFollowTuningApplied = false;

	bool bFinisherCinematicActive = false;

	float FinisherCinematicElapsed = 0.f;
	float FinisherCinematicDuration = 0.45f;
	float FinisherOriginalGlobalTimeDilation = 1.f;
	bool bFinisherChangedGlobalTimeDilation = false;
	FFinisherCinematicSettings ActiveFinisherSettings;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> FinisherMontage;

	TWeakObjectPtr<APlayerController> FinisherPlayerController;
	FRotator FinisherStartControlRotation = FRotator::ZeroRotator;
	FRotator FinisherStartBoomRotation = FRotator::ZeroRotator;
	FVector FinisherStartTargetOffset = FVector::ZeroVector;
	float FinisherStartArmLength = 0.f;
	float FinisherStartFOV = 90.f;
	bool bFinisherUsesControlRotation = false;

	FTimerHandle ComboResetTimer;

	/** BeginPlay may run before possession; this prevents a later possession callback from resetting combat stamina. */
	UPROPERTY(Transient)
	bool bPlayerStartingStaminaApplied = false;
};
