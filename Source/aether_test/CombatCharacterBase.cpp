#include "CombatCharacterBase.h"
#include "CombatPlayerHUDWidget.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Blueprint/UserWidget.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/ProgressBar.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameplayEffect.h"
#include "GameplayAbilitySpec.h"
#include "TimerManager.h"
#include "CombatCameraShake.h"

ACombatCharacterBase::ACombatCharacterBase()
{
	PrimaryActorTick.bCanEverTick = true;

	ASC = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("ASC"));
	Attributes = CreateDefaultSubobject<UCombatAttributeSet>(TEXT("Attributes"));
	PlayerHUDClass = UCombatPlayerHUDWidget::StaticClass();
}

void ACombatCharacterBase::BeginPlay()
{
	Super::BeginPlay();
	InitAbilitySystem();

	// Combat characters must not collapse the player's spring arm when they
	// move between the camera and its target. World geometry still blocks it.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	// BeginPlay can run before the local game viewport/subsystem is ready.
	// Defer the first attempt so AddToViewport has a valid screen target.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &ACombatCharacterBase::CreatePlayerHUDIfNeeded);
	}
}

void ACombatCharacterBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateCombatFacing(DeltaSeconds);
	UpdateCombatCamera(DeltaSeconds);
}

void ACombatCharacterBase::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (ActiveLaunchReactionMontage)
	{
		if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			AnimInstance->Montage_Stop(0.08f, ActiveLaunchReactionMontage);
		}
		ActiveLaunchReactionMontage = nullptr;
	}
}

void ACombatCharacterBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitializePlayerStartingStamina();
	CreatePlayerHUDIfNeeded();
}

void ACombatCharacterBase::OnRep_Controller()
{
	Super::OnRep_Controller();
	InitializePlayerStartingStamina();
	CreatePlayerHUDIfNeeded();
}

void ACombatCharacterBase::InitAbilitySystem()
{
	if (!ASC)
	{
		return;
	}

	ASC->InitAbilityActorInfo(this, this);

	ASC->GetGameplayAttributeValueChangeDelegate(Attributes->GetHealthAttribute())
		.AddUObject(this, &ACombatCharacterBase::HandleHealthChanged);
	ASC->GetGameplayAttributeValueChangeDelegate(Attributes->GetStaminaAttribute())
		.AddUObject(this, &ACombatCharacterBase::HandleStaminaChanged);

	EnsureInitialAttributes();
	InitializePlayerStartingStamina();

	for (const TSubclassOf<UGameplayEffect>& EffectClass : StartupEffects)
	{
		if (!EffectClass)
		{
			continue;
		}
		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		Context.AddSourceObject(this);
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(EffectClass, 1.f, Context);
		if (Spec.IsValid())
		{
			ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	}

	for (const TSubclassOf<UGameplayAbility>& AbilityClass : DefaultAbilities)
	{
		if (!AbilityClass)
		{
			continue;
		}
		ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
	}
}

void ACombatCharacterBase::EnsureInitialAttributes()
{
	if (!ASC || !Attributes)
	{
		return;
	}

	// Older child Blueprints can retain a serialized zero Health even though the
	// native AttributeSet constructor now defaults it to 100.  Do not overwrite
	// authored non-zero stats; only repair a missing value when the actor spawns.
	if (Attributes->GetMaxHealth() <= KINDA_SMALL_NUMBER)
	{
		ASC->SetNumericAttributeBase(Attributes->GetMaxHealthAttribute(), 100.f);
	}
	if (Attributes->GetHealth() <= KINDA_SMALL_NUMBER)
	{
		ASC->SetNumericAttributeBase(Attributes->GetHealthAttribute(), Attributes->GetMaxHealth());
	}
	if (Attributes->GetMaxStamina() <= KINDA_SMALL_NUMBER)
	{
		ASC->SetNumericAttributeBase(Attributes->GetMaxStaminaAttribute(), 100.f);
	}
	if (Attributes->GetStamina() <= KINDA_SMALL_NUMBER)
	{
		ASC->SetNumericAttributeBase(Attributes->GetStaminaAttribute(), Attributes->GetMaxStamina());
	}
}

void ACombatCharacterBase::InitializePlayerStartingStamina()
{
	if (bPlayerStartingStaminaApplied || !IsPlayerControlled() || !ASC || !Attributes)
	{
		return;
	}

	const float MaxStamina = Attributes->GetMaxStamina();
	if (MaxStamina <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	ASC->SetNumericAttributeBase(Attributes->GetStaminaAttribute(), MaxStamina * 0.5f);
	bPlayerStartingStaminaApplied = true;
}

bool ACombatCharacterBase::TryPayStamina(float Cost)
{
	if (Cost <= 0.f)
	{
		return true;
	}
	if (!ASC || !Attributes)
	{
		return false;
	}
	if (Attributes->GetStamina() + KINDA_SMALL_NUMBER < Cost)
	{
		return false;
	}

	if (!StaminaCostEffect)
	{
		ASC->SetNumericAttributeBase(Attributes->GetStaminaAttribute(), Attributes->GetStamina() - Cost);
		return true;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(StaminaCostEffect, 1.f, Context);
	if (!Spec.IsValid())
	{
		ASC->SetNumericAttributeBase(Attributes->GetStaminaAttribute(), Attributes->GetStamina() - Cost);
		return true;
	}
	// GE cong them gia tri am = tru stamina
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Data.StaminaCost")), -Cost);
	ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	return true;
}

bool ACombatCharacterBase::TryPayFullStamina()
{
	if (!ASC || !Attributes)
	{
		return false;
	}

	const float RequiredStamina = FMath::Max(0.f, SkillComboTotalCost);
	if (RequiredStamina <= KINDA_SMALL_NUMBER)
	{
		return true;
	}
	if (Attributes->GetStamina() + KINDA_SMALL_NUMBER < RequiredStamina)
	{
		return false;
	}

	// Spending is performed by each E hitbox window so interruption/whiffing does
	// not burn the entire combo up front. The old Blueprint node name is kept for
	// compatibility with the authored input graph.
	return true;
}

void ACombatCharacterBase::RestoreStamina(float Amount)
{
	if (Amount <= 0.f || !ASC || !Attributes)
	{
		return;
	}

	const float MaxStamina = Attributes->GetMaxStamina();
	ASC->SetNumericAttributeBase(
		Attributes->GetStaminaAttribute(),
		FMath::Clamp(Attributes->GetStamina() + Amount, 0.f, MaxStamina));
}

float ACombatCharacterBase::GetSkillComboHitCost() const
{
	return SkillComboHitCount > 0
		? FMath::Max(0.f, SkillComboTotalCost) / static_cast<float>(SkillComboHitCount)
		: FMath::Max(0.f, SkillComboTotalCost);
}

FActiveGameplayEffectHandle ACombatCharacterBase::ApplyEffectToSelf(TSubclassOf<UGameplayEffect> EffectClass, float Level)
{
	if (!ASC || !EffectClass)
	{
		return FActiveGameplayEffectHandle();
	}
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(EffectClass, Level, Context);
	if (!Spec.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}
	return ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}

bool ACombatCharacterBase::ApplyDamageToTarget(ACombatCharacterBase* Target, float Damage, bool bPlayHitReaction)
{
	if (!Target || Target->bIsDead || bIsDead || Damage <= 0.f)
	{
		return false;
	}
	if (!ASC || !DamageEffect)
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyDamageToTarget: DamageEffect chua duoc gan tren %s"), *GetName());
		return false;
	}
	UAbilitySystemComponent* TargetASC = Target->GetAbilitySystemComponent();
	if (!TargetASC)
	{
		return false;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);
	Context.AddInstigator(this, this);
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(DamageEffect, 1.f, Context);
	if (!Spec.IsValid())
	{
		return false;
	}
	// GE cong gia tri am vao Health = tru mau
	Spec.Data->SetSetByCallerMagnitude(
		FGameplayTag::RequestGameplayTag(TEXT("Data.Damage")), -Damage);
	const float HealthBefore = Target->GetHealth();
	ASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
	if (Target->GetHealth() >= HealthBefore - KINDA_SMALL_NUMBER)
	{
		// A notify must not count a capsule overlap or a malformed/zero-impact GE
		// as a confirmed hit. Extra effects such as poison are gated by this return.
		return false;
	}

	if (!Target->bIsDead && bPlayHitReaction)
	{
		Target->PlayHitReaction();
		Target->OnMeleeHitReceived(this);
	}
	if (IsPlayerControlled())
	{
		RegisterConfirmedHit();
	}
	return true;
}

void ACombatCharacterBase::PlayHitReaction()
{
	if (bIsDead || !HitReactionMontage)
	{
		return;
	}

	if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		AnimInstance->Montage_Stop(0.04f, HitReactionMontage);
	}

	PlayAnimMontage(HitReactionMontage);
}

void ACombatCharacterBase::ApplyCombatLaunch(float LaunchZ)
{
	if (bIsDead || LaunchZ <= 0.f)
	{
		return;
	}

	if (AController* OwningController = GetController())
	{
		OwningController->StopMovement();
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}

	PlayLaunchReaction();
	LaunchCharacter(FVector(0.f, 0.f, LaunchZ), false, true);
	OnCombatLaunched(LaunchZ);
}

void ACombatCharacterBase::ApplyCombatKnockback(FVector Direction, float HorizontalStrength, float LiftZ)
{
	if (bIsDead || HorizontalStrength <= 0.f)
	{
		return;
	}

	Direction.Z = 0.f;
	if (!Direction.Normalize())
	{
		return;
	}

	if (AController* OwningController = GetController())
	{
		OwningController->StopMovement();
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}

	LaunchCharacter(
		Direction * HorizontalStrength + FVector::UpVector * FMath::Max(0.f, LiftZ),
		true,
		LiftZ > 0.f);
}

void ACombatCharacterBase::PlayCombatCameraShake(float Scale)
{
	if (Scale <= 0.f || !IsPlayerControlled())
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !PlayerController->IsLocalController() || !PlayerController->PlayerCameraManager)
	{
		return;
	}

	PlayerController->PlayerCameraManager->StartCameraShake(
		UCombatHitCameraShake::StaticClass(), FMath::Clamp(Scale, 0.f, 2.f));
}

void ACombatCharacterBase::PlayLaunchReaction()
{
	if (LaunchReactionMontage)
	{
		ActiveLaunchReactionMontage = LaunchReactionMontage;
		PlayAnimMontage(LaunchReactionMontage);
		return;
	}

	UAnimSequenceBase* ReactionAnimation = LaunchReactionAnimation;
	if (!ReactionAnimation)
	{
		// Existing placed BP_Enemy actors can retain a pre-property-addition null override.
		// Keep the launcher readable until a bespoke authored reaction is assigned in the Blueprint.
		ReactionAnimation = LoadObject<UAnimSequenceBase>(nullptr,
			TEXT("/Game/Characters/Mannequins/Animations/Manny/MM_Fall_Loop.MM_Fall_Loop"));
	}

	if (ReactionAnimation)
	{
		// A dynamic montage lets the project use Manny's existing falling pose now, while retaining
		// a clean authored-montage override slot for the later animation pass.
		if (UAnimMontage* DynamicMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(
			ReactionAnimation, TEXT("DefaultSlot"), 0.04f, 0.12f, 1.f, 1))
		{
			ActiveLaunchReactionMontage = DynamicMontage;
			PlayAnimMontage(DynamicMontage);
		}
	}
}

void ACombatCharacterBase::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	const float MaxHealth = GetMaxHealth();
	UpdateHealthWidgets(Data.NewValue, MaxHealth);
	OnHealthChanged.Broadcast(Data.NewValue, MaxHealth);
	if (Data.NewValue <= 0.f && !bIsDead)
	{
		bIsDead = true;
		HandleCombatDeath();
		OnCharacterDied.Broadcast(this);
		OnDeath();
	}
}

void ACombatCharacterBase::HandleCombatDeath()
{
	if (bStopMovementOnDeath)
	{
		if (AController* OwningController = GetController())
		{
			OwningController->StopMovement();
		}
		if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
		// This blocks a Tick-driven child Blueprint from scheduling a fresh MoveTo after death.
		SetActorTickEnabled(false);
	}

	if (bHideHealthWidgetsOnDeath)
	{
		TInlineComponentArray<UWidgetComponent*> WidgetComponents(this);
		GetComponents(WidgetComponents);
		for (UWidgetComponent* WidgetComponent : WidgetComponents)
		{
			if (WidgetComponent)
			{
				WidgetComponent->SetVisibility(false, true);
			}
		}
	}

	if (bRagdollOnDeath)
	{
		if (USkeletalMeshComponent* CharacterMesh = GetMesh())
		{
			CharacterMesh->SetCollisionProfileName(TEXT("Ragdoll"));
			CharacterMesh->SetSimulatePhysics(true);
		}
		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	if (DeathDespawnDelay > 0.f)
	{
		SetLifeSpan(DeathDespawnDelay);
	}
}

void ACombatCharacterBase::UpdateHealthWidgets(float NewHealth, float NewMaxHealth)
{
	const float HealthPercent = NewMaxHealth > 0.f
		? FMath::Clamp(NewHealth / NewMaxHealth, 0.f, 1.f)
		: 0.f;

	TInlineComponentArray<UWidgetComponent*> WidgetComponents(this);
	GetComponents(WidgetComponents);
	for (UWidgetComponent* WidgetComponent : WidgetComponents)
	{
		if (!WidgetComponent)
		{
			continue;
		}

		if (UUserWidget* Widget = WidgetComponent->GetUserWidgetObject())
		{
			if (UProgressBar* HealthBar = Cast<UProgressBar>(Widget->GetWidgetFromName(TEXT("HPBar"))))
			{
				HealthBar->SetPercent(HealthPercent);
			}
		}
	}
}

void ACombatCharacterBase::HandleStaminaChanged(const FOnAttributeChangeData& Data)
{
	OnStaminaChanged.Broadcast(Data.NewValue, GetMaxStamina());
}

void ACombatCharacterBase::RegisterConfirmedHit()
{
	++ComboCount;
	OnComboChanged.Broadcast(ComboCount);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ComboResetTimer);
		World->GetTimerManager().SetTimer(ComboResetTimer, this, &ACombatCharacterBase::ResetCombo,
			FMath::Max(0.1f, ComboResetDelay), false);
	}
}

void ACombatCharacterBase::ResetCombo()
{
	if (ComboCount == 0)
	{
		return;
	}
	ComboCount = 0;
	OnComboChanged.Broadcast(ComboCount);
}

void ACombatCharacterBase::CreatePlayerHUDIfNeeded()
{
	if (!IsPlayerControlled())
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}

	// The first call can happen before the viewport subsystem exists. Keep the
	// widget and retry the viewport attachment on PossessedBy/next-tick calls.
	if (PlayerHUD)
	{
		if (!PlayerHUD->IsInViewport())
		{
			PlayerHUD->SetVisibility(ESlateVisibility::Visible);
			PlayerHUD->AddToViewport(100);
		}
		if (PlayerHUD->IsInViewport())
		{
			PlayerHUD->SetDesiredSizeInViewport(FVector2D(480.f, 240.f));
		}
		return;
	}

	TSubclassOf<UCombatPlayerHUDWidget> HUDClass = PlayerHUDClass;
	if (!HUDClass)
	{
		HUDClass = UCombatPlayerHUDWidget::StaticClass();
	}
	PlayerHUD = CreateWidget<UCombatPlayerHUDWidget>(PlayerController, HUDClass);
	if (PlayerHUD)
	{
		PlayerHUD->InitializeForCharacter(this);
		PlayerHUD->SetVisibility(ESlateVisibility::Visible);
		PlayerHUD->AddToViewport(100);
		// Native CanvasPanel widgets can report a zero desired size when they only
		// contain fixed CanvasSlots. Give the screen slot an explicit footprint so
		// the player HUD cannot be created successfully but render as zero pixels.
		PlayerHUD->SetDesiredSizeInViewport(FVector2D(480.f, 240.f));
		UE_LOG(LogTemp, Display, TEXT("Player HUD created for %s (%s), in viewport: %s"),
			*GetName(), *GetNameSafe(HUDClass), PlayerHUD->IsInViewport() ? TEXT("yes") : TEXT("no"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Player HUD creation failed for %s (%s)"), *GetName(), *GetNameSafe(HUDClass));
	}
}

void ACombatCharacterBase::UpdateCombatFacing(float DeltaSeconds)
{
	if (!bAutoFaceNearestEnemy || bIsDead || !IsPlayerControlled())
	{
		return;
	}

	const UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance || !AnimInstance->GetCurrentActiveMontage())
	{
		CurrentCombatTarget = nullptr;
		return;
	}

	const float MaxDistanceSquared = FMath::Square(AutoFaceRange);
	if (!IsValid(CurrentCombatTarget) || CurrentCombatTarget->bIsDead || CurrentCombatTarget->IsPlayerControlled()
		|| FVector::DistSquared2D(GetActorLocation(), CurrentCombatTarget->GetActorLocation()) > MaxDistanceSquared)
	{
		CurrentCombatTarget = FindNearestCombatTarget();
	}

	if (!CurrentCombatTarget)
	{
		return;
	}

	FVector ToTarget = CurrentCombatTarget->GetActorLocation() - GetActorLocation();
	ToTarget.Z = 0.f;
	if (ToTarget.IsNearlyZero())
	{
		return;
	}

	FRotator DesiredRotation = ToTarget.Rotation();
	DesiredRotation.Pitch = 0.f;
	DesiredRotation.Roll = 0.f;
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), DesiredRotation, DeltaSeconds, AutoFaceTurnSpeed));
}

void ACombatCharacterBase::UpdateCombatCamera(float DeltaSeconds)
{
	if (!bCombatCameraEnabled || bIsDead || !IsPlayerControlled())
	{
		return;
	}

	USpringArmComponent* CameraBoom = FindComponentByClass<USpringArmComponent>();
	UCameraComponent* FollowCamera = FindComponentByClass<UCameraComponent>();
	if (!CameraBoom || !FollowCamera)
	{
		return;
	}

	if (!bCameraFollowTuningApplied)
	{
		// The Blueprint boom is attached near the capsule/pelvis. Keep the target point
		// on the torso and damp both translation and rotation so jump/root-motion
		// animation is not transferred directly into the player's view.
		CameraBoom->TargetOffset.Z += CombatCameraTargetOffsetZ;
		CameraBoom->bEnableCameraLag = true;
		CameraBoom->CameraLagSpeed = FMath::Max(1.f, CombatCameraLagSpeed);
		CameraBoom->CameraLagMaxDistance = FMath::Max(0.f, CombatCameraLagMaxDistance);
		CameraBoom->bUseCameraLagSubstepping = true;
		CameraBoom->bEnableCameraRotationLag = true;
		CameraBoom->CameraRotationLagSpeed = FMath::Max(1.f, CombatCameraRotationLagSpeed);
		// Keep the horizon level when an airborne/root-motion move rolls the pawn.
		CameraBoom->bInheritRoll = false;
		bCameraFollowTuningApplied = true;
	}

	if (!bCameraDefaultsCaptured)
	{
		DefaultCameraArmLength = CameraBoom->TargetArmLength;
		DefaultCameraFOV = FollowCamera->FieldOfView;
		bCameraDefaultsCaptured = true;
	}

	const bool bCombatTargetValid = IsValid(CurrentCombatTarget) && !CurrentCombatTarget->bIsDead;
	const float TargetArmLength = bCombatTargetValid ? CombatCameraArmLength : DefaultCameraArmLength;
	const float TargetFOV = bCombatTargetValid ? CombatCameraFOV : DefaultCameraFOV;
	const float InterpSpeed = FMath::Max(1.f, CombatCameraInterpSpeed);

	CameraBoom->TargetArmLength = FMath::FInterpTo(
		CameraBoom->TargetArmLength, TargetArmLength, DeltaSeconds, InterpSpeed);
	FollowCamera->FieldOfView = FMath::FInterpTo(
		FollowCamera->FieldOfView, TargetFOV, DeltaSeconds, InterpSpeed);
}

ACombatCharacterBase* ACombatCharacterBase::FindNearestCombatTarget() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	ACombatCharacterBase* NearestTarget = nullptr;
	float BestDistanceSquared = FMath::Square(AutoFaceRange);
	for (TActorIterator<ACombatCharacterBase> It(World); It; ++It)
	{
		ACombatCharacterBase* Candidate = *It;
		if (Candidate == this || Candidate->bIsDead || Candidate->IsPlayerControlled())
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared2D(GetActorLocation(), Candidate->GetActorLocation());
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			NearestTarget = Candidate;
		}
	}
	return NearestTarget;
}

float ACombatCharacterBase::GetHealth() const
{
	return Attributes ? Attributes->GetHealth() : 0.f;
}

float ACombatCharacterBase::GetMaxHealth() const
{
	return Attributes ? Attributes->GetMaxHealth() : 0.f;
}

float ACombatCharacterBase::GetStamina() const
{
	return Attributes ? Attributes->GetStamina() : 0.f;
}

float ACombatCharacterBase::GetMaxStamina() const
{
	return Attributes ? Attributes->GetMaxStamina() : 0.f;
}

bool ACombatCharacterBase::CanDealMeleeDamage(float Cooldown) const
{
	if (Cooldown <= 0.f)
	{
		return true;
	}

	const UWorld* World = GetWorld();
	return !World || World->GetTimeSeconds() >= NextMeleeDamageTime;
}

void ACombatCharacterBase::StartMeleeDamageCooldown(float Cooldown)
{
	if (Cooldown <= 0.f)
	{
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		NextMeleeDamageTime = World->GetTimeSeconds() + Cooldown;
	}
}
