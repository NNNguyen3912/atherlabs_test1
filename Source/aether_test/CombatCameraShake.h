#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "CombatCameraShake.generated.h"

/** Small, self-contained hit shake so the project does not need a separate camera asset. */
UCLASS()
class AETHER_TEST_API UCombatHitCameraShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()

public:
	UCombatHitCameraShakePattern(const FObjectInitializer& ObjectInitializer);

private:
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
	virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params,
		FCameraShakePatternUpdateResult& OutResult) override;
	virtual bool IsFinishedImpl() const override;
	virtual void StopShakePatternImpl(const FCameraShakePatternStopParams& Params) override;

	float ElapsedTime = 0.f;
};

/** Native default camera shake used only after a confirmed combat hit. */
UCLASS()
class AETHER_TEST_API UCombatHitCameraShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UCombatHitCameraShake(const FObjectInitializer& ObjectInitializer);
};
