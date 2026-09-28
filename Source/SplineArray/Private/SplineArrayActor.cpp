// Copyright (c) 2026. MIT License.

#include "SplineArrayActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/StaticMesh.h"

ASplineArrayActor::ASplineArrayActor()
{
#if WITH_EDITORONLY_DATA
	PrimaryActorTick.bCanEverTick = true;
#else
	PrimaryActorTick.bCanEverTick = false;
#endif

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	Spline->SetupAttachment(SceneRoot);
	Spline->SetClosedLoop(false);

	Instances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Instances"));
	Instances->SetupAttachment(SceneRoot);
	Instances->SetMobility(EComponentMobility::Movable);
	Instances->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Instances->SetGenerateOverlapEvents(false);
	Instances->SetCastShadow(true);
}

void ASplineArrayActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Give a brand-new actor a usable two-point spline.
	if (Spline && Spline->GetNumberOfSplinePoints() < 2)
	{
		Spline->ClearSplinePoints(false);
		Spline->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local, false);
		Spline->AddSplinePoint(FVector(500.0f, 0.0f, 0.0f), ESplineCoordinateSpace::Local, false);
		Spline->UpdateSpline();
	}

	Rebuild();

#if WITH_EDITORONLY_DATA
	CachedSplineHash = ComputeSplineHash();
#endif
}

void ASplineArrayActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

#if WITH_EDITOR
	// Spline-point edits do not raise PostEditChangeProperty, so watch the geometry.
	if (Spline)
	{
		const uint32 Hash = ComputeSplineHash();
		if (Hash != CachedSplineHash)
		{
			CachedSplineHash = Hash;
			Rebuild();
		}
	}
#endif
}

#if WITH_EDITOR
void ASplineArrayActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	Rebuild();
}
#endif

#if WITH_EDITORONLY_DATA
uint32 ASplineArrayActor::ComputeSplineHash() const
{
	if (!Spline)
	{
		return 0;
	}

	uint32 Hash = GetTypeHash(Spline->GetNumberOfSplinePoints());
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Spline->GetSplineLength() * 100.0f)));
	Hash = HashCombine(Hash, GetTypeHash(Spline->GetRelativeLocation()));
	Hash = HashCombine(Hash, GetTypeHash(Spline->IsClosedLoop() ? 1 : 0));

	const int32 NumPoints = Spline->GetNumberOfSplinePoints();
	for (int32 Index = 0; Index < NumPoints; ++Index)
	{
		Hash = HashCombine(Hash, GetTypeHash(Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local)));
		Hash = HashCombine(Hash, GetTypeHash(Spline->GetArriveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local)));
		Hash = HashCombine(Hash, GetTypeHash(Spline->GetLeaveTangentAtSplinePoint(Index, ESplineCoordinateSpace::Local)));
	}
	return Hash;
}
#endif

float ASplineArrayActor::ComputeMeshLength() const
{
	if (!SourceMesh)
	{
		return 0.0f;
	}

	const FVector Extent = SourceMesh->GetBoundingBox().GetExtent();
	switch (ForwardAxis)
	{
	case ESplineArrayForwardAxis::Y:
		return Extent.Y * 2.0f;
	case ESplineArrayForwardAxis::Z:
		return Extent.Z * 2.0f;
	case ESplineArrayForwardAxis::X:
	default:
		return Extent.X * 2.0f;
	}
}

FQuat ASplineArrayActor::ComputeForwardAxisCorrection() const
{
	FVector Forward = FVector::ForwardVector;
	switch (ForwardAxis)
	{
	case ESplineArrayForwardAxis::Y:
		Forward = FVector::RightVector;
		break;
	case ESplineArrayForwardAxis::Z:
		Forward = FVector::UpVector;
		break;
	case ESplineArrayForwardAxis::X:
	default:
		break;
	}

	// Rotation that maps the mesh's forward axis onto the spline's local +X (the tangent).
	return FQuat::FindBetweenNormals(Forward, FVector::ForwardVector);
}

float ASplineArrayActor::ResolveSpacing(int32& OutCount) const
{
	OutCount = 0;
	if (!Spline)
	{
		return 0.0f;
	}

	const float Length = Spline->GetSplineLength();
	const float Start = FMath::Max(0.0f, StartOffset);
	const float End = FMath::Max(Start, Length - FMath::Max(0.0f, EndOffset));
	const float Usable = End - Start;
	if (Usable <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float MeshLength = ComputeMeshLength();
	float Step = 0.0f;

	switch (Distribution)
	{
	case ESplineArrayDistribution::ByCount:
		OutCount = FMath::Max(1, Count);
		Step = (OutCount > 1) ? (Usable / static_cast<float>(OutCount - 1)) : 0.0f;
		break;

	case ESplineArrayDistribution::BySpacing:
		Step = (bUseMeshLengthForSpacing ? MeshLength : Spacing) + Gap;
		Step = FMath::Max(Step, KINDA_SMALL_NUMBER);
		OutCount = FMath::FloorToInt(Usable / Step) + 1;
		break;

	case ESplineArrayDistribution::FitAlongSpline:
	{
		const float Item = (bUseMeshLengthForSpacing ? MeshLength : ItemLength) + Gap;
		Step = FMath::Max(Item, KINDA_SMALL_NUMBER);
		OutCount = FMath::Max(1, FMath::FloorToInt(Usable / Step) + 1);
		break;
	}
	}

	return Step;
}

float ASplineArrayActor::GetResolvedSpacing() const
{
	int32 Unused = 0;
	return ResolveSpacing(Unused);
}

int32 ASplineArrayActor::GetResolvedCount() const
{
	int32 Count2 = 0;
	ResolveSpacing(Count2);
	return Count2;
}

void ASplineArrayActor::Rebuild()
{
	if (!Instances)
	{
		return;
	}

	Instances->ClearInstances();
	Instances->SetStaticMesh(SourceMesh);

	if (!SourceMesh || !Spline)
	{
		return;
	}

	const float Length = Spline->GetSplineLength();
	const float Start = FMath::Max(0.0f, StartOffset);
	const float End = FMath::Max(Start, Length - FMath::Max(0.0f, EndOffset));
	const float Usable = End - Start;
	if (Usable <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	int32 Num = 0;
	const float Step = ResolveSpacing(Num);
	if (Num <= 0)
	{
		return;
	}

	// "Fit" mode centres the leftover space for a tidier look.
	float FirstDistance = Start;
	if (Distribution == ESplineArrayDistribution::FitAlongSpline && Num > 1)
	{
		FirstDistance = Start + FMath::Max(0.0f, (Usable - Step * static_cast<float>(Num - 1)) * 0.5f);
	}

	const FQuat AxisCorrection = ComputeForwardAxisCorrection();
	const FTransform ComponentTransform = Instances->GetComponentTransform();

	FRandomStream Rng(RandomSeed);

	TArray<FTransform> InstanceTransforms;
	InstanceTransforms.Reserve(Num);

	for (int32 Index = 0; Index < Num; ++Index)
	{
		float Distance = FirstDistance + Step * static_cast<float>(Index);
		if (Distance > End + KINDA_SMALL_NUMBER)
		{
			break;
		}
		Distance = FMath::Clamp(Distance, Start, End);

		const FTransform SplineTransform = Spline->GetTransformAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

		FQuat Rotation;
		if (bAlignToTangent)
		{
			Rotation = SplineTransform.GetRotation() * AxisCorrection;
			Rotation = Rotation * RotationOffset.Quaternion();
		}
		else
		{
			Rotation = RotationOffset.Quaternion();
		}

		if (bRandomizeYaw)
		{
			const float YawMin = FMath::Min(YawRangeMin, YawRangeMax);
			const float YawMax = FMath::Max(YawRangeMin, YawRangeMax);
			Rotation = Rotation * FRotator(0.0f, Rng.FRandRange(YawMin, YawMax), 0.0f).Quaternion();
		}

		FVector InstanceScale = Scale * SplineTransform.GetScale3D();
		if (bRandomizeScale)
		{
			const float ScaleMin = FMath::Min(ScaleRangeMin, ScaleRangeMax);
			const float ScaleMax = FMath::Max(ScaleRangeMin, ScaleRangeMax);
			InstanceScale *= Rng.FRandRange(ScaleMin, ScaleMax);
		}

		const FVector Location = SplineTransform.GetLocation() + Rotation.RotateVector(LocationOffset);

		const FTransform WorldTransform(Rotation, Location, InstanceScale);
		InstanceTransforms.Add(WorldTransform.GetRelativeTransform(ComponentTransform));
	}

	if (InstanceTransforms.Num() > 0)
	{
		Instances->AddInstances(InstanceTransforms);
	}
}
