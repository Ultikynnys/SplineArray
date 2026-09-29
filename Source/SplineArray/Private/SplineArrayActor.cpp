// Copyright (c) 2026. MIT License.

#include "SplineArrayActor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Async/ParallelFor.h"
#include "SplineArray.h"
#include "SplineArrayMeshTools.h"

namespace
{
	double MillisecondsSince(const FDateTime& Since)
	{
		return (FDateTime::UtcNow() - Since).GetTotalMilliseconds();
	}
}

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

	if (Spline && Spline->GetNumberOfSplinePoints() < 2)
	{
		Spline->ClearSplinePoints(false);
		Spline->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local, false);
		Spline->AddSplinePoint(FVector(500.0f, 0.0f, 0.0f), ESplineCoordinateSpace::Local, false);
		Spline->UpdateSpline();
	}

#if WITH_EDITORONLY_DATA
	CachedSplineHash = ComputeSplineHash();
#endif

	const bool bEditorWorld = GetWorld() && GetWorld()->WorldType == EWorldType::Editor;
#if WITH_EDITOR
	if (!bEditorWorld || Segments.Num() == 0 || bSplineDirty)
	{
		Rebuild();
		bSplineDirty = false;
	}
#else
	Rebuild();
#endif
}

void ASplineArrayActor::BeginPlay()
{
	Super::BeginPlay();
	Rebuild();

#if !UE_BUILD_SHIPPING
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Game && FParse::Param(FCommandLine::Get(), TEXT("SplineArrayCollisionProbe")))
	{
		int32 ConvexCount = 0;
		int32 FallbackCount = 0;
		const UBodySetup* SourceBody = SourceMesh ? SourceMesh->GetBodySetup() : nullptr;
		const bool bSourceHasConvex = SourceBody && SourceBody->AggGeom.ConvexElems.Num() > 0;
		int32 SimpleHitCount = 0;
		int32 PawnSweepCount = 0;
		int32 PawnBlockCount = 0;
		for (int32 Index = 0; Index < Segments.Num(); ++Index)
		{
			USplineMeshComponent* Segment = Segments[Index];
			UStaticMesh* Mesh = Segment ? Segment->GetStaticMesh() : nullptr;
			const UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
			const int32 Hulls = Body ? Body->AggGeom.ConvexElems.Num() : 0;
			const bool bBlocksPawn = Segment && Segment->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
			const bool bCollisionOn = Segment && Segment->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
			bool bSimpleHit = false;
			bool bPawnSweep = false;
			if (Segment)
			{
				const FVector P0 = Segment->GetStartPosition();
				const FVector P1 = Segment->GetEndPosition();
				const FVector Direction = (P1 - P0).GetSafeNormal2D();
				const FVector Normal(-Direction.Y, Direction.X, 0.0f);
				FCollisionQueryParams Params(SCENE_QUERY_STAT(SplineArrayCollisionProbe), false);
				for (const float Alpha : {0.2f, 0.5f, 0.8f})
				{
					for (const float Height : {25.0f, 45.0f, 65.0f, 85.0f, 110.0f, 140.0f})
					{
						const FVector Center = Segment->GetComponentTransform().TransformPosition(FMath::Lerp(P0, P1, Alpha) + FVector(0.0f, 0.0f, Height));
						const FVector Side = Segment->GetComponentTransform().TransformVectorNoScale(Normal) * 65.0f;
						FHitResult ComponentHit;
						bSimpleHit |= Segment->LineTraceComponent(ComponentHit, Center + Side, Center - Side, Params);
						FHitResult PawnHit;
						bPawnSweep |= GetWorld()->SweepSingleByChannel(PawnHit, Center + Side, Center - Side, FQuat::Identity, ECC_Pawn,
							FCollisionShape::MakeSphere(12.0f), Params) && PawnHit.GetComponent() == Segment;
						if (bSimpleHit && bPawnSweep)
						{
							break;
						}
					}
					if (bSimpleHit && bPawnSweep)
					{
						break;
					}
				}
			}
			ConvexCount += Hulls > 0;
			FallbackCount += Hulls == 0 && Body && Body->CollisionTraceFlag == CTF_UseComplexAsSimple;
			SimpleHitCount += bSimpleHit;
			PawnSweepCount += bPawnSweep;
			PawnBlockCount += bBlocksPawn && bCollisionOn;
			UE_LOG(LogSplineArray, Log, TEXT("CollisionProbe segment=%d mesh=%s convex=%d simpleHit=%d pawnBlock=%d pawnSweep=%d"),
				Index, Mesh ? *Mesh->GetName() : TEXT("None"), Hulls, bSimpleHit, bBlocksPawn && bCollisionOn, bPawnSweep);
		}
		const bool bShapeOk = bSourceHasConvex ? ConvexCount == Segments.Num() : (ConvexCount == 0 && FallbackCount == Segments.Num());
		const bool bBlocks = PawnBlockCount == Segments.Num() && PawnSweepCount == Segments.Num();
		UE_LOG(LogSplineArray, Warning, TEXT("CollisionProbe result=%s actor=%s sourceConvex=%d segments=%d convex=%d fallback=%d simpleHits=%d pawnBlocks=%d pawnSweeps=%d"),
			bShapeOk && bBlocks ? TEXT("PASS") : TEXT("FAIL"), *GetName(), bSourceHasConvex, Segments.Num(), ConvexCount, FallbackCount, SimpleHitCount, PawnBlockCount, PawnSweepCount);
	}
#endif
}

void ASplineArrayActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if WITH_EDITORONLY_DATA
	if (Spline)
	{
		const uint32 Hash = ComputeSplineHash();
		if (Hash != CachedSplineHash)
		{
			CachedSplineHash = Hash;
			bSplineDirty = true;
		}
	}
#endif
}

#if WITH_EDITOR
void ASplineArrayActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ASplineArrayActor, MeshAxisLength))
	{
		return;
	}
	bSplineDirty = true;
}

void ASplineArrayActor::ForceRebuild()
{
	bSplineDirty = false;
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
	const FDateTime RebuildStart = FDateTime::UtcNow();
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

	TArray<float> SplinePointDistances;
	TArray<TArray<TUniquePtr<FMeshDescription>>> SliceDescriptions;
	{
		const int32 NumSplinePoints = Spline->GetNumberOfSplinePoints();
		SplinePointDistances.Reserve(NumSplinePoints);
		for (int32 PointIndex = 0; PointIndex < NumSplinePoints; ++PointIndex)
		{
			SplinePointDistances.Add(Spline->GetDistanceAlongSplineAtSplinePoint(PointIndex));
		}
	}

	TArray<float> CutDistances;

	struct FPendingSlice
	{
		float From = 0.0f;
		float To = 0.0f;
		double Lower = 0.0;
		double Upper = 0.0;
		bool bWholeMesh = false;
	};

	TArray<FPendingSlice> Slices;
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

			FPendingSlice Slice;
			Slice.From = From;
			Slice.To = To;
			Slice.bWholeMesh = SplineArrayMeshTools::IsWholeMeshBisect(SourceMesh, AxisIndex, FMath::Min(NearMesh, FarMesh), FMath::Abs(FarMesh - NearMesh), Slice.Lower, Slice.Upper);
			Slices.Add(Slice);
		}

		if (bPartial)
		{
			break;
		}
	}

	TArray<FString> NewKeys;
	NewKeys.Reserve(Slices.Num());
	for (const FPendingSlice& Slice : Slices)
	{
		NewKeys.Add(Slice.bWholeMesh ? FString() : SplineArrayMeshTools::BisectCacheKey(SourceMesh, AxisIndex, Slice.Lower, Slice.Upper));
	}

	bool bUseBaked = BakedSegments.Num() == Slices.Num() && BakedSegmentKeys == NewKeys;
	for (int32 Index = 0; bUseBaked && Index < Slices.Num(); ++Index)
	{
		const UStaticMesh* Baked = BakedSegments.IsValidIndex(Index) ? BakedSegments[Index].Get() : nullptr;
		const bool bWhole = NewKeys[Index].IsEmpty();
		if (bWhole ? Baked != nullptr : Baked == nullptr || !Baked->bAllowCPUAccess)
		{
			bUseBaked = false;
		}
#if WITH_EDITOR
		if (!bWhole && bUseBaked && SourceMesh->GetBodySetup() && SourceMesh->GetBodySetup()->AggGeom.ConvexElems.Num() > 0
			&& (!Baked->GetBodySetup() || Baked->GetBodySetup()->AggGeom.ConvexElems.Num() == 0))
		{
			bUseBaked = false;
		}
#endif
	}

	SliceDescriptions.SetNum(Slices.Num());
	double SliceMs = 0.0;
	double BuildMs = 0.0;
	int32 WholeMeshCopies = 0;

	TArray<UStaticMesh*> SegmentMeshes;
	SegmentMeshes.SetNum(Slices.Num());

	if (bUseBaked)
	{
		for (int32 Index = 0; Index < Slices.Num(); ++Index)
		{
			SegmentMeshes[Index] = NewKeys[Index].IsEmpty() ? nullptr : BakedSegments[Index];
		}
	}

#if WITH_EDITOR
	if (!bUseBaked)
	{
		for (UStaticMesh* Old : BakedSegments)
		{
			if (Old && Old != SourceMesh && Old->GetOutermost() == GetOutermost())
			{
				Old->Rename(nullptr, GetTransientPackage(), REN_DoNotDirty | REN_DontCreateRedirectors);
				Old->MarkAsGarbage();
			}
		}
		BakedSegments.Reset();
		BakedSegmentKeys.Reset();
	}
#endif

	if (!bUseBaked)
	{
		const FPendingSlice* SlicePtr = Slices.GetData();
		const double SliceStart2 = FPlatformTime::Seconds();
		ParallelFor(Slices.Num(), [&](int32 Index)
		{
			if (SlicePtr[Index].bWholeMesh)
			{
				return;
			}
			SliceDescriptions[Index] = SplineArrayMeshTools::SliceMeshLODs(SourceMesh, AxisIndex, SlicePtr[Index].Lower, SlicePtr[Index].Upper);
		});
		SliceMs = (FPlatformTime::Seconds() - SliceStart2) * 1000.0;
	}

	for (int32 Index = 0; Index < Slices.Num(); ++Index)
	{
		if (!bUseBaked && SliceDescriptions[Index].Num() > 0)
		{
			const bool bPieWorld = GetWorld() && GetWorld()->WorldType == EWorldType::PIE;
			SegmentMeshes[Index] = SplineArrayMeshTools::CreateBisectTarget(SourceMesh, bPieWorld ? nullptr : GetOutermost());
		}
	}

	{
		const double BuildStart2 = FPlatformTime::Seconds();
		const int32 DesiredLODCount = SourceMesh->GetNumLODs();
		TArray<UStaticMesh*> PreparedMeshes;
		for (int32 Index = 0; Index < Slices.Num(); ++Index)
		{
			const TArray<TUniquePtr<FMeshDescription>>& CutLODs = SliceDescriptions[Index];
			if (CutLODs.Num() == 0 || !SegmentMeshes[Index])
			{
				continue;
			}
			if (SplineArrayMeshTools::PrepareBisectedMesh(SegmentMeshes[Index], SourceMesh, *CutLODs[0], DesiredLODCount, AxisIndex, Slices[Index].Lower, Slices[Index].Upper))
			{
				PreparedMeshes.Add(SegmentMeshes[Index]);
			}
			else
			{
				SegmentMeshes[Index] = nullptr;
			}
		}
		SplineArrayMeshTools::BuildBisectedMeshes(PreparedMeshes);
		for (UStaticMesh* Mesh : PreparedMeshes)
		{
			if (Mesh->GetNumLODs() == 0)
			{
				int32 FoundIndex = INDEX_NONE;
				SegmentMeshes.Find(Mesh, FoundIndex);
				if (FoundIndex != INDEX_NONE)
				{
					SegmentMeshes[FoundIndex] = nullptr;
				}
			}
		}
		BuildMs = (FPlatformTime::Seconds() - BuildStart2) * 1000.0;

		const bool bPieWorld = GetWorld() && GetWorld()->WorldType == EWorldType::PIE;
		if (!bPieWorld)
		{
			Modify();
			BakedSegments.Reset();
			BakedSegmentKeys.Reset();
			for (int32 Index = 0; Index < Slices.Num(); ++Index)
			{
				BakedSegmentKeys.Add(NewKeys[Index]);
				BakedSegments.Add(SegmentMeshes[Index]);
			}
			GetOutermost()->MarkPackageDirty();
		}
	}

	for (int32 Index = 0; Index < Slices.Num(); ++Index)
	{
		UStaticMesh* SegmentMesh = SegmentMeshes[Index];
		if (!SegmentMesh)
		{
			if (Slices[Index].bWholeMesh)
			{
				SegmentMesh = SourceMesh;
				++WholeMeshCopies;
			}
			else
			{
				continue;
			}
		}
		else
		{
			BisectCache.Add(SplineArrayMeshTools::BisectCacheKey(SourceMesh, AxisIndex, Slices[Index].Lower, Slices[Index].Upper), SegmentMesh);
		}

		const float From = Slices[Index].From;
		const float To = Slices[Index].To;

		USplineMeshComponent* Segment = NewObject<USplineMeshComponent>(this, NAME_None, RF_Transient);
		Segment->SetupAttachment(SceneRoot);
		Segment->SetMobility(EComponentMobility::Movable);
		Segment->SetStaticMesh(SegmentMesh);
		if (Material)
		{
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

	UE_LOG(LogSplineArray, Log, TEXT("Rebuild actor=%s segments=%d slices=%d wholeCopies=%d baked=%d slice=%.2fms build=%.2fms rebuildTotal=%.2fms"),
		*GetName(), Segments.Num(), Slices.Num(), WholeMeshCopies, bUseBaked ? 1 : 0, SliceMs, BuildMs, MillisecondsSince(RebuildStart));
}


