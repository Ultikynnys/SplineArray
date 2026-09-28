// Copyright (c) 2026. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SplineArrayActor.generated.h"

class USplineComponent;
class UInstancedStaticMeshComponent;
class USplineMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/** How copies of the mesh are distributed along the spline. */
UENUM(BlueprintType)
enum class ESplineArrayDistribution : uint8
{
	/** Place an exact number of copies, spread evenly between the start/end offsets. */
	ByCount        UMETA(Hidden),
	/** Place a copy every "Spacing" cm until the end of the spline is reached. */
	BySpacing      UMETA(Hidden),
	/** Automatically tile the measured mesh along the spline. */
	FitAlongSpline UMETA(DisplayName = "Fit Along Spline"),
	/** Chain copies exactly end to end using the mesh's length along its Forward Axis, plus a +/-5% offset. */
	EndToEnd     UMETA(DisplayName = "End To End")
};

/** Which local axis of the source mesh should point along the spline. */
UENUM(BlueprintType)
enum class ESplineArrayForwardAxis : uint8
{
	X UMETA(DisplayName = "+X"),
	Y UMETA(DisplayName = "+Y"),
	Z UMETA(DisplayName = "+Z"),
	NegativeX UMETA(DisplayName = "-X"),
	NegativeY UMETA(DisplayName = "-Y"),
	NegativeZ UMETA(DisplayName = "-Z")
};

/**
 * Repeats a static mesh along a spline component, in the spirit of Blender's
 * Array + Curve modifiers. Copies use spline mesh components so each mesh bends
 * along its interval.
 *
 * The layout rebuilds automatically when the actor is constructed, when any
 * property changes, and (in the editor) when the spline is edited.
 */
UCLASS(Blueprintable, ClassGroup = (SplineArray), meta = (DisplayName = "Spline Array Actor"))
class SPLINEARRAY_API ASplineArrayActor : public AActor
{
	GENERATED_BODY()

public:
	ASplineArrayActor();

	//~ Begin AActor
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End AActor

	/** Recomputes the generated mesh from the spline and settings. */
	UFUNCTION(BlueprintCallable, Category = "Spline Array")
	void Rebuild();

	/** Distance (cm) between two consecutive copies with the current settings. */
	UFUNCTION(BlueprintPure, Category = "Spline Array")
	float GetResolvedSpacing() const;

	/** Number of copies produced with the current settings. */
	UFUNCTION(BlueprintPure, Category = "Spline Array")
	int32 GetResolvedCount() const;

protected:
	// --- Components ---

	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY()
	TObjectPtr<USplineComponent> Spline;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Instances;

	// --- Source ---

	/** Mesh that gets repeated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array", meta = (DisplayName = "Mesh"))
	TObjectPtr<UStaticMesh> SourceMesh;

	/** Mesh axis deformed along the spline; negative directions reverse the mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array", meta = (DisplayName = "Axis"))
	ESplineArrayForwardAxis ForwardAxis = ESplineArrayForwardAxis::X;

	/** Read-only: the source mesh's length (cm) along its Forward Axis. */
	UPROPERTY(Transient)
	float MeshAxisLength = 0.0f;

	// --- Distribution ---

	UPROPERTY()
	ESplineArrayDistribution Distribution = ESplineArrayDistribution::FitAlongSpline;

	UPROPERTY()
	int32 Count = 10;

	UPROPERTY()
	float Spacing = 200.0f;

	UPROPERTY()
	float ItemLength = 100.0f;

	/** Extra gap (cm) added between copies in the spacing / fit modes. */
	UPROPERTY()
	float Gap = 0.0f;

	/** Previously serialized fractional offset; migrated to AxisOffsetPercent on load. */
	UPROPERTY()
	float LengthOffset = 0.0f;

	/** Distance (cm) to skip from the start of the spline. */
	UPROPERTY()
	float StartOffset = 0.0f;

	/** Distance (cm) to skip from the end of the spline. */
	UPROPERTY()
	float EndOffset = 0.0f;

	// --- Orientation / transform ---

	/** Legacy setting retained for existing actors. */
	UPROPERTY()
	bool bAlignToTangent = true;

	/** Rotational correction applied to every copy, after spline alignment. */
	UPROPERTY()
	FRotator RotationOffset = FRotator::ZeroRotator;

	/** Offset applied to every copy, in the copy's local space. */
	UPROPERTY()
	FVector LocationOffset = FVector::ZeroVector;

	/** Base scale applied to every copy (multiplied by any spline scale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array", meta = (DisplayName = "Mesh Scale"))
	FVector Scale = FVector(1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array", meta = (DisplayName = "Material"))
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array", meta = (DisplayName = "Axis Offset (%)", ClampMin = "-5.0", ClampMax = "5.0", UIMin = "-5.0", UIMax = "5.0"))
	float AxisOffsetPercent = 0.0f;

	// --- Randomisation ---

	UPROPERTY()
	bool bRandomizeYaw = false;

	UPROPERTY()
	float YawRangeMin = -180.0f;

	UPROPERTY()
	float YawRangeMax = 180.0f;

	UPROPERTY()
	bool bRandomizeScale = false;

	UPROPERTY()
	float ScaleRangeMin = 0.8f;

	UPROPERTY()
	float ScaleRangeMax = 1.2f;

	UPROPERTY()
	int32 RandomSeed = 0;

private:
	/** Length of the mesh along its Forward Axis, in cm. */
	float ComputeMeshLength() const;

	/** Rotation that maps the mesh's Forward Axis onto the spline's forward (X) direction. */
	FQuat ComputeForwardAxisCorrection() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineMeshComponent>> Segments;

	/** Generated bisected meshes (source mesh + slab), held for the current layout. */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UStaticMesh>> BisectCache;

	/** Resolves the distance between copies and, out, how many copies to place. Returns 0 if it can't be resolved. */
	float ResolveSpacing(int32& OutCount) const;

#if WITH_EDITORONLY_DATA
	/** Hash of the spline geometry, used to detect edits that don't raise PostEditChangeProperty. */
	uint32 ComputeSplineHash() const;
	uint32 CachedSplineHash = 0;
#endif
};
