// Copyright (c) 2026. MIT License.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SplineArrayActor.generated.h"

class USplineComponent;
class UInstancedStaticMeshComponent;
class UStaticMesh;

/** How copies of the mesh are distributed along the spline. */
UENUM(BlueprintType)
enum class ESplineArrayDistribution : uint8
{
	/** Place an exact number of copies, spread evenly between the start/end offsets. */
	ByCount        UMETA(DisplayName = "By Count"),
	/** Place a copy every "Spacing" cm until the end of the spline is reached. */
	BySpacing      UMETA(DisplayName = "By Spacing"),
	/** Fit as many copies of "Item Length" as possible along the spline. */
	FitAlongSpline UMETA(DisplayName = "Fit Along Spline")
};

/** Which local axis of the source mesh should point along the spline. */
UENUM(BlueprintType)
enum class ESplineArrayForwardAxis : uint8
{
	X UMETA(DisplayName = "X (forward)"),
	Y UMETA(DisplayName = "Y (right)"),
	Z UMETA(DisplayName = "Z (up)")
};

/**
 * Repeats a static mesh along a spline component, in the spirit of Blender's
 * Array + Curve modifiers. All copies are rendered through a single
 * InstancedStaticMeshComponent, so even large counts are cheap.
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
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End AActor

	/** Recomputes and rebuilds every instance from the current spline + settings. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Spline Array")
	void Rebuild();

	/** Distance (cm) between two consecutive copies with the current settings. */
	UFUNCTION(BlueprintPure, Category = "Spline Array")
	float GetResolvedSpacing() const;

	/** Number of copies produced with the current settings. */
	UFUNCTION(BlueprintPure, Category = "Spline Array")
	int32 GetResolvedCount() const;

protected:
	// --- Components ---

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spline Array|Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spline Array|Components")
	TObjectPtr<USplineComponent> Spline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spline Array|Components")
	TObjectPtr<UInstancedStaticMeshComponent> Instances;

	// --- Source ---

	/** Mesh that gets repeated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Source")
	TObjectPtr<UStaticMesh> SourceMesh;

	/** Local axis of the mesh that is aligned to the spline tangent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Source")
	ESplineArrayForwardAxis ForwardAxis = ESplineArrayForwardAxis::X;

	/** Use the mesh's bounding-box length (along Forward Axis) as the base spacing instead of a fixed value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Source")
	bool bUseMeshLengthForSpacing = false;

	// --- Distribution ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Distribution")
	ESplineArrayDistribution Distribution = ESplineArrayDistribution::ByCount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Distribution", meta = (ClampMin = "1", UIMin = "1", EditCondition = "Distribution == ESplineArrayDistribution::ByCount", EditConditionHides))
	int32 Count = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Distribution", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "Distribution == ESplineArrayDistribution::BySpacing", EditConditionHides))
	float Spacing = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Distribution", meta = (ClampMin = "0.01", Units = "cm", EditCondition = "Distribution == ESplineArrayDistribution::FitAlongSpline", EditConditionHides))
	float ItemLength = 100.0f;

	/** Extra gap (cm) added between copies (works with the mesh-length option too). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Distribution", meta = (ClampMin = "0.0", Units = "cm", EditCondition = "Distribution != ESplineArrayDistribution::ByCount", EditConditionHides))
	float Gap = 0.0f;

	/** Distance (cm) to skip from the start of the spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Distribution", meta = (ClampMin = "0.0", Units = "cm"))
	float StartOffset = 0.0f;

	/** Distance (cm) to skip from the end of the spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Distribution", meta = (ClampMin = "0.0", Units = "cm"))
	float EndOffset = 0.0f;

	// --- Orientation / transform ---

	/** Align each copy to the spline's tangent/rotation. When off, copies use only RotationOffset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Orientation")
	bool bAlignToTangent = true;

	/** Rotational correction applied to every copy, after spline alignment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Orientation")
	FRotator RotationOffset = FRotator::ZeroRotator;

	/** Offset applied to every copy, in the copy's local space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Orientation")
	FVector LocationOffset = FVector::ZeroVector;

	/** Base scale applied to every copy (multiplied by any spline scale). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Orientation")
	FVector Scale = FVector(1.0f);

	// --- Randomisation ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Randomisation")
	bool bRandomizeYaw = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Randomisation", meta = (EditCondition = "bRandomizeYaw", EditConditionHides, Units = "deg"))
	float YawRangeMin = -180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Randomisation", meta = (EditCondition = "bRandomizeYaw", EditConditionHides, Units = "deg"))
	float YawRangeMax = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Randomisation")
	bool bRandomizeScale = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Randomisation", meta = (EditCondition = "bRandomizeScale", EditConditionHides))
	float ScaleRangeMin = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Randomisation", meta = (EditCondition = "bRandomizeScale", EditConditionHides))
	float ScaleRangeMax = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spline Array|Randomisation")
	int32 RandomSeed = 0;

private:
	/** Length of the mesh along its Forward Axis, in cm. */
	float ComputeMeshLength() const;

	/** Rotation that maps the mesh's Forward Axis onto the spline's forward (X) direction. */
	FQuat ComputeForwardAxisCorrection() const;

	/** Resolves the distance between copies and, out, how many copies to place. Returns 0 if it can't be resolved. */
	float ResolveSpacing(int32& OutCount) const;

#if WITH_EDITORONLY_DATA
	/** Hash of the spline geometry, used to detect edits that don't raise PostEditChangeProperty. */
	uint32 ComputeSplineHash() const;
	uint32 CachedSplineHash = 0;
#endif
};
