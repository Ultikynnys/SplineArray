// Copyright (c) 2026. MIT License.

#include "SplineArrayActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "SplineArrayMeshTools.h"

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

void ASplineArrayActor::PostLoad()
{
	Super::PostLoad();
	if (!FMath::IsNearlyZero(LengthOffset) && FMath::IsNearlyZero(AxisOffsetPercent))
	{
		AxisOffsetPercent = FMath::Clamp(LengthOffset * 100.0f, -5.0f, 5.0f);
	}
	LengthOffset = 0.0f;
	Distribution = ESplineArrayDistribution::FitAlongSpline;
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
		Hash = HashCombine(Hash, GetTypeHash(Spline->GetRotationAtSplinePoint(Index, ESplineCoordinateSpace::Local).Quaternion()));
		Hash = HashCombine(Hash, GetTypeHash(Spline->GetScaleAtSplinePoint(Index)));
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
	case ESplineArrayForwardAxis::NegativeY:
		return Extent.Y * 2.0f;
	case ESplineArrayForwardAxis::Z:
	case ESplineArrayForwardAxis::NegativeZ:
		return Extent.Z * 2.0f;
	case ESplineArrayForwardAxis::X:
	case ESplineArrayForwardAxis::NegativeX:
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
	case ESplineArrayForwardAxis::NegativeX:
		Forward = -FVector::ForwardVector;
		break;
	case ESplineArrayForwardAxis::NegativeY:
		Forward = -FVector::RightVector;
		break;
	case ESplineArrayForwardAxis::NegativeZ:
		Forward = -FVector::UpVector;
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
	const float AxisScale = ForwardAxis == ESplineArrayForwardAxis::X || ForwardAxis == ESplineArrayForwardAxis::NegativeX ? Scale.X
		: ForwardAxis == ESplineArrayForwardAxis::Y || ForwardAxis == ESplineArrayForwardAxis::NegativeY ? Scale.Y : Scale.Z;
	const float SegmentLength = MeshLength * FMath::Abs(AxisScale);

	if (SegmentLength <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float Step = SegmentLength * (1.0f + FMath::Clamp(AxisOffsetPercent, -5.0f, 5.0f) / 100.0f);
	OutCount = FMath::Max(1, FMath::CeilToInt(Usable / Step));
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

	TInlineComponentArray<USplineMeshComponent*> ExistingSegments(this);
	for (USplineMeshComponent* Segment : ExistingSegments)
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
	const float MeshScaleAlongAxis = ForwardAxis == ESplineArrayForwardAxis::X || ForwardAxis == ESplineArrayForwardAxis::NegativeX ? Scale.X
		: ForwardAxis == ESplineArrayForwardAxis::Y || ForwardAxis == ESplineArrayForwardAxis::NegativeY ? Scale.Y : Scale.Z;
	const float SegmentLength = MeshAxisLength * FMath::Abs(MeshScaleAlongAxis);
	if (SegmentLength <= KINDA_SMALL_NUMBER || End <= Start)
	{
		return;
	}

	const ESplineMeshAxis::Type MeshAxis = ForwardAxis == ESplineArrayForwardAxis::X || ForwardAxis == ESplineArrayForwardAxis::NegativeX ? ESplineMeshAxis::X
		: ForwardAxis == ESplineArrayForwardAxis::Y || ForwardAxis == ESplineArrayForwardAxis::NegativeY ? ESplineMeshAxis::Y : ESplineMeshAxis::Z;
	const bool bReverseAxis = ForwardAxis == ESplineArrayForwardAxis::NegativeX
		|| ForwardAxis == ESplineArrayForwardAxis::NegativeY || ForwardAxis == ESplineArrayForwardAxis::NegativeZ;
	const int32 AxisIndex = static_cast<int32>(MeshAxis);
	const FVector2D CrossScale = MeshAxis == ESplineMeshAxis::X ? FVector2D(FMath::Abs(Scale.Y), FMath::Abs(Scale.Z))
		: MeshAxis == ESplineMeshAxis::Y ? FVector2D(FMath::Abs(Scale.X), FMath::Abs(Scale.Z))
		: FVector2D(FMath::Abs(Scale.X), FMath::Abs(Scale.Y));
	const FTransform SplineToParent = Spline->GetRelativeTransform();
	const float ScaleAlongAxis = FMath::Max(FMath::Abs(MeshScaleAlongAxis), KINDA_SMALL_NUMBER);
	const float Step = FMath::Max(SegmentLength * (1.0f + FMath::Clamp(AxisOffsetPercent, -5.0f, 5.0f) / 100.0f), KINDA_SMALL_NUMBER);

	BisectCache.Empty();

	const FBox MeshBounds = SourceMesh->GetBoundingBox();
	const float MeshAxisMin = static_cast<float>(MeshBounds.Min[AxisIndex]);
	const float MeshAxisMax = static_cast<float>(MeshBounds.Max[AxisIndex]);

	// A single SplineMeshComponent renders one cubic between its endpoints, so a copy that
	// spans several spline control points cuts the corner. Breaking every copy at the
	// control-point distances makes each sub-segment lie inside one spline cubic and hug the curve.
	TArray<float> SplinePointDistances;
	{
		const int32 NumSplinePoints = Spline->GetNumberOfSplinePoints();
		SplinePointDistances.Reserve(NumSplinePoints);
		for (int32 PointIndex = 0; PointIndex < NumSplinePoints; ++PointIndex)
		{
			SplinePointDistances.Add(Spline->GetDistanceAlongSplineAtSplinePoint(PointIndex));
		}
	}

	TArray<float> CutDistances;

	for (float Cursor = Start; Cursor < End - KINDA_SMALL_NUMBER; Cursor += Step)
	{
		const float Span = FMath::Min(SegmentLength, End - Cursor);
		if (Span <= KINDA_SMALL_NUMBER)
		{
			break;
		}

		const bool bPartial = Span < SegmentLength - KINDA_SMALL_NUMBER;
		const float CopyEnd = Cursor + Span;

		CutDistances.Reset();
		CutDistances.Add(Cursor);
		for (const float PointDistance : SplinePointDistances)
		{
			if (PointDistance > Cursor + KINDA_SMALL_NUMBER && PointDistance < CopyEnd - KINDA_SMALL_NUMBER)
			{
				CutDistances.Add(PointDistance);
			}
		}
		CutDistances.Add(CopyEnd);

		for (int32 CutIndex = 0; CutIndex + 1 < CutDistances.Num(); ++CutIndex)
		{
			const float From = CutDistances[CutIndex];
			const float To = CutDistances[CutIndex + 1];
			if (To - From <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const float NearMesh = bReverseAxis
				? MeshAxisMax - (From - Cursor) / ScaleAlongAxis
				: MeshAxisMin + (From - Cursor) / ScaleAlongAxis;
			const float FarMesh = bReverseAxis
				? MeshAxisMax - (To - Cursor) / ScaleAlongAxis
				: MeshAxisMin + (To - Cursor) / ScaleAlongAxis;
			const float SliceStart = FMath::Min(NearMesh, FarMesh);
			const float SliceLength = FMath::Abs(FarMesh - NearMesh);

			UStaticMesh* SegmentMesh = SplineArrayMeshTools::BisectMesh(SourceMesh, AxisIndex, SliceStart, SliceLength, BisectCache);

			USplineMeshComponent* Segment = NewObject<USplineMeshComponent>(this, NAME_None, RF_Transient);
			Segment->SetupAttachment(SceneRoot);
			Segment->SetMobility(EComponentMobility::Movable);
			Segment->SetStaticMesh(SegmentMesh);
			if (Material)
			{
				// Bisected meshes are built from a mesh description and carry no material slots,
				// so drive the override from the material itself instead of the slot count.
				const int32 NumMaterialSlots = FMath::Max(1, SegmentMesh->GetStaticMaterials().Num());
				for (int32 Slot = 0; Slot < NumMaterialSlots; ++Slot)
				{
					Segment->SetMaterial(Slot, Material);
				}
			}
			Segment->SetForwardAxis(MeshAxis, false);
			Segment->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Segment->SetGenerateOverlapEvents(false);
			Segment->SetCastShadow(true);
			Segment->SetStartScale(CrossScale, false);
			Segment->SetEndScale(CrossScale, false);

			const FVector P0 = SplineToParent.TransformPosition(Spline->GetLocationAtDistanceAlongSpline(From, ESplineCoordinateSpace::Local));
			const FVector P1 = SplineToParent.TransformPosition(Spline->GetLocationAtDistanceAlongSpline(To, ESplineCoordinateSpace::Local));
			const FVector T0 = SplineToParent.TransformVector(Spline->GetTangentAtDistanceAlongSpline(From, ESplineCoordinateSpace::Local).GetSafeNormal() * (To - From));
			const FVector T1 = SplineToParent.TransformVector(Spline->GetTangentAtDistanceAlongSpline(To, ESplineCoordinateSpace::Local).GetSafeNormal() * (To - From));

			if (bReverseAxis)
			{
				Segment->SetStartAndEnd(P1, -T1, P0, -T0, true);
			}
			else
			{
				Segment->SetStartAndEnd(P0, T0, P1, T1, true);
			}
			Segment->RegisterComponent();
			Segments.Add(Segment);
		}

		if (bPartial)
		{
			break;
		}
	}
}


