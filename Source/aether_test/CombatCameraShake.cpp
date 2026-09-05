#include "CombatCameraShake.h"

UCombatHitCameraShakePattern::UCombatHitCameraShakePattern(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UCombatHitCameraShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
	OutInfo.Duration = FCameraShakeDuration(0.13f);
	OutInfo.BlendIn = 0.01f;
	OutInfo.BlendOut = 0.06f;
}

void UCombatHitCameraShakePattern::StartShakePatternImpl(const FCameraShakePatternStartParams& Params)
{
	ElapsedTime = 0.f;
}

void UCombatHitCameraShakePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params,
	FCameraShakePatternUpdateResult& OutResult)
{
	ElapsedTime += Params.DeltaTime;

	const float Duration = 0.13f;
	const float Progress = FMath::Clamp(ElapsedTime / Duration, 0.f, 1.f);
	const float Envelope = 1.f - Progress;
	const float Pulse = FMath::Sin(ElapsedTime * 75.f);

	OutResult.Location = FVector(
		0.f,
		Pulse * 1.5f * Envelope,
		FMath::Abs(Pulse) * 0.8f * Envelope);
	OutResult.Rotation = FRotator(
		Pulse * 0.8f * Envelope,
		-Pulse * 0.55f * Envelope,
		Pulse * 0.35f * Envelope);
}

bool UCombatHitCameraShakePattern::IsFinishedImpl() const
{
	return ElapsedTime >= 0.13f;
}

void UCombatHitCameraShakePattern::StopShakePatternImpl(const FCameraShakePatternStopParams& Params)
{
	ElapsedTime = 0.13f;
}

UCombatHitCameraShake::UCombatHitCameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCombatHitCameraShakePattern>(TEXT("RootShakePattern")))
{
	bSingleInstance = true;
}
