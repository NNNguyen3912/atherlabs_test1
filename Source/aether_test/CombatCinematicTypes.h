#pragma once

#include "CoreMinimal.h"
#include "CombatCinematicTypes.generated.h"

/** Designer-owned values copied from a Finisher Cinematic Window notify. */
USTRUCT(BlueprintType)
struct AETHER_TEST_API FFinisherCinematicSettings
{
	GENERATED_BODY()

	/** Slows the whole game world: characters, physics, animation and VFX. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slow Motion", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float WorldTimeDilation = 0.35f;

	/** Total yaw travelled during the notify. The notify length controls the base rotation speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float OrbitDegrees = 360.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float PitchOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "100.0"))
	float ArmLength = 390.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "1.0", ClampMax = "179.0"))
	float FieldOfView = 76.f;

	/** Added to the camera target offset captured at notify begin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "-200.0", ClampMax = "200.0"))
	float TargetOffsetZ = 25.f;

	/** Blend speed for arm length, FOV and target offset. Rotation timing comes from the notify length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.1"))
	float FramingInterpSpeed = 10.f;

	/** Slow at the start/end and fast in the middle. Disable for constant angular speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Speed")
	bool bEaseInOut = true;

	/** Higher values exaggerate the slow start/end when Ease In Out is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Speed", meta = (ClampMin = "1.0", ClampMax = "8.0", EditCondition = "bEaseInOut"))
	float EaseExponent = 2.f;
};
