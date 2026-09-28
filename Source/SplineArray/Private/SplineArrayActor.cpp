// Copyright (c) 2026. MIT License.

#include "SplineArrayActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineMeshComponent.h"
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
	Instances->SetVisibility(false);
	Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
	const float AxisScale = ForwardAxis == ESplineArrayForwardAxis::X ? Scale.X
		: ForwardAxis == ESplineArrayForwardAxis::Y ? Scale.Y : Scale.Z;
	const float SegmentLength = MeshLength * FMath::Abs(AxisScale);
	float Step = 0.0f;

	switch (Distribution)
	{
	case ESplineArrayDistribution::ByCount:
		if (SegmentLength <= KINDA_SMALL_NUMBER || Usable < SegmentLength)
		{
			return 0.0f;
		}
		OutCount = FMath::Max(1, Count);
		Step = OutCount > 1 ? (Usable - SegmentLength) / static_cast<float>(OutCount - 1) : 0.0f;
		break;

	case ESplineArrayDistribution::BySpacing:
		Step = FMath::Max(Spacing + Gap, KINDA_SMALL_NUMBER);
		OutCount = SegmentLength > KINDA_SMALL_NUMBER && Usable >= SegmentLength
			? FMath::FloorToInt((Usable - SegmentLength) / Step) + 1 : 0;
		break;

	case ESplineArrayDistribution::FitAlongSpline:
		Step = FMath::Max(SegmentLength + Gap, KINDA_SMALL_NUMBER);
		OutCount = SegmentLength > KINDA_SMALL_NUMBER ? FMath::CeilToInt(Usable / Step) : 0;
		break;

	case ESplineArrayDistribution::EndToEnd:
		if (SegmentLength <= KINDA_SMALL_NUMBER)
		{
			return 0.0f;
		}
		Step = SegmentLength * (1.0f + FMath::Clamp(LengthOffset, -0.05f, 0.05f));
		OutCount = FMath::CeilToInt(Usable / Step);
		break;
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

	for (USplineMeshComponent* Segment : Segments)
	{
		if (IsValid(Segment))
		{
			Segment->DestroyComponent();
		}
	}
	Segments.Empty();
	Instances->ClearInstances();
	Instances->SetStaticMesh(nullptr);
	MeshAxisLength = ComputeMeshLength();

	if (!SourceMesh || !Spline || MeshAxisLength <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float Start = FMath::Max(0.0f, StartOffset);
	const float End = Spline->GetSplineLength() - FMath::Max(0.0f, EndOffset);
	int32 Num = 0;
	const float Step = ResolveSpacing(Num);
	if (Num <= 0 || End <= Start)
	{
		return;
	}

	const float AxisScale = ForwardAxis == ESplineArrayForwardAxis::X ? Scale.X
		: ForwardAxis == ESplineArrayForwardAxis::Y ? Scale.Y : Scale.Z;
	const float SegmentLength = MeshAxisLength * FMath::Abs(AxisScale);
	const ESplineMeshAxis::Type MeshAxis = ForwardAxis == ESplineArrayForwardAxis::X ? ESplineMeshAxis::X
		: ForwardAxis == ESplineArrayForwardAxis::Y ? ESplineMeshAxis::Y : ESplineMeshAxis::Z;
	FRandomStream Random(RandomSeed);

	for (int32 Index = 0; Index < Num; ++Index)
	{
		const float From = Start + Step * Index;
		const float To = FMath::Min(From + SegmentLength, End);
		if (To <= From + KINDA_SMALL_NUMBER)
		{
			break;
		}

		USplineMeshComponent* Segment = NewObject<USplineMeshComponent>(this);
		Segment->SetupAttachment(SceneRoot);
		Segment->SetMobility(EComponentMobility::Movable);
		Segment->SetStaticMesh(SourceMesh);
		Segment->SetForwardAxis(MeshAxis, false);
		Segment->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Segment->SetGenerateOverlapEvents(false);
		Segment->SetCastShadow(true);
		Segment->SetRelativeRotation(RotationOffset);
		Segment->SetRelativeLocation(LocationOffset);

		FVector SegmentScale = Scale;
		if (bRandomizeScale)
		{
			SegmentScale *= Random.FRandRange(FMath::Min(ScaleRangeMin, ScaleRangeMax), FMath::Max(ScaleRangeMin, ScaleRangeMax));
		}
		Segment->SetRelativeScale3D(SegmentScale);
		if (bRandomizeYaw)
		{
			Segment->AddLocalRotation(FRotator(0.0f, Random.FRandRange(FMath::Min(YawRangeMin, YawRangeMax), FMath::Max(YawRangeMin, YawRangeMax)), 0.0f));
		}

		const FTransform ParentToSegment = Segment->GetRelativeTransform().Inverse();
		const FTransform SplineToParent = Spline->GetRelativeTransform();
		const FVector P0 = ParentToSegment.TransformPosition(SplineToParent.TransformPosition(
			Spline->GetLocationAtDistanceAlongSpline(From, ESplineCoordinateSpace::Local)));
		const FVector P1 = ParentToSegment.TransformPosition(SplineToParent.TransformPosition(
			Spline->GetLocationAtDistanceAlongSpline(To, ESplineCoordinateSpace::Local)));
		const FVector T0 = ParentToSegment.TransformVector(SplineToParent.TransformVector(
			Spline->GetTangentAtDistanceAlongSpline(From, ESplineCoordinateSpace::Local).GetSafeNormal() * (To - From)));
		const FVector T1 = ParentToSegment.TransformVector(SplineToParent.TransformVector(
			Spline->GetTangentAtDistanceAlongSpline(To, ESplineCoordinateSpace::Local).GetSafeNormal() * (To - From)));

		Segment->SetStartAndEnd(P0, T0, P1, T1, true);
		Segment->RegisterComponent();
		Segments.Add(Segment);
	}
}


